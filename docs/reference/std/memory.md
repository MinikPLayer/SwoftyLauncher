---
title: Memory
description: Read F1 25 memory without crashing on a bad pointer - region-checked reads, image and object checks, and safe C strings with swofty/mem.hpp.
---

# Memory

When your plugin follows pointers through game objects, a stale or wrong pointer must not crash the game. `swofty/mem.hpp` checks that the bytes are committed and readable before it touches them. It also tells game objects from garbage and reads C strings only when they are real text. The header is self-contained: it needs no core table and works with any loader.

| | |
|---|---|
| Header | `swofty/mem.hpp` |
| Namespace | `swofty::mem` |
| Table | none |
| Thread | Any |

## Calls

| Call | Returns |
|---|---|
| `readable(p, n)` | `true` when `n` bytes at `p` are committed and readable |
| `read<T>(addr, T &out)` | `true` and copies a `T` when its bytes are readable |
| `in_image(p)` | `true` when `p` lies in the game exe's image (code, vtables, static data) |
| `object_like(p)` | `true` for a readable, 8-byte aligned pointer whose first word is a vtable in the image |
| `c_string(p, std::string &out, max = 256)` | `true` for 1 to `max - 1` bytes of printable UTF-8 ending in a NUL |
| `printable_utf8(s, n)` | `true` for valid UTF-8 without control characters (tab, newline and carriage return pass) |
| `invalidate()` | drops every thread's cached region answers |
| `Trust t(ms)` | while `t` is alive, this thread keeps the answer for a region of 1 MB or more for `ms` instead of `ttl_ms` |

Each call takes an address as `uintptr_t` or as a pointer. `c_string` rejects text that isn't valid UTF-8, contains control characters or has no NUL within `max`, so it never accepts a pointer's own bytes as a name.

## Cost

Each thread caches the region answers of `VirtualQuery` for 64 pages and keeps each for `ttl_ms` (250 ms). Most checks in a per-frame walk hit the cache, so the guards are cheap enough to run every frame. Memory the game frees can stay marked readable in the cache until the entry expires. If you know the game has just freed or remapped memory you were reading, call `invalidate()`.

A fresh `VirtualQuery` answer for an address in one of the game's multi-GB heaps can take milliseconds on Windows. A poll that reads long-lived objects in those heaps less often than every 250 ms can hold a `Trust` for its reads, so it pays that once per `ms` instead of on every poll. Only use it for memory that stays mapped for as long as you trust it.

## Example

```cpp
#include <cstddef>
#include <swofty/mem.hpp>

struct Named {
    void **vtable;
    const char *name;
};

bool name_of(uintptr_t obj, std::string &out)
{
    uintptr_t text = 0;
    return swofty::mem::object_like(obj) && swofty::mem::read(obj + offsetof(Named, name), text) &&
           swofty::mem::c_string(text, out, 64);
}
```

In a real addon, `Named` is a struct from the [Game API](/reference/game/): take field offsets from those structs rather than writing them by hand.
