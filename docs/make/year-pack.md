---
title: Make a year pack
description: A 2008 season box for Grand Prix and Time Trial, with its own teams, drivers, cars, sounds, rules, calendar and career season data, built on the stock game as variants.
---

# Make a year pack

You'll build `mod.season-2008`, a 2008 season. The player sees an **F1 2008** box in Grand Prix and Time Trial, with the 2008 teams, drivers, cars and liveries, 2008 points and refuelling, and the 2008 calendar first in the track list. Career modes can race 2008 too, because the pack also registers the season's line-up and ratings.

Everything in the pack is built on stock records. A 2008 McLaren is a **variant** of the stock McLaren: it inherits everything and sets only what 2008 changes. Values the box sets are its own, and addons that change the stock McLaren don't replace them (see [How changes reach seasons and careers](/std/inheritance)).

## The folder

```text
mod.season-2008/
├── swofty.json
├── native/
│   ├── build.sh
│   ├── season_2008.dll
│   └── src/
│       ├── season.cpp        teams, drivers, cars, the box
│       └── career.cpp        the career season and ratings
├── liveries/                 one folder of textures per car
├── sounds/                   v8_ferrari.bdl, v8_mercedes.bdl, …
├── art/box_2008.png          the box's tile
└── lang/season.json          team names, the box title
```

Two teams carry the guide: McLaren and Ferrari. The other eight follow the same pattern.

## 1. Teams and drivers

<Split>

Each 2008 team is a variant of a stock team, with its own names and colours. A driver who's in the stock game, such as Lewis Hamilton, is used as is. A driver who isn't gets an identity of their own, with a stock **donor** driver for everything the pack doesn't set: here the donor's head and animations.

Team and driver names are text keys, defined in `lang/season.json`.

<template #example>
<Example root="mod.season-2008" code-file="native/src/season.cpp" files="swofty.json, native/src/career.cpp, native/build.sh, lang/season.json, art/box_2008.png">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/cars.hpp>
#include <swofty/systems/content.hpp>
#include <swofty/systems/drivers.hpp>
#include <swofty/systems/liveries.hpp>
#include <swofty/systems/sounds.hpp>
#include <swofty/systems/teams.hpp>

using namespace swofty;

static Ref<teams::Identity> team(const char *key, const char *stock, const char *name, const char *colour)
{
    return teams::identities.variant(key, teams::identities[stock], [=](teams::Identity &t) {
        t.names.title = Text::key(name);
        t.colours.primary = Colour(colour);
    });
}

static Ref<drivers::Identity> driver(const char *key, const char *donor, const char *first, const char *last,
                                     const char *nationality, int number)
{
    return drivers::identities.add(key, {
        .donor = drivers::identities[donor],
        .names = { .first = Text::key(first), .last = Text::key(last) },
        .nationality = nationality,
        .number = number,
    });
}

SWOFTY_REGISTER
{
    auto mclaren = team("mclaren", "game/mclaren", "lng_s08_mclaren", "#C0C0C0");
    auto ferrari = team("ferrari", "game/ferrari", "lng_s08_ferrari", "#DC0000");

    auto kovalainen = driver("heikki_kovalainen", "game/lando_norris", "lng_s08_heikki", "lng_s08_kovalainen", "FI", 23);
    auto massa      = driver("felipe_massa", "game/pierre_gasly", "lng_s08_felipe", "lng_s08_massa", "BR", 2);

    register_cars();                                 // step 2
    register_box(mclaren, ferrari, kovalainen, massa);   // step 3
}

SWOFTY_ADDON()
```

</template>
</Example>
</template>
</Split>

[Teams](/std/systems/teams) and [Drivers](/std/systems/drivers) list every field: logos, short names, helmets, suits and heads.

## 2. Cars, liveries and sounds

<Split>

Each 2008 car is a variant of the stock car it drives like, with its own livery and engine sound. The pack sets each car's pace tier for 2008 and **protects** it, so a generic AI or physics mod's selector leaves the 2008 pace alone unless the player allows it.

Some packs use the stock sound files for a car. If an imported mod overwrites those files, `stock("...")` keeps serving the game's original to the pack's cars.

<template #example>
<Example root="mod.season-2008" code-file="native/src/season.cpp" title="Above SWOFTY_REGISTER" files="swofty.json, sounds/v8_ferrari.bdl, sounds/v8_mercedes.bdl, liveries/ferrari, liveries/mclaren">
<template #code>

```cpp
static Ref<cars::Vehicle> mclaren_car, ferrari_car;

static Ref<cars::Vehicle> car(const char *key, const char *stock, const char *sound, int tier)
{
    auto v8 = sounds::engine_sounds.add(key, {
        .bundle = file(std::string("sounds/") + sound + ".bdl"),
        .settings_from = sounds::engine_sounds[std::string(stock)],
        .tuning_from = sounds::engine_sounds[std::string(stock)],
    });
    auto livery = liveries::liveries.add(key, {
        .vehicle = cars::vehicles[stock],
        .textures = file(std::string("liveries/") + key + "/"),
    });
    auto v = cars::vehicles.variant(key, cars::vehicles[stock], [=](cars::Vehicle &c) {
        c.label = std::string("s08_") + key;
        c.engine_sound = v8;
        c.livery = livery;
        c.performance_tier = tier;
    });
    cars::vehicles.protect(v, { "performance_tier" });
    return v;
}

static void register_cars()
{
    mclaren_car = car("mclaren", "game/mclaren_2025", "v8_mercedes", 1);
    ferrari_car = car("ferrari", "game/ferrari_2025", "v8_ferrari", 1);
}
```

</template>
</Example>
</template>
</Split>

[Cars](/std/systems/cars), [Liveries](/std/systems/liveries) and [Sounds](/std/systems/sounds) cover models, special liveries and sound tuning.

## 3. The season and its box

<Split>

The **package** is the season itself: its grid, its rules and its calendar. The **box** is the tile the player picks in Grand Prix and Time Trial, and it opens the package.

The rules apply when a session loads: 2008 points, refuelling, and a tyre class of the pack's choosing. Every box shares the `swofty_f1` setup mode, so the pack needs no mode of its own.

The calendar names its tracks by venue. The box shows the 2008 calendar first, then every other track that fits under **More tracks**. A round whose venue nobody provides is left out.

<template #example>
<Example root="mod.season-2008" code-file="native/src/season.cpp" data-file="registry/content.json" title="Above SWOFTY_REGISTER" files="swofty.json, art/box_2008.png, lang/season.json">
<template #code>

```cpp
#include <swofty/systems/career.hpp>
#include <swofty/systems/sessions.hpp>
#include <swofty/systems/tracks.hpp>

static Ref<content::Package> season_2008;

static void register_box(Ref<teams::Identity> mclaren, Ref<teams::Identity> ferrari,
                         Ref<drivers::Identity> kovalainen, Ref<drivers::Identity> massa)
{
    auto points = sessions::points.add("2008", { .points = { 10, 8, 6, 5, 4, 3, 2, 1 } });

    auto calendar = career::calendars.add("2008", {
        .rounds = {
            { .round = 1, .race = "lng_gp_australia", .venue = tracks::venues["venue/albert_park"], .date = 2008y / 3 / 16 },
            { .round = 2, .race = "lng_gp_malaysia",  .venue = tracks::venues["venue/sepang"],      .date = 2008y / 3 / 23 },
            // … 18 rounds
        },
        .seasons = 1,
    });

    season_2008 = content::packages.add("f1_2008", {
        .title = "lng_s08_title",
        .entrant_list = "s08_f1_2008",
        .season = 2008,
        .calendar = calendar,
        .teams = {
            { .key = "mclaren", .team = mclaren, .vehicle = mclaren_car,
              .drivers = { { .driver = drivers::identities["game/lewis_hamilton"], .number = 22 },
                           { .driver = kovalainen, .number = 23 } } },
            { .key = "ferrari", .team = ferrari, .vehicle = ferrari_car,
              .drivers = { { .driver = drivers::identities["game/kimi_raikkonen"], .number = 1 },
                           { .driver = massa, .number = 2 } } },
        },
        .rules = { .points = points, .refuelling = true },
    });

    content::era_boxes.add("f1_2008", { .package = season_2008, .image = "art/box_2008.png", .order = 2008 });
}
```

</template>
<template #data>

```json
{
  "session.points": { "2008": { "points": [10, 8, 6, 5, 4, 3, 2, 1] } },
  "content.package": {
    "f1_2008": {
      "title": "lng_s08_title", "entrant_list": "s08_f1_2008", "season": 2008,
      "calendar": "mod.season-2008/2008",
      "teams": [
        { "key": "mclaren", "team": "mod.season-2008/mclaren", "vehicle": "mod.season-2008/mclaren",
          "drivers": [ { "driver": "game/lewis_hamilton", "number": 22 },
                       { "driver": "mod.season-2008/heikki_kovalainen", "number": 23 } ] }
      ],
      "rules": { "points": "mod.season-2008/2008", "refuelling": true }
    }
  },
  "era.box": { "f1_2008": { "package": "mod.season-2008/f1_2008", "image": "art/box_2008.png", "order": 2008 } }
}
```

</template>
</Example>
</template>
</Split>

[Seasons and GP/TT content](/std/content) covers boxes, packages and their rules in full, and [Session rules](/std/systems/sessions) covers points, formats and grid size.

## 4. The career season

<Split>

A career mode such as Progressive races a year through its **career season**: the year's box, its line-up by career slot, and each driver's ratings for the year. Registering it lets any career mode that covers 2008 use your pack.

`box` points at your package, and `calendar` at the 2008 calendar. The season has no data group of its own: a career mode makes it its own with a variant, as [Make a career mode](/make/career) shows. A career freezes each season's line-up and ratings when that season starts.

<template #example>
<Example root="mod.season-2008" code-file="native/src/career.cpp" files="swofty.json, native/src/season.cpp">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/career.hpp>
#include <swofty/systems/content.hpp>
#include <swofty/systems/drivers.hpp>

using namespace swofty;

void register_career()
{
    auto season = career::seasons.add("2008", {
        .year = 2008,                                // no data group: career modes take it as a variant
        .box = content::packages["mod.season-2008/f1_2008"],
        .calendar = career::calendars["mod.season-2008/2008"],
        .teams = {
            { .slot = 1, .team = teams::identities["mod.season-2008/mclaren"],
              .seats = { drivers::identities["game/lewis_hamilton"], drivers::identities["mod.season-2008/heikki_kovalainen"] } },
            { .slot = 2, .team = teams::identities["mod.season-2008/ferrari"],
              .seats = { drivers::identities["game/kimi_raikkonen"], drivers::identities["mod.season-2008/felipe_massa"] } },
        },
    });

    career::ratings.add("2008/game/lewis_hamilton", {
        .season = season, .driver = drivers::identities["game/lewis_hamilton"],
        .experience = 70, .racecraft = 88, .awareness = 84, .pace = 92,
    });
    // … one per driver
}
```

</template>
</Example>
</template>
</Split>

Call `register_career()` from `SWOFTY_REGISTER`. [Career](/std/systems/career) covers seasons, ratings and how career modes pick a box per year.

## swofty.json

```json
{
  "schema_version": 1,
  "id": "mod.season-2008",
  "name": "F1 2008",
  "version": "1.0.0",
  "author": "you",
  "category": "seasons",
  "compat": { "game": "f1_25", "launcher": ">=1.6.0" },
  "operations": { "native": [ { "dll": "native/season_2008.dll" } ] }
}
```

The pack needs no other addon. Tracks it doesn't ship are named by venue, so a 2008 calendar races Hockenheim when a track addon provides `venue/hockenheimring` (see [Add a track](/make/track)).

## Try it

```sh
native/build.sh
swofty addons install ./mod.season-2008
swofty addons enable mod.season-2008
swofty run --apply
```

Open Grand Prix, pick **F1 2008** in the Addons row, and race Albert Park. `swofty/loader.log`:

```text
[registry] register: mod.season-2008 (season_2008.dll) 2 engine_sound, 2 livery, 2 vehicle, 2 team.identity,
           2 driver.identity, 1 content.package, 1 era.box, 1 career.season, 20 career.driver_ratings (early stage)
[registry] applied at boot: content.package 1 (entrant list s08_f1_2008, 20 entrants)
[era]      era.box mod.season-2008/f1_2008: Addons row, order 2008
```

## When something's missing

- **A track the calendar names isn't installed:** that round is left out of the box's calendar, and of a career's 2008 season.
- **The player turns the pack off:** the F1 2008 box disappears. Careers that raced 2008 in it need the pack: the launcher warns before Play, and a guard stops the load. A career that hasn't reached 2008 yet stops at the season break if the pack is still off.
- **An AI mod changes every F1 car:** it skips the 2008 cars' protected pace, unless the player turns on **Also change year packs** for that mod.

## Publish

See [Packaging and publishing](/reference/addons/publishing). Bump the pack's version when its records change: careers that already raced a season keep what they froze.
