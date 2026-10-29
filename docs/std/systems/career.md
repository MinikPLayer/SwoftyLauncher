---
title: Career
description: Career modes, contents, seasons, calendars, ratings, characters and wizards in code, with the career events and operations.
---

# Career

With the career system you build Driver Career and My Team careers: a career's tile, its seasons and line-ups, the calendar, ratings, contracts, R&D, money and the people in it. Progressive Career, for example, plays Driver Career from 2016 to 2026 with each year's real line-ups, ratings and cars.

A **career mode** is the game's mode row, with its own save container and **data group** (the key that keeps one mode's career tables and save slots apart). A **season** record is one year's line-up. Each season names the **box** it races in: the Grand Prix and Time Trial season build whose cars, looks and rules it uses.

## The header

<Split>

`swofty/systems/career.hpp` has eight registries. The main ones:

- `career::modes`: a mode row, its save container and data group (`career.mode`);
- `career::contents`: a career tile and the data it clones (`career.content`);
- `career::seasons`: a season's box, line-up and mid-season changes (`career.season`);
- `career::calendars`: an event list (`career.calendar`);
- `career::ratings`: a driver's ratings at the start of a season (`career.driver_ratings`).

The rest: `characters`, `wizards`, `hub_tabs`.

<template #example>
<Example root="swofty-sdk" code-file="include/swofty/systems/career.hpp">
<template #code>

```cpp
#pragma once
#include <swofty/career.hpp>
#include <swofty/events.hpp>
#include <swofty/registry.hpp>
#include <swofty/systems/content.hpp>
#include <swofty/systems/drivers.hpp>
#include <swofty/systems/teams.hpp>

namespace swofty::career {

enum class Kind { driver_career, my_team };
enum class Freeze { per_season, at_creation };
enum class AfterLast { market, hold, end };
enum class ChangeKind { swap, stand_in, move, replace };
enum class Careers { driver, my_team, both };

struct ModeRecord {
    static constexpr Version version{1, 0};
    std::string data_group;                    // partitions 8 career tables and the save slots: "f1_career_progressive"
    std::string save_container;                // save_data.name: the mode's own container in the profile save
    Text title;                                // the career tile's title
    Kind kind = Kind::driver_career;
    Freeze freeze = Freeze::per_season;        // per_season: each season's line-up, ratings and rules freeze when it starts
    StatId stat_id = StatId::automatic;        // a game_modes.ModeChampionship row
    json::Value extra;
};
SWOFTY_RECORD(ModeRecord, data_group, save_container, title, kind, freeze, stat_id)
inline Registry<ModeRecord> modes{"career.mode", {.applies = Applies::boot}};

struct Content {
    static constexpr Version version{1, 0};
    Ref<ModeRecord> mode;
    Path tile;                                 // the tile image
    Ref<Content> model;                        // clone the 8 data-group tables, championship and components from it
    Ref<Calendar> calendar;                    // the wizard's calendar
    AfterLast after_last = AfterLast::market;  // past the last season record: the market, the last year forever, or the end
    Text title;
    Text description;
    std::string data_group;
    std::string suffix;
    std::string save_container;
    int car_group = -1;
    int badge = 0;
    std::string entitlement;
    Careers careers = Careers::driver;
    json::Value extra;
};
SWOFTY_RECORD(Content, mode, tile, model, calendar, after_last, title, description, data_group, suffix, save_container,
              car_group, badge, entitlement, careers)
inline Registry<Content> contents{"career.content", {.applies = Applies::boot}};

struct SeasonTeam {
    int slot = 0;                              // 1-12, the career team that carries this season's team
    Ref<teams::Identity> team;                 // empty: the slot is empty this season
    std::array<Ref<drivers::Identity>, 2> seats;
};
SWOFTY_RECORD(SeasonTeam, slot, team, seats)

struct SeasonChange {
    int round = 0, slot = 0, seat = 0;         // from this round on, this seat
    Ref<drivers::Identity> out, in;
    ChangeKind kind = ChangeKind::swap;        // swap: the two drivers exchange seats; stand_in: `in` drives until
                                               // `until_round`, then `out` returns; move: `in` was on the grid in
                                               // another seat and moves to this one; replace: `in` takes the seat for
                                               // the rest of the season and `out` leaves the grid
    std::optional<int> until_round;            // stand_in only
};
SWOFTY_RECORD(SeasonChange, round, slot, seat, out, in, kind, until_round)

struct Season {
    static constexpr Version version{1, 0};
    std::string data_group;
    int year = 0;
    Ref<::swofty::content::Package> box;       // the box it races in: its cars, looks and rules; .or_else() names the fallback
    Ref<Calendar> calendar;                    // the season's real calendar; empty: the career's
    Keyed<SeasonTeam, &SeasonTeam::slot> teams;
    Keyed<SeasonChange, &SeasonChange::round, &SeasonChange::slot, &SeasonChange::seat> changes;
    json::Value extra;                         // fields from a newer minor, kept for the round trip
};
SWOFTY_RECORD(Season, data_group, year, box, calendar, teams, changes)
inline Registry<Season> seasons{"career.season",
                                {.applies = Applies::read, .snapshot = true, .snapshot_budget = 65536}};

struct Round {
    int round = 0;
    Text race;                                 // the race name: "lng_gp_usa"
    Ref<tracks::Venue> venue;                  // resolves to the enabled track that provides it (the last in load order)
    std::chrono::year_month_day date;          // the game dates weekends from the track data in the mode's year
};
SWOFTY_RECORD(Round, round, race, venue, date)

struct Calendar {
    static constexpr Version version{1, 0};
    Keyed<Round, &Round::round> rounds;        // schedule.event_lists rows
    int seasons = 10;                          // _season_instancecount_: the career length
    std::vector<int> transfer_windows;         // the round after which a window opens
    json::Value extra;
};
SWOFTY_RECORD(Calendar, rounds, seasons, transfer_windows)
inline Registry<Calendar> calendars{"career.calendar", {.applies = Applies::boot}};

struct DriverRatings {
    static constexpr Version version{1, 0};
    Ref<Season> season;
    Ref<drivers::Identity> driver;
    float experience = 0, racecraft = 0, awareness = 0, pace = 0;   // 0..100, one decimal
    json::Value extra;
};
SWOFTY_RECORD(DriverRatings, season, driver, experience, racecraft, awareness, pace)
inline Registry<DriverRatings> ratings{"career.driver_ratings",
                                       {.applies = Applies::read, .snapshot = true, .snapshot_budget = 65536}};

struct CharacterSwap {
    std::string character;
    std::string with;
};
SWOFTY_RECORD(CharacterSwap, character, with)

struct Character {
    static constexpr Version version{1, 0};
    Ref<Character> replaces;                   // the stock character it stands in for, per data group
    Text first_name, last_name;
    Path portrait;                             // the core makes the message_avatars slice
    std::vector<std::string> mute;             // speech lines it mutes: "agent", "agent_cutscene"
    std::string data_group;
    Keyed<CharacterSwap, &CharacterSwap::character> swaps;
    std::map<std::string, std::map<std::string, std::string>> strings;
    json::Value extra;
};
SWOFTY_RECORD(Character, replaces, first_name, last_name, portrait, mute, data_group, swaps, strings)
inline Registry<Character> characters{"career.character", {.applies = Applies::content}};

struct WizardStep {
    std::string route;                         // the setup screen it opens, in flow_career_season_setup
    Text title;
    std::vector<std::string> options;          // the addon options this step sets for the new career
};
SWOFTY_RECORD(WizardStep, route, title, options)

struct WizardRoute {
    std::string from;
    std::string to;
};
SWOFTY_RECORD(WizardRoute, from, to)

struct WizardSwap {
    int at = 0;
    int with = 0;
};
SWOFTY_RECORD(WizardSwap, at, with)

struct Wizard {                                // new-career setup steps, after the game's own
    static constexpr Version version{1, 0};
    Ref<Content> content;                      // the career tile it runs for
    std::vector<WizardStep> steps;
    std::string data_group;
    bool starts_in_f1 = false;
    Keyed<WizardRoute, &WizardRoute::from> tile_routes;
    Keyed<WizardSwap, &WizardSwap::at> replace_steps;
    Keyed<WizardSwap, &WizardSwap::at> force_options;
    Keyed<WizardRoute, &WizardRoute::from> confirm_routes;
    std::vector<std::string> hide_driver_tabs;
    json::Value extra;
};
SWOFTY_RECORD(Wizard, content, steps, data_group, starts_in_f1, tile_routes, replace_steps, force_options, confirm_routes,
              hide_driver_tabs)
inline Registry<Wizard> wizards{"career.wizard", {.applies = Applies::content}};

struct HubTab {
    static constexpr Version version{1, 0};
    std::string name;
    std::string title;
    std::string data_group;
    std::vector<std::string> options;
    json::Value extra;
};
SWOFTY_RECORD(HubTab, name, title, data_group, options)
inline Registry<HubTab> hub_tabs{"hub.tab", {.applies = Applies::read}};

namespace events {

struct SeasonStart {                           // a season begins: the first, then each after the break (notify)
    static constexpr const char *point = "career.season.start";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    int32_t year;
    int32_t season_index;
};

struct AddActivity {                           // Game_CareerActivities_AddActivity (cancel)
    static constexpr const char *point = "career.calendar.add_activity";
    static constexpr uint32_t version = 1;
    static constexpr uint8_t contract_event = 0x0f;
    uint32_t size, payload_version;
    void *calendar;                            // the driver calendar the activity goes on
    const void *activity;                      // the activity definition
    uint32_t driver;                           // the calendar's driver, stat id << 16
    uint32_t day;                              // before handlers may move it
    uint8_t type;                              // the activity type
    bool player;                               // the calendar is the human player's
};

struct ContractDecide {
    static constexpr const char *point = "career.contract.decide";
    static constexpr uint32_t version = 1;
    static constexpr uint32_t negotiate = 0x5e3c3c2d;
    static constexpr uint32_t ends_this_season = 0xd2985448;
    static constexpr uint32_t next = 0x7c9b1ec4;
    uint32_t size, payload_version;
    void *state;
    uint32_t link;
};

struct ContractSign {
    static constexpr const char *point = "career.contract.sign";
    static constexpr uint32_t version = 1;
    static constexpr uint32_t work_out_replaced = 0x3fff0000;
    uint32_t size, payload_version;
    void *contracts;
    void *contract;
    uint32_t driver;
    uint32_t replaced;
    uint32_t team;
    uint8_t type;
    bool notify;
};

struct StandingsRebuild {                      // Career_Rebuild_SeasonStandings (modify)
    static constexpr const char *point = "career.standings.rebuild";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    void *championship;                        // the standings the game rebuilt
};

// no payload beyond size and payload_version: Created (career.created), Loaded (career.loaded),
// Unloaded (career.unloaded), SeasonEnd (career.season.end), SeasonBreak (career.season.break), Saving (career.saving)

} // namespace events

struct ApplyReport {                           // what apply_teams and apply_lineup did, seat by seat
    size_t changed = 0, unchanged = 0, refused = 0;
    std::vector<std::string> refusals;         // one line per refused seat: which seat, and why
};

const SwoftyCareerOps *ops();
Result set_slots(std::string_view data_group, const std::vector<uint32_t> &team_stats);
Result apply_teams(const Season &season, ApplyReport *report = nullptr);
Result apply_lineup(const Season &season, ApplyReport *report = nullptr);
Result set_seat(int slot, int seat, Ref<drivers::Identity> driver);
Result set_seat_of_team(uint32_t team_stat, int seat, Ref<drivers::Identity> driver);
Result set_driver_ratings(Ref<drivers::Identity> driver, const DriverRatings &ratings);
Result set_car_number(Ref<drivers::Identity> driver, int number, bool swap = false);
Result swap_drivers(Ref<drivers::Identity> a, Ref<drivers::Identity> b);   // the game's full move sequence
Result apply_dropped_scores(const events::StandingsRebuild &, int);   // unavailable
Result report_season(int year, int season_index);   // the career's owner: the season at season_index is year

} // namespace swofty::career
```

</template>
</Example>
</template>
</Split>

## Progressive's career mode

<Split>

Progressive Career plays Driver Career from 2016 to 2026 with each year's real line-ups and ratings. Its mode is a variant of the 2026 Driver Career with its own data group and save container. Its careers never mix with stock ones, and changes other addons make to the stock mode flow into it.

It reads its own `data/years.json` and registers a season and a set of ratings per year. Each season names its box: Classic Seasons' box for that year, with the stock year as the fallback. At each season start it applies the line-up.

<template #example>
<Example root="swofty.progressive" code-file="native/src/register.cpp" data-file="registry/career.json" files="swofty.json, native/CMakeLists.txt, native/src/years.hpp, native/src/years.cpp, native/src/calendars.cpp, data/years.json, art/tile.png">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/events.hpp>
#include <swofty/systems/career.hpp>
#include "years.hpp"                               // progressive::years(): data/years.json, its own format

using namespace swofty;

static const events::Selector kProgressive = events::content("swofty.progressive/f1_career_progressive");

SWOFTY_REGISTER
{
    auto mode = career::modes.variant("progressive", career::modes["game/f1_career_2026"], [](career::ModeRecord &m) {
        m.data_group = "f1_career_progressive";
        m.save_container = "driver_career_progressive";
        m.title = "lng_progressive_title";
    });

    career::contents.add("f1_career_progressive", {
        .mode = mode,
        .tile = file("art/tile.png"),
        .model = career::contents["game/f1_career_2026"],
    });

    for (const progressive::Year &y : progressive::years()) {
        auto season = career::seasons.add(std::to_string(y.year), {      // "swofty.progressive/2017"
            .data_group = "f1_career_progressive",
            .year = y.year,
            .box = content::packages[y.box].or_else(content::packages["game/f1_2025"]),   // "swofty.classic-seasons-2017/f1_2017"
            .calendar = career::calendars[y.calendar],                                   // the year's real calendar (calendars.cpp)
            .teams = y.teams(),                    // slot, team, two seats
            .changes = y.changes(),                // mid-season swaps and stand-ins by round
        });
        for (const progressive::Rating &r : y.ratings)
            career::ratings.add(std::format("{}/{}", y.year, r.driver), {
                .season = season,
                .driver = drivers::identities[r.driver],
                .experience = r.experience, .racecraft = r.racecraft, .awareness = r.awareness, .pace = r.pace,
            });
    }
}

SWOFTY_ON_LOAD
{
    events::on<career::events::SeasonStart>().in(kProgressive)
        .after([](const career::events::SeasonStart &e, const events::Context &ctx) {
            auto season = ctx.record<career::Season>();    // this season, frozen as it started
            career::ApplyReport report;
            career::apply_lineup(*season, &report);
            swofty::log("[progressive] %d: %zu seat(s) set, %zu unchanged, %zu refused", e.year,
                        report.changed, report.unchanged, report.refused);
        });
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
{
  "career.mode": {
    "progressive": { "extends": "game/f1_career_2026", "data_group": "f1_career_progressive",
                     "save_container": "driver_career_progressive", "title": "lng_progressive_title" }
  },
  "career.content": {
    "f1_career_progressive": { "mode": "swofty.progressive/progressive", "tile": "art/tile.png",
                               "model": "game/f1_career_2026" }
  },
  "career.season": {
    "2017": { "data_group": "f1_career_progressive", "year": 2017,
              "box": "swofty.classic-seasons-2017/f1_2017|game/f1_2025", "calendar": "swofty.progressive/2017",
              "teams": [ { "slot": 1, "team": "game/mercedes", "seats": ["game/lewis_hamilton", "game/valtteri_bottas"] } ],
              "changes": [ { "round": 15, "slot": 7, "seat": 1, "out": "game/daniil_kvyat", "in": "game/pierre_gasly" } ] }
  },
  "career.driver_ratings": {
    "2017/game/lewis_hamilton": { "season": "swofty.progressive/2017", "driver": "game/lewis_hamilton",
                                  "experience": 94, "racecraft": 93, "awareness": 90, "pace": 95 }
  }
}
```

</template>
</Example>
</template>
</Split>

```text
[registry] register: swofty.progressive (progressive.dll) 1 career.mode, 1 career.content, 11 career.season,
           242 career.driver_ratings (early stage, 3.8 ms)
[registry] stat ids: career.mode swofty.progressive/progressive -> 82
[registry] applied at boot: career.mode 1, career.content 1 (8 data-group tables cloned from game/f1_career_2026) (1,204 database records)
[registry] career season 2016 starts in swofty.progressive/f1_career_progressive: snapshot of career.season 1,
           career.driver_ratings 22 (3 KB)
[progressive] 2016: 22 seat(s) set, 0 unchanged, 0 refused
```

The data version has no season-start handler. It lists the seasons, and applying a line-up needs code.

## A season's box

A season's box supplies its cars, liveries, sounds and session rules. The box each season uses is chosen when the career is created.

- **Two packs for one year.** When more than one installed pack offers a box for a season's year, the career setup page shows a choice for that year. It defaults to the box the season names, and the choice is frozen at creation.
- **A missing pack at the season break.** If the season's box is gone when its season starts, and the pack brought rules or systems, the career stops at the season break with a guard popup naming the pack. If the pack only brought cars and looks, the season uses the fallback after `.or_else()`, and the pack's tile in ADDON SETTINGS and in the launcher says so. No popup appears.

```text
[registry] career season 2019: box swofty.classic-seasons-2019/f1_2019 is off (cars and looks only); using game/f1_2025
```

## What a career freezes

A career freezes each season's line-ups, ratings and rules when that season starts. Drivers and tracks installed mid-career join at the next season. A fix to Progressive's 2019 line-up reaches a 2016 career when it gets to 2019.

A career's owner can freeze everything at creation instead:

```cpp
career::modes.layer(career::modes["swofty.progressive/progressive"], [](career::ModeRecord &m) {
    m.freeze = career::Freeze::at_creation;
});
```

The career's hub can offer the same choice to the player as a **Freeze content** toggle. Cars, liveries, tracks and sounds are never frozen. A career reads them live from its boxes.

The frozen data is the career's snapshot, saved in its `swofty_registry` entry. Each snapshot registry has a budget of 64 KB per season, and a career has 256 KB in all.

### When a season starts

The core fires `career.created` for the first career loaded after a new-career route. It fires `career.season.start` once per career and year, then activates the season's records and freezes the season.

The season's year comes from one of two places:

- **The career's owner reports it.** The addon that owns the data group, through its career content or its `career.mode` record, calls `career::report_season(year, season_index)` at the season's start. Any other addon gets `Result::refused`. A career an addon owns takes its years from these reports only.
- **The core works it out.** For a career no addon owns, such as the stock 2025 and 2026 careers, the core reads the Championship's `season_index` at an idle Team HQ once the season has its race weekends and a current weekend. The effective `career.season` records of the career's data group, sorted by year, give the year: index 0 is the first year, index 1 the second. With no records, two records for one year or a gap between years, nothing fires and the log says why.

```cpp
// Progressive, at the first idle Team HQ of a season it hasn't applied, once the season has its weekends:
// year is the career's first year plus season_index and its test seasons
career::report_season(year, season_index);
```

```text
[progressive] season 2017: reported to the core as season index 1 of career 1f2a: ok
[registry] career::report_season(2017, 1) from swofty.progressive: ok, career 1f2a season index 1 is 2017; career.season.start
[registry] career season 2017 starts in 1f2a/f1_career_progressive: snapshot of career.season, career.driver_ratings (39 record(s), 12.0 KB -> 2.4 KB compressed); saved with the next profile save
[registry] career 7c01 season index 1: no career.season.start (no career.season record has data group f1_career_2026)
```

A career's id is its mode and its save slot: `<mode>:<slot guid>`, or `<mode>:<slot id>` for a slot without a guid. The mode is the save container name of the career content that owns the data group, else `<data group>.<driver_career|my_team>`. The core reads the slot when the game selects it for a load and keeps the id once `career.loaded` fires. An entry saved under a career's older `dc-<driver>` key moves to the new id the first time that career loads.

A career's snapshot has 256 KB, and the whole entry 4 MB of payload. An entry past 4 MB isn't written with that save.

The core writes the entry with a profile save only with `SWOFTY_REGISTRY_SAVE=on`. Every profile save logs the entry's size. By default each profile save logs the entry it would write and how many bytes it would add, and writes nothing:

```text
[registry] swofty_registry dry: this profile save would write a 2591-byte entry (1 career(s), 1 season snapshot(s)), adding 2591 bytes to the save; nothing written (SWOFTY_REGISTRY_SAVE is unset, and saving defaults to dry)
```

## After the last year

A career can run past the last season record its addon provides: Progressive past 2026, or Classic Career past its last pack. By default (`AfterLast::market`) the career keeps the last year's box, with its cars and looks, and hands the line-up to the game's driver market. The core extends the retirement schedules past their last stock date, December 2035, so drivers keep retiring.

The career's owner can choose otherwise:

```cpp
career::contents.layer(career::contents["swofty.progressive/f1_career_progressive"], [](career::Content &c) {
    c.after_last = career::AfterLast::hold;    // the last year's line-up every season; end: the last year is the final season
});
```

## Calendars

The 1.6.0 core stores `career.calendar` records and applies nothing from them; it logs `[registry] career.calendar: N record(s); this core applies nothing from it`. Likewise `career.mode` records: the core reads a mode's data group and applies nothing else.

```cpp
auto calendar = career::calendars.add("f1_1990", {
    .rounds = {
        { .round = 1, .race = "lng_gp_usa", .venue = tracks::venues["venue/phoenix"].or_else(tracks::venues["venue/las_vegas"]),
          .date = 1990y / 3 / 11 },
        { .round = 4, .race = "lng_gp_monaco", .venue = tracks::venues["venue/monaco"], .date = 1990y / 5 / 27 },
    },
    .seasons = 1,
});
```

The game dates every weekend from the track data in the mode's year. It builds every season's weekends when a career is created, and the save keeps that list, so a calendar with more seasons reaches new careers only.

A history career can race each season's real calendar (`Season::calendar`). The core rebuilds the next season's rounds at the season break and drops venues that aren't installed. The career setup page asks whether to use real calendars or the wizard's calendar every season. New careers default to real calendars.

```text
[registry] career season 2017 calendar: 20 rounds from swofty.progressive/2017; venue/sepang not installed, round dropped
```

## Described, in headers that come later

The market, contracts, R&D, facilities, the economy and sponsors are described here. The 1.6.0 headers have no registry for them: `career.market`, `career.contract_rules`, `career.rnd.department`, `career.rnd.tree`, `career.facility`, `career.economy` and `career.sponsor`.

```cpp
struct RndDepartment {
    Text name;
    Colour colour;
    Path icon;
    float cost_scale = 1.0f;                   // UpgradeDepartmentEntry economy scale
};

struct Retirement {                            // one retirement schedule
    std::chrono::year_month_day date;          // stock schedules use absolute dates; the last is December 2035
    std::vector<Ref<drivers::Identity>> drivers;   // up to 8
};

struct Market {                                // driver_market, per data group
    std::string data_group;
    std::vector<Retirement> retirements;       // driver_retirements: 6 schedules of 8 drivers in stock
    bool firings = true;                       // driver_firings
    std::vector<Ref<drivers::Identity>> available;   // drivers_available_from_team_setup
};
SWOFTY_RECORD(Market, data_group, retirements, firings, available)

struct ContractRules {                         // driver_contracts and career.generic_contracts, per data group
    std::string data_group;
    std::map<std::string, std::array<int, 2>> rating_ranges;   // the rating range each team tier asks for
    std::array<int64_t, 2> salary;             // the lowest and highest salary offered
    int secret_meetings = 9;                   // secret meetings a season offers
    bool counteroffers = true;                 // career.generic_contracts counteroffers
};
SWOFTY_RECORD(ContractRules, data_group, rating_ranges, salary, secret_meetings, counteroffers)

struct RndUpgrade {                            // one node of a tree: an upgrade_N row
    int cost = 0, weighting = 0, size = 0;
    int type = 0;                              // upgrade_type: 0..18 (the game keeps a std::bitset<19>)
    Ref<RndDepartment> department;
    std::vector<int> links;                    // the nodes it unlocks, by index
    std::array<int, 2> grid_coordinates;       // where the node sits in the tree
    int unlock_at_facility_level = 0;
};

struct RndTree {                               // one team's tree in one data group
    std::string data_group;
    std::string token;                         // one of the 12 compiled-in team tokens: "ferrari" .. "user_defined"
    std::vector<Ref<RndDepartment>> departments;
    std::vector<RndUpgrade> nodes;
};
SWOFTY_RECORD(RndTree, data_group, token, departments, nodes)

struct Facility {                              // career.my_team_facilities: Engineering, Personnel, Corporate
    Text name;
    std::string group;                         // "engineering", "personnel", "corporate"
    std::vector<std::string> perk_tree;        // the FacilityPerkTree nodes, in order
    StatId stat_id = StatId::automatic;
};
SWOFTY_RECORD(Facility, name, group, perk_tree, stat_id)

struct Economy {                               // resource points and bank, per data group
    std::string data_group;
    std::map<std::string, float> resource_point_rates;   // resource_points.*
    std::map<std::string, int64_t> budgets;    // bank: the budget per team key
    int64_t initial_cash = 0;                  // bank: starting cash
};
SWOFTY_RECORD(Economy, data_group, resource_point_rates, budgets, initial_cash)

struct Sponsor {                               // career.my_team_sponsors: 15 title and 72 secondary in stock
    Text name;
    Path logo;
    bool title = false;                        // a title sponsor
    std::vector<int> loyalty;                  // the loyalty track: points per level
    StatId stat_id = StatId::automatic;
};
SWOFTY_RECORD(Sponsor, name, logo, title, loyalty, stat_id)
```

A market record for a mode of its own would clear the stock retirements while its seasons cover the grid. The game draws four R&D departments natively; a fifth and more come from native code, as R&D Tyres does. Team trees are found through 12 compiled-in tokens, and a 13th tree needs native code.

## Characters and wizards

```cpp
career::characters.add("flavio", {
    .replaces = career::characters["game/elena"],
    .first_name = "lng_f1990_agent_first",
    .last_name = "lng_f1990_agent_last",
    .portrait = file("art/flavio.png"),
    .mute = { "agent", "agent_cutscene" },
});
```

## Events

```cpp
// Progressive: a Follow-your-driver player signs in the first Team HQ, so the contract event never goes on his calendar
events::on<career::events::AddActivity>().in(kProgressive)
    .replace([](career::events::AddActivity &a, const events::Context &) {
        if (a.type != career::events::AddActivity::contract_event || !a.player || progressive::follow_mode() != 1)
            return false;
        return progressive::sign_instead(a);
    });
```

## Operations

```cpp
career::set_slots("f1_career_progressive", { 476, 477, 478, 484, 480, 481, 479, 482, 485, 483, 486 });
career::apply_lineup(*career::seasons.get(career::seasons["swofty.progressive/2017"]), &report);
career::set_driver_ratings(drivers::identities["game/lando_norris"], { .experience = 70, .racecraft = 88, .awareness = 85, .pace = 90 });
career::swap_drivers(drivers::identities["game/alex_albon"], drivers::identities["game/logan_sargeant"]);
career::report_season(2017, 1);
```

The calls run on the core's career operations table (`SwoftyCoreCareerOps`, `SwoftyCareerOps` in `swofty_api.h`, `SWOFTY_CAREER_OPS_VERSION` 6). A call returns `unavailable` on a core whose table is older than the version that added it. `apply_dropped_scores` returns `unavailable` on every core.

| Table version | Adds | Header call |
| --- | --- | --- |
| 1 | `available`, `driver_stat`, `set_slots`, `apply_lineup`, `set_driver_ratings`, `swap_drivers` | `career::set_slots`, `career::apply_lineup`, `career::set_driver_ratings`, `career::swap_drivers` |
| 2 | `report_season` | `career::report_season` |
| 3 | `apply_teams` | `career::apply_teams` |
| 4 | `set_seat`, `set_car_number`, `set_track_fuel` | `career::set_seat`, `career::set_seat_of_team`, `career::set_car_number`, `tracks::set_track_fuel` |
| 5 | `set_logo` | `teams::set_logo` |
| 6 | `set_look` | `drivers::set_look` |

`set_seat` takes a 1-based `slot` of the loaded career's data group, `set_seat_of_team` a team stat id; `set_car_number` takes `swap`, which gives the driver who held the number the caller's old one. `set_driver_ratings` writes experience, racecraft, awareness and pace by game stat id 0, 1, 3 and 4.

- **Slots:** a season's `slot` is one of the data group's career team slots. `set_slots` names the team each slot races as, by team stat id, and `apply_lineup` reads the table of the season's `data_group`. Without a table every seat is refused.
- **`apply_teams`:** each slot the season lists (1-12) races as its team; a team with no career rows gets them, built from its donor's. An empty team leaves the slot out of the Championship, and a slot the season doesn't list keeps its team. Call it before `apply_lineup`.
- **Added teams in the new-career wizard:** a season whose slot names a team an addon made (its own `team.identity`, not a `game/` row) is an added team. The career's team rows are the ones its career list holds for every start season, so the added team wears its slot's row: the request `{"start": {"data_group": "<group>", "year": <start season>}}` dresses those rows (names, colours) while the wizard shows that start season, and `"year": 0` takes the dress off. The season start dresses the same row the same way, so the career never gets a second row for the team. At boot, each new driver (a `driver.identity` with a donor) of an added team gets a teamless entrant in the first teamless list the career list references, so the career has a row to seat him in.
- **`set_seat`:** the driver takes race seat 0 or 1 of a slot in Driver Career. A race driver exchanges seats with the seat's driver; a free agent takes the seat and the seat's driver leaves the grid.
- **What `apply_lineup` changes:** each seat's driver moves to it; a race-seat driver whose seat goes to someone else, and who has no seat in the record, leaves the grid. Mid-season `changes` aren't applied.
- **What it refuses:** a driver not in the drivers table, a driver without a single career row, a driver named on two seats (the second), the player (never moved) and the player's seat (never given away). The refused seats keep their drivers.

A batch of seat changes follows the game's own sequence, in this order:

1. for each driver who leaves the grid, his contract set to state 3;
2. for each driver who moves or leaves, `MessageDriverMoveConfirmed`, then his entrant write;
3. every row read back against the plan, and every write undone on a mismatch;
4. each seated driver without a contract for his team signs one: state 4 or 5, then `MessageDriverContractChanged`;
5. a number for each seated row without one;
6. the line-up changes against the line-up from before the batch, then the three moves-finalised posts (upgrades, recognition, plain).

Any other order crashes the career. Every operation runs on the flow thread.

```text
[career-ops] apply_lineup 2017 (season, f1_career_progressive): 1 seat(s) set, 21 unchanged, 0 refused; 2 row(s) written,
             1 contract(s) signed, 0 number(s) drawn, finalise posted (2 line-up change(s)): move fernando_alonso(7)
             mclaren_26(484) seat 1 -> mercedes_26(476) seat 1; release lewis_hamilton(5) released
```

## Readiness and limits

- **The most ready system:** 59 career and 46 game functions at L2, saves at L3. Ready: contracts, line-ups, standings, resource points, upgrade start, cancel and cost, the calendar, activities, the wizard step, save containers.
- **Partial:** Team HQ tabs (L1). Facilities, sponsors and the market are described under "Described, in headers that come later".
- **Unknown:** the resource point spend, regulation changes, a 13th team tree.
- **The save heap** is 3 MB. The core keeps career snapshots outside it, in the `swofty_registry` entry, with a budget per registry and per career.
- **R&D:** four departments drawn natively; upgrade types are a table of 19 and a `std::bitset<19>` (ids of 19 and up throw without native code).
- **The 10-season career limit** is calendar data (`_season_instancecount_`), not code. Career days are 16-bit (about 200 seasons). Stock retirement schedules end in December 2035.
- **Saves keep the season list.** Rounds of a season that hasn't started can be replaced, added or removed. The upgrade loader drops instances it doesn't know.
- **Hashed per-team career tables** can't gain rows. **Eight tables** are partitioned by data group.
- **My Team** freezes when the upgrade availability is refreshed over and over. **Follow-career scene paths** crashed new Progressive careers (#411).
