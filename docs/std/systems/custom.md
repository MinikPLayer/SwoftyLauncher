---
title: Your own systems
description: How an addon defines a system of its own in code (its record structs and registries, its scope, its projection onto core records, scoped hooks, a service and its own events) so other addons can build on it, with a six-compound tyre system as the worked example.
---

# Your own systems

An addon can bring a whole system the game doesn't have: a six-compound tyre range, a points system with dropped scores, five R&D departments, a two-qualifying weekend. It builds the system in code out of the same pieces the core uses, and other addons extend it the way they extend anything else.

A system has six parts:

1. **Registries.** It uses the core's registries where the game already has the concept, and declares its own (a `Registry<T>` named `<addon>.<name>`) for the rest.
2. **A scope.** Where the system applies, as an `events::Selector`: a data group, a setup mode, a content or a car package.
3. **A projection.** Code that turns your records into core records when the core resolves.
4. **Hooks.** Handlers for what records can't do, filtered to your scope.
5. **A service.** A table of functions other addons call.
6. **Events.** Moments of your system that other addons subscribe to.

```text
f1990::compounds  (swofty.f1-1990.tyre_compound) ──projects──▶ tyres::visual, tyres::classes, tyres::per_event,
f1990::tyre_rules (swofty.f1-1990.tyre_rules)                    tyres::allocations, tyres::rules ──core──▶ game rows, icons
        ▲ records and layers from other addons (a seventh compound, wear)        ▲ service swofty.f1-1990.tyres, events
```

## A six-compound tyre system

F1 1990 races on Goodyear A (hardest) to E (softest, qualifying only), plus W for the wet, and each round brings its own choice. The game has nothing like this, so F1 1990 declares two registries of its own in a header it ships for other addons.

<Split>

The header holds the record structs, the registry handles, the scope, the events and the service table. Other addons include it to add compounds, layer the rules or call the service.

`rounds` is keyed by round, so another addon's layer can change one round.

<template #example>
<Example root="swofty.f1-1990" code-file="api/swofty_f1_1990_api/tyres.hpp" files="swofty.json, native/src/tyres.cpp, native/src/projection.cpp, native/CMakeLists.txt, tyres/goodyear.vcmpd">
<template #code>

```cpp
#pragma once
#include <swofty/registry.hpp>
#include <swofty/events.hpp>
#include <swofty/systems/tyres.hpp>
#include <swofty/systems/tracks.hpp>

namespace f1990 {

using namespace swofty;

inline const events::Selector scope = events::setup_mode("swofty.f1-1990/f1_1990");   // Grand Prix stays stock

struct TyreCompound {
    static constexpr Version version{1, 0};
    std::string letter;                        // "A" .. "E", "W"; at most 2 characters
    int softness = 0;                          // 1 = hardest; 0 for the wet
    Ref<tyres::Physical> physical;             // its compound in the car pack's .vcmpd
    Colour colour;
    bool qualifying_only = false;
    bool wet = false;
};
SWOFTY_RECORD(TyreCompound, letter, softness, physical, colour, qualifying_only, wet)
inline Registry<TyreCompound> compounds{"swofty.f1-1990.tyre_compound", { .applies = Applies::boot }};

struct RoundTyres {
    int round;                                 // the key
    Ref<tracks::Venue> venue;
    std::vector<Ref<TyreCompound>> race;       // empty: the season default
    std::vector<Ref<TyreCompound>> qualifying;
};
SWOFTY_RECORD(RoundTyres, round, venue, race, qualifying)

struct TyreRules {
    static constexpr Version version{1, 0};
    std::vector<Ref<TyreCompound>> race;       // the default race compounds
    std::vector<Ref<TyreCompound>> qualifying;
    Ref<TyreCompound> wet;
    struct { int race = 7, qualifying = 3, wet = 3; } sets;
    Keyed<RoundTyres, &RoundTyres::round> rounds;   // a layer changes one round: rounds.at(4)
};
SWOFTY_RECORD(TyreRules, race, qualifying, wet, sets, rounds)
inline Registry<TyreRules> tyre_rules{"swofty.f1-1990.tyre_rules", { .applies = Applies::boot }};

namespace events {
struct CompoundFitted {                        // a car leaves its pit box on a new set
    static constexpr const char *point = "swofty.f1-1990.compound_fitted";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    uint32_t car;                              // vehicle index in the session
    Ref<TyreCompound> compound;
};
}

struct Tyres {                                 // the service swofty.f1-1990.tyres
    static constexpr const char *name = "swofty.f1-1990.tyres";
    static constexpr uint32_t version = 1;
    uint32_t size;
    Ref<TyreCompound> (*current)(uint32_t car);                 // empty outside 1990
    std::vector<Ref<TyreCompound>> (*event_compounds)();        // softest first
    char (*letter_of_option)(uint8_t option);                   // 'D'
};

} // namespace f1990
```

</template>
</Example>
</template>
</Split>

<Split>

F1 1990 adds its six compounds and the season's rules, then hands the core a projection for the core registries it needs. The core calls the projection when it resolves, after every addon has registered, so the projection sees other addons' compounds and layers too.

<template #example>
<Example root="swofty.f1-1990" code-file="native/src/tyres.cpp" data-file="registry/tyres.json" files="swofty.json, api/swofty_f1_1990_api/tyres.hpp, native/src/projection.cpp, native/CMakeLists.txt, tyres/goodyear.vcmpd">
<template #code>

```cpp
#include <swofty_f1_1990_api/tyres.hpp>
#include "projection.hpp"

using namespace swofty;

SWOFTY_REGISTER
{
    auto letter = [](const char *key, const char *l, int soft, const char *colour, tyres::Code code) {
        auto phys = tyres::physical.add(std::string("goodyear_") + key, {
            .package = cars::packages["swofty.f1-1990/f1_1990"], .code = code, .vcmpd = file("tyres/goodyear.vcmpd"),
        });
        return f1990::compounds.add(key, { .letter = l, .softness = soft, .physical = phys, .colour = Colour(colour) });
    };
    auto a = letter("a", "A", 1, "#FFFFFF", tyres::Code::har);
    auto b = letter("b", "B", 2, "#F6C21C", tyres::Code::med);
    auto c = letter("c", "C", 3, "#E2231A", tyres::Code::sof);
    auto d = letter("d", "D", 4, "#9B2FAE", tyres::Code::sup);
    auto e = letter("e", "E", 5, "#2F74E8", tyres::Code::uso);
    auto w = letter("w", "W", 0, "#22A45D", tyres::Code::wet);
    f1990::compounds.layer(e, [](f1990::TyreCompound &t) { t.qualifying_only = true; });
    f1990::compounds.layer(w, [](f1990::TyreCompound &t) { t.wet = true; });

    f1990::tyre_rules.add("season", {
        .race = { b, c, d }, .qualifying = { e }, .wet = w,
        .rounds = {
            { .round = 4,  .venue = tracks::venues["venue/monaco"], .race = { c, d } },
            { .round = 11, .venue = tracks::venues["venue/spa"],    .race = { a, b, c } },
        },
    });

    f1990::tyre_rules.check([](const f1990::TyreRules &r, swofty::Errors &err) {
        for (const auto &round : r.rounds)
            if (f1990::dry_count(r, round) > 4)
                err.add("/rounds/round=" + std::to_string(round.round), "names more than 4 dry compounds; 4 fit");
    });

    // the projection: core records from the effective f1990 records, when the core resolves
    tyres::visual.provide(f1990::project_visual, f1990::list_visuals);
    tyres::classes.provide(f1990::project_class, f1990::list_classes);
    tyres::allocations.provide(f1990::project_allocation, f1990::list_allocations);
    tyres::per_event.provide(f1990::project_event, f1990::list_events);
    tyres::rules.add("f1_1990", { .within = f1990::scope, .tyre_warmers = false, .refill_when_empty = true });
}

SWOFTY_ON_LOAD
{
    events::define<f1990::events::CompoundFitted>(f1990::scope);
    services::publish(f1990::kTyres);                        // the table, in projection.cpp
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
{
  "swofty.f1-1990.tyre_compound": {
    "a": { "letter": "A", "softness": 1, "physical": "swofty.f1-1990/goodyear_a", "colour": "#FFFFFF" },
    "b": { "letter": "B", "softness": 2, "physical": "swofty.f1-1990/goodyear_b", "colour": "#F6C21C" },
    "c": { "letter": "C", "softness": 3, "physical": "swofty.f1-1990/goodyear_c", "colour": "#E2231A" },
    "d": { "letter": "D", "softness": 4, "physical": "swofty.f1-1990/goodyear_d", "colour": "#9B2FAE" },
    "e": { "letter": "E", "softness": 5, "physical": "swofty.f1-1990/goodyear_e", "colour": "#2F74E8", "qualifying_only": true },
    "w": { "letter": "W", "softness": 0, "physical": "swofty.f1-1990/goodyear_w", "colour": "#22A45D", "wet": true }
  },
  "swofty.f1-1990.tyre_rules": {
    "season": {
      "race": ["swofty.f1-1990/b", "swofty.f1-1990/c", "swofty.f1-1990/d"], "qualifying": ["swofty.f1-1990/e"],
      "wet": "swofty.f1-1990/w", "sets": { "race": 7, "qualifying": 3, "wet": 3 },
      "rounds": [
        { "round": 4,  "venue": "venue/monaco", "race": ["swofty.f1-1990/c", "swofty.f1-1990/d"] },
        { "round": 11, "venue": "venue/spa",    "race": ["swofty.f1-1990/a", "swofty.f1-1990/b", "swofty.f1-1990/c"] }
      ]
    }
  }
}
```

</template>
</Example>
</template>
</Split>

The data version gives other addons the same records to read and layer. The projection, the check, the event and the service need code.

```text
[registry] register: swofty.f1-1990 (f1_1990_tyres.dll) 2 own registries (swofty.f1-1990.tyre_compound 1, swofty.f1-1990.tyre_rules 1),
           6 swofty.f1-1990.tyre_compound, 1 swofty.f1-1990.tyre_rules, 6 tyre.physical, 1 tyre.rules,
           4 providers (tyre.visual, tyre.class, tyre.allocation, tyre.event) (early stage, 1.4 ms)
[registry] swofty.f1-1990 projection: 6 compounds, 2 round overrides -> tyre.visual 6, tyre.class 3, tyre.allocation 3, tyre.event 16
[registry] applied at boot: tyre.physical 6 (1 file in an overlay archive), tyre.visual 6, tyre.class 3, tyre.allocation 3 (131 database records)
```

### The scope

The whole system applies only inside 1990, and Grand Prix and every other mode stay stock. A scope is an `events::Selector`, the same value the core understands on records, layers, hook subscriptions and events:

```cpp
events::data_group("f1_career_1990")                           // one career's data
events::setup_mode("swofty.f1-1990/f1_1990")                   // every session run under that mode
events::content("swofty.classic-seasons-2017/f1_2017")         // one GP/TT package and its variants
events::vehicle_package("swofty.f1-1990/f1_1990")              // cars of one package
```

The projection carries it onto every core record: each class lists only the 1990 mode, and the rules have `.within = f1990::scope`.

A layer says where it applies with `.in(...)`, using the same selectors. A selector can also pick records by a core tag, such as `c/f1_cars` or `c/street_circuits`. The core defines these tags, and any addon adds records to them. A variant is in every tag its base is in, so the 1990 cars, variants of stock F1 cars, are in `c/f1_cars`.

```cpp
// blankets back on in 1990 careers only; a 1990 session outside a career keeps them off
tyres::rules.layer(tyres::rules["swofty.f1-1990/f1_1990"], [](tyres::Rules &r) { r.tyre_warmers = true; })
    .in(events::data_group("f1_career_1990"));

// every F1 car's AI, whoever added the car, races more cautiously; inside 1990 only
ai::vehicle_configs.layer(tagged(&ai::VehicleConfig::vehicle, cars::vehicles.tag("c/f1_cars")), [](ai::VehicleConfig &c) {
    c.racecraft.overtake_aggression *= 0.9f;
}).in(f1990::scope);
```

A filtered layer applies when matching content becomes active or a session loads, and is put back when it ends. On a registry the game reads only at boot, the core refuses the filter with a log line.

### The projection

The game knows compound classes, visual compounds, events and allocations, not Goodyear letters. The projection turns the effective letters and rounds into those, and the core allocates the stat ids.

- **It reads** the 6 `swofty.f1-1990.tyre_compound` records and the `swofty.f1-1990.tyre_rules` record with its 2 round overrides, plus the records and layers other addons add.
- **It provides** a `tyre.visual` for each letter (`swofty.f1-1990/goodyear_{a,b,c,d,e,w}`, ids from the allocator), one `tyre.class` per distinct round shape (such as `{B,C,D,E,W}` and `{C,D,E,W}`), a `tyre.event` per round (`swofty.f1-1990/f1_1990/r01` to `r16`, with the class and presets), and one `tyre.allocation` per shape.
- **It refuses** a round that names more than 4 dry compounds. A class has 5 options, and W takes one.

```cpp
// native/src/projection.cpp
std::optional<ClassShape> shape_of(const f1990::TyreRules &rules, const f1990::RoundTyres &r)
{
    std::vector<f1990::TyreCompound> dry = effective(r.race.empty() ? rules.race : r.race)
                                         + effective(r.qualifying.empty() ? rules.qualifying : r.qualifying);
    std::sort(dry.begin(), dry.end(), [](auto &x, auto &y) { return x.softness > y.softness; });
    if (dry.size() > 4)
        return std::nullopt;                                   // the check already dropped the layer that did this
    ClassShape s;
    for (const auto &c : dry)
        s.options.push_back({ c.letter, { c.physical }, visual_of(c) });
    s.options.push_back({ "W", { effective(rules.wet).physical }, visual_of(effective(rules.wet)) });
    return s;                                                  // rounds with the same shape share one tyre.class
}

std::optional<tyres::Class> project_class(std::string_view id)
{
    auto shape = shape_by_id(id);                              // "swofty.f1-1990/gy_bcdew"
    if (!shape)
        return std::nullopt;
    return tyres::Class{ .modes = { sessions::modes["swofty.f1-1990/f1_1990"] }, .options = shape->slots() };
}
```

What the game allows, which the projection works within:

- **Physics is not the limit:** there are 9 physics codes, tuned per car package in its own `.vcmpd`.
- **A class has at most 5 option slots.**
- **The badge follows the slot, not the compound.** One class per round shape keeps the right letter on screen.
- **The car and its allocation each pick their class in a different function** (`Sess_TyreClass_FromName`, `setPresetChoice`). The core answers both from `tyre.event` through `session.tyres.class_of`.

### Scoped hooks

What records can't do, a scoped handler does. It never runs outside 1990:

```cpp
SWOFTY_ON_LOAD
{
    // when a car runs out of fresh sets of a compound, borrow unused ones from another session
    events::on<tyres::events::ChooseSet>().in(f1990::scope).before([](tyres::events::ChooseSet &c, const events::Context &) {
        if (c.candidates == 0)
            c.chosen = f1990::borrow_unused(c.car, c.wanted_slot);
    });

    // fire the system's own event when a car leaves its box on a new set
    events::on<sessions::events::PitStop>().in(f1990::scope).after([](const sessions::events::PitStop &p, const events::Context &) {
        if (p.tyres_changed) {
            f1990::events::CompoundFitted e{ sizeof e, 1, p.car, f1990::current(p.car) };
            events::fire(e);
        }
    });
}
```

### The service

Other addons ask in letters and never learn how the letters map onto slots:

```cpp
// another addon: a strategy overlay
SWOFTY_ON_LOAD
{
    services::when<f1990::Tyres>([](const f1990::Tyres &t) { g_tyres = &t; });

    events::on<f1990::events::CompoundFitted>().after([](const f1990::events::CompoundFitted &e, const events::Context &ctx) {
        if (ctx.player() && ctx.player()->car == e.car)
            swofty::hud::toast("Tyres", "You're on the " + std::string(f1990::compounds.get(e.compound)->letter));
    });
}
```

A strategy overlay or a telemetry dashboard shows "C", in F1 1990 and in any addon that extends it.

## Another addon extends it

<Split>

`community.goodyear-1990-plus` adds a seventh compound, F (a softer qualifying tyre on `hso`, a physics code none of the six uses), qualifies Monaco and Hungary on it, and makes C wear faster. It changes none of F1 1990's files.

It includes F1 1990's header. Naming F1 1990's registry and records makes F1 1990 a requirement: without it, the core drops this addon's registrations and the guards stop the player.

<template #example>
<Example root="community.goodyear-1990-plus" code-file="native/src/plus.cpp" data-file="registry/tyres.json" files="swofty.json, native/CMakeLists.txt, registry/layers.json">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/tyres.hpp>
#include <swofty_f1_1990_api/tyres.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    auto f_physics = tyres::physical.variant("goodyear_f", tyres::physical["swofty.f1-1990/goodyear_e"], [](tyres::Physical &p) {
        p.code = tyres::Code::hso;
        p.grip = 1.04f;
        p.wear = 1.6f;
    });

    auto f = f1990::compounds.add("f", {
        .letter = "F", .softness = 6, .physical = f_physics, .colour = Colour("#FF7800"), .qualifying_only = true,
    });

    f1990::tyre_rules.layer(f1990::tyre_rules["swofty.f1-1990/season"], [=](f1990::TyreRules &r) {
        r.rounds.at(4).qualifying = { f };               // Monaco
        r.rounds.at(10).qualifying = { f };              // Hungary
    });

    // C wears 15% faster wherever it's used
    tyres::physical.layer(tyres::physical["swofty.f1-1990/goodyear_c"], [](tyres::Physical &p) { p.wear *= 1.15f; });
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
{
  "tyre.physical": {
    "goodyear_f": { "extends": "swofty.f1-1990/goodyear_e", "code": "hso", "grip": 1.04, "wear": 1.6 }
  },
  "swofty.f1-1990.tyre_compound": {
    "f": { "letter": "F", "softness": 6, "qualifying_only": true,
           "physical": "community.goodyear-1990-plus/goodyear_f", "colour": "#FF7800" }
  }
}
```

</template>
</Example>
</template>
</Split>

```json
// registry/layers.json: the layers, with the full ids they change
{
  "swofty.f1-1990.tyre_rules": {
    "swofty.f1-1990/season": { "rounds": [ { "round": 4,  "qualifying": ["community.goodyear-1990-plus/f"] },
                                           { "round": 10, "qualifying": ["community.goodyear-1990-plus/f"] } ] }
  },
  "tyre.physical": { "swofty.f1-1990/goodyear_c": { "wear": 1.15 } }
}
```

When the core resolves:

- F1 1990's projection sees seven compounds. Rounds 4 and 10 get a new class shape, `{F, D, C, W}`.
- If the allocator runs out of visual compound ids, the registration is refused with a log line that names `tyre.visual` and the limit, instead of the game crashing later.
- A layer that makes a round name five dry compounds fails F1 1990's check and is dropped; the conflicts report names its addon.
- With F1 1990 not installed, nothing of this addon applies, and the guard stops a 1990 career with a popup. A `requires` in `registry.json` lets the launcher say so before the game starts; it's optional.

```text
[registry] register: community.goodyear-1990-plus (plus.dll) 1 tyre.physical, 1 swofty.f1-1990.tyre_compound,
           1 layer on swofty.f1-1990.tyre_rules, 1 layer on tyre.physical (early stage, 0.2 ms)
[registry] swofty.f1-1990.tyre_rules swofty.f1-1990/season: edit by community.goodyear-1990-plus changed /rounds/round=4/qualifying,
           /rounds/round=10/qualifying
[registry] swofty.f1-1990 projection: 7 compounds, 4 round overrides -> tyre.visual 7, tyre.class 4, tyre.event 16
```

## More systems, the same shape

**Points with dropped scores (1990: 9-6-4-3-2-1, best 11 results).** The points are a core record (see [Sessions](./sessions)). The best-results rule is F1 1990's own registry, so another addon can change it to best 12 with a layer:

```cpp
struct ChampionshipRules { int best_results = 0; };            // 0: every result counts
SWOFTY_RECORD(ChampionshipRules, best_results)
inline Registry<ChampionshipRules> championship_rules{"swofty.f1-1990.championship_rules",
                                                      { .applies = Applies::read, .snapshot = true }};

SWOFTY_REGISTER
{
    championship_rules.add("drivers", { .best_results = 11 });
}

SWOFTY_ON_LOAD
{
    events::on<career::events::StandingsRebuild>().in(events::data_group("f1_career_1990"))
        .after([](const career::events::StandingsRebuild &s, const events::Context &) {
            auto rules = championship_rules.get(championship_rules["swofty.f1-1990/drivers"]);
            career::apply_dropped_scores(s, rules->best_results);    // keeps the gross totals for the UI
        });
}
```

**Five R&D departments (Aero, Chassis & Suspension, Engine, Tyres, Reliability & Gearbox), as the design shapes them; the R&D registries come in a later header:**

```cpp
SWOFTY_REGISTER
{
    auto tyres_dept = career::rnd_departments.add("tyres", {
        .name = "lng_f1990_rnd_tyres", .colour = Colour("#22A45D"), .icon = file("rnd/tyres.svg"), .cost_scale = 0.8f,
    });
    career::rnd_trees.variant("ferrari", career::rnd_trees["game/rnd_tree_ferrari"], [=](career::RndTree &t) {
        t.data_group = "f1_career_1990";
        auto dept = [](const char *id) { return career::rnd_departments[id]; };
        t.departments = { dept("game/aero"), dept("game/chassis"), dept("game/engine"), tyres_dept, dept("game/reliability") };
    });
}
```

The game draws four departments natively. R&D Tyres draws a fifth in native code; a registry for more departments comes in a later header.

**Two qualifying sessions:** a `sessions::formats` record with a `best_of` grid rule, shown in full on [Sessions and weekends](./sessions#f1-1990-s-weekend). `best_of` is a grid rule the core adds to `session.grid.build`, and every addon that needs it uses the core's. F1 1990 gets a setup mode of its own for this weekend shape. Every Grand Prix and Time Trial box shares `swofty_f1`, and a system whose rules fit that mode's shape sets them per box (see [Rules per box](./sessions#rules-per-box)).

**A character (Elena becomes Flavio):**

```cpp
career::characters.add("flavio", {
    .replaces = career::characters["game/elena"],
    .first_name = "lng_f1990_agent_first",
    .last_name = "lng_f1990_agent_last",
    .portrait = file("ui/flavio.png"),
    .mute = { "agent", "agent_cutscene" },
});
```

## What the core provides for systems

The core provides these pieces for systems. They're built in this order:

1. **Scope as a core value** (`events::Selector`), on records, layers, hooks and events.
2. **A stable id allocator.** Ids never collide between addons and never move between game starts. One store per install holds them, and an id is reused only when no career uses it.
3. **Whole-row clones** that copy every key of the donor row. A missing unnamed key can't crash the boot.
4. **Career tiles and save partitions** as records.
5. **Hook filters by scope.**
6. **UI extension points:** texture-array slices, icon glyphs, scoped screens and scoped strings.
7. **Capacity checks at registration:** row counts, grid size and ids.
8. **Services.**
9. **Dependencies** (see [Dependencies and guards](/std/dependencies)).
10. **A career id,** given to each career when it's created.

Extending game enums comes after these.

F1 1990 is the test case. Its version for 1.5 is 16 DLLs that check "am I inside 1990" six different ways. On these pieces, 13 of the 16 become records, core-answered hook points or fixes in the core. One small DLL is left, with three handlers: the best-of grid, a qualifying engine line, and dropped scores.
