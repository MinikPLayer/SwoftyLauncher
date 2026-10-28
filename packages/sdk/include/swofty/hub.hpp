// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <string>
#include <string_view>
#include <windows.h>
#include <swofty/mixin.hpp>

// Team HQ hub tabs. An addon's manifest `hub_tab` gives careers of one data group a Team HQ laptop tab
// with some of the addon's options (the core builds it; see swofty_api.h SwoftyHub). From the plugin:
// hide_rnd() hides the R&D tab of Team HQ while it is on (from the build at Team HQ entry, and in an
// open Team HQ from the next frame), set_option() moves one of the addon's options to a value, as if the
// player picked it.
//
// Feature detection: the table is read only when the running swofty_core.dll exports SwoftyCoreHub.

namespace swofty::hub {

enum class Result { ok, late, unavailable, invalid };

namespace detail {

inline const SwoftyHub *table()
{
    static std::atomic<const SwoftyHub *> found{nullptr};
    if (auto t = found.load())
        return t;
    HMODULE core = GetModuleHandleW(L"swofty_core.dll");
    auto get = core ? reinterpret_cast<const SwoftyHub *(*)()>(GetProcAddress(core, "SwoftyCoreHub")) : nullptr;
    const SwoftyHub *t = get ? get() : nullptr;
    if (t && t->version < 1)
        t = nullptr;
    found = t;
    return t;
}

inline Result result(int r)
{
    return r == SWOFTY_ASSET_OK ? Result::ok
           : r == SWOFTY_ASSET_LATE ? Result::late
           : r == SWOFTY_ASSET_UNAVAILABLE ? Result::unavailable
                                           : Result::invalid;
}

}

inline bool available()
{
    auto t = detail::table();
    return t && t->available();
}

inline Result hide_rnd(bool on)
{
    auto t = detail::table();
    return t ? detail::result(t->hide_rnd(on ? 1 : 0)) : Result::unavailable;
}

inline Result set_option(std::string_view addon, std::string_view option, std::string_view value)
{
    auto t = detail::table();
    if (!t)
        return Result::unavailable;
    return detail::result(t->set_option(std::string(addon).c_str(), std::string(option).c_str(), std::string(value).c_str()));
}

inline const char *name(Result r)
{
    return r == Result::ok ? "ok" : r == Result::late ? "late" : r == Result::unavailable ? "unavailable" : "invalid";
}

}
