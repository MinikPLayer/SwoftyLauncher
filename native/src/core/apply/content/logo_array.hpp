#pragma once

#include "bc7.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <vector>

// Growing one stock logo texture array by slices: the three fragments of an array (the image header, the BC7 pixel
// data of every slice, and the view with its slice count) and the key list in overrides.uidb, as
// classic-seasons/tools/logos.py does it. The array is checked the same way (BC7, one mip, one slice of w*h bytes
// per key, the view's count equal to the slice count) and a key that is listed already is refused.

namespace swofty_core::apply::content::logo_array {

inline constexpr uint32_t FORMAT_BC7 = 70;
inline constexpr size_t HEAD_FORMAT = 8, HEAD_MIPS = 24, HEAD_COUNT = 28, HEAD_SIZE = 32, VIEW_COUNT = 0x14;

struct Stock {
    std::string head, data, view;
    std::vector<std::string> names;
};

struct Slice {
    std::string key, bc7;
};

struct Shape {
    uint32_t width = 0, height = 0, count = 0;
    size_t slice_bytes() const { return size_t(width) * height; }
};

inline uint32_t le32(const std::string &s, size_t at)
{
    uint32_t v = 0;
    std::memcpy(&v, s.data() + at, 4);
    return v;
}

inline void put32(std::string &s, size_t at, uint32_t v)
{
    std::memcpy(&s[at], &v, 4);
}

inline bool shape_of(const Stock &stock, Shape &out, std::string &why)
{
    if (stock.head.size() < HEAD_SIZE || stock.view.size() < VIEW_COUNT + 4)
        return why = "the stock fragments are shorter than an array header", false;
    uint32_t fmt = le32(stock.head, HEAD_FORMAT), w = le32(stock.head, HEAD_FORMAT + 4), h = le32(stock.head, HEAD_FORMAT + 8);
    uint32_t mips = le32(stock.head, HEAD_MIPS), count = le32(stock.head, HEAD_COUNT);
    if (fmt != FORMAT_BC7 || mips != 1)
        return why = "unexpected stock array (format " + std::to_string(fmt) + ", " + std::to_string(mips) + " mips)", false;
    if (!w || !h || w % 4 || h % 4)
        return why = "the stock array is " + std::to_string(w) + "x" + std::to_string(h) + ", not a multiple of 4", false;
    if (stock.data.size() != size_t(count) * w * h || stock.names.size() != count || le32(stock.view, VIEW_COUNT) != count)
        return why = "the stock array holds " + std::to_string(stock.data.size()) + " byte(s), " +
                     std::to_string(stock.names.size()) + " key(s) and a view count of " +
                     std::to_string(le32(stock.view, VIEW_COUNT)) + " for " + std::to_string(count) + " slice(s)",
               false;
    out = {w, h, count};
    return true;
}

inline bool grow(const Stock &stock, const std::vector<Slice> &add, Stock &out, std::string &why)
{
    Shape shape;
    if (!shape_of(stock, shape, why))
        return false;
    std::set<std::string> taken(stock.names.begin(), stock.names.end());
    for (auto &s : add) {
        if (!taken.insert(s.key).second)
            return why = "the array already lists " + s.key, false;
        if (s.bc7.size() != shape.slice_bytes())
            return why = s.key + " slice is " + std::to_string(s.bc7.size()) + " bytes, not " + std::to_string(shape.slice_bytes()),
                   false;
    }
    out = stock;
    uint32_t total = shape.count + uint32_t(add.size());
    put32(out.head, HEAD_COUNT, total);
    put32(out.view, VIEW_COUNT, total);
    for (auto &s : add) {
        out.data += s.bc7;
        out.names.push_back(s.key);
    }
    return true;
}

inline std::string fit_rgba(const uint8_t *rgba, uint32_t w, uint32_t h, uint32_t tw, uint32_t th)
{
    std::string out(size_t(tw) * th * 4, '\0');
    double scale = std::min(double(tw) / w, double(th) / h);
    uint32_t nw = std::clamp<uint32_t>(uint32_t(std::lround(w * scale)), 1, tw);
    uint32_t nh = std::clamp<uint32_t>(uint32_t(std::lround(h * scale)), 1, th);
    uint32_t ox = (tw - nw) / 2, oy = (th - nh) / 2;
    auto pre = [&](uint32_t x, uint32_t y, double *c) {
        const uint8_t *p = rgba + (size_t(y) * w + x) * 4;
        double a = p[3] / 255.0;
        c[0] = p[0] * a;
        c[1] = p[1] * a;
        c[2] = p[2] * a;
        c[3] = p[3];
    };
    for (uint32_t y = 0; y < nh; y++)
        for (uint32_t x = 0; x < nw; x++) {
            double acc[4] = {0, 0, 0, 0}, c[4];
            if (nw < w || nh < h) {
                double x0 = double(x) * w / nw, x1 = double(x + 1) * w / nw, y0 = double(y) * h / nh, y1 = double(y + 1) * h / nh, total = 0;
                for (uint32_t sy = uint32_t(y0); sy < h && sy < y1; sy++)
                    for (uint32_t sx = uint32_t(x0); sx < w && sx < x1; sx++) {
                        double cover = (std::min(x1, sx + 1.0) - std::max(x0, double(sx))) * (std::min(y1, sy + 1.0) - std::max(y0, double(sy)));
                        pre(sx, sy, c);
                        for (int k = 0; k < 4; k++)
                            acc[k] += c[k] * cover;
                        total += cover;
                    }
                for (int k = 0; k < 4 && total > 0; k++)
                    acc[k] /= total;
            } else {
                double fx = std::clamp((x + 0.5) * w / nw - 0.5, 0.0, w - 1.0), fy = std::clamp((y + 0.5) * h / nh - 0.5, 0.0, h - 1.0);
                uint32_t ax = uint32_t(fx), ay = uint32_t(fy), bx = std::min(ax + 1, w - 1), by = std::min(ay + 1, h - 1);
                double tx = fx - ax, ty = fy - ay;
                const uint32_t px[4][2] = {{ax, ay}, {bx, ay}, {ax, by}, {bx, by}};
                const double wt[4] = {(1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty};
                for (int i = 0; i < 4; i++) {
                    pre(px[i][0], px[i][1], c);
                    for (int k = 0; k < 4; k++)
                        acc[k] += c[k] * wt[i];
                }
            }
            uint8_t *d = reinterpret_cast<uint8_t *>(&out[(size_t(oy + y) * tw + ox + x) * 4]);
            double a = acc[3];
            uint8_t alpha = uint8_t(std::clamp(std::lround(a), 0L, 255L));
            if (alpha) {
                double un = 255.0 / a;
                for (int k = 0; k < 3; k++)
                    d[k] = uint8_t(std::clamp(std::lround(acc[k] * un), 0L, 255L));
            }
            d[3] = alpha;
        }
    return out;
}

inline bool slice_from_rgba(const Shape &shape, const uint8_t *rgba, uint32_t w, uint32_t h, std::string &out, std::string &why,
                            bool *scaled = nullptr)
{
    if (!w || !h)
        return why = "the image is empty", false;
    if (scaled)
        *scaled = false;
    if (w != shape.width || h != shape.height) {
        std::string fit = fit_rgba(rgba, w, h, shape.width, shape.height);
        out = bc7::encode_logo(reinterpret_cast<const uint8_t *>(fit.data()), int(shape.width), int(shape.height));
        if (scaled)
            *scaled = true;
        return true;
    }
    out = bc7::encode_logo(rgba, int(w), int(h));
    return true;
}

}
