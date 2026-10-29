---
title: Characters
description: Per-career character overrides with swofty/characters.hpp — swap a character's portrait and name, mute a speaker and set career-only strings, for one career data group.
---

# Characters

`swofty/characters.hpp` changes who a character is in careers of one data group: another character's portrait, names and job on a character's ref, a speaker's voice lines played silently, and strings that hold only while such a career is loaded; careers of every other data group, and every other mode, stay stock.

```cpp [native/src/agent.cpp]
#include <string>

#include <swofty/characters.hpp>
#include <swofty/mixin.hpp>
#include <swofty/storage.hpp>

namespace ch = swofty::characters;

constexpr const char *kGroup = "f1_career_1990";   // the career data group the entries apply to

SWOFTY_SETTING(bool, agent_voice_mute, true);

SWOFTY_ON_LOAD
{
#ifdef SWOFTY_API_CHARACTERS                       // a header that declares the table
    if (!ch::available())                          // an older core: every call returns unavailable
        return;

    // the agent's ref (portrait, names, job) shows the flavio row, under elena's stat_id
    ch::Result r = ch::swap(kGroup, "elena", "flavio");
    swofty::log("[f1-1990] agent: elena shows flavio in %s careers (%s)", kGroup, ch::name(r));

    // the agent's lines play at speech volume 0, with their subtitles and callbacks
    if (agent_voice_mute) {
        ch::Result a = ch::mute(kGroup, "agent"), b = ch::mute(kGroup, "agent_cutscene");
        swofty::log("[f1-1990] voice: agent muted in %s careers (%s, %s)", kGroup, ch::name(a), ch::name(b));
    }

    // {"eng": {"lng_key": "text", ...}, "fre": {...}}: set while a 1990 career is loaded
    std::string json;
    if (swofty::storage::read("data/flavio-strings.json", json) == swofty::storage::Result::ok)
        swofty::log("[f1-1990] strings: %s", ch::name(ch::strings_json(kGroup, json)));
#endif
}

SWOFTY_ADDON()
```

```json [swofty.json]
{
  "schema_version": 1,
  "id": "author.my-career",
  "name": "My Career",
  "version": "0.1.0",
  "author": "Your Name",
  "kind": ["database", "native"],
  "compat": { "game": "f1_25" },
  "requires": [{ "id": "swofty.core", "version": ">=0.2.12" }],
  "options": [
    { "id": "agent_voice_mute", "type": "toggle", "label": "Silent agent", "default": true, "binds": "native" }
  ],
  "operations": {
    "database": ["db/characters.json"],
    "native": [{ "dll": "native/agent.dll", "early": true, "files": ["data/"] }]
  }
}
```

`db/characters.json` adds the `flavio` row to `entrants.characters`. `"early": true` puts the ref hooks in place before the game's first character list build (see [When to register](#when-to-register)).

`swofty/loader.log`, from the registrations to a 1990 career load:

```text
[characters] swap 'elena' in careers of 'f1_career_1990' to flavio
[characters] mute agent lines in careers of 'f1_career_1990'
[characters] mute agent_cutscene lines in careers of 'f1_career_1990'
[characters] 1858 string(s) in 11 language(s) for careers of 'f1_career_1990'
[characters] setup: character refs armed, voices armed
[characters] character list built from 00000003257FD9C8 (119 rows, group '')
[characters] career loaded: active group 'f1_career_1990'
[characters] strings: cleared 0, set 1858 for 'f1_career_1990' (0 failed)
[characters] career loaded; character list rebuilt (1 stand-in ref(s))
```

- **Header:** `swofty/characters.hpp`, namespace `swofty::characters`. Compile-time check: `#ifdef SWOFTY_API_CHARACTERS`.
- **Table:** `SwoftyApi::characters` (`SwoftyCharacters`). The member arrived with API version 8; the table is feature-detected and null on cores without it.
- **Feature:** `career`, stable.
- **Thread:** registration in addon init or `SWOFTY_ON_LOAD`, then from any thread. `refresh` on the game thread.

Every call returns a `swofty::characters::Result` with the [result codes](./#result-codes); `ch::name(r)` names it. A core without the table returns `unavailable` from every call. Group, character, speaker and key names are 1 to 127 characters with no control characters; anything else returns `invalid`.

## When to register

The core installs its hooks only once an addon needs them: the ref hooks after the first `swap`, the voice hook after the first `mute`.

- **From the init check** you pass to `SWOFTY_ADDON`: installed with that commit's addon mixins. An addon's own wrap of the same function chains with the core's.
- **From `SWOFTY_ON_LOAD`:** installed right after that commit's `SWOFTY_ON_LOAD` handlers.
- **Until then** a registration returns `ok`. After the last mixin commit, one that needs hooks that were never installed returns `unavailable` and logs once:

```text
[characters] a swap registered after the last mixin commit; the ref hooks are not installed
```

A game where no addon swaps or mutes runs none of these hooks, and an addon that wraps `Game_SpeechSystem_PostLine` or the character ref functions itself keeps its wraps.

Register from an addon with `"early": true` to have the ref hooks in place before the game's first character list build; otherwise `refresh()` and the career events rebuild the list only after the game has built it once more.

## Data groups

```cpp
std::string group = swofty::characters::career_group();   // "f1_career_2026", an addon's group, or ""
```

Every entry is keyed by a career data group: the data group of the career's `CareerSetupComponent`, the name the [career content](./career-content) tile carries (`f1_career_2025`, `f1_career_2026`, an addon's own group). `career_group()` returns the loaded career's data group, or `""` with no career loaded. The core works it out when the core's `career.loaded` event fires, clears it on `career.unloaded`, and reads it again on `refresh()`:

```text
[characters] career loaded: active group 'f1_career_2026'
[characters] career unloaded: active group ''
```

The data group is read the way the driver-career save-slot predicate (0x1438fb390) reads it: each championship of the career entity (`+0x1040..+0x1048`), its `CareerSetupComponent` through the championship's component list (`+0x208`, vtable slot `+0x10`), the component's data-group index (virtual `+0x268`), the group list (`*(component +0x290) +0x560`) and the group name (`*(group +0x28) +0x10`). These offsets are not KB struct rows yet.

## swap and restore

```cpp
namespace ch = swofty::characters;
ch::swap("f1_career_1990", "elena", "flavio");   // the elena ref is built from the flavio row
ch::restore("f1_career_1990", "elena");          // drops that swap
```

In careers of `group`, the ref of the `entrants.characters` row `character` is built from the row `replacement` (names, `job_role`, the `message_<name>` textures), under `character`'s `stat_id`.

- `swap` returns `late` when the group's career is already loaded: the swap applies at the next character list build, or at once after `refresh()`.
- When two addons swap the same character in the same group, the last one wins and the core logs it.
- The replacement row has to be in the character table (add it with a database operation). Without it the ref stays stock.

How it applies: the UI keeps one VM node per character row under `character_list`, filled by `Game_EntrantsUi_BuildCharacterRef`; the subtitle box, emails and contacts bind those nodes. The game builds the list in `Game_EntrantsUi_OnCharacterListLoaded` (the `GameCharacterListLoaded` handler) when it loads its characters. The core keeps that character vector, and when a career load or unload changes the active group for a swapped character, it runs the handler again, so the list is rebuilt for the new group.

```text
[characters] swap 'elena' in careers of 'f1_career_1990' to flavio
[characters] 'elena' in 'f1_career_1990' is also swapped to 'flavio' by another addon; the last one wins
[characters] no 'flavio' row in the character list; 'elena' keeps its own ref
[characters] career loaded; character list rebuilt (1 stand-in ref(s))
```

## mute

```cpp
swofty::characters::mute("f1_career_1990", "agent");            // on
swofty::characters::mute("f1_career_1990", "agent", false);     // off again
```

In careers of `group`, lines of `speaker` (`agent`, `agent_cutscene`, ...) play at speech volume 0. `Game_SpeechSystem_PostLine` posts each line on the line's emitter. For a muted speaker the core sets the emitter's `vol_speech` RTPC to 0 before the line is posted, so its subtitles and end-of-line callbacks still run. An emitter that is muted once stays muted; when it speaks for another speaker that line is silent too, and the core logs it:

```text
[characters] mute agent lines in careers of 'f1_career_1990'
[characters] agent line muted (emitter 000000012A4C8E10, 1 so far)
[characters] emitter 000000012A4C8E10, muted earlier, also speaks engineer; that line is silent too
```

## Strings

```cpp
namespace ch = swofty::characters;
ch::string("f1_career_1990", "lng_agent_name", "Flavio");            // every language ("*")
ch::string("f1_career_1990", "lng_agent_name", "Flavio", "fre");     // one language code
ch::clear_string("f1_career_1990", "lng_agent_name");                // drops the "*" one
ch::strings_json("f1_career_1990", R"({"eng": {"lng_agent_name": "Flavio"}, "fre": {"lng_agent_name": "Flavio"}})");
```

- `string(group, key, text, language = "*")`: in careers of `group`, `key` shows `text` (a [string override](./strings)).
- `clear_string(group, key, language = "*")`: drops that string.
- `strings_json(group, json)`: every string of a JSON object of language objects, `{"eng": {"lng_key": "text"}, "fre": {...}}`. Anything other than an object of objects returns `invalid`; values that are not strings are skipped.

A group's strings are set through the [string overrides](./strings) when the group becomes active and cleared when it stops being active. The core does not call `swofty::lang::refresh()`.

```text
[characters] 1858 string(s) in 11 language(s) for careers of 'f1_career_1990'
[characters] strings: cleared 0, set 1858 for 'f1_career_1990' (0 failed)
```

## clear and refresh

```cpp
swofty::characters::clear("f1_career_1990");   // every swap, mute and string this addon made for the group
swofty::characters::refresh();                 // game thread: read the data group again, rebuild the list now
```

Several addons can register for the same or different groups. Entries belong to the module that registered them, so `clear` drops only yours.

`refresh()` reads the loaded career's data group again and rebuilds the game's character list now, so a swap registered while a career is loaded shows. It returns `late` when the list cannot be rebuilt yet:

```text
[characters] refresh; the character list is not rebuilt (the game has not built it yet)
```

## available

```cpp
#ifdef SWOFTY_API_CHARACTERS
if (swofty::characters::available()) { /* ... */ }
#endif
```

`available()` is `true` when the core has the table, the `career` feature is on, and its character or voice hooks are installed or not needed yet. To build one plugin against cores with and without the library, guard the calls with `#ifdef SWOFTY_API_CHARACTERS` and check `available()` at runtime.

The refs and the voices are core mixin sets of their own (`swofty_core.dll (character refs)`, `swofty_core.dll (character voices)`), so a binding that does not resolve turns off only that part. After each commit that installs a part, the core logs each part as `armed`, `off` or `not used`:

```text
[characters] setup: character refs armed, voices not used
```
