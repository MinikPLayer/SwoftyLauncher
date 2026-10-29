---
title: "Test suite: design and plan"
description: How swofty test reaches each game state without clicking, what already works in the game and the launcher, what needs building or reverse engineering, and the order to build it in.
---

# Test suite: design and plan

The goal: any test, written by an addon author or an agent, starts F1 25 in the state it needs and checks it, with no menu clicking, no playing through races and no risk to the player's own saves. [Testing your addon](/std/testing) is the author's side. This page is how it's built.

It has two halves:

- **The CLI** (`swofty test`) plans the run, takes the game lock, builds a throwaway profile, prepares the save sandbox and the environment, launches the game, collects the results and puts everything back.
- **The test agent** is a core component that runs only in test mode. It takes commands from the CLI over a local mailbox and carries them out inside the game: open a route, start or skip a session, hand the player's car to the AI, read and write values, wait for events, capture frames, save, quit.

## What already works

- **Launch and roll back.** `swofty run --apply --direct` builds a profile into the game folder and puts it back on exit. Session logs, crash reports with symbolised stacks, the data dir's session lock and exact-PID game detection all exist.
- **A race straight from launch.** The game's own `-benchmark <xml>` mode boots into an all-AI race in about 25 seconds with no menus. The XML sets the track, laps, weather, number of cars, the player's grid slot and, through an undocumented `setup_mode`, the box. The AI drives the player's car.
- **Opening menus.** The core already fires the game's UI routes on the home view model (an existing core menu driver does it), and the career hook sees every route the UI fires, so routes can be recorded and replayed.
- **Changing careers.** The Career Editor's live API sets money, resource points, R&D, contracts, points and calendar data in a running career.
- **Reading state.** The car and career structs in the KB, the core's events and log lines, the game's own log channels, `registry/effective.json` and frame capture.
- **Tracing.** The trace plugin installs hooks from KB signatures and logs registers and fields. It ran in the real game for the first time on 2026-10-28, with all 11 hooks installed.

## Environments

- **`boot`**: launch to the main menu. The agent passes the title screen without input (`title`, below); where that isn't bound, the CLI presses the virtual pad instead.
- **`benchmark`**: write `benchmark/swofty_test.xml` into the game folder for the run and launch with `-benchmark`. The CLI needs to pass game arguments in a direct launch. Race sessions only.
- **`route`**: `boot`, then the agent fires each route. Routes from the home screen work now. Picking an item inside a screen (a box, a track, a team) needs the screens' selection handlers.
- **`career`**: the save sandbox, the fixture's saves copied into it, then the agent continues the career to `at`: `team_hq` (the default), `weekend_hub` or `session`. After the agent's hello the CLI sends `career` `{"op":"load","slot":<n>|"newest","at":<at>,"mode"?}` (`career load <slot|newest> at <at>` in the shell; the mode comes from the fixture's `holds.careers` when it says which) and waits for its answer; the agent logs `[test] career ready at <at> (slot <n>)`. Every step waits on a flow or screen state with its own time limit and fails naming the step that didn't happen. The steps, from static RE on the Steam build (no signatures yet, so the agent binds them only on the KB build):
  1. **The title** (`title`, below), unless the front end (`StateFrontend`) is already up.
  2. **Route `career`** on the home view model, as the core's menu driver fires routes (L2).
  3. **The career tile.** fe_career's view model (`vt_Game_FECareerViewModel` 0x1457385b0, L2) is seen at the tile list populate loop like the home one. Its TileList (vm+0x2c8, entries `{node, item}` from +0x78 to +0x80) holds the career items (vtable 0x145703eb0); the item's kind is the NeStr at +0x178 (djb2 0x73b00842 `driver_career`, 0x353c4a02 `my_team_career`). The agent calls the item's GetAction (slot 5, `FeTile_Route_CareerItem` 0x141725620, L2), which writes the wizard data group and returns `{type 1, route solo_career_continue_driver|_my_team}` while the mode has a save, and fires that route as the tile screen does on `select_pressed` (`Vm_Fire_Route(vm->router, vm, &route, 0)`). Without a mode it tries My Team, then Driver Career; a tile whose action is not a continue route fails the load.
  4. **The resume card.** The resume view models (vt 0x1457264a0 driver, 0x145726670 My Team) are seen in `Game_CareerContinue_HandleCommand` (0x141a1db90, L1 with an agree, V1485), which the UI raises `transition_in_finished` into when the screen opens. Of the three cards (vm+0x328+i*0x540; has a save at +0, slot id at +8), the agent picks the slot asked for, or the first with a save for `newest`, and raises `slot_focused` then `slot_pressed` with data `i` through the handler. Presses are ignored while vm+0x1380 is set. `Game_SaveSlotManager_SelectSlot` loads it with no confirm popup.
  5. **Team HQ:** the flow enters `StateTeamHQ` after the press. `at team_hq` is done.
  6. **`weekend_hub`:** Team HQ's advance (`advance` with no day limit, below) to the next event, then the flow enters `StateSession`, the weekend's hub (L0: ts3b's reading; a slot saved inside a weekend may resume there directly, which needs a fixture and one in-game check).
  7. **`session`:** answers `unavailable`. The garage's `goto_track` command (`vt_Game_GarageCockpitViewModel` 0x145739830, slot 10 0x141bc5130, djb2 0xac456bd2, L1) is found, but not what releases the car after it, nor the confirms of its popups (time warning, parc fermé, tyre choice): it needs one trace of `goto_track` in a practice session.
  - There is no deep link to a given slot: the deep-link manager's career activity resumes the mode's current slot only.
- **`grand_prix` / `time_trial`**: either the game's settings-driven dev session (`DevSessionDataContract`: track, session, setup mode, entrants; reader 0x141125e80), if what switches it on can be found, or navigation with in-screen selection.

## The agent's commands

- **Skip a session:** post `MessageSetAdvanceDestination` (vtable 0x1457548b8) on the message bus, the same message the game's Simulate Session popup sends. Destination 2 is next session, 3 simulate, 4 skip to a session, 6 quick practice skip; the session type is the race (15) and so on.
- **Pass the title screen without input (`title`):** the title (`StateIntro`, `FeFlow_StateIntro_OnUpdate` 0x1436da4d0, L1) waits for its sync point (state+0xcd0, `sync_points.boot`; `Vm_SyncPoint_IsComplete` 0x1442626c0) to have both bool nodes true: the request node +0x80 and the UI's node +0xa0, which the title's UI sets on a button. It then sends its own request (kind 0x2e) whose callback sets +0xef4, and its next update returns the front-end link. The agent hooks the update (KB build only), and once the title's view exists sets the two nodes with `Vm_NodeSet_Bool` (0x140973e20, `(node, &value, 0, 0)` as the sync point's own writes), so the game's own path runs from there. It logs `[test] title: set the sync point's ... node`, then `[test] title: engaged`; a title that doesn't pass answers with the gate it stopped at. Where the input went in the pad run of 2026-10-28 is open: the game reads XInput only in one per-device poll (0x143a9e770, through a vtable), calls `GetForegroundWindow` only in its audio setup and `GetActiveWindow` only for a message box, so the press was not gated on those; a window-activation check in the input layer was not looked for.
- **Advance the career calendar (`advance {days}`):** Team HQ's advance view model (`vt_Game_TeamHQAdvanceViewModel` 0x14577a9c0, L2) is captured at its per-frame update (slot 12, 0x1422ca380, L2). The agent raises `timeline_advance_pressed` (djb2 0xdb427f42) through its onCommandRaised (slot 10, 0x1422c3540, L2) as the Advance button does: PressAdvance (0x1422c6860) checks vm+0x620, canAdvance (+0x758) and advancing (+0x759), shows today's cards instead when there are any, else runs `Career_TeamHqAdvance_Start` (0x1422bb0f0) and the screen's `start_advancing`. The game advances a day per tick to the next event; for `days` the agent raises `timeline_stop_pressed` (0x91c24f96) once the day (the game's own `(u32 [[vm+0x6e0]+0x2b0] >> 5) / 45`, the career clock in minutes over 1440) reaches the start plus `days`. The answer comes when advancing (+0x759) clears, or the view model goes away as the flow leaves Team HQ: `{"from_day", "to_day"}`. Sandbox only.
- **The AI drives the player's car:** `Game_Set_VehicleController_Thunk(system, vehicle id, 1, reason)` with the controller-set flag DEV (index 6 of the game's flag table: 0 PIT_LANE, 5 PAUSE, 6 DEV, 7 BENCHMARK, ...), which the thunk sets in the car's controller set when the third argument is 1 and clears when it is 0. Pass 0 to hand the car back.
- **Read and write car and career values:** the typed accessors `cars::live` uses, so tests and addons share one implementation of the scrambled values and the derived-value rules.
- **Input:** a virtual pad inside the game process (XInput slot 0), for what has no direct call yet.
- **Save now (`save now`):** the game's own profile save, `Game_SaveMgr_RequestProfileSave_Thunk` (0x1436595b0, L2) with the save manager system and `(mgr, &{0, 0}, 1, 0)`, as Team HQ's autosave after an advance. The manager is the system keyed `k_Game_SaveMgrSystemKey` (0x146100ca8) in `g_Game_Manager`'s system map (`[0x14604c2c8]+0x30`; `idx = 0x14030ffd0(map, &key)`, a miss returns the entry count; entries are 0x10 bytes from `[map+0x18]`, the system at +8). It answers once the sandbox has served a FileWrite. Sandbox only: the agent refuses it, `career load` and `advance` unless the save sandbox armed for the run (`sandbox.json` says armed), since the game would otherwise save into the player's own profile.

## The test run

One contract starts the agent and the sandbox, so a test DLL, the agent and the sandbox all arm or none does:

- **The run folder.** For a batch that needs the agent or the sandbox, the CLI makes `<data>/test-runs/<run>-<batch>/` with `run.json`: `{"schema":1,"run":"<id>","token":"<32 hex>","agent":{"v":1,"dlls":["dll/<suite>.dll"]},"saves":"saves","exe_sha256":"..."}`. `saves` (and `saves/remote/`) only when the save sandbox is required, which is every `career` batch and `swofty test shell --fixture`. The agent's mailbox is `agent/`: `commands.jsonl` (the CLI appends one JSON command per line, `{"seq","cmd","args","timeout_ms"}`), `results.jsonl` (the agent appends one line per reply, `{"seq","status":"ok|error|unavailable|timeout","value","error","ms"}`, or event, `{"event":"hello|fired|expect|log|refused",...}`), `dll/` and `frames/`.
- **The environment.** The game starts with `SWOFTY_TEST_RUN=<run folder>` (a Unix path maps to Wine's drive) and `SWOFTY_TEST_TOKEN=<token>`, plus `SWOFTY_EARLY_STAGE=1` with the sandbox, so the core loads before the save device exists.
- **Arming.** The core reads `run.json` once (`save_sandbox.cpp`). With either variable set, a token that isn't `run.json`'s, a missing or malformed `run.json`, or a test DLL path outside `agent/` ends the game with `[saves] TEST SANDBOX FAILED: <why>` (exit 91) before the title can load a save, whether the run asked for the sandbox or not. A good run arms the agent (`[test] agent armed for run <id>: ...; save sandbox required|off`) and, with `saves`, the sandbox. Normal Play sets neither variable, so it never arms either, never loads a test DLL and never installs their hooks.
- **The end.** After the batch the CLI removes the token from `run.json`; the agent sees that within half a second, stops and lets go of the pad. A run folder with the sandbox stays for `fixture capture`; one without is removed after the agent's results are kept as the batch's `agent.jsonl`.

## The save sandbox

The game keeps every career, Grand Prix and setting in one profile save: two encrypted files in Steam's `userdata/<account>/3059520/remote/`, which Steam Cloud syncs. It reads and writes them through Steam's storage API, so turning Steam Cloud off isn't enough: the files would still be written to the player's real folder.

The sandbox is a core feature at the save device's start (`Ne_SaveSystem2Win_Start`, 0x14413af70; the RE is in `re/db/subsystems/core.md#save-sandbox`). It runs only when `swofty test` starts the game with a run token:

- **The run folder.** The CLI makes `<data>/test-runs/<run>-<batch>/` with `run.json` (`{"schema":1,"run","token","saves":"saves","exe_sha256"}`) and starts the game with `SWOFTY_TEST_RUN=<folder>`, `SWOFTY_TEST_TOKEN=<token>` and `SWOFTY_EARLY_STAGE=1`, so the core loads at the game's first mutex, before the save device exists. After the batch the token is removed from `run.json`.
- **The proxy.** The device caches Steam's `ISteamRemoteStorage` at its Steam helper +0x108, written once before Start. The core's wrap of Start checks the device (its vtable slot +0x08 is Start, the helper exists, `m_storeInCloud` +0x3e8 is on, the interface's vtable lies outside the game) and swaps the pointer for a proxy that serves every slot from `saves/remote/`. The device calls FileWrite, FileRead, FileDelete, FileExists, GetFileSize, GetFileTimestamp, GetFileCount, GetFileNameAndSize and the write batch; the proxy serves those and a few more, answers 0 to the rest and logs them, and never calls Steam. The save files are opaque bytes to it. Loads and saves mount into `%TEMP%` and go through the interface, so nothing else is written.
- **The user data folder stays.** The helper's +0x00 (`GetUserDataFolder`) is only the prefix the device strips from its title folder to build remote names, never a path it opens; rewriting it would break the names and redirect nothing.
- **The corrupt-save upload is off.** Device slot +0x58 (`Ne_SaveSystem2Win_WriteInfoXml` 0x144141c00) zips a save and uploads it through Steam's global interface, outside the helper, so in a test run it returns without doing anything.
- **Fail closed.** With a token in the environment, any doubt ends the game, logging `[saves] TEST SANDBOX FAILED: <why>` and writing `sandbox.json` `{"state":"failed"}`: a token that isn't the run's, a binding that doesn't resolve, a wrap that doesn't install, a device or interface that isn't the expected one, or a mount (`Ne_SaveSystem2Win_Mount`, which every load and save passes before any file read) before the sandbox armed. Armed, it logs `[saves] test sandbox armed (run <id>)` and writes `{"state":"armed"}`.
- **Preflight and backstop.** The CLI refuses a career batch unless the core's binding set for this exe's sha256 carries `Ne_SaveSystem2Win_Start`, `_Mount` and `_WriteInfoXml`; the export gate needs an independent `agree` for each, so that is the static proof. After the batch it fails the tests unless `sandbox.json` says armed, and compares the player's Steam save folder listing (names, sizes, times, never contents) before and after.
- **Local-save mode** (`Steam.cloudEnabled` false, the EA layout) is not a sandbox: the device then reads and writes the player's own `userdata/<id>/3059520/local/savegame`. The sandbox refuses it.

## Fixtures

- **What's in one:** the two profile save files, the addon storage that lives outside the save (Progressive's career index, the Career Editor's points sidecar), the launcher profile, and `fixture.json` with the exe build and the addons it needs. About 1 MB.
- **Layout:** `fixture.json` (schema 1: `name`, `description`, `created`, `exe` `{sha256, version}`, `addons` `{id: version}`, `holds` `{careers: [{mode, slot, name, season, round, team, driver}], notes}`, `files` `{path: {size, sha256}}`, `source`), `save/` with the Steam remote names (`savegame@profile#NXDSMWW.BWW` and `#QKRHMYXE`, plus mid-session saves; never highlights or corrupt-save bundles) and `storage/<addon id>/`. A run checks every file against `fixture.json`, copies `save/` into the sandbox and `storage/` into the batch profile's addon storage copy, which the session loads. Each career batch gets its own launcher profile and run folder.
- **Where they live:** a test's `fixture` is a folder next to its test file (`fixtures/my-team-r3`), else a fixture in the store (`$SWOFTY_TEST_FIXTURES`, else `<data>/test-fixtures/`) by the same path or its last part. Fixtures for public addons can ship in the addon's `tests/fixtures/`. The project's own fixtures carry swofty's careers and names, so they stay out of the repo: the store on this machine is `/mnt/gaming/swofty-std16/fixtures/` (set `SWOFTY_TEST_FIXTURES` to it).
- **Capture:** `swofty test fixture capture <name>` copies the newest run's sandbox (or `--run <id>`'s) and the addon storage the run left, with the run's exe and addons; `--from <dir>` takes a copy of a save folder instead and refuses the live one. `--career mode:slot`, `--description` and `--notes` fill `holds`. A fixture is never overwritten. `swofty test fixture list` and `check <name>` verify the files.
- **The first ones:** `swofty-profile-20261028`, swofty's whole profile from the 2026-10-28 backup copy (exe d873a1c8, game 1.0.141.7865; its careers are not listed yet), then a Progressive 2018 career from an existing backup and captures from test runs: a My Team career at the contract window, a Driver Career at the season break, a Grand Prix weekend.
- **Building states instead of capturing them:** the Career Editor's API, plus new operations to advance the calendar, set race results and save.

## Rules

- The test agent and test DLLs load only when `swofty test` builds the profile and passes a token for that run. Normal Play never loads them.
- One game per machine. Tests queue on the existing game lock (`/mnt/work/.f1-game.lock.d` here), and `--take-over` closes a running game first.
- The CLI never accepts the offline play terms for the player: a run without them fails with exit code 2.
- Artefacts have size caps. Frames are optional.

## What ships in 1.6.0

- **The CLI:** `swofty test plan`, `run`, `shell` and `fixture capture|list|check`. Environments `none`, `boot` and `benchmark` run in game. `route` and `career` (`team_hq`, `weekend_hub`) are experimental and not working in 1.6.0; `grand_prix` and `time_trial` parse and are listed as needing session setup. The test file schema is in [Testing your addon](/std/testing#the-test-file).
- **The test agent** (`native/src/core/testagent`): the mailbox, `ai_drives_player`, `read`, `set`, `sample`, `pad`, `capture` and `skip` work. `route`, `career load`, `advance`, `save now` and `title` are experimental and not working in 1.6.0. Test DLLs written with `swofty/test.hpp` load only in a test run.
- **Title screen:** the CLI waits 15 seconds for the agent to pass it, then presses the virtual pad.
- **The save sandbox** (`native/src/core/save_sandbox.cpp`): runs in game. The Steam remote-storage proxy is fail-closed on any doubt. A profile load and three saves ran with the player's saves unchanged. Measured: the 0-entry snapshot case of the property-bag pool; the 64 KB and 256 KB cases are not measured. The fixture store is host-tested.
- **Snapshots:** the registry save default is dry in 1.6.0.
- **Checked only in the host tests** (no game run): the CLI's flow against a fake game and a fake agent, the run folder and token contract, the sandbox decision logic and the fixture check.

## Build order

1. **CLI only, now.** `swofty test plan` and `run` with the `boot` and `benchmark` environments: pass game arguments in a direct launch, stage the benchmark XML, and check logs, crashes, performance and that the game folder is clean afterwards. Most car-physics traces (T112 to T125) can run unattended this way.
2. **The test agent.** The mailbox, routes from the home screen, skip session, the AI driving the player's car, value reads and writes through `cars::live`, frame capture, the virtual pad and `swofty test shell`.
3. **Saves and careers.** The save sandbox, fixtures, continuing a career into its hub, calendar advance, race results and save now. Then `career` environments.
4. **Any screen and session.** Selection inside screens and the dev session start, for `grand_prix` and `time_trial` from any box, track and car.
5. **CI.** A machine that runs every addon's smoke tests before a launcher release, and publishes the reports.

The reverse engineering behind steps 3 and 4, in order of value: the save sandbox install point, continuing a career, what switches on the dev session, selection inside the Grand Prix and Time Trial screens, what ends a session after a skip, and the race results write.
