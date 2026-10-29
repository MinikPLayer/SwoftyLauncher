---
title: Cars
description: Cars in code - vehicles, packages and handling as typed registries, with the car events and operations, protected history values and imported car ports.
---

# Cars

Add a car, change how every car handles, or register parts that come off in a crash. Classic Seasons' 2017 Ferrari, for example, is one `cars::Vehicle` record that starts from the stock 2025 Ferrari and sets its own package, model, livery, engine sound and handling.

A few terms used on this page:

- A **vehicle** is a `cars::Vehicle` record: one car, one row in the game's `entrants.vehicles` table. It points at its package, model, handling, engine sound, livery and power unit, and each of those is a record other addons can change.
- A **package** is a `cars::Package` record: the row that says where a group of cars keeps its files and AI data.
- **Handling** is a `cars::Handling` record: one car's VTF, the game's physics file, decoded into typed fields.
- A **layer** changes values on a record that already exists. A **variant** is a separate record that inherits another record's values and overrides some of them. A **selector** layers every record that matches a condition, now and later.
- A **box** is one of the year tiles in Grand Prix or Time Trial. The **base** is the data every year shares.

## The header

<Example root="swofty-sdk" code-file="include/swofty/systems/cars.hpp">
<template #code>

```cpp
#include <swofty/registry.hpp>

namespace swofty::cars {

enum class Class { f1, f1_26, f2, classic, my_team, safety_car };
enum class Attribute;                              // a live car's R&D attribute: engine_power ... (the map is an RE item)
enum class Department;                             // an R&D department: the one whose data a fit reads

struct Package {                                   // one entrants.vehicles.vehicle_packages row
    Ref<Package> donor;                            // the row it copies, all 72 keys (usually game/f1_2025)
    std::string resource_path;                     // "cs_2017_vehicle_package/teams/[FILENAME]/wep/[FILENAME]"
    std::string ai_package_folder;                 // the folder AI track data is found by
    std::string driver_animation_set;              // "cs_2017"
    bool halo = true;                              // has_cockpit_halo
    bool aero_2026 = false;                        // is_26_aero_regulations
    Ref<Package> setups_from;                      // setup presets copied from this package's row (default: donor)
    StatId stat_id = StatId::automatic;            // the core allocates it
};

struct AiSlot {
    Ref<ai::TrackPack> track_pack;                 // the baked AI track data the car drives on
    std::string asset_filename = "auto";           // the name AI tables are keyed by; auto takes a free stock one
};

struct Vehicle {                                   // one entrants.vehicles row
    Ref<Package> package;                          // its package
    std::string label;                             // the row label: "cs17_ferrari"
    Ref<Vehicle> donor;                            // the row add() copies, all 246 keys (a variant copies its base)
    Path model;                                    // the car model files (teams/<t>/idf/, wep/); liveries fit by it;
                                                   // empty: the donor's
    Ref<sounds::EngineSound> engine_sound;         // missing: falls back to the donor's
    Ref<liveries::Livery> livery;                  // the livery it races in
    Ref<Handling> handling;                        // its VTF
    Ref<power::Unit> power_unit;                   // its power unit
    AiSlot ai;                                     // where its AI data comes from
    Class performance_class = Class::f1;           // performance_class
    int performance_tier = 0;                      // performance_tier
    float tyre_wear_scale = 1.0f;                  // tyre_wear_scale
    StatId stat_id = StatId::automatic;            // vehicle ids run to 1,023
};

struct WingDamage {                                // F1WingDamage, read on every front wing impact
    float speed_threshold;                         // impacts below this do nothing (stock 3)
    float damage_scale;                            // damage per unit of impact (stock 0.0001)
    float chassis_damage_multiple;                 // share of a body hit that reaches the wing (stock 0.3)
    float side_detach_scale;                       // damage at which one side breaks off (stock 0.5)
    float full_detach_scale;                       // damage at which the wing comes off (stock 1)
};

struct SuspensionDamage {                          // F1SuspensionDamage, read on every impact
    float speed_threshold;                         // stock 7
    float front_damage_scale, rear_damage_scale;   // stock 0.001, 0.0001
    float wheel_detach_scale;                      // stock 1
    float parts_detach_scale;                      // the game raises it to at least 1.5 x wheel_detach_scale
    float linear_fracture_limit, angular_fracture_limit;   // stock 50, 30
};

struct Chassis { float mass, moi_x, moi_y, moi_z, com_y, com_z; };   // mass in kg (2025 cars: 798-813)
struct F1Aero {                                    // F1Aero (the rest: see Car physics and handling)
    float front_coeff_lift_multiplier, rear_coeff_lift_multiplier;
    float body_drag_coeff_min, body_drag_coeff_max;
};
struct CoreAero { bool slip_streaming, slip_stream_while_braking; };

struct Handling {                                  // one VTF, decoded; edits change fields, the core re-encodes it
    Ref<Handling> from;                            // start from another car's VTF, then edit
    Path vtf;                                      // or a whole VTF of your own
    Class cls = Class::f1;                         // the car's class; "class" in data files
    float damage_scale = 1.0f;                     // scales every *_damage threshold at once
    Chassis chassis;                               // Chassis
    F1Aero f1_aero;                                // F1Aero
    CoreAero core_aero;                            // CoreAero
    WingDamage wing_damage;                        // F1WingDamage
    SuspensionDamage suspension_damage;            // F1SuspensionDamage
    TyreDamage tyre_damage;                        // F1TyreDamage
    BodyDamage body_damage;                        // F1BodyDamage
    Tyres tyres;                                   // the tyre thermal and wear fields
    std::vector<RadUpgrade> rad_upgrades;          // the radUpgrade* blocks R&D fits
    // Engine, GearBox, FuelTank, FuelECU, BrakeECU, Steering, Differential, SuspensionSystem, F1Tuning,
    // F1Assists, Turbo, EnergyRecoverySystem, F1Plank: every section, typed the same way (Car physics and handling)
};

inline Registry<Vehicle> vehicles{"vehicle", { .applies = Applies::boot }};
inline Registry<Package> packages{"vehicle.package", { .applies = Applies::boot }};
inline Registry<Handling> handling{"vehicle.handling", { .applies = Applies::session }};   // the 1.6.0 core stores handling records and applies nothing from them

Result reset_upgrades(int car);                                      // undo the last fitted upgrade
// the cars on track, live: cars::live (Car physics and handling)

}

namespace swofty::cars::events {

struct UpgradesFit {                               // Car_Fit_UpgradesToRating: before, after
    static constexpr const char *point = "car.upgrades.fit";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    int32_t car;                                   // the live car
    const char *vehicle_id;                        // its record
    Department department;                         // the department whose data is fitted (int32_t)
    float driver_stat;                             // the driver stat the fit reads
    float rating;                                  // the achieved rating; before handlers may change it

    Ref<Vehicle> vehicle() const;
};

struct VtfRead {                                   // the Car_Read_<Section> readers: before
    static constexpr const char *point = "car.vtf.read";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    live::Car car;                                 // the car being built: its index, vehicle, team and driver
    const char *section_str;                       // "F1WingDamage"
    const char *field_str;                         // the field's name
    const char *upgrade_str;                       // the radUpgrade block being applied, or "" for the base values
    float value;                                   // in clear, before the game obfuscates it

    std::string_view section() const { return swofty::detail::payload_str(section_str); }
    std::string_view field() const { return swofty::detail::payload_str(field_str); }
    std::string_view upgrade() const { return swofty::detail::payload_str(upgrade_str); }
};

struct Built {                                     // a car's parts were read from its handling file: after
    static constexpr const char *point = "car.built";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    live::Car car;
};

struct Reset {                                     // the game re-read a car's parts (a rebuild or an upgrade replay): after
    static constexpr const char *point = "car.reset";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    live::Car car;
    bool upgrade;                                  // true after an R&D upgrade replay
};

struct ControllersRebuilt {                        // the game built the car's controller set: after
    static constexpr const char *point = "car.controllers.rebuilt";
    static constexpr uint32_t version = 1;
    enum Reason : uint32_t { rebuild = 0, resettle = 1 };
    uint32_t size, payload_version;
    live::Car car;
    uint32_t reason;                               // rebuild: after the sets' build (Sess_ControllerSystem_BuildSets); in-game check T144
};

struct PartsRegister {                             // Car_FrontWing_Init: after
    static constexpr const char *point = "car.parts.register";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    int32_t car;
    const char *vehicle_id;
    void *front_wing;                              // the game's front wing component
    int32_t (*add_fn)(void *user, const char *node, float detach_at, bool debris);
    void *user;

    Ref<Vehicle> vehicle() const { return swofty::detail::payload_ref<Vehicle>(vehicle_id); }
    Result add(const Part &part) const
    {
        return add_fn ? swofty::detail::result(add_fn(user, part.node.c_str(), part.detach_at, part.debris))
                      : Result::unavailable;
    }
};

}
```

</template>
</Example>

## Heavier Damage, in code

<Split>

Heavier Damage makes wings and suspension break harder on every car, and gives the 2026 cars the Realism Overhaul's performance.

- **A selector picks the cars:** every handling record in the core's `c/f1_cars` tag, which holds the stock F1, 2026 and My Team cars, and any car another addon adds to it. A tag is a shared, named set of records that any addon can add to. It reaches every year's cars, not only the base.
- **Two option-bound edits** read `wing_damage` and `suspension_damage`. When the player changes one, every car built from the next session load gets it, with no restart.
- **No VTF files ship.** The core applies the edited fields to each car as the game builds it.
- **R&D Tyres** changes the tyre fields and the `radUpgrade*` blocks of the same 91 cars' VTFs. Each addon edits only its own fields, and both apply.

<template #example>
<Example root="william09leo.heavier-damage-realism" code-file="native/src/damage.cpp" data-file="registry/layers.json" files="swofty.json, native/CMakeLists.txt, README.txt">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/cars.hpp>

using namespace swofty;

namespace {

// how much harder than stock each level is: damage per hit, and how early parts come off
float harder(const std::string &level) { return level == "light" ? 1.2f : level == "heavy" ? 2.0f : 1.5f; }

void wing(cars::WingDamage &w, float k)
{
    w.damage_scale *= k;
    w.full_detach_scale /= k;
    w.side_detach_scale /= k;
}

void suspension(cars::SuspensionDamage &s, float k)
{
    s.front_damage_scale *= k;
    s.rear_damage_scale *= k;
    s.wheel_detach_scale /= k;
}

}

SWOFTY_REGISTER
{
    // every F1 car: stock, My Team, every year pack's and any other addon's that joins the tag
    auto f1_cars = tagged<cars::Handling>(cars::handling.tag("c/f1_cars"));

    cars::handling.layer(f1_cars, [](cars::Handling &h) {
        auto level = option<std::string>("wing_damage");          // re-runs when the player changes it
        if (level != "stock")
            wing(h.wing_damage, harder(level));
    });

    cars::handling.layer(f1_cars, [](cars::Handling &h) {
        auto level = option<std::string>("suspension_damage");
        if (level != "stock")
            suspension(h.suspension_damage, harder(level));
    });

    // the 2026 Career Realism Overhaul's performance, 2026 cars only
    cars::handling.layer(where<cars::Handling>([](const cars::Handling &h) { return h.cls == cars::Class::f1_26; }),
                         [](cars::Handling &h) {
                             h.f1_aero.front_coeff_lift_multiplier *= 0.97f;
                             h.f1_aero.rear_coeff_lift_multiplier *= 0.97f;
                             h.f1_aero.body_drag_coeff_min *= 1.02f;
                             h.f1_aero.body_drag_coeff_max *= 1.02f;
                         });
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
// registry/layers.json: selector layers, one per option value
{
  "vehicle.handling": {
    "$select": [
      { "tag": "c/f1_cars", "when": { "option": "wing_damage", "is": "light" },
        "patch": { "wing_damage": { "damage_scale": 0.00012, "side_detach_scale": 0.42, "full_detach_scale": 0.83 } } },
      { "tag": "c/f1_cars", "when": { "option": "wing_damage", "is": "normal" },
        "patch": { "wing_damage": { "damage_scale": 0.00015, "side_detach_scale": 0.33, "full_detach_scale": 0.67 } } },
      { "tag": "c/f1_cars", "when": { "option": "wing_damage", "is": "heavy" },
        "patch": { "wing_damage": { "damage_scale": 0.0002, "side_detach_scale": 0.25, "full_detach_scale": 0.5 } } },
      { "tag": "c/f1_cars", "when": { "option": "suspension_damage", "is": "normal" },
        "patch": { "suspension_damage": { "front_damage_scale": 0.0015, "rear_damage_scale": 0.00015, "wheel_detach_scale": 0.67 } } },
      { "where": { "class": "f1_26" },
        "patch": { "f1_aero": { "front_coeff_lift_multiplier": 0.97, "rear_coeff_lift_multiplier": 0.97 } } }
    ]
  }
}
```

A data patch sets values and can't scale them. The 2026 patch sets the result outright.

</template>
</Example>
</template>
</Split>

```text
[registry] register: william09leo.heavier-damage-realism (heavier_damage.dll) 3 layers on vehicle.handling (early stage, 0.2 ms)
[registry] vehicle.handling: selector tag c/f1_cars from william09leo.heavier-damage-realism matches 171 records
           (91 game/, 80 swofty.classic-seasons-*)
[registry] vehicle.handling game/ferrari_2026: edit by william09leo.heavier-damage-realism changed /wing_damage/damage_scale,
           /wing_damage/side_detach_scale, /wing_damage/full_detach_scale (option wing_damage=normal)
[registry] vehicle.handling game/ferrari_2026: 2 addons, no field contested (swofty.rnd-tyres-thermal: /tyres, /rad_upgrades;
           william09leo.heavier-damage-realism: /wing_damage, /suspension_damage, /f1_aero)
[cars] built vehicle 0 (game/ferrari_2026, team game/ferrari, driver game/charles_leclerc): 23 parts, 3 layers
[registry] option william09leo.heavier-damage-realism/wing_damage: normal -> heavy; vehicle.handling applies from the next session load
```

The options are declared in `swofty.json`:

```json
{
  "id": "william09leo.heavier-damage-realism",
  "options": [
    { "id": "wing_damage",       "type": "choice", "values": ["stock", "light", "normal", "heavy"], "default": "normal" },
    { "id": "suspension_damage", "type": "choice", "values": ["stock", "light", "normal", "heavy"], "default": "normal" }
  ]
}
```

## A car of your own

<Split>

Classic Seasons 2017 adds the 2017 Ferrari: a package, then a vehicle on it. The vehicle is a variant of the stock 2025 Ferrari.

- **The core copies every key of the base rows,** and writes the setup preset row for the new package folder from `setups_from`. Without one, the AI's setups ground the car.
- **A field the variant sets beats layers on its base.** The 2017 car sets its own `livery`, so a drop-in livery for the 2025 Ferrari doesn't reach it (see [Liveries](/std/systems/liveries)).
- **The car has its own model,** the 2017 mesh. A livery fits a car by its model. Liveries painted for the 2025 Ferrari can't be put on the 2017 car.
- **Its pace is protected.** The handling's `chassis` and `f1_aero` recreate the 2017 pecking order. Another addon's selector skips protected fields, unless the player turns on "Also change year packs" for that addon. A layer on this record itself still competes by load order.
- The core allocates both stat ids. `engine_sound` and `livery` are [sounds](/std/systems/sounds) and [liveries](/std/systems/liveries) records. Both apply at boot, before the database loads.

<template #example>
<Example root="swofty.classic-seasons-2017" code-file="native/src/cars.cpp" data-file="registry/cars.json" files="swofty.json, native/src/content.cpp, cars/ferrari/wep/ferrari.erp, cars/ferrari/idf/ferrari.erp, sounds/cs17_ferrari.bdl">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/cars.hpp>
#include <swofty/systems/sounds.hpp>
#include <swofty/systems/liveries.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    auto package = cars::packages.add("cs_2017_ferrari", {
        .donor = cars::packages["game/f1_2025"],
        .resource_path = "cs_2017_vehicle_package/teams/[FILENAME]/wep/[FILENAME]",
        .driver_animation_set = "cs_2017",
    });

    auto handling = cars::handling.variant("ferrari", cars::handling["game/ferrari_2025"], [](cars::Handling &h) {
        h.chassis.mass = 728.0f;
        h.f1_aero.front_coeff_lift_multiplier = 0.92f;
        h.f1_aero.rear_coeff_lift_multiplier = 0.92f;
    });
    cars::handling.protect(handling, { "chassis", "f1_aero" });       // the 2017 pecking order

    // a variant of the stock car: every field it doesn't set, and every layer on game/ferrari_2025, flows in
    cars::vehicles.variant("ferrari", cars::vehicles["game/ferrari_2025"], [=](cars::Vehicle &v) {
        v.package = package;
        v.label = "cs17_ferrari";
        v.model = file("cars/ferrari/");
        v.engine_sound = sounds::engine_sounds["swofty.classic-seasons-2017/ferrari"];
        v.livery = liveries::liveries["swofty.classic-seasons-2017/ferrari"];
        v.handling = handling;
        v.ai = { .track_pack = ai::track_packs["swofty.classic-seasons-2017/ferrari"] };
        v.tyre_wear_scale = 1.04f;
    });
}
```

</template>
<template #data>

```json
// registry/cars.json
{
  "vehicle.package": {
    "cs_2017_ferrari": { "donor": "game/f1_2025", "driver_animation_set": "cs_2017",
                         "resource_path": "cs_2017_vehicle_package/teams/[FILENAME]/wep/[FILENAME]" }
  },
  "vehicle.handling": {
    "ferrari": { "extends": "game/ferrari_2025", "chassis": { "mass": 728 },
                 "f1_aero": { "front_coeff_lift_multiplier": 0.92, "rear_coeff_lift_multiplier": 0.92 }, "$protect": ["chassis", "f1_aero"] }
  },
  "vehicle": {
    "ferrari": {
      "extends": "game/ferrari_2025",
      "package": "swofty.classic-seasons-2017/cs_2017_ferrari", "label": "cs17_ferrari", "model": "cars/ferrari/",
      "engine_sound": "swofty.classic-seasons-2017/ferrari", "livery": "swofty.classic-seasons-2017/ferrari",
      "handling": "swofty.classic-seasons-2017/ferrari",
      "ai": { "track_pack": "swofty.classic-seasons-2017/ferrari" }, "tyre_wear_scale": 1.04
    }
  }
}
```

</template>
</Example>
</template>
</Split>

```text
[registry] stat ids: vehicle swofty.classic-seasons-2017/ferrari -> 515 (pool to 1023)
[registry] vehicle.package swofty.classic-seasons-2017/cs_2017_ferrari: setup presets from game/f1_2025 (1 row, 421 keys)
[registry] vehicle.handling swofty.classic-seasons-2017/ferrari: selector from someone.realism-pack skipped /f1_aero
           (protected by swofty.classic-seasons-2017; "Also change year packs" is off)
[registry] applied at boot: vehicle.package 1, vehicle 1 (2 rows, 3 database records)
```

## Imported car ports

A car port replaces the stock cars in place: fanfreluche's F1 2020 cars, for example, put new models, liveries and handling on the stock 2025 cars. The importer asks once where a car port or season pack applies:

```text
F1 2020 cars                      Applies in  ( ) Everywhere
                                              (x) Grand Prix and Time Trial only
```

- **Grand Prix and Time Trial only** is the default. Careers keep the stock cars, including 2025 Driver Career and Progressive's 2025 season.
- **Everywhere** changes the stock cars in every mode and every career that uses them.
- **Single liveries, helmets and sounds** aren't asked. They apply everywhere.

The rest of the importer's choices are on [The Simplified importer](/std/importer).

## Parts at run time

`car.parts.register` fires when the game builds a car's front wing. A handler adds the parts that come off in a crash:

```cpp
events::on<cars::events::PartsRegister>().after([](cars::events::PartsRegister &p, const events::Context &) {
    if (p.vehicle().id() == "game/mclaren_2026")
        p.add({ .node = "mirror_l", .detach_at = 0.6f, .debris = false });
});
```

```text
[events] car.parts.register (car 4, game/mclaren_2026): 1 after handler; 1 part added
```

## Described, in headers that come later

`vehicle.damage_model` and `vehicle.parts` are described here. The 1.6.0 headers have no registry for either:

```cpp
enum class Component;                              // a damage.damage_levels row: front_wing, gearbox, engine ... (11)
enum class Fault;                                  // repair_times.faults: open_drs, closed_drs, front_wing ... (21)

struct DamageLevel {                               // one damage.damage_levels row
    float rate = 1.0f;                             // damage_rate_multiplier
    float minimal, repairable, retirement, terminal;   // the *_damage thresholds
    bool should_pit;                               // should_pit
};

struct DamageModel {
    std::map<Component, DamageLevel> levels;       // front_wing, gearbox, engine ... (11 rows)
    float puncture_limit;                          // damage.damage_limits
    std::map<Fault, float> repair_times;           // repair_times.faults: open_drs, closed_drs, front_wing ... (21)
};

struct Parts { Ref<Vehicle> vehicle; std::vector<cars::Part> parts; };   // cars::Part is in the header: the node, detach_at, debris
```

`vehicle.damage_model` holds the damage levels, the puncture limit and the repair times, per car or for every car (`game/default`):

```cpp
damage_models.layer(damage_models["game/default"], [](DamageModel &d) {
    d.levels[Component::front_wing].repairable *= 0.8f;
    d.repair_times[Fault::front_wing] = 9.5f;
});
```


## Upgrades and handling as a car is built

An R&D upgrade is a `radUpgrade*` block in the handling file, read on top of the car's values and scaled by the development progress. [Car physics and handling](/std/systems/car-physics#r-d-upgrades) has the operators and the stock blocks. Changing a car on track during a session is `cars::live`, on the same page.

```cpp
// car.upgrades.fit: cap the rating an upgrade reaches
events::on<cars::events::UpgradesFit>().before([](cars::events::UpgradesFit &f, const events::Context &) {
    f.rating = std::min(f.rating, 0.95f);
});

// car.vtf.read: change a value as the game reads it for one car, in clear
events::on<cars::events::VtfRead>().before([](cars::events::VtfRead &r, const events::Context &) {
    if (r.section() == "F1WingDamage" && r.car.team().id() == "game/haas")
        r.value *= 0.9f;
});

cars::reset_upgrades(car);                                        // Car_Reset_LastFittedUpgrade
```

## Readiness and limits

- **Records: ready.** The VTF round trip is proven on 74 of 74 files.
- **Events and operations: partial.** `car.upgrades.fit` and `reset_upgrades` are L2 (`Car_Fit_UpgradesToRating`, `Car_Reset_LastFittedUpgrade`). The `Car_Read_<Section>` readers, the car build and the reset path are L2; in-game traces confirm when each runs.
- **Handling records are stored, not applied.** The 1.6.0 core keeps `vehicle.handling` records and layers and logs `[registry] vehicle.handling: N record(s); this core applies nothing from it`. The game parses a handling file once, then reads each car's parts from it when the car is built, at every session load, and again when it rebuilds a car; the core that applies layers at that read ships later.
- **Damage models and parts records** (`vehicle.damage_model`, `vehicle.parts`) are described in the design and have no registry in the 1.6.0 headers.
- **Live physics values are XOR-obfuscated.** `car.vtf.read` and `cars::live` hand you the value in clear, and the core encodes your writes.
- **Unknown:** who sets the per-car damage switches (CarLive +0x130), the tyre temperature model, and compound switching.
- **Setup presets are side data,** found by package folder. The core writes the row with the package. A missing row grounds the AI's setups, and a `_metadata_`-only row crashes session load.
- **AI data** is found by `ai_package_folder/asset_filename`. `AiSlot` maps an added car onto a stock slot.
- **A car is registered** only when it's available and an entrant list names it.
- **Dead stock rows:** `f1_classic` and the 23 `f1cg_*` packages have no archives and no AI data. Use `f1_2025` or `f1_2026` as the donor.
- **Scale:** Classic Seasons 2016 to 2024 add about 80 packages.
