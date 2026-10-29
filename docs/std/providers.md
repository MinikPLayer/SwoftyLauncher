---
title: Providers
description: Who fills a registry's base. Records added up front with Registry::add, dynamic providers that compute records on read, and shared ids such as venues that go to the last provider in load order.
---

# Providers

A **provider** is the addon that fills a record's **base**: the value every [layer](/std/layers) starts from. Progressive Career provides `swofty.progressive/2017`, its 2017 line-up, and a what-if addon layers that record to change the grid. A record has exactly one base, under the provider's own id. Every other addon changes it through a layer.

An addon provides in two ways, and can use both for one registry:

- **`add`**: records you know up front, added in `SWOFTY_REGISTER`.
- **`provide`**: a callback the core calls when a record is read. The core caches the result until you `invalidate` it.

Shared ids, such as the venue `venue/sochi`, are the one case where several addons provide the same id. The last one in load order wins (see [Shared ids](#shared-ids)).

## Records added up front

<Split>

Progressive Career keeps its history in its own `data/years.json`. At the early stage it reads that file and adds one `career.season` record per year.

`years.json` is Progressive's private format. The registry records are the public one: any addon can read the 2016 grid or layer it.

<template #example>
<Example root="swofty.progressive" code-file="native/src/register.cpp" data-file="registry/seasons.json" files="swofty.json, data/years.json, native/progressive.dll">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/career.hpp>
#include <swofty/systems/drivers.hpp>
#include <swofty/systems/teams.hpp>

#include "history.hpp"   // progressive::history(), built from data/years.json

using namespace swofty;

static Ref<drivers::Identity> driver(const std::string &key) { return drivers::identities["game/" + key]; }
static Ref<teams::Identity> team(const std::string &key) { return teams::identities["game/" + key]; }

SWOFTY_REGISTER
{
    for (const progressive::Year &y : progressive::history().years()) {     // 2016-2025
        career::Season s{ .data_group = "f1_career_progressive", .year = y.year };
        for (const progressive::Entry &e : y.entries)
            s.teams.at(e.slot) = { .slot = e.slot, .team = team(e.team),
                                   .seats = { driver(e.seats[0]), driver(e.seats[1]) } };
        for (const progressive::Swap &c : y.swaps)
            s.changes.at(c.round, c.slot, c.seat) = { .round = c.round, .slot = c.slot, .seat = c.seat,
                                                      .out = driver(c.out), .in = driver(c.in), .kind = career::ChangeKind::swap };
        career::seasons.add(std::to_string(y.year), std::move(s));          // "swofty.progressive/2016"
    }
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
{
  "career.season": {
    "2016": {
      "data_group": "f1_career_progressive",
      "year": 2016,
      "teams": [
        { "slot": 1, "team": "game/mercedes",   "seats": ["game/lewis_hamilton", "game/nico_rosberg"] },
        { "slot": 3, "team": "game/redbull",    "seats": ["game/daniel_ricciardo", "game/daniil_kvyat"] },
        { "slot": 8, "team": "game/toro_rosso", "seats": ["game/carlos_sainz-jr", "game/max_verstappen"] }
      ],
      "changes": [
        { "round": 5, "slot": 3, "seat": 1, "out": "game/daniil_kvyat", "in": "game/max_verstappen", "kind": "swap" },
        { "round": 5, "slot": 8, "seat": 1, "out": "game/max_verstappen", "in": "game/daniil_kvyat", "kind": "swap" }
      ]
    },
    "2017": { "...": "..." }
  }
}
```

</template>
</Example>
</template>
</Split>

```text
[registry] register: swofty.progressive (progressive.dll) 10 career.season, 1 career.mode (early stage, 2.1 ms)
```

## Dynamic providers

<Split>

A **dynamic provider** is a callback. The core calls it the first time a record is read, and caches the result until the provider calls `invalidate`.

Return `std::nullopt` for an id that isn't yours, and the core tries the next provider. The second callback lists the ids you answer. [Listing](/std/reading#listing) finds them through it.

<template #example>
<Example root="swofty.progressive" code-file="native/src/ratings.cpp" files="swofty.json, native/src/register.cpp, native/progressive.dll">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/career.hpp>

#include "ratings_model.hpp"

using namespace swofty;

// id: "swofty.progressive/<year>/<driver>"
static std::optional<career::DriverRatings> ratings(std::string_view id)
{
    constexpr std::string_view prefix = "swofty.progressive/";
    if (!id.starts_with(prefix))
        return std::nullopt;                // not ours
    std::string_view rest = id.substr(prefix.size());              // "2017/game/max_verstappen"
    std::size_t slash = rest.find('/');
    if (slash == std::string_view::npos)
        return std::nullopt;
    int year = std::stoi(std::string(rest.substr(0, slash)));
    return progressive::ratings_model().season_start(std::string(rest.substr(slash + 1)), year);
}

static void list(std::function<void(std::string_view)> add)
{
    for (int year = 2016; year <= 2025; ++year)
        for (const std::string &d : progressive::history().drivers(year))
            add("swofty.progressive/" + std::to_string(year) + "/" + d);
}

SWOFTY_ON_LOAD
{
    career::ratings.provide(ratings, list);
}
```

</template>
</Example>
</template>
</Split>

When the data behind a dynamic record changes, the provider says so:

```cpp
void on_ratings_model_reloaded()
{
    career::ratings.invalidate();                                                       // every record
    career::ratings.invalidate(career::ratings["swofty.progressive/2017/game/max_verstappen"]);  // one record
}
```

The core drops the cached value, calls the provider again on the next read, and tells [watchers](/std/reading#watching).

## Both together

An addon can `add` the records it knows and `provide` the rest. When both answer the same id, the dynamic value wins. When the callback returns `std::nullopt`, the added record stands.

```cpp
SWOFTY_REGISTER
{
    // every driver's start-of-season ratings for 2016 to 2025, from data/ratings.json
    for (const auto &[key, r] : progressive::stored_ratings())
        career::ratings.add(key, r);                    // "2017/game/max_verstappen" -> "swofty.progressive/2017/game/max_verstappen"
}

SWOFTY_ON_LOAD
{
    // in game, drivers the file doesn't list come from the ratings model
    career::ratings.provide(ratings, list);
}
```

## One provider per record

`add` always uses your own id, so only your addon can provide an id. When it provides the same key twice (two `add` calls, or a data file and code), the later one wins and the core logs the clash:

```text
[registry] career.season swofty.progressive/2017: added twice by swofty.progressive
           (registry/seasons.json, then progressive.dll); progressive.dll wins. The conflicts report lists it.
```

To change another addon's records, use a [layer](/std/layers), not a second provider.

## Shared ids

The core owns a few kinds of shared ids, each for something real that several addons can bring:

- `venue/<key>`: a real circuit, provided by a track;
- `person/<key>`: a real driver, provided by a driver record;
- `constructor/<key>`: a real team, provided by a team record.

A record says which shared ids it provides. Other records refer to the shared id, and the reference resolves to the provider. When two enabled addons provide the same id, the last one in load order wins, the same as every other contest between addons. The core logs the result, and the conflicts report lists the loser.

```cpp
// mod.sochi: a track that is the real Sochi Autodrom
tracks::tracks.add("sochi", { .code = "sochi", .base = tracks::tracks["game/baku"],
                              .provides = { tracks::venues["venue/sochi"] } });
```

```text
[registry] track.venue venue/sochi: provided by mod.sochi/sochi and community.sochi-2014/sochi;
           community.sochi-2014/sochi wins (later in load order). The conflicts report lists it.
```

A 2016 calendar that asks for `venue/sochi` gets the winning track. Moving `mod.sochi` later in the load order makes it the winner instead. The [Tracks](/std/systems/tracks), [Drivers](/std/systems/drivers) and [Teams](/std/systems/teams) pages show each provider's record.

## Threads and timing

- `add`, `variant` and `provide` work in `SWOFTY_REGISTER`. On a registry the game reads only at boot (`Applies::boot`), that's the only place they work: after the boot apply, the core refuses them with `late` and a log line that says to move the call into `SWOFTY_REGISTER`.
- On every other registry they also work at any time while the game runs. A record added then takes effect the next time the game reads that data (see [When changes apply](/std/build)).
- A call made before the core is set up, from `SwoftyAddonInit`, is queued and applied in load order.
- A dynamic provider runs on the thread that reads the record, under the registry's lock. It must return quickly, and it must not read the same registry: a re-entrant read gets nothing, with `Status::late`.

## Without code

A data file adds records up front, the same as `add`. Dynamic providers need code.

```json
// registry/seasons.json: keys are your own, refs are full ids
{ "career.season": { "2016": { "data_group": "f1_career_progressive", "year": 2016, "teams": [ "..." ] } } }
```
