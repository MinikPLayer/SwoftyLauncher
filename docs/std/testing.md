---
title: Testing your addon
description: Write tests that start the game in a known state, such as a race at a chosen track, a career at the contract window or a given menu, run steps, check what happened and report, with one command and no clicking.
---

# Testing your addon

`swofty test` starts F1 25 in the state your test needs, runs your steps, checks the results and writes a report. A test never clicks through menus or plays through races to get somewhere: it names where it starts, and the launcher and the core take it there.

```sh
swofty test run ./my-addon            # every test the addon ships
swofty test run ./my-addon -t smoke   # only the tests tagged smoke
swofty test plan ./my-addon           # what would run, and how many game starts it takes
```

```text
swofty test: 6 tests in 3 game starts
  benchmark bahrain 3 laps .......... tyre_temps_rise_on_track    pass   (1 m 52 s)
  career fixtures/my-team-r3 ........ rnd_tab_hidden_in_team_hq   pass
                                      rnd_upgrade_applies          FAIL  expected engine.max_torque > 493, got 493
  route laptop_rnd .................. rnd_page_lists_departments  pass
5 passed, 1 failed  ·  report: swofty-test/20261028-1402/report.json, junit.xml
```

## A test

A test lives in your addon's `tests/` folder. The test file says where each test starts, what it does and what must be true afterwards.

<Split>

Each test has:

- **An environment**: where the game starts. A race with AI, a career from a saved fixture, a menu, or just the main menu.
- **Steps**: what happens once it's there. Wait for an event, read values, open a menu, skip a session.
- **Checks**: what must be true. A log line, a value, no crash, the frame rate.

Tests with the same environment share one game start, so a suite of twenty career tests starts the game once.

<template #example>
<Example root="swofty.rnd-tyres-thermal" data-file="tests/thermal.test.json" files="swofty.json, tests/native/tests.cpp, tests/native/tests.dll, tests/fixtures/my-team-r3/fixture.json">
<template #data>

```json
{
  "schema": 1,
  "suite": "swofty.rnd-tyres-thermal",
  "tests": [
    {
      "id": "tyre_temps_rise_on_track",
      "tags": ["smoke"],
      "env": { "kind": "benchmark", "track": "bahrain", "laps": 3, "cars": 10 },
      "steps": [
        { "wait": { "event": "race.start" }, "timeout_s": 90 },
        { "sample": { "read": "car[0].tyres[fl].temperature", "every_ms": 500, "for_s": 60, "as": "fl" } }
      ],
      "assert": [
        { "series": "fl", "rises_by": 8 },
        { "log": "\\[rnd-tyres\\] thermal model on" },
        { "no_crash": true }
      ]
    },
    {
      "id": "rnd_tab_hidden_in_team_hq",
      "env": { "kind": "career", "fixture": "fixtures/my-team-r3", "at": "team_hq" },
      "fn": "rnd_tab_hidden"
    }
  ]
}
```

</template>
</Example>
</template>
</Split>

## Environments

| `kind` | Starts at | Use it for |
|---|---|---|
| `boot` | the main menu | menus, tiles, addon settings, anything at startup |
| `benchmark` | a race with AI cars, straight from launch | physics, AI, sounds, tyres, anything on track |
| `route` | a menu, opened by its route | a screen your addon adds or changes |
| `career` | a saved career, loaded into Team HQ, a weekend or a session | careers: contracts, R&D, seasons, calendars |
| `grand_prix`, `time_trial` | a session from a box, at a track, in a car | year packs, tracks, rules per box |

```json
{ "kind": "benchmark", "track": "monaco", "laps": 2, "cars": 20, "weather": "rain", "box": "swofty.classic-seasons-2017/f1_2017" }
{ "kind": "route", "routes": ["career", "team_hq", "contracts"] }
{ "kind": "career", "fixture": "fixtures/progressive-2018-r10", "at": "weekend_hub" }
{ "kind": "time_trial", "box": "mod.season-2008/f1_2008", "track": "venue/albert_park", "car": "mod.season-2008/ferrari" }
```

`route` and `career` are experimental and not working in 1.6.0; `grand_prix` and `time_trial` are planned.

In a `benchmark` race the AI drives every car, including the player's. For a test that needs the player's controls, such as the MFD or DRS, a step drives the car with the virtual pad.

## Steps

```json
{ "wait": { "event": "career.season.start" }, "timeout_s": 120 }
{ "route": "contracts" }
{ "skip": "session" }
{ "advance": { "days": 7 } }
{ "ai_drives_player": true }
{ "pad": { "press": "mfd_fuel", "then": "right" } }
{ "set": { "car[player].f1_aero.lift_scale": 1.05 } }
{ "read": "career.money", "as": "money_before" }
{ "capture": "frame", "name": "team_hq" }
{ "save": "now" }
```

`skip` and `advance` use the game's own simulate and calendar code, so a test can get from the first race of a season to the season break in seconds. `read` and `set` use the same names as `cars::live` and the system pages.

## Checks

```json
{ "log": "\\[career\\] season 2019 starts", "min": 1 }
{ "value": "car[0].chassis.mass", "between": [790, 830] }
{ "value": "money_after", "greater_than": "money_before" }
{ "event": "car.reset", "count": 2 }
{ "no_crash": true }
{ "perf": { "p99_ms": { "max": 40 } } }
```

## The test file

A file named `*.test.json` in the addon's `tests/` folder holds one suite. Every key is checked when the file is read, and an unknown key is an error naming it.

```json
{
  "schema": 1,
  "suite": "swofty.rnd-tyres-thermal",
  "defaults": {
    "timeout_s": 300,
    "addons": ["swofty.rnd-tyres-thermal", "swofty.core"],
    "settings": { "swofty.rnd-tyres-thermal": { "thermal_model": true } }
  },
  "tests": [
    {
      "id": "thermal_off_by_default",
      "tags": ["smoke"],
      "fresh": true,
      "env": { "kind": "boot" },
      "settings": { "swofty.rnd-tyres-thermal": { "thermal_model": false } },
      "timeout_s": 60,
      "steps": [{ "wait": { "log": "\\[rnd-tyres\\] thermal model off" }, "timeout_s": 30 }],
      "assert": [{ "no_crash": true }]
    }
  ]
}
```

- **`defaults`:** `timeout_s`, `addons` and `settings` for every test in the file. A test's own `settings` add to them.
- **`addons`:** the addons the test's profile switches on. The default is the addon whose folder holds the file.
- **`settings`:** addon settings (`addon.option` values) set for the test's profile.
- **`fresh`:** the test gets its own game start instead of sharing one with tests of the same environment.
- **`fn`:** the name of a `SWOFTY_TEST` function in the addon's test DLL, in place of `steps` and `assert`.

The environments take these keys:

```json
{ "kind": "none" }
{ "kind": "boot" }
{ "kind": "benchmark", "track": "bahrain", "laps": 3, "cars": 10, "weather": "dry", "box": "...", "grid_pos": 1, "camera": "...", "driver": "..." }
{ "kind": "route", "routes": ["career", "team_hq"] }
{ "kind": "career", "fixture": "fixtures/my-team-r3", "at": "team_hq", "slot": 1 }
{ "kind": "grand_prix", "box": "...", "track": "...", "car": "...", "team": "...", "session": "...", "weather": "..." }
```

`none` starts the game and is ready at the loader's attached line, before any menu. `boot` and `route` are ready at the main menu. `benchmark` needs `track`, `route` needs `routes`, `career` needs `fixture`.

## Tests in code

A test can also be a C++ function in a test DLL your addon ships next to the test file. It runs inside the game, a step at a time, without blocking it.

```cpp
#include <swofty/test.hpp>
#include <swofty/systems/career.hpp>

SWOFTY_TEST(rnd_tab_hidden)(swofty::test::Context &t) -> swofty::test::Task
{
    co_await t.at("team_hq", 60);
    t.expect(!t.read<bool>("ui.team_hq.tabs.rnd.visible"), "the R&D tab is hidden");
    co_await t.route("laptop_rnd");
    co_await t.log("\\[rnd-tyres\\] blocked", 5);
    co_await t.capture("team_hq");
}

SWOFTY_TESTS()
```

The test DLL loads only during `swofty test`, never in normal play.

## Fixtures

A fixture is a saved game in a known state: a career after ten races, a My Team at the contract window, a Grand Prix weekend. Capture one from a running test session:

```sh
swofty test shell --fixture swofty-profile-20261028   # start the game in test mode, in the save sandbox
> career load 1                         # or play to the state you want
> save now
> quit
swofty test fixture capture progressive-2018-r10 --run <the run the shell named>
```

```text
fixtures/progressive-2018-r10/
  fixture.json        the game build, the addons and versions it needs, what it holds
  save/               the game's save files
  storage/            addon storage that lives outside the save
```

Outside the shell, `swofty test fixture capture <name>` captures the last test run's saves, and `--from <folder>` a copy of a save folder. `swofty test fixture list` and `swofty test fixture check <name>` check a fixture's files against its `fixture.json`. A test names a fixture by a folder next to its test file, or by name in your fixture store (`SWOFTY_TEST_FIXTURES`, else `test-fixtures` in the launcher's data folder).

```json
{ "kind": "career", "fixture": "fixtures/progressive-2018-r10", "at": "team_hq", "slot": 2 }
```

`at` is `team_hq` (the default), `weekend_hub` or `session`. `slot` picks the career by its save slot when the fixture holds more than one; without it the newest is continued. `session` isn't available.

`career load`, `advance` and `save now` are experimental and not working in 1.6.0. They run only in the save sandbox: in a `career` environment, or in `swofty test shell --fixture <name>`. Without a fixture the shell runs on your own saves, so those commands are refused.

Tests always run on a copy of a fixture in a separate save folder. They never read or write your own saves, and nothing they save reaches Steam Cloud: if the launcher can't make sure of that for your game's build, the test doesn't start.

## What runs today

- **Runs in game:** `none`, `boot` and `benchmark`. The title screen is passed by the virtual pad after 15 seconds, and a `boot` run reaches the main menu with every addon loaded.
- **Save sandbox:** runs. Loading a fixture profile and saving three times in the sandbox leaves the player's saves unchanged.
- **Experimental, not working in 1.6.0:** the `route` environment, the `career` environment, and the agent commands `route`, `career load`, `advance`, `save now` and `title`. They reach the front end but not the home screen.
- **Planned, not runnable:** `grand_prix` and `time_trial` environments. `swofty test plan` lists them as "needs session setup inside the game's screens". A `career` environment with `"at": "session"` answers `unavailable`.
- **Steps and checks the launcher reads itself** (`wait`, and the `log`, `event`, `no_crash` and `perf` checks) work from the session's `loader.log`. Steps that read or write the running game (`read`, `sample`, `set`, `pad`, `capture`, and the `value` and `series` checks) go through the test agent in the game.

## Running tests

- One game runs at a time on a machine, so tests queue for it. `swofty test run` waits up to 30 minutes for the game to be free (`--wait 0` doesn't wait), and with `--take-over` it closes a running game first.
- Every run uses its own launcher profile with only the addons the tests name, and puts the game folder back afterwards.
- The report is JSON and JUnit, with logs, frames and the values sampled. The exit code is 0 when everything passed, 1 when a test failed, 2 when the game couldn't be started and 3 when the game was still busy after the wait.
