---
title: Car physics and handling
description: How a car's handling is built from its handling file, every part you can change, when a change reaches the car, how to change a car live during a session, and how R&D, setup, damage and AI fit in.
---

# Car physics and handling

Every car on track is built from its **handling file**: the `.vtf` in the car's package (`teams/ferrari/handling/ferrari.vtf`). The file has one section per part of the car, such as Engine, F1Aero or F1WingDamage, and each section is a list of named values. Your addon changes those values in three ways:

- **Layer the handling record.** The change reaches every car built from that handling file, from the next session.
- **Change a car live.** Through `cars::live`, during a session, on one car or many, with the car's team and driver in hand.
- **Edit an upgrade block.** The R&D upgrades are blocks inside the same file, so changing one changes what R&D does.

A damage addon shows all three. It offers Light, Normal and Heavy damage in ADDON SETTINGS, and a change reaches the next race with no restart. A Live option goes further: it changes the cars on track at once.

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/cars.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    auto f1 = tagged<cars::Handling>(cars::handling.tag("c/f1_cars"));

    // Every F1 car built from now on: next session, every year.
    cars::handling.layer(f1, [](cars::Handling &h) {
        auto level = option<std::string>("wing_damage");          // "light", "normal", "heavy"
        if (level == "heavy") {
            h.wing_damage.speed_threshold = 2.0f;
            h.wing_damage.damage_scale = 0.000175f;
            h.wing_damage.full_detach_scale = 0.4f;
        }
    });
}
```

## The parts of a car

A built car has 23 parts, one per section of its handling file. `cars::Handling` has a member for each, named after the section, and each part has a field for every value in that section, named after the key. Here are the parts most addons change:

```cpp
namespace swofty::cars {

struct WingDamage {                        // F1WingDamage: the front wing
    float speed_threshold;                 // impacts below this speed do nothing (stock 3)
    float damage_scale;                    // damage per unit of impact (stock 0.0001)
    float chassis_damage_multiple;         // share of a body hit that reaches the wing (stock 0.3)
    float side_detach_scale;               // damage at which one side breaks off (stock 0.5)
    float full_detach_scale;               // damage at which the wing comes off (stock 1)
};

struct SuspensionDamage {                  // F1SuspensionDamage
    float speed_threshold;                 // stock 7
    float front_damage_scale;              // stock 0.001
    float rear_damage_scale;               // stock 0.0001
    float wheel_detach_scale;              // stock 1
    float parts_detach_scale;              // at least 1.5 x wheel_detach_scale: the game raises a lower value
    float linear_fracture_limit;           // stock 50
    float angular_fracture_limit;          // stock 30
};

struct Engine {                            // Engine
    Curve revs_power_spline;               // power in W against engine speed in rad/s (2025 Ferrari: 550 kW at 1191.5 rad/s)
    float idle_rotation_rate;              // rad/s
    float limiter_rotation_rate;           // rad/s
    float mass, inertia;
    float wear_ice_rate, wear_tc_rate, wear_mguh_rate, wear_ce_rate;   // power unit wear per second of use
    // the throttle maps, engine braking, temperatures and the rest of the section
};

struct FuelEcu {                           // FuelECU: one value per fuel mode (lean, standard, rich, max)
    std::array<float, 4> fuel_burn_scale, fuel_power_scale, fuel_revs_scale, temperature_scale;
};

struct Ers {                               // EnergyRecoverySystem
    float max_deploy_power;                // W (2025: 126.3 kW, 2026: 350 kW)
    float energy_store_capacity;           // J (4 MJ)
    float exhaust_power_coefficient, overtake_power_efficiency, max_brake_torque_mguk;
    std::array<ErsMode, 4> modes;          // none, medium, overtake, hotlap: deploy per gear
};

struct F1Aero {                            // F1Aero
    float front_coeff_lift_multiplier, rear_coeff_lift_multiplier;
    float body_drag_coeff_min, body_drag_coeff_max;
    Curve front_coeff_lift_vs_ride_height, rear_coeff_lift_vs_ride_height;
    // wing settings, DRS, slipstream, brake duct and radiator terms, ride-height clamps
};

struct Chassis {                           // Chassis
    float mass;                            // kg
    float moi_x, moi_y, moi_z;             // moments of inertia
    float com_y, com_z;                    // centre of mass
};

}
```

The other parts work the same way: `gearbox` (ratios R, N, 1st to 9th, final drive, shift times), `fuel_tank`, `brake_ecu` (brake torques, ABS and traction control curves), `steering`, `core_aero` (slipstreaming), `differential`, `suspension` (springs, dampers, anti-roll bars, camber and toe, and the tyre and brake records per axle), `turbo`, `tuning` (the setup ranges), `assists`, `plank`, `tyre_damage` and `body_damage`.

A key the handling file doesn't have keeps the game's default.

## When a change reaches the car

The game reads a handling file once when the file loads. It then builds each car from it when the car is put on track, which happens at every session load. A handling layer applies at that build, so:

- A layer reaches every car built after it, in every year, without a restart. Changing an option reaches the next session.
- A car that's already on track keeps its values until it's built again. Use `cars::live` to change it now.
- The game rebuilds a car's parts from the file during some events, such as handing a car from the player to the AI. Your layer is part of every rebuild, so it survives them.
- With Equal Performance on, every car is built from the base car's file, so a layer on the base car reaches all of them.

The core logs each build:

```text
[cars] built vehicle 0 (game/ferrari_2025, team game/ferrari, driver game/charles_leclerc): 23 parts, 3 layers
[cars] vehicle 0 handling: F1WingDamage /speed_threshold 3 -> 2 (william09leo.heavier-damage-realism)
```

## Changing a car live

`cars::live` gives you each car on track during a session: its index, its vehicle record, its team and driver, and its parts. `car.control()` reads who drives it, human or AI, and its controller flags, fresh on every call (see [Who drives a car](./sessions#who-drives-a-car)). Writes take effect on the car's next physics step. The core decodes and encodes the game's scrambled values, recomputes anything the game derives from the value you changed, and puts your value back if the game rebuilds the car.

A per-team pace slider:

```cpp
#include <swofty/systems/cars.hpp>
#include <swofty/events.hpp>

using namespace swofty;

SWOFTY_ON_LOAD
{
    // every time a car is built, and whenever the player moves a slider
    events::on<cars::events::Built>().after([](const cars::events::Built &e, const events::Context &) {
        float pace = option<float>(std::format("pace_{}", e.car.team().id()));   // 0.95 .. 1.05
        e.car.f1_aero().lift_scale = pace;                                       // downforce, every step
        e.car.f1_aero().drag_scale = 2.0f - pace;
    });
}
```

Engine power and fuel, live:

```cpp
for (cars::live::Car &car : cars::live::cars()) {
    if (car.is_ai())
        continue;
    car.engine().revs_power_spline.scale_y(1.05f);   // the core recomputes max torque and the limiter
    car.fuel_tank().fuel_mass = 50.0f;               // kg, next step
    car.ers().max_deploy_power = 160'000.0f;         // W, next step
}
```

Some parts change on the next step with nothing else to do: downforce and drag scales, fuel mass, ERS values, tyre wear factors, suspension and tyre records. Others are derived by the game, and the core re-runs the game's own code for them after your write: engine torque and the limiter, gear tables, body drag and wing coefficients, mass and inertia, and brake torque per wheel.

The game resets a car's parts to the handling file when it rebuilds them, and it re-applies every R&D upgrade after an upgrade is fitted. That also sets the fuel mode back to Standard. The core repeats your live writes after both. `cars::events::Reset` tells you when it happened.

Damage settings are read on every impact, so a live change to `wing_damage` or `suspension_damage` reaches the next hit. Lowering `full_detach_scale` during a race can take off a wing that's already damaged on its next hit.

## R&D upgrades

An R&D upgrade isn't a number in an array. Each upgrade type is a block inside the handling file, such as `radUpgradeEngine` or `radUpgradeFrontDownforce`, with the same sections and keys as the rest of the file. When the career fits an upgrade, the game reads the block on top of the car's values, scaled by the development progress `p` from 0 to 1:

- `*`: the value is multiplied by `(v - 1) * p + 1`.
- `+` and `-`: `v * p` is added or taken away.
- `/`: the value is divided by `(v - 1) * p + 1`.
- No operator, or `=`: the value is set to `v`, whatever the progress.

A block applies once per car build. The stock 2025 blocks change, among others:

```text
radUpgradeEngine            Engine/RevsPowerSpline +, FuelTank/FuelBsfcMin x0.9
radUpgradeFrontDownforce    F1Aero/FrontCoeffLiftMultiplier x1.26, FrontWingCoL1 x1.1
radUpgradeDrag              F1Aero/BodyDragCoeffMin x0.87, BodyDragCoeffMax x0.89
radUpgradeWeightReduction   Chassis/Mass -18
radUpgradeTyreWear          Suspension Tyre/UpgradeWearFactor x0.825
radUpgradeWearICE           Engine/WearICERate x0.5
```

A block is a list of changes, each a section, a key, an operator and a value:

```cpp
namespace swofty::cars {
struct UpgradeChange { std::string section, key; char op; float value; };   // op: '*', '+', '-', '/', '='
struct RadUpgrade { std::string name; std::vector<UpgradeChange> changes; };
}
```

Edit a block with the same layer, or add a new one for a department of your own:

```cpp
cars::handling.layer(f1, [](cars::Handling &h) {
    cars::RadUpgrade &engine = h.upgrade("radUpgradeEngine");        // the block, created if it's missing
    engine.set("FuelTank", "FuelBsfcMin", '*', 0.85f);               // a bigger fuel saving at full progress
    engine.set("Engine", "WearICERate", '*', 0.8f);
});
```

## Setup

The setup screen's sliders pick a point between a minimum and a maximum from the `tuning` part, then write it into the differential, suspension, brakes, engine braking, centre of mass and fuel load. The wing sliders are the exception: their range is the wing's own setting count in `f1_aero`. Widen or move a range with a layer:

```cpp
cars::handling.layer(f1, [](cars::Handling &h) {
    h.tuning.differential_on_throttle_multiple_min = 0.4f;   // a more open differential at the low end
    h.tuning.brake_bias_max = 0.7f;
});
```

## AI cars

AI cars use the same handling file, the same physics and the same R&D upgrades as the player. The difficulty setting changes how the AI drives, not its car: there's no hidden grip, power or drag boost. The AI-only values are in the handling file, so you can change them like any other:

- `brake_ecu`: the AI's stability control (`asc_strength_for_ai`, off in 2025) and `brake_migration_torque_ai`.
- `suspension` tyres: the AI's slip limits and grip factors on a wet or fast tyre.
- An extra tyre wear factor the player's car doesn't have.
- AI cars run their tyre contact every third physics step, where the player's runs every step.

For how the AI drives, see [AI](/std/systems/ai).

## Readiness

The parts, their fields and how they're stored are mapped from the game's code and checked independently. An in-game trace confirmed that cars are built once at session load and torn down when it ends, that the game rebuilds every car twice while a session loads and rebuilds one car when control of it changes, and the live values of engine speed, fuel, ERS, tyre wear, mass and the aero coefficients. Still to confirm in game: DRS, the fuel-mix and setup controls, career R&D upgrades, and how a car's index matches its participant.

Credit: the front wing damage, suspension damage, fuel ECU and ERS layouts were cross-checked against findings shared by Will (william09leo), the author of Heavier Damage.
