// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <optional>
#include <swofty/events.hpp>
#include <swofty/systems/cars.hpp>
#include <swofty/systems/control.hpp>

// The spectator camera: the game's spectator director follows any driver with a TV pod or broadcast
// camera and has its own HUD. The calls go through the core's session operations (SwoftyCoreSessionOps),
// run on the game's flow thread and answer unavailable on a core without them. current() is empty while
// the director is off.

namespace swofty::spectator {

using Result = swofty::Result;

enum class Camera : int32_t { tv_pod = 1, broadcast = 2 };

struct View {
    cars::live::Car following;
    Camera camera;
};

inline Result set_active(bool on)
{
    auto t = swofty::detail::live_session_ops();
    return t && t->spectator_set_active ? swofty::detail::result(t->spectator_set_active(on ? 1 : 0)) : Result::unavailable;
}

inline Result follow(cars::live::Car car)
{
    auto t = swofty::detail::live_session_ops();
    return t && t->spectator_follow ? swofty::detail::result(t->spectator_follow(car.index())) : Result::unavailable;
}

inline Result set_camera(Camera camera)
{
    auto t = swofty::detail::live_session_ops();
    return t && t->spectator_set_camera ? swofty::detail::result(t->spectator_set_camera(static_cast<int32_t>(camera)))
                                        : Result::unavailable;
}

inline std::optional<View> current()
{
    auto t = swofty::detail::live_session_ops();
    int32_t car = -1, camera = -1;
    if (!t || !t->spectator_current || t->spectator_current(&car, &camera) != SWOFTY_ASSET_OK || car < 0)
        return std::nullopt;
    return View{cars::live::Car(static_cast<uint32_t>(car)), static_cast<Camera>(camera)};
}

namespace events {

struct HudVisible {
    static constexpr const char *point = "hud.spectator.visible";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    cars::live::Car following;
    bool visible;
};

}

}
