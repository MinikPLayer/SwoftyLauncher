//! The Tools page: a sideways row of large cards for the tools that make or
//! edit Swofty addons, listed in `assets/tools.json` with their art in
//! `assets/tools/`.

use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::{Arc, LazyLock};
use std::time::Duration;

use gpui_kit::assets::IconName;
use gpui_kit::component::button::{Button, ButtonVariants};
use gpui_kit::component::notification::Notification;
use gpui_kit::component::{Disableable, Icon, WindowExt, h_flex, v_flex};
use gpui_kit::prelude::FluentBuilder;
use gpui_kit::*;
use serde::Deserialize;
use swofty_core::app_control::Block;

use crate::pages::GUTTER;
use crate::shell::{Page, Shell};
use crate::theme::{Palette, scrollbar_x};

mod art {
    include!(concat!(env!("OUT_DIR"), "/tools_art.rs"));
}

const GAP: f32 = 24.;
/// The strip under the cards that holds the scrollbar.
const BAR: f32 = 18.;

#[derive(Clone, Debug, Deserialize)]
pub struct Link {
    pub label: String,
    pub url: String,
}

#[derive(Clone, Debug, Deserialize)]
pub struct Tool {
    pub name: String,
    pub maker: String,
    pub about: String,
    #[serde(default)]
    pub image: Option<String>,
    #[serde(default)]
    pub credit: Option<String>,
    #[serde(default)]
    pub icon: Option<String>,
    #[serde(default)]
    pub open: Option<String>,
    #[serde(default)]
    pub get: Option<String>,
    #[serde(default)]
    pub more: Option<Link>,
}

#[derive(Deserialize)]
struct List {
    tools: Vec<Tool>,
}

pub fn tools() -> &'static [Tool] {
    static TOOLS: LazyLock<Vec<Tool>> = LazyLock::new(|| {
        serde_json::from_str::<List>(include_str!("../assets/tools.json"))
            .map(|l| l.tools)
            .unwrap_or_default()
    });
    &TOOLS
}

fn format(bytes: &[u8]) -> Option<ImageFormat> {
    match bytes {
        [0x89, b'P', b'N', b'G', ..] => Some(ImageFormat::Png),
        [0xff, 0xd8, ..] => Some(ImageFormat::Jpeg),
        [b'R', b'I', b'F', b'F', _, _, _, _, b'W', b'E', b'B', b'P', ..] => Some(ImageFormat::Webp),
        _ => None,
    }
}

fn image(name: &str) -> Option<Arc<Image>> {
    static IMAGES: LazyLock<HashMap<&str, Arc<Image>>> = LazyLock::new(|| {
        art::ART
            .iter()
            .filter_map(|(name, bytes)| {
                Some((*name, Arc::new(Image::from_bytes(format(bytes)?, bytes.to_vec()))))
            })
            .collect()
    });
    IMAGES.get(name).cloned()
}

fn icon(name: &str) -> IconName {
    match name {
        "database" => IconName::Database,
        "folder-input" => IconName::FolderInput,
        "app-window" => IconName::AppWindow,
        "user-pen" => IconName::UserPen,
        "users" => IconName::Users,
        "package" => IconName::Package,
        "table" => IconName::Table,
        "map" => IconName::Map,
        "palette" => IconName::Palette,
        "code" => IconName::Code,
        _ => IconName::Wrench,
    }
}

fn program(name: &str) -> Option<PathBuf> {
    let path = std::env::current_exe()
        .ok()?
        .parent()?
        .join(format!("{name}{}", std::env::consts::EXE_SUFFIX));
    path.is_file().then_some(path)
}

pub(crate) fn web_link(
    id: impl Into<SharedString>,
    label: impl Into<SharedString>,
    url: impl Into<String>,
    p: Palette,
) -> Stateful<Div> {
    let url = url.into();
    h_flex()
        .id(ElementId::Name(id.into()))
        .gap(px(6.))
        .items_center()
        .cursor_pointer()
        .text_size(px(14.))
        .font_weight(FontWeight::MEDIUM)
        .text_color(p.accent_hover)
        .hover(|s| s.underline())
        .child(label.into())
        .child(Icon::new(IconName::ExternalLink).size(px(14.)))
        .on_click(move |_, _, cx| cx.open_url(&url))
}

enum Action {
    Go(Page),
    TurnOn(String),
    Run(PathBuf),
    Get(String),
    None,
}

/// The row's scroll position, the card it last settled on and the glide in
/// flight.
#[derive(Default)]
pub(crate) struct Carousel {
    scroll: ScrollHandle,
    stride: f32,
    anchor: f32,
    settle: Option<Task<()>>,
    glide: Option<Task<()>>,
}

impl Shell {
    fn tool_action(&self, tool: &Tool) -> Action {
        let get = || tool.get.clone().map_or(Action::None, Action::Get);
        let Some((kind, target)) = tool.open.as_deref().and_then(|o| o.split_once(':')) else {
            return get();
        };
        match kind {
            "page" => match target {
                "database" => Action::Go(Page::Database),
                "importer" => Action::Go(Page::Importer),
                "addons" => Action::Go(Page::Addons),
                _ => get(),
            },
            "addon" => match self.library.sections.iter().position(|s| s.addon == target) {
                Some(i) => Action::Go(Page::Section(i)),
                None if !self.vanilla() && self.library.addons.iter().any(|a| a.manifest.id == target) => {
                    Action::TurnOn(target.to_owned())
                }
                None => get(),
            },
            "program" => program(target).map_or_else(get, Action::Run),
            _ => get(),
        }
    }

    fn tool_button(&self, index: usize, tool: &Tool, cx: &mut Context<Self>) -> Option<Button> {
        let id = |what: &str| SharedString::from(format!("tool-{index}-{what}"));
        let button = match self.tool_action(tool) {
            Action::None => return None,
            Action::Get(url) => Button::new(id("get"))
                .primary()
                .icon(IconName::ExternalLink)
                .label("Get")
                .on_click(move |_, _, cx| cx.open_url(&url)),
            Action::Go(page) => Button::new(id("open"))
                .primary()
                .label("Open")
                .on_click(cx.listener(move |this, _, _, cx| this.go(page, cx))),
            Action::TurnOn(addon) => Button::new(id("on"))
                .primary()
                .icon(IconName::Power)
                .label("Turn on")
                .on_click(
                    cx.listener(move |this, _, window, cx| this.set_addon_enabled(&addon, true, window, cx)),
                ),
            Action::Run(path) => {
                let name = tool.name.clone();
                Button::new(id("run"))
                    .primary()
                    .label("Open")
                    .on_click(move |_, window, cx| {
                        let mut command = std::process::Command::new(&path);
                        if let Some(dir) = path.parent() {
                            command.current_dir(dir);
                        }
                        match command.spawn() {
                            Ok(_) => window.push_notification(
                                Notification::info(format!("{name} opens in your browser")),
                                cx,
                            ),
                            Err(e) => {
                                crate::app_log::action("start a tool").file(&path).failed(&e);
                                window.push_notification(
                                    match Block::of(&path, &e) {
                                        Some(b) => Notification::error(b.text()).autohide(false),
                                        None => Notification::error(e.to_string()),
                                    }
                                    .title(format!("Could not start {name}")),
                                    cx,
                                )
                            }
                        }
                    })
            }
        };
        Some(button)
    }

    fn tool_art(tool: &Tool, width: f32, height: f32, p: Palette) -> Div {
        let frame = div()
            .relative()
            .w(px(width))
            .h(px(height))
            .flex_shrink_0()
            .overflow_hidden()
            .border_b_1()
            .border_color(p.border_variant);
        let Some(art) = tool.image.as_deref().and_then(image) else {
            return frame.flex().items_center().justify_center().bg(p.selected).child(
                Icon::new(icon(tool.icon.as_deref().unwrap_or_default()))
                    .size(px(height * 0.28))
                    .text_color(p.accent_hover),
            );
        };
        frame
            .bg(p.chrome)
            .child(img(art).size_full().object_fit(ObjectFit::Cover))
            .children(tool.credit.clone().map(|credit| {
                div()
                    .absolute()
                    .right(px(10.))
                    .bottom(px(10.))
                    .px(px(8.))
                    .py(px(3.))
                    .rounded(px(5.))
                    .bg(hsla(0., 0., 0., 0.62))
                    .text_size(px(11.))
                    .text_color(hsla(0., 0., 1., 0.86))
                    .child(credit)
            }))
    }

    fn tool_card(
        &self,
        index: usize,
        tool: &Tool,
        width: f32,
        art: f32,
        p: Palette,
        cx: &mut Context<Self>,
    ) -> AnyElement {
        let more = tool
            .more
            .clone()
            .map(|l| web_link(format!("tool-{index}-more"), l.label, l.url, p));
        v_flex()
            .w(px(width))
            .flex_shrink_0()
            .rounded(px(14.))
            .border_1()
            .border_color(p.border)
            .bg(p.surface)
            .overflow_hidden()
            .child(Self::tool_art(tool, width - 2., art, p))
            .child(
                v_flex()
                    .flex_1()
                    .px(px(24.))
                    .pt(px(20.))
                    .pb(px(22.))
                    .gap(px(12.))
                    .child(
                        v_flex()
                            .gap(px(2.))
                            .child(
                                div()
                                    .text_size(px(21.))
                                    .font_weight(FontWeight::SEMIBOLD)
                                    .child(tool.name.clone()),
                            )
                            .child(
                                div()
                                    .text_size(px(13.))
                                    .text_color(p.text_placeholder)
                                    .child(format!("by {}", tool.maker)),
                            ),
                    )
                    .child(
                        div()
                            .flex_1()
                            .text_size(px(14.))
                            .line_height(px(21.))
                            .text_color(p.text_muted)
                            .child(tool.about.clone()),
                    )
                    .child(
                        h_flex()
                            .pt(px(6.))
                            .gap(px(18.))
                            .items_center()
                            .children(self.tool_button(index, tool, cx))
                            .children(more),
                    ),
            )
            .into_any_element()
    }

    fn tools_pos(&self) -> f32 {
        -f32::from(self.tools.scroll.offset().x)
    }

    fn tools_snaps(&self) -> Vec<f32> {
        let max = f32::from(self.tools.scroll.max_offset().x).max(0.);
        let mut snaps: Vec<f32> = (0..tools().len())
            .map(|i| (i as f32 * self.tools.stride).min(max))
            .collect();
        snaps.dedup();
        snaps
    }

    fn tools_glide(&mut self, to: f32, cx: &mut Context<Self>) {
        const STEPS: u32 = 16;
        let from = self.tools_pos();
        let scroll = self.tools.scroll.clone();
        self.tools.anchor = to;
        self.tools.glide = Some(cx.spawn(async move |this, cx| {
            for step in 1..=STEPS {
                cx.background_executor().timer(Duration::from_millis(14)).await;
                let t = 1. - (1. - step as f32 / STEPS as f32).powi(3);
                scroll.set_offset(point(px(-(from + (to - from) * t)), px(0.)));
                if this.update(cx, |_, cx| cx.notify()).is_err() {
                    break;
                }
            }
        }));
    }

    fn tools_step(&mut self, forward: bool, cx: &mut Context<Self>) {
        let pos = self.tools_pos();
        let snaps = self.tools_snaps();
        let target = if forward {
            snaps.iter().copied().find(|s| *s > pos + 1.)
        } else {
            snaps.iter().copied().rev().find(|s| *s < pos - 1.)
        };
        if let Some(to) = target {
            self.tools_glide(to, cx);
        }
    }

    // A wheel or trackpad scroll settles on the next card in the direction it moved.
    fn tools_settle(&mut self, cx: &mut Context<Self>) {
        let (pos, anchor) = (self.tools_pos(), self.tools.anchor);
        let snaps = self.tools_snaps();
        let nearest = snaps
            .iter()
            .copied()
            .min_by(|a, b| (a - pos).abs().total_cmp(&(b - pos).abs()));
        let target = if (pos - anchor).abs() < 6. {
            nearest
        } else if pos > anchor {
            snaps.iter().copied().find(|s| *s >= pos - 0.5).or(nearest)
        } else {
            snaps.iter().copied().rev().find(|s| *s <= pos + 0.5).or(nearest)
        };
        match target {
            Some(to) if (to - pos).abs() > 0.5 => self.tools_glide(to, cx),
            Some(to) => self.tools.anchor = to,
            None => {}
        }
    }

    fn tools_wheel(&mut self, cx: &mut Context<Self>) {
        self.tools.glide = None;
        self.tools.settle = Some(cx.spawn(async move |this, cx| {
            cx.background_executor().timer(Duration::from_millis(160)).await;
            this.update(cx, |s, cx| s.tools_settle(cx)).ok();
        }));
    }

    pub(crate) fn tools_page(&mut self, p: Palette, window: &Window, cx: &mut Context<Self>) -> AnyElement {
        let content = self.content_width;
        let card = (content * 0.53).clamp(560., 640.).min(content).floor();
        let art = ((card - 2.) * 9. / 16.)
            .min(f32::from(window.viewport_size().height) - 460.)
            .max(180.)
            .round();
        self.tools.stride = card + GAP;
        let pos = self.tools_pos();
        let max = f32::from(self.tools.scroll.max_offset().x).max(0.);
        let (back, ahead) = (pos > 1., pos < max - 1.);
        let cards: Vec<AnyElement> = tools()
            .iter()
            .enumerate()
            .map(|(i, t)| self.tool_card(i, t, card, art, p, cx))
            .collect();
        let snaps = self.tools_snaps();
        let current = snaps
            .iter()
            .enumerate()
            .min_by(|a, b| (a.1 - pos).abs().total_cmp(&(b.1 - pos).abs()))
            .map_or(0, |(i, _)| i);
        let dots = (snaps.len() > 1).then(|| {
            h_flex()
                .mr(px(10.))
                .gap(px(6.))
                .children(snaps.iter().copied().enumerate().map(|(i, to)| {
                    let on = i == current;
                    div()
                        .id(SharedString::from(format!("tools-dot-{i}")))
                        .h(px(8.))
                        .w(px(if on { 22. } else { 8. }))
                        .rounded(px(4.))
                        .cursor_pointer()
                        .bg(if on { p.accent } else { p.border })
                        .on_click(cx.listener(move |this, _, _, cx| this.tools_glide(to, cx)))
                }))
        });
        let fade = |right: bool| {
            const BANDS: usize = 14;
            h_flex()
                .absolute()
                .top_0()
                .bottom_0()
                .when(right, |d| d.right_0())
                .when(!right, |d| d.left_0())
                .children((1..=BANDS).map(move |i| {
                    let k = if right { i } else { BANDS + 1 - i };
                    div()
                        .h_full()
                        .w(px(6.))
                        .bg(p.background.opacity((k as f32 / BANDS as f32).powf(1.6)))
                }))
        };
        let arrows = h_flex()
            .gap(px(8.))
            .items_center()
            .when(!back && !ahead, |d| d.invisible())
            .children(dots)
            .child(
                Button::new("tools-back")
                    .outline()
                    .icon(IconName::ChevronLeft)
                    .disabled(!back)
                    .on_click(cx.listener(|this, _, _, cx| this.tools_step(false, cx))),
            )
            .child(
                Button::new("tools-ahead")
                    .outline()
                    .icon(IconName::ChevronRight)
                    .disabled(!ahead)
                    .on_click(cx.listener(|this, _, _, cx| this.tools_step(true, cx))),
            );
        v_flex()
            .size_full()
            .child(self.header(
                Page::Tools.label(),
                "Tools that make or edit Swofty addons".to_owned(),
                arrows,
                p,
            ))
            .when(cards.is_empty(), |d| {
                d.child(
                    div()
                        .px(px(GUTTER))
                        .text_color(p.text_muted)
                        .child("No tools listed"),
                )
            })
            .child(
                div()
                    .relative()
                    .w_full()
                    .on_mouse_up(
                        MouseButton::Left,
                        cx.listener(|this, e: &MouseUpEvent, _, cx| {
                            if e.position.y > this.tools.scroll.bounds().bottom() - px(BAR) {
                                this.tools_settle(cx);
                            }
                        }),
                    )
                    .child(
                        h_flex()
                            .id("tools-row")
                            .w_full()
                            .items_stretch()
                            .overflow_x_scroll()
                            .track_scroll(&self.tools.scroll)
                            .on_scroll_wheel(
                                cx.listener(|this, _: &ScrollWheelEvent, _, cx| this.tools_wheel(cx)),
                            )
                            .px(px(GUTTER))
                            .pt(px(4.))
                            .pb(px(BAR))
                            .gap(px(GAP))
                            .children(cards),
                    )
                    .when(ahead, |d| d.child(fade(true)))
                    .when(back, |d| d.child(fade(false)))
                    .child(scrollbar_x(&self.tools.scroll)),
            )
            .into_any_element()
    }
}

#[cfg(test)]
mod tests {
    use super::{art, format, image, tools};

    #[test]
    fn lists_the_tools() {
        let names: Vec<&str> = tools().iter().map(|t| t.name.as_str()).collect();
        assert_eq!(names, ["Simplified Suite", "EGO GameDatabases Editor"]);
        for t in tools() {
            assert!(t.open.is_some() || t.get.is_some(), "{} has no button", t.name);
            assert!(t.about.ends_with('.'), "{}", t.name);
            let file = t.image.as_deref().unwrap_or_default();
            assert!(
                image(file).is_some(),
                "{} has no art at assets/tools/{file}",
                t.name
            );
        }
    }

    #[test]
    fn every_art_file_is_an_image() {
        assert!(!art::ART.is_empty());
        for (name, bytes) in art::ART {
            assert!(
                format(bytes).is_some(),
                "assets/tools/{name} is not a PNG, JPEG or WebP"
            );
        }
    }
}
