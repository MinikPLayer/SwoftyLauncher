---
title: Standard library
description: The headers and SwoftyApi tables an engine plugin builds on - mixins, settings, logging, events, overlay, telemetry, database reads and storage.
---

# Standard library

When your [engine plugin](/reference/addons/native-plugins) initializes, it receives a `const SwoftyApi *`. Through that pointer, you can use the mixin system, settings and logging, plus nested tables for events, overlay, telemetry, database reads and storage. These make up the standard library. Each library has one C++ wrapper header and one reference page.

## Libraries

| Library | Header | Feature | API | Thread |
|---|---|---|---|---|
| [Mixin system](./mixin) | `swofty/mixin.hpp` | `hook` | 2 | Game thread |
| [Settings](./settings) | `swofty/mixin.hpp` | `options` | 3 | Flow thread |
| [Logging](./logging) | `swofty/mixin.hpp` | `log` | 1 | Any |
| [Events](./events) | `swofty/events.hpp` | `events` (stable) | 5 | Flow thread |
| [Overlay](./overlay) | `swofty/overlay.hpp` | `overlay` (experimental) | 5 | Render thread |
| [Telemetry](./telemetry) | `swofty/telemetry.hpp` | `telemetry` (experimental) | 5 | Flow thread |
| [Database read](./db) | `swofty/db.hpp` | `db` (stable) | 5 | Caller thread |
| [Storage](./storage) | `swofty/storage.hpp` | `storage` (stable) | 5 | Caller thread |
| [Career](./career) | `swofty/career.hpp` | `career` (stable) | 6, `block` 7 | Flow thread |
| [Popup](./popup) | `swofty/popup.hpp` | `popup` (stable) | 7 | Any; shown on the flow thread |
| [Strings](./strings) | `swofty/lang.hpp` | `lang` (stable) | 8 | Any; `refresh` on the game thread |
| [Career contents](./career-content) | `swofty/career_content.hpp` | `career` (experimental) | 8, feature-detected | Addon init, flow thread |
| [Characters](./characters) | `swofty/characters.hpp` | `career` (stable) | 8, feature-detected | Any; `refresh` on the game thread |
| [Career wizard](./career-wizard) | `swofty/career_wizard.hpp` | `career` (stable) | 8, feature-detected | Any |
| [Era picker boxes](./era-picker) | `swofty/era.hpp` | none | 8, feature-detected | Addon init; others any |
| [Race HUD](./hud) | `swofty/hud.hpp` | `popup` (stable) | 8, feature-detected | Any; posted on the flow thread |
| [Memory](./memory) | `swofty/mem.hpp` | none (header only) | any | Any |

For screens and home tiles, use an addon operation rather than a `SwoftyApi` table; see [UI](./ui). You can find the merged engine-side contract for the version-5 tables in [the native-libraries design](/design/native-libraries).

## Headers

Include the header for the library you need. The [addon template](/reference/addons/template) ships these headers under `native/include/`.

| Header | Location | Contents |
|---|---|---|
| `swofty_api.h` | `packages/sdk/include/` | The C ABI: `SwoftyApi`, mixin records, setting values, the nested tables. MIT |
| `swofty/mixin.hpp` | `packages/sdk/include/swofty/` | The C++ layer: mixin macros, bindings, settings, `swofty::log`, `SWOFTY_ADDON` |
| `swofty/events.hpp` | `packages/sdk/include/swofty/` | Lifecycle events and the frame tick |
| `swofty/overlay.hpp` | `packages/sdk/include/swofty/` | The 2D overlay |
| `swofty/telemetry.hpp` | `packages/sdk/include/swofty/` | Live car and session reads |
| `swofty/db.hpp` | `packages/sdk/include/swofty/` | Game-database reads |
| `swofty/storage.hpp` | `packages/sdk/include/swofty/` | Addon storage |
| `swofty/career.hpp` | `packages/sdk/include/swofty/` | Flow states, the running career, its components, the game bus and career-mode blocks |
| `swofty/popup.hpp` | `packages/sdk/include/swofty/` | The game's message popup with your text |
| `swofty/career_content.hpp` | `packages/sdk/include/swofty/` | Extra career tiles in the Addons row of Driver Career, My Team or both, with their own save slots, and the per-career check |
| `swofty/characters.hpp` | `packages/sdk/include/swofty/` | Per-career character refs, voices and strings |
| `swofty/career_wizard.hpp` | `packages/sdk/include/swofty/` | Per-career new-career wizard control |
| `swofty/era.hpp` | `packages/sdk/include/swofty/` | Boxes in the Addons row of the Grand Prix / Time Trial era picker |
| `swofty/hud.hpp` | `packages/sdk/include/swofty/` | Toasts in the race HUD's own notification |
| `swofty/mem.hpp` | `packages/sdk/include/swofty/` | Guarded memory reads: `read`, `in_image`, `object_like`, `c_string` |
| `swofty/assets.hpp` | `packages/sdk/include/swofty/` | Runtime asset loading; see [Asset API](/reference/addons/asset-api) |
| `swofty/game.hpp`, `swofty/game/*.hpp` | `native/include/swofty/` | The [Game API](/reference/game/): the engine's functions, structs and sites |

If you're writing Rust, the `swofty-sdk` crate (`packages/sdk`) mirrors the C ABI.

## Result codes

The version-5, version-6 and version-7 tables (`events`, `overlay`, `telemetry`, `db`, `storage`, `career`, `popup`) share the same result codes, so you can interpret their results the same way. Each wrapper exposes a `Result` enum; use `name(r)` to turn it into a string:

| `Result` | `SWOFTY_ASSET_*` | Meaning |
|---|---|---|
| `ok` | `OK` (0) | The call succeeded |
| `late` | `LATE` (1) | Too late to apply, or a cursor is exhausted |
| `unavailable` | `UNAVAILABLE` (2) | The loader is older than the table's version, the feature is off, or the lane is not implemented |
| `invalid` | `INVALID` (3) | A bad argument |

Guard every call with `available()`. A table's `available()` returns non-zero only when its feature is on and the core implements it. If the loader offers a version below the table's (5, 6 for `career`, 7 for `career::block` and `popup`) or the table pointer is null, the wrapper returns `unavailable`.

## Threads

| Thread | What runs on it |
|---|---|
| Flow thread | `Vm_Update_FlowStateMachine` each frame. Lifecycle, flow and career events, the `on_frame` tick and telemetry reads fire here; call the career library's game calls (`entity`, `component`, `post`) here |
| Render thread | The swapchain present path. `on_paint` callbacks fire here |
| Caller thread | `db` and `storage` calls run on the thread that calls them |

You can use subscriptions, paint callbacks, and `db` and `storage` calls from any thread. Your callbacks must return at once, without blocking the thread they run on.

## API versions

To see which version the running loader offers, read `SwoftyApi::version`. The header constant, `SWOFTY_API_VERSION` in `swofty_api.h`, is 8.

| Members | Offered by |
|---|---|
| `version`, `log`, `module_base`, `scan` | `version.dll`, as version 1 |
| `resolve`, `register_mixins`, `fields`, `setting`, `watch_setting`, `assets` | `swofty_core.dll`, as version 4 |
| `events`, `overlay`, `telemetry`, `db`, `storage` | `swofty_core.dll`, as version 5 |
| `career` | `swofty_core.dll` 0.2.11, as version 6 |
| `popup`, `career->block`, `career->unblock` | `swofty_core.dll` 0.2.12, as version 7 |
| `lang` | `swofty_core.dll`, as version 8 |

The helpers you use determine which version you need: the mixin helpers in `mixin.hpp` need version 2, the settings helpers version 3, `assets.hpp` version 4, the five table headers version 5, `career.hpp` version 6 (its `block` and `unblock` version 7), `popup.hpp` version 7, and `lang.hpp` version 8. `mem.hpp` needs no table.
