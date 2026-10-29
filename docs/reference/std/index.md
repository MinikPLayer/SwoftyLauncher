---
title: Standard library
description: The headers and SwoftyApi tables an engine plugin builds on - mixins, settings, logging, events, overlay, telemetry, database reads, storage, career, popups, strings and the career front end.
---

# Standard library

The standard library is the set of C++ headers and `SwoftyApi` tables an [engine plugin](/reference/addons/native-plugins) calls to hook the game, read its state and change what the player sees.

## SwoftyApi

The loader calls your DLL's `SwoftyAddonInit` export with a `const SwoftyApi *`. `SWOFTY_ADDON()` writes that export for you and keeps the pointer in `swofty::api`. Every wrapper header reads its table from there.

```cpp
// what SWOFTY_ADDON() expands to (swofty/mixin.hpp)
extern "C" __declspec(dllexport) const uint32_t SwoftyAddonApi = SWOFTY_API_VERSION; // the version you built against: 8
extern "C" __declspec(dllexport) int SwoftyAddonInit(const SwoftyApi *a)
{
    return ::swofty::detail::init(a, nullptr); // stores a in swofty::api, resolves bindings, registers mixins
}
```

`SwoftyApi` is the C struct in `swofty_api.h`. `version` says which members the running loader fills:

```c
typedef struct SwoftyApi {
    uint32_t version;                                // what the loader offers: 1 from version.dll alone, 8 from swofty_core.dll
    void (*log)(const char *fmt, ...);               // v1, version.dll: write a line to swofty/loader.log
    uintptr_t (*module_base)(void);                  // v1, version.dll: the base address of F1_25.exe
    int (*scan)(const char *section, const char *pattern, uintptr_t *out); // v1, version.dll: find a byte pattern
    uintptr_t (*resolve)(const char *id);            // v4, swofty_core.dll: a binding's address, or 0
    int (*register_mixins)(const SwoftyMixin *mixins, size_t count);       // v4: from SwoftyAddonInit only
    void *(*fields)(const void *obj, size_t size);   // v4: your addon's data on a game object
    int (*setting)(const char *id, uint32_t type, SwoftySettingValue *out); // v4: read a setting
    int (*watch_setting)(const char *id, SwoftySettingFn fn, void *user);   // v4: called when a setting changes
    const SwoftyAssets *assets;                      // v4
    const SwoftyEvents *events;                      // v5
    const SwoftyOverlay *overlay;                    // v5
    const SwoftyTelemetry *telemetry;                // v5
    const SwoftyDb *db;                              // v5
    const SwoftyStorage *storage;                    // v5
    const SwoftyCareer *career;                      // v6, core 0.2.11 (block and unblock: v7)
    const SwoftyPopup *popup;                        // v7, core 0.2.12
    const SwoftyLang *lang;                          // v8
    const SwoftyCareerContent *career_content;       // v8 layout; NULL on a core without it
    const SwoftyCharacters *characters;              // v8 layout; NULL on a core without it
    const SwoftyCareerWizard *career_wizard;         // v8 layout; NULL on a core without it
    const SwoftyEra *era;                            // v8 layout; NULL on a core without it
} SwoftyApi;
```

## A plugin in full

This plugin uses two libraries: [events](./events) tells it when a session starts, and [storage](./storage) keeps a counter in its own folder between launches.

```cpp [native/src/session_counter.cpp]
#include <cstdlib>
#include <string>
#include <swofty/events.hpp>
#include <swofty/mixin.hpp>
#include <swofty/storage.hpp>

static void on_session_start(const SwoftyEvent *, void *) // runs on the flow thread: return at once
{
    std::string text;
    int sessions = 0;
    if (swofty::storage::read("sessions.txt", text) == swofty::storage::Result::ok) // swofty/addons/<id>/sessions.txt
        sessions = std::atoi(text.c_str());
    sessions++;
    swofty::storage::Result r = swofty::storage::write("sessions.txt", std::to_string(sessions)); // temp file, then rename
    swofty::log("[session-counter] session %d started (write %s)", sessions, swofty::storage::name(r));
}

SWOFTY_ON_LOAD // runs once, after every plugin's mixins are installed
{
    if (!swofty::events::available() || !swofty::storage::available()) {
        swofty::log("[session-counter] needs the events and storage libraries (API 5)");
        return;
    }
    swofty::events::Result r = swofty::events::subscribe(SWOFTY_EVENT_SESSION_START, on_session_start);
    swofty::log("[session-counter] subscribe: %s", swofty::events::name(r));
}

SWOFTY_ADDON()
```

```json [swofty.json]
{
  "schema_version": 1,
  "id": "author.session-counter",
  "name": "Session Counter",
  "version": "1.0.0",
  "author": "Your name",
  "kind": ["native"],
  "compat": { "game": "f1_25" },
  "requires": [{ "id": "swofty.core", "version": ">=0.1.0" }],
  "operations": { "native": [{ "dll": "native/session_counter.dll" }] }
}
```

`swofty/loader.log` after a launch and the first session:

```text
addon session_counter.dll: loaded at 00006fffea3c0000, calling SwoftyAddonInit (API v8)
addon session_counter.dll: SwoftyAddonInit returned 1 (ok)
[core] addon session_counter.dll: registered 1 mixin(s)                        <- SWOFTY_ON_LOAD is one mixin
[events] subscribe session.start by author.session-counter -> ok token 3, queued until the events setup
[session-counter] subscribe: ok
[events] 1 registration(s) queued before setup are armed: session.start token 3 (author.session-counter)
[events] setup: live; onFrame armed, session.start armed, race.start armed
[storage] api: author.session-counter (session_counter.dll) size sessions.txt -> invalid: swofty/addons/author.session-counter/sessions.txt is missing
[storage] api: author.session-counter (session_counter.dll) write sessions.txt (1 byte(s)) -> ok
[session-counter] session 1 started (write ok)
```

Build the DLL as in [Build and load one](/reference/addons/native-plugins#build-and-load-one).

## The map

Each entry names the header, the `SwoftyApi` member and its API version, the feature that gates it, and the thread it runs on.

- **Hooking the game**
  - **[Mixin API](./mixin)** (`swofty/mixin.hpp`): bindings, `SWOFTY_WRAP` and the other mixin macros, chains, init and per-object fields. `register_mixins` and `resolve`, API 2; `hook`; the game thread that reaches the hooked instruction.
  - **[Memory](./memory)** (`swofty/mem.hpp`): guarded reads of game memory: `read`, `in_image`, `object_like`, `c_string`. Header only, no table, any API version; any thread.
- **Your addon's own state**
  - **[Settings](./settings)** (`swofty/mixin.hpp`): read your manifest `options` and react when the player changes one. `setting` and `watch_setting`, API 3; `options`; the flow thread.
  - **[Logging](./logging)** (`swofty/mixin.hpp`): `swofty::log` writes to `swofty/loader.log`. `log`, API 1; `log`; any thread.
  - **[Storage](./storage)** (`swofty/storage.hpp`): read and write files under `swofty/addons/<id>/`. `storage`, API 5; feature `storage` (stable); the caller's thread.
- **The game's state**
  - **[Events](./events)** (`swofty/events.hpp`): lifecycle, flow and career events, and a per-frame tick. `events`, API 5; feature `events` (stable); the flow thread.
  - **[Telemetry](./telemetry)** (`swofty/telemetry.hpp`): live car, session and participant reads. `telemetry`, API 5; feature `telemetry` (experimental); the flow thread.
  - **[Database read](./db)** (`swofty/db.hpp`): read values and walk tables of the game database. `db`, API 5; feature `db` (stable); the caller's thread.
  - **[Career](./career)** (`swofty/career.hpp`): flow states, the running career, its components, the game bus and career-mode blocks. `career`, API 6 (`block` and `unblock` 7); feature `career` (stable); the flow thread.
- **What the player sees**
  - **[Overlay](./overlay)** (`swofty/overlay.hpp`): 2D rectangles, lines, text and images drawn over the game. `overlay`, API 5; feature `overlay` (experimental); the render thread.
  - **[Popup](./popup)** (`swofty/popup.hpp`): the game's message popup with your text. `popup`, API 7; feature `popup` (stable); any thread, shown on the flow thread.
  - **[Race HUD](./hud)** (`swofty/hud.hpp`): toasts in the race HUD's own notification, and HUD data leaves. The `SwoftyCoreHud` export, feature-detected (API 8 header); feature `popup` (stable); any thread, posted on the flow thread.
  - **[Strings](./strings)** (`swofty/lang.hpp`): the text a `lng_*` key shows, changed while the game runs. `lang`, API 8; feature `lang` (stable); any thread, `refresh` on the game thread.
- **The career front end**
  - **[Career contents](./career-content)** (`swofty/career_content.hpp`): extra tiles in the Addons row of Driver Career, My Team or both, with their own save slots, and the per-career check. `career_content`, API 6 or later and the `SwoftyCoreCareerContent` export; feature `career` (experimental); addon init and the flow thread.
  - **[Characters](./characters)** (`swofty/characters.hpp`): per-career character refs, voices and strings. `characters`, API 8, feature-detected; feature `career` (stable); any thread, `refresh` on the game thread.
  - **[Career wizard](./career-wizard)** (`swofty/career_wizard.hpp`): per-career control of the new-career wizard. `career_wizard`, API 8, feature-detected; feature `career` (stable); any thread.
  - **[Era picker boxes](./era-picker)** (`swofty/era.hpp`): boxes in the Addons row of the Grand Prix / Time Trial era picker. `era`, API 8 and the `SwoftyCoreEra` export; no feature; `add_box` at addon init, the others any thread.
  - **Team HQ tabs** (`swofty/hub.hpp`): hide the R&D tab and set an option from the plugin. The `SwoftyCoreHub` export, feature-detected. See [Add a Team HQ tab](/reference/addons/in-game-ui#add-a-team-hq-tab).
- **Screens, tiles and assets**
  - **[UI](./ui)**: screens and home tiles come from an addon operation, not a `SwoftyApi` table.
  - **Assets** (`swofty/assets.hpp`): runtime asset loading. `assets`, API 4. See [Asset API](/reference/addons/asset-api).

The merged engine-side contract for the version-5 tables is in [the native-libraries design](/design/native-libraries).

## Headers

The C ABI and the C++ wrappers live in `packages/sdk/include/`. The [addon template](/reference/addons/template) ships them under `native/include/`. Include the header of each library you use.

```text
packages/sdk/include/
  swofty_api.h              the C ABI: SwoftyApi, mixin records, setting values, the nested tables. MIT
  swofty/mixin.hpp          the C++ layer: mixin macros, bindings, settings, swofty::log, SWOFTY_ADDON
  swofty/events.hpp         lifecycle events and the frame tick
  swofty/overlay.hpp        the 2D overlay
  swofty/telemetry.hpp      live car and session reads
  swofty/db.hpp             game-database reads
  swofty/storage.hpp        addon storage
  swofty/career.hpp         flow states, the running career, its components, the game bus and career-mode blocks
  swofty/popup.hpp          the game's message popup with your text
  swofty/lang.hpp           runtime string overrides
  swofty/career_content.hpp extra career tiles in the Addons row of Driver Career, My Team or both,
                            with their own save slots, and the per-career check
  swofty/characters.hpp     per-career character refs, voices and strings
  swofty/career_wizard.hpp  per-career new-career wizard control
  swofty/era.hpp            boxes in the Addons row of the Grand Prix / Time Trial era picker
  swofty/hud.hpp            toasts in the race HUD's own notification
  swofty/hub.hpp            Team HQ tabs (not in the template's native/include/)
  swofty/mem.hpp            guarded memory reads: read, in_image, object_like, c_string
  swofty/assets.hpp         runtime asset loading
native/include/
  swofty/game.hpp, swofty/game/*.hpp   the Game API: the engine's functions, structs and sites
```

The [Game API](/reference/game/) documents `swofty/game.hpp`. If you're writing Rust, the `swofty-sdk` crate (`packages/sdk`) mirrors the C ABI.

## Result codes

The version-5, version-6 and version-7 tables (`events`, `overlay`, `telemetry`, `db`, `storage`, `career`, `popup`) and the later ones share these result codes. Each wrapper has a `Result` enum, and `name(r)` turns one into a string:

| `Result` | `SWOFTY_ASSET_*` | Meaning |
|---|---|---|
| `ok` | `OK` (0) | The call succeeded |
| `late` | `LATE` (1) | Too late to apply, or a cursor is exhausted |
| `unavailable` | `UNAVAILABLE` (2) | The loader is older than the table's version, the feature is off, or the lane is not implemented |
| `invalid` | `INVALID` (3) | A bad argument |

```cpp
if (!swofty::popup::available())                // non-zero only when the feature is on and the core implements it
    return;
swofty::popup::Result r = swofty::popup::show("My addon", "Saved.");
swofty::log("[my-addon] popup: %s", swofty::popup::name(r)); // "ok", "late", "unavailable" or "invalid"
```

Guard every call with `available()`. When the loader offers a version below the table's (5; 6 for `career`; 7 for `career::block` and `popup`; 8 for `lang`), or the table pointer is null, the wrapper returns `unavailable` without calling the core.

## Features

A feature is a switch in the core. A stable feature is always on. An experimental feature stays off until `"features"` in `swofty/assets.json` lists it:

```text
[overlay] the overlay feature is experimental and off; the HUD present hook is not armed
[events] feature off; subscribe/on_frame will report unavailable
```

- **Stable:** `events`, `storage`, `db`, `lang`, `career`, `popup`.
- **Experimental:** `overlay`, `telemetry`. An addon turns `overlay` on with `"experimental_features": ["overlay"]` in its [manifest](/reference/addons/manifest).
- `hook`, `options` and `log` name the mixin, settings and logging members of `SwoftyApi`. No switch turns them off.

## Threads

- **Flow thread:** `Vm_Update_FlowStateMachine`, each frame. Lifecycle, flow and career events, the `on_frame` tick and telemetry reads fire here. Call the career library's game calls (`entity`, `component`, `post`) here.
- **Render thread:** the swapchain present path. `on_paint` callbacks fire here.
- **Caller thread:** `db` and `storage` calls run on the thread that calls them.

You can subscribe, add paint callbacks and call `db` and `storage` from any thread. Your callbacks must return at once and never block the thread they run on.

## API versions

Read `SwoftyApi::version` to see what the running loader offers. `SWOFTY_API_VERSION` in `swofty_api.h` is 8.

- **1**, `version.dll`: `version`, `log`, `module_base`, `scan`.
- **4**, `swofty_core.dll`: `resolve`, `register_mixins`, `fields`, `setting`, `watch_setting`, `assets`.
- **5**, `swofty_core.dll`: `events`, `overlay`, `telemetry`, `db`, `storage`.
- **6**, `swofty_core.dll` 0.2.11: `career`.
- **7**, `swofty_core.dll` 0.2.12: `popup`, `career->block`, `career->unblock`.
- **8**, `swofty_core.dll`: `lang`, and the slots for `career_content`, `characters`, `career_wizard` and `era`.

The helpers you include set the version you need:

```text
swofty/mixin.hpp                                         2 for the mixin helpers, 3 for the settings helpers
swofty/assets.hpp                                        4
swofty/events.hpp, overlay.hpp, telemetry.hpp, db.hpp, storage.hpp   5
swofty/career.hpp                                        6 (block and unblock: 7)
swofty/popup.hpp                                         7
swofty/lang.hpp                                          8
swofty/mem.hpp                                           none: it needs no table
```
