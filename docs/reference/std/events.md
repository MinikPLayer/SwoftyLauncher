---
title: Events
description: Subscribe to F1 25 lifecycle events and run a callback every rendered frame with swofty/events.hpp.
---

# Events

The events library calls your functions when the game reaches a lifecycle point (a session starts, a career loads, Team HQ goes idle) and once per rendered frame.

```cpp [native/src/my_plugin.cpp]
#include <swofty/career.hpp>
#include <swofty/events.hpp>
#include <swofty/mixin.hpp>

#include <string_view>

namespace ev = swofty::events;

static uint32_t g_frame_token; // filled by on_frame, used by unsubscribe
static uint64_t g_frames;

static void on_session_start(const SwoftyEvent *e, void *)
{
    // e->frame counts flow-thread frames, e->time is seconds on the core's event clock
    swofty::log("[my-addon] session start on frame %llu, %.1f s", (unsigned long long)e->frame, e->time);
}

static void on_career(const SwoftyEvent *e, void *)
{
    auto *s = static_cast<const SwoftyCareerState *>(e->data); // valid only during this call
    if (e->name == std::string_view(SWOFTY_EVENT_CAREER_LOADED))
        swofty::log("[my-addon] career loaded, mode %s, career %s", swofty::career::name(swofty::career::mode(*s)),
                    s->key[0] ? s->key : "(not yet known)");
    else // career.hub: a is 1 entered / 0 left, b is the mode
        swofty::log("[my-addon] Team HQ %s (%s)", e->a ? "entered" : "left",
                    swofty::career::name(static_cast<swofty::career::Mode>(e->b)));
}

static void on_frame(double dt, void *)
{
    if (++g_frames == 600) { // about ten seconds at 60 fps
        swofty::log("[my-addon] 600 frames, last dt %.4f s; stopping the tick", dt);
        ev::unsubscribe(g_frame_token); // allowed from inside a callback
    }
}

SWOFTY_ON_LOAD
{
    if (!ev::available()) // false on a core before API 5, or when the core found no frame tick on this build
        return;
    ev::subscribe(SWOFTY_EVENT_SESSION_START, on_session_start);
    ev::subscribe(SWOFTY_EVENT_CAREER_LOADED, on_career);
    ev::subscribe(SWOFTY_EVENT_CAREER_HUB, on_career);
    ev::Result r = ev::on_frame(on_frame, nullptr, &g_frame_token);
    swofty::log("[my-addon] frame tick: %s (token %u)", ev::name(r), g_frame_token);
}

SWOFTY_ADDON()
```

```json [swofty.json]
{
  "schema_version": 1,
  "id": "author.my-addon",
  "name": "My Addon",
  "version": "1.0.0",
  "author": "Your name",
  "kind": ["native"],
  "compat": { "game": "f1_25" },
  "requires": [{ "id": "swofty.core", "version": ">=0.2.11" }],
  "operations": { "native": [{ "dll": "native/my_plugin.dll" }] }
}
```

`swofty/loader.log`:

```text
[events] subscribe session.start by author.my-addon -> ok token 7, queued until the events setup
[events] subscribe career.loaded by author.my-addon -> ok token 8, queued until the events setup
[events] subscribe career.hub by author.my-addon -> ok token 9, queued until the events setup
[events] on_frame by author.my-addon -> ok token 10, queued until the events setup
[my-addon] frame tick: ok (token 10)
[events] Vm_Update_FlowStateMachine hooked at base+0x4423ce0
[events] Game_GameModule_OnSessionEnter hooked at base+0x80c300
[events] Game_ChampionshipFormationLapComponent_OnRaceStarted hooked at base+0xb808a0
[events] setup: live; onFrame armed, session.start armed, race.start armed
[events] 4 registration(s) queued before setup are armed: session.start token 7 (author.my-addon), career.loaded token 8 (author.my-addon), career.hub token 9 (author.my-addon), on_frame token 10 (author.my-addon)
[events] flow tick 1 observed, dt 0.000000; onFrame is live
[events] flow tick 2 observed, dt 0.000015; onFrame is live
[my-addon] 600 frames, last dt 0.0167 s; stopping the tick
[events] unsubscribe token 10 -> ok
[career] career.loaded: mode driver_career, key 'dc-00020001' (components live, component mode driver_career, Team HQ answer none)
[my-addon] career loaded, mode driver_career, career dc-00020001
[career] career.hub entered (mode driver_career)
[my-addon] Team HQ entered (driver_career)
```

- **Header:** `swofty/events.hpp`, namespace `swofty::events`
- **Table:** `SwoftyApi::events` (`SwoftyEvents`), API version 5
- **Feature:** `events`, stable, always on
- **Thread:** the flow thread (`Vm_Update_FlowStateMachine` each frame)

Every call returns `swofty::events::Result` (`ok`, `late`, `unavailable`, `invalid`); `swofty::events::name(r)` turns it into a string. See the [result codes](./#result-codes).

## available

```cpp
if (swofty::events::available()) { ... }
```

`available()` is `true` when the core implements events and its setup has not found the frame tick missing on this build. It is `true` from `SwoftyAddonInit` on, also before the setup runs.

## subscribe

```cpp
// void(const SwoftyEvent *ev, void *user)
static void on_session(const SwoftyEvent *ev, void *user);

uint32_t token = 0;
swofty::events::Result r = swofty::events::subscribe(SWOFTY_EVENT_SESSION_START, on_session, /*user*/ nullptr, &token);
// ok and fills token; invalid for an unknown event name or a null fn
```

`user` and `token` are optional (`nullptr`). Your callback gets a `SwoftyEvent`:

```c
typedef struct SwoftyEvent {
    const char *name;   // the event name
    uint64_t frame;     // the core's flow-thread frame counter at the event
    double time;        // seconds since the core dispatched its first frame tick or event
    int64_t a, b;       // event-specific integers
    const void *data;   // event-specific pointer, or null; valid only during the callback
} SwoftyEvent;
```

A name the core does not know returns `invalid`. A core older than an event answers `invalid` for it:

```text
[events] subscribe career.content.pressed by swofty.progressive -> invalid
```

## on_frame

```cpp
// void(double dt, void *user): dt is the seconds since the previous frame callback (0 on the first)
static void tick(double dt, void *user);

uint32_t token = 0;
swofty::events::on_frame(tick, nullptr, &token);
// ok and fills token; unavailable once the setup found no frame tick on this build
```

`on_frame` callbacks run on the first flow-thread update after each rendered frame, and at least every 33 ms when no frame is rendered, as on loading screens. Only one thread runs them at a time. The core logs the first two ticks:

```text
[events] flow tick 1 observed, dt 0.000000; onFrame is live
[events] flow tick 2 observed, dt 0.000015; onFrame is live
```

On a build where the frame tick does not resolve:

```text
[events] setup: flow tick did not resolve (onFrame will not fire on this build); onFrame off, session.start off, race.start off
[events] on_frame by author.my-addon -> unavailable: the flow tick did not resolve on this build, so onFrame never fires
```

## unsubscribe

```cpp
swofty::events::unsubscribe(token); // ok, or invalid for an unknown token
```

```text
[events] unsubscribe token 10 -> ok
```

A subscription lasts until you call `unsubscribe`. If your addon's `SwoftyAddonInit` returns failure, the core drops its subscriptions:

```text
[events] dropped 3 subscription(s) from a module that unloaded or failed to init
```

## Callbacks

- Callbacks run on the flow thread. Yours must return at once and never block.
- The core calls them outside its lock, so a callback may `subscribe` or `unsubscribe`, its own token included.
- Copy anything you keep from `data`: the pointer is valid only during the call.

## Registering before the setup

Registration is accepted at any time from `SWOFTY_ON_LOAD` on, and already in `SwoftyAddonInit`. When the addons load in one batch (no early stage), every `SWOFTY_ON_LOAD` runs before the core's events setup. A `subscribe` or `on_frame` made then returns `ok`, the core logs it as queued, and arms it when the setup completes:

```text
[events] on_frame by swofty.career-editor -> ok token 2, queued until the events setup
[events] 5 registration(s) queued before setup are armed: career.starting token 1 (swofty.rnd-tyres-thermal), on_frame token 2 (swofty.career-editor), career.loaded token 3 (swofty.career-editor), career.unloaded token 4 (swofty.career-editor), career.hub token 5 (swofty.career-editor)
```

If the setup finds no frame tick, the core logs each queued `on_frame` that will not fire, and keeps the queued event subscriptions:

```text
[events] on_frame token 2 by swofty.career-editor was queued before setup and will not fire: the flow tick did not resolve on this build
[events] 4 event subscription(s) queued before setup stay registered; each fires only when its source hook is armed (session.start off, race.start off; the career and flow events follow the [career] setup line)
```

## Event names

Choose the event with a `SWOFTY_EVENT_*` constant from `swofty_api.h`:

| Constant | String |
|---|---|
| `SWOFTY_EVENT_SESSION_START` | `session.start` |
| `SWOFTY_EVENT_SESSION_END` | `session.end` |
| `SWOFTY_EVENT_RACE_START` | `race.start` |
| `SWOFTY_EVENT_LAP` | `lap` |
| `SWOFTY_EVENT_FLAG` | `flag` |
| `SWOFTY_EVENT_STATE` | `state` |
| `SWOFTY_EVENT_SCREEN` | `screen` |
| `SWOFTY_EVENT_PAUSE` | `pause` |
| `SWOFTY_EVENT_RESUME` | `resume` |
| `SWOFTY_EVENT_RESULTS` | `results` |
| `SWOFTY_EVENT_FLOW_STATE` | `flow.state` |
| `SWOFTY_EVENT_CAREER_LOADED` | `career.loaded` |
| `SWOFTY_EVENT_CAREER_UNLOADED` | `career.unloaded` |
| `SWOFTY_EVENT_CAREER_HUB` | `career.hub` |
| `SWOFTY_EVENT_CAREER_STARTING` | `career.starting` |
| `SWOFTY_EVENT_CAREER_CONTENT_PRESSED` | `career.content.pressed` ([career contents](./career-content)) |

`session.start` fires when the game enters a session (`Game_GameModule_OnSessionEnter`), `race.start` when the formation lap component reports the race started. Both pass `a = b = 0` and no `data`.

## Flow and career events

These five come from the [career library](./career) (API version 6, feature `career`; `career.starting` API version 7). The payload pointer is valid only during the callback; copy what you keep. To ask for the current state instead of waiting for an event, use `swofty::career::state()` and `swofty::career::flow()`.

```cpp
static void on_flow(const SwoftyEvent *e, void *)
{
    // flow.state: a front-end flow state changed status
    auto *f = static_cast<const SwoftyFlowState *>(e->data);
    // e->a: new status, e->b: old status (SWOFTY_FLOW_NONE 0, ENTERING 1, UPDATING 2, EXITING 3)
    // f->name: "StateTeamHQUpdate", or "#" and the hash in hex when the core cannot read the name
    // f->hash: the djb2 of the name
}

static void on_loaded(const SwoftyEvent *e, void *)
{
    // career.loaded: a career is loaded and its mode is known
    auto *s = static_cast<const SwoftyCareerState *>(e->data);
    // e->a: mode, SWOFTY_CAREER_MY_TEAM (1) or SWOFTY_CAREER_DRIVER (2); e->b: 0
    // s->key: the career key once the player's entrant is known, "" before
}

static void on_unloaded(const SwoftyEvent *e, void *)
{
    // career.unloaded: the career is gone, or another career replaced it (then career.loaded follows)
    // e->a: the mode it had; e->b: 0; data: const SwoftyCareerState *
}

static void on_hub(const SwoftyEvent *e, void *)
{
    // career.hub: Team HQ is idle for the mode: StateTeamHQUpdate is updating with no time advance,
    // session, end of season or season break running. Fires in My Team and Driver Career alike.
    // e->a: 1 entered, 0 left; e->b: mode; data: const SwoftyCareerState *
}

static void on_starting(const SwoftyEvent *e, void *)
{
    // career.starting: the player chose to start or load a career in that mode on the Career menu,
    // before the game builds it
    auto *s = static_cast<const SwoftyCareerStart *>(e->data);
    // e->a: mode; e->b: SWOFTY_CAREER_START_PICK (0), _NEW (1) or _CONTINUE (2)
    // s->route: the front-end route; s->blocked: 1 when a block cancelled it
}
```

The `blocked` flag comes from [blocking a career mode](./career#blocking-a-career-mode). Real lines from Driver Career, and from R&D Tyres blocking My Team:

```text
[career] career.starting: driver_career (continue, route 'solo_career_continue_driver')
[career] career.loaded: mode driver_career, key 'dc-00020001' (components live, component mode driver_career, Team HQ answer none)
[career] career.hub entered (mode driver_career)
[career] career.hub left (mode driver_career)
[career] career.unloaded (mode driver_career)
[career] career.starting: my_team (pick, route 'career_dlc_select_my_team'); blocked by swofty.rnd-tyres-thermal
[rnd-tyres] My Team career start (route 'career_dlc_select_my_team') cancelled by the core; Driver Career only
```

- `career.hub` left always comes before `career.unloaded`.
- `flow.state` runs on the thread that changes the state, which is the flow thread.

## Availability

The core implements `on_frame`, `session.start`, `race.start` and, from core 0.2.11 (API version 6), the four flow and career events above, and from core 0.2.12 (API version 7) `career.starting`. `career.content.pressed` fires on cores with [career contents](./career-content). The other names (`session.end`, `lap`, `flag`, `state`, `screen`, `pause`, `resume`, `results`) are accepted but do not fire yet.

A build where a hook does not resolve leaves only that event silent. The `[events] setup:` and `[career] setup:` lines say which:

```text
[events] Game_GameModule_OnSessionEnter signature matched 0 time(s); hook not installed
[events] setup: live; onFrame armed, session.start off, race.start off
[career] setup: live; flow states armed, Team HQ answer armed, career layout yes, entrants yes, bus yes, string key bound
```

You can check the engine status in [the native-libraries design](/design/native-libraries#_1-events-frame-scheduler).
