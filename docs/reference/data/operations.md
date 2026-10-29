---
title: Operations
description: What an operation is, every field of swofty.json operations, prefixes, sources and settings, the build and runtime routes, and the order and conflicts between addons.
---

# Operations

An **operation** (op) is one data change an addon asks for, written as JSON in an op file that `swofty.json` lists under `operations`.

## Operations in full

Every `swofty build` (and every **Play**) reads the op files of each enabled addon and applies them to a copy of the game's files, or hands them to swofty_core, the in-game loader, to apply in memory while the game boots. The game folder keeps its stock files.

```jsonc [swofty.json]
"operations": {
  "database": ["db/changes.json"],          // database ops; the build adds db. to each
  "erp": ["erp/texture.json"],              // ERP ops; the build adds erp. to each
  "assets": ["assets/livery.json"],         // any op, each with its prefix
  "files": [                                // loose files, no op file; each entry is a file.put
    { "source": "files/sponsor.dds", "target": "livery_package_shared/sponsor.dds" }
  ],
  "screens": [                              // new in-game screens, installed as swofty/ui/<name>.ui
    { "name": "my_screen", "file": "ui/my_screen.ui", "flow": "...", "anchor": "...", "view_model": "..." }
  ],
  "ui": ["ui/home.pkgpatch.json"],          // UI patches: not built; the build skips them with a warning
  "native": [{ "dll": "bin/my_plugin.dll" }] // not an operation: an engine plugin with code
}
```

An `assets` file mixes every kind. The prefix before the dot names the kind:

```json [assets/livery.json]
{
  "schema_version": 1,
  "ops": [
    { "op": "erp.put-resource", "erp": "2025_asset_groups/f1_2026_vehicle_package/teams/toro_rosso/wep/toro_rosso.erp",
      "resource": "eaid://f1_2026_vehicle_package/teams/toro_rosso/wep/toro_rosso.wep", "source": "erp/car.erp" },
    { "op": "db.set", "select": "entrants.teams.team[_metadata_=toro_rosso_26]", "field": "display_name_code", "value": "AT" },
    { "op": "lang.set", "language": "*", "key": "lng_team_tor_hashtag", "value": "AlphaTauri" },
    { "op": "file.put", "source": "files/sponsor.dds", "target": "livery_package_shared/sponsor.dds" },
    { "op": "group.register", "group": "asset_groups/cs_2016_vehicle_package/teams/mercedes/wep/mercedes.assetgroup",
      "erps": ["2025_asset_groups/cs_2016_vehicle_package/teams/mercedes/wep/mercedes.erp"],
      "template": "asset_groups/f1_2025_vehicle_package/teams/mercedes/wep/mercedes.assetgroup" },
    { "op": "item.show", "id": "ts.sauber", "name": "Sauber 2026", "author": "Team Simplified", "version": "1.2.0" },
    { "op": "warn", "message": "Racing Bulls livery" }
  ]
}
```

An addon with only operations is a [data addon](./). An addon with code can list operations too.

## An op file

```jsonc [db/changes.json]
{
  "schema_version": 1,                 // always 1
  "ops": [                             // applied in order
    { "op": "set", "select": "rules.stop_go_penalty", "field": "penalty_duration", "value": "10" }
  ],
  "fields": {                          // optional, for add-row: extra key names to copy, keyed by <table>.<row name>,
    "entrants.teams.team": ["_metadata_", "stat_id", "display_name_code"]   // or the only ones with copy: listed
  },
  "strict": true,                      // optional: an add-row completeness warning fails the build
  "side_data": { "ignore": ["bank.*"] } // optional: side-data findings you reviewed
}
```

- `fields`: [Key names](/reference/ops/database#key-names). `strict`: [The completeness check](/reference/ops/database#the-completeness-check). `side_data`: [Acknowledging findings](/reference/ops/database#acknowledging-findings).
- An unknown op name or field fails the build and names the file.
- An ERP file may hold a `foreach` key at the top. The build drops it.

### Prefixes

A `database` file and an `assets` file with `db.` ops are the same model. The build adds `db.` to every op in a `database` file and `erp.` to every op in an `erp` file, then plans them together. Choose `assets` when one file changes several kinds at once.

```jsonc
{ "op": "set", ... }       // in a database file: read as db.set
{ "op": "set", ... }       // in an assets file: fails the build; write db.set
{ "op": "warn", ... }      // in any file: warn takes no prefix
{ "op": "erp.warn", ... }  // fails the build
```

### Settings

Every op takes two extras from the addon's [`db` settings](/reference/addons/settings#_3-read-a-db-setting):

```jsonc
{ "op": "set", "select": "rules.stop_go_penalty", "field": "penalty_duration",
  "value": "{{penalty_seconds}}" },              // {{id}} in any string: the setting's value
{ "op": "set", "select": "entrants.teams.team[_metadata_=gm_cadillac_26]", "field": "display_name_code",
  "value": "CAD", "when": { "short_names": true } } // kept only when each listed setting has that value
```

An <span v-pre>`{{id}}`</span> that names no option of the addon fails the build with `<addon id>: unknown option <id>`.

## The kinds

```text
db.set  db.add-row  db.list-append  db.list-remove      game database records          Database operations
db.put-record  db.remove-record  (db.remove-row)
erp.replace-resource  erp.add-resource                  archive resources, and mip      ERP operations
erp.copy-resource  erp.put-resource  erp.overlay        files in the game folder
erp.remove-resource  erp.merge-slices  erp.add-mipmaps
erp.patch-pkg  erp.set-material
lang.set  lang.table  lang.default                      in-game strings, one language   In-game text
                                                        or all (*); new keys are added
group.register                                          an asset group in the engine's  Asset API
                                                        asset manifest
file.put                                                a loose file in the game folder File operations
track.add      { source }                               a whole track: database rows,   A track with its own name
                                                        archive, menus, ERS
item.show      { id, name, author?, version?,           a read-only tile in the in-game Asset API
                 description? }                         ADDON SETTINGS screen
warn           { message }                              nothing; prints the message
```

Pages: [Database operations](/reference/ops/database), [ERP operations](/reference/ops/erp), [In-game text](./text), [Asset API](/reference/addons/asset-api#op-files), [File operations](/reference/ops/files), [A track with its own name](./track-names).

- `db.remove-row` is defined but not built: a build with one fails.
- A `patch-pkg` edit other than `clone-entry` and `set-value` makes the build skip every ERP op and every `warn` of that addon, with `warning: <addon id>: ERP operations skipped; <edit> is not built yet`.
- `track.add` takes `source`, a folder holding an unpacked track package.

### Sources

A `source` is a path in your addon folder. To read from the addon's data or cache folder, give an object:

```jsonc
"source": "erp/car.erp"                                            // root "addon", the default
"source": { "root": "data", "path": "library/my-mod/files/car.erp" } // $DATA/addon-data/<id>/
"source": { "root": "cache", "path": "built/car.erp" }             // $DATA/cache/addons/<id>/
```

A path that leaves its folder fails the build. See [Sources](/reference/addons/asset-api#sources).

### Code addons

`native` sits in the `operations` block but isn't an operation: it names an [engine plugin](/reference/addons/native-plugins), a DLL with code that runs in the game. `launcher`, outside `operations`, names a [launcher plugin](/reference/addons/launcher-api), whose build hook can return ops of its own. An addon with either is a code addon.

## Build route and runtime route

Each kind reaches the game one of two ways:

- **Build route.** `swofty build` writes a changed copy of the game file into the overlay folder, and **Play** swaps it into the game folder for the session through the journal.
- **Runtime route.** `swofty build` writes the changes to `swofty/assets.json`, and swofty_core applies them in memory while the game boots. The game's files stay stock.

| Kind | Build route | Runtime route | With the in-game loader |
|---|---|---|---|
| `group` | none | Registered when the engine parses its asset manifest | Runtime |
| `erp` | The archive is rewritten | An overlay archive the game reads first | Runtime |
| `db` | `gamedatabases.2025.bin` is rewritten | `db_edits`, applied as the database loads | Runtime |
| `lang` | none | A string table added after the stock one | Runtime |
| `file` | The file is copied into the game folder | Served from `swofty/overlay/` | Build (runtime is experimental) |

- Without the in-game loader, `erp`, `db` and `file` take the build route, and `group` and `lang` ops wait: the build lists them and the game doesn't see them.
- `track.add`, `item.show` and `warn` have no route: the build handles them itself.
- Some changes force the build route: an `erp.remove-resource`, a replaced resource whose layout differs from the stock one, and database edits the loader can't apply.
- To force a kind onto the build route, add it to `"build_assets"` in `$DATA/config.json`, for example `"build_assets": ["db"]`. To turn on the experimental runtime `file` route, add `"runtime_assets": ["file"]`.

```text
warning: yourname.my-addon: 3 lang op(s) wait for lang support in swofty_core
warning: yourname.my-addon: 2 file op(s) wait; the runtime file feature is experimental and "runtime_assets" in config.json does not turn it on
```

The full rules are in [Deliveries](/reference/addons/asset-api#deliveries).

## Order and conflicts

A build applies ops in this order:

1. `track.add` ops from every addon, in load order. Track activation clones the base track's database rows before any other database op runs.
2. Addons in the profile's load order.
3. Within one addon: its `database` files, then `erp` files, then `assets` files, each in the order listed, then `files` entries, then the ops its build hook returns.

A later write to the same target wins. Replacing what another addon wrote counts as a conflict, and the build prints one line per pair of addons, the winner first:

```text
warning: Scuderia AlphaTauri overrides Example Livery on 12 database values and 1 text string
```

Two items of one addon that write the same target print one line per target:

```text
warning: conflict: <target>: <addon> (<item>) overrides <addon> (<item>)
```

What replaces a target, and what composes with it:

```text
a database record          replaced by  db.set, db.put-record, db.remove-record
                           composes     db.add-row, db.list-append
a resource in an archive   replaced by  erp.put-resource, erp.replace-resource, erp.remove-resource, erp.overlay
                           composes     erp.add-resource, erp.copy-resource, erp.patch-pkg, erp.set-material
a slice of a texture array replaced by  erp.merge-slices
a string in a language     replaced by  lang.set
                           composes     lang.default, which never conflicts
a game path                replaced by  file.put, erp.add-mipmaps
an asset group             composes     group.register
```

To make your addon win, move it later in the load order, or name the other addon in `load_after` ([manifest](/reference/addons/manifest#requires-conflicts-load-after)). See [Order and conflicts](/reference/addons/asset-api#order-and-conflicts).

## Check what a build did

```sh
swofty build
```

```text
profile modded: swofty.core -> yourname.my-addon                 the load order
yourname.my-addon: add-row entrants.teams.team_11 (from ...): copied <n> key(s)   lines from each addon's ops
files: 2 game file(s) replaced, 1 added                          the build route's loose files
runtime: swofty/assets.json with 3 asset group(s), 4 resource(s), 12 database edit(s), 1 language table(s), 0 overlay file(s)
                                                                 what goes the runtime route
My Addon: 12 database values, 4 archive resources, 3 files, 2 text strings, 1 track
                                                                 what each addon writes
warning: yourname.my-addon: <message>                            a warn op
warning: yourname.my-addon: 2 lang op(s) wait for lang support in swofty_core
                                                                 ops with no route: the in-game loader is missing
warning: My Addon overrides Other Addon on 12 database values    a later addon replaced targets an earlier one wrote
warning: conflict: <target>: <addon> (<item>) overrides <addon> (<item>)
                                                                 two items of one addon wrote the same target
overlay 9c41e0b2…: $DATA/cache/overlay/modded/9c41e0b2…         the build's folder, with plan.json and swofty/assets.json
  replace 2025_asset_groups/ui_package/flow_frontend.erp         game files the build route replaces
  place swofty/assets.json                                       files the in-game loader reads
  add 1 file(s) under livery_package_shared                      game files the build route adds
```

[`swofty conflicts`](/reference/cli#swofty-conflicts) lists every overridden target. A build that can't apply an op fails and names the addon and the cause, such as `applying yourname.my-addon: track_data.game_setup.TrackDataSet[_metadata_=f9]: no row matches`.

## Reference

- [Database operations](/reference/ops/database), [ERP operations](/reference/ops/erp), [File operations](/reference/ops/files), [UI patches](/reference/ops/ui-patches): each kind's fields.
- [Asset API](/reference/addons/asset-api): asset op files, deliveries, what swofty_core does in game, and the native API.
- [Changes at runtime](/reference/addons/runtime-assets): one worked example per runtime kind.
- [Addon manifest](/reference/addons/manifest#operations): the `operations` block among every other `swofty.json` field.
