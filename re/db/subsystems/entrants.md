# entrants: team / driver / vehicle identity, grid roster, liveries

Owner: see `subsystems.tsv`. Prefix `Ent`. Scope: who is on the grid and the identity a modder hooks —
the entrant/team/driver/vehicle object model, the grid participant roster and its caps, team & driver
identity (names, numbers, colours, liveries/decals), vehicle-package selection and the engine-supplier
mapping. The participant *selection* pipeline (setup modes, candidate pool, selector, weekend start,
garages) is owned by `session` — this lane owns the identity objects those stages carry.
Prior prose: research/grid24-cap.md.

## The grid participant: EntrantRecord {#entrant-record}
One `entrants.entrant_lists.entrant_list_N` row -> one 0x88 `EntrantRecord`, assembled by
`Sess_Parse_Entrant` (0x142452e60, session lane; proto now recorded). The entrant row has 10 fields
(`_metadata_, badge_customisation, car_number, driver, driver_customisation, entrant_reference, team,
team_position, vehicle, vehicle_customisation`). The parser reads them through a view-model getter
(`(**[vtbl+0x10])(ctx, row, &field_key)`), with the field-name key strings at
0x145612afa `driver`, 0x145612b01 `team`, 0x1456241c7 `vehicle`, 0x14578e38b `driver_customisation`,
0x1456e6ec5 `vehicle_customisation`, 0x14578adca `badge_customisation`, 0x14576d9bc `team_position`,
0x14568dc7f `car_number`.

Resolution: `driver`, `team`, `badge_customisation` are **labels** looked up by
`Gdb_Find_RowByLabel("entrants.drivers" / "entrants.teams" / "entrants.badges", label)` then
`Gdb_Get_FieldInt(row,"stat_id")`; the stat id lands in the high word of a dword (sentinel 0x3fff when
the label is empty or unresolved). The **team branch reads stat_id without checking the found flag**
(0x14245323c) — an unresolved team label crashes (B26b). `vehicle`, `driver_customisation`,
`vehicle_customisation` are packed by small helpers (`Ent_Pack_VehicleRef` 0x1425069b0,
`Ent_Pack_DriverCustomisation` 0x142453880, `Ent_Pack_VehicleCustomisation` 0x1424fc250). A driver-
registry list walk (param_4, matching driver/team/vehicle ids) yields `+0x60` from node `+0x4b0`.

The full struct map is in `structs/EntrantRecord.h`; store instructions 0x1424536be..0x14245374b
(param_1 = rsi). Key offsets: +0x48 ordinal, +0x52 driver stat id, +0x58 team stat id (+0x5a),
+0x5c vehicle ref, +0x64 vehicle_customisation (livery preset), +0x68 badge stat id, +0x80
team_position, +0x83 car_number, +0x4b flags (bit 0x10 = exclude from AI count, set later).

## Team identity {#team}
`Sess_Load_Teams` (0x142469440, session lane) iterates the `entrants.teams` cursor (count =
`_team_instancecount_` = 358 stock), and for each `available` row builds a team record from 25 identity
fields: `display_name/_code/_short/_super_short`, `primary/secondary/tertiary_colour`,
`ui_team_colour`, `team_wear_*_colour`, `team_colour_valid`, `livery_name`, `team_base`, `team_boss`,
`audio_id`, `prestige`, `stat_id`, `show_team_badge`, `show_custom_badge_in_UI`, `animation_set`.
`nationality` and `expression_label` are resolved by label to their own rows' `stat_id`. It then walks
the nested `<team row>.story_info_N` table (`description`, `hashtag`; missing count -> 0 rows). A 13th
team (T12) needs `_team_instancecount_` bumped AND a cloned team row with all 30 keys (incl the
`story_info` subtree) — see research/grid24-cap.md#b26f.

## Vehicle identity, vehicle-package and engine-supplier {#vehicle}
`Sess_Load_VehicleAiConfig` (0x1425027e0, session lane) builds the ~0x1110-byte per-vehicle AI-config
object from an `entrants.vehicles` row (count `_vehicle_instancecount_` = 413 stock). Its identity byte
block is `structs/VehicleIdentity.h`:
- **vehicle_package** (`+0xcd8`): the `vehicle_package` label -> row
  `entrants.vehicles.vehicle_packages.<pkg>` -> `stat_id`. This selects the car's visual/asset package
  (`Game_Read_VehiclePackageRow` 0x142504a90 / `VehiclePackageRow` carries resource paths, decals,
  halo, 2026-aero and equal-performance flags).
- **engine-supplier** (`+0xcd9`): the vehicle `engine` label -> row `entrants.engines.<engine>` ->
  `stat_id`. This is the engine-supplier mapping (vehicles reference a shared engine-maker table).
- **performance_class** (`+0xcda`) / **performance_tier** (`+0xcdb`): read directly. `performance_class`
  is the car-class filter key used by `Sess_Build_CandidatePool` (all 2026 cars are class 0, so the
  pool filter passes everything).

Other identity fields the same loader reads: `display_name`, `chassis_name`, `engine_name_override`,
`asset_filename`, `animation_set`, `safety_car_type`, `drs_capable`, `equal_performance_vehicle`,
`lcd_*`, `hud_dial`, `dial_min/max`, `garage_offset_X/Y`.

## Custom / My-Team setup {#custom-team}
`Ent_Parse_TeamSetupRow` (0x14276fda0, re-homed from career) parses a created/My-Team entry's identity
(helmet, custom_avatar, mods, team) off its setup row (single caller 0x14279d1ac) and holds the TEAM
department-enum resolver + store (`site_Career_TeamDeptEnumStore` 0x142774258, career lane).
`Sess_Parse_CustomTeamConfig` (0x14290bd30, session lane) is the GP custom-team config path.
`game::CustomDecalModelComponent::activate` is referenced only by `Sess_Unk_1427b8db0` (custom livery
decals; not yet decompiled).

## Grid cap structure: the engine live-vehicle table {#grid-cap}
This is the headline question for the 24->26-car goal. Findings (static analysis, KB build d873a1c8):

- **`g_Sess_VehicleCount` (0x146ce86dc) is a u8.** All 147 `.xtls` readers load it as a byte
  (`movzx ... byte`/`cmp byte`). Logical max 255. It is the runtime live-vehicle count.
- **`g_Sess_VehicleTable` (0x146ce86e0) is a fixed-base, statically-allocated inline pointer array**,
  not a heap pointer: every one of the ~170 readers takes the literal base with
  `lea reg, [rip+0x...]` and indexes `mov rax, [reg + id*8]` (id = a u8 vehicle index), after the
  bound check `cmp [count], id; jbe/jae`. Element = pointer to the live vehicle object; `[veh+0x580]`
  reaches its wheel/axle sub-objects (see Sess_TyreRules_OnSectorCrossed).
- **No writer in `.xtls`.** The table and count are populated from `.data1` (Denuvo-protected) bodies,
  so the physical capacity cannot be read statically from `.xtls`, and the BSS tail is densely packed
  with unrelated statics so the "next global" does not bound it. **Physical capacity needs a runtime
  read** (flagged below).
- **Hard code clamp = 27 (0x1b).** `Game_GameModule_OnSessionEnter` clamps the per-vehicle init loop
  to `min(count, 27)` (`site_Ent_SessionEnterVehClamp` 0x14080c7fd, sig
  `entrants.session_enter_veh_clamp`): `movzx ecx,[count]; cmp cl,0x1b; mov eax,0x1b; cmovb eax,ecx`,
  then loops eax times over `g_Sess_VehicleTable`. An identical sibling clamp is at 0x140173d80 (func
  0x1401717b0). Only these two of the 147 readers hardcode a ceiling; the rest scale with the runtime
  count. **27 does not block a 24-26 car grid (26 < 27)**; it would cut the 27th+ car in those two
  loops only.

**Answer (static): fixed array, not dynamic.** The grid is backed by a statically-sized, u8-indexed
pointer array gated by a u8 count. Corroborating the physical size from the AI-cap audit
(research/grid24-cap.md#ai-vehicle-cap): per-vehicle engine resources are sized 32 — `VehicleRecordable`
buffer 0x180 + 32x0x1000, `GeneralAIData` 0x180 + 32x2KiB, and u8 vehicle-index tables are
bound-checked against 0x1f (31). So the vehicle table's physical capacity is almost certainly **32**,
with a 27 iteration clamp on session enter. For 24-26 cars the table (>=27/32) is **not** the blocker.

### The real >=26-car blockers (not this table)
1. **AI name/recordable-id table = 30** (`g_Sess_AiVehicleNameTable`, `site_Sess_AiVehicleRegisterCall`)
   — crashes at 30 cars; already fixed by the `grid_capacity.dll` redirect mixin (session lane).
2. **Per-vehicle robin_hood map resize at 26 distinct indices** (`Sess_Veh_UpdateWheelMap` 0x142e90560,
   B26h race-load abort) — the 32->64 rehash free fails; still needs the decisive no-loader runtime
   test (H1 tamper vs H2/H3 real). This is the current ceiling for a 26-car race.

### Runtime-verification needs (this lane)
- Read the physical capacity / allocation of `g_Sess_VehicleTable` at runtime (confirm 32) and the
  live value of `g_Sess_VehicleCount` for a 24/26-car grid.
- Confirm `site_Ent_SessionEnterVehClamp` is hit with the expected count and that nothing past 27 is
  needed for a 26-car grid.
- Trace `Sess_Parse_Entrant` to confirm the EntrantRecord field map (driver/team/vehicle/badge stat
  ids, car_number +0x83, team_position +0x80) on a real T12/T13 run.

## Inbox
- 2026-10-26 08:48 ce-lateagent: ce-lateagent 2026-10-26: EntrantRecord +0x08..+0x47 is the id block Ne_Make_RandomIdBlock (0x1441207d0) builds (rec+0x08 kind 1, +0x10/+0x18 random u64 A, +0x20/+0x28 B, +0x30/+0x38 C, +0x40 u16 0); the parser (0x142452e60) sets +0x48 to its running index (post-incremented counter) and +0x82 to 0xff; Career_Set_EntrantData (0x148618ee0) also writes +0x82 = 0xff. Sess_Match_EntrantIdentity_Thunk (0x1431146d0) matches AI rows by the +0x48 dword alone. Owner please raise name_block/unk_0x82 (decomp:re/decomp/1441207d0.c;decomp:re/decomp/148618ee0.c).
- 2026-10-26 11:13 ce-fa-look: EntrantRecord +0x54 (driver_customisation) holds the stat_id of the entrants.driver_customisations look row in the high word, low word 0 (pool rows of a new 2025 Driver Career: free agent 168 0x00e50000 = look 229; Driver Manager dm_208_look stat 366 packed as 0x016e0000), 0x3fff0000 for none. Observed from live rows plus T008 run 2 (L1, Ent_Pack_DriverCustomisation not read). A seated driver whose row has 0x3fff0000 hangs the next session load (T008 run 1: d3d12core fence spin on the loading screen); with his look packed the session loads (run 2).
- 2026-10-28 19:15 re-wear: Ent_Pack_DriverCustomisation (0x142453880) takes the label in rdx (mov rdi,rdx at 0x1424538a5): proto is (uint32_t *out, const char *label). It searches the label in every entrants.driver_customisations folder's preloaded_driver_customisation_properties and on a hit writes domain 0 / stat_id (Gdb_Get_FieldInt 'stat_id', 0x142453be2..0x142453c0f), on a miss 0x3fff0000. Ghidra drops the found path as unreachable. Also called by Wear_Load_TeamCustomisations for official_driver_customisations (0x14248684a). See subsystems/wearables.md#looks.
- 2026-10-28 19:11 re-teams: re-teams 2026-10-28: the team record Ent_EntrantsSystem_AddTeam (0x142474a60) copies is structs/TeamRecord.h (0x768, L2, teams lane); it inserts into team set 1 (system+0x2d8) through Team_Insert_RecordByStatId (0x140cdbd40, call 0x142475392); ReplaceTeam calls it at 0x1424733fd. Set 0 (system+0x2c8) is the database set Sess_Load_Teams fills; RemoveTeam/ReplaceTeam refuse it ('preloaded'). Team keys are {u8 set, u8, u16 stat_id} (RemoveTeam: param>>16 = stat id, param&0xff = set). Owner please raise the AddTeam/RemoveTeam/ReplaceTeam rows (decomp:re/decomp/142474a60.c, 1424720e0.c, 1424726f0.c).
