---
title: Add a track
description: Build mod.hockenheim with Austria as its base, Hockenheimring as its venue, track assets, per-track data, and career dependency protection.
---

# Add a track

You'll build `mod.hockenheim`, a track addon that adds Hockenheimring with its own world, AI data, names, lap count and fuel. Players see Hockenheim in every Grand Prix and Time Trial box it fits, and career calendars that race the German Grand Prix pick it through its venue, `venue/hockenheimring`.

## The folder

The C++ and **Without code** versions share the track assets; choose one registration version.

```text
mod.hockenheim/
├── swofty.json
├── registry.json
├── native/                         # C++ version
│   ├── build.sh
│   ├── hockenheim.dll
│   └── src/
│       └── track.cpp
├── registry/                       # Without code version
│   └── track.json
├── track/
│   └── hockenheim.erp
├── mipmaps/
│   └── hockenheim_d.tif.mipmaps
└── ui/
    ├── images.json
    └── minimap.json
```

The world, textures, AI data and menu images are yours to make. This guide covers registering them.

## 1. Register Hockenheim

<Split>

Write `native/src/track.cpp`, or use `registry/track.json` from **Without code**. [Track inheritance](/std/inheritance#tracks) carries Austria's data into Hockenheim and carries Hockenheim into each box's base track list.

<template #example>
<Example root="mod.hockenheim" code-file="native/src/track.cpp" data-file="registry/track.json" files="swofty.json, registry.json, native/build.sh, native/hockenheim.dll, track/hockenheim.erp, mipmaps/hockenheim_d.tif.mipmaps, ui/images.json, ui/minimap.json">
<template #code>

```cpp
#include <string>

#include <swofty/registry.hpp>
#include <swofty/systems/tracks.hpp>
#include <swofty/systems/ui.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    ui::strings.add("lng_hockenheim_title", {
        .text = { { "*", "Hockenheimring" } },
    });
    ui::strings.add("lng_hockenheim_gp", {
        .text = { { "*", "German Grand Prix" } },
    });

    auto hockenheim = tracks::tracks.add("hockenheim", {
        .code = "hockenheim",
        .name = "Hockenheimring",
        .base = tracks::tracks["game/austria"],
        // No slot: allocate a new track row.
        .provides = { tracks::venues["venue/hockenheimring"] },
        .names = {
            .title = Text::key("lng_hockenheim_title"),
            .gp = Text::key("lng_hockenheim_gp"),
            .acronym = "HOC",
        },
        .files = file("track/"),
        .length_metres = 4574,
        .corners = 17,
        .latitude = 49.3278f,
        .longitude = 8.5658f,
        // stat_id stays automatic and is pinned for the install.
    });

    tracks::data.layer(
        under<tracks::TrackData>("mod.hockenheim/hockenheim/"),
        [](tracks::TrackData &d) {
            // Menus read the names on TrackData.
            d.names.title = Text::key("lng_hockenheim_title");
            d.names.gp = Text::key("lng_hockenheim_gp");
            d.names.acronym = "HOC";
            d.laps = 67;
            d.fuel.burn_per_lap = 1.62f;
            d.pit.speed_limit = 80;
        });

    for (const char *set : {
        "f1_2025", "sprint_f1_2025", "f1_2026", "sprint_f1_2026"
    }) {
        tracks::data.layer(
            tracks::data[std::string("mod.hockenheim/hockenheim/") + set],
            [](tracks::TrackData &d) {
                d.tyres = {
                    .softer = "C4",
                    .medium = "C3",
                    .harder = "C2",
                };
            });
    }

    // Both modes use the supplied 22-slot, 11-garage world.
    for (const char *mode : { "f1", "swofty_f1" }) {
        tracks::layers.add(std::string("hockenheim_") + mode, {
            .track = hockenheim,
            .mode = mode,
            .world = {
                "environment_package/tracks/hockenheim",
                "hockenheim_f1_26",
            },
            .grid = 22,
            .garages = 11,
        });
    }
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
{
  "lang.string": {
    "lng_hockenheim_title": {
      "text": { "*": "Hockenheimring" }
    },
    "lng_hockenheim_gp": {
      "text": { "*": "German Grand Prix" }
    }
  },
  "track": {
    "hockenheim": {
      "code": "hockenheim",
      "name": "Hockenheimring",
      "base": "game/austria",
      "provides": ["venue/hockenheimring"],
      "names": {
        "title": "lng_hockenheim_title",
        "gp": "lng_hockenheim_gp",
        "acronym": "HOC"
      },
      "files": "track/",
      "length_metres": 4574,
      "corners": 17,
      "latitude": 49.3278,
      "longitude": 8.5658
    }
  },
  "track.data": {
    "$select": [
      {
        "ids": "mod.hockenheim/hockenheim/*",
        "patch": {
          "names": {
            "title": "lng_hockenheim_title",
            "gp": "lng_hockenheim_gp",
            "acronym": "HOC"
          },
          "laps": 67,
          "fuel": { "burn_per_lap": 1.62 },
          "pit": { "speed_limit": 80 }
        }
      }
    ],
    "mod.hockenheim/hockenheim/f1_2025": {
      "tyres": { "softer": "C4", "medium": "C3", "harder": "C2" }
    },
    "mod.hockenheim/hockenheim/sprint_f1_2025": {
      "tyres": { "softer": "C4", "medium": "C3", "harder": "C2" }
    },
    "mod.hockenheim/hockenheim/f1_2026": {
      "tyres": { "softer": "C4", "medium": "C3", "harder": "C2" }
    },
    "mod.hockenheim/hockenheim/sprint_f1_2026": {
      "tyres": { "softer": "C4", "medium": "C3", "harder": "C2" }
    }
  },
  "track.layer": {
    "hockenheim_f1": {
      "track": "mod.hockenheim/hockenheim",
      "mode": "f1",
      "world": {
        "asset_group": "environment_package/tracks/hockenheim",
        "eri": "hockenheim_f1_26"
      },
      "grid": 22,
      "garages": 11
    },
    "hockenheim_swofty_f1": {
      "track": "mod.hockenheim/hockenheim",
      "mode": "swofty_f1",
      "world": {
        "asset_group": "environment_package/tracks/hockenheim",
        "eri": "hockenheim_f1_26"
      },
      "grid": 22,
      "garages": 11
    }
  }
}
```

</template>
</Example>
</template>
</Split>

## 2. Name the addon and its requirement

<Split>

Write `swofty.json` and `registry.json`. The [manifest](/std/manifest) names the DLL, while the record requirement lets the launcher check Austria before Play.

<template #example>
<Example root="mod.hockenheim" data-file="swofty.json" files="registry.json, native/src/track.cpp, native/build.sh, native/hockenheim.dll, track/hockenheim.erp, mipmaps/hockenheim_d.tif.mipmaps, ui/images.json, ui/minimap.json">
<template #data>

```json
{
  "schema_version": 1,
  "id": "mod.hockenheim",
  "name": "Hockenheimring",
  "version": "1.0.0",
  "author": "you",
  "category": "tracks",
  "compat": {
    "game": "f1_25",
    "launcher": ">=1.6.0"
  },
  "operations": {
    "native": [
      { "dll": "native/hockenheim.dll" }
    ]
  }
}
```

```json
// registry.json
{
  "requires": [
    { "record": "track:game/austria" }
  ]
}
```

</template>
</Example>
</template>
</Split>

For **Without code**, remove `operations` and the `native/` folder; the core discovers [data providers](/std/providers) in `registry/`.

## 3. Supply the world and AI data

The `hockenheim_f1_26` world in step 1 says it has 22 grid slots and 11 garages. Those must be real objects in your world files: the numbers describe the world, they don't create anything. [Worlds, slots and AI lines](/std/systems/tracks#worlds-slots-and-ai-lines) describes the records that pick the world.

Supply Hockenheim's baked AI data for the car packages you test, using `ai/<package folder>/<asset_filename>/*.erp`; [AI packs](/std/systems/ai) determine which baked data each car reads.

A new track also needs the stack-built keys listed under [track limits](/std/systems/tracks#readiness-and-limits):

```text
CareerHubFirstVisit_N
CareerHubLastVisit_N
TrackHighlights_N
TrackVideo_N
```

## 4. Check the boxes that inherit it

<Split>

[Each box's track list](/std/systems/tracks#a-track-is-in-every-year) offers Hockenheim wherever it fits: enough garages for the box's teams, room for its grid, AI data for its cars, and Time Trial support where the box has Time Trial. A box where it doesn't fit leaves it out. To hide it from a box where it fits but you don't want it, set `offered` to false.

<template #example>
<Example root="mod.hockenheim" code-file="native/src/track.cpp" data-file="registry/layers.json" files="swofty.json, registry.json, registry/track.json, track/hockenheim.erp">
<template #code>

```cpp
// Optional, inside SWOFTY_REGISTER: hide Hockenheim from one box.
tracks::per_package.layer(
    tracks::per_package[
        "swofty.classic-seasons-2016/f1_2016/hockenheim"
    ].optional(),
    [](tracks::PackageTrack &t) {
        t.offered = false;
    });
```

</template>
<template #data>

```json
{
  "track.per_package": {
    "swofty.classic-seasons-2016/f1_2016/hockenheim?": {
      "offered": false
    }
  }
}
```

</template>
</Example>
</template>
</Split>

Test each box you care about:

- **Team count:** the selected world has a garage for every team.
- **Grid size:** it has enough grid slots and pit boxes for the session.
- **AI data:** every participating car resolves suitable baked data.
- **Time Trial:** entering, driving and finishing the session work.

The supplied world covers up to 22 grid slots and 11 garages; a My Team world needs its own assets with 24 slots and 12 garages.

Time Trial on added tracks can crash or freeze, and community tracks crash with more than 27 cars. Test both before you publish.

## 5. Let a career choose the venue

<Split>

A career addon's calendar names `venue/hockenheimring`, and gets whichever enabled track provides that venue. When two tracks provide it, the later one in load order wins. This [calendar round](/std/systems/tracks#a-calendar-that-uses-it-optionally) belongs in the career addon's calendar, outside the track addon.

<template #example>
<Example root="swofty.classic-seasons-2016" code-file="native/src/calendar.cpp" data-file="registry/calendar.json" files="swofty.json">
<template #code>

```cpp
// Entry in the calendar's .rounds list.
{
    .round = 12,
    .race = Text::key("lng_gp_germany"),
    .venue = tracks::venues["venue/hockenheimring"].optional(),
},
```

</template>
<template #data>

```json
{
  "round": 12,
  "race": "lng_gp_germany",
  "venue": "venue/hockenheimring?"
}
```

</template>
</Example>
</template>
</Split>

```text
[registry] career.calendar swofty.classic-seasons-2016/f1_2016: round 12 venue/hockenheimring -> mod.hockenheim/hockenheim
```

## 6. Make a track zip without C++

<Split>

Package the assets with a root `manifest.json`, then import `hockenheim.zip`. The [track importer](/std/systems/tracks#imported-tracks) creates `mod.hockenheim`, its file-placement operation and `registry/track.json`.

<template #example>
<Example root="hockenheim.zip" data-file="manifest.json" files="hockenheim.erp, mipmaps/hockenheim_d.tif.mipmaps, ui/images.json, ui/minimap.json">
<template #data>

```json
{
  "track_code": "hockenheim",
  "track_name": "Hockenheimring",
  "version": "1.0.0",
  "author_name": "you",
  "erp": "hockenheim.erp",
  "activation": {
    "copy_from_track": "austria"
  },
  "copy_directories": [
    {
      "source": "mipmaps/",
      "destination": "asset_groups/environment_package/tracks/hockenheim/mipmaps/"
    }
  ],
  "ui": {
    "schema_version": 1,
    "images_file": "ui/images.json",
    "minimap_file": "ui/minimap.json"
  }
}
```

</template>
</Example>
</template>
</Split>

```sh
swofty mods import hockenheim.zip
swofty build
```

Import installs and enables the addon in the active profile. The imported package remains under `track/`, alongside `ops/track.json`; preserve the generated manifest and operations.

A [track zip's display name](/reference/mods/tracks#the-track-s-name) doesn't supply menu strings, and the importer never guesses a venue from a track's code name. Add the **Without code** records from step 1 to the imported addon's `registry/track.json`, including `"provides": ["venue/hockenheimring"]`, and keep any other records the importer wrote.

## Try it

Build the C++ version from inside `mod.hockenheim`, then run the install commands from its parent folder. Skip the native build for **Without code**; an imported zip is already installed and enabled.

`native/build.sh` comes from the [addon template](/reference/addons/template).

```sh
# Inside mod.hockenheim, for the C++ version:
native/build.sh
```

```sh
# From the folder containing mod.hockenheim:
swofty addons install ./mod.hockenheim
swofty addons enable mod.hockenheim
swofty run --apply
```

Open Grand Prix, choose a supported box and select Hockenheim. Check the displayed name, start a race, drive through the pit lane and confirm the AI completes laps.

Look for these lines in `swofty/loader.log`:

```text
[registry] track mod.hockenheim/hockenheim: base game/austria, new row, provides venue/hockenheimring
[registry] applied at session load: track.data mod.hockenheim/hockenheim/f1_2026 (laps 67, fuel 1.62, tyres C4/C3/C2)
```

The pinned-id line records the allocation for this install; its number and date vary:

```text
[tracks] hockenheim: stat id 43 (pinned since 2026-10-27)
```

These lines confirm registration and track-data application. Verify world loading, garages, AI and each supported mode in game.

## When something's missing

A missing `requires` entry stops Play and names the requirement; a missing hard reference leaves its record unresolved and can hide or guard the affected content. An optional calendar venue drops that round when the career starts, as described under [missing references](/std/dependencies#when-a-reference-is-missing).

```text
[registry] career.calendar swofty.classic-seasons-2016/f1_2016: round 12 dropped (venue/hockenheimring: no
           enabled track provides it; optional); round 16 races at mod.bahrain24/sepang
```

Once a career has raced Hockenheim, its [dependency record](/std/dependencies#careers-remember-what-they-were-made-with) retains the providing addon, resolved track and pinned stat id. The core stores it in the career's `swofty_registry` container and mirrors it to `<game>/swofty/careers/index.json`.

```json
{
  "addons": [
    {
      "id": "mod.hockenheim",
      "version": "1.0.0",
      "need": "^1.0.0",
      "why": "calendar round 12: track mod.hockenheim/hockenheim"
    }
  ],
  "records": [
    {
      "ref": "track:venue/hockenheimring",
      "resolved": "mod.hockenheim/hockenheim",
      "stat_id": 43,
      "used": "calendar round 12"
    }
  ]
}
```

This is an excerpt of career-owned data, not a file to ship. Removing or disabling `mod.hockenheim` makes the launcher warn that the career cannot load; the core's [dependency guard](/std/dependencies#guards) stops Continue with a “Missing addons for this career” popup directing the player to restore the addon.

A career that skipped Hockenheim records it under `dropped`; enabling the track later does not make that career depend on it.

For a career identified only after loading from the save-slot list, the popup tells the player to quit without saving and restore the addon. The launcher's session backup protects recovery.

## Publish

Package the tested addon and follow [Publishing](/reference/addons/publishing) to publish `mod.hockenheim`.