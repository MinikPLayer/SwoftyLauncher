---
title: Commands and the profile summary
description: swofty subcommands a launcher plugin adds with launcher.commands, and the line it puts on the profile card with summary.
---

# Commands and the profile summary

A plugin adds top-level `swofty` subcommands with `launcher.commands`, and a line on the profile card with `summary`.

## A command in full

The template's `swofty example` lists its items:

```json [swofty.json]
"launcher": {
  "program": { "windows": "bin/example-helper.exe", "linux": "bin/example-helper" },
  "commands": [{ "name": "example", "about": "List the example items" }]
}
```

```rust [src/main.rs]
fn command(&self, host: &mut Host) -> Result<Value, String> {
    let on = self.enabled(host)?;
    let lines: Vec<String> = self.items().into_iter()
        .map(|n| format!("{} {n}", if on.contains(&n) { "on " } else { "off" }))
        .collect();
    Ok(json!(lines.join("\n"))) // the text swofty prints
}
```

```text
$ swofty example
on  okayama.exmod
off suzuka.exmod
```

```json
{ "id": 5, "method": "command", "params": { "path": ["example"], "args": [] } }
{ "id": 5, "result": "on  okayama.exmod\noff suzuka.exmod" }
```

## Declaring commands

```json [swofty.json]
"commands": [
  { "name": "mods", "about": "Manage the mod library",
    "args": [],
    "subcommands": [
      { "name": "import", "about": "Import mod files",
        "args": [ { "name": "paths", "help": "The files to import", "required": true, "many": true } ] }
    ] }
]
```

```text
CommandSpec { name, about, args: [ArgSpec], subcommands: [CommandSpec] }
ArgSpec     { name, help, required, many }
```

- `name` and `about` are required; `args` and `subcommands` default to empty.
- Every `ArgSpec` key is required.
- Choose a name that core and earlier addons haven't used, or it is refused.

## The command call

- `path` is the path of names: `["example"]`, or `["mods", "import"]` for a subcommand.
- `args` are the arguments.
- Return the text you want printed.
- Commands run against `--profile` (default: the active profile) and the Steam install of the game.
- `command` has 300 seconds.

## Profile summary

Return `{ label, items }` from `summary` to add a line to the profile card, or `null` for no line:

```rust [src/main.rs]
fn summary(&self, host: &mut Host) -> Result<Value, String> {
    let on = self.enabled(host)?;
    if on.is_empty() {
        return Ok(Value::Null);
    }
    Ok(json!({ "label": "EXAMPLE", "items": on }))
}
```

```json
{ "method": "summary" }
{ "id": 6, "result": { "label": "EXAMPLE", "items": ["Okayama", "Suzuka"] } }
```

- Your line appears after `ADDONS`. Team Simplified uses it to show `MODS` with its enabled mod names.
- `summary` has 5 seconds.
- The launcher asks only when the plugin is already running and idle: a summary never starts the plugin. While the plugin is busy, the card keeps its last summary.
