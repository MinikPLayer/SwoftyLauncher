---
title: .serps packages
description: Lay out a .serps package or a zip of game files, write mod.serps, import it, and see what the importer reads, compares and writes into the addon it makes.
---

# `.serps` packages

A `.serps` package is the Team Simplified suite's mod format: a zip with whole game files under a content root and a `mod.serps` beside it.

## A package in full

```text
Scuderia AlphaTauri ORLEN.serps
├── mod.serps
└── F1 25/
    ├── game_package/gamedatabases/gamedatabases.2025.bin
    ├── localisatioa/2025_release/language_eng.lng
    ├── 2025_asset_groups/f1_2026_vehicle_package/teams/toro_rosso/wep/toro_rosso_barcelona_26.erp
    └── shader_package_2025/object_opaque.nefx2.sm51.erp
```

```jsonc [mod.serps]
{
  "mod_name": "Scuderia AlphaTauri ORLEN",   // or "name"; mod_name wins
  "author": "RM Designs",
  "version": "1.0",
  "game": "F1® 25",
  "game_version": "1.0.141.7865",           // the game version it was made for; removals need it
  "studio_version": "1.4.2"                 // the Simplified Studio version that made it
}
```

```sh
swofty mods import "Scuderia AlphaTauri ORLEN.serps"
swofty build
```

```text
Scuderia AlphaTauri ORLEN is now the addon Scuderia AlphaTauri ORLEN
```

The importer compares each file with the clean game files, makes an addon of the differences, `mod.rm-designs.scuderia-alphatauri-orlen`, installs it and turns it on in the active profile ([How mods load](./#how-mods-load)). A zip, 7z or RAR archive, or a folder, laid out the same way imports too, with or without `mod.serps`.

## 1. Lay out the package

Put your files under an `F1 25/` folder, at the paths they have in the game folder.

```text
F1 25 with V10 Engines - By Jor (V10.3)/F1 25/audio/...   content root: the F1 25 folder that holds game folders
Wrapper/F1 25 (my mod)/game_package/...                   content root: a folder whose name starts with F1 25
Mod/shader_package_2025/...                               no F1 25 folder: the first folder that holds a game folder
```

- The importer looks at most three folders down. A folder whose name starts with `F1 25` and that holds game folders wins over one that only starts with the game's name. Next comes the first folder named like the game. Without one, the content root is the first folder, from the top down, that holds a game folder.
- A game folder is `asset_groups`, `actionmaps`, `audio`, `videos`, `game_package`, `character_package`, `localisation` or `localisatioa`, or a folder whose name ends in `_package`, `_package_shared` or `_asset_groups`, or starts with `shader_package_`.
- `localisatioa/` and `localisation/` both name the stock language file with the same file name.

Ship only the files your mod changes. The importer keeps every difference, so a whole `gamedatabases.2025.bin` from an older game build changes every record the game has updated since.

## 2. Write `mod.serps`

`mod.serps` sits at the package root. It's JSON, and every field is optional. The reader accepts a UTF-8 byte order mark, comments, trailing commas, and camel- or Pascal-case keys (`modName`, `Mod_Name`, `GameVersion`).

Without `mod.serps`, the importer takes the name from the file or folder name. A trailing version such as `v1.0.6` or `(V10.3)` becomes the version, and a trailing ` - By Jor` becomes the author:

```text
Pirelli Tyres Collection v1.0.6.zip                 name "Pirelli Tyres Collection", version v1.0.6
F1 25 with V10 Engines - By Jor (V10.3).zip         name "F1 25 with V10 Engines", author Jor, version V10.3
```

## 3. Import and build

```sh
swofty mods import "Scuderia AlphaTauri ORLEN.serps"
swofty addons list
swofty build
```

- The mod's id is `<author>.<name>` from `mod.serps`: each lowercased, each run of other characters turned into `-`. `RM Designs` and `Scuderia AlphaTauri ORLEN` give `rm-designs.scuderia-alphatauri-orlen`. Without either, the id comes from the file name: `My Mod (v2)` gives `my-mod-v2`.
- The addon's id is `mod.<mod id>`, at most 64 characters; a longer one keeps its first words and ends in a short hash.
- Importing a mod again replaces its addon, and every profile keeps its place and switch.
- `swofty build` prints the addon's ops among the rest. Read it for conflicts with other addons, and for ops waiting on the in-game loader. The **Addons** page shows the same counts on the addon's row ([Check an addon](/using/addons#check-an-addon)).
- The addon's `import-report.txt` lists what the conversion did, and every conversion writes a log to `$DATA/addon-data/teamsimplified.mods/logs/`.

## What the importer accepts

```text
a .serps file, 7z or RAR archive   always, by its extension
a zip                              it holds mod.serps at its root or one folder down, a folder whose name
                                   starts with F1 25, or a game folder at most three folders down
a folder                           as for a zip
a track zip                        manifest.json with a track_code at its root, or one such zip inside (Track packages)
```

Encrypted archives aren't supported. Entries with absolute paths or `..` are left out and counted: `<file>: 2 unsafe paths left out`.

## What the importer reads

Each file under the content root gets a class:

```text
gamedatabases.2025.bin, wherever it sits           database; it must parse as F1 25's database, or the import fails
language_<code>.lng, wherever it sits              text
a .erp the game ships                              archive
any other file the game ships                      replacement
a file the game doesn't ship                       addition
.exe, .dll, .asi; files whose names start with     skipped, and named in the result:
eaanticheat, eajavelin or preloader_, except       "programs, EA AntiCheat and EA installer files are never copied"
EAAntiCheat.splash.png
```

## What the importer applies

The importer compares each file with the clean files of the installed game build, never with the game folder as modded, and keeps the differences:

| Class | Compared by | Changed | Added | Removed | Written as |
|---|---|---|---|---|---|
| Database | Record key | Same key, other tag or bytes | Key not in stock | Stock key missing from the package | `db.put-record`, `db.remove-record` |
| Archive | Resource name | Type, fragment layout, sizes or index digest differ | Name not in stock | Stock name missing from the package | `erp.overlay`, `erp.remove-resource` |
| Text | Language and key | Value differs | Key not in stock | Never | `lang.set`, `lang.default` |
| Replacement, Addition | Whole file | Whole file | Whole file | Never | `file.put`; a `.mipmaps` file is an `erp.add-mipmaps` |

`ops/erp.json` is listed under `operations.erp`, so its ops take no prefix:

```jsonc [ops/erp.json]
{
  "schema_version": 1,
  "ops": [
    // every changed and added resource of one game archive, in one ready archive
    { "op": "overlay", "erps": ["2025_asset_groups/f1_2026_vehicle_package/teams/toro_rosso/wep/toro_rosso_barcelona_26.erp"],
      "source": "erp/overlay.erp" },
    // team logo arrays from the mod's ui_texture shader archives, merged slice by slice
    { "op": "merge-slices", "erp": "2025_asset_groups/vfx_package/common.erp",
      "resource": "eaid://ui_package/art/texture_arrays/overrides.uidb/overrides.team_logos.f1_2026.texarray.image",
      "source": "logos/ui_textures.erp" }
  ]
}
```

- Text counts like any other change: every changed string and every key the stock files don't have becomes a `lang.set`, and new keys are added to the game's text. A language the package lacks takes the changes of its `language_eng.lng`, or of its first language file by name.
- Text a mod only fills in becomes a `lang.default`, which never undoes another addon's rename. That is a translation replaced with the game's English text, and a key the mod's database points a record at whose text is the game's own text for the key the record pointed at before. When a team rename package fills in the game's own text for the teams it doesn't rename, those defaults don't undo another mod's rename.
- Removals apply only when `game_version` matches the installed game. Otherwise they're dropped, and `import-report.txt` says how many and which version the package was made for: `warning: 12 removals left out: the mod was made for 1.0.138.7700, not this game version (1.0.141.7865)`.
- The launcher skips every database removal of a stock record, whatever the game version, and of a record a track adds, and warns with the count per addon. See [Removing stock records](/reference/ops/database#removing-stock-records).
- A database change touches only the records the package changed, and archives merge one resource at a time.
- Every resource a mod adds or changes goes into an overlay of the `2025_asset_groups/` archive it belongs to. A changed resource may change size there.
- Resources a mod carries in a `shader_package_2025/` archive stay in that archive, and a shader archive removes nothing. The launcher merges them into the game's copy and writes it into the game files, and each GPU build of the archive gets its own copy.
- Team logo texture arrays in `ui_texture*` archives merge slice by slice across every enabled addon.
- A special livery row the package adds is reported as owned, so the livery shows in the picker.
- A write that repeats a value an earlier op of the mod already put in place is left out: `left out 12 op(s) that repeat the value an earlier op of the mod writes`.

Without the in-game loader, `lang.set` ops wait and the text doesn't change.

## Parts

A mod can come in parts: folders that each hold game files of their own, such as `2026 Broadcast graphics/shader_package_2025/` beside `Realistic Tyre colours/shader_package_2025/`. A mod with two or more parts imports as one addon per part, `mod.<mod id>.<part>`, each with its own switch, place in the load order and conflicts. A part is named by the folders that tell it apart: `Realistic Tyre colours — <mod name>`.

When the parts are alternatives that change the same things, such as tyre colour sets for different seasons, they import as variants: every variant is installed and only one is on.

## Before you share it

- Set `mod_name`, `author`, `version` and `game_version` in `mod.serps`.
- Include a README that credits each author whose files you ship, and get their permission to redistribute those files.
- Don't ship executables or DLLs: the importer skips them.

The `.serps` reader is ported from the Simplified Launcher and is based on Simplified Suite by Team Simplified, [github.com/TeamSimplified/simplified-suite](https://github.com/TeamSimplified/simplified-suite).
