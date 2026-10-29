---
title: Add your own system
description: Build F1 1990's six-compound tyre range as the addon's own registry, feed it to the game's tyre reads, give it an event and a service, and let other addons add a compound or change one with a layer.
---

# Add your own system

You'll build the tyre system F1 1990 needs: six Goodyear compounds, A to E plus a wet, where E is for qualifying only. The game has no such range, so the addon defines its own registry of compounds, turns it into a tyre class the game can read, and lets other addons extend it. A second addon adds a new wet, and a third changes one compound's colour. The player sees F1 1990's compounds in the tyre screens and on the HUD.

A system of your own is a registry like the game's: other addons add records to it, read it and layer it, with the same rules. [Your own systems](/std/systems/custom) covers it in depth.

## The folder

```text
mod.f1-1990/
├── swofty.json
├── api/
│   └── swofty_f1_1990_api/
│       └── tyres.hpp           the public header other addons include
├── native/
│   ├── build.sh
│   ├── f1_1990_tyres.dll
│   └── src/tyres.cpp
├── tyres/goodyear.vcmpd        the compounds' physics
└── art/tyres/                  a badge and a worn badge per compound
    ├── a.svg  a_worn.svg  …  w.svg  w_worn.svg

mod.f1-1990-wets/               adds a compound
mod.f1-1990-tweaks/             layers a compound
```

## 1. Declare the system in a public header

<Split>

The header is the system's contract. It declares the record types, the two registries, the event the system fires and the service it publishes. Other addons include it and build against it, so ship it with the addon.

A registry's name starts with your addon id. Its `version` is the record's shape: add fields in a minor version, and other addons built against `1.0` keep working.

Every F1 1990 session runs in its own setup mode, `mod.f1-1990/f1_1990`. `scope` names it, so handlers and rules can say "only in F1 1990".

<template #example>
<Example root="mod.f1-1990" code-file="api/swofty_f1_1990_api/tyres.hpp" files="swofty.json, native/build.sh, native/src/tyres.cpp, native/f1_1990_tyres.dll, tyres/goodyear.vcmpd">
<template #code>

```cpp
#pragma once
#include <swofty/registry.hpp>
#include <swofty/events.hpp>
#include <swofty/systems/tyres.hpp>

namespace f1990 {

inline const swofty::events::Selector scope = swofty::events::setup_mode("mod.f1-1990/f1_1990");

struct TyreCompound {
    static constexpr swofty::Version version{1, 0};
    std::string letter;                          // "A" .. "E", "W"
    int softness = 0;                            // higher is softer; the wet is 0
    swofty::Ref<swofty::tyres::Physical> physical;
    swofty::Colour colour;
    bool qualifying_only = false;
    bool wet = false;
};
SWOFTY_RECORD(TyreCompound, letter, softness, physical, colour, qualifying_only, wet)
inline swofty::Registry<TyreCompound> compounds{"mod.f1-1990.tyre_compound", { .applies = swofty::Applies::boot }};

struct TyreRules {                               // which compounds a season offers
    static constexpr swofty::Version version{1, 0};
    std::vector<swofty::Ref<TyreCompound>> race, qualifying;
    swofty::Ref<TyreCompound> wet;
};
SWOFTY_RECORD(TyreRules, race, qualifying, wet)
inline swofty::Registry<TyreRules> tyre_rules{"mod.f1-1990.tyre_rules", { .applies = swofty::Applies::boot }};

namespace events {
struct CompoundChosen {                          // a car asked for a compound
    static constexpr const char *point = "mod.f1-1990.compound_chosen";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version, car;
    swofty::Ref<TyreCompound> compound;
};
}

struct Tyres {                                   // the service
    static constexpr const char *name = "mod.f1-1990.tyres";
    static constexpr uint32_t version = 1;
    uint32_t size;
    std::vector<swofty::Ref<TyreCompound>> (*offered)();   // four dry compounds, softest first, then the wet
};

} // namespace f1990
```

</template>
</Example>
</template>
</Split>

## 2. Register the range

<Split>

`SWOFTY_REGISTER` adds the six compounds and the season's rules. Each compound gets its own physics record from the `.vcmpd`, on F1 1990's car package.

`check` adds a rule every `TyreRules` record must pass, including ones other addons add or layer: four dry compounds, none twice. A record that fails is dropped and logged.

<template #example>
<Example root="mod.f1-1990" code-file="native/src/tyres.cpp" files="swofty.json, api/swofty_f1_1990_api/tyres.hpp, native/build.sh, native/f1_1990_tyres.dll, tyres/goodyear.vcmpd">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/cars.hpp>
#include <swofty/systems/tyres.hpp>
#include <swofty_f1_1990_api/tyres.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    auto compound = [](const char *key, int softness, const char *colour,
                       tyres::Code code, bool qualifying_only, bool wet) {
        auto physical = tyres::physical.add(std::string("goodyear_") + key, {
            .package = cars::packages["mod.f1-1990/f1_1990"],
            .code = code,
            .vcmpd = file("tyres/goodyear.vcmpd"),
            .slick = !wet,
            .full_wet = wet,
        });
        std::string letter = key;
        for (char &c : letter) c = char(std::toupper(c));
        return f1990::compounds.add(key, {
            .letter = letter, .softness = softness, .physical = physical,
            .colour = Colour(colour), .qualifying_only = qualifying_only, .wet = wet,
        });
    };

    compound("a", 1, "#FFFFFF", tyres::Code::har, false, false);
    auto b = compound("b", 2, "#F6C21C", tyres::Code::med, false, false);
    auto c = compound("c", 3, "#E2231A", tyres::Code::sof, false, false);
    auto d = compound("d", 4, "#9B2FAE", tyres::Code::sup, false, false);
    auto e = compound("e", 5, "#2F74E8", tyres::Code::uso, true,  false);
    auto w = compound("w", 0, "#22A45D", tyres::Code::wet, false, true);

    f1990::tyre_rules.check([](const f1990::TyreRules &r, Errors &errors) {
        if (r.race.size() + r.qualifying.size() != 4)
            errors.add("/race", "name exactly four dry compounds");
    });
    f1990::tyre_rules.add("season", { .race = { b, c, d }, .qualifying = { e }, .wet = w });

    feed_the_game();                             // step 3
}
```

</template>
</Example>
</template>
</Split>

## 3. Feed it to the game

<Split>

The game reads tyre classes: five options, each a physics record and a look. A **provider** answers those reads from your registry when the game asks. It reads the compounds and rules as they are after every addon has added and layered them, so a new wet or a changed colour from another addon reaches the game with no extra code.

`offered()` is the season's range, softest dry first, then the wet. The class built from it applies in F1 1990's setup mode.

<template #example>
<Example root="mod.f1-1990" code-file="native/src/tyres.cpp" title="Above SWOFTY_REGISTER" files="swofty.json, api/swofty_f1_1990_api/tyres.hpp, native/build.sh, native/f1_1990_tyres.dll, art/tyres/a.svg">
<template #code>

```cpp
#include <swofty/systems/sessions.hpp>

namespace {

constexpr std::string_view class_id = "mod.f1-1990/goodyear";

std::vector<Ref<f1990::TyreCompound>> offered()
{
    auto rules = f1990::tyre_rules.get(f1990::tyre_rules["mod.f1-1990/season"]);
    if (!rules)
        return {};
    auto dry = rules->qualifying;
    dry.insert(dry.end(), rules->race.begin(), rules->race.end());
    std::ranges::sort(dry, std::greater{}, [](auto &r) { return f1990::compounds.get(r)->softness; });
    dry.push_back(rules->wet);
    return dry;
}

void feed_the_game()
{
    tyres::visual.provide([](std::string_view id) -> std::optional<tyres::Visual> {
        auto c = f1990::compounds.get(f1990::compounds[id]);
        if (!c)
            return std::nullopt;
        std::string badge = c->wet ? "w" : c->letter;
        for (char &ch : badge) ch = char(std::tolower(ch));
        return tyres::Visual{
            .name = Text::literal(c->letter), .colour = c->colour,
            .material_context = c->wet ? "Wet" : "Soft", .audio_id = c->wet ? "Wet" : "Soft",
            .icon = file("art/tyres/" + badge + ".svg"), .icon_worn = file("art/tyres/" + badge + "_worn.svg"),
        };
    }, [](auto emit) { for (auto &c : f1990::compounds.all()) emit(c.id()); });

    tyres::classes.provide([](std::string_view id) -> std::optional<tyres::Class> {
        auto range = offered();
        if (id != class_id || range.size() != 5)
            return std::nullopt;
        tyres::Class cls{ .modes = { sessions::modes["mod.f1-1990/f1_1990"] } };
        const char *slot[] = { "SuperSoft", "Soft", "Medium", "Hard", "Wet" };
        for (size_t i = 0; i < 5; ++i)
            cls.options[i] = { slot[i], { f1990::compounds.get(range[i])->physical },
                               tyres::visual[range[i].id()] };
        return cls;
    }, [](auto emit) { emit(class_id); });
}

} // namespace
```

</template>
</Example>
</template>
</Split>

## 4. Fire an event and publish a service

<Split>

Other addons can react to F1 1990 or ask it questions. `events::define` declares the custom event under the addon's id, and the system fires it whenever a car asks for a compound: here from the game's own set-choice event. `services::publish` offers the `Tyres` table, so a strategy addon can ask what the season offers.

A handler on `ChooseSet` can also change the choice. F1 1990 moves a car off E in the race, because E is for qualifying only.

<template #example>
<Example root="mod.f1-1990" code-file="native/src/tyres.cpp" title="Below SWOFTY_REGISTER" files="swofty.json, api/swofty_f1_1990_api/tyres.hpp">
<template #code>

```cpp
#include <swofty/events.hpp>
#include <swofty/services.hpp>

static const f1990::Tyres tyres_service{ sizeof(f1990::Tyres), &offered };

SWOFTY_ON_LOAD
{
    events::define<f1990::events::CompoundChosen>(f1990::scope);
    services::publish(tyres_service);

    events::on<tyres::events::ChooseSet>().in(f1990::scope)
        .before([](tyres::events::ChooseSet &choice, const events::Context &ctx) {
            auto range = offered();
            if (choice.wanted_slot >= range.size())
                return;
            if (ctx.in(events::session_type({ events::SessionType::race }))
                && f1990::compounds.get(range[choice.wanted_slot])->qualifying_only)
                choice.wanted_slot = 1;          // the softest race compound
        })
        .after([](const tyres::events::ChooseSet &choice, const events::Context &) {
            auto range = offered();
            if (choice.wanted_slot < range.size())
                events::fire(f1990::events::CompoundChosen{
                    sizeof(f1990::events::CompoundChosen), 1, choice.car, range[choice.wanted_slot] });
        });
}

SWOFTY_ADDON()
```

</template>
</Example>
</template>
</Split>

`swofty.json` names the DLL. The header lives in `api/`, and other addons' build scripts add it to their include path:

```json
{
  "schema_version": 1,
  "id": "mod.f1-1990",
  "name": "F1 1990",
  "version": "1.0.0",
  "author": "you",
  "category": "cars",
  "compat": { "game": "f1_25", "launcher": ">=1.6.0" },
  "operations": { "native": [ { "dll": "native/f1_1990_tyres.dll" } ] }
}
```

## 5. Another addon adds a compound

<Split>

`mod.f1-1990-wets` adds a new wet under its own key, `mod.f1-1990-wets/w`, and layers the season's rules to use it. It reuses F1 1990's wet physics and changes the colour. F1 1990's provider picks it up: the game's wet option is now the new compound.

The **Without code** tab is the same addon as data files.

<template #example>
<Example root="mod.f1-1990-wets" code-file="native/src/wets.cpp" data-file="registry/tyres.json" files="swofty.json, registry.json, native/build.sh, native/wets.dll, registry/layers.json">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty_f1_1990_api/tyres.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    auto wet = f1990::compounds.add("w", {
        .letter = "W", .softness = 0,
        .physical = tyres::physical["mod.f1-1990/goodyear_w"],
        .colour = Colour("#1E90FF"), .wet = true,
    });
    f1990::tyre_rules.layer(f1990::tyre_rules["mod.f1-1990/season"],
                            [=](f1990::TyreRules &r) { r.wet = wet; });
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
// registry/tyres.json
{ "mod.f1-1990.tyre_compound": {
    "w": { "letter": "W", "softness": 0, "physical": "mod.f1-1990/goodyear_w", "colour": "#1E90FF", "wet": true } } }
```

```json
// registry/layers.json
{ "mod.f1-1990.tyre_rules": { "mod.f1-1990/season": { "wet": "mod.f1-1990-wets/w" } } }
```

</template>
</Example>
</template>
</Split>

Its `swofty.json` requires F1 1990, and its `registry.json` names the registry and the records it builds on, so the launcher checks them before Play:

```json
{
  "requires": [
    { "registry": "mod.f1-1990.tyre_compound", "version": "^1" },
    { "record": "mod.f1-1990.tyre_rules:mod.f1-1990/season", "for": "the season's wet" }
  ]
}
```

## 6. Another addon changes one compound

<Split>

`mod.f1-1990-tweaks` paints C orange with a [layer](/std/layers). It changes one field and leaves everything else to F1 1990.

<template #example>
<Example root="mod.f1-1990-tweaks" code-file="native/src/tweaks.cpp" data-file="registry/layers.json" files="swofty.json, registry.json, native/build.sh, native/tweaks.dll">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty_f1_1990_api/tyres.hpp>

SWOFTY_REGISTER
{
    f1990::compounds.layer(f1990::compounds["mod.f1-1990/c"],
                           [](f1990::TyreCompound &c) { c.colour = swofty::Colour("#FF7800"); });
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
{ "mod.f1-1990.tyre_compound": { "mod.f1-1990/c": { "colour": "#FF7800" } } }
```

</template>
</Example>
</template>
</Split>

## Try it

Build each DLL with its `native/build.sh` from the [addon template](/reference/addons/template), then:

```sh
for id in mod.f1-1990 mod.f1-1990-wets mod.f1-1990-tweaks; do
    swofty addons install ./$id
    swofty addons enable $id
done
swofty run --apply
```

Start an F1 1990 session. The tyre screen offers E, D, C, B and the new wet, and C is orange. `swofty/loader.log`:

```text
[registry] register: mod.f1-1990 (f1_1990_tyres.dll) 6 tyre.physical, 6 mod.f1-1990.tyre_compound, 1 mod.f1-1990.tyre_rules
[registry] mod.f1-1990.tyre_rules mod.f1-1990/season: edit by mod.f1-1990-wets changed /wet -> mod.f1-1990-wets/w
[registry] mod.f1-1990.tyre_compound mod.f1-1990/c: edit by mod.f1-1990-tweaks changed /colour -> #FF7800
```

## When something's missing

- **F1 1990 is off:** both companions require it, so the launcher stops Play and names it.
- **A record a companion builds on is gone,** such as the season's rules after an F1 1990 update: the companion's `registry.json` names it, and the launcher stops Play.
- **The wets addon is off:** the season goes back to F1 1990's own wet at the next game start.
- **A layer breaks a rule,** such as a `TyreRules` with five dry compounds: `check` rejects it, the core drops that layer and logs it, and the season keeps its last valid rules.

## Publish

Publish each addon on its own: see [Packaging and publishing](/reference/addons/publishing). Bump F1 1990's record `version` minor when you add fields, and its major only for a change other addons must adapt to: see [Versioning](/std/versioning).
