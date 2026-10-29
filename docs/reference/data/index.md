---
title: Data addons
description: A data addon changes F1 25's content with operations only. One complete data addon with a database edit, an ERP edit, a text change and a file put, then a map of every operation family.
---

# Data addons

A **data addon** changes F1 25's content with **operations** only: no DLL and no launcher plugin. An **operation** (op) is one data change written as JSON: set a database field, replace a resource in an archive, add a string, copy a loose file. Each time the launcher builds a profile, it applies the ops of every enabled addon to a copy of the game's files. The game folder keeps its stock files.

## A data addon in full

This addon makes a stop-go penalty last 10 seconds, swaps the loading screen background, renames Melbourne in the 2026 menus and adds one loose file:

```text
yourname.first-data-addon/
├── swofty.json
├── db/
│   └── changes.json
├── erp/
│   ├── texture.json
│   └── background.tif
├── assets/
│   └── text.json
└── files/
    └── sponsor.dds
```

```jsonc [swofty.json]
{
  "schema_version": 1,
  "id": "yourname.first-data-addon",
  "name": "First data addon",
  "version": "1.0.0",
  "author": "Your Name",
  "description": "A 10-second stop-go, a new loading screen and a renamed track.",
  "kind": ["database", "erp", "text", "files"],   // the categories the Addons page shows
  "compat": { "game": "f1_25", "game_builds": [">=25251167"] },
  "operations": {
    "database": ["db/changes.json"],   // database ops, written without the db. prefix
    "erp": ["erp/texture.json"],       // ERP ops, written without the erp. prefix
    "assets": ["assets/text.json"]     // any op, each with its prefix: lang.set, file.put, db.set, ...
  }
  // no "native" and no "launcher": those carry code and make it a code addon
}
```

The database edit:

```json [db/changes.json]
{
  "schema_version": 1,
  "ops": [
    { "op": "set", "select": "rules.stop_go_penalty", "field": "penalty_duration", "value": "10" }
  ]
}
```

The ERP edit. `source` is a file in the addon that holds the raw bytes of fragment 0:

```json [erp/texture.json]
{
  "schema_version": 1,
  "ops": [
    {
      "op": "replace-resource",
      "erp": "2025_asset_groups/ui_package/screens/fe_loading.erp",
      "resource": "eaid://ui_package/art/textures/loading/background.tif",
      "fragment": 0,
      "source": "erp/background.tif"
    }
  ]
}
```

The text change and the file put, in one `assets` file:

```jsonc [assets/text.json]
{
  "schema_version": 1,
  "ops": [
    // every language (*): the string behind a new key
    { "op": "lang.set", "language": "*", "key": "lng_my_track_title", "value": "Test Ring" },
    // point Melbourne's 2026 menu name at that key
    { "op": "db.set",
      "select": "track_data.game_setup.TrackDataSet[_metadata_=f1_2026].TrackData[_metadata_=melbourne]",
      "field": "TrackNameData_0.title", "value": "lng_my_track_title" },
    // a loose file: source in the addon, target in the game folder
    { "op": "file.put", "source": "files/sponsor.dds", "target": "livery_package_shared/sponsor.dds" },
    // printed at build
    { "op": "warn", "message": "Melbourne shows as Test Ring in 2026" }
  ]
}
```

Install the folder, turn it on in a modded profile and build. The `vanilla` profile takes no addons:

```sh
swofty addons install ./yourname.first-data-addon
swofty addons enable yourname.first-data-addon
swofty build
```

The build prints what each addon writes, what reaches the game at run time, and the game files it replaces or adds:

```text
profile modded: swofty.core -> yourname.first-data-addon
files: 0 game file(s) replaced, 1 added
runtime: swofty/assets.json with 0 asset group(s), 1 resource(s), 2 database edit(s), 1 language table(s), 0 overlay file(s)
First data addon: 2 database values, 1 archive resource, 1 file, 1 text string
warning: yourname.first-data-addon: Melbourne shows as Test Ring in 2026
overlay 9c41e0b2…: $DATA/cache/overlay/modded/9c41e0b2…
  place swofty/assets.json
  …
  add 1 file(s) under livery_package_shared
```

The counts in the `runtime:` line cover every enabled addon, so other addons add to them. `swofty build` writes the result to `$DATA/cache/overlay/<profile>/<buildhash>/` and leaves the game folder alone. To play it, press **Play** in the launcher.

## Make one

1. Make a folder with a `swofty.json` at its root, as above. The [addon manifest](/reference/addons/manifest) lists every field.
2. Find the operation for each change in [the map](#the-map).
3. Write the op files and list each one under its field in `operations` ([Operations](./operations)).
4. Install the folder, turn it on in a modded profile, run `swofty build` and read every line of output ([Check what a build did](./operations#check-what-a-build-did)).
5. Test it in game with only your addon turned on ([Test in game](/reference/addons/publishing#test-in-game)).
6. Package it as a `.swoftyaddon` and publish it ([Package it](/reference/addons/publishing#package-it)). Players install it on the **Addons** page.

`kind` lists the categories the **Addons** page shows for the addon: `database`, `erp`, `text`, `ui`, `files`, `native` and `simplified`. A data addon uses the first five.

A mod made for the Team Simplified suite, such as a `.serps` package or a track zip, isn't written as ops by hand: the Simplified Importer turns it into an addon ([Mods](/reference/mods/)).

## The map

- **[Operations](./operations)**: op files, the `operations` fields, prefixes, sources, settings, the build and runtime routes, order and conflicts, and reading a build.
- **[Database operations](/reference/ops/database)**: records in `gamedatabases.2025.bin`. Walkthrough: [Game database](./database).
  - `set`: change a field, or copy one with `value_from`.
  - `add-row`: copy a row with every key and change some fields.
  - `list-append`, `list-remove`: edit a comma-separated list field.
  - `put-record`, `remove-record`: write or remove one raw record.
  - `remove-row`: defined, not built.
  - [Selecting rows](/reference/ops/database#selecting-rows), [key names](/reference/ops/database#key-names), [the side-data check](/reference/ops/database#the-side-data-check), [rows SwoftyMod adds](/reference/ops/database#rows-swoftymod-adds).
- **[ERP operations](/reference/ops/erp)**: resources in `.erp` archives. Walkthrough: [Textures and models](./textures).
  - `replace-resource`, `add-resource`: raw fragment files.
  - `copy-resource`, `put-resource`, `overlay`: resources from other archives.
  - `remove-resource`: take a resource out.
  - `merge-slices`: the changed slices of a texture array.
  - `add-mipmaps`: loose mip files.
  - `patch-pkg`: entries inside a `!pkg` resource.
  - `set-material`: constants of a `.material`.
- **[In-game text](./text)**: `lang.set`, `lang.table` and `lang.default`.
- **[File operations](/reference/ops/files)**: `operations.files` and `file.put`, loose game files.
- **Asset groups**: `group.register`, an asset group in the engine's asset manifest ([Asset API](/reference/addons/asset-api#op-files)).
- **Tracks**: `track.add`, a whole community track ([A track with its own name](./track-names), [How tracks are installed](/reference/installing-tracks)).
- **`item.show`**: a read-only tile in the in-game **ADDON SETTINGS** screen ([Asset API](/reference/addons/asset-api#op-files)).
- **`warn`**: a message the build prints.
- **[UI patches](/reference/ops/ui-patches)**: `operations.ui`, planned and not built.
- **Worked examples**:
  - [Game database](./database): find a key, change a value, add a row, rename a track.
  - [In-game text](./text): stock tags and your own strings.
  - [Textures and models](./textures): inspect archives, replace and copy resources, mip files, `!pkg` edits.
  - [Adding a livery](./livery): a complete data addon that adds a special livery to the picker.
  - [A track with its own name](./track-names): ship a community track and name it.
- **Elsewhere**: `operations.screens` adds in-game screens ([In-game UI](/reference/addons/in-game-ui#add-a-screen)), and a menu layout is a `replace-resource` on a scene ([Editing menus](/reference/ui/editing-menus)).
