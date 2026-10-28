// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <string_view>
#include <utility>
#include <swofty/overlay.hpp>

// A small immediate-mode toolkit on the overlay: panels, labels, buttons, toggles, sliders, lists, text
// fields and toasts. Keep one ui::State per addon (it outlives frames) and make a ui::Ui at the top of
// each paint callback. Rects are framebuffer pixels; theme sizes are for 1920x1080 and scale with
// ui_scale(). Widgets react only while the addon holds input focus (focus(true)), to the mouse, the
// keyboard and the gamepad: up/down (d-pad, left stick, arrow keys) and LB/RB or Tab move between
// widgets, A/Enter/Space activates, left/right and the triggers move a slider, B/Esc is back().

namespace swofty::overlay::ui {

enum class ToastAt { left_middle, top_center, top_right };

struct Theme {
    uint32_t panel_top = 0x16161ef2, panel_bottom = 0x24242ff0, edge = 0xffffff24, accent = 0xe10600ff;
    uint32_t text = 0xffffffff, dim = 0xffffffa8, hover = 0xffffff18, press = 0xffffff2e, track = 0xffffff2a;
    uint32_t toast = 0x111118f2, field = 0x0000006a, selection = 0xe1060070;
    float radius = 10, title = 22, body = 18, pad = 14, row = 34;
    ToastAt toast_at = ToastAt::left_middle;
};

struct Toast {
    std::string text;
    uint64_t start = 0, until = 0;
};

struct State {
    uint32_t hot = 0, active = 0;
    int nav = -1, nav_count = 0;
    bool nav_shown = false;
    float last_x = -1, last_y = -1;
    uint64_t last_ms = 0;
    uint32_t edit = 0;
    size_t caret = 0, anchor = 0;
    float edit_scroll = 0;
    uint64_t blink = 0;
    std::deque<Toast> toasts;
};

inline uint32_t id_of(std::string_view s)
{
    uint32_t h = 2166136261u;
    for (unsigned char c : s)
        h = (h ^ c) * 16777619u;
    return h ? h : 1;
}

inline uint64_t now_ms()
{
    return GetTickCount64();
}

class Ui {
  public:
    Ui(Paint *paint, State &state, const Theme &theme = {})
        : p(paint), st(state), th(theme), s(ui_scale(paint)), in(input(paint))
    {
        uint64_t now = now_ms();
        dt = st.last_ms ? std::min(0.1f, float(now - st.last_ms) / 1000.0f) : 0.016f;
        st.last_ms = now;
        st.hot = 0;
        if (!in.focused) {
            st.active = 0;
            st.edit = 0;
            st.nav_shown = false;
            return;
        }
        if (in.x != st.last_x || in.y != st.last_y) {
            if (st.last_x >= 0)
                st.nav_shown = false;
            st.last_x = in.x;
            st.last_y = in.y;
        }
        bool kb = st.edit == 0, shift = in.key_down(VK_SHIFT);
        up = in.pad_hit(pad::up) || (kb && in.key_typed(VK_UP));
        down = in.pad_hit(pad::down) || (kb && in.key_typed(VK_DOWN));
        left = in.pad_hit(pad::left) || (kb && in.key_typed(VK_LEFT));
        right = in.pad_hit(pad::right) || (kb && in.key_typed(VK_RIGHT));
        accept = in.pad_hit(pad::a) || (kb && (in.key_pressed(VK_RETURN) || in.key_pressed(VK_SPACE)));
        back = in.pad_hit(pad::b) || (kb && in.key_pressed(VK_ESCAPE));
        next = in.pad_hit(pad::rb) || (kb && in.key_typed(VK_TAB) && !shift);
        prev = in.pad_hit(pad::lb) || (kb && in.key_typed(VK_TAB) && shift);
        float trig = in.pad_rt - in.pad_lt;
        if (up || down || left || right || accept || next || prev || trig != 0) {
            if (!st.nav_shown || st.nav < 0) {
                fresh = true;
                if (st.nav < 0)
                    st.nav = 0;
            }
            st.nav_shown = true;
        }
    }

    ~Ui()
    {
        st.nav_count = slot;
        if (!slot) {
            st.nav = -1;
            return;
        }
        if (st.nav >= slot)
            st.nav = slot - 1;
        if (fresh || st.nav < 0 || !st.nav_shown)
            return;
        if ((down && !consumed_v) || next)
            st.nav = (st.nav + 1) % slot;
        else if ((up && !consumed_v) || prev)
            st.nav = (st.nav + slot - 1) % slot;
    }

    Ui(const Ui &) = delete;
    Ui &operator=(const Ui &) = delete;

    Paint *p;
    State &st;
    Theme th;
    float s;
    Input in;
    float dt = 0;
    bool up = false, down = false, left = false, right = false, accept = false, back = false, next = false,
         prev = false;
    bool consumed_v = false, fresh = false;
    int slot = 0;

    float u(float v) const { return v * s; }

    bool over(const Rect &r) const { return in.focused && in.inside(r.x, r.y, r.w, r.h); }

    int nav_slot() { return slot++; }

    bool nav_on(int idx) const { return in.focused && st.nav_shown && st.nav == idx; }

    // Hot/active bookkeeping for one widget: returns true on the frame a press inside it is released
    // inside it. A press also moves keyboard/gamepad navigation to the widget.
    bool interact(uint32_t id, const Rect &r, int idx = -1)
    {
        bool inside = over(r);
        if (inside && (!st.active || st.active == id))
            st.hot = id;
        if (inside && in.clicked()) {
            st.active = id;
            if (idx >= 0)
                st.nav = idx;
        }
        bool click = false;
        if (st.active == id && (in.released & mouse::left)) {
            click = inside;
            st.active = 0;
        }
        return click;
    }

    void ring(const Rect &r) const
    {
        rect_rounded_outline(p, r.x - u(3), r.y - u(3), r.w + u(6), r.h + u(6), u(th.radius * 0.6f + 3), u(2), th.accent);
    }

    TextStyle style(float size, uint32_t rgba, uint32_t align) const
    {
        TextStyle t;
        t.size = u(size);
        t.rgba = rgba;
        t.align = align;
        return t;
    }
};

inline Rect inset(const Rect &r, float d)
{
    return {r.x + d, r.y + d, std::max(0.0f, r.w - 2 * d), std::max(0.0f, r.h - 2 * d)};
}

// A panel with a title bar; returns the content rect under the title.
inline Rect panel(Ui &ui, const Rect &r, std::string_view title)
{
    const Theme &th = ui.th;
    gradient(ui.p, r.x, r.y, r.w, r.h, th.panel_top, th.panel_bottom, false, ui.u(th.radius));
    rect_rounded_outline(ui.p, r.x, r.y, r.w, r.h, ui.u(th.radius), ui.u(1), th.edge);
    float pad = ui.u(th.pad);
    if (title.empty())
        return inset(r, pad);
    float bar = ui.u(th.title * 1.8f);
    TextStyle t = ui.style(th.title, th.text, align::left | align::middle);
    text(ui.p, r.x + pad, r.y + bar / 2, t, title);
    rect(ui.p, r.x + pad, r.y + bar - ui.u(2), ui.u(36), ui.u(3), th.accent);
    return {r.x + pad, r.y + bar + pad / 2, std::max(0.0f, r.w - 2 * pad), std::max(0.0f, r.h - bar - pad * 1.5f)};
}

inline void label(Ui &ui, const Rect &r, std::string_view s, uint32_t rgba = 0, uint32_t a = align::left | align::middle)
{
    text_in(ui.p, r, ui.style(ui.th.body, rgba ? rgba : ui.th.text, a), s);
}

inline bool button(Ui &ui, const Rect &r, std::string_view label_text)
{
    uint32_t id = id_of(label_text) ^ 0x9e3779b9u;
    int idx = ui.nav_slot();
    bool nav = ui.nav_on(idx);
    bool click = ui.interact(id, r, idx) || (nav && ui.accept);
    const Theme &th = ui.th;
    float rr = ui.u(th.radius * 0.6f);
    rect_rounded(ui.p, r.x, r.y, r.w, r.h, rr, 0xffffff10);
    if (ui.st.active == id)
        rect_rounded(ui.p, r.x, r.y, r.w, r.h, rr, th.press);
    else if (ui.st.hot == id || nav)
        rect_rounded(ui.p, r.x, r.y, r.w, r.h, rr, th.hover);
    rect_rounded_outline(ui.p, r.x, r.y, r.w, r.h, rr, ui.u(1.5f), ui.st.hot == id ? th.accent : th.edge);
    if (nav)
        ui.ring(r);
    label(ui, r, label_text, th.text, align::center | align::middle);
    return click;
}

inline bool toggle(Ui &ui, const Rect &r, std::string_view label_text, bool &on)
{
    uint32_t id = id_of(label_text) ^ 0x7f4a7c15u;
    int idx = ui.nav_slot();
    bool nav = ui.nav_on(idx);
    bool click = ui.interact(id, r, idx) || (nav && ui.accept);
    if (click)
        on = !on;
    const Theme &th = ui.th;
    if (ui.st.hot == id || nav)
        rect_rounded(ui.p, r.x, r.y, r.w, r.h, ui.u(6), th.hover);
    if (nav)
        ui.ring(r);
    float h = std::min(r.h * 0.7f, ui.u(24)), w = h * 1.8f, x = r.x + r.w - w - ui.u(6), y = r.y + (r.h - h) / 2;
    rect_rounded(ui.p, x, y, w, h, h / 2, on ? th.accent : th.track);
    circle(ui.p, on ? x + w - h / 2 : x + h / 2, y + h / 2, h / 2 - ui.u(3), th.text);
    label(ui, {r.x + ui.u(6), r.y, r.w - w - ui.u(18), r.h}, label_text);
    return click;
}

// A horizontal slider: the label and the value (printed with fmt) above a track. Drag it, use the wheel
// over it, or with navigation on it press left/right (5% steps) or hold the triggers. Returns true while
// the value changes.
inline bool slider(Ui &ui, const Rect &r, std::string_view label_text, float &value, float lo, float hi,
                   const char *fmt = "%.0f")
{
    uint32_t id = id_of(label_text) ^ 0x2545f491u;
    int idx = ui.nav_slot();
    bool nav = ui.nav_on(idx);
    const Theme &th = ui.th;
    float top = r.h * 0.5f, ty = r.y + top + (r.h - top) / 2, knob = ui.u(9);
    Rect track{r.x + knob, ty - ui.u(3), r.w - 2 * knob, ui.u(6)};
    Rect hit{r.x, r.y + top, r.w, r.h - top};
    ui.interact(id, hit, idx);
    float before = value, a = std::min(lo, hi), b = std::max(lo, hi);
    if (ui.st.active == id && ui.in.held() && track.w > 0) {
        float t = std::clamp((ui.in.x - track.x) / track.w, 0.0f, 1.0f);
        value = lo + (hi - lo) * t;
    }
    if (ui.st.hot == id && ui.in.wheel != 0)
        value = std::clamp(value + (hi - lo) * 0.05f * ui.in.wheel, a, b);
    if (nav) {
        if (ui.left || ui.right)
            value = std::clamp(value + (hi - lo) * 0.05f * (ui.right ? 1.0f : -1.0f), a, b);
        float trig = ui.in.pad_rt - ui.in.pad_lt;
        if (trig != 0)
            value = std::clamp(value + (hi - lo) * 0.5f * trig * ui.dt, a, b);
    }
    float t = hi != lo ? std::clamp((value - lo) / (hi - lo), 0.0f, 1.0f) : 0;
    if (nav)
        ui.ring(r);
    label(ui, {r.x, r.y, r.w, top}, label_text, th.dim);
    char buf[48];
    snprintf(buf, sizeof buf, fmt, double(value));
    label(ui, {r.x, r.y, r.w, top}, buf, th.text, align::right | align::middle);
    rect_rounded(ui.p, track.x, track.y, track.w, track.h, track.h / 2, th.track);
    rect_rounded(ui.p, track.x, track.y, track.w * t, track.h, track.h / 2, th.accent);
    float kx = track.x + track.w * t;
    circle(ui.p, kx, ty, ui.st.active == id || ui.st.hot == id || nav ? knob * 1.15f : knob, th.text);
    return value != before;
}

// A scrolling list of count rows; item(i) returns row i's text. The wheel scrolls, a click selects, and
// with navigation on it up/down move the selection until the first or last row. Returns true when the
// selection changes.
template <class ItemFn>
bool list(Ui &ui, const Rect &r, std::string_view id_text, int count, ItemFn &&item, int &selected, float &scroll)
{
    uint32_t base = id_of(id_text);
    int idx = ui.nav_slot();
    bool nav = ui.nav_on(idx);
    const Theme &th = ui.th;
    float row = ui.u(th.row);
    float content = row * float(std::max(count, 0)), max_scroll = std::max(0.0f, content - r.h);
    int before = selected;
    if (nav && count > 0) {
        if (ui.down && selected < count - 1) {
            selected = std::max(selected + 1, 0);
            ui.consumed_v = true;
        } else if (ui.up && selected > 0) {
            selected--;
            ui.consumed_v = true;
        }
        if (selected != before) {
            float y = float(selected) * row;
            if (y < scroll)
                scroll = y;
            else if (y + row > scroll + r.h)
                scroll = y + row - r.h;
        }
    }
    if (ui.over(r) && ui.in.wheel != 0)
        scroll -= ui.in.wheel * row * 3;
    scroll = std::clamp(scroll, 0.0f, max_scroll);
    rect_rounded(ui.p, r.x, r.y, r.w, r.h, ui.u(6), 0x00000040);
    if (nav)
        ui.ring(r);
    {
        ClipScope clip(ui.p, r.x, r.y, r.w, r.h);
        int first = int(scroll / row), last = std::min(count, int((scroll + r.h) / row) + 1);
        for (int i = std::max(first, 0); i < last; i++) {
            Rect rr{r.x, r.y + float(i) * row - scroll, r.w - ui.u(8), row};
            uint32_t id = base + uint32_t(i) * 2654435761u;
            if (ui.interact(id, rr, idx) && ui.over(r))
                selected = i;
            if (i == selected) {
                rect(ui.p, rr.x, rr.y, rr.w, rr.h, 0xffffff14);
                rect(ui.p, rr.x, rr.y, ui.u(3), rr.h, th.accent);
            } else if (ui.st.hot == id) {
                rect(ui.p, rr.x, rr.y, rr.w, rr.h, th.hover);
            }
            label(ui, {rr.x + ui.u(12), rr.y, rr.w - ui.u(16), rr.h}, item(i), i == selected ? th.text : th.dim);
        }
    }
    if (max_scroll > 0) {
        float h = std::max(ui.u(20), r.h * r.h / content), y = r.y + (r.h - h) * (scroll / max_scroll);
        rect_rounded(ui.p, r.x + r.w - ui.u(5), y, ui.u(3), h, ui.u(1.5f), th.track);
    }
    return selected != before;
}

// ----- text fields ---------------------------------------------------------------------------------

inline size_t utf8_prev(std::string_view s, size_t i)
{
    while (i > 0 && (static_cast<unsigned char>(s[--i]) & 0xc0) == 0x80) {
    }
    return i;
}

inline size_t utf8_next(std::string_view s, size_t i)
{
    if (i < s.size())
        i++;
    while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xc0) == 0x80)
        i++;
    return i;
}

inline void utf8_put(std::string &out, uint32_t cp)
{
    if (cp < 0x80) {
        out += char(cp);
    } else if (cp < 0x800) {
        out += char(0xc0 | cp >> 6);
        out += char(0x80 | (cp & 0x3f));
    } else if (cp < 0x10000) {
        out += char(0xe0 | cp >> 12);
        out += char(0x80 | (cp >> 6 & 0x3f));
        out += char(0x80 | (cp & 0x3f));
    } else {
        out += char(0xf0 | cp >> 18);
        out += char(0x80 | (cp >> 12 & 0x3f));
        out += char(0x80 | (cp >> 6 & 0x3f));
        out += char(0x80 | (cp & 0x3f));
    }
}

// The clipboard's text as UTF-8 (one line: line breaks become spaces), or empty.
inline std::string clipboard_text()
{
    std::string out;
    if (!OpenClipboard(nullptr))
        return out;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (auto *w = static_cast<const wchar_t *>(GlobalLock(h))) {
            int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
            if (n > 1) {
                out.resize(size_t(n));
                WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr);
                out.resize(size_t(n - 1));
            }
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    for (char &c : out)
        if (c == '\r' || c == '\n' || c == '\t')
            c = ' ';
    return out;
}

inline bool set_clipboard_text(std::string_view utf8)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), nullptr, 0);
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (size_t(n) + 1) * sizeof(wchar_t));
    if (!g)
        return false;
    auto *w = static_cast<wchar_t *>(GlobalLock(g));
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), w, n);
    w[n] = 0;
    GlobalUnlock(g);
    if (!OpenClipboard(nullptr)) {
        GlobalFree(g);
        return false;
    }
    EmptyClipboard();
    bool ok = SetClipboardData(CF_UNICODETEXT, g) != nullptr;
    CloseClipboard();
    if (!ok)
        GlobalFree(g);
    return ok;
}

// A one-line text field holding up to max_bytes of UTF-8. Click it (or press A/Enter with navigation
// on it) to edit: typing inserts at the caret, Backspace/Delete remove, Left/Right/Home/End move the
// caret (with Shift they select), Ctrl+A selects all, Ctrl+C/X/V copy, cut and paste, and Enter, Esc,
// Tab or a click outside end editing. Returns true when the text changes.
inline bool text_field(Ui &ui, const Rect &r, std::string_view id_text, std::string &value, size_t max_bytes = 128,
                       std::string_view placeholder = {})
{
    uint32_t id = id_of(id_text) ^ 0x51ed27f3u;
    int idx = ui.nav_slot();
    bool nav = ui.nav_on(idx);
    State &st = ui.st;
    const Theme &th = ui.th;
    float pad = ui.u(10);
    TextStyle ts = ui.style(th.body, th.text, align::left | align::middle);
    auto width_to = [&](size_t n) {
        float w = 0;
        measure(ui.p, ts, std::string_view(value).substr(0, n), &w, nullptr);
        return w;
    };
    auto caret_at = [&](float x) {
        float local = x - (r.x + pad) + st.edit_scroll;
        size_t best = 0;
        for (size_t i = 0;;) {
            float w = width_to(i);
            if (w > local) {
                size_t pi = utf8_prev(value, i);
                if (i > 0 && local - width_to(pi) < w - local)
                    return pi;
                return i;
            }
            best = i;
            if (i >= value.size())
                break;
            i = utf8_next(value, i);
        }
        return best;
    };
    bool inside = ui.over(r);
    bool editing = st.edit == id, started = false;
    bool changed = false;
    uint64_t now = now_ms();
    if (inside && ui.in.clicked()) {
        if (!editing) {
            st.edit = id;
            st.edit_scroll = 0;
            editing = started = true;
        }
        st.caret = st.anchor = caret_at(ui.in.x);
        st.blink = now;
        st.nav = idx;
    } else if (editing && ui.in.clicked() && !inside) {
        st.edit = 0;
        editing = false;
    } else if (!editing && nav && ui.accept) {
        st.edit = id;
        st.caret = st.anchor = value.size();
        st.edit_scroll = 0;
        st.blink = now;
        editing = started = true;
    }
    if (editing && st.active == 0 && ui.in.held() && inside)
        st.caret = caret_at(ui.in.x);
    if (editing) {
        st.caret = std::min(st.caret, value.size());
        st.anchor = std::min(st.anchor, value.size());
        const Input &in = ui.in;
        bool ctrl = in.key_down(VK_CONTROL), shift = in.key_down(VK_SHIFT);
        auto sel = [&] { return std::pair{std::min(st.caret, st.anchor), std::max(st.caret, st.anchor)}; };
        auto erase_sel = [&] {
            auto [a, b] = sel();
            if (a == b)
                return false;
            value.erase(a, b - a);
            st.caret = st.anchor = a;
            return true;
        };
        auto insert = [&](std::string_view text) {
            erase_sel();
            size_t room = max_bytes > value.size() ? max_bytes - value.size() : 0;
            size_t n = std::min(room, text.size());
            while (n > 0 && n < text.size() && (static_cast<unsigned char>(text[n]) & 0xc0) == 0x80)
                n--;
            value.insert(st.caret, text.substr(0, n));
            st.caret = st.anchor = st.caret + n;
            changed = changed || n > 0;
        };
        auto move = [&](size_t to) {
            st.caret = to;
            if (!shift)
                st.anchor = to;
        };
        std::string typed;
        for (uint32_t i = 0; i < in.char_count; i++)
            utf8_put(typed, in.chars[i]);
        if (!typed.empty())
            insert(typed);
        if (in.key_typed(VK_BACK)) {
            if (!erase_sel() && st.caret > 0) {
                size_t a = utf8_prev(value, st.caret);
                value.erase(a, st.caret - a);
                st.caret = st.anchor = a;
            }
            changed = true;
        }
        if (in.key_typed(VK_DELETE)) {
            if (!erase_sel() && st.caret < value.size())
                value.erase(st.caret, utf8_next(value, st.caret) - st.caret);
            changed = true;
        }
        if (in.key_typed(VK_LEFT))
            move(!shift && st.caret != st.anchor ? sel().first : utf8_prev(value, st.caret));
        if (in.key_typed(VK_RIGHT))
            move(!shift && st.caret != st.anchor ? sel().second : utf8_next(value, st.caret));
        if (in.key_typed(VK_HOME))
            move(0);
        if (in.key_typed(VK_END))
            move(value.size());
        if (ctrl && in.key_pressed('A')) {
            st.anchor = 0;
            st.caret = value.size();
        }
        if (ctrl && (in.key_pressed('C') || in.key_pressed('X'))) {
            auto [a, b] = sel();
            if (a != b) {
                set_clipboard_text(std::string_view(value).substr(a, b - a));
                if (in.key_pressed('X'))
                    changed = erase_sel() || changed;
            }
        }
        if (ctrl && in.key_typed('V'))
            insert(clipboard_text());
        if (!started &&
            (in.key_pressed(VK_RETURN) || in.key_pressed(VK_ESCAPE) || in.key_pressed(VK_TAB) || in.pad_hit(pad::b))) {
            st.edit = 0;
            editing = false;
        }
        if (in.char_count || in.key_typed(VK_BACK) || in.key_typed(VK_DELETE) || in.key_typed(VK_LEFT) ||
            in.key_typed(VK_RIGHT))
            st.blink = now;
    }
    rect_rounded(ui.p, r.x, r.y, r.w, r.h, ui.u(6), th.field);
    rect_rounded_outline(ui.p, r.x, r.y, r.w, r.h, ui.u(6), ui.u(1.5f),
                         editing ? th.accent : ui.st.hot == id || inside ? 0xffffff50 : th.edge);
    if (nav && !editing)
        ui.ring(r);
    Rect inner{r.x + pad, r.y, std::max(0.0f, r.w - 2 * pad), r.h};
    if (editing) {
        float cx = width_to(st.caret);
        if (cx - st.edit_scroll > inner.w)
            st.edit_scroll = cx - inner.w;
        if (cx - st.edit_scroll < 0)
            st.edit_scroll = cx;
    }
    float scroll = editing ? st.edit_scroll : 0;
    {
        ClipScope clip(ui.p, inner.x, inner.y, inner.w, inner.h);
        if (editing && st.caret != st.anchor) {
            size_t a = std::min(st.caret, st.anchor), b = std::max(st.caret, st.anchor);
            float x0 = width_to(a), x1 = width_to(b), h = ts.size * 1.1f;
            rect(ui.p, inner.x + x0 - scroll, r.y + (r.h - h) / 2, x1 - x0, h, th.selection);
        }
        if (value.empty() && !editing && !placeholder.empty()) {
            TextStyle ph = ts;
            ph.rgba = th.dim;
            text(ui.p, inner.x, r.y + r.h / 2, ph, placeholder);
        } else {
            text(ui.p, inner.x - scroll, r.y + r.h / 2, ts, value);
        }
        if (editing && (now - st.blink) % 1060 < 530) {
            float cx = width_to(st.caret), h = ts.size * 1.1f;
            rect(ui.p, inner.x + cx - scroll, r.y + (r.h - h) / 2, std::max(1.0f, ui.u(2)), h, th.text);
        }
    }
    return changed;
}

// ----- toasts ---------------------------------------------------------------------------------------

// Queue a toast for seconds; draw_toasts() shows the queue.
inline void toast(State &st, std::string text_utf8, float seconds = 3)
{
    uint64_t now = now_ms();
    st.toasts.push_back({std::move(text_utf8), now, now + uint64_t(std::max(0.5f, seconds) * 1000)});
    while (st.toasts.size() > 5)
        st.toasts.pop_front();
}

// Toasts stack down from theme.toast_at, sliding in and fading out. The default, left_middle, starts
// under the game's standings tower on the left edge of the safe area; top_center starts under the top
// edge; top_right starts at the top right corner, over the game's timing box.
inline void draw_toasts(Ui &ui)
{
    uint64_t now = now_ms();
    while (!ui.st.toasts.empty() && ui.st.toasts.front().until <= now)
        ui.st.toasts.pop_front();
    if (ui.st.toasts.empty())
        return;
    const Theme &th = ui.th;
    Rect safe = safe_area(ui.p);
    float w = ui.u(380);
    float x0 = th.toast_at == ToastAt::left_middle ? safe.x
               : th.toast_at == ToastAt::top_center ? safe.x + (safe.w - w) / 2
                                                    : safe.x + safe.w - w;
    float y = th.toast_at == ToastAt::left_middle ? safe.y + safe.h * 0.4f
              : th.toast_at == ToastAt::top_center ? safe.y + ui.u(90)
                                                   : safe.y;
    float dir = th.toast_at == ToastAt::left_middle ? -1.0f : th.toast_at == ToastAt::top_right ? 1.0f : 0.0f;
    TextStyle t = ui.style(th.body, th.text, align::left | align::top);
    t.max_width = w - ui.u(th.pad * 2 + 6);
    for (const Toast &e : ui.st.toasts) {
        float tw = 0, h = 0;
        measure(ui.p, t, e.text, &tw, &h);
        h += ui.u(th.pad * 2);
        float in_t = std::min(1.0f, float(now - e.start) / 180.0f), out_t = std::min(1.0f, float(e.until - now) / 300.0f);
        float a = std::min(in_t, out_t), slide = (1 - in_t) * ui.u(40) * dir;
        float x = x0 + slide, yy = y - (dir == 0 ? (1 - in_t) * ui.u(16) : 0);
        OpacityScope fade(ui.p, a);
        rect_rounded(ui.p, x, yy, w, h, ui.u(8), th.toast);
        rect_rounded(ui.p, x, yy, ui.u(5), h, ui.u(2.5f), th.accent);
        text(ui.p, x + ui.u(th.pad + 6), yy + ui.u(th.pad), t, e.text);
        y += (h + ui.u(10)) * std::max(a, 0.25f);
    }
}

}
