---
title: Storage
description: Read and write an addon's own files under swofty/addons/<id>/ with swofty/storage.hpp.
---

# Storage

Use storage to read and write your addon's own files under `swofty/addons/<id>/`. Give it paths relative to that folder: the core rejects absolute paths, drive letters and any path that climbs above it. Your addon can only reach its own folder because the core identifies the calling DLL the same way as the [asset API](/reference/addons/asset-api).

| | |
|---|---|
| Header | `swofty/storage.hpp` |
| Namespace | `swofty::storage` |
| Table | `SwoftyApi::storage` (`SwoftyStorage`) |
| Feature | `storage` — stable |
| API version | 5 |
| Thread | Caller thread (any thread) |

In an [OverlayFS session](/reference/launch#overlayfs-sessions) on Linux, the folder is part of the game's mount, so what an addon stores persists in `$DATA/profiles/<profile>/writes/swofty/addons/<id>/`, separately for each profile, and stays after the game exits.

Check the `swofty::storage::Result` each call returns: `ok`, `late`, `unavailable` or `invalid`. The [result codes](./#result-codes) explain what each means. To keep writes atomic, the core writes a temp file, then renames it.

## Calls

| Call | Does |
|---|---|
| `available()` | `true` when the feature is on and implemented |
| `read(path, buf, cap, &len)` | reads the whole file into `buf` and sets `len` to the file size; returns `invalid` if the size exceeds `cap` |
| `read(path, out)` | reads the whole file into a `std::string` |
| `write(path, data)` | replaces the file atomically (`std::string_view` or `std::span<const std::byte>`) |
| `append(path, data)` | appends to the file, creating it if it doesn't exist |
| `remove(path)` | deletes the file |
| `size(path, &out)` | writes the file size in bytes |
| `list(dir, buf, cap, &len)` | writes the directory's names into `buf`, separated by newlines; returns `invalid` if the listing exceeds `cap` |

## Example

Here's how you append a line to a log file, then read it back:

```cpp [native/src/my_plugin.cpp]
#include <swofty/storage.hpp>

namespace st = swofty::storage;

SWOFTY_ON_LOAD
{
    if (!st::available())
        return;

    st::append("laps.csv", "lap,time\n");

    std::string body;
    if (st::read("laps.csv", body) == st::Result::ok)
        swofty::log("[my-addon] laps.csv is %zu bytes", body.size());
}

SWOFTY_ADDON()
```

You'll find the file at `swofty/addons/<id>/laps.csv` in the game folder. Name your plugin in `operations.native`:

```json [swofty.json]
"operations": {
  "native": [{ "dll": "native/my_plugin.dll" }]
}
```

## Availability

The storage engine lane hasn't landed yet, so every call on current builds returns `unavailable`. Guard every call with `available()`. See [the native-libraries design](/design/native-libraries#_5-storage) for the design.
