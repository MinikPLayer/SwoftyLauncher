---
title: Simplified Importer
description: Turn mods and community tracks into Swofty addons with the Simplified Importer, made with Team Simplified.
---

# Simplified Importer

The **Simplified Importer** turns a mod into a Swofty addon. It's made with Team Simplified and comes with the launcher as their addon, **Simplified Importer** (id `teamsimplified.mods`). After an import, the mod is an ordinary addon on the [Addons](/using/addons) page: you turn it on per profile, set its place in the load order and check its conflicts there.

Anything that already has a `swofty.json` is an addon; install it on the [Addons](/using/addons) page instead.

## Import a mod or track

1. Open **Simplified Importer** in the sidebar.
2. Drop the mod anywhere on the launcher window, or select **Choose a mod…**. You can import:
   - a zip, 7z or RAR archive, or a folder, with files laid out like the game folder;
   - a `.serps` package made with Simplified Studio;
   - a community track zip, or a download zip that holds one;
   - a modded `gamedatabases.2025.bin`.
3. The Simplified Importer compares the mod with the game's clean files, makes an addon of what it changes, installs it and turns it on in the active profile. The launcher then opens **Addons** and highlights the new addon.

The game folders can sit at the top of the mod, up to three folders down, or in a folder named like `F1 25`. The Simplified Importer leaves out `.exe`, `.dll` and `.asi` files and names them in the result. It can't read encrypted archives.

Importing the same mod again replaces its addon and keeps its place and switch in every profile.

## What a mod becomes

| In the mod | In the addon |
|---|---|
| A modded game database | The records it changes, adds or removes |
| Modded language files | The text it changes; text a mod only fills in can't undo another addon's renamed text |
| Archive resources | The resources it changes, adds or removes, in one ready archive per game archive |
| Team logos | Only the logos it changes, merged into the game's logo arrays |
| Other game files | The files it replaces or adds |
| A community track | The track, as a track addon |

Each addon gets a category from what it changes, such as Tracks, Cars or Helmets, and the mod's name, author and version. A mod that loads from the game's shader packages, such as a Simplified Studio or My Team mod, also gets the type Simplified.

A mod made of several parts imports as one addon per part. When the parts are alternatives that change the same things, such as tyre colour sets for different seasons, they import as variants: every variant is installed and only one is on. Turn on another on the **Addons** page.

The page shows what each import became. **Details** lists every change.

## Face mods

A mod that adds a driver head the game doesn't have, such as a face mod with a `heads/<name>.erp` archive, gets a driver head row when the profile builds. [Driver Manager](/using/driver-manager) and the [DB Editor](/using/db-editor) offer the head by its name and the addon's name, such as **Rio Haryanto (Rio_Haryanto_facemod)**. A driver whose head comes from an addon that's off keeps the game's head.

## Tracks

Drop a track zip on the launcher without unpacking it. It becomes a track addon in the **Tracks** category; turn it on to add the track to Grand Prix track selection. You can turn on any number of tracks in one profile.

A track whose package doesn't say which track it replaces or copies can't be used. The launcher warns when you import it, marks its addon **Track left out** on the Addons page, and leaves it out when you play; the rest of the profile still loads. Import a fixed version, or ask the track's author for one.

[How tracks are installed](/reference/installing-tracks) covers what turning a track on changes.

When a track, or an addon that changes a track's AI racing line, ships files with size headers that don't match them, the launcher fixes the headers as it builds the profile. If it can't read a file to fix it, **Play** stops with a message that names the track or addon, such as `Test Ring would crash F1 25: …`. Turn it off in the profile to play. If a career's [Race weekends](/using/db-editor) entry uses that track, set the weekend back to another track first.

## Mods from an earlier version

Before 1.3.0, mods lived in a library on a **Mods** page. The first time 1.3.0 starts, the Simplified Importer converts every mod in that library into an addon, in each profile's order and with the same switches. A dialog lists each mod with a tick when it converts or a cross and the reason when it can't. A large library may convert over more than one start; **Convert now** on the Simplified Importer page converts the rest.

The library stays in `$DATA/addon-data/teamsimplified.mods/library/` as the source for **Re-import**.

## When something goes wrong

Every conversion writes a log to `$DATA/addon-data/teamsimplified.mods/logs/`. **Send Logs to Developers** includes the newest ones.

## From a terminal

| Command | Does |
|---|---|
| `swofty mods import <path>…` | Import zips, 7z and RAR archives, `.serps` packages, folders, track zips and modded databases |
| `swofty mods list` | List the library and the addon each mod became |

Turn the addons on or off and remove them with `swofty addons`; see [Addons](/using/addons#from-a-terminal).

## Next

- [Addons](/using/addons): turn your imported mods on and set their order.
- [Profiles](/using/profiles): keep different sets of addons apart.
- To make a mod or track, see [Mods](/reference/mods/) in the developer docs.
