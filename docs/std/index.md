---
title: The 1.6 standard library
description: The design for Swofty Launcher 1.6. The registry is the center of the standard library. Addons register records and layers in code, hooks and services connect them, and the core applies everything live in the game.
---

# The 1.6 standard library

**Status:** proposal. **Ships as:** Swofty Launcher 1.6.0, `SWOFTY_API_VERSION` 9.

The standard library is built around one idea: **the registry**. A registry is shared, typed data that any addon can provide, read and change, in code. Progressive Career's 2017 line-up is a record in the `career.season` registry: a what-if addon changes who drives for Mercedes, and Career Editor reads the result. Everything else in the standard library fills a registry, changes it, reacts to it or applies it to the game.

- **Each addon registers its own keys.** A Grand Prix and Time Trial season, a car, an engine sound and a career season are each a record under the id of the addon that brings it, added with `add` in `SWOFTY_REGISTER`.
- **Records are bases others build on.** Another addon can extend a base in place with a `layer`, such as an engine sound for the 2017 Ferrari everywhere it races. Or it can make a `variant` of it under its own key, such as an "F1 2017 (V10)" box in the year picker.
- **Everything is live in the game.** The core applies each change when the game reads that data. The manifest only names the addon and its dependencies. When an addon is packed, `swofty dev pack` also writes `registry.lock.json`, the list of what it registers, for the launcher and the Addon Browser to show. Nothing is applied from that file.
- **The standard library only ever grows.** Nothing in 1.6 removes or changes what addons made for 1.5 and earlier rely on, and nothing is removed in a later version either (see [Versioning](/std/versioning)).

## The registry in one example

<Split>

Progressive Career knows who drove for which team in each season. It adds that to the `career.season` registry, one record per year, under its own id.

<template #example>
<Example root="swofty.progressive" code-file="native/src/register.cpp" files="swofty.json, data/years.json, native/progressive.dll">
<template #code>

```cpp
#include <swofty/events.hpp>
#include <swofty/registry.hpp>
#include <swofty/systems/career.hpp>

#include "history.hpp"   // progressive::history(), built from data/years.json

using namespace swofty;

SWOFTY_REGISTER
{
    for (const progressive::Year &y : progressive::history().years())
        career::seasons.add(std::to_string(y.year), y.to_season());   // "swofty.progressive/2017"
}

SWOFTY_ON_LOAD
{
    events::on<career::events::SeasonStart>().after([](const career::events::SeasonStart &e, const events::Context &ctx) {
        if (auto s = career::seasons.get(career::seasons["swofty.progressive/" + std::to_string(e.year)]))
            career::apply_lineup(*s);    // the core's verified seat code, not Progressive's own
    });
}

SWOFTY_ADDON()
```

</template>
</Example>
</template>
</Split>

<Split>

A companion addon changes the 2017 grid without touching Progressive's files. Its edit runs on Progressive's season, in load order.

<template #example>
<Example root="tails.whatif-history" code-file="native/src/whatif.cpp" data-file="registry/layers.json" files="swofty.json, native/whatif.dll">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/career.hpp>
#include <swofty/systems/drivers.hpp>

using namespace swofty;

static Ref<drivers::Identity> d(const char *key) { return drivers::identities[std::string("game/") + key]; }

SWOFTY_REGISTER
{
    career::seasons.layer(career::seasons["swofty.progressive/2017"], [](career::Season &s) {
        s.teams.at(1).seats = { d("fernando_alonso"), d("valtteri_bottas") };
        s.teams.at(4).seats = { d("lewis_hamilton"), d("stoffel_vandoorne") };
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
        { "slot": 4, "seats": ["game/lewis_hamilton", "game/stoffel_vandoorne"] }
      ]
    }
  }
}
```

</template>
</Example>
</template>
</Split>

Any code that needs the grid reads the **effective** value: Progressive's base with every layer applied in load order.

```cpp
// any DLL: read the season the player will race
if (auto s = career::seasons.get(career::seasons["swofty.progressive/2017"]))
    for (const career::SeasonTeam &t : s->teams)
        swofty::log("slot %d: %s and %s", t.slot, std::string(t.seats[0].id()).c_str(), std::string(t.seats[1].id()).c_str());
```

```text
slot 1: game/fernando_alonso and game/valtteri_bottas
slot 4: game/lewis_hamilton and game/stoffel_vandoorne
```

A career freezes each season's line-up, ratings and rules when that season starts. A change to the 2017 grid reaches a running career when it gets to 2017, and a season that has already started keeps what it froze (see [Career snapshots](/std/scopes#career-snapshots)). The save default is dry in 1.6.0: the snapshot is built and sized but written into the save only with `SWOFTY_REGISTRY_SAVE=on`.

## A base someone else builds on

Classic Seasons 2017 adds the F1 2017 Grand Prix and Time Trial season, its cars and its era box under its own keys. An engine sound pack adds its sound under its own key and points the 2017 Ferrari at it. That's a layer on Classic Seasons' record. It copies no files, and as a data file it needs no DLL.

```cpp
// mod.esp
SWOFTY_REGISTER
{
    auto v10 = sounds::engine_sounds.add("v10_screamer", { .bundle = file("sounds/v10_screamer.bdl"),
                                                           .settings_from = sounds::engine_sounds["game/ferrari_2025"] });
    cars::vehicles.layer(cars::vehicles["swofty.classic-seasons-2017/ferrari"], [=](cars::Vehicle &v) { v.engine_sound = v10; });
}
```

At the next game start, the core puts the car's and the sound's records into the game database before the database loads, and points the game at the bundle in `mod.esp`'s folder when it opens the sound ([When changes apply](/std/build)).

## The map

The registry and its subfeatures come first. Then the parts that connect addons to each other and to the game.

- **[The registry](/std/registry)**: named, typed collections of records.
  - **Handles**: `Registry<T>` in a header per system, with `add`, `variant`, `provide`, `layer` and `get`.
  - **Records are structs**: `SWOFTY_RECORD`, `Ref<T>` between records, `Keyed` lists, and `extra` for the round trip.
  - **Ids**: `<addon id>/<key>`, the shared `venue/`, `person/` and `constructor/` ids, and stat ids kept per install.
  - **Tags**: shared sets such as `c/f1_cars` and `c/street_circuits` that any addon adds records to.
  - **Your own registry**: a struct, an `inline Registry<T>` and a `check`, in a header you ship.
  - **[Keys, bases and variants](/std/bases)**: every addon registers its own keys; bases others extend in place or build variants of; copies become variants.
- **[Providers](/std/providers)**: who fills a registry's base.
  - **`add`**: records you know up front, in `SWOFTY_REGISTER`.
  - **Dynamic providers**: a callback the core calls on a read, cached until `invalidate`.
  - **Shared ids**: a venue, person or constructor goes to the last provider in load order.
- **[Reading](/std/reading)**: getting data out.
  - **Effective values**: the base plus every layer, with `status` when there's nothing.
  - **Origins**: which addon set a field.
  - **Listing**: every record id, or the ids under a prefix.
  - **Watching**: a callback when a record's effective value changes.
- **[Layers](/std/layers)**: how addons change each other's records.
  - **Edits**: a function on the effective value. The core records what it changed.
  - **Precedence**: load order, the same rule as files and mixins.
  - **Validation**: an edit whose result breaks the record drops that layer.
  - **Conflicts**: clashes are logged and reported, never silent.
  - **Selectors**: `where`, `under` and `tagged`, one layer for every matching record, including ones added later. Each runs once per record, and skips the fields a year pack protects.
  - **Where a layer applies**: `.in(...)` limits a layer to a setup mode, a career or a box.
  - **Option-bound layers**: an edit that reads `swofty::option<T>()` re-runs when the option changes.
- **[Scopes and snapshots](/std/scopes)**: how long a change lasts.
  - **Global, profile and career scopes.**
  - **Career snapshots**: each season's values, frozen into the career when the season starts.
- **[Content registries](/std/content)**: Grand Prix and Time Trial season packages, era boxes, cars, engine sounds, liveries, helmets, tracks and identities.
- **[Systems catalogue](/std/systems/)**: every game system's registries, hook points, operations and readiness, and the limits the core enforces for all of them.
- **[Your own systems](/std/systems/custom)**: an addon's own registries, scope, projection, scoped hooks and service; a six-compound tyre system others extend.
- **[Dependencies and guards](/std/dependencies)**: needing addons, registries and single records like tracks; what happens when they're missing; guards; careers that remember what they were made with.
- **[Events](/std/events)**: the lifecycle from boot to the chequered flag, with the context of everything, scoped and overridable by content owners.
- **[When changes apply](/std/build)**: at boot, when content becomes active, at a session load or on every read; how files reach the game; the core's appliers; registering while the game runs.
- **[Versioning](/std/versioning)**: how registries, hook payloads and tables change over time, only ever growing.
  - **Schema versions**: a minor adds; a major adds a new version next to the old one.
  - **The tolerant reader rule**: ignore what you don't know.
  - **Every major forever**: adapters between versions.
  - **Status**: experimental, stable, superseded.
  - **ABI and table versions**, and **how a change is made**.
- **[Hooks](/std/hooks)**: one ordered chain per game function.
  - **Game hook points**, owned by the core.
  - **Phases**: `before`, `replace`, `after`.
  - **Addon hook points**: an addon's own extension points.
  - **Living with mixins**: raw mixins chain around hook points.
- **[Services](/std/services)**: one addon calling another, versioned, without `GetProcAddress`.
- **[Operations](/std/operations)**: the core's verbs that apply data to the game.
- **[The launcher side](/std/launcher)**: what the launcher does and doesn't know, editing pages that write an addon's storage, the conflicts view and Send Logs.
- **[Manifest](/std/manifest)**: `registry.json` holds only dependencies; everything else is code.
- **[The Simplified importer](/std/importer)**: imported mods recognised as records others stack on; the parts it doesn't recognise built exactly as the launcher builds any raw file.
- **[Coverage](/design/std-2/coverage)**: every Addon Browser addon and the common imported mods, before and after 1.6.
- **[Compatibility](/std/compatibility)**: how every addon made for 1.5 and earlier keeps working, and how the release proves it.
- **[The C ABI](/std/abi)**: the registration export and the tables under the C++ API.
- **[Rollout and docs](/design/std-2/rollout)**: the releases, and how the public docs are written.
- **[Implementation plan](/design/std-2/implementation)**: how parallel agents build 1.6.0 in rounds.

## Sharing between addons

An addon's own storage and its DLL's exports are private. No other addon can read Progressive's `data/years.json`, and Progressive's DLLs talk to each other through `GetProcAddress` exports nobody else knows about. To share, an addon puts its data in a registry, offers a [hook point](/std/hooks) or publishes a [service](/std/services).

The libraries addons made for 1.5 and earlier use run on registries and hook points:

- Career contents, era boxes, Team HQ tabs, the career wizard, characters and strings are registries, with one rule for scope and conflicts.
- The contract, seat, rating and logo code that Progressive Career and Career Editor each reverse-engineered and wrapped on their own is one set of core hook points and [operations](/std/operations).

Their older calls keep working (see [Compatibility](/std/compatibility)).
