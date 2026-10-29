---
title: Game systems
description: Every game system in the 1.6 standard library as a typed C++ header - its records, registries, events and operations, when each applies in the game, how ready the reverse engineering is, and the limits the core enforces for every registry.
---

# Game systems

Each part of the game you can change, such as cars, tracks, drivers or session rules, is a game system. Each system is a C++ header under `swofty/systems/`, in its own namespace, with:

- its **records**: structs such as `cars::Vehicle`, one item of game data each;
- its **registries**: typed handles such as `cars::vehicles`, where you add records and change other addons' records;
- its **events**: points in the game your code can run at;
- its **operations**: functions that change the running game, such as `tracks::set_track_fuel`.

This addon adds a V10 engine sound and puts it on Classic Seasons' 2017 Ferrari:

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/cars.hpp>
#include <swofty/systems/sounds.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    auto v10 = sounds::engine_sounds.add("v10_screamer", { .bundle = file("sounds/v10_screamer.bdl"),
                                                            .settings_from = sounds::engine_sounds["game/ferrari_2025"] });
    cars::vehicles.layer(cars::vehicles["swofty.classic-seasons-2017/ferrari"], [=](cars::Vehicle &v) { v.engine_sound = v10; });
}

SWOFTY_ADDON()
```

Every system page has the header, a complete addon modelled on a real one, smaller examples for the rest, and the readiness and limits. Each main example also shows the same addon as data files, for addons with no DLL.

## The map

Shipped in 1.6.0:

- **[Seasons and GP/TT content](/std/content)** `swofty/systems/content.hpp`: `content::packages`, `era_boxes`.
- **[Cars](./cars)** `swofty/systems/cars.hpp`: `cars::vehicles`, `packages`, `handling`.
- **[Liveries](./liveries)** `swofty/systems/liveries.hpp`: `liveries::liveries`.
- **[Teams](./teams)** `swofty/systems/teams.hpp`: `teams::constructors`, `identities`, `logos`, `pit_times`.
- **[Drivers](./drivers)** `swofty/systems/drivers.hpp`: `drivers::people`, `identities`, `heads`, `looks`, `stats`.
- **[Helmets, suits and gloves](./wearables)** `swofty/systems/wearables.hpp`: `wearables::helmets`, `suits`, `gloves`, `season_sets`.
- **[Sounds](./sounds)** `swofty/systems/sounds.hpp`: `sounds::engine_sounds`.
- **[Career](./career)** `swofty/systems/career.hpp`: `career::modes`, `contents`, `seasons`, `calendars`, `ratings`, `characters`, `wizards`, `hub_tabs`.
- **[UI and strings](./ui)** `swofty/systems/ui.hpp`: `ui::strings`, `addon_options`.
- **[Sessions](./sessions)** `swofty/systems/sessions.hpp`: `sessions::Rules`, `set_control`.
- **[Tracks](./tracks)** `swofty/systems/tracks.hpp`: `tracks::set_track_fuel`, `copy_track_fuel`, `restore_track_fuel`.
- **[AI](./ai)** `swofty/systems/ai.hpp`: the `ai.controller.update` payload.
- **[The spectator camera](./graphics#the-spectator-camera)** `swofty/systems/spectator.hpp`: `spectator::set_active`, `follow`, `set_camera`, `current`.
- **[Your own systems](./custom)**: an addon's own registries, scope, projection, scoped hooks and service.

Described on their pages, in headers that come later:

- **[Cars](./cars)**: `vehicle.damage_model`, `vehicle.parts`.
- **[Sounds](./sounds)**: `audio.car`.
- **[Career](./career)**: `career.market`, `career.contract_rules`, `career.rnd.department`, `career.rnd.tree`, `career.facility`, `career.economy`, `career.sponsor`.
- **[Tracks](./tracks)**: `tracks::tracks`, `venues`, `data`, `per_package`, `layers`, `slots`, `ai_lines` (1.6.x).
- **[Tyres](./tyres)** `tyres::physical`, `visual`, `classes`, `per_event`, `allocations`, `wear_scales`, `rules`.
- **[Power units, ERS and DRS](./power-units)** `power::units`, `suppliers`, `drs_rules`, `component_rules`.
- **[Sessions and weekends](./sessions)** `sessions::modes`, `formats`, `lengths`, `points`, `rules_profiles`, `safety_car`, `grids`.
- **[Weather](./weather)** `weather::climates`, `forecasts`.
- **[AI](./ai)** `ai::vehicle_configs`, `package_maps`, `track_packs`, `general`, `difficulty`.
- **[Liveries](./liveries)** `liveries::decal_sets`, `sponsor_packs`, `ai_assignments`.
- **[UI](./ui)** `ui::screens`, `tiles`, `texture_slices`, `routes`, `options`, `hud_widgets`.
- **[Graphics, replays and telemetry](./graphics)** `graphics::looks`, `passes`, `settings`.

## When a registry applies

Addons run live in the game. `SWOFTY_REGISTER` only declares what an addon adds and changes. The core applies each change at the moment the game reads that data. Each registry says when, with `RegistryOptions::applies` (`x-applies` in its schema):

```cpp
inline Registry<Vehicle> vehicles{"vehicle", { .applies = Applies::boot }};   // database rows, before the database loads
inline Registry<PackageTrack> per_package{"track.per_package", { .applies = Applies::content }};   // when a box, career or season is active; put back after
inline Registry<TrackData> data{"track.data", { .applies = Applies::session }};   // at session load
inline Registry<Logo> logos{"team.logo", { .applies = Applies::read }};   // on every read, through the registry
```

Files reach the game in one of two ways:

- **Archives** (worlds, cars, heads, helmets, liveries, VTFs) are listed in the game's asset manifest, which the game parses once at boot. The core adds and changes the manifest's groups at that parse. The game then reads the files from the addon's folder, or from overlay archives the core builds at boot. They stay fixed until the next game start. Handling values are the exception: the game reads each car's parts from its VTF when the car is built, and the core applies handling layers there (see [Car physics and handling](/std/systems/car-physics)).
- **Loose files the game opens by path** (engine bundles, language files) are redirected when the game opens them: the game reads the addon's own file instead. The redirect is the core's `file` feature, which is still marked unstable.

An addon can also register and layer while the game runs, from `SWOFTY_ON_LOAD` or an event. The change takes effect at the next moment the game reads that data, and the core says when. Data the game reads at boot is the exception. It never reaches the database from outside `SWOFTY_REGISTER`, and the core refuses it:

```text
[registry] layer from tails.whatif-history on career.season f1_career_progressive/2017 registered at run time; applies from the next session load
[registry] layer from mod.esp on vehicle game/ferrari_2026 refused: vehicle applies at boot; register it in SWOFTY_REGISTER
```

## Limits every registry inherits

The game has hard limits. The core enforces them once, for every registry, and logs what it refuses. A player sees a refusal as a status line on the addon's tile in ADDON SETTINGS and in the launcher.

### Stat ids

A stat id is the number the game's database uses for a row. A record leaves `stat_id` at `StatId::automatic`, and the core allocates it:

```cpp
drivers::identities.variant("alain_prost", drivers::identities["game/jarno_opmeer"], [](drivers::Identity &d) {
    d.number = 1;                                  // d.stat_id stays StatId::automatic
});
```

```text
[registry] stat ids: driver.identity swofty.f1-1990/alain_prost -> 241 (pool 208-254, 57 free after this)
[registry] stat ids: session.mode swofty.f1-1990/f1_1990 -> 16 (pool 5,7,16-31)
```

- **One store per install.** The core keeps the ids it allocates in the launcher's data folder, at `$DATA/registry/ids.json`, keyed by record id. A record gets the same id at every later start.
- **Ids are reused only when no career uses them.** An id stays taken while any career's save refers to it.
- **Each career carries its own id map** in its save entry (`swofty_registry`): every record it uses, with the stat id it was saved with. A save synced to another PC can't swap a track or a driver.

The pools are small and differ per table:

- entrant lists: 0 to 255;
- drivers: 0 to 254, with 58 free. Entrant records pack the driver id in 14 bits, so the real ceiling may be higher, and it needs reverse engineering;
- teams: up to 514;
- vehicles: up to 1,023. Classic Seasons 0.6.4's vehicle ids already run to 599;
- vehicle packages: 0 to 255;
- heads: from 265;
- setup modes and seasons: 31 or lower (they're mask bits). The free setup mode ids are 5, 7 and 16 to 31, which is 18.

Every Grand Prix and Time Trial box shares the `swofty_f1` setup mode. Each box's points, DRS, tyre classes and rules apply at session load. A box takes its own setup mode only when it needs a different shape at boot, such as F1 1990's two qualifying sessions.

### The record budget

The game reserves its value map for the records in the database file before it loads them: 2^20 slots for the stock file, which hold 838,860 records. A record past that makes the map grow during the load, and growing frees every record it moves, so the next read crashes the game. The core counts the records addons register in `SWOFTY_REGISTER` and raises the game's reservation before the first record goes in, and the game still picks the size itself. 2^21 slots hold 1,677,721 records and use 17 MB more memory than stock. The file route already shows the game runs on a 2^21 map: 843,179 records boot.

- **Separate budgets.** The core applies edits from addons made for 1.5 and earlier first, and counts them on their own. It then admits 1.6 records against the room that's left, whole addons in load order. Refusing a 1.6 addon never removes an older addon's edit.
- **A whole addon is refused,** never part of one, with a log line naming it.
- **The safety net.** If the pre-size is unavailable, such as after a game update, the room is about 92,000 records: 838,860 minus the records the game loads, minus a 4,096 margin. The core reports how full the database is after each boot. When it's short, the next launch moves the older addons' edits to the file route (the 1.5.11 fix), padded past 838,860 records, so the game reserves a big enough map for the file. That costs one restart, and the launcher still never learns what 1.6 addons register.

```text
[registry] room: value map sized for 1,677,721 records before load (older addons' edits 84,112; 1.6 records 15,020)
[registry] room: pre-size unavailable on this game build; 81,240 of 91,904 records used
[registry] register: mod.bigpack refused 14,980 records: past the database room (10,664 left); nothing from mod.bigpack applied
```

### The grid

The game has 32 vehicle slots, a 27-car clamp at session enter, an AI table of 30, and a per-vehicle map that rehashes at the 26th car. The real caps are lower on some tracks and code paths:

- 28 cars crashed a Grand Prix at session enter on a community track.
- Replay holders are sized for 22 to 24 cars (`PitStopMG` 0x1800, `CannedEventsSystem` 1024 with an unchecked memcpy at 0x1445d1741).
- Grid slots differ per track. Custom Grid Size's table gives Imola, Singapore and Abu Dhabi 26, and Monaco 28.

Grid size is one boot setting per install, and each box is clamped to it. A track whose world can't hold the grid is left out of the box (see [Tracks](/std/systems/tracks)). A grid above the vehicle slots is refused when the registry resolves, naming the limit:

```cpp
sessions::grids.layer(sessions::grids["game/f1_2026"], [](sessions::Grid &g) { g.cars = 34; });
```

```text
[registry] session.grid game/f1_2026: edit by someone.biggrid refused: 34 cars, the game holds 32 vehicle slots (27 without
           the grid patch); the record keeps 26
[registry] session.grid swofty.f1-1990/f1_1990: 26 cars clamped to 22, the install's grid size
```

### The rest

- **Row counts.** A table reads only the rows below its `_instancecount_`. The core bumps it, and nobody else writes it.
- **Missing keys.** A copied row that lacks a key a stock row has reads garbage, not null. The core copies every key of the donor or base row, the printf-built and stack-built ones included. In the game the database holds only hashes of the key paths, so the copy needs the full key names of each table for the running game build. The launcher's tables have them (`tools/track_keys.py`, `deps.rs`).
- **Side data.** Rows are found by label too: setup presets by package folder, track data by mode mask, AI data by `ai_package_folder/asset_filename`. The core writes the side data with the row.
- **Save heap.** Profile saves live in a 3 MB `SAVE_DATA` heap. A career's snapshots and id map live in its `swofty_registry` entry, which the core keeps outside that heap and writes into the save stream (see [Scopes and snapshots](/std/scopes)).
- **Read once.** The game reads driver statistics once at boot, and AI handling and setup presets once at session enter. Edits on them apply at those moments, or through an event.
- **Both builds.** Every event and operation resolves on the Steam and the EA build, or reports `unavailable`. Database values changed after boot are written in place on the Steam build only. On the EA build and after a game update, those changes report `unavailable`. When a box can't apply its changes for that reason, Grand Prix and Time Trial offer "Play anyway", and careers stop.

## The order

Systems move into the core in this order. It follows readiness, how many addons need each system, and where older addons conflict most:

1. **Career seasons, line-ups and contracts:** the most L2 code. Two addons already wrap the same functions.
2. **Content at boot:** vehicle, package, engine sound, livery, wearables, heads, team and driver identity, texture slices. These have the most addons and most of the file conflicts. They fill the database, and the core owns stat-id allocation and the record budget from the start.
3. **Tracks and track data.**
4. **Tyres:** the custom tyre systems.
5. **Session rules:** grid size, weekend formats, points.
6. **UI and strings.**
7. **Later:** AI, power unit and DRS at run time, weather, graphics, telemetry, replays. The reverse engineering they need is scheduled now: the AI difficulty application, the DRS decision, the engine component selector, the attribute map, a single team-logo resolver and the per-box track list builder.
