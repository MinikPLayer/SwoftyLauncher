---
title: "Standard library 1.6: decisions"
description: The product decisions from the 1.6 design reviews, the options each one weighed and the choice made, and the engineering calls settled alongside them.
---

# Decisions

Six reviews stress-tested the design from different seats: players, career authors, year pack authors, small mods and the importer, game limits, and the platform as a whole. They found about 40 open questions. Most were engineering calls, listed at the end. These eleven change what players or authors experience. Each lists the options weighed and the one chosen. The pages describe the chosen option.

## 1. What the launcher knows before the game runs

Code addons register in the game. Before the first session, the launcher can't list what an addon adds. That leaves three features with nothing to read:

- the importer's **Applies to** picker;
- the Addon Browser's "Sochi is missing; install the addon that provides it";
- the check before Play for a code addon's tracks and cars.

- **(a)** The launcher knows only `swofty.json`. Those features read the report the core writes after each session (`registry/catalog.json`) and say "as of your last session".
- **(b)** As (a), plus each addon ships a `registry.lock.json` made when it's packed. `swofty dev pack` runs the addon's `SWOFTY_REGISTER` against a recorder and writes down what it registers: ids, labels, what it provides. The Browser and the launcher read it. Nothing is applied from it. The game still applies everything live.

**Chosen: (b).** It's part of the addon, like the manifest, not a cache of the player's game.

## 2. When a career freezes its history

- **(a)** Everything at career creation.
- **(b)** Each season's line-ups, ratings and rules when that season starts. Which box each season uses is still chosen at creation. Drivers and tracks installed mid-career join at the next season.

**Chosen: (b).** A fix to Progressive's 2019 line-up reaches a 2016 career when it gets to 2019, and a what-if edit reaches the next season of a running career. The snapshot is about a tenth of the size, which matters in a 3 MB save heap. A career owner can still choose (a), and the career's hub can offer a "freeze content" toggle.

## 3. After the last year a career covers

Progressive past 2026, or Classic Career past its last pack.

- **(a)** Hold the last year forever: the same grid every season, nobody retires.
- **(b)** Keep the last year's cars and looks, and hand the line-up to the game's market, with retirements extended.
- **(c)** End the career.

**Chosen: (b)** by default, with (a) and (c) as options for the career's owner.

## 4. Real calendars

For careers:

- **(a)** The wizard's calendar every season.
- **(b)** Each season's real calendar, rebuilt at the season break. Missing venues are dropped.
- **(c)** A wizard choice between the two.

For a Grand Prix or Time Trial box's track list:

- **(a)** Every track in the base that fits the box.
- **(b)** The season's real calendar first, then every other fitting track under "More tracks".
- **(c)** Only the calendar.

**Chosen: (c) for careers, defaulting to real calendars; (b) for boxes.**

## 5. Two packs for the same year in one career

Two 2016 packs are installed, and a Progressive career reaches 2016.

- **(a)** Load order picks.
- **(b)** The career setup page shows a choice for each year that has more than one, defaulting to the career addon's preferred pack. The choice is frozen at creation.
- **(c)** Only the pack the career addon names.

**Chosen: (b).**

## 6. Generic AI and physics mods against a year's tuned values

A selector layers every matching record, so a generic AI mod overwrites Classic Seasons' per-year pace and AI tiers. That's how a 2016 Alonso ends up winning.

- **(a)** Selectors win.
- **(b)** A year pack marks its history fields as protected. Another addon's selector skips them unless the player turns on "also change year packs" for that addon.
- **(c)** Year packs always win.

**Chosen: (b).**

## 7. How far an imported mod reaches by default

- **A car port or season pack imported in place** changes every year, careers included. Fanfreluche's 2020 cars end up in Progressive's 2025 season.
- **A mod that overwrites stock files**, such as F1 2012's V8 sounds, also reaches Classic Seasons 2024, whose own cars point at those stock files.
- **Applies to** moves an import onto another car rather than adding one.
- **Two full liveries on one car** mix asset by asset.

**Chosen:**

- Car ports and season packs ask once, "Everywhere or Grand Prix and Time Trial only", defaulting to Grand Prix and Time Trial. Single liveries, helmets and sounds stay everywhere.
- Year packs can pin a stock file, `stock("...")`, which serves the original whatever an import replaced.
- Applies to adds a target and keeps the original.
- For new imports, a later full livery replaces an earlier one whole, and partial liveries still stack. Existing profiles keep the asset-by-asset mix until the player presses the fix.

## 8. A "what if" per profile

All launcher profiles share one game save and one addon storage folder, so a what-if history set up in one profile shows in every profile.

- **(a)** Drop profile scope.
- **(b)** Give each profile its own addon storage. Saves stay shared, and each career gets an id so the launcher knows which profile started it.
- **(c)** Swap the game's save files per profile at Play.

**Chosen: (b).** (c) fights Steam Cloud.

## 9. When something is missing or broken

Chosen defaults:

- An unmet `requires`, or a `breaks` on an installed addon, stops Play with a message naming the fix. `conflicts` only warns.
- If a pack for a career's next season is missing at the season break, the load stops when the pack brings rules or systems. When it only brings cars and looks, the season falls back to the stock year.
- If a box can't apply live (after a game update, or on the EA build), Grand Prix and Time Trial offer "Play anyway", and careers stop.
- The vanilla profile warns before Play when a career needs addons, and backs up saves.
- Players learn that something didn't apply from a status line on the addon's tile in ADDON SETTINGS and in the launcher. The only popups are guards that stop a load.

The career fallback in the second point shows on the status line only, with no popup.

## 10. Database room

The game's database has room for about 92,000 more records, shared by build operations from older addons and registry records. Classic Seasons years plus community tracks is the mix that crashed before the 1.5.11 fix. Applied live, the last registrations past the room would be refused, and a year or a track would silently disappear.

- **(a)** Refuse past the room, with a status line.
- **(b)** The core reports how full the database is after a boot, and the next launch moves older addons' database edits to the 1.5.11 file route, padded past 838,860 records so the game reserves a big enough table: one restart. The launcher still never applies what registry addons register.
- **(c)** Make the in-game fix (sizing the game's table before it loads) a 1.6.0 release blocker.

**Chosen: (c), with (b) shipped as the safety net.**

## 11. Developer tools and experimental APIs

Where the developer tools live (`swofty dev new`, `link`, `run`, `logs`, `check`, `test`, `pack`, and a fake core for unit tests):

- **(a)** In the public launcher behind a Developer mode toggle.
- **(b)** A separate SDK download.
- **(c)** The repository only.

**Chosen: (a).** One loader for development and play.

Experimental APIs in addons published to the Addon Browser: **allowed, with an "experimental API" badge.** Any experimental shape a published addon uses is kept, with an adapter, at the next release. The release check runs those addons and tells their authors.

## Settled in the design

These were decided from the reviews and are going into the pages:

- **Careers:** a career season names its box (`career::Season::box`), with a fallback.
- **Tracks:**
  - Every track in the base is offered in every box where it fits: garages for the box's team count, grid size, AI data for the box's cars, and Time Trial support.
  - A track that doesn't fit is left out of that box. A guard covers it until the track select screen's code is understood.
  - A venue goes to the last addon in load order that provides it, like every other contest.
  - The importer never guesses a venue from a track's code name: `bahrain24` is Sepang.
- **Ids:**
  - One stat-id store per install, in the game. An id is reused only when no career uses it.
  - Each career carries its id map in its save entry, so a save synced to another PC can't swap a track or a driver.
  - Real people and teams get shared `person/` and `constructor/` ids, like `venue/`.
- **Rules per box:** every Grand Prix and Time Trial box shares the `swofty_f1` setup mode. Each box's points, DRS, tyre classes and rules apply at session load. Only boxes that need a different shape at boot get their own mode (F1 1990's two qualifying sessions).
- **Layers:**
  - Layers can say where they apply, such as `.in(setup_mode)` or `.in(career)`, using the event filters.
  - A selector runs once per record, so `*=` doesn't compound on variants.
- **Loading:**
  - `SWOFTY_REGISTER` runs early, and `SWOFTY_ON_LOAD` stays at the stage the manifest asks for.
  - A mixin overlap at the early stage never drops an older addon.
  - Older addons' database edits and registry records have separate budgets, so one registry addon over its room can't drop an older addon's edits.
- **Imports:**
  - Imports and live layers are ordered by load order, in the game.
  - A new version of an installed mod is recognised by its lineage, and the player chooses "Update" or "Install side by side".
  - Liveries fit by car model (`vehicle.model`). An author may declare a livery's target, and players pick from cars that fit.
- **Tags:** the core defines shared tags (`c/f1_cars`, `c/street_circuits`), and any addon can add records to one.
- **Failures and naming:**
  - A faulting `after` handler is isolated and logged. A faulting `before` or `replace` handler crashes the game with the addon named.
  - An addon can keep its old id as an alias after a rename, when the Browser author is the same.
- **Car handling** (from the car-struct reverse engineering, PR #441): handling applies as each car is built, at every session load, so an option change reaches the next race without a restart. `cars::live` changes a car already on track, with the core handling the game's scrambled values, its derived values and its rebuilds. R&D upgrades are the handling file's `radUpgrade*` blocks.
- **Unchanged from 1.5:**
  - Raw overwrites on catalogued hook points follow the 1.5 rule for every addon: nothing is restricted.
  - Grid size stays one boot setting per install for 1.6.0, and each box is clamped to it.
  - Hook points the game calls every frame use tables built at content or session moments.

## Scope decisions (swofty, 2026-10-28)

- **Identities, career teams, logos and wearables move into 1.6.0.** A career with entirely new teams (up to 12), their logos and badges per context, and the drivers' suits, helmets and gloves are records with image files, usable as JSON data files, with no database editing by hand.
- **Everything is in 1.6.0 except tracks.** What was 1.7.0 and 1.8.0 ships in 1.6.0: the older libraries underneath registries, `liveries::liveries`, the game hook points and the career operations. The tracks system, every base track in every fitting box and the track select hook move to 1.6.x.
