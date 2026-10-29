// SPDX-License-Identifier: MIT
#pragma once
#include <swofty/registry.hpp>
#include <swofty/systems/forward.hpp>

// Liveries: the textures or wep archive a car wears. A car wears a livery when its vehicle record's `livery`
// names it; the livery fits every car with the model of the car it was made for. Archives are added to the game's
// asset manifest at boot and loose textures are redirected each time the game opens them, both to the addon's own
// files. A special livery is also a livery picker entry: the core writes its se_livery row and its unlock row.

namespace swofty::liveries {

using Result = swofty::Result;

enum class Unlock { owned, game };
SWOFTY_ENUM(Unlock, owned, game)

struct Livery {
    static constexpr Version version{1, 0};
    Ref<cars::Vehicle> vehicle;
    Path textures;
    Path archive;
    bool partial = false;
    bool special = false;
    std::string suffix = "auto";
    std::string picker_name;
    Ref<tracks::Venue> event;
    Unlock unlock = Unlock::owned;
    StatId stat_id = StatId::automatic;
    json::Value extra;
};
SWOFTY_RECORD(Livery, vehicle, textures, archive, partial, special, suffix, picker_name, event, unlock, stat_id)

inline Registry<Livery> liveries{"livery", {.applies = Applies::boot}};

}
