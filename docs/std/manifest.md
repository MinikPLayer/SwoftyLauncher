---
title: Manifest
description: swofty.json names the addon; registry.json, next to it, holds what the addon needs, breaks, conflicts with and was once called, and the build operations its records replace; registry.lock.json records what the addon registers. Everything an addon registers is code.
---

# Manifest

An addon names itself in `swofty.json`, the manifest every launcher reads. Everything it registers is code. Two optional files sit next to `swofty.json`:

- **`registry.json`** holds what the addon needs and recommends, the addons it breaks or conflicts with, the ids it was published under before, guards for addons without code, and the build operations its records replace. You write it.
- **`registry.lock.json`** records what the addon's code registers. `swofty dev pack` writes it. You never edit it.

For example, Progressive Career provides ten seasons and a career mode in its DLL. Its `registry.json` says it needs the `career.season` registry, and its lock file lists the ten seasons it provides.

<Split>

Progressive Career provides seasons and ratings, makes its career mode a variant of the stock one, replaces a hook, defines its own hook point and publishes a service. All of it is in the DLL.

Its manifests stay small:

- `swofty.json` names the addon, its version, its options and its build operations;
- `registry.json` only says what Progressive needs and recommends. The launcher checks it before the game starts.

<template #example>
<Example root="swofty.progressive" code-file="native/src/progressive.cpp" files="swofty.json, registry.json, registry.lock.json, native/progressive.dll, data/years.json, api/progressive_api/hooks.hpp, api/progressive_api/history.hpp">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/hooks.hpp>
#include <swofty/services.hpp>
#include <swofty/systems/career.hpp>
#include <swofty/systems/tracks.hpp>

#include <progressive_api/hooks.hpp>     // shipped in api/ for other addons to include
#include <progressive_api/history.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    // its career mode: a variant of the stock 2026 Driver Career
    career::modes.variant("progressive", career::modes["game/f1_career_2026"], [](career::ModeRecord &m) {
        m.data_group = "f1_career_progressive";
        m.save_container = "driver_career_progressive";
        m.title = "lng_progressive_title";
    });

    // every season from its own data/years.json: swofty.progressive/2016 ... /2025
    for (const career::Season &s : progressive::history().seasons())
        career::seasons.add(std::to_string(s.year), s);

    // ratings: computed when read, listed for the ids it knows
    career::ratings.provide(progressive::ratings, progressive::list_ratings);

    // the 2016 Russian Grand Prix needs Sochi; optional, so a missing track drops that round
    progressive::sochi = tracks::venues["venue/sochi"].optional();
}

SWOFTY_ON_LOAD
{
    hooks::replace<career::events::AddActivity>(progressive::keep_contract_off_calendar);
    hooks::define<progressive_api::SeasonApply>();
    services::publish(progressive::kHistory);
}

SWOFTY_ADDON()
```

</template>
</Example>
</template>
</Split>

```json
// swofty.json
{
  "schema_version": 1,
  "id": "swofty.progressive",
  "name": "Progressive Career",
  "version": "1.3.0",
  "author": "Swofty",
  "kind": ["database", "native"],
  "category": "career",
  "compat": { "game": "f1_25", "launcher": ">=1.5.15" },
  "requires": [ { "id": "swofty.classic-seasons", "version": ">=0.6.0" } ],
  "operations": { "native": [ { "dll": "native/progressive.dll", "files": ["data/"] } ] }
}
```

```json
// registry.json: what it needs and recommends, checked before the game starts
{
  "requires": [
    { "registry": "career.season", "version": "^1" },
    { "registry": "career.driver_ratings", "version": "^1" }
  ],
  "recommends": [
    { "record": "track:venue/sochi", "for": "the 2016 Russian Grand Prix" }
  ]
}
```

```text
[registry] register: swofty.progressive (progressive.dll) 1 career.mode, 10 career.season, 1 provider on career.driver_ratings (early stage, 1.8 ms)
```

- **`swofty.json` keeps the fields every launcher reads:** id, name, version, compat, options, operations, launcher. `schema_version` stays 1, and the 1.6 standard library adds no field to it.
- **On launcher 1.5 and earlier,** `registry.json` is never opened and `SWOFTY_REGISTER` is never called. This addon then runs from its build operations and DLLs, which keep their own `years.json` path. On 1.6 it also adds the registry records the other addons build on.
- **`SWOFTY_REGISTER` runs at the early stage.** The loader loads the DLL then and calls its registration. `SWOFTY_ON_LOAD` runs at the stage `swofty.json` asks for: the window stage, unless the DLL's entry sets `"early": true`. Having a `SWOFTY_REGISTER` doesn't change that stage.
- **Headers others include** (`api/progressive_api/hooks.hpp`, `api/progressive_api/history.hpp`) ship in the addon's folder. Its docs page links them.

## The lock file

`swofty dev pack` runs the addon's `SWOFTY_REGISTER` against a recorder, outside the game, and writes `registry.lock.json` into the addon:

```json
// swofty.progressive/registry.lock.json, written by swofty dev pack
{
  "generated_by": "swofty dev pack 1.6.0",
  "provides": {
    "career.mode": [ { "id": "swofty.progressive/progressive", "label": "lng_progressive_title" } ],
    "career.season": [ "swofty.progressive/2016", "swofty.progressive/2017", "...", "swofty.progressive/2025" ]
  },
  "providers": [ "career.driver_ratings" ],
  "needs": [
    { "record": "track:venue/sochi", "policy": "optional" },
    { "record": "track:venue/sepang", "or": ["track:venue/bahrain"], "policy": "optional" }
  ]
}
```

- **What it records:** every id the code registers, with its label, what each record provides (a track's venue), and every reference the code uses, required or optional.
- **Fallbacks:** a reference written `a|b` (`Ref::or_else`), such as `venue/sepang|venue/bahrain`, is one `needs` entry: `record` is the first choice and `or` lists the others. It's met when any of them is provided.
- **Files:** `files` lists the files in the addon's folder that the code names with `file(...)`. The build places them in the game next to the addon's data files, as it does for the files a data file's `Path` fields name.
- **Who reads it:** the launcher, for the check before Play and the importer's **Applies to** picker, and the Addon Browser, to answer "which addon provides `venue/sochi`" and install it.
- **What it isn't:** nothing is applied from it. The game runs `SWOFTY_REGISTER` and applies everything live.
- **Options:** an edit that reads an option is recorded with the option's default.

## Everything else is code

The manifests have no entry for anything an addon registers:

```cpp
// registries: an addon's own registry is a handle and a record struct
struct Preset { std::string name; int start_year = 2016; static constexpr Version version{1, 0}; };
SWOFTY_RECORD(Preset, name, start_year);
inline Registry<Preset> presets{"tails.whatif.presets"};

// provides: base records under your own keys, variants, and dynamic providers
presets.add("real_2016", { .name = "real_2016", .start_year = 2016 });
career::modes.variant("progressive", career::modes["game/f1_career_2026"], edit);
career::ratings.provide(compute, list);

// overrides: layers on anyone's records, at a scope
career::seasons.layer(career::seasons["swofty.progressive/2017"], edit);
career::seasons.layer(career::seasons["swofty.progressive/2017"], edit, Scope::profile);

// select: one layer for every match, now and later
cars::handling.layer(where<cars::Handling>([](const cars::Handling &h) { return h.cls == cars::Class::f1; }), edit);
ai::vehicle_configs.layer(under<ai::VehicleConfig>("swofty.classic-seasons-2017/"), edit);

// when: an edit that reads one of your options re-runs when the player changes it
cars::handling.layer(f1_cars, [](cars::Handling &h) {
    if (swofty::option<std::string>("damage") == "heavy")
        h.damage_scale = 1.6f;
});

// hooks and services: in SWOFTY_ON_LOAD
hooks::replace<career::events::AddActivity>(handler);
hooks::define<progressive_api::SeasonApply>();
services::publish(table);

// guards: in SWOFTY_ON_LOAD
guards::on(guards::Point::career_content_open, check);
```

- **Registries:** [The registry](/std/registry). **Provides:** [Keys, bases and variants](/std/bases), [Providers](/std/providers). **Overrides:** [Layers](/std/layers), [Scopes](/std/scopes).
- **Selectors:** [Selectors](/std/layers#selectors). **Options:** [Option-bound layers](/std/layers#option-bound-layers).
- **Hooks and services:** [Hooks](/std/hooks), [Events](/std/events), [Services](/std/services). **Guards:** [Guards](/std/dependencies#guards).
- **No `editable` flag.** A launcher page writes its addon's own per-profile state, and the addon's code applies it (see [The launcher side](/std/launcher#editing-a-layer-from-a-launcher-page)).

## Options stay in swofty.json

An option stays in `swofty.json`, where ADDON SETTINGS and the launcher find it (see [options](/reference/addons/manifest#options)). An edit reads it with `swofty::option<T>()`:

<Split>

Heavier Damage Realism has one damage level option instead of 16 pre-built copies of every car. Its edit reads the option, and the core re-runs the edit when the player changes the level.

<template #example>
<Example root="william09leo.heavier-damage-realism" code-file="native/src/damage.cpp" files="swofty.json, native/damage.dll">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/cars.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    auto f1 = where<cars::Handling>([](const cars::Handling &h) { return h.cls == cars::Class::f1; });

    cars::handling.layer(f1, [](cars::Handling &h) {
        std::string level = swofty::option<std::string>("damage");   // declared in swofty.json
        if (level == "heavy")
            h.damage_scale = 1.6f;
        else if (level == "brutal")
            h.damage_scale = 2.4f;
    });
}

SWOFTY_ADDON()
```

</template>
</Example>
</template>
</Split>

```json
// swofty.json
{ "options": [ { "id": "damage", "type": "choice", "values": ["realistic", "heavy", "brutal"], "default": "realistic" } ] }
```

## An addon that needs 1.6

Tails' "what if" addon only layers registries, so it does nothing without the 1.6 standard library. It says so with `"compat": { "launcher": ">=1.6.0" }` in `swofty.json`. Launcher 1.5 then says it needs a newer launcher instead of installing something that does nothing.

```json
// swofty.json
{
  "schema_version": 1,
  "id": "tails.whatif-history",
  "name": "What If History",
  "version": "1.0.0",
  "author": "Tails",
  "kind": ["native"],
  "category": "career",
  "compat": { "game": "f1_25", "launcher": ">=1.6.0" },
  "operations": { "native": [ { "dll": "native/whatif.dll" } ] },
  "launcher": {
    "program": { "windows": "bin/whatif.exe", "linux": "bin/whatif" },
    "sections": [ { "id": "history", "title": "What if history" } ],
    "build": true
  }
}
```

```json
// registry.json
{
  "requires": [
    { "registry": "career.season", "version": "^1" },
    { "registry": "career.driver_ratings", "version": "^1" },
    { "service": "progressive.history", "version": ">=1" }
  ]
}
```

Its layers are in its DLL (see [Scopes](/std/scopes)), and its page is in [The launcher side](/std/launcher#editing-a-layer-from-a-launcher-page).

## Requirements in code and in the manifest

<Split>

In code, a requirement is stated where it's used:

- a hard `Ref` is **required**: the core checks it in game, and a guard stops the player with a popup;
- `.optional()` is **recommended**: a missing record drops the element that holds it.

The lock file records both, so the launcher checks them before the game starts. The `registry.json` copy adds a `for` text for players and works for an addon without code.

<template #example>
<Example root="mod.esp" code-file="native/src/esp.cpp" data-file="registry.json" files="swofty.json, registry.lock.json, sounds/v10_screamer.bdl, native/esp.dll">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/cars.hpp>
#include <swofty/systems/tracks.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    // a hard reference: required
    auto ferrari = cars::vehicles["swofty.classic-seasons-2017/ferrari"];

    // optional: recommended
    auto sochi = tracks::venues["venue/sochi"].optional();

    // ...
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
{
  "requires":   [ { "record": "vehicle:swofty.classic-seasons-2017/ferrari" } ],
  "recommends": [ { "record": "track:venue/sochi", "for": "the 2016 Russian Grand Prix" } ]
}
```

</template>
</Example>
</template>
</Split>

## Data-file guards

An addon with code adds guards in code. An addon without code declares them in `registry.json`:

```json
{
  "guards": [
    { "on": "career.start", "mode": "my_team",
      "block": { "title": "My Team not available",
                 "body": "This addon only works in Driver Career. To play My Team, turn the addon off in the launcher, then relaunch." } }
  ]
}
```

See [Guards](/std/dependencies#guards) for the points, `block`, `require` and the code form.

## Build operations that records replace

An addon that also runs on launcher 1.5 keeps the build operations its records cover, in a file of their own, and names that file in `replaced_operations`. Launcher 1.6 leaves the file out of the build, and the core applies the records instead. Launchers 1.5 and earlier never read `registry.json` and build the file.

Classic Seasons 2017 registers its cars, packages and season in `registry/content.json`. `db/records.json` holds the rows those records make, and `db/season.json` the rest of its database operations:

```json
// swofty.classic-seasons-2017/swofty.json
{
  "operations": {
    "database": ["db/season.json", "db/records.json"],
    "assets": ["lang/season.json", "assets/cars.json", "assets/sounds.json"]
  }
}
```

```json
// registry.json
{ "replaced_operations": ["db/records.json"] }
```

```text
registry: F1 2017 GP/TT Addon: db/records.json is applied by the core from the addon's registry records
```

- **When a file is left out:** the core in the launcher's loader applies data-file records (its `features.json` lists `registry_rows`), and the addon has data files in `registry/`. Otherwise the launcher builds the file. A build that leaves a file out loads the core at the early stage (`"early_stage": true` in `swofty/assets.json`), where it applies the records before the game reads its database.
- **The same rows:** the records make the rows the file makes, with the same labels and stat ids. Classic Seasons pins each record's `stat_id` to the id its row has in 0.6.4. Careers saved with 0.6.4 load with the same ids.
- **Paths** are the ones `swofty.json`'s `operations` lists, compared without a leading `./`.

## The fields of registry.json

- **`requires`:** an array of what this addon needs, each one of:
  - `{ "registry": "career.season", "version": "^1" }`
  - `{ "service": "progressive.history", "version": ">=1" }`
  - `{ "hook": "career.calendar.add_activity", "version": ">=1" }`
  - `{ "record": "vehicle:swofty.classic-seasons-2017/ferrari", "version": ">=1.0.0" }` (`version` optional)

  Play stops while a `registry` or `record` entry is missing. The core checks `service` and `hook` entries in game, once every addon has loaded, and shows a missing one as a status line on the addon's tile. See [Dependencies and guards](/std/dependencies).
- **`recommends`:** the same entries, optional, each with a `for` shown to players. They never stop Play.
- **`breaks`:** `[{ "id", "version", "why" }]`, addon versions this addon doesn't work with. Play stops while one is installed and on.
- **`conflicts`:** the same shape. The launcher warns before Play.
- **`aliases`:** `[{ "id" }]`, ids this addon was published under before a rename. The Addon Browser accepts an alias only from the same Browser author. See [Renamed addons](/std/dependencies#renamed-addons).
- **`saves`:** `{ "careers": "loaded" }` records the addon in every career loaded while it's on.
- **`guards`:** declarative guards, for addons without code.
- **`replaced_operations`:** operation files of `swofty.json` (`["db/records.json"]`) that the addon's data files replace. Launcher 1.6 leaves them out of the build when its core applies the records. See [Build operations that records replace](#build-operations-that-records-replace).

## Addon requirements stay in swofty.json

`swofty.json`'s `requires` (`[{id, version}]`, see [requires](/reference/addons/manifest#requires-conflicts-load-after)) means "this addon needs that addon". `registry.json`'s `requires` adds finer dependencies: a single track, a registry version, a service, a hook point. The launcher checks both, in the same place, with the same messages.
