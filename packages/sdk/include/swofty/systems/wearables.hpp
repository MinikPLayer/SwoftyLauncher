// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <swofty/events.hpp>
#include <swofty/registry.hpp>
#include <swofty/systems/forward.hpp>

// Helmets, suits and gloves (one customisation item row and its archive each) and a team's kit per season
// (one team_customisation_for_year row). An item's colours fill its colour_N_default keys; empty, the core
// takes them from the `from` row. A driver wears items through a drivers::Look (swofty/systems/drivers.hpp).
// What a row holds that this header doesn't name is kept in the record's `extra`.

namespace swofty::wearables {

using Result = swofty::Result;

struct Helmet {
    static constexpr Version version{1, 0};
    Path archive;
    Ref<Helmet> from;
    Text name;
    Ref<drivers::Identity> driver;
    std::optional<std::array<Colour, 4>> colours;
    bool official_team_item = false;
    StatId stat_id = StatId::automatic;
    json::Value extra;
};
SWOFTY_RECORD(Helmet, archive, from, name, driver, colours, official_team_item, stat_id)

struct Suit {
    static constexpr Version version{1, 0};
    Path archive;
    Ref<Suit> from;
    Ref<teams::Identity> team;
    std::optional<std::array<Colour, 4>> colours;
    StatId stat_id = StatId::automatic;
    json::Value extra;
};
SWOFTY_RECORD(Suit, archive, from, team, colours, stat_id)

struct Gloves {
    static constexpr Version version{1, 0};
    Path archive;
    Ref<Gloves> from;
    Ref<teams::Identity> team;
    std::optional<std::array<Colour, 4>> colours;
    StatId stat_id = StatId::automatic;
    json::Value extra;
};
SWOFTY_RECORD(Gloves, archive, from, team, colours, stat_id)

struct SeasonSet {
    static constexpr Version version{1, 0};
    Ref<teams::Identity> team;
    int season = 0;
    Ref<Suit> suit;
    Ref<Gloves> gloves;
    json::Value extra;
};
SWOFTY_RECORD(SeasonSet, team, season, suit, gloves)
SWOFTY_RECORD_REGISTRY(SeasonSet, "wearable.season_set")

inline Registry<Helmet> helmets{"helmet", {.applies = Applies::boot}};
inline Registry<Suit> suits{"suit", {.applies = Applies::boot}};
inline Registry<Gloves> gloves{"gloves", {.applies = Applies::boot}};
inline Registry<SeasonSet> season_sets{"wearable.season_set", {.applies = Applies::boot}};

}

namespace swofty::wearables::events {

struct CatalogueBuild {
    static constexpr const char *point = "wearable.catalogue.build";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    int32_t (*add_fn)(void *user, const char *registry, const char *id);
    void *user;

    Result add(const Ref<Helmet> &item) { return add_item("helmet", item.id()); }
    Result add(const Ref<Suit> &item) { return add_item("suit", item.id()); }
    Result add(const Ref<Gloves> &item) { return add_item("gloves", item.id()); }

private:
    Result add_item(const char *registry, std::string_view id)
    {
        if (!add_fn)
            return Result::unavailable;
        std::string s(id);
        return swofty::detail::result(add_fn(user, registry, s.c_str()));
    }
};

}
