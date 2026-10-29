# teams: team identity, side data, logos and badges, team count

Owner: see `subsystems.tsv`. Prefix `Team`. Scope: what the game needs to load an entirely new team from records alone
(the `entrants.teams` row with all its keys, every side table the game finds by team label), the loaded team record
behind `Sess_Load_Teams`, team colours, team logos and badges per context (the `overrides.uidb` logo arrays, their baked
`.texarray` resources, the registry-path format sites that pick a slice), and what limits the team count. The entrant
roster and identity objects stay with `entrants`; the participant selection pipeline stays with `session`.
Design for the core's team and logo appliers: work/std16/RE-TEAMS.md (std16 worktrees, gitignored).

## The loaded team record {#team-record}
`Sess_Load_Teams` (0x142469440, L2, session lane) is called with (entrants system, set): 0x142483002 passes set 0,
0x142461443 passes set 1. It replaces the set's slot vector at `system+0x2c8+set*0x10` (pair `{allocator +0x2c0,
slots* +0x2c8}`) and walks the `entrants.teams` cursor (`_team_instancecount_`, 358 stock). Every row whose `available`
is true becomes one `TeamRecord` (0x768 bytes, `structs/TeamRecord.h`), stored by `Team_Insert_RecordByStatId`
(0x140cdbd40, L2) at `slots[stat_id]`: a heap vector of 0x10-byte `{allocator, TeamRecord*}` entries that grows to the
stat id. A second row with the same stat id is dropped (the insert returns 0). There is no fixed team array: the team
count is bounded only by the u16 stat id and the row count key.

What the loader needs from a row, and how it fails:
- Strings (`display_name`, `_code`, `_short`, `_super_short`, `timing_tower_texture_path`, `livery_name`,
  `team_base`): absent = empty string. Copied into 0x80-byte holders (longer values are cut at 127 characters).
- Colours (`primary/secondary/tertiary_colour`, `team_wear_*_colour`, `ui_team_colour`): read as
  `*(int32*)(*slot + 8)` **without a null check** (0x14246adc6..0x14246ae86): a row missing any of the 7 colour keys
  crashes the loader. Values are the database's signed ARGB int32 (type 11), stored unchanged.
- Labels resolved to stat ids by `Gdb_Find_RowByLabel`: `nationality` (entrants.nationality, u8), `team_boss`
  (entrants.characters, u8, 0xff when empty), `expression_label` (entrants.expression_label, u16), `animation_set`
  (entrants.animation.driver_animation_sets, u8). Unresolved labels give 0 (the value slot is null-checked).
- `prestige` (optional int, kept when >= 0), `audio_id` (u8), `show_team_badge`, `show_custom_badge_in_UI`,
  `team_colour_valid` (bools, default 1 for the last).
- `story_info`: the nested table `<row>.story_info_N` (`description`, `hashtag`); a missing
  `_story_info_instancecount_` means no story (the record's `has_story` stays 0), not a crash.
- Optional `<row path>.paddockMarker` (string, `Gdb_Lookup_Key`); no stock row has it.
- `available` is false in the file for every team; the startup availability pass (`Sess_Apply_ContentAvailability`
  0x1413e4cf0, L1) sets it at runtime, count-bounded, for teams whose stat id is on a content list (B26f and Custom
  Grid Size teams became available through the entrant list that names them; not traced, L1).

Consumers reach a team as `[slots + stat_id*0x10 + 8]` → `TeamRecord*`, e.g. the logo key format sites
(`mov rdi,[rcx+rax+8]; mov r8,[rdi+0x18]` at 0x1416a3ad9 and 0x141879892): every logo and badge key is built from
`TeamRecord.label`, the row's `_metadata_`.

Runtime team sets: `Ent_EntrantsSystem_AddTeam` (0x142474a60, entrants lane, L1) copies a record body and inserts it
into set 1 (`system+0x2d8`) through `Team_Insert_RecordByStatId` (0x142475392); `Ent_EntrantsSystem_RemoveTeam` and
`_ReplaceTeam` refuse set 0 ("Not allowed to remove preloaded team"). The team key is `{u8 set, u8, u16 stat_id}`.

## When a team is loaded: availability {#team-available}
Every `entrants.teams` row has `available=False` in the file. `Sess_Apply_ContentAvailability` (0x1413e4cf0, L2,
single caller 0x1413ed3d5) collects team keys `{u8 set, u8, u16 stat_id}` from every career setup component's team
(component+0x2e0, 0x1413e9361) and from every `EntrantRecord.team` (+0x58) of the loaded entrant lists (0x1413e945f),
then walks the `entrants.teams` cursor (rows `0.._team_instancecount_-1`) and sets `available=True` on each row whose
`stat_id` matches a collected key with set 0 (0x1413e9fe9). `Sess_Load_Teams` then loads only those rows. A new team
is therefore loaded when an available entrant list has an entrant whose `team` label is the new row's `_metadata_`
(and the row lies below `_team_instancecount_`, which bounds both this pass and `Gdb_Find_RowByLabel`).

## Side data found by team label {#side-data}
Every table below carries the team as a label (an `id` field or the row's own `_metadata_`) that its loader resolves to the
team row's `stat_id` with `Gdb_Find_RowByLabel(entrants.teams)`; the stored key is `stat_id << 16` (set 0). Only teams in the
cursor range `0.._team_instancecount_-1` resolve. `Sess_Load_Teams` gives 0 for an unresolved label; the side loaders were not traced for the same fallback, so treat a
misspelled label as a bug. Levels are L1 unless noted (loader decompiled, field names read).

| table (row key `team` unless noted) | loader | fields read | what a new team needs |
|---|---|---|---|
| `pit_lane.pit_times.team_pit_times_N` | `Team_Load_PitTimes` 0x1434c8bf0 (L2) | min/max_pit_time, min/max_pit_time_fully_upgraded | its own row; entries are `{stat_id << 16, 4 floats}`; a missing row is not traced |
| `bank.initial_team_cash.InitialTeamCash_N` | `Team_Load_BankSettings` 0x140c69430 | cash | its own row (8-byte `{key, cash}` entries) |
| `career.my_team_renown_and_form...initial_renown_and_form.InitialTeamRenownAndForm_N` (row `_metadata_` is cased, `team` is the label) | `Team_Load_RenownAndForm` 0x140f90350 | renown_points, form_points | a row in the data group its career uses (`f1_career_2025`/`2026`) |
| `career.my_team_sponsors...team_primary_sponsors.team_primary_sponsor_N` | `Team_Load_MyTeamSponsors` 0x14110e4d0 | primary_sponsor | a row in the group the career uses (My Team) |
| `recognition.RecognitionDataGroup_N.team_modifiers.TeamModifier_N` | `Team_Load_Recognition` 0x141060240 | ai_weighting, team_comparison_driver, weightings | Driver Career only; stock labels are the `_26` rows |
| `facilities.facilities.Facilities_N.team_developments.TeamDevelopment_N` | `Team_Load_Facilities` 0x140de9ca0 | installed, purchased, shutdown (facility ids) | a row, or the team starts with no facilities |
| `driver_market...team_driver_rating_range_requirements.TeamDriverRatingRangeRequirement_N` | `Team_Load_DriverMarket` 0x140dcdb00 | rating_range | a row per data group (`2025`, `2026`) |
| `f1_world.expected_finish_position.teams.TeamExpectedFinishPosition_N` | `Team_Parse_ExpectedFinishPosition` 0x142730d80 | expected_finish_position | F1 World only |
| `showroom.teams_history.team_history_N` | `Team_Parse_ShowroomHistoryRow` 0x142e0e280 (from `Team_Load_ShowroomData` 0x142e0dd20) | team_name, drivers, history, wins, poles, camera_sequence, audio_vo_name, addon_content | Showroom only |
| `story_mode.StoryModeSaga_N.team_performance_upgrades.TeamPerformanceUpgrades_N` | `Team_Load_StoryPerformanceUpgrades` 0x14134d190 | bhp_adjustment, mass_adjustment | Story mode only |
| `animation.team_data.teamData_N` | `Team_Load_AnimationTeamData` 0x14022adf0 | team_boss (character), vehicle package flags | keyed by VEHICLE label (`ferrari_2024`), not by team: a new car needs its own row |
| `entrants.standings.standings_N.team_reference_N` | not located | team (an id like `redbull_24`) | story-mode standings sets; the career standings are built from the entrants (see `career.md` inbox) |
| `entrants.team_customisation.team_customisation_for_year_N.team_customisation_N` | `Wear_Load_TeamCustomisations` 0x142485190 (wearables lane) | suit, gloves, `linked_team` (a team label), `official_driver_customisations` | see work/std16/RE-WEARABLES.md |
| `entrants.entrant_specialisation...specialisation_team_reference_N`, `resource_points.data_group_N.teams.team_N`, `beta.team_colours.teamColours_N`, `f1_world.fan_zone.fz_choices_stings` | not located | team / sting_team labels | not needed for a career season; `fz_choices_stings` only for the Fan Zone |

The row counts of the tables above are stock (10 to 13 teams); every table has its own `_<row>_instancecount_` key the loader
reads first, so a row added without bumping the count is never read.

## Logos and badges {#logos}
Every logo is a registry path `#overrides.<array>.<key>` that the UI resolves with `Vm_Resolve_RegistryNode`
(0x1442b1b60). The key is either the team row label (`TeamRecord.label`, +0x18) or `timing_tower_texture_path` (+0x4d8,
only the two race-HUD arrays). The array is a BC7 texture array (`<array>.texarray` view + `.texarray.image` pixels) whose
slice order is the order of the names listed under `overrides.uidb` `overrides.<array>`; a key missing from that list does
not resolve. Contexts, with the formatting site and the build-time carriers (Classic Seasons' `erp/logos.json`):

| context | array | format site | key |
|---|---|---|---|
| Driver Select (Choose Driver details) | `teams.driver_select_team_logos` | `site_Game_DriverSelect_TeamLogoFormat` 0x1416a3aee (L2, EA-verified) | label |
| Career Team Select, team lists | `team_logos.driver_career_2024_team_select` | `site_Game_TeamSelect_TeamLogoFormat` 0x1418798a5 (L2, EA-verified); `Team_Build_TeamListItem`, `Team_Build_AcademySelect` | label |
| Team HQ, R&D, laptop, contracts | `teams.driver_select_team_logos` | `Team_Set_TeamBadgeLogo`, `Team_Build_RnDTeamEntry`, `Team_Build_LobbyTeamLogo` (L1) | label |
| GP/TT team select tiles | `team_logos.<entrant list ui_path>` (`f1_2023`, `f1_2021`, ...) and `_showroom` | `Team_Build_TeamSelectTiles` (L1) | label |
| standings, results, post race, press interview, player card, Fan Zone | `teams.badges_broadcast` | `Team_Format_BadgeBroadcastKey` and the `Team_Build_*` L1 rows | label |
| pre-race grid rundown | `broadcast_ui_generic.grid_team_logos` | not located | label |
| race HUD timing tower | `osd.timing_tower.team_badges_hud` | `Team_Set_TimingPillBadge` (L2); label load `site_Team_TimingPillBadgeLabel` | `timing_tower_texture_path` |
| broadcast timing tower, session.cars | `osd.timing_tower.team_badges_broadcast` | `Team_Build_SessionCars` (L2); `site_Team_SessionCarBadgeLabel` | `timing_tower_texture_path` |
| multiplayer team select | `teams.mp_team_select_logos` | not located | label |
| Driver Career recognition perk | `driver_career_rewards.recognition.team_icons` | `Team_Build_RecognitionTeamIcon` (L1) | 2023 names |

Image source routes found:
1. **Array slice** (proven by Classic Seasons, `tools/logos.py`): append the key to the `overrides.<array>` name list in
   `ui_package/databases.erp` `overrides.uidb` (`patch-pkg` `clone-entry`, `Value` = a texture path), grow the array's
   fragment 0 slice count (+28) and the view's count (+0x14), append the BC7 slice (same size as the array's, mip count 1),
   and `put-resource` both resources into every `.erp` that carries the array. The carriers differ per array (23 archives for
   `teams.driver_select_team_logos`, 8 for `teams.badges_broadcast`).
2. **Texture path**: the string nodes `team_badge_texture`/`team_logo_texture` (timing tower pill, session.cars, lobby, player
   select) take `eaid://ui_package/art/textures/render_badge_<id>` as well as a `#overrides.` path (`Team_Format_RenderBadgePath`,
   46 call sites). The team record's `show_custom_badge_in_UI` (+0x58d) is the flag that picks it on the two race-HUD sites.
3. **Resolver redirect** (Progressive's `progressive_teamlogos.dll`): wrap `Vm_Resolve_RegistryNode`, rewrite
   `#overrides.<array>.<label>` to another existing key. Reuses slices that exist; cannot show a new image.

## Colours {#colours}
`TeamRecord` +0x564..+0x57c: primary, secondary, tertiary, team_wear primary/secondary/tertiary, ui_team_colour, all the
database's signed ARGB int32 (type 11; Mercedes -13249833 is 0xFF35D2D7 as a u32), copied unchanged into the record, no
null check. `team_colour_valid` (+0x760) is read from the row (default 1). Per-label UI colour animations exist too
(`goto_team_colour_%s`, `Team_Build_RnDTeamEntry`): they are authored UI animation names, so a new label has none.

## Team count {#team-count}
No fixed per-team array was found on the career or session paths read: the record slots, the standings points and the
grouping are growing vectors indexed by the u16 stat id or built per call. Limits are the data and the world: the entrant list
(`_entrant_instancecount_`, `max_entrants`, `max_teams`), the world's garages and grid slots (garage index = position in the
team order list, a garage holds 2 cars), 30 AI vehicle names, the vehicle table of 32 with the 27 clamp on session enter, and
a race seat is `team_position < 2`. Custom Grid Size's 11th/12th teams work through exactly these.

## Inbox
- 2026-10-28 19:15 re-teams: re-teams fork C 2026-10-28, team count: GP/TT/career team counts are bounded by data and the world, not by fixed code arrays. Data: the entrant list's _entrant_instancecount_, max_entrants, max_teams (-1 = none), entrants.teams/vehicles instance counts, <=32 entrant lists per setup mode. World: the track layer (track_data.track_layers, chosen by game setup mode + season; f1_26 row 8 serves f1_26/f1a_mp, myteam_26 row 9 serves My Team) decides the garages, grid slots and pit boxes; garage index = position in the team order list with no bound (Sess_Assign_GarageSlotsByTeamOrder 0x1424a8830 -> Sess_Assign_GarageSlotsByPreference reads *(garages+8)[i]), unlisted teams past the garage count get no garage, and a garage holds 2 cars (Sess_Set_GarageSlotEntrant_Thunk ignores slot >= 2). Code limits already known: 30 AI vehicle names (grid_capacity redirect), vehicle table 32 with the 27 clamp on session enter. Session grouping (Sess_Group_EntrantsByTeam_Thunk) and the standings are growing vectors. Details: scratch C-count.md, folded into work/std16/RE-TEAMS.md.
- 2026-10-28 23:56 team-v1: team-v1 2026-10-28: Sess_Load_Teams builds the record key as stat_id<<16 with NO set byte for every set (0x14246c499); its set-0 call (0x142483002) and set-1 call (0x142461443, Ent_EntrantsSystem_Construct) are indistinguishable by key. native/src/core/team_slots.cpp filters on key set == 0, so database_set follows whichever set loaded last and redress fires for both sets. Fix by reading the set at Sess_Load_Teams entry (dl) or comparing rcx with system+0x2c8+set*0x10. Entrants address teams by a word {u8 table 0..2, u16 index}; table 1 = user team set (Team_Set_TimingPillBadge).
- 2026-10-29 00:10 w21c: w21c 2026-10-29: correction to the 2026-10-28 team-v1 inbox note: Sess_Load_Teams only walks the team rows for set 0 (test bpl,bpl at 0x1424695af, jne to 0x14246ca8c for any other set), so site_Team_LoadInsertCall / site_Team_LoadInserted fire for the database set alone and the missing set byte in the key cannot mix the sets. team_slots.cpp no longer filters on a set byte.
