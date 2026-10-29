---
title: ERP operations
description: Every ERP operation with an example, its fields and the build lines it prints — replace, add, copy, put, overlay and remove resources, texture array slices, mip files, scenes, !pkg edits and materials — and how archive changes reach the game.
---

# ERP operations

ERP operations change resources in the game's `.erp` archives: art, models, worlds, menus and handling files.

## An ERP op file in full

```jsonc [erp/frontend.json]
{
  "schema_version": 1,
  "ops": [
    // swap one fragment of a resource for a raw file
    { "op": "replace-resource", "erp": "2025_asset_groups/ui_package/flow_career_hub.erp",
      "resource": "eaid://ui_package/art/screens/career/rnd_overview.idf", "source": "ui/rnd_overview.idf" },
    // a new resource from raw fragment files, one per fragment
    { "op": "add-resource", "erp": "2025_asset_groups/environment_package/second_world/main_front_end/wep/main_front_end.erp",
      "resource": "eaid://environment_package/core_assets/objects/textures/main_front_end/names/f1_team_names/f1_teamname_cs_tm_01.tif.image",
      "type": "GfxSurfaceRes", "sources": ["erp/frontend/team.head", "erp/frontend/cs_tm_01.f1", "erp/frontend/cs_tm_01.f2"] },
    // the mip files those textures point at, copied into the game folder
    { "op": "add-mipmaps", "source": "erp/frontend/mipmaps/",
      "target": "environment_package/core_assets/objects/textures/main_front_end/names/" },
    // a resource from an archive in the addon: replaced if present, added if not
    { "op": "put-resource", "erp": "2025_asset_groups/ui_package/flow_frontend.erp",
      "resource": "eaid://ui_package/art/texture_arrays/overrides.uidb/overrides.team_logos.f1_2023.texarray", "source": "erp/logos/logos.erpk" },
    // a resource copied byte for byte, under a new name
    { "op": "copy-resource", "source": "erp/car.erp",
      "erp": "2025_asset_groups/f1_2026_vehicle_package/teams/toro_rosso/wep/toro_rosso_barcelona_26.erp",
      "resource": "eaid://f1_2026_vehicle_package/teams/toro_rosso/wep/toro_rosso.wep",
      "as": "eaid://f1_2026_vehicle_package/teams/toro_rosso/wep/toro_rosso_barcelona_26.wep" },
    // only the slices of a texture array that differ from the game's
    { "op": "merge-slices", "erp": "2025_asset_groups/vfx_package/common.erp",
      "resource": "eaid://ui_package/art/texture_arrays/overrides.uidb/overrides.team_logos.f1_2026.texarray.image",
      "source": "logos/ui_textures.erp" },
    // constants of a material
    { "op": "set-material", "erp": "2025_asset_groups/f1_2025_vehicle_package/teams/redbull/wep/redbull.erp",
      "resource": "eaid://f1_2025_vehicle_package/teams/redbull/materials/redbull_paint.material",
      "params": { "me_lacquerColour": [0.0, 1.0, 0.0] } },
    // printed at build
    { "op": "warn", "message": "Classic Seasons front end" }
  ]
}
```

These ops are trimmed from Classic Seasons, the R&D Tyres addon and the docs' livery example. List the file in `operations.erp` in your [manifest](/reference/addons/manifest#operations), or put the same ops in an `operations.assets` file with an `erp.` prefix (`erp.put-resource`). For a walkthrough, see [Textures and models](/reference/data/textures).

- `erp` paths are relative to the game folder. `source` paths are relative to your addon folder.
- List the real resource names of an archive with `python3 tools/erp.py <archive> '*'`.
- `swofty build` applies every operation on this page. A `patch-pkg` edit other than `clone-entry` and `set-value` is not built, and an addon whose ERP files use one has its ERP files and `warn` ops skipped with `warning: <addon id>: ERP operations skipped; <edit> is not built yet`.
- ERP files take `when` and <span v-pre>`{{id}}`</span> settings like [database operations](/reference/ops/database#settings). Only the archives a build changes are written to the overlay.

The build skips an operation whose `erp` archive isn't in the game folder and no earlier addon adds, and warns `<addon>: archive changes skipped; <path> is not a file in F1 25`. Check each `erp` path against the game folder, such as `2025_asset_groups/environment_package/tracks/...`.

## Operations

### replace-resource

```jsonc
{
  "op": "replace-resource",
  "erp": "2025_asset_groups/ui_package/screens/fe_loading.erp",      // the archive, relative to the game folder
  "resource": "eaid://ui_package/art/textures/loading/background.tif", // the resource's full name
  "fragment": 0,                                                      // optional; defaults to 0
  "source": "erp/background.tif"                                     // raw fragment bytes, in the addon
}
```

`replace-resource` replaces fragment `fragment` of `resource` in `erp` with the file `source` and keeps the other fragments byte for byte. The source may be larger or smaller than the stock fragment. It fails when the archive has no such resource or the resource has no such fragment.

### add-resource

```jsonc
{
  "op": "add-resource",
  "erp": "2025_asset_groups/ui_package/screens/fe_loading.erp",
  "resource": "eaid://ui_package/art/textures/loading/sponsor.tif.image",
  "type": "GfxSurfaceRes",                                    // the resource type, at most 15 characters
  "sources": ["erp/sponsor.head.bin", "erp/sponsor.pixels.bin"] // one raw file per fragment, in order
}
```

`add-resource` appends `resource` to `erp`. The new resource takes its type argument, fragment names, flags and compression from the first resource in `erp` of the same type with the same number of fragments. It fails when `erp` has none, and when `erp` already has a resource with that name:

```text
2025_asset_groups/ui_package/screens/fe_loading.erp: add-resource eaid://ui_package/art/textures/loading/sponsor.tif.image takes its layout from a GfxSurfaceRes resource with 2 fragment(s) in the archive, and it has none
2025_asset_groups/ui_package/screens/fe_loading.erp: eaid://ui_package/art/textures/loading/sponsor.tif.image already exists; put-resource replaces a resource
```

### Raw resources

`replace-resource` and `add-resource` take raw fragment data: the bytes a fragment holds after decompression. The build compresses each fragment the way the archive stores it and updates its size and the resource hash.

### put-resource

```jsonc
{
  "op": "put-resource",
  "erp": "2025_asset_groups/ui_package/flow_frontend.erp",
  "resource": "eaid://ui_package/art/texture_arrays/overrides.uidb/overrides.team_logos.f1_2023.texarray",
  "source": "erp/logos/logos.erpk",   // an archive in the addon that holds the resource
  "as": "eaid://..."                  // optional: the name it gets in erp
}
```

`put-resource` copies a resource from an archive in the addon into `erp`, replacing a resource of the same name or adding it.

### overlay

```jsonc
{
  "op": "overlay",
  "erps": ["2025_asset_groups/f1_2026_vehicle_package/teams/redbull/wep/redbull.erp"], // every archive it goes into
  "source": "erp/overlay.erp"                                                          // a ready archive in the addon
}
```

`overlay` puts every resource in a ready archive into each of `erps`, replacing or adding it, as a `put-resource` of each would. On the runtime route the archive loads as it is ([Asset API](/reference/addons/asset-api)). The Simplified Importer writes one `overlay` per game archive a mod changes.

### remove-resource

```json
{ "op": "remove-resource", "erp": "2025_asset_groups/ui_package/flow_frontend.erp", "resource": "eaid://ui_package/art/textures/old_logo.tif.image" }
```

See [Removing resources](#removing-resources).

### Other operations

- `copy-resource`: [Copying resources](#copying-resources).
- `merge-slices`: [Texture arrays](#texture-arrays).
- `add-mipmaps`: [Mip files](#mip-files).
- `patch-pkg`: [Editing `!pkg` resources](#editing-pkg-resources).
- `set-material`: [Materials](#materials).
- `warn` takes `message` and prints it when the addon is built. It takes no `erp.` prefix.

## Copying resources

```jsonc
{
  "op": "copy-resource",
  "source": "erp/car.erp",               // an archive in the addon; or "from_erp": a game archive
  "erp": "2025_asset_groups/f1_2026_vehicle_package/teams/toro_rosso/wep/toro_rosso_barcelona_26.erp",
  "resource": "eaid://f1_2026_vehicle_package/teams/toro_rosso/wep/toro_rosso.wep",
  "as": "eaid://f1_2026_vehicle_package/teams/toro_rosso/wep/toro_rosso_barcelona_26.wep"   // optional: a new name
}
```

`copy-resource` appends a resource to the archive `erp`, copying its parts byte for byte. It reads the resource from the game archive `from_erp`, or from `source`, an archive in your addon folder. `as` gives the copy a new name. The copy fails when `erp` already has a resource with that name.

Run `patch-pkg` after the copy to change the copy's contents, such as the layer names a renamed world lists.

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

A whole replacement that changes the layout of a stock entry in `erp` rewrites that archive into the game files and prints `erp: <erp> built into the game files; ...`, as for any other [layout change](#how-changes-reach-the-game).

The in-game loader patches only archives under `2025_asset_groups`. Every ERP operation on any other archive, such as a shader package in `shader_package_2025`, merges into the game's copy of that archive in load order and writes it into the game files, and the build prints:

```text
erp: shader_package_2025/object_flag.nefx2.sm51.erp built into the game files; the in-game loader patches only archives under 2025_asset_groups
```

## Mip files

A texture (`GfxSurfaceRes`) with three fragments keeps the rest of its mip chain in a loose file. Fragment 2 starts with a length byte and that file's path, relative to the game folder, such as `f1_safetycar_package/safety_car_2/textures/safety_car_2_decal_da.tif.mipmaps`.

```jsonc
{ "op": "add-mipmaps",
  "source": "mipmaps/",                                       // a file, or a folder, in the addon
  "target": "f1_safetycar_package/safety_car_2/textures/" }   // a path relative to the game folder
```

`add-mipmaps` copies `source` to `target` in the game folder. A folder copies every file under it, keeping their paths below `target`. The files replace or add game files through the journal, on both routes. Point a texture at a new mip file by replacing fragment 2 with `replace-resource`, or by adding the texture with `add-resource`.

```text
add-mipmaps: mipmaps/ is missing
add-mipmaps: mipmaps/ has no files
```

## Editing `!pkg` resources

```jsonc
{
  "op": "patch-pkg",
  "when": { "cars": 26 },                 // a db setting: only for a 26-car grid
  "erp": "2025_asset_groups/environment_package/tracks/bahrain/wep/bahrain_f1_26_myteam.erp",
  "resource": "eaid://environment_package/tracks/bahrain/wep/bahrain-vehicles_current_myteam.layer",
  "fragment": 0,                          // optional; defaults to 0
  "edits": [
    {
      "op": "clone-entry",
      "at": "instances",                                  // the container
      "src": "[name=vehicles_current_myteam$PitBox12]",   // the entry to copy: a selector in an array
      "as": "vehicles_current_myteam$PitBox13",           // the copy's value of the selected field
      "set": {                                            // values to change on the copy
        "uniqueid": "8631efce0fbce521eb5b71c1d07dd616",
        "overrides.VehPlacement.Id": 13,
        "overrides.WorldObject.transform": [-0.9988, 0.0, -0.048, 0.0, 0.0, 1.0, 0.0, 0.0, 0.048, 0.0, -0.9988, 0.0, -350.3214, -15.4045, -190.421, 1.0]
      }
    }
  ]
}
```

A `set-value` edit sets one value: `{ "op": "set-value", "path": "<dotted path with [field=value] selectors>", "value": 12 }`.

`patch-pkg` changes entries inside a `!pkg` resource, such as a track layer, and keeps the rest of the archive unchanged. Its `edits` work as in [UI patches](/reference/ops/ui-patches#routes-and-ui-databases):

- `clone-entry` copies the entry `src` in the container at `at` and names the copy `as`. In an array, `src` is a selector such as `[name=vehicles_current_myteam$PitBox12]`, and `as` replaces the selected field.
- `set` and `set-value` paths use dots and `[field=value]` selectors. A value is written in the type of the value it replaces: numbers for integers and floats, 16 numbers for a `mat4`, a hex string for a `woid`.
- `from` on `clone-entry` takes the entry from another resource, given as `erp`, `resource` and `fragment`, as shipped with the game.


To find an entry, extract the fragment with `tools/erpx.py` and read its tree with `python3 tools/pkg.py text <fragment.bin>`.

## How changes reach the game

With the in-game loader, changed and added resources reach the game as overlay archives instead of rewritten archives ([deliveries](/reference/addons/asset-api#deliveries)). Both routes produce the same resource bytes. The overlay can only carry a replaced resource whose type, argument and fragment layout, fragment sizes included, match the stock entry. An archive with any other replacement, such as an edited scene that grew, is rewritten into the game files instead, and the build prints:

```text
erp: 2025_asset_groups/ui_package/flow_career_hub.erp built into the game files; 1 resource(s) change their stock layout, which the in-game loader cannot load, such as eaid://ui_package/art/screens/career/rnd_overview.idf: fragment 0 is 30684 bytes, the stock one 29596; stock archives that bind to the replacement read it with the stock layout
```

The rest of the build's archives stay on the overlay. `swofty restore` puts a rewritten archive back like any other replaced game file. A resource copied from an archive with a different argument size is still left out, and the build warns.

For menu changes, see [UI patches](/reference/ops/ui-patches). For the archive format, see [ERP archives](/reference/erp).

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

## Removing resources

`remove-resource` takes `resource` out of `erp`. Removing a resource `erp` does not have is a no-op, logged. The in-game loader cannot take a resource out of a stock archive, so a build with any `remove-resource` delivers every ERP operation by rewriting the archives, and prints:

```text
erp: built into the game files; 1 resource removal(s) cannot apply at runtime
```
