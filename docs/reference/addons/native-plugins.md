---
title: Engine plugins
description: An engine plugin is a native DLL an addon names in operations.native. The in-game loader loads it into F1 25 and gives it SwoftyApi. A complete plugin first, then the build, the operations.native entry, the files it ships, load order and the SwoftyApi tables.
---

# Engine plugins

An engine plugin is your own code running inside F1 25: a prebuilt Windows x86-64 DLL that your addon names in `operations.native`. The in-game loader loads it and hands it a `const SwoftyApi *`, your way into the engine.

## A plugin in full

This is the [addon template](./template#templates-addon)'s plugin. It reads three settings, reacts when the player changes one, logs each career load and runs one signature scan:

```cpp [native/src/sample_plugin.cpp]
#include <swofty/career.hpp>
#include <swofty/events.hpp>
#include <swofty/mixin.hpp>

SWOFTY_SETTING(bool, verbose, false);                  // the option "verbose" in swofty.json, false if it's missing
SWOFTY_SETTING(int, greeting_count, 1);
SWOFTY_SETTING(std::string_view, greeting, "hello");

SWOFTY_ON_CHANGE(verbose, bool on)                     // the player changed it in ADDON SETTINGS
{
    swofty::log("[sample] verbose logging %s", on ? "on" : "off");
}

SWOFTY_ON_CHANGE(greeting, std::string_view text)
{
    if (verbose)
        swofty::log("[sample] greeting is now \"%.*s\"", int(text.size()), text.data());
}

void on_career_loaded(const SwoftyEvent *ev, void *)
{
    auto *s = static_cast<const SwoftyCareerState *>(ev->data);
    swofty::log("[sample] career loaded: %s %s", swofty::career::name(swofty::career::mode(*s)), s->key);
}

SWOFTY_ON_LOAD                                         // runs at the mixin commit, after SwoftyAddonInit
{
    if (swofty::career::available())                   // the career table is null on an older core
        swofty::events::subscribe(SWOFTY_EVENT_CAREER_LOADED, on_career_loaded);
    std::string_view text = greeting;
    for (int i = 0; i < greeting_count; i++)
        swofty::log("[sample] %.*s from the sample mod", int(text.size()), text.data());
    uintptr_t hit = 0;
    int n = swofty::api->scan(".xtls", "48 89 5C 24 ?? 57 48 83 EC 20", &hit);   // hit count; hit is set on exactly one
    if (verbose)
        swofty::log("[sample] example signature: %d hit(s), first at %p", n, reinterpret_cast<void *>(hit));
}

SWOFTY_ADDON()                                         // exports SwoftyAddonInit and SwoftyAddonApi
```

Name the DLL in `swofty.json`:

```json [swofty.json]
"operations": {
  "native": [{ "dll": "native/sample_plugin.dll" }]
}
```

Build it, install the addon and play:

```sh
native/build.sh
swofty addons install .
swofty addons enable example.sample-mod
swofty run --apply
```

`swofty/loader.log` in the game folder shows the loader loading the plugin, then the plugin's own lines:

```text
addons: loading the addons without "early": true at the window stage
addon sample_plugin.dll: loading …\swofty\addons\example.sample-mod\native\sample_plugin.dll
addon sample_plugin.dll: loaded at 00006fffea3e0000, calling SwoftyAddonInit (API v8)
addon sample_plugin.dll: SwoftyAddonInit returned 1 (ok)
…
[sample] hello from the sample mod
addons: mixin commit done, 2 mixin addon(s) active
```

Write your plugin in C or C++ against [`swofty_api.h`](https://github.com/Swofty-Developments/SwoftyLauncher/blob/main/packages/sdk/include/swofty_api.h) (MIT) and the `swofty/*.hpp` headers beside it.

## The map

- **[Write the plugin](#the-entry-point)**: `SwoftyAddonInit`, `SWOFTY_ADDON()` and `SWOFTY_ON_LOAD`.
- **[Build the DLL](#build-the-dll)**: `zig c++` or any toolchain that makes a Windows x86-64 DLL.
- **[The `operations.native` entry](#the-operations-native-entry)**: the DLL, `early`, and the `files` it reads.
  - **[Files in the game folder](#files-in-the-game-folder)**: where the build puts them.
  - **[Load order](#load-order)**: the early stage and the window stage.
- **[SwoftyApi](#swoftyapi)**: the members and API versions.
- **[Settings](#settings)**: reading your addon's options.
- **[The mixin system](./hooking)**: wrap an engine function or run code at an instruction.
- **[In-game UI](./in-game-ui)**: add a screen and a home tile, and change existing screens.
- **[Standard library](/reference/std/)**: the headers and `SwoftyApi` tables for mixins, settings, logging, events, the running career, overlay, telemetry, database reads, storage and UI.
- **[Engine API](/reference/game/)**: the engine functions, structs and sites you can call and hook.
- **[Changes at runtime](./runtime-assets#from-a-native-plugin)**: change textures, text and database records from a plugin while the game boots.
- **[Rules](#rules)**.

To test a plugin, build the profile, play, and find your lines in `swofty/loader.log` ([Packaging and publishing](./publishing)).

## The entry point

The loader calls into your plugin through `SwoftyAddonInit`. `SWOFTY_ADDON()` exports it, and exports `SwoftyAddonApi` with the value of `SWOFTY_API_VERSION` (8), the version the plugin was built against:

```c
__declspec(dllexport) const uint32_t SwoftyAddonApi = SWOFTY_API_VERSION;
__declspec(dllexport) int SwoftyAddonInit(const SwoftyApi *api);   // non-zero on success, 0 on failure
```

- The DLL stays loaded whether init succeeds or fails, so leave no hooks installed if initialization fails.
- If your DLL exports only `SwoftyModInit`, the loader accepts that name too. `SWOFTY_MOD()` is the same as `SWOFTY_ADDON()`.
- `SWOFTY_ON_LOAD` code runs once the loader commits the mixins of every plugin loaded at that stage.

The smallest plugin:

```cpp [native/src/my_plugin.cpp]
#include <swofty/mixin.hpp>

SWOFTY_ON_LOAD
{
    swofty::log("[my-addon] loaded, game base 0x%llx", (unsigned long long)swofty::api->module_base());
}

SWOFTY_ADDON()
```

What the loader writes for each kind of DLL:

```text
addon my_plugin.dll: loaded at 00006fffea3e0000, calling SwoftyAddonInit (API v8)
addon my_plugin.dll: SwoftyAddonInit returned 0 (FAILED)
addon old_plugin.dll: loaded at 00006fffea3e0000, API v1 plugin, SwoftyModInit runs after the mixin commit
addon helper.dll: loaded at 00006fffea3e0000 but exports no SwoftyAddonInit or SwoftyModInit, left idle
```

## Build the DLL

The template's `native/build.sh` runs:

```sh
zig c++ -target x86_64-windows-gnu -std=c++23 -O2 -Wall -Wextra -Iinclude -shared -s \
    -o my_plugin.dll src/my_plugin.cpp
```

```text
built /path/to/addon/native/my_plugin.dll
```

MSVC or any other toolchain that produces a Windows x86-64 DLL works too.

## The `operations.native` entry

```jsonc [swofty.json]
"operations": {
  "native": [
    {
      "dll": "native/my_plugin.dll",       // the plugin DLL, relative to the addon folder
      "early": false,                      // true loads it at the early stage; false waits for the window stage
      "files": ["lang/", "erp/frag.bin"]   // files and folders it reads while the game runs; default empty
    }
  ]
}
```

`early` is described under [the early stage](/reference/addons/asset-api#the-early-stage). Install, enable and build to put the plugin in the game folder:

```sh
swofty addons install .
swofty addons enable example.my-addon
swofty build
```

This copies `native/my_plugin.dll` to `swofty/addons/example.my-addon/native/` in the game folder and lists it in `swofty/settings.json` for the loader. Press **Play**, or run `swofty run`, to load it.

### Files in the game folder

List the files your plugin needs while the game runs in `files`. With the addon enabled in the profile, the build copies those entries and your `dll` to `swofty/addons/<id>/` in the game folder, keeping their paths within the addon. The entry above produces:

```text
swofty/addons/example.my-addon/native/my_plugin.dll
swofty/addons/example.my-addon/lang/...
swofty/addons/example.my-addon/erp/frag.bin
```

- Listed folders include their subfolders, except entries whose names start with `.`.
- Nothing else from your addon folder is copied.
- A file path you pass to the [Asset API](/reference/addons/asset-api#native-plugins) or [Storage](/reference/std/storage) is relative to `swofty/addons/<id>/`. So `lang_table("eng", "lang/extra.lng")` reads the file you shipped as `lang/extra.lng`.
- These files install through the journal, as the loader does. Turn the addon off, uninstall it, or switch to a profile without it, and the next build removes them. `swofty restore` removes them too.
- A missing listed file makes the build fail, and so do two plugins in the profile with the same file name.

### Load order

The profile's load order carries through to the `plugins` list the build writes in `swofty/settings.json`. The loader starts with `swofty/addons/core/`, then loads each listed plugin from its addon folder, then any other DLL directly in `swofty/addons/`, in name order.

With `"early": true` your plugin loads at the early stage. Otherwise it waits for the window stage, in the same order:

```text
addons: swofty\addons\core: 2 dll(s)
addon classic_seasons.dll: loads at the window stage
addons: swofty\addons: no .dll files
…
addons: loading the addons without "early": true at the window stage
addon classic_seasons.dll: loading …\swofty\addons\swofty.classic-seasons\native\classic_seasons.dll
```

To find which addon a plugin belongs to, swofty_core looks up its file name through `dlls` in `swofty/settings.json`.

## SwoftyApi

The `const SwoftyApi *` you receive in `SwoftyAddonInit` has these members:

```c
typedef struct SwoftyApi {
    uint32_t version;                                                    // the API version the loader offers
    // version.dll (v1)
    void (*log)(const char *fmt, ...);                                   // write to swofty/loader.log
    uintptr_t (*module_base)(void);                                      // the base address of F1_25.exe
    int (*scan)(const char *section, const char *pattern, uintptr_t *out);   // "48 89 5C 24 ?? 57" in ".xtls": the hit count; out set only on exactly one
    // swofty_core.dll (v4)
    uintptr_t (*resolve)(const char *id);                                // a binding's address, or 0
    int (*register_mixins)(const SwoftyMixin *mixins, size_t count);    // from SwoftyAddonInit only
    void *(*fields)(const void *obj, size_t size);                       // per-addon storage on a game object
    int (*setting)(const char *id, uint32_t type, SwoftySettingValue *out);   // read a setting
    int (*watch_setting)(const char *id, SwoftySettingFn fn, void *user);    // call fn when a setting changes
    const SwoftyAssets *assets;                                          // change assets while the game boots
    // swofty_core.dll (v5)
    const SwoftyEvents *events;                                          // lifecycle events and the frame tick
    const SwoftyOverlay *overlay;                                        // the 2D overlay
    const SwoftyTelemetry *telemetry;                                    // live car and session reads
    const SwoftyDb *db;                                                  // game-database reads
    const SwoftyStorage *storage;                                        // addon storage
    // swofty_core.dll (v6; career-mode blocks v7)
    const SwoftyCareer *career;                                          // flow states, the running career, its components, the game bus
    // swofty_core.dll (v7)
    const SwoftyPopup *popup;                                            // the game's message popup with your text
    // swofty_core.dll (v8)
    const SwoftyLang *lang;                                              // runtime string overrides
    // swofty_core.dll (v8), feature-detected: null on cores without them
    const SwoftyCareerContent *career_content;                           // career content tiles
    const SwoftyCharacters *characters;                                  // per-career character overrides
    const SwoftyCareerWizard *career_wizard;                             // new-career wizard control
    const SwoftyEra *era;                                                // era picker boxes
} SwoftyApi;
```

Each table has its page in the standard library:

- `log`: [Logging](/reference/std/logging). `resolve`: [bindings](/reference/std/mixin#bindings). `register_mixins`: [the mixin system](/reference/addons/hooking). `fields`: [fields](/reference/std/mixin#fields). `setting`: [Settings](/reference/std/settings#read-from-c).
- `assets`: [Asset API](/reference/addons/asset-api#native-plugins).
- `events`: [Events](/reference/std/events). `overlay`: [Overlay](/reference/std/overlay). `telemetry`: [Telemetry](/reference/std/telemetry). `db`: [Database read](/reference/std/db). `storage`: [Storage](/reference/std/storage).
- `career`: [Career](/reference/std/career). `popup`: [Popup](/reference/std/popup). `lang`: [Strings](/reference/std/strings).
- `career_content`: [Career contents](/reference/std/career-content). `characters`: [Characters](/reference/std/characters). `career_wizard`: [Career wizard](/reference/std/career-wizard). `era`: [Era picker boxes](/reference/std/era-picker).

For guarded reads of game memory (`read`, `in_image`, `object_like`, `c_string`), include the header-only [`swofty/mem.hpp`](/reference/std/memory); it needs no table.

The API version depends on what's loaded: `version.dll` alone offers version 1; `swofty_core.dll` raises it to 8 (7 before string overrides, 6 before core 0.2.12, 5 before core 0.2.11). For the engine functions and structs you can call and hook, see the [Game API](/reference/game/).

## Settings

Read your addon's [options](/reference/addons/manifest#options) by option `id`, and react when the player changes them in game, with `swofty/mixin.hpp`:

```cpp
SWOFTY_SETTING(bool, show_panel, true);

SWOFTY_ON_CHANGE(show_panel, bool on)
{
    swofty::log("[my-addon] panel %s", on ? "on" : "off");
}
```

In C, call `api->setting(id, type, &out)` and `api->watch_setting(id, fn, user)`. [Addon settings](./settings) shows a whole addon's settings, and [Settings](/reference/std/settings) has the full C++ and C reference.

## Rules

- Find addresses by signature, not fixed addresses. Stop when a signature has zero or multiple matches.
- Hook only `.xtls` code. Never patch `.data1` bodies.
- Keep hook setup out of `DllMain`.
- Keep modded play [offline](/reference/safety).
