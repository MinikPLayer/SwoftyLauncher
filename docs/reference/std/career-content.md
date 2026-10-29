---
title: Career contents
description: Add a separate career to F1 25, with its own tile on the career content select and its own save slots, with swofty/career_content.hpp.
---

# Career contents

`swofty/career_content.hpp` adds a career of your own to F1 25: a tile in an Addons row on the career content select (Career > Driver Career or Career > My Team Career), with its own save slots, and the calls that tell which career is loaded.

```cpp [native/src/my_career.cpp]
#include <cstring>

#include <swofty/career_content.hpp>
#include <swofty/events.hpp>
#include <swofty/mixin.hpp>

namespace content = swofty::career::content;

constexpr const char *kGroup = "f1_career_1990";   // the save-slot key: the entrant list the championship names

// career.loaded: act on your own career only
static void on_career(const SwoftyEvent *ev, void *)
{
    bool mine = !std::strcmp(ev->name, SWOFTY_EVENT_CAREER_LOADED) && swofty::career::is(kGroup);
    swofty::log("[my-career] %s", mine ? "1990 career loaded" : "another career");
}

// career.content.pressed: a = the content's index (-1 for a stock tile), data = the tile's data group
static void on_pressed(const SwoftyEvent *ev, void *)
{
    swofty::log("[my-career] tile pressed: %s", static_cast<const char *>(ev->data));
}

// add() runs in the init check, before the core's mixin commit
static bool init()
{
    content::Result r = content::add({
        .id = "f1-1990",                                      // names it in logs and info()
        .title = "lng_f1990_career_content",                  // lng_* keys for the tile's labels
        .description = "lng_f1990_career_content_description",
        .data_group = kGroup,
        .suffix = "90",                                       // Choose Driver lists F1CAREER_90, F2CAREER_90, ICONS_90
        .mode = "driver_career_1990",                         // save_data.name of the game_modes.ModeChampionship row
        .car_group = 2,                                       // customisation car group stat_id the press selects
    });
    swofty::log("[my-career] content %s", swofty::career::name(r));
    return true;                                              // false would fail the addon's init
}

SWOFTY_ON_LOAD
{
    swofty::events::subscribe(SWOFTY_EVENT_CAREER_LOADED, on_career);
    swofty::events::subscribe(content::pressed_event, on_pressed);
}

SWOFTY_ADDON(init)
```

```json [swofty.json]
{
  "schema_version": 1,
  "id": "author.my-career",
  "name": "My Career",
  "version": "0.1.0",
  "author": "Your Name",
  "kind": ["database", "text", "native"],
  "compat": { "game": "f1_25", "launcher": ">=1.5.10" },
  "requires": [{ "id": "swofty.core", "version": ">=0.2.12" }],
  "operations": {
    "database": ["db/career.json"],
    "assets": ["lang/career.json"],
    "native": [{ "dll": "native/my_career.dll" }]
  }
}
```

`db/career.json` adds the mode row, the entrant lists and the championship (see [Data your addon must provide](#data-your-addon-must-provide)). `lang/career.json` sets the two `lng_*` keys with `lang.set` ops ([Add or change strings](/reference/addons/runtime-assets#add-or-change-strings)).

`swofty/loader.log`, from registration to a press:

```text
[career] content 'f1-1990' from swofty.f1-1990 registered (tile 1 after the stock ones, 2026 model, data group f1_career_1990, suffix 90, mode driver_career_1990): queued for the next mixin commit
[career] contents: 1 registered; offering the career content hooks
[career] contents: hooks installed; 1 content(s) live, their data is checked once the database has loaded
[career] content 'f1-1990' ready: mode driver_career_1990 statId 81, data group f1_career_1990 (entrant list stat_id 107), suffix 90 (F1CAREER_90, F2CAREER, ICONS)
[career] content 'f1-1990': its mode (statId 81) passes the driver-career gate
[career] content 'f1-1990': tile added as button_3 (items 2 -> 3)
[career] Driver Career content select: 2 stock tile(s), 1 content tile(s) in the Addons row
[career] content 'f1-1990' pressed: car group 0x00e00000 (the model tile's pick)
```

- **Header:** `swofty/career_content.hpp`. Namespace `swofty::career::content`, plus `swofty::career::data_group`, `is` and `current_data_group`.
- **Table:** `SwoftyApi::career_content` (`SwoftyCareerContent`). The member arrived with API version 8, and the table is feature-detected (see [Availability](#availability)). `careers` and the Addons row need launcher 1.5.10.
- **Feature:** `career`.
- **Thread:** `add` in addon init. `data_group`, `is` and `current_data_group` on the flow thread. The others from any thread.

Every call that returns a `Result` uses the shared [result codes](./#result-codes). The data side (the mode row, the championship, the entrant lists) comes from your addon's [database operations](/reference/ops/database). The core builds the tile and owns every native step the stock code would refuse for a new career.

## Registering a content

Call `add()` from the check function you pass to `SWOFTY_ADDON`, which runs before the core's mixin commit. Progressive Career's tile (`addons/progressive/native/src/ui/tile.cpp`):

```cpp
content::Result r = content::add({.id = "progressive",
                                  .title = "lng_pg_career_content",
                                  .description = "lng_pg_career_content_description",
                                  .data_group = "f1_career_progressive",
                                  .suffix = "PG26",
                                  .mode = "driver_career_progressive",
                                  .model = content::Model::f1_2026,
                                  .careers = content::Career::driver});
```

The descriptor, field by field:

```cpp
struct Content {
    // id: names the content in logs and info(). 1 to 48 characters of a-z, 0-9, _, -
    // title, description: lng_* keys (1 to 96 characters) for the tile's labels; add the strings with
    //   your addon's language table
    // data_group: the save-slot key, the name of the entrant list your championship's
    //   ComCareerSetup.entrant_list names. 1 to 48 characters of a-z, 0-9, _, -.
    //   Not f1_career_2025 or f1_career_2026
    // suffix: the tile's suffix; the driver select formats F1CAREER_%s, F2CAREER_%s and ICONS_%s with it.
    //   1 to 8 characters of A-Z, a-z, 0-9, _, -. Not 25 or 26
    // mode: the save_data.name of your game_modes.ModeChampionship row; the core reads its statId at runtime
    std::string_view id, title, description, data_group, suffix, mode;
    // the stock tile the content copies: its route, menu systems and vtable
    Model model = Model::f1_2026;              // or Model::f1_2025
    // the stat_id of the customisation car group the press selects (0..65535);
    // -1 takes what the model tile's press takes
    int32_t car_group = -1;
    // the tile's notification value, below 256; 0 shows no badge
    uint32_t badge = 0;
    // replaces the 2026 model's entitlement string (f1_2026), up to 48 characters; empty keeps it
    std::string_view entitlement = {};
    // where the tile shows: Career::driver, Career::my_team or Career::both
    Career careers = Career::driver;
};
```

In C, `careers` is a mask of `SWOFTY_CAREER_CONTENT_DRIVER` (1) and `SWOFTY_CAREER_CONTENT_MY_TEAM` (2). A descriptor whose `size` ends before `careers` shows in Driver Career.

`add()` returns:

- `ok` when the content is queued, or live at once if another addon's content already installed the hooks.
- `invalid` for a bad or duplicate descriptor (another content with the same `id`, `data_group` or `suffix`); the log names the field.
- `unavailable` on an older core, with the `career` feature off, or when the hooks could not be installed.

Up to 8 contents can be registered; their tiles fill the Addons row in registration order.

```text
[career] content 'f1-1990' from swofty.f1-1990 refused: suffix 26 is a stock career's
[career] content 'f1-1990' from swofty.f1-1990 refused: the career feature is off
```

## The Addons row

The content select lists the stock tiles under a red **Built In** label and the contents under an **Addons** label.

- The labels and the row show only when at least one content is ready for the career on screen. With none, the screen is the stock one.
- Down from the last Built In tile moves into the Addons row, and Up from its first tile moves back. The description under the rows follows the focused tile.
- The core takes the career from the route the career type screen ran to open the content select: `career_dlc_select_my_team` is My Team and `career_dlc_select_driver` is Driver Career (the `career.starting` pick, see [events](./events#flow-and-career-events)).

The core names the career and counts the tiles on each build:

```text
[career] Driver Career content select: 2 stock tile(s), 1 content tile(s) in the Addons row
[career] My Team content select: 2 stock tile(s), 0 content tile(s) in the Addons row
```

The row comes from the launcher's UI build, all in `flow_frontend.erp`:

- `fe_career_dlc_select.ui` and its scene get a second spawner (`addon_buttons`, raising `addon_select_pressed` and `addon_focus_changed`) and the two labels.
- `fe_home.ui` gets the tile template `addon_tile_button_default`, a copy of `fe_tile_button_default` laid out in `addon_stacker_items`.

A game run without that build (an addon that owns those resources, or an older launcher) gets no Addons row, and the content tiles follow the stock tiles in the one list:

```text
[career] Driver Career content select: no Addons row (its view model was not found on the stack); 1 content tile(s) follow the stock tiles
```

## What the core does for a content

The core installs the tile, gate and slot predicate hooks as one core mixin set, and the router as a core mixin set of its own that the [career wizard](./career-wizard) library shares, at the first mixin commit after a content is registered. A game where no addon registers a content runs none of them.

- **Tile:** `site_FeTile_CareerContentEnd` (a site at the end of `FeTile_Build_List` case 6, which the showroom case 4 shares). It runs on the content select only: a list whose view model has the vtable `vt_Game_CareerDLCSelectViewModel`, or a list with the stock `career_default_content` or `career_dlc_content` tile. While the tile list's buttons node points at the view model's `addon_buttons` list, it appends `button_N` (numbered after the stock tiles) with `FeHome_Add_CareerTile`, then replaces its home item with a career content item built by `FeTile_Construct_CareerItem`: the model's route (`+0x178`), your data group (`+0x180`) and suffix (`+0x188`), and a copy of the model's vtable whose populate sets your title and description and whose press selects your car group and calls the router. In the Addons row the tile's VM `type` is `addon_tile_button_default`. It sets `addon_buttons_visible`.
- **Addons row commands:** `vt_Game_CareerDLCSelectViewModel` slot 10 (`Game_FECareerDLCSelectViewModel_OnCommandRaised`). It runs `addon_select_pressed` and `addon_focus_changed` as the stock `select_pressed` and `focus_changed` while the tile list's buttons node and item vector (view model `+0x2c8`) start at the Addons row.
- **Router:** `FeTile_Route_CareerItem` (a wrap, `swofty_core.dll (career tile route)`). It records the pressed tile's data group and fires `career.content.pressed`.
- **Gate:** `Game_Career_IsDriverCareerMode` (a wrap). It passes the mode championship whose `statId` (`+0x2a8`) is your mode's. The stock gate accepts only the 2025 and 2026 driver careers, so without this the new-career path reads through a null slot.
- **Slot predicate:** `vt_Game_DriverCareerSlotPredicate` slot 2. Asked for your data group, a stock "no" for your mode's career becomes the predicate's own data-group check. Questions for other data groups keep the game's answer.

At the press, a content tile whose menu systems (`+0x150..+0x178`) are unset takes the model tile's, as the 2026 tile's are what the career wizard needs.

```text
[career] content 'progressive': tile added as button_3 (items 2 -> 3)
[career] content 'progressive': its mode (statId 82) passes the driver-career gate
[career] content 'progressive': save-slot predicate said no for its mode; data group 120 of 108 is "(none)", wanted "f1_career_progressive": no match
```

## Data your addon must provide

The core checks the first four requirements below once the database has loaded and logs the result for each content. A content that is not ready is checked again every second and each time the career tiles are built, and the reason is logged again only when it changes. Until it is ready it gets no tile and its mode keeps the stock gate; `info()` reports it `live` with the reason in `why`.

```text
[career] content 'f1-1990' ready: mode driver_career_1990 statId 81, data group f1_career_1990 (entrant list stat_id 107), suffix 90 (F1CAREER_90, F2CAREER, ICONS)
[career] content 'f1-1990' (swofty.f1-1990) not ready: no game_modes.ModeChampionship row saves to driver_career_1990 (save_data.name); no tile and its mode keeps the stock gate until it is, checked again every 1000 ms
```

Checked, required:

- **A `game_modes.ModeChampionship` row** whose `save_data.name` (or `save_data_0.name`) is `mode`. Its `statId` is how the gate and the predicate recognise your career.
- **An `entrants.entrant_lists` row named `data_group`**, with a `stat_id` below 256. The `CareerSetupComponent` stores the `stat_id` of the list `ComCareerSetup.entrant_list` names, as a byte; the predicate reads the name back from that index.
- **A content set official entrant list `F1CAREER_<suffix>`.** The driver select looks it up with the tile's suffix.

Checked, warning only:

- **`F2CAREER_<suffix>` and `ICONS_<suffix>`.** The F2 and Icons tabs of the driver select are empty without them.

Not checked:

- **Your mode name and setup mode** in the content set's `available_game_modes_0.game_mode_championships` and `available_game_setup_modes_0.setup_modes`. The wizard only offers modes the content set lists.
- **A championship row cloned with all its component rows** (`ComCareer`, `ComCareerSetup`, `ComInfluence`, ...) and `ComCareerSetup_0.entrant_list` = `data_group`. A championship missing component rows reads as My Team.
- **A data-group row named `data_group` in each data-group table** (`career.career_setup.CareerSetupDataGroup`, `accolades.AccoladeDataGroup`, `career.leverage.LeverageDataGroup`, `recognition.RecognitionDataGroup`, `resource_points.data_group`, `specialist_manager.dataGroup`, and the My Team ones). The career systems look their data up by the career's data group.

The F1 1990 addon's `tools/career.py` and `tools/grid.py` write a complete set of these rows; use them as the reference.

## Saves when the addon is off

A content whose `game_modes.ModeChampionship` row has its own `save_data.name` keeps its careers in a save container of that name in the profile save. In a game run without the addon, the core keeps that container and writes it back unchanged with each profile save, so the careers are there again once the addon is back on; the game shows them nowhere while the addon is off. `loader.log` names each kept container:

```text
[saves] profile load 1: kept gm_driver_career_progressive (2162004 bytes), which no loaded game mode claims
[saves] profile save: wrote back 2 save container(s) no loaded game mode claims: gm_driver_career_progressive, driver_career_progressive_SaveSlotGameModeComponent
```

A core without this drops the container at the first profile save without the addon; the launcher's save backup ([`swofty saves`](/reference/cli#swofty-saves)) covers that case.

## Which career is loaded

```cpp
// Progressive Career: gate behaviour on its own career (follow.cpp, report.cpp)
if (!swofty::career::is("f1_career_progressive"))
    return;
std::string group = swofty::career::current_data_group();   // "" with no career loaded

std::string g;
swofty::career::Result r = swofty::career::data_group(&g);  // the loaded career; or pass a career entity
// ok, late (no career loaded) or invalid (no CareerSetupComponent with a named data group)

// the last career content tile pressed, stock ones included (f1_career_2026 too)
if (content::pressed_is("f1_career_1990"))
    swofty::log("[my-career] the 1990 tile opened this wizard");

content::Info i{};
if (content::info("f1-1990", &i) == content::Result::ok)
    swofty::log("[my-career] %s: mode statId %lld, %u tile(s), %u press(es) %s", i.id,
                (long long)i.mode_stat_id, i.tiles, i.presses, i.why);
```

- `data_group(std::string *out, const void *career = nullptr)`: `ok` with the data group of a career entity (`nullptr`: the loaded one); `late` when no career is loaded; `invalid` when the career has no named data group. It calls into the game: use it on the game thread.
- `current_data_group()`: the loaded career's data group, or `""`.
- `is(data_group, career = nullptr)`: the career is filed under that data group.
- `content::pressed(std::string *out)`: `ok` with the data group of the last career content tile pressed, stock ones included; `late` before the first press; `unavailable` while no content hooks are installed.
- `content::pressed_is(data_group)`: the last pressed tile has that data group.
- `content::info(id, Info *out)`: a content's state (`queued`, `live`, `off` with `why`; `live` with `why` while its data is not ready), its `mode_stat_id`, `data_group_index`, and its tile and press counts. `content::state(info)` turns the state into a `content::State`.
- `content::available()`: the core implements contents and the `career` feature is on.

`data_group` reads the career the way the save-slot predicate does: each championship's `CareerSetupComponent` data-group index into its entrant-list names. Use `is()` to gate behaviour on one career, for example on the [`career.loaded`](./events#flow-and-career-events) event, as in the example at the top of this page.

```c
typedef struct SwoftyCareerContentInfo {
    uint32_t index;
    uint32_t state;              // SWOFTY_CAREER_CONTENT_QUEUED (0), _LIVE (1), _OFF (2)
    int64_t mode_stat_id;
    int64_t data_group_index;
    uint32_t tiles;
    uint32_t presses;
    char id[64];
    char data_group[64];
    char why[192];
} SwoftyCareerContentInfo;
```

## Events

`career.content.pressed` (`content::pressed_event`, `SWOFTY_EVENT_CAREER_CONTENT_PRESSED`):

- `a`: the registered content's index, or `-1` for a stock tile.
- `b`: 0.
- `data`: `const char *`, the data group of the pressed tile.

It fires on the flow thread before the router picks the save-slot list or the new-career wizard, and only while at least one content is registered.

## Availability

The table has no API version of its own; its `SwoftyApi::career_content` member arrived with version 8. The header reads `SwoftyApi::career_content` only when the running `swofty_core.dll` exports `SwoftyCoreCareerContent`, so an addon built against it also loads on an older core, where `available()` is `false` and `add()` returns `unavailable`. To keep code that predates the header building, guard on the header:

```cpp
#if __has_include(<swofty/career_content.hpp>)
#include <swofty/career_content.hpp>
#define MY_CONTENT_API 1
#endif
```

`careers` and the Addons row came with launcher 1.5.10. An older core reads the descriptor without `careers`: the content shows in Driver Career only, after the stock tiles in the one list. An addon that registers a content for My Team sets `"compat": { "launcher": ">=1.5.10" }` in its `swofty.json`.

In My Team, a content gets its tile and its press. The gate and slot predicate hooks are the Driver Career ones (`Game_Career_IsDriverCareerMode`, `vt_Game_DriverCareerSlotPredicate`), and a My Team content's new-career path has not been run in game.

The hooks rely on bindings that are L2 in the KB with an independent verify row each; the press path, the gate pass and the slot predicate were confirmed in game for the 2026 model only. The 2025 model also needs `FeTile_CareerDefaultItem_Populate`, whose verify row is still `partial`, so it is not exported yet and a 2025-model content turns off with `needs FeTile_CareerDefaultItem_Populate`.
