---
title: Changes at runtime
description: Replace a texture, add strings, edit the game database and register an asset group from an addon, with swofty_core applying each change in memory while the game boots.
---

# Changes at runtime

An addon's [asset ops](./asset-api) of the kinds `group`, `erp`, `db` and `lang` don't rewrite the game's files. The build writes them to `swofty/assets.json`, and swofty_core applies them in memory while the game boots, so the game folder keeps its stock files. This is the runtime route; [Deliveries](./asset-api#deliveries) lists which kinds take it. This page shows each kind on that route: a texture, strings, database records and an asset group, written in op files, from an engine plugin, or from a launcher plugin's build hook.

Each example below goes in its own op file, listed in `operations.assets`:

```json [swofty.json]
{
  "schema_version": 1,
  "id": "example.runtime",
  "name": "Runtime example",
  "version": "1.0.0",
  "author": "Your Name",
  "compat": { "game": "f1_25" },
  "operations": {
    "assets": ["assets/texture.json", "assets/text.json", "assets/database.json", "assets/group.json"]
  }
}
```

Install and enable the addon, then build the profile to check what it planned:

```sh
swofty addons install example-runtime
swofty addons enable example.runtime
swofty build
```

Then start the game. You'll find the runtime log entries under `[assets]` in `swofty/loader.log`.

## Replace a texture

First, put your new texture in an `.erp` archive in the addon, using the stock texture's resource name. Then use `erp.put-resource` to copy it over the stock resource:

```json [assets/texture.json]
{
  "schema_version": 1,
  "ops": [
    { "op": "erp.put-resource",
      "erp": "2025_asset_groups/ui_package/flow_boot.erp",
      "resource": "eaid://ui_package/art/textures/press_start/ps_logo.tif.image",
      "source": "erp/logo.erp" }
  ]
}
```

- Set `erp` to the game archive that holds the resource. To find its resource names, run `python3 tools/erp.py <archive> '*'` ([Textures and models](/reference/data/textures)).
- Set `source` to the archive in your addon that holds the resource. Use `"as"` to give it a different name in the game archive. If that name doesn't exist there, the resource is added.
- The build puts your changed resources in an overlay archive under `swofty/assets/resources/` and lists it in `resource_erps`. Every asset group that lists `flow_boot.erp` reads this overlay archive first.
- Keep the stock resource's type, argument and fragment layout and sizes when replacing it. If your resource changes them, the build leaves it out and reports a warning.
- `erp.replace-resource` and `erp.add-resource` use the overlay archive too. If your build includes an `erp.remove-resource`, it rewrites the game archives instead.

```text
[assets] resources: overlay ERP swofty/assets/resources/0.erp prebuilt by the launcher (... bytes, 1 resource(s): 1 replaced, 0 added) for 1 target ERP(s): 2025_asset_groups/ui_package/flow_boot.erp
```

## Add or change strings

Use `lang.set` to set one string. Choose `"*"` to set it in every language, or a code such as `eng` to set it in just that language:

```json [assets/text.json]
{
  "schema_version": 1,
  "ops": [
    { "op": "lang.set", "language": "*", "key": "lng_team_tor_hashtag", "value": "AlphaTauri" },
    { "op": "lang.set", "language": "eng", "key": "lng_example_hint", "value": "Runtime example loaded" },
    { "op": "lang.table", "language": "eng", "source": "lang/extra_eng.lng" }
  ]
}
```

- To load a whole `.lng` file from your addon, use `lang.table`. You can write that file with `swofty_formats::lng`.
- You can also supply a key the game doesn't have. It gets added, so a database field such as `name_loc_tag` can refer to it ([In-game text](/reference/data/text)).
- The build merges every addon's strings into one table per language under `swofty/assets/lang/`. When the game loads a language, the core adds that table after the stock table. That's why your strings win over the stock ones.

```text
[assets] lang: added 2 string(s) from swofty/assets/lang/eng.lng for 'eng' at priority ...
```

## Edit the database

To edit the database, use the same form as in [database operations](/reference/ops/database), adding the `db.` prefix:

```json [assets/database.json]
{
  "schema_version": 1,
  "ops": [
    { "op": "db.set", "select": "entrants.teams.team[_metadata_=toro_rosso_26]", "field": "display_name_code", "value": "AT" },
    { "op": "db.put-record", "key": "0xf01d03c7a1b2c3d4", "type": 0, "value": "01000000" }
  ]
}
```

- Use `db.put-record` to write one raw record by path or hash. Set `type` to its tag and `value` to its bytes in hex.
- The build applies every op to a copy of the database, then writes each record that differs from stock to `db_edits` and sets `early_stage`. This leaves the game's database file stock.
- The early stage gets your addons loaded before the game builds its database, so the core can apply every edit as the database loads.

```text
[assets] gamedb: 2 overwritten, 0 created, 0 retyped, 0 skipped; ...
```

If your build removes records, writes a record of tag 10, 15 or 16, or writes one whose value is not text, it writes the database file instead and reports a warning.

## Register an asset group

The engine loads content by asset group, so you use `group.register` to add a group to its asset manifest. The group points at archives in the game folder:

```json [assets/group.json]
{
  "schema_version": 1,
  "ops": [
    { "op": "file.put", "source": "erp/showcar.erp", "target": "2025_asset_groups/example_package/showcar.erp" },
    { "op": "group.register",
      "group": "asset_groups/example_package/showcar.assetgroup",
      "erp": "2025_asset_groups/example_package/showcar.erp" }
  ]
}
```

- Ship the archive with `file.put`. Loose files are copied into the game folder at build, or served from `swofty/overlay/` if your profile turns on the experimental `file` feature.
- Use `erps` to list several archives, `deps` to name groups that must load first, and `template` to name a stock group to copy settings from.
- The core skips groups already in the stock manifest. If a database field points at a stock group whose archive is missing, the session load hangs.
- Refer to the group through database fields such as a track's `worldAssetGroup`. Tracks use this route: each track's archive goes to `2025_asset_groups/environment_package/tracks/<code>/<code>.erp`, with one group per world, bake and probe set ([How tracks are installed](/reference/installing-tracks)).

### Driver heads

A `file.put` of `2025_asset_groups/animation_package/scenes/cinematics/drivers/heads/<label>.erp` adds a driver head when no `entrants.animation.driver_heads.driver_head` row has the label `<label>`, in the stock database or from an addon's `add-row`. The build then adds, with your addon as the origin:

- an `add-row` copied from `male_ls222`, or from `female_ls112` for a female head, that sets `_metadata_` to `<label>`, `model_path` and `model_path_female_rig` from the head model in your archive, `has_morph_data` to whether you put `morph_poses/<label>.erp`, `available_for_customisation` to `False`, and a new `stat_id`;
- a `group.register` for each archive you put among `heads/<label>.erp`, `heads/<label>_malerig.erp`, `morph_poses/<label>.erp` and `morph_poses/<label>_malerig.erp`, with `lando_norris`'s groups as templates, or `female_ls493`'s for a female head.

A head is female when its model sits under `heads/female_*` or you put `heads/<label>_malerig.erp`. The male-rig archive's model becomes `model_path`, and without one `model_path` is empty. An archive without a head model gets no row, and the build warns. Add your own row with `add-row` to set other values; the build then adds no row and no groups for that head.

A driver or character whose `head` an addon changes to a label with no driver head row keeps the game's head, and the build names the addon in a warning.

Helpers list the heads that the profile's addons put with the `profile_heads` host call, from launcher 1.5.12.

## From an engine plugin {#from-a-native-plugin}

You can add the same entries from an [engine plugin](./native-plugins) at plugin init, using `swofty/assets.hpp`. File paths refer to files in your addon's folder, `swofty/addons/<id>/`:

```cpp [native/src/livery.cpp]
#include <swofty/assets.hpp>
#include <swofty/mixin.hpp>

namespace assets = swofty::assets;

SWOFTY_ON_LOAD
{
    std::string_view erps[] = {"2025_asset_groups/ui_package/flow_boot.erp"};
    assets::Fragment logo[] = {{"files/ps_logo.bin"}};
    assets::put_resource("eaid://ui_package/art/textures/press_start/ps_logo.tif.image", logo, erps);
    assets::lang_set("*", "lng_team_tor_hashtag", "AlphaTauri");
    if (assets::db_set("0xf01d03c7a1b2c3d4", "int", "1") == assets::Result::late)
        swofty::log("[livery] the database was already built");
}

SWOFTY_ADDON()
```

```json [swofty.json]
"operations": {
  "native": [{ "dll": "native/livery.dll", "early": true }]
}
```

- Set `"early": true` to load your plugin before the game database, so `db_set` applies. Without it, your plugin loads at the window trigger and `db_set` returns `late`.
- Check each call's result: `ok`, `late`, `unavailable` or `invalid`. You'll also find one `[assets] api:` line per call in `swofty/loader.log`. See [Asset API](./asset-api#native-plugins) for when each call is accepted.

## From a build hook

You can return the same ops from a [launcher plugin's `build` reply](./launcher-api#the-build-reply), or from `Addon::build` in a built-in addon. They run after your addon's declared ops and take the same routes described above.

Team Simplified uses this to merge `.serps` mods. For each enabled mod, it returns `db.put-record`, `db.remove-record`, `erp.put-resource`, `erp.remove-resource` and `lang.set`, with sources in its `data` root.

## Reference

- [Asset API](./asset-api): every op kind, sources, deliveries, order and conflicts, and the native calls.
- [Operations](/reference/data/operations): every `operations` field and the route each one takes.
- [Database operations](/reference/ops/database): the selector and value forms that `db.*` ops share.
- [How tracks are installed](/reference/installing-tracks): a track delivered through asset groups and database edits.
