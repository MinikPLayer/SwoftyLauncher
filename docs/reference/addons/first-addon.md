---
title: Your first addon
description: Build an addon with one setting and a launcher plugin that adds a sidebar page reporting the installed game, then install it and open the page. Every file and command first, then what each part does.
---

# Your first addon

This addon has one setting and a sidebar page that reports the installed game. The page comes from a **launcher plugin**: a helper program the launcher runs. The helper here is Rust, so you need `cargo` (the Rust toolchain); the protocol is plain JSON, so a plugin can be written in any language.

## The files

```text
my-first-addon/
├── swofty.json
├── Cargo.toml
└── src/
    └── main.rs
```

```jsonc [swofty.json]
{
  "schema_version": 1,
  "id": "yourname.my-first-addon",       // at least two dot-separated parts
  "name": "My First Addon",
  "version": "0.1.0",
  "author": "Your Name",
  "description": "A launcher page that reports the installed game.",
  "compat": { "game": "f1_25" },
  "options": [
    {
      "id": "units",
      "type": "choice",
      "label": "Lap display",
      "description": "How the plugin labels lap times.",
      "values": ["seconds", { "value": "mmss", "label": "mm:ss.mmm" }],   // a value, or a value with its own label
      "default": "seconds",
      "binds": "native"                   // an engine plugin reads it live
    }
  ],
  "launcher": {
    "program": { "linux": "bin/my-first-addon", "windows": "bin/my-first-addon.exe" },   // the helper for each OS
    "sections": [{ "id": "game", "title": "Game", "icon": "gamepad" }]                   // one sidebar page
  }
}
```

The comments are for this page; `swofty.json` is plain JSON.

```toml [Cargo.toml]
[package]
name = "my-first-addon"
version = "0.1.0"
edition = "2024"

[dependencies]
serde_json = "1"
```

```rust [src/main.rs]
use std::io::{BufRead, Lines, StdinLock, StdoutLock, Write};

use serde_json::{Value, json};

const API: u64 = 1;

struct Launcher {
    lines: Lines<StdinLock<'static>>,
    out: StdoutLock<'static>,
    next: u64,
}

impl Launcher {
    // one JSON message per line
    fn send(&mut self, message: &Value) {
        writeln!(self.out, "{message}").and_then(|()| self.out.flush()).ok();
    }

    // a host call: ask the launcher, read its reply from the next line
    fn call(&mut self, method: &str) -> Value {
        let id = self.next;
        self.next += 1;
        self.send(&json!({ "id": id, "method": method }));
        match self.lines.next() {
            Some(Ok(line)) => serde_json::from_str::<Value>(&line)
                .map(|reply| reply["result"].clone())
                .unwrap_or(Value::Null),
            _ => std::process::exit(0),
        }
    }

    // the page: two host calls, then a view built from their answers
    fn view(&mut self) -> Value {
        let build = self.call("game_build_key");
        let exe = self.call("game_exe_version");
        let text = |v: &Value| v.as_str().unwrap_or("not found").to_owned();   // null when no game was located
        json!({
            "subtitle": "The game this profile builds against",
            "body": { "blocks": [
                { "label": "INSTALLED GAME" },
                { "stats": [
                    { "label": "Build", "value": text(&build) },
                    { "label": "F1_25.exe", "value": text(&exe) }
                ] }
            ] }
        })
    }
}

fn main() {
    let stdin = std::io::stdin();
    let mut host = Launcher {
        lines: stdin.lock().lines(),
        out: std::io::stdout().lock(),
        next: 0,
    };
    while let Some(Ok(line)) = host.lines.next() {
        let Ok(message) = serde_json::from_str::<Value>(&line) else { continue };
        let Some(id) = message["id"].as_u64() else { continue };
        let result = match message["method"].as_str().unwrap_or_default() {
            "hello" => json!({ "api": API }),
            "view" => host.view(),
            other => {
                host.send(&json!({ "id": id, "error": format!("unknown method {other}") }));
                continue;
            }
        };
        host.send(&json!({ "id": id, "result": result }));
    }
}
```

## Build, install and open it

```sh
cargo build --release
mkdir -p bin
cp target/release/my-first-addon bin/            # the path program names for linux
swofty addons install .                          # copies the folder to $DATA/addons/yourname.my-first-addon/
swofty addons enable yourname.my-first-addon     # into the active profile
```

On Windows, build `my-first-addon.exe` and copy it to `bin/`.

Open **Game** under **ADDON PANELS** in the launcher's sidebar. The launcher starts your helper and calls `view`:

```text
INSTALLED GAME
Build       25251167-d873a1c8d204
F1_25.exe   1.0.141.7865
```

`not found` in place of a value means no game was located.

To see the setting in game, run `swofty run --apply`, open **Career → ADDON SETTINGS** and find the **Lap display** choice on the **My First Addon** tile.

## The manifest

- **`id`**: at least two dot-separated parts, using lowercase letters, digits, `.`, `_` and `-`. The `swofty.` prefix is reserved.
- **`options`**: one choice, stored per profile. It shows on your addon's row on the **Addons** page and on its tile in **ADDON SETTINGS** in game. With `binds: native`, an [engine plugin](./native-plugins#settings) reads the setting live; [Settings](./settings) shows how to read it.
- **`launcher.program`**: the helper the launcher runs, for each OS.
- **`launcher.sections`**: the sidebar pages the helper draws. This one adds **Game**.

[The manifest reference](./manifest) lists every field.

## The helper

The launcher writes one JSON request per line to the helper's stdin and reads one reply per line from its stdout:

```text
launcher  {"id":0,"method":"hello","params":{"api":1,"addon":"yourname.my-first-addon",…}}
helper    {"id":0,"result":{"api":1}}
launcher  {"id":1,"method":"view","params":{"section":"game"}}
helper    {"id":0,"method":"game_build_key"}
launcher  {"id":0,"result":"25251167-d873a1c8d204"}
helper    {"id":1,"method":"game_exe_version"}
launcher  {"id":1,"result":"1.0.141.7865"}
helper    {"id":1,"result":{"subtitle":"The game this profile builds against","body":{…}}}
```

- **`hello`** answers with the protocol version the helper speaks.
- **`view`** answers with the page. Before it replies, the helper asks the launcher questions: the `game_build_key` and `game_exe_version` [host calls](./launcher-api#host-calls-the-plugin-sends-back).
- Any other method gets an `error` reply.

## When the helper fails

If your helper crashes or stalls, the launcher stays running. The page shows the error and a **Restart** button:

```text
stopped (exit status: 3)                                  [Restart]
My First Addon is not running
Restart it to load this page.
```

The helper's stderr goes to `$DATA/cache/addons/yourname.my-first-addon/helper.log`.

## Install again after a change

Use a modded profile to enable your addon; the `vanilla` profile takes no addons. Installing again replaces the copy in `$DATA/addons/yourname.my-first-addon/`, so rebuild the helper and run `swofty addons install .` after each change.

## Package it

Zip the folder's contents as a `.swoftyaddon` file, with `swofty.json` at the zip root:

```sh
cargo build --release && cp target/release/my-first-addon bin/
zip -r ../yourname.my-first-addon.swoftyaddon . -x 'target/*'
```

Ship the binaries in `bin/` and leave out `target/`. Include your own files, never copies of the game's archives, database or executable. [Packaging and publishing](./publishing) has the full checklist.

## Next

- [Launcher plugins](./launcher-api): add an importer, a build hook, commands and a profile summary, with the full protocol.
- [Settings](./settings): read the `units` setting in an engine plugin or a launcher plugin.
- [Templates](./template): start your next addon from a template.
- [Addon manifest](./manifest): every `swofty.json` field.
