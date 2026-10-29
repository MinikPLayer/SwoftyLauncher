---
title: Events and effects
description: The events a launcher plugin's controls send, and the effects the plugin returns to tell the launcher what to do next.
---

# Events and effects

An `event` call carries one event from a control on the page, and the plugin returns one effect that tells the launcher what to do next.

## An event handler in full

The template's **Example** page handles a switch and a file picker:

```rust [src/main.rs]
fn event(&self, host: &mut Host, event: &Value) -> Result<Value, String> {
    // { "toggle": { "id": "okayama.exmod", "on": true } }
    if let Some(toggle) = event.get("toggle") {
        let name = toggle["id"].as_str().unwrap_or_default().to_owned();
        let mut on = self.enabled(host)?;
        on.retain(|n| *n != name);
        if toggle["on"].as_bool() == Some(true) {
            on.push(name);
        }
        host.call("set_state", json!({ "on": on }))?;
        return Ok(json!("reload"));                          // ask for every section's view again
    }
    // { "picked": { "id": "pick", "paths": ["/home/you/okayama.exmod"] } }
    if let Some(picked) = event.get("picked") {
        let paths: Vec<PathBuf> = serde_json::from_value(picked["paths"].clone()).unwrap_or_default();
        let lines = self.import(host, &paths)?;
        return Ok(json!({ "notice": lines.join("\n") }));    // a notification, then a reload
    }
    Ok(json!("none"))
}
```

```text
launcher  {"id":5,"method":"event","params":{"section":"items","event":{"picked":{"id":"pick","paths":["/home/you/okayama.exmod"]}}}}
plugin    {"method":"progress","params":{"done":0.0,"text":"Importing okayama.exmod"}}
plugin    {"id":5,"result":{"notice":"Imported okayama.exmod"}}
```

- `event` has 300 seconds; each message the plugin sends resets the wait.
- Until the effect comes back, the control that fired the event shows busy.
- An error reply shows as a notification and keeps the plugin running.

## Events

```json
{ "click": { "id": "remove" } }
{ "toggle": { "id": "okayama.exmod", "on": true } }
{ "choose": { "id": "livery", "index": 2 } }
{ "move": { "row": "okayama.exmod", "to": 0 } }
{ "picked": { "id": "pick", "paths": ["/home/you/okayama.exmod"] } }
{ "submit": { "form": "finance", "values": { "cash": 15000000 }, "changed": ["cash"] } }
{ "submit": { "form": "drivers", "row": "ver", "values": { "pace": 96 }, "changed": ["pace"] } }
{ "submit": { "form": "teams", "row": "ferrari", "values": { "colour": "#00A19C" }, "changed": ["colour"] } }
{ "search": { "id": "drivers", "query": "ver" } }
{ "tab": { "id": "page", "tab": "drivers" } }
{ "startup": {} }
```

- **`click`**: a [button](./views#buttons), a ghost row, or a row [menu](./views#rows) item.
- **`toggle`**: a switch.
- **`choose`**: a choice control.
- **`move`**: **Move up** or **Move down** in a `reorder` list.
- **`picked`**: the paths a `pick` button's file picker returned.
- **`submit`** *(1.0.10)*: a form's **Apply**, a live field, or an edited table cell ([Forms](./forms), [Tables](./tables)). A table cell adds `"row"`. A switch cell *(1.5.8)* sends `true` or `false`. A colour field or cell *(1.5.25)* sends `"#RRGGBB"` ([Colour fields](./forms#colour-fields)).
- **`search`** *(1.0.10)*: a search box that asks for events ([Search](./tables#search)).
- **`tab`** *(1.0.10)*: a tab click ([Tabs](./blocks#tabs)).
- **`startup`** *(1.3.0)*: once per launcher start, to the first section of an addon whose manifest sets `launcher.startup`.

### startup

```json [swofty.json]
"launcher": {
  "program": { "windows": "bin/migrate-helper.exe", "linux": "bin/migrate-helper" },
  "sections": [{ "id": "mods", "title": "Mods", "icon": "package" }],
  "startup": true
}
```

```json
{ "method": "event", "params": { "section": "mods", "event": { "startup": {} } } }
```

The event runs like any other: it has 300 seconds and can show a [task list](./task-lists), such as a first-run migration that converts mods.

## Effects

```json
"none"
"reload"
{ "notice": "Imported okayama.exmod" }
{ "set_addon_enabled": true }
{ "open": "/home/you/.local/share/swofty/addon-data/example.launcher-addon/items" }
{ "show_addon": "yourname.okayama" }
{ "show_addon": { "id": "yourname.okayama", "notice": "Imported okayama.exmod" } }
```

- **`none`**: does nothing.
- **`reload`**: asks every section for its view again.
- **`notice`**: shows a notification and reloads.
- **`set_addon_enabled`**: turns this addon on or off in the active profile.
- **`open`**: opens the file or folder with the system handler.
- **`show_addon`** *(1.3.0)*: switches to the **Addons** page and highlights that addon. With `{ id, notice }` it also shows the notice as a notification.

The template's dashboard opens its data folder from a header button:

```rust [src/main.rs]
match event["click"]["id"].as_str() {
    Some("open") => {
        std::fs::create_dir_all(&self.data_dir).map_err(|e| e.to_string())?;
        Ok(json!({ "open": self.data_dir }))
    }
    _ => Ok(json!("none")),
}
```
