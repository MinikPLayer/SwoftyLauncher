---
title: ERP operations
description: Every ERP operation and field — replace, add, copy, put and remove resources, mip files, scenes and !pkg edits — and how archive changes reach the game.
---

# ERP operations

ERP operations describe changes to artwork and other resources in `.erp` archives. They are one kind of [operation](/reference/data/operations); for a walkthrough, see [Textures and models](/reference/data/textures). List these files in `operations.erp` in your [manifest](/reference/addons/manifest#operations), or put the same ops in an `operations.assets` file with an `erp.` prefix (`erp.put-resource`).

`swofty build` applies every operation below. A `patch-pkg` edit other than `clone-entry` and `set-value` is not built, and an addon whose ERP files use one has its ERP files skipped with a warning.

## Example

```json
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

The resource name above is an example. List the real names with:

```sh
python3 tools/erp.py <archive> '*'
```

`erp` paths are relative to the game folder. `source` paths are relative to your addon folder.

The build skips an operation whose `erp` archive isn't in the game folder and no earlier addon adds, and warns `<addon>: archive changes skipped; <path> is not a file in F1 25`. Check each `erp` path against the game folder, such as `2025_asset_groups/environment_package/tracks/...`.

## Operations

| Operation | Fields | Purpose |
|---|---|---|
| `replace-resource` | `erp`, `resource`, `fragment`, `source` | Replace one fragment of a resource with a raw file; `fragment` defaults to 0 |
| `add-resource` | `erp`, `resource`, `type`, `sources` | Add a resource, with one raw source file per fragment |
| `copy-resource` | `from_erp` or `source`, `erp`, `resource`, optional `as` | Copy a resource between archives |
| `put-resource` | `erp`, `resource`, `source`, optional `as` | Copy a resource from an archive in the addon, replacing a resource of the same name or adding it |
| `remove-resource` | `erp`, `resource` | Remove a resource |
| `add-mipmaps` | `source`, `target` | Copy a mip file, or a folder of them, to a game-relative path |
| `patch-pkg` | `erp`, `resource`, `fragment`, `edits` | Run `clone-entry` and `set-value` edits on a `!pkg` resource |
| `overlay` | `erps`, `source` | Put every resource in a ready archive into each of `erps`, replacing or adding it. On the runtime route the archive loads as it is ([Asset API](/reference/addons/asset-api)) |
| `set-material` | `erp`, `resource`, `params` | Set constants of a `.material` resource, such as a car paint's colour or roughness ([Materials](#materials)) |
| `merge-slices` | `erp`, `resource`, `source` | Write the slices of a texture array that differ from the game's over the array already in `erp` ([Texture arrays](#texture-arrays)) |
| `warn` | `message` | Print a message when the addon is built |

## Raw resources

`replace-resource` and `add-resource` take raw fragment data: the bytes a fragment holds after decompression. The build compresses each fragment the way the archive stores it and updates its size and the resource hash.

- `replace-resource` replaces fragment `fragment` of `resource` in `erp` with the file `source` and keeps the other fragments byte for byte. The source may be larger or smaller than the stock fragment. It fails when the archive has no such resource or the resource has no such fragment.
- `add-resource` appends `resource` to `erp`. `type` is the resource type, such as `GfxSurfaceRes`, at most 15 characters. `sources` lists one file per fragment, in order. The new resource takes its type argument, fragment names, flags and compression from the first resource in `erp` of the same type with the same number of fragments, and the operation fails when `erp` has none. It also fails when `erp` already has a resource with that name; `put-resource` replaces or adds.

```json
{
  "op": "add-resource",
  "erp": "2025_asset_groups/ui_package/screens/fe_loading.erp",
  "resource": "eaid://ui_package/art/textures/loading/sponsor.tif.image",
  "type": "GfxSurfaceRes",
  "sources": ["erp/sponsor.head.bin", "erp/sponsor.pixels.bin"]
}
```

## Texture arrays

A texture array, such as `overrides.team_logos.f1_2026.texarray.image`, is a `GfxSurfaceRes` whose fragment 1 holds every slice, one after another, all the same size. Fragment 0 gives the slice count at offset 28. `merge-slices` reads `resource` from the archive `source` in your addon and writes only its changed slices:

```json
{
  "op": "merge-slices",
  "erp": "2025_asset_groups/vfx_package/common.erp",
  "resource": "eaid://ui_package/art/texture_arrays/overrides.uidb/overrides.team_logos.f1_2026.texarray.image",
  "source": "logos/ui_textures.erp"
}
```

- Each slice of `source` that differs from the game's stock slice replaces that slice of the array already in `erp`. That array is the stock one, or the one earlier addons in the load order wrote.
- The game's stock array is the one in `erp`. When `erp` has none, it is the one in the first archive, by name, directly in `2025_asset_groups/<package>/`, where `<package>` follows `eaid://` in `resource`. The merged array is then added to `erp`.
- An array with fewer slices than the game's, in the same format and slice size, merges the slices it has. Slices past the game's count are left out with a warning.
- An array of another format or slice size, a resource that is not a texture array, or a resource the game has no stock array for replaces the whole resource, with a warning.
- When the array in `erp` has another layout from an earlier addon, the slices merge over the game's array instead, with a warning.
- A `merge-slices` that changes no slice leaves `erp` as it is.

The build prints one line per op:

```text
erp: example.logos: 2 of 12 slice(s) of eaid://ui_package/art/texture_arrays/overrides.uidb/overrides.team_logos.f1_2026.texarray.image merged into 2025_asset_groups/vfx_package/common.erp
```

Conflicts count per slice. Two addons that change different slices of one array do not conflict. Two that change the same slice print `<later addon> overrides <earlier addon> on 1 archive resource`, and the later one in the load order wins. Each overridden slice counts as one archive resource, and [`swofty conflicts`](/reference/cli#swofty-conflicts) names it `<erp>:<resource> slice <n>`. A `merge-slices` that replaces the whole resource overrides every slice an earlier `merge-slices` wrote. A later `put-resource`, `replace-resource`, `remove-resource` or `overlay` of the resource overrides the addons that merged slices into it, and a later `merge-slices` into a resource an earlier addon wrote whole overrides that addon on each slice it changes.

On the runtime route the merged array goes into the overlay archive of the last addon that changed it. An `overlay` op whose archive holds a resource that a later `merge-slices` changes in one of its `erps` goes in resource by resource, and the build prints:

```text
erp: example.pack: its overlay archive goes in resource by resource; a later erp.merge-slices merges into <resource> in <erp>
```

A whole replacement that changes the layout of a stock entry in `erp` rewrites that archive into the game files and prints `erp: <erp> built into the game files; ...`, as for any other [layout change](#editing-pkg-resources).

The in-game loader patches only archives under `2025_asset_groups`. Every ERP operation on any other archive, such as a shader package in `shader_package_2025`, merges into the game's copy of that archive in load order and writes it into the game files, and the build prints:

```text
erp: shader_package_2025/object_flag.nefx2.sm51.erp built into the game files; the in-game loader patches only archives under 2025_asset_groups
```

## Removing resources

`remove-resource` takes `resource` out of `erp`. Removing a resource `erp` does not have is a no-op, logged. The in-game loader cannot take a resource out of a stock archive, so a build with any `remove-resource` delivers every ERP operation by rewriting the archives, and prints:

```text
erp: built into the game files; 1 resource removal(s) cannot apply at runtime
```

## Scenes

A [UI scene](/reference/ui/idf), the `.idf` layout a screen attaches to, is the `UILayout` resource named exactly `<path>.idf`, with no `?context=`. Its single fragment is a `!pkg` scene. To ship an edited scene, replace that fragment with the edited file:

```json
{
  "op": "replace-resource",
  "erp": "2025_asset_groups/ui_package/flow_career_hub.erp",
  "resource": "eaid://ui_package/art/screens/career/rnd_overview.idf",
  "source": "ui/rnd_overview.idf"
}
```

When the resource is a `UILayout`, the build checks that `source` is a scene saved from that same `.idf`, and fails otherwise.

Several archives often carry the same scene. List one `replace-resource` per archive that holds it:

```sh
python3 tools/erp.py <archive> 'rnd_overview.idf' | grep UILayout
```

[Swofty UI](/reference/ui/swofty-ui) exports an edited scene with these operations for every archive that holds it. See [Editing menus with an addon](/reference/ui/editing-menus).

The game reads a scene from whichever archive loads it first. When the build resizes a resource that another stock archive in the same package folder also carries, it warns:

```text
warning: erp: eaid://ui_package/art/screens/career/rnd_overview.idf is resized, but 2025_asset_groups/ui_package/flow_team_hq.erp also carries it and keeps the stock copy; replace it there too, or the game may show either one
```

## Binary XML with stats

Some resources hold binary XML (`\x01BXML`) in fragment 0 and its string stats in fragment 1: 120 bytes that start with `TSMD`. AI racing lines (`.aispline`), AI brake settings and car handling files (`.vtf`) are among them. The game sizes the memory it reads fragment 0 into from those stats, so fragment 0 with more or longer strings than its stats count crashes F1 25 when it loads.

When a resource the build writes has fragment 0 that its stats don't match, the build writes new stats from fragment 0, as the game's own tools do, and prints:

```text
erp: fixed the size headers of 20 AI racing line(s) in Example AI Pack so F1 25 reads them safely
```

A ready `overlay` archive with such a resource goes in resource by resource, and its resources get new stats the same way. The build prints:

```text
erp: example.pack: its overlay archive goes in resource by resource; 3 resource(s) in it need their size headers fixed
```

When fragment 0 starts as binary XML but doesn't parse, the build can't write its stats. It stops, and Play doesn't start:

```text
Example AI Pack would crash F1 25: its bahrain_ai_track.aispline doesn't match the size header the game reads with it, and the launcher can't read the file to fix the header. Turn Example AI Pack off in this profile.
```

To ship an edited fragment 0 that loads with other tools too, replace fragment 1 with stats made from it:

```sh
python3 -c "import sys; sys.path.insert(0, 'tools'); import vtf; d = open(sys.argv[1], 'rb').read(); open(sys.argv[2], 'wb').write(vtf.tsmd(vtf.parse(d)))" line.aispline line.tsmd
```

```json
[
  { "op": "replace-resource", "erp": "2025_asset_groups/environment_package/tracks/bahrain/wep/bahrain_common.erp", "resource": "eaid://environment_package/tracks/bahrain/ai/bahrain_ai_track.aispline", "fragment": 0, "source": "ai/line.aispline" },
  { "op": "replace-resource", "erp": "2025_asset_groups/environment_package/tracks/bahrain/wep/bahrain_common.erp", "resource": "eaid://environment_package/tracks/bahrain/ai/bahrain_ai_track.aispline", "fragment": 1, "source": "ai/line.tsmd" }
]
```

## Mip files

A texture (`GfxSurfaceRes`) with three fragments keeps the rest of its mip chain in a loose file. Fragment 2 starts with a length byte and that file's path, relative to the game folder, such as `f1_safetycar_package/safety_car_2/textures/safety_car_2_decal_da.tif.mipmaps`.

`add-mipmaps` copies `source` to `target` in the game folder. A folder copies every file under it, keeping their paths below `target`. The files replace or add game files through the journal, on both routes. Point a texture at a new mip file by replacing fragment 2 with `replace-resource`, or by adding the texture with `add-resource`.

```json
{ "op": "add-mipmaps", "source": "mipmaps/", "target": "f1_safetycar_package/safety_car_2/textures/" }
```

## Copying resources

`copy-resource` appends a resource to the archive `erp`, copying its parts byte for byte. It reads the resource from the game archive `from_erp`, or from `source`, an archive in your addon folder. `as` gives the copy a new name. The copy fails when `erp` already has a resource with that name.

```json
{
  "op": "copy-resource",
  "source": "erp/car.erp",
  "erp": "2025_asset_groups/f1_2026_vehicle_package/teams/toro_rosso/wep/toro_rosso_barcelona_26.erp",
  "resource": "eaid://f1_2026_vehicle_package/teams/toro_rosso/wep/toro_rosso.wep",
  "as": "eaid://f1_2026_vehicle_package/teams/toro_rosso/wep/toro_rosso_barcelona_26.wep"
}
```

Run `patch-pkg` after the copy to change the copy's contents, such as the layer names a renamed world lists.

## Editing `!pkg` resources

`patch-pkg` changes entries inside a `!pkg` resource, such as a track layer, and keeps the rest of the archive unchanged. Its `edits` work as in [UI patches](/reference/ops/ui-patches#routes-and-ui-databases):

- `clone-entry` copies the entry `src` in the container at `at` and names the copy `as`. In an array, `src` is a selector such as `[name=vehicles_current_myteam$PitBox12]`, and `as` replaces the selected field.
- `set` and `set-value` paths use dots and `[field=value]` selectors. A value is written in the type of the value it replaces: numbers for integers and floats, 16 numbers for a `mat4`, a hex string for a `woid`.
- `from` on `clone-entry` takes the entry from another resource, given as `erp`, `resource` and `fragment`, as shipped with the game.

```json
{
  "op": "patch-pkg",
  "when": { "cars": 26 },
  "erp": "2025_asset_groups/environment_package/tracks/bahrain/wep/bahrain_f1_26_myteam.erp",
  "resource": "eaid://environment_package/tracks/bahrain/wep/bahrain-vehicles_current_myteam.layer",
  "edits": [
    {
      "op": "clone-entry", "at": "instances",
      "src": "[name=vehicles_current_myteam$PitBox12]", "as": "vehicles_current_myteam$PitBox13",
      "set": {
        "uniqueid": "8631efce0fbce521eb5b71c1d07dd616",
        "overrides.VehPlacement.Id": 13,
        "overrides.WorldObject.transform": [-0.9988, 0.0, -0.048, 0.0, 0.0, 1.0, 0.0, 0.0, 0.048, 0.0, -0.9988, 0.0, -350.3214, -15.4045, -190.421, 1.0]
      }
    }
  ]
}
```

ERP files take `when` and `{{id}}` settings like [database operations](/reference/ops/database). Only the archives a build changes are written to the overlay.

With the in-game loader, changed and added resources reach the game as overlay archives instead of rewritten archives ([deliveries](/reference/addons/asset-api#deliveries)). Both routes produce the same resource bytes. The overlay can only carry a replaced resource whose type, argument and fragment layout, fragment sizes included, match the stock entry. An archive with any other replacement, such as an edited scene that grew, is rewritten into the game files instead, and the build prints:

```text
erp: 2025_asset_groups/ui_package/flow_career_hub.erp built into the game files; 1 resource(s) change their stock layout, which the in-game loader cannot load, such as eaid://ui_package/art/screens/career/rnd_overview.idf: fragment 0 is 30684 bytes, the stock one 29596; stock archives that bind to the replacement read it with the stock layout
```

The rest of the build's archives stay on the overlay. `swofty restore` puts a rewritten archive back like any other replaced game file. A resource copied from an archive with a different argument size is still left out, and the build warns.

For menu changes, see [UI patches](/reference/ops/ui-patches). For the archive format, see [ERP archives](/reference/erp).

## Materials

`set-material` changes constants of a `GfxMaterialRes` (`.material`) resource in place. `params` maps a constant's name to a number, or to a list for a vector constant. Each value is written as the material declares it (float or uint). Every other byte of the material stays as it is.

```json
{
  "op": "set-material",
  "erp": "2025_asset_groups/f1_2025_vehicle_package/teams/redbull/wep/redbull.erp",
  "resource": "eaid://f1_2025_vehicle_package/teams/redbull/materials/redbull_paint.material",
  "params": { "me_lacquerColour": [0.0, 1.0, 0.0], "me_flakesStrength": 1.0, "me_flakesColourInfluence": [1.0, 0.0, 1.0] }
}
```

On the 2025 Red Bull in a race, this turns the yellow paint green and the dark blue purple.

A value can come from a `db` [option](/reference/addons/manifest#options): <span v-pre>`"{{id}}"`</span> as the whole value, or as an item of a list. The option's value is read as one or more numbers separated by spaces or commas, and a toggle reads as 1 or 0. A choice can hold a colour, such as `"0 1 0"`:

```json
"params": { "me_lacquerColour": "{{coat}}", "me_flakesStrength": "{{flakes}}" }
```

List a material's constants and their current values with:

```sh
python3 tools/gfx_material.py dump <archive> <material name>
```

The operation fails when the material has no constant of that name, when the constant is a bindless heap index (`me_*HeapIndex`, which the game fills in at load), or when a list has the wrong number of values. A material's constants depend on its shader; `tools/gfx_material_layouts.json` lists them for every shader in the game.

