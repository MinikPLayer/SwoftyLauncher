---
title: Career wizard
description: Control the new-career wizard for one career data group with swofty/career_wizard.hpp — start in F1, skip or replace steps, force a season length and route a content tile.
---

# Career wizard

`swofty/career_wizard.hpp` shapes the new-career wizard for the careers of one data group, and leaves the wizards of every other data group stock.

Progressive Career's tile DLL, trimmed: a [career content](./career-content) tile, then the wizard for its data group.

```cpp [native/src/ui/tile.cpp]
#include <swofty/career_content.hpp>
#include <swofty/career_wizard.hpp>
#include <swofty/mixin.hpp>

namespace {

namespace content = swofty::career::content;
namespace wz = swofty::career::wizard;

constexpr const char *kGroup = "f1_career_progressive";                       // the tile's data group
constexpr const char *kSetupRoute = "career_wizard_driver_career_progressive"; // swofty.json career_setup.route
constexpr const char *kSlotListNewCareer = "career_wizard_season_length_select"; // New Career on the save-slot list

// the check function: registrations here install with this commit's addon mixins
bool register_tile()
{
    content::Result r = content::add({.id = "progressive",
                                      .title = "lng_pg_career_content",
                                      .description = "lng_pg_career_content_description",
                                      .data_group = kGroup,
                                      .suffix = "PG26",
                                      .mode = "driver_career_progressive",
                                      .model = content::Model::f1_2026,
                                      .careers = content::Career::driver});
    wz::Result w[7] = {
        wz::start_in_f1(kGroup),                                // no Skip to F1 / Start in F2, no team icons
        wz::skip_driver_type(kGroup),                           // confirm opens Choose Driver directly
        wz::hide_driver_tab(kGroup, "header_f2"),               // no F2 tab on Choose Driver
        wz::hide_driver_tab(kGroup, "header_icon"),             // no Icons tab
        wz::tile_route(kGroup, kSetupRoute),                    // the tile opens the career setup page
        wz::skip_driver_icons(kGroup),                          // no Add Driver Icons To Market step
        wz::tile_route(kGroup, kSetupRoute, kSlotListNewCareer) // New Career on the save-slot list does too
    };
    swofty::log("[progressive] tile: content 'progressive' %s; wizard: start in F1 %s, driver type step %s, F2 tab %s, "
                "Icons tab %s, setup page route %s, driver icons step %s, save-slot list New Career to the setup page %s",
                swofty::career::name(r), wz::name(w[0]), wz::name(w[1]), wz::name(w[2]), wz::name(w[3]),
                wz::name(w[4]), wz::name(w[5]), wz::name(w[6]));
    return true;
}

}

SWOFTY_ADDON(register_tile)
```

```json [swofty.json]
{
  "schema_version": 1,
  "id": "author.my-career",
  "name": "My Career",
  "version": "1.0.0",
  "author": "Your name",
  "kind": ["erp", "native"],
  "compat": { "game": "f1_25" },
  "requires": [{ "id": "swofty.core", "version": ">=0.2.13" }],
  "options": [
    { "id": "start_season", "type": "choice", "label": "Starting season", "default": "2016",
      "values": ["2016", "2017", "2018"], "binds": "native" }
  ],
  "career_setup": {
    "route": "career_wizard_driver_career_progressive",
    "title": "Progressive Career",
    "options": ["start_season"]
  },
  "operations": {
    "erp": ["erp/wizard.json"],
    "native": [{ "dll": "native/progressive_ui.dll" }]
  }
}
```

`career_setup` makes the launcher add the route the tile opens ([Routes the tile opens](#routes-the-tile-opens)). `erp/wizard.json` adds the copy for the save-slot list. What `swofty/loader.log` shows, at load and then as the player starts a career:

```text
[progressive] tile: content 'progressive' ok; wizard: start in F1 ok, driver type step ok, F2 tab ok, Icons tab ok, setup page route ok, driver icons step ok, save-slot list New Career to the setup page ok
[wizard] setup: start in F1 armed, tile route armed, steps and options armed, confirm route armed, driver tabs armed
[wizard] f1_career_progressive tile opens career_wizard_driver_career_progressive instead of career_wizard_driver_career_to_f1
[wizard] f1_career_progressive starts in F1 (no F2 choice, no team-icons step)
[wizard] f1_career_progressive: step 14 replaced by step 6
[wizard] f1_career_progressive: confirm opens driver_career_driver_select instead of driver_career_driver_type_select
[wizard] f1_career_progressive: Choose Driver header tabs: 2 of 4 off
```

- **Header:** `swofty/career_wizard.hpp`, namespace `swofty::career::wizard`
- **Table:** `SwoftyApi::career_wizard` (`SwoftyCareerWizard`), API version 8 (the table member), feature-detected: the table is null on cores without it. Compile-time check: `#ifdef SWOFTY_API_CAREER_WIZARD`
- **Feature:** `career` (stable)
- **Thread:** register from addon init or `SWOFTY_ON_LOAD`; then any thread

Every call returns a `swofty::career::wizard::Result` with the [result codes](./#result-codes), and `wz::name(r)` names it. A core without the table returns `unavailable` from every call. A call whose hook is not installed on this build registers and returns `unavailable`.

## When to register

The core installs a control's hook only once an addon registers an entry for it:

```cpp
bool init()                       // the check passed to SWOFTY_ADDON
{
    wz::start_in_f1("f1_career_1990"); // installed with this commit's addon mixins: your own wrap
    return true;                       // of Game_CareerWizard_StartsInF1 chains with the core's
}

SWOFTY_ON_LOAD
{
    wz::hide_driver_tab("f1_career_1990", "header_f2"); // installed right after this commit's SWOFTY_ON_LOAD handlers
}

SWOFTY_ADDON(init)
```

Until the commit, a registration returns `ok`. After the last mixin commit, one whose hook was never installed returns `unavailable` and logs:

```text
[wizard] driver tabs registered after the last mixin commit; its hook is not installed
```

A game where no addon registers runs none of the wizard hooks, and an addon that wraps the same functions itself keeps its wraps.

## Data groups

The wizard state carries the data group of the career content tile it was opened from: `f1_career_2025`, `f1_career_2026`, or an addon tile's group. Every entry applies only to wizards of its group.

```cpp
std::string g = wz::group(); // "f1_career_progressive" after the player pressed Progressive's tile; "" before any tile
```

`group()` returns the data group of the last career tile the player pressed, once the tile route hook is installed (with a [career content](./career-content), a `tile_route` or a `hide_driver_tab` entry).

The group, a `to` route and a tab name are 1 to 127 characters with no control characters; anything else is `invalid`. Passing the stock value back drops an entry: `on = false`, `with == from`, or an empty `to`. Entries belong to the module that registered them. For the same key in the same group, the last one registered wins.

## available

```cpp
if (!wz::available())
    swofty::log("[my-career] no wizard control on this core");
```

`true` when the core has the table, the `career` feature is on, and at least one wizard hook is installed or none was needed yet. To build one plugin against cores with and without the library, guard the calls with `#ifdef SWOFTY_API_CAREER_WIZARD` and check `wz::available()` at runtime.

## start_in_f1

```cpp
wz::start_in_f1(group);        // on
wz::start_in_f1(group, false); // drops the entry
```

`Game_CareerWizard_StartsInF1` answers yes for the group: no Skip to F1 / Start in F2 step, no team-icons step (My Team / Konnersport), and the driver select takes the F1 path.

```text
[wizard] f1_career_1990 starts in F1 (no F2 choice, no team-icons step)
```

## tile_route

```cpp
wz::tile_route(group, "career_wizard_driver_career_1990");             // from = route_season_length
wz::tile_route(group, "my_route", "career_wizard_season_length_select"); // a route the game runs after the press
wz::tile_route(group, "my_route", "");                                   // whatever route the tile picks
```

`tile_route(group, to, from = route_season_length)`: the tile opens route `to` where it would open `from` (`""` for any route). `route_season_length` is `career_wizard_driver_career_to_f1`. See [Routes the tile opens](#routes-the-tile-opens).

```text
[wizard] f1_career_1990 tile opens career_wizard_driver_career_1990 instead of career_wizard_driver_career_to_f1
```

## replace_step and skip_driver_icons

```cpp
wz::replace_step(group, wz::step::classic_drivers, wz::step::confirm); // where the wizard pushes 14, it pushes 6
wz::skip_driver_icons(group);                                          // the same call
```

`replace_step(group, from, with)`: where the wizard would push step `from`, it pushes `with`. `skip_driver_icons(group, on = true)` is `replace_step(group, step::classic_drivers, step::confirm)`: no Add Driver Icons To Market step. Step values above 0xff are `invalid`.

```text
[wizard] f1_career_progressive: step 14 replaced by step 6
```

## force_option and season_length

```cpp
wz::force_option(group, wz::option::start_in_f2, wz::option::skip_to_f1); // the player picks 7, the wizard records 6
wz::season_length(group, wz::option::full_season);                       // Full Season whatever the player picks
```

`force_option(group, from, with)`: where the player picks option `from`, the wizard records `with`. `season_length(group, choice)`: the season length step records `option::full_season` (or `custom_season`) whatever the player picks; it is two `force_option` calls, for 9 and 10. Option values above 0xff are `invalid`.

```text
[wizard] f1_career_1990: option 10 recorded as 9
```

## Step and option bytes

| `step::` | Byte |
|---|---|
| `experience` | 3 |
| `season_length` | 4 |
| `settings` | 5 |
| `confirm` | 6 |
| `classic_drivers` | 14 |
| `my_team_entry` | 15 |
| `team_icons` | 16 |
| `classic_drivers_settings` | 17 |

| `option::` | Byte | The player's choice |
|---|---|---|
| `driver` | 2 | Driver |
| `my_team` | 3 | My Team |
| `skip_to_f1` | 6 | Skip to F1 |
| `start_in_f2` | 7 | Start in F2 |
| `full_season` | 9 | Full Season (next step: settings) |
| `custom_season` | 10 | next step: track select; a label the KB has not confirmed |
| `recommended_settings` | 0xb | Recommended settings |
| `custom_settings` | 0xc | Custom settings |
| `team_icon` | 0x1e | a team icon |

## confirm_route and skip_driver_type

```cpp
wz::confirm_route(group, "my_route");  // from = route_driver_type
wz::skip_driver_type(group);           // confirm_route to route_driver_select
```

`confirm_route(group, to, from = route_driver_type)`: the confirm step opens `to` where it would open `from`. `skip_driver_type(group, on = true)`: the confirm step opens Choose Driver (`driver_career_driver_select`) instead of Official Driver / Custom Driver (`driver_career_driver_type_select`).

```text
[wizard] f1_career_progressive: confirm opens driver_career_driver_select instead of driver_career_driver_type_select
```

## hide_driver_tab

```cpp
wz::hide_driver_tab(group, "header_f2");
wz::hide_driver_tab(group, "header_icon");
```

`hide_driver_tab(group, tab, on = true)`: the Choose Driver tab `tab` (`header_f1`, `header_selected_team_icon`, `header_f2`, `header_icon`) is off and drawn as no tab. It needs the tile route hook too, for the last tile's group.

```text
[wizard] f1_career_progressive: Choose Driver header tabs: 2 of 4 off
[wizard] f1_career_1990: the Choose Driver header group does not look like a content group; its tabs stay
```

## clear

```cpp
wz::clear(group); // drops every wizard entry your addon registered for group
```

## Routes the tile opens

`tile_route` only changes which route the tile fires: the route has to exist in the front end's routes. To skip the season length step, add a copy of `career_wizard_driver_career_to_f1` that goes to the settings step with an [erp operation](/reference/addons/asset-api). The F1 1990 addon's `erp/wizard.json`:

```json [erp/wizard.json]
{"op": "patch-pkg", "erp": "2025_asset_groups/ui_package/persistent.erp",
 "resource": "eaid://ui_package/routes/routes_frontend_general.uir", "fragment": 0,
 "edits": [{"op": "clone-entry", "at": "RouteGroupings[Id=fe_career_dlc_select].Routes",
            "src": "[Id=career_wizard_driver_career_to_f1]", "as": "career_wizard_driver_career_1990",
            "set": {"Commands[Dependencies=3].Attributes[Id=To].Value": "career_wizard_settings_select",
                    "Commands[Dependencies=6].Attributes[Id=Target].Value": "career_wizard_settings_select"}}]}
```

```cpp
wz::tile_route("f1_career_1990", "career_wizard_driver_career_1990");
```

A wizard that skips the season length step records no season length option.

When the content has saves, its tile opens the save-slot list (`career_continue_driver`) instead, and that screen's New Career runs `career_wizard_season_length_select` in its own route group. A `tile_route` entry whose `from` is that name applies when the route runs after a press of the group's tile:

```cpp
wz::tile_route(group, "my_route", "career_wizard_season_length_select"); // add my_route to career_continue_driver with the same kind of erp copy
```

```text
[wizard] f1_career_progressive: route career_wizard_season_length_select runs as career_wizard_driver_career_progressive
```

A [career setup page](/reference/addons/in-game-ui#add-a-career-setup-page) has the launcher add such a route for you: `tile_route(group, "<route>")` then opens a settings page of your addon's options, whose CONFIRM opens the season length step.

## How it applies

Each control is a core mixin set of its own, so a binding that does not resolve turns off only that control:

```text
start in F1      Game_CareerWizard_StartsInF1 (wrap)
                 its third argument is the wizard's data group
tile route       FeTile_Route_CareerItem (wrap, swofty_core.dll (career tile route), shared with career contents)
                 every career tile press; records the tile's data group (item +0x180) and rewrites
                 a type-1 route after the game's router. hide_driver_tab needs it too, for the last tile's group
steps, options   Game_CareerWizard_PushNextStep (wrap)
                 runs after the player's option byte is pushed on wizard +0x100..+0x108; a forced option
                 rewrites it before the step machine runs, a replaced step rewrites the step byte it pushed on +0xe0..+0xe8
confirm route    site_Game_CareerWizardConfirm_NextRoute (site)
                 rdx is the route the confirm step picked, rsi the view model (+0x270 the wizard)
driver tabs      site_Game_DriverSelectHeader_BuildTabs (redirect), Game_DriverSelectHeader_SetTabState (wrap)
                 the tab builder's call records which tabs of the header's content group are hidden, for the
                 last tile's group while no career of another group is loaded; the tab state wrap keeps those
                 tabs at content_visible 0 and enabled 0, the state the game gives a tab it doesn't offer,
                 and skips an index past the header's tabs
```

The wizard state holds the data group at `+0x128`, copied from the tile item's `+0x180`. Once the driver tabs hook is installed, a tab state index past the header's tabs is always skipped:

```text
[wizard] Choose Driver tab 5 state skipped, past the header's tabs
```

## Log

After each commit that installs a control, one setup line. Each control is `armed`, `off` (its hook did not install) or `not used`:

```text
[wizard] setup: start in F1 armed, tile route armed, steps and options not used, confirm route armed, driver tabs armed
```

Then up to six lines of each kind, as the wizard runs:

```text
[wizard] <group> starts in F1 (no F2 choice, no team-icons step)
[wizard] <group> tile opens <to> instead of <from>
[wizard] <group>: option <a> recorded as <b>
[wizard] <group>: step <a> replaced by step <b>
[wizard] <group>: confirm opens <to> instead of <from>
[wizard] <group>: Choose Driver header tabs: <n> of <m> off
```
