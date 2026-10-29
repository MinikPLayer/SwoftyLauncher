---
title: Asset API
description: The asset op files in operations.assets, their sources, how each kind reaches the game, order and conflicts, what swofty_core does in game, and the native assets API.
---

# Asset API

An asset op is one change to the game, to an asset group, an archive resource, a database record, a language string, a loose file or a track, that an addon lists in `operations.assets` or returns from a [build hook](/reference/addons/launcher-api/build-reply).

The launcher's asset planner brings together every op in your profile, then delivers each kind by rewriting game files at build or through swofty_core while the game boots. The types are in the `swofty-addon` crate (`packages/addon-api`), module `swofty_addon::asset`. [Changes at runtime](/reference/addons/runtime-assets) walks through each kind; the ops are [operations](/reference/data/operations) like the rest.

## An addon's ops in full

```json [swofty.json]
{
  "schema_version": 1,
  "id": "example.livery",
  "name": "Scuderia AlphaTauri",
  "version": "1.0.0",
  "author": "Your Name",
  "compat": { "game": "f1_25" },
  "operations": {
    "assets": ["assets/livery.json"]
  }
}
```

```json [assets/livery.json]
{
  "schema_version": 1,
  "ops": [
    { "op": "erp.put-resource", "erp": "2025_asset_groups/f1_2026_vehicle_package/teams/toro_rosso/wep/toro_rosso.erp",
      "resource": "eaid://f1_2026_vehicle_package/teams/toro_rosso/wep/toro_rosso.wep", "source": "erp/car.erp" },
    { "op": "db.set", "select": "entrants.teams.team[_metadata_=toro_rosso_26]", "field": "display_name_code", "value": "AT" },
    { "op": "lang.set", "language": "*", "key": "lng_team_tor_hashtag", "value": "AlphaTauri" },
    { "op": "file.put", "source": "files/sponsor.dds", "target": "livery_package_shared/sponsor.dds" },
    { "op": "warn", "message": "Racing Bulls livery" }
  ]
}
```

`swofty build` prints what each addon writes, and who overrides whom:

```text
Scuderia AlphaTauri: 412 database values, 3 archive resources, 2 files, 5 text strings
Scuderia AlphaTauri overrides Example Livery on 12 database values and 1 file
```

## Op files

The prefix on an op names its kind. Every op, with its fields:

```jsonc
// group: an asset group in the engine's asset manifest
{ "op": "group.register", "group": "asset_groups/example_package/showcar.assetgroup",
  "erp": "2025_asset_groups/example_package/showcar.erp",   // or "erps": [...]
  "deps": ["..."], "template": "..." }                     // optional

// erp: archive resources
{ "op": "erp.put-resource", "erp": "<game archive>", "resource": "eaid://...", "source": "erp/car.erp",
  "as": "eaid://..." }            // copy a resource from an archive in the addon into a game archive, replacing or adding it; "as" optional
{ "op": "erp.replace-resource" }  // replace one fragment of a resource with a raw file
{ "op": "erp.add-resource" }      // add a resource from raw fragment files
{ "op": "erp.remove-resource" }   // remove a resource
{ "op": "erp.copy-resource" }     // copy a resource
{ "op": "erp.patch-pkg" }         // edit a !pkg resource in place
{ "op": "erp.set-material", "erp": "...", "resource": "...", "params": { "...": 0.5 } } // set constants of a .material resource
{ "op": "erp.overlay", "erps": ["<target archive>", "..."], "source": "erp/overlay.erp" }
                                  // put every resource in a ready archive into each target, as a put-resource of each would;
                                  // on the runtime route the archive loads as it is
{ "op": "erp.merge-slices", "erp": "...", "resource": "...", "source": "erp/array.erp" }
                                  // write the slices of a texture array that differ from the game's over the array in erp
{ "op": "erp.add-mipmaps", "source": "mips/", "target": "..." } // copy a mip file, or a folder of them, into the game folder

// db: database records
{ "op": "db.set" }                // change a field
{ "op": "db.add-row" }            // copy a row
{ "op": "db.list-append" }        // built
{ "op": "db.list-remove" }        // built
{ "op": "db.remove-row" }         // not built yet: a build with one fails
{ "op": "db.put-record", "key": "0xf01d03c7a1b2c3d4", "type": 0, "value": "01000000" }
                                  // one raw record: key is a path, or 0x and 16 hex digits; type is the tag; value is hex bytes
{ "op": "db.remove-record", "key": "...", "allow_stock_removal": false }
                                  // remove one record; a stock record stays unless allow_stock_removal is true

// lang: language strings
{ "op": "lang.set", "language": "*", "key": "lng_...", "value": "..." }  // one string; language is a code, or * for every language
{ "op": "lang.table", "language": "eng", "source": "lang/extra_eng.lng" } // every string in a table
{ "op": "lang.default", "language": "eng", "key": "lng_...", "value": "..." }
                                  // one string set before every lang.set and lang.table, which win over it

// file: loose game files
{ "op": "file.put", "source": "files/sponsor.dds", "target": "livery_package_shared/sponsor.dds" } // replace or add a file in the game folder

// no kind
{ "op": "track.add", "source": "tracks/my_track/" } // a folder holding an unpacked track package: activate it before every database op
{ "op": "item.show", "id": "...", "name": "...", "author": "...", "version": "...", "description": "..." }
                                  // a read-only tile in the in-game ADDON SETTINGS screen, such as one per mod a build hook merges
{ "op": "warn", "message": "..." } // print a message at build
```

- The `erp.` ops without fields above take the fields in [ERP operations](/reference/ops/erp). `erp.merge-slices` is in [Texture arrays](/reference/ops/erp#texture-arrays).
- The `db.` ops without fields above take the fields in [database operations](/reference/ops/database). `db.remove-record` and stock records: [Removing stock records](/reference/ops/database#removing-stock-records).
- `lang.default`: [In-game text](/reference/data/text). `track.add`: [A track with its own name](/reference/data/track-names).
- Files you list in `operations.database` and `operations.erp` load into the same model, with `db.` and `erp.` added to each op. Each `operations.files` entry becomes a `file.put`.
- `when` and <span v-pre>`{{id}}`</span> from your addon's [`db` settings](/reference/addons/settings#_3-read-a-db-setting) work in every op, as in database operations.
- Keep `patch-pkg` edits to `clone-entry` and `set-value`. Other edits aren't built: if your addon uses one, the build skips all its `erp` ops and prints a warning.

### Sources

`source` is a path in your addon's folder, or an object that names another root:

```jsonc
"source": "erp/car.erp"                                              // addon: $DATA/addons/<id>/
"source": { "root": "data", "path": "library/my-mod/files/car.erp" } // data:  $DATA/addon-data/<id>/
"source": { "root": "cache", "path": "built/car.erp" }               // cache: $DATA/cache/addons/<id>/
```

- Keep each path inside its folder. A path that leaves it fails the build.
- The build hash covers every op, plus the size and modification time of each source it names. For a folder source, that includes every file in the folder.

## Deliveries

The planner sends each kind by one of three routes:

```text
Build     the build rewrites the game file and installs it through the journal
Runtime   the build writes swofty/assets.json, and swofty_core applies it in memory while the game boots; game files stay stock
Waiting   no delivery exists for the kind; the build lists the op and its origin
```

The runtime features your installed loader offers are in `swofty/addons/core/features.json`:

```json
{ "schema": 1, "features": { "group": "stable", "erp": "stable", "db": "stable", "lang": "stable", "file": "experimental",
  "events": "stable", "storage": "stable", "overlay": "experimental", "telemetry": "experimental",
  "shaders": "experimental", "career": "stable", "popup": "stable", "postfx": "experimental" } }
```

Each kind's deliveries:

```text
kind    build delivery                                                     runtime feature   route
group   none                                                               stable            Runtime
erp     the archive is rewritten, unchanged resources copied byte for byte  stable            Runtime
db      gamedatabases.2025.bin is rewritten                                stable            Runtime
lang    none                                                               stable            Runtime
file    the file is copied into the game folder                            experimental      Build, or Runtime when turned on
```

The route rules:

- A `stable` kind takes the runtime route.
- To use the runtime route for an `experimental` kind, name it in `"runtime_assets"` in `$DATA/config.json`. The build then lists it in `features` in `swofty/assets.json`, which turns it on in swofty_core.
- To keep kinds with a build delivery (`erp`, `db`, `file`) on the build route even when they are `stable`, name them in `"build_assets"` in `$DATA/config.json`.

```json [$DATA/config.json]
{
  "runtime_assets": ["file"],
  "build_assets": ["erp", "db"]
}
```

- Otherwise a kind with a build delivery is built, and a kind without one waits:

```text
example.livery: 5 lang op(s) wait for lang support in swofty_core
example.livery: 2 file op(s) wait; the runtime file feature is experimental and "runtime_assets" in config.json does not turn it on
```

- A loader without `features.json` has no runtime features. `swofty build` without the loader builds `erp`, `db` and `file`, and lists `group` and `lang` as waiting.
- Every op of one kind takes the same route in a build. The exceptions are `erp` changes the in-game loader can't load, which go into the game files while the rest stay on the runtime route:

```text
erp: 2025_asset_groups/ui_package/flow_boot.erp built into the game files; 1 resource(s) change their stock layout, which the in-game loader cannot load, such as eaid://...: ...
erp: <archive> built into the game files; the in-game loader patches only archives under 2025_asset_groups
```

- A changed stock layout means a changed type, argument or fragment sizes of a stock resource, such as a resized scene.
- An `erp.remove-resource` puts the whole `erp` build into the game files: swofty_core cannot take a resource out of a stock archive.

```text
erp: built into the game files; 1 resource removal(s) cannot apply at runtime
```

- `erp.add-mipmaps` files go into the game folder through the journal on both routes, as a track's mip files do.
- An `erp.overlay` archive is listed in `resource_erps` as it is on the runtime route, without its resources being read or written again. If one of its resources would change a target's stock layout, or the `erp` build takes the build route, each resource in it goes in as an `erp.put-resource` would. The first case prints:

```text
erp: example.pack: its overlay archive goes in resource by resource; ...
```

- An addon that writes the same overlay each time, such as one made when a mod is imported, keeps the build hash the same between launches.
- A `db` build goes into the game file instead, with a warning, when it removes records, writes a record of tag 10, 15 or 16, writes one whose value is not text, or writes text swofty_core ignores. swofty_core ignores an int, colour or t12 that is not integer text (`1.5E+07`), an int outside the 32-bit range, a float that is not a finite 32-bit number, and a bool other than true or false. Empty text is the game's 0, 0.0 or false for those tags and applies.

## Order and conflicts

Your profile's ops apply in this order:

1. Tracks: every `track.add` op, from the Team Simplified library or any addon. Their database rows and archives come first.
2. Addons in your profile's load order.
3. Within an addon, its op files in `operations` order (`database`, then `erp`, then `assets`), then `operations.files`, then the ops its build hook returns.

A later write wins, so load order matters when addons write to the same target. The planner records these targets:

```text
an asset group               group.register
a resource in an archive     erp.put-resource, erp.replace-resource, erp.remove-resource, erp.add-resource,
                             erp.copy-resource, erp.patch-pkg, erp.set-material,
                             erp.overlay (each resource its archive holds, in each of its erps)
a database record            db.set, db.put-record, db.remove-record
a string in a language       lang.set
a game path                  file.put, erp.add-mipmaps (its target)
```

- A `file.put` of a whole archive writes every resource that ERP ops write in that archive. Ops from the addon that puts it, or from addons after it in load order, apply to the archive it puts. A put after them in load order replaces their changes.
- Replacing a target another origin already wrote is a conflict for `erp.put-resource`, `erp.replace-resource`, `erp.remove-resource`, `erp.overlay`, `erp.add-mipmaps`, `db.set`, `db.put-record`, `db.remove-record`, `lang.set` and `file.put`.
- `erp.add-resource`, `erp.copy-resource`, `erp.patch-pkg`, `erp.set-material`, `group.register` and added rows compose.

After its stages, the build prints a line per addon with what it writes, then a warning per pair of addons in conflict, the winner first:

```text
Scuderia AlphaTauri: 412 database values, 3 archive resources, 2 files, 5 text strings
Scuderia AlphaTauri overrides Example Livery on 12 database values and 1 file
```

A conflict between two items of one addon, from ops its build hook returns, prints the target and both origins, each an addon id with the item in brackets:

```text
conflict: text lng_team_tor_hashtag (eng): example.pack (alphatauri) overrides example.pack (base)
```

Each build also writes the pairs, with the overridden items, to `$DATA/cache/overlay/<profile>/conflicts.json` ([`swofty conflicts`](/reference/cli#swofty-conflicts)).

With a track that has menu pictures in the profile, the build keeps the track picture arrays, `minimap.uidb` and `overrides.uidb` it writes for the tracks. An `erp.put-resource`, `erp.replace-resource`, `erp.remove-resource`, `erp.copy-resource` or `erp.overlay` that replaces one of them is left out, while `erp.patch-pkg` edits still apply on top. The build warns, and lists the pair with the last of those tracks in load order as the winner:

```text
Example Cars ships its own track menu pictures; the launcher's track menus are kept
Test Ring overrides Example Cars on 4 archive resources
```

## In the game

The build's `swofty/assets.json` and its files are in the loader folder `swofty/` inside the game folder. Paths in the JSON are relative to `swofty/`:

```jsonc [swofty/assets.json]
{
  "asset_groups": [                 // group
    { "group": "asset_groups/example_package/showcar.assetgroup", "erps": ["2025_asset_groups/example_package/showcar.erp"],
      "deps": [], "template": "", "origin": "example.livery" }
  ],
  "resource_erps": [                // erp: one overlay archive per origin, from the last origin in load order to the first,
                                    // since a group reads them in that order and the first that holds a resource wins
    { "file": "assets/resources/0.erp", "erps": ["2025_asset_groups/ui_package/flow_boot.erp"], "origin": "example.livery" }
                                    // assets/resources/p<n>.erp for erp.overlay archives
  ],
  "db_edits": [                     // db: each record that differs from the database after track activation
    { "hash": "...", "type": "...", "value": "..." }
  ],
  "early_stage": true,
  "lang": {                         // lang: assets/lang/<code>.lng, and assets/lang/all.lng for every language
    "eng": { "table": "assets/lang/eng.lng" },
    "*": { "table": "assets/lang/all.lng" }
  },
  "overlay": true,                  // file: files under overlay/<target>
  "features": ["file"]
}
```

swofty_core applies each kind at a fixed point as the game boots:

```text
group   assets.cpp      when the engine parses its asset manifest; a group the stock manifest already has is skipped
erp     resources.cpp   when the engine parses its asset manifest: every group listing a target archive reads the overlay archive first
db      gamedb.cpp      when the game database loads (Gdb_Load_GameDatabase)
lang    overlay.cpp     when the game loads a language file (Loc_Load_LanguageFile), as a table added after the stock table
file    overlay.cpp     each time the game resolves a path under swofty/overlay/
```

### The early stage

The game builds its database before the window opens. To get swofty_core in before the database loads, `"early_stage": true` makes the loader load it inside the game's `CreateMutexW("Codemasters F1 25")` call:

```text
[14:13:47.300] trigger: KERNEL32!CreateMutexW from 0x140178667 (.xtls) name 'Codemasters F1 25' on thread 29952, 120 ms after attach; loading addons now (early stage)
```

- swofty_core still runs its window-stage handlers at the window trigger.
- The build sets `early_stage` whenever there are database edits. `SWOFTY_EARLY_STAGE=1` or `0` overrides it.
- `"early": true` on an `operations.native` entry loads that plugin with swofty_core at the early stage. The build lists these plugins in `early_plugins` in `swofty/settings.json`; a nonempty list turns the early stage on.
- Every other plugin loads at the window trigger, with a second mixin commit. A window-stage mixin that overlaps one installed at the early stage drops its addon.
- Without the early stage, addons load after the database is built. Then only in-place int, float and bool overwrites of existing keys apply. New keys, strings and type changes are skipped and logged.

### Experimental features

An experimental feature applies only when `features` in `swofty/assets.json` lists it. Otherwise swofty_core skips it and logs:

```text
[assets] overlay skipped: the runtime file feature is experimental and "features" in swofty/assets.json does not turn it on
```

### Logs

`swofty/loader.log` has every step of your runtime assets, with the `[assets]` prefix: each group, each resource with its fragments and stock archives, each database edit (`overwrote`, `created`, `retyped` or `skipped` with the reason), each string table and each overlaid file. Without `swofty/assets.json`, swofty_core logs nothing and installs no hooks.

```text
[assets] gamedb: 412 overwritten, 0 created, 0 retyped, 0 skipped; ...
[assets] gamedb: entrants.teams.team.x skipped: a new key needs "type"
```

## Native plugins

A native plugin uses the `assets` table added in `SwoftyApi` version 4, wrapped by the SDK's `swofty/assets.hpp`. Your calls feed the same lists in swofty_core as `swofty/assets.json`. Entries from `assets.json` apply first, then calls in plugin load order. [From a native plugin](./runtime-assets#from-a-native-plugin) has an example.

```cpp
#include <swofty/assets.hpp>

namespace swofty::assets {

enum class Result { ok, late, unavailable, invalid };
struct Fragment {
    std::string_view file;
    std::string_view fourcc = {};   // empty: the stock resource's value
    uint32_t flags = 0;             // 0: the stock resource's value
};

bool available(std::string_view feature);   // group, erp, db, lang or file

// kind group, as group.register; accepted until the engine parses its asset manifest
Result register_group(std::string_view group, std::span<const std::string_view> erps,
                      std::span<const std::string_view> deps = {}, std::string_view templ = {});

// kind erp, as a resources entry: the core builds the overlay archive; accepted until swofty_core
// writes the resource overlay, at the end of plugin init. erps empty: the core finds the archives that hold the uri
Result put_resource(std::string_view uri, std::span<const Fragment> fragments,
                    std::span<const std::string_view> erps = {});

// kind db, as a db_edits entry; accepted until the game database loads (only "early": true plugins load before it)
Result db_set(std::string_view key, std::string_view type, std::string_view value);
Result db_set(uint64_t hash, uint8_t tag, std::span<const std::byte> value);

// kind lang, as lang.set and lang.table; accepted until the game loads its first language file
Result lang_set(std::string_view language, std::string_view key, std::string_view text);
Result lang_table(std::string_view language, std::string_view file);

// kind file, as file.put; accepted until the game first reads that path
Result overlay_file(std::string_view game_path, std::string_view file);

const char *name(Result r);         // "ok", "late", "unavailable" or "invalid"

}
```

| Result | Meaning |
|---|---|
| `ok` | The entry joined the list |
| `late` | The call came after its cutoff |
| `unavailable` | swofty_core lacks the feature, or it is experimental and `features` in `swofty/assets.json` does not turn it on, or the core offers an API version below 4 |
| `invalid` | An argument is empty or malformed, or a file is missing |

- Plugin init includes every plugin's `SwoftyAddonInit` and `SWOFTY_ON_LOAD`. The core waits until the last of them finishes before reading the lists.
- After plugin init, `register_group`, `db_set`, `lang_set`, `lang_table` and `overlay_file` join the core's lists directly, provided their cutoff hasn't passed and that kind already has something to apply. Otherwise they return `late`. `put_resource` returns `late`.
- At the window stage the game database is already built, so `db_set` returns `late`.
- To change strings after the language file loads, use [string overrides](/reference/std/strings) instead of `lang_set`.
- `db_set` takes a key path, or `0x` and 16 hex digits. `type` is a `db_edits` type name (`int`, `float`, `string`, `enum`, `id`, `bool`, ...) or a tag number. The raw form takes the hash and the tag, and its value bytes are the record's text.
- `put_resource` fragments are files with `fourcc` and `flags` as in a `resources` entry.
- File paths are relative to your addon's folder, `swofty/addons/<id>/`. To have the build copy a file there, list it or its folder in `files` on your plugin's `operations.native` entry ([Files in the game folder](/reference/addons/native-plugins#files-in-the-game-folder)). A path that leaves the folder, or a missing file, returns `invalid`.
- The core identifies your addon from the calling DLL through `dlls` in `swofty/settings.json`. If no addon lists that DLL, every call that takes a file returns `invalid`.

Every call is in the log, with its addon id and DLL:

```text
[assets] api: example.livery (livery.dll) register_group asset_groups/example_package/showcar.assetgroup -> ok
[assets] api: example.livery (livery.dll) db_set entrants.teams.team.x = 1 -> late: the core loaded at the window stage, after the game database loaded
```

A plugin that calls the API names itself in `operations.native`. `"early": true` loads it at the early stage:

```json [swofty.json]
"operations": {
  "native": [{ "dll": "native/livery.dll", "early": true }]
}
```
