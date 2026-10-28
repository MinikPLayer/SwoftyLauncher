#pragma once
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace hud::guard {

constexpr uint64_t REMOVED_AFTER_DRAW_MS = 10000;

inline constexpr std::string_view FG_MODULES[] = {
    "sl.dlss_g.dll",
    "nvngx_dlssg.dll",
    "amd_fidelityfx_dx12.dll",
    "amd_fidelityfx_framegeneration_dx12.dll",
    "ffx_frameinterpolation_x64.dll",
    "ffx_fsr3_x64.dll",
    "libxess_fg.dll",
    "dlssg_to_fsr3_amd_is_better.dll",
};

inline bool same_name(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z')
            x = char(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z')
            y = char(y - 'A' + 'a');
        if (x != y)
            return false;
    }
    return true;
}

inline std::string_view base_name(std::string_view path)
{
    size_t at = path.find_last_of("\\/");
    return at == std::string_view::npos ? path : path.substr(at + 1);
}

inline bool fg_module(std::string_view path)
{
    std::string_view name = base_name(path);
    for (auto m : FG_MODULES)
        if (same_name(name, m))
            return true;
    return false;
}

inline bool streamline_hook(std::string_view path)
{
    return same_name(base_name(path), "sl.dlss_g.dll");
}

inline std::string fg_in(const std::vector<std::string> &modules)
{
    for (auto &m : modules)
        if (fg_module(m) && !streamline_hook(m))
            return std::string(base_name(m));
    return {};
}

inline bool streamline_in(const std::vector<std::string> &modules)
{
    for (auto &m : modules)
        if (streamline_hook(m))
            return true;
    return false;
}

inline bool xml_int(const std::string &text, const char *element, const char *attr, int &out)
{
    size_t at = text.find(std::string("<") + element + " ");
    if (at == std::string::npos)
        return false;
    size_t end = text.find('>', at);
    std::string key = std::string(" ") + attr + "=\"";
    size_t k = text.find(key, at);
    if (k == std::string::npos || k > end)
        return false;
    out = atoi(text.c_str() + k + key.size());
    return true;
}

struct FgSetting {
    bool known = false;
    int mode = -1, multi = -1;
    bool on() const { return !known || mode != 0; }
};

inline int hdr_mode(const std::string &hardware_settings)
{
    int mode = -1;
    xml_int(hardware_settings, "hdr", "mode", mode);
    return mode;
}

inline constexpr unsigned COLOR_SPACE_UNSET = ~0u;

enum Output : unsigned { SDR = 0, HDR10_PQ = 1, SCRGB = 2 };

inline Output output_for(int format, unsigned color_space, int game_hdr_mode)
{
    constexpr unsigned G2084_P2020 = 12;
    if (format == 24)
        return color_space == G2084_P2020 || (color_space == COLOR_SPACE_UNSET && game_hdr_mode > 0) ? HDR10_PQ : SDR;
    if (format == 10)
        return color_space == G2084_P2020 ? HDR10_PQ : SCRGB;
    return SDR;
}

inline FgSetting fg_setting(const std::string &hardware_settings)
{
    FgSetting f;
    f.known = xml_int(hardware_settings, "frame_gen", "mode", f.mode);
    xml_int(hardware_settings, "multi_frame_gen", "value", f.multi);
    return f;
}

inline std::string chain(const std::vector<std::string> &modules, std::string_view skip_a, std::string_view skip_b)
{
    std::string out;
    std::string_view last;
    for (auto &m : modules) {
        std::string_view name = base_name(m);
        if (name.empty() || same_name(name, skip_a) || same_name(name, skip_b) || same_name(name, last))
            continue;
        if (!out.empty())
            out += " <- ";
        out += name;
        last = name;
    }
    return out.empty() ? "unknown" : out;
}

inline bool removal_after_draw(uint64_t now_ms, uint64_t last_draw_ms)
{
    return last_draw_ms && now_ms >= last_draw_ms && now_ms - last_draw_ms <= REMOVED_AFTER_DRAW_MS;
}

// Whether a COM object's own vtable sends this slot to fn. A hook's trampoline runs the hooked
// implementation's body, which reads its this pointer as its own object type, so the core calls it only
// for an object of that type; a wrapper such as Streamline's queue proxy goes through its own vtable.
inline bool vtable_slot_is(const void *object, size_t slot, const void *fn)
{
    return object && fn && (*static_cast<const void *const *const *>(object))[slot] == fn;
}

}
