---
title: Weather
description: Track climate and scripted forecasts in code, with the weather setup event and the seed and script operations.
---

# Weather

With the weather system you change how often it rains at a track, and script a session's weather slot by slot. A wet-race addon, for example, makes the Belgian Grand Prix start dry and end in heavy rain.

A **climate** is a track's weather weights and temperature range. A **forecast** is a script for one session: a list of **slots**, each a kind of weather, how long it lasts and how long the change into it takes.

::: info In 1.6.0
The weather system has no header in 1.6.0 (`swofty/systems/weather.hpp` ships later). This page describes the design.
:::

## The header

<Split>

`swofty/systems/weather.hpp` has two registries:

- `weather::climates`: a track's weather weights and temperatures (`weather.track`);
- `weather::forecasts`: a scripted forecast for one session (`weather.forecast`).

The core applies a forecast at `session.weather.setup`, the moment Career Mode Editor's weather module also uses.

<template #example>
<Example root="swofty-sdk" code-file="include/swofty/systems/weather.hpp">
<template #code>

```cpp
#pragma once
#include <swofty/registry.hpp>
#include <swofty/events.hpp>
#include <swofty/systems/sessions.hpp>
#include <swofty/systems/tracks.hpp>

namespace swofty::weather {

enum class Kind { clear, cloudy, overcast, light_rain, heavy_rain, storm };

struct Climate {
    Ref<tracks::Track> track;                  // tracks.Track_N.Weather_N
    std::array<float, 5> weights;              // clear, cloudy, overcast, light rain, heavy rain
    float temperature_min;                     // degrees C
    float temperature_max;
};
inline Registry<Climate> climates{"weather.track", { .applies = Applies::boot }};

struct Slot {
    Kind kind;                                 // the weather in this slot
    std::chrono::minutes duration;             // how long it lasts
    std::chrono::minutes transition;           // how long the change into it takes
};

struct Forecast {
    events::Selector within;                   // where it applies: a content, a data group, a track
    sessions::Session session;                 // which session of the weekend
    std::vector<Slot> slots;                   // the script, in order
    std::optional<uint32_t> seed;              // a fixed random seed for what the script leaves open
};
inline Registry<Forecast> forecasts{"weather.forecast", { .applies = Applies::session, .snapshot = true }};

namespace events {

struct Setup {                                 // Sess_GameWeatherSystem_SetupSession
    static constexpr const char *point = "session.weather.setup";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    sessions::Session session;                 // the session being set up
    uint32_t seed;                             // the random seed; before handlers may set it
    uint32_t rain_chance;                      // 0..100; before handlers may change it
    std::span<Slot> slots;                     // the datasets as slots; before handlers may rewrite them
};

} // namespace events

Result set_seed(sessions::Session session, uint32_t seed);              // before the session's setup
Result script(sessions::Session session, std::span<const Slot> slots);

} // namespace swofty::weather
```

</template>
</Example>
</template>
</Split>

## A scripted forecast

<Split>

A wet-race addon scripts the Belgian Grand Prix: dry for 20 minutes, clouding over, then rain to the flag. It applies in every career and every Grand Prix and Time Trial box that races at Spa, and only while its `storm` option is on.

Qualifying keeps the game's weather, with a fixed seed so every player gets the same one.

<template #example>
<Example root="mod.spa-storm" code-file="native/src/forecast.cpp" data-file="registry/weather.json" files="swofty.json, native/CMakeLists.txt">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/events.hpp>
#include <swofty/systems/weather.hpp>

using namespace swofty;
using namespace std::chrono_literals;
using weather::Kind;

static const events::Selector kSpa = events::track("game/spa");

SWOFTY_REGISTER
{
    auto race = weather::forecasts.add("spa_race", {
        .within = kSpa,
        .session = sessions::Session::race,
        .slots = {
            { Kind::clear,      20min, 0min },
            { Kind::overcast,   15min, 5min },
            { Kind::light_rain, 20min, 5min },
            { Kind::heavy_rain, 60min, 3min },
        },
    });

    weather::forecasts.layer(race, [](weather::Forecast &f) {
        if (!option<bool>("storm"))
            f.slots.clear();                               // no slots: the game's own weather
    });
}

SWOFTY_ON_LOAD
{
    events::on<weather::events::Setup>().in(kSpa).when(events::session_type({ events::SessionType::qualifying }))
        .before([](weather::events::Setup &w, const events::Context &) {
            w.seed = 1990;
        });
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
{
  "weather.forecast": {
    "spa_race": {
      "within": { "track": "game/spa" },
      "session": "race",
      "slots": [
        { "kind": "clear",      "duration": 20, "transition": 0 },
        { "kind": "overcast",   "duration": 15, "transition": 5 },
        { "kind": "light_rain", "duration": 20, "transition": 5 },
        { "kind": "heavy_rain", "duration": 60, "transition": 3 }
      ]
    }
  }
}
```

</template>
</Example>
</template>
</Split>

```text
[registry] register: mod.spa-storm (spa_storm.dll) 1 weather.forecast, 1 layer on weather.forecast (early stage, 0.1 ms)
[events] session.weather.setup (driver_career, swofty.progressive/f1_career_progressive, race, game/spa):
         forecast mod.spa-storm/spa_race applied by the core (4 slots); 0 before handlers
[events] session.weather.setup (grand_prix, qualifying_1, game/spa): 1 before handler (mod.spa-storm): seed 1990
```

The data version is always on. It can't read the `storm` option, and it has no seed handler.

A career freezes the forecasts that apply to a season when that season starts.

## Climate

A climate change reaches every box and career that races at the track, except a box that sets its own climate there.

```cpp
// a wetter Silverstone, everywhere
weather::climates.layer(weather::climates["game/silverstone"], [](weather::Climate &c) {
    c.weights = { 0.30f, 0.25f, 0.20f, 0.15f, 0.10f };
    c.temperature_max = 21.0f;
});
```

## Events and operations

```cpp
// cap the rain chance of every race in 1990 careers
events::on<weather::events::Setup>().in(events::content("swofty.f1-1990/f1_career_1990"))
    .when(events::session_type({ events::SessionType::race }))
    .before([](weather::events::Setup &w, const events::Context &) { w.rain_chance = std::min(w.rain_chance, 30u); });

// a launcher page asked for a fixed seed: Career Mode Editor's "weather seed"
weather::set_seed(sessions::Session::race, 424242);
weather::script(sessions::Session::practice_1, std::array{ weather::Slot{ Kind::heavy_rain, 60min, 0min } });
```

## Readiness and limits

- **Ready** for setup and seeds: `Sess_GameWeatherSystem_SetupSession`, the seed functions and the dataset build are L2, shipped in Career Mode Editor.
- **Partial:** the dataset layout is L1.
- **Unknown:** the track wetness and drying model, and how rain intensity changes grip.
- **The front-end scene loader** rebuilds dataset 0 as clear (0x1428c1ce0).
- **A forced seed persists in saves.**
- **Rain quality settings** (`rainBeads`, `rainSplashes`, `rainHigh`, `trackHeatHaze`) are graphics settings, see [Graphics](./graphics).
