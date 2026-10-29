---
title: How tracks are installed
description: What activating a community track changes in the archives, the game database, the menus and the ERS settings.
---

# How tracks are installed

A track is installed by a `track.add` operation: at each build, activation clones a base track's database rows, installs the track's archive and adds its menu entries.

## A track activation in full

```json [ops/track.json]
{
  "schema_version": 1,
  "ops": [
    { "op": "track.add", "source": "track" }
  ]
}
```

`source` is a folder in the addon that holds an unpacked [track package](/reference/mods/tracks): `manifest.json`, the track's `.erp`, `mipmaps/` and `ui/`. The Simplified Importer writes this file into every track addon it makes, `mod.<code>`. A data addon can list `track.add` in an `assets` file too ([A track with its own name](/reference/data/track-names)).

```sh
swofty build
```

```text
okayama: track data: okayama cloned from melbourne in 19 dataset(s): f1, f2, classic, f1_2023, sprint_f1_2023, f1_2024, sprint_f1_2024, f2_2023, f1_2025, sprint_f1_2025, f2_2024, multiclass, sprint_multiclass, f2_2025_sprint, f2_2025, f1_2026, sprint_f1_2026, f2_2026_sprint, f2_2026
runtime: swofty/assets.json with 77 asset group(s), 0 resource(s), 0 database edit(s), 0 language table(s), 0 overlay file(s)
```

- Both `swofty build` and `swofty apply` activate every `track.add`, in load order, before the other database ops. Those ops can't remove the records activation adds; see [Removing stock records](/reference/ops/database#removing-stock-records).
- Two `track.add` ops with one track code fail the build with `track okayama is added twice`.
- Mods in the importer's library that haven't converted yet get a `track.add` from the importer's build hook instead, with the source `library/<id>/track` in Team Simplified's `addon-data` folder.
- The code is in `packages/core/src/activation/`.

For playing with tracks, see [Simplified Importer](/using/mods#tracks).

## The track archive

Putting a track's archive in place isn't enough: the game only loads an asset group when its asset manifest (`fileList.manifest`) lists it. A listed group `asset_groups/<path>.assetgroup` points to the file `2025_asset_groups/<path>.erp`. Your new track's own paths, such as `environment_package/tracks/okayama/wep/okayama_common.assetgroup`, aren't on that list.

How the build handles this depends on the in-game core. When its `features.json` has `group`, the package's `<code>.erp` is added at its natural path, `2025_asset_groups/environment_package/tracks/<code>/<code>.erp`. Then `swofty/assets.json` registers these groups for it at runtime:

- the main group, `asset_groups/environment_package/tracks/<code>/<code>.assetgroup`;
- `wep/<code>_<variant>` for every world in the archive, stand-ins included;
- the 2019 template groups `bake/<code>_ao.bake`, `bake/<code>_light_list.bake`, `bake/<code>_radiance.bake`, `probes/config.ddgi` and `probes/key_2019`;
- `probes/probes_garage_<nn>` for every garage probe set in the archive;
- `ai/<vehicle_package>/<team>/<team>` for every AI team folder in the archive.

You can enable any number of tracks this way.

Otherwise, the build puts the package's whole `<code>.erp` into an existing invitational event slot, `2025_asset_groups/ui_package/texture_arrays/invitational_events/event_images/invitational_image_<n>.erp`, alongside the slot's own pictures. Your profile's tracks take `invitational_image_4`, `_10`, `_13` to `_18` in order. That leaves room for at most eight enabled tracks; a ninth fails the build with `at most 8 tracks can be enabled at once`.

The build also fills gaps in the archive:

- For each variant world the package lacks (`classic`, `f2_previous`, ...), it copies the nearest world the package has as a stand-in.
- It points texture mip paths at `environment_package/tracks/<code>/mipmaps/<file>` and adds the files from the package's `mipmaps/` there.
- It gives every route of the AI racing line (`<code>_ai_track.aispline`) a change of passing space. F1 25 sorts each gate of a route into space on the left, on the right or on both sides from its white lines and the stock `ai_track_config.space_lines` settings, and crashes when an AI car drives a route without a single change, such as a pit lane drawn at one width. On such a route the build moves one white line at a gate no other route uses.
- It makes room for the F1 2026 grid if the built database's `f1_2026` grid list holds more cars than the `f1_26_myteam` world (with the common world) has grid slots, garages or pit boxes. The layer holding the last grid slot gets copies of the last slot, garage, in-garage spot and pit box. Each copy goes one row or bay beyond the two before it. Without two slots, garages or boxes to work out the spacing from, the build warns you that a Grand Prix there with that many cars crashes.

## The database

- **`tracks.Track_<n>`**: for a new code, `copy_slot` clones the slot track's row with a new `statId`. If the code already has a row (`bahrain24`), it keeps it. Your `track_overrides` apply here.
- **`tracks.track_resource.TrackResource_<n>`**: the track's resource row is cloned as `swofty_<code>`. On the natural route, its group fields use the 2019 template values with `[FILENAME]` = `<code>`: `iblAssetGroupsRoot` is `asset_groups/environment_package/tracks/<code>/probes/` and `iblKey` is `key_2019.assetgroup`. `trackMapAssetGroup` names the main group. On the slot route, every asset group points to the track's slot. The track row's `track_resource` names the cloned row.
- **Per-track tables** (per-race DNFs, pit lane splines and slide settings, practice programmes for 2025 and 2026, tyre wear and tyre wear scales, specialist objectives, track trivia, replay hero shots): if your code has no row, it gets a clone of the slot track's row, or the base track's when the slot has none. A new code always needs the per-race DNF row: the game reads it without checking that it exists.
- **`<datasets>.TrackDataSet_<i>.TrackData_<j>`**: `copy_from_track`, or `copy_slot` when the package has no `copy_from_track`, clones the base track's row and its sub-tables in every dataset that has it; for a new code, the copy is the dataset's last row. The copy's `_metadata_`, `track` and `TrackNameData_0.file_name` become the code. `track_data.game_setup.TrackDataSet[_metadata_=f1_2026].TrackData[_metadata_=<code>]` selects the copy in the `f1_2026` dataset. Your `overrides` apply here, limited to their `datasets`.
- **Content sets' `available_tracks`**: the code is added wherever the base track is listed, putting your track in the Grand Prix track list.
- **`track_data.sorted_track_order`**: gets a `TrackRef` row for the code.

Each clone copies all source-row keys listed in `track_tables.json`, plus any keys named by your manifest's overrides. The table's `_<row>_instancecount_` grows by one.

A database mod built from a whole game database sets the records it holds to its own copy's values. Where that is the game's own value of a record a track changes, the track's value stays, and a track list the mod rewrites keeps the tracks added to it.

Some sub-table names in `F1_25.exe` exist only as strings built on the stack. To collect every name the game-DB readers use, including those stack-built names, use `tools/track_keys.py`.

## Menus

- The build appends flags, previews and splines from `ui/images.json` as slices to the texture arrays named in `overrides.uidb`. Every UI archive carrying an array gets the same slices. Your track's `TrackTextures` rows then name `overrides.<list>.<code>`.
- The three track-select scenes and the session picture go into `invitational_image_2`. They're named in `ui_dynamic_texture_loading.uidta`.
- Your `ui/minimap.json` becomes an entry in `minimap.uidb`, in both `databases.erp` and `flow_race.erp`. Its textures load from the track's main group, or from its slot on the slot route.

## ERS

You won't find your manifest's `ers` values in the database. They go into per-track `TrackSettings` entries in the car handling files: every 2026 handling file gets an entry for your code, cloned from the base track's and given your manifest's values. Without an entry, the track still loads, but the cars use their default ERS settings.

An addon that changes a 2026 handling file gets the same entry in its copy, and so does every handling file with `TrackSettings` in a car archive an addon puts whole, such as a Classic Seasons season's cars. The manifest's `OverrideDetectionLapProgress` and `OverrideActivationLapProgress` set where overtake mode is detected and where it can be used, as fractions of the lap.

## Checking a build

```sh
swofty build
python3 tools/map_check.py <overlay folder> --game <game folder> --maps "$DATA/addons"
```

To check for missing keys too, put `F1_25.exe` in the game folder (or use `--exe`) and install `numpy`, `pefile` and `capstone`. Then `map_check.py` also checks that your track's row has every key the game reads on the source row: the slot track's Track row, its track resource, per-track rows and TrackData rows.

You can also run `cargo test -p swofty-core --test real_maps` to activate the Sepang and Okayama packages in the database from `F1_25_DIR` and check the row counts and cloned rows.
