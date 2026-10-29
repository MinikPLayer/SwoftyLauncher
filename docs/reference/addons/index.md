---
title: Addons
description: An addon is a folder or .swoftyaddon with a swofty.json. It can change game data through operations, run an engine plugin in the game, run a launcher plugin, or all three. One complete addon, then a map of every part of making one.
---

# Addons

An addon is a folder with a `swofty.json` at its root, shared as a `.swoftyaddon` file and installed on the launcher's **Addons** page.

## An addon in full

This addon makes a stop-go penalty 10 seconds long. It is one manifest and one database change:

```text
stop-go/
├── swofty.json
└── db/
    └── penalty.json
```

```jsonc [swofty.json]
{
  "schema_version": 1,                 // always 1
  "id": "yourname.stop-go",            // lowercase, dotted, at least two parts; swofty. is reserved
  "name": "Ten-second stop-go",
  "version": "0.1.0",                  // semver
  "author": "Your Name",
  "description": "Makes a stop-go penalty 10 seconds long.",
  "kind": ["database"],                // what it touches
  "compat": { "game": "f1_25" },       // the only game
  "operations": {
    "database": ["db/penalty.json"]    // op files the build applies to the game database
  }
}
```

```json [db/penalty.json]
{
  "schema_version": 1,
  "ops": [
    { "op": "set", "select": "rules.stop_go_penalty", "field": "penalty_duration", "value": "10" }
  ]
}
```

The comments are for this page. `swofty.json` is plain JSON, and a field the launcher doesn't know is an error ([Addon manifest](./manifest)).

Install it, enable it, build it and play it:

```sh
swofty addons install ./stop-go          # checks the manifest, copies the folder to $DATA/addons/yourname.stop-go/
swofty addons enable yourname.stop-go    # into the active profile; the vanilla profile takes no addons
swofty build                             # applies the op to a copy of the database; the game folder is untouched
swofty run --apply                       # installs the build, starts F1 25 and follows the logs
```

```text
profile default: swofty.core -> … -> yourname.stop-go
overlay 2ce84ff2c20476d4: $DATA/cache/overlay/default/2ce84ff2c20476d4
  replace game_package/gamedatabases/gamedatabases.2025.bin
```

`swofty build` prints the load order, every `warning:` line and the game files the build replaces. In game, a stop-go penalty now lasts 10 seconds. When the game exits, the game folder returns to its stock files.

To share it, zip the folder's contents with `swofty.json` at the zip root and name the file `yourname.stop-go.swoftyaddon` ([Packaging and publishing](./publishing#package-it)).

## Data and code

The manifest lists what the addon ships:

- **Data.** [Operations](/reference/data/operations) that change the game's database, text, archives and files. No code runs. An addon with only operations is a **data addon**, like the one above.
- **Code.** An [engine plugin](./native-plugins), a DLL that runs inside F1 25, and a [launcher plugin](./launcher-api), a program that adds pages and commands to the launcher. An addon with either is a **code addon**.

One addon can ship data and code together. This one has a database operation, an engine plugin, a launcher page and a setting:

```json [swofty.json]
{
  "schema_version": 1,
  "id": "me.my-addon",
  "name": "My Addon",
  "version": "0.1.0",
  "author": "Me",
  "compat": { "game": "f1_25" },
  "options": [{ "id": "verbose", "type": "toggle", "label": "Verbose", "default": false, "binds": "native" }],
  "operations": {
    "database": ["db/changes.json"],
    "native": [{ "dll": "native/my_plugin.dll" }]
  },
  "launcher": {
    "program": { "linux": "bin/helper", "windows": "bin/helper.exe" },
    "sections": [{ "id": "items", "title": "Items", "icon": "package" }]
  }
}
```

Content players drop on the **Mods** page (`.serps` packages, track zips) is a mod, not an addon; see [Mods](/reference/mods/).

## Make an addon

1. **Create the folder and `swofty.json`.** Give the addon an `id`, `name`, `version` and `author`. [Your first addon](./first-addon) walks through it, and [the manifest](./manifest) lists every field.
2. **Add operations, code or both.** List data changes under `operations` ([Data addons](/reference/data/)), name a DLL in `operations.native` ([Engine plugins](./native-plugins)), or name a program in `launcher` ([Launcher plugins](./launcher-api)). A [template](./template) gives you a working start.
3. **Add settings.** Declare `options` so players can change your addon from the launcher and in game ([Settings](./settings)).
4. **Build and test.** Install the folder, run `swofty build`, read the warnings, then play with `swofty run --apply` ([Build and test](./publishing#build)).
5. **Package and publish.** Zip the folder's contents as a `.swoftyaddon` and share it ([Packaging and publishing](./publishing#package-it)).

## The map

Every part of making an addon, with the manifest field that turns it on:

- **[Your first addon](./first-addon)**: a complete addon with one setting and a launcher page, built and installed step by step.
- **[Templates](./template)**: the repository templates, what each file is and how to build them.
  - `templates/addon`: data and an engine plugin.
  - `templates/launcher-addon`: a launcher plugin.
  - `templates/postfx-pass` and `templates/shader-pack`: a post-process pass and colour looks.
- **[Addon manifest](./manifest)**: every `swofty.json` field.
  - **Identity**: `id`, `name`, `version`, `kind`, `category`, `origin`.
  - **Compatibility and dependencies**: `compat`, `requires`, `conflicts`, `load_after`, `saves`.
  - **Settings and their pages**: `options`, `career_setup`, `hub_tab`.
  - **Operations**: `operations` and its seven lists.
  - **Looks**: `experimental_features`, `shader`, `postfx`.
  - **Launcher plugin**: `launcher`.
- **[Settings](./settings)** (`options`): read in op files (`binds: db`), in an engine plugin (`binds: native`) and in a launcher plugin.
- **Data** (`operations`): change the game's files at build time, or in memory while it boots.
  - **[Data addons](/reference/data/)**: database values, text, textures, models and loose files.
  - **[Menus and UI](/reference/ui/)**: move or add elements on a menu screen (`operations.erp`), or add a screen (`operations.screens`).
  - **[Changes at runtime](./runtime-assets)**: textures, strings, database records and asset groups applied in memory.
  - **[Asset API](./asset-api)**: the asset op files in `operations.assets`, and the native assets table.
- **[Engine plugins](./native-plugins)** (`operations.native`): a DLL that runs inside F1 25. Hooks, telemetry, an overlay.
  - **[The mixin system](./hooking)**: wrap an engine function or run code at an instruction.
  - **[In-game UI](./in-game-ui)**: a screen, a home tile, a career setup page and a Team HQ tab.
  - **[Standard library](/reference/std/)**: events, the career, the overlay, telemetry, database reads, storage, settings and logging.
  - **[Engine API](/reference/game/)**: the engine functions, structs and sites you can call and hook.
- **[Launcher plugins](./launcher-api)** (`launcher`): a helper program that adds sidebar pages, an importer, a build hook, `swofty` commands and a profile summary.
- **Looks**: the race picture.
  - **[Shader packs](./shader-packs)** (`shader`): change the race's colours with a `.cube` LUT.
  - **[Post-process passes](./postfx)** (`postfx`): run your own shader on the race picture.
- **[Packaging and publishing](./publishing)**: build, test in game, read the logs, package a `.swoftyaddon` and release it.

## What the launcher does with an addon

1. **Install.** `swofty addons install <path>` checks the manifest and copies the addon to `$DATA/addons/<id>/`.
2. **Enable.** Your [profile](/using/profiles) lists the addons it uses, their load order and each addon's settings.
3. **Build.** Press **Play** (or run `swofty build`) and the launcher applies every enabled addon's operations to copies of the game's files, or queues them for the game to apply in memory. It also runs each launcher plugin's build step.
4. **Launch.** The launcher puts the build and the in-game loader into the game folder. The loader starts SwoftyMod, which loads each addon's engine plugin.
5. **Play.** Open **ADDON SETTINGS** in game to see each addon's options. Change one and its engine plugin receives the change right away.
6. **Exit.** Settings you changed in game are saved back to the profile, and the game folder returns to how it was.
