use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use gpui_kit::assets::IconName;
use gpui_kit::component::button::{Button, ButtonVariants};
use gpui_kit::component::dialog::DialogFooter;
use gpui_kit::component::input::{Input, InputEvent, InputState, Textarea, TextareaState};
use gpui_kit::component::spinner::Spinner;
use gpui_kit::component::{Disableable, Icon, Sizable, WindowExt, h_flex, v_flex};
use gpui_kit::prelude::FluentBuilder;
use gpui_kit::*;
use swofty_core::multiplayer::session as mp_session;
use swofty_core::send_logs::{self, Bundle, GameInfo, MESSAGE_MAX, Report, Stage};
use swofty_core::store::Store;

use crate::reveal;
use crate::shell::Shell;
use crate::theme::{Palette, palette};

pub const LABEL: &str = "Send Logs to Developers";
pub const SAVE_LABEL: &str = "Save Logs";

pub fn label() -> &'static str {
    if send_logs::available() { LABEL } else { SAVE_LABEL }
}

pub fn icon() -> IconName {
    if send_logs::available() {
        IconName::Send
    } else {
        IconName::Save
    }
}

const WHAT_IS_SENT: &str = "Your launcher and game logs. They can include folder paths with your computer's \
                            user name, your hardware, and your installed mods and profiles. Passwords and \
                            tokens in the launcher's settings are left out.";

const NO_SESSION: &str = "We don't see a game launch in your logs yet. Press Play and reproduce the problem \
                          first, then send logs.";

#[derive(Clone, PartialEq)]
enum Phase {
    Editing,
    Working,
    Sent { parts: usize, zip: Option<PathBuf> },
    Saved(PathBuf),
    Failed { text: String, zip: Option<PathBuf> },
}

impl Phase {
    fn done(&self) -> bool {
        matches!(self, Self::Sent { .. } | Self::Saved(_))
    }
}

pub struct SendLogs {
    store: Store,
    game: GameInfo,
    name: Entity<InputState>,
    message: Entity<TextareaState>,
    phase: Phase,
    tried: bool,
    session: bool,
    stage: Arc<Mutex<Stage>>,
    _work: Option<Task<()>>,
    _poll: Option<Task<()>>,
}

impl SendLogs {
    fn new(store: Store, game: GameInfo, window: &mut Window, cx: &mut Context<Self>) -> Self {
        let remembered = store.config().discord_username.unwrap_or_default();
        let name = cx.new(|cx| {
            InputState::new(window, cx)
                .placeholder("e.g. swofty")
                .default_value(remembered)
        });
        let message = cx.new(|cx| {
            TextareaState::new(window, cx)
                .placeholder("What happened? What were you doing when it went wrong?")
                .rows(4)
        });
        cx.subscribe(&name, |_, _, e: &InputEvent, cx| {
            if matches!(e, InputEvent::Change) {
                cx.notify();
            }
        })
        .detach();
        cx.subscribe(&message, |_, _, e: &InputEvent, cx| {
            if matches!(e, InputEvent::Change) {
                cx.notify();
            }
        })
        .detach();
        let session = send_logs::has_session(&store);
        Self {
            store,
            game,
            name,
            message,
            phase: Phase::Editing,
            tried: false,
            session,
            stage: Arc::new(Mutex::new(Stage::Collecting)),
            _work: None,
            _poll: None,
        }
    }

    fn working(&self) -> bool {
        self.phase == Phase::Working
    }

    fn checks(&self, cx: &App) -> (Result<String, String>, Result<String, String>) {
        (
            send_logs::check_name(&self.name.read(cx).value()),
            send_logs::check_message(&self.message.read(cx).value()),
        )
    }

    fn ready(&self, cx: &App) -> bool {
        matches!(self.checks(cx), (Ok(_), Ok(_)))
    }

    fn submit(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        if self.working() || self.phase.done() {
            return;
        }
        let (Ok(name), Ok(message)) = self.checks(cx) else {
            self.tried = true;
            cx.notify();
            return;
        };
        self.phase = Phase::Working;
        if let Ok(mut s) = self.stage.lock() {
            *s = Stage::Collecting;
        }
        let (store, stage) = (self.store.clone(), self.stage.clone());
        let report = Report {
            name: name.clone(),
            message,
            game: self.game.clone(),
            room: mp_session::active_state().map(|s| mp_session::snapshot(&s)),
        };
        let sending = send_logs::available();
        self._work = Some(cx.spawn_in(window, async move |this, cx| {
            let (zip, sent) = cx
                .background_executor()
                .spawn(async move {
                    let bundle = Bundle::collect(&store, &report);
                    let progress = |s: Stage| {
                        if let Ok(mut slot) = stage.lock() {
                            *slot = s;
                        }
                    };
                    progress(Stage::Packing);
                    let zip = bundle
                        .save(&send_logs::kept_dir(&store), send_logs::KEPT)
                        .map_err(|e| e.to_string());
                    let sent =
                        sending.then(|| send_logs::send(&bundle, &progress).map_err(|e| e.to_string()));
                    (zip, sent)
                })
                .await;
            this.update(cx, |f, cx| f.finish(name, zip, sent, cx)).ok();
        }));
        self._poll = Some(cx.spawn(async move |this, cx| {
            loop {
                cx.background_executor().timer(Duration::from_millis(150)).await;
                let Ok(working) = this.update(cx, |f, cx| {
                    cx.notify();
                    f.working()
                }) else {
                    break;
                };
                if !working {
                    break;
                }
            }
        }));
        cx.notify();
    }

    fn finish(
        &mut self,
        name: String,
        zip: Result<PathBuf, String>,
        sent: Option<Result<usize, String>>,
        cx: &mut Context<Self>,
    ) {
        if let Err(e) = &zip {
            eprintln!("swofty-launcher: could not keep the logs zip: {e}");
        }
        self.phase = match (sent, zip) {
            (Some(Ok(parts)), zip) => Phase::Sent { parts, zip: zip.ok() },
            (Some(Err(e)), zip) => Phase::Failed {
                text: format!("Not sent. {e}"),
                zip: zip.ok(),
            },
            (None, Ok(zip)) => Phase::Saved(zip),
            (None, Err(e)) => Phase::Failed {
                text: format!("Not saved. {e}"),
                zip: None,
            },
        };
        if self.phase.done() {
            let mut config = self.store.config();
            if config.discord_username.as_deref() != Some(name.as_str()) {
                config.discord_username = Some(name);
                if let Err(e) = self.store.save_config(&config) {
                    eprintln!("swofty-launcher: could not remember the Discord username: {e}");
                }
            }
        }
        cx.notify();
    }

    fn status(&self, p: Palette) -> Option<Div> {
        let row = || h_flex().gap(px(8.)).items_start().text_size(px(13.));
        let text = |t: String| div().flex_1().min_w_0().child(t);
        let with_zip = |line: Div, zip: Option<&PathBuf>, also: bool| {
            v_flex()
                .gap(px(6.))
                .child(line)
                .children(zip.map(|z| manual(z, also, p)))
        };
        match &self.phase {
            Phase::Editing => None,
            Phase::Working => {
                let stage = self.stage.lock().map(|s| s.to_string()).unwrap_or_default();
                Some(
                    row()
                        .text_color(p.text_muted)
                        .child(Spinner::new().with_size(px(15.)))
                        .child(text(stage)),
                )
            }
            Phase::Sent { parts, zip } => Some(with_zip(
                row()
                    .text_color(p.success)
                    .child(Icon::new(IconName::CircleCheck).size(px(15.)))
                    .child(text(match parts {
                        1 => "Sent — thanks!".into(),
                        n => format!("Sent in {n} parts — thanks!"),
                    })),
                zip.as_ref(),
                true,
            )),
            Phase::Saved(zip) => Some(with_zip(
                row()
                    .text_color(p.success)
                    .child(Icon::new(IconName::CircleCheck).size(px(15.)))
                    .child(text("Saved.".into())),
                Some(zip),
                false,
            )),
            Phase::Failed { text: t, zip } => Some(with_zip(
                row()
                    .text_color(p.error)
                    .child(Icon::new(IconName::CircleAlert).size(px(15.)))
                    .child(text(t.clone())),
                zip.as_ref(),
                true,
            )),
        }
    }

    fn footer(&self, form: Entity<Self>, cx: &App) -> DialogFooter {
        let close = |label: &'static str| {
            Button::new("send-logs-cancel")
                .label(label)
                .on_click(|_, window, cx| window.close_dialog(cx))
        };
        match &self.phase {
            phase if phase.done() => DialogFooter::new().child(
                Button::new("send-logs-done")
                    .primary()
                    .label("Done")
                    .on_click(|_, window, cx| window.close_dialog(cx)),
            ),
            phase => {
                let working = *phase == Phase::Working;
                let sending = send_logs::available();
                DialogFooter::new()
                    .child(close(if working { "Close" } else { "Cancel" }))
                    .child(
                        Button::new("send-logs-send")
                            .primary()
                            .icon(icon())
                            .label(match (phase, sending) {
                                (Phase::Working, true) => "Sending…",
                                (Phase::Working, false) => "Saving…",
                                (Phase::Failed { .. }, _) => "Try again",
                                (_, true) => "Send",
                                (_, false) => "Save",
                            })
                            .loading(working)
                            .disabled(working || !self.ready(cx))
                            .on_click(move |_, window, cx| {
                                form.update(cx, |f, cx| f.submit(window, cx));
                            }),
                    )
            }
        }
    }
}

fn manual(zip: &Path, also: bool, p: Palette) -> Div {
    let zip = zip.to_path_buf();
    h_flex()
        .flex_wrap()
        .gap_x(px(4.))
        .pl(px(23.))
        .text_size(px(13.))
        .child(div().text_color(p.text_muted).child(if also {
            "You can also send your logs manually for debugging."
        } else {
            "You can send your logs manually for debugging."
        }))
        .child(
            div()
                .id("send-logs-reveal")
                .cursor_pointer()
                .text_color(p.accent_hover)
                .hover(|s| s.underline())
                .child(format!("Click here to open them in {}.", reveal::FILE_MANAGER))
                .on_click(move |_, _, cx| reveal::show(&zip, cx)),
        )
}

fn field(title: &'static str, hint: Option<SharedString>, p: Palette) -> Div {
    h_flex()
        .justify_between()
        .text_size(px(13.))
        .child(div().font_weight(FontWeight::MEDIUM).child(title))
        .children(hint.map(|h| div().text_size(px(12.)).text_color(p.text_placeholder).child(h)))
}

fn no_session(p: Palette) -> Div {
    h_flex()
        .gap(px(10.))
        .items_start()
        .p(px(12.))
        .rounded(px(6.))
        .bg(p.warning.opacity(0.08))
        .border_1()
        .border_color(p.warning.opacity(0.4))
        .child(
            Icon::new(IconName::TriangleAlert)
                .size(px(15.))
                .text_color(p.warning),
        )
        .child(div().flex_1().min_w_0().text_size(px(13.)).child(NO_SESSION))
}

impl Render for SendLogs {
    fn render(&mut self, _: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        let p = palette(cx);
        let locked = self.working() || self.phase.done();
        let count = self.message.read(cx).value().chars().count();
        let (name, message) = self.checks(cx);
        let hint = |check: Result<String, String>| {
            check.err().filter(|_| !locked).map(|e| {
                div()
                    .text_size(px(12.))
                    .text_color(if self.tried { p.error } else { p.text_muted })
                    .child(e)
            })
        };
        v_flex()
            .gap(px(14.))
            .when(!self.session && !self.phase.done(), |d| d.child(no_session(p)))
            .child(
                v_flex()
                    .gap(px(6.))
                    .child(field(
                        "Your Discord username",
                        Some("So the developers can reply".into()),
                        p,
                    ))
                    .child(Input::new(&self.name).disabled(locked))
                    .children(hint(name)),
            )
            .child(
                v_flex()
                    .gap(px(6.))
                    .child(field(
                        "What went wrong?",
                        Some(format!("{count}/{MESSAGE_MAX}").into()),
                        p,
                    ))
                    .child(Textarea::new(&self.message).h(px(96.)).disabled(locked))
                    .children(hint(message)),
            )
            .child(
                h_flex()
                    .gap(px(10.))
                    .items_start()
                    .p(px(12.))
                    .rounded(px(6.))
                    .bg(p.elevated)
                    .border_1()
                    .border_color(p.border)
                    .child(Icon::new(IconName::Info).size(px(15.)).text_color(p.accent))
                    .child(
                        v_flex()
                            .flex_1()
                            .min_w_0()
                            .gap(px(2.))
                            .child(
                                div()
                                    .text_size(px(13.))
                                    .font_weight(FontWeight::SEMIBOLD)
                                    .child("What gets sent"),
                            )
                            .child(
                                div()
                                    .text_size(px(13.))
                                    .text_color(p.text_muted)
                                    .child(WHAT_IS_SENT),
                            ),
                    ),
            )
            .children(self.status(p))
    }
}

impl Shell {
    pub(crate) fn send_logs(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        let game = GameInfo {
            dir: self.install.as_ref().map(|i| i.dir.clone()),
            build_id: self.install.as_ref().and_then(|i| i.build_id),
            version: self.install.as_ref().and_then(|i| i.exe_version.clone()),
        };
        let store = self.store.clone();
        let form = cx.new(|cx| SendLogs::new(store, game, window, cx));
        let focus = std::cell::Cell::new(true);
        window.open_alert_dialog(cx, move |alert, window, cx| {
            if focus.replace(false) {
                let (named, name, message) = {
                    let f = form.read(cx);
                    (f.checks(cx).0.is_ok(), f.name.clone(), f.message.clone())
                };
                if named {
                    message.update(cx, |s, cx| s.focus(window, cx));
                } else {
                    name.update(cx, |s, cx| s.focus(window, cx));
                }
            }
            let submit = form.clone();
            let footer = form.read(cx).footer(form.clone(), cx);
            alert
                .width(px(560.))
                .title(label())
                .description(if send_logs::available() {
                    "Sends your logs to the developers on Discord, so they can see what went wrong."
                } else {
                    "Saves your logs to a zip you can send yourself. Sending them to the developers isn't available in this build."
                })
                .child(form.clone())
                .footer(footer)
                .on_ok(move |_, window, cx| {
                    submit.update(cx, |f, cx| f.submit(window, cx));
                    false
                })
        });
    }

    pub(crate) fn send_logs_button(&self, p: Palette, cx: &mut Context<Self>) -> impl IntoElement {
        h_flex()
            .id("send-logs")
            .min_h(px(36.))
            .px(px(10.))
            .py(px(6.))
            .gap(px(9.))
            .rounded(px(8.))
            .border_1()
            .border_color(p.border_variant)
            .bg(p.surface)
            .text_size(px(13.))
            .font_weight(FontWeight::MEDIUM)
            .cursor_pointer()
            .text_color(p.text_muted)
            .hover(move |s| s.bg(p.hover).border_color(p.border).text_color(p.text))
            .on_click(cx.listener(|this, _, window, cx| this.send_logs(window, cx)))
            .child(Icon::new(icon()).size(px(15.)).flex_shrink_0())
            .child(div().flex_1().min_w_0().child(label()))
    }
}
