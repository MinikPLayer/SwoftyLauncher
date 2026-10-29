---
title: The launcher side
description: What the launcher does (install, build, check dependencies, start the game), what it knows about code addons before and after a session, launcher pages that edit an addon's own per-profile data, the conflicts view, Send Logs and Developer mode.
---

# The launcher side

The launcher installs addons, builds their build operations, checks the dependencies they declare and starts the game. Addons register their records in code, and the core applies them in the game. The launcher doesn't run that code. What it knows about a code addon comes from two files: the `registry.lock.json` the addon ships, made when it was packed, and the `registry/catalog.json` the core writes after each session.

For example, Tails' "what if" addon edits Progressive Career's grids from a launcher page. The page saves the player's edits in the addon's own data. The addon's DLL reads them in the game and applies them as a layer.

<Split>

The page never touches a registry:

1. the page saves the player's seats in the addon's own **per-profile state**;
2. the addon's **build step** puts that state in the addon's folder in the game;
3. the addon's **DLL** reads the file and applies it as a `Scope::profile` [layer](/std/scopes#profile).

The card is step 3. Steps 1 and 2 are the Rust page [below](#editing-a-layer-from-a-launcher-page).

<template #example>
<Example root="tails.whatif-history" code-file="native/src/whatif.cpp" files="swofty.json, registry.json, launcher/src/main.rs, bin/whatif.exe, native/whatif.dll">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/storage.hpp>
#include <swofty/systems/career.hpp>
#include <swofty/systems/drivers.hpp>

#include "whatif.hpp"   // whatif::parse: the page's file, as one Seat per edited seat

using namespace swofty;

SWOFTY_REGISTER
{
    // written by the build step from the active profile's state
    std::string text;
    if (storage::read("profile/whatif.json", text) != storage::Result::ok)
        return;                                         // nothing edited in this profile

    for (const whatif::Seat &seat : whatif::parse(text))   // { season, slot, seat, driver }
        career::seasons.layer(career::seasons[seat.season], [seat](career::Season &s) {
            s.teams.at(seat.slot).seats[seat.seat] = drivers::identities[seat.driver];
        }, Scope::profile);
}

SWOFTY_ADDON()
```

</template>
</Example>
</template>
</Split>

```text
[registry] register: tails.whatif-history (whatif.dll) 2 layers on career.season (early stage, 0.2 ms)
[registry] career.season swofty.progressive/2017: edit by tails.whatif-history (profile) changed /teams/slot=1/seats/0
[registry] career.season swofty.progressive/2017: edit by tails.whatif-history (profile) changed /teams/slot=1/seats/1
```

## What the launcher does

- **Installs** addons, with their data files (`registry/*.json`) and their `registry.lock.json` in their folders.
- **Builds** each addon's build operations and its launcher plugin's build reply, in load order. It leaves out the operation files an addon's `registry.json` names in `replaced_operations`: the core applies those records (see [Build operations that records replace](/std/manifest#build-operations-that-records-replace)).
- **Checks dependencies** before Play: `swofty.json`'s `requires`, and `registry.json`'s `requires`, `breaks` and `conflicts` (see [Manifest](/std/manifest) and [Dependencies and guards](/std/dependencies)). An unmet `requires` or a `breaks` on an installed addon stops Play with a message naming the fix. A `conflicts` entry warns.
- **Starts the game.** The core applies every registration, data file and layer in the game, at the moment the game reads that data.

A profile of addons made for 1.5 and earlier builds exactly as before (see [Compatibility](/std/compatibility)).

## What the launcher knows about code addons

Before the game runs, the launcher reads each enabled addon's `registry.lock.json`. `swofty dev pack` writes it when the addon is packed, by running the addon's `SWOFTY_REGISTER` against a recorder. It lists the ids the addon registers, their labels, and what they provide and need. Nothing is applied from it: the game applies everything live. With it, the launcher can:

- check before Play that a code addon's tracks and cars are there for the content that needs them;
- list a code addon's cars in the importer's **Applies to** picker;
- show what an addon provides on its page.

After each session, the core writes `registry/catalog.json`: what actually loaded, with every record's label, owner and variant chain, each box's teams and track list, and what was refused or dropped. The launcher shows it marked "as of your last session".

```json
// registry.lock.json in mod.sochi, written by swofty dev pack
{
  "generated_by": "swofty dev pack 1.6.0",
  "provides": { "track": [ { "id": "mod.sochi/sochi", "label": "Sochi Autodrom" } ],
                "track.venue": [ "venue/sochi" ] },
  "needs": []
}
```

## Profiles

- **Each launcher profile has its own addon storage.** What an addon writes with `swofty::storage` in one profile isn't seen in another. The launcher keeps each profile's copy in `$DATA/addon-profiles/<id>/<profile>/` between sessions, outside the addon's own storage. The first time 1.6 runs, every addon's 1.5 storage is kept as a copy that every profile made before 1.6 starts from. A profile made under 1.6 starts empty, and a duplicate starts from a copy of its source.
- **Saves are shared** by every profile, as the game keeps one save.
- **Each career has an id.** The career index records which profile started it, and the launcher shows that next to the career.
- **The vanilla profile** warns before Play when a career needs addons, and backs up the saves.

## The registry folder in the game

The build writes one index, so the core finds data-file addons (they have no DLL in `settings.json`'s `plugins`). The core writes its reports and state files in the launcher's data folder, not the game folder, so one stat-id store serves the install whatever happens to the game folder: a restore of it, or a session whose writes go to an OverlayFS layer. The install names the data folder in `swofty/state.json`, as the game sees the path (`Z:\...` under Wine and Proton); the core reads `SWOFTY_STATE_DIR` first. A core that can't write there keeps its files in `<game>/swofty/registry/`, and the launcher reads both.

```text
$DATA/
  registry/
    catalog.json                        written by the core after each session: what loaded
    effective.json                      written by the core at each session load and on a crash
    conflicts.json                      written by the core at each session load
    room.json                           written by the core after each boot: how full the game database is
    ids.json                            the stat-id store, kept by the core
  careers/
    index.json                          the career index

<game>/swofty/
  state.json                            names $DATA to the core, placed by the install
  registry/
    index.json                          load order and each enabled addon's data files, written by the build
  addons/
    mod.esp/registry/sounds.json        a data file, installed with its addon
    mod.esp/registry/layers.json
    tails.whatif-history/profile/whatif.json   that addon's per-profile data, put there by its build step
```

The archives the core builds for team logos stay under `<game>/swofty/addons/swofty.logos/`: the game's file system reaches only files under the game folder.

```json
// index.json
{
  "version": 1,
  "profile": "Tails what-if",
  "order": ["swofty.core", "swofty.classic-seasons-2017", "swofty.progressive", "mod.esp", "tails.whatif-history"],
  "data": {
    "mod.esp": ["addons/mod.esp/registry/layers.json", "addons/mod.esp/registry/sounds.json"]
  },
  "registry": { "mod.esp": "addons/mod.esp/registry.json" },
  "addons": { "mod.esp": { "name": "Engine Sound Pack", "version": "1.0.0", "code": false } }
}
```

The core validates every data file and every edit when it applies them. A broken one is dropped, logged with the addon's name, and listed in `conflicts.json`.

When `room.json` says the profile's records don't fit in the game database, the next launch moves the database edits of build operations to the file route, padded past 838,860 records, so the game reserves a big enough table for the file before it loads it. That costs one restart.

## Status lines

Players learn that something didn't apply from a status line on the addon's tile, in ADDON SETTINGS in the game and on the addon's row in the launcher:

```text
Heavier Damage Realism        1 layer dropped (vehicle.handling game/redbull_2026: invalid damage_scale)
Progressive Career            hook career.calendar.add_activity unavailable (old_addon overwrites it)
F1 2016 GP/TT Addon           as of your last session: round 12 left out (no track provides Hockenheim)
```

The only popups are guards that stop a load (see [Guards](/std/dependencies#guards)).

## Editing a layer from a launcher page

A launcher page edits the addon's own data and nothing else, with these host calls: `get_state` and `set_state` for the per-profile state, the `build` reply to put it in the game, and `registry_dir` to show the last session's grid. This is Tails' page, written against the launcher plugin template:

```rust
// launcher/src/main.rs
use serde_json::{Value, json};

// the page: one row per season and slot, from the last session's effective.json, with this profile's edits on top
fn view_history(host: &mut Host) -> Value {
    let edits = host.call("get_state", Value::Null).unwrap_or(Value::Null);   // null: nothing edited in this profile
    let last = host.call("registry_dir", Value::Null).ok()
        .and_then(|d| d.as_str().map(|d| format!("{d}/effective.json")))
        .and_then(|p| std::fs::read_to_string(p).ok())
        .and_then(|t| serde_json::from_str::<Value>(&t).ok())
        .unwrap_or(Value::Null);                                              // null until the game has run once
    let mut rows = Vec::new();
    for (id, season) in last["career.season"].as_object().into_iter().flatten() {
        if !id.starts_with("swofty.progressive/") {
            continue;
        }
        let year = season["value"]["year"].as_i64().unwrap_or(0);
        for team in season["value"]["teams"].as_array().into_iter().flatten() {
            let slot = &team["slot"];
            let seats = edits[id].as_array().into_iter().flatten()
                .find(|t| &t["slot"] == slot).map(|t| t["seats"].clone())
                .unwrap_or_else(|| team["seats"].clone());
            rows.push(json!({
                "id": format!("{id}#{slot}"),
                "cells": [year, team["team"], seats[0], seats[1]],
                "edit": [false, false, "driver", "driver"],
            }));
        }
    }
    json!({ "subtitle": "What if history", "body": { "table": {
        "columns": ["Season", "Team", "Seat 1", "Seat 2"], "rows": rows } } })
}

// a cell edit: save the seats in this addon's state for the active profile
fn edit_seat(host: &mut Host, id: &str, slot: i64, seats: [&str; 2]) -> Result<(), String> {
    let mut edits = host.call("get_state", Value::Null)?;
    if edits.is_null() {
        edits = json!({});
    }
    let teams = edits[id].as_array().cloned().unwrap_or_default();
    let mut teams: Vec<Value> = teams.into_iter().filter(|t| t["slot"] != json!(slot)).collect();
    teams.push(json!({ "slot": slot, "seats": seats }));
    edits[id] = Value::Array(teams);
    host.call("set_state", edits)?;                    // an error on the vanilla profile
    host.invalidate("history");
    Ok(())
}

// the build step ("build": true in the launcher block): put this profile's edits where the DLL reads them
fn build(&self, host: &mut Host) -> Result<Value, String> {
    let edits = host.call("get_state", Value::Null)?;
    let file = self.cache_dir.join("whatif.json");
    std::fs::write(&file, edits.to_string()).map_err(|e| e.to_string())?;
    Ok(json!({ "ops": [{
        "op": "file.put",
        "source": { "root": "cache", "path": "whatif.json" },
        "target": "swofty/addons/tails.whatif-history/profile/whatif.json"
    }] }))
}
```

```text
launcher  {"id":7,"method":"build","params":{"options":{},"features":{"stable":["file"],"experimental":[]}}}
plugin    {"id":0,"method":"get_state","params":null}
launcher  {"id":0,"result":{"swofty.progressive/2017":[{"slot":1,"seats":["game/fernando_alonso","game/valtteri_bottas"]}]}}
plugin    {"id":7,"result":{"ops":[{"op":"file.put","source":{"root":"cache","path":"whatif.json"},"target":"swofty/addons/tails.whatif-history/profile/whatif.json"}]}}
```

- **An addon edits only its own data.** There's no registry host call and no declaration: the page writes its addon's state, and the addon's code turns it into its own layer.
- **Per profile:** `get_state` and `set_state` read and write the addon's entry in the active profile. Duplicating or renaming a profile copies it, so a "what if" profile and a real-history profile sit side by side.
- **The grid the page shows** comes from the last `effective.json` the core wrote. Before the game has run once, there's none, and the page shows only its own edits. Nothing depends on it.
- **The edit reaches the game on the next Play:** the build writes the file, and the DLL applies it at the next game start.

See [Host calls](/reference/addons/launcher-api/host-calls) and [The build reply](/reference/addons/launcher-api/build-reply) for each call.

## The conflicts view

The core writes `registry/conflicts.json` at each session load. The launcher shows the last one on the profile, next to the file conflicts of the profile's last build (`conflicts.json`):

```json
{
  "registry": [
    {
      "kind": "field",
      "registry": "career.season",
      "id": "swofty.progressive/2017",
      "path": "/teams/slot=1/seats",
      "addons": ["tails.whatif-history", "someone.realism"],
      "wins": "someone.realism"
    },
    {
      "kind": "invalid_layer",
      "registry": "career.season",
      "id": "swofty.progressive/2018",
      "addon": "tails.whatif-history",
      "error": "/teams/slot=4/seats/1 \"game/kimi_raikkonen\" already holds a seat"
    },
    {
      "kind": "hook_unavailable",
      "point": "career.calendar.add_activity",
      "holder": "old_addon",
      "subscribers": ["swofty.progressive"]
    },
    {
      "kind": "handler_faulted",
      "point": "session.lap",
      "phase": "after",
      "addon": "someone.lapticker"
    }
  ]
}
```

Reordering addons resolves field conflicts the same way it resolves file conflicts. The new order applies at the next game start.

## Send Logs

The bundle adds the core's `registry/effective.json` (from the data folder), `registry/conflicts.json` and `registry/catalog.json`. `effective.json` is written at each session load and on a crash. It holds each registry's effective records, with the origin of every field that a layer changed:

```json
{
  "career.season": {
    "swofty.progressive/2017": {
      "value": { "year": 2017, "teams": [ { "slot": 1, "team": "game/mercedes", "seats": ["game/fernando_alonso", "game/valtteri_bottas"] } ] },
      "origins": { "/teams/slot=1/seats": "tails.whatif-history" },
      "snapshot": "1.0"
    }
  }
}
```

The triage bot can then say "the grid is from tails.whatif-history's layer" instead of guessing.

## Developer mode

The developer tools are in the public launcher, behind a **Developer mode** toggle in its settings. With it on, the CLI has a `swofty dev` family:

```text
swofty dev new <id>        a new addon from the template
swofty dev link <folder>   install an addon by reference: every build copies from the folder
swofty dev run             build, start the game and stream the addon's log lines
swofty dev logs            follow loader.log, filtered by addon
swofty dev check           check data files, references and the manifest without starting the game
swofty dev test            run the addon's unit tests against a fake core
swofty dev pack            pack the addon and write its registry.lock.json
```

The fake core runs an addon's `SWOFTY_REGISTER` outside the game, so unit tests can check records, layers and origins without a game install.
