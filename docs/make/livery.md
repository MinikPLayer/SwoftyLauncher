---
title: Make a livery
description: Build a Red Bull repaint for the stock car, Classic Seasons 2017, or an extra choice in the livery picker.
---

# Make a livery

You'll build `mod.rb-repaint`, a Red Bull repaint, three ways: on the stock Red Bull in every year that inherits its livery, on Classic Seasons 2017's own Red Bull only, or as an extra choice in the livery picker. Pick the one you want; the player sees the repaint on track in Grand Prix and Time Trial.

## The folder

```text
mod.rb-repaint/
  swofty.json
  livery/
    redbull_d.tif.mipmaps
    redbull_m.tif.mipmaps
  archives/
    redbull_repaint.erp
  native/
    build.sh
    livery.dll
    src/
      livery.cpp
  registry/
    livery.json
    layers.json
```

Choose **C++** or **Without code**. The C++ version uses `native/`; the data version uses `registry/` and leaves `operations` out of the manifest. The archive is for the special-livery version. [Textures](/reference/data/textures) covers painting and exporting the texture files.

## 1. Repaint the stock Red Bull

<Split>

Put your stock Red Bull textures in `livery/`, then create the files below. [`cars::Vehicle::model`](/std/systems/cars) points at the car's model files and uses the donor's model when empty. The livery's `vehicle` declares which car it fits; the [layer](/std/layers) makes that car use it.

<template #example>
<Example root="mod.rb-repaint" code-file="native/src/livery.cpp" data-file="registry/livery.json" files="swofty.json, native/build.sh, native/livery.dll, livery/redbull_d.tif.mipmaps, livery/redbull_m.tif.mipmaps, registry/layers.json">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/cars.hpp>
#include <swofty/systems/liveries.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    auto car = cars::vehicles["game/redbull_2025"];

    auto repaint = liveries::liveries.add("repaint", {
        .vehicle = car,
        .textures = file("livery/"),
    });

    cars::vehicles.layer(car, [=](cars::Vehicle &v) {
        v.livery = repaint;
    });
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
// registry/livery.json
{
  "livery": {
    "repaint": {
      "vehicle": "game/redbull_2025",
      "textures": "livery/"
    }
  }
}
```

```json
// registry/layers.json
{
  "vehicle": {
    "game/redbull_2025": {
      "livery": "mod.rb-repaint/repaint"
    }
  }
}
```

</template>
</Example>
</template>
</Split>

Create `swofty.json`:

```json
{
  "schema_version": 1,
  "id": "mod.rb-repaint",
  "name": "Red Bull Repaint",
  "version": "1.0.0",
  "author": "you",
  "category": "cars",
  "compat": {
    "game": "f1_25",
    "launcher": ">=1.6.0"
  },
  "operations": {
    "native": [
      { "dll": "native/livery.dll" }
    ]
  }
}
```

The repaint reaches every year that inherits the stock car's livery. Classic Seasons 2017 keeps the livery its own car sets. See [How changes reach seasons and careers](/std/inheritance).

## 2. Put it on the 2017 car

<Split>

For a repaint that targets only Classic Seasons 2017, replace the previous source or data files with these. Use textures that fit the 2017 car's model, and place this addon after Classic Seasons 2017 in load order.

<template #example>
<Example root="mod.rb-repaint" code-file="native/src/livery.cpp" data-file="registry/livery.json" files="swofty.json, native/build.sh, native/livery.dll, livery/redbull_d.tif.mipmaps, livery/redbull_m.tif.mipmaps, registry/layers.json">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/cars.hpp>
#include <swofty/systems/liveries.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    // Required reference to the year's own car.
    auto car = cars::vehicles["swofty.classic-seasons-2017/redbull"];

    auto repaint = liveries::liveries.add("repaint", {
        .vehicle = car,
        .textures = file("livery/"),
    });

    // Change this car directly, including its own /livery value.
    cars::vehicles.layer(car, [=](cars::Vehicle &v) {
        v.livery = repaint;
    });
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
// registry/livery.json
{
  "livery": {
    "repaint": {
      "vehicle": "swofty.classic-seasons-2017/redbull",
      "textures": "livery/"
    }
  }
}
```

```json
// registry/layers.json
{
  "vehicle": {
    "swofty.classic-seasons-2017/redbull": {
      "livery": "mod.rb-repaint/repaint"
    }
  }
}
```

</template>
</Example>
</template>
</Split>

Add this top-level field to `swofty.json`; [`requires`](/std/dependencies) stops Play while Classic Seasons 2017 is missing or off.

```json
"requires": [
  { "id": "swofty.classic-seasons-2017" }
]
```

## 3. Offer it in the livery picker

<Split>

For an extra stock Red Bull picker choice, replace the previous implementation with this one, remove the 2017 `requires` field, and remove `registry/layers.json` from the data version. Supply `archives/redbull_repaint.erp` for the stock model. A [special livery](/std/systems/liveries) creates its picker and unlock rows; the [string record](/std/systems/ui#strings) supplies its visible name.

<template #example>
<Example root="mod.rb-repaint" code-file="native/src/livery.cpp" data-file="registry/livery.json" files="swofty.json, native/build.sh, native/livery.dll, archives/redbull_repaint.erp">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/cars.hpp>
#include <swofty/systems/liveries.hpp>
#include <swofty/systems/ui.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    ui::strings.add("lng_mod_rb_repaint", {
        .text = { { "*", "Red Bull Repaint" } },
    });

    liveries::liveries.add("repaint", {
        .vehicle = cars::vehicles["game/redbull_2025"],
        .archive = file("archives/redbull_repaint.erp"),
        .special = true,
        .suffix = "auto",
        .picker_name = "lng_mod_rb_repaint",
        .unlock = liveries::Unlock::owned,
    });

    // The player selects this entry in the livery picker.
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
// registry/livery.json
{
  "livery": {
    "repaint": {
      "vehicle": "game/redbull_2025",
      "archive": "archives/redbull_repaint.erp",
      "special": true,
      "suffix": "auto",
      "picker_name": "lng_mod_rb_repaint",
      "unlock": "owned"
    }
  },
  "lang.string": {
    "lng_mod_rb_repaint": {
      "text": {
        "*": "Red Bull Repaint"
      }
    }
  }
}
```

</template>
</Example>
</template>
</Split>

The core allocates the archive suffix and stat id; use a unique picker key to avoid colliding with a stock special livery.

## 4. Let a player drop it in

<Split>

A player can drag a livery `.serps` package or game-file zip onto the [importer](/std/importer), which creates a livery record and a layer on the base car. **Applies to** adds another car as a target, such as **F1 2017 Red Bull (Classic Seasons 2017)**, and keeps the stock car. The picker lists only cars whose model the livery fits, and saves the choice in `importer.json` for re-imports. An author can declare the target car in the `.serps` package.

<template #example>
<Example root="mod.rb-repaint" data-file="registry/layers.json" title="After choosing the 2017 Red Bull">
<template #data>

```json
// The layers for an imported mod mod.rb-repaint after Applies to adds the 2017 Red Bull
{
  "vehicle": {
    "game/redbull_2025": { "livery": "mod.rb-repaint/repaint" },
    "swofty.classic-seasons-2017/redbull": { "livery": "mod.rb-repaint/repaint" }
  }
}
```

</template>
</Example>
</template>
</Split>

On the 2017 car, the import competes with Classic Seasons by load order. [Making mods](/reference/mods/) covers building a `.serps` package.

## Try it

Keep one of the three versions. For C++, build the DLL from inside `mod.rb-repaint/` with `native/build.sh` from the [addon template](/reference/addons/template):

```sh
native/build.sh
```

For **Without code**, omit the native build and remove `operations` from `swofty.json`. From the folder containing `mod.rb-repaint/`, install, enable, and run:

```sh
swofty addons install ./mod.rb-repaint
swofty addons enable mod.rb-repaint
swofty run --apply
```

Open Time Trial and choose the stock Red Bull for step 1, F1 2017's Red Bull for step 2, or **Red Bull Repaint** in the stock car's livery picker for step 3. Restart the game after changing these boot-time records.

Check `swofty/loader.log` for the corresponding entries. For the stock repaint:

```text
[registry] vehicle game/redbull_2025: edit by mod.rb-repaint changed /livery -> mod.rb-repaint/repaint
[registry] vehicle swofty.classic-seasons-2017/redbull: /livery set by the variant; layer on game/redbull_2025 from
           mod.rb-repaint not inherited for this field
```

For the direct 2017 repaint:

```text
[registry] vehicle swofty.classic-seasons-2017/redbull: edit by mod.rb-repaint changed /livery -> mod.rb-repaint/repaint
```

For the special livery, the allocated suffix and stat id can differ:

```text
[registry] livery mod.rb-repaint/repaint: special, wep_override _lx01, stat id 22 (se_livery), owned
```

## When something's missing

- **Classic Seasons 2017 is off:** the step 2 addon has a required dependency. The launcher names it before Play and stops the launch until it is available.
- **The target car record is gone:** the hard `vehicle` reference leaves the livery unresolved. A layer that fails reference validation is dropped, leaving the base and other valid layers in place.
- **The repaint addon is off:** the player sees the livery underneath it; the special picker choice is absent.

[Dependencies](/std/dependencies) distinguish missing addon requirements from unresolved records.

## Publish

Package your chosen version and follow [Publishing](/reference/addons/publishing) to publish `mod.rb-repaint` to the Addon Browser.