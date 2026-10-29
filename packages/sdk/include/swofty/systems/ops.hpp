// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <swofty/events.hpp>
#include <swofty/systems/forward.hpp>
#include <swofty_api.h>

// The core's career operations table (SwoftyCareerOps, the core export SwoftyCoreCareerOps) as the system
// headers reach it: career.hpp and teams.hpp share it. On a core without the table every call returns unavailable.

namespace swofty::career::detail {

inline std::atomic<const SwoftyCareerOps *> &ops_override()
{
    static std::atomic<const SwoftyCareerOps *> t{nullptr};
    return t;
}

inline void set_ops_table(const SwoftyCareerOps *t) { ops_override() = t; }

inline const SwoftyCareerOps *ops_table()
{
    if (auto t = ops_override().load())
        return t->version >= 1 ? t : nullptr;
#if defined(_WIN32)
    static std::atomic<const SwoftyCareerOps *> found{nullptr};
    if (auto t = found.load())
        return t;
    HMODULE core = GetModuleHandleW(L"swofty_core.dll");
    auto get = core ? reinterpret_cast<SwoftyCoreCareerOpsFn>(GetProcAddress(core, "SwoftyCoreCareerOps")) : nullptr;
    const SwoftyCareerOps *t = get ? get() : nullptr;
    if (t && t->version < 1)
        t = nullptr;
    found = t;
    return t;
#else
    return nullptr;
#endif
}

inline const SwoftyCareerOps *live_ops()
{
    auto t = ops_table();
    return t && t->available && t->available() ? t : nullptr;
}

}
