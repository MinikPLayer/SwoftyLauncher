---
title: Launcher plugins
description: A launcher plugin is a helper program named in launcher.program, run as a subprocess that speaks newline-delimited JSON. It adds sidebar pages, an importer, a build hook, swofty commands and a profile summary. One full plugin, and a map of the protocol.
---

# Launcher plugins

A **launcher plugin** is a prebuilt program inside your addon that the launcher runs as a subprocess and talks to in newline-delimited JSON over stdin and stdout. It can add:

- pages in the launcher's sidebar;
- an importer for files dropped on the window;
- asset ops contributed when a profile builds;
- `swofty` subcommands;
- a line on the profile card.

Any language that reads and writes a line of JSON works. The examples use [`templates/launcher-addon`](./template#templates-launcher-addon), a Rust helper that depends only on `serde_json`.

## A plugin in full

This plugin adds one sidebar page with a switch per item, handles the switch's event, and keeps the switches in the profile with two host calls. It is `templates/launcher-addon` trimmed to those three parts.

```json [swofty.json]
{
  "schema_version": 1,
  "id": "example.launcher-addon",
  "name": "Example Launcher Addon",
  "version": "0.1.0",
  "author": "Your Name",
  "compat": { "game": "f1_25" },
  "launcher": {
    "program": { "windows": "bin/example-helper.exe", "linux": "bin/example-helper" },
    "sections": [{ "id": "items", "title": "Example", "icon": "package" }]
  }
}
```

```rust [src/main.rs]
use std::io::{BufRead, Lines, StdinLock, StdoutLock, Write};
use std::path::PathBuf;

use serde_json::{Value, json};

const API: u64 = 1;

struct Host {
    lines: Lines<StdinLock<'static>>,
    out: StdoutLock<'static>,
    next: u64, // ids of the plugin's own host calls
}

impl Host {
    fn send(&mut self, message: &Value) {
        // one JSON object on one line, flushed
        writeln!(self.out, "{message}").and_then(|()| self.out.flush()).ok();
    }

    // A host call: a request with an id; the launcher answers it before anything else.
    fn call(&mut self, method: &str, params: Value) -> Result<Value, String> {
        let id = self.next;
        self.next += 1;
        self.send(&json!({ "id": id, "method": method, "params": params }));
        let line = match self.lines.next() {
            Some(Ok(line)) => line,
            _ => std::process::exit(0), // stdin closed: the launcher is gone
        };
        let reply: Value = serde_json::from_str(&line).map_err(|e| e.to_string())?;
        match reply.get("error") {
            Some(e) => Err(e.to_string()),
            None => Ok(reply["result"].clone()),
        }
    }
}

struct Addon {
    data_dir: PathBuf, // from hello; shared by every profile
    vanilla: bool,     // the untouched profile refuses set_state
}

impl Addon {
    fn items(&self) -> Vec<String> {
        let mut names: Vec<String> = std::fs::read_dir(self.data_dir.join("items"))
            .into_iter()
            .flatten()
            .filter_map(|e| e.ok()?.file_name().into_string().ok())
            .collect();
        names.sort();
        names
    }

    // get_state: this addon's entry in the active profile's state, or null
    fn enabled(&self, host: &mut Host) -> Result<Vec<String>, String> {
        let state = host.call("get_state", Value::Null)?;
        Ok(serde_json::from_value(state["on"].clone()).unwrap_or_default())
    }

    // view: the page, rebuilt on every call
    fn view(&self, host: &mut Host) -> Result<Value, String> {
        let on = self.enabled(host)?;
        let rows: Vec<Value> = self.items().iter().map(|name| json!({
            "id": name,
            "title": name,
            "subtitle": "Example item",
            "trailing": { "switch": { "id": name, "on": on.contains(name), "enabled": !self.vanilla } }
        })).collect();
        Ok(json!({
            "subtitle": format!("{} items, {} on", rows.len(), on.len()),
            "body": { "list": { "rows": rows, "reorder": false } }
        }))
    }

    // event: a switch flipped; store the new set, then ask for the page again
    fn event(&self, host: &mut Host, event: &Value) -> Result<Value, String> {
        if let Some(toggle) = event.get("toggle") {
            let name = toggle["id"].as_str().unwrap_or_default().to_owned();
            let mut on = self.enabled(host)?;
            on.retain(|n| *n != name);
            if toggle["on"].as_bool() == Some(true) {
                on.push(name);
            }
            host.call("set_state", json!({ "on": on }))?; // replaces the whole state value
            return Ok(json!("reload"));
        }
        Ok(json!("none"))
    }
}

fn main() {
    let stdin = std::io::stdin();
    let mut host = Host { lines: stdin.lock().lines(), out: std::io::stdout().lock(), next: 0 };
    let mut addon = Addon { data_dir: PathBuf::new(), vanilla: false };
    while let Some(Ok(line)) = host.lines.next() {
        let Ok(message) = serde_json::from_str::<Value>(&line) else {
            eprintln!("not JSON: {line}"); // stderr goes to helper.log
            continue;
        };
        let params = &message["params"];
        let method = message["method"].as_str().unwrap_or_default();
        let Some(id) = message["id"].as_u64() else {
            // a notification: the active profile changed
            if method == "profile" {
                addon.vanilla = params["vanilla"].as_bool().unwrap_or(false);
            }
            continue;
        };
        let result = match method {
            "hello" => {
                addon.data_dir = PathBuf::from(params["data_dir"].as_str().unwrap_or_default());
                addon.vanilla = params["profile"]["vanilla"].as_bool().unwrap_or(false);
                Ok(json!({ "api": API })) // any other number stops the plugin
            }
            "view" => addon.view(&mut host),
            "event" => addon.event(&mut host, &params["event"]),
            other => Err(format!("unknown method {other}")),
        };
        let reply = match result {
            Ok(result) => json!({ "id": id, "result": result }),
            Err(error) => json!({ "id": id, "error": error }), // fails this call only
        };
        host.send(&reply);
    }
}
```

The session when the page opens and a switch is flipped:

```text
launcher  {"id":0,"method":"hello","params":{"api":1,"addon":"example.launcher-addon","data_dir":"/home/you/.local/share/swofty/addon-data/example.launcher-addon","cache_dir":"/home/you/.local/share/swofty/cache/addons/example.launcher-addon","profile":{"name":"default","vanilla":false,"enabled":true},"launcher":"1.5.19","host_calls":["addons","install_addon","set_addon_enabled","remove_addon","move_addon","tasks","profile_heads"]}}
plugin    {"id":0,"result":{"api":1}}
launcher  {"id":1,"method":"view","params":{"section":"items"}}
plugin    {"id":0,"method":"get_state","params":null}
launcher  {"id":0,"result":{"on":["okayama.exmod"]}}
plugin    {"id":1,"result":{"subtitle":"2 items, 1 on","body":{"list":{"rows":[…],"reorder":false}}}}
launcher  {"id":2,"method":"event","params":{"section":"items","event":{"toggle":{"id":"suzuka.exmod","on":true}}}}
plugin    {"id":1,"method":"get_state","params":null}
launcher  {"id":1,"result":{"on":["okayama.exmod"]}}
plugin    {"id":2,"method":"set_state","params":{"on":["okayama.exmod","suzuka.exmod"]}}
launcher  {"id":2,"result":null}
plugin    {"id":2,"result":"reload"}
launcher  {"id":3,"method":"view","params":{"section":"items"}}
```

The full template also has a dashboard page, an importer, a build hook, a command and a summary. Its **Overview** section uses every [dashboard block](./launcher-api/blocks), a [form](./launcher-api/forms), an editable [table](./launcher-api/tables), `tick` and `invalidate`.

## Build, install and open it

```sh
cp -r templates/launcher-addon my-addon        # 1. copy the template (or the smaller helper in Your first addon)
cd my-addon
cargo build --release                          # 2. build one binary per OS
mkdir -p bin
cp target/release/example-helper bin/          #    for Windows, build example-helper.exe and copy it to bin/
swofty addons install .                        # 3. install it
swofty addons enable example.launcher-addon
```

- Ship one binary per OS you support, at the path in `program`. If players use both platforms, ship both.
- Enable the addon in a modded profile: its **Example** section shows in the sidebar, under the addon's name. Opening the section starts the plugin and calls `view`.
- When something fails, read `$DATA/cache/addons/<id>/helper.log` ([Running and failures](./launcher-api/protocol#isolation-and-failures)).
- Ship the binaries in the `.swoftyaddon` ([Packaging and publishing](./publishing)).

The smaller helper is in [Your first addon](./first-addon).

## The launcher block

`launcher` in `swofty.json` names the program and lists what it answers. The launcher reads the block without starting the plugin.

```jsonc [swofty.json]
"launcher": {
  // a path inside the addon folder per OS; keys: linux, windows, macos (at least one)
  "program": { "windows": "bin/example-helper.exe", "linux": "bin/example-helper" },
  // one sidebar page each; the launcher lists them without starting the plugin
  "sections": [
    { "id": "overview", "title": "Overview", "icon": "activity" },
    { "id": "items", "title": "Example", "icon": "package" }
  ],
  "imports": ["exmod"],      // extensions claimed from drops, without the dot
  "claims": false,           // true: the program decides which drops it takes (Importers)
  "build": true,             // the plugin answers build
  "commands": [{ "name": "example", "about": "List the example items" }],
  "startup": false           // 1.3.0: send the first section a startup event once per launcher start
}
```

- A `sections/<id>.svg` in your addon replaces the page's icon in the sidebar and on rows without art. `icon` is one of the [fixed icons](./launcher-api/views#icons).
- Simplified for Swofty's sections are listed under **SIMPLIFIED FOR SWOFTY** in this order, its `mods` section as **Importer**. Every other addon's sections are under **ADDON PANELS**.
- The block takes no other keys: an unknown key fails the manifest.

## The map

- **[The protocol](./launcher-api/protocol)**: messages, the session, and how the launcher runs the plugin. {#the-protocol}
  - **Messages**: requests, replies and notifications, one per line.
  - **[Calls the launcher sends](./launcher-api/protocol#calls-the-launcher-sends)**: `hello`, `profile`, `view`, `event`, `import`, `build`, `command`, `summary`, `tick`.
  - **[Running the plugin](./launcher-api/protocol#how-the-launcher-runs-the-plugin)**: when it starts, its folder, its log.
  - **[Time limits](./launcher-api/protocol#time-limits)**: 5 seconds or 300 seconds.
  - **[Isolation and failures](./launcher-api/protocol#isolation-and-failures)**: the errors a failed plugin shows, and **Restart**.
  - **[The Rust side](./launcher-api/protocol#the-rust-side)**: the `Addon` trait and `run_helper`.
- **[Host calls](./launcher-api/host-calls)**: what the plugin asks the launcher while it handles a call. {#host-calls-the-plugin-sends-back}
  - **[Progress](./launcher-api/host-calls#progress)** and **[state](./launcher-api/host-calls#state)**: `progress`, `get_state`, `set_state`.
  - **[Game files](./launcher-api/host-calls#game-files)**: `game_build_key`, `game_exe_version`, `game_is_stock`, `game_original`, `game_original_lang`.
  - **[Folders](./launcher-api/host-calls#folders)**: `game_dir`, `addon_game_dir`, `steam_save_dir` (1.0.10).
  - **[Game status](./launcher-api/host-calls#game-status)**: `game_status` (1.0.10).
  - **[Profile tracks](./launcher-api/host-calls#profile-tracks)** (1.0.12) and **[profile heads](./launcher-api/host-calls#profile-heads)** (1.5.12). {#profile-heads}
  - **[Managing addons](./launcher-api/host-calls#managing-addons)**: `addons`, `install_addon`, `set_addon_enabled`, `remove_addon`, `move_addon` (1.3.0), and `AddonBuilder`.
- **[Views](./launcher-api/views)**: the page a section shows. {#views}
  - **Bodies**: `list`, `empty`, `blocks`.
  - **[Rows](./launcher-api/views#rows)**: art, tags, a trailing control, `detail` and a right-click `menu`.
  - **[Buttons](./launcher-api/views#buttons)**: styles, sizes and kinds (`confirm`, `pick`).
  - **[Tones](./launcher-api/views#tones)** and **[icons](./launcher-api/views#icons)**.
- **[Events and effects](./launcher-api/events)**: what a control sends, and what the plugin answers. {#events-and-effects}
- **[Blocks](./launcher-api/blocks)**: the content of a `blocks` body or a row's detail. {#dashboard-blocks}
  - **Basic blocks**: stats, facts, label, text, names, buttons.
  - **[Dashboard blocks](./launcher-api/blocks#dashboard-blocks)** (1.0.10): hero, pills, tiles, meters, card, grid.
  - **[Image](./launcher-api/blocks#image)** (1.0.12) and **[drop zone](./launcher-api/blocks#drop-zone)** (1.3.0).
  - **[Tabs](./launcher-api/blocks#tabs)** (1.0.10). {#tabs}
- **[Forms](./launcher-api/forms)** (1.0.10): number, slider, text, select, toggle and colour fields with an Apply button, or live. {#forms}
  - **[Colour fields](./launcher-api/forms#colour-fields)** (1.5.25): a colour dot that opens a picker. {#colour-fields}
- **[Tables](./launcher-api/tables)** (1.0.10): sortable columns, editable cells, colours, pictures, switches. {#tables}
  - **[Colour cells](./launcher-api/tables#colour-cells)** (1.5.25): a cell that opens the colour picker.
  - **[Row details](./launcher-api/tables#row-details)** (1.5.8): a panel beside the table. {#row-details}
  - **[Search](./launcher-api/tables#search)**: a search box over a table or a list. {#search}
- **[Live pages](./launcher-api/live-pages)** (1.0.10): `tick` from the launcher, `invalidate` from the plugin. {#live-pages}
- **[Task lists](./launcher-api/task-lists)** (1.3.0): a dialog that ticks off items while an event runs. {#task-lists}
- **[The build reply](./launcher-api/build-reply)**: asset ops returned at build, with `items` and `fields`. {#the-build-reply}
- **[Importers](./launcher-api/importers)**: claiming dropped files and folders. {#importers-and-drag-and-drop}
- **[Commands and the profile summary](./launcher-api/commands)**: `swofty` subcommands and a line on the profile card. {#commands}
- **[Changes at runtime](./runtime-assets#from-a-build-hook)**: how build-hook ops reach the game while it boots.

## Versions

Parts of the protocol need a newer launcher. `hello` carries the launcher's version from 1.0.10, and each page marks what needs which version. Set the lowest launcher you need in `swofty.json`:

```json
"compat": { "game": "f1_25", "launcher": ">=1.0.10" }
```

Or check `launcher` in `hello` and leave out what that launcher can't show. The template sends its colour field and colour column only to launcher 1.5.25 or later:

```rust [src/main.rs]
const COLOURS: [u64; 3] = [1, 5, 25];

fn at_least(version: &str, wanted: [u64; 3]) -> bool {
    let core = version.split(['-', '+']).next().unwrap_or_default(); // without a pre-release or build suffix
    let parts: Vec<u64> = core.split('.').map(|p| p.parse().unwrap_or(0)).collect();
    parts.len() == 3 && parts[..] >= wanted[..]
}

// in hello
addon.colours = at_least(params["launcher"].as_str().unwrap_or_default(), COLOURS);
```

An older launcher rejects a view that holds a block it doesn't know, and stops a plugin that sends a host call it doesn't know. Both mark the plugin failed ([Isolation and failures](./launcher-api/protocol#isolation-and-failures)).
