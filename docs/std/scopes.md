---
title: Scopes and snapshots
description: Global, profile and career scopes; how a career freezes its line-ups, ratings and rules season by season; and the career's save entry that holds them.
---

# Scopes and snapshots

Every [layer](/std/layers) has a **scope**: how long it lasts, and whether it belongs to every profile, one launcher profile or one career. A career also **freezes** its history: when each season starts, the core keeps that season's line-ups, ratings and rules as they are, and the career reads those from then on.

<Split>

Tails' "what if" addon uses all three scopes on Progressive Career's 2017 season:

- **global:** a change that applies wherever the addon is on, in every profile;
- **profile:** the grid the player edited on the addon's launcher page, saved in this launcher profile's storage;
- **career:** a swap made by the in-game editor, saved with the career that's loaded.

A scope is the last argument of `layer`. It defaults to `Scope::global`.

<template #example>
<Example root="tails.whatif-history" code-file="native/src/whatif.cpp" data-file="registry/layers.json" files="swofty.json, registry.json, native/whatif.dll, bin/whatif.exe">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/storage.hpp>
#include <swofty/systems/career.hpp>
#include <swofty/systems/drivers.hpp>

using namespace swofty;

static Ref<drivers::Identity> driver(std::string_view id) { return drivers::identities[id]; }

SWOFTY_REGISTER
{
    auto y2017 = career::seasons["swofty.progressive/2017"];

    // global: every profile the addon is on in
    career::seasons.layer(y2017, [](career::Season &s) {
        s.teams.at(1).seats = { driver("game/fernando_alonso"), driver("game/valtteri_bottas") };
    });

    // profile: storage reads the active launcher profile's copy of whatif.json
    std::string text;
    if (storage::read("whatif.json", text) == storage::Result::ok)
        for (const whatif::Seat &seat : whatif::parse(text))
            career::seasons.layer(career::seasons[seat.season], [seat](career::Season &s) {
                s.teams.at(seat.slot).seats[seat.seat] = driver(seat.driver);
            }, Scope::profile);
}

// career: the in-game editor, while a career is loaded
void swap_from_round(int round)
{
    career::seasons.layer(career::seasons["swofty.progressive/2017"], [round](career::Season &s) {
        career::SeasonChange &c = s.changes.at(round, 1, 1);   // round, slot 1, seat 1; appended when missing
        c.out = driver("game/valtteri_bottas");
        c.in = driver("game/esteban_ocon");
        c.kind = career::ChangeKind::swap;
    }, Scope::career);
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
// a data file is always a global layer
{
  "career.season": {
    "swofty.progressive/2017": {
      "teams": [ { "slot": 1, "seats": ["game/fernando_alonso", "game/valtteri_bottas"] } ]
    }
  }
}
```

</template>
</Example>
</template>
</Split>

```text
[registry] register: tails.whatif-history (whatif.dll) 3 layers on career.season (early stage, 0.3 ms)
[registry] career.season swofty.progressive/2017: edit by tails.whatif-history (global) changed /teams/slot=1/seats
[registry] career.season swofty.progressive/2017: edit by tails.whatif-history (profile) changed /teams/slot=4/seats/0
[registry] layer from tails.whatif-history on career.season swofty.progressive/2017 registered at run time;
           careers that haven't started their 2017 season get it when it starts
```

```cpp
enum class Scope {
    global,   // every profile the addon is on in; the addon's code and its data files
    profile,  // one launcher profile; the addon's code applies what it reads from that profile's storage
    career,   // one career; written in game while that career is loaded, saved in its save entry
};
```

A scope says how long a layer lasts and who owns it. Where a layer applies, such as one mode or one setup mode, is a separate filter, `.in()` (see [Where a layer applies](/std/layers#where-a-layer-applies)).

## Global

A global layer applies wherever the addon is on. Layers in `SWOFTY_REGISTER` and every data file are global unless they say otherwise. `SWOFTY_REGISTER` runs at every game start, so a layer there is on for as long as the addon is.

A global layer added later, at run time, lasts until the addon clears it or the game closes:

```cpp
career::seasons.layer(y2017, give_alonso_the_mercedes);   // from SWOFTY_ON_LOAD or later: this session
career::seasons.clear_layer(y2017);                         // Scope::global by default
```

## Profile

A profile layer belongs to one launcher profile. Players can keep a "what if" profile and a real-history profile side by side, and switch between them in the launcher.

Each launcher profile has its own addon storage. The addon's launcher page saves the player's edits in the storage of the profile being edited. In game, `swofty::storage` reads the active profile's storage. The addon's code reads the file in `SWOFTY_REGISTER` and applies it with `Scope::profile`, as in the example above. The page side is in [The launcher side](/std/launcher#editing-a-layer-from-a-launcher-page).

- **The profile layer changes when the player edits the page and starts the game again.** In game, an addon doesn't rewrite it. A change made in game uses `global` (this session) or `career` (this career).
- **Within one addon,** its `global` layer applies before its `profile` layer.
- **A data file can't be profile-scoped.** Per-profile data needs code that reads it.
- **Game saves are shared by every profile.** Each career gets an id when it's created, and the launcher records which profile started it.

## Career

A career layer belongs to the career that's loaded, and is saved in that career's [save entry](#the-career-s-save-entry). Use it for changes made inside a career, for example an in-game editor that moves a driver mid-season: `swap_from_round(8)` above puts Ocon in the Mercedes from round 8 of the 2017 season, in this career only.

With no career loaded, the layer is refused with `late`:

```text
[registry] career.season swofty.progressive/2017: career layer from tails.whatif-history refused (late): no career is loaded
```

## Career snapshots

A career freezes its history season by season. When a season starts, the core takes the records that season uses from every **snapshot registry**, a registry declared with `.snapshot = true`, and keeps them in the career's save entry. That frozen copy is the **snapshot**. `career.season` (line-ups) and `career.driver_ratings` (ratings) are snapshot registries, and so are the rules registries such as `tyre.rules`.

In 1.6.0 the save default is dry: the core builds the snapshot and logs its size, and writes it into the save only with `SWOFTY_REGISTRY_SAVE=on` (see [Career](/std/systems/career)). With the default, a career reads every registry live.

Progressive's 2016 career shows what reaches it and what doesn't:

- **When the career is created,** the box each season uses (`career::Season::box`) is chosen and fixed for the career.
- **When the 2016 season starts,** the core freezes the 2016 line-up, the 2016 ratings and the 2016 rules. Later changes to 2016 records don't reach this career.
- **Seasons the career hasn't reached stay live.** A fix to Progressive's 2019 line-up reaches this career when it gets to 2019. A what-if edit reaches its next season.
- **Drivers and tracks installed mid-career join at the next season.**

A record belongs to a season by its year: `career::Season::year`, `career::DriverRatings::season`, or a `year(...)` selector in a rules record's `within`. A snapshot record with no year, such as an addon's own preset, is frozen again at each season start.

<Split>

While a career is loaded:

- `get` returns **snapshot + career layers** for the seasons the career has started. For later seasons it returns the live value plus career layers.
- Registries without a snapshot read live: base plus every layer.
- [Watchers](/std/reading#watching) fire once for every snapshot record when the career loads, and again when it unloads.

An addon's own registry takes the same options. `.snapshot_budget` is in bytes after compression, for one season's records of that registry in one career. It defaults to 65536 (64 KB).

<template #example>
<Example root="tails.whatif-history" code-file="native/src/presets.cpp" files="swofty.json, native/src/whatif.cpp, native/whatif.dll">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/career.hpp>

using namespace swofty;

// the core's declarations, in swofty/systems/career.hpp:
//   inline Registry<Season> seasons{"career.season", { .applies = Applies::read, .snapshot = true, .snapshot_budget = 65536 }};
//   inline Registry<DriverRatings> ratings{"career.driver_ratings", { .applies = Applies::read, .snapshot = true, .snapshot_budget = 65536 }};

// an addon's own registry, frozen into each career too; it has no year, so each season start freezes it again
struct Preset {
    std::string name;
    int start_year = 2016;
    static constexpr Version version{1, 0};
};
SWOFTY_RECORD(Preset, name, start_year);
inline Registry<Preset> presets{"tails.whatif.presets", { .snapshot = true, .snapshot_budget = 16384 }};

SWOFTY_ON_LOAD
{
    // fires for every snapshot record when a career loads, and again when it unloads
    static Token watch = career::seasons.watch([](Ref<career::Season> id) {
        if (id.id().empty())
            return;                                   // the end of the batch
        if (auto s = career::seasons.get(id))         // the snapshot, plus this career's layers
            swofty::log("[whatif] %.*s: %zu teams", int(id.id().size()), id.id().data(), s->teams.size());
    });
}

SWOFTY_ADDON()
```

</template>
</Example>
</template>
</Split>

```text
new career, data group f1_career_progressive, first season 2016
  career.created
    core: career id saved; each season's box chosen (2016-2026); stat-id map saved
  career.season.start 2016
    core: freeze career.season          1 record     3.8 KB -> 0.9 KB compressed
    core: freeze career.driver_ratings  38 records   8.1 KB -> 1.6 KB compressed
    core: saved in the career's save entry (swofty_registry)
  ...
  Progressive fixes its 2019 line-up while the career is in 2017
  -> 2016 and 2017 stay as they were frozen; the career gets the fix when its 2019 season starts

load the career again later
  career.loaded
    core: career.season and career.driver_ratings for 2016-2017 read from the save entry (schema 1.0, current 1.1: read as is)
```

### Freezing everything at creation

A career's owner can freeze every season when the career is created instead. It sets `freeze` on its career mode:

```cpp
career::modes.add("fixed_history", {
    .data_group = "f1_career_fixed_history",
    .title = Text::key("lng_fixed_history"),
    .kind = career::Kind::driver_career,
    .freeze = career::Freeze::at_creation,     // default: career::Freeze::per_season
});
```

With `career::Freeze::at_creation`, the core freezes every season's records at `career.created`, and later changes reach only new careers. A career's hub can offer the same choice to the player as a "Freeze content" toggle.

### The career's save entry

The core keeps everything it saves for a career in that career's entry in `swofty_registry`, one root child of the profile save:

- the career's id;
- the snapshot, season by season;
- the career's layers;
- the career's stat-id map;
- the career's dependency record (see [Dependencies and guards](/std/dependencies)).

The game never decodes `swofty_registry`. The core holds its bytes outside the game's heaps and writes them into the save stream through the save splice. The game's own `SAVE_DATA` heap is 3 MB for the whole profile save and has no room for it: a 16,368-byte copy failed there with 5,200 bytes free. An addon that isn't installed when the save is written doesn't lose its records: the save splice keeps entries it doesn't recognise.

**The stat-id map** lists the stat id of every record the career's save refers to: cars, teams, drivers, looks and tracks. Stat ids come from one store per install, in the launcher's data folder, at `$DATA/registry/ids.json`. The core reuses an id only when no career uses it. When a career loads, the core checks the install's ids against the career's map, so a save synced to another PC can't swap a track or a driver.

### Budget

- **Each snapshot registry has a budget,** `.snapshot_budget`: bytes after compression, for one season's records, per career. The default is 65536.
- **A career's whole entry is capped at 256 KB.**
- **Over budget,** the core doesn't save that season's records of the registry, logs the sizes, and the career reads that registry live for that season.
- **These limits bound the bytes in the save file.** The game's 3 MB heap never holds them.

```text
[registry] career.driver_ratings 2019: snapshot 71 KB compressed, budget 64 KB; not saved, this career reads it live for 2019
```

### Migration

A snapshot keeps the schema version it was written with. On load:

- **Same major, older or newer minor:** read as is. Fields the reader doesn't know are kept (see [the tolerant reader rule](/std/versioning#the-tolerant-reader-rule)).
- **Older major:** the registry owner's `migrate` converts it. Without one, the core upgrades it through the version [adapter](/std/versioning)'s `up`, and logs that it did.

```cpp
// the core's migration for career.season from schema 1 to schema 2 (an example: schema 2 turns each seat into a driver and a number)
swofty::migrate(career::seasons, career::v2::seasons, [](career::Season v1) {
    career::v2::Season v2{ .data_group = v1.data_group, .year = v1.year };
    for (const career::SeasonTeam &t : v1.teams) {
        career::v2::SeasonTeam &team = v2.teams.at(t.slot);
        team.team = t.team;
        for (std::size_t i = 0; i < t.seats.size(); ++i)
            team.seats[i] = { .driver = t.seats[i], .number = std::nullopt };
    }
    v2.changes = v1.changes;
    return v2;
});
```
