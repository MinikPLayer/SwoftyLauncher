// Windows unit test for the overlay toolkit (packages/sdk/include/swofty/overlay_ui.hpp), run under Wine
// by native/test/run_toolkit_test.sh. A fake core table feeds each frame's input and records what the
// widgets draw; a stub swofty_core.dll exports SwoftyCoreOverlayVersion so the SDK sees overlay v3.

#include <swofty/overlay_ui.hpp>

#include <cstdio>
#include <string>
#include <vector>

namespace ov = swofty::overlay;
namespace ui = swofty::overlay::ui;

static int failures;

#define CHECK(cond, msg)                                                                                                \
    do {                                                                                                                \
        if (cond) {                                                                                                     \
            printf("ok  : %s\n", msg);                                                                                  \
        } else {                                                                                                        \
            printf("FAIL: %s\n", msg);                                                                                  \
            failures++;                                                                                                 \
        }                                                                                                               \
    } while (0)

static SwoftyInput next_in;
static std::vector<std::string> texts;
static std::vector<ov::Rect> panels;
static int focus_calls;

static SwoftyOverlay make_table()
{
    SwoftyOverlay t{};
    t.available = [] { return 1; };
    t.size = [](const SwoftyPaint *, float *w, float *h) {
        *w = 1920;
        *h = 1080;
    };
    t.rect = [](SwoftyPaint *, float, float, float, float, uint32_t) {};
    t.rect_outline = [](SwoftyPaint *, float, float, float, float, float, uint32_t) {};
    t.line = [](SwoftyPaint *, float, float, float, float, float, uint32_t) {};
    t.text = [](SwoftyPaint *, float, float, float, uint32_t, const char *s) { texts.push_back(s); };
    t.measure = [](const SwoftyPaint *, float size, const char *s, float *w, float *h) {
        if (w)
            *w = 10.0f * float(strlen(s));
        if (h)
            *h = size;
    };
    t.rect_rounded = [](SwoftyPaint *, float x, float y, float w, float h, float, uint32_t) {
        panels.push_back({x, y, w, h});
    };
    t.rect_rounded_outline = [](SwoftyPaint *, float, float, float, float, float, float, uint32_t) {};
    t.rect_gradient = [](SwoftyPaint *, float, float, float, float, float, uint32_t, uint32_t, uint32_t) {};
    t.circle = [](SwoftyPaint *, float, float, float, uint32_t) {};
    t.circle_outline = [](SwoftyPaint *, float, float, float, float, uint32_t) {};
    t.arc = [](SwoftyPaint *, float, float, float, float, float, float, uint32_t) {};
    t.polyline = [](SwoftyPaint *, const float *, uint32_t, float, uint32_t, uint32_t) {};
    t.polygon = [](SwoftyPaint *, const float *, uint32_t, uint32_t) {};
    t.text_styled = [](SwoftyPaint *, float, float, const SwoftyTextStyle *, const char *s) { texts.push_back(s); };
    t.measure_styled = [](const SwoftyPaint *, const SwoftyTextStyle *st, const char *s, float *w, float *h) {
        if (w)
            *w = 10.0f * float(strlen(s));
        if (h)
            *h = st->px;
    };
    t.push_clip = [](SwoftyPaint *, float, float, float, float) {};
    t.pop_clip = [](SwoftyPaint *) {};
    t.push_transform = [](SwoftyPaint *, const float *) {};
    t.pop_transform = [](SwoftyPaint *) {};
    t.push_opacity = [](SwoftyPaint *, float) {};
    t.pop_opacity = [](SwoftyPaint *) {};
    t.safe_area = [](const SwoftyPaint *, float *x, float *y, float *w, float *h) {
        *x = 67;
        *y = 38;
        *w = 1786;
        *h = 1004;
    };
    t.ui_scale = [](const SwoftyPaint *) { return 1.0f; };
    t.input = [](const SwoftyPaint *, SwoftyInput *out) {
        uint32_t size = out->size;
        *out = next_in;
        out->size = size;
        return 0;
    };
    t.focus = [](int) {
        focus_calls++;
        return 0;
    };
    return t;
}

static SwoftyOverlay table = make_table();
static SwoftyApi api_table;
static SwoftyPaint *const paint = reinterpret_cast<SwoftyPaint *>(&api_table);

static void bit(uint8_t *bits, uint32_t vk)
{
    bits[vk >> 3] |= uint8_t(1u << (vk & 7));
}

struct In {
    SwoftyInput s{};
    In()
    {
        s.size = sizeof s;
        s.focused = 1;
        s.x = s.y = 5;
    }
    In &press(uint32_t vk)
    {
        bit(s.keys_pressed, vk);
        bit(s.keys_typed, vk);
        bit(s.keys_down, vk);
        return *this;
    }
    In &down(uint32_t vk)
    {
        bit(s.keys_down, vk);
        return *this;
    }
    In &type(const char *t)
    {
        for (; *t && s.char_count < 16; t++)
            s.chars[s.char_count++] = uint32_t(uint8_t(*t));
        return *this;
    }
    In &pad(uint32_t b)
    {
        s.pad_buttons |= b;
        s.pad_pressed |= b;
        return *this;
    }
    In &at(float x, float y)
    {
        s.x = x;
        s.y = y;
        return *this;
    }
    In &click()
    {
        s.buttons |= SWOFTY_MOUSE_LEFT;
        s.pressed |= SWOFTY_MOUSE_LEFT;
        return *this;
    }
    In &release()
    {
        s.released |= SWOFTY_MOUSE_LEFT;
        return *this;
    }
};

struct Panel {
    ui::State st;
    bool on = false;
    float level = 50;
    int selected = 0;
    float scroll = 0;
    unsigned clicks = 0;
    std::string name;
    bool back = false;
    ov::Rect field{100, 600, 300, 40};

    void frame(const In &in, ui::Theme theme = {})
    {
        next_in = in.s;
        texts.clear();
        panels.clear();
        ui::Ui u(paint, st, theme);
        back = u.back;
        ui::toggle(u, {100, 100, 300, 40}, "Toggle", on);
        ui::slider(u, {100, 150, 300, 52}, "Level", level, 0, 100);
        static const char *rows[] = {"a", "b", "c"};
        ui::list(u, {100, 220, 300, 200}, "rows", 3, [](int i) { return std::string_view(rows[i]); }, selected, scroll);
        if (ui::button(u, {100, 440, 180, 40}, "Go"))
            clicks++;
        ui::text_field(u, field, "name", name, 16, "Name");
        ui::draw_toasts(u);
    }
};

static void test_keyboard()
{
    Panel p;
    p.frame(In());
    CHECK(p.st.nav_count == 5 && !p.st.nav_shown, "five widgets take part in navigation; no ring before a key");
    p.frame(In().press(VK_DOWN));
    CHECK(p.st.nav == 0 && p.st.nav_shown, "the first arrow press shows the ring on the first widget");
    p.frame(In().press(VK_RETURN));
    CHECK(p.on, "enter toggles the toggle under the ring");
    p.frame(In().press(VK_DOWN));
    CHECK(p.st.nav == 1, "down moves to the slider");
    p.frame(In().press(VK_RIGHT));
    p.frame(In().press(VK_RIGHT));
    p.frame(In().press(VK_LEFT));
    CHECK(p.level == 55, "right and left move the slider by 5%");
    p.frame(In().press(VK_DOWN));
    CHECK(p.st.nav == 2 && p.selected == 0, "down moves to the list");
    p.frame(In().press(VK_DOWN));
    p.frame(In().press(VK_DOWN));
    CHECK(p.st.nav == 2 && p.selected == 2, "down inside the list moves the selection");
    p.frame(In().press(VK_DOWN));
    CHECK(p.st.nav == 3 && p.selected == 2, "down past the last row leaves the list");
    p.frame(In().press(VK_UP));
    CHECK(p.st.nav == 2 && p.selected == 2, "up enters the list again at its selection");
    p.frame(In().press(VK_TAB));
    CHECK(p.st.nav == 3, "tab moves to the next widget from inside a list");
    p.frame(In().press(VK_SPACE));
    CHECK(p.clicks == 1, "space presses the button");
    p.frame(In().press(VK_TAB));
    p.frame(In().press(VK_RETURN));
    CHECK(p.st.nav == 4 && p.st.edit != 0, "enter starts editing the text field");
    p.frame(In().type("Lap 12"));
    CHECK(p.name == "Lap 12" && p.st.caret == 6, "typing inserts at the caret");
    p.frame(In().down(VK_SHIFT).press(VK_LEFT));
    p.frame(In().down(VK_SHIFT).press(VK_LEFT));
    CHECK(p.st.caret == 4 && p.st.anchor == 6, "shift+left selects backwards");
    CHECK(p.st.nav == 4, "arrows edit the field instead of moving the ring");
    p.frame(In().type("34"));
    CHECK(p.name == "Lap 34" && p.st.caret == 6 && p.st.anchor == 6, "typing replaces the selection");
    p.frame(In().press(VK_BACK));
    CHECK(p.name == "Lap 3", "backspace removes the character before the caret");
    p.frame(In().press(VK_HOME));
    p.frame(In().type("#"));
    CHECK(p.name == "#Lap 3", "home moves the caret to the start");
    p.frame(In().press(VK_END));
    p.frame(In().press(VK_LEFT));
    p.frame(In().press(VK_DELETE));
    CHECK(p.name == "#Lap ", "delete removes the character after the caret");
    p.frame(In().down(VK_CONTROL).press('A'));
    CHECK(p.st.anchor == 0 && p.st.caret == p.name.size(), "ctrl+a selects everything");
    p.frame(In().type("abcdefghijklmnop"));
    p.frame(In().type("q"));
    CHECK(p.name == "abcdefghijklmnop", "typing replaces a full selection and stops at max_bytes");
    p.frame(In().press(VK_ESCAPE));
    CHECK(p.st.edit == 0 && !p.back, "escape while editing ends editing and is not back");
    p.frame(In().press(VK_ESCAPE));
    CHECK(p.back, "escape outside a text field is back");
}

static void test_utf8_editing()
{
    Panel p;
    p.frame(In().at(105, 620).click());
    CHECK(p.st.edit != 0 && p.st.caret == 0, "a click in an empty field starts editing");
    p.frame(In().at(105, 620).release());
    In accents;
    accents.s.chars[0] = 0xe9;
    accents.s.chars[1] = 0x20ac;
    accents.s.char_count = 2;
    p.frame(accents);
    CHECK(p.name == "\xc3\xa9\xe2\x82\xac" && p.st.caret == 5, "typed code points become UTF-8");
    p.frame(In().press(VK_BACK));
    CHECK(p.name == "\xc3\xa9", "backspace removes a whole code point");
    p.frame(In().press(VK_LEFT));
    CHECK(p.st.caret == 0, "left steps over a whole code point");
    p.frame(In().at(105 + 20, 620).click());
    CHECK(p.st.caret == 2, "a click puts the caret at the nearest boundary");
    p.frame(In().at(105 + 20, 620).release());
    p.frame(In().at(900, 900).click());
    CHECK(p.st.edit == 0, "a click outside ends editing");
}

static void test_pad()
{
    Panel p;
    p.frame(In().pad(SWOFTY_PAD_DOWN));
    p.frame(In().pad(SWOFTY_PAD_A));
    CHECK(p.on, "the d-pad shows the ring and A activates");
    p.frame(In().pad(SWOFTY_PAD_RB));
    CHECK(p.st.nav == 1, "RB moves to the next widget");
    In trig;
    trig.s.pad_rt = 1;
    p.st.last_ms = ui::now_ms() - 100;
    p.frame(trig);
    CHECK(p.level > 50 && p.level < 56, "the right trigger raises the slider over time");
    p.frame(In().pad(SWOFTY_PAD_LB));
    CHECK(p.st.nav == 0, "LB moves to the previous widget");
    p.frame(In().pad(SWOFTY_PAD_B));
    CHECK(p.back, "B is back");
    In mouse;
    mouse.s.x = 300;
    mouse.s.y = 300;
    p.frame(mouse);
    CHECK(!p.st.nav_shown, "moving the mouse hides the ring");
}

static void test_unfocused()
{
    Panel p;
    In off;
    off.s.focused = 0;
    off.press(VK_DOWN).press(VK_RETURN);
    p.frame(off);
    p.frame(off);
    CHECK(!p.on && !p.st.nav_shown && !p.back, "nothing reacts without focus");
}

static void test_toasts()
{
    Panel p;
    ui::toast(p.st, "hello");
    p.st.toasts.back().start -= 1000;
    p.frame(In());
    bool left = false;
    for (auto &r : panels)
        left = left || (r.x == 67 && r.w == 380 && r.y >= 38 + 1004 * 0.4f - 1);
    CHECK(left, "toasts sit on the left edge of the safe area at 40% height by default");
    ui::Theme top;
    top.toast_at = ui::ToastAt::top_center;
    p.frame(In(), top);
    bool centre = false;
    for (auto &r : panels)
        centre = centre || (r.x == 67 + (1786 - 380) / 2.0f);
    CHECK(centre, "top_center centres them");
}

int main()
{
    HMODULE stub = LoadLibraryW(L"swofty_core.dll");
    api_table.version = 8;
    api_table.overlay = &table;
    swofty::api = &api_table;
    CHECK(stub && ov::v3(), "the SDK sees overlay v3 through the stub core");
    test_keyboard();
    test_utf8_editing();
    test_pad();
    test_unfocused();
    test_toasts();
    if (failures)
        printf("\n%d check(s) FAILED\n", failures);
    else
        printf("\nall toolkit checks passed\n");
    return failures ? 1 : 0;
}
