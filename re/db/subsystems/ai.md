# ai: driver / team race AI (difficulty, pace, overtaking, defending, racing line, strategy, mistakes)

Owner: see `subsystems.tsv` (prefix `Ai`). Scope: the on-track AI a modder would hook — how the AI
races (pace/consistency, overtaking & defending, racing-line/corner logic), its deployment strategy
(ERS/battery), its mistakes/spins/retirements, and the global AI tuning. All addresses are VAs,
image base `0x140000000`, build `d873a1c8…` (Steam). Code is `.xtls`.

The AI R&D/economy side (team upgrade spend, resource-point AI rates, difficulty mass-adjustment
scalar) lives in the **career** lane (`Career_Load_RnDTweakables` 0x14353cea0,
`Career_Load_ResourceRateConfig` 0x14122ada0, `RnDDepartmentConfig`); the AI-vehicle *registration*
and the per-vehicle AI-config load path live in the **session** lane (see cross-refs). This lane owns
the race-craft behaviour config + the on-track consumer code.

## Two data surfaces a modder tunes {#surfaces}

1. **Per-vehicle race-craft config** — `entrants.vehicles.vehicle_N.ai_vehicle_config.*` (221 float
   keys, 30 groups below). Read by NAME (no code change to add/retune) via `Gdb_Get_FieldInt`
   (gamedb 0x1445a2cf0) in `Sess_Parse_AiHandlingConfig` (session, 0x1406828a0) → flat node-ptr
   table `AiHandlingConfig` (see `structs/AiHandlingConfig.h`). This is where overtaking, defending,
   drafting, ERS strategy, mistakes, spins, retirement, race starts etc. are tuned per car.
2. **Global AI settings** — `ai.general_ai_settings.*` (awareness_time, max_test_time/radius/speed/
   distance). Resolved once by `Ai_Resolve_GeneralSettings` (0x1405fb920) into the static cache
   `g_Ai_GeneralSettings` (0x146046c50) and read across the whole AI driving cluster. These are the
   AI perception / look-ahead (awareness) + spatial-test (raycast radius/speed/distance) parameters.

Note: these are the *data* knobs. The numeric **AI difficulty slider** (0–110) is applied elsewhere
(not located in this pass — see "needs trace"); it almost certainly scales inputs to the controllers
below rather than editing these per-car base values.

## Functions {#functions}

| role | function | addr | level | sig |
|---|---|---|---|---|
| resolve the 5 `ai.general_ai_settings.*` nodes | `Ai_Resolve_GeneralSettings` | 0x1405fb920 | L2 | ai.general_settings_resolve |
| parse the 2 merge-rate heat maps (8 floats each) | `Ai_Parse_MergeHeatMap` | 0x140682580 | L2 | ai.merge_heatmap_parse |
| large per-vehicle AI driver update (reads all 5 settings) | `Ai_Update_VehicleDriver` | 0x140614d70 | L1 | - |
| AI look-ahead graph/curve evaluator (clamped by settings) | `Ai_Eval_LookaheadGraph` | 0x140681140 | L1 | - |
| AI update dispatch (tail-jumps into Ai_Update_VehicleDriver) | `Ai_Unk_1405f5c50` | 0x1405f5c50 | L1 | - |
| AI driver-controller helpers (read settings; roles TBD) | `Ai_Unk_1406427e0` / `140650f90` / `1406807b0` / `140662de0` / `140665e80` / `1406663d0` | — | L1 | - |

`Ai_Resolve_GeneralSettings` (0x1405fb920) {#general-settings}: five absl flat-hash lookups in the
global game-value DB (`_DAT_146cc4278`) keyed by the five fnv1a64 hashes, storing the node ptrs into
`out[0..4]` = awareness_time, max_test_distance, max_test_radius, max_test_speed, max_test_time.
Result cached in `g_Ai_GeneralSettings` behind a C++11 static guard (`g_Ai_GeneralSettingsInitGuard`,
0x146046c78). ~39 call sites across the AI cluster (VA 0x1405f0000–0x140690000) — i.e. this is the
hot entry to the on-track AI brain. Hook it to override global AI awareness/test parameters.
db_keys readers: 0x1405fb920 and 0x140614d70 (gamedb lane rows).

`Ai_Parse_MergeHeatMap` (0x140682580) {#merge-heatmap}: `which=1` fills
`merge_rates.steering_heat_map.*`, `which=0` fills `merge_rates.vehicle_heat_map.*` (8 floats each:
blocking start/end, drafting-human/ai, side-swipe, celebration, space-seeking, max merge rate).
Called twice by `Sess_Parse_AiHandlingConfig` into `AiHandlingConfig` slots 0x77 / 0x7f.

The AI driving cluster (VA 0x1405f0000–0x140690000) is **dark code**: no log/pretty-function
strings, so functions are named only by decompile + data flow (the general-settings hashes and the
`ai_vehicle_config` key reads). The 20 distinct callers of `Ai_Resolve_GeneralSettings` are the AI
controller surface; 7 are recorded above, the rest are L1 seeds for future passes:
0x14064d6b0, 0x14064f4f0, 0x140653ec0, 0x14065f080, 0x1406602a0, 0x1406632d0, 0x140667f40,
0x140669ae0 (in-cluster), plus out-of-cluster setup callers 0x142f5bb50, 0x1434be750, 0x1434cc6b0.

## Per-vehicle race-craft vocabulary (`ai_vehicle_config.*`) {#vocabulary}

The parser reads 221 keys into 30 top-level groups (slot index = read order). Each entry below is a `<group>.<leaf>` game-DB float key under `entrants.vehicles.vehicle_N.ai_vehicle_config` (per vehicle):

- **allow_to_pass** (16) — letting a faster car by / deciding to be passed (yield geometry, look-ahead, urgency speed factors)
    - allow_to_pass_gap, dist_for_max_slow_down_before_pull_out, dist_for_min_slow_down_before_pull_out, dist_needed_after_racing_line_yield, high_urgently_speed_factor, high_urgently_speed_modifier, low_urgently_speed_factor, low_urgently_speed_modifier, max_look_ahead_dist, min_look_ahead_dist, pull_out_time_to_braking_zone, time_behind_end, time_behind_start, yield_dist_to_corner_mult, yield_pullout_dist, yield_target_lengths
- **blocking** (5) — defending position (block pull-out factor, safe blocking distance, race-start blocking)
    - block_pulled_out_factor, need_to_leave_space_offset, radius_for_sharp_corner_blocking, safe_blocking_distance, safe_blocking_distance_at_race_start
- **buffer_space_settings** (5) — following/car-spacing buffers (follow buffer min/max, track-edge & vehicle buffer)
    - follow_buffer_max_distance, follow_buffer_min_distance, follow_buffer_speed_percentage, track_edge_buffer, vehicle_buffer
- **celebration** (3) — post-finish celebration behaviour (lead time, speed limit)
    - lead_time_needed, speed_limit, speed_limit_fade_out_time
- **doughnut** (10) — post-race doughnut / burnout grip model
    - dot_to_steering, dot_to_throttle, grip_fl, grip_fr, grip_rl, grip_rr, lateral_grip_multiplier_fl, lateral_grip_multiplier_fr, lateral_grip_multiplier_rl, lateral_grip_multiplier_rr
- **damage_settings** (6) — how aero/puncture (burst) damage degrades braking, radius and speed
    - aero_brake_distance_increase_factor, aero_radius_modifier, aero_speed_reduction_factor, burst_brake_distance_increase_factor, burst_radius_modifier, burst_speed_reduction_factor
- **drafting** (20) — slipstream logic vs AI and vs human (start/stop windows, offset, approach/gain thresholds)
    - drafting_a_human.draft_approach_speed_needed, drafting_a_human.draft_end_time, drafting_a_human.draft_max_correction, drafting_a_human.draft_offset_mult, drafting_a_human.draft_potential_speed_gain_needed, drafting_a_human.draft_start_time, drafting_a_human.drafting_a_drafter_start_time, drafting_an_ai.draft_approach_speed_needed, drafting_an_ai.draft_end_time, drafting_an_ai.draft_max_correction, drafting_an_ai.draft_offset_mult, drafting_an_ai.draft_potential_speed_gain_needed, drafting_an_ai.draft_start_time, drafting_an_ai.drafting_a_drafter_start_time, time_to_braking_zone_to_start_drafting, time_to_braking_zone_to_stop_drafting, time_to_move
- **downforce_loss** (2) — dirty-air downforce loss when following (aero reduction, speed removal %)
    - aero_reduction_scalar, speed_removal_percentage
- **ers_use** (26) — ERS/battery deployment STRATEGY: push-to-overtake / defend / close-gap / build-gap battery levels, plus xmode passive/enabled variants and distance/time triggers
    - push_on_in_or_out_lap_battery_level, push_to_build_gap_battery_level, push_to_build_gap_time_to_car_ahead, push_to_build_gap_time_to_car_behind, push_to_close_gap_battery_level, push_to_close_gap_time_to_car_ahead, push_to_defend_battery_level, push_to_defend_speed_differential, push_to_overtake_battery_level, push_to_overtake_distance_to_corner, push_to_overtake_intercept_time_defecit, push_to_overtake_speed_differential, switch_from_medium_to_none_in_race_battery_level, switch_from_none_to_medium_in_race_battery_level, xmode_enabled.push_to_build_gap_battery_level, xmode_enabled.push_to_close_gap_battery_level, xmode_enabled.push_to_defend_battery_level, xmode_enabled.push_to_overtake_battery_level, xmode_enabled.switch_from_medium_to_none_in_race_battery_level, xmode_enabled.switch_from_none_to_medium_in_race_battery_level, xmode_passive.push_to_build_gap_battery_level, xmode_passive.push_to_close_gap_battery_level, xmode_passive.push_to_defend_battery_level, xmode_passive.push_to_overtake_battery_level, xmode_passive.switch_from_medium_to_none_in_race_battery_level, xmode_passive.switch_from_none_to_medium_in_race_battery_level
- **exit_fallibility** (4) — corner-exit MISTAKES: max throttle error by mass/grip (dry/wet), max yaw angle
    - max_throttle_error_by_grip_dry, max_throttle_error_by_grip_wet, max_throttle_error_by_mass, max_yaw_angle_deg
- **formation_lap** (1) — formation-lap parking speed limit
    - parking_speed_limit
- **gearbox_controller** (5) — downshift aggressiveness (grip-to-slip-angle dry/wet, physics auto downshift)
    - aggressive_downshift_grip_to_slip_angle_for_dry_tyres, aggressive_downshift_grip_to_slip_angle_for_wet_tyres, normal_downshift_grip_to_slip_angle_for_dry_tyres, normal_downshift_grip_to_slip_angle_for_wet_tyres, use_physics_auto_gearbox_downshift_speed
- **lost_control** (10) — spin/loss-of-control onset & recovery (spin initiate/continue angles, recovery time)
    - angle, over_rotation_to_spin_time, recovery_time, spin_angle_continue_ratio, spin_angle_intiate_ratio, spin_cancel_time, spin_continue_angle, spin_initiate_angle, spun_angle_to_max_steering_angle_ratio, unspun_angle_to_max_steering_angle_ratio
- **merge_rates** (19) — how fast the AI merges toward a target line/space incl two heat-map tables (vehicle & steering): blocking/drafting/side-swipe/celebration merge rates
    - corner_radius_merge_rage_graph, near_corner_merge_rate_graph, steering_heat_map.blocking_ending_merge_rate, steering_heat_map.blocking_starting_merge_rate, steering_heat_map.celebration_merge_rate, steering_heat_map.max_merge_rate, steering_heat_map.max_merge_rate_for_Side_Swipe, steering_heat_map.max_merge_rate_for_drafting_ai, steering_heat_map.max_merge_rate_for_drafting_human, steering_heat_map.space_seeking_merge_rate, track_edge_proximity_merge_rate_graph, vehicle_heat_map.blocking_ending_merge_rate, vehicle_heat_map.blocking_starting_merge_rate, vehicle_heat_map.celebration_merge_rate, vehicle_heat_map.max_merge_rate, vehicle_heat_map.max_merge_rate_for_Side_Swipe, vehicle_heat_map.max_merge_rate_for_drafting_ai, vehicle_heat_map.max_merge_rate_for_drafting_human, vehicle_heat_map.space_seeking_merge_rate
- **merge_to_racing_line** (9) — returning to the racing line (offsets, merge distance, safety-car pull-out)
    - end_offset, inside_start_offset, max_merge_dist, merge_dist_mult, min_merge_dist, outside_start_offset, rate_of_merge_in_cornor, safty_car_pull_out_merge_dist, safty_car_pull_out_time
- **overtake** (4) — OVERTAKING geometry (divebomb turned-in radius, pit-fork side, side-switch time, speed-difference buffer)
    - dist_from_pit_fork_to_stay_on_correct_side, divebomb_turned_in_radius, side_switch_time_requirement_graph, speed_difference_buffer
- **pedal_controller** (5) — low-level throttle/brake pedal control (lift-and-coast, cruise throttle, brake scalar)
    - lateral_traction_slip_angle_cut_in_proportion, lift_and_coast_thottle, max_throttle_when_cruising, spin_out_ramp_up_time, track_speed_error_brake_scalar
- **physics_settings** (1) — turning-circle speed scalar
    - turning_circle_speed_scalar
- **race_start** (10) — race START/launch (rpm window, rev time, bogus-launch random-to-traction, short-shift scalars)
    - first_gear_short_shift_scalar, max_rpm, min_rpm, poor_traction_max_speed, race_start_time, random_to_traction_scalar_mod, random_to_traction_scalar_mod_wet, rev_time_max, rev_time_min, second_gear_short_shift_scalar
- **retire** (3) — retirement / DNF behaviour (time before looking for a retirement gate, aligning to grid)
    - dist_to_finish_aligning_to_grid, dist_to_start_aligning_to_grid, time_before_looking_for_retirement_gate
- **speed_limit_settings** (4) — imposed speed limits (out-lap, post-race, pre-retirement, puncture)
    - out_lap_distance_to_build_one_second_gap, post_race_speed_limit, pre_retirement_speed_limit, puncture_speed_limit
- **spinning_out** (5) — active spin behaviour (recovery speed, slide angle, stopped-spinning thresholds)
    - angle_to_track_to_mark_as_finished, low_speed_to_mark_as_finished, recovery_speed, slide_angle_component, stopped_spinning_speed
- **steering_controller** (6) — steering slip handling (peak slip cut-in, vehicle-alongside multiplier, half-life)
    - peak_slip_cut_in_angle, peak_slip_cut_in_proportion, peak_slip_mulitplier_vehicle_alongside, peak_slip_multiplier_clear_either_side, peak_slip_smoothing_half_life, time_to_half_rotate_steering_wheel
- **steering_pid_settings** (9) — steering PID gains/limits (P/I/D, smoothing, result limit)
    - derivative_gain, derivative_limit, input_smoothing_half_life, integral_gain, integral_limit, output_smoothing_half_life, proportional_gain, proportional_limit, result_limit
- **swerve** (8) — tyre-warming / defensive SWERVING (min/max time between, speed caps dry/wet, length, width, count)
    - max_num_swerves, max_time_between_swerves, min_num_swerves, min_time_between_swerves, swerve_length, swerve_speed_cap_dry, swerve_speed_cap_wet, swerve_width
- **throttle_pid_settings** (7) — throttle PID gains/limits (P/I/D, result limit)
    - derivative_gain, derivative_limit, integral_gain, integral_limit, proportional_gain, proportional_limit, result_limit
- **traction_control** (4) — AI traction control (grip strength, peak slip factor, throttle fallibility scalar)
    - grip_atc_strength, peak_slip_factor, radius_atc_strength_scalar, throttle_fallibility_scalar
- **trailbraking** (3) — trail-braking model (effectiveness start radius, gradient modifier dry/wet)
    - braking_effectiveness_start_radius, gradient_braking_modifier_dry, gradient_braking_modifier_wet
- **under_safety_car** (5) — behaviour behind the safety car (follow gaps, delta-pace points)
    - delta_pace_high_speed_point, delta_pace_low_speed_point, delta_pace_time_adjustment, large_follow_gap, small_follow_gap
- **yielding** (6) — yielding to faster cars / in pit lane (force/pick side distances, pit slow-down & yield speeds)
    - closing_too_fast_to_yield_speed, distance_to_force_side, distance_to_pick_side, pit_slow_down_distance, pit_start_yield_speed, pit_yield_speed

(The `*_graph` keys — `merge_rates.corner_radius_merge_rage_graph`, `near_corner_merge_rate_graph`,
`track_edge_proximity_merge_rate_graph`, `overtake.side_switch_time_requirement_graph` — are curve
tables evaluated by `Ai_Eval_LookaheadGraph`.)

## Cross-references (other lanes) {#xrefs}
- **session**: `Sess_Build_AiSystem` (0x1405ff750, L2) sets up the AiManager + registers 30 AI
  vehicles (GeneralAIData id 0x3c, per-vehicle 0x3980 buffer) at `site_Sess_AiVehicleRegisterCall`;
  `Sess_Load_VehicleAiConfig` (0x1425027e0) / `Sess_Startup_VehicleAiLoop` (0x142505f30) build the
  ~0x1110-byte per-vehicle AI config (of which `AiHandlingConfig` is a sub-block) keyed by stat_id;
  `Sess_Build_MaxAiParticipantsOption` (0x140bc6f70) is the debug "Max ai participants" slider.
- **career**: AI R&D spend + economy — `Career_Load_RnDTweakables` (recommended-upgrade pick %),
  `Career_Load_ResourceRateConfig` (`AiResourceRateModifiers` vs player), and the
  `difficulty_mass_adjustment_scalar` in `RnDDepartmentConfig` (an AI-vs-player car-mass handicap).
- **game**: `Game_RivalryManager_SetTrace` (0x141261010) / `_2` (0x141261170) — the championship
  rivals system (driver/team rivalry tracking); race-craft-adjacent, left in the game lane.
- **gamedb**: the 5 `ai.general_ai_settings.*` keys are gamedb-lane db_keys rows (readers 0x1405fb920,
  0x140614d70).

## Needs runtime verification (no L3 here yet) {#needs-trace}
- **AI difficulty slider (0–110) application** — NOT located statically. A trace of a race with the
  slider at two extremes (watching the inputs to the steering/throttle PID + `allow_to_pass`/`ers_use`
  reads) would pin where difficulty scales the controllers. Highest-value open item for "difficulty".
- `Ai_Update_VehicleDriver` (0x140614d70) and the 6 `Ai_Unk_*` cluster helpers — precise per-decision
  roles (which reads overtake vs defend vs strategy) need a runtime trace hooking the
  `ai.general_ai_settings` resolve + the `AiHandlingConfig` consumer reads per vehicle per tick.
- Confirm `AiHandlingConfig` slot→offset map (0xe2 slots) by reading a live config object; the float
  values sit at `*(node+8)` of each slot's resolved node.
- Whether retuning `ai_vehicle_config` keys at runtime (vs DB load) takes effect — the parse happens
  once at `Sess_Load_VehicleAiConfig`; a behaviour hook would need to re-run the parse or patch the
  live `AiHandlingConfig` table.

## Inbox
- 2026-10-28 11:55 car-re: car-d (car-re), 2026-10-28: AI-only PHYSICS knobs (all VTF data, no difficulty multiplier found): BrakeECU ASCStrengthForAI (2025 Ferrari 0.0), ASCMaxBrakeAI 0.5, ASCMinYawRatioAI 0.25, ASCMaxYawRatioAI 0.75, BrakeMigrationTorqueAI 450 (Car_Read_BrakeECU 0x1446752f0); tyre SlipRatioMin/MaxLimitAIDegrees and WetnessDownslopeFactorAI/SpeedDownslopeFactorAI (Car_Read_SuspensionSystem 0x1446aa090); Vehicle.FineUpdateRateAI and Vehicle.TyreContactSubStepCycleAI (Game_Read_VehicleConfig 0x14463a800). Scenario rules parser Car_Parse_ScenarioRules 0x14139d600 (L1) carries ai_level + vehicle_mass and distance-based difficulty_at_min/max_distance + vehicle_mass_at_min/max_distance: a scripted AI-level/mass handicap path worth tracing for the difficulty slider question. Notes: subsystems/car.md#ai.
- 2026-10-28 01:28 engine-re: 0x1406b3110 Ai_Handle_PitStateChanged_Thunk L2 candidate (engine-re, decomp re/decomp/1406b3110.c, xref lea 0x1406b2944,0x1406b34ed): for event.state 15/2 finds the AI entry for event.car in self+0x98 (stride 0x28), refreshes +0x14, sets +0x13 tyre-change flag, +0x16=1, +0x17=+0x18=0. Needs ai-re owner to raise.
- 2026-10-25 04:14 vmknolle: vmknolle 2026-10-25 (Steam d873a1c8), AI weather tyre strategy, L1 static (decomp 0x1436a4500, 0x1436a3ab0; callers 0x1436a3b83, 0x1436a67d8, 0x1436a69cb): 0x1436a4500 = predicted track wetness over a look-ahead window (args: laps, lap time, start offset): at time 0 it takes the global track wetness weather+0x3f0a38 ([[sys+0x10]+0x90]+0xf48), later times evaluate the TimeOfDay wetness curve 0x140b12c10(session+0xd48 + slot*0x30, time) and it sums the change. 0x1436a3ab0 = tyre class choice: trend = forecast - current; |trend| <= weather_strategy.wetness_trend_threshold (config +0x80 via sys+0xb8, default 0.01) keeps the tyre; otherwise by current tyre (car+0x9c98-0x6c: <3 slick, 3 inter, 4 wet) and trend sign it moves to a wetter or drier class. Track strategy config parsed by 0x1436ac890 into trackcfg+0x340 (num_look_ahead_laps +0x70, lap_time_scalar clamps +0x50/+0x60). Consequence for mods: the AI only sees ONE track wetness (now) plus the TOD forecast curve, never local wetness.
- 2026-10-28 14:54 re-infra: contributor vmknolle (PRs #448, #450) proposes L2 for Ai_Handle_PitStateChanged_Thunk 0x1406b3110, Ai_Apply_TetherSpeedCap 0x140669790 (renamed from Ai_ScDeltaPace) and site_Ai_ScFollowFlagCmp 0x140642faf. They landed at L1 in this lane with the contributor's evidence and notes kept; evidence is in the rows' evidence/notes columns and verify.tsv V1187 (agree), V1188 (partial), V1189 (agree). Raising them is the ai-re owner's call.
