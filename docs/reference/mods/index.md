---
title: Mods
description: Make content for the Team Simplified suite — .serps packages, game files laid out like the game folder, and community track zips — and see how the Simplified Importer turns each one into an addon.
---

# Mods

A **mod** is content for the Team Simplified suite: game files in the game folder's layout, with no `swofty.json`, that the Simplified Importer turns into an ordinary addon.

## A mod and the addon it becomes

```text
Scuderia AlphaTauri ORLEN.serps                        the mod
├── mod.serps                                          name, author, version, game version
└── F1 25/                                             the content root: paths as in the game folder
    ├── game_package/gamedatabases/gamedatabases.2025.bin
    ├── localisatioa/2025_release/language_eng.lng
    └── 2025_asset_groups/f1_2026_vehicle_package/teams/toro_rosso/wep/toro_rosso_barcelona_26.erp
```

```sh
swofty mods import "Scuderia AlphaTauri ORLEN.serps"
```

```text
$DATA/addons/mod.rm-designs.scuderia-alphatauri-orlen/    the addon it becomes
├── swofty.json             origin: { tool: teamsimplified.mods, tool_version, file, sha256 }
├── ops/database.json       operations.database: put-record and remove-record, the records the database changes
├── ops/erp.json            operations.erp: overlay, one per game archive, plus remove-resource, add-mipmaps,
│                           and merge-slices and put-resource for team logos
├── ops/text.json           operations.assets: lang.set, and lang.default for text the mod only fills in
├── ops/files.json          operations.assets: file.put, other game files
├── ops/track.json          operations.assets: track.add, for a track package
├── erp/overlay.erp         the changed and added resources of one archive (overlay-2.erp, ... for more)
├── files/                  the loose files
└── import-report.txt       what the conversion did
```

The importer compares each file with the game's clean files, keeps only the differences, and writes them as [operations](/reference/data/operations). It installs the addon and turns it on in the active profile, and prints `Scuderia AlphaTauri ORLEN is now the addon Scuderia AlphaTauri ORLEN`. From then on it is an addon like any other: turn it on and off, set its place in the load order and check its conflicts with `swofty addons` or on the **Addons** page.

To describe changes as operations you write yourself, make a [data addon](/reference/data/) instead.

## Make one

1. Pick the type:
   - a `.serps` package: `mod.serps` and an `F1 25/` folder of game files, made with Simplified Studio or by hand ([`.serps` packages](./serps));
   - game files in a zip, 7z or RAR archive, or a folder, at their game paths, with no `mod.serps` (the same page);
   - a track zip: `manifest.json`, the track's `.erp`, menu pictures and mip files, in the EGO GameDatabases Editor format ([Track packages](./tracks)).
2. Import it into a test profile and build: `swofty mods import <file>`, then `swofty build`. Read every line of output and the addon's `import-report.txt` ([Import and build](./serps#_3-import-and-build)).
3. Test it in game with only your mod's addon turned on, then with the mods players combine it with.
4. Publish the file. Players drop it on the launcher's **Simplified Importer** page ([Simplified Importer](/using/mods)).

A single `.erp` file isn't a mod on its own: put it at its game path, such as `F1 25/2025_asset_groups/ui_package/<name>.erp`, and zip the folder.

## The map

- **[`.serps` packages](./serps)**: the package layout, `mod.serps`, and what the importer reads, compares and writes.
  - The content root and the files it classes.
  - The mod's id, name and version.
  - Database records, archive resources, text and loose files.
  - Parts and variants.
- **[Track packages](./tracks)**: the EGO track zip, its `manifest.json`, the track's name and checking a build.
- **[How tracks are installed](/reference/installing-tracks)**: what activating a track changes in the archives, database, menus and ERS.

## How mods load

Since importer 0.7.0, each mod converts once into a standalone addon, `mod.<mod id>`, in `$DATA/addons/`:

```text
import    unpack the file, find the content root, store it in the library
          $DATA/addon-data/teamsimplified.mods/library/<mod id>/
compare   each file against the clean game files of the installed build
convert   write swofty.json and the ops/ files into a new addon, mod.<mod id>
install   install the addon, turn it on in the active profile, then delete the library copy
build     the addon's ops apply at its place in the load order, like any addon's
```

- The library copy is deleted once a mod has converted. The installed addon is the only copy, so importing the mod again needs its file.
- Importing the same mod again replaces its addon, which keeps its place and switch in every profile.
- A mod converts by comparing it with the game's own files. Without them, an import fails and leaves nothing in the library; a track package converts without them:

```text
Scuderia AlphaTauri ORLEN was not imported. Mods turn into addons once Swofty Launcher finds F1 25.
Scuderia AlphaTauri ORLEN was not imported. Update Swofty Launcher to 1.3.0 or later to turn mods into addons.
Scuderia AlphaTauri ORLEN was not converted: <cause>
```

- Mods in a library from before 1.3.0 convert at startup, and **Convert now** on the importer's page converts the rest. Until then, the importer's own build hook loads them, in the profile's mod order. It also loads the merged team logo archive and Team Simplified's SERPs Base Files for the addons that need them.
- `swofty mods list` lists the mods still in the library, so a converted mod no longer shows there; `swofty addons list` shows its addon. `swofty mods enable`, `disable` and `move` set mods that haven't converted; for an addon id, `swofty mods enable` runs `swofty addons enable`.

The `.serps` format and the importer come from Simplified Suite by Team Simplified, [github.com/TeamSimplified/simplified-suite](https://github.com/TeamSimplified/simplified-suite).
