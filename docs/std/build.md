---
title: When changes apply
description: Addons run live in the game. The core applies each registered record and layer at the moment the game reads that data, at boot, when content becomes active, at a session load or on every read, and the game reads files from the addon's own folder.
---

# When changes apply

This page tells you when a record or a layer you register reaches the game. A **record** is one item of data in a registry, such as a car or a career season. A **layer** is a change one addon makes to a record. The core applies each one at the moment the game reads that data, and the launcher never needs to know what addons register.

<Split>

One addon, four moments:

- The car's sound goes into the game database at boot.
- The 2017 fuel rule applies when the 2017 package becomes active, and is put back when it ends.
- The season is read through the registry whenever the career asks for it.
- The bundle file is read from the addon's folder when the game opens it.

Nothing here is written into the game folder.

<template #example>
<Example root="mod.esp" code-file="native/src/esp.cpp" data-file="registry/content.json" files="swofty.json, native/esp.dll, sounds/v10_screamer.bdl, registry/layers.json">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/cars.hpp>
#include <swofty/systems/career.hpp>
#include <swofty/systems/content.hpp>
#include <swofty/systems/sessions.hpp>
#include <swofty/systems/sounds.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    // file: when the game opens the bundle's path, it reads this file from the addon's folder
    auto v10 = sounds::engine_sounds.add("v10_screamer", {
        .bundle = file("sounds/v10_screamer.bdl"),
        .settings_from = sounds::engine_sounds["game/ferrari_2025"],
    });

    // boot: vehicle is a database record, applied while the database loads
    cars::vehicles.layer(cars::vehicles["swofty.classic-seasons-2017/ferrari"], [=](cars::Vehicle &v) {
        v.engine_sound = v10;
    });

    // content: applied when the 2017 package becomes active, put back when it ends
    content::packages.layer(content::packages["swofty.classic-seasons-2017/f1_2017"], [](content::Package &p) {
        p.rules.refuelling = false;
        p.rules.race_fuel_kg = 105;
    });

    // read: seasons are read through the registry, every time
    career::seasons.layer(career::seasons["swofty.progressive/2017"], [](career::Season &s) {
        s.teams.at(1).seats[0] = drivers::identities["game/fernando_alonso"];
    });
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
{
  "engine_sound": {
    "v10_screamer": { "bundle": "sounds/v10_screamer.bdl", "settings_from": "game/ferrari_2025" }
  }
}
```

```json
// registry/layers.json
{
  "vehicle":         { "swofty.classic-seasons-2017/ferrari": { "engine_sound": "mod.esp/v10_screamer" } },
  "content.package": { "swofty.classic-seasons-2017/f1_2017": { "rules": { "refuelling": false, "race_fuel_kg": 105 } } },
  "career.season":   { "swofty.progressive/2017": { "teams": [ { "slot": 1, "seats": ["game/fernando_alonso", "game/valtteri_bottas"] } ] } }
}
```

</template>
</Example>
</template>
</Split>

`swofty/loader.log`, from the game start to the end of a Time Trial in the 2017 Ferrari:

```text
[14:13:47.300] trigger: KERNEL32!CreateMutexW from 0x140178667 (.xtls) name 'Codemasters F1 25' on thread 29952, 120 ms after attach; loading addons now (early stage)
[registry] register: mod.esp (esp.dll) 1 engine_sound, 3 layers on vehicle, content.package, career.season (early stage, 0.4 ms)
[registry] vehicle swofty.classic-seasons-2017/ferrari: edit by mod.esp changed /engine_sound -> mod.esp/v10_screamer
[registry] applied at boot: engine_sound 1 (1 bundle path redirected to mod.esp), vehicle 1 (3 database records)
[registry] content swofty.classic-seasons-2017/f1_2017 active: 4 scoped changes applied
[registry] content swofty.classic-seasons-2017/f1_2017 ended: 4 scoped changes put back
```

## The moments

Each registry says when the game reads its data, with `RegistryOptions::applies` (`x-applies` in its schema):

```cpp
inline Registry<Vehicle> vehicles{"vehicle", { .applies = Applies::boot }};
inline Registry<Package> packages{"content.package", { .applies = Applies::content }};
inline Registry<TrackData> data{"track.data", { .applies = Applies::session }};
inline Registry<Season> seasons{"career.season", { .applies = Applies::read, .snapshot = true, .snapshot_budget = 65536 }};
```

- **`boot`:** while the database loads. Database-backed records, such as vehicles, teams, drivers, tracks and modes, go into the game database as the game loads it.
- **`content`:** when content becomes active. A career loads, a Grand Prix or Time Trial box is picked, or a career season starts. Scoped changes apply, and are put back when that content ends.
- **`session`:** at each session load. The session's records apply for that session.
- **`read`:** on every read. Runtime registries, such as seasons, ratings and rules, are read through the registry.
- **Files** (`Path` fields) are read from the addon's folder. Archives are fixed at boot, and loose files are redirected each time the game opens them. See [Files](#files).

The core logs every apply and every put-back.

## At boot

`SWOFTY_REGISTER` runs once per game start, at the early stage, on the loader thread, addon by addon in load order. The core loads every DLL that has a `SWOFTY_REGISTER` at the early stage and runs its registration there. Registering only declares. Nothing is written yet.

`SWOFTY_ON_LOAD` runs at the stage the manifest asks for: at the early stage with `"early": true`, and at the window stage otherwise. Having a `SWOFTY_REGISTER` doesn't change that stage.

A hook point's mixin never drops an older addon, whatever stage its subscriber registered at. When it overlaps an older addon's mixin, the hook point is unavailable and the older addon keeps running.

Then the core resolves every registry (bases, variants, layers, selectors, options), and puts the boot records into the game database while the game loads it.

### Database room

The game makes its value map big enough for the records in the database file before it loads them: 2^20 slots for the stock file, which hold 838,860 records. A record added past that makes the map grow while it loads, and growing frees every record it moves, so the game crashes. The core makes the game reserve room for the records addons add too, from the records they register in `SWOFTY_REGISTER`, before the game loads the database. That sizing is a release requirement for launcher 1.6.0.

Edits from addons made for 1.5 and earlier, and records from 1.6 addons, have separate budgets:

1. The core applies the older addons' database edits first, and counts them.
2. It admits 1.6 records against the room that's left, in load order.
3. A 1.6 record refused for room never removes an older addon's edit.

Until the sizing ships, the room is 838,860 records minus what the game loads, minus a 4,096-record margin: about 92,000 records with the stock database. The core keeps a safety net for that room. After each boot, it reports how full the database is. When older addons' edits and 1.6 records don't fit together, the next launch moves the older addons' database edits to the file route, padded past 838,860 records, so the game reserves a big enough map for the file before it loads it. That costs one restart. The launcher reads that one report, and still never learns what addons register.

```text
[registry] room: older addons' edits 84,112; 1.6 records asked 15,020; room 7,792
[registry] register: someone.megapack (megapack.dll) 4,200 vehicle: 1,180 refused, past the database room (~92k records this game start)
[registry] room report written; the next launch moves older addons' database edits to the file route
```

## When content becomes active

A change to a content-scoped registry applies when its content becomes active, and is put back when that content ends. Content ends when the player picks other content, another mode or another career, or when the career unloads. The put-back runs before the next content's changes apply.

Progressive 1.2.2's fuel patch works this way: the 2017 rules hold while the 2017 package is active, and Grand Prix in 2026 is stock again afterwards.

## On every read

Runtime registries apply nothing up front. Every read goes through the registry and returns the effective value (see [Reading](/std/reading)). Their providers can be dynamic, and they can have [career snapshots](/std/scopes#career-snapshots). An [operation](/std/operations) or an addon's own code applies them to the game.

## Files

A record field of type `Path` (`file("sounds/v10_screamer.bdl")`) names a file in the addon's folder. The game folder stays as the launcher built it. How the game reaches the file depends on how the game opens it:

- **Archives** (track worlds, cars, heads, helmets, liveries, handling files) are fixed at boot. The core patches the game's asset manifest groups while the game parses the manifest. The game then reads the files from the addon's folder, or from the core's overlay archives, until it closes.
- **Loose files opened by path** (engine bundles) are redirected each time the game opens the path, to the addon's own file. This uses the core's file route, which isn't a stable feature yet.
- **Swapping an archive per session** (grid slots for a bigger grid, handling) needs reverse engineering of how the game expands an asset group at each load. Until that's done, these apply at boot.

## The core's appliers

An **applier** turns an effective record into the change the game needs: database rows, a redirected file, an archive edit. The core has one per core registry, in `swofty_core`, and runs it at that registry's moment:

```cpp
// swofty_core: src/std/engine_sound.cpp
void apply(Ref<EngineSound> id, const EngineSound &sound, ApplyCtx &ctx)
{
    std::string bundle = ctx.bundle_name(id);                               // "esp_v10_screamer"
    ctx.redirect("audio/2025/bundles/" + bundle + ".bdl", sound.bundle);    // opens of this path read the addon's file
    db::Row settings = ctx.db_row("audio.audio_bundle_settings", sound.settings_from);
    ctx.db_add_row("audio.audio_bundle_settings", settings.with_label(bundle));
    ctx.note("engine sound %s: bundle %s, settings from %s", id, bundle, sound.settings_from);
}
```

```text
apply     engine_sound "mod.esp/v10_screamer"
            redirect audio/2025/bundles/esp_v10_screamer.bdl -> mod.esp/sounds/v10_screamer.bdl
            db add   audio.audio_bundle_settings (copied from ferrari_2025, bundle esp_v10_screamer)
          vehicle "swofty.classic-seasons-2017/ferrari"
            db set   audio.audio_car_data.<cs17_ferrari switch>.*.bundle_name = esp_v10_screamer
```

**An addon's own registries** are applied by the addon's own code: it reads its effective records and adds core records or calls operations (see [Your own systems](/std/systems/custom)).

**Order:**

- Records and layers apply in load order. A record sits at the position of the addon that provides it, and a layer at the position of the addon that wrote it.
- Imports and live layers are ordered together, by load order, in the game.
- Records and layers act on the game files the launcher built from older addons' build operations.

## While the game runs

Addons can register and layer at any time, not only in `SWOFTY_REGISTER`. On a `content`, `session` or `read` registry, the change takes effect at the next moment the game reads that data, and the core logs when.

A `boot` registry is applied once, while the database loads. An `add`, `variant` or `layer` on a boot registry after that is refused as `late`, and never reaches the database. Register boot data in `SWOFTY_REGISTER`.

```text
[registry] layer from tails.whatif-history on career.season swofty.progressive/2017 registered at run time; applies at the next read (a running career: from its next season start)
[registry] layer from someone.realism on vehicle game/mercedes_2025 refused (late): vehicle applies at boot; register it in SWOFTY_REGISTER (realism.cpp:41)
```

`SWOFTY_ON_LOAD` is where events, hooks and services are set up, and where code registers while the game runs.

## The launcher

The launcher installs addons, builds older addons' operations, checks the dependencies the manifest declares, and starts the game. It never needs to know what addons register.

- **`registry.lock.json`.** `swofty dev pack` runs the addon's `SWOFTY_REGISTER` against a recorder and writes down what it registers: ids, labels and what it provides. The file ships inside the addon. The launcher and the Addon Browser read it to show what an addon adds. Nothing is applied from it. The game applies everything live.
- **Its editing pages** (a "what if" editor) write the addon's own storage, and the addon's code applies that as a layer in the game (see [The launcher side](/std/launcher)).
- **Diagnostics come from the game.** The core writes `registry/effective.json` for Send Logs and the conflicts report at each session load. The launcher can show the last one, and nothing depends on it.
- **Every build path for older addons stays.** Hand-written `operations`, imports and launcher plugin build replies work as they always have. A profile of addons made for 1.5 and earlier builds byte-identical game files (see [Compatibility](/std/compatibility)). For imported mods, see [The Simplified importer](/std/importer).

## What this means for addons

- **Pure data addons stay pure data.** An engine sound pack, a livery pack, a season variant: data files plus their assets, no DLL, no launcher plugin.
- **Nothing gets copied.** A pack that changes one car layers that car's record. It doesn't ship its own copy of Classic Seasons' rows.
- **Changes are live.** An addon that changes things in game, like a "2017 as a base" editor, registers and layers while the game runs.

## Without code

Data files in `registry/` apply at the same moments, by their registry's `x-applies`. The core reads them in game at the early stage, after the addon's DLL (if any) has registered.
