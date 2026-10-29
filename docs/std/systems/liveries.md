---
title: Liveries
description: Liveries in code - liveries and special liveries as a typed registry, and how a livery fits a car by its model. AI livery choices, decal sets, sponsor packs and the livery events and operations are described for headers that come later.
---

# Liveries

Repaint a car, add a choice to the livery picker, or choose which special livery each AI team races. A Macarena Red Bull repaint, for example, is one `liveries::Livery` record made for the 2026 Red Bull, plus a layer that makes that car race in it.

A few terms used on this page:

- A **livery** is a `liveries::Livery` record: the textures or archive a car wears.
- A **special livery** is an extra entry in a car's livery picker, next to the team's own.
- A car's **model** is its mesh: `cars::Vehicle::model`. A livery fits every car with the same model as the car it was made for.
- A **layer** changes values on a record that already exists. A **variant** is a separate record that inherits another record's values and overrides some of them.
- A **box** is one of the year tiles in Grand Prix or Time Trial. The **base** is the data every year shares.

## The header

<Example root="swofty-sdk" code-file="include/swofty/systems/liveries.hpp">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/forward.hpp>

namespace swofty::liveries {

using Result = swofty::Result;

enum class Unlock { owned, game };                 // owned: the core reports it owned; game: the game's unlock row decides

struct Livery {
    static constexpr Version version{1, 0};
    Ref<cars::Vehicle> vehicle;                    // the car it was made for (compatible_vehicle); it fits every car
                                                   // with the same model
    Path textures;                                 // a folder of textures over the car's own
    Path archive;                                  // or a whole wep archive for the car
    bool partial = false;                          // covers part of the car (a number, a sponsor); partials stack
    bool special = false;                          // a picker entry: an se_livery row and an unlock row
    std::string suffix = "auto";                   // wep_override: "_barcelona_26"; auto takes the next free "_lxNN"
    std::string picker_name;                       // the lang key the picker shows
    Ref<tracks::Venue> event;                      // the grand prix it's made for, if any
    Unlock unlock = Unlock::owned;                 // who decides whether the player has it
    StatId stat_id = StatId::automatic;            // the se_livery and unlock rows' ids
    json::Value extra;
};
SWOFTY_RECORD(Livery, vehicle, textures, archive, partial, special, suffix, picker_name, event, unlock, stat_id)

inline Registry<Livery> liveries{"livery", {.applies = Applies::boot}};

}
```

</template>
</Example>

## A livery pack and a special livery, in code

<Split>

One addon, two kinds of livery:

- **The pack** gives three of Classic Seasons' 2017 cars a different look. Each livery is made for its 2017 car and painted on that car's 2017 model. An edit on the vehicle makes the car race in it, in every place the 2017 cars appear.
- **The special livery** adds a Scuderia AlphaTauri car to the 2026 Racing Bulls' livery picker, next to the team's own. The core writes the `se_livery` row, the unlock row and the archive slot, and reports it owned.
- Classic Seasons doesn't know the pack exists, and the pack copies none of its files.

<template #example>
<Example root="rmdesigns.liveries" code-file="native/src/liveries.cpp" data-file="registry/liveries.json" files="swofty.json, liveries/ferrari_2017/ferrari_d.tif.mipmaps, liveries/mclaren_2017/mclaren_d.tif.mipmaps, liveries/redbull_2017/redbull_d.tif.mipmaps, archives/toro_rosso_alphatauri.erp, lang/liveries.json, registry/layers.json">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/cars.hpp>
#include <swofty/systems/liveries.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    // the pack: one livery per car, and the car races in it
    for (const char *team : { "ferrari", "mclaren", "redbull" }) {
        auto car = cars::vehicles[std::string("swofty.classic-seasons-2017/") + team].optional();
        auto look = liveries::liveries.add(std::string(team) + "_2017", {
            .vehicle = car,
            .textures = file(std::string("liveries/") + team + "_2017/"),
        });
        cars::vehicles.layer(car, [=](cars::Vehicle &v) { v.livery = look; });
    }

    // the special livery: a picker entry for the 2026 Racing Bulls
    liveries::liveries.add("alphatauri_2026", {
        .vehicle = cars::vehicles["game/toro_rosso_2026"],
        .archive = file("archives/toro_rosso_alphatauri.erp"),
        .special = true,
        .picker_name = "lng_rm_livery_alphatauri",
        .unlock = liveries::Unlock::owned,
    });
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
// registry/liveries.json
{
  "livery": {
    "ferrari_2017": { "vehicle": "swofty.classic-seasons-2017/ferrari?", "textures": "liveries/ferrari_2017/" },
    "mclaren_2017": { "vehicle": "swofty.classic-seasons-2017/mclaren?", "textures": "liveries/mclaren_2017/" },
    "redbull_2017": { "vehicle": "swofty.classic-seasons-2017/redbull?", "textures": "liveries/redbull_2017/" },
    "alphatauri_2026": { "vehicle": "game/toro_rosso_2026", "archive": "archives/toro_rosso_alphatauri.erp",
                         "special": true, "picker_name": "lng_rm_livery_alphatauri", "unlock": "owned" }
  }
}
```

```json
// registry/layers.json
{
  "vehicle": {
    "swofty.classic-seasons-2017/ferrari": { "livery": "rmdesigns.liveries/ferrari_2017" },
    "swofty.classic-seasons-2017/mclaren": { "livery": "rmdesigns.liveries/mclaren_2017" },
    "swofty.classic-seasons-2017/redbull": { "livery": "rmdesigns.liveries/redbull_2017" }
  }
}
```

</template>
</Example>
</template>
</Split>

```text
[registry] register: rmdesigns.liveries (liveries.dll) 4 livery, 3 layers on vehicle (early stage, 0.3 ms)
[registry] vehicle swofty.classic-seasons-2017/ferrari: edit by rmdesigns.liveries changed /livery -> rmdesigns.liveries/ferrari_2017
[registry] livery rmdesigns.liveries/alphatauri_2026: special, wep_override _lx01, stat id 22 (se_livery), owned
[registry] applied at boot: livery 4 (2 rows, 1 unlock row; 1 archive and 3 texture folders added to the asset manifest), vehicle 3
```

Without Classic Seasons 2017, the three pack liveries are dropped (`optional()`), and the special livery still loads.

## A livery fits by model

A livery fits a car when the car's effective model is the model of the car the livery was made for. A variant that doesn't set its own `model` has its base's model. Classic Seasons' 2017 cars set their own 2017 models, so a livery painted for the 2026 Red Bull doesn't fit the 2017 Red Bull.

- **The author may declare the target.** In code it's `Livery::vehicle`. An imported mod can name it in its `mod.serps`, and the author's target beats the car the importer reads from the archive path.
- **Without a declared target,** the importer takes the car from the archive path: `f1_2026_vehicle_package/teams/redbull/` is `game/redbull_2026`.
- **A layer that puts a livery on a car it doesn't fit is dropped,** with a log line.

```text
[registry] vehicle swofty.classic-seasons-2017/redbull: livery mod.macarena-red-bull/macarena dropped: made for
           game/redbull_2026, and the model differs (swofty.classic-seasons-2017 cars/redbull/)
```

## A livery is in every year

<Split>

A livery imported as a single mod becomes a livery record under the mod's own key, plus a layer on the base team car. It applies everywhere: every box and career whose car for that team is the base car, or a variant that keeps the base's livery, shows it.

- **A year addon's own livery wins in its own box.** A variant's own value beats layers on its base.
- **Applies to adds a target and keeps the original.** On the importer page, the player can put the livery on another car too. The picker lists only cars that fit. Here Custom Grid Size's 11th Red Bull has the 2026 Red Bull's model and its own livery, and Applies to adds it.
- **A later full livery replaces an earlier one whole.** `Vehicle::livery` holds one livery, and the later layer in load order wins.
- **Partial liveries stack.** A number decal or a sponsor change sets `partial`. It goes over whatever livery the car wears, and partials apply in load order.
- **Imports already in a profile** keep mixing two full liveries asset by asset until the player presses the fix on the importer page.

<template #example>
<Example root="mod.macarena-red-bull" code-file="native/src/livery.cpp" data-file="registry/livery.json" files="swofty.json, ops/erp.json, livery/redbull_d.tif.mipmaps, livery/redbull_m.tif.mipmaps">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/cars.hpp>
#include <swofty/systems/liveries.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    auto base = cars::vehicles["game/redbull_2026"];
    auto look = liveries::liveries.add("macarena", { .vehicle = base, .textures = file("livery/") });

    // every year whose Red Bull is this car, or a variant that keeps its livery
    cars::vehicles.layer(base, [=](cars::Vehicle &v) { v.livery = look; });

    // Applies to: also Custom Grid Size's 11th Red Bull, which has the same model
    if (option<bool>("also_t11"))
        cars::vehicles.layer(cars::vehicles["swofty.custom-grid-size/redbull_t11"].optional(),
                             [=](cars::Vehicle &v) { v.livery = look; });
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
// registry/livery.json: what the importer writes
{
  "livery": { "macarena": { "vehicle": "game/redbull_2026", "textures": "livery/" } }
}
```

```json
// registry/layers.json
{
  "vehicle": {
    "game/redbull_2026": { "livery": "mod.macarena-red-bull/macarena" }
  }
}
```

</template>
</Example>
</template>
</Split>

```text
[registry] vehicle game/redbull_2026: edit by mod.macarena-red-bull changed /livery -> mod.macarena-red-bull/macarena
[registry] vehicle swofty.custom-grid-size/redbull_t11: extends game/redbull_2026, same model; Applies to adds
           mod.macarena-red-bull/macarena
```

The importer routes a `.vtf` inside a livery's `wep.erp` to `vehicle.handling`. Installing a livery leaves R&D Tyres' handling in place.

## AI liveries

The 1.6.0 header has no `ai_assignments` registry, no decal sets or sponsor packs, and no livery events or operations (`grant`, `assign`). This section and the next describe them for headers that come later.

<Split>

More Special Liveries lets the player pick which special livery each AI team races. Each pick is an `ai_assignments` record bound to the addon's option. The core applies it at session load through `livery.ai_select`, the point where the game picks each car's livery suffix.

<template #example>
<Example root="mildtdesign.more-special-liveries" code-file="native/src/ai.cpp" files="swofty.json">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/liveries.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    liveries::ai_assignments.add("mclaren_2026", {
        .vehicle = cars::vehicles["game/mclaren_2026"],
        .season = 2026,
    });

    liveries::ai_assignments.layer(liveries::ai_assignments["mildtdesign.more-special-liveries/mclaren_2026"],
        [](liveries::AiAssignment &a) {
            auto pick = option<std::string>("ai_2026_mclaren");       // default, monaco, silverstone
            if (pick != "default")
                a.livery = liveries::liveries["game/mclaren_" + pick + "_25"];
        });
}

SWOFTY_ADDON()
```

</template>
</Example>
</template>
</Split>

```text
[registry] livery.ai_assignment mildtdesign.more-special-liveries/mclaren_2026: livery game/mclaren_silverstone_25
[events] livery.ai_select (car 7, game/mclaren_2026): game/mclaren_silverstone_25 (owned); car 8: the same
[registry] livery.ai_assignment mildtdesign.more-special-liveries/mclaren_2026: game/mclaren_monaco_25 is locked; races the default
```

## The picker and ownership

```cpp
// livery.special.list: add a livery to a car's picker at run time, without a database row
events::on<liveries::events::SpecialList>().after([](liveries::events::SpecialList &l, const events::Context &) {
    if (l.vehicle.id() == "game/toro_rosso_2026")
        l.list.push_back(liveries::liveries["rmdesigns.liveries/alphatauri_2026"]);
});

liveries::grant(liveries::liveries["game/mclaren_silverstone_25"]);   // report a stock special livery owned
```

My Team decal sets and sponsor slices:

```cpp
liveries::sponsor_packs.add("real_sponsors", {
    .slices = { { "sponsors_2026[petronas]", file("sponsors/petronas.png") } },
});
```

## Readiness and limits

- **Livery records: ready, applied at boot.** A livery's archive is routed to the addon's file over the car's wep archive, and an asset group is registered for it when the car's archive is not in the game folder; its textures folder is routed file by file over the car's `textures` folder. A special livery also gets its `se_livery` row, its unlock row and an ownership report. All of it uses the same row engine and file routing as vehicles, and a livery that no car wears applies nothing.
- **Common archive:** the asset group lists the car's package `teams/common.erp` first. A package that reuses the stock common archive keeps its archive on the 1.5 `file.put` and `group.register` operations (Classic Seasons 2022 and 2023 do).
- **Classic Seasons:** seasons 2016 to 2021 and 2024 declare each team car's archive as a `liveries::Livery` record in `registry/content.json`; `assets/liveries.json` holds the same archives as 1.5 operations and `registry.json` names it in `replaced_operations`.
- **Events and operations: unknown (L1).** `Game_Build_SpecialLiveryList`, `Game_FrontendSceneComponentCar_SetCarLivery`, `Game_FrontendSceneManager_SetCustomCarLivery` and the custom-livery colour setters are L1. `assign` comes later.
- **Archives and textures apply at boot.** The core adds them to the game's asset manifest when the game parses it, and the game reads them from the addon folder until the next game start.
- **Unknown:** where AI drivers' My Team livery colours come from, and how the game picks a random livery design. Runtime My Team livery compositing for AI is a known hang risk.
- **Ownership keys** are djb2(`eSELivery` + label). A special livery's label must not collide with a stock one.
- **Shader archives come in pairs** per GPU (`object_my_team.nefx2.sm51.erp` and `_amd`). Whole-archive shader replacements (Real Life Logos, BP Ultimate Livery) are outside this registry and replace each other by file.
- **A wep slot** takes a same-length token for the files named after the team or car model (`lx01rbs` for `rbswapc`). Nothing clashes with the stock car or another slot.
- **A locked AI pick** races the default livery.
