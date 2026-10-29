---
title: Rollout and docs
description: The releases that ship the proposed 1.6 standard library (1.6.0 and 1.6.x), what each must prove, and how its hand-written public docs and guides are laid out.
---

# Rollout and docs

The 1.6 standard library ships in two releases. 1.6.0 brings everything except tracks: the registry, the content registries, the older libraries underneath registries, identities, wearables, the first game hook points and the core's career operations. 1.6.x brings the tracks system. `SWOFTY_API_VERSION` is 9 for both.

## The releases

Each release goes through the usual flow: a prep commit on main, the main CI, then the tag. Each one needs swofty's go-ahead in the chat where it's prepared.

### 1.6.0: the registry, live in the game

**Ships:**

- **The registry in the core:**
  - `Registry<T>` handles in one header per system (`swofty/systems/<system>.hpp`), records as structs (`SWOFTY_RECORD`), `Ref<T>` between records, `Keyed` lists and `extra` for the round trip;
  - `swofty::json`, for reading another addon's records without its header;
  - `SWOFTY_REGISTER`, which runs at the early stage in load order, while `SWOFTY_ON_LOAD` stays at the stage the manifest asks for;
  - `add`, `variant` and dynamic `provide`, under each addon's own keys;
  - layers as edits, with selectors (`where`, `under`, `tagged`), option-bound edits, `.in(...)` filters, origins, conflicts and validation;
  - core tags (`c/f1_cars`, `c/street_circuits`), which any addon can add records to;
  - `Registry::protect`, for the fields a year pack keeps from other addons' selectors unless the player turns on "Also change year packs";
  - scopes (global, profile, career), adapters between majors, `watch` and `status`.
- **The core's live apply paths.** The core applies each registry at the moment the game reads it (`Applies::boot | content | session | read`), and logs every apply and every put-back:
  - `boot` records go into the game database in memory before it loads;
  - `content` changes apply when a box or a career becomes active, and are put back when it ends;
  - `session` changes apply at each session load;
  - `read` registries answer every read through the registry;
  - files the game opens come from the addon's folder: archives through the asset manifest, patched at boot, and loose files redirected when the game opens them.
- **Career snapshots per season.** A career freezes each season's line-ups, ratings and rules when that season starts, in the `swofty_registry` save entry. The entry sits outside the game's 3 MB save heap, within each snapshot registry's `snapshot_budget` and 256 KB per career in all. Which box each season uses is chosen when the career is created. A career's owner can freeze everything at creation instead (`career::Freeze::at_creation`).
- **Ids.** One stat-id store per install, in the launcher's data folder (`$DATA/registry/ids.json`). An id is reused only when no career uses it. Each career carries its id map in its save entry. Real people and teams get shared `person/` and `constructor/` ids, like `venue/`.
- **Database room.** The core sizes the game's database table before the game loads it, from the records addons register. This is a 1.6.0 release blocker. Edits from addons made for 1.5 and earlier and registry records have separate budgets. As a safety net, the core reports how full the database is after each boot, and the next launch moves older addons' database edits to the 1.5.11 file route, padded so the game grows its own table. That costs one restart.
- **Events, hooks and services:** `swofty::events::on<E>()` with the context and selectors, the hooks table with addon-defined points, and services. A faulting `after` handler is isolated and logged. A faulting `before` or `replace` handler crashes the game with the addon named.
- **The system headers** for the first content: `career::seasons` and `career::ratings`, `content::packages` and `content::era_boxes`, `cars::vehicles` and `cars::packages`, and `sounds::engine_sounds`.
- **The launcher:**
  - it installs addons, builds the operations of addons made for 1.5 and earlier, checks dependencies and starts the game;
  - the dependency check reads `swofty.json`'s `requires`, `registry.json` and each addon's `registry.lock.json`; an unmet `requires`, or a `breaks` on an installed addon, stops Play with a message naming the fix, and `conflicts` only warns;
  - each profile has its own addon storage, saves stay shared, and each career gets an id, with the launcher recording which profile started it;
  - the conflicts view shows the core's last `registry/conflicts.json`, and Send Logs adds `registry/effective.json`;
  - an addon's tile in the launcher and in ADDON SETTINGS shows a status line when something didn't apply.
- **Developer mode** in the launcher, with the `swofty dev` tools: `new`, `link`, `run`, `logs`, `check`, `test` with a fake core, and `pack`. `swofty dev pack` runs the addon's `SWOFTY_REGISTER` against a recorder and writes `registry.lock.json`: the ids it registers, their labels and what it provides. Nothing is applied from it.
- **Progressive Career on the registry:** it provides `career::seasons` and `career::ratings`, every DLL reads the effective values instead of `years.json`, and it publishes the `progressive.history` service and the `swofty.progressive.season.apply` point. Its `GetProcAddress` exports stay.
- **The first career operations,** moved into the core from Progressive's proven seat, swap and ratings code: `career::apply_lineup`, `career::set_driver_ratings`, `career::swap_drivers` and `career::set_slots`. Progressive calls them on a 1.6 core and keeps its own path for older cores.
- **Classic Seasons on the registry:** each season addon provides its `content::packages`, `content::era_boxes`, `cars::vehicles` and `sounds::engine_sounds` records under its own keys. Its hand-written operations shrink to what those records don't cover.
- **Two example addons**, which the first two guides build:
  - `templates/std/whatif`: a launcher page that saves seats in the addon's per-profile storage, and a DLL that applies them as a `Scope::profile` layer on `career::seasons`;
  - `templates/std/engine-sound`: an engine sound under its own key, a layer that puts it on the 2017 Ferrari, and an "F1 2017 (V10)" variant of the 2017 package with its own Grand Prix and Time Trial box.
- **Release tooling:** the compatibility gate (`tools/compat/`), `tools/std/schemacheck.py`, and the save fixtures for snapshots.

**Stays as in 1.5 for 1.6.0:**

- Grid size is one boot setting per install, and each box is clamped to it.
- Raw overwrites on catalogued hook points follow the 1.5 rule for every addon. Nothing is restricted.

**Done when:**

- the gate passes on every Addon Browser addon made for 1.5 and earlier;
- Progressive on the registry matches Progressive 1.2 in game: the same seats, swaps, ratings, logos, fuel and follow contracts in a 2016 and a 2017 career;
- the what-if example's 2017 change shows on Choose Driver, on the grid, in follow mode and in the season's ratings. A career already in its 2017 season keeps the old grid, and a career still in 2016 races the change when it reaches 2017;
- Classic Seasons on the registry gives the same game as 0.6.x: the database the core loads matches, row for row, the one 0.6.x's operations build, and Time Trial shows the same cars, liveries and sounds;
- the engine sound example plays its bundle on the 2017 Ferrari in Time Trial, and the V10 variant shows as its own box;
- a profile whose registry records go past the stock room boots with every record applied;
- the public docs below are written.

### 1.6.0, second half: everything else except tracks

**Ships:**

- career contents, Team HQ tabs, the career wizard, characters, strings and options stored in registries underneath their 1.5 calls: `career::contents`, `hub.tab`, `career::wizards`, `career::characters`, `ui::strings` and `addon.option` (era boxes already are);
- the content registries, applied live: `liveries::liveries` and `wearables::helmets`;
- `teams::identities` and `teams::logos` (names, colours, logos per context), `drivers::identities` (names, numbers, looks) and the wearables that link a driver's suit, helmet and gloves, so a career can have entirely new teams, up to 12 per season (`career::apply_teams`);
- the game hook points `career.calendar.add_activity`, `career.contract.decide`, `career.contract.sign` and `session.fuel.load`;
- the operations `career::set_seat`, `career::set_car_number`, `teams::set_logo` and `tracks::set_track_fuel` (`career::apply_lineup`, `career::set_driver_ratings` and `career::swap_drivers` are in the first half);
- the migration guide from 1.5 database operations and `file.put` to the registry, with Classic Seasons as the worked example.

**Done when:**

- the shim tests pass;
- the gate shows no change in the mixin chains of addons made for 1.5 and earlier;
- Progressive and Career Editor use the hook points instead of their own wraps of those functions;
- Progressive and Career Editor call the operations instead of their own copies of that code;
- a career with entirely new teams (a 2012 career) shows their names, colours, logos and drivers' wearables in driver select, team select, the grid and the HUD, without editing a database file by hand, and never touches the stock 2026 modes.

### 1.6.x: the tracks system

**Ships:**

- `tracks::tracks`, `tracks::venues`, `tracks::data` and `tracks::per_package`;
- every track in the base offered in every box where it fits: garages for the box's team count, grid size, AI data for the box's cars, and Time Trial support. A box lists the season's real calendar first, then every other fitting track under "More tracks";
- the track select list hook (`ui.track_select.build`), which still needs reverse engineering. Until then, a guard covers a track that doesn't fit.

**Done when:**

- a track provided as a record appears in its box, with its calendar slot, garages and AI data;
- every base track fitting a box is offered in it, and a track that doesn't fit is left out.

### After 1.6

- A new registry, hook point or operation ships as experimental first. It becomes stable once two addons use it (see [Status](/std/versioning#status)).
- Addons on the Addon Browser may use experimental APIs. The Browser shows an "experimental API" badge on them.
- Any experimental shape a published addon uses is kept, with an adapter, at the next release. The release check runs those addons and tells their authors.

## The docs

The public docs for 1.6 are written by hand. Nothing in them is generated.

### The order

The Developers section opens with what you can build, then the guides, then the registry and its subfeatures, then the game systems, and only then the parts that connect addons:

- **Start here:** what you can build, what an addon is, your first addon, the templates.
- **[Guides](/make/)** (`/guide/` is the player guide). Each one builds one addon from an empty folder to publishing: the folder, `swofty.json`, the code, trying it in game, what happens when something it needs is missing, and publishing. There are seven:
  - a what-if history editor;
  - an engine sound pack;
  - a livery;
  - a track;
  - a year pack;
  - a career mode;
  - your own system.
- **The registry:** [the registry in one example](/std/), [records and schemas](/std/registry), [keys, bases and variants](/std/bases), [how changes reach careers](/std/inheritance), [providers](/std/providers), [reading](/std/reading), [layers](/std/layers), [scopes and snapshots](/std/scopes), [when changes apply](/std/build), [versioning](/std/versioning).
- **Game systems:** an [overview with the limits](/std/systems/) every registry inherits, then one page per system, from [seasons and GP/TT content](/std/content) and [cars](/std/systems/cars) to [your own systems](/std/systems/custom). Each page opens with the system's header: its records, its events with their payloads, and its operations. It ends with what's ready, what's unknown, and a "Without code" section.
- **Connecting addons:** [events](/std/events), [hooks](/std/hooks), [services](/std/services), [operations](/std/operations), [dependencies and guards](/std/dependencies).
- **In-game code**, with the libraries of 1.5 and earlier (`/reference/std/*`).
- **Launcher plugins**, ending with [what the launcher does with registries](/std/launcher).
- **Mods and importing**, starting with [the Simplified importer](/std/importer).
- **Packaging and publishing:** `swofty.json`, [`registry.json`](/std/manifest) and publishing.
- **Reference:** the command line, [the C ABI](/std/abi), [compatibility with older addons](/std/compatibility) and the build operations.

### How each page is written

- **One sentence, then code.** Each page opens with a sentence saying what the subfeature is, then a complete example: a whole `SWOFTY_REGISTER`, a whole layer, a whole handler, with the includes and `SWOFTY_ADDON()`. Then the details, each with a smaller example.
- **Examples in cards.** A page's main example and each section's main example sit in a Split with an Example card: the code, the same thing as a data file where there is one, and the addon's file tree.
- **Code over prose.** If a sentence can be a code comment next to the line it describes, it goes there.
- **Few tables.** Tables only for small fixed lookups such as result codes. Everything else is code, a short list, or a log excerpt.
- **Real logs.** Where the core logs something (a conflict, a dropped layer, a snapshot), the page shows the actual log line, as it appears in `loader.log`.
- **Examples that compile.** Every C++, Rust and JSON example longer than a few lines lives in a real addon under `templates/std/`, and the page includes it with VitePress's `<<<` file include. CI builds those addons and validates their data files against the schemas.
- **Status at the top.** A page about an experimental or superseded feature starts with a VitePress `<Badge>` and names the release it arrived in.
- **The same voice as the current docs:** short, direct sentences that state facts.

### What changes in the existing docs

- The library pages for 1.5 and earlier (`/reference/std/*`) stay. Each gets a short note at the top linking to its 1.6 system page, and a line saying the older calls keep working.
- `/reference/addons/manifest` links to `registry.json` and to `registry.lock.json`.
- `/reference/addons/launcher-api` gains the what-if page from [The launcher side](/std/launcher): a page that edits only its own addon's per-profile storage, with the existing host calls.
- Every release note gets a "Standard library" section, quoted from each registry's `CHANGES.md`.
