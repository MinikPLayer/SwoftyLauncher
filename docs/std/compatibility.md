---
title: Compatibility
description: How addons made for 1.5 and earlier run on the 1.6 standard library, how each of their libraries maps onto registries and hooks, the older event names, and the release gate that checks it.
---

# Compatibility

**An addon made for 1.5 or earlier runs on 1.6 with no change and no rebuild.** Nothing it uses is removed or changes meaning, in 1.6 or in any later version.

For example, Classic Seasons 2016 calls `swofty::era::add_box` and `swofty::lang::set` from its 1.5 DLL. On 1.6 the box and the string appear exactly as before, and 1.6 addons can also see and change them.

## An older addon on 1.6

<Split>

The calls an older addon makes don't change. On 1.6, each of its libraries keeps its header and table, and stores what it's given in a registry or a hook point, in the calling addon's name:

- `swofty::era::add_box` provides a `content::era_boxes` record.
- `swofty::lang::set` writes the calling addon's global layer of a `ui::strings` record.

A call has the same visible result as on 1.5, and 1.6 addons can see and change it.

<template #example>
<Example root="swofty.classic-seasons-2016" code-file="native/src/classic_seasons.cpp" files="swofty.json, native/build.sh, lang/en.json">
<template #code>

```cpp
// an addon made for 1.5, unchanged: built against the 1.5 headers, API v8
#include <swofty/mixin.hpp>
#include <swofty/era.hpp>
#include <swofty/lang.hpp>

SWOFTY_ON_LOAD
{
    swofty::era::add_box({ .entrant_list = "cs_f1_2016", .title = "lng_cs_era_2016" });
    swofty::lang::set("lng_team_mer_title", "Mercedes-AMG Petronas", "*");
}

SWOFTY_ADDON()
```

</template>
</Example>
</template>
</Split>

What the 1.6 core does with those two calls, written as the 1.6 API:

```cpp
// provided in swofty.classic-seasons-2016's name: id "swofty.classic-seasons-2016/cs_f1_2016"
content::era_boxes.add("cs_f1_2016", {
    .package = content::packages["swofty.classic-seasons-2016/cs_f1_2016"],   // the package of entrant list cs_f1_2016
    .title = "lng_cs_era_2016", .image = {}, .order = 0,
});

// swofty.classic-seasons-2016's global layer, in its load-order position
ui::strings.layer(ui::strings["game/lng_team_mer_title"], [](ui::String &s) {
    s.text["*"] = "Mercedes-AMG Petronas";      // "*": every language
});
```

```text
[registry] era.box swofty.classic-seasons-2016/cs_f1_2016: provided by swofty.classic-seasons-2016 (era::add_box, API v8)
[registry] lang.string game/lng_team_mer_title: global layer by swofty.classic-seasons-2016 (lang::set, API v8)
```

A 1.6 addon can then reorder or hide that box with a layer, without the older addon knowing:

```cpp
content::era_boxes.layer(content::era_boxes["swofty.classic-seasons-2016/cs_f1_2016"], [](content::EraBox &b) {
    b.order = 2016;
});
```

When two addons set the same string key, load order decides, and the core's conflicts report lists it. For a single addon the result is identical to 1.5.

**The other libraries:**

- **Career contents:** registering a content provides a `career::contents` record.
- **Team HQ tabs:** the `hub_tab` manifest field provides a `hub.tab` record.
- **Career wizard:** each call writes the addon's global layer of its data group's `career::wizards` record.
- **Characters:** each call writes a layer of the character's `career::characters` record.
- **Events:** `subscribe` attaches an `after` handler to the event's hook point (see [Events](#events)).
- **Settings:** options become `addon.option` records, which the launcher and ADDON SETTINGS already edit.
- **Storage:** an addon's own files stay private. Each launcher profile has its own copy (see [Profiles](/std/launcher#profiles)).
- **Mixins:** they chain with hook points (see [Living with mixins](/std/hooks#living-with-mixins)).

## Events

`swofty::events::subscribe(name, fn, user, token)` attaches an `after` handler, by the names addons made for 1.5 use. Each name fires with its event:

- `flow.state`, `career.starting`, `career.loaded`, `career.hub`, `career.unloaded`, `session.start` and `race.start` are the same events by the same names.
- `screen` is `menu.screen`.
- `pause` and `resume` are `game.pause` and `game.resume`.
- `state` is `session.state.enter`.
- `lap` is `session.lap`.
- `flag` is `session.flag`.
- `results` is `session.results.finalise`.
- `session.end` is `session.end`.

The 1.5 core accepts the last eight (`session.end`, `lap`, `flag`, `state`, `screen`, `pause`, `resume`, `results`) and never fires them. An older addon subscribed to one of them receives it on 1.6. The full list of events is on [Events](/std/events#the-lifecycle).

## What stays exactly as it is

- **`SwoftyApi`:** append-only. Version 9 adds `registry`, `hooks` and `services` after `era`, and a check like `api->version >= 8` still passes.
- **Exported tables:** `SwoftyCoreHub`, `SwoftyCoreHud`, `SwoftyCoreEra`, `SwoftyCoreCareerContent` and the rest keep their names, layouts and versions.
- **Result codes, feature gates and `available()`** don't change.
- **Loading:** an older DLL has no `SwoftyAddonRegister`, and it loads at the stage its manifest says.
- **Mixins:** chains, priorities, load order and the conflict rules don't change.
- **Manifests:** every `swofty.json` field keeps its meaning, and `schema_version` stays 1.
- **Addon commands:** a command an addon adds to the `swofty` CLI never becomes unreachable. Every addon command also runs as `swofty <addon id>:<command>`. When a built-in command has its name (1.6 adds `dev`, `test`, `registry` and `careers`), or another addon took it first, that form runs it, and `swofty --help` lists it there. No addon on the Addon Browser has a command with one of those names.
- **Builds:** 1.6 records never change what the launcher builds: the core applies them in the game. A profile of older addons builds to the same game files. Only the core's own files and its version stamp differ, as with every launcher update, so the first Play after updating rebuilds once.
- **Database room:** the core applies the database edits of build operations first and counts them on their own. 1.6 records are admitted against the room that's left. A 1.6 addon over that room is refused, and never removes an edit an older addon made.

## Hook points can't drop an older addon

- The core installs a game hook point's mixin only when an addon subscribes to it. With no subscriber, the game function is patched exactly as on 1.5.
- When it's installed and collides with an overwrite, the overwrite wins and the core's mixin is the one dropped, whichever version the overwriting addon was made for. Only the hook point's subscribers lose the point, and they're told (see [Hooks](/std/hooks#living-with-mixins)).
- The core installs hook-point mixins at the window stage, after the mixin conflict pass. An overlap with a mixin installed at the early stage never drops an older addon.

## Progressive Career's exports

Progressive keeps `ProgressiveRealTeam`, `ProgressiveFollowTeam`, `ProgressiveFollowMode` and `ProgressiveNamesYear` as `GetProcAddress` exports in every later release, next to the `progressive.history` service.

## The release gate

1.6.0 can't be tagged until these checks pass. Each later release runs them again.

**1. Every addon on the Addon Browser, on both cores.** For every published version whose launcher range includes 1.5:

```text
tools/compat/run.sh
  for each addon version on the Addon Browser:
    download it
    build a profile with only it (plus its requirements) on the 1.5 core  -> files-1.5/, build.json
    build the same profile on the 1.6 core                                 -> files-1.6/, build.json
    compare: the game files byte for byte, apart from the core's own files and version stamp
    start each build in the Wine harness to the main menu                  -> loader-1.5.log, loader-1.6.log
    compare: the DLLs loaded, the dropped DLLs (none new), the mixin chains (identical apart from
             hook points, which only appear with a subscriber), each addon's start lines
  report: one line per addon version, pass or the first difference
```

**2. Shim tests.** For each older table, a fixture addon makes every call, and the test compares the resulting registry records and game-side effects with what 1.5 produced.

**3. In game.** The existing checks run on the 1.6 core with the 1.5 builds of those addons:

- a Progressive Career 1.2 career start (Follow your driver, 2016) to Team HQ;
- a Classic Seasons era box to Time Trial;
- a Team HQ tab;
- a wizard route.

**4. Database room.** The core sizes the game database's table before the game loads it, and the profiles that ran out of room before 1.5.11 (every Classic Seasons year plus community tracks) boot with every record. The launcher also ships a fallback: after a boot the core reports how full the database is, and when the records don't fit, the next launch moves the database edits of build operations to the file route, padded so the game grows its own table.

## Nothing is removed

- Every table, call, manifest field and mixin rule of 1.5 stays, in 1.6 and in every later version.
- When 1.6 has a better way, the older page says so and links to it, and the old way keeps working. Players never see a warning about it.
