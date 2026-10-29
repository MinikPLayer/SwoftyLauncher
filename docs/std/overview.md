---
title: How the pieces fit
description: The six parts of the standard library, what each one is for and why they are separate, in one addon that uses all of them, and a list of which part to reach for.
---

# How the pieces fit

An addon made for 1.6 works with the game and with other addons through six parts. Each part answers one question, and the docs have a section for each:

- **[The registry](/std/registry)**: what the game has. Seasons, cars, engine sounds, tracks and career line-ups are records. Your addon adds records under its own id, or layers changes onto anyone's records, and the core puts them into the game when the game reads that data.
- **[Events](/std/events)**: when something happens. A season starts, a session loads, a car is built. Your handler runs at that moment, with the mode, the career, the session and the player in its context, and can change what the game is about to use.
- **[Hooks](/std/hooks)**: a game function many addons need. The core owns the function and runs every subscribed addon in one ordered chain, so two addons that change the same function work side by side. An addon can also define hook points of its own for others.
- **[Services](/std/services)**: calling another addon. One addon publishes a versioned table of functions under a name, and others look it up.
- **[Operations](/std/operations)**: changing the running game. The core's verified verbs, such as seating a driver or setting ratings, take the same values the registries hold.
- **[Dependencies and guards](/std/dependencies)**: what your addon needs. The launcher, the Addon Browser and the game check them, and a guard turns the player back with a message that names the fix.

They are separate because they change at different times and belong to different owners. Records are data that anyone can read and override, so they sit in a registry instead of an addon's private files. Events and hooks are moments in the game, which the core owns. Services belong to the addon that publishes them, operations belong to the core, and dependencies are checked before the game starts.

## One addon, every part

<Split>

A history addon adds its seasons to the registry, seats each season's drivers when the season starts, offers its history to other addons, and needs Progressive Career.

- `career::seasons.add` puts each season in the **registry**.
- `events::on<SeasonStart>` runs at the **event**.
- `career::apply_lineup` is the **operation** that seats the drivers.
- `services::publish` offers a **service**.
- `registry.json` declares the **dependency**.

<template #example>
<Example root="mod.history" code-file="native/src/history.cpp" data-file="registry.json" files="swofty.json, native/history.dll">
<template #code>

```cpp
#include <swofty/events.hpp>
#include <swofty/registry.hpp>
#include <swofty/services.hpp>
#include <swofty/systems/career.hpp>

#include "history_api.hpp"   // struct HistoryApi: name "mod.history", version 1, function pointers

using namespace swofty;

SWOFTY_REGISTER
{
    for (int year = 2016; year <= 2020; ++year)
        career::seasons.add(std::to_string(year), history::season(year));          // registry
}

SWOFTY_ON_LOAD
{
    events::on<career::events::SeasonStart>().after([](const career::events::SeasonStart &e, const events::Context &) {
        if (auto s = career::seasons.get(career::seasons["mod.history/" + std::to_string(e.year)]))
            career::apply_lineup(*s);                                               // operation
    });

    static const HistoryApi kApi{ &history::team_of };
    services::publish(kApi);                                                        // service
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
{
  "requires": [
    { "id": "swofty.progressive", "version": ">=1.3.0" },
    { "registry": "career.season", "version": "^1" }
  ]
}
```

</template>
</Example>
</template>
</Split>

## Which part do I need?

- **Add content**, such as a season, a car, a sound or a track: the [registry](/std/registry), with `add`. Start from [Keys, bases and variants](/std/bases).
- **Change another addon's content or the game's**: a [layer](/std/layers). Use `where`, `under` or `tagged` to change many records at once.
- **Do something at a moment**, such as a season start or a session load: an [event](/std/events).
- **Change how a game function behaves**: a [hook](/std/hooks).
- **Let other addons call your code**: a [service](/std/services). For data they should read or override, use a registry instead.
- **Change the running game**, such as seating a driver: an [operation](/std/operations).
- **Need another addon or a track**: a [dependency](/std/dependencies), plus a guard if the player must not start without it.

Each game system, such as cars, tracks or careers, has a page in [Game systems](/std/systems/) that lists its registries, events, hook points and operations.
