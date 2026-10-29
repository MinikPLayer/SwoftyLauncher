---
title: Dependencies and guards
description: Dependencies on addons, registries, services and single records such as tracks; addons that break or conflict with others; renamed addons; what happens when something is missing; guards that turn the player back with a clear message; and careers that remember what they were made with.
---

# Dependencies and guards

Content depends on other content. A 2016 season races at Sochi, which a track addon provides. A career made with R&D Tyres has Tyres upgrades in its save. This page covers how your addon says what it needs, and what the player sees when something is missing.

Every dependency is checked four times:

1. **At install,** the Addon Browser offers what's needed and recommended.
2. **Before Play,** the launcher names what's missing, what's skipped, and which careers won't load.
3. **In game,** a guard cancels the start, the load or the box press, with a popup that names the fix.
4. **After load,** a career that loaded without its addons is stopped before it can save over its progress.

**Optional when the calendar is built, required once it's in it.** A calendar round on a missing track is dropped when the season's calendar is built. Once a track is in a career's built calendar, the career needs it.

## Declaring what you need

`requires` in `registry.json` is an array, like the `requires` in `swofty.json`. Each entry names an addon, a registry, a service, a hook point or one record:

```json
// swofty.classic-seasons-2016/registry.json
{
  "requires": [
    { "registry": "content.package", "version": "^1" },
    { "registry": "track", "version": "^1" }
  ],
  "recommends": [
    { "record": "track:venue/sochi",          "for": "the 2016 Russian Grand Prix" },
    { "record": "track:venue/hockenheimring", "for": "the 2016 German Grand Prix" },
    { "record": "track:venue/sepang",         "for": "the 2016 Malaysian Grand Prix" }
  ]
}
```

In code, the same is stated where it's used. The core checks it in game, and the guards stop the player with a popup:

```cpp
#include <swofty/systems/content.hpp>
#include <swofty/systems/tracks.hpp>

SWOFTY_REGISTER
{
    using namespace swofty;
    auto sochi   = tracks::venues["venue/sochi"].optional();                    // optional: the round is dropped without it
    auto ferrari = cars::vehicles["swofty.classic-seasons-2017/ferrari"];       // a hard reference: required
}
```

`swofty dev pack` records both references in the addon's `registry.lock.json`, so the launcher and the Addon Browser see them before the game runs (see [The lock file](/std/manifest#the-lock-file)).

- **`requires`:** Play stops while one is missing, with a message naming the fix, the same way it stops for a missing addon.
- **`recommends`:** optional. It never stops Play. The Addon Browser offers it at install, and the launcher names it before Play when something is skipped because of it.
- **`for`:** shown to players. Keep it short and concrete.
- **A `record` entry** means "some enabled addon provides this record". Its `version` is a range on the providing addon's version.

## Addons that break others

`breaks` and `conflicts` in `registry.json` name addon versions that don't work with yours. Either side can declare them:

```json
// swofty.progressive/registry.json
{
  "breaks": [
    { "id": "swofty.career-editor", "version": "<0.5.0",
      "why": "its contract overwrite switches off Progressive's contract hook" }
  ],
  "conflicts": [
    { "id": "someone.lineups", "version": "*", "why": "both rewrite the season's line-ups" }
  ]
}
```

- **`breaks`** stops Play while a matching version is installed and on:

```text
Progressive Career 1.3.0 doesn't work with Career Editor 0.4.2
  Its contract overwrite switches off Progressive's contract hook.
  Update Career Editor in the Addon Browser, or turn one of them off.
```

- **`conflicts`** warns before Play and lets the player go on.
- **The install solver** treats `breaks` as a constraint: it won't install a version that breaks an installed addon without asking.
- **`swofty.json`'s `conflicts`** keeps its own meaning (see [requires, conflicts, load_after](/reference/addons/manifest#requires-conflicts-load-after)).

## Renamed addons

An addon can keep its old id as an alias after a rename:

```json
// mod.sepang/registry.json: Sepang was published as mod.bahrain24
{ "aliases": [ { "id": "mod.bahrain24" } ] }
```

- **What follows the alias:** `requires`, `load_after` and every career's dependency record. A career made with `mod.bahrain24` loads with `mod.sepang` on.
- **Who can claim one:** the Addon Browser accepts an alias only when the old id and the new one have the same Browser author.
- **Record ids** keep their own owner prefix. A record that moves to a new id is a new record, and careers that used the old one still need it.

## Tracks have stable identities

A track is a record its addon provides. Every real circuit also has a **venue** id the core owns, so content can ask for "Sochi" without caring which addon brings it. Real people and teams get shared ids the same way, under `person/` and `constructor/`, and driver and team records provide them.

```text
track
  game/catalunya               stock, playable
  game/sochi                   stock row, no assets (playable: false)
  mod.sochi/sochi              Sochi Autodrom: fills the game/sochi slot, provides venue/sochi
  mod.bahrain24/sepang         Sepang: fills the game/bahrain24 slot, provides venue/sepang
  mod.hockenheim/hockenheim    a new track code, provides venue/hockenheimring
venue/sochi, venue/sepang, venue/hockenheimring, venue/portimao, ...     one per real circuit
```

```cpp
// mod.sochi: the track fills the stock Sochi slot and provides the venue
#include <swofty/systems/tracks.hpp>

SWOFTY_REGISTER
{
    using namespace swofty;
    tracks::tracks.add("sochi", {                                  // "mod.sochi/sochi"
        .code = "sochi",
        .name = "Sochi Autodrom",
        .slot = tracks::tracks["game/sochi"],
        .provides = { tracks::venues["venue/sochi"] },
    });
}
```

```json
// the same, without code: mod.sochi/registry/tracks.json. A track addon with a track.add build operation gets the same record.
{ "track": { "sochi": { "code": "sochi", "slot": "game/sochi", "name": "Sochi Autodrom", "provides": ["venue/sochi"] } } }
```

- **A `venue/…` reference** resolves to the last enabled track in load order that provides it, like every other contest. An exact id (`mod.sochi/sochi`) pins one addon. When two enabled tracks provide one venue, the conflicts view lists both.
- **A track's venue comes from its record,** never from its code. `mod.bahrain24` uses the `bahrain24` slot and is Sepang.
- **Stat ids are kept.** The core keeps one stat-id store per install, in the launcher's data folder, at `$DATA/registry/ids.json`. The first id a track, driver, team or vehicle gets stays with it. An id is given to another record only when no career uses it.
- **Each career carries its own id map** in its save entry: `{record id: stat id}` for every track, driver, team and vehicle it uses. At `career.load` the core checks the map against this install's store, so a save synced to another PC can't race a different track or field a different driver. When the saved id is free, the core takes it. When another record holds it, the guard stops the load and names both records.

```text
[tracks] hockenheim: stat id 43 (kept since 2026-10-27)
[guards] career.load: f1_career_progressive/6f3e9a needs track mod.hockenheim/hockenheim at stat id 43;
         this install has mod.okayama/okayama at 43. Blocked.
```

## When a reference is missing

The registry's owner sets what may happen when a referenced record isn't there. The author of the referencing record picks within that:

```json
// the career.calendar schema: a round's venue
"venue": {
  "type": "string",
  "x-ref": "track.venue",
  "x-missing": { "default": "required", "allow": ["required", "optional", "fallback"], "drop": "element" }
}
```

```json
// the vehicle schema: a car's engine sound falls back to its donor's
"engine_sound": { "type": "string", "x-ref": "engine_sound", "x-missing": { "default": "fallback", "to": "/donor" } }
```

In code, a reference is a `Ref<T>`, and the author picks with `.optional()` and `.or_else()`. In a data file it's a string with two modifiers:

```cpp
// swofty.classic-seasons-2016's calendar
.rounds = {
    { .round = 4,  .race = Text::key("lng_gp_russia"),   .venue = tracks::venues["venue/sochi"].optional() },
    { .round = 12, .race = Text::key("lng_gp_germany"),  .venue = tracks::venues["venue/hockenheimring"].optional() },
    { .round = 16, .race = Text::key("lng_gp_malaysia"), .venue = tracks::venues["venue/sepang"].or_else(tracks::venues["venue/bahrain"]) },
}
```

```json
"rounds": [
  { "round": 4,  "race": "lng_gp_russia",   "venue": "venue/sochi?" },
  { "round": 12, "race": "lng_gp_germany",  "venue": "venue/hockenheimring?" },
  { "round": 16, "race": "lng_gp_malaysia", "venue": "venue/sepang|venue/bahrain" }
]
```

- `"venue/sochi"` is required: the record doesn't resolve without it.
- `"venue/sochi?"` is optional: the element, here the calendar round, is dropped when nothing provides it.
- `"venue/sepang|venue/bahrain"` is a fallback chain: the first one that resolves.

Readers get the resolved value, never the modifiers. `career::calendars.status(id)` lists what was dropped or replaced:

```text
[registry] career.calendar swofty.classic-seasons-2016/f1_2016: round 12 dropped (venue/hockenheimring: no
           enabled track provides it; optional); round 16 races at mod.bahrain24/sepang
```

The player can change a policy from the launcher's Missing content panel ("Skip round 12", "Race it at Red Bull Ring"), within what the schema allows. The choice is saved in the profile, and the core applies it as a profile layer.

## Guards

A guard stops a front-end action and tells the player why. `career::block`, which R&D Tyres uses to keep players out of My Team, is one kind of guard. Guards run at these points:

- `career.start`: a new-career route;
- `career.load`: a continue route;
- `career.content.open`: a career tile press;
- `era.box.open`: a GP/TT box press;
- `route`: any front-end route, by name.

<Split>

R&D Tyres keeps players out of My Team, which it doesn't support, and an R&D challenge career checks that R&D Tyres is on before its tile opens.

In code a guard is a handler on a guard point. An addon without code declares the same guards in `registry.json`, the one place a data file can hold them.

<template #example>
<Example root="swofty.rnd-tyres-thermal" code-file="native/src/guards.cpp" data-file="registry.json" files="swofty.json, native/CMakeLists.txt, native/src/rnd_departments.cpp">
<template #code>

```cpp
#include <swofty/guards.hpp>

SWOFTY_ON_LOAD
{
    using namespace swofty::guards;

    on(Point::career_start, [](const Target &t, Verdict &v) {
        if (t.mode == Mode::my_team)
            v.block("My Team not available",
                    "R&D: Tyres & Thermal Management only works in Driver Career. To play My Team, turn the addon off "
                    "in the launcher (or use a profile without it), then relaunch.");
    });

    on(Point::career_content_open, [](const Target &t, Verdict &v) {
        if (t.data_group != "f1_career_rnd_challenge")
            return;
        if (!swofty::addons::on("swofty.rnd-tyres-thermal"))
            v.missing({ .name = "R&D: Tyres & Thermal Management", .id = "swofty.rnd-tyres-thermal",
                        .need = ">=0.11.0", .state = Missing::State::off, .for_ = "the Tyres R&D department" });
    });
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
{
  "guards": [
    { "on": "career.start", "mode": "my_team",
      "block": { "title": "My Team not available",
                 "body": "R&D: Tyres & Thermal Management only works in Driver Career. To play My Team, turn the addon off in the launcher (or use a profile without it), then relaunch." } },
    { "on": "career.load", "mode": "my_team", "same_as": 0 },
    { "on": "career.content.open", "data_group": "f1_career_rnd_challenge",
      "require": [ { "ref": "addon:swofty.rnd-tyres-thermal", "version": ">=0.11.0", "for": "the Tyres R&D department" } ] }
  ]
}
```

</template>
</Example>
</template>
</Split>

- **`block`** cancels with your own title and body.
- **`missing`** cancels, and the core writes the popup from every missing item, in the same words everywhere.
- **`career::block(mode, title, body)`** is a guard too, with the same log lines and popup.
- **Guards are the only popups.** Anything else that didn't apply shows as a status line on the addon's tile in ADDON SETTINGS and in the launcher.

The core also runs a **dependency guard** at every point: a career's dependencies on `career.load`, and record requirements on `career.content.open` and `era.box.open`. The popups it writes:

```text
Missing addons for this career
  This career was made with Sochi Autodrom (mod.sochi). Turn it on in the launcher, then load it again.
  The career hasn't been loaded, so none of its progress is lost.

Missing addons for this career
  This career was made with 2 addons that are off: Sochi Autodrom (mod.sochi) and R&D: Tyres & Thermal
  Management (swofty.rnd-tyres-thermal). Turn them on in the launcher, then load it again.

Missing addons for this career
  This career was made with Sochi Autodrom 1.1.0 (mod.sochi), and 1.0.0 is installed. Update it in the
  launcher's Addon Browser, then load the career again.

F1 2017 (V10) can't start
  F1 2017 (V10) builds on F1 2017 GP/TT Addon (swofty.classic-seasons-2017), which is off. Turn it on in the
  launcher, then pick the box again.
```

```text
[guards] career.load: driver_career (continue, route 'solo_career_continue_driver', career
         f1_career_progressive/pg-1849223-max_verstappen); blocked by deps: mod.sochi off
[guards] route 'solo_career_continue_driver' cancelled; the player stays on the menu and the popup is queued
[guards] era.box.open: mod.esp/f1_2017_v10 blocked by deps: content.package swofty.classic-seasons-2017/f1_2017
         unresolved (swofty.classic-seasons-2017 off)
```

### When a box can't apply

A box's rules apply live, through game functions the core binds for each game build. After a game update, or on the EA build, a binding can be missing. Then:

- **A Grand Prix or Time Trial box** shows a guard with **Play anyway**. The box runs with the values the core couldn't change.
- **A career** stops, with no Play anyway.

```text
F1 2016 can't apply all of its rules on this game version
  Track fuel and laps will be the 2026 values. Swofty Launcher will fix this in an update.
  [Play anyway]  [Back]
```

### At the season break

Each season of a career uses a box, chosen when the career was created. When the pack that provides the next season's box is missing at the season break:

- **A pack that brings rules or systems** stops the career. The guard shows the missing-addons popup, and the career doesn't load into that season until the pack is back.
- **A pack that only brings cars and looks** falls back to the stock year. The season plays with the stock cars, and the pack's tile shows a status line. There's no popup.

## Careers remember what they were made with

Every career gets an id and a dependency record when it's created. The record is written again at each Team HQ visit after a change. It's saved in the career's entry in `swofty_registry`, the save container the core keeps outside the game's save heap, and mirrored to `$DATA/careers/index.json` in the launcher's data folder:

```json
// career.deps "f1_career_progressive/pg-1849223-max_verstappen"
{
  "career": "6f3e9a",
  "profile": "progressive",
  "label": "Progressive Career: Max Verstappen, 2016, season 1",
  "match": { "driver": 4103, "team": 2, "season_index": 0 },
  "made_with": { "launcher": "1.6.0", "core": "0.3.0", "game_build": 25251167 },
  "addons": [
    { "id": "swofty.progressive", "version": "1.3.0", "need": "^1.3.0", "why": "career content f1_career_progressive" },
    { "id": "mod.sochi",          "version": "1.1.0", "need": "^1.1.0", "why": "calendar round 4: track mod.sochi/sochi" }
  ],
  "records": [
    { "ref": "track:venue/sochi", "resolved": "mod.sochi/sochi", "stat_id": 18, "used": "calendar round 4" }
  ],
  "dropped": [
    { "ref": "track:venue/hockenheimring", "policy": "optional", "season": 2016, "for": "calendar round 12" }
  ]
}
```

- **`career` and `profile`:** the career's id and the launcher profile that started it. `match` identifies a career whose id can't be read yet.
- **Which addons go in:** every addon whose records the career uses, plus any addon that says so, with `saves.careers` in `registry.json` or a code call:

```json
// swofty.rnd-tyres-thermal/registry.json
{ "saves": { "careers": "loaded" } }
```

```cpp
#include <swofty/deps.hpp>

swofty::deps::used("swofty.rnd-tyres-thermal", "the Tyres R&D department");    // this career now needs the addon
swofty::deps::used_record("track", "mod.hockenheim/hockenheim", "calendar round 12");
```

- **`saves.affects` and its message** stay in `swofty.json` (see [saves](/reference/addons/manifest#saves)).
- **`dropped`** lists what each season skipped. A track installed later joins the next season's calendar when that calendar names its venue, and the launcher can say "this career skips Hockenheim in 2016".

## Before Play

```text
Missing content
  F1 2016 GP/TT Addon: the German Grand Prix (round 12) is left out, because no track provides Hockenheim.
  The Malaysian Grand Prix (round 16) races at Sepang (mod.bahrain24).
  [Install Hockenheim]

  2 careers won't load in this profile:
    Progressive Career: Max Verstappen, 2016: needs Sochi Autodrom (mod.sochi), which is off.
    Driver Career: Lando Norris, season 3: needs R&D: Tyres & Thermal Management, which is off.
  [Turn on Sochi Autodrom]  [Turn on R&D: Tyres & Thermal Management]
  The launcher backs up your saves before this session.
```

- **What stops Play:** a missing `requires` entry, or a `breaks` on an installed addon, with a message naming the fix.
- **What warns:** a `conflicts` entry, and a reference inside a record. An unresolved record hides its era box or tile, or guards it.
- **Where the launcher gets records:** each enabled addon's `registry.lock.json`, data files and `registry.json`, plus the core's `registry/catalog.json` from the last session for what actually loaded.
- **Install buttons** appear when the Addon Browser knows a published addon that provides the record.
- **The vanilla profile** has no core in the game, so no guards run. Before a vanilla session the launcher lists the careers that need addons ("2 careers use addons; loading them in vanilla can lose their progress") and backs up the saves.

## Turning an addon off

```text
Sochi Autodrom is in 1 career
  Progressive Career: Max Verstappen, 2016 races at Sochi in round 4. That career won't load until Sochi
  Autodrom is back on. Your other careers aren't affected.
  [Turn off]  [Cancel]
```

`swofty addons disable mod.sochi` prints the same text and needs `--yes`. `swofty careers` lists every career and what it needs, checked against a profile.

## After load: the backstop

Resume Career is checked before the load, from the career index. A career picked from the save-slot list is identified only once it has loaded, until the slot press is reverse-engineered. If it loaded with a required addon missing:

1. The core shows the popup: quit to the menu without saving, turn the addon on, load again.
2. A **save fence** writes back the bytes this career had at load in place of the new ones, so even a save can't drop progress. This needs the career's position inside its save container to be reverse-engineered, and is planned for 1.7.
3. Until then, the launcher's save backup before the session is the safety net. It covers every addon a career in the index depends on, not only `saves.affects` addons.

## The Addon Browser

The server reads what each addon version **provides** (registry records, tracks with their venues) and **needs** from its `registry.lock.json`, its data files and its `registry.json`. It serves both:

```text
GET /v1/provides?record=track:venue/sochi       -> [ { "addon": "mod.sochi", "version": "1.1.0", "record": "mod.sochi/sochi" } ]
GET /v1/addons/<id>/versions/<v>/tree           -> the full dependency tree with providers
```

- **At install:** a needed record resolves to an addon. If one addon provides it, it installs like an addon requirement. If several do, the player picks.
- **When nothing provides it:** "No addon on the Addon Browser provides Hockenheim yet. F1 2016 races without the German Grand Prix."
- **At publish:** the server suggests dependencies the author forgot, and checks every alias against the Browser author.
- **Launchers of 1.5 and earlier** get the same `requires` from the API as before.
