# Season

Owner: `native/src/season.cpp` (ce-season). Both modes. The season lives in the career's
championship object (`vt_Career_Championship`, the component that saves `completion`,
`season_index` and `seasons[]`). Each season (`vt_Career_Season`) owns the calendar (one Weekend per
round, saved as `weekend_names[]` in round order) and the standings model
(`game::championship::standings::F1SeasonStandings`), which the game derives from session results
(`Season::applyResultsToStandings`) and rebuilds when a season loads.

The plugin gets the championship from `ce::comp::get(Championship)` on every section and op (the
components helper reads `CareerEntity.current_championship`, the entity's current championship that the
game's own set-current-championship writes, and logs each step). It is accepted only with the
championship vtable. The first time a season is found: `season: championship via comp::get 0x…; season
n/N at 0x…`.

The current season is always `seasons[season_index]`; seasons 3+ need no special slot. Nothing is
cached between frames.

`"ready": false` carries `reason`: `no_career`, `no_entity`, `no_championship` (the helper found
none), `championship_vtable` (it returned another object), `no_season`, `bad_season`, `bad_calendar`,
`bad_standings`. A failure logs once:
`season: championship not found (<reason>): comp::get … vt …, entity …`.

Errors (`ok: false`, `error`): `not_ready` (no career, or the championship not seen yet), `bad_args`,
`not_future` (the op touches the current, a past or a started round), `round_count` (the op would
change the round count away from the season's race weekends, see "Round count"), `unknown_weekend`,
`duplicate_weekend`, `unknown_track`, `no_career_id`, `no_driver`, `storage_failed`, `unsupported` (the op is documented but not
available in this build; `after` is `{ "detail": string }`), `game_refused`.

Every op logs one line: `[career-editor] season.<op> … <before> -> <after>`, or
`[career-editor] season.<op> refused: unsupported (<detail>)`. The first time a season is found:
`[career-editor] season: championship 0x… season <n>/<count> at 0x…`.

## Identifiers

- **Rounds** are 1-based (`round`). The current round is `season.round`. Only rounds strictly after
  the current round are editable (`calendar.editable_from`).
- **Weekends** are named by the gamedb track event they were built from (`rw_bahrain`,
  `rw_monaco_26`, …). The season saves the names in round order (`weekend_names[]`); each weekend
  also saves its own `track_stat_id` (u8, the Weekend deserialiser 0x140afbef0 reads the field
  `track_stat_id` into weekend+0x278), and the game indexes its runtime track table with that id
  (0x14309e220). Community tracks have no event of their own: the launcher names their weekends `rw_<code>` or
  `rw_<code>_26` and sends the track id (see below).
- **Drivers** in the standings are the game's entrant key (`key`, u32): the low byte is the session
  participant id, the high 16 bits the driver stat id (`driver_stat_id`). Teams use `team_stat_id`.
- **Sessions** use the game's session id (`session`, u32). Within a round the last points-scoring
  session is the main race; an earlier points-scoring session is the sprint.

## Snapshot section `season`

```json
{
  "ready": true,
  "season_index": 0,          // 0-based index into the championship's seasons
  "season_number": 1,         // season_index + 1
  "seasons": 1,               // seasons created so far in this career
  "name": "…",                // the season's own name
  "year": 2026,               // base year + season_index; base 2026/2025 from the season name, or 2026 when a
                              // weekend name ends in _26; null when neither says
  "round": 5,                 // current round, 1-based
  "rounds": 24,               // rounds in the season
  "weekend": "rw_miami",      // current round's weekend name
  "race_weekends": 24,        // race-weekend activities on the game's calendar this season (see "Round count"), null if unreadable
  "completion": 0,            // championship completion state (raw)
  "season_completion": 0,     // season completion_state (raw)
  "current_minutes": 123456,  // CareerTimeComponent current_minutes (what its serialiser saves), null if absent
  "day": 85                   // current_minutes / 1440, null if absent
}
```

## Snapshot section `standings`

```json
{
  "ready": true,
  "season_index": 0,
  "rounds_scored": 4,         // rounds with points in the standings model
  "drivers": [{
    "key": 3080192, "driver_stat_id": 47, "team_stat_id": 3,   // team from the championship entrants, null if unknown
    "position": 1, "points": 101, "wins": 2, "podiums": 3,
    "results": [{
      "round": 1, "session": 10, "race": true,                 // race: the round's main race (false: sprint)
      "pos": 1,                                                // 1-based, null if not placed
      "status": 0, "scored": true,                             // raw standings status; scored: status 0/1 (the game's points rule)
      "fastest_lap": false,
      "adjustment": false,                                     // true: the model's extra-points entry (season.points.adjust), not a result
      "points": 25
    }]
  }],
  "adjustments": [{                                            // this season's stored adjustments (sidecar)
    "driver_stat_id": 47, "key": 3080192, "points": -10,
    "live": true,                                              // in the live standings' extra-points list now
    "pending": false                                           // true: stored but not applied yet (see season.points.adjust)
  }],
  "adjustments_pending": false,                                // true: round 1 has no sessions yet, so the game skips the extra list
  "career_id": "…",                                            // ce::career_id(), null when the bridge has none (adjust is refused)
  "teams": [{ "team_stat_id": 3, "position": 1, "points": 180, "wins": 3 }]
}
```

Points per result are the standings model's own points records (points table, shortened-race scaling
and fastest-lap bonus already applied by the game). `points` is their sum. `position` orders by points,
then count-back of main-race finishes (P1s, then P2s, …). Team totals sum their drivers' records by the
team each driver races for now (`ce::comp::entrants()`); after a mid-season driver move the driver's
earlier points count for the new team, so the in-game constructors' table is authoritative then.
Driver `points` already include the driver's adjustment (the game adds it as one more points entry).
Driver and team names are not in the section: resolve the stat ids from the gamedb (`entrants.drivers`,
`entrants.teams`).

## Snapshot section `calendar`

```json
{
  "ready": true,
  "season_index": 0,
  "current_round": 5,
  "editable_from": 6,         // first round the ops accept, null when none
  "race_weekends": 24,        // race-weekend activities on the game's calendar this season, null if unreadable
  "round_count_ok": true,     // rounds == race_weekends; false: season.calendar.repair is needed; null if unreadable
  "rounds": [{
    "round": 1, "weekend": "rw_bahrain", "track": "bahrain",   // track: weekend name without rw_ and the _26 suffix
    "track_stat_id": 3,       // weekend+0x278, null when 0xff; the round's real track
    "completed": true, "current": false,
    "state": 0,               // the weekend's own state (+0x23c); 0 = not started
    "editable": false,        // after the current round and state 0
    "sprint": false,          // true/false from the round's points-scoring sessions once raced; null for rounds not raced yet (use the gamedb weekend_format)
    "sessions": [{ "session": 10, "completion": 2 }]            // built sessions, else []; completion: u32 state, 2 = complete
  }]
}
```

Dates are not in this section yet: the save stores weekend names only and the per-round date source is
not bound.

## Round count

The game's season flow reads two things that the save keeps apart:

- `Season.weekends` (one Weekend per round, saved as `weekend_names[]`) and `Season.current_weekend`.
  The current-weekend getter (Championship list vtable +0x98 -> +0x90, 0x140ae6ca0) returns
  `weekends[current_weekend]`, or null when `current_weekend` is past the end.
- The race-weekend activities on the career calendar. The ScheduleComponent builds its calendar entries
  (+0x2a8, 0x28 bytes each: activity definition +0x10, minutes +0x18) from the season length's event list
  in the gamedb (`event_list` / `default_track_list`, read at component init 0x140bc8fb0). The daily tick
  (0x14126a1a0) re-times them (0x140bc9700) and puts all of them on every driver and team calendar of the
  season. The career clock stops at each race-weekend activity (ActivityDef type 0x0a, `RaceWeekend`),
  and the game races the current weekend. They are not saved, so a load rebuilds them from the gamedb.

`GameModeChampionship::setWhereToNext` (0x1407ba860) reads the current weekend without a null check
(+0x64b), and `Season::completeSeason` (0x1407ba160) completes the season only when the current weekend
is complete and is the last one (`count <= current_weekend + 1`). So the round count has to equal the
race-weekend count:

| rounds vs race weekends | what the game does |
|---|---|
| equal | the season ends after its last round |
| fewer rounds (a round removed) | after the last round the next race-weekend activity finds no weekend: crash in `setWhereToNext` reading null+0x23c |
| more rounds (a round added) | the race-weekend activities run out before the last round; the season can't complete |

`race_weekends` counts the `RaceWeekend` activities of the busiest driver calendar in the
ScheduleComponent's record for this season index (`ScheduleSeason.driver_calendars`,
`DriverCalendar.activities`, `CalendarActivity.activity`, `ActivityDef.type`), the count the game's
own season-length lookup takes (0x141267be0, which the daily tick matches against the contract
schedule's season lengths). `reorder` and `set_round`
keep the count. `add_round` and `remove_round` are accepted only when they leave the count at
`race_weekends` or move it closer (a season edited before this rule); otherwise `round_count`, also when
`race_weekends` is unreadable.

## Ops

All ops run on the flow thread while `ce::safe()` (plain `CE_OP`), both modes.

Calendar ops accept only rounds after the current round whose weekend has not started (`state` 0, the
check the game's own `RebuildNotStartedWeekends` uses). They change the season's own weekend list,
which its serialiser saves as `weekend_names[]` in round order and its deserialiser rebuilds by name.
Results: `before`/`after` = `{ "rounds": [string], "tracks": [int|null] }` (every round's weekend name
and `track_stat_id` in round order).

**Index names.** A season the game builds from a track set (`Career_Add_TrackSetToSeason`, a Driver
Career) names its weekends by index: `"0"`, `"1"`, ... (`Career_Create_SeasonWeekend`'s `"%I64u"`).
The name says nothing about the track, so the launcher names each round by `track_stat_id`, and the ops
accept a 1 to 3 digit name only with `"verified": true` and `"track_stat_id"` (the same digits in
another season are another track). New rounds the launcher adds there still use `rw_` names.

**Weekend names and tracks** for `set_round` / `add_round`: the name is an index name (above) or
lower-case `[a-z0-9_]`, 3 to 31 characters (the serialiser's name buffer), not already in this season (the season's name map refuses
duplicates). A new weekend is built with its real `track_stat_id` (weekend+0x278); 0xff is only a
placeholder and some readers index the track table with it unchecked, so it is never used:
- a name (not an index name) that already appears in any season of this championship takes that
  weekend's track id;
- any other name needs `"verified": true` and `"track_stat_id": 0..254`, meaning the launcher resolved
  both from the active profile's last build. The plugin cannot list the track-event tables itself
  (their names are only hashed in the gamedb), and a name or id that does not resolve would break the
  weekend when it starts, so the launcher must only send what the active profile defines.

How the launcher resolves them (host call `profile_tracks`, launcher 1.0.12; it reads the gamedb in
the profile's latest build folder, or the stock one before the first build):

| Track | Weekend name | `track_stat_id` |
|---|---|---|
| Stock | its event: `schedule.all_track_events.fixed_event_N._metadata_` whose `activity` names `schedule.activities.activity_race_weekend_N`, whose `track` names the `tracks.Track_N` row; the `_26` event when this season's names end in `_26`, never a `_beta` event | that row's `statId` (suzuka 13, monaco 5) |
| Community, new code (Okayama, `copy_slot`) | `rw_<code>` / `rw_<code>_26` (no event exists; activation adds none) | the cloned row's `statId` (the next free id, 43 on a stock database) |
| Community, filling a slot (Sepang in `bahrain24`) | `rw_bahrain24` / `rw_bahrain24_26` (the slot has no event) | the slot row's `statId` (35) |

A track is offered only when its code is in the `2025` or `2026` content set's `available_tracks` (the
Grand Prix list; activation adds community tracks wherever their base track is listed) and, for a
stock track, when it has an event. Tracks already on this season's calendar are not offered again, and
a weekend name this championship knows whose track the build no longer has is listed as not offered.
Undo of `set_round` / `remove_round` sends `verified` and the track id when the old name resolves.

Errors: `unknown_weekend` (new name without `verified`), `unknown_track` (no usable track id),
`duplicate_weekend`.

### `season.calendar.reorder`
Args: `{ "order": [string] }`, the weekend names of every round after the current one, in the new
order (a permutation of `calendar.rounds[r].weekend` for `r >= editable_from`; every moved round must
be not started). Permutes the season's weekend list in place; the weekend objects and the season's name
map are unchanged. Undo: `reorder` with the `before` order of the future rounds. `"unsafe": true`
(direct write to the owner's list).

### `season.calendar.set_round`
Args: `{ "round": int, "weekend": string, "verified"?: bool, "track_stat_id"?: int }`. Replaces a
future round's weekend:
removes it with the game's own removal sequence (`Career_Teardown_Weekend`, `Ne_Detach_ChildNode`,
erase, the weekend's destructor, free through the season allocator) and builds the new one with
`Career_Create_SeasonWeekend` (with the track id above) + `Career_Add_SeasonWeekend`, then moves it
into the same round. If the game refuses the new weekend, the old one is rebuilt in place with its own
track id and the op returns `game_refused`. Undo: `set_round` back to the `before` name (its track id is
in the log line).

### `season.calendar.add_round`
Args: `{ "weekend": string, "at"?: int (default: after the last round), "verified"?: bool,
"track_stat_id"?: int }`. Inserts a new round at `at` (1-based; must be after the current round). Only
while the season has fewer rounds than `race_weekends` (else `round_count`). Undo: `remove_round` at `at`.

### `season.calendar.remove_round`
Args: `{ "round": int }`. Removes a future, not-started round. Only while the season has more rounds
than `race_weekends` (else `round_count`). Undo: `add_round` with the removed
weekend name and `at` = `round` (the name stays known while another season has it; otherwise pass
`verified` and the `track_stat_id` from the log line).

Risks for these four: the race dates stay with the game's race-weekend activities (see "Round
count"), so a moved or replaced round races on the date of the round it lands on. The transfer-window
round and per-round weather need the in-game check.

### `season.calendar.repair`
Args: `{ "weekends"?: [{ "weekend": string, "verified"?: bool, "track_stat_id"?: int }] }`. Brings the
round count back to `race_weekends`:
- more rounds: removes the last rounds after the current one that have not started, from the end
  (`not_future` when too few of them have not started);
- fewer rounds: appends the missing rounds after the last one. With `weekends` (exactly the missing
  count, each checked like `add_round`), those; without, weekends of this championship's other seasons
  (the previous seasons first, latest first, then later ones, each in round order) whose name and track
  are not on this calendar, with that weekend's name and track id (`unknown_track` when too few).
- equal: nothing (`ok`, `before` = `after`).

`round_count` when `race_weekends` is unreadable. Result `before`/`after` as for the other calendar ops.
Undo: backup only (the History tab's save backups).

Log line: `[career-editor] season.calendar.repair season <n> <rounds> rounds -> <race weekends> (race
weekends on the game's calendar): removed round <r> "<name>"; added "<name>" (track <t>) as round <r>;
... <before> -> <after>`.

### `season.calendar.toggle_sprint`
Args: `{ "round": int, "sprint": bool }`. **`unsupported` live.** A loaded career keeps only weekend
names; the weekend format (`default` / `sprint`) comes from the career calendar's gamedb track list when
a weekend is built. What a DB preset would do: set `weekend_format` on that event's entry in the track
list the career uses (`schedule.track_lists.track_event_list_N`, picked by the season-length row's
`track_list_name`, e.g. `F1_2025_full`) to `sprint` or `default`, value only, no rows added. It applies
to careers started after the change, and to weekends the game builds after it. Not shipped as a preset
yet: those entries sit in tables the pristine gamedb dump shows only by hash, so the row path still has
to be resolved.

### `season.points.adjust`
Args: `{ "driver": int (driver stat id, or the standings `key`), "delta": int (-500..500, ≠ 0) }`.
`"team"` is refused (`unsupported`): the game's list is per driver, and the constructors' table sums
its drivers' adjustments.

Path: the adjustment is stored in the career's sidecar `careers/<career id>.season.json` (by season
index and driver), then the op runs the game's championship standings rebuild
(`Career_Rebuild_ChampionshipStandings`). Its per-season step (`Career_Rebuild_SeasonStandings`) is
wrapped: the stored adjustments are merged into the game's own extra-points list (a new list from the
game's default allocator, which the callee copies and frees itself; the plugin never touches it again).
The standings model adds that list as one more points entry, so the drivers' and constructors' tables
move together and the game's later recomputes keep it. The rebuild's other list (constructors-side
values at standings+0x210) is passed through untouched. The same wrap re-applies it on every rebuild the game runs (season start,
load). The section re-runs the rebuild once, in a safe state, when a stored adjustment is missing from
the live standings. Without this addon the adjustment disappears; nothing in the save is touched.

Refused with `no_career_id` when `ce::career_id()` is empty, `no_driver` when the driver is not in this
season's entrants. The total stays within -1000..1000.

Result: `before`/`after` = `{ "driver_stat_id", "key", "points", "adjustment", "pending" }` (`points` =
the live championship total). **Pending:** the game skips the whole standings re-init (and drops the
extra lists) while the season's first weekend has no sessions yet, e.g. at a season start before round 1
is set up. The op then stores the adjustment, skips the rebuild and returns `ok` with
`after.pending: true`; it applies at the next rebuild once round 1 has sessions (the section re-runs
it). `game_refused` when the rebuild ran but the total did not move as expected; the adjustment stays
stored. That happens when the driver is missing from round 1's entrants (the list is matched against
them). Undo: `season.points.adjust` with `-delta`.

### `season.points.clear`
Args: `{ "driver"?: int }`. Removes the stored adjustment of one driver, or every adjustment of the
current season, and rebuilds. Result: `before`/`after` = `{ "adjustments": [ { "driver_stat_id", "key",
"points" } ] }`. Undo: `season.points.adjust` with each removed value.

### `season.result.set`
Args: `{ "round": int, "session"?: u32, "driver": u32 key, "pos": int (1..22) }`. **`unsupported`,
phase 3.** Results are saved per session (`results_list`, a 15-bit packed value per entrant matched
through the session's 0x60-byte result entries); that layout is not bound yet. The op will edit that
entry and run the season standings rebuild above.

## Bindings

Every game address and field `season.cpp` uses is a KB row; field offsets come from the generated
structs (`swofty/game/career.hpp`, `core.hpp`) via `offsetof`/`sizeof`, read through a region check.

| kind | names |
|---|---|
| structs (ce-season) | `Championship`, `ChampionshipSeasonEntry`, `Season`, `Weekend`, `Session`, `F1SeasonStandings`, `StandingsRound`, `StandingsSession`, `StandingsResult`, `StandingsPoints`, `NeOptionalVec` |
| structs (other lanes) | `CareerTimeComponent.current_minutes` (economy), `EntrantRecord` (drivers), `NeAllocator` / `NeAllocatorVtbl` (core) |
| vtables | `vt_Career_Championship`, `vt_Career_Season`, `vt_Career_Weekend` |
| globals | `g_Ne_DefaultAllocator` |
| structs (read, round count) | `ScheduleComponent.seasons`, `ScheduleSeason.driver_calendars`, `DriverCalendar.activities`, `CalendarActivity.activity`, `ActivityDef.type` |
| functions (called) | `Career_Rebuild_ChampionshipStandings`, `Career_Create_SeasonWeekend`, `Career_Add_SeasonWeekend`, `Career_Teardown_Weekend`, `Ne_Detach_ChildNode` |
| functions (wrapped) | `Career_Rebuild_SeasonStandings` |
| components helper | `ce::comp::get(Championship)` (CareerEntity.current_championship), `ce::comp::get(CareerTime)`, `ce::comp::get(Schedule)`, `ce::comp::entrants()` |

Constants left in code, none of them a field: the track placeholder `0xff`, the weekend destructor's
vtable slot 0, a game vector's end pointer following its begin (`sizeof(void *)`), and size limits.
