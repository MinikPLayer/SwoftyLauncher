// Host-native unit test for the portable overlay draw list (native/src/core/hud_drawlist.hpp). The
// D3D12 present hook and GPU renderer can only run in game; this covers everything that does not: the
// tessellation (golden vertex counts and bounds), batching, the clip/transform/opacity stacks, the font
// metrics and text layout, image decoding, the paint registry and the image registry. With a second
// argument it rasterises a probe scene on the CPU to <dir>/hud-scene.png. Built and run by
// native/test/run_hud_test.sh.

#include "hud_drawlist.hpp"
#include "hud_font_atlas.hpp"
#include "hud_guard.hpp"
#include "hud_image.hpp"
#include "hud_input.hpp"
#include "hud_png.hpp"

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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

struct Bounds {
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
};

static Bounds bounds(const hud::Frame &f, size_t from = 0)
{
    Bounds b;
    for (size_t i = from; i < f.verts.size(); i++) {
        b.x0 = std::min(b.x0, f.verts[i].x), b.y0 = std::min(b.y0, f.verts[i].y);
        b.x1 = std::max(b.x1, f.verts[i].x), b.y1 = std::max(b.y1, f.verts[i].y);
    }
    return b;
}

static bool near(float a, float b, float eps = 1e-3f)
{
    return std::fabs(a - b) <= eps;
}

static hud::Frame frame(float w = 1920, float h = 1080)
{
    hud::Frame f;
    hud::begin(f, w, h);
    return f;
}

static void paint_box(SwoftyPaint *paint, void *user)
{
    auto &f = *reinterpret_cast<hud::Frame *>(paint);
    int tag = int(reinterpret_cast<intptr_t>(user));
    hud::rect(f, 10, 20, 30, 40, 0x112233ff);
    hud::text(f, 1, 2, 16, 0xffffffff, tag == 7 ? "seven" : "x");
}

static void paint_leaky(SwoftyPaint *paint, void *)
{
    auto &f = *reinterpret_cast<hud::Frame *>(paint);
    hud::push_clip(f, 0, 0, 10, 10);
    hud::push_opacity(f, 0.5f);
    const float m[6] = {2, 0, 0, 2, 100, 100};
    hud::push_transform(f, m);
}

static void paint_after(SwoftyPaint *paint, void *)
{
    hud::rect(*reinterpret_cast<hud::Frame *>(paint), 50, 60, 10, 10, 0xff0000ff);
}

static void paint_nothing(SwoftyPaint *paint, void *user)
{
    (void)paint;
    (void)user;
}

static void test_font()
{
    using namespace hud::font;
    CHECK(GLYPH_COUNT >= 190 && find('A') && find(0xe9) && find(0x20ac), "the atlas has ASCII, Latin-1 and the euro sign");
    CHECK(glyph(0x4e2d) == find('?'), "a codepoint the font lacks draws as ?");
    CHECK(glyph(0xa0) == find(' ') && glyph('\t') == find(' '), "a no-break space and a tab draw as a space");
    CHECK(kern('A', 'V') < 0 && kern('T', 'a') < 0 && kern('x', 'x') == 0, "kerning pairs come from the font");
    CHECK(BASELINE > 0.6f && BASELINE < 0.9f, "the baseline sits inside the one-em text box");
    const Glyph *a = find('A');
    CHECK(a && near((a->x1 - a->x0) * ATLAS_EM, float(a->u1 - a->u0)) && near((a->y1 - a->y0) * ATLAS_EM, float(a->v1 - a->v0)),
          "a glyph quad and its atlas rect have the same size");
    size_t i = 0;
    CHECK(decode("\xc3", i) == 0xfffd, "a cut-off sequence decodes to U+FFFD");
    i = 0;
    CHECK(decode("\xe2\x82\xac", i) == 0x20ac && i == 3, "a three-byte sequence decodes");
}

static void test_measure()
{
    float w = -1, h = -1;
    hud::measure(16, "", &w, &h);
    CHECK(w == 0 && h == 16, "measure of empty text is zero wide and one line tall");
    float a = 0, v = 0, av = 0;
    hud::measure(32, "A", &a, nullptr);
    hud::measure(32, "V", &v, nullptr);
    hud::measure(32, "AV", &av, nullptr);
    CHECK(near(av, a + v + hud::font::kern('A', 'V') * 32), "measure applies kerning between glyphs");
    float w12 = 0, w72 = 0;
    hud::measure(12, "Lap 12 / 57", &w12, nullptr);
    hud::measure(72, "Lap 12 / 57", &w72, nullptr);
    CHECK(near(w72, w12 * 6, 1e-2f) && w12 > 0, "width scales with the font size");
    hud::measure(20, "one\ntwo\nthree", &w, &h);
    float three = 0;
    hud::measure(20, "three", &three, nullptr);
    CHECK(near(h, 20 + 2 * 24) && near(w, three), "lines split at newlines: height adds 1.2 em per line, width is the widest");
    hud::TextStyle s;
    s.px = 20;
    s.max_width = 60;
    std::vector<hud::Line> lines;
    hud::layout(s, "box box box box", lines);
    bool fits = true;
    for (auto &l : lines)
        fits = fits && l.width <= 60.001f;
    CHECK(lines.size() >= 2 && fits, "max_width wraps at spaces and every line fits");
    hud::layout(s, "WWWWWWWWWWWWWW", lines);
    CHECK(lines.size() >= 2, "a word wider than max_width breaks mid-word");
    s.tracking = 2;
    s.max_width = 0;
    float tw = 0;
    hud::measure(s, "abc", &tw, nullptr);
    float plain = 0;
    hud::measure(20, "abc", &plain, nullptr);
    CHECK(near(tw, plain + 4), "tracking adds pixels between glyphs");
}

static void test_culling()
{
    hud::Frame f = frame();
    hud::rect(f, 0, 0, 0, 10, 0xff0000ff);
    CHECK(f.verts.empty(), "zero-width rect is dropped");
    hud::rect(f, 0, 0, 10, 10, 0x00000000);
    CHECK(f.verts.empty(), "fully transparent rect is dropped");
    hud::line(f, 5, 5, 5, 5, 1, 0xffffffff);
    CHECK(f.verts.empty(), "zero-length line is dropped");
    hud::text(f, 0, 0, 16, 0xffffffff, "");
    CHECK(f.verts.empty(), "empty text is dropped");
    hud::text(f, 0, 0, 16, 0xffffffff, "   ");
    CHECK(f.verts.empty(), "spaces draw nothing");
    hud::rect_outline(f, 0, 0, 10, 10, 0, 0xffffffff);
    CHECK(f.verts.empty(), "zero-thickness outline is dropped");
    hud::circle(f, 5, 5, 0, 0xffffffff);
    hud::arc(f, 5, 5, 10, 1, 1, 2, 0xffffffff);
    CHECK(f.verts.empty(), "zero-radius circles and empty arcs are dropped");
    hud::push_opacity(f, 0);
    hud::rect(f, 0, 0, 10, 10, 0xffffffff);
    hud::pop_opacity(f);
    CHECK(f.verts.empty(), "nothing draws at zero opacity");
}

static void test_v1_geometry()
{
    hud::Frame f = frame();
    hud::rect(f, 10, 20, 30, 40, 0x11223344);
    CHECK(f.verts.size() == 6, "a rect is two triangles");
    Bounds b = bounds(f);
    CHECK(b.x0 == 10 && b.y0 == 20 && b.x1 == 40 && b.y1 == 60, "a rect covers exactly its rectangle (no fringe)");
    CHECK(f.verts[0].rgba == 0x44332211u && f.verts[0].mode == hud::SOLID, "colours pack little-endian RGBA");
    size_t at = f.verts.size();
    hud::rect_outline(f, 0, 0, 100, 50, 3, 0xffffffff);
    CHECK(f.verts.size() - at == 24, "an outline is four rects");
    Bounds o = bounds(f, at);
    CHECK(o.x0 == 0 && o.y0 == 0 && o.x1 == 100 && o.y1 == 50, "an outline stays inside its rectangle");
    at = f.verts.size();
    hud::line(f, 0, 0, 100, 0, 4, 0xffffffff);
    Bounds l = bounds(f, at);
    CHECK(f.verts.size() - at == 6 && l.y0 == -2 && l.y1 == 2 && l.x1 == 100, "a line is a quad of its thickness");
    at = f.verts.size();
    hud::text(f, 7, 8, 18, 0xffffffff, "hi");
    CHECK(f.verts.size() - at == 12 && f.verts[at].mode == hud::sdf_mode(0, 0), "text is one SDF quad per glyph");
    Bounds t = bounds(f, at);
    CHECK(t.y0 >= 8 - 18 * 0.25f && t.y1 <= 8 + 18 * 1.25f && t.x0 >= 7 - 18 * 0.3f, "text sits around its one-em box");
    at = f.verts.size();
    hud::image(f, 3, 100, 100, 64, 64, 0x80808080);
    CHECK(f.verts.size() - at == 6 && f.verts[at].mode == hud::IMAGE && f.verts[at].rgba == 0x80808080u,
          "an image is one textured quad with its tint");
    CHECK(f.batches.back().tex == 4, "an image batch binds texture slot + 1");
}

static void test_v2_geometry()
{
    hud::Frame f = frame();
    hud::rect_rounded(f, 100, 100, 200, 80, 0, 0xffffffff);
    CHECK(f.verts.size() == 30, "a square-cornered AA rect is a 4-point fan plus fringe (9n-6)");
    Bounds b = bounds(f);
    CHECK(near(b.x0, 99.5f) && near(b.y0, 99.5f) && near(b.x1, 300.5f) && near(b.y1, 180.5f),
          "the AA fringe reaches half a pixel outside");
    size_t at = f.verts.size();
    hud::rect_rounded(f, 100, 100, 200, 80, 10, 0xffffffff);
    CHECK(f.verts.size() - at == 174, "radius 10 corners take 4 segments each: 20 points, 174 vertices");
    at = f.verts.size();
    hud::rect_rounded(f, 0, 0, 20, 20, 100, 0xffffffff);
    Bounds pill = bounds(f, at);
    CHECK(near(pill.x1 - pill.x0, 21, 0.05f) && near(pill.y1 - pill.y0, 21, 0.05f) && f.verts.size() - at == 9 * 16 - 6,
          "a radius larger than half the size makes a circle without doubled points");
    at = f.verts.size();
    hud::circle(f, 500, 500, 50, 0xffffffff);
    CHECK(f.verts.size() - at == 282, "a radius 50 circle has 32 points");
    Bounds c = bounds(f, at);
    CHECK(near(c.x0, 449.5f, 0.02f) && near(c.x1, 550.5f, 0.02f), "circle bounds are the radius plus the fringe");
    at = f.verts.size();
    hud::circle_outline(f, 500, 500, 50, 4, 0xffffffff);
    CHECK(f.verts.size() - at == 32 * 18, "a circle outline is 18 vertices per segment");
    Bounds co = bounds(f, at);
    CHECK(co.x1 <= 550.5f + 0.02f && co.x1 >= 549.5f, "a circle outline stays inside its radius");
    at = f.verts.size();
    const float pts[] = {0, 0, 100, 0, 100, 100};
    hud::polyline(f, pts, 3, 3, 0xffffffff, 0);
    CHECK(f.verts.size() - at == 36, "an open 3-point polyline is two segments of 18 vertices");
    at = f.verts.size();
    hud::polyline(f, pts, 3, 0.5f, 0xffffffff, 0);
    CHECK(f.verts.size() - at == 24, "a hairline polyline is 12 vertices per segment");
    CHECK((f.verts[at + 2].rgba >> 24) == 128, "a half-pixel hairline is half opaque");
    at = f.verts.size();
    hud::polyline(f, pts, 3, 2, 0xffffffff, SWOFTY_POLYLINE_CLOSED);
    CHECK(f.verts.size() - at == 54, "a closed 3-point polyline has three segments");
    at = f.verts.size();
    hud::arc(f, 0, 0, 100, 0, hud::PI / 2, 10, 0xffffffff);
    size_t arc_n = f.verts.size() - at;
    Bounds a = bounds(f, at);
    CHECK(arc_n % 18 == 0 && arc_n / 18 >= 8, "a quarter arc is a stroke of several segments");
    CHECK(near(a.x1, 105.5f, 0.05f) && near(a.y1, 105.5f, 0.05f) && a.x0 > -1 && a.y0 > -1,
          "a quarter arc from 0 to pi/2 sweeps clockwise into +x +y, (thickness + 1) / 2 each side of the radius");
    at = f.verts.size();
    hud::arc(f, 0, 0, 100, 0, 2 * hud::PI, 10, 0xffffffff);
    CHECK((f.verts.size() - at) % 18 == 0, "a full arc is a closed ring");
    at = f.verts.size();
    hud::rect_gradient(f, 0, 0, 100, 100, 0, 0xff0000ff, 0x0000ffff, SWOFTY_GRADIENT_VERTICAL);
    CHECK(f.verts.size() - at == 30, "a gradient rect is an AA rect");
    uint32_t top = 0, bottom = 0;
    for (size_t i = at; i < f.verts.size(); i++) {
        if (f.verts[i].y < 50 && (f.verts[i].rgba >> 24) == 255)
            top = f.verts[i].rgba;
        if (f.verts[i].y > 50 && (f.verts[i].rgba >> 24) == 255)
            bottom = f.verts[i].rgba;
    }
    CHECK(top == 0xff0000ffu && bottom == 0xffff0000u, "a vertical gradient runs from the top colour to the bottom one");
    at = f.verts.size();
    const float tri[] = {0, 0, 50, 0, 25, 40};
    hud::polygon(f, tri, 3, 0xffffffff);
    CHECK(f.verts.size() - at == 21, "a filled triangle is one fan triangle plus fringe");
    const float ccw[] = {25, 40, 50, 0, 0, 0};
    at = f.verts.size();
    hud::polygon(f, ccw, 3, 0xffffffff);
    Bounds pc = bounds(f, at);
    CHECK(near(pc.y0, -0.5f, 0.01f), "the fringe goes outward for either winding");
}

static void test_text_effects()
{
    hud::Frame f = frame();
    hud::TextStyle s;
    s.px = 24;
    s.outline_rgba = 0x000000ff;
    s.outline_px = 2;
    s.shadow_rgba = 0x00000080;
    s.shadow_dx = 2;
    s.shadow_dy = 2;
    s.shadow_blur = 3;
    hud::text(f, 0, 0, s, "Ab");
    CHECK(f.verts.size() == 36, "shadow, outline and fill each draw one quad per glyph");
    CHECK((f.verts[0].mode & 0xff) == hud::SDF && (f.verts[0].mode >> 16 & 0xff) == 24, "the shadow pass is blurred");
    CHECK((f.verts[12].mode >> 8 & 0xff) > 0 && (f.verts[24].mode >> 8 & 0xff) == 0, "the outline pass is dilated");
    CHECK(f.batches.size() == 1 && f.batches[0].tex == hud::TEX_FONT, "text is one batch on the font atlas");
    hud::Frame g = frame();
    hud::TextStyle c;
    c.px = 20;
    c.align = SWOFTY_TEXT_CENTER | SWOFTY_TEXT_MIDDLE;
    hud::text(g, 960, 540, c, "Centre");
    Bounds b = bounds(g);
    CHECK(std::fabs((b.x0 + b.x1) / 2 - 960) < 3 && std::fabs((b.y0 + b.y1) / 2 - 540) < 6, "centred text centres on its anchor");
    hud::Frame r = frame();
    c.align = SWOFTY_TEXT_RIGHT | SWOFTY_TEXT_BASELINE;
    hud::text(r, 500, 300, c, "Right");
    float w = 0;
    hud::measure(c, "Right", &w, nullptr);
    Bounds rb = bounds(r);
    CHECK(rb.x1 <= 500 + hud::font::SPREAD + 1 && rb.x0 >= 500 - w - hud::font::SPREAD - 1 && rb.y1 > 300 && rb.y0 < 300,
          "right/baseline text ends at x with its baseline on y");
}

static void test_batches()
{
    hud::Frame f = frame();
    hud::rect(f, 0, 0, 10, 10, 0xffffffff);
    hud::text(f, 0, 0, 16, 0xffffffff, "A");
    hud::rect(f, 0, 0, 10, 10, 0xffffffff);
    CHECK(f.batches.size() == 1 && f.batches[0].tex == hud::TEX_FONT && f.batches[0].count == 18,
          "solid geometry joins the batch around text");
    hud::image(f, 0, 0, 0, 10, 10, 0xffffffff);
    CHECK(f.batches.size() == 2 && f.batches[1].tex == 1, "an image starts a batch on its texture");
    hud::push_clip(f, 5, 5, 100, 100);
    hud::rect(f, 0, 0, 10, 10, 0xffffffff);
    CHECK(f.batches.size() == 3 && f.batches[2].clip == (hud::Clip{5, 5, 105, 105}), "a clip starts a batch with its rect");
    hud::push_clip(f, 50, 50, 200, 200);
    CHECK(f.clip == (hud::Clip{50, 50, 105, 105}), "nested clips intersect");
    hud::pop_clip(f);
    hud::pop_clip(f);
    hud::pop_clip(f);
    CHECK(f.clip == (hud::Clip{0, 0, 1920, 1080}), "pops restore the frame clip and an extra pop is ignored");
    hud::push_clip(f, 2000, 0, 10, 10);
    size_t n = f.verts.size();
    hud::rect(f, 0, 0, 10, 10, 0xffffffff);
    CHECK(f.verts.size() == n, "nothing draws inside an empty clip");
    hud::pop_clip(f);
}

static void test_transforms()
{
    hud::Frame f = frame();
    const float move[6] = {1, 0, 0, 1, 100, 50};
    hud::push_transform(f, move);
    hud::rect(f, 0, 0, 10, 10, 0xffffffff);
    Bounds b = bounds(f);
    CHECK(b.x0 == 100 && b.y0 == 50 && b.x1 == 110 && b.y1 == 60, "a translate moves geometry");
    const float twice[6] = {2, 0, 0, 2, 0, 0};
    hud::push_transform(f, twice);
    size_t at = f.verts.size();
    hud::rect(f, 0, 0, 10, 10, 0xffffffff);
    b = bounds(f, at);
    CHECK(b.x0 == 100 && b.x1 == 120 && b.y1 == 70, "transforms compose: scale inside a translate");
    at = f.verts.size();
    hud::polyline(f, std::array<float, 4>{0, 0, 10, 0}.data(), 2, 2, 0xffffffff, 0);
    b = bounds(f, at);
    CHECK(near(b.y1 - b.y0, 5), "stroke thickness scales with the transform (2 px at 2x is 4 px plus fringe)");
    hud::pop_transform(f);
    hud::pop_transform(f);
    float c = std::cos(hud::PI / 2), s = std::sin(hud::PI / 2);
    const float rot[6] = {c, s, -s, c, 0, 0};
    hud::push_transform(f, rot);
    at = f.verts.size();
    hud::rect(f, 0, 0, 20, 10, 0xffffffff);
    b = bounds(f, at);
    CHECK(near(b.x0, -10) && near(b.x1, 0) && near(b.y0, 0) && near(b.y1, 20), "a quarter turn rotates clockwise on screen");
    hud::pop_transform(f);
    hud::push_opacity(f, 0.5f);
    hud::push_opacity(f, 0.5f);
    at = f.verts.size();
    hud::rect(f, 0, 0, 10, 10, 0xffffffffu);
    CHECK((f.verts[at].rgba >> 24) == 64, "opacities multiply");
    hud::pop_opacity(f);
    hud::pop_opacity(f);
    at = f.verts.size();
    hud::rect(f, 0, 0, 10, 10, 0xffffffffu);
    CHECK((f.verts[at].rgba >> 24) == 255, "popping restores full opacity");
}

static void test_layout_helpers()
{
    hud::Frame f = frame(1920, 1080);
    float x, y, w, h;
    hud::safe_area(f, &x, &y, &w, &h);
    CHECK(x == 67 && y == 38 && w == 1786 && h == 1004, "the safe area insets 3.5% each side");
    CHECK(hud::ui_scale(f) == 1, "1080p is UI scale 1");
    CHECK(hud::ui_scale(frame(3840, 2160)) == 2 && near(hud::ui_scale(frame(3440, 1440)), 1440.0f / 1080),
          "4K is 2 and ultrawide follows the height");
}

static void test_nine_slice()
{
    hud::Frame f = frame();
    hud::image_nine(f, 2, 32, 32, 100, 100, 300, 120, 8, 8, 8, 8, 2, 0xffffffff);
    CHECK(f.verts.size() == 54, "a nine-slice panel is nine quads");
    CHECK(near(f.verts[1].x, 116) && near(f.verts[1].u, 0.25f), "corners keep their inset times scale and map to the inset UVs");
    Bounds b = bounds(f);
    CHECK(b.x0 == 100 && b.x1 == 400 && b.y1 == 220, "the panel fills its rectangle");
    hud::Frame g = frame();
    hud::image_nine(g, 2, 32, 32, 0, 0, 10, 10, 8, 8, 8, 8, 2, 0xffffffff);
    Bounds s = bounds(g);
    CHECK(s.x1 == 10 && s.y1 == 10, "insets shrink to fit a small panel");
}

static void test_cap()
{
    hud::Frame f = frame();
    for (uint32_t i = 0; i < hud::MAX_VERTS / 6 + 10; i++)
        hud::rect(f, 0, 0, 1, 1, 0xffffffff);
    CHECK(f.verts.size() <= hud::MAX_VERTS && f.dropped == 10, "primitives past the vertex cap are dropped and counted");
}


static void test_text_cache()
{
    hud::Frame f = frame();
    hud::TextStyle st;
    st.px = 24;
    hud::text(f, 10, 10, st, "Lap 12 / 57");
    std::vector<hud::Vertex> first = f.verts;
    CHECK(f.text_cache.misses == 1 && f.text_cache.hits == 0, "the first draw of a string lays it out");
    hud::begin(f, 1920, 1080);
    hud::text(f, 10, 10, st, "Lap 12 / 57");
    bool same = f.verts.size() == first.size();
    for (size_t i = 0; same && i < first.size(); i++)
        same = f.verts[i].x == first[i].x && f.verts[i].y == first[i].y && f.verts[i].u == first[i].u;
    CHECK(f.text_cache.hits == 1 && same, "a cached layout draws the same vertices");
    st.px = 25;
    hud::text(f, 10, 10, st, "Lap 12 / 57");
    CHECK(f.text_cache.misses == 2, "another size is another layout");
    for (int i = 0; i < int(hud::TEXT_CACHE_MAX) + 50; i++)
        hud::text(f, 0, 0, st, std::to_string(i));
    for (uint64_t k = 0; k < hud::TEXT_CACHE_KEEP + 2; k++)
        hud::begin(f, 1920, 1080);
    CHECK(f.text_cache.map.size() <= hud::TEXT_CACHE_MAX, "the cache drops layouts unused for a while once it is full");
}

static void test_input()
{
    using namespace hud::input;
    Snapshot s;
    std::vector<Event> ev = {{Ev::move, 0, 100, 50},  {Ev::down, SWOFTY_MOUSE_LEFT, 110, 60}, {Ev::wheel, 0, 1, 0},
                             {Ev::wheel, 0, 1, 0},     {Ev::chr, 'a', 0, 0},                   {Ev::chr, 0x1b, 0, 0},
                             {Ev::key_down, 0x77, 0, 0}, {Ev::key_down, 0x77, 0, 0}};
    fold(s, ev, 2, 2);
    CHECK(s.x == 220 && s.y == 120, "client positions scale to framebuffer pixels");
    CHECK(s.buttons == SWOFTY_MOUSE_LEFT && s.pressed == SWOFTY_MOUSE_LEFT && s.wheel == 2, "presses and wheel notches fold");
    CHECK(s.chars.size() == 1 && s.chars[0] == 'a', "control characters are not text");
    CHECK(bit(s.keys_pressed, 0x77) && bit(s.keys_down, 0x77), "a key press is an edge and a held key");
    fold(s, {{Ev::up, SWOFTY_MOUSE_LEFT, 110, 60}}, 1, 1);
    CHECK(s.buttons == 0 && s.released == SWOFTY_MOUSE_LEFT && s.pressed == 0 && s.wheel == 0 && s.chars.empty(),
          "edges, wheel and text last one frame; held state carries over");
    CHECK(!bit(s.keys_pressed, 0x77) && bit(s.keys_down, 0x77), "a key held into the next frame is not pressed again");
    fold(s, {{Ev::key_down, 0x77, 0, 0}}, 1, 1);
    CHECK(!bit(s.keys_pressed, 0x77), "auto-repeat of a held key is not a new press");
    fold(s, {{Ev::key_down, 0x08, 0, 0}, {Ev::key_down, 0x08, 0, 0}}, 1, 1);
    CHECK(bit(s.keys_typed, 0x08) && bit(s.keys_pressed, 0x08), "keys_typed includes the first press");
    fold(s, {{Ev::key_down, 0x08, 0, 0}}, 1, 1);
    CHECK(bit(s.keys_typed, 0x08) && !bit(s.keys_pressed, 0x08), "auto-repeat shows in keys_typed only");
    PadRaw pads[4] = {};
    pads[1] = {true, SWOFTY_PAD_A, 0, 255, 0, 32767, 0, 0};
    fold_pads(s, pads, 4, 1000);
    CHECK(s.pad_connected == 2 && (s.pad_pressed & SWOFTY_PAD_A) && s.pad_rt == 1 && s.pad_ly == 1,
          "a connected pad reports buttons, triggers and the stick");
    CHECK((s.pad_buttons & SWOFTY_PAD_UP) && (s.pad_pressed & SWOFTY_PAD_UP), "the left stick past half way holds the d-pad");
    fold_pads(s, pads, 4, 1100);
    CHECK(!(s.pad_pressed & (SWOFTY_PAD_A | SWOFTY_PAD_UP)), "held buttons do not press again at once");
    fold_pads(s, pads, 4, 1400);
    CHECK((s.pad_pressed & SWOFTY_PAD_UP) && !(s.pad_pressed & SWOFTY_PAD_A), "a held direction repeats after 400 ms");
    fold_pads(s, pads, 4, 1450);
    CHECK(!(s.pad_pressed & SWOFTY_PAD_UP), "and not again before 90 ms");
    fold_pads(s, pads, 4, 1500);
    CHECK((s.pad_pressed & SWOFTY_PAD_UP), "then every 90 ms");
    pads[1].ly = 3000;
    pads[1].buttons = 0;
    fold_pads(s, pads, 4, 1600);
    CHECK(s.pad_ly == 0 && !(s.pad_buttons & SWOFTY_PAD_UP), "small stick moves sit in the deadzone");
    Utf16 u;
    CHECK(u.push(0xd83c) == 0 && u.push(0xdfce) == 0x1f3ce && u.push('x') == 'x', "surrogate pairs join into one code point");
    Focus f;
    CHECK(f.take("a", true, 0) && f.held() && f.owner == "a", "an addon takes focus");
    CHECK(!f.take("b", false, 10) && f.owner == "a", "another addon cannot release it");
    CHECK(f.take("b", true, 20) && f.owner == "b", "taking focus moves it to the newest taker");
    f.painted("b", 1000);
    CHECK(!f.expire(2500, 2000) && f.expire(3001, 2000) && !f.held(), "focus lapses after 2 s without a paint");
    s.chars = {'q'};
    s.wheel = 1;
    s.buttons = SWOFTY_MOUSE_LEFT;
    SwoftyInput in{};
    in.size = sizeof in;
    view(s, f, "a", in);
    CHECK(!in.focused && in.x == 110 && in.buttons == 0 && in.char_count == 0 && in.wheel == 0 && in.size == sizeof in,
          "without focus an addon sees the cursor and keys only");
    f.take("a", true, 0);
    view(s, f, "a", in);
    CHECK(in.focused && in.buttons == SWOFTY_MOUSE_LEFT && in.char_count == 1 && in.chars[0] == 'q' && in.wheel == 1,
          "the focus holder sees buttons, wheel and text");
    view(s, f, "b", in);
    CHECK(!in.focused && in.x == -1 && in.keys_down[0x77 >> 3] == 0, "other addons see nothing while one holds focus");
}

static void test_registry()
{
    hud::Registry reg;
    CHECK(reg.add(nullptr, nullptr, "a") == 0, "on_paint with a null callback yields no token");
    CHECK(reg.count() == 0, "a refused registration is not stored");
    uint32_t a = reg.add(paint_box, nullptr, "a");
    uint32_t b = reg.add(paint_nothing, nullptr, "b");
    CHECK(a != 0 && b != 0 && a != b, "two registrations get distinct non-zero tokens");
    CHECK(!reg.remove(9999), "removing an unknown token fails");
    CHECK(reg.remove(a), "removing a known token succeeds");
    CHECK(reg.count() == 1, "remove drops exactly one subscription");
    CHECK(!reg.remove(a), "a token cannot be removed twice");
}

static void test_build_dispatch()
{
    hud::Registry reg;
    reg.add(paint_box, reinterpret_cast<void *>(intptr_t(7)), "a");
    reg.add(paint_box, reinterpret_cast<void *>(intptr_t(1)), "b");
    hud::Frame f;
    hud::build(reg.subs(), f, 1920, 1080);
    CHECK(f.width == 1920 && f.height == 1080, "build records the drawable size for size()");
    CHECK(f.verts.size() == 6 + 5 * 6 + 6 + 6, "both callbacks ran: a rect and their text each");
    hud::build(reg.subs(), f, 800, 600);
    CHECK(f.verts.size() == 48 && f.width == 800, "each frame rebuilds the vertices from scratch");
    hud::Registry leaky;
    leaky.add(paint_leaky, nullptr, "a");
    leaky.add(paint_after, nullptr, "b");
    hud::build(leaky.subs(), f, 1920, 1080);
    Bounds b = bounds(f);
    CHECK(f.verts.size() == 6 && b.x0 == 50 && (f.verts[0].rgba >> 24) == 255 && f.batches[0].clip.x1 == 1920,
          "one callback's unpopped clip, opacity and transform do not reach the next");
    hud::build(reg.subs(), f, 1920, 1080, {"b"});
    CHECK(f.verts.size() == 6 + 5 * 6, "an owner in the overlay_off list is skipped at draw time");
    hud::build(reg.subs(), f, 1920, 1080, {"a", "b"});
    CHECK(f.verts.empty(), "every listed owner is skipped");
}

static void test_image_registry()
{
    hud::Registry reg;
    int first = reg.intern_image("logo");
    int again = reg.intern_image("logo");
    int other = reg.intern_image("flag");
    CHECK(first == 0 && again == 0, "interning the same id twice returns the same slot");
    CHECK(other == 1, "a new id gets a new slot");
    CHECK(reg.find_image("logo") == 0, "find_image returns a registered slot");
    CHECK(reg.find_image("missing") == -1, "find_image returns -1 for an unknown id");
    CHECK(reg.image_count() == 2, "two distinct images registered");
    int set = reg.set_image("logo", 2, 1, std::vector<uint8_t>(8, 255));
    CHECK(set == 0 && reg.image(0)->width == 2 && reg.image(0)->generation == 1, "set_image keeps the slot and bumps the generation");
    reg.set_image("logo", 2, 1, std::vector<uint8_t>(8, 0));
    CHECK(reg.image(0)->generation == 2, "reloading an id bumps it again so the renderer uploads it");
}

static void test_image_decode()
{
    std::vector<uint8_t> rgb = {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};
    std::string png = hud::png::encode_rgb(rgb.data(), 2, 2);
    hud::img::Pixels px;
    std::string why;
    CHECK(hud::img::decode(png, px, why) && px.width == 2 && px.height == 2 && px.rgba[0] == 255 && px.rgba[5] == 255 &&
              px.rgba[3] == 255,
          "a PNG decodes to RGBA rows");
    CHECK(!hud::img::decode("not an image", px, why) && !why.empty(), "a file that is not an image says why");
    std::string dds(128 + 8, '\0');
    auto put = [&](size_t at, uint32_t v) { memcpy(&dds[at], &v, 4); };
    memcpy(&dds[0], "DDS ", 4);
    put(4, 124), put(12, 1), put(16, 2), put(80, 0x41), put(88, 32);
    put(92, 0x00ff0000), put(96, 0x0000ff00), put(100, 0x000000ff), put(104, 0xff000000);
    put(128, 0x80ff0000), put(132, 0xff00ff00);
    CHECK(hud::img::decode(dds, px, why) && px.width == 2 && px.rgba[0] == 128 && px.rgba[3] == 128 && px.rgba[5] == 255,
          "an uncompressed A8R8G8B8 DDS decodes and premultiplies");
    put(84, 0x31545844), put(80, 0x4);
    CHECK(!hud::img::decode(dds, px, why), "a compressed DDS is refused");
    hud::img::Pixels atlas;
    CHECK(hud::img::grey(hud::font::ATLAS_PNG, hud::font::ATLAS_PNG_SIZE, atlas) && atlas.width == uint32_t(hud::font::ATLAS_W) &&
              atlas.height == uint32_t(hud::font::ATLAS_H),
          "the embedded font atlas decodes at its stated size");
}

// A CPU rasteriser for the draw list: the same blend and SDF maths as the overlay's pixel shader, so
// a test scene can be looked at without the game.
static void raster(const hud::Frame &f, const hud::img::Pixels &atlas, std::vector<float> &px, int W, int H)
{
    auto sample = [&](float u, float v) {
        float x = u * atlas.width - 0.5f, y = v * atlas.height - 0.5f;
        int x0 = int(std::floor(x)), y0 = int(std::floor(y));
        float fx = x - x0, fy = y - y0;
        auto at = [&](int xx, int yy) {
            xx = std::clamp(xx, 0, int(atlas.width) - 1), yy = std::clamp(yy, 0, int(atlas.height) - 1);
            return atlas.rgba[size_t(yy) * atlas.width + size_t(xx)] / 255.0f;
        };
        return (at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx) * (1 - fy) + (at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx) * fy;
    };
    for (const hud::Batch &b : f.batches)
        for (uint32_t t = b.first; t + 2 < b.first + b.count; t += 3) {
            const hud::Vertex &a = f.verts[t], &bb = f.verts[t + 1], &c = f.verts[t + 2];
            float area = (bb.x - a.x) * (c.y - a.y) - (bb.y - a.y) * (c.x - a.x);
            if (std::fabs(area) < 1e-8f)
                continue;
            int x0 = std::max(int(std::floor(std::min({a.x, bb.x, c.x}))), int(b.clip.x0));
            int x1 = std::min(int(std::ceil(std::max({a.x, bb.x, c.x}))), std::min(W, int(std::ceil(b.clip.x1))));
            int y0 = std::max(int(std::floor(std::min({a.y, bb.y, c.y}))), int(b.clip.y0));
            int y1 = std::min(int(std::ceil(std::max({a.y, bb.y, c.y}))), std::min(H, int(std::ceil(b.clip.y1))));
            float dudx = 0, dudy = 0;
            {
                float w1x = (c.y - a.y) / area, w2x = -(bb.y - a.y) / area;
                float w1y = -(c.x - a.x) / area, w2y = (bb.x - a.x) / area;
                dudx = (bb.u - a.u) * w1x + (c.u - a.u) * w2x;
                dudy = (bb.u - a.u) * w1y + (c.u - a.u) * w2y;
            }
            for (int y = std::max(y0, 0); y < y1; y++)
                for (int x = std::max(x0, 0); x < x1; x++) {
                    float px_ = x + 0.5f, py = y + 0.5f;
                    float w1 = ((px_ - a.x) * (c.y - a.y) - (py - a.y) * (c.x - a.x)) / area;
                    float w2 = ((bb.x - a.x) * (py - a.y) - (bb.y - a.y) * (px_ - a.x)) / area;
                    float w0 = 1 - w1 - w2;
                    if (w0 < -1e-6f || w1 < -1e-6f || w2 < -1e-6f)
                        continue;
                    float col[4];
                    for (int k = 0; k < 4; k++)
                        col[k] = (((a.rgba >> (8 * k)) & 0xff) * w0 + ((bb.rgba >> (8 * k)) & 0xff) * w1 +
                                  ((c.rgba >> (8 * k)) & 0xff) * w2) /
                                 255.0f;
                    float u = a.u * w0 + bb.u * w1 + c.u * w2, v = a.v * w0 + bb.v * w1 + c.v * w2;
                    float tex[4] = {1, 1, 1, 1};
                    uint32_t kind = a.mode & 0xff;
                    if (kind == hud::SDF) {
                        float d = sample(u, v);
                        float fw = std::max((std::fabs(dudx) + std::fabs(dudy)) * atlas.width / (2 * hud::font::SPREAD) * 2, 1e-5f);
                        float e = 0.5f - float((a.mode >> 8) & 255) / 510.0f;
                        float h = fw * (0.5f + float((a.mode >> 16) & 255) / 16.0f);
                        float tt = std::clamp((d - (e - h)) / (2 * h), 0.0f, 1.0f);
                        float al = tt * tt * (3 - 2 * tt);
                        tex[0] = tex[1] = tex[2] = tex[3] = al;
                    } else if (kind == hud::IMAGE) {
                        bool check = (int(u * 8) + int(v * 8)) % 2;
                        tex[0] = check ? 1 : 0.2f, tex[1] = 0.6f, tex[2] = check ? 0.2f : 1, tex[3] = 1;
                    }
                    float sa = tex[3] * col[3];
                    float *d = &px[(size_t(y) * W + x) * 3];
                    for (int k = 0; k < 3; k++)
                        d[k] = tex[k] * col[k] * col[3] + d[k] * (1 - sa);
                }
        }
}

static void scene(hud::Frame &f)
{
    float s = hud::ui_scale(f);
    float sx, sy, sw, sh;
    hud::safe_area(f, &sx, &sy, &sw, &sh);
    hud::rect_outline(f, sx, sy, sw, sh, 1, 0xffffff40);
    hud::rect_gradient(f, sx, sy, 520 * s, 400 * s, 14 * s, 0x15151cf0, 0x2a2a38e0, SWOFTY_GRADIENT_VERTICAL);
    hud::rect_rounded_outline(f, sx, sy, 520 * s, 400 * s, 14 * s, 2 * s, 0xe10600ff);
    hud::TextStyle t;
    t.rgba = 0xffffffff;
    float y = sy + 12 * s;
    for (float px : {12.0f, 18.0f, 24.0f, 36.0f, 48.0f, 72.0f}) {
        t.px = px * s;
        char buf[64];
        snprintf(buf, sizeof buf, "%g px AVATAR \xc3\x9c\x62\x65r 1:23.456", px);
        hud::text(f, sx + 16 * s, y, t, buf);
        y += t.px * 1.3f;
    }
    float cx = sx + 700 * s, cy = sy + 170 * s;
    hud::circle(f, cx, cy, 120 * s, 0x000000a0);
    hud::arc(f, cx, cy, 100 * s, 0.75f * hud::PI, 2.25f * hud::PI, 14 * s, 0xffffff30);
    hud::arc(f, cx, cy, 100 * s, 0.75f * hud::PI, 1.7f * hud::PI, 14 * s, 0xe10600ff);
    hud::circle_outline(f, cx, cy, 120 * s, 2 * s, 0xffffffc0);
    hud::TextStyle g;
    g.px = 56 * s;
    g.align = SWOFTY_TEXT_CENTER | SWOFTY_TEXT_MIDDLE;
    g.outline_rgba = 0x000000ff;
    g.outline_px = 2 * s;
    hud::text(f, cx, cy, g, "287");
    std::vector<float> line;
    for (int i = 0; i <= 60; i++)
        line.insert(line.end(), {sx + 900 * s + i * 8 * s, sy + 170 * s + std::sin(i * 0.3f) * 60 * s});
    hud::polyline(f, line.data(), uint32_t(line.size() / 2), 3 * s, 0x00d2beff, 0);
    const float rot = 0.3f;
    float c = std::cos(rot), sn = std::sin(rot), ox = sx + 1100 * s, oy = sy + 400 * s;
    const float m[6] = {c, sn, -sn, c, ox - c * ox + sn * oy, oy - sn * ox - c * oy};
    hud::push_transform(f, m);
    hud::rect_rounded(f, ox - 100 * s, oy - 30 * s, 200 * s, 60 * s, 30 * s, 0x2d9affd0);
    hud::TextStyle r;
    r.px = 28 * s;
    r.align = SWOFTY_TEXT_CENTER | SWOFTY_TEXT_MIDDLE;
    hud::text(f, ox, oy, r, "ROTATED");
    hud::pop_transform(f);
    hud::push_clip(f, sx, sy + 430 * s, 300 * s, 60 * s);
    hud::TextStyle w;
    w.px = 40 * s;
    w.shadow_rgba = 0x000000c0;
    w.shadow_dx = w.shadow_dy = 3 * s;
    w.shadow_blur = 4 * s;
    hud::text(f, sx, sy + 430 * s, w, "Clipped text runs past its box");
    hud::pop_clip(f);
    hud::push_opacity(f, 0.5f);
    hud::image(f, 0, sx + 340 * s, sy + 430 * s, 120 * s, 120 * s, 0xffffffff);
    hud::pop_opacity(f);
    hud::image_nine(f, 0, 64, 64, sx + 480 * s, sy + 430 * s, 300 * s, 120 * s, 16, 16, 16, 16, s, 0xffffffff);
}

static void test_scene(const char *out_dir)
{
    hud::img::Pixels atlas;
    hud::img::grey(hud::font::ATLAS_PNG, hud::font::ATLAS_PNG_SIZE, atlas);
    hud::Frame f = frame(1920, 1080);
    scene(f);
    CHECK(f.verts.size() > 1000 && !f.dropped, "the probe scene tessellates");
    if (!out_dir)
        return;
    int W = 1920, H = 1080;
    std::vector<float> px(size_t(W) * H * 3);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float *p = &px[(size_t(y) * W + x) * 3];
            p[0] = p[1] = p[2] = (x / 64 + y / 64) % 2 ? 0.35f : 0.45f;
        }
    raster(f, atlas, px, W, H);
    std::vector<uint8_t> rgb(px.size());
    for (size_t i = 0; i < px.size(); i++)
        rgb[i] = uint8_t(std::clamp(px[i], 0.0f, 1.0f) * 255 + 0.5f);
    std::string png = hud::png::encode_rgb(rgb.data(), W, H);
    std::string path = std::string(out_dir) + "/hud-scene.png";
    if (FILE *fp = fopen(path.c_str(), "wb")) {
        fwrite(png.data(), 1, png.size(), fp);
        fclose(fp);
        printf("wrote %s\n", path.c_str());
    }
}

static void test_guard()
{
    using namespace hud::guard;
    CHECK(fg_module("sl.dlss_g.dll") && fg_module("C:\\Games\\F1 25\\NVNGX_DLSSG.DLL"), "dlss frame generation modules count");
    CHECK(fg_module("amd_fidelityfx_dx12.dll") && fg_module("libxess_fg.dll"), "fsr and xess frame generation modules count");
    CHECK(!fg_module("sl.interposer.dll") && !fg_module("nvngx_dlss.dll") && !fg_module("F1_25.exe"),
          "the interposer, dlss super resolution and the game do not");
    CHECK(fg_in({"swofty_core.dll", "sl.interposer.dll", "F1_25.exe"}).empty(), "a plain creation stack has no frame generation");
    CHECK(fg_in({"swofty_core.dll", "nvngx_dlssg.dll", "sl.dlss_g.dll", "F1_25.exe"}) == "nvngx_dlssg.dll",
          "the innermost frame generation module is named");
    CHECK(fg_in({"sl.dlss_g.dll", "sl.interposer.dll", "F1_25.exe"}).empty() &&
              streamline_in({"sl.dlss_g.dll", "sl.interposer.dll", "F1_25.exe"}),
          "streamline's frame generation hook alone is not a frame generation swapchain");
    std::string hw = "<x>\n\t<antialiasing taa=\"true\" dlss=\"false\" />\n\t<frame_gen mode=\"0\" />\n\t"
                     "<multi_frame_gen value=\"0\" />\n</x>";
    FgSetting fs = fg_setting(hw);
    CHECK(fs.known && fs.mode == 0 && fs.multi == 0 && !fs.on(), "frame_gen mode 0 reads as off");
    fs = fg_setting("<frame_gen mode=\"1\" /><multi_frame_gen value=\"2\" />");
    CHECK(fs.known && fs.mode == 1 && fs.multi == 2 && fs.on(), "frame_gen mode 1 reads as on");
    CHECK(fg_setting("<frame_gen />").on() && fg_setting("").on(), "a missing frame_gen mode reads as on");
    CHECK(hdr_mode("<resolution>\n\t\t\t<hdr mode=\"1\" peakNits=\"1000\" />") == 1 && hdr_mode("<x/>") == -1,
          "the game's hdr mode reads from hardware settings");
    CHECK(output_for(24, 12, 0) == HDR10_PQ && output_for(24, 0, 1) == SDR && output_for(24, COLOR_SPACE_UNSET, 1) == HDR10_PQ &&
              output_for(24, COLOR_SPACE_UNSET, 0) == SDR && output_for(24, COLOR_SPACE_UNSET, -1) == SDR,
          "a 10-bit swapchain is HDR10 from SetColorSpace1, or from the game's hdr mode when no colour space was set");
    CHECK(output_for(10, COLOR_SPACE_UNSET, 0) == SCRGB && output_for(10, 12, 0) == HDR10_PQ && output_for(28, 12, 1) == SDR,
          "fp16 is scRGB unless PQ was set, and 8-bit is SDR");
    CHECK(chain({"swofty_core.dll", "dxgi.dll", "sl.interposer.dll", "sl.interposer.dll", "", "F1_25.exe", "KERNEL32.DLL"},
                "swofty_core.dll", "dxgi.dll") == "sl.interposer.dll <- F1_25.exe <- KERNEL32.DLL",
          "the caller chain drops the core, the implementation, repeats and unknown frames");
    CHECK(chain({"swofty_core.dll"}, "swofty_core.dll", "") == "unknown", "an empty chain reads unknown");
    CHECK(!removal_after_draw(5000, 0), "a removal with no overlay draw is not attributed");
    CHECK(removal_after_draw(15000, 10000), "a removal 5 s after a draw is attributed");
    CHECK(removal_after_draw(20000, 10000) && !removal_after_draw(20001, 10000), "the window is 10 s");
}

// Models d3d12hooks::exec_original: the hook sits on the native queue's ExecuteCommandLists (vtable
// slot 10) and its trampoline runs that body, which reads this as a native queue and each list as a
// native list. Streamline's queue proxy (vtable, refcount, native queue at +0x10) is what a swapchain
// created through Streamline's factory records as its queue; its device makes list proxies of the
// same shape, which its ExecuteCommandLists unwraps. Resources are never wrapped.
namespace dispatch {

constexpr size_t EXEC = 10;
constexpr uint32_t NATIVE = 0x51554555;

struct Native {
    const void *const *vt;
    uint32_t magic;
    unsigned executed;
};

struct Proxy {
    const void *const *vt;
    uint32_t refs;
    Native *base;
};

using ExecFn = void (*)(void *self, unsigned n, void *const *lists);

unsigned misread;

bool native(const void *object)
{
    uint32_t magic;
    memcpy(&magic, static_cast<const char *>(object) + offsetof(Native, magic), sizeof magic);
    return magic == NATIVE;
}

void native_exec(void *self, unsigned n, void *const *lists)
{
    for (unsigned i = 0; i < n; i++)
        if (!native(lists[i])) {
            misread++;
            return;
        }
    if (!native(self)) {
        misread++;
        return;
    }
    static_cast<Native *>(self)->executed += n;
}

void through_vtable(void *self, unsigned n, void *const *lists)
{
    reinterpret_cast<ExecFn>((*static_cast<const void *const *const *>(self))[EXEC])(self, n, lists);
}

const void *list_proxy_vt[EXEC + 1];

void proxy_exec(void *self, unsigned n, void *const *lists)
{
    std::vector<void *> unwrapped(lists, lists + n);
    for (auto &l : unwrapped)
        if (*static_cast<const void *const *const *>(l) == list_proxy_vt)
            l = static_cast<Proxy *>(l)->base;
    through_vtable(static_cast<Proxy *>(self)->base, n, unwrapped.data());
}

void other_hook(void *self, unsigned n, void *const *lists)
{
    native_exec(self, n, lists);
}

const void *hooked()
{
    return reinterpret_cast<const void *>(&native_exec);
}

void exec_original(void *q, unsigned n, void *const *lists)
{
    if (hud::guard::vtable_slot_is(q, EXEC, hooked()))
        native_exec(q, n, lists);
    else
        through_vtable(q, n, lists);
}

}

static void test_original_dispatch()
{
    using namespace dispatch;
    const void *native_vt[EXEC + 1] = {}, *proxy_vt[EXEC + 1] = {}, *rehooked_vt[EXEC + 1] = {}, *list_vt[1] = {};
    native_vt[EXEC] = hooked();
    proxy_vt[EXEC] = reinterpret_cast<const void *>(&proxy_exec);
    rehooked_vt[EXEC] = reinterpret_cast<const void *>(&other_hook);
    Native game{native_vt, NATIVE, 0}, rehooked{rehooked_vt, NATIVE, 0};
    Proxy proxy{proxy_vt, 8, &game};
    Native list{list_vt, NATIVE, 0};
    Proxy list_proxy{list_proxy_vt, 1, &list};
    void *native_list[] = {&list}, *proxied_list[] = {&list_proxy}, *four[] = {&list, &list, &list, &list};

    CHECK(hud::guard::vtable_slot_is(&game, EXEC, hooked()), "a native queue runs the hooked body");
    CHECK(!hud::guard::vtable_slot_is(&proxy, EXEC, hooked()), "streamline's queue proxy does not");
    CHECK(!hud::guard::vtable_slot_is(&rehooked, EXEC, hooked()), "a queue whose slot another hook moved does not");
    CHECK(!hud::guard::vtable_slot_is(nullptr, EXEC, hooked()) && !hud::guard::vtable_slot_is(&game, EXEC, nullptr),
          "no object or no hook is never the hooked body");

    misread = 0;
    native_exec(&proxy, 1, native_list);
    CHECK(misread == 1 && game.executed == 0,
          "the hooked body given the proxy reads the proxy as a queue (the crash at Present)");

    misread = 0;
    exec_original(&game, 1, native_list);
    exec_original(&proxy, 2, four);
    exec_original(&rehooked, 4, four);
    CHECK(misread == 0, "exec_original never hands the hooked body anything but a native queue");
    CHECK(game.executed == 3, "the proxy's lists reach the native queue behind it");
    CHECK(rehooked.executed == 4, "a queue hooked by someone else still runs, through its vtable");

    misread = 0;
    game.executed = 0;
    exec_original(&game, 1, proxied_list);
    CHECK(misread == 1 && game.executed == 0,
          "a list made on the queue proxy's device crashes the native queue, which passes the slot check");
    exec_original(&proxy, 1, proxied_list);
    CHECK(misread == 1 && game.executed == 1, "streamline's queue proxy unwraps its own list proxies");
    misread = 0;
    exec_original(&game, 1, native_list);
    exec_original(&proxy, 1, native_list);
    CHECK(misread == 0 && game.executed == 3,
          "a list made on the device of a game resource runs on the native queue and through the proxy");
}

static void test_png(const char *out)
{
    using namespace hud::png;
    uint32_t w = 97, h = 61;
    std::vector<uint8_t> px(size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            uint8_t *p = &px[(size_t(y) * w + x) * 4];
            p[0] = uint8_t(x * 2), p[1] = uint8_t(y * 4), p[2] = uint8_t((x / 8 + y / 8) % 2 ? 255 : 0), p[3] = 255;
        }
    std::vector<uint8_t> rgb;
    CHECK(to_rgb(Fmt::rgba8, px.data(), w * 4, w, h, rgb) && rgb.size() == size_t(w) * h * 3 && rgb[3] == 2 &&
              rgb[size_t(w) * 3 + 1] == 4,
          "rgba8 rows convert to rgb");
    std::vector<uint8_t> bgra(px);
    for (size_t i = 0; i < bgra.size(); i += 4)
        std::swap(bgra[i], bgra[i + 2]);
    std::vector<uint8_t> rgb2;
    CHECK(to_rgb(Fmt::bgra8, bgra.data(), w * 4, w, h, rgb2) && rgb2 == rgb, "bgra8 converts to the same rgb");
    uint32_t v = 1023u | (512u << 10) | (3u << 30);
    std::vector<uint8_t> one;
    CHECK(to_rgb(Fmt::rgb10a2, reinterpret_cast<uint8_t *>(&v), 4, 1, 1, one) && one[0] == 255 && one[1] == 128 &&
              one[2] == 0,
          "r10g10b10a2 keeps the top 8 bits");
    uint16_t hf[4] = {0x3c00, 0x3800, 0x0000, 0x3c00};
    CHECK(to_rgb(Fmt::rgba16f, reinterpret_cast<uint8_t *>(hf), 8, 1, 1, one) && one[0] == 255 && one[1] == 188 &&
              one[2] == 0,
          "scRGB half floats encode to sRGB");
    CHECK(fmt_of(24) == Fmt::rgb10a2 && fmt_of(10) == Fmt::rgba16f && fmt_of(28) == Fmt::rgba8 &&
              fmt_of(87) == Fmt::bgra8 && fmt_of(2) == Fmt::unknown,
          "swapchain formats map to converters");
    std::string png = encode_rgb(rgb.data(), w, h);
    CHECK(png.compare(0, 8, std::string("\x89PNG\r\n\x1a\n", 8)) == 0 && png.find("IHDR") == 12 &&
              png.find("IEND") != std::string::npos,
          "the capture writes a PNG signature, IHDR and IEND");
    if (!out)
        return;
    if (FILE *f = fopen(out, "wb")) {
        fwrite(png.data(), 1, png.size(), f);
        fclose(f);
    }
    if (FILE *f = fopen((std::string(out) + ".rgb").c_str(), "wb")) {
        fwrite(rgb.data(), 1, rgb.size(), f);
        fclose(f);
    }
}

int main(int argc, char **argv)
{
    test_font();
    test_measure();
    test_culling();
    test_v1_geometry();
    test_v2_geometry();
    test_text_effects();
    test_batches();
    test_transforms();
    test_layout_helpers();
    test_nine_slice();
    test_cap();
    test_text_cache();
    test_input();
    test_registry();
    test_build_dispatch();
    test_image_registry();
    test_image_decode();
    test_guard();
    test_original_dispatch();
    test_png(argc > 1 ? argv[1] : nullptr);
    test_scene(argc > 2 ? argv[2] : nullptr);
    if (failures)
        printf("\n%d check(s) FAILED\n", failures);
    else
        printf("\nall hud draw-list checks passed\n");
    return failures ? 1 : 0;
}
