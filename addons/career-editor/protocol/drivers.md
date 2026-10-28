# Drivers

Owner: drivers (`native/src/drivers.cpp`). Status per op is at the end of each entry.

Every rating read and write goes through the career's `DriverStatisticsComponent` (My Team) or its
subclass `DriverCareerPlayerDriverStatisticsComponent` (Driver Career; same driver table plus the
player's attribute sets). The plugin gets the component from the championship entity's own component
registry (`ce::comp::get`) on every use and checks its driver table before reading. Drivers are looked up by
`driverStatId`, never by position in the table, and every write is read back through the game's own
rating getter for that same `driverStatId`.

## Ids

- `driverStatId` (int): the id the save uses, `(kind << 14) | index`. `kind` is 0 for the normal
  driver pool; other kinds are custom or generated drivers.
- `infoStatId` (int): a statistic, from `driver_statistics_info` (`stat_id`). The five stock ones are
  named `experience`, `racecraft`, `awareness`, `pace`, `focus`. The snapshot lists every statistic the
  component holds, so mods that add statistics show up too.

## How a rating is made (what the editor writes)

The game's getter (`Career_Get_DriverStatValue`, component vtable +0x280) computes:

`value = round(clamp(base(x) + dynamicLevel + temporaryBoost + perks + specialist + (team attribute - 1)))`

- `base(x)` is the driver's evolution curve for that statistic, a Catmull-Rom curve over normalised
  points, evaluated at `x` = career progress (career clock / career length, both in minutes, 0..1) and scaled to
  `miny..maxy` (0..100). The curve is `patchedEvolutionGraph` when the career has an applied ratings
  patch (`last_patch_applied_checksum` > 1 and the gamedb flag `use_story_mode_data` is 0) and that
  statistic has one, otherwise `evolutionGraph`. With live updates off the game forces the applied
  checksum to 1, so the base curve is used. The op never writes these flags.
- Dynamic statistics (`isDynamicStat`) have no curve; their base is 0 and the value is `dynamicLevel`.
- The team attribute term (`MyTeamAttributes` value for the driver's team, minus 1) applies to
  non-dynamic statistics of drivers who hold a team seat.
- Clamp bounds come from the component (vtable +0x268 / +0x270).
- The op computes every non-curve term as the game getter's raw value minus the curve value, so the
  team attribute term is included, and it reads the result back through the getter.
- The Driver Career player: `DriverCareerPlayerDriverStatisticsComponent` overrides vtable +0x280
  (0x14113e260). The override takes the value above unclamped and unrounded for each driver in its
  0xf8-byte list at +0x458 (driver key at +8), turns it into points through the 8-byte table at +0x3c8
  (one entry per rating, linear between entries), adds that driver's points for the statistic (a map
  in the list entry, float at +4 of its 0x18-byte slot), clamps the sum to the entries of the rating
  bounds (+0x3a8 / +0x3ac) and turns it back into a rating. Drivers outside the list get the value
  above. In a reported career, raising the player's experience curve value by 27 raised his
  experience by 19, and a curve value of 14 still read 51.

`drivers.ratings.set` edits the curve the getter is using right now (or `dynamicLevel` for a dynamic
statistic): it rewrites the curve's point values in place, keeps the point count and x positions,
and rebuilds the curve's 65-entry lookup table and cached end value with the game's own curve
evaluator (`Career_Eval_Curve`), exactly as the game's curve loader (`Career_Parse_CurveString`) does. When the read-back after that write is more than 0.5 from
the target (the Driver Career player), the op bisects the edit with the getter: the flat curve value
(0..1), the shift (-1..1) or the `dynamicLevel` (0..100), every set written at each step, until the
value is within 0.5 of the target. Nothing else changes: no checksum, no patch state. The save
already stores both curves and `dynamicLevel`, so the edit persists with the next save. A later EA
ratings patch replaces it only if the player accepts that patch.

## Snapshot section `drivers`

```json
{
  "available": true,
  "reason": "",
  "component": "DriverStatisticsComponent",
  "patch": { "appliedChecksum": 0, "declinedChecksum": 0, "usesPatchedCurves": false },
  "stats": [
    { "infoStatId": 4, "name": "pace", "weight": 0.571, "default": 25.0 }
  ],
  "drivers": [
    {
      "driverStatId": 9,
      "name": "max_verstappen",
      "initials": "VER",
      "number": 1,
      "raceNumber": 3,
      "team": { "teamStatId": 478, "name": "red_bull" },
      "seat": { "hasCar": true, "position": 0 },
      "onGrid": true,
      "player": false,
      "contract": { "teamStatId": 478, "active": true },
      "overall": 95,
      "ratings": {
        "pace": { "value": 96, "base": 95.8, "curve": "base" }
      }
    }
  ],
  "otherDrivers": [
    { "driverStatId": 120, "name": "some_f2_driver", "initials": "SFD", "overall": 71, "values": [60, 70, 65, 72, 80] }
  ],
  "progress": 0.31,
  "contracts": null,
  "recordedMoves": [],
  "pendingSwaps": []
}
```

- `available` false means there is no career, or the component is missing: `reason` says which, and
  nothing else is in the section.
- `drivers` lists the drivers in the championship line-up (every driver with an entrant row: race
  seats and reserves). `otherDrivers` lists the rest of the career's statistics table (free agents,
  F2 and other drivers) compactly: `values` holds the shown value of each statistic in `stats` order
  (null when the driver has none). `drivers.ratings.set` accepts any driver from either list, and
  `drivers.freeAgent.add` gives a driver of `otherDrivers` an entrant row in Driver Career.
- `seat.hasCar` is false for a reserve; `seat.position` is the seat order in the team (null when
  unset). `team` is null for a row with no team. `onGrid` is `seat.hasCar`; `player` is the human
  entrant.
- `contract` is Driver Career only: the driver's `DriverContract` object in
  `DriverCareerContractsManagerComponent`'s contract list, `teamStatId` from its record (+0x00, null
  for no team) and `active` from its active byte (+0x2c). It is `null` when the game keeps no contract
  object for the driver. The key is absent outside Driver Career.
- `name` (row label, e.g. `max_verstappen`), `initials` and `number` come from the gamedb
  `entrants.drivers` row with that `stat_id`; team `name` from `entrants.teams`; statistic names from
  `driver_statistics_info` (`StatisticInfo_N`), read with the db library's `string_at` / `int_at`.
  Anything that is not 1..63 bytes of printable UTF-8 is `null` (statistic names fall back to
  `stat_<infoStatId>`). `number` is the gamedb default number.
- `raceNumber` is the racing number on the driver's entrant row (`EntrantRecord.car_number`, +0x83),
  the number the car, the grid and the standings show in this career; null when the row has none
  (0xff). `drivers.number.set` changes it; `drivers.swap` gives one to a driver who takes a seat without
  one.
- `ratings` is keyed by statistic name. `value` is what the game shows (the game's own getter,
  clamped and rounded). `curve` is `"patched"`, `"base"` or `"dynamic"` (no curve). `base` is that
  curve's value at `progress`, one decimal (null for dynamic statistics). Op results add
  `dynamicLevel` and `boost` per statistic.
- `overall` is the game's own overall rating (`Career_Get_DriverOverallRating`, the weighted sum the
  contract calculator and Team HQ cards use), rounded.
- `progress` is the curve position the game uses (career clock / career length in minutes, 0..1).
- `contracts` is null outside My Team. It lists `DriverContractsMyTeamComponent`'s records as the
  save stores them. A contract record:

```json
{
  "driverStatId": 12, "oldTeamStatId": 3, "newTeamStatId": 1,
  "salary": 1200000, "signingBonus": 0, "teamPerfMoney": 0, "teamPerfRaceValue": 0,
  "successChance": 0.75, "negotiationBonus": 0, "isCounteroffer": false,
  "reason": 0, "weekendIndex": 5, "careerDayIndex": 140,
  "dayContractSent": 138, "dayContractDecided": 140, "accepted": true, "acknowledged": true,
  "clause": { "stat": 1, "target": 3 }
}
```

  `reason` is the game's `driverMoveReason` enum value (names not mapped yet), `clause` is null when
  the record has no performance clause. The contract length is not part of these records; it is not
  read yet.
- `recordedMoves` is the last 20 entries of the game's own move log (`driverMoveData` in
  `CareerDriverContractRenewalComponent`, saved with the career). It is `null` when the career has no
  renewal component: Driver Career, and every My Team career reported so far. `reason` 0 is a signing, 3 a driver
  benched in his team (the values the game's own signing code writes). `drivers.swap` adds two.
- `pendingSwaps` is always empty: a swap applies immediately.

## Op `drivers.ratings.set`

Args:

```json
{
  "driverStatId": 12,
  "ratings": { "pace": 97, "focus": 80 },
  "mode": "flat",
  "expect": { "pace": 96 }
}
```

- `driverStatId` (int, required): must be in the snapshot's `drivers`.
- `ratings` (object, required, at least one key): statistic name or `"<infoStatId>"` → target value
  (number, 0..100, then clamped to the component's bounds, vtable +0x268 / +0x270). The target is the value the game
  shows (`value`), not the curve value: the plugin subtracts the other terms (dynamic level, boosts,
  modifiers) first.
- `mode` (string, optional, default `"flat"`):
  - `"flat"`: every curve point gets the target, so the rating stays at the target for the rest of
    the career (no further evolution of that statistic).
  - `"shift"`: every curve point moves by the same amount (clamped to 0..1 normalised), so the value
    at the current progress hits the target and the driver's growth or decline keeps its shape.
  - `"restore"`: undo (see Undo).
  Dynamic statistics ignore `mode` (their `dynamicLevel` moves by the difference).
- `expect` (object, optional): statistic → the `value` the launcher showed. If the live value differs
  the op fails with `stale` and changes nothing.
- The player's own driver: allowed. In Driver Career the player's statistics go through the subclass
  getter (vtable +0x280 override, see How a rating is made), so the op searches the curve value or
  level that reads back as the target. A target below the value a zero curve gives (or above the value
  a full curve gives) fails with `out_of_range`, which names the lowest (or highest) value that can be
  set; the op rolls back first, so the player's driver is never left half-edited.
- Safety: before writing, the op records every other driver's value for the touched statistics; after
  writing, any change on another driver rolls the edit back and fails with `write_failed`. Every
  statistic set of the driver is written (the default set and each context-aware set), so the value
  is the same in every race context.

Result `before` / `after`:

```json
{ "driverStatId": 12, "ratings": { "pace": { "value": 96, "base": 96.0, "dynamicLevel": 0.0, "curve": "base" } }, "overall": 92 }
```

Errors (`error` is `"<code>: <detail>"`): `no_component`, `bad_args`, `unknown_driver`,
`unknown_stat`, `bad_value`, `stale`, `out_of_range` (no curve value or level reads back as the
target; the detail names the nearest value that can be set), `write_failed` (the curve is empty or not
writable, the read-back did not show the target, or another driver's value moved). Every write of the
op is rolled back first.

Undo: yes. Undo is `drivers.ratings.set` with the `before` values and `"mode": "restore"`, which writes
back the exact point values from before the edit (the plugin keeps the old points of the last 64 curve
edits in memory; without a match, for example after the game restarted, `restore` writes a flat curve
at the old value instead). For dynamic statistics undo moves `dynamicLevel` back by the difference.

Modes: My Team and Driver Career. Runs only while `ce::safe()`.

Status: implemented; needs in-game proof (log line `[career-editor] drivers.ratings.set ...`).

## Op `drivers.number.set`

Changes a driver's racing number now, in My Team and Driver Career, for AI drivers and the player.

Where the number lives (static RE, 2026-10-25):
- **The entrant row.** `EntrantRecord.car_number` (+0x83) of the championship entity's row is the
  career's number. The save keeps it with the row: the CareerEntity loader (0x1407a00d0) reads the
  rows with `Ent_Deserialise_EntrantVector` (0x142458f20), which restores +0x83 from each packed
  record, and `Ent_Serialise_EntrantRecord` (0x142458890) packs it. The current weekend keeps its own
  copy of the line-up (weekend +0x18 vector object, rows +0x20..+0x28), which sessions are built from.
- **The game's own writers** change both: `Career_DriverNumbers_AssignFree` (0x140c01280) and
  `Career_DriverNumbers_ApplyPlayerNumber` (0x140c00830), both `CarNumberComponent`, call
  `Career_Set_EntrantData_Thunk` with the row's own data and position and the number as its flag
  argument, then store the number at +0x83 of the identity-matching row of the weekend the entity
  registry's vtable slot +0xa8 returns (`Career_Weekend_SetEntrantNumber` 0x140af6800).
- **Uniqueness.** `Career_DriverNumbers_Add` (0x140c01550) builds the list of free numbers from the
  candidates in `[CarNumberComponent+0x290]+0x1f8..+0x200`, minus every number held by a row of the
  championship (all rows, teamless free agents included: the owner registry's vtable +0x1c8 returns
  every row of entity +0x2e0). `Career_DriverNumbers_CheckExisting` (0x140c01c30), which the signings
  and the career setup (0x140c2a870) call, keeps a row's number and only draws a free one for a row
  with 0xff.
- **The gamedb default** (`entrants.drivers` `car_number`, the snapshot's `number`) is not read
  mid-career: the career setup takes the entrant lists' `car_number` into the rows once
  (`Sess_Parse_Entrant`) and draws numbers for rows without one.
- **`DriverNameOverrideComponent`** (vtable 0x145676610, the `driver_number_override` rule:
  `Career_DriverNameOverride_GetNumber` 0x140b9a810) is a display override: number 1 for last
  season's champion, or a per-driver number by game mode. Its only caller is the Choose Driver tile
  builder (0x141bf7980), which shows it instead of the row's number. The op does not touch it.
- **The player's number.** `CarNumberComponent`'s vtable slot +0x40 (0x140c00570) calls
  `Career_DriverNumbers_ApplyPlayerNumber`: when the blueprint flag `player_car_number_replacement`
  (+0x2b1) is set and the number chosen in the player's profile differs from the row's, it writes the
  profile's number back over the player's row (and, with `preloaded_car_number_replacement` +0x2b0,
  gives AI drivers holding it a free number). When that slot runs was not traced, so a number the op
  gives the player may go back to the profile's number then; the in-game check covers it.

Args:

```json
{ "driverStatId": 9, "number": 33, "swap": false, "expect": 1 }
```

- `driverStatId` (int, required): a driver with an entrant row (`drivers`, not `otherDrivers`).
- `number` (int, required): 1..99.
- `swap` (bool, optional, default false): when another row holds the number, that driver takes this
  driver's old number. Without it such a number is refused (`taken`).
- `expect` (int, optional): the `raceNumber` the launcher showed.

The op writes each changed row with `Career_Set_EntrantData_Thunk` (the row's own data and position,
the number as the flag) and then the weekend row's +0x83, as the game's writers do. It checks first
that each AI row is the only row its identity matches (and that there is one human row), and after
writing that every row of the table reads back byte-identical except the numbers it set; otherwise it
writes the old numbers back and fails with `write_failed`. The rules are `numbers::plan`
(`native/src/race_numbers.cpp`, host-tested in `native/test/race_numbers_test.cpp`).

Result `before` / `after`:

```json
{ "driver": { "driverStatId": 9, "raceNumber": 33 }, "swappedWith": { "driverStatId": 12, "raceNumber": 1 } }
```

`swappedWith` only when the op swapped. Errors: `bad_args`, `bad_value`, `no_entrant`, `taken`
(names the holder's `driverStatId`), `stale`, `busy`, `refused`, `no_component`, `write_failed`.

Undo: `drivers.number.set` with the old number (`swap` when the edit swapped, so the other driver gets
his number back). A driver who had no number before has no undo.

Log line: `[career-editor] drivers.number.set driver <id> #<old> -> #<new>[, driver <id> #<new> -> #<old>] (Career_Set_EntrantData_Thunk, weekend row updated|not found; <n> other entrants unchanged)`.

Modes: My Team and Driver Career. Runs only while `ce::safe()`.

Status: implemented; needs in-game proof. Test: give an AI race driver a free number in Team HQ, check
it on the Team HQ grid and standings, save and reload, check again, then load a session and check the
car and the HUD. Repeat with a number another driver has (swap on), and with the player's own number
across a reload.

## Op `drivers.contract.set`

**unsafe**: a raw write of the money fields of one contract record, which the record serialiser
writes straight back to the save. No game code was found that caches a value derived from them, but
the payout readers were not traced. My Team only.

Args:

```json
{ "driverStatId": 12, "list": "decided", "newTeamStatId": 1, "salary": 2500000, "signingBonus": 0, "expect": { "salary": 1200000 } }
```

- `driverStatId` (int, required); `list` (`"decided"` default, or `"pending"`); `newTeamStatId`
  (int, optional) picks the record when the driver has several in that list.
- At least one of `salary`, `signingBonus`, `teamPerfMoney`, `teamPerfRaceValue` (int,
  0..1,000,000,000). Teams, days, clauses and the contract length are not editable.
- `expect` (optional): field → the value the launcher showed.

Result `before` / `after`: the contract record (shape above). Errors: `wrong_mode`, `bad_args`,
`no_contract`, `ambiguous`, `bad_value`, `stale`, `write_failed`. Undo: `drivers.contract.set` with
the `before` values. Runs only while `ce::safe()`.

Status: implemented; needs in-game proof (log line `[career-editor] drivers.contract.set ...`).

## Op `drivers.contracts.repair`

Driver Career only. Every AI race driver (a team and a car) whose `contract` is not active for the
team he races for signs that team's contract through the swap's add-contract step, with the record of
another active contract for the team. It repairs careers where 0.2.5 or older seated a driver without
a contract.

Args: `{}`. Result `before` / `after`:

```json
{ "drivers": [ { "driverStatId": 174, "teamStatId": 482, "contract": { "teamStatId": null, "active": false } } ] }
```

`drivers` lists the race drivers that needed a contract, before and after. The op fails with
`write_failed` (naming each driver and why) when one of them could not sign, for example when the game
keeps no contract object for him; the others stay signed. `wrong_mode` outside Driver Career,
`no_component` without the contracts component. Undo: backup only (the History tab's save backups).

Log line: `[career-editor] drivers.contracts.repair <n> race driver(s) without a contract for their team, <k> signed: <id> (team <t>) signed, ...`.

Status: implemented; needs in-game proof.

## Op `drivers.swap`

Swaps seats **now**, mid-season, with the game's own driver-move primitives in the game's own order.

- **My Team** depends on whether the career has a `CareerDriverContractRenewalComponent`:
  - **With one:** for each driver `Career_Record_DriverMove(renewal, driver, oldTeam, newTeam, reason)`
    (move log `driverMoveData` + `MessageDriverMoveConfirmed`), then `Career_Set_EntrantData_Thunk`.
  - **Without one** (every My Team career reported so far; the registry get the game's own renewal
    lookups use returns null): the op follows My Team's own processor, `DriverContractsMyTeamComponent`
    vtable slot +0x188 (`Career_Process_PendingDriverMoves`, 0x140db18e0). That is the slot the
    Driver Career signing fills in its component. The processor posts no per-move message. The op
    copies the line-up, runs `Career_Set_EntrantData_Thunk` for each driver, gives race numbers
    (below), then finalises against that copy, as the processor does (copy at 0x140db2f40, compare at
    0x140db4e1d).
  - Either way, both drivers must have a team (race seat or reserve); a teamless driver is refused
    (`no_team`).
- **Driver Career** has no move log. The op follows the Driver Career signing
  (`DriverCareerContractsManagerComponent`, 0x140d766b0):
  - **Race driver ↔ race driver:** for each driver post `MessageDriverMoveConfirmed`
    (`DriverMoveConfirmedMessage`, reason 0) then `Career_Set_EntrantData_Thunk`; then each signs his
    new seat's contract (below); then race numbers (below); then finalise.
  - **Free agent (teamless entrant row) → race seat**, the game's "replace old driver" sequence:
    1. the free agent: `MessageDriverMoveConfirmed` {driver, old team none, new team = the seat's
       team, reason 0}, then `Career_Set_EntrantData_Thunk` with the seat's team, car, seat value and
       position;
    2. the seat's driver: `Career_Set_DriverContractState(contract, day, 3, registry value)` on his
       contract (found in `DriverCareerContractOwner` by `DriverContract.driver`), then
       `MessageDriverMoveConfirmed` {driver, old team, new team none (0x3fff), reason 1}, then
       `Career_Set_EntrantData_Thunk` with team, car, driver registry value, livery and badge all
       0x3fff0000, seat value 0xffff0012, position 0. He is now a free agent exactly as the game makes
       one;
    3. the free agent signs the seat's contract (below);
    4. race numbers (below), then finalise.
  - **Seat contract**, the signing's add-contract step (0x140d77a70..0x140d77cbb), for every driver
    who takes a race seat: the offer is the 0x2c-byte record of the outgoing driver's contract while
    it is active for the seat's team, otherwise of another active contract for that team, with
    `signed_day` (+0x0c) set to today. The driver's own contract ends with
    `Career_Set_DriverContractState` (state 4 when it is for the same team, 5 for another; nothing
    happens when it is not active); the offer is copied into his contract, `active` (+0x2c) is set,
    and `MessageDriverContractChanged` (`DriverContractChangedMessage`: driver, the record before, the
    record after, the offer's `kind` byte) is posted. Its listeners include
    `DriverStatisticsComponent`. A driver who takes a seat without this step races with no contract
    for that team, so the game's contract-based screens leave the seat out (the dar1us report of
    2026-10-25: VCARB looked one driver short after a 0.2.5 replace). The op plans every signing before
    it writes: a driver the game keeps no contract object for, or a team with no active contract to
    copy, is refused with `no_contract` and nothing is written.
  - **Finalise** (what every signing batch ends with): `Career_Build_LineupChanges` (the current
    line-up vs the line-up copied before the op's first write, in both modes), then post
    `MessageDriverMovesFinalised_upgrades`, `MessageDriverMovesFinalised_recognition` and
    `MessageDriverMovesFinalised` (`DriverMovesFinalisedMessage`). The `_upgrades` handler
    (UpgradesComponent 0x14123b3f0) creates the team modifiers a driver new to a race seat lacks
    (`Career_Init_TeamModifiers`) and re-assigns per-driver R&D. Without it the free agent's R&D is
    costed with no modifier list: the `Career_Calc_UpgradeCost` access violation of 2026-10-20 18:32
    (`mods` = 0, read at +0x62). The game's own signing compares with the weekend's line-up, which is
    the same line-up only for the first batch of a weekend; up to 0.2.5 the op did that too, so a second
    swap in the same weekend announced the first one again (dar1us: 5 changes for one replace) and a
    swap back announced nothing.
  - **Refused in Driver Career:** reserves (a team but no car), `reserve_dc`. Driver Career R&D is
    per race driver (0x14351ccb0 costs only drivers in race positions; the modifier list is created
    only through the finalise path for race-seat changes), and no game path was found that moves a
    reserve this way.
- **Race numbers**, in both modes, after the contracts and before the finalise, where the Driver Career
  signing gives them (0x140d7814e): each of the two drivers whose row now has a team and no number
  (+0x83 0xff) gets one from `Career_DriverNumbers_CheckExisting(CarNumberComponent, driver)`. The
  game's own replace keeps the released driver's number; in a replace the driver numbered is the free
  agent, when he has none. The function draws a free number from the component's candidates
  (`Career_DriverNumbers_AssignFree`, the list `Career_DriverNumbers_Add` builds) and writes it with
  `Career_Set_EntrantData_Thunk` and on the matching entrant row of the current session (entity
  registry vtable +0xa8 returns the Session). The component is the one the game
  passes: `DriverCareerContractsManagerComponent.car_numbers` (+0x368) in Driver Career,
  `DriverContractsMyTeamComponent.car_numbers` (+0x2e8, which `Career_Assign_EntrantTeamSlot` passes
  for a row with 0xff) in My Team, accepted when its `entity` (+0x278) is the championship entity. The
  op then re-reads the line-up: every row must be unchanged apart from those numbers, and each new
  number must be 1..99 and on no other row (`numbers::unnumbered` and `numbers::sole_holder`,
  host-tested in `native/test/race_numbers_test.cpp`). A failed check is logged as a WARNING; the swap
  stays applied and `after` shows the numbers as they are.

What follows the seat: team, car (vehicle keys, so livery and car), seat value, seat position. What
stays with the driver: the row identity (driver appearance, helmet, suit), driver data, ratings,
driver flag. The car number stays with the driver (EntrantRecord.car_number is written from the incoming driver's own value, as the game's own signing does), and a driver who takes a seat without one then gets a free one (Race numbers, above); the seat value (+0x78) comes from the replaced seat.

Args:

```json
{ "a": 12, "b": 40, "expect": { "aTeamStatId": 1, "bTeamStatId": 3 } }
```

- `a`, `b` (int, required): `driverStatId`s. Both need an entrant row (`seat` not null in the
  snapshot). My Team: race seat ↔ race seat on two teams, reserve ↔ race seat (including promoting
  a reserve inside his own team). Driver Career: race seat ↔ race seat, and free agent (no team) ↔
  race seat.
- `expect` (optional): the teams the launcher showed; a mismatch fails with `stale`.
- `clearNumbers` (optional, list of `driverStatId`s): drivers whose race number goes back to none
  after the swap. Only the driver a Driver Career replace releases may be listed; any other driver is
  refused with `bad_args` before anything is written. The number is written the way
  `drivers.number.set` writes one (`Career_Set_EntrantData_Thunk` with 0xff as the number, then the
  weekend row). The launcher's undo of a replace sends it.

Refused: `no_team` (My Team: a driver has no team), `reserve_dc` (Driver Career: a reserve),
`no_contract` (Driver Career: a driver who would take a seat has no contract object, or nobody holds
an active contract with the seat's team to copy),
`player_locked` (either row is the human entrant), `same_team` (two race drivers of one
team, or two reserves of one team), `no_seat` (neither has a car), `no_entrant` (a driver who is not
in the line-up: listed under `otherDrivers`, with no entrant row and so no seat; the error text says
so and points to the `drivers` list. The game's entrant writer only
rewrites existing rows and never adds one; no row-creating path was found in the game's signing
code. In Driver Career `drivers.freeAgent.add` gives such a driver a row first), `refused` (the game has `disableDriverMoves` set), `busy` (the game is mid entrant
update), `no_component` (no championship entity, or no component to move drivers with; the detail
names the mode and the components it looked for), `bad_args`, `stale`.

Checks: before writing, each row must be the only row the game's entrant compare would match (same
human bit and, for AI rows, the same identity word at +0x48); otherwise the op is refused with
`refused` and writes nothing. The game's writer posts its begin message before it searches and only
ends the update when a row matched, so a miss would leave the game stuck mid entrant update. Each
write is checked again right before it runs. After the four calls the op re-reads the entrant table. Both rows must have kept their
identity and address and carry the other's team and car, every other row must be byte-identical, and
every team must race exactly the cars it raced before (same team and car keys, each car on one row:
`lineup::seats_kept`, host-tested in `native/test/lineup_test.cpp`). Otherwise it writes both rows back
to their old data (recording the reverse moves) and fails with `write_failed`; a broken seat count is
named in the error (`team 482 would race 1 car(s) instead of 2`). Contracts are signed only after this
check passes.

Result `before` / `after` (`a` and `b` are the args' drivers; up to 0.2.5 a Driver Career replace
listed the free agent as `a`):

```json
{ "a": { "driverStatId": 12, "teamStatId": 1, "seat": { "vehicle": 65538, "hasCar": true, "position": 0 }, "raceNumber": 4 },
  "b": { "driverStatId": 40, "teamStatId": 3, "seat": { "vehicle": 196610, "hasCar": true, "position": 1 }, "raceNumber": 81 },
  "appliedNow": true }
```

`raceNumber` (from 0.3.1) is the row's racing number, null for none; `after` has the numbers the op
gave or cleared.

Applies immediately (`appliedNow: true`); nothing is queued, so `pendingSwaps` stays empty.

Not changed by the op: My Team contract records (`decided`/`pending` keep the team they were signed
with), the driver's contract held by the renewal component (the game's own signing writes a new
contract there; this op does not), and the driver market's own lists. Driver Career contracts follow
the seat (above). The game's listeners get the driver-move messages; whether the market
re-derives its seat cache from them is what the in-game test has to show (see the test steps).

Undo: `drivers.swap` with the same `a` and `b` (the swap is symmetric; it records two more moves).
The launcher takes each driver's `expect` team from `after` by `driverStatId`. Undoing a Driver Career
replace is a replace the other way: the released driver signs the seat's contract again (the one the
free agent signed, or a teammate's), and the free agent's contract is released. A free agent the
replace gave his first number (`before` team and `raceNumber` null, `after` `raceNumber` set) is
listed in `clearNumbers` and goes back to no number. A race driver the op numbered keeps the
number.

Modes: My Team with a renewal component (move log + message), My Team without one (finalise only)
and Driver Career (message + finalise). Runs only while `ce::safe()`.

Log lines, Driver Career:
`[career-editor] drivers.swap driver <A> [team <X> car 0x<cx> pos <p>] <-> driver <B> [...] -> driver <A> [team <Y> ...], driver <B> [team <X> ...] (Driver Career: 2 moves announced; <N> other entrants unchanged, every team kept its cars, applied now)`
and for a free agent: `... (Driver Career replace: incoming announced, outgoing contract released and announced to no team; ...)` (the free agent is printed first; a teamless row shows `team 16383 car 0x3fff0000 pos -1`), each followed by
`[career-editor] drivers.swap signed <n>/<m> seat contract(s) (MessageDriverContractChanged)`, then for
each driver the op numbered
`[career-editor] drivers.swap race number: driver <id> none -> #<n> (Career_DriverNumbers_CheckExisting; no other entrant holds it)`
(a `WARNING` with the reason instead when the check fails or the component is missing), for each
cleared number
`[career-editor] drivers.swap race number: driver <id> #<n> -> none (clearNumbers, Career_Set_EntrantData_Thunk, weekend row updated|not found)`,
and
`[career-editor] drivers.swap finalised <n> line-up change(s) (MessageDriverMovesFinalised_upgrades, _recognition, MessageDriverMovesFinalised)`.
My Team without a renewal component says `My Team: 2 rows written, moves finalised against the line-up before the swap`, followed by the same finalised line.
My Team with one says `2 moves recorded` and posts no finalise.

Status: implemented; needs in-game proof. Test: swap two AI drivers on different teams in Team HQ,
check the Team HQ grid / standings driver list, save, reload the career, advance to the next weekend
and load into a session. Race numbers: in a fresh Driver Career put free agent 146 (Victor Martins,
no number) into Hadjar's VCARB seat; the snapshot and the race number line must give Martins a number
that no other driver holds, Hadjar keeps his. Save, reload and check again, then undo and check that
Martins has no number and Hadjar has his seat and number back.

### Free agents in My Team (experimental, `experimentalFreeAgent`)

By default the op refuses a teamless driver in My Team (`no_team`). With `"experimentalFreeAgent": true`
(default false) a free agent (a teamless entrant row) takes a race seat, as in a Driver Career replace,
and takes over the seat's contract records. Untested in game; the launcher offers it only on the My Team
**Free agent into a seat** card, marked experimental.

What the game does (static RE, 2026-10-25):
- **No mid-season signing writes seats in My Team.** The only My Team code that writes entrant rows is
  `Career_Process_PendingDriverMoves` (0x140db18e0, `DriverContractsMyTeamComponent` vtable +0x188).
  That slot is the season-change hook in the four components that override it; the Driver Career one
  (0x140d76570) runs the season's queued signings and clears the auctions. The processor writes every
  row teamless and then seats only the drivers named in `pendingDriverMoves` (+0x7d0),
  then `reserve_drivers`.
- **Signings are records.** The contract window (0x140daec40, `my_team_contract_window_opened`) and the
  forced signing (0x140dbd0a0, "Forcing driver %s onto the player's team, as the player didn't sign 2
  drivers") build a 0x70-byte `ContractRecord`, post its messages (0x140db7660: vtables 0x145697010
  and, for the player's team, 0x145697040) and push it onto `pendingDriverMoves` (0x140dabfb0). Nothing
  is seated until the processor runs.
- **The racing number.** `Career_Assign_EntrantTeamSlot` calls `Career_DriverNumbers_CheckExisting`
  (0x140c01c30) when the incoming row's number (+0x83) is 0xff; a row that has a number keeps it.

What the op does with the flag:
1. One driver must be a free agent and the other a race driver (`no_seat` otherwise). The rows are
   written as in a Driver Career replace: the free agent gets the seat's team, car, seat value and
   position, the seat's driver becomes teamless (team, car, registry value, livery and badge 0x3fff0000,
   seat value 0xffff0012). No move message is posted (My Team's processor posts none); the moves are
   finalised against the line-up from before the op, as for other My Team swaps.
2. Every record of the seat's driver with that team in `pendingDriverMoves`, `decidedDriverContracts`
   and `lastSeasonsSignings` gets the free agent's driver key (`ContractRecord.driver`, +0x50), so the
   records name the driver the rows seat and the processor seats him, not the released driver, at the
   season change. Salary, days and clauses stay as signed. `pendingDriverContracts` (negotiations in
   progress) is left alone. The op adds no record: when the seat's driver has no record for next season
   yet, the free agent has none either, as the released driver would not have.
3. Undo is the same op with the same `a`, `b` and the flag: the rows go back and the records are
   renamed back.

Log lines: `... (My Team, experimental free agent signing: 2 rows written, the seat's contract records
taken over, moves finalised against the line-up before the swap; ...)` and
`[career-editor] drivers.swap experimental: driver <free agent> took over <n> contract record(s) of driver <released> for team <t> (pendingDriverMoves, decidedDriverContracts, lastSeasonsSignings)`.

In-game test, on a backed-up My Team save with
`SWOFTY_GAME_LOG_CHANNELS=default,DRIVERMOVES*,DRIVER_NUMBERS` (keep the editor's `snapshot.json`
`drivers` section, with `contracts`, before and after each step):
1. In Team HQ, sign an F2 free agent into an AI team's race seat with **Free agent into a seat**. The
   editor shows him in the seat and the old driver as a free agent; `contracts` shows the moved records
   under his id. Check the Team HQ standings, driver list and driver market.
2. Save, quit to the menu, reload the career. Both rows and records must be as after step 1.
3. Load the next weekend's session: the free agent drives the car (check his number on the HUD).
4. Undo from History on a copy: both drivers and the records go back.
5. Play or simulate to the season change, keeping every `processPendingDriverMoves`,
   `Creating contract for reserve driver` and `DRIVER_NUMBERS` line. After it, check who holds the seat.
   If the free agent lost it, keep the logs: the processor then reads something other than these
   records.
6. Repeat step 1 with the player's own team's second seat, then go through a season change (the
   "Player has %i drivers signed" and forced-purchase lines).

## Op `drivers.freeAgent.add`

Driver Career only. A driver in the career's statistics table with no entrant row (`otherDrivers`, for
example a Driver Manager driver made after the career started) gets a teamless free-agent entrant row
and a `DriverContract` object, laid out as the career setup makes them for a pool free agent. The save
keeps both, and `drivers.swap` can then seat him.

What the game does (static RE, 2026-10-26):
- **Rows.** The game's entrant writer (`Career_Set_EntrantData_Thunk`) only rewrites existing rows. The
  career setup (`Game_CareerSetup_ResetEntrants` 0x140c34960, the line-up builder 0x140c2a870 and the
  CareerEntity loader 0x1407a00d0) posts the entrant begin message (vtable 0x14578d4e8), sets
  `entrant_update_busy` (+0x2f8), fills the entity's entrant vector object and posts the end message
  (vtable 0x14578d510, payload the vector object at entity +0x2d8). The vector object is
  {allocator +0x2d8, begin +0x2e0, end +0x2e8, capacity +0x2f0}; the capacity is an element count.
  0x140bd5100 (the setup's copy into it) grows it through the allocator's vtable +0x18
  (`alloc(allocator, bytes, 8)`), copies each 0x88-byte row and frees the old buffer through +0x38
  (`free(allocator, old begin)`, 0x140bd520d..0x140bd521c).
- **A pool row** (`Sess_Parse_Entrant` 0x142452e60; Driver Manager pool rows have `team_position` -1
  and `car_number` 255): vtable 0x145612e20, the id block +0x08..+0x47 from 0x1441207d0 (kind byte 1,
  then three random 64-bit values, each stored twice at +0x10/+0x18, +0x20/+0x28, +0x30/+0x38, and a
  zero u16 at +0x40), +0x48 the parser's running index (`ordinal_flags`), +0x50 the driver key, +0x54
  the packed driver look (0x3fff0000 without one), team, car, registry value, livery and badge
  0x3fff0000, +0x70 vtable 0x145612ed0, +0x78 0xffff0012, +0x80 0 (no position), +0x82 0xff, +0x83 the
  number (0xff). The game's identity compare (0x1431146d0) matches AI rows by the +0x48 dword alone.
- **Contracts.** The contract rebuild (0x140d78fa0, from `DriverCareerContractsManagerComponent`
  0x140d78980) gives every row of the owner registry's vtable +0x1c8 list (every row of entity +0x2e0)
  a 0x58-byte `DriverContract` (align 0x10) from the contract owner's allocator (+0x210): zeroed, that
  allocator at +0x30 (the history's allocator), the driver key at +0x50. It pushes the slot
  {allocator, contract} onto the owner's slot vector object {allocator +0x290, begin +0x298, end
  +0x2a0, capacity +0x2a8} with 0x140d9dca0 (x1.5 growth through the slot allocator's vtable +0x18,
  old buffer freed through +0x38). The contract loader (0x140d9e020) builds the same object for every
  saved contract and fills the record (+0x00..+0x2b) and `active` only when `has_contract` was saved;
  the saver (0x140d9ecd0) writes the driver, `has_contract`, the record when active, and the history
  of every slot.
- **The rows' save.** `Ent_Deserialise_EntrantVector` (0x142458f20) resizes the vector object to the
  saved count, so an added row loads back. The vector's saver was not found statically.

Args: `{ "driverStatId": 208, "dryRun": false }`: a driver of `otherDrivers` (in the statistics table,
no entrant row). `dryRun` (optional, default false) runs every check, logs the template, and writes
nothing (`after` equals `before`).

Steps:
1. Checks, before anything is written: Driver Career, the championship entity, the contracts manager
   and its contract owner, not mid entrant update, the driver in the statistics table and without an
   entrant row, both vector objects readable (count within the capacity, allocator vtables in the
   image), and the driver's existing contract (if any) inactive.
2. The template: the first untouched pool free agent of the line-up, a teamless AI row with no car,
   registry value, livery or badge (0x3fff0000), seat value 0xffff0012, position 0, +0x82 0xff, no
   number, the line-up's vtables, whose contract is inactive with a zero record (+0x00..+0x2f) and an
   empty history. His contract's +0x30 and its slot's allocator must be the owner's +0x210 allocator
   and his contract's +0x50 his driver key. Without such a driver the op refuses (`refused`).
3. The contract (skipped when the driver already has an inactive one): 0x58 bytes, align 0x10, from
   the owner's +0x210 allocator, zeroed, the allocator at +0x30 and the driver key at +0x50, pushed as
   the slot {allocator, contract}, in place while the capacity allows, otherwise into a buffer of 1.5x
   the count from the slot allocator, the old buffer freed, as 0x140d9dca0 does.
4. The row: the template row with a new id block (as 0x1441207d0 lays it out), `ordinal_flags` = the
   template's high byte and the highest running index of the line-up plus one (refused when another row
   has it), the driver key at +0x50 and the driver's look at +0x54, appended the same way to the entity's
   vector object. The look is the stat_id of the `entrants.driver_customisations` row that his
   `entrants.entrant_lists.entrants` row's `driver_customisation` names, in the high word (look 229 is
   0x00e50000, as every pool row of a new career has it; a Driver Manager driver's row `dm_<id>` names
   `dm_<id>_look`). Without one the op refuses. A seated driver whose row has no look (0x3fff0000) stops
   the next session at its loading screen (T008 run 1).
5. `Career_Set_EntrantData_Thunk` with the new row's identity, its own data, position and number. It
   rewrites the row unchanged and brackets the write with the entrant begin and end messages, the end
   message carrying the vector object, as the setup brackets its rebuild.
6. Verify: the line-up has one more row, every old row is byte-identical, the new row is the last row
   and reads back as written, the contract list has one more contract (none when kept), the driver's
   contract is the new one and reads back as created, and the entity is not mid update. Otherwise the
   op drops the row and the contract again (the contract's memory goes back to its allocator), rewrites
   the template row with its own data so the end message carries the restored line-up, and fails with
   `write_failed`.

Result `before` / `after`:

```json
{ "driverStatId": 208, "seat": { "hasCar": false, "position": null }, "contract": { "teamStatId": 0, "active": false } }
```

`seat` is null without an entrant row; `contract` is null without a contract object. The empty
record's team is 0.

Errors: `bad_args`, `wrong_mode`, `no_component`, `busy`, `unknown_driver` (not in the statistics
table), `already_listed` (the driver has an entrant row), `refused` (no look for the driver, no untouched free agent to copy,
a contract layout other than the rebuild's, an active contract without a row, the line-up full or no
free running index), `write_failed`.

Undo: backup only (the History tab's save backups). No game path removes a row mid-career.

Not set: the current weekend's copy of the line-up (weekend +0x18), which the end message's listeners
or the next weekend rebuild. Test T008 (`re/tests/T008.md`) covers it.

Log lines: first `[career-editor] drivers.freeAgent.add driver <id>: template free agent <id> ordinal_flags 0x<8 hex> driver_customisation 0x<8 hex> contract <0x58 bytes as hex>` (the template's +0x48, +0x54 and contract object as read), then with `dryRun` `... dry run, entrant index <n>, look 0x<8 hex>, contract kept|to create; nothing written`, otherwise
`[career-editor] drivers.freeAgent.add driver <id>: entrant row added [team 16383 car 0x3fff0000 pos -1, entrant index <n>, look 0x<8 hex>, copied from free agent <id>], contract created|kept (inactive, zero record); <n> -> <n+1> rows, <c> -> <c+1> contracts, <n> other rows unchanged (Career_Set_EntrantData_Thunk announced the line-up)`.

Modes: Driver Career. Runs only while `ce::safe()`.

Status: implemented; needs in-game proof (T008).

## Bindings

| KB name | VA | level | use |
|---|---|---|---|
| `Career_Get_DriverOverallRating` | 0x14113d120 | L2, sig `career.get_overall` | `overall` in the snapshot and in op results |
| `Career_Eval_Curve` | 0x1445075d0 | L2, sig `career.eval_curve` | curve value at `progress`, lookup-table rebuild |
| `Career_Get_CareerLengthDays_Thunk` | 0x14111dd30 | L2, sig `career.get_career_length_days_thunk` | career length in minutes, divisor of the clock for `progress` |
| `Career_Get_DriverStatValue` | 0x141140ee0 | L2, no sig | called through the component vtable +0x280 (never by address) |
| `Career_Get_PlayerDriverStatValue` | 0x14113e260 | L1, no sig | the Driver Career player's override of vtable +0x280, called through the vtable (never by address) |
| `Career_Get_DriverStatBase_Thunk` | 0x1411471f0 | L2, no sig | evidence for the curve choice and the dynamic-stat rule |
| `Career_Parse_CurveString` | 0x141157100 | L2, no sig | evidence for the lookup-table rebuild |
| `Career_Serialise_DriverContractList` | 0x140da5610 | L2, no sig | evidence for the contract record layout |
| `Career_Set_EntrantData_Thunk` | 0x1407b72e0 | L2, sig `career.set_entrant_data_thunk` | `drivers.swap`: rewrites one entrant row; `drivers.number.set`: rewrites a row with its new number; `drivers.freeAgent.add`: announces the added row |
| `NeAllocatorVtbl` alloc / free | vtable +0x18 / +0x38 | L2 (struct) | `drivers.freeAgent.add`: the entrant vector's, the contract slots' and the contracts' allocators, called through their vtables |
| `Game_CareerSetup_ResetEntrants`, 0x140bd5100, 0x140d78fa0, 0x140d9dca0, 0x140d9e020, 0x140d9ecd0, 0x1441207d0, 0x1431146d0 | | see the KB | evidence for `drivers.freeAgent.add`; not called |
| `Career_DriverNumbers_CheckExisting` | 0x140c01c30 | L2, sig `career.driver_numbers_check_existing` | `drivers.swap`: gives a swapped driver whose row has a team and no number a free one |
| `Career_DriverNumbers_AssignFree`, `Career_DriverNumbers_ApplyPlayerNumber`, `Career_Weekend_SetEntrantNumber` | 0x140c01280 / 0x140c00830 / 0x140af6800 | L1, no sig | evidence for `drivers.number.set`'s order; not called |
| `Ent_Deserialise_EntrantVector`, `Ent_Serialise_EntrantRecord` | 0x142458f20 / 0x142458890 | L1, no sig | evidence that the save keeps +0x83 |
| `Career_DriverNameOverride_GetNumber` | 0x140b9a810 | L1, no sig | evidence: the Choose Driver display override, not the career's number |
| `Career_Record_DriverMove` | 0x140d8f1f0 | L2, sig `career.record_driver_move` | `drivers.swap` (My Team): move log + driver-move message |
| `vt_Career_MessageDriverMovesFinalised`, `…FinalisedUpgrades`, `…FinalisedRecognition` | 0x145696d10 / d20 / d30 | L2, sigs `career.message_driver_moves_finalised*` | finalise (Driver Career, My Team without a renewal component) |
| `Career_Build_LineupChanges` | 0x142457a90 | L2, sig `career.build_lineup_changes` | finalise payload; reads only begin (+8) and end (+0x10) of both vector objects |
| `Career_Set_DriverContractState` | 0x140d9de30 | L2, sig `career.set_driver_contract_state` | Driver Career replace: release the outgoing contract; seat contract: end the incoming driver's own |
| `vt_Career_MessageDriverContractChanged` | 0x145696e60 | L2, sig `career.message_driver_contract_changed` | seat contract: the contract message, posted with `swofty::career::post` |
| `vt_Career_MessageDriverMoveConfirmed` | 0x145696d40 | L2, sig `career.message_driver_move_confirmed` | `drivers.swap` (Driver Career): the move message, posted with `swofty::career::post` |
| `Career_Sign_DriverToTeam` | 0x140d8fcb0 | L1 (DISPUTE V0294), no sig | evidence for the call order only; not called. Its `offer` is a 0x2c-byte contract record it also writes into the driver's contract, and it first backfills the incoming driver's old seat with that team's reserve (no reserve: it signs nothing) |
| `Career_Process_PendingDriverMoves` | 0x140db18e0 | L1, no sig | evidence, not called: `DriverContractsMyTeamComponent` vtable slot +0x188; the My Team swap without a renewal component follows its order (no per-move message, finalise against its pre-write copy) |
| `Career_Assign_EntrantTeamSlot` | 0x140db70a0 | L2, no sig | evidence (only valid inside the processor: it works on the processor's local seat map) |

KB structs (re/db/structs, all fields L2, raised by ce-drivers): `DriverStatisticsComponent`,
`DriverStatsEntry`, `ContextStatSet`, `DriverStatistic`, `EvolutionCurve`, `CurvePoint`, `StatInfo`,
`DriverContractsMyTeamComponent`, `ContractRecord`, `CareerDriverContractRenewalComponent`,
`DriverMoveRecord`, `EntrantWriteData`, `DriverCareerContractsManagerComponent`,
`DriverMoveConfirmedMessage`, `DriverMovesFinalisedMessage`, `LineupChange`,
`DriverCareerContractOwner`, `DriverContract`, `DriverContractChangedMessage`, and `EntrantRecord`
(extended); `CarNumberComponent` and the `car_numbers` fields of both contracts components (raised by
ce-swap-number). Read from other lanes:
`CareerEntity` (economy), `CareerTimeComponent` (economy).

Snapshot status: implemented; needs in-game proof (no log line; check `snapshot.json`).
