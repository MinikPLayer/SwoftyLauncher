// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <swofty/events.hpp>
#include <swofty/systems/cars.hpp>
#include <swofty/systems/forward.hpp>

// AI: in 1.6.0 the hook point around the AI controller's per-frame decision (the AI registries ship with
// the AI system).

namespace swofty::ai::events {

struct ControllerUpdate {
    static constexpr const char *point = "ai.controller.update";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    int32_t vehicle;
    float throttle;
    float brake;
    float steering;

    cars::live::Car car() const { return cars::live::Car(static_cast<uint32_t>(vehicle)); }
};

}
