---
title: Keys, bases and variants
description: Every addon registers its own keys. Any record can be a base that other addons extend in place with a layer, or build variants of with Registry::variant.
---

# Keys, bases and variants

Every addon adds records under **its own keys**. Any record is a **base**: other addons can extend it in place with a [layer](/std/layers), or build a **variant** of it under their own key. A variant is a separate record that starts from the base's values and changes some of them.

Classic Seasons 2017 adds the F1 2017 Grand Prix and Time Trial season under `swofty.classic-seasons-2017/`. An engine sound pack layers Classic Seasons' 2017 Ferrari with a V10 sound, or makes "F1 2017 (V10)", a variant that shows as a second box next to the real one.

## Every addon registers its own keys

<Split>

Classic Seasons 2017 registers the F1 2017 Grand Prix and Time Trial season: a content package, its cars, and the era box that shows it in the year picker. `add` puts each one under the addon's id and returns a typed `Ref` that the next record points at.

<template #example>
<Example root="swofty.classic-seasons-2017" code-file="native/src/register.cpp" data-file="registry/content.json" files="swofty.json, native/cs2017.dll, sounds/cs17_ferrari.bdl">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/cars.hpp>
#include <swofty/systems/content.hpp>
#include <swofty/systems/sounds.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    auto sound = sounds::engine_sounds.add("ferrari", {
        .bundle = file("sounds/cs17_ferrari.bdl"),
        .settings_from = sounds::engine_sounds["game/ferrari_2025"],
    });
    auto ferrari = cars::vehicles.add("ferrari", { .label = "cs17_ferrari", .engine_sound = sound });

    auto f1_2017 = content::packages.add("f1_2017", {
        .title = "lng_cs_era_2017",
        .entrant_list = "cs_f1_2017",
        .teams = { { .key = "ferrari", .vehicle = ferrari } /* ...and nine more */ },
    });
    content::era_boxes.add("f1_2017", { .package = f1_2017, .order = 2017 });
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
{
  "engine_sound": {
    "ferrari": { "bundle": "sounds/cs17_ferrari.bdl", "settings_from": "game/ferrari_2025" }
  },
  "vehicle": {
    "ferrari": { "label": "cs17_ferrari", "engine_sound": "swofty.classic-seasons-2017/ferrari" }
  },
  "content.package": {
    "f1_2017": {
      "title": "lng_cs_era_2017",
      "entrant_list": "cs_f1_2017",
      "teams": [ { "key": "ferrari", "vehicle": "swofty.classic-seasons-2017/ferrari" } ]
    }
  },
  "era.box": {
    "f1_2017": { "package": "swofty.classic-seasons-2017/f1_2017", "order": 2017 }
  }
}
```

</template>
</Example>
</template>
</Split>

Many addons share one registry, and each owns the records it adds:

```text
content.package                                   <- one shared registry
  swofty.classic-seasons-2016/f1_2016             <- Classic Seasons 2016's key
  swofty.classic-seasons-2017/f1_2017             <- Classic Seasons 2017's key
  community.f1-2012/f1_2012                       <- another addon's season, its own key
  mod.esp/f1_2017_v10                             <- a variant built on Classic Seasons' 2017 (see below)
era.box
  swofty.classic-seasons-2017/f1_2017             <- the year picker's box for that package
  mod.esp/f1_2017_v10                             <- a second box: the variant shows as its own entry
engine_sound
  swofty.classic-seasons-2017/ferrari             <- cs17_ferrari.bdl and its settings
  mod.esp/v10_screamer                            <- a sound another addon brings
```

- **The owner's id comes first.** `add("f1_2017")` in `swofty.classic-seasons-2017` makes `swofty.classic-seasons-2017/f1_2017`. No addon can add a record under another addon's id.
- **Game names are owned too.** An addon owns the names it registers for the game, such as its career content's data group (`f1_career_progressive`) and its entrant lists (`cs_f1_2017`). A record from another addon that claims one is refused:

```text
[registry] content.package community.f1-2017-redux/f1_2017: refused: entrant list cs_f1_2017 is owned by
           swofty.classic-seasons-2017 (use a layer to change another addon's record)
```

- **The core's records** describe the stock game and use `game/`: `game/ferrari_2025`, `game/f1_2026`.
- **Everyone else can still change your records** through [layers](/std/layers). Owning a key means you provide its base. It doesn't lock the record. To keep history values such as pace and AI tiers out of other addons' selectors, [protect those fields](/std/layers#protected-fields).

## Bases

A **base** is any record other addons build on. Nothing marks it, and every record can be one. Classic Seasons' F1 2017 package is the base for everything else about 2017 in Grand Prix and Time Trial. Another addon builds on it in one of two ways.

### Extend it in place, with a layer

A [layer](/std/layers) changes the base itself. Everything that uses the base sees the change: the 2017 box, Progressive Career's 2017 season, and anything else that reads it.

```cpp
// mod.esp: the 2017 Ferrari gets the V10 sound everywhere the 2017 package is used
content::packages.layer(content::packages["swofty.classic-seasons-2017/f1_2017"], [=](content::Package &p) {
    p.teams.at("ferrari").engine_sound = v10;
});
```

### Build a variant {#build-a-variant-with-extends}

<Split>

A **variant** is a new record, under your own key, that starts from a base and changes it. The base stays as it was. The variant shows up as its own entry, here a second box in the year picker.

`variant` runs your edit on the base's **effective value**: the base with every layer on it applied. This variant is the 2017 package with three sounds changed and its own title. Every team it doesn't touch, and every other field, comes from the base.

In a data file, a variant is a record with `"extends"`, and its value is a merge patch on the base.

<template #example>
<Example root="mod.esp" code-file="native/src/esp.cpp" data-file="registry/content.json" files="swofty.json, native/esp.dll, sounds/v10_screamer.bdl">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/content.hpp>
#include <swofty/systems/sounds.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    auto v10 = sounds::engine_sounds.add("v10_screamer", {
        .bundle = file("sounds/v10_screamer.bdl"),
        .settings_from = sounds::engine_sounds["game/ferrari_2025"],
    });

    auto base = content::packages["swofty.classic-seasons-2017/f1_2017"];
    auto v10s = content::packages.variant("f1_2017_v10", base, [=](content::Package &p) {
        p.title = "lng_esp_era_2017_v10";
        for (const char *team : { "mercedes", "ferrari", "redbull" })
            p.teams.at(team).engine_sound = v10;
    });

    content::era_boxes.add("f1_2017_v10", { .package = v10s, .order = 2017 });
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
{
  "engine_sound": {
    "v10_screamer": { "bundle": "sounds/v10_screamer.bdl", "settings_from": "game/ferrari_2025" }
  },
  "content.package": {
    "f1_2017_v10": {
      "extends": "swofty.classic-seasons-2017/f1_2017",
      "title": "lng_esp_era_2017_v10",
      "teams": [
        { "key": "mercedes", "engine_sound": "mod.esp/v10_screamer" },
        { "key": "ferrari",  "engine_sound": "mod.esp/v10_screamer" },
        { "key": "redbull",  "engine_sound": "mod.esp/v10_screamer" }
      ]
    }
  },
  "era.box": {
    "f1_2017_v10": { "package": "mod.esp/f1_2017_v10", "order": 2017 }
  }
}
```

</template>
</Example>
</template>
</Split>

## Copies are variants

When your addon needs its own version of a stock record, make it a variant of that record. Progressive Career needs its own career mode, which is the 2026 Driver Career's mode with its own data group. Classic Seasons needs its own entrant lists. As variants, they get every change to the original: when Simulate Sessions changes the stock game mode, Progressive's mode gets the change too.

A copy of a stock row made with the manifest's [operations](/std/manifest) keeps the values the original had when it was copied. Later changes to the original don't reach it.

```cpp
// swofty.progressive: its career mode, a variant of the stock 2026 Driver Career
SWOFTY_REGISTER
{
    career::modes.variant("progressive", career::modes["game/f1_career_2026"], [](career::ModeRecord &m) {
        m.data_group = "f1_career_progressive";
        m.save_container = "driver_career_progressive";
        m.title = "lng_progressive_title";
    });
}
```

```text
[registry] career.mode swofty.progressive/progressive: variant of game/f1_career_2026
           + layer from kizi.simulate-sessions on game/f1_career_2026 flows in
```

The core writes the variant into the game database as its own rows with its own stat ids, the same rows a copy makes, with every change on the base included.

## How a variant resolves

```text
effective(mod.esp/f1_2017_v10) =
    effective(swofty.classic-seasons-2017/f1_2017)    the base with all of ITS layers
  + the variant's own edit                            run on that value
  + layers on mod.esp/f1_2017_v10                     in load order
```

- **The base's layers flow into the variant.** If a livery addon layers a new Williams livery onto the 2017 base, the V10 variant gets it too, unless the variant's edit sets Williams' livery itself.
- **A selector runs once per record.** A [selector](/std/layers#selectors) is a layer on every record that matches a condition. When it matches both the base and the variant, the variant gets it once, at the selector's own place in load order, and not a second time through the base. A selector that multiplies downforce by 1.1 gives the variant 1.1, not 1.21.
- **A variant is in every [tag](/std/registry#tags) its base is in.** Classic Seasons' 2017 Ferrari is a variant of `game/ferrari_2025`, so it's in `c/f1_cars`.
- **Variants can extend variants.** `someone/f1_2017_v10_wet` can be a variant of `mod.esp/f1_2017_v10`. The chain resolves from the root.
- **A cycle is refused**, and so is a chain deeper than 8. The record that closes the loop fails validation.
- **A missing base** leaves the variant unresolved when the addon that provides the base is off. `get` returns nothing, `status` returns `Status::no_record`, and the core names the missing addon.

The base `Ref` in `variant` is a hard reference. When the base's addon is off, the core's guard stops the load with a popup that names the addon to turn on (see [Dependencies and guards](/std/dependencies)). To have the launcher stop Play with the same message before the game starts, add the record to `registry.json`:

```json
{ "requires": [ { "record": "content.package:swofty.classic-seasons-2017/f1_2017" } ] }
```

## Extend or vary?

- **Layer the base** when your change should apply wherever the base is used: a sound pack that fixes a car's engine note, a livery update, a corrected driver number.
- **Make a variant** when players should be able to choose between the original and yours: a "2017 with V10s" box next to the real 2017, or a "what if 2017" grid in its own career content.

Both only add. Neither stops the base, or any other addon's change, from working.
