---
title: Addon manifest
description: Every swofty.json field in one annotated manifest, then each group of fields with an example, its rules and the launcher version it needs.
---

# Addon manifest

`swofty.json` sits at the root of every addon. It names the addon, says which game and launcher it works with, declares its settings and lists what it ships.

## Every field

This manifest uses every field. Only `schema_version`, `id`, `name`, `version`, `author` and `compat` are required; leave out the rest until you need them.

```jsonc [swofty.json]
{
  // Identity
  "schema_version": 1,                               // always 1
  "id": "yourname.my-addon",                         // lowercase, dotted, at least two parts
  "name": "My Addon",                                // display name
  "version": "1.0.0",                                // semver
  "author": "Your Name",                             // display author
  "description": "One line, shown on the Tools page.",
  "license": "MIT",                                  // SPDX identifier
  "homepage": "https://example.com/my-addon",
  "icon": "icon.png",                                // a path inside the addon
  "kind": ["database", "erp", "text", "ui", "files", "native"],   // what it touches
  "category": "career",                              // what it is for
  "origin": { "tool": "teamsimplified.mods", "tool_version": "2.0.0", "file": "okayama.zip", "sha256": "9f2c…" },

  // Compatibility and dependencies
  "compat": {
    "game": "f1_25",                                 // the only value
    "game_builds": [">=25251167"],                   // empty: every build
    "launcher": ">=1.5.13",                          // the launcher versions it installs and builds on
    "seasons": ["f1_2026"]
  },
  "requires": [{ "id": "swofty.core", "version": ">=0.1.0" }],
  "conflicts": [{ "id": "*", "touches": "db:entrants.entrant_lists.entrant_list_34.*" }],
  "load_after": ["swofty.track.*"],
  "saves": { "affects": true, "message": "Retire drivers on the Drivers page instead of removing the addon." },

  // Settings and their pages
  "options": [
    { "id": "start_season", "type": "choice", "label": "Starting season", "default": "2016",
      "values": ["2016", "2017", { "value": "2018", "label": "Season 2018" }], "binds": "native" },
    { "id": "ai_offset", "type": "slider", "label": "AI offset", "min": -10, "max": 10, "step": 1,
      "default": 0, "binds": "native", "hidden": true },
    { "id": "look", "type": "choice", "label": "Look", "default": "noir", "values": ["noir", "overcast"], "binds": "db" },
    { "id": "strength", "type": "slider", "label": "Strength", "min": 0, "max": 100, "step": 10,
      "default": 100, "binds": "db" },
    { "id": "vignette", "type": "toggle", "label": "Vignette", "default": true, "binds": "db" }
  ],
  "career_setup": {
    "route": "career_wizard_driver_career_my_addon",
    "title": "My Addon",
    "options": ["start_season", "ai_offset"],
    "next": "career_wizard_season_length_select"
  },
  "hub_tab": { "name": "my_addon", "title": "My Addon Settings", "options": ["ai_offset"], "data_group": "f1_career_my_addon" },

  // What it ships
  "operations": {
    "database": ["db/grid24.json"],
    "erp": ["erp/livery.json"],
    "ui": ["ui/tooltip.pkgpatch.json"],
    "files": [{ "source": "files/sponsor.dds", "target": "livery_package_shared/sponsor.dds" }],
    "native": [{ "dll": "native/plugin.dll", "early": false, "files": ["lang/"] }],
    "screens": [{ "name": "grid_page", "file": "ui/grid_page.ui", "flow": "flow_options",
                  "anchor": "settings_container", "view_model": false }],
    "assets": ["assets/ops.json"]
  },
  "launcher": {
    "program": { "windows": "bin/my-addon-helper.exe", "linux": "bin/my-addon-helper" },
    "sections": [{ "id": "items", "title": "Items", "icon": "package" }],
    "imports": ["exmod"],
    "build": true,
    "commands": [{ "name": "my-addon", "about": "List the items" }],
    "claims": false,
    "startup": false
  },

  // Looks
  "experimental_features": ["overlay", "shaders", "postfx"],
  "shader": { "lut": "looks/{{look}}.cube", "strength": "{{strength}}" },
  "postfx": [{ "shader": "fx/vignette.hlsl", "name": "vignette", "options": ["{{vignette}}", 0.5], "order": 0, "budget_ms": 1.5 }]
}
```

The comments are for this page; `swofty.json` is plain JSON. A field the launcher doesn't know is an error, so a misspelled field or a field from a newer launcher stops the install:

```text
swofty: My Addon (yourname.my-addon) needs a newer Swofty Launcher (unknown field `colour`)
swofty: My Addon (yourname.my-addon) needs a newer Swofty Launcher (unknown value `postfx`)
```

A rule broken inside a known field names the field:

```text
swofty: invalid manifest: yourname.my-addon: option ai_offset: a slider has at most 100 steps
```

## Identity

```json
"schema_version": 1,
"id": "author.my-addon",
"name": "My Addon",
"version": "1.0.0",
"author": "Your name",
"description": "One-line summary.",
"license": "MIT",
"homepage": "https://example.com/my-addon",
"icon": "icons/my-addon.svg",
"kind": ["database", "erp"]
```

- **`schema_version`** (integer): `1`.
- **`id`** (string): a lowercase dotted id with at least two parts, each part `[a-z0-9_-]`, such as `author.my-addon`. The `swofty.` prefix is reserved. The `dbedit.` prefix is kept for addons the DB Editor saves, and the launcher refuses to install any other addon with it.
- **`name`** (string): the display name.
- **`version`** (string): semver, such as `1.0.0`.
- **`author`** (string): the display author.
- **`description`** (string, optional): a one-line summary, shown on the Tools page.
- **`license`** (string, optional): an SPDX identifier, such as `MIT`.
- **`homepage`** (string, optional): a URL.
- **`icon`** (string, optional): a path inside your addon, shown on the Tools page and your addon's sidebar pages. Use an `.svg` for a one-colour glyph in the launcher's icon style, or a square `.png`, `.jpg` or `.webp` to show the image as it is.
- **`kind`** (string array): what the addon touches.

| `kind` | Launcher |
|---|---|
| `database`, `erp`, `ui`, `files`, `native` | any |
| `text` | 1.3.0 or later |
| `simplified` | 1.3.6 or later. Marks a mod merged into the game's shader packages, as the Simplified Suite loads `.serps` mods |

An earlier launcher rejects a manifest with a `kind` it doesn't know.

### category

`kind` says what an addon touches; `category` (string, optional) says what it is for. The **Addons** page groups and filters by it.

```json
"category": "cars"
```

| Value | Label | Launcher |
|---|---|---|
| `seasons` | Seasons | |
| `tracks` | Tracks | |
| `cars` | Cars | |
| `helmets` | Helmets | 1.3.6 or later |
| `shaders` | Shaders | 1.4.0 or later |
| `career` | Career | 1.5.3 or later |
| `ai` | AI | 1.5.3 or later |
| `performance` | Performance | 1.5.3 or later |
| `utility` | Utility | 1.5.3 or later |

- An addon without a category is listed as Uncategorised.
- DB Editor addons can't use `shaders`.
- Launchers from 1.5.3 read a category they don't know as no category. Earlier launchers refuse the manifest, so an addon that uses one of the 1.5.3 categories sets `compat.launcher` to `>=1.5.3`.
- Launchers from 1.3.6 read the categories of earlier launchers: `liveries` as `cars`, and `drivers-teams`, `database`, `gameplay`, `ui`, `audio`, `tools` and `other` as no category.

### origin

`origin` (object, optional) records the tool that generated an addon and the file it came from. Tools set it; leave it out in an addon you write by hand.

```jsonc
"origin": {
  "tool": "teamsimplified.mods",   // db-editor, or the id of the addon that generated it
  "tool_version": "2.0.0",         // optional: the tool's version
  "file": "okayama.zip",           // optional: the file name the addon was made from
  "sha256": "9f2c…"                // optional: that file's SHA-256, in hex
}
```

The **Addons** page shows the source as Built in, DB Editor, Imported or Installed.

A launcher before 1.3.0 rejects `category` and `origin`; set `"compat": { "launcher": ">=1.3.0" }` when you use them.

## Compatibility and dependencies

### compat

`compat` (object) is required.

```jsonc
"compat": {
  "game": "f1_25",                 // f1_25; any other value is rejected
  "game_builds": [">=25251167"],   // semver ranges matched against the build id as <id>.0.0; empty accepts every build
  "launcher": ">=0.1.0",           // optional: a semver range the launcher must satisfy
  "seasons": ["f1_2026"]           // season tags
}
```

On a launcher outside `compat.launcher`, installing the addon fails, and so does building a profile that enables it:

```text
yourname.my-addon needs Swofty Launcher >=1.5.13; this is 1.5.12. Update the launcher, or turn the addon off
```

### requires, conflicts, load_after

```jsonc
"requires": [{ "id": "swofty.core", "version": ">=0.1.0" }],                            // version defaults to *
"conflicts": [{ "id": "*", "touches": "db:entrants.entrant_lists.entrant_list_34.*" }],  // touches is optional
"load_after": ["swofty.track.*"]
```

- **`requires`** (`[{ id, version }]`): the addons that must be installed. `version` is a semver range and defaults to `*`.
- **`conflicts`** (`[{ id, touches? }]`): the addons that must not be enabled together. Use `touches` to narrow the conflict to a shared target; `id` may be `*`.
- **`load_after`** (string array): addon ids or `*` patterns to build before your addon.

A profile with your addon enabled builds only when every addon in `requires` is enabled at a matching version, and the build loads your addon after them, as if they were in `load_after`, whatever their places in the player's list. List as many as your addon needs; `swofty.core` and the other built-in addons are always present.

The [Addon Browser](/using/addon-browser#requirements) reads `requires` when a player installs your addon. It installs each required addon the player lacks, and each one's own `requires`, from the Addon Browser. It refuses the install when a required addon isn't published there, or when no published version matches the range and works with the player's launcher. Publish the addons yours requires before you publish yours.

### saves

`saves` (object, optional) marks an addon that changes what careers store, such as one that adds drivers. The launcher warns before such an addon is turned off or removed:

```jsonc
"saves": {
  "affects": true,     // ask before the addon is turned off in a profile or removed; defaults to false
  "message": "Retire drivers on the Drivers page instead of removing the addon."   // optional: shown under the warning
}
```

The launcher shows the warning in a dialog: **This addon has affected your saves**, "Careers you played with it may not load without it.", then your `message`. `swofty addons disable` and `swofty addons remove` ask on a terminal, and need `--yes` otherwise:

```text
Warning: Driver Manager (swofty.driver-manager) has affected your saves. Careers you played with it may not load without it.
Careers that use its drivers won't load without it.
```

- Turning the addon on is never asked about.
- Before a session whose profile leaves this addon out, the launcher backs up the Steam profile save; see [`swofty saves`](/reference/cli#swofty-saves).
- Leave `saves` out for an addon that doesn't affect saves.
- A launcher before 1.0.12 rejects the field; set `"compat": { "launcher": ">=1.0.12" }` when you use it.

## Settings and their pages

### options

`options` declares the settings players can change. They appear in your addon's row on the launcher's **Addons** page and on its page in **ADDON SETTINGS** in game, and each profile stores its own values. [Settings](./settings) covers reading them.

```jsonc
"options": [
  { "id": "show_panel", "type": "toggle", "label": "Show panel", "default": true, "binds": "native" },
  { "id": "ai_offset", "type": "slider", "label": "AI offset", "min": -10, "max": 10, "step": 1,
    "default": 0, "binds": "native" },
  { "id": "team_livery", "type": "choice", "label": "Team livery", "default": "titlesponsor",
    "values": ["titlesponsor", { "value": "tier01", "label": "Tier 1 sponsor" }, "replica"], "binds": "db" },
  { "id": "debug_overlay", "type": "toggle", "label": "Debug overlay", "default": false, "binds": "native",
    "description": "Shown under the label.",
    "hidden": true }                 // kept off the ADDON SETTINGS page in game; launcher 1.5.13 or later
]
```

- **`id`** (string): `[a-z][a-z0-9_]*`, at most 40 characters, unique within your addon.
- **`type`** (string): `toggle`, `slider` or `choice`.
- **`label`** (string, optional): shown to players. Defaults to `id`.
- **`description`** (string, optional): shown under the label.
- **`default`**: `true` or `false` for a toggle, a number on the slider's grid, or one of the choice values.
- **`min`, `max`, `step`** (numbers): a slider's range. Set all three, with `min` below `max` and `step` above 0. `max - min` must be a whole number of steps, at most 100. A toggle and a choice take none of them.
- **`values`** (array): 1 to 16 values for a choice, each a string, or `{ "value", "label" }` to show a different name. Values are non-empty and each is listed once. A toggle and a slider take no `values`.
- **`binds`** (string): where the value goes.
- **`hidden`** (boolean, optional): `true` keeps the option off your addon's page in ADDON SETTINGS in game, such as a developer option or a per-career setting. The launcher's **Addons** page still lists it, and [`career_setup`](#career-setup) and [`hub_tab`](#hub-tab) may list it. Defaults to `false`. A launcher before 1.5.13 rejects the field.

| `binds` | Read by | A change applies |
|---|---|---|
| `native` | the [engine plugin](./native-plugins#settings), while the game runs | at once |
| `db` | the build, through <span v-pre>`{{id}}`</span> and `when` in op files | after a rebuild |

### career_setup

`career_setup` (object, optional) adds a page in game, in the CUSTOM SETTINGS look, that shows some of your addon's options before the new-career wizard. [Add a career setup page](./in-game-ui#add-a-career-setup-page) covers the page and its routes.

```jsonc
"options": [
  { "id": "start_season", "type": "choice", "label": "Starting season", "default": "2016",
    "values": ["2016", "2017", "2018"], "binds": "native" }
],
"career_setup": {
  "route": "career_wizard_driver_career_progressive",   // [a-z0-9_], at most 48 characters
  "title": "Progressive Career",                        // optional: defaults to the addon's name
  "options": ["start_season"],                          // ids from your options, in row order
  "next": "career_wizard_season_length_select"          // optional: this is the default
}
```

- **`route`**: the route in the career content select's route group that opens the page. A career content tile opens it through `tile_route`.
- **`title`**: the page's title and its category tile's title, shown in capitals as written. Not empty when set.
- **`options`**: ids from your `options`, in the order the rows show them. At least one, each once. `hidden` options may be listed.
- **`next`**: the new-career wizard screen CONFIRM opens, `[a-z0-9_]`. Defaults to `career_wizard_season_length_select`.

The page needs your addon on in the profile. An addon has at most one `career_setup`. A launcher before 1.5.13 rejects the field; set `"compat": { "launcher": ">=1.5.13" }` when you use it.

### hub_tab

`hub_tab` (object, optional) has the launcher build the page of a Team HQ laptop tab that shows some of your addon's options. [Add a Team HQ tab](./in-game-ui#add-a-team-hq-tab) covers the page and what your plugin adds.

```jsonc
"hub_tab": {
  "name": "progressive",                  // [a-z0-9_], at most 40 characters
  "title": "Progressive Settings",        // the tab's title; not empty
  "options": ["rnd_mode"],                // 1 to 6 ids from your options, in page order, each once
  "data_group": "f1_career_progressive"   // [a-z0-9_]: the career data group whose careers show the tab
}
```

The page template is `swofty_hub_<name>` and its data `view_model_data.hud_swofty_hub_<name>`. `hidden` options may be listed in `options`.

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

`operations` lists your data edits and code. The build applies them to the game's files, or the game applies them in memory while it boots. [Operations](/reference/data/operations) explains what each one changes. Write source paths relative to your addon folder, using `/`, and target paths relative to the game folder.

```jsonc
"operations": {
  "database": ["db/grid24.json"],               // database operation files
  "erp": ["erp/livery.json"],                   // ERP operation files
  "ui": ["ui/tooltip.pkgpatch.json"],           // UI patches
  "files": [{ "source": "files/sponsor.dds", "target": "livery_package_shared/sponsor.dds" }],   // verbatim copies into the game folder
  "native": [{ "dll": "native/plugin.dll", "early": false, "files": ["lang/"] }],               // engine plugins
  "screens": [{ "name": "grid_page", "file": "ui/grid_page.ui", "flow": "flow_options",
                "anchor": "settings_container", "view_model": false }],                         // screens to register in game
  "assets": ["assets/ops.json"]                 // asset op files
}
```

- **`database`**: [database operation files](/reference/ops/database).
- **`erp`**: [ERP operation files](/reference/ops/erp).
- **`ui`**: [UI patches](/reference/ops/ui-patches).
- **`files`**: verbatim file copies into the game folder; see [File operations](/reference/ops/files).
- **`native`**: [engine plugins](./native-plugins).
- **`screens`**: [screens](./in-game-ui#add-a-screen) to register in game.
- **`assets`**: [asset op files](./asset-api#op-files), `{ "schema_version": 1, "ops": [...] }` with prefixed ops such as `db.set`, `erp.patch-pkg` and `warn`.

### native entries

```jsonc
{
  "dll": "native/plugin.dll",   // the DLL path
  "early": false,               // true loads it before the menu, at the early stage
  "files": ["lang/"]            // the files and folders it reads in game
}
```

An entry may also carry `capabilities` (a string array) from older manifests. It is accepted and has no effect. [Engine plugins](./native-plugins#the-operations-native-entry) covers each field.

### screens entries

```jsonc
// a screen from your own .ui file
{
  "name": "grid_page",              // [a-z0-9_], at most 64 characters, unique across the profile's addons
  "file": "ui/grid_page.ui",        // raw .ui data; installed as swofty/ui/grid_page.ui
  "flow": "flow_options",           // [a-z0-9_]; defaults to flow_frontend
  "anchor": "settings_container",   // optional: register after this screen in the same flow
  "view_model": false               // true asks core for a native view model
}

// your addon's options page as a screen
{ "name": "my_addon_settings", "page": "options", "anchor": "settings_container" }
```

- Each screen has a `file`, or `"page": "options"` and no file. `page` takes no other value.
- With `"page": "options"`, the launcher builds your addon's options page, as ADDON SETTINGS shows it, as the screen. The route `open_<name>` opens it from `anchor` ([Use your options page](./in-game-ui#use-your-options-page)).
- Two addons in one profile can't declare the same screen `name`; the build stops with `screen grid_page is declared by a.one and b.two`.
- The names `swofty_settings`, `swofty_addon_<n>` and `swofty_map_<n>` belong to ADDON SETTINGS.

Some operation types are still in development. Check [Current status](/design/#current-status) and use [the template](./template) as your starting point.

## launcher

`launcher` (object, optional) names a [launcher plugin](./launcher-api): a prebuilt program the launcher runs as a subprocess. It adds sidebar pages, an importer, a build hook and commands.

```jsonc
"launcher": {
  "program": { "windows": "bin/example-helper.exe", "linux": "bin/example-helper" },   // keys: linux, windows, macos; at least one
  "sections": [{ "id": "items", "title": "Example", "icon": "package" }],              // one sidebar page each
  "imports": ["exmod"],            // extensions without the dot; dropped files with them go to your plugin
  "build": true,                   // your plugin answers the build call; defaults to false
  "commands": [{                   // swofty subcommands
    "name": "example",
    "about": "List the example items",
    "args": [{ "name": "filter", "help": "Only items whose name has this text", "required": false, "many": false }],
    "subcommands": []
  }],
  "claims": false,                 // true: you decide which dropped files and folders you import
  "startup": false                 // true: a startup event once per launcher start; launcher 1.3.0 or later
}
```

- **`program`**: the plugin's path inside your addon for each OS.
- **`sections`**: each `{ id, title, icon }` adds a sidebar page. Choose an `icon` from the launcher's [icon set](./launcher-api#views). To give a page an icon of your own, ship a one-colour `sections/<id>.svg`: the sidebar and the page's rows without art show it. A page with the id `tracks` is listed in the sidebar's ADDONS group, beside Tools and Database, rather than under your addon.
- **`imports`**: when someone drops a file with one of these extensions, the launcher routes it to your plugin. Each entry is non-empty and has no dot.
- **`build`**: set to `true` when your plugin answers the `build` call.
- **`commands`**: `{ name, about, args, subcommands }`. Each arg is `{ name, help, required, many }`.
- **`claims`**: with `true`, the launcher runs `<program> claims <path>` and reads `yes`, `maybe` or `no` ([Importers](./launcher-api#importers-and-drag-and-drop)). Defaults to `false`.
- **`startup`**: with `true`, your addon's first section receives a `startup` event once per launcher start, with the 300-second event budget, for long work such as converting mods ([Events and effects](./launcher-api#events-and-effects)). Launcher 1.3.0 or later. Defaults to `false`.

## Looks

`experimental_features` (string array, optional) lists the experimental engines of the in-game core the addon uses. While the addon is on, the build turns each one on, and the **Addons** page tags the addon **Uses experimental features**, with a tag for what each engine does.

```json
"experimental_features": ["overlay", "shaders", "postfx"]
```

| Value | Engine | Tag | Launcher |
|---|---|---|---|
| `overlay` | [Overlay](/reference/std/overlay) | Draws over the game | 1.3.7 or later |
| `shaders` | [Shader packs](./shader-packs) | Changes the picture | 1.4.0 or later |
| `postfx` | [Post-process passes](./postfx) | Changes the picture | 1.5.4 or later |

An earlier launcher rejects the manifest.

### shader

`shader` (object, optional) grades the game's picture with a `.cube` 3D LUT. [Shader packs](./shader-packs) covers making a look.

```jsonc
"experimental_features": ["shaders"],     // required for shader
"shader": {
  "lut": "looks/{{look}}.cube",           // a .cube file inside the addon; {{id}} is replaced with a db option's value
  "strength": "{{strength}}"              // optional: 0 to 100, or {{id}} naming a db slider within 0 to 100; defaults to 100
}
```

`shader` needs `"shaders"` in `experimental_features` and launcher 1.4.0 or later. One look applies at a time: with two addons that have a `shader` on, the later one in the load order applies.

### postfx

`postfx` (array, optional) lists HLSL post-process passes that run on the game's picture after its tonemap and before the HUD. [Post-process passes](./postfx) covers the shader contract.

```jsonc
"experimental_features": ["postfx"],      // required for postfx
"postfx": [{
  "shader": "fx/vignette.hlsl",           // a .hlsl file inside the addon
  "name": "vignette",                     // optional: the pass's name in the build output and logs; defaults to the file name without .hlsl
  "options": ["{{amount}}", 0.5],         // optional: up to 16 values for option(i)
  "order": 0,                             // optional: lower runs first among every addon's passes; defaults to 0
  "budget_ms": 1.5                        // optional: GPU milliseconds a frame past which the core turns the pass off; defaults to 1.5
}]
```

- Each `options` value is a number, or a string <span v-pre>`"{{id}}"`</span> naming one `db` option whose value is a number or a toggle.
- Passes with the same `order` keep the load order.
- `budget_ms` is above 0 and at most 16.

## Paths and validation

When you install the addon, the installer checks your manifest and the files it names:

- Keep every path relative and inside the addon: no leading `/`, no `\`, no `..`.
- Use a lowercase dotted `id`; keep option ids `[a-z][a-z0-9_]*` and unique, and screen names `[a-z0-9_]` and unique.
- Name at least one program in `launcher.program`; each `imports` entry must be non-empty and have no dot.
