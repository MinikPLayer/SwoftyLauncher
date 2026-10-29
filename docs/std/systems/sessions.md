---
title: Sessions and weekends
description: Setup modes, weekend formats, session lengths, points, rules profiles, safety car and grid size in code, with the entrant, grid, result and penalty events.
---

# Sessions and weekends

With the session system you set what a race weekend looks like: its sessions, how the grid forms, the points, the rules and penalties, and the safety car. A 2009 season pack, for example, scores 10-8-6-5-4-3-2-1 with no point for the fastest lap, and F1 1990 runs two qualifying sessions and counts each driver's best 11 results.

A **setup mode** is the game's row that a weekend runs under: its format, length, rules and grid all hang off it. Every Grand Prix and Time Trial box shares one setup mode, `swofty_f1`, and each box's own points, DRS, tyre class and rules apply when a session loads in that box. A box gets a setup mode of its own only when it needs a different shape at boot, such as F1 1990's two qualifying sessions.

::: info In 1.6.0
The header `swofty/systems/sessions.hpp` has `sessions::Rules` (the `rules` of a `content::Package`), `sessions::set_control` and the `session.player.control` payload `sessions::events::PlayerControl`; `swofty/systems/control.hpp` has `Control` and `ControlState`. The registries on this page (`modes`, `formats`, `lengths`, `points`, `rules_profiles`, `safety_car`, `grids`) and the penalty, results and weekend events ship later; those sections describe that design.
:::

## The header

<Split>

`swofty/systems/sessions.hpp` has seven registries:

- `sessions::modes`: a setup mode, the row everything else hangs off (`session.mode`);
- `sessions::formats`: a weekend structure: sessions, groups, grid rules (`session.weekend_format`);
- `sessions::lengths`: practice, qualifying and race length options (`session.length`);
- `sessions::points`: a standing preset (`session.points`);
- `sessions::rules_profiles`: a rule manager profile and its penalties (`session.rules_profile`);
- `sessions::safety_car`: safety car and red flag frequencies (`session.safety_car`);
- `sessions::grids`: an entrant setup mode: how many cars and teams (`session.grid`).

`sessions::Rules` is not a registry. It's the value a box holds in `content::Package::rules`.

<template #example>
<Example root="swofty-sdk" code-file="include/swofty/systems/sessions.hpp">
<template #code>

```cpp
#pragma once
#include <swofty/registry.hpp>
#include <swofty/events.hpp>
#include <swofty/systems/drivers.hpp>
#include <swofty/systems/power.hpp>
#include <swofty/systems/tracks.hpp>

namespace swofty::tyres { struct Class; }

namespace swofty::sessions {

enum class Session : uint8_t {                 // the game's session id enum (0..18)
    practice_1 = 1, practice_2, practice_3,
    qualifying_1 = 5, qualifying_2, qualifying_3,
    qualifying_one_shot = 9,                          // one-shot qualifying
    sprint_shootout_1 = 10, sprint_shootout_2, sprint_shootout_3,
    sprint_shootout_one_shot,                         // one-shot sprint shootout
    race = 15, race_2, race_3,
    time_trial,
};

struct Points {
    std::vector<int> points;                   // per place, whole numbers; padded to the grid size
    std::optional<int> fastest_lap;            // fastest_lap_bonus; nullopt: none
    std::array<std::vector<int>, 3> reduced;   // reduced_points_data: races stopped at 75, 50 and 25 % distance
    StatId stat_id = StatId::automatic;        // a game_setup.session_standings row
};
inline Registry<Points> points{"session.points", { .applies = Applies::boot }};

struct GridRule;                               // how a race group's grid forms
GridRule grid_type(std::string_view game_type);                    // one of the game's grid types: "qualifying", ...
GridRule best_of(std::vector<Session> sessions);                   // each car's best time over several sessions

enum class GroupType { practice, qualifying, race };

struct Group {
    GroupType type;
    std::vector<Session> sessions;             // in order
    Text label;                                // the name the group shows; the core scopes it to this mode
    GridRule grid = grid_type("qualifying");   // race groups: how the grid forms
    Ref<Points> standings;                     // race groups: the points
};

struct Format {
    std::vector<Group> groups;                 // practice, qualifying and race groups of one weekend_structure
};
inline Registry<Format> formats{"session.weekend_format", { .applies = Applies::boot }};

struct Length {
    float laps_multiplier = 1.0f;              // race laps from the track's full distance
    int laps_override = 0;                     // fixed laps; 0: use the multiplier
    float tyre_wear_scale = 1.0f;
    int weather_slots = 0;                     // forecast slots across the session
    int flashback_limit = -1;                  // -1: unlimited
};
inline Registry<Length> lengths{"session.length", { .applies = Applies::boot }};

struct RulesProfile {
    bool blue_flags = true;
    int corner_cutting = 2;                    // 0 off .. 3 strict
    bool collisions = true;
    bool drive_through_replacement = false;    // a drive-through becomes a time penalty
    bool jump_starts = true;
    bool parc_ferme = true;
};
inline Registry<RulesProfile> rules_profiles{"session.rules_profile", { .applies = Applies::boot }};

struct SafetyCar {
    std::array<int, 8> max_deployments = { 0, 0, 3, 3, 2, 3, 5, 8 };   // per race length option
    std::array<int, 8> max_red_flags = { 0, 0, 1, 1, 1, 1, 2, 3 };
};
inline Registry<SafetyCar> safety_car{"session.safety_car", { .applies = Applies::boot }};

struct Grid {
    int cars = 22;                             // max_entrants; clamped to the install's grid size, set once at boot
    int teams = 11;                            // max_teams
    bool reuse_entrants = false;
};
inline Registry<Grid> grids{"session.grid", { .applies = Applies::boot }};

struct Mode {
    std::string branding;                      // a game_setup.branding row: logos, MFD, car setup
    Ref<Format> format;                        // weekend_options
    Ref<Length> race_length;
    Ref<RulesProfile> rules;
    Ref<SafetyCar> safety;
    Ref<Grid> grid;                            // entrant_setup_modes
    Ref<power::DrsRule> drs;                   // drs_allowed_lap_group
    bool pitstops = true;                      // enable_pitstops
    bool night_starts = false;                 // night_starts_enabled
    std::vector<Ref<tracks::Track>> restricted_tracks;
    StatId stat_id = StatId::automatic;        // a game_setup_modes row: 31 or lower (a u32 mask)
};
inline Registry<Mode> modes{"session.mode", { .applies = Applies::boot }};

struct Rules {                                 // a box's session rules (content::Package::rules), applied at session load
    Ref<Mode> mode;                            // empty: swofty_f1, the mode every GP/TT box shares
    Ref<Points> points;                        // empty: the mode's
    Ref<power::DrsRule> drs;                   // empty: the mode's
    Ref<tyres::Class> tyre_class;              // the box's class; a track entry (tracks::PackageTrack) overrides it
    Ref<RulesProfile> rules_profile;           // empty: the mode's
    bool refuelling = false;
    std::optional<float> race_fuel_kg;         // the race fuel load; nullopt: the track's
};
SWOFTY_RECORD(Rules, mode, points, drs, tyre_class, rules_profile, refuelling, race_fuel_kg)

enum class PenaltyKind { warning, time, drive_through, stop_go, grid, disqualification };

namespace events {

struct Load {                                  // the session about to load (modify); payload on the Events page
    static constexpr const char *point = "session.load";
};

struct EntrantsSelect {                        // Sess_Select_Entrants
    static constexpr const char *point = "session.entrants.select";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    std::span<const Ref<drivers::Identity>> candidates;   // the pool
    uint32_t limit;                            // max_entrants
    std::vector<Ref<drivers::Identity>> *chosen;          // before handlers may fill it
};

struct GridBuild {                             // Game_GridSetup_SetupStartingGrid
    static constexpr const char *point = "session.grid.build";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    const GridRule *rule;                      // the race group's grid rule
    std::array<uint8_t, 32> *vehicle_id;       // the starting order; before handlers may write it
    uint8_t count;
};

struct GroupResult {                           // Sess_Build_GroupResult
    static constexpr const char *point = "session.grid.group_result";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    uint8_t group;                             // the session group whose result is built
    void *result;                              // GroupResult rows: car, best time, position
};

struct PenaltyIssue {                          // Sess_Issue_TimePenalty, _GridPlacePenalty, Sess_Apply_DecidedPenalties
    static constexpr const char *point = "session.penalty.issue";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    uint32_t car;
    PenaltyKind kind;                          // drive_through, stop_go, time, grid, ...
    float amount;                              // seconds or places; before handlers may change it; replace = veto
};

struct PitStop {                               // a car stops in its pit box and leaves it (notify)
    static constexpr const char *point = "session.pit_stop";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    uint32_t car;                              // vehicle index in the session
    float stationary_s;                        // time in the box
    uint8_t tyres_changed;                     // 0 or 1: the car left on a new set
};

// payloads on the Events page
struct WeekendStart    { static constexpr const char *point = "weekend.start"; };
struct ResultsFinalise { static constexpr const char *point = "session.results.finalise"; };
struct StateEnter      { static constexpr const char *point = "session.state.enter"; };
struct SafetyCarDeploy { static constexpr const char *point = "session.safety_car.deploy"; };

} // namespace events

Result set_grid(std::span<const uint8_t> order);                   // during session.grid.build
Result issue_penalty(uint32_t car, PenaltyKind kind, float amount);

} // namespace swofty::sessions
```

</template>
</Example>
</template>
</Split>

## F1 1990's weekend

<Split>

A 1990 weekend has two practice sessions, Friday qualifying, Saturday qualifying and the race. The grid comes from each car's better time over the two qualifying sessions. Points go 9-6-4-3-2-1, with no point for the fastest lap, and only a driver's best 11 results count.

F1 1990 has a setup mode of its own: its weekend has a different shape at boot. The mode is a variant of the 2026 mode and carries all of its 280 keys.

The grid follows the addon's `cars` option, 22 or 26. Grid size is one boot setting per install, and every box and mode is clamped to it, so a 26-car 1990 field needs an install set to 26 cars or more.

<template #example>
<Example root="swofty.f1-1990" code-file="native/src/rules.cpp" data-file="registry/rules.json" files="swofty.json, native/CMakeLists.txt">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/events.hpp>
#include <swofty/systems/sessions.hpp>
#include <swofty/systems/career.hpp>

using namespace swofty;
using sessions::Session;
using sessions::GroupType;

SWOFTY_REGISTER
{
    auto points = sessions::points.add("f1_1990_race", {
        .points = { 9, 6, 4, 3, 2, 1 },
        .fastest_lap = std::nullopt,
        .reduced = { { { 4, 3, 2, 1, 1 } }, { { 4, 3, 2, 1, 1 } }, { { 4, 3, 2, 1, 1 } } },   // half points, rounded
    });

    auto weekend = sessions::formats.add("two_qualifying", { .groups = {
        { .type = GroupType::practice,   .sessions = { Session::practice_1, Session::practice_2 } },
        { .type = GroupType::qualifying, .sessions = { Session::sprint_shootout_one_shot }, .label = "lng_f1990_qualifying_1" },
        { .type = GroupType::qualifying, .sessions = { Session::qualifying_one_shot },      .label = "lng_f1990_qualifying_2" },
        { .type = GroupType::race,       .sessions = { Session::race },
          .grid = sessions::best_of({ Session::sprint_shootout_one_shot, Session::qualifying_one_shot }),
          .standings = points },
    } });

    auto grid = sessions::grids.variant("f1_1990", sessions::grids["game/f1_26"], [](sessions::Grid &g) {
        g.cars = option<int>("cars");                      // 22 or 26, clamped to the install; applies from the next game start
        g.teams = g.cars / 2;
    });

    sessions::modes.variant("f1_1990", sessions::modes["game/f1_26"], [=](sessions::Mode &m) {
        m.format = weekend;
        m.grid = grid;
    });
}

SWOFTY_ON_LOAD
{
    // best 11 results: the core rebuilds the standings, then drops the rest
    events::on<career::events::StandingsRebuild>().in(events::content("swofty.f1-1990/f1_career_1990"))
        .after([](const career::events::StandingsRebuild &s, const events::Context &) {
            career::apply_dropped_scores(s, 11);           // keeps the gross totals for the UI
        });
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
{
  "session.points": {
    "f1_1990_race": { "points": [9, 6, 4, 3, 2, 1], "fastest_lap": null,
                      "reduced": [[4, 3, 2, 1, 1], [4, 3, 2, 1, 1], [4, 3, 2, 1, 1]] }
  },
  "session.weekend_format": {
    "two_qualifying": { "groups": [
      { "type": "practice",   "sessions": ["practice_1", "practice_2"] },
      { "type": "qualifying", "sessions": ["sprint_shootout_one_shot"], "label": "lng_f1990_qualifying_1" },
      { "type": "qualifying", "sessions": ["qualifying_one_shot"],      "label": "lng_f1990_qualifying_2" },
      { "type": "race", "sessions": ["race"], "grid": { "best_of": ["sprint_shootout_one_shot", "qualifying_one_shot"] },
        "standings": "swofty.f1-1990/f1_1990_race" }
    ] }
  },
  "session.grid": {
    "f1_1990": { "extends": "game/f1_26", "cars": 26, "teams": 13 }
  },
  "session.mode": {
    "f1_1990": { "extends": "game/f1_26", "format": "swofty.f1-1990/two_qualifying", "grid": "swofty.f1-1990/f1_1990" }
  }
}
```

</template>
</Example>
</template>
</Split>

```text
[registry] register: swofty.f1-1990 (f1_1990_rules.dll) 1 session.points, 1 session.weekend_format, 1 session.grid,
           1 session.mode (early stage, 0.5 ms)
[registry] stat ids: session.mode swofty.f1-1990/f1_1990 -> 16 (pool 5, 7, 16-31)
[registry] applied at boot: session.mode 1 (280 keys from game/f1_26), session.weekend_format 1, session.points 1 (padded to 26),
           session.grid 1 (26 entrants, option cars) (412 database records)
[events] session.grid.build in setup mode swofty.f1-1990/f1_1990: grid rule best_of(sprint_shootout_one_shot, qualifying_one_shot) by the core
[events] career.standings.rebuild in swofty.f1-1990/f1_career_1990: 1 after handler (swofty.f1-1990)
```

With the install set to 22 cars, the core clamps the 1990 grid and says so:

```text
[registry] session.grid swofty.f1-1990/f1_1990: 26 cars clamped to 22, the install's grid size
```

The data version has no dropped scores: those need the handler. A data file can't follow an option either. It states one grid size, or ships one file per value with a guard.

## Rules per box

A box sets its rules in `content::Package::rules`. They apply when a session loads in that box, and every other box in `swofty_f1` keeps its own.

```cpp
// a 2009 pack: 10-8-6-5-4-3-2-1, no fastest-lap point, no DRS
SWOFTY_REGISTER
{
    auto points = sessions::points.add("f1_2009", { .points = { 10, 8, 6, 5, 4, 3, 2, 1 }, .fastest_lap = std::nullopt });
    auto no_drs = power::drs_rules.add("none", { .race_start = 200, .red_flag_restart = 200, .safety_car_end = 200 });

    content::packages.layer(content::packages["community.f1-2009/f1_2009"], [=](content::Package &p) {
        p.rules.points = points;
        p.rules.drs = no_drs;
        p.rules.refuelling = true;
    });
}
```

```text
[events] session.load (grand_prix, community.f1-2009/f1_2009, race, game/silverstone): box rules applied by the core
         (points community.f1-2009/f1_2009, drs community.f1-2009/none, refuelling on)
```

A career freezes each season's championship rules when that season starts. A fix to a year's points reaches a running career at its next season.

## Grid rules

`best_of` is a grid rule the core adds to `session.grid.build`. No addon has to write its own. For a rule the core doesn't have, replace the group result yourself:

```cpp
events::on<sessions::events::GroupResult>().in(events::setup_mode("swofty.f1-1990/f1_1990"))
    .replace([](sessions::events::GroupResult &g, const events::Context &) {
        return f1990::rank_reverse_top(g, 8);              // true: handled; the game's result isn't built
    });
```

A group result is built only when the group's last session id is 7, 8, 9, 12, 13 or 14 (mask 0x7380 in 0x140af83a0). A Q1/Q2 knockout can't be a format: it needs a core hook there.

## Lengths, rules and the safety car

```cpp
sessions::lengths.layer(sessions::lengths["game/race_50"], [](sessions::Length &l) { l.flashback_limit = 3; });

auto strict = sessions::rules_profiles.variant("strict", sessions::rules_profiles["game/f1_26"], [](sessions::RulesProfile &r) {
    r.corner_cutting = 3;
    r.drive_through_replacement = false;
});

sessions::safety_car.layer(sessions::safety_car["game/default"], [](sessions::SafetyCar &s) {
    s.max_deployments[7] = 4;                              // the longest race length: 4 safety cars, not 8
});
```

## Who drives a car

Each car has a set of controllers: a human one, an AI one, and flags such as the pit-lane flag. The game switches a car between them when the player retires, when a session is simulated and in the pit lane. After a flashback or a Return to Garage it rebuilds the set, so a pointer kept from before then points at freed memory. The core reads the set fresh every time and applies changes only between rebuilds.

```cpp
namespace swofty::sessions {

enum class Control : int32_t { human, ai };

struct ControlState {                          // read fresh each time; never keep it across frames
    Control mode;
    uint32_t flags;                            // the game's controller flags, bit n = flag n (PIT_LANE is bit 0)
    bool pit_lane() const;
};

enum class Result { ok, late, unavailable, invalid, blocked };

Result set_control(live::Car car, Control mode);   // applied at the next safe moment, never during a rebuild

namespace events {

struct PlayerControl {                             // Game_Set_VehicleController: before, replace and after
    static constexpr const char *point = "session.player.control";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    cars::live::Car car;
    Control from, to;
    int32_t reason;                                // the controller flag the switch sets or clears (0 to 17; PIT_LANE is 0)
};

} // namespace events
} // namespace swofty::sessions
```

```cpp
// hand the player's car to the AI, then back
sessions::set_control(player, sessions::Control::ai);
sessions::set_control(player, sessions::Control::human);

// keep the player in control in the pit lane
events::on<sessions::events::PlayerControl>().replace([](sessions::events::PlayerControl &c, const events::Context &) {
    auto state = c.car.control();
    return c.to == sessions::Control::ai && state && state->pit_lane();   // true: the switch doesn't happen
});

// drop anything kept about a car's controllers when the game rebuilds them
events::on<cars::events::ControllersRebuilt>().after([](const cars::events::ControllersRebuilt &r, const events::Context &) {
    my_addon::forget(r.car);
});
```

- **`car.control()`** returns the mode and flags, or nothing while the car's controllers are being rebuilt.
- **`sessions::set_control`** works on any car and goes through the game's own switch, so `session.player.control` fires for it. It sets a controller flag of its own (DEV, 6; the config key `session_control_flag` picks another) to hold a car on the AI and clears it to give the car back. Handing a car back to its human fails with `Result::blocked` while any other flag is set (the pit-lane flag is the usual one) and the game is not called. A call made while the game rebuilds its controller sets, or from inside a `session.player.control` handler, is queued and applies on the next frame. `Result::late` means the controllers have not been built yet, or the call came from another thread. Handing a car to the AI resets the car's vehicle systems, which the player sees as a hitch.
- **`reason`** is the flag index the game passes as the fourth argument of `Game_Set_VehicleController`: the switch sets bit `reason` when `to` is `ai` and clears it when `to` is `human`. A `before` handler may change `to` and `reason`.
- **`car.controllers.rebuilt`** fires after a flashback or a Return to Garage has rebuilt a car's controllers (see [Cars](./cars)).

## Events and operations

```cpp
// a 20-second time penalty instead of every drive-through
events::on<sessions::events::PenaltyIssue>().when(events::session_type({ events::SessionType::race }))
    .before([](sessions::events::PenaltyIssue &p, const events::Context &) {
        if (p.kind == sessions::PenaltyKind::drive_through) {
            p.kind = sessions::PenaltyKind::time;
            p.amount = 20.0f;
        }
    });

// a reverse grid for a sprint, from inside the grid build
events::on<sessions::events::GridBuild>().in(events::content("mod.reverse/sprint")).before(
    [](sessions::events::GridBuild &g, const events::Context &) {
        std::reverse(g.vehicle_id->begin(), g.vehicle_id->begin() + std::min<int>(g.count, 10));
    });

sessions::issue_penalty(car, sessions::PenaltyKind::grid, 3.0f);
```

## Readiness and limits

- **Ready:** the entrant, grid, result and safety car handlers are L2, used by Custom Grid Size and F1 1990.
- **Partial:** points and penalties. The data is ready, and the runtime code is L1. A box's points apply through the standings lookup, which still needs reverse engineering, and a box's DRS through `session.drs.allowed` (see [Power units](./power-units)). Until then those two come from the setup mode.
- **Unknown:** flags and red flags, the knockout cut writer, red-flag restarts, penalty decision thresholds at run time, the difficulty preset selector.
- **Who drives a car: implemented, waiting on verification.** `Game_Set_VehicleController` (the fourth argument is a flag index, not a free reason code) and the controller set (human controller at +0x20, AI controller at +0x28, flags at +0x30, mode = dword[+0x10] ^ dword[+0x18]) are L2. The core binds them after the independent verify, and until then `set_control` and `car.control()` report unavailable. Whether a flashback or Return to Garage rebuilds the sets is test T144; `car.controllers.rebuilt` fires after every build of the sets (`Sess_ControllerSystem_BuildSets`, L2), and needs the T144 in-game run.
- **Grid:** 32 vehicle slots, a 27-car clamp at session enter, an AI table of 30, a wheel map that rehashes at the 26th car, replay holders sized for 22 to 24 cars. `sessions::grids` refuses a size the game can't hold, naming the limit.
- **Grid size is one boot setting per install.** The track worlds that hold the grid are picked once at boot for every box, and each box and mode is clamped to that size.
- **Points** are whole numbers in arrays of 23, padded to the grid size. 1990's half points are rounded.
- **Setup modes** take stat ids of 31 or lower. 18 are free (5, 7, 16 to 31), and F1 1990 uses 16. A mode needs the content set and an available mode row.
- **Option restrictions** are tags shared across modes (`gamesetup_f1`).
