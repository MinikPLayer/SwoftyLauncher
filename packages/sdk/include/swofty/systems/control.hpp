// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <cstdint>
#include <optional>

#include <swofty/record.hpp>
#include <swofty_api.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Who drives a car, and the core's session operations (SwoftyCoreSessionOps) that read and change it.
// The types are shared by sessions.hpp (set_control, session.player.control) and cars.hpp (live::Car::control);
// the spectator calls of spectator.hpp use the same table. A core without the table answers unavailable.

namespace swofty::sessions {

enum class Control : int32_t { human, ai };

struct ControlState {
    Control mode;
    uint32_t flags;
    static constexpr uint32_t pit_lane_flag = 1u;
    bool pit_lane() const { return (flags & pit_lane_flag) != 0; }
};

enum class Result { ok, late, unavailable, invalid, blocked };

}

namespace swofty::detail {

inline std::atomic<const SwoftySessionOps *> &session_ops_override()
{
    static std::atomic<const SwoftySessionOps *> t{nullptr};
    return t;
}

inline void set_session_ops_table(const SwoftySessionOps *t) { session_ops_override() = t; }

inline const SwoftySessionOps *session_ops_table()
{
    if (auto t = session_ops_override().load())
        return t->version >= 1 ? t : nullptr;
#if defined(_WIN32)
    static std::atomic<const SwoftySessionOps *> found{nullptr};
    if (auto t = found.load())
        return t;
    HMODULE core = GetModuleHandleW(L"swofty_core.dll");
    auto get = core ? reinterpret_cast<SwoftyCoreSessionOpsFn>(GetProcAddress(core, "SwoftyCoreSessionOps")) : nullptr;
    const SwoftySessionOps *t = get ? get() : nullptr;
    if (t && t->version < 1)
        t = nullptr;
    found = t;
    return t;
#else
    return nullptr;
#endif
}

inline const SwoftySessionOps *live_session_ops()
{
    auto t = session_ops_table();
    return t && t->available && t->available() ? t : nullptr;
}

inline sessions::Result session_result(int r)
{
    return r == SWOFTY_ASSET_OK ? sessions::Result::ok
           : r == SWOFTY_ASSET_LATE ? sessions::Result::late
           : r == SWOFTY_ASSET_UNAVAILABLE ? sessions::Result::unavailable
           : r == SWOFTY_REFUSED ? sessions::Result::blocked
                                 : sessions::Result::invalid;
}

inline std::optional<sessions::ControlState> read_control(uint32_t car)
{
    auto t = live_session_ops();
    uint32_t mode = 0, flags = 0;
    if (!t || !t->control || t->control(car, &mode, &flags) != SWOFTY_ASSET_OK)
        return std::nullopt;
    return sessions::ControlState{mode == 1 ? sessions::Control::ai : sessions::Control::human, flags};
}

}
