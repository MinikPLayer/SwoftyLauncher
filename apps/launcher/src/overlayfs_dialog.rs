//! The window `swofty run --use-overlayfs` opens when it can't mount the game
//! and would copy the mod files into the game folder instead. The wrapper
//! waits for it and reads its exit code: 0 to continue (copying), 1 to cancel
//! the launch. It runs instead of the launcher, so it works while the
//! launcher is open.

use std::path::PathBuf;
use std::sync::atomic::{AtomicI32, Ordering};

use gpui_kit::assets::IconName;
use gpui_kit::component::button::{Button, ButtonVariants};
use gpui_kit::component::checkbox::Checkbox;
use gpui_kit::component::{Icon, TitleBar, h_flex, v_flex};
use gpui_kit::*;

use swofty_core::overlayfs::Kind;
use swofty_core::store::Store;

use crate::overlayfs_answer::{CONTINUE, EXIT, remember};
use crate::theme;

const DOCS: &str = "https://github.com/Swofty-Developments/SwoftyLauncher/blob/main/docs/guide/linux.md";

static CODE: AtomicI32 = AtomicI32::new(EXIT);

/// Show the dialog and exit with its answer.
pub(crate) fn run(kind: Kind, reason: String, data: Option<PathBuf>) -> ! {
    let store = data.map_or_else(Store::open_default, Store::new);
    let app = gpui_kit::application().with_assets(gpui_kit::assets::AllAssets);
    app.run(move |cx| {
        gpui_kit::init(cx);
        theme::init(cx);
        let options = WindowOptions {
            window_bounds: Some(WindowBounds::Windowed(Bounds::centered(
                None,
                size(px(560.), px(330.)),
                cx,
            ))),
            window_min_size: Some(size(px(560.), px(330.))),
            app_id: Some("net.swofty.ModLauncher.OverlayDialog".into()),
            titlebar: Some(TitlebarOptions {
                title: Some(kind.title().into()),
                ..TitleBar::title_bar_options()
            }),
            ..TitleBar::window_options()
        };
        gpui_kit::open_window(options, cx, |_, cx| {
            cx.new(|_| Dialog {
                kind,
                reason,
                store,
                hide: false,
            })
        })
        .expect("failed to open window");
        cx.on_window_closed(|cx, _| cx.quit()).detach();
        cx.activate(true);
    });
    std::process::exit(CODE.load(Ordering::Relaxed));
}

struct Dialog {
    kind: Kind,
    reason: String,
    store: Store,
    hide: bool,
}

impl Dialog {
    fn answer(&mut self, code: i32, cx: &mut Context<Self>) {
        remember(&self.store, self.kind, self.hide, code);
        CODE.store(code, Ordering::Relaxed);
        cx.quit();
    }

    fn explanation(&self) -> &'static str {
        match self.kind {
            Kind::Unsupported => {
                "This launch can't run the game from an OverlayFS mount. Swofty can copy the mod files \
                 into the game folder for this session instead, as it does without --use-overlayfs, \
                 and put the game folder back when the game closes."
            }
            Kind::MountFailed => {
                "Swofty couldn't mount the game's OverlayFS. It can copy the mod files into the game \
                 folder for this session instead, as it does without --use-overlayfs, and put the \
                 game folder back when the game closes."
            }
        }
    }
}

impl Render for Dialog {
    fn render(&mut self, _: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        let p = theme::palette(cx);
        let reason = v_flex()
            .gap(px(4.))
            .p(px(12.))
            .rounded(px(8.))
            .bg(p.elevated)
            .border_1()
            .border_color(p.border)
            .child(
                div()
                    .text_size(px(11.))
                    .font_weight(FontWeight::SEMIBOLD)
                    .text_color(p.text_muted)
                    .child("REASON"),
            )
            .child(
                div()
                    .text_size(px(13.))
                    .line_height(px(19.))
                    .text_color(p.text)
                    .child(self.reason.clone()),
            );
        let docs = div()
            .id("overlayfs-docs")
            .cursor_pointer()
            .text_size(px(12.))
            .text_color(p.accent)
            .hover(|s| s.underline())
            .child("About OverlayFS launches")
            .on_click(|_, _, cx| cx.open_url(DOCS));
        v_flex()
            .size_full()
            .bg(p.background)
            .child(TitleBar::new().bg(p.chrome))
            .child(
                v_flex()
                    .flex_1()
                    .gap(px(14.))
                    .p(px(20.))
                    .child(
                        h_flex()
                            .gap(px(10.))
                            .items_center()
                            .child(
                                Icon::new(IconName::Construction)
                                    .size(px(20.))
                                    .text_color(p.warning),
                            )
                            .child(
                                div()
                                    .text_size(px(17.))
                                    .font_weight(FontWeight::BOLD)
                                    .text_color(p.text)
                                    .child(self.kind.title()),
                            ),
                    )
                    .child(
                        div()
                            .text_size(px(13.))
                            .line_height(px(19.))
                            .text_color(p.text_muted)
                            .child(self.explanation()),
                    )
                    .child(reason)
                    .child(docs)
                    .child(div().flex_1())
                    .child(
                        h_flex()
                            .justify_between()
                            .items_center()
                            .child(
                                Checkbox::new("overlayfs-hide")
                                    .label("Don't show again")
                                    .checked(self.hide)
                                    .on_click(cx.listener(|this, checked: &bool, _, cx| {
                                        this.hide = *checked;
                                        cx.notify();
                                    })),
                            )
                            .child(
                                h_flex()
                                    .gap(px(8.))
                                    .child(
                                        Button::new("overlayfs-exit")
                                            .h(px(34.))
                                            .px(px(16.))
                                            .label("Exit")
                                            .on_click(cx.listener(|this, _, _, cx| this.answer(EXIT, cx))),
                                    )
                                    .child(
                                        Button::new("overlayfs-continue")
                                            .primary()
                                            .h(px(34.))
                                            .px(px(16.))
                                            .label("Continue")
                                            .on_click(
                                                cx.listener(|this, _, _, cx| this.answer(CONTINUE, cx)),
                                            ),
                                    ),
                            ),
                    ),
            )
    }
}
