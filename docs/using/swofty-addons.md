---
title: Swofty's addons
description: The addons Swofty publishes in the Addon Browser - F1 25 RE-Optimization, the Career Mode Editor, the Driver Manager, R&D Tyres & Thermal Management and Any Team Transfers - and how to install, update and remove them.
---

# Swofty's addons

Swofty publishes these addons in the [Addon Browser](/using/addon-browser), next to everyone else's. Install them from there; they arrive switched off in every profile.

| Addon | Id | By |
|---|---|---|
| [F1 25 RE-Optimization](#f1-25-re-optimization) | `xanpavle.re-optimization` | [xanpavle](https://github.com/xanpavle), ported by Swofty |
| [Career Mode Editor](/using/career-editor) | `swofty.career-editor` | Swofty |
| [Driver Manager](/using/driver-manager) | `swofty.driver-manager` | Swofty |
| R&D: Tyres & Thermal Management | `swofty.rnd-tyres-thermal` | Swofty |
| [Any Team Transfers](#any-team-transfers) | `swofty.any-team-transfers` | Swofty |

## Install, update or remove one

- **Install it:** open the **Addon Browser**, find it, and select **Install**. The launcher opens the **Addons** page with the new addon highlighted.
- **Turn it on:** on the **Addons** page, toggle its switch in the profile you want it in, then press **Play**. From a terminal: `swofty addons enable <id>`.
- **Update it:** when Swofty publishes a new version, **Play** becomes **Update addons**. See [Updating](/using/updating#addon-updates).
- **Remove it:** expand its row on the **Addons** page and select **Remove addon**. Install it again from the Addon Browser to get it back.

Launchers before 1.4.0 carried these addons in an `addons/` folder next to `bin/`. Updating to 1.4.0 removes that folder; the installed copies stay installed and update from the Addon Browser.

## F1 25 RE-Optimization

F1 25 RE-Optimization is [xanpavle's renderer optimisation for F1 25](https://github.com/xanpavle/F1-25-RE-Optimization), released under the MIT licence. Swofty ported it to a launcher addon with his permission. It makes the renderer do less work: it lowers a set of quality settings that cost a lot of GPU time, holds the expensive shadow, fog and crowd values down inside the engine, overrides a few engine-level renderer limits, and picks the upscaler your GPU supports. It tracks upstream 0.2.5.

- It is off until you turn it on.
- It works offline and in single-player only, like every modded session.
- It is made for the Steam build of F1 25 (build 25251167). On any other build its hooks don't install, and `swofty/loader.log` says so with `SAFE`.
- It doesn't change `hardware_settings_config.xml` in your Documents folder. The addon lowers the settings in memory as the game reads the file. When the game saves the file, for example after you change a graphics option, the values the addon lowered go back to yours.

### What it lowers

The graphics profile lowers these on top of whatever preset you pick:

| Setting | Before (example) | With the profile |
|---|---|---|
| Volumetric fog map | 1920 × 1080 | 512 × 288 |
| World and night shadow maps | 4096 | 2048 |
| 32-bit sky shadow map | on | off |
| Shadow and spot shadow sampling | 3 | 2 |
| Crowd shadows | on | off |
| Screen-space reflections (SSR) quality | 4 | 2 |
| Ambient occlusion (SSAO) | 3 | 1 |
| Grass draw distance | 120 m | 80 m |
| Crowd billboard distance | 6000 | 2500 |
| High-quality particles | on | off |
| Hair | on | off |
| God rays, lens streak, motion blur v2 | on | off |
| Car reflection maps | | scale 1.0, one cube face updated per frame |
| Procedural cloud render scale | | 2.0 |
| Skid mark parallax | on | off |
| Texture streaming texel density bias | | 0.25 |
| GPU culling (`cs_culling`) | off | on |
| Ray-traced ambient occlusion and DDGI | on | off |

It leaves resolution, your preset, mirrors, ray-traced shadows and reflections, rain, the upscaler quality level (Quality, Balanced, Performance) and frame generation alone.

### What it holds in the engine

The game clamps shadow, fog and crowd settings to your graphics preset after it reads the settings file. In-engine enforcement sets these where the engine reads them, after that clamp:

| Setting | Tuned value |
|---|---|
| World, night, sky and spot shadow maps | 2048 |
| Sky shadow cascades, night shadow sources | 4 |
| Shadow and spot shadow sampling | 2 |
| Volumetric fog map | 512 × 288 |
| Crowd billboard distance | 2500 |
| Texture streaming budget | 2048 MiB |

- **Cheaper** (the default) uses the lower of your preset's value and the tuned value, and turns off sky-shadow low LOD, the 32-bit sky shadow map, crowd shadows, god rays, lens streak and motion blur v2. It never turns an effect on.
- **Target** always uses the tuned value, turns the same effects off, and keeps rain, track heat haze, ground cover, grass flattening, volumetric fog and night character shadows on.
- **Off** leaves the engine's values.

### Settings

You'll find these in the addon's row on the **Addons** page and under **ADDON SETTINGS** in game.

| Setting | Id | Default | Does |
|---|---|---|---|
| Apply the graphics profile | `apply_profile` | on | Lowers the settings in [What it lowers](#what-it-lowers) |
| Keep ray-traced AO and DDGI | `keep_rt_ao_ddgi` | off | Leaves ray-traced ambient occlusion and DDGI as you set them |
| In-engine enforcement | `enforce_mode` | `cheaper` | `cheaper`, `target` or `off`, as in [What it holds in the engine](#what-it-holds-in-the-engine) |
| Pin effects | `pin_effects` | on | Turns the effects in [What it holds in the engine](#what-it-holds-in-the-engine) off (and, in Target mode, on); off leaves them to your preset |
| Upscaler | `upscaler` | `auto` | Picks the upscaler: `auto`, `dlss`, `fsr3`, `xess`, `taa`, `off` (no anti-aliasing) or `keep` (your in-game choice) |
| VRS and async compute | `override_vrs_async` | on | Turns on variable rate shading (`Renderer.Gfx.enableVRS`) and async compute (`Renderer.Gfx.enableAsyncCompute`) |
| Grass and ground cover caps | `override_ground_cover` | on | Caps ground-cover instances: `groundCoverMaxInstances` 40000, `groundCover2MaxInstances` 80000, `groundCover2MaxMeshInstances` 4000, and turns off `groundCoverForceUltraQuality` |
| Static instance caps | `override_instance_caps` | on | Sets `Renderer.maxStaticInstances` and `Renderer.maxPVSRenderInstances` to 20000. Distant props can pop in |
| Log setting reads | `log_reads` | off | Writes every engine setting the game reads, and each change, to `swofty/loader.log` |

The graphics profile, the upscaler and the enforcement mode apply when the game reads its settings at launch, so a change to any of them applies on the next launch. The upscaler applies with the profile on or off. FSR 3 is written the way the game reads it (`fsr3="1"` with `cas="1"`); before 0.2.5 a forced or automatic FSR 3 did not engage.

With `upscaler` set to `auto`, the addon picks by GPU:

| GPU | Upscaler |
|---|---|
| NVIDIA RTX 20, 30, 40 and 50 series | DLSS |
| NVIDIA GTX 10 and 16 series | FSR 3 |
| AMD Radeon | FSR 3 |
| Intel Arc | XeSS |
| Other Intel GPUs | FSR 3 |
| Not detected | Your in-game choice |

### Its log

The addon writes to `swofty/loader.log` in the game folder. Its lines start with `[re-opt]`: the GPU it detected, the upscaler it picked, the settings it lowered and each hook it installed or skipped.

## Any Team Transfers

In a Driver Career contract negotiation, every team makes you an offer, so Team Select lets you sign for any team on the grid, your current team included. Without it, the game offers two or three teams ranked close to yours, plus your current team when it renews you.

- Team Select opens when your contract ends and you start the contract event in Team HQ. The contract terms, the signing and the move at the end of the season are the game's own.
- A Progressive Career that follows your driver's real career still signs only for his real team.
- It needs Swofty Launcher 1.5.13 or later, on the Steam or EA build of F1 25. On a game build where the function it hooks has changed, offers stay stock and `swofty/loader.log` says why.
