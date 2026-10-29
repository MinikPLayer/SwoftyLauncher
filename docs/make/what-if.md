---
title: Build a what-if history editor
description: A companion addon for Progressive Career with a launcher page where players change each season's starting grid and each driver's ratings, applied as a layer on Progressive's seasons without touching its files.
---

# Build a what-if history editor

You'll build `tails.whatif-history`, a companion addon for Progressive Career. It adds a page to the launcher with a season picker, the season's grid and a ratings table. A player puts Alonso in the 2017 Mercedes, raises his pace, and the next Progressive career races that history.

Progressive registers each season's grid and ratings as registry records. Your addon layers those records, so it never changes Progressive's files.

## The folder

```text
tails.whatif-history/
├── swofty.json
├── registry.json
├── launcher/                 the launcher page, in Rust
│   ├── Cargo.toml
│   └── src/main.rs
├── bin/
│   ├── whatif                built from launcher/ for Linux
│   └── whatif.exe            and for Windows
├── native/                   the in-game code
│   ├── build.sh
│   ├── whatif.dll
│   └── src/whatif.cpp
└── test/                     unit tests for swofty dev test
    ├── register_test.cpp
    └── storage/profile/whatif.json
```

The addon has two halves. The **launcher page** saves the player's edits for the active profile. The **DLL** reads them when the game starts and layers Progressive's seasons.

## swofty.json

<Split>

`requires` makes Progressive a dependency, so it loads first and the launcher stops Play when it's missing. The `launcher` block adds the page and asks the launcher to run the page's build step before each Play.

`registry.json` names the two registries the addon layers, with the major version it was written against.

Both files are in `templates/std/whatif` in the launcher's repository, with the rest of this addon.

<template #example>
<Example root="tails.whatif-history" data-file="swofty.json" files="registry.json, launcher/Cargo.toml, launcher/src/main.rs, bin/whatif, bin/whatif.exe, native/build.sh, native/src/whatif.cpp, native/whatif.dll">
<template #data>

<<< @/../templates/std/whatif/swofty.json

<<< @/../templates/std/whatif/registry.json{json} [registry.json]

</template>
</Example>
</template>
</Split>

## What Progressive registers

Each Progressive season is a `career::Season` record, `swofty.progressive/2016` to `swofty.progressive/2026`. Its `teams` are keyed by slot, and each team has two seats. Each driver's ratings for a season are a `career::DriverRatings` record, keyed by year and driver:

```text
career.season           swofty.progressive/2017
  teams[slot 1]           team constructor/mercedes, seats [game/lewis_hamilton, game/valtteri_bottas]
  teams[slot 2]           team constructor/ferrari,  seats [game/sebastian_vettel, game/kimi_raikkonen]
career.driver_ratings   swofty.progressive/2017/game/lewis_hamilton
  season swofty.progressive/2017, experience 94, racecraft 93, awareness 90, pace 95
```

See [Career](/std/systems/career#progressive-s-career-mode) for the full records.

## The page

<Split>

The page shows a tab per Progressive season, with the season's grid and ratings from the last session's `registry/effective.json` in the launcher's data folder, which the core writes each time a session loads. Before the game has run once, it shows the tabs 2016 to 2026 and asks the player to start the game once.

An edited cell writes the edit into the addon's state for the active profile with `set_state`. The build step writes that state into the addon's folder in the game, where the DLL reads it, and writes an empty file for a profile with no edits. Each profile has its own edits, so a "what if" profile and a real-history profile sit side by side.

The `Host` type and the message loop are the [launcher plugin template](/reference/addons/launcher-api)'s.

<template #example>
<Example root="tails.whatif-history" code-file="launcher/src/main.rs" files="swofty.json, registry.json, launcher/Cargo.toml, bin/whatif, bin/whatif.exe, native/build.sh, native/src/whatif.cpp, native/whatif.dll">
<template #code>

<<< @/../templates/std/whatif/launcher/src/main.rs

</template>
</Example>
</template>
</Split>

`launcher/Cargo.toml` needs one dependency, `serde_json = "1"`. The page's state and the file the build step writes have the same shape:

<<< @/../templates/std/whatif/test/storage/profile/whatif.json{json} [profile/whatif.json]
 See [Host calls](/reference/addons/launcher-api/host-calls) and [The build reply](/reference/addons/launcher-api/build-reply) for `get_state`, `set_state`, `game_dir` and the build step.

## Apply the edits in the game

<Split>

`SWOFTY_REGISTER` runs at every game start, before the game loads its database. It reads the file the build step wrote and adds one layer per edited season and one per edited driver.

Each layer has `Scope::profile`: it applies while this profile plays. Progressive's seasons and ratings are snapshot registries, so a career copies each season's grid and ratings when that season starts. A new career gets the edits from its first season. A running career gets them for every season it hasn't reached yet, and keeps what it already started.

A layer only changes the fields it sets. Progressive's mid-season swaps, team names and everything else in the season still come from Progressive.

The DLL reads the page's file with `swofty::json`, the SDK's JSON reader.

<template #example>
<Example root="tails.whatif-history" code-file="native/src/whatif.cpp" data-file="registry/layers.json" files="swofty.json, registry.json, launcher/src/main.rs, native/build.sh, native/whatif.dll">
<template #code>

<<< @/../templates/std/whatif/native/src/whatif.cpp

</template>
<template #data>

```json
{
  "career.season": {
    "swofty.progressive/2017": {
      "teams": [ { "slot": 1, "seats": ["game/fernando_alonso", "game/valtteri_bottas"] } ]
    }
  },
  "career.driver_ratings": {
    "swofty.progressive/2017/game/fernando_alonso": { "pace": 96 }
  }
}
```

</template>
</Example>
</template>
</Split>

The **Without code** tab is a fixed preset: the same layers as a data file, with no launcher page. It applies in every profile where the addon is on. Ship one or the other, not both.

## Test it without the game

`swofty dev test` builds each `test/*.cpp` for your computer and runs it against the fake core, which runs your `SWOFTY_REGISTER` and records every layer. The test gives the DLL a storage folder with the sample file above (`test/whatif_test.hpp` holds that stand-in and a Progressive 2017 season), runs the registration, and checks each layer's target, scope and edit:

<<< @/../templates/std/whatif/test/register_test.cpp

```text
$ swofty dev test
ok    one layer on career.season
ok    on Progressive's 2017 season
ok    in the profile scope
ok    Alonso takes the first Mercedes seat
...
ok    register_test
3 tests, 0 failed
```

## Try it

Build the page for each system you list in `launcher.program`, and the DLL with `native/build.sh`:

```sh
(cd launcher && cargo build --release && cp target/release/whatif ../bin/)
native/build.sh
swofty dev pack
swofty addons install ./tails.whatif-history
swofty addons enable swofty.progressive
swofty addons enable tails.whatif-history
```

1. Open **What if history** in the launcher and pick 2017.
2. Put `game/fernando_alonso` in the Mercedes' first seat, and set his pace to 96.
3. Press Play, or run `swofty run --apply`, and start a Progressive career in 2017.

`swofty/loader.log`:

```text
addon whatif.dll: calling SwoftyAddonRegister (API v9) at the early stage
[registry] register: tails.whatif-history 2 layer(s)
```

`registry/effective.json` shows each changed field with `tails.whatif-history` as its origin:

```json
"swofty.progressive/2017": {
  "status": "ok",
  "value": { "year": 2017, "teams": [ { "slot": 1, "team": "constructor/mercedes", "seats": ["game/fernando_alonso", "game/valtteri_bottas"] } ] },
  "origins": { "/teams/slot=1/seats/0": "tails.whatif-history" },
  "owner": "swofty.progressive",
  "snapshot": "1.0"
}
```

If another addon changes the same seat, `registry/conflicts.json` lists both, and the later one in load order wins.

A career copies each season's grid and ratings when that season starts. A career already in 2017 keeps the grid it started 2017 with. A career still in 2016 races the edit when it reaches 2017.

## When Progressive changes or is missing

The page keeps the player's edits by record id, and the DLL checks each one against what Progressive registers:

- **A season Progressive no longer has:** the layer targets a record that doesn't exist, so the core skips it and logs it.
- **A slot Progressive removed:** `s.teams.find(slot)` returns nothing, and that seat edit is skipped.
- **A driver that no longer exists:** the seat's reference can't resolve, the core rejects that edit and logs it, and the seat keeps Progressive's driver.
- **A rating record that's gone:** skipped, like a missing season.
- **Progressive turned off:** `requires` stops Play, and the launcher says Progressive is needed. Careers made with the edits also record Progressive as a dependency, and a [guard](/std/dependencies#guards) stops them loading without it.

## Publish

Package the addon with its DLL and both page binaries, and publish it: see [Packaging and publishing](/reference/addons/publishing). Leave the player's state out: it lives in their profile.
