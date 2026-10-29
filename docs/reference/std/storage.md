---
title: Storage
description: Read and write an addon's own files under swofty/addons/<id>/ with swofty/storage.hpp; the launcher keeps them between sessions.
---

# Storage

`swofty/storage.hpp` reads and writes your addon's own files under `swofty/addons/<id>/`, and the launcher keeps them between sessions.

```cpp [native/src/my_plugin.cpp]
#include <cstdlib>
#include <string>

#include <swofty/events.hpp>
#include <swofty/storage.hpp>

namespace st = swofty::storage;

int g_sessions = 0; // sessions this addon has seen, across game runs

void on_session(const SwoftyEvent *, void *)
{
    ++g_sessions;
    st::write("count.txt", std::to_string(g_sessions));                // replaces the file atomically
    st::append("sessions.csv", std::to_string(g_sessions) + ",start\n"); // creates the file when it is missing
}

SWOFTY_ON_LOAD
{
    if (!st::available())
        return;
    std::string text;
    st::Result r = st::read("count.txt", text); // paths are relative to swofty/addons/<id>/
    if (r == st::Result::ok)
        g_sessions = std::atoi(text.c_str());
    swofty::log("[my-addon] %d session(s) so far (count.txt: %s)", g_sessions, st::name(r));
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
  "operations": { "native": [{ "dll": "native/my_plugin.dll" }] }
}
```

```text [swofty/loader.log]
[storage] author.my-addon: files in swofty/addons/author.my-addon/
[storage] api: author.my-addon (my_plugin.dll) size count.txt -> invalid: swofty/addons/author.my-addon/count.txt is missing
[my-addon] 0 session(s) so far (count.txt: invalid)
[storage] api: author.my-addon (my_plugin.dll) write count.txt (1 byte(s)) -> ok
[storage] api: author.my-addon (my_plugin.dll) append sessions.csv (8 byte(s)) -> ok
```

While the game runs, the files are in `swofty/addons/<id>/` in the game folder, or in `%LOCALAPPDATA%\Swofty\addon-storage\<id>\` when [the game can't write in its folder](#when-the-game-can-t-write-in-its-folder). Between sessions they are in `$DATA/addon-storage/<id>/`.

- **Header:** `swofty/storage.hpp`, namespace `swofty::storage`
- **Table:** `SwoftyApi::storage` (`SwoftyStorage`), API version 5
- **Feature:** `storage`, stable
- **Thread:** the caller's thread; any thread works

Every call returns a `swofty::storage::Result`: `ok`, `late`, `unavailable` or `invalid` (see [result codes](./#result-codes)), and `name(r)` turns it into a string. On a loader older than version 5, or with a null table, the wrapper returns `unavailable`.

## Paths

A path is relative to your addon's folder. The core identifies the calling DLL the same way as the [asset API](/reference/addons/asset-api), so your addon reaches only its own folder.

```cpp
st::write("careers.json", text);          // swofty/addons/<id>/careers.json
st::write("careers/dc-0036.json", text);  // a subfolder; the core creates missing folders
st::write("C:/temp/x.json", text);        // invalid: absolute path
st::write("../other.addon/x.json", text); // invalid: climbs above the folder
```

The core rejects absolute paths, drive letters and any path that climbs above the folder, with `invalid`:

```text
[storage] api: author.my-addon (my_plugin.dll) write ../other.addon/x.json (12 byte(s)) -> invalid: '../other.addon/x.json' is not a relative path inside swofty/addons/author.my-addon/
```

A DLL that no addon in `settings.json` lists has no folder, and every call from it returns `invalid`:

```text
[storage] api: no addon (stray.dll) read x.json -> invalid: no addon in settings.json lists stray.dll, so it has no folder
```

## available

```cpp
if (!swofty::storage::available())
    return; // the feature is off, or this core doesn't implement it
```

`available()` returns `true` while the `storage` feature is on and the core implements it.

## read

```cpp
// Into a std::string: the wrapper asks for the size, then reads the whole file
std::string text;
if (st::read("data/years.json", text) != st::Result::ok)
    swofty::log("[my-addon] no data/years.json");     // out is untouched on an error

// Into your own buffer: len is set to the file size; invalid when it is larger than cap
char buf[4096];
size_t len = 0;
st::Result r = st::read("count.txt", buf, sizeof buf, &len);
```

A missing file returns `invalid`. A file your addon ships (a file in `files` on `operations.native`) reads the same way. Progressive Career reads its season data like this:

```cpp
// addons/progressive/native/src/ratings.cpp, trimmed
std::string text;
if (swofty::storage::read("data/years.json", text) != swofty::storage::Result::ok) {
    swofty::log("[progressive] ratings: data/years.json is missing; drivers keep the game's ratings");
    return;
}
```

## write

```cpp
st::write("careers.json", out);                                   // std::string_view
st::write("blob.bin", std::span<const std::byte>(bytes, n));      // raw bytes
```

`write` replaces the whole file. To keep writes atomic, the core writes a temp file, then renames it. Progressive Career keeps its career list this way and saves a copy of a file it can't parse:

```cpp
// addons/progressive/native/src/store.cpp, trimmed
swofty::storage::Result r = swofty::storage::write("careers.json", out);
if (r != swofty::storage::Result::ok)
    swofty::log("[progressive] store: writing %s failed (%s)", "careers.json", swofty::storage::name(r));
```

## append

```cpp
st::append("journal.jsonl", line + "\n"); // creates the file when it is missing
```

`append` takes a `std::string_view` or a `std::span<const std::byte>`, like `write`.

## remove and size

```cpp
st::remove("inbox/1792480486817.json");   // deletes the file; invalid when it is missing

uint64_t bytes = 0;
if (st::size("careers.json", &bytes) == st::Result::ok)
    swofty::log("[my-addon] careers.json is %llu bytes", (unsigned long long)bytes);
```

## list

```cpp
char buf[8192];
size_t len = 0;
if (st::list("inbox", buf, sizeof buf, &len) == st::Result::ok) {
    std::string_view names(buf, len);   // "a.json\nb.json\nold/\n": one name per line, folders end in '/'
}
// st::list("", ...) lists the addon's own folder
```

`list` writes the folder's names into `buf`, one per line, and sets `len` to the size of the listing. It returns `invalid` when the listing is larger than `cap`, or when the path is not a folder.

## What the core logs

The core logs each storage call with its addon, DLL, call and result. After 3 successful calls of the same call on the same path, it stops logging that one; failures are always logged. `SWOFTY_STORAGE_LOG=all` logs every call.

```text
[storage] api: swofty.career-editor (career_editor.dll) read careers/dc-00360000.season.json (139 byte(s)) -> ok
[storage] api: swofty.career-editor (career_editor.dll) list inbox -> ok; further successful calls like this are not logged (failures still are; SWOFTY_STORAGE_LOG=all logs every call)
[storage] api: swofty.career-editor (career_editor.dll) size careers/dc-00020001.season.json -> invalid: swofty/addons/swofty.career-editor/careers/dc-00020001.season.json is missing
```

## Between sessions

The launcher keeps what your addon writes. `$DATA` is the launcher's data folder (see [Where your files are stored](/guide/installation#where-your-files-are-stored)).

```text
game folder, during a session        swofty/addons/<id>/careers.json
launcher, between sessions           $DATA/addon-storage/<id>/careers.json
install log, at the next install     storage: put back 1 file(s) <id> wrote
```

- When a session's files are rolled back, every file in `swofty/addons/<id>/` that the install didn't place moves to `$DATA/addon-storage/<id>/`, keeping its path. The game folder is then back to stock.
- The next install of a profile with your addon moves them back into `swofty/addons/<id>/` before the game starts. The install log has a `storage: put back <n> file(s) <id> wrote` line.
- Switching to a profile without your addon, updating or reinstalling the addon, **Restore game folder** (`swofty restore`) and the one-time setup keep the files. A restore first moves the files in every addon folder that the launcher didn't place to `$DATA/addon-storage/<id>/`, then removes `swofty/`.
- A file your addon removed stays removed.
- A file at a path your addon ships (a file in `files` on `operations.native`) is replaced by the shipped one in the game folder. The written copy stays in `$DATA/addon-storage/<id>/`.
- When `swofty/addons/<id>/` and `$DATA/addon-storage/<id>/` both have a copy of a file at a rollback or at the next install, the copy modified last wins and the other is removed. A file the game wrote in `$DATA/addon-storage/<id>/` during the session is newer than the copy the install put back, so it stays. A file a session that ended without rolling back left in `swofty/addons/<id>/` is kept over an older stored copy.
- Files are kept per data folder. A launcher with another data folder (`SWOFTY_DATA_DIR`) doesn't see them.

In an [OverlayFS session](/reference/launch#overlayfs-sessions) on Linux, the folder is part of the game's mount, so what an addon stores persists in `$DATA/profiles/<profile>/writes/swofty/addons/<id>/`, separately for each profile, and stays after the game exits. Nothing is moved to `$DATA/addon-storage/<id>/`, because the game folder is never changed. Files stored in a copy-mode session aren't in the mount.

## When the game can't write in its folder

F1 25 can't create files in `swofty/addons/<id>/` when it runs without the rights to change its folder, such as the EA app copy in `C:\Program Files\EA Games\F1 25\` started from an EA app that isn't running as administrator. The core tests the folder at your addon's first storage call and keeps the answer for the session. When the game can create files there, the log has:

```text
[storage] <id>: files in swofty/addons/<id>/
```

When it can't, your addon's storage for the session is `%LOCALAPPDATA%\Swofty\addon-storage\<id>\`, the folder the launcher keeps the files in between sessions, and the log has:

```text
[storage] <id>: the game can't create files in swofty/addons/<id>/ (error 5), so this session its files are in C:\Users\<you>\AppData\Local\Swofty\addon-storage\<id>\, and a file not there is read from swofty/addons/<id>/
```

Without `LOCALAPPDATA` in the game's environment, writes fail:

```text
[storage] <id>: the game can't create files in swofty/addons/<id>/ (error 5) and LOCALAPPDATA is not set, so writes fail
```

Your addon makes the same calls either way. In that folder:

- `write` and `append` write there. `append` to a file that is only in `swofty/addons/<id>/` copies it there first.
- `read`, `size` and `list` look there first, then in `swofty/addons/<id>/`, so the files the install placed and the files the launcher put back still read. `list` names each file once.
- `remove` deletes the copy there. When `swofty/addons/<id>/` also has the file, the game can't delete it: `remove` returns `invalid` and leaves both copies.
- The core writes a `.swofty-data-folder` file there that says why. While it is there, the launcher API's [`addon_game_dir`](/reference/addons/launcher-api) names this folder, so your launcher page reads and writes the same files as your plugin. The next install removes it.
- `.swofty-data-folder` and `.swofty-write-test` at the top of either folder belong to the core: `list` doesn't name them, and calls with those paths return `invalid`.
- The folder is in `%LOCALAPPDATA%` of the Windows account the game runs as. A launcher with another data folder (`SWOFTY_DATA_DIR`), or run from another Windows account, doesn't take these files in.

## Design

[The native-libraries design](/design/native-libraries#_5-storage) has the design of this library.
