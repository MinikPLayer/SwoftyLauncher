---
title: The Simplified importer
description: How the Simplified importer turns imported mods into registry records others can stack on, where an import shows, how imports and live layers are ordered, how a livery is put on another car, how a new version of a mod is recognised, and how everything it can't recognise builds as before.
---

# The Simplified importer

The importer turns a player's mod (a `.serps` package, a zip of game files, a track zip) into an addon, and **recognises** what the mod is: a livery, a car model, handling, a helmet, an engine sound, a track, a team's identity. It writes those as registry records that other addons can stack on. Everything it doesn't recognise builds exactly as the converter made it.

For example, Macarena Red Bull becomes a livery record for the 2026 Red Bull plus a handling layer. R&D Tyres can then win the handling while the livery still loads.

## What changes for one mod

Macarena Red Bull converts into `mod.macarena-red-bull-v1-0`: one overlay that writes 325 resources into `redbull.erp`. Two of them are handling files.

The importer writes a registry index next to the converted addon, and leaves the addon's own files alone:

```text
mod.macarena-red-bull-v1-0/
  swofty.json            unchanged
  ops/erp.json           unchanged: the overlay, 325 resources
  erp/overlay.erp        unchanged
  registry/content.json  the livery record
  registry/layers.json   point the Red Bull at the livery; the two handling files, as their own layer
  registry/raw.json      every op, split into slices, each claimed by a record or kept raw
```

```json
// registry/content.json: "redbull_2026" becomes mod.macarena-red-bull-v1-0/redbull_2026
{
  "livery": {
    "redbull_2026": {
      "vehicle": "game/redbull_2026",
      "textures": { "erp": "erp/overlay.erp", "prefix": "eaid://f1_2026_vehicle_package/teams/redbull/", "kinds": ["textures", "materials"] },
      "picker_name": "Macarena Red Bull"
    }
  }
}
```

```json
// registry/layers.json: a layer of vehicle "game/redbull_2026" and one of vehicle.handling "game/redbull_2026"
{
  "vehicle": { "game/redbull_2026": { "livery": "mod.macarena-red-bull-v1-0/redbull_2026" } },
  "vehicle.handling": { "game/redbull_2026": { "vtf": { "erp": "erp/overlay.erp", "resources": [
    "eaid://f1_2026_vehicle_package/teams/redbull/handling/redbull.vtf",
    "eaid://f1_2026_vehicle_package/teams/fom_car_redbull/handling/fom_car_redbull.vtf" ] } } }
}
```

A file field (a `Path`) in a data file is usually a string: a file or folder in the addon. An imported mod's files sit inside the overlay archive the converter made, so the importer writes a path into that archive as an object:

- **`{ "erp", "prefix", "kinds" }`:** every resource in the archive `erp` whose path starts with `prefix` and sits in one of the folders named in `kinds` (`textures`, `materials`, `idf`, `wep`).
- **`{ "erp", "resources" }`:** the named resources in the archive `erp`.

`erp` is a path in the addon's folder, and resource paths are the archive's own `eaid://` paths.

The livery and the handling are separate fields. When R&D Tyres layers `vehicle.handling` and loads after the mod, R&D's handling wins and the livery still loads. When the mod loads after R&D Tyres, the mod's handling wins, and the conflicts view offers the fix in one click:

```json
{
  "kind": "field", "registry": "vehicle.handling", "id": "game/redbull_2026", "path": "/vtf",
  "addons": ["swofty.rnd-tyres-thermal", "mod.macarena-red-bull-v1-0"], "wins": "mod.macarena-red-bull-v1-0",
  "fix": { "label": "Use only Macarena Red Bull's livery",
           "choice": { "mod": "mod.macarena-red-bull-v1-0", "drop": ["vehicle.handling"] } }
}
```

## Where an import shows

What a player drops in becomes part of the **base**, the data every Grand Prix and Time Trial box and every career inherits, unless an addon sets its own value for that year:

- **A livery** (Macarena Red Bull) becomes a livery record and a layer on `game/redbull_2026`. It shows in every box whose Red Bull is that base car.
- **A track zip** (Hockenheim) becomes a track record that provides `venue/hockenheimring`. It shows in every box's track select that it fits, with its base data.
- **An engine sound pack** (V10) becomes `engine_sound` records and layers on the base cars. It's heard in every box that uses those base cars.
- **A car port or a season pack** asks once where it goes (see [Season packs](#season-packs-and-their-own-entry)).

A year addon keeps its own look by setting its own value. Classic Seasons' 2017 Red Bull is a variant of the stock 2025 Red Bull that sets its 2017 livery, and a variant's own value beats layers on its base:

```text
[registry] vehicle swofty.classic-seasons-2017/redbull: /livery set by the variant (2017 livery);
           layer on game/redbull_2025 from mod.rb-singapore-by-pf126p not inherited for this field
[registry] vehicle game/redbull_2025: /livery -> mod.rb-singapore-by-pf126p/redbull_2025
```

A year pack whose cars point at a stock file can pin it with `stock("...")`. The core then gives that year the original file, whatever an import put in its place. With Classic Seasons 2024 pinning the stock 2024 engine bundles, F1 2012's V8 sounds reach the stock cars and not the 2024 box.

To put a drop-in on a year's car too, the player adds that car in **Applies to** (see [Applies to](#targeting-another-base)).

## Recognisers

Each recogniser lives next to the [applier](/std/build) it inverts, in `packages/core/src/std/`. A recogniser may only claim part of a mod if the applier rebuilds exactly that part, byte for byte. Anything else stays raw.

```rust
// packages/core/src/std/vehicle.rs
pub fn apply(id: &RecordId, v: &Vehicle, ctx: &mut ApplyCtx) -> Result<(), ApplyError>;   // record -> ops
pub fn recognize(ops: &OpSlices, ctx: &RecognizeCtx) -> Vec<Claim>;                        // ops -> records

pub struct Claim {
    pub record: Option<Record>,     // a record under the mod's own key
    pub layer: Option<Layer>,       // or a layer on a game/ record or another addon's base
    pub slices: Vec<SliceRef>,      // the raw op slices it stands for
}
```

What each recogniser looks for:

- **Track:** a `track.add` op from a track zip becomes track `mod.<id>/<track>`. Its venue comes from the track's name in the mod, matched against the core's venue names, or from the player's choice on the importer page ("This track is: Sepang"). It never comes from the track's code: `bahrain24` is Sepang, `hanoi` is Valencia and `miami24` is Igora Drive.
- **Car archive:** overlays of `2025_asset_groups/<pkg>/teams/<t>/wep/<t>.erp`, split by resource path:
  - `teams/<t>/textures/` and `materials/` become a livery;
  - `teams/<t>/idf/` and `wep/` become the vehicle's `model`;
  - `teams/<t>/handling/*.vtf` and `fom_car_<t>/handling/*.vtf` become `vehicle.handling`;
  - `teams/common*.erp` and `shared/generics/` stay raw.

  `<pkg>/<t>` is matched to a `game/` vehicle through the stock database (`entrants.vehicles`: `asset_filename`, `vehicle_package`), never a hard-coded folder list.
- **Special livery:** `se_livery` and `unlockable_se_livery` rows plus `<t><wep_override>.erp` become a livery with `special`.
- **Helmet:** helmet archives and their item rows become a helmet.
- **Engine sound:** `audio/2025/bundles/<maker>_<year>.bdl` becomes an `engine_sound` layer on that maker.
- **Identity:** named team and driver rows and their text become `team.identity` and `driver.identity` layers.
- **Tyre visuals:** slick texture arrays and their mipmaps become a `tyre.visual` layer.
- **Season pack:** car archives for 3 or more teams in one vehicle package become one vehicle per team, plus a dormant `content.package` variant.

## Everything else stays raw

Every op the addon has is listed in `registry/raw.json`, in build order, split into slices. Each slice is claimed by a record or kept, with the reason:

```json
// mod.f1-2020-cars-for-f1-25/registry/raw.json (abridged)
{
  "recognizer": 1,
  "ops": [
    { "file": "ops/erp.json", "index": 0, "op": "overlay",
      "target": "2025_asset_groups/animation_package/scenes/incardriver/2025/incardriver_f1.erp",
      "slices": [ { "resources": 708, "claimed": null, "why": "no recognizer: in-car driver animation" } ] },
    { "file": "ops/erp.json", "index": 3, "op": "overlay",
      "target": "2025_asset_groups/f1_2025_vehicle_package/teams/ferrari/wep/ferrari.erp",
      "slices": [
        { "match": ["teams/ferrari/textures/", "teams/ferrari/materials/"], "claimed": "vehicle game/ferrari_2025 /livery" },
        { "match": ["teams/ferrari/idf/", "teams/ferrari/wep/"],            "claimed": "vehicle game/ferrari_2025 /model" },
        { "match": ["teams/ferrari/handling/", "teams/fom_car_ferrari/handling/"], "claimed": "vehicle.handling game/ferrari_2025 /vtf" } ] }
  ]
}
```

- **Raw stays raw.** Database drift from an older game build, in-car animation, shared archives, UI flows and tool junk are never claimed, and build exactly as the converter made them.
- **Mods that haven't converted yet** load through the importer's build hook, and stay raw until they convert.

## Order and precedence

A mod's place is its addon's place in the profile's load order. The importer page lists imported addons in that order, and dragging one reorders the profile.

Imports and live layers are ordered by load order, in the game:

- **Raw against raw:** two imports, or an import and an addon's build operations. Per target (resource, record, text key, file, texture slice), the later one wins.
- **A claimed field against a live layer:** the core reads each import's claims from its `registry/raw.json` and resolves them with every other layer, at the import's place in the load order. Per field, the later one wins. A claim that wins is already in the build. A claim that loses gives way: the core applies the winner's value over the built archive, and resources only the losing mod had stay, with a log line.
- **Two full liveries on one car:** the later one replaces the earlier one whole. A **partial** livery, such as a number decal or a sponsor patch, covers only part of the car's texture set, and partial liveries stack per resource on top of the full one. The importer marks each recognised livery full or partial, and the importer page shows which.
- **A player choice** (a target, a dropped part, where a season pack goes) counts as a layer at the mod's own place.

```text
[registry] vehicle.handling game/redbull_2026 /vtf: swofty.rnd-tyres-thermal (later) wins over
           mod.macarena-red-bull-v1-0 (raw claim); the build's handling files for this field are replaced in game
[registry] livery game/redbull_2026: mod.rb-japan-gp (full, later) replaces mod.macarena-red-bull-v1-0 (full);
           mod.redbull-numbers (partial) stacks on top
```

The launcher masks raw slices only for the player's choices. Masking needs one addition to `erp.overlay`: an `except` list of resources that the build skips. When nothing is masked, no `except` is written, and the build is byte-identical to the converter's.

## Targeting another base

**Applies to** puts an imported part on more cars. It adds a target and keeps the original, so a livery can show on the stock car and on a year's car at once. A target that goes away (its addon turned off) leaves the others in place.

**Liveries fit by car model.** A livery is made for one car model, the files in `cars::Vehicle::model`. The picker lists only cars whose effective `model` is that model. A helmet fits any driver, and an engine sound fits any vehicle.

```text
Macarena Red Bull                                       livery · handling
  Livery     Applies to  [x] Red Bull 2026 (stock)
             No other car uses the 2026 Red Bull's model. Classic Seasons' 2016-2024 cars are ports with their own models.
  Handling   [x] Use the mod's handling
```

The picker's list comes from what's installed: each addon's data files and its `registry.lock.json`, which lists the cars a code addon registers, plus the last session's `registry/catalog.json`.

**An author may declare the target** in `mod.serps`, in a block that older importers skip. The importer uses it as declared: the author knows which car the livery was painted on, even when the files sit in a stock car's folder.

```json
// mod.serps: a livery painted on fanfreluche's F1 2017 Ferrari port, which uses the stock 2025 Ferrari folder
{
  "mod_name": "Ferrari 2017 Monza", "author": "someone", "version": "1.0", "game_version": "1.0.141.7865",
  "swofty": {
    "targets": [ { "kind": "livery", "from": "game/ferrari_2025", "to": "swofty.classic-seasons-2017/ferrari" } ],
    "requires": [ { "record": "content.package:swofty.classic-seasons-2017/f1_2017" } ]
  }
}
```

```text
[import] livery mod.ferrari-2017-monza/ferrari_2025: target swofty.classic-seasons-2017/ferrari declared by the
         author in mod.serps; the files sit in the game/ferrari_2025 folder
```

Players pick only from cars that fit. The choice is saved in `importer.json`, keyed by what it acts on (see [A new version of a mod](#a-new-version-of-a-mod)).

## Season packs and their own entry

A car pack for a whole season (fanfreluche's F1 2020 cars, F1 2012 Temporada) is recognised team by team. It also provides a dormant `content.package` variant:

```json
{
  "vehicle": {
    "ferrari": { "extends": "game/ferrari_2025",
                 "model": { "erp": "erp/overlay-4.erp", "prefix": "eaid://f1_2025_vehicle_package/teams/ferrari/", "kinds": ["idf", "wep"] },
                 "livery": "mod.f1-2020-cars-for-f1-25/ferrari", "handling": "mod.f1-2020-cars-for-f1-25/ferrari" }
  },
  "vehicle.handling": {
    "ferrari": { "vtf": { "erp": "erp/overlay-4.erp", "resources": [
      "eaid://f1_2025_vehicle_package/teams/ferrari/handling/ferrari.vtf",
      "eaid://f1_2025_vehicle_package/teams/fom_car_ferrari/handling/fom_car_ferrari.vtf" ] } }
  },
  "content.package": {
    "f1_2020": { "extends": "game/f1_2025", "title": "F1 2020 cars for F1 25",
                 "teams": [ { "key": "ferrari", "vehicle": "mod.f1-2020-cars-for-f1-25/ferrari" } ] }
  }
}
```

A car port or season pack asks once, when it's imported, where it goes:

```text
F1 2020 cars for F1 25 replaces 10 cars.
  Where   (o) Grand Prix and Time Trial only     ( ) Everywhere, careers included
```

- **Grand Prix and Time Trial only** is the default. The pack's cars show in Grand Prix and Time Trial, and careers keep their own cars. Swapping car archives between modes within one game run needs reverse engineering of how the game loads them per session.
- **Everywhere** replaces the cars the pack changes in every mode, careers included: Progressive's 2025 season races the 2020 cars.
- **Own GP/TT entry** (a switch on the importer page) gives the pack its own cloned vehicle package and an era box, the way Classic Seasons builds `cs_2017_*`. Two car packs can then be installed side by side, instead of both writing the same ten team slots.
- **Raw parts stay global.** In-car driver animation, `teams/common.erp`, menu flows and tyre textures aren't recognised, and still apply in every mode.

Single liveries, helmets and sounds don't ask: they go everywhere.

## A new version of a mod

The importer recognises a new version of an installed mod by its **lineage**: its `mod.serps` author and title, or, for a zip, its file name without the version and byline plus the parts it changes.

```text
Macarena Red Bull V1.1 looks like an update of Macarena Red Bull (V1.0).
  [Update]  keeps its place in the load order and your choices      [Install side by side]
```

The player's choices are keyed by what they act on, not by the mod's own record ids, so they carry over to the update:

```json
// importer.json
{ "lineage": "macarena-red-bull", "drop": ["vehicle.handling@game/redbull_2026"],
  "targets": { "livery@game/redbull_2026": ["vehicle:game/redbull_2026"] } }
```

- **A choice that matches nothing** in the new version is dropped, with a notice.
- **A target the new version doesn't fit** is removed, with a notice. The livery stays on its other targets.
- **New parts** are listed as "New in this version".
- **Tracks:** an import with the same track code but a different author or lineage asks the player. It never replaces the installed track silently.

## Existing imports

On upgrade, a startup pass indexes every already-imported mod in place:

```text
[import] index mod.macarena-red-bull-v1-0: 325 resources claimed (livery 323, vehicle.handling 2), 0 raw; round trip ok
[import] index mod.f1-2012-season-v1-0-bydanix: 2,114 claimed, 4,312 raw (database drift from an older game build)
```

- **Nothing is reconverted.** The pass writes only the new files. `ops/`, the archives and `swofty.json` keep their bytes, and the build hash doesn't change.
- **Two full liveries on one car** in an existing profile keep their asset-by-asset mix until the player presses the fix in the conflicts view.
- **Car ports and season packs** already installed keep reaching every mode until the player changes where they go.
- **Launcher 1.5 builds the same profile as before.** `swofty.json` is untouched, and launcher 1.5 never opens `registry/`.
- **The importer itself** ships a new major release that needs launcher 1.6. Launchers of 1.5 and earlier keep getting the newest 0.11.x.

## What the importer provides

The importer owns three registries under its own id:

- **`teamsimplified.mods.package`:** one record per imported mod: its source file and hash, its lineage, its parts and variants, the records it provides, its raw count and the player's choices;
- **`teamsimplified.mods.raw`:** each mod's `raw.json`, for Send Logs and the triage bot;
- **`teamsimplified.mods.rule`:** path rules other addons provide, so mods made against their files are targeted automatically. Classic Seasons 2017 can say that `cs_2017_vehicle_package/teams/<t>/` is `vehicle swofty.classic-seasons-2017/<t>`.

```text
swofty mods show <id>                       records and raw slices of one import
swofty mods target <id> <record> <base>     add a target to a recognised record
swofty mods recognize --check [<id>]        re-run the round trip without writing
```

## The release gate for the importer

- **Round-trip tests:** for each recogniser, applying the claims of a real mod gives the exact ops the converter made.
- **Golden builds:** every imported mod in the player bundles the triage bot has, built on 1.5.19 and on 1.6, with byte-identical game files.
- **A downgrade build:** a 1.6-indexed profile builds on 1.5.19 exactly as before.
- **Masking, targeting, lineage updates and where a season pack goes,** each with an in-game check.
