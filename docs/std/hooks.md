---
title: Hooks
description: Game hook points, their phases, hook points the game calls every frame, addon hook points, what happens when a handler faults, and how hook points share game functions with raw mixins.
---

# Hooks

A **hook point** is a named place in the game where the core runs every subscribed addon in one ordered chain. A **game hook point** covers one game function: the core owns the mixin on that function, and addons subscribe to the point instead of patching the function themselves. Two addons that need the same game function then work side by side.

For example, Progressive Career and Career Editor both need `Game_CareerActivities_AddActivity`. Each subscribes to `career.calendar.add_activity`, and the core runs both handlers, in order, on every call.

## A hook in full

<Split>

Progressive Career keeps the contract event off a Follow-your-driver player's calendar. It subscribes a `replace` handler to the core's `career.calendar.add_activity` point.

The payload type is in the career system's header, `swofty/systems/career.hpp`, in `swofty::career::events`.

<template #example>
<Example root="swofty.progressive" code-file="native/src/follow.cpp" files="swofty.json, native/CMakeLists.txt, native/src/progressive.hpp">
<template #code>

```cpp
#include <swofty/hooks.hpp>
#include <swofty/systems/career.hpp>

using swofty::career::events::AddActivity;

SWOFTY_ON_LOAD
{
    // replace: return true to stand in for the game's code; false lets the next handler (or the game) run
    swofty::hooks::replace<AddActivity>([](AddActivity &a) -> bool {
        if (a.type != AddActivity::contract_event || !a.player || progressive::follow_mode() != 1)
            return false;
        if (!progressive::has_contract(a.driver)) {
            swofty::log("[progressive] follow: contract event on day %u: no contract yet; the first Team HQ signs", a.day);
            return false;                    // a new career: let the game add it, Team HQ removes it later
        }
        return progressive::sign_instead(a);  // signed: the event is never added
    });
}

SWOFTY_ADDON()
```

</template>
</Example>
</template>
</Split>

The payload is a plain struct with a size and a version. The core fills it from the game function's arguments:

```cpp
// swofty/systems/career.hpp (excerpt)
namespace swofty::career::events {

struct AddActivity {
    static constexpr const char *point = "career.calendar.add_activity";
    static constexpr uint32_t version = 1;
    static constexpr uint8_t contract_event = 0x0f;
    uint32_t size, payload_version;
    void *calendar;                                // the driver calendar the activity goes on
    const void *activity;                          // the activity definition
    uint32_t driver;                               // the calendar's driver, stat id << 16
    uint32_t day;                                  // before handlers may move it
    uint8_t type;                                  // the activity type
    bool player;                                   // the calendar belongs to the human player
};

} // namespace swofty::career::events
```

Payload fields are C types. A record id or a text is a `const char *` with a member function for its C++ form (`vehicle_id` and `vehicle()`, `section_str` and `section()`), and a callback is a C function pointer with a `user` pointer and a member function that calls it (see [ABI](/std/abi#rules)).

## Phases

Every call to a hook point runs three phases:

```text
game calls Game_CareerActivities_AddActivity(calendar, activity, day)
  core fills AddActivity
  before   handlers, highest priority first, then load order; each may change the payload
  replace  handlers, same order, until one returns true; if none does, the game's own code runs
  after    handlers, all of them; they see the final payload and the result
  core returns to the game
```

```cpp
// before: change the input
swofty::hooks::before<AddActivity>([](AddActivity &a) {
    if (a.type == AddActivity::contract_event && a.player)
        a.day = std::max(a.day, progressive::first_race_day());   // never before round 1
});

// after: observe what happened
swofty::hooks::after<AddActivity>([](const AddActivity &a, const swofty::hooks::Outcome &o) {
    if (a.type == AddActivity::contract_event)
        swofty::log("[career-editor] contract event %s on day %u", o.replaced ? "skipped" : "added", a.day);
});

// a priority, for when order matters (default 0; higher runs first)
swofty::hooks::before<AddActivity>(swofty::hooks::Priority{10}, [](AddActivity &a) { ... });
```

A hook point's reference says which thread it runs on: the flow thread for career points, the session thread for session points. Handlers must return quickly and must not call the same hook point.

## Game hook points

The core adds a game hook point when a second addon needs the same game function, with the payload and a verified binding. The first planned points, with the game function each covers and the addons that need it:

```text
career.calendar.add_activity     Game_CareerActivities_AddActivity      Progressive (contract events), Career Editor
career.contract.decide           Career_Decide_ContractNegotiation      Progressive (follow), Career Editor (contracts)
career.contract.sign             Career_Sign_DriverContract             Progressive, Career Editor
career.season.process            the season change (vtable slot +0x188)  Progressive (seasons), Career Editor
session.fuel.load                the race fuel lookup                   Progressive (seasons before 2026)
team.logo.resolve                the logo key lookups                   Progressive, Classic Seasons
```

`career.season.process` covers a function at L0 in the KB. It needs reverse engineering to L2 before it ships.

The core installs a game hook point's mixin only once an addon subscribes to it. With no subscriber, the game's code is untouched.

## Hook points the game calls every frame

Some game functions run every frame or hundreds of times a second: the logo key for each row of the timing tower, a text lookup, a view-model path, a driver's rating. The catalogue marks these points `hot`:

```text
team.logo.resolve       driver.ratings.read      lang.lookup      ui.vm.resolve      track.data.get
```

On a hot point the core builds a table at a content or session moment: when a box is picked, when a career loads, when a session loads. It runs the handler chain once per entry (each team, each driver and stat, each key) and stores the result. The game's calls then read the table. Your handler is written the same way as on any other point. It runs once per entry when the table is built, and it must not depend on anything that changes during the session.

```text
[hooks] team.logo.resolve (hot): table built at session.load: 11 teams, 2 handlers (swofty.progressive, swofty.classic-seasons-2017), 0.08 ms
[hooks] driver.ratings.read (hot): table built at career.loaded: 22 drivers x 5 stats, 1 handler (swofty.progressive), 0.31 ms
```

## Addon hook points

An addon can define its own point and fire it, so other addons can extend it:

```cpp
// api/progressive_api/hooks.hpp: let other addons adjust a season right before Progressive applies it
namespace progressive_api {
struct SeasonApply {
    static constexpr const char *point = "swofty.progressive.season.apply";
    static constexpr uint32_t version = 1;
    uint32_t size;
    uint32_t payload_version;
    int year;
    swofty::career::Season *season;    // before handlers may change it
};
}

// progressive.dll
SWOFTY_ON_LOAD
{
    swofty::hooks::define<progressive_api::SeasonApply>();
}

void apply_season(swofty::career::Season season)
{
    progressive_api::SeasonApply p{ sizeof p, progressive_api::SeasonApply::version, season.year, &season };
    if (!swofty::hooks::fire(p))                 // false: a replace handler applied it instead
        return;
    swofty::career::apply_lineup(season);
}
```

```cpp
// another addon: a driver who skipped a season comes back on the reserve seat
swofty::hooks::before<progressive_api::SeasonApply>([](progressive_api::SeasonApply &p) {
    if (p.year == 2017)
        swofty::log("[whatif] season %d has %zu teams", p.year, p.season->teams.size());
});
```

The owning addon ships the payload header (`progressive_api/hooks.hpp`) for others to include. `hooks::define` is the declaration: nothing goes in the manifest.

## When a handler faults

The core calls every handler inside a guard that catches access violations and C++ exceptions:

- **An `after` handler that faults** is isolated. The core logs the addon, the point and the phase, detaches that handler for the rest of the session, and lists it in `conflicts.json`. The game carries on.
- **A `before` or `replace` handler that faults** crashes the game. The crash report's first line names the addon, the point and the phase.

```text
[hooks] career.calendar.add_activity: after handler from someone.calendar-log faulted
        (access violation at calendar_log.dll+0x2210); handler detached for this session
```

## Living with mixins

A hook point is one entry in the game function's mixin chain, at the core's priority. Raw mixins keep working around it:

```text
[mixin] Game_CareerActivities_AddActivity: swofty_core.dll(hooks, 100) > old_addon.dll(0)
```

- **A `SWOFTY_WRAP` on the same function** chains below the hook point, as any two wraps chain.
- **A raw overwrite on the same function,** from any addon, conflicts with the hook point's mixin. The core's mixin is the one dropped. The hook point reports `unavailable`, its subscribers don't run, and the addon with the overwrite keeps working. No addon is stopped from overwriting a function that has a hook point.

```text
[hooks] career.calendar.add_activity unavailable: old_addon.dll overwrites Game_CareerActivities_AddActivity;
        subscribers swofty.progressive, swofty.career-editor not run (the launcher lists this as a conflict)
```

- **A hook point never drops an addon.** The core installs hook-point mixins at the window stage, after the mixin conflict pass, whatever stage the subscriber registered at. The early-stage overlap rule (a mixin that overlaps one installed at the early stage drops the later addon whole) never applies to a hook point's mixin.
- **A hook point installed while the game runs,** when its first subscriber arrives late, checks for overlap against every mixin installed at that moment, mid-hooks included. On an overlap it refuses itself and reports `unavailable`.

Subscribers can check before relying on a point:

```cpp
if (!swofty::hooks::live<AddActivity>())
    swofty::log("[progressive] the add-activity hook point isn't live; falling back to the contract scene skip");
```
