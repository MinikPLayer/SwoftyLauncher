---
title: Power units, ERS and DRS
description: Power units, engine suppliers, DRS rules and engine component rules in code, with the engine wear, grid penalty and DRS events.
---

# Power units, ERS and DRS

With the power system you give cars an engine, set when DRS opens, and change the engine component rules. F1 1990's cars, for example, run a normally aspirated V10 or V12 with no ERS and no DRS.

A **power unit** is the engine, turbo, fuel and ERS blocks a car's handling file carries. A **supplier** is the engine maker a car shows. **Component rules** are how many engine parts a driver gets per season, how fast they wear, and the grid drops for using more.

::: info In 1.6.0
The power system has no header in 1.6.0 (`swofty/systems/power.hpp` ships later). This page describes the design. `sessions::Rules::drs` refers to its `power::DrsRule`.
:::

## The header

<Split>

`swofty/systems/power.hpp` has four registries:

- `power::units`: a power unit, applied into every car that points at it (`power_unit`);
- `power::suppliers`: an engine supplier row (`engine.supplier`);
- `power::drs_rules`: a DRS-allowed lap group (`rules.drs`);
- `power::component_rules`: a component set: allocation, grid drops, wear (`rules.pu_components`).

A car points at its unit through `cars::Vehicle::power_unit`.

<template #example>
<Example root="swofty-sdk" code-file="include/swofty/systems/power.hpp">
<template #code>

```cpp
#pragma once
#include <swofty/registry.hpp>
#include <swofty/events.hpp>
#include <swofty/systems/cars.hpp>
#include <swofty/systems/drivers.hpp>
#include <swofty/systems/sessions.hpp>

namespace swofty::power {

struct Unit {
    VtfSection engine;                         // Engine: RevsPowerSpline, rev limiter
    VtfSection turbo;                          // Turbo: MaxBoost, TurboMode
    VtfSection fuel_tank;                      // FuelTank
    VtfSection fuel_ecu;                       // FuelECU: burn, power, revs and temperature scales per Lean/Standard/Rich/Max
    std::optional<VtfSection> ers;             // EnergyRecoverySystem; nullopt: the car has no ERS (as F2 cars don't)
    VtfSection upgrades;                       // radUpgradeEngine, ERS, FuelConsumption, WearES, WearMGUK, WearMGUH
};
inline Registry<Unit> units{"power_unit", { .applies = Applies::boot }};

struct Supplier {
    Text name;                                 // the supplier name
    std::string nationality;                   // "japan", "italy", ...
    std::string rev_dial;                      // the HUD rev dial and rev lights fields
    StatId stat_id = StatId::automatic;        // an entrants.engines row
};
inline Registry<Supplier> suppliers{"engine.supplier", { .applies = Applies::boot }};

struct DrsRule {
    int race_start = 2;                        // DRS opens on this lap after the start
    int red_flag_restart = 2;                  // ... after a red-flag restart
    int safety_car_end = 2;                    // ... after a full safety car ends
};
inline Registry<DrsRule> drs_rules{"rules.drs", { .applies = Applies::boot }};

enum class Component { ce, es, gearbox, ice, mguh, mguk, tc };

struct ComponentRule {
    int allocation;                            // parts per season
    std::map<std::string, float> wear_rates;   // wear_rates.*, per session kind
};

struct ComponentRules {
    int season;                                // the game picks its set by season only
    std::map<Component, ComponentRule> components;   // "25": all seven; "26": no mguh
    int first_new_grid_drop = 10;              // places for the first part over the allocation
    int subsequent_grid_drop = 5;              // places for each part after that
    int parc_ferme_grid_drop = 22;             // places for a change under parc fermé
};
inline Registry<ComponentRules> component_rules{"rules.pu_components", { .applies = Applies::boot }};

namespace events {

struct Wear {                                  // Game_EngineMgmt_ApplySessionWear
    static constexpr const char *point = "career.engine.wear";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    Ref<drivers::Identity> driver;             // whose parts wear
    sessions::Session session;                 // the session that ends
    Component component;                       // the part
    float wear;                                // the wear the game adds; before handlers may change it
};

struct GridPenalty {                           // Game_EngineMgmt_AddGridPenalty
    static constexpr const char *point = "career.engine.grid_penalty";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    Ref<drivers::Identity> driver;             // who drops
    int places;                                // before handlers may change it; replace = no penalty
    Component reason;                          // the part over the allocation
};

struct DrsAllowed {                            // the DragReductionSystem decision; unavailable until it's reverse-engineered
    static constexpr const char *point = "session.drs.allowed";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    uint32_t car;                              // vehicle index in the session
    bool allowed;                              // before handlers may change it
};

} // namespace events

Result add_grid_penalty(Ref<drivers::Identity> driver, int places, Component reason);
Result set_component_wear(Ref<drivers::Identity> driver, Component c, float wear);

} // namespace swofty::power
```

</template>
</Example>
</template>
</Split>

## A 1990 power unit without ERS

<Split>

F1 1990's cars run a normally aspirated engine with no ERS. The unit is a variant of the stock 2025 unit: its own engine spline and fuel map, and no `EnergyRecoverySystem` section, which is how the game has no ERS (F2 cars have none). There's no ERS-off switch.

The DRS rule opens DRS on lap 200, which means never. F1 1990 has its own setup mode and sets the rule on that mode. Grid penalties for engine parts don't exist in 1990, so a scoped handler replaces them.

<template #example>
<Example root="swofty.f1-1990" code-file="native/src/power.cpp" data-file="registry/power.json" files="swofty.json, native/CMakeLists.txt, pu/v12_engine.vtf, pu/v10_engine.vtf, pu/fuel_1990.vtf">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/events.hpp>
#include <swofty/systems/power.hpp>
#include <swofty/systems/cars.hpp>
#include <swofty/systems/sessions.hpp>

using namespace swofty;

static const events::Selector k1990 = events::vehicle_package("swofty.f1-1990/f1_1990");

SWOFTY_REGISTER
{
    auto stock = power::units["game/pu_2025"];

    auto v12 = power::units.variant("v12_1990", stock, [](power::Unit &u) {
        u.engine = VtfSection::read(file("pu/v12_engine.vtf"));
        u.fuel_ecu = VtfSection::read(file("pu/fuel_1990.vtf"));
        u.ers.reset();                                     // no EnergyRecoverySystem section: no ERS
    });
    auto v10 = power::units.variant("v10_1990", v12, [](power::Unit &u) {
        u.engine = VtfSection::read(file("pu/v10_engine.vtf"));
    });

    power::suppliers.add("honda", { .name = "lng_f1990_engine_honda", .nationality = "japan" });
    power::suppliers.add("ferrari", { .name = "lng_f1990_engine_ferrari", .nationality = "italy" });

    auto never = power::drs_rules.add("never", { .race_start = 200, .red_flag_restart = 200, .safety_car_end = 200 });

    sessions::modes.layer(sessions::modes["swofty.f1-1990/f1_1990"], [=](sessions::Mode &m) { m.drs = never; });

    cars::vehicles.layer(under<cars::Vehicle>("swofty.f1-1990/"), [=](cars::Vehicle &v) {
        v.power_unit = v.label.starts_with("f1990_ferrari") ? v12 : v10;
    });
}

SWOFTY_ON_LOAD
{
    // 1990 has no engine allocation: no grid drops for parts
    events::on<power::events::GridPenalty>().in(k1990)
        .replace([](power::events::GridPenalty &p, const events::Context &) {
            swofty::log("[f1-1990] no grid penalty for %s (%d places)", p.driver.id().data(), p.places);
            return true;
        });
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
{
  "power_unit": {
    "v12_1990": { "extends": "game/pu_2025", "engine": "pu/v12_engine.vtf", "fuel_ecu": "pu/fuel_1990.vtf", "ers": null },
    "v10_1990": { "extends": "swofty.f1-1990/v12_1990", "engine": "pu/v10_engine.vtf" }
  },
  "engine.supplier": {
    "honda":   { "name": "lng_f1990_engine_honda", "nationality": "japan" },
    "ferrari": { "name": "lng_f1990_engine_ferrari", "nationality": "italy" }
  },
  "rules.drs": {
    "never": { "race_start": 200, "red_flag_restart": 200, "safety_car_end": 200 }
  }
}
```

</template>
</Example>
</template>
</Split>

```text
[registry] register: swofty.f1-1990 (f1_1990_pu.dll) 2 power_unit, 2 engine.supplier, 1 rules.drs,
           1 layer on session.mode, 1 selector layer on vehicle (early stage, 0.6 ms)
[registry] vehicle: selector under swofty.f1-1990/ from swofty.f1-1990 matched 26 records
[registry] applied at boot: power_unit 2 (26 handling files in an overlay archive), engine.supplier 2, rules.drs 1
[events] career.engine.grid_penalty in vehicle package swofty.f1-1990/f1_1990: replace by swofty.f1-1990 -> handled
[f1-1990] no grid penalty for swofty.f1-1990/alain_prost (10 places)
```

The core writes each unit into the handling files of the cars that use it, and adds them to the game's asset manifest at boot in an overlay archive. The data version has no grid-penalty handler, and no selector over the cars. Each 1990 car then names its unit in its own `vehicle` record.

## DRS per box

Every Grand Prix and Time Trial box shares the `swofty_f1` setup mode. A box sets its own DRS rule in its rules, and the rule applies when a session loads in that box:

```cpp
// a 2010 pack: no DRS in its box; every other box keeps its own rule
auto never = power::drs_rules.add("never", { .race_start = 200, .red_flag_restart = 200, .safety_car_end = 200 });

content::packages.layer(content::packages["community.f1-2010/f1_2010"], [=](content::Package &p) {
    p.rules.drs = never;
});
```

The core applies a box's DRS rule through `session.drs.allowed`. Until the DRS decision is reverse-engineered, that hook point is unavailable and a box runs its setup mode's DRS rule.

## Swapping a unit into other cars

A unit applies into every car that points at it. Classic Seasons 0.5.3 puts the stock 2025 Ferrari's blocks into every car of every season:

```cpp
cars::vehicles.layer(under<cars::Vehicle>("swofty.classic-seasons-"), [](cars::Vehicle &v) {
    v.power_unit = power::units["game/pu_2025"];
});
```

```text
[registry] vehicle: selector under swofty.classic-seasons- from swofty.classic-seasons matched 80 records
```

Checked over UDP after the swap: ICE 550 kW plus MGU-K 120 kW, a 4 MJ store, and DRS for every car.

## Component rules

```cpp
// the 2026 set, with one more power unit per season and softer drops
power::component_rules.layer(power::component_rules["game/components_26"], [](power::ComponentRules &r) {
    r.components[power::Component::ice].allocation += 1;
    r.first_new_grid_drop = 5;
});
```

## Events and operations

```cpp
// half the engine wear in practice
events::on<power::events::Wear>().when(events::session_type({ events::SessionType::practice }))
    .before([](power::events::Wear &w, const events::Context &) { w.wear *= 0.5f; });

// a stewards' addon hands out its own drop
power::add_grid_penalty(drivers::identities["game/max_verstappen"], 5, power::Component::gearbox);
power::set_component_wear(drivers::identities["game/max_verstappen"], power::Component::gearbox, 0.0f);
```

## Readiness and limits

- **Applies at boot: ready.** Unit swaps shipped (Classic Seasons, F1 1990) and were verified. The handling-file round trip is proven.
- **Events: partial.** Engine management (`ApplySessionWear`, `AddGridPenalty`) is L2. DRS and ERS decisions are L1 or unknown: `session.drs.allowed` is unavailable until the DragReductionSystem decision is reverse-engineered, and per-box DRS waits on it. Also unknown: the component-set selector, the ERS deploy and harvest writers, and overtake-mode gating.
- **The game picks a component set by season only.** Choosing one per career needs the selector found.
- **AI tables carry baked ERS data per car** (`ers_boost_data`, `ers_use_granular`). A new unit can outrun the baked tables.
- **The HUD dial differs per package** (`hud_dial` modern, 26, f2).
- **Live physics values are XOR-obfuscated.** Units apply to the handling file before it loads.
- **2025 fuel mixes:** only Lean differs. In 2026 and F2 cars the mixes have no effect.
