---
title: Layers
description: Edits, selectors, tags, where a layer applies, protected fields, option-bound changes, precedence, validation and conflicts. How addons change each other's registry records in code.
---

# Layers

A **layer** is one addon's change to a record it doesn't own. In code, a layer is an **edit**: a function that gets the record's **effective value** (the provider's value with every earlier layer applied) and changes it. The provider's own record is never touched. Your change sits on top, at a [scope](/std/scopes) that says how long it lasts.

<Split>

Tails' "what if" addon gives Alonso the 2017 Mercedes, fills the empty eleventh slot with Manor, and removes Gasly's round 15 debut.

The edit gets Progressive's 2017 season as a `career::Season`, with every earlier layer already applied. The core runs it in load order and records which fields it changed.

<template #example>
<Example root="tails.whatif-history" code-file="native/src/whatif.cpp" data-file="registry/layers.json" files="swofty.json, native/whatif.dll">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/career.hpp>
#include <swofty/systems/drivers.hpp>
#include <swofty/systems/teams.hpp>

using namespace swofty;

static Ref<drivers::Identity> d(const char *key) { return drivers::identities[std::string("game/") + key]; }

SWOFTY_REGISTER
{
    career::seasons.layer(career::seasons["swofty.progressive/2017"], [](career::Season &s) {
        s.teams.at(1).seats = { d("fernando_alonso"), d("valtteri_bottas") };
        s.teams.at(4).seats = { d("lewis_hamilton"), d("stoffel_vandoorne") };
        s.teams.at(11) = { .slot = 11, .team = teams::identities["game/manor"],
                           .seats = { d("pascal_wehrlein"), d("esteban_ocon") } };
        s.changes.erase(15, 8, 1);       // round 15, slot 8, seat 1: Kvyat keeps his seat
    });
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
{
  "career.season": {
    "swofty.progressive/2017": {
      "teams": [
        { "slot": 1, "seats": ["game/fernando_alonso", "game/valtteri_bottas"] },
        { "slot": 4, "seats": ["game/lewis_hamilton", "game/stoffel_vandoorne"] },
        { "slot": 11, "team": "game/manor", "seats": ["game/pascal_wehrlein", "game/esteban_ocon"] }
      ],
      "changes": [
        { "round": 15, "slot": 8, "seat": 1, "$delete": true }
      ]
    }
  }
}
```

</template>
</Example>
</template>
</Split>

```text
[registry] register: tails.whatif-history (whatif.dll) 1 layer on career.season (early stage, 0.2 ms)
[registry] career.season swofty.progressive/2017: edit by tails.whatif-history changed
           /teams/slot=1/seats, /teams/slot=4/seats, /teams/slot=11, /changes/round=15,slot=8,seat=1 (removed)
```

## Edits

The base, from Progressive, and the effective value after Tails' edit:

```text
                       base (swofty.progressive)                 effective
teams/slot=1           mercedes  hamilton, bottas                mercedes  alonso, bottas
teams/slot=4           mclaren   alonso, vandoorne               mclaren   hamilton, vandoorne
teams/slot=11          -         (empty)                         manor     wehrlein, ocon
changes                round 15 slot 8 seat 1: kvyat -> gasly    (none)
```

- **An edit changes only what it touches.** `team` in slots 1 and 4 is kept. Every other team, and every field the edit doesn't set, comes through as it was.
- **`Keyed` lists are matched by key.** `teams.at(11)` finds slot 11, or appends it. `changes.erase(15, 8, 1)` removes the element with that key.
- **The core diffs each edit.** It compares the value before and after, by field and by key, and records each changed field's origin (see [Origins](/std/reading#origins)).
- **Code addons write edits. Data files write merge patches,** with the same meaning (see [Without code](#without-code)).

## Writing a layer

Write a layer in `SWOFTY_REGISTER`, which runs at every game start. You can also write one at any time while the game runs, from `SWOFTY_ON_LOAD` or later:

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/career.hpp>

using namespace swofty;

void give_alonso_the_mercedes(int year)
{
    auto id = career::seasons["swofty.progressive/" + std::to_string(year)];
    career::seasons.layer(id, [](career::Season &s) {
        s.teams.at(1).seats[0] = drivers::identities["game/fernando_alonso"];
    });
}

void undo(int year)
{
    career::seasons.clear_layer(career::seasons["swofty.progressive/" + std::to_string(year)]);
}
```

```text
[registry] layer from tails.whatif-history on career.season swofty.progressive/2017 registered at run time;
           careers that haven't started their 2017 season get it when it starts
```

- **One layer per record per scope.** Calling `layer` again on the same record and scope replaces your edit. It doesn't stack on it. Put every change to one record in one edit.
- **A layer written while the game runs takes effect the next time the game reads that data.** The core logs when (see [When changes apply](/std/build)). A career that has already frozen a season keeps that season as it was (see [Career snapshots](/std/scopes#career-snapshots)).
- **Boot data reaches the game only from `SWOFTY_REGISTER`.** The game reads a registry with `Applies::boot` once, when its database loads. A layer on one written later can't reach the database in this session. It reaches the next game start only when `SWOFTY_REGISTER` writes it again.
- **From a launcher page,** the page saves the player's choice in the addon's storage, and the addon's code applies it as a profile layer. See [The launcher side](/std/launcher#editing-a-layer-from-a-launcher-page).

## Selectors

<Split>

A layer can target every record that matches a **selector**, instead of one id. It reaches matching records from every provider: stock records, a year pack's own records, records from addons the layer's author has never heard of, and records added later.

Here the selector is a **tag**. A tag is a shared, named set of records. The core defines the shared tags under `c/`, such as `c/f1_cars` on `vehicle` and `c/street_circuits` on `track`, and any addon can add its own records to one (see [Tags](/std/registry#tags)). `tagged(&ai::VehicleConfig::vehicle, ...)` matches every AI config whose car is in `c/f1_cars`, including F1 1990's cars, which are clones of the 2026 package.

<template #example>
<Example root="kizi.ai-tweaks" code-file="native/src/ai_tweaks.cpp" data-file="registry/layers.json" files="swofty.json, native/ai_tweaks.dll">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/ai.hpp>
#include <swofty/systems/cars.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    // more aggressive AI in every F1 car: stock, from year packs, or added later
    auto f1 = tagged(&ai::VehicleConfig::vehicle, cars::vehicles.tag("c/f1_cars"));

    ai::vehicle_configs.layer(f1, [](ai::VehicleConfig &c) {
        c.racecraft.overtake_aggression = 0.85f;
        c.racecraft.defend_aggression = 0.8f;
    });
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
{
  "ai.vehicle_config": {
    "$select": [
      {
        "tag": { "vehicle": "c/f1_cars" },
        "patch": { "racecraft": { "overtake_aggression": 0.85, "defend_aggression": 0.8 } }
      }
    ]
  }
}
```

</template>
</Example>
</template>
</Split>

```text
[registry] ai.vehicle_config: kizi.ai-tweaks selector (ai_tweaks.cpp:11) matched 612 records
           (519 game/, 80 swofty.classic-seasons-*, 13 swofty.f1-1990/), edit applied to each
```

The forms a selector takes:

```cpp
tagged(cars::vehicles.tag("c/f1_cars"))                                     // every record in a tag
tagged(&ai::VehicleConfig::vehicle, cars::vehicles.tag("c/f1_cars"))       // every record whose Ref field is in a tag
where<ai::VehicleConfig>([](const ai::VehicleConfig &c) { return c.cls == ai::Class::f1; })      // a field equals a value
where<ai::VehicleConfig>([](const ai::VehicleConfig &c) {
    return c.cls == ai::Class::f1 || c.cls == ai::Class::f1_26; })                                  // one of several values
where<career::Season>([](const career::Season &s) { return s.year >= 2016 && s.year <= 2019; })  // a numeric range
under<content::Package>("swofty.classic-seasons-2017/")                                           // ids under a prefix
under<ai::VehicleConfig>("").except({ ai::vehicle_configs["game/safety_car"] })                   // everything but some ids
```

- **`where` runs on each record's base value,** before any layers. The edit then runs on each match.
- **A tag's members are optional.** A member whose record is missing is skipped. A variant is in every tag its base is in.
- **To combine conditions in code,** test them in one `where`: `f1_cars.contains(c.vehicle) && c.cls == ai::Class::f1_26`.
- **Within one addon,** its selector layers apply first, then its layers on single ids. An addon can set a default for every car and then a different value for one.
- **A selector's edit beats a year pack's own value,** field by field, the same as any other layer. The exception is a field the year pack protects (see [Protected fields](#protected-fields)).

### A selector runs once per record

A variant inherits its base's effective value, and a selector often matches both. The core runs the selector's edit on each record once. When a variant resolves, the core skips the run it would inherit from the base, and runs the edit on the variant at the selector's own place in load order. An edit that multiplies a value doesn't compound on variants:

```cpp
// someone.tyre-wear: 20% more tyre wear on every F1 car
cars::vehicles.layer(tagged(cars::vehicles.tag("c/f1_cars")), [](cars::Vehicle &v) { v.tyre_wear_scale *= 1.2f; });
```

```text
vehicle game/ferrari_2025                     tyre_wear_scale 1.0 -> 1.2
vehicle swofty.classic-seasons-2017/ferrari   tyre_wear_scale 1.0 -> 1.2   variant of game/ferrari_2025; the edit ran once
```

### Protected fields

<Split>

An addon can protect fields of its own records. Another addon's selector then skips those fields on those records. Year packs protect their history values, such as each car's pace tier and AI tuning. A generic AI or physics mod then leaves a year's tuned values alone.

`protect` names fields the way `origin` does. In a data file, `"$protect"` goes inside your own record.

<template #example>
<Example root="swofty.classic-seasons-2017" code-file="native/src/cars.cpp" data-file="registry/vehicles.json" files="swofty.json, native/cs17.dll">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/cars.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    auto ferrari = cars::vehicles.variant("ferrari", cars::vehicles["game/ferrari_2025"], [](cars::Vehicle &v) {
        v.label = "cs17_ferrari";
        v.performance_tier = 2;           // 2017 pace
    });

    // other addons' selectors leave the 2017 pace alone
    cars::vehicles.protect(ferrari, { "performance_tier" });
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
{
  "vehicle": {
    "ferrari": {
      "extends": "game/ferrari_2025",
      "label": "cs17_ferrari",
      "performance_tier": 2,
      "$protect": ["performance_tier"]
    }
  }
}
```

</template>
</Example>
</template>
</Split>

```text
[registry] vehicle swofty.classic-seasons-2017/ferrari: selector of someone.pace-equaliser skipped /performance_tier
           (protected by swofty.classic-seasons-2017); its other changes applied
```

- **The selector still runs on the record.** Its changes to protected fields are dropped, and its other changes apply.
- **The player can let a selector through.** ADDON SETTINGS has an "Also change year packs" setting for each addon. With it on, that addon's selectors change protected fields too.
- **A layer on the record itself isn't stopped.** `cars::vehicles.layer(cars::vehicles["swofty.classic-seasons-2017/ferrari"], ...)` can change `performance_tier`, and it competes with other layers on that record by load order.
- **You can protect only your own records.**

## Where a layer applies

<Split>

A layer applies everywhere unless it says where. `.in()` on the layer limits it to a mode, a setup mode, a career's data group or a box. It takes the same selectors as [event scopes](/std/events#scope).

Kizi's AI Tweaks makes the AI more aggressive in Driver Career only. Grand Prix and Time Trial keep the stock AI.

<template #example>
<Example root="kizi.ai-tweaks" code-file="native/src/ai_tweaks.cpp" data-file="registry/layers.json" files="swofty.json, native/ai_tweaks.dll">
<template #code>

```cpp
#include <swofty/events.hpp>
#include <swofty/registry.hpp>
#include <swofty/systems/ai.hpp>
#include <swofty/systems/cars.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    auto f1 = tagged(&ai::VehicleConfig::vehicle, cars::vehicles.tag("c/f1_cars"));

    ai::vehicle_configs.layer(f1, [](ai::VehicleConfig &c) {
        c.racecraft.overtake_aggression = 0.9f;
    }).in(events::mode(events::Mode::driver_career));
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
{
  "ai.vehicle_config": {
    "$select": [
      {
        "tag": { "vehicle": "c/f1_cars" },
        "in": { "mode": "driver_career" },
        "patch": { "racecraft": { "overtake_aggression": 0.9 } }
      }
    ]
  }
}
```

</template>
</Example>
</template>
</Split>

```text
[registry] ai.vehicle_config: layer from kizi.ai-tweaks (in mode driver_career) applied at session load, 612 records
[registry] ai.vehicle_config: layer from kizi.ai-tweaks (in mode driver_career) put back at session end
```

Other places a layer can name:

```cpp
.in(events::setup_mode("swofty.f1-1990/f1_1990"))                 // every session run under F1 1990's setup mode
.in(events::data_group("f1_career_progressive"))                   // Progressive careers
.in(events::content("swofty.classic-seasons-2017/f1_2017"))        // that box and every variant of it
.in(events::any({ events::mode(events::Mode::grand_prix), events::mode(events::Mode::time_trial) }))
```

- **On a registry the game reads at content or session moments** (`Applies::content`, `Applies::session`), the layer applies when matching content becomes active or a matching session loads. The core puts it back when that content or session ends.
- **On a registry read on demand** (`Applies::read`), `get` includes the layer while the selector matches.
- **On a registry the game reads at boot** (`Applies::boot`), the game reads the data once, before any content is active. A layer there with `.in()` is refused:

```text
[registry] vehicle game/ferrari_2025: layer from someone.gp-livery has .in(mode grand_prix), but vehicle applies at boot; refused
```

- **`.in()` and scope are separate.** The [scope](/std/scopes) says how long a layer lasts and which profile or career owns it. `.in()` says where it applies. A layer can have both.

## Option-bound layers

<Split>

An edit that calls `swofty::option<T>()` depends on that option. When the player changes it in ADDON SETTINGS or the launcher, the core runs the edit again with the new value.

One layer replaces 16 pre-built copies of every car, one per damage level. The option itself stays in `swofty.json`, where ADDON SETTINGS and the launcher find it:

```json
{ "options": [ { "id": "damage", "type": "choice", "values": ["realistic", "heavy", "brutal"], "default": "realistic" } ] }
```

<template #example>
<Example root="william09leo.heavier-damage-realism" code-file="native/src/damage.cpp" data-file="registry/layers.json" files="swofty.json, native/damage.dll">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/cars.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    auto f1 = where<cars::Handling>([](const cars::Handling &h) { return h.cls == cars::Class::f1; });

    cars::handling.layer(f1, [](cars::Handling &h) {
        std::string level = swofty::option<std::string>("damage");
        if (level == "heavy")
            h.damage_scale = 1.6f;
        else if (level == "brutal")
            h.damage_scale = 2.4f;
    });
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
{
  "vehicle.handling": {
    "$select": [
      { "where": { "class": "f1" }, "when": { "option": "damage", "is": "heavy" },  "patch": { "damage_scale": 1.6 } },
      { "where": { "class": "f1" }, "when": { "option": "damage", "is": "brutal" }, "patch": { "damage_scale": 2.4 } }
    ]
  }
}
```

</template>
</Example>
</template>
</Split>

The new value takes effect the next time the game reads that data: at once for registries read on demand, at the next session load for session data such as handling, and at the next game start for boot data.

```text
[registry] option damage of william09leo.heavier-damage-realism changed: heavy -> brutal;
           1 selector layer on vehicle.handling re-runs, applies from the next session load
```

## Precedence

The provider's value is the bottom. Every layer beats it, whatever the load order. Load order only decides between layers.

Layers apply in **load order**, the same order that decides which file wins and how mixins chain. Within one addon, its `global` layer comes before its `profile` layer.

```text
load order: swofty.progressive (provider), tails.whatif-history, someone.realism

  base                    slot 1 seats: hamilton, bottas         (swofty.progressive)
  + tails.whatif-history  slot 1 seats: alonso, bottas           (global)
  + tails.whatif-history  slot 1 seats: alonso, jenson_button    (profile)
  + someone.realism       slot 1 seats: alonso, bottas           (global)
  = effective             slot 1 seats: alonso, bottas           someone.realism wins /teams/slot=1/seats
```

- **The player reorders addons in the launcher** to change which layer wins, the same way they reorder addons that change the same file.
- **Reordering decides between layers on the same record.** A layer on a base record doesn't reach a field that a variant sets itself, wherever it sits in the load order. To change that field, layer the variant (see [How a variant resolves](/std/bases#how-a-variant-resolves)).

## Validation

The core validates the **result** of each edit. If it breaks the record's shape, leaves a `Ref` that doesn't resolve, or fails the owner's `check`, that layer is dropped as a whole: every record it touches loses its change. The base and the other addons' layers still apply.

```text
[registry] career.season swofty.progressive/2017: layer of tails.whatif-history (global) dropped:
           /teams/slot=1/seats/0 game/fernando_alonso is also in /teams/slot=4/seats/0 (a driver holds one seat a season)
```

The owner adds its rules with `check`. The core's rule for `career.season`:

```cpp
career::seasons.check([](const career::Season &s, Errors &err) {
    std::set<std::string_view> seen;
    for (const career::SeasonTeam &t : s.teams)
        for (const Ref<drivers::Identity> &d : t.seats)
            if (!d.id().empty() && !seen.insert(d.id()).second)
                err.add("teams/slot=" + std::to_string(t.slot) + "/seats", std::string(d.id()) + " already holds a seat");
});
```

## Conflicts

A conflict is any case where an addon's change doesn't end up in the effective value:

- **Field contest:** two addons' edits set the same field to different values. The later one wins.
- **Dropped layer:** a layer failed validation.
- **Refused layer:** a layer with `.in()` on a boot registry.
- **Protected field:** a selector's change to a field another addon protects.
- **Provider clash:** one id provided twice (see [Providers](/std/providers#one-provider-per-record)).

The core logs each conflict once per session with both addons and the field, and writes the conflicts report at each session load. Send Logs includes it, and the launcher's [conflicts view](/std/launcher#the-conflicts-view) shows the last one.

When one of an addon's layers is dropped or refused, a status line on the addon's tile in ADDON SETTINGS and in the launcher says so. Conflicts never open a popup in game. The only popups are guards that stop a load.

```text
[registry] conflict: career.season swofty.progressive/2017 /teams/slot=1/seats:
           tails.whatif-history sets [game/fernando_alonso, game/jenson_button], someone.realism sets [game/fernando_alonso, game/valtteri_bottas];
           someone.realism wins (later in load order)
```

## Without code

A data file's layer is a [JSON Merge Patch](https://www.rfc-editor.org/rfc/rfc7396) in `registry/layers.json`, keyed by registry name, then the full id of the record it changes. Name the fields to change, leave the rest out, and use `null` to remove a field.

The base:

```json
{
  "data_group": "f1_career_progressive",
  "year": 2017,
  "teams": [
    { "slot": 1, "team": "game/mercedes", "seats": ["game/lewis_hamilton", "game/valtteri_bottas"] },
    { "slot": 4, "team": "game/mclaren",  "seats": ["game/fernando_alonso", "game/stoffel_vandoorne"] },
    { "slot": 11, "team": null, "seats": [] }
  ],
  "changes": [
    { "round": 15, "slot": 8, "seat": 1, "out": "game/daniil_kvyat", "in": "game/pierre_gasly", "kind": "swap" }
  ]
}
```

The layer, from the data tab at the top of this page, gives the same effective value as the edit:

```json
{
  "data_group": "f1_career_progressive",
  "year": 2017,
  "teams": [
    { "slot": 1, "team": "game/mercedes", "seats": ["game/fernando_alonso", "game/valtteri_bottas"] },
    { "slot": 4, "team": "game/mclaren",  "seats": ["game/lewis_hamilton", "game/stoffel_vandoorne"] },
    { "slot": 11, "team": "game/manor", "seats": ["game/pascal_wehrlein", "game/esteban_ocon"] }
  ],
  "changes": []
}
```

- **Keyed lists** (`Keyed` in code, `x-key` in the schema) are matched by key. Slots 1 and 4 merge: their `seats` are replaced and `team` is kept. Slot 11 merges too and gains a team.
- **Lists that aren't keyed** (`seats`) are replaced whole, as in RFC 7396.
- **`"$delete": true`** with an element's key fields removes that element from a keyed list.
- **A key the base doesn't have** is appended.
- **An empty keyed list** changes nothing, and **an element without all its key fields** is an error that drops the whole layer.
- **`"$select"`** holds selector layers, as a list of entries:
  - `where` matches fields (`{ "class": "f1" }`), one of several values (`{ "class": ["f1", "f1_26"] }`) or a numeric range (`{ "year": { "min": 2016, "max": 2019 } }`);
  - `ids` matches a prefix (`"swofty.classic-seasons-2017/*"`, or `"*"` with `"except": ["game/safety_car"]`);
  - `tag` matches a tag's members (`"c/f1_cars"`), or records whose field is in a tag (`{ "vehicle": "c/f1_cars" }`);
  - an entry with more than one of `ids`, `where` and `tag` matches records that pass all of them.
- **`"in"`** in a `$select` entry says where the layer applies: `{ "mode": "driver_career" }`, `{ "setup_mode": "swofty.f1-1990/f1_1990" }`, `{ "data_group": "f1_career_progressive" }` or `{ "content": "swofty.classic-seasons-2017/f1_2017" }`.
- **`"when"`** binds a layer to an option: `is` for one value, `in` for several, `on`/`off` for a toggle.
- **`"$protect"`** inside one of your own records lists the fields other addons' selectors skip.
