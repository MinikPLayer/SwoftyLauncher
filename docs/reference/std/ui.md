---
title: UI
description: Addon screens, ADDON SETTINGS pages and their view models, home tiles and UI patches.
---

# UI

Your addon adds screens to F1 25 through an operation in `swofty.json`, not a `SwoftyApi` table: the launcher installs them and swofty_core registers them with the engine's own screen loader.

For a step-by-step walkthrough of a screen and a home tile, see the [in-game UI tutorial](/reference/addons/in-game-ui).

```text
author.my-addon/
  swofty.json
  ui/my_page.ui          raw screen data (a !pkg file), as found in the game's UI archives
```

```json [swofty.json]
{
  "schema_version": 1,
  "id": "author.my-addon",
  "name": "My Addon",
  "version": "1.0.0",
  "author": "Your name",
  "kind": ["ui"],
  "compat": { "game": "f1_25" },
  "requires": [{ "id": "swofty.core", "version": ">=0.1.0" }],
  "operations": {
    "screens": [
      { "name": "my_page", "file": "ui/my_page.ui", "flow": "flow_options", "anchor": "settings_container" }
    ]
  }
}
```

The build installs the file as `swofty/ui/my_page.ui` and lists it in the `ui` block of `swofty/settings.json`. When `flow_options` loads, swofty_core registers the screen after `settings_container`'s resource, and `swofty/loader.log` shows:

```text
[ui] settings.json: 4 addon screen(s), register on, strict off, recover on
[ui]   my_page (flow_options, anchor settings_container, djb2 0x1a2b3c4d, 20480 bytes from ui/my_page.ui)
[ui] screen my_page: anchor 'settings_container' (flow_options) loaded, resource 000000000420C088; registering at the next screen open
[ui] registered screen my_page (flow_options) via the engine screen loader (parse returned 35 = build job queued, position 1 of 1), attempt 1, 20480 bytes, resource 000000000420C088, trigger screen open
```

## Screens

List each screen in `operations.screens`:

```json [swofty.json]
"screens": [{ "name": "my_page", "file": "ui/my_page.ui", "flow": "flow_options", "anchor": "settings_container", "view_model": false }]
```

- `name`: your screen's route. `[a-z0-9_]`, at most 64 characters, unique across the profile's addons.
- `file`: your screen's raw `.ui` data, relative to the addon folder. Leave it out with `page`.
- `page`: `"options"` has the launcher build the screen from your addon's options, as ADDON SETTINGS shows them ([Use your options page](/reference/addons/in-game-ui#use-your-options-page)).
- `flow`: the flow that loads the screen. Default `flow_frontend`. `[a-z0-9_]`.
- `anchor`: a game screen in the same flow. Your screen registers after that game screen's resource loads. `[a-z0-9_]`.
- `view_model`: `true` asks swofty_core for a native view model ([View models](#view-models)). Default `false`.

The launcher rejects a manifest whose screen has a bad `name`, `flow` or `anchor`, is declared twice, or has neither a `file` nor `page: "options"`.

When the flow loads, swofty_core registers each listed screen. If the engine rejects any nodes, swofty_core logs each one with the reason:

```text
[ui] addon screen 'my_page' from ui/my_page.ui: node '<node>' (index <i>, class <fourcc>) <reason>, code <n> (0x<n>)
```

A route to an unregistered addon screen is cancelled, reopening the last screen:

```text
[ui] route -> screen 'my_page' is not registered; route cancelled (<step>), front end kept
[ui] re-opening the last screen 'settings_container' after the cancelled route
```

### Reserved names

When naming yours, leave these alone:

- `swofty_settings`, `swofty_addon_<n>`, `swofty_map_<n>` and `swofty_mod_<n>`: ADDON SETTINGS.
- `swofty_setup_<route>`: [career setup pages](/reference/addons/in-game-ui#add-a-career-setup-page).

## ADDON SETTINGS

Each time you build the profile, the launcher generates ADDON SETTINGS from the game's own screens:

- `swofty_settings`: one tile per enabled addon and track, with its name, author, version and description.
- `swofty_addon_<n>`: the page for `addons[n]` of `swofty/settings.json`, with one row per option.
- `swofty_map_<n>`: a track's code, author and version.

Its home tile routes to `mod_menu`. The core registers the pages like any addon screen:

```text
[ui]   swofty_settings (flow_options, anchor settings_container, djb2 0x90c2d901, 104568 bytes from ui/swofty_settings.ui)
[ui] registered screen swofty_settings (flow_options) via the engine screen loader (parse returned 35 = build job queued, position 1 of 1), attempt 1, 104568 bytes, resource 000000000420C088, trigger screen open
```

## View models

Set `"view_model": true` to request a view model for your screen. You get one only if swofty_core has a view model whose prefix matches your screen's name; it registers that model under `view_model_data.<screen>`. swofty_core ships one view model, for `swofty_addon_` pages:

```text
view_model_data.swofty_addon_3
  title               the addon's name
  o0.label            option 0's label
  o0.text             the option's current value, as displayed
  o0.index, o0.value  its current value's index
  o0.min, o0.max      the index range: 0 and the last index
  o0.step             1
  o0.is_slider        true for a slider
  o1.label ...        the same for every option k, as o<k>.*
```

```text
[ui] view model for swofty_addon_0 registered (native 0000000000CF0000, binding key 0x97515dc5 = djb2(screen), path view_model_data.swofty_addon_0)
```

A screen with `"view_model": true` and no matching view model is not registered, so routes to it are cancelled:

```text
[ui] screen my_page (flow_options) needs a view model and its view model is not attached yet; not registered, so routes to it are cancelled and the front end stays where it is
```

### Commands

Fire these commands from a screen to change options, turn an addon on or off, or restart the game:

```text
swofty_next:<addon>:<option>             select the next value
swofty_prev:<addon>:<option>             select the previous value
swofty_set:<addon>:<option>:<index>      select value number index
swofty_enable:<addon>:<0 or 1>           turn the addon off or on
swofty_restart                           close the game so the launcher rebuilds and relaunches it
```

```text
swofty_next:author.my-addon:greeting
swofty_set:author.my-addon:greeting:2
swofty_enable:author.my-addon:0
```

## Home tiles

To build tiles for the home screen, use these [Game API](/reference/game/) bindings:

- [`FeTile_Build_List`](/reference/game/fe_tiles#FeTile_Build_List): builds a tile list.
- [`site_FeHome_AfterCareerCall`](/reference/game/fe_home#site_FeHome_AfterCareerCall): the instruction after the Career tile is added. `r14` is the `TileList *`.
- [`FeHome_Add_CareerTile`](/reference/game/fe_home#FeHome_Add_CareerTile): appends a Career tile to a list.
- [`FeTile_ItemBase_Populate`](/reference/game/fe_tiles#FeTile_ItemBase_Populate): a tile's base `populate`.
- [`FeHome_Handle_Command`](/reference/game/fe_home#FeHome_Handle_Command): handles a home-screen command.
- [`Vm_Set_String`](/reference/game/vm#Vm_Set_String), [`Vm_Set_Bool`](/reference/game/vm#Vm_Set_Bool): set a view-model property.
- [`g_Vm_Registry`](/reference/game/vm#g_Vm_Registry): the view-model registry.
- [`Ne_Make_StrFromCStr`](/reference/game/core#Ne_Make_StrFromCStr): makes an engine string.

A tile is a `HomeItem`. Its virtual table has `populate` in slot 4 and `get_action` in slot 5; replace both for one tile with [`swofty::subclass`](./mixin#subclass). `get_action` returns an `Action` of type 1 whose `str` is the route to open. swofty_core's ADDON SETTINGS tile, from `native/src/core/swofty_core.cpp`:

```cpp
#include <swofty/game.hpp>
#include <swofty/mixin.hpp>

using namespace swofty::game;

constexpr const char *ROUTE = "mod_menu";

Action *mod_get_action(HomeItem *item, Action *out)
{
    out->type = 1;                                   // type 1: open a route
    out->str = nullptr;
    Ne_Make_StrFromCStr(&out->str, ROUTE, 0, 0);     // the route to open
    return out;
}

void mod_populate(HomeItem *item, void *node, uint32_t a7); // sets title and description, see the tutorial

SWOFTY_SITE(site_FeHome_AfterCareerCall, swofty::Context &c)
{
    auto list = reinterpret_cast<TileList *>(c.r14);  // the TileList being built
    FeHome_Add_CareerTile(list, uint8_t(c.r12 + 1));   // append a copy of the Career tile
    c.r12 += 1;                                        // move the builder's counters past it
    c.r15 += 1;
    HomeItem *item = list->items_end[-1].tile->item;
    swofty::subclass(item, {{4, reinterpret_cast<void *>(mod_populate)}, {5, reinterpret_cast<void *>(mod_get_action)}});
    Ne_Make_StrFromCStr(&item->id, ROUTE, 0, 0);
}
```

The full walkthrough is [Add a home tile](/reference/addons/in-game-ui#add-a-home-tile).

## UI patches

[UI patches](/reference/ops/ui-patches) let you edit the game's own screens, routes and UI databases, but the build does not apply them yet. It skips `operations.ui` with a warning:

```text
author.my-addon: 1 UI operation file(s) skipped; that stage is not built yet
```
