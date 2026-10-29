# Design notes

If you're contributing to Swofty Launcher for F1 25, start here for the intended design and the work still needed. To set it up, follow [Installation](/guide/installation).

## Goals

- Manage addons, mods and community tracks in one desktop app.
- Keep modded play offline only.
- Switch profiles and restore the original game in one click.
- Change addon options through ADDON SETTINGS under Career.
- Support Windows and Linux through Steam and Proton.

## Main parts

| Path | Role |
|---|---|
| `apps/launcher` | Desktop interface, built with Rust and GPUI |
| `apps/cli` | Terminal commands and Linux Steam launch wrapper |
| `packages/core` | Addons, tracks, profiles, backups and launch setup |
| `packages/formats` | Game file readers and writers |
| `packages/sdk` | Plugin API |
| `native/` | In-game loader and menu entry |
| `addons/swoftymod` | Built-in menu changes |

Keep the interface separate from the shared core. When you're working on the Rust format code, check it with the Python tools in `tools/`.

## Addons and tracks

An addon's [`swofty.json` manifest](/reference/addons/manifest) tells you what it contains: database changes, assets, UI patches, loose files and plugins.

Tracks already have a community package format. Import the package unchanged, then use its track code in the profile.

Apply changes in load order, and report any missing requirements or conflicts before launch.

You'll find the plans for mods, the Launcher API, the Asset API and the built-in Team Simplified addon in [Addons and mods](/design/addons-and-mods).

The proposed 1.6 standard library, where addons register records, layers, events and hooks in code and the core applies them live in the game, is at [Standard library 1.6](/std/), with its product choices on [Decisions](/design/std-2/decisions).

## Profiles and saves

Your profile holds your addons, tracks and options. The design also calls for separate saves under `$DATA/saves/<profile>/` for each profile, keeping them away from vanilla saves and Steam Cloud.

Save isolation isn't implemented yet. The built-in `vanilla` profile has no addons, and you can't edit it.

## Launch and restore

When you launch a modded session, the intended flow is:

1. Check the game and profile.
2. Prepare the changed files and record backups.
3. Start an offline modded session.
4. Show logs while the game runs.
5. Restore the original files when it exits.

Record changes before you apply them: that record lets you recover an interrupted session. Never restore an old build's files over a new one. See [Backups and recovery](/reference/restore).

On Linux, you launch through a Steam launch option; on Windows, you use a separate modded launch. Normal Steam launches remain vanilla when the launcher's launch option is absent and the game has been restored.

## In-game settings

SwoftyMod adds ADDON SETTINGS below Career, an F9/R3 prompt and a launcher panel.

Open ADDON SETTINGS and you'll find a tile and a page for each enabled addon and track. These are generated from the player's game files and the profile when you build the profile. Change a database setting and you'll need to restart; plugin settings apply immediately.

## Current status

The repository is pre-release. Here's what works and what still needs to be finished before the full player workflow is ready:

- You can install addons, use profiles, import mods and tracks, and read command-line logs.
- You can build database operations, `!pkg` edits, resource copies and loose files. Other addon ERP and UI build steps are skipped.
- If the in-game loader's core has the feature, asset groups, changed ERP resources, database edits and language strings go into `swofty/assets.json` and apply in memory. A Grand Prix has loaded a track through asset groups and database edits. The file overlay is experimental.
- Enabled tracks are activated: database rows, track archive, menu art, minimap and ERS. `swofty apply` installs them through the journal. Neither has been tested in game yet.
- Backup and restore code has tests for interrupted changes and game updates.
- The native menu entry is tested with a test executable under Wine. It currently opens the game's Settings page.
- Press Play in the launcher to build the profile and install it with the in-game loader. When the game exits, the launcher rolls both back. This flow hasn't been tested in game yet.
- The built-in Team Simplified addon imports `.serps` mods into a library. You manage each profile's order and switches in its **Mods** section and `swofty mods`. At build time, it merges each enabled mod's changes against clean game files; the section shows you each mod's changes and conflicts.
- You can change addon settings in the launcher and on the command line. The in-game page is generated, but saving changes there still waits on the native bridge's in-game test.
- Save isolation, offline enforcement and plugin approval are unfinished.

## Contributor checks

Keep changes compatible with the game build they target. Native hooks must use verified signatures and stop if a match is missing or ambiguous.

Follow `re/PROTOCOL.md` for game research. See [Development](/reference/development) for build and test commands.
