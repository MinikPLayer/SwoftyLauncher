---
title: The protocol
description: The messages a launcher plugin exchanges with the launcher, the calls the launcher sends, when the plugin runs, its time limits and how it fails.
---

# The protocol

A launcher plugin reads requests from stdin and writes replies and host calls to stdout, one UTF-8 JSON object per line.

## A session in full

Every call the launcher can send, in one session. `launcher` lines go to the plugin's stdin, `plugin` lines come from its stdout:

```text
launcher  {"id":0,"method":"hello","params":{"api":1,"addon":"example.launcher-addon","data_dir":"/home/you/.local/share/swofty/addon-data/example.launcher-addon","cache_dir":"/home/you/.local/share/swofty/cache/addons/example.launcher-addon","profile":{"name":"default","vanilla":false,"enabled":true},"launcher":"1.5.19","host_calls":["addons","install_addon","set_addon_enabled","remove_addon","move_addon","tasks","profile_heads"]}}
plugin    {"id":0,"result":{"api":1}}
launcher  {"id":1,"method":"view","params":{"section":"items"}}
plugin    {"method":"progress","params":{"done":0.0,"text":"Loading"}}
plugin    {"id":0,"method":"get_state","params":null}
launcher  {"id":0,"result":{"on":["okayama.exmod"]}}
plugin    {"id":1,"result":{"subtitle":"1 item, 1 on","body":{"list":{"rows":[],"reorder":false}}}}
launcher  {"id":2,"method":"event","params":{"section":"items","event":{"toggle":{"id":"okayama.exmod","on":true}}}}
plugin    {"id":2,"result":"reload"}
launcher  {"method":"profile","params":{"name":"wet-race","vanilla":false,"enabled":true}}
launcher  {"id":3,"method":"import","params":{"paths":["/home/you/Downloads/okayama.exmod"]}}
plugin    {"id":3,"result":["Imported okayama.exmod"]}
launcher  {"id":4,"method":"build","params":{"options":{"size":24},"features":{"stable":["file"],"experimental":[]}}}
plugin    {"id":4,"result":{"ops":[…],"lines":["1 items on"],"warnings":[]}}
launcher  {"id":5,"method":"command","params":{"path":["example"],"args":[]}}
plugin    {"id":5,"result":"on  okayama.exmod"}
launcher  {"id":6,"method":"summary"}
plugin    {"id":6,"result":{"label":"EXAMPLE","items":["okayama.exmod"]}}
launcher  {"id":7,"method":"tick","params":{"section":"overview"}}
plugin    {"id":7,"result":null}
```

## Messages

```text
request       {"id":1,"method":"view","params":{"section":"items"}}      id, method and params
reply         {"id":1,"result":{…}}                                      the request's id and a result
error reply   {"id":1,"error":"items.json is not valid JSON"}            the request's id and an error string
notification  {"method":"invalidate","params":{"section":"items"}}       method and params, no id
```

- The launcher sends one request at a time.
- While it handles a request, the plugin may make [host calls](./host-calls) back to the launcher before it replies. Host calls carry the plugin's own ids; the launcher answers each with the same id.
- `progress`, `tasks` and `invalidate` are notifications: send them without an `id`.

## Calls the launcher sends

```text
method      params                                     result
hello       { api, addon, data_dir, cache_dir,         { "api": 1 }
              profile, launcher, host_calls }
profile     { name, vanilla, enabled }                 (a notification: no reply)
view        { section }                                a View (Views)
event       { section, event }                         an Effect (Events and effects)
import      { paths }                                  [string]: lines to show
build       { options, features }                      { ops, items, lines, warnings, fields } (The build reply)
command     { path, args }                             a string to print
summary     (no params)                                { label, items } or null
tick        { section }                                null (Live pages)
```

### hello

Every session starts with `hello`. Reply `{"api":1}`: a plugin that answers with another number is stopped.

```json
{ "api": 1, "addon": "example.launcher-addon",
  "data_dir": "…/addon-data/example.launcher-addon",
  "cache_dir": "…/cache/addons/example.launcher-addon",
  "profile": { "name": "default", "vanilla": false, "enabled": true },
  "launcher": "1.5.19",
  "host_calls": ["addons", "install_addon", "set_addon_enabled", "remove_addon", "move_addon", "tasks", "profile_heads"] }
```

- `data_dir` is shared by every profile. Use `cache_dir` for anything your plugin caches; it also holds the plugin's `helper.log`.
- `profile.enabled` says whether your addon is on in the active profile. `profile.vanilla` marks the untouched profile: it takes no addons and refuses state writes.
- `launcher` is the launcher's version. It is missing before 1.0.10. Everything the docs mark *1.0.10* needs that launcher: set `"compat": { "launcher": ">=1.0.10" }` in `swofty.json` when you use it. An older launcher rejects a view that holds a block or field value it doesn't know: the plugin fails with `sent a bad result: unknown variant …`.
- `host_calls` lists the host calls a plugin must check for before it sends them:
  - A launcher that answers the [addon host calls](./host-calls#managing-addons) lists `"addons"`, `"install_addon"`, `"set_addon_enabled"`, `"remove_addon"`; from 1.3.0 `"move_addon"` follows them.
  - From 1.3.0 the list always has `"tasks"` ([Task lists](./task-lists)), with or without the addon calls.
  - From 1.5.12 `"profile_heads"` follows `"tasks"` when the launcher has found a game ([Profile heads](./host-calls#profile-heads)).
  - An older launcher that answers none of these leaves `host_calls` out.

### profile

When the active profile has changed since the last call, the launcher sends a `profile` notification before the next request:

```json
{ "method": "profile", "params": { "name": "wet-race", "vanilla": false, "enabled": true } }
```

### view, event and tick

`view` names the open section. `event` names the section and the [event](./events) its control fired. `tick` names the open section of a [live page](./live-pages).

```json
{ "method": "view", "params": { "section": "items" } }
{ "method": "event", "params": { "section": "items", "event": { "toggle": { "id": "okayama.exmod", "on": true } } } }
{ "method": "tick", "params": { "section": "overview" } }
```

### import

The absolute paths of the dropped files routed to your plugin, together in one call. Return one line per result ([Importers](./importers)):

```json
{ "method": "import", "params": { "paths": ["/home/you/Downloads/okayama.exmod"] } }
```

### build

The profile's value for every option your addon declares, and the installed loader's runtime features ([The build reply](./build-reply)):

```json
{ "method": "build", "params": { "options": { "size": 24 }, "features": { "stable": ["file"], "experimental": [] } } }
```

### command and summary

`command` carries the path of names, starting with the top-level name, and the arguments ([Commands](./commands)). `summary` has no params.

```json
{ "method": "command", "params": { "path": ["example"], "args": [] } }
{ "method": "summary" }
```

## How the launcher runs the plugin

```text
$DATA/addons/<id>/                      the working directory
$DATA/addons/<id>/<program[os]>         started with stdin and stdout piped
$DATA/cache/addons/<id>/helper.log      stderr, replaced each time the plugin starts
```

- The plugin starts when the launcher first needs it: when the launcher starts with one of its sections in the sidebar, or when someone drops a file routed to it, builds a profile that enables it, or runs one of its commands.
- The launcher picks `program[<os>]` for the host OS (`linux`, `windows`, `macos`). Without that entry the plugin fails with `has no helper for <os>`.
- On Windows the process gets no console window. On Linux and macOS the launcher sets the program's execute bit when it is missing.
- It keeps running until the launcher exits or the addon is reinstalled. It should exit when stdin closes.
- The launcher asks for a section's `view` at start, when the section opens, after a change, and after an [`invalidate`](./live-pages). It keeps the last view and shows it at once while it asks for a new one. While another call is running, such as an `import` or a `build`, it shows the last view and asks again afterwards.
- It asks for a `summary` only when the plugin is already running and idle; a summary never starts the plugin. While the plugin is busy, the profile card keeps its last summary.

## Time limits

```text
hello, view, tick, summary          5 seconds
event, import, build, command       300 seconds
```

- Each message your plugin sends resets the wait, except `invalidate`.
- Until you return an effect, the control that fired the event shows busy.

## Isolation and failures

A crash, a hang or malformed output stops the plugin without taking the launcher down. The launcher marks the plugin failed, and its sections show the error, prefixed with the addon's name:

```text
Example Launcher Addon has no helper for macos
Example Launcher Addon could not start /home/you/.local/share/swofty/addons/example.launcher-addon/bin/example-helper: No such file or directory (os error 2)
Example Launcher Addon speaks launcher API 2; this launcher speaks 1
Example Launcher Addon did not answer within 5 s
Example Launcher Addon stopped (exit status: 3)
Example Launcher Addon sent a bad message: expected value at line 1 column 1
Example Launcher Addon answered a request it was not sent
Example Launcher Addon sent a bad result: missing field `subtitle`
Example Launcher Addon sent a host call without an id
```

That covers a plugin that:

- does not start, or has no `program` for this OS;
- does not answer `hello`, `view`, `tick` or `summary` within 5 seconds, or any other call within 300 seconds;
- exits or closes stdout;
- writes a line that is not a message, replies to an `id` it was not sent, or returns a result of the wrong shape.

A failed plugin's sections show the error in a bad-tone notice with a **Restart** button (id `swofty.restart`). The body is empty and reads `<name> is not running`. **Restart** clears the failure, and the next call starts the plugin again. Reinstalling the addon also stops its plugin.

To fail one call only, reply with an error. The launcher shows it as a notification and keeps the plugin running:

```json
{ "id": 4, "error": "items.json is not valid JSON" }
```

A failed `tick` shows nothing, and the launcher goes on ticking.

## The Rust side

Inside the launcher, every plugin is driven through the Rust trait `Addon` (`packages/addon-api/src/launcher.rs`). The launcher's `HelperAddon` implements it for a plugin and turns each method into a message:

```rust
pub trait Addon: Send + Sync {
    fn sections(&self) -> Vec<SectionSpec>;                                  // launcher.sections
    fn view(&self, cx: &Cx, section: &str) -> Result<View>;                  // view
    fn event(&self, cx: &Cx, section: &str, event: Event) -> Result<Effect>; // event
    fn tick(&self, cx: &Cx, section: &str) -> Result<bool>;                  // tick; true sends invalidate
    fn importer(&self) -> Option<&dyn Importer>;                             // claims, import
    fn build(&self, cx: &BuildCx) -> Result<AssetBatch>;                    // build
    fn commands(&self) -> Vec<CommandSpec>;                                  // launcher.commands
    fn command(&self, cx: &Cx, path: &[String], args: &[String]) -> Result<String>; // command
    fn summary(&self, cx: &Cx) -> Option<Summary>;                           // summary
    // …
}
```

A Rust helper can implement the same trait and hand it to `swofty_addon::helper::run_helper`, which reads the requests, answers `hello`, and turns the host calls into `Cx` methods:

```rust [src/main.rs]
fn main() {
    // also answers `<program> claims <path>` from the importer's claims
    swofty_addon::helper::run_helper(std::sync::Arc::new(MyAddon::new()));
}
```

- `cx.addons()` is `None` when `hello` doesn't list all four addon calls ([Managing addons](./host-calls#managing-addons)).
- `cx.tasks(&TaskList { … })` sends nothing and returns `false` when `hello` doesn't list `tasks` ([Task lists](./task-lists)).

Compiled-in Rust addons that implement `Addon` inside the launcher are legacy: write a launcher plugin. Team Simplified is moving to a launcher plugin, and `addons/simplified` is being removed.
