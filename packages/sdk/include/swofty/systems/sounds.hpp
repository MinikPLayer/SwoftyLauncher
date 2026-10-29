// SPDX-License-Identifier: MIT
#pragma once
#include <swofty/events.hpp>
#include <swofty/registry.hpp>
#include <swofty/systems/forward.hpp>

// Sounds: engine sounds (a bundle and where its mixing settings and tuning come from). They apply at boot;
// the bundle file is redirected when the game opens it.

namespace swofty::sounds {

using Result = swofty::Result;

struct EngineSound {
    static constexpr Version version{1, 0};
    Path bundle;
    Ref<EngineSound> settings_from;
    Ref<EngineSound> tuning_from;
    json::Value extra;
};
SWOFTY_RECORD(EngineSound, bundle, settings_from, tuning_from)

inline Registry<EngineSound> engine_sounds{"engine_sound", {.applies = Applies::boot}};

}

namespace swofty::sounds::events {

struct BundleResolve {
    static constexpr const char *point = "audio.bundle.resolve";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    int32_t car;
    const char *vehicle_id;
    const char *switch_name_str;
    const char *engine_sound_id;
    int32_t (*set_engine_sound_fn)(void *user, const char *id);
    void *user;

    Ref<cars::Vehicle> vehicle() const { return swofty::detail::payload_ref<cars::Vehicle>(vehicle_id); }
    std::string_view switch_name() const { return swofty::detail::payload_str(switch_name_str); }
    Ref<EngineSound> engine_sound() const { return swofty::detail::payload_ref<EngineSound>(engine_sound_id); }
    Result set_engine_sound(const Ref<EngineSound> &sound)
    {
        if (!set_engine_sound_fn)
            return Result::unavailable;
        std::string id(sound.id());
        return swofty::detail::result(set_engine_sound_fn(user, id.c_str()));
    }
};

}
