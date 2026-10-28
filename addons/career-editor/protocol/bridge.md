# Bridge: mailbox, snapshot, status, journal

Owner: bridge (`native/src/bridge.cpp`, `state.cpp`, `plugin.cpp`, `launcher/`).

All files live in the addon's storage folder, `<game>/swofty/addons/swofty.career-editor/` (the `addon_game_dir` host call). The game side writes them with the storage library (atomic temp + rename). The launcher writes inbox files atomically as well: `inbox/<seq>.json.tmp`, then rename to `inbox/<seq>.json`. Names that don't match `<digits>.json` are ignored.

In an OverlayFS session (`swofty run --use-overlayfs`, Linux) the game folder isn't changed and `addon_game_dir` answers differently: while the game runs, it is a path through the game's mount namespace (`/proc/<pid>/root/…/swofty/addons/swofty.career-editor/`), so the launcher's writes, renames and deletes reach the game and the game's reach the launcher exactly as in the game folder. When the game isn't running it is the profile's upper dir, `<data>/profiles/<profile>/writes/swofty/addons/swofty.career-editor/`: the last snapshot shows there, and a batch queued while the game is closed runs in the next session. The launcher asks again every tick. A batch written in the few milliseconds between the mount and the session's record goes into the upper dir, where the game finds it too. The launcher reads the loader's binding sets (`bindings*.json`) from `addon_game_dir/../core`, falling back to `game_dir/swofty/addons/core`, because the real game folder has no `swofty/` folder in an OverlayFS session.

| file | writer | reader |
|---|---|---|
| `inbox/<seq>.json` | launcher | game (deleted once acked) |
| `outbox/<seq>.json` | game | launcher (deletes it after reading) |
| `snapshot.json` | game | launcher |
| `journal.jsonl` | game (append) | launcher |

## Batches (`inbox/<seq>.json`) {#batches}

```json
{
  "seq": 1792467946485,          // unique, increasing; the launcher uses max(last + 1, unix ms)
  "session": "launcher-run-id",  // echoed in the ack and the journal
  "label": "Set cash",           // free text, echoed in the journal
  "stop_on_error": false,        // true: skip the remaining ops after the first failure
  "ops": [
    { "op": "economy.cash.set", "args": { "value": 5000000 }, "expect": 4200000 }
  ]
}
```

- At most 256 ops per batch. Batches run in `seq` order, one per frame.
- `expect` is copied into `args.expect` when `args` has none. Ops that support stale checks compare it with the live value before writing and fail with an error when they differ.
- Gate: a batch with any `CE_OP` op waits for `safe` (Team HQ idle). A batch whose ops are all `CE_OP_ANYTIME` waits only for `career` (any career state except loading). A waiting batch stays in the inbox; `snapshot.bridge.blocked` says which one and why. Each op re-checks its own gate when it runs.
- The launcher withdraws a waiting batch by deleting its inbox file.
- An inbox file whose ack already exists in `outbox/` is dropped without running (no double apply after a restart).
- `bridge.ping` (no args) runs at any time, touches nothing and answers `{boot, mode, safe}` in `before`/`after`. Use it to prove the mailbox end to end.

## Acks (`outbox/<seq>.json`) {#acks}

```json
{
  "seq": 1792467946485, "boot": 1792467944023, "session": "launcher-run-id",
  "t": 1792467948106,             // unix ms when the batch ran
  "mode": "my_team",
  "ok": false,                    // every op ok
  "ms": 1,
  "results": [
    { "i": 0, "op": "economy.cash.set", "ok": true, "ran": true, "error": null,
      "args": { "value": 5000000, "expect": 4200000 },
      "before": { "cash": 4200000, "team": 655370 }, "after": { "cash": 5000000, "team": 655370 } },
    { "i": 1, "op": "nope.op", "ok": false, "ran": false, "error": "unknown op 'nope.op' (not in this build)",
      "args": {}, "before": null, "after": null }
  ]
}
```

`args` are the args the op received (with `expect` merged in). `ran` is true when the op's code ran (a failed op may still have read state). A rejected batch (unreadable, not JSON, no `ops`, too many ops) gets `{"seq", "boot", "t", "ok": false, "error": "...", "results": []}`.

Every op and section call runs inside an SEH guard (access violation, in-page error, divide by zero, illegal instruction, misalignment). A fault answers the op with `fault 0x<code> inside the op`, logs `FAULTED`, and switches that op or section off until the game restarts (`off` in the snapshot lists). C++ exceptions are caught too.

Error strings the bridge itself produces: `unknown op '<name>' (not in this build)`, `blocked: <why>`, `skipped: an earlier op in the batch failed`, `op switched off after a fault this session: ...`, `fault 0x<code> inside the op`, `exception: ...`.

## Journal (`journal.jsonl`) {#journal}

One line per op that succeeded (failed ops are only in the ack):

```json
{"t":1792467946485,"seq":100,"i":0,"session":"s1","label":"Set cash","mode":"my_team","op":"economy.cash.set","args":{"value":5000,"expect":1000},"before":{"cash":1000},"after":{"cash":5000}}
```

Undo is built by the launcher from journal lines (see each area's op docs for what undo means per op; the default is the same op with `args.value` set from `before`).

## Snapshot (`snapshot.json`) {#snapshot}

Rewritten when its content changes (checked every second, and right after a batch runs or a batch starts waiting) and at least every 5 s as a heartbeat. Nothing in it ticks on its own: `flow[].since` is the unix ms the state entered its status and `sections_at` is when a section last changed, so an idle game writes only the heartbeat. `written` older than about 20 s means the bridge is not running.

```json
{
  "v": 1,
  "written": 1792467954741,       // unix ms
  "boot": 1792467944023,          // changes when the game restarts
  "pid": 12345,
  "addon": { "id": "swofty.career-editor", "version": "0.1.0", "api": 5 },
  "status": { ... },              // below
  "bridge": {
    "pending": [1792467946485],   // queued seqs
    "blocked": { "seq": 1792467946485, "why": "Team HQ is advancing time" } | null,
    "last_ack": 1792467946400 | null,
    "applied": 12, "failed": 1,   // op counts this game session
    "ops": [ { "name": "economy.cash.set", "anytime": false, "off": null | "fault 0x... " } ],
    "sections": [ { "name": "economy", "off": null } ],
    "duplicates": []              // registrations ignored because the name was taken
  },
  "sections_live": true,          // false: the sections below are the last values seen (or absent)
  "sections_at": 1792467954741 | null,   // when a section last changed
  "sections": { "economy": { ... }, "season": { ... }, ... }
}
```

A section is absent when it has never rendered in this career. Sections render only while a career is loaded and the flow is settled (Team HQ idle, the weekend hub, or a running session), the launcher page is open (`viewer.json`, which the page rewrites every 3 s, is under 15 s old) and the game is not the window in front; they are cleared when the career unloads. A section that faults is switched off for the game session and listed with `off`.

### Status {#status}

```json
{
  "game": "running",
  "career": true,                 // a career is loaded
  "mode": "my_team" | "driver_career" | "none",
  "career_id": "mt-000a000a-00004000" | null,   // ce::career_id(), see below
  "humans": 1,                    // human entrants; more than 1 blocks every op (two-player career)
  "phase": "team_hq" | "team_hq_busy" | "weekend_hub" | "session" | "loading" | "frontend" | "unknown",
  "safe": true,                   // CE_OP ops can run now
  "anytime": true,                // CE_OP_ANYTIME ops can run now
  "why": null | "a session is running; edits wait for Team HQ",
  "why_anytime": null | "no career is loaded",
  "season": 1 | null, "year": 2026 | null, "round": 5 | null, "rounds": 24 | null,
  "day": null, "weekend": "rw_miami" | null,           // copied from the season section
  "player": { "driver": 655361, "team": 655370 } | null, // packed keys (components helper)
  "detect": {
    "components": "live" | "no entity seen yet" | "fallback to flow",
    "component_mode": "my_team" | "driver_career" | "none",
    "team_hq_answer": "my_team" | "driver_career" | "none",
    "flow_career": true
  },
  "state": "StateTeamHQUpdate",   // the last flow state that changed status (or "#<djb2>")
  "transitions": 812,
  "flow": [ { "state": "StateTeamHQUpdate", "status": "Updating", "since": 1792467950000 } ]
}
```

`career_id` (`ce::career_id()`, empty in C++ when unknown): `mt-<team key>-<driver key>` in My Team, `dc-<driver key>` in Driver Career (8 hex digits each, the player's packed keys from the components helper). It is stable for a career's life (the My Team team and the Driver Career driver never change) but two careers with the same team and driver share it. The save slot would be a better key; the slot manager component is not bound yet.

`why` values: `no career is loaded`, `two-player careers are not supported`, `Team HQ is advancing time`, `Team HQ is heading to a session`, `Team HQ is running the end of season`, `Team HQ is in the season break`, `a session is running; edits wait for Team HQ`, `not in Team HQ`, `Team HQ is settling` (the first 0.75 s after Team HQ comes up). `why_anytime`: `no career is loaded`, `the game is loading`.

## State detection {#state-detection}

| binding | what the bridge reads |
|---|---|
| `site_FeFlow_SetStateStatus` (in `FeFlow_Set_StateStatus` 0x144423180, the function behind the FLOW log line `Transitioning "<state>" <old> -> <new>`) | rcx = flow state, edx = new status (0 None, 1 Entering, 2 Updating, 3 Exiting); state `+0xb0` = djb2 of the state name, `+0xb8` = old status; `+0x20`/`+0x28` = name, copied only when it is plain text (djb2 matches) |
| `site_Career_TeamHqIsMyTeamResult` (in `Career_StateTeamHq_OnStateEnter`, after `Career_Is_MyTeamGameMode_Thunk`) | al = the game's own My Team answer, which picks `enter_my_team_career` vs `enter_team_hq` |
| `ce::comp` (components helper, economy) | entity + `CareerComponent` = career loaded; `CareerBank` or `MyTeamFacilities` = My Team, `DriverCareerPlayerDriverStatistics` = Driver Career; the human entrant's keys |

Tracked states: `StateTeamHQ`, `StateTeamHQUpdate`, `StateTeamHQTimeAdvance`, `StateTeamHQToSession`, `StateTeamHQEndOfSeason`, `StateTeamHQSeasonBreak`, `StateSession`, `StateLoadSession`, `StateUnloadSession`, `SessionStateCareerHubUpdate`, `PostRaceCareerHub`, `StateCareerHubSelector`, `StateFrontend`, `StateOnlineFrontend`, `StateFrontendWorld`, `StateCareerDataSync`. Both sites only read. The log gets one `[career-editor] flow:` line per tracked transition and one per first sighting of any other state.

## Launcher side {#launcher}

The launcher plugin (`launcher/`, `career-editor-helper`) keeps its own files in `$DATA/addon-data/swofty.career-editor/`:

| file | what |
|---|---|
| `sent.jsonl` | every batch it wrote: seq, label, career id, ops, and `undo_of` for undo batches |
| `history.jsonl` | every acked op (from `outbox/`, which it then deletes): before, after, error, args, career id |
| `backups/<YYYY-MM-DD_HH-MM-SS>/remote/` + `backup.json` | Steam save folder copies (`steam_save_dir`) |
| `backups/session.json` | the game session (launcher session, else pid + snapshot `boot`) already backed up |
| `prefs.json` | page choices (ratings edit mode, R&D team) |

- Before the first batch of each game session it copies the Steam save folder; a failed copy stops the batch. Without a Steam save folder it sends and says no backup was taken.
- Restore needs the game closed and an explicit switch; it first backs up the current saves, then copies the backup's files over the save folder (files not in the backup are left alone).
- Undo sends the inverse op with `expect` = the edit's `after`: `economy.cash.*` → `economy.cash.set` to `before.cash`; `economy.rp.*` → `economy.rp.set` to `before.rp`; `drivers.ratings.set` → the same op with the `before` values and `"mode": "restore"`; `rnd.fit` → the opposite `fitted`; `season.calendar.reorder` → the `before` order of the future rounds; `weather.plan.*` → `weather.plan.set` with the `before` sessions; `weather.seed.*` → lock with `before.seed` or unlock. `rnd.complete/unlock/reset/refund` and `facilities.*` are backup-only. History and undo are scoped to the loaded `career_id`.
- One sidebar section, `editor` ("Career Editor"): a status hero, then a `tabs` bar (Overview, Team, Drivers, R&D, Season, Weather, History). The active tab lives in the helper for the launcher session. A tab whose section the bridge doesn't register is disabled; History shows the queued count as a badge.
- A snapshot that doesn't parse (or isn't UTF-8) keeps the last good one on screen with an "Editor data unreadable" notice; the bridge's Writer replaces invalid UTF-8 with U+FFFD and escapes control characters, and logs any section over 64 KB once.
- Failed ops show their error code as the result pill and the detail beside it; a result with `after.not_applied` shows as "Stored, not applied" with the reason.
- While the game runs with no snapshot, the page asks `game_build_key`, `game_exe_version` and `game_dir` once per game run and logs the answer. When the exe SHA-256 prefix in the build key matches the `build.sha256` of none of the `swofty/addons/core/bindings*.json` sets in the game folder, the notice gives the exe version as one the mods weren't made for instead of pointing at the profile.
- Pages poll through `tick` (snapshot and outbox mtimes, `game_status`) and `invalidate` every section when something changed. Forms are disabled until the bridge is live with a career loaded; an op missing from `snapshot.bridge.ops` shows as not available in this build.
