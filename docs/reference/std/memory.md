---
title: Memory
description: Read F1 25 memory without crashing on a bad pointer - region-checked reads, image and object checks, and safe C strings with swofty/mem.hpp.
---

# Memory

`swofty/mem.hpp` reads game memory only after checking that the bytes are committed and readable, so a stale or wrong pointer gives you `false` instead of a crash.

```cpp [native/src/my_plugin.cpp]
#include <swofty/career.hpp>
#include <swofty/events.hpp>
#include <swofty/game.hpp>
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <cstddef>
#include <string>

namespace mem = swofty::mem;
using swofty::game::CareerEntity;

// The career's track data system, or 0. From Progressive Career's fuel code, trimmed.
static uintptr_t track_data_system(std::string &why)
{
    auto e = reinterpret_cast<uintptr_t>(swofty::career::entity());
    if (!e)
        return why = "no championship entity yet", 0;
    if (!mem::object_like(e)) // a game object starts with a vtable inside the exe image
        return why = "the championship entity is not a game object", 0;
    uintptr_t sys = 0;
    if (!mem::read(e + offsetof(CareerEntity, track_data), sys) || !mem::object_like(sys)) // +0xf58
        return why = "the championship entity's +0xf58 is not a game object", 0;
    return sys;
}

static void on_hub(const SwoftyEvent *ev, void *)
{
    if (!ev->a) // 0: Team HQ left
        return;
    std::string why;
    uintptr_t sys = track_data_system(why);
    if (sys)
        swofty::log("[my-addon] track data system at %p", reinterpret_cast<void *>(sys));
    else
        swofty::log("[my-addon] no track data: %s", why.c_str());
}

SWOFTY_ON_LOAD
{
    swofty::events::subscribe(SWOFTY_EVENT_CAREER_HUB, on_hub); // career.hub runs on the flow thread
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
[career] career.hub entered (mode driver_career)
[my-addon] track data system at 000000002B6D4E90
```

- **Header:** `swofty/mem.hpp`, namespace `swofty::mem`
- **Table:** none; the header is self-contained, needs no core table and works with any loader
- **Feature:** none
- **Thread:** any

`CareerEntity` comes from the [Game API](/reference/game/) (`swofty/game.hpp`). Take field offsets from those structs rather than writing them by hand.

Each call takes an address as `uintptr_t` or as a pointer. Everything except `printable_utf8`, `ttl_ms` and `big_region` is Windows-only (`#ifdef _WIN32`).

## readable

```cpp
bool ok = mem::readable(p, 64); // true when 64 bytes at p are committed and readable
```

A null pointer or a range that wraps the address space is `false`. Pages marked `PAGE_GUARD` or `PAGE_NOACCESS` are not readable.

## read

```cpp
uintptr_t group = 0;
if (mem::read(wizard + kWizardGroup, group)) // copies a T into out only when its bytes are readable
    ...;
```

`read<T>(addr, T &out)` returns `true` and copies `sizeof(T)` bytes; on `false`, `out` is untouched.

## in_image and object_like

```cpp
mem::in_image(p);    // true when p lies in the game exe's image (code, vtables, static data)
mem::object_like(p); // a readable, 8-byte aligned pointer whose first word is an 8-byte aligned vtable in the image
```

A C++ object of the game starts with a vtable inside the exe image, so `object_like` tells game objects from garbage. Career Editor and Progressive Career check every component pointer with it:

```cpp
uintptr_t component(const void *desc)
{
    auto c = reinterpret_cast<uintptr_t>(swofty::career::component_by_desc(desc));
    return c && swofty::mem::object_like(c) ? c : 0;
}
```

## c_string

```cpp
std::string g;
if (mem::c_string(group, g, 64) && g == "f1_career_progressive") // max 64: at most 63 bytes before the NUL
    ...;
```

`c_string(p, std::string &out, max = 256)` is `true` for 1 to `max - 1` bytes of printable UTF-8 ending in a NUL; `out` is empty otherwise. It rejects text that isn't valid UTF-8, contains control characters or has no NUL within `max`, so it never accepts a pointer's own bytes as a name. It checks readability on the first byte and at each new 4 KB page.

## printable_utf8

```cpp
mem::printable_utf8(s, n);           // true for valid UTF-8 without control characters
mem::printable_utf8(std::string_view(name));
```

Tab, newline and carriage return pass. Overlong forms, surrogates, C1 controls (U+0080 to U+009F) and code points above U+10FFFF fail.

## Cost and the cache

```cpp
// mem::ttl_ms     = 250: how long a cached answer stays good
// mem::big_region = 1 MB: regions this size or larger are also cached per region
mem::invalidate(); // drop every thread's cached answers now
```

Each thread caches the region answers of `VirtualQuery` for 64 pages and keeps each for `ttl_ms` (250 ms). Regions of at least `big_region` bytes are also kept in a per-region table of 8 entries, so every page of the game's large heap views shares one answer. Most checks in a per-frame walk hit the cache, so the guards are cheap enough to run every frame.

Memory the game frees can stay marked readable in the cache until the entry expires. If you know the game has just freed or remapped memory you were reading, call `invalidate()`; otherwise the entries expire on their own.

## Trust

```cpp
static void poll(double, void *) // a poll that runs every 2 s, not every frame
{
    mem::Trust t(5000); // this thread keeps the answer for regions of 1 MB or more for 5 s
    uintptr_t sys = 0;
    if (mem::read(entity + offsetof(CareerEntity, track_data), sys) && mem::object_like(sys))
        ...;
} // t ends here: back to ttl_ms
```

A fresh `VirtualQuery` answer for an address in one of the game's multi-GB heaps can take milliseconds on Windows; under Wine and Proton its cost grows with the rest of the region (about 7 µs per GB). A poll that reads long-lived objects in those heaps less often than every 250 ms can hold a `Trust` for its reads, so it pays that once per `ms` instead of on every poll. While `t` is alive, this thread keeps the answer for a region of 1 MB or more for `ms` instead of `ttl_ms`; an `ms` below `ttl_ms` changes nothing. Only use it for memory that stays mapped for as long as you trust it.
