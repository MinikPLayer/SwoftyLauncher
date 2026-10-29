---
title: Live pages
description: How a launcher plugin page follows something that changes on its own - tick requests from the launcher and invalidate notifications from the plugin.
---

# Live pages

*Launcher 1.0.10.* A live page follows something that changes on its own, such as a file the game writes: the launcher **ticks** it, and the plugin **invalidates** it.

## A live page in full

The template's **Overview** page asks for ticks, and invalidates itself when `dashboard.json` changes or the game starts or stops:

```rust [src/main.rs]
fn overview(&mut self, host: &mut Host) -> Result<Value, String> {
    let d = self.dashboard();
    self.seen = self.modified();        // dashboard.json's modified time when this view was built
    let status = host.call("game_status", Value::Null).unwrap_or(Value::Null);
    self.running = status["running"] == true;
    // …
    Ok(json!({
        "subtitle": "Every launcher block, live from dashboard.json",
        "tick": true,                   // a tick about once a second while the page is open
        "body": { "blocks": blocks }
    }))
}

fn tick(&mut self, host: &mut Host, section: &str) -> Result<Value, String> {
    if section == "overview" {
        let running = host.call("game_status", Value::Null)?["running"] == true;
        if self.modified() != self.seen || running != self.running {
            host.invalidate(section);   // the launcher asks for this section's view again
        }
    }
    Ok(Value::Null)
}
```

```rust [src/main.rs]
impl Host {
    fn invalidate(&mut self, section: &str) {
        self.send(&json!({ "method": "invalidate", "params": { "section": section } }));
    }
}
```

```text
launcher  {"id":12,"method":"tick","params":{"section":"overview"}}
plugin    {"id":5,"method":"game_status","params":null}
launcher  {"id":5,"result":{"running":true,"pid":48211,"since":1760941234.5,"session":"2026-10-20T14-22-33"}}
plugin    {"method":"invalidate","params":{"section":"overview"}}
plugin    {"id":12,"result":null}
launcher  {"id":13,"method":"view","params":{"section":"overview"}}
```

## tick

Return `"tick": true` in a view and, while that page is open, the launcher sends a `tick` request about once a second. Reply `null`; the launcher ignores the result.

- A tick has the 5-second budget of `view`. Keep it cheap: check a file's modified time and return.
- A tick is skipped, not queued, while another call runs or the page isn't on screen.
- A tick never starts the plugin.
- A tick may make host calls, such as `game_status`, like any other call.
- A failed tick shows nothing, and the launcher goes on ticking.

## invalidate

A notification that tells the launcher a section's view is out of date:

```json
{ "method": "invalidate", "params": { "section": "overview" } }
{ "method": "invalidate", "params": {} }
```

- Send it whenever you know: during a call (in reply to a tick, or after an event) or on your own between calls, such as from a thread that watches a file. Write each message as one whole line.
- With `section`, the launcher asks for that section's view again; without one, for every section of your plugin.
- It asks only for sections that are open or that it has already loaded, and only once for several invalidations that arrive together. While another call runs, it asks once that call ends.
- An `invalidate` has no `id` and gets no reply. It doesn't reset a call's time limit.

Career Editor sends it with or without a section:

```rust [addons/career-editor/launcher/src/host.rs]
pub fn invalidate(&mut self, section: Option<&str>) {
    let params = match section {
        Some(s) => json!({ "section": s }),
        None => json!({}),
    };
    self.send(&json!({ "method": "invalidate", "params": params }));
}
```

A new view updates every [form](./forms) field you haven't edited and leaves edited fields alone.
