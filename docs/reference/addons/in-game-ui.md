---
title: In-game UI
description: Add a screen to F1 25 with operations.screens, put a tile on the home screen, and change an existing screen.
---

# In-game UI

An addon changes the game's menus through its manifest, which the launcher builds into the game's UI files, and through its plugin, which handles the live parts: view-model data, commands and [mixins](/reference/addons/hooking).

## Progressive Career's menus

Progressive Career adds a settings page between its Driver Career tile and the new-career wizard, and a Team HQ tab. The manifest declares both; the launcher builds their screens from the game's own settings pages:

```json [swofty.json]
{
  "id": "swofty.progressive",
  "name": "Progressive Career",
  "options": [
    { "id": "start_season", "type": "choice", "label": "Starting season", "default": "2026",
      "values": ["2016", "2017", "2018", "2019", "2020", "2021", "2022", "2023", "2024", "2025", "2026"],
      "description": "The real season a new Progressive career starts in. Each new season moves on a year, up to 2026.",
      "binds": "native", "hidden": true },
    { "id": "rnd_mode", "type": "choice", "label": "R&D", "default": "real_life",
      "values": [{ "value": "real_life", "label": "Real life" }, { "value": "hybrid", "label": "Hybrid" }],
      "description": "Real life: each season's car brings that year's pace, with no R&D. ...",
      "binds": "native", "hidden": true },
    { "id": "team_moves", "type": "choice", "label": "Team moves", "default": "follow_driver",
      "values": [{ "value": "follow_driver", "label": "Follow your driver's real career" }, { "value": "choose", "label": "Choose your team" }],
      "binds": "native", "hidden": true }
  ],
  "career_setup": {
    "route": "career_wizard_driver_career_progressive",
    "title": "Progressive Career",
    "options": ["start_season", "rnd_mode", "team_moves"]
  },
  "hub_tab": {
    "name": "progressive",
    "title": "Progressive Settings",
    "options": ["rnd_mode"],
    "data_group": "f1_career_progressive"
  },
  "operations": {
    "native": [{ "dll": "native/progressive_ui.dll" }, { "dll": "native/progressive_back.dll" }, { "dll": "native/progressive.dll", "files": ["data/"] }]
  }
}
```

The plugin points the career tile at the setup page, and keeps Team HQ's R&D tab in step with the R&D option:

```cpp [native/src/ui/tile.cpp]
#include <swofty/career_content.hpp>
#include <swofty/career_wizard.hpp>
#include <swofty/mixin.hpp>

namespace content = swofty::career::content;
namespace wz = swofty::career::wizard;

constexpr const char *kGroup = "f1_career_progressive";
constexpr const char *kSetupRoute = "career_wizard_driver_career_progressive"; // career_setup.route

bool register_tile()
{
    content::add({.id = "progressive", .title = "lng_pg_career_content",
                  .description = "lng_pg_career_content_description", .data_group = kGroup, .suffix = "PG26",
                  .mode = "driver_career_progressive", .model = content::Model::f1_2026,
                  .careers = content::Career::driver});
    wz::Result r = wz::tile_route(kGroup, kSetupRoute); // the tile opens the setup page, not the wizard's first step
    swofty::log("[progressive] tile: setup page route %s", wz::name(r));
    return true;
}

SWOFTY_ADDON(register_tile)
```

```cpp [native/src/season.cpp]
#include <swofty/hub.hpp>

void hub_sync(const std::string &rnd)
{
    swofty::hub::Result h = swofty::hub::hide_rnd(rnd == "real_life");                   // no R&D tab in Real life
    swofty::hub::Result o = swofty::hub::set_option("swofty.progressive", "rnd_mode", rnd); // the tab's row shows it
    swofty::log("[progressive] Team HQ tab: R&D %s (%s), option %s", rnd.c_str(), swofty::hub::name(h), swofty::hub::name(o));
}
```

The build log has a line for each page:

```text
ui: swofty.progressive has a career setup page swofty_setup_career_wizard_driver_career_progressive: the route career_wizard_driver_career_progressive opens it from the career content select, CONFIRM opens career_wizard_season_length_select, and career_wizard_season_length_select's route back_career_wizard_driver_career_progressive opens it again
ui: swofty.progressive has a hub tab template swofty_hub_progressive for f1_career_progressive careers in 2 archive(s); its rows read view_model_data.hud_swofty_hub_progressive
```

## The ways to change the menus

- **Settings for your addon:** [`options`](/reference/addons/settings). swofty_core builds the addon's page in **ADDON SETTINGS**: each [option](/reference/addons/manifest#options) is a row, and **Left** and **Right** change its value. The launcher generates those pages from the game's own files each time it builds the profile, so most addons don't need their own screen.
- **[A new screen](#add-a-screen):** `operations.screens`. swofty_core registers it with the game's UI system.
  - **[Your options page as a screen](#use-your-options-page)**: `"page": "options"`.
  - **[View models](#view-models)**: native data and commands for a screen.
- **[Settings before the new-career wizard](#add-a-career-setup-page):** `career_setup`. The launcher builds a page in the CUSTOM SETTINGS look.
- **[Settings in Team HQ](#add-a-team-hq-tab):** `hub_tab`. The launcher builds the page of a laptop tab, and swofty_core adds the tab.
- **[A tile on the home screen](#add-a-home-tile):** a [site mixin](/reference/addons/hooking#run-code-at-an-instruction) on the home-tile builder.
- **[A change to an existing screen](#change-an-existing-screen):** [UI patches](/reference/ops/ui-patches).
- **A change to a screen's layout:** an edited [scene](/reference/ui/editing-menus), made in [Swofty UI](/reference/ui/swofty-ui).

## Add a screen

A screen starts with a `.ui` file, the raw screen data you'd find in the game's UI archives. Declare it in `swofty.json`:

```jsonc [swofty.json]
"operations": {
  "screens": [
    {
      "name": "my_page",              // [a-z0-9_], at most 64 characters, unique across the profile's addons: the screen's name and route
      "file": "ui/my_page.ui",        // the .ui file, relative to the addon folder; left out with "page"
      "flow": "flow_options",         // the flow the screen belongs to; default flow_frontend
      "anchor": "settings_container", // a game screen in the same flow: the screen registers after that screen's resource loads
      "view_model": false             // true asks swofty_core for a native view model
    }
  ]
}
```

The declaration is in the manifest because two steps need it before your plugin runs: at build, the launcher installs each `.ui` file into the game and lists it in `swofty/settings.json`; at boot, swofty_core registers each screen when its flow loads. Your plugin handles the live parts: [mixins](/reference/addons/hooking), view-model data and the screen's commands.

Run `swofty build`. It installs the file as `swofty/ui/my_page.ui` and lists it in the `ui` block of `swofty/settings.json`. When the flow loads, swofty_core registers your screen through the engine's own screen loader:

```text
[ui] registered screen my_page (flow_options) via the engine screen loader (parse returned <rc> = build job queued, position 1 of 1), attempt 1, 18432 bytes, resource 000001F2C3A40000, trigger <trigger>
```

If the engine rejects a node, `swofty/loader.log` names it and gives the reason:

```text
[ui] addon screen 'my_page' from <file>: node 'title' (index 4, class UILB) was rejected by its class (init from data returned non-zero), code <code> (0x<code>)
```

If you route to an addon screen that didn't register, the route is cancelled and the last screen reopens:

```text
[ui] route -> screen 'my_page' is not registered; route cancelled (<step>), front end kept
[ui] re-opening the last screen 'settings_container' after the cancelled route
```

A screen in `flow_options` gets the flow entry, loading region and route group an ADDON SETTINGS page gets. Its group has the routes `open`, `open_no_input`, `close` and `back`. `back` closes the screen and opens its `anchor` again (`settings_container` when you give none).

::: warning Reserved names
`swofty_settings`, `swofty_addon_<n>`, `swofty_map_<n>` and `swofty_mod_<n>` belong to ADDON SETTINGS, and `swofty_setup_<route>` to [career setup pages](#add-a-career-setup-page).
:::

### Use your options page

To show your addon's options on a screen of their own, declare a screen with `"page": "options"` and no `file`:

```json [swofty.json]
"screens": [
  { "name": "ai_liveries", "page": "options", "flow": "flow_options", "anchor": "settings_container" }
]
```

- The build makes the page ADDON SETTINGS shows for your addon, with its rows and native view model, and names it `swofty_addon_<name>_<n>`.
- It adds the route `open_<name>` to the anchor's route group. The route closes the anchor and opens the page, as ADDON SETTINGS opens its pages.
- Back on the page returns to the anchor.
- The screen needs your addon on in the profile, and works only in `flow_options`.

Open it from the anchor with that route, for example from an event in the anchor's `.ui`:

```text
settings_container.Action.route { key: open_ai_liveries }
```

### View models

Set `"view_model": true` to request a native view model. Your screen gets one if swofty_core has one whose prefix matches the screen's name. It ships one for `swofty_addon_` pages:

```text
title                 the page title
o<k>.label            for each option k: its label,
o<k>.text             its value's text,
o<k>.value            its value,
o<k>.min, o<k>.max    and its range
```

It handles these commands to change option values:

```text
swofty_next:<addon>:<option>           next value
swofty_prev:<addon>:<option>           previous value
swofty_set:<addon>:<option>:<index>    value number index
```

A screen that needs a view model it can't get is not registered:

```text
[ui] screen my_page (flow_options) needs a view model and its view model is not attached yet; not registered, so routes to it are cancelled and the front end stays where it is
```

The [UI reference](/reference/std/ui) lists every view-model property and command.

## Add a career setup page

A career setup page shows some of your addon's options between a career content tile and the new-career wizard, in the look of the game's CUSTOM SETTINGS. It needs launcher 1.5.13 or later.

- Its home has a CONFIRM tile on the left and a category tile with the page's title.
- The category tile opens the rows page: one row per option. **Left** and **Right** change the value, and the panel on the right shows the focused option's description.
- Back on the rows page returns to the home.
- CONFIRM opens the wizard's season length step. Back on the home returns to the career content select.

List the options in `career_setup` ([manifest](./manifest#career-setup)):

```jsonc [swofty.json]
"career_setup": {
  "route": "career_wizard_driver_career_progressive", // [a-z0-9_], at most 48 characters
  "title": "Progressive Career",
  "options": ["start_season", "rnd_mode", "team_moves"],
  "next": "career_wizard_season_length_select"        // the screen CONFIRM opens; this is the default
}
```

The build adds `route` to the career content select's route group, `fe_career_dlc_select`. The route is a copy of `career_wizard_driver_career_to_f1`, the route a Driver Career tile takes to the season length step, and opens the page instead. Point your career content tile at it with the [career wizard library](/reference/std/career-wizard):

```cpp
wz::tile_route(group, "career_wizard_driver_career_progressive");
```

### What the build makes

```text
swofty_setup_<route>                 the home, a screen in flow_options
swofty_addon_<route>_<n>             the rows page, a screen in flow_options with the swofty_addon_ view model;
                                     <n> is the addon's index, as in swofty_addon_<n>

fe_career_dlc_select.<route>         closes the content select, moves to options_flow, opens the home and switches the
                                     backdrop to the CUSTOM settings scene (Customisation, Generic, Main)
swofty_setup_<route>.confirm         CONFIRM: closes the home, moves to career_season_setup_flow and opens next
swofty_setup_<route>.back            Back: closes the home, moves to frontend_flow and opens the content select with the
                                     driver_career message, as the career menu opens it for Driver Career.
                                     AutoFocus is false: the content select focuses its own tiles
swofty_setup_<route>.open_options    the category tile: closes the home and opens the rows page
<next>.back_<route>                  as the wizard's own routes leave its flow: next and the slices of its route group
                                     (career_wizard_summary, slice_gen_screen_title) transition out and the slices close.
                                     Then it moves to options_flow, opens the home and switches the backdrop as the entry
                                     route does. AutoFocus is false
```

- The home focuses its category tile each time it opens.
- Back on `next` takes the game's own back route for that step. Fire `back_<route>` there to return to the page, for example from your plugin's wizard back-route site for your data group.
- The build stops when the content select already has a route named `route`, or when `next` is not a screen in `flow_career_season_setup.erp`.

Progressive Career fires `back_<route>` from the wizard's back-route site, on the first wizard step only:

```cpp [native/src/ui/back.cpp]
SWOFTY_SITE(site_Game_CareerWizard_BackRoute, swofty::Context &c)
{
    auto route = reinterpret_cast<const char *>(c.rdx);   // the route name the game is about to fire
    // ... return unless route is "back", the wizard's data group is f1_career_progressive,
    // and its step and option stacks are empty (the first step the page opens)
    c.rdx = reinterpret_cast<uintptr_t>("back_career_wizard_driver_career_progressive");
}
```

### The options on the page

- The rows change the options live, as ADDON SETTINGS does. A `native` option's value reaches your plugin through [settings](/reference/std/settings), and the launcher stores it in the profile.
- The options also show on your addon's page in ADDON SETTINGS, except `hidden` ones.
- Their values belong to the profile, not to a career. Copy them into the career's [storage](/reference/std/storage) when the career is created.
- The description panel shows the option's `description`. A choice has no description per value.

## Add a Team HQ tab

Team HQ is the career hub's laptop, the screen `laptop_container` in `career_hub_flow`. Its tabs come from the laptop's view model, which the game fills when the hub loads (`Career_Build_HubLaptop`); a `.ui` edit can't add one. A tab needs launcher 1.5.13 or later. The launcher builds the tab's page from `hub_tab` ([manifest](./manifest#hub-tab)), and swofty_core adds the tab, publishes the page's data and handles its commands.

```jsonc [swofty.json]
"hub_tab": {
  "name": "progressive",                 // [a-z0-9_], at most 40 characters
  "title": "Progressive Settings",
  "options": ["rnd_mode"],               // 1 to 6 of the addon's options
  "data_group": "f1_career_progressive"  // careers of this data group get the tab
}
```

```text
[hub] builders: Driver Career Team HQ (team_hq_container) hooked, laptop (laptop_container) hooked
[hub] progressive: tab 'Progressive Settings' for careers of f1_career_progressive, 1 option row(s) from swofty.progressive
[hub] progressive: tab 'Progressive Settings' added after STANDINGS (key 0x00c9b739, page swofty_hub_progressive, data view_model_data.hud_swofty_hub_progressive)
[hub] progressive: next swofty.progressive.rnd_mode
```

### What the build makes

The template `swofty_hub_<name>`, added to the templates of `screens/laptop_overview.ui` in `flow_career_hub.erp` and `flow_team_hq.erp`. It is the game's settings rows page on the laptop: its scene is the settings page scene `art/screens/options/options_edit_screen.idf`, and its parts are the settings screen templates from `flow_options.erp` (`screens/settings_container.ui`). Its root, with the fade-in and the button prompts, comes from the inbox page `tab_content_emails_panel`; none of the inbox's parts are kept.

```text
swofty_hub_<name>
├─ left: one settings row per option, in options order (at most 6)
│    label   #view_model_data.hud_swofty_hub_<name>.o<i>.label
│    value   ….o<i>.value, between left and right arrows; the focused row is highlighted as on the game's settings pages
├─ top right: the category box, with the page title ….title
├─ under it: the description panel, with the focused option's label in capitals as the heading
│            and its description from the manifest as the body
└─ button prompts: Adjust for Left and Right
```

- The settings screen title, category filter and scroll bar are hidden.
- The scenes the page uses, with their animations, meshes, material bindings, materials and textures, are copied from `flow_options.erp` into each archive that lacks them, byte for byte and under their own names. No stock resource changes.
- The build fails when a copied resource refers to a resource that the archive, the copies and the shared archives (`persistent.erp`, `flow_persistent.erp`) all lack, but `flow_options.erp` has.
- Up and Down move between the options. Right on option `i` raises the command `swofty_next:<addon id>:<option id>` on the template, and Left raises `swofty_prev:<addon id>:<option id>`, as on ADDON SETTINGS pages.
- The template's `shown` and `enabled` events focus the first option.
- The build checks the template against the game's UI loader rules.

```text
ui: hub tabs copy <n> settings screen resources from flow_options.erp into <archive>
```

### What swofty_core does

- Adds a primary tab after STANDINGS, for careers of `data_group`, in the career hub's laptop (`laptop_container`) and in Driver Career's Team HQ (`team_hq_container`). The tab has one sub-tab whose content template is `swofty_hub_<name>`, built the way the laptop builds STANDINGS (content `tabbing_secondary_controller`, header `tabbing_secondary_tab`).
- Names the tab with the string key `lng_swofty_hub_<name>`, set to the title in capitals.
- Publishes `title`, `count`, and `label`, `value` and `description` for each option `o<k>`, under `view_model_data.hud_swofty_hub_<name>`, and again after every change of one of its options.
- Handles `swofty_next` and `swofty_prev` raised on the sub-tab, and steps the option as ADDON SETTINGS does.

### From your plugin

`swofty/hub.hpp` controls the tab from your plugin. Its table is read only when the running `swofty_core.dll` exports `SwoftyCoreHub`, so a plugin built against it also loads on an older core and sees `unavailable`.

```cpp
#include <swofty/hub.hpp>

namespace hub = swofty::hub;

if (hub::available()) {                          // the hub tab builders are hooked
    hub::hide_rnd(true);                         // hide Team HQ's R&D tab while on
    hub::set_option("swofty.progressive", "rnd_mode", "hybrid"); // pick an option's state, as if the player did
}
hub::Result r = hub::hide_rnd(false);
swofty::log("[my-addon] R&D tab shown: %s", hub::name(r)); // ok, late, unavailable or invalid
```

- `hide_rnd(on)`: Team HQ builds the R&D tab hidden, and an open Team HQ hides or shows it from the next frame. The tab stays hidden when Team HQ shows all its tabs again. Cores before the live path leave the tab out from the next Team HQ entry instead.
- `set_option(addon, option, value)` picks the option state whose value is `value`: a choice value, `"true"` or `"false"`, or a number. It runs the same command ADDON SETTINGS runs.

```text
[hub] R&D tab hidden (hide_rnd on); an open Team HQ follows on the next frame
```

## Add a home tile

The game builds the home screen's tiles in `FeTile_Build_List`. swofty_core adds ADDON SETTINGS there: a [site mixin](/reference/addons/hooking#run-code-at-an-instruction) runs right after the game adds the Career tile, adds a copy of it, and gives the copy its own text and route. This is the code, from `native/src/core/swofty_core.cpp`:

```cpp [native/src/core/swofty_core.cpp]
#include <swofty/game.hpp>
#include <swofty/mixin.hpp>

using namespace swofty::game;

// 1. Name it.
constexpr const char *ROUTE = "mod_menu";
constexpr const char *TITLE = "ADDON SETTINGS";
constexpr const char *DESCRIPTION = "Configure the addons installed by Swofty Launcher";

void set_string(void *reg, void *parent, const char *name, const char *text)
{
    NeStr *s = nullptr;
    Ne_Make_StrFromCStr(&s, text, 0, 0);
    Vm_Set_String(reg, parent, name, &s, 1);
    Ne_Make_StrFromCStr(&s, nullptr, 0, 0);
}

// 2. Set its text: the tile's populate method fills its view-model node.
void mod_populate(HomeItem *item, void *node, uint32_t a7)
{
    FeTile_ItemBase_Populate(item, node, a7);
    auto vm_node = static_cast<uint8_t *>(item->button_node.node);
    if (!vm_node)
        return;
    void *reg = *g_Vm_Registry, *parent = vm_node + 8;
    uint8_t no = 0;
    set_string(reg, parent, "title", TITLE);
    Vm_Set_Bool(reg, parent, "title_localised", &no, 1);
    set_string(reg, parent, "description", DESCRIPTION);
    Vm_Set_Bool(reg, parent, "description_localised", &no, 1);
}

// 3. Route it: get_action returns an action of type 1, whose string names the route to open.
Action *mod_get_action(HomeItem *item, Action *out)
{
    out->type = 1;
    out->str = nullptr;
    Ne_Make_StrFromCStr(&out->str, ROUTE, 0, 0);
    return out;
}

// 4. Add the tile: append a copy of the Career tile, then move the builder's counters past it.
SWOFTY_SITE(site_FeHome_AfterCareerCall, swofty::Context &c)
{
    auto list = reinterpret_cast<TileList *>(c.r14);
    FeHome_Add_CareerTile(list, uint8_t(c.r12 + 1));
    c.r12 += 1;
    c.r15 += 1;
    HomeItem *item = list->items_end[-1].tile->item;
    // this tile gets a copy of the virtual table with slot 4 (populate) and slot 5 (get_action) replaced;
    // the other Career tiles keep the game's table
    swofty::subclass(item, {{4, reinterpret_cast<void *>(mod_populate)}, {5, reinterpret_cast<void *>(mod_get_action)}});
    Ne_Make_StrFromCStr(&item->id, ROUTE, 0, 0);
}
```

- [`swofty::subclass`](/reference/std/mixin#subclass) gives one object its own copy of its virtual table.
- Every function and struct here is in the [Game API](/reference/game/): [`fe_home`](/reference/game/fe_home), [`fe_tiles`](/reference/game/fe_tiles), [`vm`](/reference/game/vm) and [`core`](/reference/game/core).

::: tip
swofty_core already owns `site_FeHome_AfterCareerCall`. If you add a second site mixin there, it runs in the same chain, ordered by [priority](/reference/addons/hooking#chains).
:::

## Change an existing screen

[UI patches](/reference/ops/ui-patches) describe edits to the game's screens, routes and UI databases: clone a node, set a label's text, change properties, or add or remove signals. List your patches in `operations.ui`:

```json [ui/example.pkgpatch.json]
{
  "schema_version": 1,
  "targets": [
    { "erp": "2025_asset_groups/ui_package/databases.erp", "resource": "eaid://ui_package/databases/tool_tips.uidb" }
  ],
  "edits": [
    {
      "op": "clone-entry",
      "at": "tool_tips.Contents.tooltips_dictionary.Contents",
      "src": "settings_fe_home",
      "as": "sample_mod_hint",
      "set": { "Name": "sample_mod_hint", "Contents.text.Value": "Sample Mod" }
    }
  ]
}
```

::: warning Not built yet
The build skips `operations.ui` and prints a warning:

```text
example.sample-mod: 1 UI operation file(s) skipped; that stage is not built yet
```

To apply the same `clone-entry` and `set-value` edits to `!pkg` resources, use the [`patch-pkg` ERP operation](/reference/data/textures#patch-a-pkg-resource).
:::

## Reference

- [Addon manifest](./manifest#operations): the `screens` field.
- [UI library](/reference/std/ui): view-model properties and commands.
- [Menus and UI](/reference/ui/): editing a screen's layout with Swofty UI.
