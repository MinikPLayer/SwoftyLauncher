---
title: Graphics, replays and telemetry
description: Looks, post-process passes and graphics settings in code, with the present and setting events; and what's planned for replays and telemetry.
---

# Graphics, replays and telemetry

With the graphics system you change the picture: its colour grade, extra post-process passes on each frame, and the game's hardware settings. Monochrome Looks, for example, grades the race in black and white at a strength the player picks.

A **look** is a colour grade from a LUT file, applied at a strength. A **pass** is a shader that runs on the finished frame. A **setting** is one of the game's hardware settings, such as rain beads or ray-traced reflections.

::: info In 1.6.0
`swofty/systems/spectator.hpp` ships in 1.6.0: `spectator::set_active`, `follow`, `set_camera`, `current` and the `hud.spectator.visible` payload. `swofty/systems/graphics.hpp` (looks, passes, settings) ships later; the looks, passes and settings sections describe that design.
:::

## The header

<Split>

`swofty/systems/graphics.hpp` has three registries:

- `graphics::looks`: a LUT with a strength, stacked in order (`graphics.look`);
- `graphics::passes`: a post-process pass: shader, inputs, order (`graphics.pass`);
- `graphics::settings`: a hardware-setting override (`graphics.setting`).

Looks stack in order, so every enabled look applies.

<template #example>
<Example root="swofty-sdk" code-file="include/swofty/systems/graphics.hpp">
<template #code>

```cpp
#pragma once
#include <swofty/registry.hpp>
#include <swofty/events.hpp>

namespace swofty::graphics {

struct Look {
    Path lut;                                  // a .cube file; the core grades it into the game's ACES LUT
    float strength = 1.0f;                     // 0: the game's colours, 1: the full look
    int order = 0;                             // looks stack in ascending order
};
inline Registry<Look> looks{"graphics.look", { .applies = Applies::read }};   // (each change regrades at once)

struct Pass {
    Path shader;                               // HLSL, built against swofty/postfx.hlsli
    int order = 0;                             // passes run in ascending order, after the tonemap
    bool before_hud = true;                    // run on the frame before the HUD is drawn
    std::map<std::string, float> params;       // constants the shader reads
};
inline Registry<Pass> passes{"graphics.pass", { .applies = Applies::read }};

enum class Policy { set, at_most, at_least };

struct Setting {
    std::string key;                           // a hardware setting: "rainBeads", "trackHeatHaze", "rt_reflections", ...
    int value;                                 // bools are 0 and 1
    Policy policy = Policy::set;               // at_most: cap the player's choice instead of replacing it
};
inline Registry<Setting> settings{"graphics.setting", { .applies = Applies::read }};   // (when the game reads the setting)

namespace events {

struct Present {                               // IDXGISwapChain::Present, by vtable (core-owned)
    static constexpr const char *point = "render.present";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    void *swap_chain;
    uint32_t width, height;
    bool hdr;                                  // looks are skipped under HDR
};

struct SettingRead {                           // the Ne_Get_Setting* and Ne_Parse_Long sites
    static constexpr const char *point = "graphics.setting.read";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    std::string_view key;
    int value;                                 // before handlers may change it
};

} // namespace events

Result set_setting(std::string_view key, int value);

} // namespace swofty::graphics
```

</template>
</Example>
</template>
</Split>

## A LUT look

<Split>

Monochrome Looks grades the picture in monochrome, overcast or noir. The player picks the look and its strength in ADDON SETTINGS; the edit reads both options, so a change regrades at once, in the race.

<template #example>
<Example root="example.monochrome-looks" code-file="native/src/looks.cpp" data-file="registry/graphics.json" files="swofty.json, native/CMakeLists.txt, looks/monochrome.cube, looks/overcast.cube, looks/noir.cube">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/events.hpp>
#include <swofty/systems/graphics.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    auto grade = graphics::looks.add("grade", { .lut = file("looks/monochrome.cube"), .strength = 1.0f });

    graphics::looks.layer(grade, [](graphics::Look &l) {
        l.lut = file("looks/" + option<std::string>("look") + ".cube");   // monochrome, overcast, noir
        l.strength = option<int>("strength") / 100.0f;                    // 0..100 in steps of 10
    });
}

SWOFTY_ON_LOAD
{
    events::on<graphics::events::Present>().after([](const graphics::events::Present &p, const events::Context &) {
        static bool told = false;
        if (p.hdr && !told) {
            swofty::log("[monochrome] HDR is on: looks are skipped");
            told = true;
        }
    });
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
{
  "graphics.look": {
    "grade": { "lut": "looks/monochrome.cube", "strength": 1.0 }
  }
}
```

</template>
</Example>
</template>
</Split>

```text
[registry] register: example.monochrome-looks (looks.dll) 1 graphics.look, 1 layer on graphics.look (early stage, 0.1 ms)
[registry] graphics.look example.monochrome-looks/grade: edit by example.monochrome-looks changed /lut -> looks/noir.cube,
           /strength -> 0.8
[grade] stack: example.monochrome-looks/grade (0.8) -> aces_rrt_odt_lut 64^3, graded in 3.1 ms
[registry] option example.monochrome-looks/look changed: graphics.look 1 re-resolved; applied at once
```

The data version is one fixed look. Following the options takes code. A shader pack made for 1.5 and earlier, with `"shader": { "lut", "strength" }` in its manifest, loads as this record.

## Passes

```cpp
graphics::passes.add("glare", {
    .shader = file("shaders/glare.hlsl"),
    .order = 10,
    .before_hud = true,
    .params = { { "threshold", 0.85f }, { "intensity", option<float>("glare") } },
});
```

## Settings

Two addons can change the same hardware setting. Each change is a record, and its policy says how they combine.

```cpp
graphics::settings.add("no_rain_beads", { .key = "rainBeads", .value = 0 });
graphics::settings.add("rt_cap", { .key = "rt_reflections", .value = 1, .policy = graphics::Policy::at_most });

events::on<graphics::events::SettingRead>().before([](graphics::events::SettingRead &s, const events::Context &) {
    if (s.key == "trackHeatHaze")
        s.value = 0;
});

graphics::set_setting("rainSplashes", 0);
```

## The spectator camera

The game has a spectator mode: a director that follows any driver with a broadcast or TV pod camera, and its own HUD. Addons can turn it on in a single-player session, pick who it follows and with which camera, and keep its HUD up while it follows another car.

```cpp
namespace swofty::spectator {

enum class Camera : int32_t { tv_pod = 1, broadcast = 2 };

Result set_active(bool on);                    // the director and its HUD
Result follow(live::Car car);                  // the director moves to this car
Result set_camera(Camera camera);
struct View { live::Car following; Camera camera; };
std::optional<View> current();                 // what the director shows now

namespace events {

struct HudVisible {                            // the game decides whether the spectator HUD shows: before
    static constexpr const char *point = "hud.spectator.visible";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    live::Car following;
    bool visible;                              // set true to keep the HUD up
};

} // namespace events
} // namespace swofty::spectator
```

```cpp
// a director in an offline race: follow a car with the broadcast camera, HUD always on
spectator::set_active(true);
spectator::follow(car);                        // a cars::live::Car from cars::live::cars()
spectator::set_camera(spectator::Camera::broadcast);

events::on<spectator::events::HudVisible>().before([](spectator::events::HudVisible &h, const events::Context &) {
    h.visible = true;
});
```

The game hides the spectator HUD while the director follows a car other than the player's; `hud.spectator.visible` is where an addon keeps it.

## Replays and telemetry

Planned, with no header yet:

- **`replay.hero_shots`:** per-track highlight cameras, applied at boot. Hook point `replay.recordable.register` (`Sess_Recordable_Register`), where grid-size addons size the replay holders.
- **`telemetry.channel`:** a value an addon publishes per car or per session (a custom tyre system's thermal state), so dashboards read one feed. Hook points `session.tick` (every frame after physics, core-owned) and `telemetry.udp.prepare` (append addon packets, or widen past 22 cars).

## Readiness and limits

- **Looks and passes: ready** (shipped). They sit outside the KB. The core hooks D3D12 and DXGI by vtable.
- **The spectator camera: implemented, waiting on verification.** `SpectatorDirector::setActive` (`Sess_SpectatorDirector_SetActive`, L2), the requested and current driver (+0xDC, +0xD6) and camera (+0xE0, +0xD8), the UI flag `session.spectator_mode.active` (+0xD0) and the HUD hide with reason 6 (`Sess_SpectatorHud_Hide`, L2) are in the KB. `follow` stores the driver byte in the requested-driver field (+0xDC), as the HUD next and previous commands do. The core finds the director by watching the game's calls to it, so a spectator call answers late until the game has called the director once in the session. `hud.spectator.visible` fires on every hide with `following` filled in; a handler that sets `visible` skips the hide, and the HUD is not shown again if it was already hidden. Whether the director renders a usable camera in an offline race is in-game check 4.
- **`render.present` runs every frame.** A look is graded into the LUT once when it changes, not per frame. A `Present` handler should only read state it already has.
- **Settings: ready.** The hardware-setting sites are L2 (16 bool and 12 int), used by one addon.
- **Replays and telemetry: partial.** `Sess_Recordable_Register` is L2; the rest is L1. UDP holds 22 cars; the live per-wheel values are unverified.
- **HDR and frame generation change the pipeline.** Looks are skipped under HDR. The HUD draws after the ACES LUT.
- **Replay holders** are sized for 22 to 24 cars: the hidden grid-size limit.
- **Game updates** move the setting sites; vkd3d hangs with ray tracing; shader archives come in per-GPU pairs (`_amd`).
