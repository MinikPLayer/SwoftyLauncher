---
title: Career wizard
description: Control the new-career wizard for one career data group with swofty/career_wizard.hpp — start in F1, skip or replace steps, force a season length and route a content tile.
---

# Career wizard

Use the career wizard library to shape the new-career wizard for careers of one data group: start in F1 with no F2 choice, skip or replace wizard steps, force a choice, change the route the career content tile or the confirm step opens, and hide Choose Driver tabs. The wizards of every other data group stay stock.

| | |
|---|---|
| Header | `swofty/career_wizard.hpp` |
| Namespace | `swofty::career::wizard` |
| Table | `SwoftyApi::career_wizard` (`SwoftyCareerWizard`), null on cores without it |
| API version | 8 (the table member), feature-detected: the table is null on cores without it |
| Compile-time check | `#ifdef SWOFTY_API_CAREER_WIZARD` |
| Feature | `career` — stable |
| Thread | registration: addon init or `SWOFTY_ON_LOAD`, then any thread |

Every call returns a `swofty::career::wizard::Result` with the [result codes](./#result-codes). A core without the table returns `unavailable` from every call; a call whose hook is not installed on this build registers and returns `unavailable`.

## When to register

The core installs a control's hook only once an addon registers an entry for it. A registration from the check function you pass to `SWOFTY_ADDON` is installed with that commit's addon mixins: an addon's own wrap of the same function chains with the core's. A registration from `SWOFTY_ON_LOAD` is installed right after that commit's `SWOFTY_ON_LOAD` handlers. Until then a registration returns `ok`; after the last mixin commit, one whose hook was never installed returns `unavailable` and logs `registered after the last mixin commit`. A game where no addon registers runs none of the wizard hooks, and an addon that wraps the same functions itself keeps its wraps.

## Data groups

The wizard state carries the data group of the career content tile it was opened from (`f1_career_2025`, `f1_career_2026`, an addon tile's group). Every entry applies only to wizards of its group. `group()` returns the data group of the last career tile the player pressed, once the tile route hook is installed (with a [career content](./career-content), a `tile_route` or a `hide_driver_tab` entry).

## Calls

| Call | Does |
|---|---|
| `available()` | `true` when the core has the table, the `career` feature is on, and at least one wizard hook is installed or none was needed yet |
| `group()` | the data group of the last career tile pressed, or `""` |
| `start_in_f1(group, on = true)` | `Game_CareerWizard_StartsInF1` answers yes: no Skip to F1 / Start in F2 step, no team-icons step (My Team / Konnersport), and the driver select takes the F1 path |
| `tile_route(group, to, from = route_season_length)` | the tile opens route `to` where it would open `from` (`""` for any route) |
| `replace_step(group, from, with)` | where the wizard would push step `from`, it pushes `with` |
| `skip_driver_icons(group, on = true)` | `replace_step(group, step::classic_drivers, step::confirm)`: no Add Driver Icons To Market step |
| `force_option(group, from, with)` | where the player picks option `from`, the wizard records `with` |
| `season_length(group, choice)` | the season length step records `option::full_season` (or `custom_season`) whatever the player picks |
| `confirm_route(group, to, from = route_driver_type)` | the confirm step opens `to` where it would open `from` |
| `skip_driver_type(group, on = true)` | the confirm step opens Choose Driver (`driver_career_driver_select`) instead of Official Driver / Custom Driver |
| `hide_driver_tab(group, tab, on = true)` | the Choose Driver tab `tab` (`header_f1`, `header_selected_team_icon`, `header_f2`, `header_icon`) is off and drawn as no tab |
| `clear(group)` | drops every wizard entry your addon registered for `group` |

Passing the stock value back drops an entry: `on = false`, `with == from`, or an empty `to`. Entries belong to the module that registered them; for the same key in the same group the last one registered wins.

Step bytes (`step::`): 3 experience, 4 season length, 5 settings, 6 confirm, 14 classic drivers, 15 my team entry, 16 team icons, 17 classic drivers settings. Option bytes (`option::`): 2 driver, 3 my team, 6 Skip to F1, 7 Start in F2, 9 Full Season (next step: settings), 10 (next step: track select; `custom_season`, a label the KB has not confirmed), 0xb Recommended settings, 0xc Custom settings, 0x1e team icon.

## Routes the tile opens

`tile_route` only changes which route the tile fires; the route has to exist in the front end's routes. To skip the season length step, add a copy of `career_wizard_driver_career_to_f1` that goes to the settings step with an [erp operation](/reference/addons/asset-api), for example (the F1 1990 addon's `erp/wizard.json`):

```json
{"op": "patch-pkg", "erp": "2025_asset_groups/ui_package/persistent.erp",
 "resource": "eaid://ui_package/routes/routes_frontend_general.uir", "fragment": 0,
 "edits": [{"op": "clone-entry", "at": "RouteGroupings[Id=fe_career_dlc_select].Routes",
            "src": "[Id=career_wizard_driver_career_to_f1]", "as": "career_wizard_driver_career_1990",
            "set": {"Commands[Dependencies=3].Attributes[Id=To].Value": "career_wizard_settings_select",
                    "Commands[Dependencies=6].Attributes[Id=Target].Value": "career_wizard_settings_select"}}]}
```

then call `tile_route(group, "career_wizard_driver_career_1990")`. A wizard that skips the season length step records no season length option.

When the content has saves, its tile opens the save-slot list (`career_continue_driver`) instead, and that screen's New Career runs `career_wizard_season_length_select` in its own route group. A `tile_route` entry whose `from` is that name applies when the route runs after a press of the group's tile: `tile_route(group, "my_route", "career_wizard_season_length_select")` runs `my_route` instead, which you add to the `career_continue_driver` group with the same kind of erp copy. The log has `[wizard] <group>: route <from> runs as <to>`.

A [career setup page](/reference/addons/in-game-ui#add-a-career-setup-page) has the launcher add such a route for you: `tile_route(group, "<route>")` then opens a settings page of your addon's options, whose CONFIRM opens the season length step.

## How it applies

| Hook | Binding | Does |
|---|---|---|
| start in F1 | `Game_CareerWizard_StartsInF1` (wrap) | its third argument is the wizard's data group |
| tile route | `FeTile_Route_CareerItem` (wrap, `swofty_core.dll (career tile route)`, shared with [career contents](./career-content)) | every career tile press; records the tile's data group (item `+0x180`) and rewrites a type-1 route after the game's router. `hide_driver_tab` needs it too, for the last tile's group |
| steps and options | `Game_CareerWizard_PushNextStep` (wrap) | runs after the player's option byte is pushed on wizard `+0x100..+0x108`; a forced option rewrites it before the step machine runs, a replaced step rewrites the step byte it pushed on `+0xe0..+0xe8` |
| confirm route | `site_Game_CareerWizardConfirm_NextRoute` (site) | `rdx` is the route the confirm step picked, `rsi` the view model (`+0x270` the wizard) |
| driver tabs | `site_Game_DriverSelectHeader_BuildTabs` (redirect), `Game_DriverSelectHeader_SetTabState` (wrap) | the tab builder's call records which tabs of the header's content group are hidden, for the last tile's group while no career of another group is loaded; the tab state wrap keeps those tabs at `content_visible` 0 and `enabled` 0, the state the game gives a tab it doesn't offer, and skips an index past the header's tabs |

Each hook is a core mixin set of its own, so a binding that does not resolve turns off only that control. Once the driver tabs hook is installed, a tab state index past the header's tabs is always skipped.

## Example

```cpp
#include <swofty/career_wizard.hpp>

namespace wz = swofty::career::wizard;

SWOFTY_ON_LOAD
{
    constexpr char group[] = "f1_career_1990";
    wz::start_in_f1(group);
    wz::tile_route(group, "career_wizard_driver_career_1990");
    wz::skip_driver_icons(group);
    wz::skip_driver_type(group);
    wz::hide_driver_tab(group, "header_f2");
    wz::hide_driver_tab(group, "header_icon");
}
```

To build one plugin against cores with and without the library, guard the calls with `#ifdef SWOFTY_API_CAREER_WIZARD` and check `wz::available()` at runtime.

## Log

`[wizard] setup: start in F1 armed|off|not used, tile route ..., steps and options ..., confirm route ..., driver tabs ...` after each commit that installs a control, then up to six lines per control: `[wizard] <group> starts in F1`, `<group> tile opens <to> instead of <from>`, `<group>: option <a> recorded as <b>`, `<group>: step <a> replaced by step <b>`, `<group>: confirm opens <to> instead of <from>`, `<group>: Choose Driver header tabs: <n> of <m> off`.
