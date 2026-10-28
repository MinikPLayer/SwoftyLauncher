# Native standard libraries

If you're contributing to the native standard library, this is the contract for the five additions
that addon plugins reach through `SwoftyApi`. The SDK surface, feature gates and stubs ship now.
Each library gets its own engine implementation; check its status line below to see where it's
still a stub.

These additions raise `SWOFTY_API_VERSION` to 5. You reach each library's nested table on `SwoftyApi`
(`events`, `overlay`, `telemetry`, `db`, `storage`) through a C++ wrapper header
(`swofty/events.hpp`, `swofty/overlay.hpp`, `swofty/telemetry.hpp`, `swofty/db.hpp`,
`swofty/storage.hpp`). Every call returns one of the shared result codes: `ok`, `late`,
`unavailable` (older core, feature off, or not yet implemented), `invalid` (bad argument).
If the loader offers a version below 5 or the table pointer is null, the wrapper returns
`unavailable`. To check whether the core implements a table and has its feature on, use that
table's `available()`: it returns non-zero only when both are true.

You'll find the engine addresses below in `re/db/functions.tsv`. They're VAs for the catalogued
`F1_25.exe` build, so confirm a hook by decompiling before you rely on it.

## Feature gates

The feature table lives in `native/src/core/stage.cpp` (`FEATURES`); `native/build.sh` writes it to
`swofty/addons/core/features.json`. Stable features are always on. An experimental feature is on
when `"features"` in `swofty/assets.json` lists it. The build writes it there while an addon in the
profile lists it under `experimental_features` in its `swofty.json`.

| Feature | Default | Library |
|---|---|---|
| `events` | stable | Events + frame scheduler |
| `storage` | stable | Storage |
| `db` | stable | Database read (shares the gate with database write) |
| `overlay` | experimental | Overlay / draw |
| `telemetry` | experimental | Telemetry |
| `shaders` | experimental | Shader looks ([Shader packs](/reference/addons/shader-packs)) |
| `postfx` | experimental | Post-process passes ([Post-process passes](/reference/addons/postfx)) |

## Threads

| Thread | What runs on it |
|---|---|
| Flow thread | The thread that calls `Vm_Update_FlowStateMachine` each frame. Lifecycle events, the `on_frame` tick, and all telemetry reads fire here. |
| Render thread | The swapchain present path. `on_paint` callbacks fire here. |
| Caller thread | `db` reads and `storage` calls run on the thread that calls them. Returned `db` string pointers are valid until the next `db` call on that thread. |

You can use subscriptions, paint callbacks and `storage` and `db` calls from any thread.
Your callbacks must return quickly: they must not block the thread they run on.

## 1. Events + frame scheduler

`swofty/events.hpp`, table `SwoftyEvents`, feature `events` (stable).

| Call | Result |
|---|---|
| `available()` | non-zero when the feature is on and implemented |
| `subscribe(event, fn, user, token)` | `ok` and fills `token`; `invalid` for an unknown event name or null `fn` |
| `on_frame(fn, user, token)` | `ok` and fills `token` |
| `unsubscribe(token)` | `ok`, or `invalid` for an unknown token |

Event names are strings (`SWOFTY_EVENT_*`): `session.start`, `session.end`, `race.start`, `lap`,
`flag`, `state`, `screen`, `pause`, `resume`, `results`. Your subscriber receives a `SwoftyEvent`
(`name`, `frame`, `time` seconds since attach, `a`/`b` event integers, `data` pointer). `on_frame`
receives the seconds since the previous frame. Callbacks run on the flow thread until you call
`unsubscribe`. If your addon's init returns failure, the core drops its subscriptions.

You can call `subscribe`, `on_frame` and `unsubscribe` at any time. Callbacks start firing once
a session or the flow machine is running.

Hooks: the per-frame tick is `Vm_Update_FlowStateMachine` 0x144423ce0. Lifecycle events come from
`Game_GameModule_OnSessionEnter` 0x14080c300 (`session.start`), `Sess_Weekend_Start` 0x140af0460,
`Game_ChampionshipFormationLapComponent_OnRaceStarted` 0x140b808a0 (`race.start`),
`Game_SessionStateFinaliseResultsSync_OnResultsFinalised` 0x1409d2b20 (`results`), and the
`*_OnStateUpdate` functions (`Game_StateCareerDataSync_OnStateUpdate` 0x1436b0700,
`Game_SessionStateFinaliseResultsSync_OnStateUpdate` 0x142d5c9b0) for `state`, `screen` and
`session.end`. The `lap`, `flag`, `pause` and `resume` sources are found in the events lane.

Status: stub. `native/src/core/events.cpp` returns `unavailable` from every call.

## 2. Overlay / draw

`swofty/overlay.hpp`, table `SwoftyOverlay`, feature `overlay` (experimental).

| Call | Result |
|---|---|
| `available()` | non-zero when the feature is on and implemented |
| `on_paint(fn, user, token)` | `ok` and fills `token` |
| `remove(token)` | `ok`, or `invalid` for an unknown token |
| `size(paint, w, h)` | drawable size in pixels |
| `rect` / `rect_outline` / `line` / `text` | draw into the current frame |
| `measure(paint, size, utf8, w, h)` | text bounds |
| `load_image(id, file)` | `ok`, or `invalid` for a bad id, a missing file or a file that is not PNG or uncompressed DDS |
| `image(paint, id, ...)` | `ok`, or `invalid` for an unknown id |
| v2: shapes, styled text, `image_uv` / `image_nine` / `image_size`, clip / transform / opacity stacks, `safe_area`, `ui_scale` | present when the core exports `SwoftyCoreOverlayVersion` returning 2 or more |

Drawing uses immediate mode:

1. Register a paint callback with `on_paint`. The core calls it once per presented frame with a
   `SwoftyPaint` handle and issues the draw calls you make through that handle.
2. Query `size` each frame. Coordinates are framebuffer pixels, with the origin at the top-left.
3. Draw with colours in `0xRRGGBBAA` format. Text uses the built-in font at a pixel size.

For textures, call `load_image` outside paint to load once from the addon folder. Then draw by id;
the texture is multiplied by a tint (`0xffffffff` for none).

You can call `on_paint`, `remove` and `load_image` at any time. Draw calls, though, are valid only
inside a paint callback, using that callback's `paint` handle.

The core's D3D12 and DXGI hooks live in one layer, `native/src/core/d3d12_hooks.{hpp,cpp}`. Features
subscribe to it instead of hooking the same entry points:

| Call | Does |
|---|---|
| `arm_swapchain()` | Hooks the DXGI factory's `CreateSwapChain`, `CreateSwapChainForHwnd`, `CreateSwapChainForCoreWindow` and `CreateSwapChainForComposition`, and the swapchain's `Present`, `ResizeBuffers`, `ResizeBuffers1` and `SetColorSpace1`. Once per process |
| `arm_device()` | Hooks `ID3D12Object::SetName`, `ID3D12GraphicsCommandList::ResourceBarrier` and `ID3D12CommandQueue::ExecuteCommandLists`. Once per process |
| `on_present(fn)` | `fn(swapchain, known, caller)` before the original `Present`, one present at a time. `known` is the swapchain's creation record (queue, caller chain, frame generation, Streamline, colour space), or null for a swapchain created before `arm_swapchain` |
| `on_presented(fn)` | `fn(swapchain, hr)` after the original `Present` |
| `on_resize(fn)` | `fn(swapchain)` before `ResizeBuffers` and `ResizeBuffers1` |
| `on_exec(fn)` / `on_barrier(fn)` | Observers in subscription order. One that returns `true` has made the call itself through `exec_original` / `barrier_original`, and the rest are skipped |
| `on_setname(fn)` | `fn(object, name)` before the original `SetName` |

The overlay (`hud.cpp`) and shader packs (`grade.cpp`) subscribe to it.

The overlay records each swapchain's creation queue and the modules on the creation call stack, and draws only on
swapchains it saw created, on their creation queue, when no frame generation module created or
presents them. A swapchain whose creation passed through Streamline's frame generation hook
(`sl.dlss_g.dll`) is drawn on only while the game's hardware settings have `frame_gen mode="0"`. Submissions use a ring of three command lists behind one fence and never wait in
`Present`. A device removal stops the overlay for the rest of the session.

Paint callbacks tessellate into one vertex list (`hud_drawlist.hpp`) split into batches that share a
texture and a clip rect. Text is Titillium Web SemiBold from a signed distance field atlas generated
by `native/tools/hud_font_sdf.py`. Images decode with stb_image (PNG) or the core's DDS reader and
upload once to a shader-visible heap of 256 descriptors. An addon id in `overlay_off` in
`swofty/assets.json` gets `unavailable` from `on_paint` and its callbacks are skipped.

Status: every v1 and v2 call draws.

## 3. Telemetry

`swofty/telemetry.hpp`, table `SwoftyTelemetry`, feature `telemetry` (experimental).

| Call | Result |
|---|---|
| `available()` | non-zero when the feature is on, implemented, and a session is live |
| `session(out)` | `ok` and fills `SwoftySessionState`; `unavailable` outside a session |
| `car(index, out)` | `ok` and fills `SwoftyCarState`; `invalid` for an out-of-range index |
| `participant(index, out)` | `ok` and fills `SwoftyParticipant` |
| `shared_memory(name)` | turns the feed on and names the mapping |

Structs are versioned (`version` = `SWOFTY_TELEMETRY_VERSION`, currently 1). `SwoftyCarState` holds
speed (m/s), rpm, gear, throttle/brake/clutch/steer, per-wheel tyre temp/wear and brake temp, fuel,
position, lap, sector, lap and sector times, lap distance, world position and velocity.
`SwoftySessionState` holds session type, track, flag, weather, lap count, participant count, player
index, session time and temperatures. `SwoftyParticipant` holds name, team, position, lap and best
lap. Each struct ends in a `reserved` block, leaving room to add fields without changing its size.

Shared feed: `shared_memory` opens a named file mapping (`Local\SwoftyTelemetry`) laid out as
`SwoftyTelemetryFeed`:

| Field | Type | Notes |
|---|---|---|
| `version` | `uint32` | `SWOFTY_TELEMETRY_VERSION` |
| `size` | `uint32` | `sizeof(SwoftyTelemetryFeed)` |
| `seq` | `uint64` | seqlock; odd while a frame is being written |
| `frame` | `uint64` | frame counter |
| `session` | `SwoftySessionState` | current session |
| `cars` | `SwoftyCarState[24]` | per participant |
| `participants` | `SwoftyParticipant[24]` | per participant |

To read the feed:

1. Check that `seq` is even. Retry if it's odd.
2. Copy the data.
3. Check that `seq` did not change. Retry if it did.

Reads run on the flow thread and give you the most recent frame's values. You can call
`shared_memory` at any time; once enabled, the feed stays on for the lifetime of the process.

Hooks: session and participant data come from the session witnesses `Sess_Veh_UpdateWheelMap`
0x142e90560, `Sess_ParticipantVec_PushBack` 0x140af2ab0, `Sess_Select_Entrants` 0x1424a2350,
`Sess_Startup_VehicleAiLoop` 0x142505f30 and `Game_GameModule_OnSessionEnter` 0x14080c300. The
per-car physics/telemetry struct isn't catalogued yet, so you'll need to find it to implement this
library.

Status: stub. `native/src/core/telemetry.cpp` returns `unavailable` from every call.

## 4. Database read

`swofty/db.hpp`, table `SwoftyDb`, feature `db` (stable, shared with the database write operations).

| Call | Result |
|---|---|
| `available()` | non-zero when the database has loaded |
| `get(key, out)` / `get(hash, out)` | whole typed value by dotted key or pre-hashed key |
| `int_at` / `float_at` / `string_at` / `bool_at` | one field of a known type |
| `open(table, cursor)` | positions a cursor at the first row |
| `row(table, label, cursor)` | positions a cursor at a labelled row |
| `step(cursor)` | advances; `late` when the table is exhausted |
| `cell(cursor, field, out)` | reads one field of the cursor's current row |
| `row_name(cursor)` | the current row's label, or null |

Reads use the same keys as writes: the dotted path hashed with fnv1a64. See the
[game database reference](/reference/gamedb). A `SwoftyDbValue` carries a `tag` (`SWOFTY_DB_*`) and
the integer, float or string. You own the `SwoftyDbCursor` you pass in. String pointers remain
valid until the next `db` call on the same thread.

You can read once the database has loaded. Before then, reads return `unavailable`.

Hooks: `Gdb_Lookup_Key` 0x1445a22f0 (fnv1a64 key lookup), `Gdb_Init_TableCursor` 0x1445a25c0,
`Gdb_Get_RowByIndex` 0x1445a28f0, `Gdb_Find_RowByLabel` 0x1445a2bd0, `Gdb_Get_FieldInt`
0x1445a2cf0 and `Gdb_Get_RowName` 0x1445a3900, over the database `Gdb_Load_GameDatabase`
0x1445a0350 resolves. `native/src/core/gamedb.cpp` already resolves this database for writes; the
read path reuses the pointer.

Status: stub. `native/src/core/dbread.cpp` returns `unavailable` from every call.

## 5. Storage

`swofty/storage.hpp`, table `SwoftyStorage`, feature `storage` (stable).

| Call | Result |
|---|---|
| `available()` | non-zero when the feature is on and implemented |
| `read(path, buf, cap, len)` | `ok` and fills `buf`; `invalid` when the file exceeds `cap` |
| `write(path, data, len)` | atomic replace |
| `append(path, data, len)` | append, creating the file when absent |
| `remove(path)` | delete |
| `size(path, out)` | file size in bytes |
| `list(dir, buf, cap, len)` | newline-separated names into `buf` |

Your addon reads and writes under `swofty/addons/<id>/`, using paths relative to that folder.
The core rejects absolute paths, drive letters and any path that climbs above the folder.
To keep each addon in its own folder, it identifies the calling DLL the same way the assets API
does (`settings_addon` in `native/src/core/settings.cpp`). Writes go to a temp file, then rename it.

You can make storage calls at any time.

Status: stub. `native/src/core/storage.cpp` returns `unavailable` from every call.

## Files

| Path | Role |
|---|---|
| `packages/sdk/include/swofty_api.h` | C ABI: version 5, the five nested tables, shared structs |
| `packages/sdk/include/swofty/{events,overlay,telemetry,db,storage}.hpp` | C++ wrappers |
| `packages/sdk/src/lib.rs` | Rust mirror and the layout test |
| `templates/addon/native/include/` | the copy addons compile against |
| `native/src/core/{events,hud,telemetry,dbread,storage}.cpp` | per-library stubs wired into `manager.cpp` |
| `native/src/core/stage.cpp` | the feature table |

The wrapper headers and the mixin, settings and logging helpers are the native half of the
[standard library](/reference/std/).
