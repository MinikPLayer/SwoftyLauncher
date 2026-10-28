//! The one-time setup the first Play opens: a welcome, the question about mods
//! Swofty didn't add, then verifying the game files so the originals Swofty
//! records are the store's own. See `swofty_core::setup`.

use std::time::Duration;

use gpui_kit::assets::IconName;
use gpui_kit::component::button::{Button, ButtonVariants};
use gpui_kit::component::dialog::DialogFooter;
use gpui_kit::component::notification::Notification;
use gpui_kit::component::spinner::Spinner;
use gpui_kit::component::{Disableable, Icon, Sizable, WindowExt, h_flex, v_flex};
use gpui_kit::prelude::FluentBuilder;
use gpui_kit::*;
use swofty_core::install::{Game, Source};
use swofty_core::setup;
use swofty_core::stock;
use swofty_core::store::Store;

use crate::game::Install;
use crate::game_edited::discord_link;
use crate::pages::{game_art, label};
use crate::shell::Shell;
use crate::theme::{Palette, palette};

pub(crate) const RISK: &str = "Because of how Swofty Launcher hooks the game's engine, which lets addons run \
                               their own engine code, it is highly risky to change the game with any outside \
                               tool that doesn't explicitly support Swofty Addons.";

#[derive(Debug, Clone, Copy, PartialEq)]
enum Step {
    Welcome,
    Mods,
    Verify,
}

pub(crate) struct SetupWizard {
    store: Store,
    install: Install,
    shell: WeakEntity<Shell>,
    step: Step,
    other_mods: Option<bool>,
    working: bool,
    error: Option<String>,
    opened_verify: bool,
    unrestored: Option<String>,
    /// Steam's file list can check this copy at launch, once known.
    checks: Option<bool>,
    steam_busy: Option<&'static str>,
    _work: Option<Task<()>>,
    _poll: Option<Task<()>>,
}

impl SetupWizard {
    fn new(store: Store, install: Install, shell: WeakEntity<Shell>) -> Self {
        let started = setup::read(&store.root).filter(|s| s.version >= setup::VERSION);
        Self {
            step: if started.is_some() {
                Step::Verify
            } else {
                Step::Welcome
            },
            other_mods: started.map(|s| s.other_mods),
            store,
            install,
            shell,
            working: false,
            error: None,
            opened_verify: false,
            unrestored: None,
            checks: None,
            steam_busy: None,
            _work: None,
            _poll: None,
        }
    }

    fn probe(&mut self, cx: &mut Context<Self>) {
        let dir = self.install.dir.clone();
        cx.spawn(async move |this, cx| {
            let checks = cx
                .background_executor()
                .spawn(async move { setup::store_checks(&dir) })
                .await;
            this.update(cx, |w, cx| {
                w.checks = Some(checks);
                cx.notify();
            })
            .ok();
        })
        .detach();
    }

    /// No other mods on a copy the launch checks against Steam's list:
    /// nothing is left to verify.
    fn skips_verify(&self) -> bool {
        self.other_mods == Some(false) && self.checks == Some(true) && self.unrestored.is_none()
    }

    fn reset(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        let Some(other_mods) = self.other_mods.filter(|_| !self.working) else {
            return;
        };
        self.working = true;
        self.error = None;
        let (store, dir) = (self.store.clone(), self.install.dir.clone());
        self._work = Some(cx.spawn_in(window, async move |this, cx| {
            let result = cx
                .background_executor()
                .spawn(async move {
                    let g = Game::locate(&store.root, dir.clone()).map_err(|e| e.to_string())?;
                    let unrestored = setup::reset(&store.root, &g, other_mods)
                        .map(|r| r.unrestored)
                        .map_err(|e| e.to_string())?;
                    Ok::<_, String>((unrestored, setup::store_checks(&dir)))
                })
                .await;
            this.update_in(cx, |w, window, cx| {
                w.working = false;
                match result {
                    Ok((unrestored, checks)) => {
                        w.unrestored = unrestored;
                        w.checks = Some(checks);
                        if w.skips_verify() {
                            w.finish(window, cx);
                            return;
                        }
                        w.step = Step::Verify;
                    }
                    Err(e) => {
                        crate::app_log::action("first play setup: put the game folder back").failed(&e);
                        w.error = Some(format!("Swofty couldn't put the game folder back: {e}"))
                    }
                }
                cx.notify();
            })
            .ok();
        }));
        cx.notify();
    }

    /// After Verify game files in Steam, keep reading what Steam is doing to
    /// the game's files, so Continue waits for the verify to finish.
    fn watch_steam(&mut self, cx: &mut Context<Self>) {
        if self._poll.is_some() {
            return;
        }
        let dir = self.install.dir.clone();
        self._poll = Some(cx.spawn(async move |this, cx| {
            loop {
                cx.background_executor().timer(Duration::from_secs(1)).await;
                let d = dir.clone();
                let busy = cx
                    .background_executor()
                    .spawn(async move { steam_busy(&d) })
                    .await;
                let Ok(()) = this.update(cx, |w, cx| {
                    if w.steam_busy != busy {
                        w.steam_busy = busy;
                        w.error = None;
                        cx.notify();
                    }
                }) else {
                    break;
                };
            }
        }));
    }

    fn finish(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        if let Some(state) = steam_busy(&self.install.dir) {
            self.steam_busy = Some(state);
            self.error = Some(format!(
                "Steam is still {state} F1 25. Wait for it to finish, then continue."
            ));
            cx.notify();
            return;
        }
        if let Err(e) = setup::finish(&self.store.root) {
            crate::app_log::action("first play setup: finish").failed(&e);
            self.error = Some(e.to_string());
            cx.notify();
            return;
        }
        window.close_dialog(cx);
        self.shell.update(cx, |s, cx| s.play(window, cx)).ok();
    }

    fn content(&self, p: Palette, cx: &mut Context<Self>) -> Div {
        match self.step {
            Step::Welcome => welcome(p),
            Step::Mods => self.mods(p, cx),
            Step::Verify => self.verify(p),
        }
    }

    fn answer(&mut self, other_mods: bool, cx: &mut Context<Self>) {
        if !self.working {
            self.other_mods = Some(other_mods);
            self.error = None;
            cx.notify();
        }
    }

    fn mods(&self, p: Palette, cx: &mut Context<Self>) -> Div {
        let replaced = match self.install.source {
            Source::Unknown => {
                "Take them out of the game folder by hand: this copy can't be verified. Keep their \
                                downloads to add them back as addons."
                    .to_owned()
            }
            source => format!(
                "{} replaces them with the game's own files. Keep their downloads to add them back as addons.",
                crate::game_edited::repair(source).restores
            ),
        };
        v_flex()
            .gap(px(12.))
            .child(illustration(IconName::FolderSearch, p))
            .child(heading(
                "Are there other mods in your game folder?",
                "Mods copied into the F1 25 folder by hand, or put there by another mod manager or tool, \
                 that Swofty Launcher didn't add.",
                p,
            ))
            .child(
                h_flex()
                    .gap(px(10.))
                    .items_stretch()
                    .child(choice(
                        "setup-mods-yes",
                        "Yes, there are other mods",
                        replaced,
                        self.other_mods == Some(true),
                        p,
                    )
                    .on_click(cx.listener(|w, _, _, cx| w.answer(true, cx))))
                    .child(choice(
                        "setup-mods-no",
                        "No, only Swofty's",
                        match (self.install.source, self.checks) {
                            (Source::Steam, Some(true)) => {
                                "Swofty checks every game file against Steam's own list when you launch."
                            }
                            (Source::Steam, Some(false)) => {
                                "Swofty can't read Steam's file list for this copy, so you'll verify the game \
                                 files once."
                            }
                            (Source::EaApp, _) => {
                                "Swofty can't check the EA app's game files itself, so you'll repair the game \
                                 once."
                            }
                            _ => "Swofty checks the game files once, so it starts from a clean copy of F1 25.",
                        },
                        self.other_mods == Some(false),
                        p,
                    )
                    .on_click(cx.listener(|w, _, _, cx| w.answer(false, cx)))),
            )
            .child(callout(IconName::TriangleAlert, RISK, p))
            .children(self.status(p))
    }

    fn verify(&self, p: Palette) -> Div {
        let repair = crate::game_edited::repair(self.install.source);
        let intro = match (self.install.source, self.other_mods) {
            (Source::Unknown, _) => {
                "Swofty put back the files it changed and records the game's original files \
                                     again from what is in the game folder next."
                    .to_owned()
            }
            (_, Some(true)) => format!(
                "Swofty put back the files it changed and records the game's original files again. {} now \
                 replaces the other mods with the game's own files.",
                repair.restores
            ),
            _ if self.skips_verify() => "Swofty put back the files it changed. The launch checks every game file \
                                         against Steam's own list, so there's nothing to verify."
                .to_owned(),
            (Source::EaApp, _) => "Swofty put back the files it changed. It can't check the EA app's game \
                                   files itself, so repairing the game is the only way to be sure they're the \
                                   game's own."
                .to_owned(),
            (Source::Steam, _) if self.checks == Some(false) => {
                "Swofty put back the files it changed. It can't read Steam's file list for this copy, so \
                 verifying is the only way to be sure the game files are Steam's own."
                    .to_owned()
            }
            _ => format!(
                "Swofty put back the files it changed and records the game's original files again. {} makes \
                 sure every game file is the store's own copy.",
                repair.restores
            ),
        };
        let first = match self.install.source {
            Source::Steam => (
                "Verify game files in Steam",
                "Select Verify game files in Steam below. It's also in Steam > right-click F1 25 > \
                 Properties > Installed Files.",
            ),
            Source::EaApp => (repair.title, "EA app > Library > F1 25 > Manage > Repair."),
            Source::Unknown => (
                "Take out other mods",
                "This copy isn't from Steam or the EA app, so it can't be verified. Remove any mod files \
                 Swofty didn't add.",
            ),
        };
        v_flex()
            .gap(px(12.))
            .child(illustration(IconName::ShieldCheck, p))
            .child(heading(repair.title, intro, p))
            .child(
                v_flex()
                    .gap(px(9.))
                    .child(label("WHAT TO DO", p))
                    .child(step(1, first.0, first.1, p))
                    .when(self.install.source != Source::Unknown, |d| {
                        d.child(step(
                            2,
                            "Wait for it to finish",
                            "It checks every game file and downloads only the ones that changed.",
                            p,
                        ))
                    })
                    .child(step(
                        if self.install.source == Source::Unknown {
                            2
                        } else {
                            3
                        },
                        "Come back and continue",
                        "Swofty then builds your addons and starts F1 25.",
                        p,
                    )),
            )
            .when_some(self.unrestored.as_ref(), |d, why| {
                d.child(callout(
                    IconName::Info,
                    format!(
                        "Swofty couldn't put every file back from its old record ({why}). {} puts them back.",
                        repair.restores
                    ),
                    p,
                ))
            })
            .children(self.status(p))
    }

    fn status(&self, p: Palette) -> Option<Div> {
        let row = || h_flex().gap(px(8.)).items_start().text_size(px(13.));
        if self.working {
            return Some(
                row()
                    .text_color(p.text_muted)
                    .child(Spinner::new().with_size(px(15.)))
                    .child(
                        div()
                            .flex_1()
                            .min_w_0()
                            .child("Putting back the files Swofty changed…"),
                    ),
            );
        }
        if let (Some(state), None) = (self.steam_busy, &self.error) {
            return Some(
                row()
                    .text_color(p.text_muted)
                    .child(Spinner::new().with_size(px(15.)))
                    .child(div().flex_1().min_w_0().child(format!("Steam is {state} F1 25…"))),
            );
        }
        self.error.as_ref().map(|e| {
            row()
                .text_color(p.error)
                .child(Icon::new(IconName::CircleAlert).size(px(15.)))
                .child(div().flex_1().min_w_0().child(e.clone()))
        })
    }

    fn footer(&self, wizard: Entity<Self>, p: Palette) -> DialogFooter {
        let not_now = Button::new("setup-not-now")
            .ghost()
            .h(px(36.))
            .px(px(14.))
            .label("Not now")
            .disabled(self.working)
            .on_click(|_, window, cx| window.close_dialog(cx));
        let buttons = h_flex().gap(px(8.)).child(not_now);
        let buttons = match self.step {
            Step::Welcome => buttons.child(
                Button::new("setup-start")
                    .primary()
                    .h(px(36.))
                    .px(px(14.))
                    .icon(IconName::ArrowRight)
                    .label("Get started")
                    .on_click(move |_, _, cx| {
                        wizard.update(cx, |w, cx| {
                            w.step = Step::Mods;
                            cx.notify();
                        });
                    }),
            ),
            Step::Mods => {
                let back = wizard.clone();
                buttons
                    .child(
                        Button::new("setup-back")
                            .outline()
                            .h(px(36.))
                            .px(px(14.))
                            .icon(IconName::ArrowLeft)
                            .label("Back")
                            .disabled(self.working)
                            .on_click(move |_, _, cx| {
                                back.update(cx, |w, cx| {
                                    w.step = Step::Welcome;
                                    cx.notify();
                                });
                            }),
                    )
                    .child(
                        Button::new("setup-understand")
                            .primary()
                            .h(px(36.))
                            .px(px(14.))
                            .icon(IconName::Check)
                            .label("I understand")
                            .loading(self.working)
                            .disabled(self.working || self.other_mods.is_none())
                            .on_click(move |_, window, cx| {
                                wizard.update(cx, |w, cx| w.reset(window, cx));
                            }),
                    )
            }
            Step::Verify => {
                let steam = self.install.source == Source::Steam;
                let opened = wizard.clone();
                buttons
                    .when(steam, |d| {
                        d.child(
                            Button::new("setup-verify")
                                .map(|b| {
                                    if self.opened_verify {
                                        b.outline()
                                    } else {
                                        b.primary()
                                    }
                                })
                                .h(px(36.))
                                .px(px(14.))
                                .icon(IconName::ShieldCheck)
                                .label("Verify game files in Steam")
                                .on_click(move |_, _, cx| {
                                    cx.open_url(stock::STEAM_VERIFY_URL);
                                    opened.update(cx, |w, cx| {
                                        w.opened_verify = true;
                                        w.watch_steam(cx);
                                        cx.notify();
                                    });
                                }),
                        )
                    })
                    .child(
                        Button::new("setup-continue")
                            .map(|b| {
                                if steam && !self.opened_verify {
                                    b.outline()
                                } else {
                                    b.primary()
                                }
                            })
                            .h(px(36.))
                            .px(px(14.))
                            .icon(IconName::Play)
                            .label(match self.install.source {
                                Source::Steam => "I've verified, continue",
                                Source::EaApp => "I've repaired, continue",
                                Source::Unknown => "Continue",
                            })
                            .disabled(
                                steam
                                    && !self.skips_verify()
                                    && (!self.opened_verify || self.steam_busy.is_some()),
                            )
                            .on_click(move |_, window, cx| {
                                wizard.update(cx, |w, cx| w.finish(window, cx));
                            }),
                    )
            }
        };
        DialogFooter::new()
            .w_full()
            .justify_between()
            .child(discord_link("setup-discord", p))
            .child(buttons)
    }
}

fn steam_busy(dir: &std::path::Path) -> Option<&'static str> {
    swofty_core::steam::find_game()
        .filter(|i| swofty_core::copies::same_dir(&i.dir, dir))
        .and_then(|i| swofty_core::steam::busy(&i))
}

impl Render for SetupWizard {
    fn render(&mut self, _: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        let p = palette(cx);
        self.content(p, cx)
    }
}

fn welcome(p: Palette) -> Div {
    v_flex()
        .gap(px(12.))
        .child(illustration(IconName::Gamepad2, p))
        .child(heading(
            "Welcome to Swofty Launcher",
            "Thanks for installing Swofty Launcher! Just a few things so we can get you sorted before your \
             first modded session.",
            p,
        ))
        .child(
            v_flex()
                .gap(px(9.))
                .child(label("ONE-TIME SETUP", p))
                .child(step(
                    1,
                    "Tell us about your game folder",
                    "Whether it already holds mods Swofty didn't add.",
                    p,
                ))
                .child(step(
                    2,
                    "Check your game files",
                    "Steam's file list checks them, or you verify or repair them once, so Swofty starts \
                     from a clean copy of F1 25.",
                    p,
                ))
                .child(step(
                    3,
                    "Play",
                    "Swofty builds your addons and starts the game.",
                    p,
                )),
        )
}

fn illustration(icon: IconName, p: Palette) -> impl IntoElement {
    let badge = div()
        .absolute()
        .bottom(px(-5.))
        .right(px(-5.))
        .size(px(26.))
        .flex()
        .items_center()
        .justify_center()
        .rounded_full()
        .bg(p.accent)
        .border_2()
        .border_color(p.surface)
        .child(Icon::new(icon).size(px(12.)).text_color(p.chrome));
    h_flex()
        .w_full()
        .justify_center()
        .py(px(7.))
        .rounded(px(10.))
        .bg(p.surface)
        .border_1()
        .border_color(p.border_variant)
        .child(div().relative().child(game_art(48., 48.)).child(badge))
}

fn heading(title: &'static str, body: impl Into<SharedString>, p: Palette) -> impl IntoElement {
    v_flex()
        .items_center()
        .gap(px(6.))
        .px(px(8.))
        .child(
            div()
                .text_center()
                .text_size(px(18.))
                .line_height(px(23.))
                .font_weight(FontWeight::BOLD)
                .text_color(p.text)
                .child(title),
        )
        .child(
            div()
                .text_center()
                .text_size(px(13.))
                .line_height(px(19.))
                .text_color(p.text_muted)
                .child(body.into()),
        )
}

fn step(n: u32, title: &'static str, body: &'static str, p: Palette) -> impl IntoElement {
    h_flex()
        .gap(px(12.))
        .items_start()
        .child(
            div()
                .size(px(20.))
                .flex_shrink_0()
                .flex()
                .items_center()
                .justify_center()
                .rounded_full()
                .bg(p.selected)
                .text_size(px(12.))
                .font_weight(FontWeight::SEMIBOLD)
                .text_color(p.accent)
                .child(n.to_string()),
        )
        .child(
            v_flex()
                .flex_1()
                .min_w_0()
                .gap(px(1.))
                .child(
                    div()
                        .text_size(px(13.))
                        .font_weight(FontWeight::MEDIUM)
                        .text_color(p.text)
                        .child(title),
                )
                .child(
                    div()
                        .text_size(px(12.))
                        .line_height(px(18.))
                        .text_color(p.text_muted)
                        .child(body),
                ),
        )
}

fn callout(icon: IconName, text: impl Into<SharedString>, p: Palette) -> impl IntoElement {
    h_flex()
        .gap(px(10.))
        .items_start()
        .p(px(12.))
        .rounded(px(6.))
        .bg(p.warning.opacity(0.08))
        .border_1()
        .border_color(p.warning.opacity(0.4))
        .child(
            Icon::new(icon)
                .size(px(15.))
                .flex_shrink_0()
                .text_color(p.warning),
        )
        .child(
            div()
                .flex_1()
                .min_w_0()
                .text_size(px(13.))
                .line_height(px(19.))
                .text_color(p.text)
                .child(text.into()),
        )
}

fn choice(
    id: &'static str,
    title: &'static str,
    body: impl Into<SharedString>,
    chosen: bool,
    p: Palette,
) -> Stateful<Div> {
    v_flex()
        .id(id)
        .flex_1()
        .min_w_0()
        .gap(px(4.))
        .p(px(12.))
        .rounded(px(8.))
        .cursor_pointer()
        .border_1()
        .map(|d| {
            if chosen {
                d.border_color(p.accent).bg(p.selected)
            } else {
                d.border_color(p.border).bg(p.elevated).hover(|s| s.bg(p.hover))
            }
        })
        .child(
            h_flex()
                .gap(px(8.))
                .items_center()
                .child(
                    Icon::new(if chosen {
                        IconName::CircleCheck
                    } else {
                        IconName::Circle
                    })
                    .size(px(15.))
                    .text_color(if chosen { p.accent } else { p.text_muted }),
                )
                .child(
                    div()
                        .text_size(px(13.))
                        .font_weight(FontWeight::SEMIBOLD)
                        .text_color(p.text)
                        .child(title),
                ),
        )
        .child(
            div()
                .pl(px(23.))
                .text_size(px(12.))
                .line_height(px(18.))
                .text_color(p.text_muted)
                .child(body.into()),
        )
}

pub(crate) const FIRST_LAUNCH: &str = "First launch can take 5 to 8 minutes. Don't press Play in the EA app or \
                                       Steam yourself during this time.";

impl Shell {
    /// Shown by the first Play after the setup, which builds every addon again.
    /// Launching records that the player saw it.
    pub(crate) fn first_launch_notice(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        let shell = cx.entity().downgrade();
        let store = self.store.clone();
        window.open_alert_dialog(cx, move |alert, _, _| {
            let (shell, store) = (shell.clone(), store.clone());
            alert
                .width(px(480.))
                .title("Notice")
                .description(FIRST_LAUNCH)
                .ok_text("Launch")
                .show_cancel(true)
                .on_ok(move |_, window, cx| {
                    if let Err(e) = setup::record_notice(&store.root) {
                        crate::app_log::action("first play setup: notice").failed(&e);
                        window.push_notification(Notification::error(e.to_string()), cx);
                        return false;
                    }
                    shell.update(cx, |s, cx| s.play(window, cx)).ok();
                    true
                })
        });
    }

    /// The one-time setup, opened by Play until it is done. Finishing it
    /// presses Play again.
    pub(crate) fn open_setup(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        let Some(install) = self.install.clone() else {
            return;
        };
        let shell = cx.entity().downgrade();
        let wizard = cx.new(|cx| {
            let mut w = SetupWizard::new(self.store.clone(), install, shell);
            w.probe(cx);
            w
        });
        window.open_alert_dialog(cx, move |alert, window, cx| {
            let p = palette(cx);
            let vh = f32::from(window.viewport_size().height);
            let top = ((vh - 640.) / 2.).max(12.);
            let footer = wizard.read(cx).footer(wizard.clone(), p);
            alert
                .width(px(680.))
                .margin_top(px(top))
                .child(wizard.clone())
                .footer(footer)
        });
    }
}
