---
title: Teams
description: Teams in code - team identity, shared constructor ids, logos per context and pit times as typed registries, with the logo and name events and the team operations.
---

# Teams

Rename teams, change their colours and logos, or add a team the game doesn't have. Team Names, for example, lets the player rename Mercedes and pick its colours on a launcher page, and its code applies those choices as edits on the stock `teams::Identity` record.

A few terms used on this page:

- A **team** is a `teams::Identity` record: one row in the game's `entrants.teams` table, with its names, colours, logo and the side data the game finds by its label.
- A **constructor** is a real Formula 1 team, with an id under `constructor/` that the core owns. Every pack that adds the 2017 Toro Rosso says its team is `constructor/toro_rosso`.
- A **logo** is a `teams::Logo` record: one image for each place a logo shows.
- A **layer** changes values on a record that already exists. A layer can be **profile-scope**: it applies only in the launcher profile that set it.

## The header

<Example root="swofty-sdk" code-file="include/swofty/systems/teams.hpp">
<template #code>

```cpp
#include <swofty/registry.hpp>

namespace swofty::teams {

using Result = swofty::Result;

enum class LogoContext : int32_t { driver_select, team_select, hud, broadcast, hub };

struct Constructor {                               // a real team; the core owns the constructor/ ids
    static constexpr Version version{1, 0};
    std::string name;                              // "Scuderia Toro Rosso"
    std::string country;                           // "italy"
    Ref<Identity> team;                            // resolved: the last enabled team in load order that is this constructor
    json::Value extra;
};
SWOFTY_RECORD(Constructor, name, country, team)

struct Colours {
    Colour menu;                                   // ui_team_colour: menus and the timing tower
    Colour primary;                                // primary_colour
    Colour secondary;                              // secondary_colour
    Colour tertiary;                               // tertiary_colour
};
SWOFTY_RECORD(Colours, menu, primary, secondary, tertiary)

struct Names {
    Text title;                                    // display_name
    Text short_name;                               // display_name_short
    Text super_short;                              // display_name_super_short
    std::string code;                              // display_name_code: "MER"
};
SWOFTY_RECORD(Names, title, short_name, super_short, code)

struct Logo {
    static constexpr Version version{1, 0};
    std::map<LogoContext, Path> images;            // one image per context; a slice each, not a whole array
    json::Value extra;
};
SWOFTY_RECORD(Logo, images)

struct Identity {                                  // one entrants.teams row
    static constexpr Version version{1, 0};
    Ref<Identity> donor;                           // the row it copies, all 30 keys with story_info (new teams)
    Ref<Constructor> constructor;                  // the real team it is; empty: a fictional team
    std::vector<Ref<Constructor>> provides;        // the constructor/ ids this team provides (shared ids)
    Names names;                                   // the team's names
    Colours colours;                               // the team's colours
    std::optional<Colours> wear;                   // team_wear_*: empty follows colours (the kit follows primary)
    std::string livery_name;                       // the label HUD badges are found by
    Ref<Logo> logo;                                // its logos
    std::string audio_id;                          // audio_id
    int prestige = 0;                              // prestige
    std::string nationality;                       // nationality
    StatId stat_id = StatId::automatic;            // team ids run to 514
    json::Value extra;
};
SWOFTY_RECORD(Identity, donor, constructor, provides, names, colours, wear, livery_name, logo, audio_id, prestige,
              nationality, stat_id)

struct PitTimes {                                  // one pit_lane.pit_times row, found by team label
    static constexpr Version version{1, 0};
    Ref<Identity> team;
    Ref<PitTimes> from;                            // copy another team's row
    json::Value extra;
};
SWOFTY_RECORD(PitTimes, team, from)

inline Registry<Constructor> constructors{"team.constructor", { .applies = Applies::boot }};
inline Registry<Identity> identities{"team.identity", { .applies = Applies::boot }};   // names and colours also on read
inline Registry<Logo> logos{"team.logo", { .applies = Applies::read }};
inline Registry<PitTimes> pit_times{"team.pit_times", { .applies = Applies::boot }};

Result set_logo(Ref<Identity> team, Ref<Logo> logo);     // from now on, every context
Result set_name(Ref<Identity>, Names);                   // the loaded team's names; unavailable
Result set_colours(Ref<Identity>, Colours);              // needs the Sess_Load_Teams record layout; unavailable

}

namespace swofty::teams::events {

struct LogoResolve {                               // the two logo format sites and the HUD badge: before
    static constexpr const char *point = "team.logo.resolve";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    LogoContext context;                           // where the logo shows
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

struct NameResolve {                               // display-name lookups: before (needs RE)
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
```

</template>
</Example>

## Team Names, in code

<Split>

Team Names lets the player rename teams and change their colours from a launcher page. The page writes the player's choices to the addon's own storage. The addon's code reads them in the game and applies them as profile-scope edits on the stock team records.

- **A name is written as text** in every language (`Text::literal`), and the core makes the lang key.
- **The kit follows the primary colour** unless `wear` is set.
- **Classic Career and Progressive change the same 2026 teams.** Load order decides each field, and the conflicts report names both addons.

<template #example>
<Example root="custom.team-names" code-file="native/src/names.cpp" data-file="registry/layers.json" files="swofty.json, bin/team-names-helper.exe, launcher/main.go">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/storage.hpp>
#include <swofty/systems/teams.hpp>

#include "choices.hpp"   // parses the page's choices.json

using namespace swofty;

SWOFTY_REGISTER
{
    std::string json;
    if (storage::read("choices.json", json) != storage::Result::ok)
        return;

    for (const team_names::Choice &c : team_names::parse(json)) {      // { team, title, short, tag, code, colours }
        teams::identities.layer(teams::identities["game/" + c.team], [c](teams::Identity &t) {
            if (c.title)  t.names.title = Text::literal(*c.title);
            if (c.short_) t.names.short_name = Text::literal(*c.short_);
            if (c.tag)    t.names.super_short = Text::literal(*c.tag);
            if (c.code)   t.names.code = *c.code;
            if (c.colours) t.colours = *c.colours;
        }, Scope::profile);
    }
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
// registry/layers.json: the same choices, written by hand
{
  "team.identity": {
    "game/mercedes_2026": {
      "names": { "title": { "text": "Mercedes-AMG Petronas" }, "super_short": { "text": "Mercedes" }, "code": "MER" },
      "colours": { "menu": "#00D2BE", "primary": "#00D2BE", "secondary": "#0C0C0C", "tertiary": "#C8CCCE" }
    }
  }
}
```

</template>
</Example>
</template>
</Split>

```text
[registry] register: custom.team-names (team_names.dll) 3 layers on team.identity (early stage, 0.4 ms)
[registry] team.identity game/mercedes_2026: edit by custom.team-names (profile) changed /names/title, /names/super_short,
           /colours/primary -> #00D2BE
[registry] team.identity game/racing_bulls_2026: custom.team-names and career.classic-career both set /names/title;
           career.classic-career wins (later in load order)
[registry] applied at boot: team.identity 3 (3 rows, 33 records; 6 strings in 11 languages)
```

## A new team

A new team copies a stock row. The core copies every key, `story_info` included, writes its pit times and allocates the stat id from the install's store:

```cpp
auto toro_rosso = teams::identities.add("toro_rosso", {
    .donor = teams::identities["game/tororosso"],
    .constructor = teams::constructors["constructor/toro_rosso"],
    .provides = { teams::constructors["constructor/toro_rosso"] },
    .names = { .title = Text::key("lng_cs_team_17_toro_rosso"), .code = "STR" },
    .colours = { .menu = 0xFF0A1E3C, .primary = 0xFF0A1E3C, .secondary = 0xFFC8102E },
    .livery_name = "cs17_toro_rosso",
    .logo = teams::logos["swofty.classic-seasons-2017/toro_rosso_2017"],
});
teams::pit_times.add("toro_rosso", { .team = toro_rosso, .from = teams::pit_times["game/tororosso"] });
```

## Real teams

A `constructor/` id works as a team reference: `teams::identities["constructor/toro_rosso"]` resolves to the last enabled team in load order that is that constructor. Career addons and line-ups name constructors, not one pack's keys, and they work with whichever pack provides the team. Each pack's team is still its own row with its own stat id.

A team says which constructor ids it provides in `provides`. The stock rows of the game's real teams provide their own ids (`game/williams` provides `constructor/williams`), so a pack's team that provides the same id wins by coming later in load order. When two enabled addons provide the same id, the core logs it and the conflicts report lists the loser (see [Shared ids](/std/providers#shared-ids)).

```cpp
// a 2017 season slot that takes Toro Rosso from whichever 2017 pack provides it
career::SeasonTeam slot = { .slot = 7, .team = teams::identities["constructor/toro_rosso"] };
```

```text
[registry] team.constructor constructor/toro_rosso: provided by swofty.classic-seasons-2017/toro_rosso
```

## Logos per context

<Split>

Each logo record holds one image per context. A context takes a single image of any size: the core scales it to the slice size of every array that context feeds (the team select context feeds two arrays of different sizes). The core builds the texture arrays from every logo record, slice by slice. Classic Seasons, car ports and season packs each change their own teams' slices, and all of them apply.

Progressive changes logos per season through `team.logo.resolve`. The game formats logos every frame on the HUD and in menus. The hook point runs when content becomes active or a session loads, once per team and context. The core builds a table from the results, and the game reads the table each frame.

<template #example>
<Example root="swofty.progressive" code-file="native/src/logos.cpp" files="swofty.json, logos/williams_2017_driver_select.png">
<template #code>

```cpp
#include <swofty/events.hpp>
#include <swofty/systems/teams.hpp>

using namespace swofty;

SWOFTY_ON_LOAD
{
    events::on<teams::events::LogoResolve>().in(events::owner("swofty.progressive"))
        .before([](teams::events::LogoResolve &l, const events::Context &ctx) {
            auto key = progressive::season_logo(l.team(), ctx.career()->year, l.context);
            if (!key.empty())
                l.set_key(key);
        });
}

SWOFTY_ADDON()
```

</template>
</Example>
</template>
</Split>

```text
[events] team.logo.resolve (driver_select, game/williams, 2017): swofty.progressive -> progressive_williams_2017
```

```cpp
teams::set_logo(teams::identities["game/williams"], teams::logos["swofty.progressive/williams_2017"]);
```

## Readiness and limits

- **Partial.** The logo format sites (`site_Game_DriverSelect_TeamLogoFormat`, `site_Game_TeamSelect_TeamLogoFormat`) are L2 and shipped. The HUD badge builders are L1. The team record layout behind `Sess_Load_Teams` is L1, and `set_colours` waits on it.
- **Unknown:** a single logo-key resolver (logos are formatted at several sites) and the display-name lookups behind `team.name.resolve`.
- **Stat ids:** teams use ids up to 514, from the install's store.
- **A new team row needs all 30 keys,** `story_info` included, or the game reads garbage.
- **Side data** is found by team label. The core copies the donor's rows, relabelled, in every data group that holds one: pit times, initial cash, facilities, driver market, recognition, renown and form, primary sponsors and showroom history. Team customisation per season (suit, gloves) belongs to the wearables records, animation team data follows the vehicle, and the team icons and story standings are not copied.
- **Constructors:** a `team.constructor` record names the team that is that constructor, the last enabled one in load order. A team reference by constructor resolves to that team's row label.
- **Logos:** at boot the core reads the current `ui_package` archives of the game folder (so other addons' edits are kept), encodes each `team.logo` PNG to a BC7 slice (`apply/content/bc7.hpp`, byte for byte the output of Classic Seasons' `logos.py`), appends the slices and keys to the stock texture arrays and to the key lists of `overrides.uidb`, and writes the changed archives to `swofty/addons/swofty.logos/` (named by a hash of the records, the PNGs and the sizes and times of the archives, so an unchanged boot reuses them). The game's opens of those archives are redirected to the copies through the file route; nothing in the game folder changes. A logo whose image is the stock key of another team copies that team's slice. A context takes one image. A context the record gives no image takes the team's own image of another context (a timing tower array the other tower context's first, every other array the driver select image first) and logs `team.logo: <team> uses its <context> image for <context>`; a team with an image file gets its own timing tower key. Only a team with no image file at all takes its donor's slice for the contexts it gives nothing for (`team.logo: <team> uses <donor>'s <context> logo`). When it feeds arrays whose slices differ in size, the core scales the image to each array's slice size, keeping its aspect ratio, centred on transparent padding (area average when shrinking, bilinear when growing, on straight alpha), and logs `team.logo: <key> scaled WxH -> SxS for <array>`; an image already of that size is used as it is. `set_logo` records the team's logo for the session (career operations table version 5).
- **Logo routes per context:**

| Context | Array | Route |
| --- | --- | --- |
| driver_select, hub | `teams.driver_select_team_logos` (23 archives) | grown array, one slice and one key per team label |
| team_select | `team_logos.driver_career_2024_team_select` (4 archives), `teams.mp_team_select_logos` | grown array |
| hud | `osd.timing_tower.team_badges_hud` (1 archive) | grown array, key `timing_tower_texture_path` |
| broadcast | `osd.timing_tower.team_badges_broadcast`, `teams.badges_broadcast` (8 archives), `broadcast_ui_generic.grid_team_logos` | grown array, key `timing_tower_texture_path` for the timing tower |

- **Applied in game:** a `team.identity` record built its row from a donor with side rows, and five logo slices were built into 33 archives and routed. A team shows in team select through a content package that lists it; a team in no package has no tile. Career with a new team (`career::apply_teams`) has not been run in game.
- **Logo timing:** the redirect has to be installed before the game first opens a changed archive. The file route logs `the game opened this path before` when it was not; that case is not yet handled with a fallback to the donor's badge. The `eaid://` texture route (`TeamRecord.show_custom_badge_in_UI` with an added `ui_package/art/textures/<name>` resource) fits the HUD and broadcast pills only and is not run in game.
- **Dressing:** `career::apply_teams` dresses a slot row as the season's team through the team applier. The names and colours are written into the loaded team record once the core reaches the entrants system's team slots; until then the request is queued and the report counts it as waiting.
- **Grid:** `team_position` and garages limit how many teams a session holds. A track whose world has fewer garages than a box has teams is left out of that box (see [Tracks](/std/systems/tracks)).
- **Colours** are signed ARGB in the database, and the core converts them.
