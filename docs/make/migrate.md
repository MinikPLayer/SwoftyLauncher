---
title: Moving a 1.5 addon to the registry
description: Replace database operations and file.put with records, pin stat ids, and keep launchers before 1.6 building the old files.
---

# Moving a 1.5 addon to the registry

A 1.5 addon builds its cars, packages and seasons with operation files: `add-row` and `set` in `db/*.json`, `file.put` and `group.register` in `assets/*.json`. A 1.6 addon describes the same things as records. The core applies the records when the game boots. This guide moves Classic Seasons 2017 over, and shows the JSON-only path that needs no native code.

## What changes

The addon keeps working on launcher 1.5. Its old operation files stay on disk, `registry.json` names the ones the records replace, and each launcher builds what it understands:

```text
launcher 1.5   reads swofty.json                      builds db/records.json and assets/liveries.json
launcher 1.6   reads swofty.json and registry.json    leaves those two out, the core applies registry/content.json
```

```text
swofty.classic-seasons-2017/
  swofty.json
  registry.json
  registry/
    content.json
  db/
    season.json
    records.json
  assets/
    cars.json
    liveries.json
    sounds.json
  lang/
    season.json
```

## 1. Split the operation files

`db/season.json` keeps the operations the registry has no record for. `db/records.json` holds the operations that one record covers: the vehicle packages, vehicles, entrant lists, scenario, engine sound settings. Both stay in `operations.database`, in this order.

```json
// swofty.json
{
  "id": "swofty.classic-seasons-2017",
  "version": "0.6.4",
  "compat": { "game": "f1_25", "launcher": ">=1.5.1" },
  "requires": [
    { "id": "swofty.core", "version": ">=0.2.13" },
    { "id": "swofty.classic-seasons", "version": ">=0.6.1" }
  ],
  "operations": {
    "database": ["db/season.json", "db/records.json"],
    "assets": [
      "lang/season.json",
      "assets/cars.json",
      "assets/liveries.json",
      "assets/sounds.json"
    ]
  }
}
```

The archives a record's `livery` points at move out of `assets/cars.json` into `assets/liveries.json`. Its `file.put` and `group.register` operations become the record's `archive`.

## 2. Write the records

`registry/content.json` is a data file: one object per registry, one entry per record id.

```json
// registry/content.json
{
  "vehicle.package": {
    "cs_2017_redbull": {
      "donor": "game/f1_2025",
      "resource_path": "cs_2017_vehicle_package/teams/redbull/wep/redbull",
      "driver_animation_set": "cs_2017",
      "halo": false,
      "setups_from": "game/f1_2025",
      "blob_shadow_decal": "eaid://cs_2017_vehicle_package/shared/ao/materials/blobshadowdecal.material",
      "stat_id": 72
    }
  },
  "vehicle": {
    "redbull": {
      "label": "cs17_redbull",
      "donor": "game/redbull_2025",
      "performance_tier": 0,
      "tyre_wear_scale": 1.02,
      "stat_id": 521,
      "package": "swofty.classic-seasons-2017/cs_2017_redbull",
      "ai": { "asset_filename": "ferrari" },
      "engine_sound": "swofty.classic-seasons-2017/renault",
      "audio_switch": "f1_M_red_bull",
      "audio_from": "f1_m_red_bull",
      "livery": "swofty.classic-seasons-2017/redbull"
    }
  },
  "livery": {
    "redbull": {
      "vehicle": "swofty.classic-seasons-2017/redbull",
      "archive": "cars/teams/redbull/wep/redbull.erp"
    }
  },
  "engine_sound": {
    "renault": {
      "bundle": "sounds/cs17_renault.bdl",
      "settings_from": "game/renault_2025"
    }
  },
  "content.package": {
    "f1_2017": {
      "title": "lng_cs_f1_2017",
      "entrant_list": "cs_f1_2017",
      "season": 2017,
      "teams": [
        {
          "key": "redbull",
          "team": null,
          "vehicle": "swofty.classic-seasons-2017/redbull",
          "drivers": [
            { "driver": "game/daniel_ricciardo", "number": 3, "look": null },
            { "driver": "game/max_verstappen", "number": 33, "look": null }
          ]
        }
      ]
    }
  },
  "era.box": {
    "f1_2017": {
      "package": "swofty.classic-seasons-2017/f1_2017",
      "image": "#overrides.home_screen.tiles_solo_6x6[cs_f1_2017]",
      "order": 2017
    }
  }
}
```

A record names others by `<addon id>/<record id>` (`game/...` for the base game's). The shipped file lists every team, driver and package of the season; the example above shows one of each.

## 3. Pin the stat ids

A row's `stat_id` is the number the game's database uses for it. Saved careers store those numbers. A record with no `stat_id` takes the next free one when the core allocates, which can differ from the id the old `add-row` produced.

Set `stat_id` on every record that made a row in 0.6.4, to the id that row had:

```json
"vehicle": {
  "redbull": { "label": "cs17_redbull", "stat_id": 521 }
}
```

Classic Seasons reads the ids from a table generated from a full 0.6.4 install (`classic-seasons/tools/stat_ids.py`, `data/stat_ids.json`) and writes them into the records. A package's database scenario gets `database.scenario_stat_id` the same way. A career saved with 0.6.4 loads with the same cars, teams and entrant lists.

## 4. Name the replaced files

```json
// registry.json
{
  "replaced_operations": [
    "db/records.json",
    "assets/liveries.json"
  ]
}
```

Launcher 1.6 leaves both files out of the build when its core applies data-file records. Launcher 1.5 never reads `registry.json` and builds them. The two builds give the same database rows and the same files in the game folder. The build log on 1.6 says:

```text
registry: F1 2017 GP/TT Addon: db/records.json is applied by the core from the addon's registry records
```

See [Build operations that records replace](/std/manifest#build-operations-that-records-replace) for the rules.

## 5. Check the migration

`addons/classicseasons/test/row_for_row.sh <year>` builds the shared addon and every season to `<year>` three ways: the 0.6.x layout on launcher 1.5, the split layout on launcher 1.5, and the split layout on launcher 1.6 with the core's boot apply. The first two put the same files in the game folder, and the third gives the same database, rows matched by label.

```text
a    0.6.x layout, launcher 1.5
a15  split layout, launcher 1.5      same files as a
b16  split layout, launcher 1.6      same database as a, rows matched by label
```

## The JSON-only path

An addon with no native code puts the same `registry/content.json` next to a manifest that has no `operations` at all. It needs launcher 1.6.

```json
// swofty.json
{
  "schema_version": 1,
  "id": "mod.redbull-repaint",
  "name": "Red Bull Repaint",
  "version": "1.0.0",
  "author": "you",
  "category": "cars",
  "compat": { "game": "f1_25", "launcher": ">=1.6.0" }
}
```

```json
// registry/content.json
{
  "livery": {
    "repaint": {
      "vehicle": "game/redbull_2025",
      "archive": "archives/redbull_repaint.erp"
    }
  }
}
```

[Make a livery](/make/livery) walks through this one with textures.

## Continue

- [The registry](/std/registry) for every record kind and its fields.
- [Liveries](/std/systems/liveries) for `archive`, `textures` and the put-back.
- [Manifest](/std/manifest) for `registry.json`.
