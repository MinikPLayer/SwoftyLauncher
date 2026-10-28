#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "../../third_party/stb/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// Image files for the overlay: PNG through stb_image and uncompressed 32-bit DDS, decoded to
// premultiplied RGBA8 rows, top row first.

namespace hud::img {

constexpr uint32_t MAX_SIDE = 4096;

struct Pixels {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba;
};

inline void premultiply(std::vector<uint8_t> &rgba)
{
    for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
        uint32_t a = rgba[i + 3];
        if (a == 255)
            continue;
        for (int c = 0; c < 3; c++)
            rgba[i + c] = uint8_t((rgba[i + c] * a + 127) / 255);
    }
}

inline uint32_t le32(const uint8_t *p)
{
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

inline int mask_shift(uint32_t mask)
{
    for (int s = 0; s < 32; s += 8)
        if (mask == 0xffu << s)
            return s;
    return -1;
}

inline bool dds(std::string_view data, Pixels &out, std::string &why)
{
    auto *p = reinterpret_cast<const uint8_t *>(data.data());
    if (data.size() < 128 || le32(p + 4) != 124)
        return why = "not a DDS file", false;
    uint32_t h = le32(p + 12), w = le32(p + 16), pf_flags = le32(p + 80), fourcc = le32(p + 84);
    size_t offset = 128;
    int rs = 0, gs = 8, bs = 16, as = 24;
    bool alpha = true;
    if ((pf_flags & 0x4) && fourcc == 0x30315844) {
        if (data.size() < 148)
            return why = "truncated DX10 header", false;
        uint32_t fmt = le32(p + 128);
        offset = 148;
        if (fmt == 28 || fmt == 29) {
        } else if (fmt == 87 || fmt == 91) {
            rs = 16, bs = 0;
        } else if (fmt == 88 || fmt == 93) {
            rs = 16, bs = 0, alpha = false;
        } else {
            return why = "DXGI format " + std::to_string(fmt) + " is not one the overlay reads (R8G8B8A8 or B8G8R8A8)", false;
        }
    } else if ((pf_flags & 0x40) && le32(p + 88) == 32) {
        rs = mask_shift(le32(p + 92)), gs = mask_shift(le32(p + 96)), bs = mask_shift(le32(p + 100));
        alpha = (pf_flags & 0x1) && le32(p + 104);
        as = alpha ? mask_shift(le32(p + 104)) : 0;
        if (rs < 0 || gs < 0 || bs < 0 || as < 0)
            return why = "the DDS channel masks are not 8-bit", false;
    } else {
        return why = "compressed or non-32-bit DDS is not one the overlay reads", false;
    }
    if (!w || !h || w > MAX_SIDE || h > MAX_SIDE)
        return why = "the image is " + std::to_string(w) + "x" + std::to_string(h) + ", outside 1..4096", false;
    if (data.size() < offset + size_t(w) * h * 4)
        return why = "truncated pixel data", false;
    out.width = w;
    out.height = h;
    out.rgba.resize(size_t(w) * h * 4);
    for (size_t i = 0; i < size_t(w) * h; i++) {
        uint32_t v = le32(p + offset + i * 4);
        out.rgba[i * 4 + 0] = uint8_t(v >> rs);
        out.rgba[i * 4 + 1] = uint8_t(v >> gs);
        out.rgba[i * 4 + 2] = uint8_t(v >> bs);
        out.rgba[i * 4 + 3] = alpha ? uint8_t(v >> as) : 255;
    }
    premultiply(out.rgba);
    return true;
}

inline bool decode(std::string_view data, Pixels &out, std::string &why)
{
    if (data.size() >= 4 && !memcmp(data.data(), "DDS ", 4))
        return dds(data, out, why);
    int w = 0, h = 0, n = 0;
    if (!stbi_info_from_memory(reinterpret_cast<const stbi_uc *>(data.data()), int(data.size()), &w, &h, &n))
        return why = "not a PNG or DDS file the overlay reads", false;
    if (w <= 0 || h <= 0 || uint32_t(w) > MAX_SIDE || uint32_t(h) > MAX_SIDE)
        return why = "the image is " + std::to_string(w) + "x" + std::to_string(h) + ", outside 1..4096", false;
    stbi_uc *px = stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(data.data()), int(data.size()), &w, &h, &n, 4);
    if (!px)
        return why = std::string("PNG decode failed: ") + stbi_failure_reason(), false;
    out.width = uint32_t(w);
    out.height = uint32_t(h);
    out.rgba.assign(px, px + size_t(w) * size_t(h) * 4);
    stbi_image_free(px);
    premultiply(out.rgba);
    return true;
}

inline bool grey(const uint8_t *png, size_t size, Pixels &out)
{
    int w = 0, h = 0, n = 0;
    stbi_uc *px = stbi_load_from_memory(png, int(size), &w, &h, &n, 1);
    if (!px)
        return false;
    out.width = uint32_t(w);
    out.height = uint32_t(h);
    out.rgba.assign(px, px + size_t(w) * size_t(h));
    stbi_image_free(px);
    return true;
}

}
