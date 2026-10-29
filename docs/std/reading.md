---
title: Reading
description: Effective values, origins, listing and watching registry records through typed handles.
---

# Reading

Every read returns a record's **effective value**: the base from its [provider](/std/providers) with every [layer](/std/layers) applied in load order. Inside a career, a [snapshot registry](/std/scopes#career-snapshots) returns what the career froze for the seasons it has started.

<Split>

A Choose Driver page logs the grid the player will race, says who set Mercedes' first seat, and rebuilds its cards whenever a season changes.

- `get` returns a copy of the record's struct.
- `origin` names the addon that set a field.
- `all` lists ids under a prefix.
- `watch` calls back on every change.

Reading needs code. Data files only provide and layer.

<template #example>
<Example root="example.grid-cards" code-file="native/src/grid.cpp" files="swofty.json, native/grid.dll">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/career.hpp>

#include "cards.hpp"

using namespace swofty;

static Token g_watch;

static void log_grid(Ref<career::Season> id)
{
    std::optional<career::Season> s = career::seasons.get(id);
    if (!s)
        return;
    for (const career::SeasonTeam &t : s->teams) {
        std::string a(t.seats[0].id()), b(t.seats[1].id());
        swofty::log("[grid] %d slot %d: %s, %s", s->year, t.slot, a.c_str(), b.c_str());
    }
}

SWOFTY_ON_LOAD
{
    for (Ref<career::Season> id : career::seasons.all("swofty.progressive/"))
        log_grid(id);

    auto y2017 = career::seasons["swofty.progressive/2017"];
    swofty::log("[grid] mercedes seat 1 set by %s",
                career::seasons.origin(y2017, "teams/slot=1/seats/0").c_str());

    g_watch = career::seasons.watch([](Ref<career::Season> id) {
        if (id.id().starts_with("swofty.progressive/"))
            cards::refresh();          // Choose Driver rebuilds its cards from the new grid
    });
}

SWOFTY_ADDON()
```

</template>
</Example>
</template>
</Split>

```text
[grid] 2017 slot 1: game/fernando_alonso, game/valtteri_bottas
[grid] 2017 slot 4: game/lewis_hamilton, game/stoffel_vandoorne
[grid] mercedes seat 1 set by tails.whatif-history
```

## Effective values

`get` returns `std::nullopt` when the record doesn't exist, when no provider answers, or when the registry isn't declared. `status` says which:

```cpp
switch (career::seasons.status(id)) {
case Status::ok:          break;
case Status::no_record:   break;   // no provider answered this id
case Status::no_registry: break;   // nothing declared career.season (the core is older than the header?)
case Status::invalid:     break;   // every candidate value failed validation
case Status::late:        break;   // a re-entrant read from inside this registry's provider
}
```

- **A `Ref` is checked when the registry resolves,** not when you write it. `cars::vehicles["swofty.classic-seasons-2017/feraari"]` compiles, and reading it gives `Status::no_record`.
- **A `Ref` in the value you get back is already resolved:** `resolved()` is true, or the element that held an `.optional()` ref is gone, or an `.or_else()` ref points at its fallback.
- **The game's database loads after `SWOFTY_REGISTER` runs.** A `game/` record has no value to read there. Layers and selectors you register on `game/` records run after the database loads.

## Origins

`origin` says which addon set a field of the effective value. The field is a path into the value. An element of a `Keyed` list is named by its key, not its position:

```cpp
auto y2017 = career::seasons["swofty.progressive/2017"];
career::seasons.origin(y2017, "teams/slot=1/seats/0");   // "tails.whatif-history": its layer set Mercedes' first seat
career::seasons.origin(y2017, "teams/slot=2/seats/0");   // "swofty.progressive": the provider's base value
cars::vehicles.origin(cars::vehicles["swofty.classic-seasons-2017/ferrari"], "engine_sound");   // "mod.esp"
```

The core records origins from the fields each edit changed. The conflicts report and Send Logs' `registry/effective.json` show them.

## Listing

```cpp
// every id in a registry
for (Ref<career::Season> id : career::seasons.all())
    swofty::log("%.*s", int(id.id().size()), id.id().data());

// the ids under a prefix: every Progressive season
for (Ref<career::Season> id : career::seasons.all("swofty.progressive/"))
    ...;
```

Listing includes records added up front, from data files and from layers. A dynamic provider's ids are listed when it passes a `list` callback to `provide` (see [Dynamic providers](/std/providers#dynamic-providers)).

## Watching

A watcher runs when a record's effective value changes: a provider invalidated it, a layer changed, or a career loaded and its snapshot took over.

```cpp
static Token g_watch;

SWOFTY_ON_LOAD
{
    g_watch = career::seasons.watch([](Ref<career::Season> id) {
        if (id.id().empty())
            return;                    // end of a batch
        if (id.id().starts_with("swofty.progressive/"))
            cards::refresh();
    });
}
```

- **Watchers run on the flow thread,** after the change is visible to `get`.
- **One change can touch many records.** The core calls a watcher once per changed id, in id order, then once with an empty `Ref` (`id()` is empty) to say the batch is done.
- **Destroying the `Token`, or `g_watch.reset()`, stops it.**

## Threads and cost

- **`get`, `status`, `origin` and `all` work from any thread.**
- **Values are copied out.** Nothing a read returns points into the registry. Each `get` makes a new copy, so read a record once and keep the value for as long as you need it.
- **Hook points the game calls every frame,** such as the HUD's team logos and strings, use tables the core builds at content or session moments (see [Hooks](/std/hooks)).
- **A read on the flow thread during a career load waits until the snapshot is in place.** It never sees a half-loaded career.
