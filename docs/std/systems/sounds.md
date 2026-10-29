---
title: Sounds
description: Engine sounds in code - typed registries, the bundle event, and pinning the game's own sound files.
---

# Sounds

Give cars a new engine sound, give an added car its audio data, or keep the game's own sound on a car whatever an import replaces. A V10 pack, for example, is one `sounds::EngineSound` record, a bundle file plus the stock sound it takes its mixing settings from, and a layer that points the 2026 cars at it.

A few terms used on this page:

- An **engine sound** is a `sounds::EngineSound` record: a `.bdl` bundle, plus the stock sounds its mixing settings and tuning come from.
- A car's **audio data** is an `audio.car` record: the `audio_car_data` records that connect a car to its bundle through its switch name. It is described on this page, in a header that comes later.
- A **layer** changes values on a record that already exists. A **variant** is a separate record that inherits another record's values and overrides some of them.
- A **box** is one of the year tiles in Grand Prix or Time Trial. The **base** is the data every year shares.

## The header

<Example root="swofty-sdk" code-file="include/swofty/systems/sounds.hpp">
<template #code>

```cpp
#include <swofty/registry.hpp>

namespace swofty::sounds {

struct EngineSound {                               // an audio.audio_bundle_settings row and its .bdl
    Path bundle;                                   // the bundle file; the game's open of audio/2025/bundles/<id>.bdl
                                                   // is redirected to it
    Ref<EngineSound> settings_from;                // copy the mixing settings of this sound (usually a game/ one)
    Ref<EngineSound> tuning_from;                  // copy its tuning (engine, era, cabin, exhaust)
};

struct CarAudio {                                  // described here; the registry comes in a later header; one car's audio.audio_car_data.<switch> records
    Ref<cars::Vehicle> vehicle;                    // the car
    std::string switch_name;                       // the car template's AudioF1Car switch: "f1_M_mercedes"
    Ref<EngineSound> engine_sound;                 // the bundle the switch reaches
    Ref<CarAudio> from;                            // copy every audio_car_data field of another car
};

inline Registry<EngineSound> engine_sounds{"engine_sound", { .applies = Applies::boot }};   // rows at boot; the file when it's opened

}

namespace swofty::sounds::events {

struct BundleResolve {                             // Game_Build_AudioBundleRegistry, Game_AudioF1Car_Setup: before
    static constexpr const char *point = "audio.bundle.resolve";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    int32_t car;                                   // the live car
    const char *vehicle_id;
    const char *switch_name_str;                   // the car's audio switch
    const char *engine_sound_id;                   // the sound it gets
    int32_t (*set_engine_sound_fn)(void *user, const char *id);
    void *user;

    Ref<cars::Vehicle> vehicle() const { return swofty::detail::payload_ref<cars::Vehicle>(vehicle_id); }
    std::string_view switch_name() const { return swofty::detail::payload_str(switch_name_str); }
    Ref<EngineSound> engine_sound() const { return swofty::detail::payload_ref<EngineSound>(engine_sound_id); }
    Result set_engine_sound(const Ref<EngineSound> &sound)
    {
        if (!set_engine_sound_fn)
            return Result::unavailable;
        std::string id(sound.id());
        return swofty::detail::result(set_engine_sound_fn(user, id.c_str()));
    }
};

}
```

</template>
</Example>

A vehicle points at its sound with `cars::Vehicle::engine_sound` (see [Cars](/std/systems/cars)). The core writes the car's `audio_car_data` records from it, and a sound pack never writes raw audio keys.

## The V10 pack, in code

<Split>

An engine sound pack brings a V10 under its own key and points cars at it.

- **The V10 is a record** under the pack's key. Nothing stock is overwritten.
- **It layers the base cars** (`game/`). Every year whose car is one of them, or a variant that doesn't set its own sound, hears it. Classic Seasons' 2016 to 2018 cars set their own period sounds and keep them.
- **An option picks the cars:** every 2026 car, or the Ferrari only. The edit re-runs when the option changes, and the change applies from the next game start.
- **Other packs compose.** A V12 pack on the McLaren and this pack on the Ferrari both apply. On the same car, load order decides.
- **An imported sound pack** that overwrites stock bundles by name, such as `audi_2026.bdl`, becomes a layer on those stock sounds, and it applies everywhere. When two options of one pack overwrite the same files (36 audio files in the F1 25 Engine Sound Pack), the importer offers them as one choice.

<template #example>
<Example root="mod.esp" code-file="native/src/esp.cpp" data-file="registry/sounds.json" files="swofty.json, sounds/v10_screamer.bdl, registry/layers.json">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/cars.hpp>
#include <swofty/systems/sounds.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    // provide under your own key: the id is "mod.esp/v10_screamer"
    auto v10 = sounds::engine_sounds.add("v10_screamer", {
        .bundle = file("sounds/v10_screamer.bdl"),
        .settings_from = sounds::engine_sounds["game/ferrari_2025"],
        .tuning_from = sounds::engine_sounds["game/ferrari_2025"],
    });

    // the base 2026 cars, or only the Ferrari; years with their own sound keep it
    cars::vehicles.layer(under<cars::Vehicle>("game/"), [=](cars::Vehicle &v) {
        if (v.performance_class != cars::Class::f1_26)
            return;
        if (option<std::string>("cars") == "all" || v.label == "ferrari_2026")
            v.engine_sound = v10;
    });
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
// registry/sounds.json
{
  "engine_sound": {
    "v10_screamer": { "bundle": "sounds/v10_screamer.bdl", "settings_from": "game/ferrari_2025", "tuning_from": "game/ferrari_2025" }
  }
}
```

```json
// registry/layers.json
{
  "vehicle": {
    "$select": [
      { "ids": "game/*", "where": { "performance_class": "f1_26" }, "when": { "option": "cars", "is": "all" },
        "patch": { "engine_sound": "mod.esp/v10_screamer" } },
      { "ids": "game/ferrari_2026", "when": { "option": "cars", "is": "ferrari" },
        "patch": { "engine_sound": "mod.esp/v10_screamer" } }
    ]
  }
}
```

</template>
</Example>
</template>
</Split>

```text
[registry] register: mod.esp (esp.dll) 1 engine_sound, 1 layer on vehicle (early stage, 0.4 ms)
[registry] vehicle: selector ids game/* from mod.esp: 22 records changed (performance_class f1_26)
[registry] vehicle game/ferrari_2026: edit by mod.esp changed /engine_sound -> mod.esp/v10_screamer (option cars=all)
[registry] vehicle swofty.custom-grid-size/ferrari_t11: extends game/ferrari_2026, no own /engine_sound: mod.esp/v10_screamer
[registry] applied at boot: engine_sound 1 (1 bundle path redirected to mod.esp), vehicle 23 (92 database records)
```

[Content](/std/content) puts the V10 on Classic Seasons' 2017 cars, in place and as a separate GP/TT box.

## Keeping the game's own sound

<Split>

A year pack whose cars point at stock bundles can pin the game's original file with `stock("...")`. The core serves the game's own file for that record, whatever an import or an older addon's overlay replaced.

Classic Seasons 2024 ships no bundles of its own: its cars use the stock 2024 sounds. A season pack such as F1 2012 overwrites `audio/2025/bundles/ferrari_2024.bdl` and the other 2024 bundles with V8 sounds. Pinned, the 2024 Ferrari keeps its 2024 sound, and the import still reaches the stock 2024 cars.

<template #example>
<Example root="swofty.classic-seasons-2024" code-file="native/src/sounds.cpp" data-file="registry/sounds.json" files="swofty.json, native/src/cars.cpp">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/cars.hpp>
#include <swofty/systems/sounds.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    for (const char *maker : { "ferrari", "honda", "mercedes", "renault" }) {
        std::string name = std::string(maker) + "_2024";

        // the game's own 2024 bundle, whatever an import put at that path
        auto pinned = sounds::engine_sounds.variant(name, sounds::engine_sounds["game/" + name], [=](sounds::EngineSound &s) {
            s.bundle = stock("audio/2025/bundles/" + name + ".bdl");
        });

        cars::vehicles.layer(under<cars::Vehicle>("swofty.classic-seasons-2024/"), [=](cars::Vehicle &v) {
            if (v.engine_sound.id() == "game/" + name)
                v.engine_sound = pinned;
        });
    }
}
```

</template>
<template #data>

```json
// registry/sounds.json
{
  "engine_sound": {
    "ferrari_2024":  { "extends": "game/ferrari_2024",  "bundle": { "stock": "audio/2025/bundles/ferrari_2024.bdl" } },
    "honda_2024":    { "extends": "game/honda_2024",    "bundle": { "stock": "audio/2025/bundles/honda_2024.bdl" } },
    "mercedes_2024": { "extends": "game/mercedes_2024", "bundle": { "stock": "audio/2025/bundles/mercedes_2024.bdl" } },
    "renault_2024":  { "extends": "game/renault_2024",  "bundle": { "stock": "audio/2025/bundles/renault_2024.bdl" } }
  }
}
```

</template>
</Example>
</template>
</Split>

```text
[registry] engine_sound swofty.classic-seasons-2024/ferrari_2024: bundle pinned to the game's audio/2025/bundles/ferrari_2024.bdl
           (replaced by mod.f1-2012-temporada; the stock file is served for this record)
```

## A car's audio data

<Split>

Classic Seasons' cars need 430 raw audio records per season. In the design that is one `audio.car` record per car, copied from a stock car. The car reaches its bundle through its switch name, not through the car model's `bundleName`.

<template #example>
<Example root="swofty.classic-seasons-2017" code-file="native/src/sounds.cpp" data-file="registry/sounds.json" files="swofty.json, sounds/cs17_mercedes.bdl, sounds/cs17_ferrari.bdl, sounds/cs17_honda.bdl, sounds/cs17_renault.bdl">
<template #code>

```cpp
SWOFTY_REGISTER
{
    sounds::engine_sounds.add("mercedes", {
        .bundle = file("sounds/cs17_mercedes.bdl"),
        .settings_from = sounds::engine_sounds["game/mercedes_2025"],
        .tuning_from = sounds::engine_sounds["game/mercedes_2025"],
    });
}
```

</template>
<template #data>

```json
{
  "engine_sound": {
    "mercedes": { "bundle": "sounds/cs17_mercedes.bdl", "settings_from": "game/mercedes_2025", "tuning_from": "game/mercedes_2025" }
  }
}
```

</template>
</Example>
</template>
</Split>

## Swapping a sound at run time

```cpp
// audio.bundle.resolve: a different bundle for one car, without changing its record
events::on<sounds::events::BundleResolve>().before([](sounds::events::BundleResolve &b, const events::Context &) {
    if (b.vehicle().id() == "game/williams_2026")
        b.set_engine_sound(sounds::engine_sounds["mod.esp/v10_screamer"]);
});
```

## Readiness and limits

- **Records: ready.** Classic Seasons ships period sounds this way.
- **Events: unknown (L1).** `Game_Build_AudioBundleRegistry` (keys rows by `bundle_name`, maker and year) and `Game_AudioF1Car_Setup` are L1.
- **Rows apply at boot.** `Game_Build_AudioBundleRegistry` walks the `audio_bundle_settings` rows when it builds.
- **Bundle files are redirected,** not copied: the game opens the stock path and reads the addon's file. The redirect is the core's `file` feature, which is still marked unstable.
- **Unknown:** when the game reads `audio.audio_car_data`, at boot or at session load. Until that's known, a box that wants a different sound for a car than other boxes gives the car its own vehicle variant.
- **The reader expects 16-layer engine parts.** 2016 to 2018 bundles have fewer, and the core widens them.
- **A car reaches its bundle through its switch name.** The car model's `bundleName` doesn't pick the sound.
- **Commentary** (`audio.commentary`: track commentator overrides, speech lines) comes later.
