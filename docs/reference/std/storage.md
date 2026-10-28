---
title: Storage
description: Read and write an addon's own files under swofty/addons/<id>/ with swofty/storage.hpp; the launcher keeps them between sessions.
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

While the game runs, the file is at `swofty/addons/<id>/laps.csv` in the game folder, or at `%LOCALAPPDATA%\Swofty\addon-storage\<id>\laps.csv` when [the game can't write in its folder](#when-the-game-can-t-write-in-its-folder). Between sessions it is at `$DATA/addon-storage/<id>/laps.csv`. Name your plugin in `operations.native`:

```json [swofty.json]
"operations": {
  "native": [{ "dll": "native/my_plugin.dll" }]
}
```

## Between sessions

The launcher keeps what your addon writes. `$DATA` is the launcher's data folder (see [Where your files are stored](/guide/installation#where-your-files-are-stored)).

- When a session's files are rolled back, every file in `swofty/addons/<id>/` that the install didn't place moves to `$DATA/addon-storage/<id>/`, keeping its path. The game folder is then back to stock.
- The next install of a profile with your addon moves them back into `swofty/addons/<id>/` before the game starts. The install log has a `storage: put back <n> file(s) <id> wrote` line.
- Switching to a profile without your addon, updating or reinstalling the addon, **Restore game folder** (`swofty restore`) and the one-time setup keep the files. A restore first moves the files in every addon folder that the launcher didn't place to `$DATA/addon-storage/<id>/`, then removes `swofty/`.
- A file your addon removed stays removed.
- A file at a path your addon ships (a file in `files` on `operations.native`) is replaced by the shipped one in the game folder. The written copy stays in `$DATA/addon-storage/<id>/`.
- When `swofty/addons/<id>/` and `$DATA/addon-storage/<id>/` both have a copy of a file at a rollback or at the next install, the copy modified last wins and the other is removed. A file the game wrote in `$DATA/addon-storage/<id>/` during the session is newer than the copy the install put back, so it stays. A file a session that ended without rolling back left in `swofty/addons/<id>/` is kept over an older stored copy.
- Files are kept per data folder. A launcher with another data folder (`SWOFTY_DATA_DIR`) doesn't see them.

## When the game can't write in its folder

F1 25 can't create files in `swofty/addons/<id>/` when it runs without the rights to change its folder, such as the EA app copy in `C:\Program Files\EA Games\F1 25\` started from an EA app that isn't running as administrator. The core tests the folder at your addon's first storage call and keeps the answer for the session. When the game can create files there, the loader's log has:

```
[storage] <id>: files in swofty/addons/<id>/
```

When it can't, your addon's storage for the session is `%LOCALAPPDATA%\Swofty\addon-storage\<id>\`, the folder the launcher keeps the files in between sessions, and the log has:

```
[storage] <id>: the game can't create files in swofty/addons/<id>/ (error 5), so this session its files are in C:\Users\<you>\AppData\Local\Swofty\addon-storage\<id>\, and a file not there is read from swofty/addons/<id>/
```

Your addon makes the same calls either way. In that folder:

- `write` and `append` write there. `append` to a file that is only in `swofty/addons/<id>/` copies it there first.
- `read`, `size` and `list` look there first, then in `swofty/addons/<id>/`, so the files the install placed and the files the launcher put back still read. `list` names each file once.
- `remove` deletes the copy there. When `swofty/addons/<id>/` also has the file, the game can't delete it: `remove` returns `invalid` and leaves both copies.
- The core writes a `.swofty-data-folder` file there that says why. While it is there, the launcher API's [`addon_game_dir`](/reference/addons/launcher-api) names this folder, so your launcher page reads and writes the same files as your plugin. The next install removes it.
- `.swofty-data-folder` and `.swofty-write-test` at the top of either folder belong to the core: `list` doesn't name them, and calls with those paths return `invalid`.
- The folder is in `%LOCALAPPDATA%` of the Windows account the game runs as. A launcher with another data folder (`SWOFTY_DATA_DIR`), or run from another Windows account, doesn't take these files in.

## Availability

`available()` returns `true` while the `storage` feature is on. See [the native-libraries design](/design/native-libraries#_5-storage) for the design.
