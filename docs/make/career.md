---
title: Make a career mode
description: A Driver Career mode that runs 2008 to 2012 on year packs, taking each year's box, line-up and ratings from the packs and changing the grid at every season break.
---

# Make a career mode

You'll build `mod.decades`, a Driver Career from 2008 to 2012. The player sees a **Decades** tile in the career menu. Each season races in that year's box, with its cars, liveries and rules, and at every season break the grid changes to the next year's real line-up.

The mode builds on five year packs, `mod.season-2008` to `mod.season-2012`, each made like the one in [Make a year pack](/make/year-pack). Each pack registers its year's career season: the box, the line-up and the ratings. Decades takes each one as a variant, so it inherits the pack's data, and adds the rules of its own mode.

## The folder

```text
mod.decades/
├── swofty.json
├── registry.json
├── art/tile.png
└── native/
    ├── build.sh
    ├── decades.dll
    └── src/decades.cpp
```

## 1. The mode and its tile

<Split>

A career **mode** is what the game saves a career as. Decades' mode is a variant of the 2026 Driver Career, so it inherits everything the stock mode does, including changes other addons make to it. It gets its own data group and save container, so Decades careers never mix with stock ones.

The **content** record is the tile in the career menu. It runs five seasons, and after 2012 the career ends.

<template #example>
<Example root="mod.decades" code-file="native/src/decades.cpp" data-file="registry/career.json" files="swofty.json, registry.json, art/tile.png, native/build.sh, native/decades.dll">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/events.hpp>
#include <swofty/systems/career.hpp>

using namespace swofty;

static const events::Selector kDecades = events::content("mod.decades/f1_career_decades");

SWOFTY_REGISTER
{
    auto mode = career::modes.variant("decades", career::modes["game/f1_career_2026"], [](career::ModeRecord &m) {
        m.data_group = "f1_career_decades";
        m.save_container = "driver_career_decades";
        m.title = "lng_decades_title";
    });

    career::contents.add("f1_career_decades", {
        .mode = mode,
        .tile = file("art/tile.png"),
        .model = career::contents["game/f1_career_2026"],
        .after_last = career::AfterLast::end,          // 2012 is the final season
    });

    register_seasons();                                // step 2
}
```

</template>
<template #data>

```json
{
  "career.mode": {
    "decades": { "extends": "game/f1_career_2026", "data_group": "f1_career_decades",
                 "save_container": "driver_career_decades", "title": "lng_decades_title" }
  },
  "career.content": {
    "f1_career_decades": { "mode": "mod.decades/decades", "tile": "art/tile.png",
                           "model": "game/f1_career_2026", "after_last": "end" }
  }
}
```

</template>
</Example>
</template>
</Split>

## 2. Five seasons from five packs

<Split>

Each Decades season is a variant of a pack's season. It inherits the pack's box, calendar, line-up and mid-season changes, and sets the Decades data group so the mode uses it. A pack update that fixes its line-up reaches Decades with no change here.

Ratings are their own records, one per driver per season. Decades makes a variant of each of the pack's ratings that points at its own season.

If two packs offer the same year, the career setup page lets the player pick one. Each season's box is chosen when the career is created.

<template #example>
<Example root="mod.decades" code-file="native/src/decades.cpp" title="Above SWOFTY_REGISTER" files="swofty.json, registry.json">
<template #code>

```cpp
static void register_seasons()
{
    for (int year = 2008; year <= 2012; ++year) {
        const std::string pack = std::format("mod.season-{}/{}", year, year);   // "mod.season-2008/2008"

        auto season = career::seasons.variant(std::to_string(year), career::seasons[pack],
                                              [](career::Season &s) { s.data_group = "f1_career_decades"; });

        for (const auto &id : career::ratings.all()) {
            auto r = career::ratings.get(id);
            if (r && r->season.id() == pack)
                career::ratings.variant(std::format("{}/{}", year, r->driver.id()), id,
                                        [season](career::DriverRatings &d) { d.season = season; });
        }
    }
}
```

</template>
</Example>
</template>
</Split>

`registry.json` requires the five packs' seasons, so the launcher checks them before Play:

```json
{
  "requires": [
    { "registry": "career.season", "version": "^1" },
    { "record": "career.season:mod.season-2008/2008", "for": "the 2008 season" },
    { "record": "career.season:mod.season-2009/2009", "for": "the 2009 season" },
    { "record": "career.season:mod.season-2010/2010", "for": "the 2010 season" },
    { "record": "career.season:mod.season-2011/2011", "for": "the 2011 season" },
    { "record": "career.season:mod.season-2012/2012", "for": "the 2012 season" }
  ]
}
```

## 3. The grid at every season break

<Split>

The game starts each season with its own driver market. Decades replaces that with the year's line-up when the season starts: `apply_lineup` signs every seat in the season record, and the report says what changed.

`ctx.record<career::Season>()` is this career's season as it was frozen when the season started. A career freezes each season's line-up, ratings and rules at that moment, so a running career gets pack fixes for every season it hasn't reached yet.

The handler only runs in Decades careers: `.in(kDecades)`.

<template #example>
<Example root="mod.decades" code-file="native/src/decades.cpp" title="Below SWOFTY_REGISTER" files="swofty.json, registry.json">
<template #code>

```cpp
SWOFTY_ON_LOAD
{
    events::on<career::events::SeasonStart>().in(kDecades)
        .after([](const career::events::SeasonStart &e, const events::Context &ctx) {
            auto season = ctx.record<career::Season>();
            if (!season)
                return;
            career::ApplyReport report;
            career::apply_lineup(*season, &report);
            swofty::log("[decades] %d: %zu seat(s) set, %zu unchanged, %zu refused",
                        e.year, report.changed, report.unchanged, report.refused);
        });
}

SWOFTY_ADDON()
```

</template>
</Example>
</template>
</Split>

To freeze every season when the career is created instead, set `m.freeze = career::Freeze::at_creation` on the mode. [Career](/std/systems/career#what-a-career-freezes) covers freezing, and [Events](/std/events) the other career events: contracts, the market, R&D.

## swofty.json

```json
{
  "schema_version": 1,
  "id": "mod.decades",
  "name": "Decades",
  "version": "1.0.0",
  "author": "you",
  "category": "career",
  "compat": { "game": "f1_25", "launcher": ">=1.6.0" },
  "requires": [
    { "id": "mod.season-2008" }, { "id": "mod.season-2009" }, { "id": "mod.season-2010" },
    { "id": "mod.season-2011" }, { "id": "mod.season-2012" }
  ],
  "operations": { "native": [ { "dll": "native/decades.dll" } ] }
}
```

## Try it

```sh
native/build.sh
swofty addons install ./mod.decades
for id in mod.season-2008 mod.season-2009 mod.season-2010 mod.season-2011 mod.season-2012 mod.decades; do
    swofty addons enable $id
done
swofty run --apply
```

Start a Decades career, finish or skip the 2008 season, and check the 2009 grid. `swofty/loader.log`:

```text
[registry] register: mod.decades (decades.dll) 1 career.mode, 1 career.content, 5 career.season, 110 career.driver_ratings
[registry] career season 2008 starts in mod.decades/f1_career_decades: snapshot of career.season 1, career.driver_ratings 22
[decades] 2008: 22 seat(s) set, 0 unchanged, 0 refused
[registry] career season 2009 starts in mod.decades/f1_career_decades: snapshot of career.season 1, career.driver_ratings 22
[decades] 2009: 20 seat(s) set, 2 unchanged, 0 refused
```

## When something's missing

- **A pack is off before Play:** `requires` stops Play and names it.
- **A pack goes missing mid-career:** the career's dependency record names every pack whose box it uses. The launcher warns before Play, and a guard stops the career loading with a popup naming the pack. Its saves are never changed.
- **A pack removes a driver Decades seats:** `apply_lineup` refuses that seat, keeps the game's driver in it, and the report counts it as refused.
- **A track a season's calendar names isn't installed:** that round is left out of the season's calendar.

## Publish

See [Packaging and publishing](/reference/addons/publishing). List the packs as requirements on the Addon Browser too, so the Browser installs them with Decades.
