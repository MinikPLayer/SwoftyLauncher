---
title: Track packages
description: A community track package in the EGO GameDatabases Editor format, its manifest.json annotated, how the Simplified Importer turns it into a track addon, and how to check the build.
---

# Track packages

A **track package** is a community track in the zip format made for the EGO GameDatabases Editor. The Simplified Importer turns it into a track addon.

## A track zip in full

```text
testring.zip
├── manifest.json            the track code, name and activation, at the root of the zip
├── testring.erp             the track archive
├── mipmaps/                 optional textures
└── ui/
    ├── images.json          track artwork
    └── minimap.json         minimap placement
```

```jsonc [manifest.json]
{
  "track_code": "testring",            // names the track everywhere: letters, digits, _ and -
  "track_name": "Test Ring",           // optional display fields
  "version": "0.1.0",
  "author_name": "tester",
  "erp": "testring.erp",               // the track archive, relative to the package
  "activation": {
    "copy_from_track": "melbourne",    // the base track whose TrackData rows are cloned in every dataset
    "copy_slot": "melbourne",          // the track whose Track row and resource row are cloned; defaults to copy_from_track
    "overrides": [                     // set path to value on the cloned TrackData rows; datasets limits which
      { "datasets": ["f1_2025"], "path": "TyreData_0.compound_harder", "value": "C3" }
    ],
    "track_overrides": [               // set path to value on the cloned Track row
      { "path": "allow_night_start", "value": "False" }
    ]
  },
  "ers": { "HotLapDeployFactor": 0.7 }, // per-track ERS values written into every 2026 car handling file
  "copy_directories": [                // source folders in the package and their destination in the game
    { "source": "mipmaps/", "destination": "asset_groups/environment_package/tracks/testring/mipmaps/" }
  ],
  "ui": { "schema_version": 1, "images_file": "ui/images.json", "minimap_file": "ui/minimap.json" }
}
```

Import it and build:

```sh
swofty mods import testring.zip
swofty build
```

```text
Test Ring is now the addon Test Ring
testring: track data: testring cloned from melbourne in 19 dataset(s): f1, f2, classic, ...
```

The importer turns the zip into the addon `mod.testring`, installs it and turns it on in the active profile. You can also drop the zip on the launcher ([Tracks](/using/mods#tracks)). The addon holds the unpacked package and one op:

```text
$DATA/addons/mod.testring/
├── swofty.json          category Tracks, kind database and erp
├── track/               the unpacked package: manifest.json, testring.erp, mipmaps/, ui/
└── ops/track.json       { "op": "track.add", "source": "track" }
```

`swofty build` activates every `track.add` before the other operations run: activation clones a base track's database rows, installs your track's archive and adds its menu entries ([How tracks are installed](/reference/installing-tracks)).

## How the importer finds a track

- A zip is a track package when it holds `manifest.json` at its root, or holds exactly one zip and no `swofty.json`, `mod.serps` or `F1 25/` folder. A download zip that wraps one package is unwrapped.
- The `manifest.json` needs a `track_code` of letters, digits, `_` and `-`. Without one, the zip imports as an ordinary mod ([`.serps` packages](./serps)).
- The track code becomes the mod's id. When another mod in the library already has that id, the id is `<code>-track`, then `<code>-track-2`. Importing the same track code again reuses its id.
- A track package converts without the game's files.

## The manifest

- `activation` needs `copy_from_track`, `copy_slot` or both, each naming a stock track. Without `copy_from_track`, `copy_slot` is the base track.
- An override `path` is relative to the row. A segment of it can name a sub-row by number (`TyreData_0`), every sub-row (`TyreData_*`) or the one sub-row whose field matches (`Name[field=value]`, as in [Selecting rows](/reference/ops/database#selecting-rows)). A selector that matches more than one sub-row fails the build.
- An override `value` is a string, such as `"1.55"` or `"64,64,64"`. A JSON number is also accepted and written as its text: `1.55` sets `"1.55"`.
- Keep every path in the package inside the package.

A build leaves out a package with neither `copy_from_track` nor `copy_slot`, or with a `manifest.json` that doesn't read, and builds the rest of the profile. It warns:

```text
Portimao (mod.portimao): track Portimao is left out; its manifest.json doesn't say which track it replaces or copies. Import a fixed version, or ask its author for one
```

A race weekend set to a track the build leaves out races at its stock track again, with a warning. Importing or installing such a package shows the same warning, and the **Addons** page marks its addon **Track left out**.

For every table, archive and menu change made during activation, see [How tracks are installed](/reference/installing-tracks).

## The track's name

Menus read `TrackNameData_0.title` and the other `TrackNameData_0` fields on the cloned `TrackData` rows, so they show the base track's names until you override them. A track package carries no text, so point each field at a stock language key with an `activation.overrides` entry:

```json
{ "path": "TrackNameData_0.title", "value": "lng_track_jpn_gp_title_short" }
```

To give the track a name of its own, with new keys and strings, ship it in a data addon with `track.add`, `lang.set` and `db.set` ([A track with its own name](/reference/data/track-names)).

## Using the package elsewhere

The package is the EGO GameDatabases Editor format, so the same zip also installs with the editor. `copy_slot` needs editor 1.6.1 or later. Neither way replaces a stock track: `copy_slot` adds a new track id, and a package with the track code `bahrain24` fills the game's empty `bahrain24` slot.

The editor adds no text, so a new language key such as `lng_my_track_title` shows no name there. For a package that shows a name in both, set `TrackNameData_0.title` to a stock key, such as the base track's own.

## Racing line and other binary XML

The track archive's AI racing line, AI brake settings and splines hold binary XML with string stats beside it ([Binary XML with stats](/reference/ops/erp#binary-xml-with-stats)). When those stats don't match the data, the build writes new stats into the track archive it installs and prints:

```text
testring: fixed the size headers of 1 AI racing line(s) in Test Ring so F1 25 reads them safely
```

When the data starts as binary XML but doesn't parse, the build stops, and Play doesn't start:

```text
Test Ring would crash F1 25: its testring_ai_track.aispline doesn't match the size header the game reads with it, and the launcher can't read the file to fix the header. Turn Test Ring off in this profile, and set any race weekend that uses it back to another track.
```

Export the archive from a tool that writes each resource's stats from its own data, so the package works with other tools too.

F1 25 crashes when an AI car drives a route of the racing line, often the pit lane, on which the passing space never changes from one gate to the next. The build moves one white line on such a route and prints:

```text
testring: fixed the AI pit lane of Test Ring so F1 25 doesn't crash when an AI car drives it
```

A pit lane whose white lines vary along its length, as at the stock tracks, needs no change.

## Check the build

```sh
python3 tools/map_check.py <overlay folder> --game <game folder> --maps "$DATA/addons"
```

Use the folder `swofty build` printed under `$DATA/cache/overlay/<profile>/<buildhash>/` for `<overlay folder>`. `map_check.py` reads that folder's `plan.json` and the track packages under `--maps`: each folder's `track/manifest.json` or `manifest.json`. `--maps` defaults to the Team Simplified library, which holds no track once it has converted, so point it at `$DATA/addons`, where each track addon keeps its package in `track/`. It checks that:

- Every key the game reads on a base row also exists on your track's new row.
- Each rewritten archive and database re-serialises correctly.
- Every ERS setting is present.

See [Checking a build](/reference/installing-tracks#checking-a-build).
