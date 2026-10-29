---
title: Race HUD
description: Show your text in the race HUD's own notification from native code with swofty/hud.hpp.
---

# Race HUD

`swofty/hud.hpp` shows your text in the race HUD's central notification and writes the HUD's own data leaves.

```cpp [native/src/my_plugin.cpp]
#include <swofty/events.hpp>
#include <swofty/hud.hpp>

namespace hud = swofty::hud;

SWOFTY_SETTING(bool, shaders_on, true);

// A setting change arrives on the flow thread; toast() queues from any thread
SWOFTY_ON_CHANGE(shaders_on, bool on)
{
    hud::Result r = hud::toast(on ? "SHADERS ON" : "SHADERS OFF", {}, 2); // main line, no second line, 2 s
    if (r != hud::Result::ok)
        swofty::log("[my-addon] no toast: %s", hud::name(r));
}

void on_session(const SwoftyEvent *, void *)
{
    hud::toast("My Addon", "Shaders follow the addon's setting", 4, "checkpoint"); // a race flag icon
    hud::set("ui_menu_data.hud_lap_number_visible", false);                       // show the lap description...
    hud::set("ui_menu_data.hud_lap_description_loc_tag", false);                  // ...as literal text
    hud::set("ui_menu_data.hud_lap_description_string", "MY ADDON");
}

SWOFTY_ON_LOAD
{
    if (!hud::available()) // false on an older core, or when the message system did not resolve
        return;
    swofty::events::subscribe(SWOFTY_EVENT_SESSION_START, on_session);
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
  "requires": [{ "id": "swofty.core", "version": ">=0.1.0" }],
  "options": [
    { "id": "shaders_on", "type": "toggle", "label": "Shaders", "default": true, "binds": "native" }
  ],
  "operations": { "native": [{ "dll": "native/my_plugin.dll" }] }
}
```

```text [swofty/loader.log]
[hud] toast queued by author.my-addon for 4s: "My Addon"
[hud] set applied by author.my-addon: ui_menu_data.hud_lap_number_visible = 0
[hud] set applied by author.my-addon: ui_menu_data.hud_lap_description_loc_tag = 0
[hud] set applied by author.my-addon: ui_menu_data.hud_lap_description_string = "MY ADDON"
[hud] toast shown for author.my-addon as 'swofty_toast_4' icon checkpoint: "My Addon"
[hud] toast queued by author.my-addon for 2s: "SHADERS OFF"
[hud] toast shown for author.my-addon as 'swofty_toast_2': "SHADERS OFF"
```

The notification is the one the game uses for DRS enabled, a fastest lap or a penalty. The game's UI draws it in the game's style, so it follows the HUD's layout, scale and language settings, and needs no [overlay](./overlay) drawing.

- **Header:** `swofty/hud.hpp`, namespace `swofty::hud`
- **Table:** `SwoftyHud` version 1, from the core's `SwoftyCoreHud` export, not a `SwoftyApi` member; API version 8, feature-detected
- **Feature:** `popup`, stable
- **Thread:** `toast` and `set` from any thread; the core posts them on the flow thread

The wrapper reads the table through `GetProcAddress(swofty_core.dll, "SwoftyCoreHud")`, so a plugin built against this header also loads on an older core and sees `available()` return `false` there. Fields are only ever appended to the table; `version` is the last layout the core fills, so check it before calling a field added after version 1. `SWOFTY_API_HUD` marks a header that declares the table.

Every call returns a `swofty::hud::Result` (see [result codes](./#result-codes)), and `name(r)` turns one into `"ok"`, `"unavailable"` and so on.

## available

```cpp
if (swofty::hud::available()) {
    // the running core exports SwoftyCoreHud and found the game's message system on this build
}
```

## toast

```cpp
hud::toast("Pit limiter on");                            // default second line, 4 s, default icon
hud::toast("Pit limiter on", "Manual pit lane", 3);      // a second line, 3 s
hud::toast("Rain in 5 minutes", {}, 7, "generic");       // 7 s is as near 6 as 8: rounds to 6
hud::toast("");                                          // invalid: empty text
hud::toast("Hi", {}, 4, "green");                        // invalid: unknown icon
```

- `text` is the main line and `detail` an optional second line.
- `seconds` is rounded to the nearest of 2, 3, 4, 5, 6, 8, 10, 12, 15, 20 and 30; a tie goes to the shorter one. The default is 4, and 0 or less means 4.
- Text is UTF-8, and each string is capped at 200 bytes.
- It returns `ok` once queued, `invalid` for empty `text` or an unknown `icon`, and `unavailable` on an older core or when the message system did not resolve.
- The queue holds at most 8 toasts (the oldest goes first). The HUD shows them one after another by priority, as it does its own notifications.
- The HUD draws a toast only while it is on screen: during a session on track (race, qualifying, practice, time trial). A toast posted in the menus is not drawn.

```text
[hud] toast by author.my-addon refused: empty text
[hud] toast by author.my-addon refused: unknown icon 'green'
[hud] toast by author.my-addon: unavailable (the message system did not resolve on this build)
[hud] toast queue full (8); dropping the oldest toast, from author.my-addon
```

`icon` is empty for the default icon, or one of the game's race flag icons:

```text
generic        diagonal          black                     checkpoint      chequered
fast_lap       formation_lap     pp_icon                   red             safety_car
virtual_safety_car               virtual_safety_car_green  flag_rival_won
```

## set

`set(path, value)` writes one leaf of the HUD's data on the flow thread. It takes text, a `bool` or an `int32_t`:

```cpp
hud::set("ui_menu_data.hud_lap_description_string", "OUT LAP");  // text, capped at 200 bytes
hud::set("ui_menu_data.hud_lap_number_visible", false);          // bool
hud::set("view_model_data.hud_my_counter", int32_t(3));          // int
hud::set("ui_menu_data.lap_number_visible", false);              // invalid: not a hud_* leaf
```

- Only leaves under `ui_menu_data.hud_*` and `view_model_data.hud*` (`view_model_data.hud.` or `view_model_data.hud_`) are accepted; anything else returns `invalid`. A leading `#` is dropped. A path with a space, `[` or `]`, or a trailing `.`, returns `invalid`.
- It returns `ok` once queued, and `unavailable` as for `toast`.
- A later write to the same path replaces a queued one. The queue holds at most 64 writes; past that the oldest drops.
- The game also writes many of these leaves, so a value can be overwritten by the game's own update. Write it again when it matters.

```text
[hud] set by author.my-addon refused: 'ui_menu_data.lap_number_visible' is not a hud_* leaf (ui_menu_data.hud_*, view_model_data.hud*)
[hud] set applied by swofty.progressive: view_model_data.hud_progressive_career = 1
[hud] set failed by author.my-addon: view_model_data.hud_my_counter = 3 (the parent node did not resolve)
```

The core logs the first 16 applied writes and every failed one.

Progressive Career publishes two view-model bools its Team HQ tab reads:

```cpp
// addons/progressive/native/src/season.cpp, trimmed
constexpr const char *kVmCareer = "view_model_data.hud_progressive_career";
constexpr const char *kVmRnd = "view_model_data.hud_progressive_rnd";

void publish(bool career, const std::string &rnd)
{
    bool show_rnd = career && rnd == "hybrid";
    auto a = swofty::hud::set(kVmCareer, career), b = swofty::hud::set(kVmRnd, show_rnd);
    swofty::log("[progressive] view model: %s = %s (%s), %s = %s (%s)", kVmCareer, career ? "true" : "false",
                swofty::hud::name(a), kVmRnd, show_rnd ? "true" : "false", swofty::hud::name(b));
}
```

Leaves the race HUD reads from `ui_menu_data`:

```cpp
"ui_menu_data.hud_lap_description_string"   // text: the lap counter shows it while hud_lap_number_visible is false
"ui_menu_data.hud_lap_description_loc_tag"  // bool: true, the description is a lng_* key; false, literal text
"ui_menu_data.hud_lap_number_visible"       // bool: false shows the description instead of the lap number
"ui_menu_data.hud_total_lap_count_visible"  // bool: the lap total
```

## How it works

The race HUD's central notification (`hud_notifications_dynamic` in `templates/hud_dynamic_templates.ui`) draws the message at the front of the game's `hud_main_queue`, the same message system the [popup](./popup) uses. Its labels read the message's `message` and `sub_message` attributes and honour each attribute's `localise` flag, and its timer reads `msg_duration`.

The launcher's UI build adds `swofty_toast_<seconds>` to `message_data/hud_messages.uimsg` (in `flow_persistent.erp`), one per duration: type `hud_main`, queue `hud_main_queue`, priority 2, literal (`localise: false`) `message` and `sub_message`, the fixed `msg_duration`, the generic race flag and `enabled_in_spectator`.

```text
toast("Pit lane open", {}, 4)      queued by the core, any thread
  next flow tick                   construct 'swofty_toast_4'
                                   set message = "Pit lane open", sub_message = "", flag = #overrides.osd.race_flags.<icon>
                                   release the handle
  the race HUD                     shows it in hud_main_queue order
```

The core finds the message system from the first message the game constructs, or, when the game has constructed none yet (a benchmark race starts without one), from the UI system. The core log says what it posted:

```text
[popup] message system 00000004A9D4A330 from the UI system (no message constructed yet)
[hud] toast shown for my-addon as 'swofty_toast_4': "Pit lane open"
```

Without `swofty_toast_*` in the game's data (a launcher older than this core), the toast is dropped with one log line:

```text
[hud] toast from author.my-addon dropped: the game has no 'swofty_toast_4' message (the launcher's UI build adds the swofty_toast_* HUD messages)
```

## Testing

Environment variables for the game process:

```text
SWOFTY_TOAST_TEST=6
    a test toast (SWOFTY TOAST <n>) every 6 s, cycling through the icons, with a [hud] log line each time
    [hud] SWOFTY_TOAST_TEST: a test toast every 6s
    [hud] toast shown for SWOFTY_TOAST_TEST as 'swofty_toast_4' icon checkpoint: "SWOFTY TOAST 17"

SWOFTY_HUD_SET_TEST=HELLO
    every 5 s through set: writes HELLO to the lap counter description and hides the lap number,
    the lap total and the top-left position tower
    [hud] set applied by SWOFTY_HUD_SET_TEST: ui_menu_data.hud_lap_description_string = "HELLO"

SWOFTY_HUD_DEV_SET=b:<path>=1;s:<path>=<text>;i:<path>=<n>
    sets any registry leaf every 5 s; a development switch that skips the path check
    [hud] SWOFTY_HUD_DEV_SET: mfds.osd_mfd.active = 1 every 5 s (dev switch, any path)
```

The lap counter widget is not on screen in a benchmark race, so `SWOFTY_HUD_SET_TEST` writes show only in the `[hud] set applied` log lines there.
