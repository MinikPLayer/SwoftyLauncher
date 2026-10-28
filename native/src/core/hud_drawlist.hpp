#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "hud_font.hpp"
#include "swofty_api.h"

// The overlay's portable draw list: paint callbacks tessellate straight into triangles in framebuffer
// pixels, grouped into batches that share a texture and a clip rect. v1 calls keep their hard-edged
// geometry; v2 shapes carry a one-pixel anti-aliasing fringe.

namespace hud {

constexpr uint32_t MAX_VERTS = 1u << 17;
constexpr size_t MAX_STACK = 64;
constexpr float SAFE_INSET = 0.035f;
constexpr float AA = 1.0f;
constexpr float PI = 3.14159265358979f;

enum Kind : uint32_t { SOLID = 0, IMAGE = 1, SDF = 2 };

constexpr int TEX_NONE = -1;
constexpr int TEX_FONT = 0;

struct Vertex {
    float x, y, u, v;
    uint32_t rgba;
    uint32_t mode;
};

struct Clip {
    float x0, y0, x1, y1;
    bool operator==(const Clip &) const = default;
};

struct Batch {
    uint32_t first, count;
    int tex;
    Clip clip;
};

struct Xform {
    float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;

    void apply(float x, float y, float &ox, float &oy) const
    {
        ox = a * x + c * y + e;
        oy = b * x + d * y + f;
    }

    float scale() const { return std::sqrt(std::fabs(a * d - b * c)); }

    bool plain() const { return a == 1 && b == 0 && c == 0 && d == 1; }

    Xform then(const Xform &m) const
    {
        return {a * m.a + c * m.b, b * m.a + d * m.b, a * m.c + c * m.d, b * m.c + d * m.d, a * m.e + c * m.f + e,
                b * m.e + d * m.f + f};
    }
};

struct Pt {
    float x, y;
};

struct TextStyle {
    float px = 16;
    uint32_t rgba = 0xffffffff;
    uint32_t align = 0;
    float tracking = 0;
    float line_height = 1.2f;
    uint32_t outline_rgba = 0;
    float outline_px = 0;
    uint32_t shadow_rgba = 0;
    float shadow_dx = 0, shadow_dy = 0, shadow_blur = 0;
    float max_width = 0;
};

struct Line {
    size_t begin, end;
    float width;
};

struct Quad {
    float x0, y0, x1, y1, u0, v0, u1, v1;
};

struct LaidLine {
    float width;
    uint32_t first, count;
};

// A string laid out at one style: glyph quads relative to their line's pen origin on the baseline.
struct Layout {
    std::vector<Quad> quads;
    std::vector<LaidLine> lines;
    uint64_t used = 0;
};

constexpr size_t TEXT_CACHE_MAX = 512;
constexpr uint64_t TEXT_CACHE_KEEP = 120;

struct TextCache {
    std::unordered_map<std::string, Layout> map;
    uint64_t frame = 0;
    uint64_t hits = 0, misses = 0;
    std::string key;

    void trim()
    {
        if (map.size() <= TEXT_CACHE_MAX)
            return;
        for (auto it = map.begin(); it != map.end();)
            it = frame - it->second.used > TEXT_CACHE_KEEP ? map.erase(it) : std::next(it);
        if (map.size() > TEXT_CACHE_MAX)
            map.clear();
    }
};

struct Frame {
    float width = 0, height = 0;
    std::vector<Vertex> verts;
    std::vector<Batch> batches;
    Xform xform;
    Clip clip{};
    float opacity = 1;
    std::vector<Xform> xforms;
    std::vector<Clip> clips;
    std::vector<float> opacities;
    uint32_t xform_over = 0, clip_over = 0, opacity_over = 0;
    uint32_t dropped = 0;
    std::vector<Pt> path, local, ring_in, ring_out;
    std::vector<uint32_t> cols;
    std::vector<Line> lines;
    TextCache text_cache;
    std::string owner;
};

inline void reset_state(Frame &f)
{
    f.xform = {};
    f.clip = {0, 0, f.width, f.height};
    f.opacity = 1;
    f.xforms.clear();
    f.clips.clear();
    f.opacities.clear();
    f.xform_over = f.clip_over = f.opacity_over = 0;
}

inline void begin(Frame &f, float width, float height)
{
    f.width = width;
    f.height = height;
    f.verts.clear();
    f.batches.clear();
    f.dropped = 0;
    f.text_cache.frame++;
    f.text_cache.trim();
    reset_state(f);
}

inline bool rgba_visible(uint32_t rgba)
{
    return (rgba & 0xff) != 0;
}

inline uint32_t pack(uint32_t rrggbbaa, float opacity)
{
    uint32_t r = (rrggbbaa >> 24) & 0xff, g = (rrggbbaa >> 16) & 0xff, b = (rrggbbaa >> 8) & 0xff, a = rrggbbaa & 0xff;
    if (opacity < 1)
        a = uint32_t(std::lround(float(a) * std::max(0.0f, opacity)));
    return r | (g << 8) | (b << 16) | (a << 24);
}

inline uint32_t clear_alpha(uint32_t packed)
{
    return packed & 0x00ffffffu;
}

inline uint32_t scale_alpha(uint32_t packed, float k)
{
    uint32_t a = uint32_t(std::lround(float(packed >> 24) * std::clamp(k, 0.0f, 1.0f)));
    return clear_alpha(packed) | a << 24;
}

inline bool clipped_out(const Frame &f)
{
    return f.clip.x1 <= f.clip.x0 || f.clip.y1 <= f.clip.y0 || f.opacity <= 0;
}

inline bool reserve(Frame &f, size_t n, int tex)
{
    if (f.verts.size() + n > MAX_VERTS) {
        f.dropped++;
        return false;
    }
    uint32_t at = uint32_t(f.verts.size());
    if (!f.batches.empty()) {
        Batch &b = f.batches.back();
        if (b.first + b.count == at && b.clip == f.clip && (b.tex == TEX_NONE || tex == TEX_NONE || b.tex == tex)) {
            if (b.tex == TEX_NONE)
                b.tex = tex;
            b.count += uint32_t(n);
            return true;
        }
    }
    f.batches.push_back({at, uint32_t(n), tex, f.clip});
    return true;
}

inline void vtx(Frame &f, float x, float y, uint32_t col, float u = 0, float v = 0, uint32_t mode = SOLID)
{
    f.verts.push_back({x, y, u, v, col, mode});
}

inline void tri(Frame &f, Pt a, Pt b, Pt c, uint32_t ca, uint32_t cb, uint32_t cc)
{
    vtx(f, a.x, a.y, ca);
    vtx(f, b.x, b.y, cb);
    vtx(f, c.x, c.y, cc);
}

inline void quad_flat(Frame &f, const Pt *p, uint32_t col)
{
    if (!reserve(f, 6, TEX_NONE))
        return;
    tri(f, p[0], p[1], p[3], col, col, col);
    tri(f, p[1], p[2], p[3], col, col, col);
}

inline void rect_points(const Xform &m, float x, float y, float w, float h, Pt *out)
{
    m.apply(x, y, out[0].x, out[0].y);
    m.apply(x + w, y, out[1].x, out[1].y);
    m.apply(x + w, y + h, out[2].x, out[2].y);
    m.apply(x, y + h, out[3].x, out[3].y);
}

inline float signed_area(const std::vector<Pt> &p)
{
    float a = 0;
    for (size_t i = 0, n = p.size(); i < n; i++) {
        const Pt &u = p[i], &v = p[(i + 1) % n];
        a += u.x * v.y - v.x * u.y;
    }
    return a * 0.5f;
}

inline Pt edge_normal(Pt a, Pt b)
{
    float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
    if (len <= 1e-6f)
        return {0, 0};
    return {dy / len, -dx / len};
}

inline Pt miter(Pt n0, Pt n1)
{
    Pt m{(n0.x + n1.x) * 0.5f, (n0.y + n1.y) * 0.5f};
    float d2 = m.x * m.x + m.y * m.y;
    if (d2 > 1e-6f) {
        float inv = std::min(1.0f / d2, 100.0f);
        m.x *= inv;
        m.y *= inv;
    }
    return m;
}

// Fills a convex polygon given in framebuffer pixels: an opaque fan inset half a pixel and a fringe
// that fades out half a pixel outside. cols holds one packed colour per point.
inline void fill_convex(Frame &f, const std::vector<Pt> &pts, const std::vector<uint32_t> &cols)
{
    size_t n = pts.size();
    if (n < 3 || cols.size() != n)
        return;
    float area = signed_area(pts);
    if (std::fabs(area) < 1e-4f)
        return;
    if (!reserve(f, 3 * (n - 2) + 6 * n, TEX_NONE))
        return;
    float k = AA * 0.5f * (area > 0 ? 1.0f : -1.0f);
    std::vector<Pt> &in = f.ring_in, &out = f.ring_out;
    in.resize(n);
    out.resize(n);
    for (size_t i = 0; i < n; i++) {
        Pt m = miter(edge_normal(pts[(i + n - 1) % n], pts[i]), edge_normal(pts[i], pts[(i + 1) % n]));
        in[i] = {pts[i].x - m.x * k, pts[i].y - m.y * k};
        out[i] = {pts[i].x + m.x * k, pts[i].y + m.y * k};
    }
    for (size_t i = 1; i + 1 < n; i++)
        tri(f, in[0], in[i], in[i + 1], cols[0], cols[i], cols[i + 1]);
    for (size_t i = 0; i < n; i++) {
        size_t j = (i + 1) % n;
        uint32_t ci = cols[i], cj = cols[j];
        tri(f, in[i], out[i], out[j], ci, clear_alpha(ci), clear_alpha(cj));
        tri(f, in[i], out[j], in[j], ci, clear_alpha(cj), cj);
    }
}

inline void fill_convex(Frame &f, const std::vector<Pt> &pts, uint32_t col)
{
    f.cols.assign(pts.size(), col);
    fill_convex(f, pts, f.cols);
}

// Strokes a path given in framebuffer pixels: an opaque core of thickness - 1 with a one-pixel fringe
// on each side; a path thinner than a pixel is a faded hairline.
inline void stroke(Frame &f, const std::vector<Pt> &pts, bool closed, float thickness, uint32_t col)
{
    size_t n = pts.size();
    if (n < 2 || thickness <= 0)
        return;
    if (closed && n < 3)
        closed = false;
    size_t segs = closed ? n : n - 1;
    bool thick = thickness > AA;
    if (!reserve(f, segs * (thick ? 18 : 12), TEX_NONE))
        return;
    std::vector<Pt> &dm = f.ring_in;
    dm.resize(n);
    for (size_t i = 0; i < n; i++) {
        Pt n0 = !closed && i == 0 ? edge_normal(pts[0], pts[1]) : edge_normal(pts[(i + n - 1) % n], pts[i]);
        Pt n1 = !closed && i == n - 1 ? edge_normal(pts[n - 2], pts[n - 1]) : edge_normal(pts[i], pts[(i + 1) % n]);
        dm[i] = miter(n0, n1);
    }
    uint32_t core = thick ? col : scale_alpha(col, thickness), edge = clear_alpha(col);
    float half = thick ? (thickness - AA) * 0.5f : 0;
    auto at = [&](size_t k, float off) { return Pt{pts[k].x + dm[k].x * off, pts[k].y + dm[k].y * off}; };
    for (size_t s = 0; s < segs; s++) {
        size_t i = s, j = (s + 1) % n;
        if (thick) {
            Pt ao = at(i, half + AA), ac = at(i, half), bc = at(i, -half), bo = at(i, -half - AA);
            Pt co = at(j, half + AA), cc = at(j, half), dc = at(j, -half), d_o = at(j, -half - AA);
            tri(f, ao, co, cc, edge, edge, core);
            tri(f, ao, cc, ac, edge, core, core);
            tri(f, ac, cc, dc, core, core, core);
            tri(f, ac, dc, bc, core, core, core);
            tri(f, bc, dc, d_o, core, core, edge);
            tri(f, bc, d_o, bo, core, edge, edge);
        } else {
            Pt a = pts[i], b = pts[j], ao = at(i, AA), bo = at(j, AA), ai = at(i, -AA), bi = at(j, -AA);
            tri(f, ao, bo, b, edge, edge, core);
            tri(f, ao, b, a, edge, core, core);
            tri(f, a, b, bi, core, core, edge);
            tri(f, a, bi, ai, core, edge, edge);
        }
    }
}

inline int arc_segments(float radius_px, float angle)
{
    if (radius_px <= 0.5f)
        return 1;
    float step = 2 * std::acos(std::clamp(1 - 0.25f / radius_px, -1.0f, 1.0f));
    int n = int(std::ceil(std::fabs(angle) / std::max(step, 0.02f)));
    return std::clamp(n, 1, 256);
}

inline void arc_local(std::vector<Pt> &out, float cx, float cy, float r, float a0, float a1, int segs)
{
    for (int i = 0; i <= segs; i++) {
        float a = a0 + (a1 - a0) * float(i) / float(segs);
        out.push_back({cx + r * std::cos(a), cy + r * std::sin(a)});
    }
}

inline void rounded_local(std::vector<Pt> &out, float x, float y, float w, float h, float r, float scale)
{
    out.clear();
    r = std::clamp(r, 0.0f, std::min(w, h) * 0.5f);
    if (r * scale <= 0.5f) {
        out.assign({{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}});
        return;
    }
    int segs = arc_segments(r * scale, PI / 2);
    arc_local(out, x + r, y + r, r, PI, 1.5f * PI, segs);
    arc_local(out, x + w - r, y + r, r, 1.5f * PI, 2 * PI, segs);
    arc_local(out, x + w - r, y + h - r, r, 0, 0.5f * PI, segs);
    arc_local(out, x + r, y + h - r, r, 0.5f * PI, PI, segs);
    auto same = [](Pt a, Pt b) { return std::fabs(a.x - b.x) + std::fabs(a.y - b.y) <= 1e-4f; };
    out.erase(std::unique(out.begin(), out.end(), same), out.end());
    if (out.size() > 1 && same(out.front(), out.back()))
        out.pop_back();
}

inline void to_screen(const Xform &m, const std::vector<Pt> &local, std::vector<Pt> &out)
{
    out.resize(local.size());
    for (size_t i = 0; i < local.size(); i++)
        m.apply(local[i].x, local[i].y, out[i].x, out[i].y);
}

// ----- v1 calls ---------------------------------------------------------------------------------

inline void rect(Frame &f, float x, float y, float w, float h, uint32_t rgba)
{
    if (w <= 0 || h <= 0 || !rgba_visible(rgba) || clipped_out(f))
        return;
    Pt p[4];
    rect_points(f.xform, x, y, w, h, p);
    quad_flat(f, p, pack(rgba, f.opacity));
}

inline void rect_outline(Frame &f, float x, float y, float w, float h, float thickness, uint32_t rgba)
{
    if (w <= 0 || h <= 0 || thickness <= 0 || !rgba_visible(rgba) || clipped_out(f))
        return;
    float t = thickness;
    rect(f, x, y, w, t, rgba);
    rect(f, x, y + h - t, w, t, rgba);
    rect(f, x, y + t, t, h - 2 * t, rgba);
    rect(f, x + w - t, y + t, t, h - 2 * t, rgba);
}

inline void line(Frame &f, float x0, float y0, float x1, float y1, float thickness, uint32_t rgba)
{
    if (thickness <= 0 || !rgba_visible(rgba) || (x0 == x1 && y0 == y1) || clipped_out(f))
        return;
    float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
    float nx = -dy / len * thickness * 0.5f, ny = dx / len * thickness * 0.5f;
    Pt p[4];
    f.xform.apply(x0 + nx, y0 + ny, p[0].x, p[0].y);
    f.xform.apply(x1 + nx, y1 + ny, p[1].x, p[1].y);
    f.xform.apply(x1 - nx, y1 - ny, p[2].x, p[2].y);
    f.xform.apply(x0 - nx, y0 - ny, p[3].x, p[3].y);
    quad_flat(f, p, pack(rgba, f.opacity));
}

inline void image_uv(Frame &f, int slot, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                     uint32_t tint)
{
    if (slot < 0 || w <= 0 || h <= 0 || !rgba_visible(tint) || clipped_out(f))
        return;
    if (!reserve(f, 6, slot + 1))
        return;
    Pt p[4];
    rect_points(f.xform, x, y, w, h, p);
    uint32_t col = pack(tint, f.opacity);
    float u[4] = {u0, u1, u1, u0}, v[4] = {v0, v0, v1, v1};
    for (int k : {0, 1, 3, 1, 2, 3})
        vtx(f, p[k].x, p[k].y, col, u[k], v[k], IMAGE);
}

inline void image(Frame &f, int slot, float x, float y, float w, float h, uint32_t tint)
{
    image_uv(f, slot, x, y, w, h, 0, 0, 1, 1, tint);
}

// ----- text -------------------------------------------------------------------------------------

// Splits text into lines at '\n' and, with a max width, at the last space that keeps a line inside it
// (mid-word when a word alone is wider). A line's width is its pen advance.
inline void layout(const TextStyle &s, std::string_view text, std::vector<Line> &lines)
{
    lines.clear();
    size_t i = 0, begin = 0, space = std::string_view::npos, prev_end = 0;
    float width = 0, at_space = 0;
    uint32_t prev = 0;
    auto cut = [&](size_t end, float w, size_t next) {
        lines.push_back({begin, end, w});
        begin = i = next;
        width = 0;
        prev = 0;
        space = std::string_view::npos;
    };
    while (i < text.size()) {
        size_t at = i;
        uint32_t cp = font::decode(text, i);
        if (cp == '\n') {
            cut(at, width, i);
            continue;
        }
        if (cp < 0x20)
            continue;
        float k = prev ? font::kern(prev, cp) * s.px + s.tracking : 0;
        const font::Glyph *g = font::glyph(cp);
        float adv = g ? g->advance * s.px : 0;
        if (s.max_width > 0 && width + k + adv > s.max_width && at > begin) {
            if (cp == ' ')
                cut(at, width, i);
            else if (space != std::string_view::npos)
                cut(space, at_space, space + 1);
            else
                cut(prev_end, width, prev_end);
            continue;
        }
        if (cp == ' ') {
            space = at;
            at_space = width;
        }
        width += k + adv;
        prev = cp;
        prev_end = i;
    }
    lines.push_back({begin, text.size(), width});
}

inline float line_step(const TextStyle &s)
{
    return (s.line_height > 0 ? s.line_height : 1.2f) * s.px;
}

inline void measure(const TextStyle &s, std::string_view text, float *width, float *height)
{
    static thread_local std::vector<Line> lines;
    float w = 0;
    size_t n = 1;
    if (s.px > 0) {
        layout(s, text, lines);
        for (auto &l : lines)
            w = std::max(w, l.width);
        n = lines.size();
    }
    if (width)
        *width = w;
    if (height)
        *height = s.px > 0 ? s.px + float(n - 1) * line_step(s) : 0;
}

inline void measure(float font_size, std::string_view s, float *width, float *height)
{
    TextStyle st;
    st.px = font_size;
    measure(st, s, width, height);
}

inline uint32_t sdf_mode(float dilate, float soft)
{
    uint32_t d = uint32_t(std::clamp(dilate, 0.0f, 0.5f) * 510.0f + 0.5f);
    uint32_t s = uint32_t(std::clamp(soft, 0.0f, 31.0f) * 8.0f + 0.5f);
    return SDF | d << 8 | s << 16;
}

inline void lay_out(const TextStyle &s, std::string_view text, std::vector<Line> &lines, Layout &out)
{
    layout(s, text, lines);
    out.quads.clear();
    out.lines.clear();
    const float iw = 1.0f / float(font::ATLAS_W), ih = 1.0f / float(font::ATLAS_H);
    for (const Line &l : lines) {
        LaidLine ll{l.width, uint32_t(out.quads.size()), 0};
        float pen = 0;
        uint32_t prev = 0;
        for (size_t i = l.begin; i < l.end;) {
            uint32_t cp = font::decode(text, i);
            if (cp < 0x20)
                continue;
            if (prev)
                pen += font::kern(prev, cp) * s.px + s.tracking;
            prev = cp;
            const font::Glyph *g = font::glyph(cp);
            if (!g)
                continue;
            if (g->u1 > g->u0 && g->v1 > g->v0)
                out.quads.push_back({pen + g->x0 * s.px, g->y0 * s.px, pen + g->x1 * s.px, g->y1 * s.px, g->u0 * iw,
                                     g->v0 * ih, g->u1 * iw, g->v1 * ih});
            pen += g->advance * s.px;
        }
        ll.count = uint32_t(out.quads.size()) - ll.first;
        out.lines.push_back(ll);
    }
}

inline const Layout &cached_layout(Frame &f, const TextStyle &s, std::string_view text)
{
    TextCache &c = f.text_cache;
    c.key.assign(text);
    const float params[4] = {s.px, s.tracking, s.max_width, s.line_height};
    c.key.append(reinterpret_cast<const char *>(params), sizeof params);
    auto [it, fresh] = c.map.try_emplace(c.key);
    if (fresh) {
        c.misses++;
        lay_out(s, text, f.lines, it->second);
    } else {
        c.hits++;
    }
    it->second.used = c.frame;
    return it->second;
}

inline void glyph_run(Frame &f, const TextStyle &s, const Layout &lay, float top, float x, float dx, float dy,
                      uint32_t col, uint32_t mode)
{
    float step = line_step(s);
    bool snap = f.xform.plain();
    uint32_t h = s.align & 0x0f;
    for (size_t li = 0; li < lay.lines.size(); li++) {
        const LaidLine &l = lay.lines[li];
        float pen = x - (h == SWOFTY_TEXT_CENTER ? l.width * 0.5f : h == SWOFTY_TEXT_RIGHT ? l.width : 0) + dx;
        float base = top + float(li) * step + font::BASELINE * s.px + dy;
        if (snap) {
            pen = std::round(pen + f.xform.e) - f.xform.e;
            base = std::round(base + f.xform.f) - f.xform.f;
        }
        if (!l.count || !reserve(f, size_t(l.count) * 6, TEX_FONT))
            continue;
        for (uint32_t q = l.first; q < l.first + l.count; q++) {
            const Quad &g = lay.quads[q];
            Pt p[4];
            rect_points(f.xform, pen + g.x0, base + g.y0, g.x1 - g.x0, g.y1 - g.y0, p);
            float u[4] = {g.u0, g.u1, g.u1, g.u0}, v[4] = {g.v0, g.v0, g.v1, g.v1};
            for (int k : {0, 1, 3, 1, 2, 3})
                vtx(f, p[k].x, p[k].y, col, u[k], v[k], mode);
        }
    }
}

inline void text(Frame &f, float x, float y, const TextStyle &s, std::string_view text)
{
    if (s.px <= 0 || text.empty() || clipped_out(f))
        return;
    const Layout &lay = cached_layout(f, s, text);
    float height = s.px + float(lay.lines.size() - 1) * line_step(s);
    uint32_t v = s.align & 0xf0;
    float top = v == SWOFTY_TEXT_MIDDLE     ? y - height * 0.5f
                : v == SWOFTY_TEXT_BASELINE ? y - font::BASELINE * s.px
                : v == SWOFTY_TEXT_BOTTOM   ? y - height
                                            : y;
    float scale = s.px / font::ATLAS_EM * f.xform.scale();
    float per_px = scale > 0 ? 1.0f / (2 * font::SPREAD * scale) : 0;
    bool outline = s.outline_px > 0 && rgba_visible(s.outline_rgba);
    if (rgba_visible(s.shadow_rgba))
        glyph_run(f, s, lay, top, x, s.shadow_dx, s.shadow_dy, pack(s.shadow_rgba, f.opacity),
                  sdf_mode(outline ? s.outline_px * per_px : 0, s.shadow_blur));
    if (outline)
        glyph_run(f, s, lay, top, x, 0, 0, pack(s.outline_rgba, f.opacity), sdf_mode(s.outline_px * per_px, 0));
    if (rgba_visible(s.rgba))
        glyph_run(f, s, lay, top, x, 0, 0, pack(s.rgba, f.opacity), sdf_mode(0, 0));
}

inline void text(Frame &f, float x, float y, float font_size, uint32_t rgba, std::string_view s)
{
    if (font_size <= 0 || !rgba_visible(rgba) || s.empty())
        return;
    TextStyle st;
    st.px = font_size;
    st.rgba = rgba;
    text(f, x, y, st, s);
}

// ----- v2 shapes --------------------------------------------------------------------------------

inline void rect_rounded(Frame &f, float x, float y, float w, float h, float radius, uint32_t rgba)
{
    if (w <= 0 || h <= 0 || !rgba_visible(rgba) || clipped_out(f))
        return;
    rounded_local(f.local, x, y, w, h, radius, f.xform.scale());
    to_screen(f.xform, f.local, f.path);
    fill_convex(f, f.path, pack(rgba, f.opacity));
}

inline void rect_rounded_outline(Frame &f, float x, float y, float w, float h, float radius, float thickness,
                                 uint32_t rgba)
{
    if (w <= 0 || h <= 0 || thickness <= 0 || !rgba_visible(rgba) || clipped_out(f))
        return;
    float t = std::min(thickness, std::min(w, h) * 0.5f), half = t * 0.5f;
    rounded_local(f.local, x + half, y + half, w - t, h - t, radius - half, f.xform.scale());
    to_screen(f.xform, f.local, f.path);
    stroke(f, f.path, true, t * f.xform.scale(), pack(rgba, f.opacity));
}

inline uint32_t lerp_rgba(uint32_t a, uint32_t b, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    uint32_t out = 0;
    for (int s = 0; s < 32; s += 8) {
        float ca = float((a >> s) & 0xff), cb = float((b >> s) & 0xff);
        out |= uint32_t(std::lround(ca + (cb - ca) * t)) << s;
    }
    return out;
}

inline void rect_gradient(Frame &f, float x, float y, float w, float h, float radius, uint32_t from, uint32_t to,
                          uint32_t flags)
{
    if (w <= 0 || h <= 0 || (!rgba_visible(from) && !rgba_visible(to)) || clipped_out(f))
        return;
    rounded_local(f.local, x, y, w, h, radius, f.xform.scale());
    to_screen(f.xform, f.local, f.path);
    bool horizontal = flags & SWOFTY_GRADIENT_HORIZONTAL;
    f.cols.resize(f.local.size());
    for (size_t i = 0; i < f.local.size(); i++)
        f.cols[i] = pack(lerp_rgba(from, to, horizontal ? (f.local[i].x - x) / w : (f.local[i].y - y) / h), f.opacity);
    fill_convex(f, f.path, f.cols);
}

inline void circle(Frame &f, float cx, float cy, float radius, uint32_t rgba)
{
    if (radius <= 0 || !rgba_visible(rgba) || clipped_out(f))
        return;
    int segs = std::max(8, arc_segments(radius * f.xform.scale(), 2 * PI));
    f.local.clear();
    arc_local(f.local, cx, cy, radius, 0, 2 * PI, segs);
    f.local.pop_back();
    to_screen(f.xform, f.local, f.path);
    fill_convex(f, f.path, pack(rgba, f.opacity));
}

inline void circle_outline(Frame &f, float cx, float cy, float radius, float thickness, uint32_t rgba)
{
    if (radius <= 0 || thickness <= 0 || !rgba_visible(rgba) || clipped_out(f))
        return;
    float t = std::min(thickness, radius);
    int segs = std::max(8, arc_segments(radius * f.xform.scale(), 2 * PI));
    f.local.clear();
    arc_local(f.local, cx, cy, radius - t * 0.5f, 0, 2 * PI, segs);
    f.local.pop_back();
    to_screen(f.xform, f.local, f.path);
    stroke(f, f.path, true, t * f.xform.scale(), pack(rgba, f.opacity));
}

inline void arc(Frame &f, float cx, float cy, float radius, float from, float to, float thickness, uint32_t rgba)
{
    if (radius <= 0 || thickness <= 0 || from == to || !rgba_visible(rgba) || clipped_out(f))
        return;
    to = std::clamp(to, from - 2 * PI, from + 2 * PI);
    bool full = std::fabs(to - from) >= 2 * PI - 1e-4f;
    int segs = std::max(2, arc_segments((radius + thickness * 0.5f) * f.xform.scale(), to - from));
    f.local.clear();
    arc_local(f.local, cx, cy, radius, from, to, segs);
    if (full)
        f.local.pop_back();
    to_screen(f.xform, f.local, f.path);
    stroke(f, f.path, full, thickness * f.xform.scale(), pack(rgba, f.opacity));
}

inline void polyline(Frame &f, const float *xy, uint32_t count, float thickness, uint32_t rgba, uint32_t flags)
{
    if (!xy || count < 2 || thickness <= 0 || !rgba_visible(rgba) || clipped_out(f))
        return;
    f.path.clear();
    for (uint32_t i = 0; i < count; i++) {
        Pt p;
        f.xform.apply(xy[2 * i], xy[2 * i + 1], p.x, p.y);
        if (f.path.empty() || std::fabs(p.x - f.path.back().x) + std::fabs(p.y - f.path.back().y) > 1e-3f)
            f.path.push_back(p);
    }
    bool closed = flags & SWOFTY_POLYLINE_CLOSED;
    if (closed && f.path.size() > 2 &&
        std::fabs(f.path[0].x - f.path.back().x) + std::fabs(f.path[0].y - f.path.back().y) <= 1e-3f)
        f.path.pop_back();
    stroke(f, f.path, closed, thickness * f.xform.scale(), pack(rgba, f.opacity));
}

inline void polygon(Frame &f, const float *xy, uint32_t count, uint32_t rgba)
{
    if (!xy || count < 3 || !rgba_visible(rgba) || clipped_out(f))
        return;
    f.path.resize(count);
    for (uint32_t i = 0; i < count; i++)
        f.xform.apply(xy[2 * i], xy[2 * i + 1], f.path[i].x, f.path[i].y);
    fill_convex(f, f.path, pack(rgba, f.opacity));
}

inline void image_nine(Frame &f, int slot, float iw, float ih, float x, float y, float w, float h, float l, float t,
                       float r, float b, float scale, uint32_t tint)
{
    if (slot < 0 || iw <= 0 || ih <= 0 || w <= 0 || h <= 0)
        return;
    l = std::clamp(l, 0.0f, iw);
    r = std::clamp(r, 0.0f, iw - l);
    t = std::clamp(t, 0.0f, ih);
    b = std::clamp(b, 0.0f, ih - t);
    float k = scale > 0 ? scale : 1;
    float dl = l * k, dr = r * k, dt = t * k, db = b * k;
    if (dl + dr > w) {
        float s = w / (dl + dr);
        dl *= s;
        dr *= s;
    }
    if (dt + db > h) {
        float s = h / (dt + db);
        dt *= s;
        db *= s;
    }
    float xs[4] = {x, x + dl, x + w - dr, x + w}, ys[4] = {y, y + dt, y + h - db, y + h};
    float us[4] = {0, l / iw, 1 - r / iw, 1}, vs[4] = {0, t / ih, 1 - b / ih, 1};
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++)
            image_uv(f, slot, xs[i], ys[j], xs[i + 1] - xs[i], ys[j + 1] - ys[j], us[i], vs[j], us[i + 1], vs[j + 1],
                     tint);
}

// ----- state stacks -----------------------------------------------------------------------------

inline void push_clip(Frame &f, float x, float y, float w, float h)
{
    if (f.clips.size() >= MAX_STACK) {
        f.clip_over++;
        return;
    }
    f.clips.push_back(f.clip);
    Pt p[4];
    rect_points(f.xform, x, y, std::max(w, 0.0f), std::max(h, 0.0f), p);
    Clip c{p[0].x, p[0].y, p[0].x, p[0].y};
    for (auto &q : p) {
        c.x0 = std::min(c.x0, q.x);
        c.y0 = std::min(c.y0, q.y);
        c.x1 = std::max(c.x1, q.x);
        c.y1 = std::max(c.y1, q.y);
    }
    f.clip = {std::max(c.x0, f.clip.x0), std::max(c.y0, f.clip.y0), std::min(c.x1, f.clip.x1), std::min(c.y1, f.clip.y1)};
}

inline void pop_clip(Frame &f)
{
    if (f.clip_over) {
        f.clip_over--;
        return;
    }
    if (f.clips.empty())
        return;
    f.clip = f.clips.back();
    f.clips.pop_back();
}

inline void push_transform(Frame &f, const float *m)
{
    if (f.xforms.size() >= MAX_STACK || !m) {
        f.xform_over++;
        return;
    }
    f.xforms.push_back(f.xform);
    f.xform = f.xform.then({m[0], m[1], m[2], m[3], m[4], m[5]});
}

inline void pop_transform(Frame &f)
{
    if (f.xform_over) {
        f.xform_over--;
        return;
    }
    if (f.xforms.empty())
        return;
    f.xform = f.xforms.back();
    f.xforms.pop_back();
}

inline void push_opacity(Frame &f, float opacity)
{
    if (f.opacities.size() >= MAX_STACK) {
        f.opacity_over++;
        return;
    }
    f.opacities.push_back(f.opacity);
    f.opacity *= std::clamp(opacity, 0.0f, 1.0f);
}

inline void pop_opacity(Frame &f)
{
    if (f.opacity_over) {
        f.opacity_over--;
        return;
    }
    if (f.opacities.empty())
        return;
    f.opacity = f.opacities.back();
    f.opacities.pop_back();
}

inline void safe_area(const Frame &f, float *x, float *y, float *w, float *h)
{
    float ix = std::round(f.width * SAFE_INSET), iy = std::round(f.height * SAFE_INSET);
    if (x)
        *x = ix;
    if (y)
        *y = iy;
    if (w)
        *w = std::max(0.0f, f.width - 2 * ix);
    if (h)
        *h = std::max(0.0f, f.height - 2 * iy);
}

inline float ui_scale(const Frame &f)
{
    if (f.width <= 0 || f.height <= 0)
        return 1;
    return std::min(f.width / 1920.0f, f.height / 1080.0f);
}

// ----- registry ---------------------------------------------------------------------------------

struct Paintsub {
    uint32_t token;
    SwoftyPaintFn fn;
    void *user;
    std::string owner;
};

struct Image {
    std::string id;
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba;
    uint32_t generation = 0;
};

class Registry {
  public:
    uint32_t add(SwoftyPaintFn fn, void *user, std::string owner)
    {
        if (!fn)
            return 0;
        uint32_t token = next_++;
        subs_.push_back({token, fn, user, std::move(owner)});
        return token;
    }

    bool remove(uint32_t token)
    {
        for (size_t i = 0; i < subs_.size(); i++)
            if (subs_[i].token == token) {
                subs_.erase(subs_.begin() + i);
                return true;
            }
        return false;
    }

    size_t count() const { return subs_.size(); }

    const std::vector<Paintsub> &subs() const { return subs_; }

    int intern_image(const std::string &id)
    {
        int at = find_image(id);
        if (at >= 0)
            return at;
        Image im;
        im.id = id;
        images_.push_back(std::move(im));
        return int(images_.size() - 1);
    }

    int set_image(const std::string &id, uint32_t width, uint32_t height, std::vector<uint8_t> rgba)
    {
        int at = intern_image(id);
        Image &im = images_[size_t(at)];
        im.width = width;
        im.height = height;
        im.rgba = std::move(rgba);
        im.generation++;
        return at;
    }

    int find_image(const std::string &id) const
    {
        for (size_t i = 0; i < images_.size(); i++)
            if (images_[i].id == id)
                return int(i);
        return -1;
    }

    const Image *image(int slot) const
    {
        return slot >= 0 && size_t(slot) < images_.size() ? &images_[size_t(slot)] : nullptr;
    }

    Image *image(int slot) { return slot >= 0 && size_t(slot) < images_.size() ? &images_[size_t(slot)] : nullptr; }

    size_t image_count() const { return images_.size(); }

  private:
    std::vector<Paintsub> subs_;
    std::vector<Image> images_;
    uint32_t next_ = 1;
};

inline bool listed(const std::vector<std::string> &ids, const std::string &id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

inline void build(const std::vector<Paintsub> &subs, Frame &f, float width, float height,
                  const std::vector<std::string> &off = {})
{
    begin(f, width, height);
    for (const auto &s : subs)
        if (s.fn && !listed(off, s.owner)) {
            reset_state(f);
            f.owner = s.owner;
            s.fn(reinterpret_cast<SwoftyPaint *>(&f), s.user);
        }
}

}
