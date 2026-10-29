---
title: Tyres
description: Tyre physics, visual compounds, compound classes, allocations, wear and rules in code, with the class, allocation and set-choice events.
---

# Tyres

With the tyre system you add compounds, give them their own colours and badges, choose which compounds each event brings and how many sets each session gets, and set the rules around them. F1 1990, for example, races on Goodyear letters A to E plus W for the wet, and Spa brings a harder set than the other rounds.

A **compound class** is the set of five tyre options a session offers. An **allocation** is how many sets of each option a car gets per session. A **box** is a Grand Prix or Time Trial season tile, or one season of a career.

::: info In 1.6.0
The tyre system has no header in 1.6.0 (`swofty/systems/tyres.hpp` ships later). This page describes the design. An addon with its own tyre system declares its own registries ([Your own systems](./custom)).
:::

## The header

<Split>

`swofty/systems/tyres.hpp` has seven registries:

- `tyres::physical`: one compound in a car package's `.vcmpd` (`tyre.physical`);
- `tyres::visual`: one visual compound, with its colour and icons (`tyre.visual`);
- `tyres::classes`: a compound class, five positional options (`tyre.class`);
- `tyres::allocations`: sets per session (`tyre.allocation`);
- `tyres::per_event`: the class and presets of one event (`tyre.event`);
- `tyres::wear_scales`: wear per track per option (`tyre.wear_scale`);
- `tyres::rules`: blankets, mandatory stops, set hand-back (`tyre.rules`).

The events are hook points on the game's tyre code. Each payload names the game function it wraps.

<template #example>
<Example root="swofty-sdk" code-file="include/swofty/systems/tyres.hpp">
<template #code>

```cpp
#pragma once
#include <swofty/registry.hpp>
#include <swofty/events.hpp>
#include <swofty/systems/cars.hpp>
#include <swofty/systems/sessions.hpp>
#include <swofty/systems/tracks.hpp>

namespace swofty::tyres {

enum class Code { hso, uso, sup, sof, med, har, sha, inter, wet };   // the game's 9 physics codes (C6..C0, inter, wet)

struct Physical {
    Ref<cars::Package> package;                // the car package whose tyrecompounds.erp holds it
    Code code;                                 // the physics code it fills in that package's .vcmpd
    Path vcmpd;                                // plain XML: TyreModel, Wear, Temperature, Pressure, Wetness, Residuum
    float grip = 1.0f;                         // scale on the TyreModel rows
    float wear = 1.0f;                         // scale on the Wear block
    bool slick = true;                         // is_slick
    bool full_wet = false;                     // is_full_wet
    StatId stat_id = StatId::automatic;        // a tyres.physical_compounds row
};
inline Registry<Physical> physical{"tyre.physical", { .applies = Applies::boot }};

struct Visual {
    Text name;                                 // "lng_f1990_tyre_c"
    Colour colour;                             // the sidewall and UI colour
    std::string material_context;              // the sidewall material in the car package's teams/common.erp
    std::string audio_id;                      // "Soft", "Wet", ...
    Path icon;                                 // art: the core makes the icon-font glyph and the texture-array slices
    Path icon_worn;                            // the _worn slice in osd.car_settings_icons
    StatId stat_id = StatId::automatic;        // a tyres.visual_compounds row
};
inline Registry<Visual> visual{"tyre.visual", { .applies = Applies::boot }};

struct Option {
    std::string name;                          // "Softer", "Medium", ... (F1 layout) or "SuperSoft", "Soft", ... (F2 layout)
    std::vector<Ref<Physical>> physical;       // the compounds the slot can run
    Ref<Visual> visual;                        // the badge: it follows the slot, not the compound
};

struct Class {
    std::vector<Ref<sessions::Mode>> modes;    // the setup modes that may run it (the class's mode mask)
    std::array<Option, 5> options;             // always 5, positional
    bool allocation_screen = true;             // show_allocation_screen
    float track_wear_scale = 1.0f;             // track_wear_scales
    StatId stat_id = StatId::automatic;        // a tyres.compound_classes row
};
inline Registry<Class> classes{"tyre.class", { .applies = Applies::boot }};

struct Allocation {
    std::map<sessions::Session, std::array<int, 5>> sets;   // sets per session, one number per option, in option order
    StatId stat_id = StatId::automatic;        // a tyres.allocation_tyre_presets row
};
inline Registry<Allocation> allocations{"tyre.allocation", { .applies = Applies::boot }};

struct Event {
    Ref<tracks::Track> track;                  // the event's track
    Ref<Class> tyre_class;                     // the class both selectors return for this event
    Ref<Allocation> allocation;                // the sets per session
    Ref<Allocation> mandatory;                 // mandatory_tyre_presets; empty: none
    int required_pitstops = 0;                 // MandatoryPitstopsOverride
};
inline Registry<Event> per_event{"tyre.event", { .applies = Applies::session }};

struct WearScale {
    Ref<tracks::Track> track;                  // TrackTyreWearScaleSet: one per track
    std::array<float, 5> scale;                // Softer, Medium, Harder, Intermediate, Wet
};
inline Registry<WearScale> wear_scales{"tyre.wear_scale", { .applies = Applies::boot }};

struct Rules {
    events::Selector within;                   // where the rules apply: a setup mode, a data group, a car package
    bool tyre_warmers = true;                  // has_tyre_warmers
    bool mandatory_stop_check = true;          // tyre_regulations.allow_strategy_mandatory_stop_check
    bool locked_to_qualifying_tyre = false;    // start the race on the qualifying tyre
    std::vector<sessions::Session> carry_over_unused;   // these sessions hand their unused sets on
    bool refill_when_empty = false;            // a car with no fresh set of a compound borrows unused ones
};
inline Registry<Rules> rules{"tyre.rules", { .applies = Applies::read, .snapshot = true }};

namespace events {

struct ClassOf {                               // Sess_TyreClass_FromName and setPresetChoice
    static constexpr const char *point = "session.tyres.class_of";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    uint32_t car;                              // vehicle index in the session; UINT32_MAX for the allocation side
    bool sprint;                               // the weekend-format flag: the game asks for "<name>_sprint"
    Ref<Class> chosen;                         // the core fills it from the active box, then tyre.event; before handlers may change it
};

struct Allocate {                              // setPresetChoice, Game_WeekendAllocation_SetupRaceWeekendTyreSet
    static constexpr const char *point = "session.tyres.allocate";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    uint32_t car;                              // vehicle index in the session
    Ref<Allocation> preset;                    // the preset the game picked
    std::array<int, 5> sets;                   // the sets this car gets this session; before handlers may change them
};

struct SessionStart {                          // Sess_TyreAllocation_OnSessionStart
    static constexpr const char *point = "session.tyres.session_start";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    sessions::Session session;                 // the session that starts
    bool hand_back;                            // the game takes back the dry sets of the last session
};

struct ChooseSet {                             // Sess_Tyres_ClosestAvailableCompound: pit stop, garage, AI run plan
    static constexpr const char *point = "session.tyres.choose_set";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    uint32_t car;                              // vehicle index in the session
    uint8_t wanted_slot;                       // class option 0..4
    uint8_t session_id;                        // the session id enum, 1..18
    uint16_t candidates;                       // fresh sets of the wanted slot left
    int32_t chosen;                            // set index; -1 = the game's choice
};

struct AwsBudget {                             // the four Sess_TyreRules_* drains
    static constexpr const char *point = "session.tyres.aws_budget";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    uint32_t car;                              // vehicle index in the session
    float drain;                               // the amount the game takes; before handlers may change it
};

} // namespace events

Result set_class(uint32_t car, Ref<Class> c);                                // only before the car's tyre component is built
Result grant_sets(Ref<drivers::Identity> driver, uint8_t option, int sets);  // extra sets for this weekend

} // namespace swofty::tyres
```

</template>
</Example>
</template>
</Split>

## F1 1990's Goodyear tyres

<Split>

F1 1990 races on Goodyear A (hardest) to E (softest, qualifying only), plus W for the wet. Each letter is a compound in the car pack's `.vcmpd` and a visual with its own badge.

The classes use the F2 layout: one compound and one badge per slot, so the letter on screen is always right. Spa brings A; the other rounds bring B to E.

Everything is scoped to the 1990 setup mode, so Grand Prix and every other mode keep the stock tyres. F1 1990 has a setup mode of its own for its two qualifying sessions (see [Sessions and weekends](./sessions)).

<template #example>
<Example root="swofty.f1-1990" code-file="native/src/tyres.cpp" data-file="registry/tyres.json" files="swofty.json, native/CMakeLists.txt, native/src/rounds.hpp, tyres/goodyear.vcmpd, art/tyres/a.svg, art/tyres/a_worn.svg, art/tyres/w.svg, art/tyres/w_worn.svg">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/events.hpp>
#include <swofty/systems/tyres.hpp>
#include <swofty/systems/sessions.hpp>
#include <swofty/systems/tracks.hpp>
#include "rounds.hpp"

using namespace swofty;
using tyres::Code;
using sessions::Session;

struct Goodyear { const char *key; Code code; const char *colour; };

static constexpr Goodyear kGoodyear[] = {
    { "a", Code::har, "#FFFFFF" },             // hardest
    { "b", Code::med, "#F6C21C" },
    { "c", Code::sof, "#E2231A" },
    { "d", Code::sup, "#9B2FAE" },
    { "e", Code::uso, "#2F74E8" },             // softest, qualifying only
    { "w", Code::wet, "#22A45D" },
};

static const events::Selector k1990 = events::setup_mode("swofty.f1-1990/f1_1990");

SWOFTY_REGISTER
{
    auto mode = sessions::modes["swofty.f1-1990/f1_1990"];
    auto package = cars::packages["swofty.f1-1990/f1_1990"];

    std::map<std::string, Ref<tyres::Physical>> phys;
    std::map<std::string, Ref<tyres::Visual>> vis;
    for (const Goodyear &g : kGoodyear) {
        std::string key = std::string("goodyear_") + g.key;
        phys[g.key] = tyres::physical.add(key, {
            .package = package,
            .code = g.code,
            .vcmpd = file("tyres/goodyear.vcmpd"),
            .slick = g.code != Code::wet,
            .full_wet = g.code == Code::wet,
        });
        vis[g.key] = tyres::visual.add(key, {
            .name = std::string("lng_f1990_tyre_") + g.key,
            .colour = Colour(g.colour),
            .material_context = g.code == Code::wet ? "Wet" : "Soft",
            .audio_id = g.code == Code::wet ? "Wet" : "Soft",
            .icon = file(std::string("art/tyres/") + g.key + ".svg"),
            .icon_worn = file(std::string("art/tyres/") + g.key + "_worn.svg"),
        });
    }

    auto slot = [&](const char *name, const char *k) { return tyres::Option{ name, { phys[k] }, vis[k] }; };

    auto bcde = tyres::classes.add("gy_bcde", {
        .modes = { mode },
        .options = { slot("SuperSoft", "e"), slot("Soft", "d"), slot("Medium", "c"), slot("Hard", "b"), slot("Wet", "w") },
    });
    auto abce = tyres::classes.add("gy_abce", {
        .modes = { mode },
        .options = { slot("SuperSoft", "e"), slot("Soft", "c"), slot("Medium", "b"), slot("Hard", "a"), slot("Wet", "w") },
    });

    auto sets = tyres::allocations.add("season", { .sets = {
        { Session::sprint_shootout_s, { 3, 0, 0, 0, 0 } },   // Friday qualifying: 3 sets of E
        { Session::qualifying_s,      { 3, 0, 0, 0, 0 } },   // Saturday qualifying
        { Session::race,              { 0, 3, 2, 2, 3 } },   // 7 race sets and 3 wets
    } });

    for (const f1990::Round &r : f1990::kRounds)              // 16 rounds; Phoenix runs on game/las_vegas, ...
        tyres::per_event.add(std::format("f1_1990/r{:02}", r.number), {
            .track = tracks::tracks[r.track],
            .tyre_class = r.number == 11 ? abce : bcde,      // Spa brings A
            .allocation = sets,
        });

    tyres::rules.add("f1_1990", {
        .within = k1990,
        .tyre_warmers = false,
        .mandatory_stop_check = false,
        .carry_over_unused = { Session::sprint_shootout_s },  // Friday qualifying hands its sets on
        .refill_when_empty = true,
    });
}

SWOFTY_ON_LOAD
{
    // E is a qualifying tyre: no car takes it in a race
    events::on<tyres::events::ChooseSet>().in(k1990).when(events::session_type({ events::SessionType::race }))
        .before([](tyres::events::ChooseSet &c, const events::Context &) {
            if (c.wanted_slot == 0)
                c.wanted_slot = 1;
        });

    events::on<sessions::events::Load>().in(k1990).after([](const sessions::events::Load &, const events::Context &ctx) {
        auto ev = ctx.record<tyres::Event>();                  // this session's tyre.event
        swofty::log("[f1-1990] %s: class %s", ev->track.id().data(), ev->tyre_class.id().data());
    });
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
{
  "tyre.physical": {
    "goodyear_a": { "package": "swofty.f1-1990/f1_1990", "code": "har", "vcmpd": "tyres/goodyear.vcmpd" },
    "goodyear_b": { "package": "swofty.f1-1990/f1_1990", "code": "med", "vcmpd": "tyres/goodyear.vcmpd" },
    "goodyear_w": { "package": "swofty.f1-1990/f1_1990", "code": "wet", "vcmpd": "tyres/goodyear.vcmpd",
                    "slick": false, "full_wet": true }
  },
  "tyre.visual": {
    "goodyear_a": { "name": "lng_f1990_tyre_a", "colour": "#FFFFFF", "material_context": "Soft", "audio_id": "Soft",
                    "icon": "art/tyres/a.svg", "icon_worn": "art/tyres/a_worn.svg" }
  },
  "tyre.class": {
    "gy_bcde": {
      "modes": ["swofty.f1-1990/f1_1990"],
      "options": [
        { "name": "SuperSoft", "physical": ["swofty.f1-1990/goodyear_e"], "visual": "swofty.f1-1990/goodyear_e" },
        { "name": "Soft",      "physical": ["swofty.f1-1990/goodyear_d"], "visual": "swofty.f1-1990/goodyear_d" },
        { "name": "Medium",    "physical": ["swofty.f1-1990/goodyear_c"], "visual": "swofty.f1-1990/goodyear_c" },
        { "name": "Hard",      "physical": ["swofty.f1-1990/goodyear_b"], "visual": "swofty.f1-1990/goodyear_b" },
        { "name": "Wet",       "physical": ["swofty.f1-1990/goodyear_w"], "visual": "swofty.f1-1990/goodyear_w" }
      ]
    }
  },
  "tyre.allocation": {
    "season": { "sets": { "sprint_shootout_s": [3,0,0,0,0], "qualifying_s": [3,0,0,0,0], "race": [0,3,2,2,3] } }
  },
  "tyre.event": {
    "f1_1990/r01": { "track": "game/las_vegas", "tyre_class": "swofty.f1-1990/gy_bcde", "allocation": "swofty.f1-1990/season" },
    "f1_1990/r11": { "track": "game/spa", "tyre_class": "swofty.f1-1990/gy_abce", "allocation": "swofty.f1-1990/season" }
  },
  "tyre.rules": {
    "f1_1990": { "within": { "setup_mode": "swofty.f1-1990/f1_1990" }, "tyre_warmers": false, "mandatory_stop_check": false,
                 "carry_over_unused": ["sprint_shootout_s"], "refill_when_empty": true }
  }
}
```

</template>
</Example>
</template>
</Split>

```text
[registry] register: swofty.f1-1990 (f1_1990_tyres.dll) 6 tyre.physical, 6 tyre.visual, 2 tyre.class, 1 tyre.allocation,
           16 tyre.event, 1 tyre.rules (early stage, 1.1 ms)
[registry] stat ids: tyre.visual swofty.f1-1990/goodyear_{a,b,c,d,e,w} -> 28, 29, 30, 31, 0, 1 (0 and 1: stock rows no class reads)
[registry] applied at boot: tyre.physical 6 (1 file in an overlay archive), tyre.visual 6, tyre.class 2, tyre.allocation 1
           (94 database records, 6 glyphs, 30 texture slices)
[registry] content swofty.f1-1990/f1_career_1990 active: 3 scoped changes applied
[f1-1990] game/monaco: class swofty.f1-1990/gy_bcde
[registry] content swofty.f1-1990/f1_career_1990 ended: 3 scoped changes put back
```

The `.vcmpd` goes into an overlay archive that the core adds to the game's asset manifest at boot. The data version has no `ChooseSet` handler: a handler needs code.

## A box's tyres

Every Grand Prix and Time Trial box shares one setup mode, `swofty_f1`. A box picks its tyre class in its rules, and the class applies when a session loads in that box. A box can also give one track its own class.

```cpp
// Classic Seasons 2016: the 2016 class for the whole box, and a harder one at Monaco
content::packages.layer(content::packages["swofty.classic-seasons-2016/f1_2016"], [](content::Package &p) {
    p.rules.tyre_class = tyres::classes["swofty.classic-seasons-2016/f1_2016"];
});
tracks::per_package.layer(tracks::per_package["swofty.classic-seasons-2016/f1_2016/monaco"], [](tracks::PackageTrack &t) {
    t.tyre_class = tyres::classes["swofty.classic-seasons-2016/f1_2016_monaco"];
});
```

At session load the core answers `session.tyres.class_of` from the active box, in this order:

1. the box's entry for the track (`tracks::PackageTrack::tyre_class`);
2. the box's rules (`sessions::Rules::tyre_class`);
3. the `tyre.event` record for the track;
4. the setup mode's own class.

Before handlers then see the answer and may change it.

## Classes and the badge

A class always has five options, and the game reads them by position. The F1 layout lists the same compound pool in every dry slot, and the track picks the compound per event. Its badge shows the slot (Soft, Medium, Hard), not the compound. The F2 layout gives each slot one compound and one badge.

```cpp
// F1 layout: the stock 2026 class, with one more compound in the pool
tyres::classes.layer(tyres::classes["game/f1_26"], [](tyres::Class &c) {
    for (int i = 0; i < 3; ++i)                                     // Softer, Medium, Harder
        c.options[i].physical.push_back(tyres::physical["mod.c6/c6"]);
});
```

## Physics and wear

`tyres::physical` is per car package. A layer on another addon's compound reaches every car of that package and no other:

```cpp
// C wears 15% faster on every 1990 car; the 2026 cars use the stock .vcmpd and never see it
tyres::physical.layer(tyres::physical["swofty.f1-1990/goodyear_c"], [](tyres::Physical &p) { p.wear *= 1.15f; });

// Monaco is easier on the two softest options
tyres::wear_scales.layer(tyres::wear_scales["game/monaco"], [](tyres::WearScale &w) {
    w.scale[0] = 0.85f;
    w.scale[1] = 0.9f;
});
```

## Allocation and rules

```cpp
// an addon option: more race sets of the second option
tyres::allocations.layer(tyres::allocations["swofty.f1-1990/season"], [](tyres::Allocation &a) {
    a.sets[sessions::Session::race][1] += option<int>("extra_sets");
});

// blankets back on in every 1990 session
tyres::rules.layer(tyres::rules["swofty.f1-1990/f1_1990"], [](tyres::Rules &r) { r.tyre_warmers = true; });
```

`carry_over_unused` and `refill_when_empty` have no data switch in the game. The core does them with its own handlers on `session.tyres.session_start` and `session.tyres.choose_set`.

A career freezes each season's tyre rules (`tyre.rules`) when that season starts. A fix to a year's rules reaches a running career at its next season.

## Events

```cpp
// which class a car gets: the core answers from tyre.event, then your handler sees it
events::on<tyres::events::ClassOf>().in(events::content("swofty.f1-1990/f1_career_1990"))
    .after([](const tyres::events::ClassOf &c, const events::Context &) {
        swofty::log("[strategy] car %u runs %s", c.car, c.chosen.id().data());
    });

// one more set of the second option for the player in practice
events::on<tyres::events::Allocate>().when(events::session_type({ events::SessionType::practice }))
    .before([](tyres::events::Allocate &a, const events::Context &ctx) {
        if (ctx.player() && ctx.player()->car == a.car)
            a.sets[1] += 1;
    });
```

## Operations

```cpp
tyres::set_class(car, tyres::classes["swofty.f1-1990/gy_abce"]);             // at session.load, before the tyre component
tyres::grant_sets(drivers::identities["game/lando_norris"], /*option*/ 1, 2);
```

`set_class` after the tyre component is built returns `late`. The component builds its models from the class it had.

## Readiness and limits

- **Ready to partial:** the class and allocation entry points are L2 and used by F1 1990. Physics is L1. Unknown: the wear and temperature writers (the live per-wheel values need a trace), blanket temperature, the AI race strategy (the AI picks by slot and never sees letters), the readers of `tyre_regulations.*`, and the UDP tyre-set packet's slot count (about 20).
- **The class applies at session load.** The game picks it when the car's tyre component starts (F1 1990's class swap runs there).
- **Allocations apply at weekend start.** The game fills them once per weekend (`setPresetChoice`) and keeps the first value. The core never applies allocation records between the sessions of one weekend, so a class and its allocation always match.
- **Five options per class, by position.** With fewer, the allocation code dereferences a null option and crashes. The five are 4 dry and a wet (F2 layout), or 3 dry, an inter and a wet (F1 layout).
- **The class is picked twice:** by the car (`Sess_TyreClass_FromName`) and by the allocation (`setPresetChoice`). `session.tyres.class_of` answers both.
- **Models are built when the tyre component starts.** A class swap after that leaves models keyed by the old visuals.
- **Icons are glyphs in a font page** (`fonts_standard_icons.erp`) plus slices in four texture arrays. The core makes both from `icon`.
- **Physics live per package.** A class is useless without the matching compounds in that package's `.vcmpd`. All loaded `.vcmpd` files share one name map, where the first name wins. There are 9 physics codes; whether a 10th works is unknown.
- **Visual ids are tight.** 28 stock rows (0 to 27). The masks are 4 bytes, so ids above 31 likely don't work (inference). The allocator hands out free ids, then the stock rows 0 to 6, which no class reads.
- **The two-compound rule** is an infringement with no data switch.
- **AI packs** carry `tyre_wear` per baked car.
- **Sets have no fixed cap:** they are dynamic vectors.
