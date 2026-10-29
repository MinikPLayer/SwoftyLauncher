---
title: What you can build
description: What you can make for Swofty Launcher - content, whole game systems, careers, in-game code, launcher pages and imported mods - registered in code on the registry, and where each part of the docs starts.
---

# What you can build

You build **addons**: a folder with a `swofty.json` that players install on the **Addons** page. An addon can add content (a season, a car, a livery, a track, an engine sound), a whole system the game doesn't have (a tyre range, a points system, a career), code that runs in the game, or pages in the launcher. Players can also drop **mods** (`.serps` packages, game-file zips, track zips) on the importer, which turns them into addons.

Everything an addon adds goes into **the registry**: shared, typed data every addon can provide, read and build on, registered in code. Start there.

## One addon, start to finish

<Split>

An engine sound pack that gives the 2017 Ferrari a V10. It adds its sound under its own key, and layers Classic Seasons' 2017 Ferrari to use it. No copied files.

`SWOFTY_REGISTER` runs at every game start, before the game loads its database. The core puts the change into the game live.

The **Without code** tab is the same addon with no DLL: data files in `registry/`.

<template #example>
<Example root="mod.esp" code-file="native/src/esp.cpp" data-file="registry/sounds.json" files="swofty.json, native/build.sh, native/esp.dll, sounds/v10_screamer.bdl, registry/layers.json">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/sounds.hpp>
#include <swofty/systems/cars.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    auto v10 = sounds::engine_sounds.add("v10_screamer", {
        .bundle = file("sounds/v10_screamer.bdl"),
        .settings_from = sounds::engine_sounds["game/ferrari_2025"],
    });

    cars::vehicles.layer(cars::vehicles["swofty.classic-seasons-2017/ferrari"], [=](cars::Vehicle &v) {
        v.engine_sound = v10;
    });
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
// registry/sounds.json: your sound, under your own key
{ "engine_sound": { "v10_screamer": { "bundle": "sounds/v10_screamer.bdl", "settings_from": "game/ferrari_2025" } } }
```

```json
// registry/layers.json: a layer on Classic Seasons' 2017 Ferrari
{ "vehicle": { "swofty.classic-seasons-2017/ferrari": { "engine_sound": "mod.esp/v10_screamer" } } }
```

</template>
</Example>
</template>
</Split>

`swofty.json` names the addon, its DLL and what it needs. Nothing it registers goes in a manifest:

```json
{
  "schema_version": 1,
  "id": "mod.esp",
  "name": "V10 Engine Sounds",
  "version": "1.0.0",
  "author": "you",
  "category": "cars",
  "compat": { "game": "f1_25", "launcher": ">=1.6.0" },
  "requires": [ { "id": "swofty.classic-seasons-2017" } ],
  "operations": { "native": [ { "dll": "native/esp.dll" } ] }
}
```

The data-file version leaves out `operations`: the core finds `registry/` by its folder name.

```sh
native/build.sh
swofty addons install ./mod.esp
swofty addons enable mod.esp
swofty run --apply    # Time Trial, F1 2017, Ferrari
```

`swofty/loader.log`:

```text
[registry] register: mod.esp (esp.dll) 1 engine_sound, 1 layer on vehicle (early stage, 0.4 ms)
[registry] vehicle swofty.classic-seasons-2017/ferrari: edit by mod.esp changed /engine_sound -> mod.esp/v10_screamer
[registry] applied at boot: engine_sound 1 (1 bundle path redirected to mod.esp), vehicle 1 (3 database records)
```

## The parts of the docs

- **[Guides](/make/)**: build one real addon from an empty folder to publishing: a livery, an engine sound pack, a what-if history editor, a track, a year pack, a career mode, or a system of your own.
- **[The registry](/std/)**: what every addon builds on. Records under your own keys, providers, layers, bases and variants, scopes, when changes apply, versioning.
- **[Game systems](/std/systems/)**: one page per system the game has: cars, liveries, teams, drivers, helmets and suits, sounds, tracks, tyres, power units, sessions and weekends, weather, AI, career, UI and strings, graphics. Each says what you can provide and change, the hook points, and the limits.
  - **[Your own systems](/std/systems/custom)**: bring a system the game doesn't have, and let others extend it.
- **[Connecting addons](/std/events)**: events through the whole game, hooks on game functions, services between addons, the core's operations, and dependencies with guards that stop players cleanly when something's missing.
- **[In-game code](/reference/addons/native-plugins)**: engine plugins (Windows DLLs) with the mixin system and the in-game libraries: events, overlay, telemetry, database reads, storage, career, popups, the race HUD, strings.
- **[Launcher plugins](/reference/addons/launcher-api)**: pages in the launcher, importers, build steps and `swofty` commands, and how a page saves settings your in-game code applies as a layer.
- **[Mods and importing](/std/importer)**: what the importer recognises in a mod, and how to make `.serps` packages and track zips.
- **[Packaging and publishing](/reference/addons/publishing)**: `swofty.json`, `registry.json`, and publishing to the Addon Browser.
- **[Reference](/reference/cli)**: the command line, the C ABI, compatibility with addons made for 1.5 and earlier, build operations, the game database, ERP archives and the Engine API.

## Tools

```text
swofty              the command line: install and enable addons, import mods, build a profile, run the game,
                    follow its logs, restore the game folder                         /reference/cli
Swofty UI           browse, render and edit menu scenes (.idf), export checked scenes; releases ship it as
                    bin/swofty-ui                                                    /reference/ui/swofty-ui
tools/gamedb.py     dump the game database, get or set one value by path             /reference/gamedb/tools
tools/map_check.py  check a profile built with tracks against its game, using the build's plan.json
tools/idf.py        print a menu scene's elements and edit them from the command line /reference/ui/idf
```

The Python tools are in the repository's `tools/` folder. Releases don't ship them, except Swofty UI.

## Older addons

Addons made for 1.5 and earlier keep working: operations in `swofty.json`, engine plugins, launcher plugins and imported mods. See [Compatibility](/std/compatibility).
