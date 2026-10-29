// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <swofty/events.hpp>
#include <swofty/registry.hpp>
#include <swofty/systems/ops.hpp>
#include <swofty/systems/forward.hpp>

// Teams: real constructors (core-owned constructor/ ids), team identities (one entrants.teams row each),
// logos per context and pit times. A new team names a donor, the stock row whose every key it copies
// (story_info included); a variant of a stock team copies its base's row the same way. A constructor/ id
// works as a team reference and resolves to the last enabled team in load order that is that constructor.
// What a row holds that this header doesn't name is kept in the record's `extra`. The operations return
// unavailable until the core implements them. set_logo calls the core's career operations table (version 5).

namespace swofty::teams {

using Result = swofty::Result;

enum class LogoContext : int32_t { driver_select, team_select, hud, broadcast, hub };
SWOFTY_ENUM(LogoContext, driver_select, team_select, hud, broadcast, hub)

struct Constructor {
    static constexpr Version version{1, 0};
    std::string name;
    std::string country;
    Ref<Identity> team;
    json::Value extra;
};
SWOFTY_RECORD(Constructor, name, country, team)
SWOFTY_RECORD_REGISTRY(Constructor, "team.constructor")

struct Colours {
    Colour menu;
    Colour primary;
    Colour secondary;
    Colour tertiary;
};
SWOFTY_RECORD(Colours, menu, primary, secondary, tertiary)

struct Names {
    Text title;
    Text short_name;
    Text super_short;
    std::string code;
};
SWOFTY_RECORD(Names, title, short_name, super_short, code)

struct Logo {
    static constexpr Version version{1, 0};
    std::map<LogoContext, Path> images;
    json::Value extra;
};
SWOFTY_RECORD(Logo, images)
SWOFTY_RECORD_REGISTRY(Logo, "team.logo")

struct Identity {
    static constexpr Version version{1, 0};
    Ref<Identity> donor;
    Ref<Constructor> constructor;
    std::vector<Ref<Constructor>> provides;
    Names names;
    Colours colours;
    std::optional<Colours> wear;
    std::string livery_name;
    Ref<Logo> logo;
    std::string audio_id;
    int prestige = 0;
    std::string nationality;
    StatId stat_id = StatId::automatic;
    json::Value extra;
};
SWOFTY_RECORD(Identity, donor, constructor, provides, names, colours, wear, livery_name, logo, audio_id, prestige,
              nationality, stat_id)

struct PitTimes {
    static constexpr Version version{1, 0};
    Ref<Identity> team;
    Ref<PitTimes> from;
    json::Value extra;
};
SWOFTY_RECORD(PitTimes, team, from)
SWOFTY_RECORD_REGISTRY(PitTimes, "team.pit_times")

inline Registry<Constructor> constructors{"team.constructor",
                                          {.applies = Applies::boot, .shared = {.provider_registry = "team.identity"}}};
inline Registry<Identity> identities{"team.identity", {.applies = Applies::boot}};
inline Registry<Logo> logos{"team.logo", {.applies = Applies::read}};
inline Registry<PitTimes> pit_times{"team.pit_times", {.applies = Applies::boot}};

inline Result set_logo(Ref<Identity> team, Ref<Logo> logo)
{
    auto t = career::detail::live_ops();
    if (!t || t->version < 5 || !t->set_logo)
        return Result::unavailable;
    std::string team_id(team.id()), logo_id(logo.id());
    if (team_id.empty() || logo_id.empty())
        return Result::invalid;
    int r = t->set_logo(team_id.c_str(), logo_id.c_str());
    return r == SWOFTY_ASSET_OK ? Result::ok
           : r == SWOFTY_ASSET_LATE ? Result::late
           : r == SWOFTY_ASSET_UNAVAILABLE ? Result::unavailable
                                           : Result::invalid;
}
inline Result set_name(Ref<Identity>, Names) { return Result::unavailable; }
inline Result set_colours(Ref<Identity>, Colours) { return Result::unavailable; }

}

namespace swofty::teams::events {

struct LogoResolve {
    static constexpr const char *point = "team.logo.resolve";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    LogoContext context;
    const char *team_id;
    const char *livery_name_str;
    const char *key_str;
    int32_t (*set_key_fn)(void *user, const char *key);
    void *user;

    Ref<Identity> team() const { return swofty::detail::payload_ref<Identity>(team_id); }
    std::string_view livery_name() const { return swofty::detail::payload_str(livery_name_str); }
    std::string_view key() const { return swofty::detail::payload_str(key_str); }
    Result set_key(std::string_view key)
    {
        if (!set_key_fn)
            return Result::unavailable;
        std::string k(key);
        return swofty::detail::result(set_key_fn(user, k.c_str()));
    }
};

struct NameResolve {
    static constexpr const char *point = "team.name.resolve";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    const char *team_id;
    const char *name_str;
    bool name_literal;
    int32_t (*set_name_fn)(void *user, const char *name, bool literal);
    void *user;

    Ref<Identity> team() const { return swofty::detail::payload_ref<Identity>(team_id); }
    Text name() const
    {
        std::string_view s = swofty::detail::payload_str(name_str);
        return name_literal ? Text::literal(s) : Text::key(s);
    }
    Result set_name(const Text &name)
    {
        if (!set_name_fn)
            return Result::unavailable;
        return swofty::detail::result(set_name_fn(user, name.str().c_str(), name.is_literal()));
    }
};

}
