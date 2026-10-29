#pragma once

#include <cstdint>
#include <string>
#include <string_view>

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
#include "../../../../third_party/stb/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace swofty_core::apply::content::png {

inline bool decode(std::string_view data, std::string &rgba, uint32_t &w, uint32_t &h, std::string &why)
{
    int iw = 0, ih = 0, n = 0;
    stbi_uc *px = stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(data.data()), int(data.size()), &iw, &ih, &n, 4);
    if (!px)
        return why = std::string("not a PNG: ") + stbi_failure_reason(), false;
    w = uint32_t(iw);
    h = uint32_t(ih);
    rgba.assign(reinterpret_cast<const char *>(px), size_t(iw) * size_t(ih) * 4);
    stbi_image_free(px);
    for (size_t i = 0; i < rgba.size(); i += 4)
        if (!rgba[i + 3])
            rgba[i] = rgba[i + 1] = rgba[i + 2] = 0;
    return true;
}

}
