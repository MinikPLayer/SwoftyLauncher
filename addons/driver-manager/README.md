# Driver Manager

Creates new F1 25 drivers from the launcher's **Driver Manager** page: name, code, number, nationality, head, helmet, suit, gloves, pose, podium emote, commentary name, ratings, seasons completed and garage slot. Each driver is a free agent in the 2025 and 2026 driver pools of careers started after the driver is added. Needs Swofty Launcher for F1 25 1.0.12 or later.

User guide: [Driver Manager](https://f1.swofty.net/using/driver-manager).

## Layout

| Path | What |
|---|---|
| `launcher/` | The launcher plugin: the page, validation and the build reply. Build with `launcher/build.sh`; it writes `bin/`. |
| `icons/` | The addon icon and its SVG source. |

## Data

The plugin keeps `drivers.json` and `stock.json` in the addon's data folder, shared by every profile. `stock.json` holds the head and helmet set on drivers the game already has, by driver label. It holds each driver and the id allocator: driver ids run from 208 to 253 and look ids from 366 upward, skipping ids the game uses. Ids are never reused. The game's lists of nationalities, heads, kit, poses, emotes and commentary names are read from the clean game database and the English text at run time and cached in the addon's cache folder per game build. Whether each head and piece of kit is installed is worked out on every view and build: a file in the game folder, or a `file.put` target of an addon that is on in the profile (its manifest's `operations.files` and the `file.put` ops in its `assets` and `erp` op files). On launchers that answer the `profile_heads` host call (1.5.12 and later), the head list also holds the heads the profile's addons add to the game, such as an imported face mod, which the build gives a driver head row. Previews of items installed later are made then and added to the cached set.

## Build reply

For each valid driver the build adds, by label:

| Table | Row |
|---|---|
| `entrants.drivers` | The driver: all 129 keys copied from `jarno_opmeer`, then name, code, number, nationality, head, commentary name, birth date, AI traits, seasons and garage slot set. The AI driver config and rev lights stay Jarno's. |
| `entrants.driver_customisations.folder[ai_driver_customisations]` | The look: helmet, suit, gloves, pose, podium emote |
| `driver_statistics.driver_statistics` | The ratings holder: five curves from start to peak |
| `entrants.entrant_lists.entrants` | The entrant that ties the driver to the look |
| `f1_2026_driver_pool`, `f1_2025_driver_pool` | A free-agent row in each, for active drivers only |

Driver Manager builds after every other addon in the profile (`load_after: ["*"]`). Its labels then resolve against the tables a mod leaves, such as a mod made for an earlier game version that moves entrant lists or lowers row counts.

`lang.set` adds `lng_dm_<id>_first`, `_last` and `_full`. For each driver in `stock.json` the build sets `entrants.drivers.driver[_metadata_=<label>].head` and the `helmet` of every `preloaded_driver_customisation_properties` row whose `linked_driver_stat_id` is the label. A head or helmet that isn't installed is left out with a warning. A retired driver keeps every row except the pool rows. A driver with an error is left out of the build.

## Tests

```sh
F1_25_DIR=/path/to/game nix develop -c cargo test --release --manifest-path addons/driver-manager/launcher/Cargo.toml
```

The `fields` map lists every key of each source row. `tests/fixtures/source_rows.json` holds those key lists; `examples/audit.rs` regenerates them by hashing exe identifiers under each source row. `tests/real_db.rs` checks that each new row has exactly its source row's keys and types. It also runs the helper's build over the protocol, applies the reply through the launcher's planner to the game database and reads the rows back. With `DM_BUILT_DB` (a built database, or the overlay's `swofty/assets.json`) and `DM_DATA_DIR` set it checks the output of `swofty build` instead.
