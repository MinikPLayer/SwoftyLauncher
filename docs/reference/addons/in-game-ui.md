---
title: In-game UI
description: Add a screen to F1 25 with operations.screens, put a tile on the home screen, and change an existing screen.
---

# In-game UI

You can use your addon to change the game's menus in three ways:

| Goal | Tool |
|---|---|
| Settings for your addon | [`options`](/reference/addons/settings). swofty_core builds the addon's page in ADDON SETTINGS |
| A new screen | `operations.screens`. swofty_core registers it with the game's UI system |
| Settings before the new-career wizard | [`career_setup`](#add-a-career-setup-page). The launcher builds a page in the CUSTOM SETTINGS look |
| Settings in Team HQ | [`hub_tab`](#add-a-team-hq-tab). The launcher builds the page of a laptop tab; your plugin adds the tab |
| A tile on the home screen | A [site mixin](/reference/addons/hooking#run-code-at-an-instruction) on the home-tile builder |
| A change to an existing screen | [UI patches](/reference/ops/ui-patches) |
| A change to a screen's layout | An edited [scene](/reference/ui/editing-menus), made in [Swofty UI](/reference/ui/swofty-ui) |

If you just need settings, each [option](/reference/addons/manifest#options) becomes a row on your addon's page in **ADDON SETTINGS**. Use **Left** and **Right** to change its value. The launcher generates those pages from the game's own files each time it builds the profile, so most addons don't need their own screen. Add one when you need more than a settings row.

## Add a screen

A screen starts with a `.ui` file, the raw screen data you'd find in the game's UI archives.

Declare your screens in `swofty.json`. Two steps need that list before your plugin runs: at build time, the launcher installs each `.ui` file into the game and lists it in `swofty/settings.json`; at boot, swofty_core registers each screen when its flow loads. That's why the declaration belongs in the manifest rather than your plugin. Your plugin handles the live parts: [mixins](/reference/addons/hooking), view-model data and the screen's commands.

### 1. Declare it

```json [swofty.json]
"operations": {
  "screens": [
    { "name": "my_page", "file": "ui/my_page.ui", "flow": "flow_options", "anchor": "settings_container" }
  ]
}
```

| Field | Default | Meaning |
|---|---|---|
| `name` | | `[a-z0-9_]`, unique across the profile's addons. The screen's name and route |
| `file` | | The `.ui` file, relative to the addon folder. Leave it out with `page` |
| `page` | | `options` has the launcher build the screen from your addon's options ([Use your options page](#use-your-options-page)) |
| `flow` | `flow_frontend` | The flow the screen belongs to |
| `anchor` | | A game screen in the same flow. The screen registers after that screen's resource loads |
| `view_model` | `false` | `true` asks swofty_core for a native view model |

### 2. Build and launch

Run `swofty build` to install the file as `swofty/ui/my_page.ui` and list it in the `ui` block of `swofty/settings.json`. When the flow loads, swofty_core registers your screen through the engine's own screen loader. Check `swofty/loader.log` if the engine rejects any nodes: it names each one and gives the reason. If you route to an addon screen that didn't register, the route is cancelled and the last screen reopens.

A screen in `flow_options` gets the flow entry, loading region and route group an ADDON SETTINGS page gets. Its group has the routes `open`, `open_no_input`, `close` and `back`, and `back` closes the screen and opens its `anchor` again (`settings_container` when you give none).

### Use your options page

To show your addon's options on a screen of their own, declare a screen with `"page": "options"` and no `file`:

```json [swofty.json]
"screens": [
  { "name": "ai_liveries", "page": "options", "flow": "flow_options", "anchor": "settings_container" }
]
```

The build makes the page ADDON SETTINGS shows for your addon, with its rows and native view model, and names it `swofty_addon_<name>_<n>`. It also adds the route `open_<name>` to the anchor's route group, which closes the anchor and opens the page, as ADDON SETTINGS opens its pages. Open it from the anchor with that route, for example from an event in the anchor's `.ui`:

```text
settings_container.Action.route { key: open_ai_liveries }
```

Back on the page returns to the anchor. The screen needs your addon on in the profile and works only in `flow_options`.

::: warning Reserved names
`swofty_settings`, `swofty_addon_<n>`, `swofty_map_<n>` and `swofty_mod_<n>` belong to ADDON SETTINGS, and `swofty_setup_<route>` to [career setup pages](#add-a-career-setup-page).
:::

### View models

Set `"view_model": true` to request a native view model. Your screen gets one if swofty_core has one whose prefix matches the screen's name. It ships one for `swofty_addon_` pages, which publishes `title` and, for each option `k`, `o<k>.label`, `o<k>.text`, `o<k>.value`, `o<k>.min` and `o<k>.max`. It handles these commands to change option values:

| Command | Effect |
|---|---|
| `swofty_next:<addon>:<option>` | Next value |
| `swofty_prev:<addon>:<option>` | Previous value |
| `swofty_set:<addon>:<option>:<index>` | Value number `index` |

You'll find every view-model property and command in the [UI reference](/reference/std/ui).

## Add a career setup page

A career setup page shows some of your addon's options between a career content tile and the new-career wizard, in the look of the game's CUSTOM SETTINGS. Its home has a CONFIRM tile on the left and a category tile with the page's title. The category tile opens the rows page: one row per option, **Left** and **Right** change the value, and the panel on the right shows the focused option's description. Back on the rows page returns to the home. CONFIRM opens the wizard's season length step, and Back on the home returns to the career content select.

### 1. Declare it

List the options in `career_setup` ([manifest](./manifest#career-setup)):

```json [swofty.json]
"options": [
  { "id": "start_season", "type": "choice", "label": "Starting season", "default": "2016",
    "values": ["2016", "2017", "2018"], "binds": "native" },
  { "id": "rnd_mode", "type": "choice", "label": "R&D", "default": "real_life",
    "values": [{ "value": "real_life", "label": "Real life" }, "base", "hybrid"], "binds": "native" }
],
"career_setup": {
  "route": "career_wizard_driver_career_progressive",
  "title": "Progressive Career",
  "options": ["start_season", "rnd_mode"]
}
```

### 2. Point the tile at it

The build adds `route` to the career content select's route group, `fe_career_dlc_select`. The route is a copy of `career_wizard_driver_career_to_f1`, the route a Driver Career tile takes to the season length step, and opens the page instead. Point your career content tile at it with the [career wizard library](/reference/std/career-wizard):

```cpp
wz::tile_route(group, "career_wizard_driver_career_progressive");
```

### What the build makes

| Name | Does |
|---|---|
| `swofty_setup_<route>` | The home, a screen in `flow_options` |
| `swofty_addon_<route>_<n>` | The rows page, a screen in `flow_options` with the `swofty_addon_` [view model](#view-models). `<n>` is the addon's index, as in `swofty_addon_<n>` |
| `fe_career_dlc_select.<route>` | Closes the content select, moves to `options_flow`, opens the home and switches the backdrop to the CUSTOM settings scene (`Customisation`, `Generic`, `Main`) |
| `swofty_setup_<route>.confirm` | CONFIRM: closes the home, moves to `career_season_setup_flow` and opens `next` |
| `swofty_setup_<route>.back` | Back: closes the home, moves to `frontend_flow` and opens the content select with the `driver_career` message, as the career menu opens it for Driver Career. `AutoFocus` is `false`: the content select focuses its own tiles |
| `swofty_setup_<route>.open_options` | The category tile: closes the home and opens the rows page |
| `<next>.back_<route>` | As the wizard's own routes leave its flow: `next` and the slices of its route group (`career_wizard_summary`, `slice_gen_screen_title`) transition out and the slices close. Then it moves to `options_flow`, opens the home and switches the backdrop as the entry route does. `AutoFocus` is `false` |

The home focuses its category tile each time it opens. Back on `next` takes the game's own back route for that step. Fire `back_<route>` there to return to the page, for example from your plugin's wizard back-route site for your data group. The build log has a line for each page: `ui: <addon> has a career setup page swofty_setup_<route>: ...`. The build stops when the content select already has a route named `route`, or when `next` is not a screen in `flow_career_season_setup.erp`.

The rows change the options live, as ADDON SETTINGS does: a `native` option's value reaches your plugin through [settings](/reference/std/settings), and the launcher stores it in the profile. The options also show on your addon's page in ADDON SETTINGS, except `hidden` ones. Their values belong to the profile, not to a career; copy them into the career's [storage](/reference/std/storage) when the career is created. The description panel shows the option's `description`; a choice has no description per value.

## Add a Team HQ tab

Team HQ is the career hub's laptop, the screen `laptop_container` in `career_hub_flow`. Its tabs come from the laptop's view model, which the game fills when the hub loads (`Career_Build_HubLaptop`); a `.ui` edit can't add one. The launcher builds the tab's page from `hub_tab` ([manifest](./manifest#hub-tab)), and your plugin adds the tab, publishes the page's data and handles its commands.

### What the build makes

The template `swofty_hub_<name>`, added to the templates of `screens/laptop_overview.ui` in `flow_career_hub.erp` and `flow_team_hq.erp`. It is the game's settings rows page on the laptop: its scene is the settings page scene `art/screens/options/options_edit_screen.idf`, and its parts are the settings screen templates from `flow_options.erp` (`screens/settings_container.ui`). Its root, with the fade-in and the button prompts, comes from the inbox page `tab_content_emails_panel`; none of the inbox's parts are kept.

| Part | Shows |
|---|---|
| One settings row per option, in `options` order (at most 6), on the left | the option's label `#view_model_data.hud_swofty_hub_<name>.o<i>.label`, and its value `….o<i>.value` between left and right arrows; the focused row is highlighted as on the game's settings pages |
| Category box, top right | the page title `….title` |
| Description panel, under it | the focused option's `label` in capitals as the heading and its `description` from the manifest as the body |
| Button prompts | **Adjust** for Left and Right |

The settings screen title, category filter and scroll bar are hidden. The scenes the page uses, with their animations, meshes, material bindings, materials and textures, are copied from `flow_options.erp` into each archive that lacks them, byte for byte and under their own names; no stock resource changes. The build fails when a copied resource refers to a resource that the archive, the copies and the shared archives (`persistent.erp`, `flow_persistent.erp`) all lack, but `flow_options.erp` has. The build log has a line for each archive: `ui: hub tabs copy <n> settings screen resources from flow_options.erp into <archive>`.

Up and Down move between the options. Right on option `i` raises the command `swofty_next:<addon id>:<option id>` on the template, and Left raises `swofty_prev:<addon id>:<option id>`, as on ADDON SETTINGS pages. The template's `shown` and `enabled` events focus the first option. The build checks the template against the game's UI loader rules. The build log has a line for each page: `ui: <addon> has a hub tab template swofty_hub_<name> for <data_group> careers in 2 archive(s); …`.

### What your plugin does

- Add a primary tab to the laptop for careers of `data_group`, with a sub-tab whose content template is `swofty_hub_<name>`. The laptop builder adds each of its own tabs the same way (STANDINGS: content `tabbing_secondary_controller`, header `tabbing_secondary_tab`).
- Publish `title`, and `label` and `value` for each option, under `view_model_data.hud_swofty_hub_<name>`.
- Handle `swofty_next` and `swofty_prev` raised on the sub-tab and change the option.

## Add a home tile

To add a home tile, hook into `FeTile_Build_List`, which builds the home screen's tiles. That's how swofty_core adds ADDON SETTINGS: a [site mixin](/reference/addons/hooking#run-code-at-an-instruction) runs immediately after the game adds the Career tile, then gives the new tile its own text and route. Here's how it works in `native/src/core/swofty_core.cpp`.

**1. Name it.**

```cpp
using namespace swofty::game;

constexpr const char *ROUTE = "mod_menu";
constexpr const char *TITLE = "ADDON SETTINGS";
constexpr const char *DESCRIPTION = "Configure the addons installed by Swofty Launcher";
```

**2. Set its text.** Fill the tile's view-model node in its `populate` method:

```cpp
void set_string(void *reg, void *parent, const char *name, const char *text)
{
    NeStr *s = nullptr;
    Ne_Make_StrFromCStr(&s, text, 0, 0);
    Vm_Set_String(reg, parent, name, &s, 1);
    Ne_Make_StrFromCStr(&s, nullptr, 0, 0);
}

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
```

**3. Route it.** Return an action of type 1 from the tile's `get_action` method. The action's string tells it which route to open:

```cpp
Action *mod_get_action(HomeItem *item, Action *out)
{
    out->type = 1;
    out->str = nullptr;
    Ne_Make_StrFromCStr(&out->str, ROUTE, 0, 0);
    return out;
}
```

**4. Add the tile.** Call `FeHome_Add_CareerTile` to append a copy of the Career tile, then move the builder's counters past it in the site handler. Use [`swofty::subclass`](/reference/std/mixin#subclass) to give this tile a copy of its virtual table, replacing slot 4 (`populate`) and slot 5 (`get_action`). That gives your tile its own text and route while the other Career tiles keep the game's table.

```cpp
SWOFTY_SITE(site_FeHome_AfterCareerCall, swofty::Context &c)
{
    auto list = reinterpret_cast<TileList *>(c.r14);
    FeHome_Add_CareerTile(list, uint8_t(c.r12 + 1));
    c.r12 += 1;
    c.r15 += 1;
    HomeItem *item = list->items_end[-1].tile->item;
    swofty::subclass(item, {{4, reinterpret_cast<void *>(mod_populate)}, {5, reinterpret_cast<void *>(mod_get_action)}});
    Ne_Make_StrFromCStr(&item->id, ROUTE, 0, 0);
}
```

Look up every function and struct here in the [Game API](/reference/game/): [`fe_home`](/reference/game/fe_home), [`fe_tiles`](/reference/game/fe_tiles), [`vm`](/reference/game/vm) and [`core`](/reference/game/core).

::: tip
swofty_core already owns `site_FeHome_AfterCareerCall`. If you add a second site mixin there, it runs in the same chain, ordered by [priority](/reference/addons/hooking#chains).
:::

## Change an existing screen

Use [UI patches](/reference/ops/ui-patches) to describe edits to the game's screens, routes and UI databases. You can clone a node, set a label's text, change properties, or add or remove signals. List your patches in `operations.ui`:

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
The build skips `operations.ui` and prints a warning. To apply the same `clone-entry` and `set-value` edits to `!pkg` resources, use the [`patch-pkg` ERP operation](/reference/data/textures#patch-a-pkg-resource).
:::

## Reference

- [Addon manifest](./manifest#operations): the `screens` field.
- [UI library](/reference/std/ui): view-model properties and commands.
- [Menus and UI](/reference/ui/): editing a screen's layout with Swofty UI.
