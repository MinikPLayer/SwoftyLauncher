use std::cell::Cell;
use std::rc::Rc;

use gpui_kit::component::input::{Input, InputEvent, InputState};
use gpui_kit::component::{Sizable, h_flex, v_flex};
use gpui_kit::prelude::FluentBuilder;
use gpui_kit::*;
use swofty_core::view::{colour_hex, parse_colour};

use crate::theme::palette;

const SQUARE_W: f32 = 236.;
const SQUARE_H: f32 = 156.;
const STRIP_H: f32 = 12.;
const HANDLE: f32 = 14.;
const HUE_HANDLE: f32 = 16.;

pub(crate) enum ColourEvent {
    Commit,
    Cancel,
}

#[derive(Clone, Copy, PartialEq)]
enum Part {
    Square,
    Strip,
}

pub(crate) struct ColourPicker {
    hsv: [f32; 3],
    rgb: [u8; 3],
    focus: FocusHandle,
    inputs: [Entity<InputState>; 4],
    square: Rc<Cell<Bounds<Pixels>>>,
    strip: Rc<Cell<Bounds<Pixels>>>,
    drag: Option<Part>,
    _subs: Vec<Subscription>,
}

impl EventEmitter<ColourEvent> for ColourPicker {}

pub(crate) fn hsv_to_rgb([h, s, v]: [f32; 3]) -> [u8; 3] {
    let h = h.rem_euclid(360.) / 60.;
    let c = v * s;
    let x = c * (1. - (h % 2. - 1.).abs());
    let (r, g, b) = match h as u32 {
        0 => (c, x, 0.),
        1 => (x, c, 0.),
        2 => (0., c, x),
        3 => (0., x, c),
        4 => (x, 0., c),
        _ => (c, 0., x),
    };
    let m = v - c;
    [r, g, b].map(|ch| ((ch + m) * 255.).round().clamp(0., 255.) as u8)
}

pub(crate) fn rgb_to_hsv(rgb: [u8; 3], [h0, s0, _]: [f32; 3]) -> [f32; 3] {
    let [r, g, b] = rgb.map(|c| c as f32 / 255.);
    let max = r.max(g).max(b);
    let d = max - r.min(g).min(b);
    if max == 0. {
        return [h0, s0, 0.];
    }
    if d == 0. {
        return [h0, 0., max];
    }
    let h = if max == r {
        60. * ((g - b) / d).rem_euclid(6.)
    } else if max == g {
        60. * ((b - r) / d + 2.)
    } else {
        60. * ((r - g) / d + 4.)
    };
    [h, d / max, max]
}

pub(crate) fn channel(text: &str) -> Option<u8> {
    text.trim().parse().ok()
}

pub(crate) fn hsla_of([r, g, b]: [u8; 3]) -> Hsla {
    Rgba {
        r: r as f32 / 255.,
        g: g as f32 / 255.,
        b: b as f32 / 255.,
        a: 1.,
    }
    .into()
}

fn texts(rgb: [u8; 3]) -> [String; 4] {
    [
        colour_hex(rgb),
        rgb[0].to_string(),
        rgb[1].to_string(),
        rgb[2].to_string(),
    ]
}

impl ColourPicker {
    pub(crate) fn new(rgb: [u8; 3], window: &mut Window, cx: &mut Context<Self>) -> Self {
        let shown = texts(rgb);
        let inputs = shown.map(|text| cx.new(|cx| InputState::new(window, cx).default_value(text)));
        let mut subs = Vec::new();
        for (i, input) in inputs.iter().enumerate() {
            subs.push(cx.subscribe_in(
                input,
                window,
                move |this, _, ev: &InputEvent, window, cx| match ev {
                    InputEvent::Change => this.typed(i, window, cx),
                    InputEvent::Blur => this.sync_inputs(None, window, cx),
                    InputEvent::PressEnter { .. } => cx.emit(ColourEvent::Commit),
                    _ => {}
                },
            ));
        }
        let focus = cx.focus_handle();
        window.focus(&focus, cx);
        Self {
            hsv: rgb_to_hsv(rgb, [0., 0., 0.]),
            rgb,
            focus,
            inputs,
            square: Rc::default(),
            strip: Rc::default(),
            drag: None,
            _subs: subs,
        }
    }

    pub(crate) fn rgb(&self) -> [u8; 3] {
        self.rgb
    }

    fn typed(&mut self, i: usize, window: &mut Window, cx: &mut Context<Self>) {
        let text = self.inputs[i].read(cx).value().to_string();
        let rgb = match i {
            0 => parse_colour(&text),
            _ => channel(&text).map(|c| {
                let mut rgb = self.rgb;
                rgb[i - 1] = c;
                rgb
            }),
        };
        let Some(rgb) = rgb.filter(|c| *c != self.rgb) else {
            return;
        };
        self.rgb = rgb;
        self.hsv = rgb_to_hsv(rgb, self.hsv);
        self.sync_inputs(Some(i), window, cx);
    }

    fn sync_inputs(&mut self, skip: Option<usize>, window: &mut Window, cx: &mut Context<Self>) {
        for (i, text) in texts(self.rgb).into_iter().enumerate() {
            let input = &self.inputs[i];
            if Some(i) != skip && *input.read(cx).value() != *text {
                input.update(cx, |s, cx| s.set_value(text, window, cx));
            }
        }
        cx.notify();
    }

    fn point_at(&mut self, part: Part, at: Point<Pixels>, window: &mut Window, cx: &mut Context<Self>) {
        let b = match part {
            Part::Square => self.square.get(),
            Part::Strip => self.strip.get(),
        };
        let (w, h) = (f32::from(b.size.width), f32::from(b.size.height));
        if w <= 0. || h <= 0. {
            return;
        }
        let x = (f32::from(at.x - b.left()) / w).clamp(0., 1.);
        let y = (f32::from(at.y - b.top()) / h).clamp(0., 1.);
        match part {
            Part::Square => {
                self.hsv[1] = x;
                self.hsv[2] = 1. - y;
            }
            Part::Strip => self.hsv[0] = x * 360.,
        }
        self.rgb = hsv_to_rgb(self.hsv);
        self.sync_inputs(None, window, cx);
    }

    fn press(&mut self, part: Part, at: Point<Pixels>, window: &mut Window, cx: &mut Context<Self>) {
        self.drag = Some(part);
        window.focus(&self.focus, cx);
        self.point_at(part, at, window, cx);
    }

    fn drag_listener(&self, cx: &mut Context<Self>) -> Option<impl IntoElement> {
        let part = self.drag?;
        let this = cx.entity().downgrade();
        Some(
            canvas(
                |_, _, _| {},
                move |_, _, window, _| {
                    let moved = this.clone();
                    window.on_mouse_event(move |ev: &MouseMoveEvent, phase, window, cx| {
                        if phase != DispatchPhase::Bubble {
                            return;
                        }
                        moved
                            .update(cx, |p, cx| match ev.pressed_button {
                                Some(MouseButton::Left) => p.point_at(part, ev.position, window, cx),
                                _ => {
                                    p.drag = None;
                                    cx.notify();
                                }
                            })
                            .ok();
                    });
                    let released = this.clone();
                    window.on_mouse_event(move |_: &MouseUpEvent, phase, _, cx| {
                        if phase == DispatchPhase::Bubble {
                            released
                                .update(cx, |p, cx| {
                                    p.drag = None;
                                    cx.notify();
                                })
                                .ok();
                        }
                    });
                },
            )
            .absolute()
            .size_full(),
        )
    }
}

fn tracker(bounds: Rc<Cell<Bounds<Pixels>>>) -> impl IntoElement {
    canvas(move |b, _, _| bounds.set(b), |_, _, _, _| {})
        .absolute()
        .size_full()
}

impl Render for ColourPicker {
    fn render(&mut self, _: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        let p = palette(cx);
        let [h, s, v] = self.hsv;
        let white = hsla(0., 0., 1., 1.);
        let black = hsla(0., 0., 0., 1.);
        let hue = hsla_of(hsv_to_rgb([h, 1., 1.]));
        let square = div()
            .id("colour-square")
            .relative()
            .w(px(SQUARE_W))
            .h(px(SQUARE_H))
            .flex_shrink_0()
            .rounded(px(6.))
            .bg(hue)
            .cursor_crosshair()
            .child(div().absolute().inset_0().rounded(px(6.)).bg(linear_gradient(
                90.,
                linear_color_stop(white, 0.),
                linear_color_stop(white.opacity(0.), 1.),
            )))
            .child(div().absolute().inset_0().rounded(px(6.)).bg(linear_gradient(
                180.,
                linear_color_stop(black.opacity(0.), 0.),
                linear_color_stop(black, 1.),
            )))
            .child(tracker(self.square.clone()))
            .child(
                div()
                    .absolute()
                    .left(px(s * SQUARE_W - HANDLE / 2.))
                    .top(px((1. - v) * SQUARE_H - HANDLE / 2.))
                    .size(px(HANDLE))
                    .rounded_full()
                    .border_2()
                    .border_color(white)
                    .shadow_sm(),
            )
            .on_mouse_down(
                MouseButton::Left,
                cx.listener(|this, ev: &MouseDownEvent, window, cx| {
                    this.press(Part::Square, ev.position, window, cx)
                }),
            );
        let stops: Vec<Hsla> = (0..=6)
            .map(|i| hsla_of(hsv_to_rgb([i as f32 * 60., 1., 1.])))
            .collect();
        let strip = div()
            .id("colour-hue")
            .relative()
            .w(px(SQUARE_W))
            .h(px(STRIP_H))
            .flex_shrink_0()
            .cursor_pointer()
            .child(h_flex().size_full().children((0..6).map(|i| {
                div()
                    .h_full()
                    .flex_1()
                    .when(i == 0, |d| d.rounded_l_full())
                    .when(i == 5, |d| d.rounded_r_full())
                    .bg(linear_gradient(
                        90.,
                        linear_color_stop(stops[i], 0.),
                        linear_color_stop(stops[i + 1], 1.),
                    ))
            })))
            .child(tracker(self.strip.clone()))
            .child(
                div()
                    .absolute()
                    .left(px(h / 360. * SQUARE_W - HUE_HANDLE / 2.))
                    .top(px((STRIP_H - HUE_HANDLE) / 2.))
                    .size(px(HUE_HANDLE))
                    .rounded_full()
                    .border_2()
                    .border_color(white)
                    .bg(hue)
                    .shadow_sm(),
            )
            .on_mouse_down(
                MouseButton::Left,
                cx.listener(|this, ev: &MouseDownEvent, window, cx| {
                    this.press(Part::Strip, ev.position, window, cx)
                }),
            );
        let column = |label: &'static str, input: &Entity<InputState>, wide: bool| {
            v_flex()
                .gap(px(4.))
                .map(|d| if wide { d.flex_1().min_w_0() } else { d.w(px(38.)) })
                .child(
                    div()
                        .text_size(px(10.))
                        .font_weight(FontWeight::MEDIUM)
                        .text_color(p.text_placeholder)
                        .child(label),
                )
                .child(Input::new(input).xsmall())
        };
        let boxes = h_flex()
            .w(px(SQUARE_W))
            .items_end()
            .gap(px(6.))
            .child(
                div()
                    .size(px(22.))
                    .mb(px(1.))
                    .flex_shrink_0()
                    .rounded_full()
                    .border_1()
                    .border_color(white.opacity(0.12))
                    .bg(hsla_of(self.rgb)),
            )
            .child(column("HEX", &self.inputs[0], true))
            .child(column("R", &self.inputs[1], false))
            .child(column("G", &self.inputs[2], false))
            .child(column("B", &self.inputs[3], false));
        v_flex()
            .id("colour-picker")
            .track_focus(&self.focus)
            .relative()
            .p(px(12.))
            .gap(px(12.))
            .rounded(px(8.))
            .border_1()
            .border_color(p.border)
            .bg(p.surface)
            .shadow_lg()
            .capture_key_down(cx.listener(|_, ev: &KeyDownEvent, _, cx| {
                let event = match ev.keystroke.key.as_str() {
                    "enter" => ColourEvent::Commit,
                    "escape" => ColourEvent::Cancel,
                    _ => return,
                };
                cx.stop_propagation();
                cx.emit(event);
            }))
            .child(square)
            .child(strip)
            .child(boxes)
            .children(self.drag_listener(cx))
    }
}

#[cfg(test)]
mod tests {
    use swofty_core::view::parse_colour;

    use super::{channel, hsv_to_rgb, rgb_to_hsv, texts};

    #[test]
    fn byte_colours_round_trip_through_hsv() {
        let steps: Vec<u8> = (0..=255u8)
            .step_by(15)
            .chain([1, 2, 127, 128, 254, 255])
            .collect();
        for &r in &steps {
            for &g in &steps {
                for &b in &steps {
                    let hsv = rgb_to_hsv([r, g, b], [0., 0., 0.]);
                    assert_eq!(hsv_to_rgb(hsv), [r, g, b], "{hsv:?}");
                }
            }
        }
        assert_eq!(rgb_to_hsv([255, 0, 0], [0., 0., 0.]), [0., 1., 1.]);
        assert_eq!(hsv_to_rgb([360., 1., 1.]), [255, 0, 0]);
        assert_eq!(hsv_to_rgb([120., 1., 1.]), [0, 255, 0]);
    }

    #[test]
    fn greys_and_black_keep_the_hue_and_saturation_handles() {
        let teal = rgb_to_hsv([0x00, 0xA1, 0x9C], [0., 0., 0.]);
        let grey = rgb_to_hsv([0x80, 0x80, 0x80], teal);
        assert_eq!((grey[0], grey[1]), (teal[0], 0.));
        assert_eq!(hsv_to_rgb(grey), [0x80, 0x80, 0x80]);
        let black = rgb_to_hsv([0, 0, 0], teal);
        assert_eq!((black[0], black[1], black[2]), (teal[0], teal[1], 0.));
        assert_eq!(hsv_to_rgb(black), [0, 0, 0]);
        let white = rgb_to_hsv([255, 255, 255], black);
        assert_eq!((white[0], white[1], white[2]), (teal[0], 0., 1.));
    }

    #[test]
    fn the_hex_and_channel_boxes_parse() {
        assert_eq!(parse_colour("#00a19c"), Some([0x00, 0xA1, 0x9C]));
        assert_eq!(parse_colour("1B62C7"), Some([0x1B, 0x62, 0xC7]));
        assert_eq!(parse_colour("#0f0"), Some([0, 255, 0]));
        assert_eq!(parse_colour("#00a19"), None);
        assert_eq!(channel("161"), Some(161));
        assert_eq!(channel(" 0 "), Some(0));
        for bad in ["", "256", "-1", "1.5", "ff"] {
            assert_eq!(channel(bad), None, "{bad}");
        }
        assert_eq!(texts([0x00, 0xA1, 0x9C]), ["#00A19C", "0", "161", "156"]);
    }
}
