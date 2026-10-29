---
title: Task lists
description: The tasks notification of a launcher plugin - a dialog that ticks off a list of items while an event runs.
---

# Task lists

*Launcher 1.3.0.* While an `event` runs, a plugin can show a dialog that ticks off a list of items, such as the mods a first-run migration converts.

## A task list in full

A migration that converts each mod and sends the whole list after every step:

```rust [src/migrate.rs]
fn migrate(&self, host: &mut Host, mods: &[Mod]) -> Result<Value, String> {
    let mut items: Vec<Value> = mods.iter()
        .map(|m| json!({ "id": m.id, "label": m.name, "state": "waiting" }))
        .collect();
    let mut converted = 0;
    for (i, m) in mods.iter().enumerate() {
        items[i]["state"] = json!("running");
        self.send_tasks(host, &items);
        match self.convert(host, m) {
            Ok(()) => { items[i]["state"] = json!("done"); converted += 1; }
            Err(e) => { items[i]["state"] = json!("failed"); items[i]["note"] = json!(e); }
        }
    }
    self.send_tasks(host, &items);
    // shown inside the dialog, not as a notification
    Ok(json!({ "notice": format!("Converted {converted} of {} mods", mods.len()) }))
}

fn send_tasks(&self, host: &mut Host, items: &[Value]) {
    if !self.host_calls.iter().any(|c| c == "tasks") {
        return; // hello didn't list it
    }
    host.send(&json!({ "method": "tasks", "params": {
        "title": "Migrating your mods to Swofty addons",
        "subtitle": "Each mod in your library becomes an addon.",
        "items": items
    } }));
}
```

What the launcher receives mid-run:

```json
{ "method": "tasks", "params": {
  "title": "Migrating your mods to Swofty addons",
  "subtitle": "Each mod in your library becomes an addon.",
  "items": [
    { "id": "okayama", "label": "Okayama", "state": "done" },
    { "id": "liveries-2026", "label": "2026 liveries", "state": "running" },
    { "id": "old-hud", "label": "Old HUD", "state": "failed", "note": "No archive inside the zip" },
    { "id": "sounds", "label": "Sounds", "state": "waiting" }
  ] } }
```

## Fields

- `title`: the dialog's heading.
- `subtitle`: a line under the heading. Optional.
- `items`: one row each, in order.
  - `id`: the item's key, unique in the list.
  - `label`: the row's text.
  - `state`: `waiting`, `running`, `done`, `failed` or `skipped`.
  - `note`: short text under the label, such as why the item failed. Optional.

## The dialog

- Send `tasks` only when `hello.params.host_calls` lists it.
- Each `tasks` carries the whole list and replaces the last one.
- The first `tasks` during an event opens the dialog, and later ones update it. It stays open when the player switches pages.
- **Close** stays off until the event returns.
- When the event returns, its notice or error shows inside the dialog instead of as a notification: a `notice` effect, the `notice` of a `show_addon` effect, or an error.
- When an item has failed, the dialog also offers **Send Logs to Developers**.
- The launcher shows lists sent while it handles an `event` and ignores the rest.
- A `tasks` has no `id` and gets no reply. Sent with an `id`, it gets the error reply `tasks takes no id`.
- A Rust helper on [`run_helper`](./protocol#the-rust-side) calls `cx.tasks(&TaskList { … })`, which sends nothing and returns `false` when the launcher doesn't list `tasks`.

A [`startup` event](./events#startup) is the place for a first-run migration: it runs once per launcher start and can show a task list.
