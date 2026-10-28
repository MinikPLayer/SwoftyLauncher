use std::path::PathBuf;

use gpui_kit::assets::IconName;
use gpui_kit::component::button::{Button, ButtonVariants};
use gpui_kit::component::{Icon, TITLE_BAR_HEIGHT, h_flex, v_flex};
use gpui_kit::prelude::FluentBuilder;
use gpui_kit::*;
use swofty_core::known_problems::Diagnosis;

use crate::discord::{BLURPLE, BLURPLE_HOVER, INVITE, LOGO};
use crate::send_logs;
use crate::shell::Shell;
use crate::theme::Palette;

/// A crash newer than this opens the modal by itself; an older one only shows
/// in the play bar.
pub(crate) const FRESH_SECS: f64 = 15. * 60.;

const UNKNOWN: &str = "We couldn't tell why it crashed";
const UNKNOWN_WHAT: &str = "Swofty Launcher looked at what the game left behind and didn't recognise this crash. \
     Send your logs to the developers, then join the Swofty Developments Discord so they can get back to you.";

/// What the crash modal shows for one crashed session, or one that ended stuck.
#[derive(Debug, Clone, PartialEq)]
pub(crate) struct CrashView {
    pub dir: PathBuf,
    pub diagnosis: Option<Diagnosis>,
    pub crashed: bool,
    /// The exit code and last exception, as the play bar shows them.
    pub summary: String,
}

impl CrashView {
    /// The play bar's line under "Crashed".
    pub(crate) fn headline(&self) -> String {
        match &self.diagnosis {
            Some(d) => d.title.clone(),
            None => self.summary.clone(),
        }
    }
}

fn badge(p: Palette) -> Div {
    div()
        .size(px(56.))
        .flex()
        .items_center()
        .justify_center()
        .rounded_full()
        .bg(p.error.opacity(0.14))
        .border_1()
        .border_color(p.error.opacity(0.45))
        .child(
            Icon::new(IconName::TriangleAlert)
                .size(px(26.))
                .text_color(p.error),
        )
}

fn step(n: usize, text: String, p: Palette) -> Div {
    h_flex()
        .gap(px(12.))
        .items_start()
        .child(
            div()
                .size(px(22.))
                .flex_shrink_0()
                .flex()
                .items_center()
                .justify_center()
                .rounded_full()
                .bg(p.accent.opacity(0.16))
                .text_size(px(12.))
                .font_weight(FontWeight::SEMIBOLD)
                .text_color(p.accent)
                .child(n.to_string()),
        )
        .child(
            div()
                .flex_1()
                .min_w_0()
                .pt(px(2.))
                .text_size(px(13.))
                .line_height(px(19.))
                .text_color(p.text)
                .child(text),
        )
}

fn discord_button() -> Stateful<Div> {
    let white = hsla(0., 0., 1., 1.);
    h_flex()
        .id("crash-discord")
        .h(px(38.))
        .px(px(16.))
        .gap(px(8.))
        .items_center()
        .rounded(px(8.))
        .cursor_pointer()
        .bg(rgb(BLURPLE))
        .hover(|s| s.bg(rgb(BLURPLE_HOVER)))
        .text_size(px(14.))
        .font_weight(FontWeight::SEMIBOLD)
        .text_color(white)
        .child(svg().data(LOGO).size(px(16.)).text_color(white))
        .child("Join the support Discord")
        .on_click(|_, _, cx| cx.open_url(INVITE))
}

impl Shell {
    /// The full-window crash modal, while it is open for the newest crash.
    pub(crate) fn crash_overlay(&self, p: Palette, cx: &mut Context<Self>) -> Option<impl IntoElement> {
        let view = self.crash_view.as_ref().filter(|_| self.crash_open)?;
        let known = view.diagnosis.is_some();
        let (title, what, steps) = match &view.diagnosis {
            Some(d) => (d.title.clone(), d.what.clone(), d.steps.clone()),
            None => (
                UNKNOWN.to_owned(),
                UNKNOWN_WHAT.to_owned(),
                vec![
                    format!(
                        "Press {} and say what you were doing when it crashed.",
                        send_logs::label()
                    ),
                    "Join the support Discord, so the developers can reply to you there.".to_owned(),
                ],
            ),
        };
        let close = cx.listener(|this, _, _, cx| {
            this.crash_open = false;
            cx.notify();
        });
        let send = cx.listener(|this, _, window, cx| this.send_logs(window, cx));
        let buttons = h_flex()
            .w_full()
            .gap(px(10.))
            .justify_center()
            .flex_wrap()
            .map(|row| {
                if known {
                    row.child(
                        Button::new("crash-send-logs")
                            .outline()
                            .h(px(38.))
                            .label(send_logs::label())
                            .on_click(send),
                    )
                    .child(
                        Button::new("crash-close")
                            .primary()
                            .h(px(38.))
                            .px(px(18.))
                            .label("Got it")
                            .on_click(close),
                    )
                } else {
                    row.child(
                        Button::new("crash-send-logs")
                            .primary()
                            .h(px(38.))
                            .label(send_logs::label())
                            .on_click(send),
                    )
                    .child(discord_button())
                    .child(
                        Button::new("crash-close")
                            .ghost()
                            .h(px(38.))
                            .label("Close")
                            .on_click(close),
                    )
                }
            });
        let card = v_flex()
            .w_full()
            .max_w(px(620.))
            .gap(px(18.))
            .items_center()
            .child(badge(p))
            .child(
                v_flex()
                    .items_center()
                    .gap(px(6.))
                    .child(
                        div()
                            .text_size(px(13.))
                            .font_weight(FontWeight::SEMIBOLD)
                            .text_color(p.error)
                            .child(if view.crashed {
                                "F1 25 crashed"
                            } else {
                                "F1 25 got stuck"
                            }),
                    )
                    .child(
                        div()
                            .text_center()
                            .text_size(px(22.))
                            .line_height(px(28.))
                            .font_weight(FontWeight::BOLD)
                            .text_color(p.text)
                            .child(title),
                    )
                    .child(
                        div()
                            .text_center()
                            .text_size(px(14.))
                            .line_height(px(21.))
                            .text_color(p.text_muted)
                            .child(what),
                    ),
            )
            .child(
                v_flex()
                    .w_full()
                    .gap(px(12.))
                    .p(px(16.))
                    .rounded(px(10.))
                    .bg(p.surface)
                    .border_1()
                    .border_color(p.border_variant)
                    .child(
                        div()
                            .text_size(px(11.))
                            .font_weight(FontWeight::SEMIBOLD)
                            .text_color(p.text_muted)
                            .child(if known { "HOW TO FIX IT" } else { "WHAT TO DO" }),
                    )
                    .children(steps.into_iter().enumerate().map(|(i, s)| step(i + 1, s, p))),
            )
            .child(buttons)
            .when(!view.summary.is_empty(), |c| {
                c.child(
                    div()
                        .text_center()
                        .text_size(px(11.))
                        .text_color(p.text_placeholder)
                        .child(view.summary.clone()),
                )
            });
        Some(
            div()
                .id("crash-modal")
                .absolute()
                .top(TITLE_BAR_HEIGHT)
                .left_0()
                .right_0()
                .bottom_0()
                .occlude()
                .bg(p.background)
                .overflow_y_scroll()
                .child(
                    v_flex()
                        .min_h_full()
                        .w_full()
                        .items_center()
                        .justify_center()
                        .px(px(24.))
                        .py(px(32.))
                        .child(card),
                ),
        )
    }
}
