---
title: Coverage
description: Every addon on the Addon Browser and the common imported mods, what each one does, how it's built on the 1.6 registry, and the conflicts that ends.
---

# Coverage

This page checks the 1.6 standard library against every addon on the Addon Browser, plus Custom Grid Size and about 110 imported mods from 37 player bundles. Each card says what the addon does, how it's built on the registry, and which conflicts that ends.

For example, AI Racing Tweaks sets its AI values on 599 stock cars, one by one. On the registry it's one selector layer on `ai::vehicle_configs`, which reaches every F1 car: stock, Classic Seasons, F1 1990 and imported cars.

An addon made for 1.5 and earlier keeps working unchanged until its author moves it onto the registry (see [Compatibility](/std/compatibility)). A card's registry form applies from that point.

Two terms the cards use:

- A **layer** is an edit another addon makes to a record it doesn't own. A **selector** layer edits every record that matches, including records added later.
- A **variant** is a record under your own key that extends another record and keeps receiving the layers on it.

## Seasons

### Classic Seasons

`swofty.classic-seasons` and its seasons 2016 to 2024: 10 addons.

- **What it adds:** database rows and files for each season, about 99 added rows, 170 set records and 430 `put-record`s per season, with 80 car packages, 164 helmets, engine bundles, logos, names and the era boxes.
- **On the registry:** each season provides its records under its own keys: `content::packages`, `content::era_boxes`, `cars::vehicles`, `cars::packages`, `sounds::engine_sounds`, `liveries::liveries`, `wearables::helmets`, `wearables::suits`, `wearables::gloves`, `teams::identities`, `drivers::identities`, `drivers::heads` and `ui::strings`. The core applies them in game. Its generators shrink to writing records.
- **Its boxes:** every season's Grand Prix and Time Trial box shares the `swofty_f1` setup mode, and each box's points, DRS, tyre classes and rules apply at session load.
- **Its history:** a season marks its per-year pace and AI tiers as protected (`Registry::protect`). Another addon's selector skips those fields unless the player turns on "Also change year packs" for that addon.
- **Stock files it relies on:** a season's cars that use stock files pin them with `stock(...)`, which serves the original whatever an import replaced, such as F1 2012's V8 sounds.
- **Ends:**
  - whole logo-image replacement: logos become `teams::logos` records and texture slices (`ui::texture_slices`);
  - copied stock rows: they become variants;
  - hand-picked stat ids: the core's stat-id store gives them out.

## Career

### Progressive Career

`swofty.progressive`

- **What it adds:** career content, a wizard page, a Team HQ tab and 11 DLLs. Five of the DLLs read `years.json`. They call each other through `GetProcAddress`, and wrap the game's contracts, standings, logos and view-model resolver.
- **On the registry:**
  - its career mode is a variant of the 2026 Driver Career (`career::modes`, `game/f1_career_2026`);
  - it provides `career::seasons` (each season names its box, with a fallback), `career::ratings`, `career::calendars`, `career::rnd_trees` for its R&D development, and `tracks::data` for each season's fuel;
  - it subscribes to the contract, calendar, season, standings (`career.standings.rebuild`) and fuel hook points;
  - it publishes the `progressive.history` service.
- **Ends:** the duplicate wraps with Career Editor on standings and contracts, and the `GetProcAddress` coupling.

### Classic Career

`career.classic-career`, by Bitey.

- **What it adds:** its own tile and data group, two screens, and team names and colours written across the whole profile.
- **On the registry:** `career::contents`, `career::wizards` and `ui::screens`. Its season picker reads the effective `content::packages` and `content::era_boxes` records instead of scanning other addons' operations. Its team names and colours are a career-scope layer on `teams::identities`.
- **Ends:** names and colours leaking into every other career.

### Custom Career Length

`swofty.career-length`

- **What it adds:** longer event lists for the stock `F1_career_22` careers only.
- **On the registry:** a selector layer on every `career::calendars` record that sets `seasons`. Progressive's and Classic Career's calendars get it too. A career that must keep its length, such as F1 1990's one season, protects the field.
- **Ends:** other addons' careers stuck at 10 or 20 seasons.

### Career Mode Editor

`swofty.career-editor`

- **What it adds:** a live editor for seats, ratings, the calendar, the wallet, R&D, facilities and weather, with its own wraps.
- **On the registry:**
  - career-scope layers on `career::seasons`, `career::ratings` and `career::calendars`;
  - the operations `career::set_wallet`, `career::rnd_start`, `career::set_facility`, `career::add_weekend`, and `weather::script` for the weather;
  - the shared contract and season hook points.
- **Ends:** the duplicate wraps with Progressive.

### Driver Manager

`swofty.driver-manager`

- **What it adds:** new drivers on stat ids 208 to 253 that it picks itself, and look rows. It loads after everything else (`load_after: *`) to dodge label moves.
- **On the registry:** `drivers::identities` variants (extending `game/jarno_opmeer`), `drivers::looks`, `drivers::heads`, `drivers::stats`, and free-agent pools (`drivers::Identity::pools`). Its ids come from the core's stat-id store.
- **Ends:** id collisions with F1 1990 and Custom Grid Size, and contested look rows with Helmet Exchange.

### R&D: Tyres & Thermal Management

`swofty.rnd-tyres-thermal`

- **What it adds:** a fifth R&D department in native code, 91 handling files replaced whole, and a block on My Team.
- **On the registry:**
  - a `career::rnd_departments` record, with its upgrades in `career::rnd_trees`;
  - field edits on `cars::handling` (the tyre thermal fields and `rad_upgrades`);
  - `ui::texture_slices` for the pie art;
  - a guard on My Team (`guards::on`);
  - `deps::used(...)`, called in each career that uses a Tyres upgrade, which records that the career needs the addon.
- **Ends:** handling clashes with Heavier Damage and with every imported livery that carries handling files, and careers silently losing Tyres progress.

### Simulate Sessions

`kizi.simulate-sessions`

- **What it adds:** a flag on four named stock game modes.
- **On the registry:** a layer on each stock mode it changes in `career::modes`, such as `game/f1_career_2026`. Modes other addons make are variants of the stock ones and get the flag too.
- **Ends:** Progressive and Classic Career careers missing the flag depending on load order.

## Teams, drivers, liveries, helmets

### Team Names

`custom.team-names`

- **What it adds:** the 2026 team rows and names.
- **On the registry:** its launcher page writes the player's choices to the addon's own storage, and its code applies them as profile-scope layers on `teams::identities`. It keeps its presets in a registry of its own.
- **Ends:** last-write-wins against Classic Career and Progressive. Load order decides per field, and the conflicts report names both addons.

### More Special Liveries

`mildtdesign.more-special-liveries`

- **What it adds:** special liveries, plus a whole replacement of `settings_container.ui`.
- **On the registry:** `liveries::liveries` records marked `special`, `liveries::ai_assignments` for the livery each AI team races, and a `ui::screens` record for its page.
- **Ends:** wiping the core's ADDON SETTINGS pages and Classic Career's screens.

### Helmet Exchange

`giggles.helmet-exchange`

- **What it adds:** helmets imported from helmet mods, equipped on drivers, with its own Team HQ tab.
- **On the registry:** `wearables::helmets` records that say who may wear them, a career-scope layer on `drivers::looks` for the equipped helmet, and a `hub.tab` record with its own page. Its launcher page converts the helmet mods into its storage, and its code registers each one.
- **Ends:** contested helmet fields with Driver Manager, Classic Seasons and Progressive.

### Real Life Logos and BP Ultimate Livery

`crew8006.real-life-logos` and `crew8006.bp-ultimate-livery`

- **What they add:** whole `shader_package_2025` archives, put as files: Real Life Logos replaces `customisation_decals` (the My Team decal logos), and BP Ultimate Livery replaces `object_my_team` (the My Team car material), each with its `_amd` pair.
- **On the registry:** they stay file operations, outside the livery registries.
- **Still a clash:** two such packs, or an imported `.serps` on the same archive, replace each other by file. The conflicts view lists the clash next to registry conflicts. A registry that merges shader archives per resource isn't designed.

## Tracks

### Sochi and Autopolis

`mod.sochi` and `mod.autopolis`

- **What they add:** `track.add` copies a slot (Autopolis takes Suzuka's) and sets about 400 track-data records.
- **On the registry:** `tracks::tracks` records that claim a slot and provide a venue (`venue/sochi`), with `tracks::data` per data set and season. `Track::base` names the donor track instead of `copy_slot`, and the core's stat-id store pins each track's id. The track is offered in every box where it fits.
- **Ends:** two mods silently sharing a slot (two imported Igora Drives), and careers loading a different track after the track list changes.

### More Aggressive AI

`kizi.f1-25-26-more-aggressive-ai`

- **What it adds:** a replacement of every track's common world archive for its AI line.
- **On the registry:** a `tracks::ai_lines` record for each stock track it changes. A line names the track record it was made for, and a track mod with a different layout is a different record.
- **Ends:** an AI line applied to a track mod with a different layout.

### Custom Grid Size

Ours.

- **What it adds:** per-track slot and garage edits for 22 to 30 cars, chosen by an option.
- **On the registry:** a `content::packages` variant of `game/f1_2026` with more teams; `tracks::slots` records per world, served when a grid that size is active; and `sessions::grids`, which refuses a size the world can't hold. Grid size is one boot setting per install, and each box is clamped to it.

## AI, sessions and driving

### AI Racing Tweaks

`kizi.ai-tweaks`

- **What it adds:** AI values on 599 stock cars, named one by one, and native contact and steering hooks.
- **On the registry:** a selector layer on `ai::vehicle_configs` that reaches every F1 car, including added ones. Its steering correction is a field edit (`steering_pid`), and its contact handling runs from `ai.config.load` (`ai::events::ConfigLoad`). Its selector skips year packs' protected fields unless the player turns on "Also change year packs".
- **Ends:** Classic Seasons, Driver Manager and imported cars never getting its values.

### AI Drive and Pit Lane

`kizikafuka.ai-drive` and `kizi.pitlane`

- **What they add:** both take and give control of the player's car through their own hooks.
- **On the registry:** `session.player.control`, one ordered event for who drives the player's car, and `session.pit_lane.control` for the pit limiter.
- **Ends:** two addons patching the same control state.

### Parts Detach

`vmknolle.parts-detach`

- **What it adds:** its own byte patterns for the front wing init and the debris.
- **On the registry:** `car.parts.register` (`cars::events::PartsRegister`) and `cars::parts` records, with debris as a field of each part. Its patterns become bindings the core verifies (see [below](#bindings-an-addon-brings)).
- **Ends:** double registration when another addon embeds the same module (Race Director does), and breaking on game updates.

### Heavier Damage Realism

`william09leo.heavier-damage-realism`

- **What it adds:** 16 pre-built handling sets per car, chosen by an option, with 91 handling files replaced whole.
- **On the registry:** option-bound selector layers on `cars::handling`: the wing and suspension damage fields, and the 2026 cars' performance (`core_aero`). Its selectors skip year packs' protected fields unless the player turns on "Also change year packs".
- **Ends:** the clash with R&D Tyres, and 16 copies of every car.

### RE Optimization

`xanpavle.re-optimization`

- **What it adds:** engine setting overrides from inside an early mutex hook.
- **On the registry:** `graphics::settings` records, applied when the game reads each setting (`graphics.setting.read`).
- **Ends:** a second graphics addon fighting it over the same reads.

### Replay Unlocker

`nitrocosm.replay-unlocker` runs in the launcher only. Nothing changes.

## Imported mods

The importer writes data files for the mods it recognises. Imports and live layers are ordered by load order, in the game. A new version of an installed mod is recognised by its lineage, and the player chooses "Update" or "Install side by side".

- **Tracks** (21 of 32 players; Hockenheim 19, Sepang 17, Nurburgring 14): `tracks::tracks` records with venues and pinned ids. A season's calendar finds them by venue. The importer never guesses a venue from a track's code name: `bahrain24` is Sepang.
- **Liveries** (60 ids, 53 of them `.serps`): `liveries::liveries` records plus layers on `cars::vehicles`. Handling files inside them are split out as their own layer on `cars::handling`. A livery fits by car model (`cars::Vehicle::model`); an author may declare its target, and players pick from cars that fit. For new imports, a later full livery replaces an earlier one whole, and partial liveries stack.
- **Car ports** (fanfreluche F1 2016, 2019, 2020) **and season packs** (F1 2012 Temporada): `cars::vehicles` variants per team and a dormant `content::packages` record. The importer asks once, "Everywhere or Grand Prix and Time Trial only", defaulting to Grand Prix and Time Trial. An "own GP/TT entry" lets ports coexist.
- **Engine sound packs:** `sounds::engine_sounds` records and layers. A pack's options become variants, one chosen per car.
- **Helmets, driver models and team logos:** layers on `wearables::helmets`, `drivers::heads`, `drivers::looks` and `teams::logos`.
- **Applies to** adds a target and keeps the original.
- **Everything else** stays a raw build operation and builds as it does in 1.5.

See [The Simplified importer](/std/importer) for how each is recognised.

## Bindings an addon brings

Several third-party DLLs (AI Tweaks, Pit Lane, AI Drive, Parts Detach) find game functions with their own byte patterns, and break on game updates and on the EA build. An addon can ship its patterns as bindings:

```json
// kizi.pitlane/bindings.json
{ "bindings": [ { "name": "PitLane_Set_Limiter", "pattern": "48 89 5C 24 ?? 57 48 83 EC 20 8B FA", "resolve": { "kind": "addr" } } ] }
```

The core checks each pattern against the running exe at load, the same way it checks its own. A pattern that doesn't match one unique site drops only that addon's hooks on it, with a log line, not the whole DLL. Patterns that prove stable are offered to the knowledge base, where they become core bindings with verify rows that resolve on both builds.

## What the coverage added to the design

- `cars::handling` with field-level edits ([Cars](/std/systems/cars)).
- [Selector layers](/std/layers#selectors), [protected fields](/std/layers#protected-fields) and [option-bound layers](/std/layers#option-bound-layers).
- [Copies as variants](/std/bases#copies-are-variants).
- Tracks with slots, venues and pinned ids ([Dependencies](/std/dependencies#tracks-have-stable-identities)).
- `ui::screens` and `ui::texture_slices`, which end whole-resource UI replacements.
- Driving events: `session.player.control`, `session.pit_lane.control` and `car.parts.register`.
- Live career operations for Career Editor.
- Bindings an addon brings, above.
- Raw build-operation clashes in the conflicts view, next to registry conflicts.
