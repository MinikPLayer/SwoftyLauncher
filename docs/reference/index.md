---
title: Developer overview
description: What you can make for Swofty Launcher — mods, data addons, engine plugins, launcher plugins and menu edits — where each one starts, and the tools that help.
---

# Developer overview

You can make two kinds of content for Swofty Launcher. A **mod** is content players drop on the launcher's **Mods** page: a `.serps` package, `.erp` files or a community track zip. An **addon** is a folder or `.swoftyaddon` file with a `swofty.json`, installed on the **Addons** page. Find what you want to make below, then start on its first page.

## What do you want to make?

| You want to | Build | Start at |
|---|---|---|
| Share database, text or texture changes you made in Simplified Studio | A `.serps` mod | [Mods](/reference/mods/) |
| Share a community track | A track package (a mod) | [Track packages](/reference/mods/tracks) |
| Change database values, in-game text, textures or models with exact edits, optionally behind a setting | A data addon | [Data addons](/reference/data/) |
| Move, add or restyle elements on a menu screen | A menu edit, shipped in a data addon | [Menus and UI](/reference/ui/) |
| Add a screen or a home tile in game | An addon with `screens` and an engine plugin | [In-game UI](/reference/addons/in-game-ui) |
| Run your own code in the game: hook engine functions, read telemetry, draw an overlay | An engine plugin | [Engine plugins](/reference/addons/native-plugins) |
| Add a launcher page, an importer, a `swofty` command or a build step | A launcher plugin | [Launcher plugins](/reference/addons/launcher-api) |

Every addon follows the same five steps, whatever it holds. They're on [Addons](/reference/addons/).

## The three building blocks

An addon is made from up to three blocks. Its `swofty.json` lists each one it uses, and one addon can use all three.

**Operations.** Data changes listed under `operations` in `swofty.json`: set a database field, replace a texture, add a line of text, copy a file, register a screen. The launcher applies them when it builds a profile, to copies of the game's files or in memory while the game boots. An addon with only operations is a **data addon**; it needs no compiler. See [Operations](/reference/data/operations) for every kind.

**Engine plugin.** A Windows x86-64 DLL named in `operations.native`. The in-game loader loads it into F1 25 and hands it the `SwoftyApi`, which hooks engine functions through [the mixin system](/reference/addons/hooking) and gives access to the [standard library](/reference/std/): events, overlay, telemetry, database reads, storage, settings and logging. See [Engine plugins](/reference/addons/native-plugins).

**Launcher plugin.** A program named in the `launcher` block. The launcher runs it as a subprocess that speaks newline-delimited JSON, and it adds sidebar pages, an importer for dropped files, ops at build time, `swofty` subcommands and a line on the profile card. See [Launcher plugins](/reference/addons/launcher-api).

[Settings](/reference/addons/settings) work with all three: a `db` setting fills values in operations, and a `native` setting reaches the engine plugin while the game runs.

## Tools

| Tool | What it does | Page |
|---|---|---|
| Swofty UI | Browses, renders and edits the game's menu scenes (`.idf`), and exports checked scenes for an addon. Releases ship it as `bin/swofty-ui` | [Swofty UI](/reference/ui/swofty-ui) |
| `swofty` | The command line: install and enable addons, import mods, build a profile, run the game and follow its logs, restore the game folder | [Command line](/reference/cli) |
| `tools/gamedb.py` | Dumps the game database and gets or sets one value by path | [Game database tools](/reference/gamedb/tools#python-tools) |
| `tools/map_check.py` | Checks a profile built with tracks against the game it was built from, using the build's `plan.json` | [Game database tools](/reference/gamedb/tools#python-tools) |
| `tools/idf.py` | Prints a menu scene's elements and edits them from the command line | [UI scenes](/reference/ui/idf#tools) |

The Python tools are in the repository's `tools/` folder. Releases don't ship them, except Swofty UI.

## Reference

- [Addon manifest](/reference/addons/manifest): every `swofty.json` field.
- [Database operations](/reference/ops/database), [ERP operations](/reference/ops/erp), [File operations](/reference/ops/files), [UI patches](/reference/ops/ui-patches) and [Asset op files](/reference/addons/asset-api): every operation.
- [Game database](/reference/gamedb), [ERP archives](/reference/erp) and [`!pkg` resources](/reference/pkg): the game's file formats.
- [Assets from older F1 games](/reference/old-games): where F1 2016 to F1 24 keep helmets, faces, suits, engine sounds and cars, and how they're ported to F1 25.
- [Engine API](/reference/game/): the engine functions and structs an engine plugin can call and hook.
- [Development](/reference/development): build the launcher from source.
