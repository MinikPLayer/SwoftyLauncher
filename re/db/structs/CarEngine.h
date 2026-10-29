// struct: CarEngine
// subsystem: car
// size: 0x25b8  L2 decomp:re/decomp/1447484f0.c;xref:0x144748507 (alloc 0x25b8)
// notes: the Engine system at CarLive+0x530 (Car_Create_EngineSystem 0x1447484f0). Data fields are the embedded
// EngineData object at +0x18 (Car_Read_Engine param = this+0x18, so reader offset = field - 0x18); runtime state from +0x2508.
// CarObfFloat = XOR pair (value = a ^ b). Units: rotation rates rad/s, power W, torque Nm, temperature K.
// Derived fields (+0x2508/+0x2518/+0x2528) come from Init slot +0x30 (0x144687d60 -> 0x1553f5460) and 0x144689a70;
// they read FuelECU active scales, so re-run them after editing RevsPowerSpline/LimiterRotationRate or the fuel mode.
struct CarEngine {
    /* 0x0000 */ void *vtable;                                // L2 decomp:re/decomp/1447484f0.c;xref:0x144748504,0x144748537 system vtable vt_Car_EngineSystem 0x145bfa630 (CarComponentBase)
    /* 0x0008 */ CarLive *car;                                // L2 decomp:re/decomp/1447484f0.c;xref:0x144748528,0x1447486ec owning car
    /* 0x0010 */ void *allocator;                             // L2 decomp:re/decomp/1447484f0.c;xref:0x14474852c car allocator
    /* 0x0018 */ void *data_vtable;                           // L2 decomp:re/decomp/14467e900.c;xref:0x14467e927 EngineData vtable 0x145bfa1b0 {dtor, GetName "Engine", Car_Read_Engine, Car_Write_Engine}; reader offsets = field - 0x18
    /* 0x0020 */ void *data_allocator;                        // L2 decomp:re/decomp/14467e900.c;xref:0x14467e91c allocator (data ctor arg)
    /* 0x0028 */ CarSpline revs_power_spline;                 // L2 decomp:re/decomp/144685680.c;xref:0x1446856a2,0x1446857c8 VTF Engine/RevsPowerSpline power (W) vs rotation rate (rad/s); Ferrari 2025 peak 550060 W at 1191.5 rad/s
    /* 0x02d0 */ void *throttle_maps_allocator;               // L2 decomp:re/decomp/144685680.c;xref:0x1446856d9,0x1446856e0 VTF Engine/ThrottleMap%d vector of CarSpline (stride 0x2a8)
    /* 0x02d8 */ void *throttle_maps_begin;                   // L2 decomp:re/decomp/144685680.c;decomp:re/decomp/144688610.c;xref:0x14468579f,0x1446857f4 ThrottleMap0..N splines (pedal -> throttle) picked by the engine input step 0x144688610
    /* 0x02e0 */ void *throttle_maps_end;                     // L2 decomp:re/decomp/144685680.c;xref:0x1446857a6,0x1446857fb end
    /* 0x02f0 */ void *throttle_maps_clutch_disengaged_allocator; // L2 decomp:re/decomp/144685680.c;xref:0x14468586e VTF Engine/ThrottleMapClutchDisengaged%d vector
    /* 0x02f8 */ void *throttle_maps_clutch_disengaged_begin; // L2 decomp:re/decomp/144685680.c;xref:0x14468590f,0x144685964 begin
    /* 0x0300 */ void *throttle_maps_clutch_disengaged_end;   // L2 decomp:re/decomp/144685680.c;xref:0x144685916,0x14468596b end
    /* 0x0310 */ CarObfFloat mass;                            // L2 decomp:re/decomp/144685680.c;xref:0x1446859de,0x144685a15 VTF Engine/Mass kg, default 100 (ctor 0x14467e900), Ferrari 2025 50 (obf)
    /* 0x0320 */ CarObfFloat inertia;                         // L2 decomp:re/decomp/144685680.c;xref:0x144685a1b,0x144685a52 VTF Engine/Inertia kg m^2, default 0.3, Ferrari 0.1 (obf)
    /* 0x0330 */ CarSpline revs_friction_spline;              // L2 decomp:re/decomp/144685680.c;xref:0x144685a58 VTF Engine/RevsFrictionSpline friction torque vs rad/s
    /* 0x05d8 */ CarSpline revs_engine_braking_spline;        // L2 decomp:re/decomp/144685680.c;xref:0x144685a6e VTF Engine/RevsEngineBrakingSpline engine braking torque vs rad/s (step lerps friction->braking by engine_braking_fraction)
    /* 0x0880 */ CarObfFloat min_idle_rate_factor;            // L2 decomp:re/decomp/144685680.c;xref:0x144685a84,0x144685abb VTF Engine/MinIdleRateFactor default 0.9 (obf)
    /* 0x0890 */ CarObfFloat max_limiter_rate_factor;         // L2 decomp:re/decomp/144685680.c;xref:0x144685ac1,0x144685af8 VTF Engine/MaxLimiterRateFactor default 1.2 (obf)
    /* 0x08a0 */ CarObfFloat idle_rotation_rate;              // L2 decomp:re/decomp/144685680.c;xref:0x144685afe,0x144685b35 VTF Engine/IdleRotationRate rad/s, default 115.19 (1100 rpm), Ferrari 418.9 (4000 rpm); Reset 0x10|0x200 sets rotation_rate to it (obf)
    /* 0x08b0 */ CarObfFloat limiter_rotation_rate;           // L2 decomp:re/decomp/144685680.c;xref:0x144685b3b,0x144685b72 VTF Engine/LimiterRotationRate rad/s, default 890.12 (8500 rpm), Ferrari 1371.8 (13100 rpm); x FuelECU revs scale -> limiter_rotation_rate (obf)
    /* 0x08c0 */ CarObfFloat max_engine_braking_torque_engaged_clutch; // L2 decomp:re/decomp/144685680.c;xref:0x144685b78,0x144685baf VTF Engine/MaxEngineBrakingTorqueEngagedClutch Nm, default 20, Ferrari 25 (obf)
    /* 0x08d0 */ int32_t engine_layout;                       // L2 decomp:re/decomp/144685680.c;xref:0x144685c5c VTF Engine/EngineLayout Inline=2 RearInline=4 Transverse=1 RearTransverse=3 else 0
    /* 0x08d8 */ CarSpline temp_efficiency_spline;            // L2 decomp:re/decomp/144685680.c;xref:0x144685c62 VTF Engine/TempEfficiencySpline power efficiency vs temperature
    /* 0x0b80 */ CarSpline anti_stall_success_spline;         // L2 decomp:re/decomp/144685680.c;xref:0x144685c78 VTF Engine/AntiStallSuccessSpline
    /* 0x0e28 */ CarSpline anti_stall_rumble_spline;          // L2 decomp:re/decomp/144685680.c;xref:0x144685c8e VTF Engine/AntiStallRumbleSpline
    /* 0x10d0 */ CarObfFloat anti_stall_revs_needed_to_exit;  // L2 decomp:re/decomp/144685680.c;xref:0x144685ca4,0x144685cd2 VTF Engine/AntiStallRevsNeededToExit default 1100 (obf)
    /* 0x10e0 */ CarObfFloat anti_stall_revs_low_enough_to_enter; // L2 decomp:re/decomp/144685680.c;xref:0x144685cd8,0x144685d06 VTF Engine/AntiStallRevsLowEnoughToEnter default 320 (obf)
    /* 0x10f0 */ CarObfFloat coef_heat_absorbed;              // L2 decomp:re/decomp/144685680.c;xref:0x144685d0c,0x144685d3a VTF Engine/CoefHeatAbsorbed default 0.93 (obf)
    /* 0x1100 */ CarObfFloat specific_heat_capacity;          // L2 decomp:re/decomp/144685680.c;xref:0x144685d40,0x144685d6e VTF Engine/SpecificHeatCapacity default 5000 (obf)
    /* 0x1110 */ CarObfFloat coef_heat_transfer_constant;     // L2 decomp:re/decomp/144685680.c;xref:0x144685d74,0x144685da2 VTF Engine/CoefHeatTransferConstant (obf)
    /* 0x1120 */ CarObfFloat coef_heat_transfer_linear;       // L2 decomp:re/decomp/144685680.c;xref:0x144685da8,0x144685dd6 VTF Engine/CoefHeatTransferLinear (obf)
    /* 0x1130 */ CarObfFloat coef_heat_transfer_quadratic;    // L2 decomp:re/decomp/144685680.c;xref:0x144685ddc,0x144685e0a VTF Engine/CoefHeatTransferQuadratic (obf)
    /* 0x1140 */ CarObfFloat radiator_surface_area;           // L2 decomp:re/decomp/144685680.c;xref:0x144685e10,0x144685e3e VTF Engine/RadiatorSurfaceArea default 45 (obf)
    /* 0x1150 */ CarObfFloat initial_temperature;             // L2 decomp:re/decomp/144685680.c;xref:0x144685e44,0x144685e72 VTF Engine/InitialTemperature K, default 323.15, Ferrari 383.15; Reset 0x400 sets temperature to it (obf)
    /* 0x1160 */ float radiator_slip_stream_factor;           // L2 decomp:re/decomp/144685680.c;xref:0x144685e78 VTF Engine/RadiatorSlipStreamFactor plain float, default 0.1
    /* 0x1168 */ CarObfFloat wear_ice_rate;                   // L2 decomp:re/decomp/144685680.c;xref:0x144685e8e,0x144685ebc VTF Engine/WearICERate ICE wear rate, default 0, Ferrari 0.015; radUpgradeWearICE x0.5 (obf)
    /* 0x1178 */ CarSpline wear_ice_revs_spline;              // L2 decomp:re/decomp/144685680.c;xref:0x144685ec2 VTF Engine/WearICERevsSpline
    /* 0x1420 */ CarSpline wear_ice_power_spline;             // L2 decomp:re/decomp/144685680.c;xref:0x144685ed8 VTF Engine/WearICEPowerSpline
    /* 0x16c8 */ CarObfFloat wear_tc_rate;                    // L2 decomp:re/decomp/144685680.c;xref:0x144685eee,0x144685f1c VTF Engine/WearTCRate turbo wear rate; radUpgradeWearTC x0.5 (obf)
    /* 0x16d8 */ CarSpline wear_tc_power_spline;              // L2 decomp:re/decomp/144685680.c;xref:0x144685f22 VTF Engine/WearTCPowerSpline
    /* 0x1980 */ CarObfFloat wear_mguh_rate;                  // L2 decomp:re/decomp/144685680.c;xref:0x144685f38,0x144685f66 VTF Engine/WearMGUHRate radUpgradeWearMGUH x0.5 (obf)
    /* 0x1990 */ CarSpline wear_mguh_temp_spline;             // L2 decomp:re/decomp/144685680.c;xref:0x144685f6c VTF Engine/WearMGUHTempSpline
    /* 0x1c38 */ CarSpline wear_mguh_cooling_spline;          // L2 decomp:re/decomp/144685680.c;xref:0x144685f82 VTF Engine/WearMGUHCoolingSpline
    /* 0x1ee0 */ CarObfFloat wear_ce_rate;                    // L2 decomp:re/decomp/144685680.c;xref:0x144685f98,0x144685fc6 VTF Engine/WearCERate control-electronics wear rate; radUpgradeWearCE x0.5 (obf)
    /* 0x1ef0 */ CarObfFloat wear_ce_vibration_min_wave_length; // L2 decomp:re/decomp/144685680.c;xref:0x144685fcc,0x144685ffa VTF Engine/WearCEVibrationMinWaveLength (obf)
    /* 0x1f00 */ CarObfFloat wear_ce_vibration_minmagnitude;  // L2 decomp:re/decomp/144685680.c;xref:0x144686000,0x14468602e VTF Engine/WearCEVibrationMinmagnitude (obf)
    /* 0x1f10 */ CarObfFloat wear_ce_vibration_min_speed;     // L2 decomp:re/decomp/144685680.c;xref:0x144686034,0x144686062 VTF Engine/WearCEVibrationMinSpeed (obf)
    /* 0x1f20 */ CarObfFloat wear_ce_vibration_max_speed;     // L2 decomp:re/decomp/144685680.c;xref:0x144686068,0x144686096 VTF Engine/WearCEVibrationMaxSpeed (obf)
    /* 0x1f30 */ CarObfFloat wear_ced_vibration_rate;         // L2 decomp:re/decomp/144685680.c;xref:0x14468609c,0x1446860ca VTF Engine/WearCEDVibrationRate radUpgradeWearCE x0.5 (obf)
    /* 0x1f40 */ CarSpline wear_ce_penalty_spline;            // L2 decomp:re/decomp/144685680.c;xref:0x1446860d0 VTF Engine/WearCEPenaltySpline evaluated at wear_ce; scales ES/MGU-K wear in the ERS wear step 0x1446d1210
    /* 0x21e8 */ CarObfFloat kers_power;                      // L2 decomp:re/decomp/144685680.c;xref:0x1446860e6,0x144686114 VTF Engine/KERSPower W, default 0 (legacy KERS) (obf)
    /* 0x21f8 */ CarObfFloat kers_capacity;                   // L2 decomp:re/decomp/144685680.c;xref:0x14468611a,0x144686148 VTF Engine/KERSCapacity J, default 0; Reset copies it to kers_energy (obf)
    /* 0x2208 */ int32_t turbo_mode;                          // L2 decomp:re/decomp/144685680.c;xref:0x1446861bb VTF Engine/TurboMode Off=0 LagSpoof=1 Full=2 (Car_Powertrain_Step: Full multiplies drive torque by Turbo efficiency_factor)
    /* 0x2210 */ CarSpline revs_turbo_lag_spline;             // L2 decomp:re/decomp/144685680.c;xref:0x1446861c1 VTF Engine/RevsTurboLagSpline turbo lag vs rad/s (TurboMode LagSpoof)
    /* 0x24d8 */ CarObfFloat idle_fluctuation_amplitude;      // L2 decomp:re/decomp/144685680.c;xref:0x1446861d7,0x144686205 VTF Engine/IdleFluctuationAmplitude (obf)
    /* 0x24e8 */ CarObfFloat idle_fluctuation_frequency;      // L2 decomp:re/decomp/144685680.c;xref:0x14468620b,0x144686239 VTF Engine/IdleFluctuationFrequency default 10 (obf)
    /* 0x24f8 */ CarObfFloat red_line_throttle_cut_time;      // L2 decomp:re/decomp/144685680.c;xref:0x14468623f,0x14468626f VTF Engine/RedLineThrottleCutTime s, default 0, Ferrari 0.016 (obf)
    /* 0x2508 */ CarObfFloat scaled_power_ref;                // L2 decomp:re/decomp/1553f5460.c;xref:0x1553f54a1 derived (Init slot +0x30 / 0x144689a70): FuelECU active power scale x RevsPowerSpline+0x30 (spline field, meaning L1)
    /* 0x2518 */ CarObfFloat max_torque;                      // L3 decomp:re/decomp/1553f5460.c;xref:0x1446885a0,0x1553f54b6,0x1553f5548;trace:T122/2026-10-28-gp:7 derived: max over RevsPowerSpline points of (P x power_scale)/(rad/s x revs_scale), Nm; channel "max_torque" Live: 493.0 Nm decoded.
    /* 0x2528 */ float limiter_rotation_rate;                 // L3 decomp:re/decomp/1553f5460.c;decomp:re/decomp/144688610.c;xref:0x1553f557b;trace:T122/2026-10-28-gp:7 derived: LimiterRotationRate x FuelECU revs scale (rad/s); Reset 0x10|0x200 stores the raw value Live: 1371.8 rad/s.
    /* 0x252c */ float engine_braking_fraction;               // L2 decomp:re/decomp/1446878d0.c;decomp:re/decomp/1446e2290.c;xref:0x144687bc8 F1Tuning engine-braking slider (Reset 0x2000); Car_Powertrain_Step lerps friction->engine-braking torque by it
    /* 0x2530 */ uint8_t state_flags;                         // L2 decomp:re/decomp/144688610.c;xref:0x144688920,0x144688ce2 engine input step flags; Reset 0x3000 clears the low 5 bits
    /* 0x2538 */ float rotation_rate;                         // L3 decomp:re/decomp/1446878d0.c;xref:0x1446437e0,0x14468793e;trace:T122/2026-10-28-gp:7 engine speed rad/s (channel "rotation_rate"; "rpm" = x9.5493); Reset 0x10|0x200 = IdleRotationRate Live: 418.9 rad/s (4000 rpm) idling on the grid, up to about 11600 rpm on track.
    /* 0x253c */ float temperature;                           // L2 decomp:re/decomp/1446878d0.c;xref:0x144688520,0x1446879b5 K (channel "temperature"; Reset 0x400 = InitialTemperature)
    /* 0x2540 */ float kers_energy;                           // L2 decomp:re/decomp/1446878d0.c;xref:0x144687967,0x144687a18 legacy KERS store, Reset = KERSCapacity
    /* 0x2544 */ float load;                                  // L2 decomp:re/decomp/1446878d0.c;xref:0x144688580,0x144687944 channel "load"
    /* 0x2548 */ float torque;                                // L2 decomp:re/decomp/1446878d0.c;xref:0x144688590,0x144687944 Nm, channel "torque"
    /* 0x254c */ float wear_ice;                              // L2 decomp:re/decomp/1446878d0.c;xref:0x1446884c0,0x1446879cb PU wear 0..1 (channel "wear_ice"); Reset 0x800 zeroes all six; channel "damage" = max of the six
    /* 0x2550 */ float wear_tc;                               // L2 decomp:re/decomp/1446878d0.c;xref:0x1446884d0,0x1446879cb turbocharger wear (channel "wear_tc")
    /* 0x2554 */ float wear_es;                               // L2 decomp:re/decomp/1446d1210.c;xref:0x1446884e0 energy store wear, integrated by the ERS wear step 0x1446d1210
    /* 0x2558 */ float wear_mguh;                             // L2 decomp:re/decomp/1446878d0.c;xref:0x1446884f0,0x1446879cb MGU-H wear (channel "wear_mguh")
    /* 0x255c */ float wear_mguk;                             // L2 decomp:re/decomp/1446d1210.c;xref:0x144688500 MGU-K wear, integrated by the ERS wear step 0x1446d1210
    /* 0x2560 */ float wear_ce;                               // L2 decomp:re/decomp/1446878d0.c;xref:0x144688510,0x1446879cb control electronics wear (channel "wear_ce")
    /* 0x2570 */ float throttle_lagged;                       // L2 decomp:re/decomp/1446e2290.c;xref:0x1446885c0,0x1446e26b8,0x1446e26f1 channel "throttle_lagged" (turbo lag when TurboMode LagSpoof)
    /* 0x2574 */ float throttle;                              // L3 decomp:re/decomp/1446e2290.c;xref:0x1446885d0,0x1446e26a5,0x1446e28ff;trace:T122/2026-10-28-gp:7 0..1 effective throttle (channel "throttle") Live: 0 on the grid, 0..1 on track.
    /* 0x2578 */ float net_torque_no_ers;                     // L2 decomp:re/decomp/1446e2290.c;xref:0x1446e290b,0x1446e30ec Car_Powertrain_Step without ERS: (drive + braking) x throttle - braking
    /* 0x25b0 */ float radiator_opening;                      // L2 decomp:re/decomp/1446878d0.c;xref:0x144687c9f F1Tuning radiator slider (Reset 0x2000)
};
