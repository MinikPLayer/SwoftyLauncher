---
title: The C ABI
description: The SwoftyAddonRegister export, the SwoftyRegistry, SwoftyHooks and SwoftyServices tables under the standard library's C++ wrappers, and the career operations table.
---

# The C ABI

The typed C++ API sits on one export, `SwoftyAddonRegister`, and three C tables appended to `SwoftyApi` in API version 9. The career operations are a fourth table, from a core export only. A C caller, or an addon in another language, uses these directly.

For example, a `SWOFTY_REGISTER` block that adds an engine sound becomes one `registry->add` call from `SwoftyAddonRegister`, at the early stage.

## The registration export

<Split>

`SWOFTY_REGISTER` blocks collect into a list in the DLL. When the DLL has at least one, `SWOFTY_ADDON()` also exports `SwoftyAddonRegister`.

- The loader loads every DLL that exports `SwoftyAddonRegister` at the early stage, and calls only `SwoftyAddonRegister` there: before the game loads its database, on the loader thread, addon by addon in load order.
- `SwoftyAddonInit`, which runs `SWOFTY_ON_LOAD`, runs at the stage the manifest asks for: the window stage, unless the DLL's entry in `swofty.json` sets `"early": true`.
- The addon's `swofty.json` doesn't change.
- A DLL without `SWOFTY_REGISTER` exports only what an addon made for 1.5 exports.

<template #example>
<Example root="mod.esp" code-file="native/src/esp.cpp" files="swofty.json, native/build.sh, sounds/v10_screamer.bdl">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/sounds.hpp>
#include <swofty/systems/cars.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    auto v10 = sounds::engine_sounds.add("v10_screamer", {
        .bundle = file("sounds/v10_screamer.bdl"),
        .settings_from = sounds::engine_sounds["game/ferrari_2025"],
    });
    cars::vehicles.layer(cars::vehicles["swofty.classic-seasons-2017/ferrari"], [=](cars::Vehicle &v) {
        v.engine_sound = v10;
    });
}

SWOFTY_ADDON()

// what SWOFTY_ADDON() expands to in this DLL, trimmed
extern "C" __declspec(dllexport) const uint32_t SwoftyAddonApi = SWOFTY_API_VERSION;   // 9
extern "C" __declspec(dllexport) int SwoftyAddonRegister(const SwoftyApi *a)           // early stage
{
    return ::swofty::detail::run_register(a);   // every SWOFTY_REGISTER block, in source order
}
extern "C" __declspec(dllexport) int SwoftyAddonInit(const SwoftyApi *a)               // the manifest's stage
{
    return ::swofty::detail::init(a, nullptr);
}

// what the two calls above send to the registry table
//   a->registry->add("engine_sound", "v10_screamer", {"bundle":"sounds/v10_screamer.bdl",...}, id, cap)
//                                                    -> id "mod.esp/v10_screamer"
//   a->registry->layer_fn("vehicle", "swofty.classic-seasons-2017/ferrari", edit, user, SWOFTY_SCOPE_GLOBAL)
```

</template>
</Example>
</template>
</Split>

```text
addon esp.dll: calling SwoftyAddonRegister (API v9) at the early stage
[registry] register: mod.esp (esp.dll) 1 engine_sound, 1 layer on vehicle (early stage, 0.4 ms)
addon esp.dll: calling SwoftyAddonInit (API v9) at the window stage (manifest: early false)
```

## The tables

Like every other table, the three tables are appended to `SwoftyApi` and also exported for feature detection. An addon built against the 1.6 headers still loads on a 1.5 core, and sees the three libraries as unavailable.

```c
/* swofty_api.h */
#define SWOFTY_API_VERSION 9
#define SWOFTY_REGISTRY_VERSION 1
#define SWOFTY_HOOKS_VERSION 1
#define SWOFTY_SERVICES_VERSION 1

enum { SWOFTY_SCOPE_GLOBAL = 0, SWOFTY_SCOPE_PROFILE = 1, SWOFTY_SCOPE_CAREER = 2 };
enum { SWOFTY_PHASE_BEFORE = 0, SWOFTY_PHASE_REPLACE = 1, SWOFTY_PHASE_AFTER = 2 };
enum { SWOFTY_TOO_SMALL = 4 };   /* joins ok/late/unavailable/invalid: *len holds the size needed */

typedef struct SwoftyText { const char *data; size_t size; } SwoftyText;   /* UTF-8, borrowed for the call */

/* provider: write the record's JSON into out; return SWOFTY_ASSET_OK, or SWOFTY_ASSET_INVALID for "not mine" */
typedef int (*SwoftyProvideFn)(void *user, const char *registry, const char *id, char *out, size_t cap, size_t *len);
typedef void (*SwoftyListFn)(void *user, const char *registry, void (*add)(void *ctx, const char *id), void *ctx);
typedef void (*SwoftyRecordFn)(void *user, const char *registry, const char *id);   /* id "" ends a batch */
typedef int (*SwoftyConvertFn)(void *user, SwoftyText in, char *out, size_t cap, size_t *len);
typedef void (*SwoftyCheckFn)(void *user, SwoftyText value, void (*error)(void *ctx, const char *path, const char *msg), void *ctx);
/* edit: in is the effective value so far; write the edited value into out */
typedef int (*SwoftyEditFn)(void *user, SwoftyText in, char *out, size_t cap, size_t *len);
/* selector: non-zero when the record matches; base is the record's base value, before layers */
typedef int (*SwoftyMatchFn)(void *user, SwoftyText base);

typedef struct SwoftyRegistry {
    uint32_t version;
    int (*available)(void);
    int (*declare)(const char *registry, SwoftyText schema_json);
    int (*check)(const char *registry, SwoftyCheckFn fn, void *user);
    int (*adapter)(const char *registry, uint32_t from_major, uint32_t to_major,
                   SwoftyConvertFn up, SwoftyConvertFn down, void *user);
    int (*provide)(const char *registry, const char *id, SwoftyText value_json);
    int (*provide_fn)(const char *registry, SwoftyProvideFn fn, SwoftyListFn list, void *user);
    int (*invalidate)(const char *registry, const char *id);        /* id NULL: every record */
    int (*layer)(const char *registry, const char *id, SwoftyText patch_json, uint32_t scope);
    int (*clear_layer)(const char *registry, const char *id, uint32_t scope);
    int (*get)(const char *registry_at_major, const char *id, char *out, size_t cap, size_t *len);
    int (*status)(const char *registry, const char *id);
    int (*origin)(const char *registry, const char *id, const char *pointer, char *addon, size_t cap);
    int (*first)(const char *registry, const char *prefix, char *id, size_t cap);
    int (*next)(const char *registry, const char *prefix, const char *after, char *id, size_t cap);
    int (*watch)(const char *registry, SwoftyRecordFn fn, void *user, uint32_t *token);
    int (*unwatch)(uint32_t token);
    int (*last_error)(char *out, size_t cap);                       /* this thread's last refusal */

    /* the typed API: what Registry<T> calls */
    int (*add)(const char *registry, const char *key, SwoftyText value_json, char *id, size_t cap);
    int (*variant)(const char *registry, const char *key, const char *base_id,
                   SwoftyEditFn edit, void *user, char *id, size_t cap);
    int (*layer_fn)(const char *registry, const char *id, SwoftyEditFn edit, void *user, uint32_t scope);
    int (*layer_select)(const char *registry, SwoftyMatchFn match, const char *prefix,
                        const char *const *except, size_t except_count,
                        SwoftyEditFn edit, void *user, uint32_t scope);
    int (*option)(const char *id, uint32_t type, SwoftySettingValue *out);
    int (*migrate)(const char *registry, uint32_t from_major, uint32_t to_major, SwoftyConvertFn fn, void *user);
} SwoftyRegistry;

typedef struct SwoftyHookCall {
    uint32_t size;
    uint32_t phase;
    const char *point;
    void *payload;           /* the point's payload struct: uint32_t size, uint32_t payload_version, ... */
    int replaced;            /* set by the core after the replace phase */
} SwoftyHookCall;
typedef int (*SwoftyHookFn)(void *user, SwoftyHookCall *call);  /* replace phase: non-zero = handled */

typedef struct SwoftyHooks {
    uint32_t version;
    int (*available)(void);
    int (*live)(const char *point, uint32_t *payload_version);
    int (*attach)(const char *point, uint32_t phase, int32_t priority, SwoftyHookFn fn, void *user, uint32_t *token);
    int (*detach)(uint32_t token);
    int (*define)(const char *point, uint32_t payload_version);
    int (*fire)(const char *point, void *payload, int *replaced);
} SwoftyHooks;

typedef void (*SwoftyServiceFn)(void *user, const void *table, size_t size, uint32_t version);

typedef struct SwoftyServices {
    uint32_t version;
    int (*available)(void);
    int (*publish)(const char *name, uint32_t version, const void *table, size_t size);
    int (*when)(const char *name, uint32_t min_version, size_t min_size, SwoftyServiceFn fn, void *user);
} SwoftyServices;

/* SwoftyApi, version 9: appended after era */
    const SwoftyRegistry *registry;
    const SwoftyHooks *hooks;
    const SwoftyServices *services;

/* core exports, for an addon that must also load on a 1.5 core */
SWOFTY_EXPORT const SwoftyRegistry *SwoftyCoreRegistry(void);
SWOFTY_EXPORT const SwoftyHooks *SwoftyCoreHooks(void);
SWOFTY_EXPORT const SwoftyServices *SwoftyCoreServices(void);

/* addon export, emitted by SWOFTY_ADDON() when the DLL has a SWOFTY_REGISTER */
SWOFTY_EXPORT int SwoftyAddonRegister(const SwoftyApi *api);
```

## The career operations table

The core exports `SwoftyCoreCareerOps`. The table isn't a `SwoftyApi` field: `swofty/systems/career.hpp` finds it with `GetProcAddress`, and on a core without it every operation returns `unavailable`.

```c
typedef struct SwoftyCareerOps {
    uint32_t version;                                                          /* 6 */
    int (*available)(void);
    int (*driver_stat)(const char *driver_ref, uint32_t *out);                 /* "game/<label>", "person/<label>" */
    int (*set_slots)(const char *data_group, const uint32_t *team_stats, size_t count);
    int (*apply_lineup)(SwoftyText request_json, char *report, size_t cap, size_t *len);
    int (*set_driver_ratings)(uint32_t driver_stat, const uint16_t *stat_ids, const float *values, size_t count);
    int (*swap_drivers)(uint32_t a_stat, uint32_t b_stat);
    int (*report_season)(int32_t year, int32_t season_index);                  /* version 2 */
    int (*apply_teams)(SwoftyText request_json, char *report, size_t cap, size_t *len);   /* version 3 */
    int (*set_seat)(SwoftyText request_json, char *report, size_t cap, size_t *len);      /* version 4 */
    int (*set_car_number)(uint32_t driver_stat, int32_t number, uint32_t flags);
    int (*set_track_fuel)(SwoftyText request_json, char *report, size_t cap, size_t *len);
    int (*set_logo)(const char *team_id, const char *logo_id);                 /* version 5 */
    int (*set_look)(const char *driver_ref, const char *look_ref);             /* version 6 */
} SwoftyCareerOps;

enum { SWOFTY_REFUSED = 5 };

SWOFTY_EXPORT const SwoftyCareerOps *SwoftyCoreCareerOps(void);
```

- **Ids:** a stat id is the upper 16 bits of the game's key. `driver_stat` reads the drivers table row whose label is the part after the `/`.
- **Threads:** every call runs on the flow thread. From another thread it returns `SWOFTY_ASSET_LATE`.
- **Bindings:** a call that needs a binding the running build doesn't resolve returns `SWOFTY_ASSET_UNAVAILABLE` and logs the binding's id.
- **Logs:** each call writes one `[career-ops]` line with what changed.
- **Versions:** version 2 appends `report_season`. A caller checks `version >= 2` before it calls it.

`report_season` says the season at the Championship's `season_index` is `year`. Only the addon that owns the active career's data group calls it: the addon whose career content or `career.mode` record has that data group. The core then fires `career.season.start` once per career and year, activates the season's records and freezes its snapshot, in that order, before it returns.

| Result | When |
| --- | --- |
| `SWOFTY_ASSET_OK` | the season started, or that year already started in this career |
| `SWOFTY_REFUSED` | the caller doesn't own the data group, or no addon owns it |
| `SWOFTY_ASSET_INVALID` | a year outside 1950-2100, or a negative season index |
| `SWOFTY_ASSET_LATE` | no career is loaded, or the call came off the flow thread |

```text
[registry] career::report_season(2018, 2) from swofty.progressive: ok, career 1f2a season index 2 is 2018; career.season.start
[registry] career::report_season(2018, 2) from tails.whatif: refused (only swofty.progressive, which owns data group f1_career_progressive, reports its seasons)
```

`apply_lineup` takes one of two requests. `{"season": ...}` is a `career.season` record, which the core plans against the slot table. `{"lineup": ...}` is a line-up in stat ids, which the core writes as given:

```json
{ "lineup": {
    "year": 2017, "why": "season start", "visuals": true, "player_team": 476,
    "moves": [ { "driver": 7, "team": 476, "seat": 0, "car": 212, "holder": 5, "mate": 9 } ],
    "repositions": [ { "driver": 9, "seat": 1 } ],
    "releases": [ 5 ],
    "numbers": [ { "driver": 7, "number": 14 } ],
    "cars": [ { "driver": 7, "car": 212 } ],
    "looks": [ { "driver": 7, "look": 455 } ] } }
```

- **moves:** the driver takes `seat` (0 or 1) of `team`, with that seat's row as the template, else his new teammate's, else any row of the team. `holder` and `mate` name the seat's row and the teammate's row by driver; without them, or at -1, they're the team's rows on that position and the other. `car` is used when the template row has none (-1: none given).
- **cars and looks:** written only when `visuals` is true.
- **numbers:** never written on the player's row.
- **player_team:** the team the line-up seats the player at, or -1. A moved player signs with that team only.

The report the core writes:

```json
{ "result": "ok", "changed": 2, "unchanged": 19, "refused": 0, "refusals": [], "written": 2, "released": 1,
  "numbers": 0, "finalise": "posted", "line_up_changes": 2, "contracts_signed": 1 }
```

`result` is `ok`, `failed` (the batch was written back) or `refused`. For a season, `changed` counts seats set and `unchanged` seats already right. For a line-up, they count rows written and rows already as planned.

## What the C++ wrapper calls

```text
Registry<T>{name, options}       declare        the schema the wrapper generates from SWOFTY_RECORD and RegistryOptions
                                                (x-key from Keyed, x-applies, x-snapshot, x-snapshot-budget, x-version)
add(key, value)                  add            id comes back as "<addon id>/<key>"
variant(key, base, edit)         variant
provide(fn, list)                provide_fn
invalidate(id)                   invalidate
layer(target, edit, scope)       layer_fn
layer(select, edit, scope)       layer_select   where(): match; under(): prefix; .except(): except
clear_layer(target, scope)       clear_layer
check(fn)                        check
get(id)                          get            "<name>@<T::version.major>", e.g. "career.season@1"
status(id)                       status
origin(id, field)                origin
all(prefix)                      first, next
watch(fn)                        watch          the Token's destructor and reset() call unwatch
swofty::option<V>(id)            option
swofty::adapter(from, to, ...)   adapter
swofty::migrate(from, to, fn)    migrate
data files, C callers            provide (a JSON value), layer (a JSON merge patch)
career::set_slots                career ops set_slots
career::apply_lineup             career ops apply_lineup    {"season": the record}; the report fills ApplyReport
career::set_driver_ratings       career ops driver_stat, set_driver_ratings   ids 0, 1, 3, 4
career::swap_drivers             career ops driver_stat, swap_drivers
career::report_season            career ops report_season   version 2; SWOFTY_REFUSED is Result::refused
```

An edit crosses the ABI as JSON:

- The core calls the `SwoftyEditFn` with the effective value so far.
- The wrapper reads it into `T` through `SWOFTY_RECORD`, runs the edit, and writes the struct's fields back over the value it got. Fields `T` doesn't list come through unchanged.
- The core diffs `in` and `out`. The changed fields become the edit's origins and its conflicts. It then validates the result. A failing edit is dropped, with a log line naming the addon.
- `option` called during an edit records that the edit depends on that option, and the core runs the edit again when the player changes it. Outside an edit, it's a plain read of the calling addon's option, like `setting`.

## Rules

- **Copies:** the core copies every string and JSON text it's given. Values come back in the caller's buffer. When the buffer is too small, the call returns `SWOFTY_TOO_SMALL` with the size needed, and the C++ wrapper retries.
- **Payloads:** an event or hook point's payload struct holds C types only: fixed-width integers, `float`, `bool`, enums with a fixed underlying type, plain pointers, `const char *` record ids and text (NUL-terminated, valid for the handler call), and C function pointers with a `void *user`. The C++ forms are member functions: `e.vehicle()` returns a `Ref<Vehicle>` from `vehicle_id`, `e.section()` a `std::string_view`, `p.add(part)` calls `add_fn`. A `cars::live::Car` is a trivially copyable handle, `{ uint32_t index; void *handle; }`. An addon built with any compiler reads the same layout.
- **Results:** the shared result codes plus `SWOFTY_TOO_SMALL`. A refusal leaves its reason in `last_error` for the calling thread.
- **Timing:**
  - the registry calls (`declare`, `add`, `variant`, `provide`, `provide_fn`, `layer`, `layer_fn`, `layer_select`) are accepted from `SwoftyAddonRegister` on, and at any time after it while the game runs. A change made after the game read that data takes effect at the next moment it reads it, and the core logs when ("applies from the next session load", "applies from the next game start");
  - `define`, `attach`, `publish` and `when` are accepted from `SwoftyAddonInit` on;
  - a call before the core's setup queues and is applied in load order, the same as event subscriptions made with `subscribe`.
- **Threads:**
  - `SwoftyAddonRegister` runs on the loader thread;
  - registry reads, layers, `publish` and `when` work from any thread;
  - providers, edits and selector matches run on the thread that resolves the record. They must return quickly and not read the registry they belong to (a re-entrant read returns `late`);
  - watchers and `when` callbacks run on the flow thread;
  - game hook points run on the thread the game calls the function on. On a point the catalogue marks `hot`, the core calls a `SwoftyHookFn` when it builds the point's table at a content or session moment, not on each game call (see [Hooks](/std/hooks#hook-points-the-game-calls-every-frame)).
- **Faults:** the core calls every `SwoftyHookFn` inside a guard. A fault in the after phase is logged with the addon and the point, and that handler is detached for the session. A fault in the before or replace phase crashes the game, and the crash report names the addon, the point and the phase.
- **Growth:** fields are only appended. A table's `version` names the last layout the core fills (see [Versioning](/std/versioning#abi-and-table-versions)). On a 1.5 core, `SwoftyAddonRegister` is never called, and the three libraries read as unavailable.
