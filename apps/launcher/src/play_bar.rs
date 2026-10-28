use gpui_kit::assets::IconName;
use gpui_kit::component::button::{Button, ButtonVariants};
use gpui_kit::component::menu::{DropdownMenu, PopupMenuItem};
use gpui_kit::component::spinner::Spinner;
use gpui_kit::component::tooltip::Tooltip;
use gpui_kit::component::{Disableable, Icon, Side, Sizable, h_flex, v_flex};
use gpui_kit::prelude::FluentBuilder;
use gpui_kit::*;

use crate::game::{self, Owner, ago, duration};

pub(crate) const HEIGHT: f32 = 72.;
use crate::library::plural;
use crate::pages::avatar;
use crate::shell::{Page, PlayState, Shell};
use crate::theme::Palette;
use swofty_core::install::Source;
use swofty_core::session::LaunchMode;
use swofty_core::updater::Gate;

pub(crate) fn store_badge(source: Source, p: Palette) -> Div {
    let (text, color) = match source {
        Source::Steam => ("STEAM", p.accent),
        Source::EaApp => ("EA APP", p.warning),
        Source::Unknown => ("FOLDER", p.text_muted),
    };
    div()
        .flex_shrink_0()
        .px(px(5.))
        .rounded(px(4.))
        .bg(color.opacity(0.14))
        .text_size(px(10.))
        .line_height(px(16.))
        .font_weight(FontWeight::BOLD)
        .text_color(color)
        .child(text)
}

pub(crate) fn link(id: &'static str, label: &'static str, p: Palette) -> Stateful<Div> {
    div()
        .id(id)
        .cursor_pointer()
        .text_size(px(13.))
        .text_color(p.text_muted)
        .underline()
        .hover(move |s| s.text_color(p.text))
        .child(label)
}

fn capitalised(s: &str) -> String {
    let mut c = s.chars();
    c.next()
        .map(|f| f.to_uppercase().chain(c).collect())
        .unwrap_or_default()
}

fn status_row() -> Div {
    h_flex()
        .flex_1()
        .min_w_0()
        .overflow_hidden()
        .gap(px(9.))
        .text_size(px(13.))
}

fn launch_failed(row: Div, text: impl std::fmt::Display, p: Palette) -> Div {
    let full = text.to_string();
    let text = SharedString::from(format!("Launch failed: {}", game::cause_first(&full)));
    let tip = SharedString::from(format!("Launch failed: {full}"));
    row.text_color(p.error)
        .child(Icon::new(IconName::CircleAlert).size(px(16.)))
        .child(
            div()
                .id("launch-failed")
                .min_w_0()
                .truncate()
                .child(text)
                .tooltip(move |window, cx| {
                    let tip = tip.clone();
                    Tooltip::element(move |_, _| div().max_w(px(480.)).child(tip.clone())).build(window, cx)
                }),
        )
}

impl Shell {
    fn game_info(&self, p: Palette, wide: bool, cx: &mut Context<Self>) -> AnyElement {
        let width = if wide { 320. } else { 240. };
        let text = |title: String, line: AnyElement| {
            v_flex()
                .flex_1()
                .min_w_0()
                .items_start()
                .gap(px(1.))
                .child(
                    div()
                        .w_full()
                        .text_size(px(14.))
                        .font_weight(FontWeight::SEMIBOLD)
                        .text_color(p.text)
                        .truncate()
                        .child(title),
                )
                .child(line)
        };
        let muted = |s: String| {
            div()
                .min_w_0()
                .text_size(px(12.))
                .text_color(p.text_placeholder)
                .truncate()
                .child(s)
                .into_any_element()
        };
        let Some(i) = &self.install else {
            let (title, line) = if self.detected {
                ("F1 25 not found", "Not in Steam or the EA app")
            } else {
                ("F1 25", "Looking for the game…")
            };
            return v_flex()
                .w(px(width))
                .flex_shrink_1()
                .min_w(px(170.))
                .child(text(title.into(), muted(line.into())))
                .into_any_element();
        };
        let title = match i.version() {
            Some(v) => format!("F1 25 · {v}"),
            None => "F1 25".into(),
        };
        let mode = match i.source {
            Source::EaApp if !cfg!(windows) => "Proton".to_owned(),
            _ => i.mode.clone(),
        };
        let line = h_flex()
            .w_full()
            .min_w_0()
            .gap(px(6.))
            .child(store_badge(i.source, p))
            .child(muted(mode))
            .into_any_element();
        let shell = cx.entity().downgrade();
        let copies = self.copies.clone();
        let missing = self.missing_copies.clone();
        let active = i.dir.clone();
        Button::new("game-switcher")
            .ghost()
            .h(px(48.))
            .w(px(width))
            .flex_shrink_1()
            .min_w(px(170.))
            .px(px(8.))
            .ml(px(-8.))
            .child(
                h_flex()
                    .w_full()
                    .min_w_0()
                    .gap(px(8.))
                    .child(text(title, line))
                    .child(
                        Icon::new(IconName::ChevronsUpDown)
                            .size(px(14.))
                            .text_color(p.text_muted)
                            .flex_shrink_0(),
                    ),
            )
            .tooltip("Switch the copy of F1 25 Play uses")
            .disabled(matches!(
                self.play_state(),
                PlayState::Running | PlayState::Launching | PlayState::Restoring | PlayState::Building
            ))
            .dropdown_menu_with_anchor(Anchor::BottomLeft, move |mut menu, _, _| {
                menu = menu
                    .min_w(px(360.))
                    .max_w(px(560.))
                    .check_side(Side::Right)
                    .label("Play this copy");
                for c in &copies {
                    let (shell, dir) = (shell.clone(), c.dir.clone());
                    let (source, version) =
                        (c.source, c.version().unwrap_or_else(|| "version unknown".into()));
                    let folder = c.dir.display().to_string();
                    menu = menu.item(
                        PopupMenuItem::element(move |_, _| {
                            v_flex()
                                .w_full()
                                .min_w_0()
                                .gap(px(3.))
                                .py(px(3.))
                                .child(
                                    h_flex()
                                        .gap(px(8.))
                                        .child(store_badge(source, p))
                                        .child(div().text_size(px(13.)).child(version.clone())),
                                )
                                .child(
                                    div()
                                        .text_size(px(12.))
                                        .text_color(p.text_muted)
                                        .truncate()
                                        .child(folder.clone()),
                                )
                        })
                        .checked(c.dir == active)
                        .on_click(move |_, window, cx| {
                            shell.update(cx, |s, cx| s.use_copy(dir.clone(), window, cx)).ok();
                        }),
                    );
                }
                for dir in &missing {
                    let folder = dir.display().to_string();
                    menu = menu.item(
                        PopupMenuItem::element(move |_, _| {
                            v_flex()
                                .w_full()
                                .min_w_0()
                                .gap(px(3.))
                                .py(px(3.))
                                .child(
                                    div()
                                        .text_size(px(13.))
                                        .text_color(p.text_muted)
                                        .child("Folder not found"),
                                )
                                .child(
                                    div()
                                        .text_size(px(12.))
                                        .text_color(p.text_placeholder)
                                        .truncate()
                                        .child(folder.clone()),
                                )
                        })
                        .disabled(true),
                    );
                }
                let shell = shell.clone();
                menu.separator().item(
                    PopupMenuItem::new("Add another folder…")
                        .icon(IconName::FolderPlus)
                        .on_click(move |_, window, cx| {
                            shell.update(cx, |s, cx| s.locate_game(window, cx)).ok();
                        }),
                )
            })
            .into_any_element()
    }

    fn status(&self, state: &PlayState, p: Palette, cx: &mut Context<Self>) -> impl IntoElement {
        let dot = |c: Hsla| div().size(px(7.)).rounded_full().bg(c).flex_shrink_0();
        let row = status_row();
        if self.game_edited.is_some() {
            return row
                .text_color(p.warning)
                .child(Icon::new(IconName::TriangleAlert).size(px(16.)))
                .child(div().min_w_0().truncate().child("Your game files were edited"))
                .child(
                    link("game-edited-what", "What to do", p)
                        .on_click(cx.listener(|this, _, window, cx| this.show_game_edited(window, cx))),
                )
                .when_some(self.launch_log(), |d, log| {
                    d.child(
                        link("launch-log", "View log", p).on_click(move |_, _, cx| cx.open_with_system(&log)),
                    )
                });
        }
        if let Some(notice) = &self.notice {
            return launch_failed(row, notice, p).when_some(self.launch_log(), |d, log| {
                d.child(link("launch-log", "View log", p).on_click(move |_, _, cx| cx.open_with_system(&log)))
            });
        }
        if let Some(row) = self.update_status(self.update_gate(state), status_row(), p, cx) {
            return row;
        }
        if let Some(row) = self.addon_update_status(state, status_row(), p, cx) {
            return row;
        }
        let rebuild = !self.library.profile.is_vanilla() && !self.library.rebuild.is_empty();
        match state {
            PlayState::Running => row,
            PlayState::Elsewhere(owner) => {
                let text = match owner {
                    Owner::Other(i) => match i.source {
                        Source::Steam => "F1 25 from Steam is running",
                        Source::EaApp => "F1 25 from the EA app is running",
                        Source::Unknown => "F1 25 from another folder is running",
                    },
                    _ => {
                        "F1 25 is running from a folder the launcher doesn't know · close it to play this copy"
                    }
                };
                let row = row
                    .text_color(p.text_muted)
                    .child(dot(p.warning))
                    .child(div().min_w_0().truncate().child(text));
                match owner {
                    Owner::Other(i) => {
                        let dir = i.dir.clone();
                        row.child(link("switch-to-running", "Switch to it", p).on_click(
                            cx.listener(move |this, _, window, cx| this.use_copy(dir.clone(), window, cx)),
                        ))
                    }
                    _ => row,
                }
            }
            PlayState::Restoring => row.child(dot(p.warning)).child("Restoring the game files…"),
            PlayState::Building => {
                let Some(b) = &self.build else {
                    return row;
                };
                let text = match self.cancelling {
                    true => "Cancelling the build…".to_owned(),
                    false => format!(
                        "{} for {} · {}",
                        capitalised(&b.progress()),
                        b.profile,
                        duration((b.minutes() * 60).max(1))
                    ),
                };
                row.child(dot(p.warning))
                    .child(div().min_w_0().truncate().child(text))
                    .when(!self.cancelling, |d| {
                        d.child(
                            link("cancel-build", "Cancel", p)
                                .on_click(cx.listener(|this, _, window, cx| this.cancel_build(window, cx))),
                        )
                    })
                    .when_some(self.wrapper_log(), |d, log| {
                        d.child(
                            link("launch-log", "Log", p).on_click(move |_, _, cx| cx.open_with_system(&log)),
                        )
                    })
            }
            PlayState::Launching => row
                .child(dot(p.warning))
                .child(format!(
                    "{} {}",
                    match self.launch.as_ref().and_then(|l| l.stage.as_deref()) {
                        Some(stage) => stage.to_owned(),
                        None => format!(
                            "Starting with profile {}{}…",
                            self.library.profile.name,
                            if self.launch_mode().is_vr() { " in VR" } else { "" },
                        ),
                    },
                    self.launch
                        .as_ref()
                        .map(|l| duration(l.since.elapsed().as_secs()))
                        .unwrap_or_default()
                ))
                .when_some(self.launch_log(), |d, log| {
                    d.child(link("launch-log", "Log", p).on_click(move |_, _, cx| cx.open_with_system(&log)))
                }),
            PlayState::Crashed(session) => {
                let dir = session.dir.clone();
                let ending = self
                    .crash
                    .as_ref()
                    .filter(|(d, _)| *d == session.dir)
                    .map(|(_, e)| e);
                let open =
                    link("crash-log", "Open logs", p).on_click(move |_, _, cx| cx.open_with_system(&dir));
                let dismiss = link("dismiss", "Dismiss", p).on_click(cx.listener(|this, _, _, cx| {
                    this.dismissed = this.session.as_ref().map(|s| s.dir.clone());
                    cx.notify();
                }));
                if let Some(Err(failure)) = ending {
                    return launch_failed(row, failure, p).child(open).child(dismiss);
                }
                let view = self.crash_view.as_ref().filter(|v| v.dir == session.dir);
                let details = view.map(|_| {
                    link("crash-details", "What happened", p).on_click(cx.listener(|this, _, _, cx| {
                        this.crash_open = true;
                        cx.notify();
                    }))
                });
                let summary = view
                    .map(|v| v.headline())
                    .or_else(|| ending.and_then(|e| e.as_ref().ok()).cloned())
                    .filter(|s| !s.is_empty())
                    .unwrap_or_else(|| "reading the log…".into());
                let crashed = format!(
                    "Crashed {}",
                    session
                        .ended
                        .map(|e| ago(game::now_secs() - e))
                        .unwrap_or_default()
                );
                row.items_start()
                    .child(div().mt(px(6.)).child(dot(p.error)))
                    .child(
                        v_flex()
                            .min_w_0()
                            .gap(px(2.))
                            .child(
                                h_flex()
                                    .gap(px(12.))
                                    .child(
                                        div()
                                            .text_color(p.error)
                                            .font_weight(FontWeight::MEDIUM)
                                            .child(crashed),
                                    )
                                    .children(details)
                                    .child(open)
                                    .child(dismiss),
                            )
                            .child(
                                div()
                                    .text_size(px(12.))
                                    .text_color(p.text_muted)
                                    .truncate()
                                    .child(summary),
                            ),
                    )
            }
            PlayState::NotInstalled => row.text_color(p.text_muted).child(dot(p.text_placeholder)).child(
                div()
                    .min_w_0()
                    .truncate()
                    .child("Install it with Steam or the EA app, or locate its folder"),
            ),
            PlayState::Detecting => row.text_color(p.text_muted).child("Detecting F1 25…"),
            PlayState::Checking => row.text_color(p.text_muted).child("Checking conflicts…"),
            PlayState::Ready => {
                if rebuild {
                    return row.text_color(p.text_muted).child(dot(p.warning)).child(
                        div().min_w_0().truncate().child(format!(
                            "Ready · rebuilds {} for {} on launch",
                            self.library.profile.name,
                            plural(self.library.rebuild.len(), "changed setting", "changed settings")
                        )),
                    );
                }
                row
            }
        }
    }

    fn wrapper_log(&self) -> Option<std::path::PathBuf> {
        let log = self.store.root.join("state/wrapper-run.log");
        log.is_file().then_some(log)
    }

    fn launch_log(&self) -> Option<std::path::PathBuf> {
        let log = self.store.root.join("state/launcher-run.log");
        log.is_file().then_some(log)
    }

    pub(crate) fn profile_picker(&self, p: Palette, cx: &mut Context<Self>) -> impl IntoElement {
        let shell = cx.entity().downgrade();
        let active = self.library.profile.name.clone();
        let summary = self.library.active().map(|a| a.summary()).unwrap_or_default();
        let profiles: Vec<(String, String)> = self
            .library
            .profiles
            .iter()
            .map(|p| (p.name.clone(), p.summary()))
            .collect();
        Button::new("profile-picker")
            .outline()
            .h(px(40.))
            .min_w(px(180.))
            .child(
                h_flex().gap(px(8.)).child(avatar(&active, 24., p)).child(
                    v_flex()
                        .items_start()
                        .child(
                            div()
                                .text_size(px(13.))
                                .font_weight(FontWeight::MEDIUM)
                                .child(active.clone()),
                        )
                        .child(div().text_size(px(11.)).text_color(p.text_muted).child(summary)),
                ),
            )
            .dropdown_caret(true)
            .disabled(matches!(
                self.play_state(),
                PlayState::Running | PlayState::Launching | PlayState::Restoring | PlayState::Building
            ))
            .dropdown_menu_with_anchor(Anchor::BottomRight, move |mut menu, _, _| {
                menu = menu.min_w(px(300.)).label("Profile");
                for (name, summary) in &profiles {
                    let (shell, target) = (shell.clone(), name.clone());
                    let (label, summary) = (name.clone(), summary.clone());
                    menu = menu.item(
                        PopupMenuItem::element(move |_, _| {
                            h_flex()
                                .w_full()
                                .gap(px(24.))
                                .justify_between()
                                .child(label.clone())
                                .child(div().text_color(p.text_muted).child(summary.clone()))
                        })
                        .checked(*name == active)
                        .on_click(move |_, window, cx| {
                            shell
                                .update(cx, |s, cx| s.switch_profile(&target, window, cx))
                                .ok();
                        }),
                    );
                }
                let shell = shell.clone();
                menu.separator().item(
                    PopupMenuItem::new("Manage profiles…")
                        .icon(IconName::Settings)
                        .on_click(move |_, _, cx| {
                            shell.update(cx, |s, cx| s.go(Page::Profiles, cx)).ok();
                        }),
                )
            })
    }

    fn play_caret(&self, cx: &mut Context<Self>) -> impl IntoElement {
        let shell = cx.entity().downgrade();
        let vr = self.launch_mode().is_vr();
        let start_store = self.start_store;
        let start_label = match self.install.as_ref().map(|i| i.source) {
            Some(Source::EaApp) => "Start the EA app automatically",
            _ => "Start Steam automatically",
        };
        Button::new("play-menu")
            .h(px(52.))
            .w(px(34.))
            .rounded_l(px(0.))
            .rounded_r(px(9.))
            .primary()
            .child(Icon::new(IconName::ChevronDown).size(px(16.)))
            .dropdown_menu_with_anchor(Anchor::TopRight, move |menu, _, _| {
                let mode = |label: &'static str, icon: IconName, mode: LaunchMode| {
                    let shell = shell.clone();
                    PopupMenuItem::new(label)
                        .icon(icon)
                        .checked(vr == mode.is_vr())
                        .on_click(move |_, window, cx| {
                            shell
                                .update(cx, |s, cx| {
                                    s.set_launch_mode(mode, window, cx);
                                    s.play(window, cx);
                                })
                                .ok();
                        })
                };
                let (console, steam) = (shell.clone(), shell.clone());
                menu.min_w(px(220.))
                    .check_side(Side::Right)
                    .item(mode("Play", IconName::Play, LaunchMode::Normal))
                    .item(mode("Play in VR", IconName::RectangleGoggles, LaunchMode::Vr))
                    .separator()
                    .item(
                        PopupMenuItem::new("Play with console")
                            .icon(IconName::SquareTerminal)
                            .on_click(move |_, window, cx| {
                                console
                                    .update(cx, |s, cx| {
                                        s.set_console(true, window, cx);
                                        s.play(window, cx);
                                    })
                                    .ok();
                            }),
                    )
                    .separator()
                    .item(
                        PopupMenuItem::new(start_label)
                            .icon(IconName::CirclePower)
                            .checked(start_store)
                            .on_click(move |_, window, cx| {
                                steam
                                    .update(cx, |s, cx| s.set_start_store(!start_store, window, cx))
                                    .ok();
                            }),
                    )
            })
    }

    fn big_button(&self, state: &PlayState, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        let gate = self.update_gate(state);
        if gate != Gate::Play {
            return self.update_button(gate, p, cx);
        }
        if self.addon_update_takes_play(state) {
            return self.addon_update_button(p, cx);
        }
        let vr = self.launch_mode().is_vr();
        let (icon, label, bg, fg, enabled) = match state {
            PlayState::Ready | PlayState::Crashed(_) if vr => (
                IconName::RectangleGoggles,
                "Play in VR",
                p.accent,
                p.on_accent,
                true,
            ),
            PlayState::Ready | PlayState::Crashed(_) => (IconName::Play, "Play", p.accent, p.on_accent, true),
            PlayState::Launching => (IconName::LoaderCircle, "Launching…", p.elevated, p.accent, false),
            PlayState::Restoring => (IconName::LoaderCircle, "Restoring…", p.elevated, p.accent, false),
            PlayState::Building => (IconName::LoaderCircle, "Building…", p.elevated, p.accent, false),
            PlayState::Running => (IconName::Square, "Stop", p.elevated, p.error, true),
            PlayState::NotInstalled => (IconName::FolderSearch, "Locate game", p.elevated, p.text, true),
            PlayState::Detecting | PlayState::Checking => {
                (IconName::LoaderCircle, "Play", p.elevated, p.text_muted, false)
            }
            PlayState::Elsewhere(_) => (IconName::Play, "Play", p.elevated, p.text_muted, false),
        };
        let ready = matches!(state, PlayState::Ready | PlayState::Crashed(_));
        let missing = matches!(state, PlayState::NotInstalled);
        let running = matches!(state, PlayState::Running);
        let button = div()
            .id("play")
            .h(px(52.))
            .w(px(170.))
            .flex_shrink_0()
            .when(ready, |d| d.rounded_l(px(9.)))
            .when(!ready, |d| d.rounded(px(9.)))
            .bg(bg)
            .text_color(fg)
            .flex()
            .items_center()
            .justify_center()
            .gap(px(8.))
            .when(!ready, |d| d.border_1().border_color(p.border))
            .when(ready, |d| d.hover(move |s| s.bg(p.accent_hover)))
            .when(missing, |d| d.hover(move |s| s.bg(p.selected)))
            .when(enabled, |d| d.cursor_pointer())
            .map(|d| match icon {
                IconName::LoaderCircle => d.child(Spinner::new().icon(icon).with_size(px(18.))),
                _ => d.child(Icon::new(icon).size(px(18.))),
            })
            .child(
                div()
                    .text_size(px(15.))
                    .font_weight(FontWeight::SEMIBOLD)
                    .child(label),
            )
            .when(enabled, |d| {
                d.on_click(cx.listener(move |this, _, window, cx| {
                    if running {
                        this.confirm_stop(window, cx);
                    } else if missing {
                        this.locate_game(window, cx);
                    } else {
                        this.play(window, cx);
                    }
                }))
            });
        h_flex()
            .flex_shrink_0()
            .child(button)
            .when(ready, |d| d.child(self.play_caret(cx)))
            .into_any_element()
    }

    pub(crate) fn play_bar(&self, p: Palette, wide: bool, cx: &mut Context<Self>) -> impl IntoElement {
        let state = self.play_state();
        h_flex()
            .h(px(HEIGHT))
            .flex_shrink_0()
            .bg(p.chrome)
            .border_t_1()
            .border_color(p.border_variant)
            .child(crate::pages::game_art(56., 56.).m(px(8.)))
            .child(
                h_flex()
                    .flex_1()
                    .min_w_0()
                    .h_full()
                    .items_center()
                    .pl(px(16.))
                    .pr(px(16.))
                    .gap(px(20.))
                    .child(self.game_info(p, wide, cx))
                    .child(self.status(&state, p, cx))
                    .child(self.big_button(&state, p, cx)),
            )
    }
}
