---
title: Make an engine sound pack
description: Build a V10 sound addon that puts its sound on Classic Seasons' 2017 Ferrari and adds an F1 2017 (V10) box of its own.
---

# Make an engine sound pack

You'll build `mod.esp`, an addon that registers a V10 engine sound, puts it on Classic Seasons' 2017 Ferrari, and adds "F1 2017 (V10)": the 2017 season as its own Grand Prix and Time Trial box, with the Mercedes and the Red Bull on the V10 too. The player picks the Ferrari in the F1 2017 box and hears the V10 on track.

The finished addon is `templates/std/engine-sound` in the launcher's repository.

## The folder

```text
mod.esp/
  swofty.json
  registry.json
  sounds/
    v10_screamer.bdl          your bundle; standin.sh copies a stock one for testing
    standin.sh
  native/
    build.sh
    esp.dll
    src/
      esp.cpp
  test/
    register_test.cpp
```

## 1. Add the bundle

Put your V10 sound bundle at `sounds/v10_screamer.bdl`: one you made, or one from a sound mod you have permission to use. Sound mods keep their bundles at `audio/2025/bundles/<maker>_<year>.bdl`, which is how the [importer recognises them](/std/importer#recognisers).

To test the addon before you have one, `sounds/standin.sh` copies the stock 2025 Mercedes bundle from your game folder. The Ferrari then sounds like the Mercedes.

<<< @/../templates/std/engine-sound/sounds/standin.sh{sh}

## 2. Register the sound, the layer and the box

<Split>

`settings_from` copies the stock Ferrari sound's mixing settings, while `tuning_from` copies its engine, era, cabin and exhaust tuning; the V10 audio comes from your bundle ([Sounds](/std/systems/sounds)).

The [layer](/std/layers) changes the 2017 Ferrari's own sound, wherever that record is used: the F1 2017 box, Progressive Career's 2017 season, and the V10 box below. Later layers on the same field win by load order.

"F1 2017 (V10)" is a [variant](/std/bases) of the 2017 package. Every team, seat and field it doesn't change comes from the 2017 package. Its Mercedes and Red Bull drive variants of their cars with the V10, so the real 2017 box keeps their sounds. Its era box sits next to the real one.

<template #example>
<Example root="mod.esp" code-file="native/src/esp.cpp" data-file="registry/content.json" files="swofty.json, registry.json, native/build.sh, native/esp.dll, sounds/v10_screamer.bdl, test/register_test.cpp">
<template #code>

<<< @/../templates/std/engine-sound/native/src/esp.cpp

</template>
<template #data>

```json
// registry/content.json: the same records for an addon without code
{
  "engine_sound": {
    "v10_screamer": {
      "bundle": "sounds/v10_screamer.bdl",
      "settings_from": "game/ferrari_2025",
      "tuning_from": "game/ferrari_2025"
    }
  },
  "vehicle": {
    "swofty.classic-seasons-2017/ferrari": { "engine_sound": "mod.esp/v10_screamer" },
    "mercedes_v10": { "extends": "swofty.classic-seasons-2017/mercedes", "label": "esp_cs17_mercedes", "engine_sound": "mod.esp/v10_screamer" },
    "redbull_v10":  { "extends": "swofty.classic-seasons-2017/redbull",  "label": "esp_cs17_redbull",  "engine_sound": "mod.esp/v10_screamer" }
  },
  "content.package": {
    "f1_2017_v10": {
      "extends": "swofty.classic-seasons-2017/f1_2017",
      "title": { "literal": "F1 2017 (V10)" },
      "entrant_list": "esp_f1_2017_v10",
      "stat_id": "auto",
      "teams": [
        { "key": "mercedes", "vehicle": "mod.esp/mercedes_v10" },
        { "key": "redbull",  "vehicle": "mod.esp/redbull_v10" }
      ]
    }
  },
  "era.box": {
    "f1_2017_v10": { "package": "mod.esp/f1_2017_v10", "title": { "literal": "F1 2017 (V10)" }, "order": 2017 }
  }
}
```

</template>
</Example>
</template>
</Split>

`SWOFTY_REGISTER` runs before the game loads its database. A vehicle's `engine_sound` supplies the audio records the core applies at boot ([Cars](/std/systems/cars)), and a package and its era box apply when the box opens ([Content](/std/content#or-as-a-variant-with-its-own-box)).

To put the V10 on the stock 2025 Ferrari instead, in every year that inherits its sound, layer `cars::vehicles["game/ferrari_2025"]`. A year that sets its own sound keeps it, including Classic Seasons' cars with period sounds. See [How changes reach seasons and careers](/std/inheritance).

## 3. Name the addon and declare its target

<Split>

`swofty.json` identifies the addon and its DLL, and copies `sounds/` into the addon's folder in the game with the DLL. Its `requires` names Classic Seasons 2017, so the launcher stops Play when it's off. `registry.json` requires the records the addon builds on, so Play also stops when an update to Classic Seasons removes them ([Dependencies](/std/dependencies)).

<template #example>
<Example root="mod.esp" data-file="swofty.json" files="registry.json, native/build.sh, native/src/esp.cpp, native/esp.dll, sounds/v10_screamer.bdl, test/register_test.cpp">
<template #data>

<<< @/../templates/std/engine-sound/swofty.json

<<< @/../templates/std/engine-sound/registry.json{json} [registry.json]

</template>
</Example>
</template>
</Split>

## 4. Test it without the game

`swofty dev test` runs `test/register_test.cpp` against the fake core. It checks the sound, the layer on the Ferrari, the two car variants, the package variant and its box. Linked with the core's registry engine, it also checks the effective records: the real 2017 box keeps its Mercedes, and the V10 box drives the V10 variant.

<<< @/../templates/std/engine-sound/test/register_test.cpp

```text
$ swofty dev test
ok    adds mod.esp/v10_screamer
ok    its bundle is the addon's file
ok    one layer on the 2017 Ferrari
ok    the layer puts the V10 on it
ok    F1 2017 (V10) is a variant of the 2017 package
...
1 test, 0 failed
```

`swofty dev pack` runs the same registration against the recorder and writes `registry.lock.json`:

```json
{
  "generated_by": "swofty dev pack 1.6.0",
  "provides": {
    "content.package": [ { "id": "mod.esp/f1_2017_v10", "label": "F1 2017 (V10)" } ],
    "engine_sound": [ "mod.esp/v10_screamer" ],
    "era.box": [ { "id": "mod.esp/f1_2017_v10", "label": "F1 2017 (V10)" } ],
    "vehicle": [ { "id": "mod.esp/mercedes_v10", "label": "esp_cs17_mercedes" },
                 { "id": "mod.esp/redbull_v10", "label": "esp_cs17_redbull" } ]
  },
  "needs": [ { "record": "content.package:swofty.classic-seasons-2017/f1_2017", "policy": "required" }, "..." ]
}
```

## Try it

Build `native/esp.dll` from inside `mod.esp` with `native/build.sh`, which builds the DLL with zig. From the directory containing `mod.esp`, install and enable the addon, then run the game:

```sh
mod.esp/native/build.sh
(cd mod.esp && swofty dev pack)
swofty addons install ./mod.esp
swofty addons enable swofty.classic-seasons-2017
swofty addons enable mod.esp
swofty run --apply
```

Pick the Ferrari in the F1 2017 box in Time Trial, then open the F1 2017 (V10) box next to it. Restart the game after changing the pack.

`swofty/loader.log`:

```text
addon esp.dll: calling SwoftyAddonRegister (API v9) at the early stage
[registry] register: mod.esp 1 layer(s)
```

## When something's missing

If Classic Seasons 2017 is off, the `requires` entry in `swofty.json` stops Play and the launcher names the missing dependency. Turn Classic Seasons back on before launching.

If an update removes or renames the target Ferrari record, the `registry.json` requirement also stops Play and identifies the missing record. Update the layer and its record requirement together to a target the provider defines.

Turning off `mod.esp` removes its sound layer at the next game start, revealing the sound beneath it; a missing vehicle sound reference falls back to the donor's sound ([Missing references](/std/dependencies#when-a-reference-is-missing)).

## Publish

Follow [Packaging and publishing](/reference/addons/publishing) to publish the finished `mod.esp` addon to the Addon Browser.