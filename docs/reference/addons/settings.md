---
title: Addon settings
description: Declare a toggle, choice and slider in options, pick db or native binding, and read the value from an op file, an engine plugin and a launcher plugin.
---

# Addon settings

A setting is an entry in `options` in `swofty.json`. It shows on your addon's row on the launcher's **Addons** page and on its page in the game's **ADDON SETTINGS**, and each profile stores its own value.

## Settings in full

The [addon template](./template) declares four settings. Its engine plugin reads three of them while the game runs; the fourth is built into the game database.

```jsonc [swofty.json]
{
  "schema_version": 1,
  "id": "example.sample-mod",
  "name": "Sample Mod",
  "version": "0.1.0",
  "author": "Your Name",
  "compat": { "game": "f1_25", "game_builds": [">=25251167"] },
  "requires": [{ "id": "swofty.core", "version": ">=0.1.0" }],
  "options": [
    {
      "id": "verbose",
      "type": "toggle",
      "label": "Verbose log",
      "description": "Log setting changes and the signature scan to swofty/loader.log.",
      "default": false,
      "binds": "native"                  // the engine plugin reads it; a change applies at once
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
    },
    {
      "id": "penalty_seconds",
      "type": "slider",
      "label": "Stop-go penalty",
      "description": "Length of a stop-go penalty in seconds. It is written into the game database, so a change needs a rebuild.",
      "min": 5,
      "max": 20,
      "step": 5,
      "default": 5,
      "binds": "db"                      // the build writes it into the game files; a change needs a rebuild
    }
  ],
  "operations": {
    "database": ["db/example.json"],
    "native": [{ "dll": "native/sample_plugin.dll" }]
  }
}
```

The `db` setting goes into an op file as a placeholder:

```json [db/example.json]
{
  "schema_version": 1,
  "ops": [
    { "op": "set", "select": "rules.stop_go_penalty", "field": "penalty_duration", "value": "{{penalty_seconds}}" }
  ]
}
```

The `native` settings are read in the engine plugin by their ids:

```cpp [native/src/sample_plugin.cpp]
#include <swofty/mixin.hpp>

SWOFTY_SETTING(bool, verbose, false);                  // option id, C++ type, fallback
SWOFTY_SETTING(int, greeting_count, 1);
SWOFTY_SETTING(std::string_view, greeting, "hello");

SWOFTY_ON_CHANGE(verbose, bool on)                     // runs when the player changes it in ADDON SETTINGS
{
    swofty::log("[sample] verbose logging %s", on ? "on" : "off");
}

SWOFTY_ON_LOAD
{
    std::string_view text = greeting;                  // reading gives the profile's current value
    for (int i = 0; i < greeting_count; i++)
        swofty::log("[sample] %.*s from the sample mod", int(text.size()), text.data());
}

SWOFTY_ADDON()
```

Try it:

```sh
swofty settings set example.sample-mod penalty_seconds 10
swofty run --apply
```

Open **Career → ADDON SETTINGS**. The **Sample Mod** tile shows a **Stop-go penalty** slider at 10, a **Greetings** slider, a **Greeting** choice and a **Verbose log** toggle. A `native` setting you change takes effect the same lap. The `db` **Stop-go penalty** reads *Applies after a restart.*

## Option types

| `type` | Value | Rules |
|---|---|---|
| `toggle` | `true` or `false` | No `values`, `min`, `max` or `step`. |
| `slider` | A number on the grid | Supply `min`, `max`, `step`. `max - min` must be a whole number of steps, at most 100. Choose a `default` on the grid. |
| `choice` | One of `values` | Supply 1 to 16 values, each a string or `{ "value", "label" }`. Pick one as the `default`. |

Give each option an `id` that matches `[a-z][a-z0-9_]*`, is at most 40 characters and is unique within your addon. [The manifest reference](./manifest#options) has every field, including `label`, `description` and `hidden`.

## Where the value goes

`binds` decides who reads the value, and when a player's change takes effect:

| `binds` | Read by | A change applies |
|---|---|---|
| `native` | the addon's [engine plugin](./native-plugins#settings), while the game runs | at once |
| `db` | the build, through <span v-pre>`{{id}}`</span> and `when` in op files | after a rebuild and a restart |

A [launcher plugin](./launcher-api)'s build hook receives **every** declared option's value, whatever its `binds`, in its `build` call.

## Read a `db` setting {#_3-read-a-db-setting}

A <span v-pre>`{{id}}`</span> placeholder in an op file is replaced with the setting's value when the profile builds:

```json [db/example.json]
{ "op": "set", "select": "rules.stop_go_penalty", "field": "penalty_duration", "value": "{{penalty_seconds}}" }
```

`when` applies an op only when each listed setting has the given value. Custom Grid Size uses it to run each track's edits for the car count they were made for:

```json
{ "op": "patch-pkg", "when": { "cars": 26 }, "erp": "…", "resource": "…", "edits": [] }
```

A `db` setting goes into the build, so changing it marks the profile for a rebuild, and the next launch applies it. In game, a `db` setting's description ends with *Applies after a restart.*

## Read a `native` setting

`SWOFTY_SETTING(type, id, fallback)` declares a setting with the option's `id`, its C++ type and a fallback:

| C++ type | Option type |
|---|---|
| `bool` | `toggle` |
| Any number type | `slider` |
| `std::string_view` | `choice` |

```cpp
SWOFTY_SETTING(std::string_view, greeting, "hello");

SWOFTY_ON_CHANGE(greeting, std::string_view text)
{
    if (verbose)
        swofty::log("[sample] greeting is now \"%.*s\"", int(text.size()), text.data());
}
```

- Reading a setting gives the profile's current value.
- `SWOFTY_ON_CHANGE` runs when the player changes the setting in ADDON SETTINGS.
- With no option of that `id` and type in `swofty.json`, you get the fallback, and `swofty/loader.log` says why:

```text
[settings] sample_plugin.dll (example.sample-mod): no option greting in settings.json; the code default is used
[settings] sample_plugin.dll (example.sample-mod): greeting is a choice option, the code reads a toggle; the code default is used
```

The plugin is named in `operations.native`:

```json [swofty.json]
"native": [{ "dll": "native/sample_plugin.dll" }]
```

In C, call `api->setting(id, type, &out)` and `api->watch_setting(id, fn, user)`; see [Settings library](/reference/std/settings).

## Read a setting in a launcher plugin

Your [launcher plugin](./launcher-api) receives every option's value in `params.options` when its `build` call arrives:

```json
{ "method": "build", "params": { "options": { "greeting_count": 1, "penalty_seconds": 10 }, "features": { "stable": ["file"], "experimental": [] } } }
```

```rust [src/main.rs]
fn build(&self, options: &Value) -> Result<Value, String> {
    let seconds = options["penalty_seconds"].as_u64().unwrap_or(5);
    // … build ops from `seconds`
}
```

## How a value persists and travels

A profile stores the values under `options`, keyed by addon id. Only values the player changed from the default are there:

```json [$DATA/profiles/default.json]
"options": {
  "example.sample-mod": { "penalty_seconds": 10 }
}
```

1. When you build, the launcher writes the profile's values into `swofty/settings.json` in the game folder.
2. At launch, SwoftyMod reads that file and builds each addon's page in ADDON SETTINGS. It also answers the plugin's `setting` calls.
3. Change a value in game and the plugin updates at once: its `SWOFTY_ON_CHANGE` handlers run, and the change is written to the requests file.
4. The launcher merges the requests file into the profile, so you'll see the new value on the **Addons** page.

```text
[settings] …\swofty\settings.json: profile 'default', 16 addon(s), 10 option(s), 59 command(s); requests file …\state\requests\default.json
[settings] example.sample-mod.greeting_count = 3
```

To list, change or reset settings from the command line:

```sh
swofty settings list example.sample-mod
swofty settings set example.sample-mod penalty_seconds 10
swofty settings reset example.sample-mod
```

## Reference

- [Addon manifest](./manifest#options): every `options` field.
- [Settings library](/reference/std/settings): the C ABI and every C++ helper for `native` settings.
- [Database operations](/reference/ops/database): placeholders and `when` in op files.
- [Command line](/reference/cli#swofty-settings): `swofty settings`.
