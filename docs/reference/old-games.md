---
title: Assets from older F1 games
description: Where F1 2016 to F1 24 keep their helmets, faces, suits, engine sounds and cars, how their files differ from F1 25's, and how the Classic Seasons tools bring each asset into F1 25.
---

# Assets from older F1 games

F1 2016 to F1 25 are all built on Codemasters' EGO engine, and their helmets, driver faces, race suits, engine sounds and cars use the same [ERP archives](/reference/erp), `.mipmaps` sidecars and `.bdl` sound bundles. The resource versions and the texture layouts differ from game to game. The Classic Seasons addon ports these assets into F1 25 with the Python tools in [`addons/classicseasons/classic-seasons/tools/`](https://github.com/Swofty-Developments/SwoftyLauncher/tree/main/addons/classicseasons/classic-seasons/tools). This page lists where each game keeps them, what changes between versions, and which tool ports each asset.

## What the tools read

The tools read your own installs of the old games. The repository holds none of their files.

| Option | Folder |
|---|---|
| `--games` | A folder holding one install per game, named `F1 2016` to `F1 2022` (F1 22 as `F1 2022`). The tools take the season from the last four characters of the name. |
| `--f1-23` | Your F1 23 install |
| `--game` | Your F1 25 install, with the stock 2025 car archives |
| `--downloads`, `--mods` | Mod zips, and extracted mod folders |

F1 24 isn't read: F1 25 ships its 2024 engine bundles byte for byte and holds the 2024 grid's looks.

The tools need Python 3 with numpy, Pillow and lz4, the `zstd` command, and Node.js for BC7 decoding. They write their output into the addon folders (`helmets/`, `heads/`, `suits/`, `sounds/`, `cars/`), which aren't committed. `gen.py` then writes the addons' database rows and [asset ops](/reference/addons/asset-api) from that output.

## Game folders

| Game | Character archives | Helmet textures | Engine bundles |
|---|---|---|---|
| F1 2016 | In `game.dat` | Loose sidecars, `<stem>_helmet_<part>.tga.mipmaps` | `audio/bundles/<maker>_2016.bdl` |
| F1 2017 | In `game.dat` | Loose sidecars, `.tga.mipmaps` | `audio/bundles/<maker>_2017.bdl` |
| F1 2018 | In `game.dat` | Loose sidecars, `.tga.mipmaps` | `audio/2018/bundles/<maker>_2018.bdl` |
| F1 2019 | `2019_asset_groups/character_package/` | Loose sidecars, `.tif.2019.mipmaps` | `audio/2019/bundles/<maker>_2019.bdl` |
| F1 2020 | `2020_asset_groups/character_package/` | Helmet archive and its sidecars | `audio/2020/bundles/<maker>_2020.bdl` |
| F1 2021 | `2021_asset_groups/character_package/` | Helmet archive and its sidecars | `audio/2021/bundles/<maker>_2020.bdl` |
| F1 22 | `2022_asset_groups/character_package/` | Helmet archive and its sidecars | `audio/2022/bundles/<maker>_2022.bdl` |
| F1 23 | `2023_asset_groups/character_package/` | Helmet archive and its sidecars | `audio/2023/bundles/<maker>_2023.bdl` |
| F1 24 | Not read | Not read | `audio/2024/bundles/<maker>_2024.bdl`, byte for byte F1 25's |
| F1 25 | `2025_asset_groups/character_package/` | Helmet archive and its sidecars | `audio/2025/bundles/` |

Each game has one bundle per engine maker. F1 2021 ships the 2020 bundles under `audio/2021/bundles/`.

Driver kits sit under `character_package/drivers/male/<year>-<team>_<driver>/` and team suits under `character_package/team_livery/<year>-<team>/`. From F1 2020, a helmet archive is `<yyyy>_asset_groups/character_package/drivers/male/<look>/<stem>_helmet.erp`, where `<look>` is the kit folder and `<stem>` the driver part of its name (`2020-ferrari_charles_leclerc`, `charles_leclerc`). Loose sidecars sit under `character_package/` in every game; a helmet's are `character_package/drivers/male/<look>/textures/<stem>_helmet_<part>.<ext>.mipmaps`, with `<part>` `d`, `n` or `s`. F1 2016 to F1 2019 keep the helmet archives in `game.dat`, and the tools read those helmets from the loose sidecars alone.

## Resource versions

| Game | `GfxSurfaceRes` | Sidecar descriptor | Fragment compression | Sound bundle (`bndl`) |
|---|---|---|---|---|
| F1 2016 | 8 | 8 | Archives in `game.dat` | 0x6a |
| F1 2017 to F1 2019 | 8 | 8 | zlib (1), stored 0x81 | 0x6a |
| F1 2020 | 8 | 8 | zlib (1) in the sampled car archive; zstd 0x10, stored 0x90 in helmet archives | 0x6a |
| F1 2021 | 8 | 8 | zstd 0x10, stored 0x90 | 0x6a |
| F1 22 | 8 | 8 | zstd 0x10, stored 0x90 | 0x76 |
| F1 23 | 9 | 9 | zstd 0x11, stored 0x91 | 0x77 |
| F1 25 | 9 | 9 | zstd 0x11, stored 0x91 | 0x77 |

F1 25 plays a 0x6a bundle as it is.

A surface's third fragment names its `.mipmaps` sidecar. The version 8 descriptor is `u8 n, path[n], u32 count`, then `count` × `{u8 compression, u64 offset, u64 packed, u64 size}`. Version 9 adds a `u8 0` after the path and a second `u8` at the start of each level. Level compression 0 is stored, 1 zlib and 3 an LZ4 block. F1 23 packs the top one or two sidecar levels of some textures with compression 7, a container the tools don't read; those textures ship at half F1 23's size, or a quarter when two levels are compression 7.

In F1 2016 to F1 2019 a loose sidecar holds its levels one after another from the largest, each an LZ4 block or stored, with no descriptor. `old_helmets.py` tries each candidate format and size and keeps the shapes whose levels all decode and end with the file. Of two shapes of the same byte size (BC1 at twice the area of BC7), the one whose largest level decodes smoother wins.

[ERP operations](/reference/ops/erp#mip-files) covers sidecars in F1 25.

## Helmets

Tools: [`build_helmets.py`](https://github.com/Swofty-Developments/SwoftyLauncher/blob/main/addons/classicseasons/classic-seasons/tools/build_helmets.py), [`old_helmets.py`](https://github.com/Swofty-Developments/SwoftyLauncher/blob/main/addons/classicseasons/classic-seasons/tools/old_helmets.py). Data: `data/helmets.json`.

```sh
python3 tools/build_helmets.py --games <folder holding the old game folders> \
    [--downloads <mod zips>] [--mods <extracted mods>] [--game <F1 25>] [--jobs <n>]
```

F1 25's helmet shells (Bell HP7, Arai GP-6, Schuberth SF2 Pro, Stilo ST5 and ST6) share one texture layout, and a helmet texture fits every shell. Each entry in `data/helmets.json` has a `base`: an F1 25 look whose shell is the brand on the helmet. The base's helmet archive is relabelled byte for byte from the base look to `cs<yy>-<base look without its year>`, the same length. Its `helmet_d`, `helmet_n` and `helmet_s` take the old textures, with every level embedded and no sidecar.

The helmet's place in `helmet_d` depends on the game:

| Game | `helmet_d` | Port |
|---|---|---|
| F1 2016 to F1 2018 | Square, helmet in the top half; the bottom half holds gloves, boots and visor parts | Top half cropped, then resized (`"square": "top"`) |
| F1 2019, F1 2020 | Square, helmet stretched over the whole square | Whole square resized to 2:1 (`"square": "whole"`) |
| F1 2021 to F1 25 | 2:1, like F1 25 | Resized |

`helmet_d`'s largest level is resized to the base's full size. `helmet_n` keeps its own levels from the largest the base's `helmet_n` size holds; in F1 2016 to F1 2018 it's square too, and each level is cropped to its top half. `helmet_s` is the game's own from F1 2020 (`"spec": "own"`), whose channels match F1 25's. Earlier games get a generic `helmet_s`: the per-texel median of the 2024 grid's `helmet_s` at 1024 × 512, encoded as BC1.

F1 23's helmet shells are byte for byte F1 25's, and an F1 23 helmet texture fits the F1 25 shell of the same name. F1 23 helmets come with its kit (see [Race suits](#race-suits)).

`data/helmets.json` also takes helmet mods: DDS textures from a zip or a folder, or a helmet archive of F1 22 (surface version 8) or F1 23 (version 9) with the sidecars the mod ships. A texture the archive holds only in part, its sidecar missing from the mod, gets the generic one.

## Faces and heads

Tools: [`heads.py`](https://github.com/Swofty-Developments/SwoftyLauncher/blob/main/addons/classicseasons/classic-seasons/tools/heads.py), [`head_sheet.py`](https://github.com/Swofty-Developments/SwoftyLauncher/blob/main/addons/classicseasons/classic-seasons/tools/head_sheet.py). Data: `data/heads.json`.

A driver's head has two archives, `2025_asset_groups/animation_package/scenes/cinematics/drivers/heads/<label>.erp` and `.../morph_poses/<label>.erp`, named by the `asset_group_path_format` and `face_morph_asset_group_path_format` of its `entrants.animation.driver_heads.driver_head` row. The row's `model_path` names the model the heads archive holds:

```text
eaid://[CHARACTER_PACKAGE]/heads/male_driver/<name>/idf/<name>_rig_head_morph_v1
```

A face mod whose model name doesn't match its row's `model_path` renders an invisible head.

Every head ships under its own label, `cs_<head>`, with a row copied from a stock one and both archives registered with `group.register`. Streamed textures go as `.mipmaps` files under `character_package/heads/male_driver/<head>/textures/`.

### Face maps on a donor head

Every F1 25 head shares one face UV layout. F1 25 has two kinds of head:

| Kind | Folder | A new face map on it |
|---|---|---|
| Scanned | `heads/male_driver/<driver>` | Looks like the face |
| Generic | `heads/male_ea/male_ls<n>` | Looks wrong: the meshes are mostly East Asian scans, and the geometry dominates the face |

A face from an old game goes on a scanned donor head (`faces` in `data/heads.json`):

1. Both of the donor's archives are read from the F1 25 folder, or from the launcher's baseline when the launcher changed them.
2. The donor's name is replaced byte for byte by a code of the same length in every resource name and fragment, such as `oliver_goethe` by `cs_jolyon_pal`.
3. The copy's `head_d` surface is replaced by a 2048 px surface with all 10 levels embedded and no sidecar, and the `head_d` SRV takes its format and 10 levels.
4. `gen.py` copies `male_ls222`'s row with the copy's `model_path`.

The face comes from the `head_d` surface of another archive (2048 px, 10 levels, no sidecar), or from an F1 2016 colour map. An F1 2016 colour map is raw BC1 levels from 2048 to 256, largest first, and `heads.py` adds the levels from 128 down to 4.

Face mods use the same donor technique. The Rio Haryanto face mod is on `jak_crawford`'s head, and the Brendon Hartley and Sergey Sirotkin mods on `joshua_duerksen`'s.

### Checks

`heads.py` gives every material in F1 25's layout resource version 2. F1 25 parses a material of any other version as F1 23's layout, and some face mods label their F1 25 materials version 1, which crashes the session load. The tool fails when a head's archives name a `.sampler` resource (F1 25 has none) or a resource under `heads/male_driver/` they don't hold; the team picker's head preview crashes on such a link. A head with `repair_from` takes each such material from the named donor head instead.

`head_sheet.py` draws a contact sheet of every season's face maps. `--generic` shows the stock generic heads and `--donors` the scanned heads no Classic Seasons driver uses.

## Race suits

Tools: [`suit_sources.py`](https://github.com/Swofty-Developments/SwoftyLauncher/blob/main/addons/classicseasons/classic-seasons/tools/suit_sources.py), [`build_real_suits.py`](https://github.com/Swofty-Developments/SwoftyLauncher/blob/main/addons/classicseasons/classic-seasons/tools/build_real_suits.py), [`f1_23_suits.py`](https://github.com/Swofty-Developments/SwoftyLauncher/blob/main/addons/classicseasons/classic-seasons/tools/f1_23_suits.py), [`suit_transfer.py`](https://github.com/Swofty-Developments/SwoftyLauncher/blob/main/addons/classicseasons/classic-seasons/tools/suit_transfer.py). Data: `data/real_suits.json`.

```sh
python3 tools/suit_transfer.py --game <F1 25> --mods <mod folders>
python3 tools/build_real_suits.py --game <F1 25> --mods <mod folders> --f1-23 <F1 23>
```

`suit_sources.py` picks each team-season's suit: a real suit of its own season, else the one of the nearest season with the same team name, else a generated suit (`build_suits.py`). Every driver wears his own suit of that season when there is one, else the team's.

| Source | Seasons | How it's ported |
|---|---|---|
| Suit mods made for F1 25 | 2016, 2018 | `build_real_suits.py` keeps the mod's resources that name the base label and renames them byte for byte to `cs<yy>-<stock team>` (or `cs<yy>-<base folder>` for a driver suit), the same length. Shared body, glove and template resources stay in the stock base archive. |
| F1 2020 suit mods | 2017, 2019 | `suit_transfer.py` maps the old body's textures onto F1 25's `livery_v2` UV layout, then `build_real_suits.py` relabels the result. |
| F1 23's own kit | 2022, 2023 | `f1_23_suits.py` relabels an F1 25 base suit and gives each kit texture F1 23's pixels. |

**F1 2020 suits.** The F1 2020 team suit archive holds its own copy of the old body, boots, gloves and decal meshes; a driver archive holds the driver's own decals and boots. The old body, gloves and arms sit in F1 25's model space, and F1 2020 and F1 25 share the glove mesh and UVs. The decals are baked into the old diffuse. Each texel of F1 25's `livery_v2` layout takes the baked diffuse at the nearest point of the old body that faces the same way, with an affine UV fit where the two bodies differ (cuffs, collar, zip). The boots map the same way. `suit_transfer.py` caches the texel map in `--cache` and writes front and back renders to `art/suits/transfer/`.

**F1 23 kit.** Each entry names an F1 23 team suit (`<year>-<team>`) or driver kit (`<year>-<team>_<driver>`) under `2023_asset_groups/character_package/`, and an F1 25 base: the same team's 2024 suit, else its 2025 one; for a driver kit, the same driver's suit, else a teammate's with the same helmet shell. A kit texture takes F1 23's whole mip chain, archive and sidecar levels, embedded in the archive, in an F1 25 surface header with F1 23's format, size and level count. A texture with an F1 23 aspect that differs from F1 25's is resized. `--logos f1-23` also takes F1 23's body and glove logo meshes, renamed from `<year>-` to `cs<yy>-`.

## Engine sounds

Tool: [`sounds.py`](https://github.com/Swofty-Developments/SwoftyLauncher/blob/main/addons/classicseasons/classic-seasons/tools/sounds.py). Data: `data/sounds.json`.

```sh
python3 tools/sounds.py --games <folder holding the old game folders> --f1-23 <F1 23> [--game <F1 25>] [--years 2016-2024]
```

### How F1 25 picks a car's sound

A car's engine sound is picked by the `switchName` of the AudioF1Car component in its `<folder>-carmodel.layer` (`f1_m_<team>` on the 2025 cars). The layer's `bundleName` doesn't pick the sound. `Game_AudioF1Car_Setup` (`0x1409f4fa0`) uses the label twice:

- Its Wwise ID, FNV-1 32 with A to Z lowered, is the engine switch state. A label the soundbanks don't hold leaves the engine silent.
- The label, case kept, keys the 43 `audio.audio_car_data.<label>.*` fields in the game database. Their `bundle_name` picks `audio/2025/bundles/<name>.bdl`, which must be listed in `audio.audio_bundle_settings`. `Game_Build_AudioBundleRegistry` (`0x1409f1b50`) builds the bundle list from those rows.

A per-car sound is a case variant of the donor's label: the donor's Wwise switch, with `audio_car_data` rows of its own.

### The season labels

Each season car takes its donor label in other letter case. The label's letters are numbered from its first, and letter *i* is upper case when bit *i* of *n* is set, where *n* is the season less 2015 (1 to 9), plus 16 for the season's extra car:

| Season | Donor | Label |
|---|---|---|
| 2016 | `f1_m_mclaren` | `F1_m_mclaren` |
| 2017 | `f1_m_mclaren` | `f1_M_mclaren` |

The length stays the donor's, and `sounds.py` rewrites the layer byte for byte. It copies the donor's 43 records under the new label from the stock database, with `bundle_name` set to the season bundle of the team's `engine` in `data/seasons.json`. The F1 1990 addon labels its cars with case variants too (`F1_M_<name>_26`).

### Bundles

Each season from 2016 to 2023 takes its makers' bundles from the game of that season, at the paths in [Game folders](#game-folders); `data/sounds.json` lists them. The 2021 season takes F1 2021's `<maker>_2020.bdl`. The 2024 season uses F1 25's stock `<maker>_2024` bundles.

A bundle that isn't stock is copied to the season addon's `sounds/` as `cs<yy>_<maker>.bdl`. `gen.py` ships it with `file.put` to `audio/2025/bundles/`, adds its `audio.audio_bundle_settings` row (a copy of its maker's 2025 row), and puts each car's records. `build_cars.py` sets the labels when it builds a season; run on its own, `sounds.py` sets them in the built cars. A rerun changes nothing.

### Bundle parts

A bundle (`bndl`) holds two engine parts and two exhaust parts (`egoe`, each with its own version), read one after the other by `Game_Load_AudioBundle` (`0x144528310`) and the part reader `0x144524b30`. A part's block, from its size field at part offset 68, holds 24 bytes, then the curves (1032 bytes each: count, flag, 128 float pairs) and one mask per variation layer. The layer curves come first, then 5 global curves.

| Game | Part version | Layers | Curves | Block bytes |
|---|---|---|---|---|
| F1 2016 to F1 2018 | `0x71` | 8 | 13 | 13472 |
| F1 2019 to F1 2021 | `0x72` | 16 | 21 | 21760 |
| F1 22 | `0x76` | 16 | 21 | 21760 |
| F1 23 to F1 25 | `0x77` | 16 | 21 | 21760 |

F1 25's part reader always reads 21 curves and 16 masks, then jumps to the end of the block by its size field. On an 8-layer part it read 8 layer curves, the 5 global curves and all 16 masks from the part's samples. A mask picks the layers a state may play (the engine update `0x14452be10` picks one at random among the layers whose mask bit is clear), so those cars played random curves. `sounds.py` widens each 8-layer part to 16 layers: layers 8 to 15 copy layers 0 to 7 with their masks, the global curves follow, and the size field grows by 8288 bytes. The game then picks among the same curves as the old game. Bundles with 16 layers are copied unchanged.

The bundle header differs too: F1 2016 to F1 2019 bundles hold 0 at offset `0x24`, which F1 25 reads as the RPM range mode (0 takes the range from the engine parts only, 2 from every part; stock and F1 2020 bundles hold 2). F1 2019 bundles and F1 2018's `mercedes_2018` hold 0 in the exhaust pair at `0x14` and `0x18`. The tools leave both as they are.

## Cars

Tools: [`build_cars.py`](https://github.com/Swofty-Developments/SwoftyLauncher/blob/main/addons/classicseasons/classic-seasons/tools/build_cars.py), [`powerunit.py`](https://github.com/Swofty-Developments/SwoftyLauncher/blob/main/addons/classicseasons/classic-seasons/tools/powerunit.py), [`pace.py`](https://github.com/Swofty-Developments/SwoftyLauncher/blob/main/addons/classicseasons/classic-seasons/tools/pace.py), [`aislots.py`](https://github.com/Swofty-Developments/SwoftyLauncher/blob/main/addons/classicseasons/classic-seasons/tools/aislots.py).

```sh
python3 tools/build_cars.py [--downloads <folder>] [--game <F1 25>] [--years 2016-2024]
```

The season cars come from `F1 <year> Cars for F1 25` zips: ports already in F1 25's format that replace the stock 2025 cars, with resources named `eaid://f1_2025_vehicle_package/...`. `build_cars.py` renames them byte for byte to `eaid://cs_<year>_vehicle_package/...`, the same length. A zip without its own `teams/common.erp` keeps its shared references on the stock package. Loose sidecars the zip ships move to the season's folder; every other sidecar path stays on the stock files. A season's extra team (`"car"` in `data/seasons.json`, such as the 2016 Manor) goes in a second package, `cx_<year>_vehicle_package`, in another team's slot.

Each car then gets these passes:

| Pass | Tool | Change |
|---|---|---|
| Tyres | `tyres.py` | In a season with its own `teams/common.erp`, every car and showroom layer names the season's tyre set |
| Frame rate | `carperf.py` | Shadow casters from a lower LOD; non-livery textures capped at 1024 px |
| Wheels | `handling.py` | Wheel positions from the handling file itself |
| Pace | `pace.py` | The stock teams' mean aero, and a chassis mass from the team's real qualifying gap |
| Power unit | `powerunit.py` | The stock 2025 Engine, Turbo, FuelTank, FuelECU and EnergyRecoverySystem, and their R&D upgrades |
| AI data | `aislots.py` | Each team drives with a stock team's per-track AI data, assigned in pace order |
| Engine sound | `sounds.py` | The car's own case variant of its donor label |

### Car archives of the old games

Converting an old game's own car archive onto an F1 25 slot is pending in [PR #364](https://github.com/Swofty-Developments/SwoftyLauncher/pull/364): `tools/legacy_car.py` (`convert`, `check`, `selftest`) and its format notes in `re/db/subsystems/legacy_cars.md`. It handles cars of F1 2017, F1 2019, F1 2020 and F1 23, and F1 22 and F1 24 resources of the sampled versions. The main changes:

| Resource | F1 2017, 2019, 2020 | F1 25 |
|---|---|---|
| `GfxBufferRes` | 3 | 4, kind \| 0x1000 |
| `GfxMeshRes` | 0x10000, fixed stream order | 0x60000, with vertex declarations |
| `GfxMaterialRes` | 1 (F1 23's layout) | 2 |
| `GfxSurfaceRes` | 8 | 9 |
| `GfxSampleState` | 1 | Dropped |
| Fragments | zlib | zstd 0x11 |

The handling file (`VTF`) and the scene resources come from the F1 25 slot's archive. Testing in game is still in progress.

## Related pages

- [ERP archives](/reference/erp) and [`!pkg` resources](/reference/pkg): the archive formats.
- [Textures and models](/reference/data/textures) and [ERP operations](/reference/ops/erp): changing archives from an addon.
- [Game database](/reference/gamedb) and [Database operations](/reference/ops/database): the rows the tools copy.
- [Asset op files](/reference/addons/asset-api): `file.put` and `group.register`.
