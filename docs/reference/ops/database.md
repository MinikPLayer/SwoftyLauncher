---
title: Database operations
description: Every database operation and field with an example and the build lines it prints, selectors, key names, the side-data check, the row-count rule and db settings.
---

# Database operations

Database operations change records in the game database, `game_package/gamedatabases/gamedatabases.2025.bin`.

## A database op file in full

```jsonc [db/season.json]
{
  "schema_version": 1,
  "ops": [
    // change one field of one row
    { "op": "set", "select": "rules.stop_go_penalty", "field": "penalty_duration", "value": "10" },
    // copy another key's value
    { "op": "set", "select": "{446668bc25ba571c}[_metadata_=f1_26]", "field": "worldEri",
      "value_from": "{446668bc25ba571c}[_metadata_=myteam_26].worldEri" },
    // copy a row with every key, then change some of them
    { "op": "add-row", "table": "audio.audio_bundle_settings",
      "from": "audio.audio_bundle_settings.audio_bundle_settings[_metadata_=ferrari_2025]",
      "set": { "_metadata_": "cs16_ferrari", "bundle_name": "cs16_ferrari" } },
    // add an entry to a comma-separated list field, and take one out
    { "op": "list-append", "select": "entrants.entrant_setup_modes.entrant_setup_mode[_metadata_=swofty_f1]",
      "field": "entrant_lists", "value": "cs_f1_2016" },
    { "op": "list-remove", "select": "track_data.track_layers.TrackLayer[_metadata_=f1]",
      "field": "game_setup_modes", "value": "swofty_f1" },
    // write one raw record: type tag 2 (text), value as hex bytes ("cs16_mercedes.bdl")
    { "op": "put-record", "key": "audio.audio_car_data.F1_m_mercedes.bundle_name", "type": 2,
      "value": "637331365f6d657263656465732e62646c" },
    // remove one record; a stock record is skipped without allow_stock_removal
    { "op": "remove-record", "key": "audio.audio_car_data.F1_m_mercedes.detonations_category" }
  ]
}
```

These ops are trimmed from Classic Seasons' `db/season.json` and `db/core.json`. List the file in `operations.database` in your [manifest](/reference/addons/manifest#operations):

```json [swofty.json]
"operations": {
  "database": ["db/season.json"]
}
```

Or put the same ops in an `operations.assets` file with a `db.` prefix (`db.set`, `db.add-row`). Next to `ops`, a file can hold `fields` ([Key names](#key-names)), `strict` ([The completeness check](#the-completeness-check)) and `side_data` ([Acknowledging findings](#acknowledging-findings)). For a walkthrough, see [Game database](/reference/data/database). A native plugin reads the loaded database back with [`swofty/db.hpp`](/reference/std/db), using the same keys.

A build that can't apply an op fails, naming the addon and the cause:

```text
applying yourname.my-addon: rules.stop_go_penalty.penalty_duraton: no such key
```

## Operations

### set

```jsonc
{
  "op": "set",
  "select": "entrants.teams.team[_metadata_=williams_26]",  // the row: a path, with selectors
  "field": "display_name_code",                              // the field; a path such as TrackNameData_0.title works too
  "value": "WIL"                                             // always a string
}
```

- Write every value as a string, whatever the field's type. A bool is `True` or `False`. A list joins its entries with commas and no spaces: `"0,-0.6,0"`.
- `value_from` instead of `value` copies the value of another database path.
- `"create": true` with a `"type"` tag adds the field when the row lacks it.
- A `select` that matches no row fails the build. So does a missing field, without `create`.
- `datasets` is accepted but not built: a `set` with it fails.

```text
<select>.<field>: no such key
<select>.<field>: set needs value or value_from
<select>.<field>: create needs type
dataset-scoped set is not implemented yet
```

### add-row

```jsonc
{
  "op": "add-row",
  "table": "entrants.teams",                                 // the table's real path: row numbers or selectors, never a {hash}
  "from": "entrants.teams.team[_metadata_=williams_26]",     // the source row, a row of table
  "set": { "_metadata_": "team12_26", "display_name_code": "T12" },  // keys of the copy to change
  "copy": "all",                                             // optional: all (default) or listed
  "stat_id": "fresh"                                         // optional: fresh or keep
}
```

`add-row` copies every key of the source row: its fields, its sub-object keys such as `ai_vehicle_config.*` on vehicles, and its nested tables with their `_instancecount_` keys and rows, at any depth. `set` then changes keys of the copy. Every `set` key must be a key the copy has. The copy goes at the table's next row number, and the table's count goes up by one ([The row-count rule](#the-row-count-rule)).

- `copy`: `all` copies every key of the source row and every key in the file's `fields` list. `listed` copies only the keys in the `fields` list.
- `stat_id`: `fresh` gives the next free id above the table's highest `stat_id`. `keep` keeps the source row's id. The default is `fresh` in a top-level table whose rows all have different ids, and `keep` elsewhere ([Stat ids](#stat-ids)).

The build prints, prefixed with the addon's id:

```text
my-addon: add-row entrants.teams.team_<n> (from entrants.teams.team[_metadata_=williams_26]): stat_id <id> (the source's is <id>)
my-addon: add-row entrants.teams.team_<n> (from entrants.teams.team[_metadata_=williams_26]): copied <n> key(s)
```

It fails with one of:

```text
<table>: add-row needs the table's real path, not a {hash}
<from> is not a row of <table>
<table>.<row>: unknown field <field>
<table>.<row>: copy listed needs a fields list
<new row>.<field> already exists
```

### list-append and list-remove

```jsonc
{ "op": "list-append",
  "select": "entrants.entrant_setup_modes.entrant_setup_mode[_metadata_=swofty_f1]",
  "field": "entrant_lists",
  "value": "cs_f1_2016,cs_f1_2017" }   // each comma-separated value is added if missing
{ "op": "list-remove",
  "select": "track_data.track_layers.TrackLayer[_metadata_=f1]",
  "field": "game_setup_modes",
  "value": "swofty_f1" }               // each comma-separated value is removed
```

Both treat the field as a comma-separated list and keep its order. Several addons can append to the same list without a conflict. A missing field fails the build with `<select>.<field>: no such key`.

### put-record

```jsonc
{
  "op": "put-record",
  "key": "audio.audio_car_data.F1_m_mercedes._metadata_",  // a path, which can use selectors, or 0x and 16 hex digits
  "type": 2,                                              // the record's type tag
  "value": "46315f6d5f6d65726365646573"                   // the bytes, in hex: "F1_m_mercedes"
}
```

`put-record` writes one raw record. When the key exists, its tag and value are replaced; otherwise the record is added.

```text
<key>: value must be hex bytes
bad record key <key>
```

### remove-record

```jsonc
{ "op": "remove-record",
  "key": "tracks.Track_0.PoleOnRight",   // a path, which can use selectors, or 0x and 16 hex digits
  "allow_stock_removal": true }          // optional: remove a record of the stock database
```

A `remove-record` whose key is already absent is a no-op, and the build prints `<addon id>: <key>: already removed`.

### Removing stock records

A `remove-record` of a key in the game's stock database is skipped. The build warns once per addon with the count:

```text
warning: My Mod: 12 removal(s) of stock database records skipped; the game crashes when a record it reads is missing
```

The addon's row on the **Addons** page shows the count after a build or a conflict check. Keys that addons add, earlier in the same addon or in another one, are removed as before. Set `"allow_stock_removal": true` on the op to remove a stock record.

A `remove-record` of a record that a track's activation adds is always skipped, with or without `allow_stock_removal`:

```text
warning: My Mod: 12 removal(s) of database records the tracks add skipped; the game crashes when a record it reads is missing
```

The game crashes on any key it reads that is missing; see [Missing keys](/reference/gamedb/reading#missing-keys). A build that removes a stock record writes the database into `gamedatabases.2025.bin` instead of editing it at runtime. **Export game files (.zip)** in the DB Editor skips the same removals and lists them.

### Renamed and removed entrant lists

The game looks up its entrant lists (`entrants.entrant_lists`) by name. A database mod made from an older game version's database renames or removes a list that the newer version added:

```jsonc [db/teams.json]
// made from an older database: the stock f2_2026 list becomes f2_2025
{ "op": "set", "select": "entrants.entrant_lists.entrant_list[_metadata_=f2_2026]", "field": "_metadata_", "value": "f2_2025" }
```

When an addon's ops rename one of the stock lists, remove it, lower `_entrant_list_instancecount_` so the list is past the count, or take a list name out of a content set's `available_entrant_lists` or an entrant setup mode's `entrant_lists` or `selectable_entrant_lists`, the build applies the ops as written. It warns once per addon:

```text
warning: My Mod: renames or removes the entrant list(s) f2_2026; F2 menus may crash with it
```

### remove-row

```json
{ "op": "remove-row", "select": "tracks.Track_3" }
```

`remove-row` is defined but not built: a build with one fails with `remove-row is not implemented yet`.

## How add-row names keys

### Key names

The database stores a hash of each key's path, not the path, so the build can't list a row's keys. It probes the source row with the launcher's key catalogue, `row_keys.txt.gz`: every key path found under each row shape of the stock database, generated from the identifier strings in `F1_25.exe`, its printf format strings with each integer conversion expanded to 0..31 (`colour_%u_default` gives `colour_0_default` to `colour_31_default`), `tools/gamedb/names.txt`, the key catalogue and the run-time compositions in `tools/gamedb/compositions.txt`, such as `player.driver.preloaded` on entrant setup modes. A row shape is a row path with its row numbers written `_N`: `entrants.entrant_setup_modes.entrant_setup_mode_N`.

The file's `fields` map, keyed by `table` as written or its resolved path and the row name, adds names the catalogue lacks. Every listed field must exist on the source row:

```json
"fields": { "entrants.teams.team": ["_metadata_", "stat_id", "display_name_code"] }
```

`copy: listed` keeps the behaviour of launcher 1.2.1 and earlier: the copy has only the listed fields, and the `fields` list is required.

### The completeness check

The build prints how many keys each `add-row` copied, and checks the copy against the catalogue:

```text
copy all: a key of the source row that the fields list doesn't name (a line)
  add-row … (from …): copied 9 key(s) the fields list for … doesn't name: …
copy listed: a key of the source row that the fields list doesn't name, so the copy lacks it (a warning)
  add-row … (from …): the fields list for … misses 9 key(s) of the source row, not copied: …
both: a row shape, of the row or a nested row, that has no catalogue entry (a warning)
  add-row … (from …): the key catalogue has no entry for …; keys only the exe builds at run time may be missing
```

Rows past a row count that `set` lowers don't count as missing. With `"strict": true` at the top of the op file, each warning fails the build.

The build can't count the keys it can't name. An FNV-1a key gives no sign of the path it hashes, and the file holds records in hash order, so a key under the source row and a key elsewhere look alike. A key that only the game's code builds at run time, from names the exe holds in pieces, is missing from the catalogue until `tools/gamedb/compositions.txt` lists it. Regenerate the catalogue after changing that file:

```sh
cargo run --release -p swofty-core --example row_keys -- --game <F1 25 folder>
```

`cargo run -p swofty-core --example check_add_rows -- --game <F1 25 folder> --addon <addon folder>` applies an addon's database files to the stock database and prints the check. `--listed` checks the files as launcher 1.2.1 copied them; `--strict` stops at the first finding.

### Stat ids

Tables indexed by `stat_id` find a row by its id, so a copy that keeps its source's id hides the source or another copy. Every stock top-level table with a `stat_id` on each row gives each row its own id, except `f1_world.map.markers.TrackMapMarker_N`, `schedule.events.fixed_event_N` and `track_data.game_setup.TrackDataSet_N`. Among them: `entrants.entrant_lists.entrant_list_N`, `entrants.entrant_setup_modes.entrant_setup_mode_N`, `entrants.vehicles.vehicle_packages.vehicle_package_N`, `entrants.teams.team_N`, `entrants.drivers.driver_N`, `entrants.vehicles.vehicle_N` and `game_setup.game_setup_modes.game_setup_mode_N`.

A copy into such a table gets the next free id above the table's highest, and the build prints it:

```text
f1-1990: add-row entrants.entrant_lists.entrant_list_68 (from entrants.entrant_lists.entrant_list[_metadata_=f1_2026]): stat_id 105 (the source's is 67)
```

A `stat_id` in `set` wins, unless a row an addon added earlier in the build holds that id. The copy then gets the next free id, references to it by label follow, a `[stat_id=<id>]` selector later in the same file finds the copy, and the build warns with both addons and both ids:

```text
my-addon: stat_id 500 of my_team is taken by their_team from other-addon; the build gave it stat_id 512. The addon's author should leave stat_id out of add-row
```

The conflicts report counts each of these as a database conflict between the two addons. The id a copy moves to depends on the addons before it in the load order, so a career made with one load order expects that order's ids. An id that a stock row holds stays as set. A copy into a nested table, such as an entrant of an entrant list, keeps its source's id; the build prints a line when the nested table's ids were all different.

An `unlockable_se_livery` row added to an `unlockable_content.unlockable_items.unlockable_item_list_<n>.folder_<m>` table, by `add-row` or by `put-record`, unlocks the new special livery. The build writes the row's ownership key, the djb2 hash of `eSELivery` and the row's `_metadata_`, to the `owned_items` block of `swofty/settings.json`, and `swofty_core.dll` reports that key as owned to the livery picker. A key that any stock database value also gives is left out with a warning.

## What a copy does not bring along

`add-row` copies the source row's keys. The game also finds data for a row by its label, by a name built from the label or from a field, or by its `stat_id`, in other tables and in game files. A copy under a new label gets none of that data. The game then reads stock defaults, a missing row or a missing resource for it, often without an error: the car runs, but on the wrong setup.

- **Rows of another table labelled with the source's label**, found by their own label. Teams: `showroom.teams_history.team_history`, `f1_world.fan_zone.fz_choices_stings.FZChoicesStings` (`williams_26`).
- **Rows of another table that name the source in a field**, found by a field value or list of each row, every row read. Teams: `pit_lane.pit_times.team_pit_times`, by `team`. Game setup modes: `game_setup.branding.game_setup_branding` and `track_data.game_setup.TrackDataSet`, by `game_setup_modes`.
- **Rows labelled with a name built from the source**: a folder name, the label plus a suffix, a field value. Vehicle packages: `car_setup.presets.vehicle_package_setups_N`, labelled with the package folder `f1_2025_vehicle_package`, the first folder of `resource_path`.
- **Values elsewhere that name the source**: other rows hold the source's label or `stat_id`. Vehicles: `entrants.entrant_lists.entrant_list_N.entrant_N.vehicle`, `showroom.vehicle_offsets.vehicle_offset_N.vehicle`, `showroom.vehicles_history.vehicle_history_N.vehicle`. Vehicle packages: `entrants.vehicles.vehicle_N.vehicle_package`.
- **Game files named after the source**: paths built from the label or a field. Vehicle packages: the car template's `CarPhysics2.baseVtfAsset` and `Tyres` defaults name `eaid://f1_2025_vehicle_package/...`. Vehicles: per-track AI data at `environment_package/tracks/<track>/ai/<ai_package_folder>/<asset_filename>/`.
- **Keys of the source the catalogue does not name**: paths the exe builds at run time. Car setup presets: 420 keys per row. Game setup modes: 11 keys under `championship_won_cutscene` and `tyre_regulations`, which the `swofty_f1` copy lacked until `addons/swoftymod/db/core.json` listed them in a `fields` list. Both are in the catalogue from launcher 1.4.0. Customisation items: `colour_1_default` to `colour_4_default`; see [Colour defaults of customisation items](#colour-defaults-of-customisation-items).

### Car setups of a vehicle package

On session start the game finds a vehicle package's car setups with the row lookup `car_setup.presets.vehicle_package_setups[_metadata_=<package folder>]` (reader `0x140c04fd0`) and reads `<row>.<ai|player>.<variant>.<slider>`:

- variants: `max_downforce`, `med_downforce`, `balanced`, `med_speed`, `max_speed`;
- sliders: the 42 labels of `car_setup.sliders.slider_N`, such as `ballast`, `brake_bias`, `front_suspension_height` and `rear_suspension_height`.

Each stock row holds these 420 keys and its `_metadata_`. A copy of `f1_2025` with the folder `cs_2019_vehicle_package` needs a preset row of its own:

```json
{ "op": "add-row", "table": "car_setup.presets",
  "from": "car_setup.presets.vehicle_package_setups[_metadata_=f1_2025_vehicle_package]",
  "set": { "_metadata_": "cs_2019_vehicle_package" } }
```

```text
preset row for the copy   in game
none                      the AI drives setups that ground the car: plank contacts, sparks, running wide
_metadata_ only           loading the session crashes (a read at index -1)
all 421 keys              the copy drives its source's setups
```

Launchers before 1.4.0 copy only the catalogued `_metadata_`; give the op a `fields` list of the 421 keys for them.

### Colour defaults of customisation items

Every row of these tables holds the four keys `colour_1_default`, `colour_2_default`, `colour_3_default` and `colour_4_default`, of tag 11 (colour). No row holds a `colour_0_default` or a `colour_5_default`.

| Table | Rows |
|---|---|
| `customisation.custom_avatar.suits.Suit_N` | 362 |
| `customisation.custom_avatar.gloves.Gloves_N` | 362 |
| `customisation.custom_avatar.helmets.Helmet_N` | 279 |
| `customisation.custom_avatar.tops.Tops_N` | 360 |
| `customisation.custom_avatar.trousers.Trousers_N` | 110 |
| `customisation.custom_avatar.casual_footwear.CasualFootWear_N` | 49 |
| `customisation.custom_avatar.casual_headwear.CasualHeadwear_N` | 94 |
| `customisation.custom_avatar.eyewear.Eyewear_N` | 29 |

- Most stock rows hold empty text, which the game parses with `atoi` as 0. Customisable designs hold signed 32-bit ARGB numbers, such as `-1710619` (`#e5e5e5`).
- At boot, `Sess_Apply_ContentAvailability` calls `Game_Build_CustomisationCatalogue` (`0x1414059c0`). It builds the four names from the format string `colour_%u_default` and reads each of them on every row with `Gdb_Get_FieldInt`, with no check for a missing key (`0x1414078f2` to `0x1414078fa` for gloves, the same for the other tables). A row that lacks any of the four crashes the game at boot with an access violation at `F1_25.exe+0x14078fa`.
- A customisable suit design gives an AI driver's suit its colours from `colour_1_default` to `colour_3_default`. An AI driver in `tier01_livery76` wears its `#e5e5e5`, `#0e3858` and `#ffb43f` (`-1710619`, `-15845288`, `-19393`).
- `add-row` copies the four keys of any of these rows, empty text included, and swofty_core applies a copied empty value as the game's 0. Launcher 1.4.0 and earlier copy them only from a `fields` list, and on the runtime route their swofty_core ignores the empty values: `edit … ignored: an int needs integer text (the game parses it with atoi), got ''`.

### The side-data check

After every addon's operations ran, the build checks each `add-row` whose copy has a new label, against the built database. A finding is one of three kinds:

- **Known lookup**: a lookup a reverse-engineered reader makes, listed below. A warning per lookup and addon.
- **To review**: a name or value match with no known reader. One warning per addon with the count.
- **Out of area**: a match in a table of a game area the copy does not reach by default. Listed only.

`swofty build --side-data` lists every finding after the warnings, tagged `[known]`, `[review]`, `[ignored]` or with its area, such as `[career]`. The warnings do not fail the build, even with `"strict": true`.

#### Known lookups

- **A copy of `entrants.vehicles.vehicle_packages.vehicle_package_N`**: the game looks up the `car_setup.presets.vehicle_package_setups` row labelled with folder 1 of `resource_path`, in `Car_Load_SetupPreset` (`0x140c04fd0`). Without it, the AI drives setups that ground the car.
- **A copy of `game_setup.game_setup_modes.game_setup_mode_N`**: the `game_setup.branding.game_setup_branding` rows whose `game_setup_modes` list the mode, in `0x142a12f80`. Without them, no branding: logos and flags such as `car_setup_available` and `mfd_enabled`.
- **A copy of `game_setup.game_setup_modes.game_setup_mode_N`**: the `track_data.game_setup.TrackDataSet` rows whose `game_setup_modes` list the mode, in `0x142e44b50`. Without them, no per-track data set.
- **A copy of `entrants.teams.team_N`**: the `pit_lane.pit_times.team_pit_times` row whose `team` names the team, in `0x1434c8bf0` (the pit lane system). Without it, other pit stop times.

A copy whose name for the lookup is the source's, such as a vehicle package copy that keeps its `resource_path`, finds the source's row and gets no finding.

```text
Classic Seasons: F1 2019: car_setup.presets.vehicle_package_setups has a row labelled f1_2025_vehicle_package and none labelled cs_2019_vehicle_package; f1_2025_vehicle_package is folder 1 of resource_path of the add-row vehicle package entrants.vehicles.vehicle_packages.vehicle_package[_metadata_=f1_2025], and Car_Load_SetupPreset (0x140c04fd0) reads the package's car setups from that row on session start: without it the AI drives setups that ground the car. Add a row per copy, such as an add-row into car_setup.presets.vehicle_package_setups from car_setup.presets.vehicle_package_setups[_metadata_=f1_2025_vehicle_package] that sets _metadata_
```

#### Matches to review

1. The check lists the names the game can derive from the source row: the label, the label plus the table's row word (`f1_2025_vehicle_package`), identifier values of the row's fields (`asset_filename`, `ai_package_folder`), and the folders of path values that hold the label or a digit (`resource_path`). It keeps the names the copy changed.
2. For each such name, it finds rows labelled with it in other tables, and reports each table that has no row labelled with one of the copy's names from the same fields. A copy that keeps a field value, such as `ai_package_folder`, still reaches the rows labelled with it, except when the source's label or label plus row word gives the same name.
3. It finds values elsewhere that name the source by its label, or by its `stat_id` in an id field named after the source's table (`team_id`), and reports each field that never names the copy. When rows of other tables share the source's label, it counts only fields named after the source's table or whose catalogue meaning names it.

It leaves out:

- the table the copy went into, and rows under the source and the copy;
- tables covered by a known lookup for the copy;
- a field value that names another row, such as an entrant's `driver`, when the copy's value labels a row of a table where the source's value labels one too: that row's own copy is checked;
- rows labelled with the label of another `add-row` source of the build in the same table, such as `entrants.vehicles.vehicle_packages.vehicle_package[_metadata_=f1_2025]` for an entrant list copied from `f1_2025`;
- values inside copies the same addon made that still name the source, such as a package copy's `ai_package_folder`.

It groups the findings per table or field and addon. The copies SwoftyMod adds get no findings.

```text
Classic Seasons: F1 2019: 23 side-data finding(s) to review, matched by name with no known reader: rows labelled with a source's name in animation.team_data.teamData, audio.audio_bundle_settings.audio_bundle_settings, beta.team_colours.teamColours and 15 more and none for the copy; values naming a source in 16 field(s) and none naming the copy. swofty build --side-data lists them; list reviewed ones in the op file's side_data.ignore
```

#### Game areas

A match in a table under one of these prefixes is listed only, unless the addon made an `add-row` copy into a table of the same area:

| Area | Table prefixes |
|---|---|
| `career` | `accolades`, `bank`, `career`, `challenge_career`, `driver_academies`, `driver_contracts`, `driver_market`, `emails`, `engine_management`, `facilities`, `invitational_events`, `news`, `personal_development`, `pit_crew_training`, `press_interviews`, `recognition`, `resource_points`, `rivals`, `specialist_manager`, `sponsors` |
| `story mode` | `story_mode`, `driver_statistics.story_mode_driver_statistics` |
| `F1 World` | `f1_world`, `second_worlds`, `f1cg`, `auction` |
| `online` | `leaderboards`, `online_systems`, `entitlements`, `paddock_pass`, `options_system.options.multiplayer`, `options_system.context`, `options_system.restrictions` |
| `front end` | `showroom`, `f1_life`, `videos` |
| `customisation` | `customisation`, `unlockable_content` |
| `unnamed` | top-level names the catalogue does not know, written `{<hash>}` |

#### Acknowledging findings

`side_data.ignore` at the top of an op file lists the tables and fields whose findings you reviewed, for the copies the file makes:

```json
{
  "schema_version": 1,
  "side_data": { "ignore": ["bank.*", "engine_management.driver_wear_rates.driver_wear_rate"] },
  "ops": []
}
```

A pattern is a dotted table path (`<container>.<row word>`) or field path, as the findings print them, with or without row numbers and `_N`; `*` matches any run of characters. `accolades.AccoladeDataGroup.driver_data.AccoladeDriver` and `accolades.AccoladeDataGroup_N.driver_data.AccoladeDriver_N` are the same pattern. A pattern covers known lookups too. Acknowledged findings are listed with `[ignored]`. A pattern that matches no finding gives a warning. An unknown key under `side_data`, or a pattern that is not a path, fails the build like other op-file errors.

#### Before you copy

`swofty db deps <row>` prints the same data for a stock row: the derived names, the known lookups with the row each finds, the rows of other tables labelled with each name, and the fields whose values pick the row, each tagged with its area:

```sh
swofty db deps "entrants.vehicles.vehicle_packages.vehicle_package[_metadata_=f1_2025]"
```

`cargo run -p swofty-core --example check_add_rows -- --game <F1 25 folder> --addon <addon folder>` prints the check for one addon with the completeness check.

### Keys the catalogue does not name

A key whose path only the exe builds stays out of the catalogue until `tools/gamedb/compositions.txt` lists it, and `add-row` then copies it without a `fields` list. A name the exe prints from one format string with one or two integer conversions, such as `colour_%u_default`, needs no line there for `add-row`: the `row_keys` generator expands the exe's format strings. `tools/gamedb/catalog.py` does not, and names such a key only from a `compositions.txt` line. `tools/gamedb/deepkeys.py` finds such keys under a row; see [Finding unnamed keys](/reference/gamedb/tools#finding-unnamed-keys).

## Selecting rows

A path names a row by its index or finds it by a field. Any segment of the path can be a selector, `Name[field=value]`:

```text
tracks.Track_3                                                                  the row at index 3
entrants.teams.team[_metadata_=gm_cadillac_26]                                  the team with this label
track_data.game_setup.TrackDataSet[_metadata_=f1_2026].TrackData[_metadata_=melbourne]
                                                                                Melbourne's row in the f1_2026 dataset
{446668bc25ba571c}[_metadata_=f1_26]                                            a row in a table whose full path is unknown
```

Selectors resolve left to right, each one beneath the rows already chosen. A selector reads the table's rows up to its `_instancecount_`, or up to the first row without a `_metadata_` when the table has no count, and must match exactly one row. Otherwise the build fails and names the segment:

```text
track_data.game_setup.TrackDataSet[_metadata_=f9]: no row matches
track_data.game_setup.TrackDataSet[_metadata_=f1_2026].TrackData[_metadata_=okayama]: no row matches in track_data.game_setup.TrackDataSet_16.TrackData
entrants.teams.team[display_name_code=WIL]: 2 rows match
```

Values are unquoted and can contain `.` and `[`. A value ends at the first `]` that ends the path or comes before a `.`.

Selectors work in `select`, `field`, `value_from`, `add-row`'s `table` and `from`, and the `key` of `put-record` and `remove-record`. A `{hash}` placeholder goes only at the start of a path; selectors can follow it.

Use labels where possible. Row numbers can change between game builds. The dataset rows under `track_data.game_setup` are listed in [A track with its own name](/reference/data/track-names#_3-the-op-file).

## The row-count rule

Adding a row also updates the table's `_instancecount_`. The game ignores rows beyond this count. A new row needs every key a stock row of the table has, which `add-row` copies; see [Key names](#key-names) and [Adding a row](/reference/gamedb/recipes#adding-a-row).

`add-row` raises the count of the table `table` resolves to. Adding to `track_data.game_setup.TrackDataSet[_metadata_=f1_2026]` raises `track_data.game_setup.TrackDataSet_16._TrackData_instancecount_`.

`add-row` needs the table's real path, written with row numbers or selectors. A `{hash}` placeholder can be used for `set`, but not for `add-row`.

A mod made for an earlier game version can lower a count and leave stock rows above it. `add-row` writes the new row over those stock keys and the build prints `reuses N stock key(s) of a row above the table's count`. A key another addon wrote at the new row's number still fails the op with `already exists`.

## Rows SwoftyMod adds

SwoftyMod adds two rows labelled `swofty_f1`, copies of the stock `f1` rows, before any addon's operations run:

- a game setup mode in `game_setup.game_setup_modes`, with `stat_id` 20 and the `swofty_f1` entrant setup mode;
- an entrant setup mode in `entrants.entrant_setup_modes`, with empty `entrant_lists` and `selectable_entrant_lists`.

Every stock list of game setup modes that holds `f1` also holds `swofty_f1`, except the lists of the championships (`championships.Championship_N`: the careers and Braking Point), which keep their one setup mode. A SwoftyMod change that does not apply to the database is skipped with a warning. An addon adds its entrant lists to the mode with `list-append`:

```json
{ "op": "list-append", "select": "entrants.entrant_setup_modes.entrant_setup_mode[_metadata_=swofty_f1]",
  "field": "entrant_lists", "value": "my_entrant_list" }
```

## Settings

A `db` [option](/reference/addons/manifest#options) changes operations at build time:

- <span v-pre>`{{id}}`</span> in any string is replaced with the option's value: `true` or `false`, the number, or the choice value.
- `"when": { "id": value }` keeps the operation only when every listed option has that value.

```json
{ "op": "set", "select": "rules.stop_go_penalty", "field": "penalty_duration", "value": "{{penalty_seconds}}" },
{ "op": "set", "select": "entrants.teams.team[_metadata_=gm_cadillac_26]", "field": "display_name_code", "value": "CAD",
  "when": { "short_names": true } }
```

Changing a `db` option marks the profile for a rebuild. The next launch, or `swofty build`, applies it.

## Applying changes

Track activation runs first, then operations run in addon load order. [DB Editor](/using/db-editor) packages are addons and run in the same order; a package change that no longer applies is skipped with a warning instead of failing the build:

```text
warning: My DB Edits: change skipped: entrants.teams.team[_metadata_=williams_26]: no row matches
```

An addon whose track the build leaves out has its failing database ops skipped the same way. A later change to a field replaces the earlier value, and the build prints the conflict ([Order and conflicts](/reference/data/operations#order-and-conflicts)). With the in-game loader, the changed records reach the game through `swofty/assets.json` ([Build route and runtime route](/reference/data/operations#build-route-and-runtime-route)).

See [Game database (GVDB)](/reference/gamedb) for the file format, how the game reads it and the table catalogue. [Modding recipes](/reference/gamedb/recipes) covers adding rows, grid size and tracks.
