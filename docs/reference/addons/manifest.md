---
title: Addon manifest
description: Every swofty.json field, with types and real examples for launcher, options, compat, requires and operations.
---

# Addon manifest

Your addon needs a `swofty.json` at its root. This is where you give it a name, declare its settings and list what it ships. Set `schema_version` to `1` and stick to the fields below: unknown fields are rejected.

```json [swofty.json]
{
  "schema_version": 1,
  "id": "author.my-addon",
  "name": "My Addon",
  "version": "1.0.0",
  "author": "Your name",
  "compat": { "game": "f1_25" },
  "operations": { "database": ["db/example.json"] }
}
```

## Identity

| Field | Type | Value |
|---|---|---|
| `schema_version` | integer | `1`. |
| `id` | string | Choose a lowercase dotted id with at least two parts, each part `[a-z0-9_-]`, such as `author.my-addon`. The `swofty.` prefix is reserved. The `dbedit.` prefix is kept for addons the DB Editor saves, and the launcher refuses to install any other addon with it. |
| `name` | string | Display name. |
| `version` | string | Semver, such as `1.0.0`. |
| `author` | string | Display author. |
| `description` | string? | One-line summary, shown on the Tools page. |
| `license` | string? | SPDX identifier, such as `MIT`. |
| `homepage` | string? | A URL. |
| `icon` | string? | A path inside your addon, shown on the Tools page and your addon's sidebar pages. Use an `.svg` for a one-colour glyph in the launcher's icon style, or a square `.png`, `.jpg` or `.webp` to show the image as it is. |
| `kind` | string[] | What the addon touches: `database`, `erp`, `text`, `ui`, `files`, `native`, `simplified`. `text` needs launcher 1.3.0 or later and `simplified` 1.3.6 or later; an earlier launcher rejects the manifest. `simplified` marks a mod merged into the game's shader packages, as the Simplified Suite loads `.serps` mods. |
| `category` | string? | What the addon is for ([category](#category)). |
| `experimental_features` | string[]? | Experimental engines of the in-game core the addon uses: `overlay` ([Overlay](/reference/std/overlay)) `shaders` ([Shader packs](./shader-packs)) and `postfx` ([Post-process passes](./postfx)). While the addon is on, the build turns each one on, and the Addons page tags the addon **Uses experimental features**. Needs launcher 1.3.7 or later, and 1.4.0 or later for `shaders`; an earlier launcher rejects the manifest. |
| `origin` | object? | The tool and file the addon was made from ([origin](#origin)). |
| `shader` | object? | A colour look for the game's picture ([shader](#shader)). |
| `career_setup` | object? | A settings page in game between a Driver Career tile and the new-career wizard ([career_setup](#career-setup)). |
| `hub_tab` | object? | A settings page for one tab of the career hub's laptop, Team HQ ([hub_tab](#hub-tab)). |

## category

`kind` says what an addon touches; `category` says what it is for. The **Addons** page groups and filters by it.

```json
"category": "cars"
```

| Value | Label |
|---|---|
| `seasons` | Seasons |
| `tracks` | Tracks |
| `cars` | Cars |
| `helmets` | Helmets |
| `shaders` | Shaders |
| `career` | Career |
| `ai` | AI |
| `performance` | Performance |
| `utility` | Utility |

An addon without a category is listed as Uncategorised. `helmets` needs launcher 1.3.6 or later, `shaders` 1.4.0 or later, and `career`, `ai`, `performance` and `utility` 1.5.3 or later. DB Editor addons can't use `shaders`.

Launchers from 1.5.3 read a category they don't know as no category. Earlier launchers refuse the manifest; an addon that uses one of the 1.5.3 categories sets `compat.launcher` to `>=1.5.3`.

Launchers from 1.3.6 read the categories of earlier launchers: `liveries` as `cars`, and `drivers-teams`, `database`, `gameplay`, `ui`, `audio`, `tools` and `other` as no category.

## origin

`origin` records the tool that generated an addon and the file it came from. Tools set it; leave it out in an addon you write by hand.

```json
"origin": { "tool": "teamsimplified.mods", "tool_version": "2.0.0", "file": "okayama.zip",
            "sha256": "9f2c…" }
```

| Field | Type | Value |
|---|---|---|
| `tool` | string | `db-editor`, or the id of the addon that generated it, such as `teamsimplified.mods`. |
| `tool_version` | string? | The tool's version. |
| `file` | string? | The file name the addon was made from. |
| `sha256` | string? | That file's SHA-256, in hex. |

The **Addons** page shows the source as Built in, DB Editor, Imported or Installed. A launcher before 1.3.0 rejects `category` and `origin`; set `"compat": { "launcher": ">=1.3.0" }` when you use them.

## postfx

`postfx` lists HLSL post-process passes that run on the game's picture after its tonemap and before the HUD. [Post-process passes](./postfx) covers the shader contract.

```json
"experimental_features": ["postfx"],
"postfx": [{ "shader": "fx/vignette.hlsl", "options": ["{{amount}}"], "order": 0, "budget_ms": 1.5 }]
```

| Field | Type | Value |
|---|---|---|
| `shader` | string | The `.hlsl` file, a path inside the addon. |
| `name` | string? | The pass's name in logs. Defaults to the file name without `.hlsl`. |
| `options` | (number \| string)[]? | Up to 16 values for `option(i)`; a string is <span v-pre>`"{{id}}"`</span> naming a `db` option. |
| `order` | integer? | Lower runs first among every addon's passes. Defaults to 0. |
| `budget_ms` | number? | GPU milliseconds a frame past which the core turns the pass off. Defaults to 1.5. |

## shader

`shader` grades the game's picture with a `.cube` 3D LUT. [Shader packs](./shader-packs) covers making a look.

```json
"experimental_features": ["shaders"],
"shader": { "lut": "looks/{{look}}.cube", "strength": "{{strength}}" }
```

| Field | Type | Value |
|---|---|---|
| `lut` | string | The `.cube` file, a path inside the addon. <span v-pre>`{{id}}`</span> is replaced with a `db` option's value. |
| `strength` | number or string? | From 0 to 100, or <span v-pre>`"{{id}}"`</span> naming a `db` slider that stays within 0 to 100. Defaults to 100. |

`shader` needs `"shaders"` in `experimental_features` and launcher 1.4.0 or later. One look applies at a time: with two addons that have a `shader` on, the later one in the load order applies.

## saves

`saves` marks an addon that changes what careers store, such as one that adds drivers. The launcher warns before such an addon is turned off or removed:

```json
"saves": { "affects": true, "message": "Retire drivers on the Drivers page instead of removing the addon." }
```

| Field | Type | Value |
|---|---|---|
| `affects` | boolean | `true` makes the launcher ask before the addon is turned off in a profile or removed: "This addon has affected your saves. Careers you played with it may not load without it." Defaults to `false`. |
| `message` | string? | Shown under the warning, such as what to do instead. |

Before a session whose profile leaves this addon out, the launcher backs up the Steam profile save; see [`swofty saves`](/reference/cli#swofty-saves).

Leave `saves` out for an addon that doesn't affect saves. The launcher shows the warning in a dialog; `swofty addons disable` and `swofty addons remove` ask on a terminal and need `--yes` otherwise. Turning the addon on is never asked about. A launcher before 1.0.12 rejects the field; set `"compat": { "launcher": ">=1.0.12" }` when you use it.

## compat

```json
"compat": {
  "game": "f1_25",
  "game_builds": [">=25251167"],
  "launcher": ">=0.1.0",
  "seasons": ["f1_2026"]
}
```

| Field | Type | Value |
|---|---|---|
| `game` | string | `f1_25`. Any other value is rejected. |
| `game_builds` | string[] | Semver ranges matched against the build id as `<id>.0.0`. Leave empty to accept every build. |
| `launcher` | string? | A semver range the launcher must satisfy. On a launcher outside it, installing the addon fails, and so does building a profile that enables it. |
| `seasons` | string[] | Season tags, such as `f1_2026`. |

## requires, conflicts, load_after

```json
"requires": [{ "id": "swofty.core", "version": ">=0.1.0" }],
"conflicts": [{ "id": "*", "touches": "db:entrants.entrant_lists.entrant_list_34.*" }],
"load_after": ["swofty.track.*"]
```

| Field | Type | Value |
|---|---|---|
| `requires` | `[{ id, version }]` | List the addons that must be installed. `version` is a semver range and defaults to `*`. |
| `conflicts` | `[{ id, touches? }]` | List the addons that must not be enabled together. Use `touches` to narrow the conflict to a shared target; `id` may be `*`. |
| `load_after` | string[] | Addon ids or `*` patterns to build before your addon. |

A profile with your addon enabled builds only when every addon in `requires` is enabled at a matching version, and the build loads your addon after them, as if they were in `load_after`, whatever their places in the player's list. List as many as your addon needs; `swofty.core` and the other built-in addons are always present.

The [Addon Browser](/using/addon-browser#requirements) reads `requires` when a player installs your addon. It installs each required addon the player lacks, and each one's own `requires`, from the Addon Browser. It refuses the install when a required addon isn't published there, or when no published version matches the range and works with the player's launcher. Publish the addons yours requires before you publish yours.

## options

To give players settings they can change, declare them in `options`. They'll appear in your addon's row on the launcher's **Addons** page and on its page in **ADDON SETTINGS** in game. Values are stored per profile. Follow [Settings](./settings) for the full procedure.

```json
"options": [
  { "id": "show_panel", "type": "toggle", "label": "Show panel", "default": true, "binds": "native" },
  { "id": "ai_offset", "type": "slider", "label": "AI offset", "min": -10, "max": 10, "step": 1,
    "default": 0, "binds": "native" },
  { "id": "team_livery", "type": "choice", "label": "Team livery", "default": "titlesponsor",
    "values": ["titlesponsor", { "value": "tier01", "label": "Tier 1 sponsor" }, "replica"], "binds": "db" }
]
```

| Field | Type | Value |
|---|---|---|
| `id` | string | Use `[a-z][a-z0-9_]*`, at most 40 characters, and keep it unique within your addon. |
| `type` | string | `toggle`, `slider` or `choice`. |
| `label` | string? | Shown to players. Defaults to `id`. |
| `description` | string? | Shown under the label. |
| `default` | any | Use `true`/`false` for a toggle, a number on the slider's grid, or one of the choice values. |
| `min`, `max`, `step` | number | Set all three to define a slider's range. `max - min` must be a whole number of steps, at most 100. |
| `values` | array | Supply 1 to 16 values for a choice. Each is a string; use `{ "value", "label" }` to show a different name. |
| `binds` | string | `native` — the [engine plugin](./native-plugins#settings) reads the value while the game runs; `db` — the value is built into the game files, so a change needs a rebuild. |
| `hidden` | boolean? | `true` keeps the option off your addon's page in ADDON SETTINGS in game, such as a developer option or a per-career setting. The launcher's **Addons** page still lists it, and [`career_setup`](#career-setup) and [`hub_tab`](#hub-tab) may list it. Defaults to `false`. A launcher before 1.5.13 rejects the field. |

## career_setup

`career_setup` adds a page in game, in the CUSTOM SETTINGS look, that shows some of your addon's options before the new-career wizard. [Add a career setup page](./in-game-ui#add-a-career-setup-page) covers the page and its routes.

```json
"options": [
  { "id": "start_season", "type": "choice", "label": "Starting season", "default": "2016",
    "values": ["2016", "2017", "2018"], "binds": "native" }
],
"career_setup": {
  "route": "career_wizard_driver_career_progressive",
  "title": "Progressive Career",
  "options": ["start_season"]
}
```

| Field | Type | Value |
|---|---|---|
| `route` | string | `[a-z0-9_]`, at most 48 characters. The route in the career content select's route group that opens the page; a career content tile opens it through `tile_route`. |
| `title` | string? | The page's title and its category tile's title, shown in capitals as written. Defaults to the addon's `name`. |
| `options` | string[] | Ids from your `options`, in the order the rows show them. At least one, each once. `hidden` options may be listed. |
| `next` | string? | The new-career wizard screen CONFIRM opens. Defaults to `career_wizard_season_length_select`. |

The page needs your addon on in the profile. An addon has at most one `career_setup`. A launcher before 1.5.13 rejects the field; set `"compat": { "launcher": ">=1.5.13" }` when you use it.

## hub_tab

`hub_tab` has the launcher build the page of a Team HQ laptop tab that shows some of your addon's options. [Add a Team HQ tab](./in-game-ui#add-a-team-hq-tab) covers the page and what your plugin adds.

```json
"hub_tab": {
  "name": "progressive",
  "title": "Progressive Settings",
  "options": ["rnd_mode"],
  "data_group": "f1_career_progressive"
}
```

| Field | Type | Value |
|---|---|---|
| `name` | string | `[a-z0-9_]`, at most 40 characters. The page template is `swofty_hub_<name>` and its data `view_model_data.hud_swofty_hub_<name>`. |
| `title` | string | The tab's title. |
| `options` | string[] | 1 to 6 ids from your `options`, in the order the page shows them, each once. `hidden` options may be listed. |
| `data_group` | string | `[a-z0-9_]`. The career data group whose careers show the tab. |

The page needs your addon on in the profile. An addon has at most one `hub_tab`. A launcher before 1.5.13 rejects the field.

## hud

`hud.mfd` puts some of your options on an ADDONS page of the race MFD, after the game's own pages.

```json
"hud": { "mfd": { "title": "PIT LANE", "rows": ["limiter", "mode"] } }
```

| Field | Type | Value |
|---|---|---|
| `mfd.title` | string? | A heading row above the addon's rows, 1 to 32 characters. Defaults to the addon's `name`. |
| `mfd.rows` | array | 1 to 8 option ids of this addon, in page order. Each must be a `toggle` or `choice` option whose label is at most 32 characters. |

Each row shows the option's label and its current value. Launchers up to 1.5.26 reject a manifest with `hud`.

Development switches in the game's environment: `SWOFTY_MFD_TEST=open` opens the ADDONS page every `SWOFTY_MFD_TEST_EVERY` seconds (6 by default) through the game's own MFD open call, and `SWOFTY_MFD_TEST=next` also steps the first row's option each time, which saves the value as a change in ADDON SETTINGS does. `SWOFTY_MFD_TEST_PANEL` opens another page of the main group by name, such as `car_settings_panel`. The launcher builds the page into the game's UI files when the addon is on; the core updates a row whenever the option changes. Rows from every addon with `hud.mfd` share the one page, at most 8 in all, in load order. Changing a value from the MFD is not built yet: players change the options in ADDON SETTINGS or on the **Addons** page.

## operations

Put your data edits in `operations`: the build applies them to the game's files, or the game applies them in memory while it boots. [Operations](/reference/data/operations) explains what each field changes. Write source paths relative to your addon folder, using `/`, and target paths relative to the game folder.

```json
"operations": {
  "database": ["db/grid24.json"],
  "erp": ["erp/livery.json"],
  "ui": ["ui/tooltip.pkgpatch.json"],
  "files": [{ "source": "files/sponsor.dds", "target": "livery_package_shared/sponsor.dds" }],
  "native": [{ "dll": "native/plugin.dll", "early": false, "files": ["lang/"] }],
  "screens": [{ "name": "grid_page", "file": "ui/grid_page.ui", "flow": "flow_options", "anchor": "settings_container", "view_model": false }],
  "assets": ["assets/ops.json"]
}
```

| Field | Type | Contents |
|---|---|---|
| `database` | string[] | [Database operation files](/reference/ops/database). |
| `erp` | string[] | [ERP operation files](/reference/ops/erp). |
| `ui` | string[] | [UI patches](/reference/ops/ui-patches). |
| `files` | `[{ source, target }]` | Verbatim file copies into the game folder. See [File operations](/reference/ops/files). |
| `native` | `[NativePlugin]` | [Engine plugins](./native-plugins). |
| `screens` | `[ScreenOp]` | [Screens](./in-game-ui#add-a-screen) to register in game. |
| `assets` | string[] | [Asset op files](./asset-api#op-files) — `{ "schema_version": 1, "ops": [...] }` with prefixed ops such as `db.set`, `erp.patch-pkg` and `warn`. |

A **NativePlugin** is `{ dll, early, files }`. Give it the DLL path, use `early` to load it before the menu, and list the `files` it reads in game.

A **ScreenOp** is `{ name, file, page?, flow, anchor?, view_model }`. Choose `name` and `flow` values that use `[a-z0-9_]` and are unique across the profile's addons. Supply raw `.ui` data in `file`; it gets installed as `swofty/ui/<name>.ui`. Or leave `file` out and set `page` to `options`, and the launcher builds your addon's options page as the screen ([Use your options page](./in-game-ui#use-your-options-page)).

The `flow` defaults to `flow_frontend`. To register after another screen in the same flow, name that screen in `anchor`. Use `view_model` to ask core for a native view model. The names `swofty_settings`, `swofty_addon_<n>` and `swofty_map_<n>` belong to ADDON SETTINGS.

## launcher

To add sidebar pages, an importer, a build hook and commands, name a [launcher plugin](./launcher-api) in the `launcher` block. You supply a prebuilt program; the launcher runs it as a subprocess.

```json
"launcher": {
  "program": { "windows": "bin/example-helper.exe", "linux": "bin/example-helper" },
  "sections": [{ "id": "items", "title": "Example", "icon": "package" }],
  "imports": ["exmod"],
  "build": true,
  "commands": [{ "name": "example", "about": "List the example items" }]
}
```

| Field | Type | Value |
|---|---|---|
| `program` | `{ os: path }` | Give the plugin's path inside your addon for each OS. Keys are `linux`, `windows`, `macos`; supply at least one. |
| `sections` | `[{ id, title, icon }]` | Each entry adds a sidebar page. Choose an `icon` from the launcher's [icon set](./launcher-api#views). To give a page an icon of your own, ship a one-colour `sections/<id>.svg`: the sidebar and the page's rows without art show it. A page with the id `tracks` is listed in the sidebar's ADDONS group, beside Tools and Database, rather than under your addon. |
| `imports` | string[] | List file extensions without the dot. When someone drops a file with one of these extensions, the launcher routes it to your plugin. |
| `build` | boolean | Set to `true` when your plugin answers the `build` call. Defaults to `false`. |
| `commands` | `[CommandSpec]` | `swofty` subcommands — `{ name, about, args, subcommands }`. |
| `claims` | boolean | Set to `true` to decide yourself which dropped files and folders you import: the launcher runs `<program> claims <path>` and reads `yes`, `maybe` or `no` ([Importers](./launcher-api#importers-and-drag-and-drop)). Defaults to `false`. |
| `startup` | boolean | Set to `true` to receive a `startup` event once per launcher start, with the 300-second event budget, for long work such as converting mods ([Events and effects](./launcher-api#events-and-effects)). Launcher 1.3.0 or later. Defaults to `false`. |

## Paths and validation

When you install the addon, the installer checks your manifest and the files it names:

- Keep every path relative and inside the addon — no leading `/`, no `\`, no `..`.
- Use a lowercase dotted `id`; keep option ids `[a-z][a-z0-9_]*` and unique, and screen names `[a-z0-9_]` and unique.
- Name at least one program in `launcher.program`; each `imports` entry must be non-empty and have no dot.

Some operation types are still in development. Check [Current status](/design/#current-status) and use [the template](./template) as your starting point.
