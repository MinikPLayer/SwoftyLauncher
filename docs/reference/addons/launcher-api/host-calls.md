---
title: Host calls
description: The calls a launcher plugin sends back to the launcher while it handles a request - progress, state, the game's clean files, folders, the running game, the profile's tracks and heads, and managing other addons.
---

# Host calls

A host call is a request the plugin sends to the launcher while it handles one of the launcher's calls; the launcher answers it before anything else.

## Host calls in full

The template's **Overview** page reads the running game, two folders and its own state before it draws the page:

```rust [src/main.rs]
fn overview(&mut self, host: &mut Host) -> Result<Value, String> {
    // { running, pid, since, session }
    let status = host.call("game_status", Value::Null).unwrap_or(Value::Null);
    self.running = status["running"] == true;
    // a path, or null
    let addon_dir = host.path("addon_game_dir");
    let saves = host.path("steam_save_dir");
    // this addon's state in the active profile, or null
    let on = self.enabled(host)?;
    let live = match (self.running, status["session"].as_str()) {
        (true, Some(_)) => json!({ "text": "LIVE", "tone": "good", "dot": true }),
        (true, None) => json!({ "text": "Running without Swofty", "tone": "warn", "dot": true }),
        (false, _) => json!({ "text": "Offline", "tone": "muted", "dot": true }),
    };
    // …the hero, tiles and table, with these values in them
}
```

```rust
impl Host {
    // A host call whose result is a path or null.
    fn path(&mut self, method: &str) -> Option<String> {
        self.call(method, Value::Null).ok()?.as_str().map(str::to_owned)
    }
}
```

On the wire:

```json
{ "id": 3, "method": "game_status", "params": null }
{ "id": 3, "result": { "running": true, "pid": 48211, "since": 1760941234.5, "session": "2026-10-20T14-22-33" } }
{ "id": 4, "method": "addon_game_dir", "params": null }
{ "id": 4, "result": "/home/you/.steam/steam/steamapps/common/F1 25/swofty/addons/example.launcher-addon" }
```

## Every host call

```text
method                   params           result
progress                 { done, text }   (a notification: no id, no reply)                     Progress
tasks             1.3.0  { title, … }     (a notification)                                      Task lists
invalidate        1.0.10 { section }      (a notification)                                      Live pages
get_state                                 the addon's per-profile state, or null                State
set_state                the new value    null, or an error on the vanilla profile              State
game_build_key                            the game build's key, or null                         Game files
game_exe_version                          the F1_25.exe version, or null
game_is_stock            { rel }          true or false
game_original            { rel }          a path to a clean copy of the file
game_original_lang       { code }         a path to the stock language_<code>.lng
game_dir          1.0.10                  the game folder, or null                              Folders
addon_game_dir    1.0.10                  the addon's folder in the game, as the game sees it, or null
registry_dir      1.6.0                   the folder of the core's reports: effective.json, conflicts.json
steam_save_dir    1.0.10                  the Steam Cloud save folder, or null
game_status       1.0.10                  { running, pid, since, session }                      Game status
profile_tracks    1.0.12                  the tracks in the profile's last build, or null       Profile tracks
profile_heads     1.5.12                  the driver heads the profile's addons put, or null    Profile heads
addons                                    the installed addons                                  Managing addons
install_addon            { path }         the addon it installed
set_addon_enabled        { id, on }       null, or an error on the vanilla profile
remove_addon             { id }           null
move_addon        1.3.0  { id, delta }    null, or an error on the vanilla profile
```

Send a call with an `id` when you expect a reply. `progress`, `tasks` and `invalidate` go without one; `progress` with an `id` gets the error reply `progress takes no id`.

## Progress {#progress}

```json
{ "method": "progress", "params": { "done": 0.5, "text": "Importing okayama.exmod" } }
```

`progress` fills a progress card while an `import` runs, with `done` from `0.0` to `1.0`:

```rust [src/main.rs]
for (i, path) in paths.iter().enumerate() {
    host.progress(i as f32 / paths.len() as f32, &format!("Importing {name}"));
    std::fs::copy(path, self.items_dir().join(name)).map_err(|e| format!("{name}: {e}"))?;
}
```

## State {#state}

```json
{ "id": 2, "method": "get_state" }
{ "id": 3, "method": "set_state", "params": { "on": ["okayama.exmod"] } }
```

```rust [src/main.rs]
fn enabled(&self, host: &mut Host) -> Result<Vec<String>, String> {
    let state = host.call("get_state", Value::Null)?;
    Ok(serde_json::from_value(state["on"].clone()).unwrap_or_default())
}
```

- `get_state` reads your addon's entry in the active profile's `state`. It is `null` when the profile has none.
- `set_state` replaces the whole value. The launcher never reads its contents.
- `set_state` returns an error on the vanilla profile.
- Duplicating or renaming a profile copies its state.

## Game files {#game-files}

The game's own clean files, rather than the modded folder:

```json
{ "id": 4, "method": "game_build_key" }
{ "id": 4, "result": "25251167-d873a1c8d204" }
{ "id": 5, "method": "game_exe_version" }
{ "id": 5, "result": "1.0.141.7865" }
{ "id": 6, "method": "game_is_stock", "params": { "rel": "ui/tool_tips.erp" } }
{ "id": 7, "method": "game_original", "params": { "rel": "ui/tool_tips.erp" } }
{ "id": 8, "method": "game_original_lang", "params": { "code": "en" } }
```

When no game is found:

```text
game_build_key, game_exe_version    null
game_is_stock                       false
game_original, game_original_lang   error: "the game is not available"
```

## Folders {#folders}

*Launcher 1.0.10.*

```json
{ "id": 9, "method": "game_dir" }
{ "id": 10, "method": "addon_game_dir" }
{ "id": 11, "method": "steam_save_dir" }
```

Career Editor finds its mailbox in the running game copy this way:

```rust [addons/career-editor/launcher/src/main.rs]
fn locate(&mut self, host: &mut Host) {
    if let Ok(v) = host.call("addon_game_dir", Value::Null)
        && let Some(p) = v.as_str()
    {
        self.store.set_mailbox(PathBuf::from(p));
    }
}
```

**`game_dir`** is the folder the launcher plays: the [active copy](/using/game-copies), Steam's or the EA app's or an added folder. It is `null` when no game is found.

<a id="addon-game-dir"></a>**`addon_game_dir`** is your addon's own folder inside the game, `<game>/swofty/addons/<id>/`: the one a native plugin reads and writes with the [storage library](../native-plugins).

- When the game can't create files in that folder, the core keeps your addon's files in `$DATA/addon-storage/<id>/` for the session and writes a `.swofty-data-folder` file there. `addon_game_dir` then names that folder until the next install, which removes the file (see [Storage](/reference/std/storage#when-the-game-can-t-write-in-its-folder)).
- It is `null` when no game is found.
- The launcher doesn't create the folder. It may not exist until a session has run the addon: create it yourself before you write to it.
- Restoring the game folder removes it. Keep anything that must outlast a session in `data_dir`.
- **Ask again every time you use it.** Don't cache the path, store it or show it to the player as the game folder.
- In an [OverlayFS session](/reference/launch#overlayfs-sessions) on Linux, the game folder isn't changed, and the answer depends on the session. While the game runs, it is a path under `/proc/<pid>/root/…` that goes through the game's mount, so reads, writes, renames and deletes behave as if the game made them. When no game runs, it is the profile's upper dir, `$DATA/profiles/<profile>/writes/swofty/addons/<id>/`, where an addon's last files stay and a batch you queue is picked up by the next session. It is never the real game folder. When a game runs in a session the launcher can't reach, the call returns an error saying so.
- `game_dir` is always the real game folder, in every mode. The loader's own files (`swofty/addons/core`) aren't in it in an OverlayFS session; read them from the folder next to your addon's, `addon_game_dir/../core`.

**`steam_save_dir`** is the game's Steam Cloud save folder for the most recent Steam user, `<steam>/userdata/<steamid3>/3059520/remote/`.

- The user comes from Steam's `loginusers.vdf`, else the user folder changed last.
- It is `null` when that folder doesn't exist.
- The saves are encrypted per Steam account, so a backup restores only on the same account.

## Game status {#game-status}

*Launcher 1.0.10.*

```json
{ "running": true, "pid": 48211, "since": 1760941234.5, "session": "2026-10-20T14-22-33" }
```

- `running` is `true` while `F1_25.exe` runs. The launcher's Play button uses the same check.
- `pid` is the game's process id, or `null` when it isn't running.
- `since` is when the process started, in Unix seconds, or `null` when it isn't running or the OS doesn't say (Windows).
- `session` is the Swofty session that started this run: its folder name under `$DATA/sessions/`. A session exists when the game started through Swofty, from **Play** or from Steam with the `swofty run` wrapper. It is `null` when the game isn't running or started without Swofty.

## Profile tracks {#profile-tracks}

*Launcher 1.0.12.* Career Editor lists every track a profile can race:

```rust [addons/career-editor/launcher/src/tracks.rs]
match host.call("profile_tracks", Value::Null) {
    Ok(Value::Null) => {
        Err("The launcher hasn't found F1 25, so it can't list the tracks in this profile.".into())
    }
    Ok(v) => Ok(v),
    Err(e) => Err(format!("The launcher couldn't list this profile's tracks: {e}")),
}
```

```json
{ "built": true, "buildhash": "5d5224af722f2bfc", "tracks": [
  { "code": "suzuka", "stat_id": 13, "events": ["rw_suzuka", "rw_suzuka_26"], "playable": true, "modded": false, "name": null, "addon": null },
  { "code": "okayama", "stat_id": 43, "events": [], "playable": true, "modded": true, "name": "Okayama", "addon": "teamsimplified.mods" }
] }
```

`profile_tracks` reads the game database in the active profile's last build, or the game's own database before the profile's first build. It is `null` when no game is found.

- `built` is `false` before the profile's first build: the tracks are the stock ones.
- `buildhash` is the build the tracks come from, or `null`.
- `code` is the `tracks.Track` row's label, and `stat_id` its `statId`, the track id the game stores.
- `events` are the career events (`schedule.all_track_events`) whose race-weekend activity names this track.
- `playable` is `true` when the `2025` or `2026` content set lists the track for Grand Prix.
- `modded` is `true` when a track package in this build added or filled the row.
- `name` and `addon` are the package's `track_name` and the addon that added it, for a modded track.

## Profile heads {#profile-heads}

*Launcher 1.5.12.* Send it only when `hello.params.host_calls` lists `profile_heads`, as Driver Manager does:

```rust [addons/driver-manager/launcher/src/app.rs]
fn profile_heads(&self, host: &mut Host) -> Vec<Choice> {
    if !self.host_calls.iter().any(|c| c == PROFILE_HEADS) {
        return Vec::new();
    }
    let Ok(reply) = host.call(PROFILE_HEADS, Value::Null) else {
        return Vec::new();
    };
    heads_from(&reply)
}
```

```json
{ "heads": [
  { "label": "rio_haryanto", "addon": "mod.rio-haryanto-facemod", "addon_name": "Rio_Haryanto_facemod", "female": false }
] }
```

`profile_heads` lists the driver heads that the addons on in the active profile put with `file.put`, at `2025_asset_groups/animation_package/scenes/cinematics/drivers/heads/<label>.erp`, that drivers can wear: a head with a row in the game or an addon's `add-row`, or one whose archive holds a head model, which the build adds a row for ([Driver heads](../runtime-assets#driver-heads)).

- It reads the addons' op files, so it needs no build.
- It is `null` when no game is found. The launcher lists it in `host_calls` only when it has found a game.
- `label` is the value a driver's `head` takes.
- `addon` and `addon_name` are the addon that puts the head archive, and its name.
- `female` is `true` for a female head.

## Managing addons {#managing-addons}

The addon calls let a plugin install, list, turn on and off, reorder and remove other addons, such as a page that gathers the data addons that add a track. Send them only when `hello.params.host_calls` lists them: a launcher that doesn't know a host call stops the plugin.

```json
{ "id": 12, "method": "addons" }
{ "id": 13, "method": "install_addon", "params": { "path": "/home/you/.local/share/swofty/cache/addons/you.importer/okayama" } }
{ "id": 14, "method": "set_addon_enabled", "params": { "id": "you.okayama", "on": true } }
{ "id": 15, "method": "move_addon", "params": { "id": "you.okayama", "delta": -3 } }
{ "id": 16, "method": "remove_addon", "params": { "id": "you.okayama" } }
```

A Rust helper on [`run_helper`](./protocol#the-rust-side) gets them from `Cx::addons()`, which is `None` on a launcher that doesn't list them. Its `move_addon` returns `this launcher can't reorder addons; it needs 1.3.0` when the launcher doesn't list `move_addon`.

### addons

```json
[ { "id": "yourname.okayama-named", "name": "Okayama with its own name", "version": "1.0.0", "author": "Your Name",
    "dir": "/home/you/.local/share/swofty/addons/yourname.okayama-named", "enabled": true, "position": 3 } ]
```

- Every installed addon, with its folder and whether it is on in the active profile.
- *(1.3.0)* The list is in the active profile's load order: the addons that are on, in the order a build loads them, then the rest by id.
- *(1.3.0)* `position` is the addon's index in the active profile's addon list, from `0`, or `null` when the profile doesn't list it. An addon that is off keeps its place in that list. Launchers before 1.3.0 list the addons by id and send no `position`, which `AddonInfo::position` reads as `None`.

### install_addon

- Installs the addon at `path` as a drop on the window does: a `.swoftyaddon`, a zip that holds an addon, or a folder with `swofty.json`.
- Replaces an installed copy with the same id, and returns the addon in the shape above.
- Doesn't turn the addon on.
- *(1.3.0)* Refuses an addon with a `dbedit.` id unless the folder or zip is a DB Editor addon, which installs as a new DB Editor addon.

### set_addon_enabled

- Turns an addon on or off in the active profile. Returns an error on the vanilla profile.
- Refuses to turn off an addon that is always on, or one that [keeps data in the saves](../manifest#saves): only the **Addons** page warns about that.

### remove_addon

- Deletes the addon and takes it out of every profile, as **Remove** on the **Addons** page does.
- A plugin can't remove its own addon (`<id> can't remove itself`), a built-in one, one the launcher manages, or one that keeps data in the saves.

### move_addon

*Launcher 1.3.0.*

- Moves an addon `delta` places in the active profile's addon list: negative moves it up, positive down.
- The list holds the addons that are on and the ones turned off, and `delta` counts both.
- The addons that are on load in that list's order, and a later addon wins where two write the same thing.
- The move stops at either end of the list, and `swofty.core` stays first.
- The addon must be in the profile's list, which `set_addon_enabled` puts it in. Returns an error on the vanilla profile.

### Writing an addon to install

*1.3.0.* A Rust helper that turns files into addons can write the addon folder with `swofty_addon::builder::AddonBuilder`, then pass it to `install_addon`:

```rust [src/import.rs]
let mut b = AddonBuilder::new("you.okayama", "Okayama", "You");
b.category = Some(Category::Tracks);
b.origin = Some(Origin { tool: "you.importer".into(), tool_version: None, file: Some("okayama.zip".into()), sha256: None });
b.text("*", "lng_okayama", "Okayama").track(&unpacked, "okayama");
let out = cx.cache_dir.join("okayama");
let _ = std::fs::remove_dir_all(&out); // write refuses a folder that isn't empty
b.write(&out)?;
// then install_addon with { "path": out }
```

- `db`, `db_ops`, `erp`, `text`, `lang`, `file` and `track` add operations. `file` and `track` copy their source into the addon; `source` copies a file or folder an ERP or language op names.
- `write` writes `swofty.json`, `database.json`, `erp.json`, `text.json` and `tracks.json` as needed, and sets `compat.launcher` to `>=1.3.0`, or to the crate's own version when it is older.
- `write` sets `kind` to each of `database`, `erp`, `text` and `files` the addon has ops for.
- `write` refuses an invalid, `swofty.` or `dbedit.` id, a path that leaves the addon or the game folder, a file put twice, an op source that wasn't added, and a source outside the addon folder.
- `write` refuses programs (`.dll`, `.exe`, `.asi`, `.so`, `.dylib`, `.bat`) as a source file, a path inside the addon or a game path.
- `write` refuses a symbolic link or junction anywhere in a source.
- `write` refuses an output folder that isn't empty.
