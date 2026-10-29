---
title: Textures and models
description: Change art, models, worlds and menus from a data addon — list an archive's resources, then replace a fragment, copy a resource between archives, add mip files and patch a !pkg layer with ERP operations.
---

# Textures and models

F1 25's art, models, worlds and menus live in `.erp` archives under `2025_asset_groups/`, which a data addon changes with [ERP operations](/reference/ops/erp).

## In full

```json [swofty.json]
"operations": {
  "erp": ["erp/texture.json"]
}
```

```jsonc [erp/texture.json]
{
  "schema_version": 1,
  "ops": [
    // swap fragment 0 of a texture for a raw file: the bytes after decompression
    { "op": "replace-resource", "erp": "2025_asset_groups/f1_safetycar_package/safety_car_2/wep/safety_car_2.erp",
      "resource": "eaid://f1_safetycar_package/safety_car_2/textures/safety_car_2_decal_da.tif.image",
      "fragment": 0, "source": "erp/decal.bin" },
    // its loose mip chain
    { "op": "add-mipmaps", "source": "mipmaps/", "target": "f1_safetycar_package/safety_car_2/textures/" },
    // copy a whole car from an archive in the addon under a new name
    { "op": "copy-resource", "source": "erp/car.erp",
      "erp": "2025_asset_groups/f1_2026_vehicle_package/teams/toro_rosso/wep/toro_rosso_barcelona_26.erp",
      "resource": "eaid://f1_2026_vehicle_package/teams/toro_rosso/wep/toro_rosso.wep",
      "as": "eaid://f1_2026_vehicle_package/teams/toro_rosso/wep/toro_rosso_barcelona_26.wep" },
    { "op": "warn", "message": "safety car decal and the Barcelona Racing Bulls car" }
  ]
}
```

```text
runtime: swofty/assets.json with 0 asset group(s), 2 resource(s), 0 database edit(s), 0 language table(s), 0 overlay file(s)
```

Inside each archive are named resources, each with one or more fragments. With the in-game loader, the build delivers changed and added resources as overlay archives under `swofty/assets/resources/`, and the stock archives stay in the game folder. Without it, the build writes a new copy of each archive your operations touch. See [Build route and runtime route](./operations#build-route-and-runtime-route). A [data addon](./) lists the file in `operations.erp`.

## Look inside an archive

Start by finding the resource you want to change. Use `tools/erp.py` to list what's in an archive:

```sh
python3 tools/erp.py "2025_asset_groups/f1_safetycar_package/safety_car_2/wep/safety_car_2.erp" '*'
```

Each line gives you the resource type and name, followed by one tuple per fragment. Read each tuple as fourcc, decompressed size, packed size, flags, compression:

```text
GfxSurfaceRes eaid://f1_safetycar_package/safety_car_2/textures/safety_car_2_decal_da.tif.image [('temp', 349528, 174763, '0x10', 17), ('temp', 73, 73, '0x10', 145)]
```

Here, compression `17` means zstd (`0x11`), and `145` means stored (`0x91`). For your operation's `resource` field, copy the whole resource name, including the `eaid://` prefix. To inspect the bytes, extract the resource's first fragment with `tools/erpx.py`; it decompresses the fragment for you:

```sh
python3 tools/erpx.py "2025_asset_groups/f1_safetycar_package/safety_car_2/wep/safety_car_2.erp" safety_car_2_decal_da out.bin
```

For the file format, see [ERP archives](/reference/erp).

## Replace a fragment

Use `replace-resource` when you want to swap one fragment for a raw file from your addon and keep the others. Choose the fragment to match what you're changing: a texture's fragment 0 is its header, while fragment 1 holds its top-mip pixels:

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

Your `source` file must hold raw fragment bytes: the data you'd get after decompression. The build compresses those bytes the way the archive stores that fragment, then updates the sizes and resource hash. If you leave out `fragment`, it defaults to `0`. Give `erp` paths relative to the game folder and `source` paths relative to your addon folder.

## Copy a resource between archives

If you already have the resource in another archive, use `copy-resource` to append it to `erp`, byte for byte. Choose `source` for an archive in your addon, or `from_erp` for one the game ships. Use `as` to give the copy a new name:

```json
{
  "op": "copy-resource",
  "source": "erp/car.erp",
  "erp": "2025_asset_groups/f1_2026_vehicle_package/teams/toro_rosso/wep/toro_rosso_barcelona_26.erp",
  "resource": "eaid://f1_2026_vehicle_package/teams/toro_rosso/wep/toro_rosso.wep",
  "as": "eaid://f1_2026_vehicle_package/teams/toro_rosso/wep/toro_rosso_barcelona_26.wep"
}
```

Choose a name that isn't already in `erp`, or the copy fails. [Adding a livery](./livery) shows how to copy a whole car this way.

## Add a resource

To append a new resource built from raw files, use `add-resource` with one file per fragment. Set `type` to the resource type (at most 15 characters), then list the fragment files in order under `sources`. The new resource gets its fragment names, flags and compression from the first resource in `erp` that matches both its type and fragment count:

```json
{
  "op": "add-resource",
  "erp": "2025_asset_groups/ui_package/screens/fe_loading.erp",
  "resource": "eaid://ui_package/art/textures/loading/sponsor.tif.image",
  "type": "GfxSurfaceRes",
  "sources": ["erp/sponsor.head.bin", "erp/sponsor.pixels.bin"]
}
```

Use `put-resource` to replace a resource of the same name or add it if it's missing. To take one out, use `remove-resource`. The loader can't remove a resource from a stock archive, so a build with any `remove-resource` rewrites the archives into the game folder and prints `erp: built into the game files; <n> resource removal(s) cannot apply at runtime`.

## Merge texture array slices

Team logos and flags sit in texture arrays: one resource whose slices are separate images. When several addons change different slices of one array, use `merge-slices` instead of `put-resource`. Ship the whole array in an archive in your addon, and the build writes only the slices that differ from the game's:

```json
{
  "op": "merge-slices",
  "erp": "2025_asset_groups/vfx_package/common.erp",
  "resource": "eaid://ui_package/art/texture_arrays/overrides.uidb/overrides.team_logos.f1_2026.texarray.image",
  "source": "logos/ui_textures.erp"
}
```

Each addon's changed slices go over the array the addons before it wrote. Two addons that change different logos both show. See [Texture arrays](/reference/ops/erp#texture-arrays) for arrays of another size or format.

## Add mip files

A texture's remaining mip chain lives outside the archive, in a loose `.mipmaps` file. Fragment 2 holds its path. To supply that file, use `add-mipmaps` to copy one file, or a folder of them, to a path in the game folder:

```json
{ "op": "add-mipmaps", "source": "mipmaps/", "target": "f1_safetycar_package/safety_car_2/textures/" }
```

If you supply a folder, every file under it is copied, keeping the paths below `target`. Then point your texture at the new mip file by replacing fragment 2 with `replace-resource`.

## Patch a `!pkg` resource

Worlds, layers and UI databases are `!pkg` resources: trees of named entries. With `patch-pkg`, you can edit entries in that tree while keeping the rest of the archive unchanged. Here's how Custom Grid Size adds a pit garage to a track's grid layer:

```json [erp/monaco.json]
{
  "op": "patch-pkg",
  "when": { "cars": 26 },
  "erp": "2025_asset_groups/environment_package/tracks/monaco/wep/monaco_f1_26_myteam.erp",
  "resource": "eaid://environment_package/tracks/monaco/wep/monaco-vehicles_current_myteam.layer",
  "edits": [
    {
      "op": "clone-entry",
      "at": "instances",
      "src": "[name=vehicles_current_myteam$PitGarage23]",
      "as": "vehicles_current_myteam$PitGarage25",
      "set": {
        "uniqueid": "a5256e66c3b302404d61d63301e06514",
        "overrides.VehPlacement.Id": 25,
        "overrides.WorldObject.transform": [-0.33035, 0.0, -0.94385, 0.0, 0.0, 1.0, 0.0, 0.0, 0.94385, 0.0, -0.33035, 0.0, -334.457, -14.192, 255.083, 1.0]
      }
    }
  ]
}
```

| Edit | Fields | Effect |
|---|---|---|
| `clone-entry` | `at`, `src`, `as`, optional `set`, `from` | Copy `src` in the container `at`, name the copy `as`, and apply `set`. In an array, `src` is a `[field=value]` selector and `as` is the new selected field. `from` takes the entry from another resource (`erp`, `resource`, `fragment`) |
| `set-value` | `path`, `value` | Set one value |

To find the entry you need, extract the fragment, then read its tree with `python3 tools/pkg.py text <fragment.bin>`. Address entries with dots and `[field=value]` selectors. Your replacement value takes the existing value's type, so supply a number for an int or float, 16 numbers for a `mat4`, or a hex string for a `woid`.

::: warning Not built yet
Only `clone-entry` and `set-value` are built; the other `patch-pkg` edits are defined but not built. If you use one of those other edits, the build skips all your addon's ERP operations and warns you.
:::

## Settings and warnings

You can use `when` and <span v-pre>`{{id}}`</span> with `db` [settings](/reference/addons/settings#_3-read-a-db-setting) in ERP files, just as you would in database operations. To print a message at build, use `warn`:

```json
{ "op": "warn", "when": { "cars": 30 },
  "message": "Monaco has room for 28 cars; a 30-car race there leaves 2 cars without a grid slot." }
```

## Build and check

```sh
swofty build
```

On the runtime route, the output counts your resources in `runtime: swofty/assets.json with ... <n> resource(s)`. On the build route, it lists each rewritten archive as `replace 2025_asset_groups/...`. It also prints every `warn` message, each archive it had to rewrite, and each conflict.

## Reference

- [ERP operations](/reference/ops/erp): every operation and field.
- [Operations](./operations): how ERP files fit with the other operation kinds, order and conflicts.
- [ERP archives](/reference/erp) and [`!pkg` resources](/reference/pkg): the file formats.
- [Editing menus](/reference/ui/editing-menus): ship an edited menu scene with `replace-resource`.
