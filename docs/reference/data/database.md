---
title: Game database
description: Change teams, cars, drivers, tracks, rules and unlocks from a data addon — find a key with gamedb, write a database operation, build it and read the result.
---

# Game database

Teams, cars, drivers, tracks, rules and unlocks live in one file, `game_package/gamedatabases/gamedatabases.2025.bin`, which a data addon changes with [database operations](/reference/ops/database).

## In full

```json [swofty.json]
{
  "schema_version": 1,
  "id": "yourname.grid-12",
  "name": "Team 12",
  "version": "1.0.0",
  "author": "Your Name",
  "kind": ["database"],
  "compat": { "game": "f1_25", "game_builds": [">=25251167"] },
  "operations": { "database": ["db/changes.json"] }
}
```

```jsonc [db/changes.json]
{
  "schema_version": 1,
  "ops": [
    // change a value
    { "op": "set", "select": "rules.stop_go_penalty", "field": "penalty_duration", "value": "10" },
    // add a row: a copy of Williams with a new label
    { "op": "add-row", "table": "entrants.teams",
      "from": "entrants.teams.team[_metadata_=williams_26]",
      "set": { "_metadata_": "team12_26", "display_name_code": "T12" } },
    // rename a track in one dataset's menus
    { "op": "set", "select": "track_data.game_setup.TrackDataSet[_metadata_=f1_2026].TrackData[_metadata_=melbourne]",
      "field": "TrackNameData_0.title", "value": "lng_my_track_title" }
  ]
}
```

`swofty build` prints a line per `add-row`, with the new row's path, its new `stat_id` and the number of keys it copied:

```text
yourname.grid-12: add-row entrants.teams.team_<n> (from entrants.teams.team[_metadata_=williams_26]): stat_id <id> (the source's is <id>)
yourname.grid-12: add-row entrants.teams.team_<n> (from entrants.teams.team[_metadata_=williams_26]): copied <n> key(s)
```

The database is a flat list of hashed keys, each with a type tag and a text value. The build applies the ops to a copy; the file in the game folder stays stock. See [Data addons](./) for the steps around this page.

## 1. Find the key

Every value has a dotted path: the row, then the field. Pass that path to `tools/gamedb.py get` to hash it and print the record it names. Point the tool at a copy of the game's database:

```sh
python3 tools/gamedb.py get work/game/game_package/gamedatabases/gamedatabases.2025.bin rules.stop_go_penalty.penalty_duration
```

You'll get the key hash, type name and current value, separated by tabs:

```text
43a617d8995bfd8d	int	5
```

To find a different value, start with the [table catalogue](/reference/gamedb/catalogue), which lists every table. Then look up the table's fields and their types in the [key schema](/reference/gamedb/schema). To browse the whole database, run `tools/gamedb.py dump <db> --tree -o db.json` to write it as a tree.

## 2. Change a value

Use `set` to write one field of one row. Here, you change the penalty duration:

```json [db/changes.json]
{
  "schema_version": 1,
  "ops": [
    { "op": "set", "select": "rules.stop_go_penalty", "field": "penalty_duration", "value": "10" }
  ]
}
```

List the file in your manifest:

```json [swofty.json]
"operations": {
  "database": ["db/changes.json"]
}
```

Give `select` the row you want to change. Row numbers move between game builds, so choose a label over an index:

| Selector | Row |
|---|---|
| `tracks.Track_3` | row 3 of `tracks` |
| `entrants.teams.team[_metadata_=williams_26]` | the team labelled `williams_26` |
| `track_data.game_setup.TrackDataSet[_metadata_=f1_2026].TrackData[_metadata_=melbourne]` | Melbourne in the `f1_2026` dataset |
| `{446668bc25ba571c}[_metadata_=f1_26]` | a row in a table known only by its hash |

Any segment of the path can pick a row by a field. Each selector must match exactly one row ([Selecting rows](/reference/ops/database#selecting-rows)).

Write values as strings, whatever the field's type. For a bool, use `True` or `False`; for a list, join the entries with commas and no spaces (`0,-0.6,0`). To copy another key's value, use `value_from` instead of `value`:

```json
{ "op": "set", "select": "{446668bc25ba571c}[_metadata_=f1_26]", "field": "worldEri",
  "value_from": "{446668bc25ba571c}[_metadata_=myteam_26].worldEri" }
```

## 3. Add a row

To add a row, use `add-row`: it copies a stock row with every key, nested ones included, changes some fields and raises the table's row count:

```json [db/grid.json]
{
  "schema_version": 1,
  "ops": [
    {
      "op": "add-row",
      "table": "entrants.teams",
      "from": "entrants.teams.team[_metadata_=williams_26]",
      "set": { "_metadata_": "team12_26", "display_name_code": "T12" }
    }
  ]
}
```

Give your new row a new `_metadata_` label. In a table indexed by `stat_id`, such as teams, vehicles and entrant lists, the copy gets the next free id; the build prints it. Pass the table's real path to `add-row`, not a `{hash}`.

::: warning Read the build's add-row lines
The database keeps no key names, so the build names the source row's keys from the launcher's key catalogue. It warns when a row shape isn't in the catalogue, because a key the game only builds at run time may then be missing; see [Key names](/reference/ops/database#key-names). A missing key crashes the game.
:::

You'll find the full field sets in [Adding a row](/reference/gamedb/recipes#adding-a-row).

`remove-row` is defined but not built yet: a build with one fails.

## 4. Rename a track

Menus read a track's name from the `TrackNameData_0` row of its `TrackData` row in each dataset under `track_data.game_setup`. They don't read it from `tracks.Track_N`, so setting `tracks.Track_N.title` alone changes no menu. Set the name to a language key once per dataset:

```json
{ "op": "set", "select": "track_data.game_setup.TrackDataSet[_metadata_=f1_2026].TrackData[_metadata_=melbourne]",
  "field": "TrackNameData_0.title", "value": "lng_my_track_title" }
```

Look up each dataset's row and fields in [Renaming a track](/reference/gamedb/recipes#renaming-a-track). Supply the string behind the key with a `lang.set` operation ([In-game text](./text)). To name a community track your addon ships, see [A track with its own name](./track-names).

## 5. Drive values from a setting

To let a player pick the value, use a `db` [setting](/reference/addons/settings#_3-read-a-db-setting). It fills <span v-pre>`{{id}}`</span> placeholders, and `when` keeps an operation only for the values it lists:

```json
{ "op": "set", "select": "rules.stop_go_penalty", "field": "penalty_duration", "value": "{{penalty_seconds}}" }
```

Changing a `db` setting marks the profile for a rebuild. The change takes effect on the next build or launch.

## 6. Build and check

```sh
swofty build
```

Read the output for conflicts. Tracks activate first, then each addon's operations apply in load order. When two addons write the same key, the later write wins and the build prints the conflict.

With the in-game loader, your changed records go to the game in `db_edits` in `swofty/assets.json`, and swofty_core applies them while the game boots. The database in the overlay folder then holds only the track activations, if any. Without the loader, or with `"build_assets": ["db"]` in `$DATA/config.json`, the build rewrites the database in the overlay folder. To read your change there, run `tools/gamedb.py get` on `$DATA/cache/overlay/<profile>/<buildhash>/game_package/gamedatabases/gamedatabases.2025.bin`. See [Build route and runtime route](./operations#build-route-and-runtime-route).

## Reference

- [Database operations](/reference/ops/database): every operation and field.
- [Operations](./operations): how database files fit with the other operation kinds, order and conflicts.
- [Game database (GVDB)](/reference/gamedb): the file format and how the game reads it.
- [Modding recipes](/reference/gamedb/recipes): full field sets, grid size and tracks.
