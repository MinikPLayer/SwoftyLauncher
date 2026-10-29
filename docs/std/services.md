---
title: Services
description: Versioned function tables one addon publishes for others to look up by name, in place of GetProcAddress between addons.
---

# Services

A **service** is a table of functions that one addon publishes and other addons look up by name and version. Use one for things that are calls rather than data: "which team did this driver race for in this year", "is this career a Progressive career". Data that other addons should read or override goes in a [registry](/std/registry) instead.

For example, Progressive Career publishes `progressive.history`. Tails' "what if" addon looks it up to learn which year the loaded Progressive career is in.

## Publishing

```cpp
// progressive_api/history.hpp, shipped by Progressive for other addons to include
struct ProgressiveHistory {
    static constexpr const char *name = "progressive.history";
    static constexpr uint32_t version = 1;

    uint32_t size;
    // the team (stat id) the driver raced for in that year; 0 when he didn't race
    uint32_t (*real_team)(uint32_t driver_stat_id, int year);
    // the career year the loaded Progressive career is in; 0 outside one
    int (*career_year)(void);
    // 1 when the loaded career follows the player's real career, 0 when it chooses teams
    int (*follow_mode)(void);
};
```

<Split>

Progressive publishes its table when it loads. The struct is in a header it ships, and the table is plain function pointers.

<template #example>
<Example root="swofty.progressive" code-file="native/src/history.cpp" files="swofty.json, native/CMakeLists.txt, api/progressive_api/history.hpp, native/src/history.hpp">
<template #code>

```cpp
#include <swofty/services.hpp>
#include <progressive_api/history.hpp>
#include "history.hpp"

static const ProgressiveHistory kHistory{
    sizeof(ProgressiveHistory),
    &history::real_team,
    &history::career_year,
    &history::follow_mode,
};

SWOFTY_ON_LOAD
{
    swofty::services::publish(kHistory);
}

SWOFTY_ADDON()
```

</template>
</Example>
</template>
</Split>

## Looking it up

A lookup made before the provider has loaded waits, and the callback runs once the table is published. DLL load order doesn't matter:

```cpp
// another addon
static const ProgressiveHistory *g_history = nullptr;

SWOFTY_ON_LOAD
{
    swofty::services::when<ProgressiveHistory>([](const ProgressiveHistory &h) {
        g_history = &h;
    });
}

void on_team_hq()
{
    if (!g_history || !g_history->career_year())
        return;                                  // Progressive isn't on, or this isn't a Progressive career
    int year = g_history->career_year();
    swofty::log("[whatif] Progressive career in %d", year);
}
```

`when` passes only a table of the requested version or newer, and of at least the size the consumer was built against. A provider that publishes an older version never reaches the callback, and the core logs it once.

## Versions

A service table follows the [versioning](/std/versioning) rules:

- **Minor:** append a function at the end and raise `version`. Old consumers never read past `size`.
- **Major:** a new name (`progressive.history2`) published next to the old one. The old one keeps being published.

```cpp
// version 2 appends one function; version 1 consumers are unaffected
struct ProgressiveHistory {
    static constexpr const char *name = "progressive.history";
    static constexpr uint32_t version = 2;
    uint32_t size;
    uint32_t (*real_team)(uint32_t driver_stat_id, int year);
    int (*career_year)(void);
    int (*follow_mode)(void);
    uint32_t (*follow_team)(uint32_t driver_stat_id, int year);   // since 2
};

// a consumer that needs version 2
if (g_history->size >= offsetof(ProgressiveHistory, follow_team) + sizeof(g_history->follow_team))
    team = g_history->follow_team(driver, year);
```

## Replacing GetProcAddress

A service takes the place of reaching into another addon's DLL by export name. Progressive's own DLLs use the service, like any other addon. This is the export route it replaces:

```cpp
// reaching into progressive.dll by export name
static int (*fn)() = nullptr;
HMODULE m = GetModuleHandleW(L"progressive.dll");
fn = m ? reinterpret_cast<int (*)()>(GetProcAddress(m, "ProgressiveNamesYear")) : nullptr;
int year = fn ? fn() : 0;
```

Progressive keeps its `GetProcAddress` exports in every later release, and code that calls them keeps working (see [Compatibility](/std/compatibility#progressive-career-s-exports)).

## Declaring

`services::publish` and `services::when` are the declaration. A consumer that can't work without a service can also say so in `registry.json`, which only holds dependencies:

```json
// the consumer's registry.json (optional)
{
  "requires": [ { "service": "progressive.history", "version": ">=1" } ]
}
```

The core checks this entry in game. Once every addon has loaded, a missing or too-old service is logged with the consumer's name, and the consumer's tile in ADDON SETTINGS and in the launcher shows a status line naming the service. Without the entry, the consumer's `when` callback never runs, and the core logs the missing service once.
