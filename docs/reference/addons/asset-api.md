---
title: Asset API
description: The asset op files in operations.assets, their sources, how each kind reaches the game, order and conflicts, what swofty_core does in game, and the native assets API.
---

# Asset API

This is the reference for asset ops: the [operations](/reference/data/operations) an addon lists in `operations.assets` or returns from a [build hook](/reference/addons/launcher-api#the-build-reply). Each op makes one change: to an asset group, an archive resource, a database record, a language string, a loose file or a track. The launcher's asset planner brings together every op in your profile, then delivers each kind by rewriting game files at build or through swofty_core while the game boots.

You'll find the types in the `swofty-addon` crate (`packages/addon-api`), module `swofty_addon::asset`. For a walkthrough of each kind, see [Changes at runtime](/reference/addons/runtime-assets).

## Op files

List your op files under `operations.assets` in `swofty.json`:

```json [swofty.json]
"operations": {
  "assets": ["assets/livery.json"]
}
```

Inside each file, the prefix on an op names its kind:

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

| Op | Fields | Change |
|---|---|---|
| `group.register` | `group`, `erp` or `erps`, optional `deps`, `template` | Add an asset group to the engine's asset manifest |
| `erp.put-resource` | `erp`, `resource`, `source`, optional `as` | Copy a resource from an archive in the addon into a game archive, replacing or adding it |
| `erp.replace-resource`, `erp.add-resource`, `erp.remove-resource` | As in [ERP operations](/reference/ops/erp) | Replace one fragment of a resource with a raw file, add a resource from raw fragment files, or remove a resource |
| `erp.copy-resource`, `erp.patch-pkg` | As in [ERP operations](/reference/ops/erp) | Copy a resource, or edit a `!pkg` resource in place |
| `erp.overlay` | `erps` (target archives), `source` (a ready overlay archive) | Put every resource in a ready archive into each target, as a `put-resource` of each would. On the runtime route the archive loads as it is |
| `erp.merge-slices` | `erp`, `resource`, `source` (an archive in the addon) | Write the slices of a texture array that differ from the game's over the array in `erp` ([Texture arrays](/reference/ops/erp#texture-arrays)) |
| `erp.add-mipmaps` | `source`, `target` | Copy a mip file, or a folder of them, into the game folder |
| `db.set`, `db.add-row` | As in [database operations](/reference/ops/database) | Change a field, or copy a row |
| `db.list-append`, `db.list-remove` | As in [database operations](/reference/ops/database) | Built |
| `db.remove-row` | As in [database operations](/reference/ops/database) | Not built yet: a build with one fails |
| `db.put-record` | `key` (a path, or `0x` and 16 hex digits), `type` (the tag), `value` (hex bytes) | Write one raw record |
| `db.remove-record` | `key`, optional `allow_stock_removal` | Remove one record. A stock record stays unless `allow_stock_removal` is `true` ([Removing stock records](/reference/ops/database#removing-stock-records)) |
| `lang.set` | `language` (a code, or `*` for every language), `key`, `value` | Set one string |
| `lang.table` | `language`, `source` (an `.lng` file) | Set every string in a table |
| `lang.default` | `language`, `key`, `value` | Set one string before every `lang.set` and `lang.table`, which win over it ([In-game text](/reference/data/text)) |
| `file.put` | `source`, `target` (a path in the game folder) | Replace or add a loose game file |
| `track.add` | `source` (a folder holding an unpacked track package) | Activate a track before every database op ([A track with its own name](/reference/data/track-names)) |
| `item.show` | `id`, `name`, optional `author`, `version`, `description` | Show a read-only tile in the in-game **ADDON SETTINGS** screen, such as one per mod a build hook merges |
| `warn` | `message` | Print a message at build |

- Files you list in `operations.database` and `operations.erp` load into the same model, with `db.` and `erp.` added to each op. Each `operations.files` entry becomes a `file.put`.
- You can use `when` and <span v-pre>`{{id}}`</span> from your addon's [`db` settings](/reference/addons/settings#_3-read-a-db-setting) in every op, just as you would in database operations.
- Keep `patch-pkg` edits to `clone-entry` and `set-value`. Other edits aren't built: if your addon uses one, the build skips all its `erp` ops and prints a warning.

### Sources

Use a path in your addon's folder for `source`, or an object to name another root:

```json
"source": "erp/car.erp"
"source": { "root": "data", "path": "library/my-mod/files/car.erp" }
```

| `root` | Folder |
|---|---|
| `addon` (a plain string) | `$DATA/addons/<id>/` |
| `data` | `$DATA/addon-data/<id>/` |
| `cache` | `$DATA/cache/addons/<id>/` |

Keep each path inside its folder; a path that leaves it fails the build. The build hash covers every op, plus the size and modification time of each source it names. For a folder source, that includes every file in the folder.

## Deliveries

The planner chooses one of three routes for each kind:

| Route | Meaning |
|---|---|
| Build | The build rewrites the game file and installs it through the journal |
| Runtime | The build writes `swofty/assets.json`, and swofty_core applies it in memory while the game boots. Game files stay stock. |
| Waiting | No delivery exists for the kind. The build lists the op and its origin |

To see which runtime features your installed loader offers, look in `swofty/addons/core/features.json`:

```json
{ "schema": 1, "features": { "group": "stable", "erp": "stable", "db": "stable", "lang": "stable", "file": "experimental" } }
```

| Kind | Build delivery | Runtime feature | Route |
|---|---|---|---|
| `group` | none | stable | Runtime |
| `erp` | the archive is rewritten, unchanged resources copied byte for byte | stable | Runtime |
| `db` | `gamedatabases.2025.bin` is rewritten | stable | Runtime |
| `lang` | none | stable | Runtime |
| `file` | the file is copied into the game folder | experimental | Build, or Runtime when turned on |

- A `stable` kind takes the runtime route.
- To use the runtime route for an `experimental` kind, name it in `"runtime_assets"` in `$DATA/config.json`, for example `"runtime_assets": ["file"]`. The build then lists it in `features` in `swofty/assets.json`, which turns it on in swofty_core.
- To keep kinds with a build delivery (`erp`, `db`, `file`) on the build route even when they are `stable`, name them in `"build_assets"` in `$DATA/config.json`, for example `"build_assets": ["erp", "db"]`.
- Otherwise a kind with a build delivery is built, and a kind without one waits. The build prints `<origin>: <n> lang op(s) wait for lang support in swofty_core`, or says the feature is experimental and not turned on.
- A loader without `features.json` has no runtime features. If you run `swofty build` without the loader, it builds `erp`, `db` and `file` and lists `group` and `lang` as waiting.
- Every op of one kind takes the same route in a build, with one exception: an archive whose `erp` changes alter a stock resource's type, argument or fragment sizes, such as a resized scene, is rewritten into the game files while the rest stay on the runtime route. The build prints `erp: <archive> built into the game files; <n> resource(s) change their stock layout, ...`.
- If your `erp` build includes an `erp.remove-resource`, it goes into the game files: swofty_core cannot take a resource out of a stock archive. The build prints `erp: built into the game files; <n> resource removal(s) cannot apply at runtime`.
- `erp.add-mipmaps` files go into the game folder through the journal on both routes, as a track's mip files do.
- An `erp.overlay` archive is listed in `resource_erps` as it is on the runtime route, without its resources being read or written again. If one of its resources would change a target's stock layout, or the `erp` build takes the build route, each resource in it goes in as an `erp.put-resource` would, and the build prints `erp: <origin>: its overlay archive goes in resource by resource; ...` for the first case. An addon that writes the same overlay each time, such as one made when a mod is imported, keeps the build hash the same between launches.
- If your `db` build removes records, writes a record of tag 10, 15 or 16, writes one whose value is not text, or writes text swofty_core ignores, it goes into the game file instead, with a warning. swofty_core ignores an int, colour or t12 that is not integer text (`1.5E+07`), an int outside the 32-bit range, a float that is not a finite 32-bit number, and a bool other than true or false. Empty text is the game's 0, 0.0 or false for those tags and applies.

## Order and conflicts

Your profile's ops apply in this order:

1. Tracks: every `track.add` op, from the Team Simplified library or any addon. Their database rows and archives come first.
2. Addons in your profile's load order.
3. Within an addon, its op files in `operations` order (`database`, then `erp`, then `assets`), then `operations.files`, then the ops its build hook returns.

A later write wins, so load order matters when addons write to the same target. The planner records these targets:

| Target | Written by |
|---|---|
| An asset group | `group.register` |
| A resource in an archive | `erp.put-resource`, `erp.replace-resource`, `erp.remove-resource`, `erp.add-resource`, `erp.copy-resource`, `erp.patch-pkg`, `erp.overlay` (each resource its archive holds, in each of its `erps`) |
| A database record | `db.set`, `db.put-record`, `db.remove-record` |
| A string in a language | `lang.set` |
| A game path | `file.put`, `erp.add-mipmaps` (its `target`) |

A `file.put` of a whole archive writes every resource that ERP ops write in that archive. Ops from the addon that puts it, or from addons after it in load order, apply to the archive it puts. A put after them in load order replaces their changes.

If one origin has already written to a target, replacing it counts as a conflict. That applies to `erp.put-resource`, `erp.replace-resource`, `erp.remove-resource`, `erp.overlay`, `erp.add-mipmaps`, `db.set`, `db.put-record`, `db.remove-record`, `lang.set` and `file.put`. By contrast, `erp.add-resource`, `erp.copy-resource`, `erp.patch-pkg`, `group.register` and added rows compose. After its stages, the build prints a line per addon with what it writes, then a warning per pair of addons in conflict, the winner first:

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

You'll find the build's `swofty/assets.json` and its files in the loader folder `swofty/` inside the game folder. Paths in that JSON are relative to `swofty/`.

| Kind | `swofty/assets.json` | Files |
|---|---|---|
| `group` | `asset_groups`: `group`, `erps`, `deps`, `template`, `origin` | |
| `erp` | `resource_erps`: one overlay archive per origin, as `file`, target `erps` and `origin`, listed from the last origin in load order to the first, since a group reads them in that order and the first that holds a resource wins | `assets/resources/<n>.erp`, and `assets/resources/p<n>.erp` for `erp.overlay` archives |
| `db` | `db_edits`: each record that differs from the database after track activation, as `hash`, `type` and `value`, and `"early_stage": true` | |
| `lang` | `lang.<code>.table`, and `lang."*".table` for every language | `assets/lang/<code>.lng`, `assets/lang/all.lng` |
| `file` | `"overlay": true`, and `"file"` in `features` | `overlay/<target>` |

swofty_core applies each kind at a fixed point as the game boots:

| Kind | Applied | Core part |
|---|---|---|
| `group` | when the engine parses its asset manifest. A group the stock manifest already has is skipped. | `assets.cpp` |
| `erp` | when the engine parses its asset manifest: every group listing a target archive reads the overlay archive first | `resources.cpp` |
| `db` | when the game database loads (`Gdb_Load_GameDatabase`) | `gamedb.cpp` |
| `lang` | when the game loads a language file (`Loc_Load_LanguageFile`), as a table added after the stock table | `overlay.cpp` |
| `file` | each time the game resolves a path under `swofty/overlay/` | `overlay.cpp` |

### The early stage

The game builds its database before the window opens. To get swofty_core in before the database loads, `"early_stage": true` makes the loader load it inside the game's `CreateMutexW("Codemasters F1 25")` call. swofty_core still runs its window-stage handlers at the window trigger. The build sets `early_stage` whenever there are database edits; you can override it with `SWOFTY_EARLY_STAGE=1` or `0`.

- To load your plugin with swofty_core at the early stage, set `"early": true` on its `operations.native` entry. The build lists these plugins in `early_plugins` in `swofty/settings.json`; a nonempty list turns the early stage on.
- Every other plugin loads at the window trigger, with a second mixin commit. A window-stage mixin that overlaps one installed at the early stage drops its addon.

Without the early stage, addons load after the database is built. At that point, only in-place int, float and bool overwrites of existing keys apply. New keys, strings and type changes are skipped and logged.

### Experimental features

For an experimental feature to apply, `features` in `swofty/assets.json` must list it. Otherwise swofty_core skips it and logs:

```text
[assets] overlay skipped: the runtime file feature is experimental and "features" in swofty/assets.json does not turn it on
```

### Logs

To see what happened to your runtime assets, read `swofty/loader.log`. swofty_core logs every step with the `[assets]` prefix: each group, each resource with its fragments and stock archives, each database edit (`overwrote`, `created`, `retyped` or `skipped` with the reason), each string table and each overlaid file. Without `swofty/assets.json`, it logs nothing and installs no hooks.

## Native plugins

From a native plugin, you use the `assets` table added in `SwoftyApi` version 4, wrapped by the SDK's `swofty/assets.hpp`. Your calls feed the same lists in swofty_core as `swofty/assets.json`. Entries from `assets.json` apply first, followed by calls in plugin load order. See [From a native plugin](./runtime-assets#from-a-native-plugin) for an example.

```cpp
namespace swofty::assets {

enum class Result { ok, late, unavailable, invalid };
struct Fragment {
    std::string_view file;
    std::string_view fourcc = {};
    uint32_t flags = 0;
};

bool available(std::string_view feature);
Result register_group(std::string_view group, std::span<const std::string_view> erps,
                      std::span<const std::string_view> deps = {}, std::string_view templ = {});
Result put_resource(std::string_view uri, std::span<const Fragment> fragments,
                    std::span<const std::string_view> erps = {});
Result db_set(std::string_view key, std::string_view type, std::string_view value);
Result db_set(uint64_t hash, uint8_t tag, std::span<const std::byte> value);
Result lang_set(std::string_view language, std::string_view key, std::string_view text);
Result lang_table(std::string_view language, std::string_view file);
Result overlay_file(std::string_view game_path, std::string_view file);

}
```

| Call | Kind | Same as | Accepted until |
|---|---|---|---|
| `register_group` | `group` | `group.register` | the engine parses its asset manifest |
| `put_resource` | `erp` | a `resources` entry: the core builds the overlay archive | swofty_core writes the resource overlay, at the end of plugin init |
| `db_set` | `db` | a `db_edits` entry | the game database loads. Only plugins with `"early": true` load before it |
| `lang_set`, `lang_table` | `lang` | `lang.set`, `lang.table` | the game loads its first language file |
| `overlay_file` | `file` | `file.put` | the game first reads that path |

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
- `available(feature)` takes `group`, `erp`, `db`, `lang` or `file`.
- `db_set` takes a key path, or `0x` and 16 hex digits. `type` is a `db_edits` type name (`int`, `float`, `string`, `enum`, `id`, `bool`, ...) or a tag number. The raw form takes the hash and the tag, and its value bytes are the record's text.
- `put_resource` fragments are files with `fourcc` and `flags` as in a `resources` entry; empty and 0 take the stock resource's values. Use `erps` to name the target archives, or leave it out for the core to find the archives that hold the uri.
- Give file paths relative to your addon's folder, `swofty/addons/<id>/`. To have the build copy a file there, list it or its folder in `files` on your plugin's `operations.native` entry. See [Files in the game folder](/reference/addons/native-plugins#files-in-the-game-folder). A path that leaves the folder, or a missing file, returns `invalid`.
- The core identifies your addon from the calling DLL through `dlls` in `swofty/settings.json`. If no addon lists that DLL, every call that takes a file returns `invalid`.
- You can trace every call in the log by its addon id and DLL:

```text
[assets] api: example.livery (livery.dll) register_group asset_groups/example_package/showcar.assetgroup -> ok
[assets] api: example.livery (livery.dll) db_set entrants.teams.team.x = 1 -> late: the core loaded at the window stage, after the game database loaded
```

A plugin that calls the API names itself in `operations.native`. Add `"early": true` to load it at the early stage:

```json [swofty.json]
"operations": {
  "native": [{ "dll": "native/livery.dll", "early": true }]
}
```
