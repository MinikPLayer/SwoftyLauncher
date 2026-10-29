// SPDX-License-Identifier: MIT
#pragma once
#include <optional>
#include <swofty/events.hpp>
#include <swofty/registry.hpp>
#include <swofty/systems/cars.hpp>
#include <swofty/systems/control.hpp>
#include <swofty/systems/forward.hpp>

// Sessions: in 1.6.0 the rules a Grand Prix or Time Trial box carries (content::Package::rules), applied
// when a session loads in that box, and who drives a car: set_control hands a car to the AI or back to its
// human, and session.player.control runs around the game's own switches. The rest of the sessions system
// ships later.

namespace swofty::sessions {

struct Rules {
    Ref<Mode> mode;
    Ref<Points> points;
    Ref<power::DrsRule> drs;
    Ref<tyres::Class> tyre_class;
    Ref<RulesProfile> rules_profile;
    bool refuelling = false;
    std::optional<float> race_fuel_kg;
};
SWOFTY_RECORD(Rules, mode, points, drs, tyre_class, rules_profile, refuelling, race_fuel_kg)

inline Result set_control(cars::live::Car car, Control mode)
{
    auto t = swofty::detail::live_session_ops();
    if (!t || !t->set_control)
        return Result::unavailable;
    return swofty::detail::session_result(t->set_control(car.index(), mode == Control::ai ? 1u : 0u));
}

namespace events {

struct PlayerControl {
    static constexpr const char *point = "session.player.control";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    cars::live::Car car;
    Control from, to;
    int32_t reason;
};

}

}
