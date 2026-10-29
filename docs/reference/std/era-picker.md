---
title: Era picker boxes
description: Add a box to the Addons row of the Grand Prix and Time Trial era picker with swofty/era.hpp.
---

# Era picker boxes

`swofty/era.hpp` adds your boxes to the Addons row of the era picker, the row of boxes shown after Grand Prix or Time Trial.

```cpp [native/src/boxes.cpp]
#include <iterator>

#include <swofty/era.hpp>

namespace {

struct Season {
    const char *entrant_list;
    const char *title;
    const char *image;
    int32_t order;
};

// one box per entrant list; the lists come from this addon's database operations
constexpr Season kSeasons[] = {
    {"cs_f1_2016", "lng_cs_f1_2016", "#overrides.home_screen.tiles_solo_6x6[cs_f1_2016]", 2016},
    {"cs_f1_2017", "lng_cs_f1_2017", "#overrides.home_screen.tiles_solo_6x6[cs_f1_2017]", 2017},
};

// the check function runs during addon init, before the core's mixin commit: register boxes here
bool init()
{
    if (!swofty::era::available()) {
        swofty::log("[classic-seasons] this core has no era picker boxes; the seasons get no box");
        return true; // still load: the rest of the addon works without boxes
    }
    int added = 0;
    for (const auto &s : kSeasons)
        added += swofty::era::add_box({s.entrant_list, s.title, s.image, s.order}) == swofty::era::Result::ok;
    swofty::log("[classic-seasons] %d of %zu season box(es) registered", added, std::size(kSeasons));
    return true;
}

}

SWOFTY_ADDON(init)
```

```json [swofty.json]
{
  "schema_version": 1,
  "id": "author.classic-boxes",
  "name": "Classic Boxes",
  "version": "1.0.0",
  "author": "Your name",
  "kind": ["database", "native"],
  "compat": { "game": "f1_25" },
  "requires": [{ "id": "swofty.core", "version": ">=0.2.13" }],
  "operations": {
    "database": ["db/season.json"],
    "assets": ["lang/core.json"],
    "native": [{ "dll": "native/classic_boxes.dll" }]
  }
}
```

`db/season.json` adds the entrant lists ([The data side](#the-data-side)) and `lang/core.json` the `lng_cs_f1_*` titles. What `swofty/loader.log` shows:

```text
[era] box cs_f1_2016 from author.classic-boxes registered (title lng_cs_f1_2016, image #overrides.home_screen.tiles_solo_6x6[cs_f1_2016], order 2016): queued for the next mixin commit
[era] box cs_f1_2017 from author.classic-boxes registered (title lng_cs_f1_2017, image #overrides.home_screen.tiles_solo_6x6[cs_f1_2017], order 2017): queued for the next mixin commit
[classic-seasons] 2 of 2 season box(es) registered
[era] 2 box(es) registered; offering the era picker hooks
[era] era picker hooks installed; addon boxes show from the next build of the picker
[era] era select: 6 stock box(es), 2 addon box(es) of 2 registered
[era] swofty_f1 offers only cs_f1_2016 (of 2 lists)
```

- **Header:** `swofty/era.hpp`, namespace `swofty::era`
- **Table:** `SwoftyApi::era` (`SwoftyEra`), API version 8, feature-detected (see [Availability](#availability)). Added in launcher 1.3.6.
- **Feature:** none
- **Thread:** `add_box`: addon init. `available` and `count`: any

## The two rows

The era picker is the row of boxes shown after Grand Prix or Time Trial, and on the split-screen game mode select: F1 2026, F1 2025, F2 2025 and so on. SwoftyMod gives it a second row:

```text
Built In   [F1 2026] [F1 2025] [F2 2025] ...     the game's own boxes
Addons     [2016] [2017] [2018] ...              boxes addons register; this row scrolls on its own
```

**Up** and **Down** move between the rows. The core builds your box every time the picker is built and presses it like a stock box. When it is pressed, the core narrows the host setup mode to the box's entrant list, so Time Trial loads only that list's cars. The data side (the entrant list, its teams and drivers, and its place in the host setup mode) comes from your addon's [database operations](/reference/ops/database).

## Registering a box

Call `add_box()` from the check function you pass to `SWOFTY_ADDON`, which runs before the core's mixin commit:

```cpp
swofty::era::add_box({
    .entrant_list = "cs_f1_2016",                                  // the list the box selects
    .title = "lng_cs_f1_2016",                                     // empty: lng_cs_f1_2016
    .image = "#overrides.home_screen.tiles_solo_6x6[cs_f1_2016]",  // empty: this same path
    .order = 2016,                                                 // default 0
});
```

- `entrant_list`: the entrant list the box selects. Its press raises `entrant_list_selected` with it. 1 to 63 letters, digits or underscores. One box per entrant list.
- `title`: the `lng_*` key the box shows. Add the string with your addon's language table. Empty means `lng_<entrant_list>`.
- `image`: the texture path the box shows. Empty means `#overrides.home_screen.tiles_solo_6x6[<entrant_list>]`: add an entry with that name to the `home_screen.tiles_solo_6x6` overrides and a slice to its texture array.
- `order`: boxes show in ascending `order`, then in registration order. Default `0`.

`title` and `image` are at most 255 characters each. The strings are copied. Up to 64 boxes can be registered, across every addon.

| `add_box()` returns | When |
|---|---|
| `ok` | The box is queued for the next mixin commit, or live from the next build of the picker if the hooks are already installed |
| `invalid` | A bad entrant list name, an over-long string or a second box for the same entrant list (the log says which) |
| `unavailable` | An older core, or the era picker hooks could not be installed on this build |

A refused box logs the reason:

```text
[era] box cs_f1_2016 from author.other refused: a box for this entrant list is already registered by swofty.classic-seasons
[era] box my-list from author.other refused: entrant_list must be 1 to 63 letters, digits or underscores
[era] box cs_f1_2099 from author.other refused: the core holds at most 64 boxes
```

## The data side

A box only selects an entrant list. The list itself must be in the game database, and in the entrant lists of the core's host setup mode, `swofty_f1`. The core adds `swofty_f1` (a game setup mode and its entrant setup mode) to every F1 mode except the careers and Braking Point. Your database operations then:

1. add the entrant list (`entrants.entrant_lists`), its teams and its drivers;
2. `list-append` the entrant list onto the `swofty_f1` entrant setup mode's `entrant_lists` and `selectable_entrant_lists`;
3. add the `game_setup.scenarios` row the game checks the list against.

Steps 2 and 3, from Classic Seasons 2023's `db/season.json`:

```json [db/season.json]
{ "op": "list-append", "select": "entrants.entrant_setup_modes.entrant_setup_mode[_metadata_=swofty_f1]",
  "field": "entrant_lists", "value": "cs_f1_2023" },
{ "op": "list-append", "select": "entrants.entrant_setup_modes.entrant_setup_mode[_metadata_=swofty_f1]",
  "field": "selectable_entrant_lists", "value": "cs_f1_2023" },
{ "op": "add-row", "table": "game_setup.scenarios", "from": "game_setup.scenarios.scenario[_metadata_=f1]",
  "set": { "_metadata_": "cs_f1_2023", "display_name": "lng_cs_f1_2023", "entrant_list": "cs_f1_2023",
           "game_setup_mode": "swofty_f1", "statId": "22" } }
```

Every addon shares `swofty_f1`, so the game's per-mode setup list stays the size it was. See [Rows SwoftyMod adds](/reference/ops/database#rows-swoftymod-adds).

Each time the core builds the picker, it checks every box with the stock check (`FeTile_Is_EraScenarioAvailable_Thunk(mode, "swofty_f1", entrant list)`) and skips, with one log line, a box whose entrant list the screen's mode does not offer under `swofty_f1`. A box for a list that a missing database operation leaves out never reaches the game:

```text
[era] box cs_f1_2016 from swofty.classic-seasons skipped: this screen's mode does not offer the entrant list under swofty_f1 (it must exist and be in swofty_f1's entrant lists)
```

## Pressing a box

When a box is pressed, every `swofty_f1` entrant setup mode offers only that box's entrant list until another box is pressed or the picker is built again, and the setup's selected list is cleared. Time Trial loads every car of every list the entrant setup mode offers and the Grand Prix takes its first list, so this is what makes each box load its own season:

```text
[era] swofty_f1 offers only cs_f1_2017 (of 9 lists)
```

## Availability

```cpp
if (!swofty::era::available())
    swofty::log("[my-addon] this core has no era picker boxes");
int boxes = swofty::era::count(); // boxes every addon has registered so far
```

`available()` is true when the running `swofty_core.dll` exports `SwoftyCoreEra` and its era picker hooks are not off. An addon built against this header also loads on an older core, and sees `available()` false and `add_box()` returning `unavailable` there. `count()` returns how many boxes every addon has registered.

An addon can replace the era picker's menu with its own copy, as some imported season mods do. When the launcher can't edit that copy, it leaves it as it is and warns at build time, naming the addon. While that addon is on, the menu has no Addons row and the boxes don't show there. `available()` and `add_box()` return what they would otherwise.

## Logs

The core logs to `swofty/loader.log` with the `[era]` prefix: each registration, the hooks, the first builds of the picker, each skipped box and each narrowing.

```text
[era] 9 box(es) registered; offering the era picker hooks
[era] era picker hooks installed; addon boxes show from the next build of the picker
[era] era select: 6 stock box(es), 9 addon box(es) of 9 registered
[era] swofty_f1 offers only cs_f1_2016 (of 9 lists)
```

The three hooks are one core mixin set, offered at the first mixin commit after a box was registered, so a game without registered boxes runs none of them. When they can't be installed, the row is off and every later `add_box()` returns `unavailable`:

```text
[era] addon row off: the era picker hooks did not install (see the [core] lines for swofty_core.dll (era picker))
```

The Grand Prix and Time Trial each have their own picker, and the Addons row belongs to the one built last. Building the other clears the previous picker's Addons row and logs `detached the addon row from a previous view model`.
