---
title: Settings
description: Declare an addon's options in swofty.json and read them from a plugin with SWOFTY_SETTING or the C ABI.
---

# Settings

Settings are the options you declare under [`options`](/reference/addons/manifest#options) in `swofty.json`; players set them on the launcher's **Addons** page and in the game's **ADDON SETTINGS**, the profile stores the values, and your plugin reads each one by its option `id`.

```cpp [native/src/sample_plugin.cpp]
// Trimmed from templates/addon/native/src/sample_plugin.cpp
#include <swofty/mixin.hpp>

SWOFTY_SETTING(bool, verbose, false);                   // option "verbose", a toggle; false if it's missing
SWOFTY_SETTING(int, greeting_count, 1);                 // option "greeting_count", a slider
SWOFTY_SETTING(std::string_view, greeting, "hello");    // option "greeting", a choice

// Runs when the player changes the option in ADDON SETTINGS, with the new value
SWOFTY_ON_CHANGE(verbose, bool on)
{
    swofty::log("[sample] verbose logging %s", on ? "on" : "off");
}

SWOFTY_ON_CHANGE(greeting, std::string_view text)
{
    if (verbose) // a setting reads as its value
        swofty::log("[sample] greeting is now \"%.*s\"", int(text.size()), text.data());
}

SWOFTY_ON_LOAD
{
    std::string_view text = greeting;
    for (int i = 0; i < greeting_count; i++)
        swofty::log("[sample] %.*s from the sample mod", int(text.size()), text.data());
}

SWOFTY_ADDON() // checks every SWOFTY_SETTING against swofty.json and connects the SWOFTY_ON_CHANGE handlers
```

```json [swofty.json]
{
  "schema_version": 1,
  "id": "example.sample-mod",
  "name": "Sample Mod",
  "version": "0.1.0",
  "author": "Your Name",
  "kind": ["native"],
  "compat": { "game": "f1_25", "game_builds": [">=25251167"] },
  "requires": [{ "id": "swofty.core", "version": ">=0.1.0" }],
  "options": [
    {
      "id": "verbose",
      "type": "toggle",
      "label": "Verbose log",
      "description": "Log setting changes to swofty/loader.log.",
      "default": false,
      "binds": "native"
    },
    {
      "id": "greeting",
      "type": "choice",
      "label": "Greeting",
      "description": "What the plugin writes to swofty/loader.log when it loads.",
      "values": ["hello", { "value": "good morning", "label": "Good morning" }, "hi"],
      "default": "hello",
      "binds": "native"
    },
    {
      "id": "greeting_count",
      "type": "slider",
      "label": "Greetings",
      "description": "How many times the plugin writes the greeting.",
      "min": 0,
      "max": 5,
      "step": 1,
      "default": 1,
      "binds": "native"
    }
  ],
  "operations": { "native": [{ "dll": "native/sample_plugin.dll" }] }
}
```

```text [swofty/loader.log]
[settings] D:\F1 25\swofty\settings.json: profile 'default', 17 addon(s), 51 option(s), 185 command(s); requests file Z:\home\swofty\.local\share\swofty\state\requests\default.json
[sample] hello from the sample mod
[settings] example.sample-mod.verbose = ON
[sample] verbose logging on
```

For an example of each option type, follow the [settings tutorial](/reference/addons/settings).

- **Header:** `swofty/mixin.hpp`
- **Table:** `SwoftyApi::setting` and `SwoftyApi::watch_setting`, API version 3
- **Feature:** `options`
- **Thread:** the flow thread, for `SWOFTY_ON_CHANGE` and `watch_setting` callbacks

## Declare in the manifest

```jsonc
{
  "id": "start_season",          // lowercase letters, digits and _, starting with a letter, at most 40 characters
  "type": "choice",              // toggle, slider or choice
  "label": "Starting season",    // shown to players; defaults to the id
  "description": "The real season a new Progressive career starts in.",
  "default": "2026",             // true/false, a number on the slider's steps, or one of the choice values
  "values": ["2016", "2017", { "value": "2026", "label": "2026" }], // choice: 1 to 16 strings or { "value", "label" }
  "binds": "native"              // native: the plugin reads it while the game runs; db: built into the game files
}
```

```jsonc
{ "id": "grid_size", "type": "slider", "min": 20, "max": 30, "step": 1, "default": 22, "binds": "native" }
// min, max, step: the slider range; max - min is a whole number of steps, at most 100
```

A `db` option is built into the game files, so a change needs a rebuild. Reference it in your operation files through <span v-pre>`{{id}}`</span> and `when`; see [database operations](/reference/ops/database#settings).

## Read from C++

### SWOFTY_SETTING

```cpp
SWOFTY_SETTING(type, id, fallback); // declares inline const swofty::setting<type> id
```

The C++ type picks the option type:

| `type` | Option `type` | Conversion |
|---|---|---|
| `bool` | `toggle` | |
| An integer type | `slider` | Rounded to the nearest whole number |
| A floating-point type | `slider` | |
| `std::string_view` | `choice` | The choice's value |

```cpp
// From addons/progressive/native/src/season.cpp
SWOFTY_SETTING(std::string_view, start_season, "2026");
SWOFTY_SETTING(bool, season_visuals, true);

std::string_view s = start_season.get(); // get(): the profile's value for the session
if (season_visuals)                      // or read it as a T
    apply_visuals(season_year(s));
if (start_season == "2016")              // operator== compares the value
    swofty::log("[progressive] first season");
```

```cpp
template <class T> struct swofty::setting {
    using type = T;
    const char *id;
    T fallback;
    T get() const;
    operator T() const;
    friend bool operator==(const setting &s, T v);
};

// a variable named differently from the option id
inline const swofty::setting<bool> show{"show_panel", true};
```

`get()` returns `fallback` if the loader is older than version 3, or `swofty.json` has no option with that `id` and type. At init, `SWOFTY_ADDON()` checks every setting you've declared and logs each one that `swofty.json` doesn't declare with the same type:

```text
addon settings: greeting_count is not a slider option of this addon in swofty.json; the code default is used
[settings] sample_plugin.dll (example.sample-mod): no option greeting_count in settings.json; the code default is used
```

On a core without the settings API, every setting keeps its code default:

```text
addon settings: swofty_core offers no settings API (v3); every setting keeps its code default
```

### SWOFTY_ON_CHANGE

```cpp
// From addons/progressive/native/src/ui/suffix.cpp
SWOFTY_ON_CHANGE(start_season, std::string_view value)
{
    if (progressive_wizard(g_wizard))
        backdrop(g_wizard, season_year(value), "start season changed");
}
```

When the player changes a value in **ADDON SETTINGS**, `SWOFTY_ON_CHANGE` runs with the new value. The core logs each change:

```text
[settings] swofty.progressive.start_season = 2017
[settings] community.custom-grid.grid_size = 24
```

## Read from C

```c
enum { SWOFTY_SETTING_TOGGLE = 1, SWOFTY_SETTING_SLIDER = 2, SWOFTY_SETTING_CHOICE = 3 };

typedef struct SwoftySettingValue {
    uint32_t type;
    int32_t toggle;
    double slider;
    const char *choice;
} SwoftySettingValue;

typedef void (*SwoftySettingFn)(const char *id, const SwoftySettingValue *value, void *user);
```

```c
static void on_grid(const char *id, const SwoftySettingValue *v, void *user)
{
    api->log("[my-addon] %s is now %g", id, v->slider); // every change, with user
}

SwoftySettingValue v;
if (api->version >= 3 && api->setting("grid_size", SWOFTY_SETTING_SLIDER, &v)) // non-zero: v holds this addon's value
    grid = (int)(v.slider + 0.5);
api->watch_setting("grid_size", on_grid, NULL);
```

- `api->setting(id, type, &out)` fills `out` with the calling addon's value and returns non-zero. If the addon has no option `id` of that `type`, it returns 0.
- `api->watch_setting(id, fn, user)` calls `fn` with `user` on every change.

Both calls need API version 3 and identify your addon by the calling DLL:

```text
[settings] my_plugin.dll asked for grid_size, but no addon in settings.json lists that dll; the code default is used
[settings] my_plugin.dll (author.my-addon): grid_size is a slider option, the code reads a toggle; the code default is used
```

## Files

- `$DATA/profiles/<name>.json`, written by the launcher: `options`, each addon's values that differ from the default.
- `swofty/settings.json`, written by the build: every enabled addon with its options, values and the commands ADDON SETTINGS sends.
- The requests file, written by swofty_core: changes made in game, which the launcher merges into the profile. The `[settings]` line names it.

```text
[settings] no settings.json; every addon setting keeps its code default
[settings] writing requests file Z:\home\swofty\.local\share\swofty\state\requests\default.json (seq 3) failed, error 5
```
