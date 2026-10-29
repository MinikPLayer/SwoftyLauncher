// struct: CarErs
// subsystem: car
// size: 0x1a78  L2 decomp:re/decomp/144759840.c (alloc 0x1a78)
// notes: the EnergyRecoverySystem system at CarLive+0x5a8 (Car_Create_EnergyRecoverySystemSystem 0x144759840). Data at +0x18
// (Car_Read_EnergyRecovery param = this+0x18). ERS numbers are stored plain (no XOR). The reader also reads
// TrackSettings for the current track, so it must run after the track is known. Reset = vtable +0x18 (0x1446d0c40 -> 0x1554791c0);
// phase-1 callback 0x1446d1210 integrates ES/MGU-K wear; deployment and harvest run inside Car_Powertrain_Step.
struct CarErs {
    /* 0x0000 */ void *vtable;                                // L2 decomp:re/decomp/144759840.c;xref:0x14475986c,0x14475989f system vtable 0x145bfdf40 (CarComponentBase)
    /* 0x0008 */ CarLive *car;                                // L2 decomp:re/decomp/144759840.c;xref:0x144759890 owning car
    /* 0x0010 */ void *allocator;                             // L2 decomp:re/decomp/144759840.c;xref:0x144759894 car allocator
    /* 0x0018 */ void *data_vtable;                           // L2 decomp:re/decomp/144759840.c;xref:0x14475987a,0x1447598ad EnergyRecoverySystemData vtable 0x145bfdaf0 {dtor, GetName, Car_Read_EnergyRecovery, Car_Write_EnergyRecovery}; reader offsets = field - 0x18
    /* 0x0028 */ float max_deploy_power;                      // L2 decomp:re/decomp/1446cfcd0.c;decomp:re/decomp/1446e2290.c;xref:0x1446cfd4d,0x1446d0084 VTF EnergyRecoverySystem/Common/MaxDeployPower W, 2025 126.3e3, 2026 350e3; deploy = this x mode gear modifier x deploy factor (Car_Powertrain_Step)
    /* 0x002c */ float max_derate_power;                      // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446cfd6a Common/MaxDeratePower W (2026 -250e3)
    /* 0x0030 */ float boost_mode_power_add_limit;            // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446cfd80,0x1446d005a Common/BoostModePowerAddLimit
    /* 0x0034 */ float min_initial_deploy_power;              // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446cfdf7 Common/MinInitialDeployPower W (2026 200e3)
    /* 0x0038 */ float power_reduction_rate_limit;            // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446cfd0e,0x1446cfd95 Common/PowerReductionRateLimit (2026 100e3); per-track override
    /* 0x003c */ float power_reduction_speed_min;             // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446cfe0e Common/PowerReductionSpeedMin m/s (2026 58.333)
    /* 0x0040 */ float full_throttle_time;                    // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446cfdb2,0x1446d04e8 Common/FullThrottleTime s
    /* 0x0044 */ float off_throttle_time;                     // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446cfdc9 Common/OffThrottleTime s
    /* 0x0048 */ float off_throttle_timer_torque;             // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446cfde0 Common/OffThrottleTimerTorque
    /* 0x004c */ float harvest_torque_factor;                 // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446cfe25 Common/HarvestTorqueFactor; per-track override
    /* 0x0050 */ float harvest_limit_mguk;                    // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446cfe44,0x1446cfe7a Common/HarvestLimitMGUK J per lap (2025 2e6, 2026 8.5e6); per-track override; Reset copies to lap_harvest_limit
    /* 0x0054 */ float harvest_override_bonus_mguk;           // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446cfe63 Common/HarvestOverrideBonusMGUK J (2026 0.5e6)
    /* 0x0058 */ float harvest_limit_qualifying_mguk;         // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446cfe7f,0x1446cfe83 Common/HarvestLimitQualifyingMGUK J (defaults to HarvestLimitMGUK; 2026 9e6)
    /* 0x005c */ float deploy_limit_mguk;                     // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446cfea3 Common/DeployLimitMGUK J per lap (2025 4e6, 2026 13e6); deploy_lap_proportion divisor
    /* 0x0060 */ float energy_store_capacity;                 // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446cfebd Common/EnergyStoreCapacity J (4e6); Reset 0x210 fills energy to it
    /* 0x0064 */ float energy_deploy_efficiency_mguk;         // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446cfef6,0x1446cfefa Common/EnergyDeployEfficiencyMGUK (defaults to the MGU-K harvest efficiency; 2026 0.9)
    /* 0x0068 */ float energy_harvest_efficiency_mguk;        // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446cfed7,0x1446cfef1 Common/EnergyHarvestEfficencyMGUK (sic), 0.95; radUpgradeERS x1.042
    /* 0x006c */ float energy_harvest_efficiency_mguh;        // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446cff15 Common/EnergyHarvestEfficencyMGUH 0.95 (2026 0 = no MGU-H)
    /* 0x0070 */ float max_brake_torque_mguk;                 // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446cff2f Common/MaxBrakeTorqueMGUK Nm (200, 2026 500)
    /* 0x0074 */ float exhaust_power_coefficient;             // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446cff49 Common/ExhaustPowerCoefficient 16.8 (MGU-H harvest); radUpgradeFuelConsumption x1.2, radUpgradeERS x1.15
    /* 0x0078 */ float turbo_power_coefficient;               // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446cff63 Common/TurboPowerCoefficient 7.8
    /* 0x007c */ float overtake_power_efficiency;             // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446cff7d Common/OvertakePowerEfficiency 0.74; radUpgradeERS x1.35
    /* 0x0080 */ uint8_t use_derate_lights;                   // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446cff97 Common/UseDerateLights (bool)
    /* 0x0081 */ uint8_t override_mode_available;             // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446cffb1 Common/OverrideModeAvailable (2026 true)
    /* 0x0082 */ uint8_t allow_full_throttle_power_increase;  // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446cffcb Common/AllowFullThrottlePowerIncrease (2026 true)
    /* 0x0088 */ CarSpline speed_deploy_spline;               // L2 decomp:re/decomp/1446cfcd0.c;decomp:re/decomp/1446e2290.c;xref:0x1446cffe5 Common/SpeedDeploySpline deploy fraction vs speed (per-track override); Car_Powertrain_Step evaluates +0x88 (= spline+0x18)
    /* 0x0330 */ CarSpline speed_deploy_override_spline;      // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446d0003,0x1446d004a Common/SpeedDeployOverrideSpline (defaults to a copy of SpeedDeploySpline); used while override_active
    /* 0x05d8 */ CarSpline speed_harvest_spline;              // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446d0394 Common/SpeedHarvestSpline
    /* 0x0880 */ CarSpline charge_deploy_spline;              // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446d03af Common/ChargeDeploySpline
    /* 0x0b28 */ float base_wear_rate_es;                     // L2 decomp:re/decomp/1446cfcd0.c;decomp:re/decomp/1446d1210.c;xref:0x1446d03ca Common/BaseWearRateES 1e-5; radUpgradeWearES x0.5; ES wear step 0x1446d1210
    /* 0x0b30 */ CarSpline charge_es_wear_rate_spline;        // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446d03e5 Common/ChargeESWearRateSpline vs state of charge
    /* 0x0dd8 */ CarSpline speed_es_wear_rate_spline;         // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446d041b Common/SpeedESWearRateSpline
    /* 0x1080 */ CarSpline es_wear_capacity_modifier_spline;  // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446d0400 Common/ESWearCapacityModifierSpline: usable capacity fraction vs wear_es (energy is capped to capacity x it)
    /* 0x1328 */ float base_wear_rate_mguk;                   // L2 decomp:re/decomp/1446cfcd0.c;decomp:re/decomp/1446d1210.c;xref:0x1446d0436 Common/BaseWearRateMGUK 1.5e-5; radUpgradeWearMGUK x0.5
    /* 0x1330 */ CarSpline deployment_mguk_damage_rate_spline; // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446d0451 Common/DeploymentMGUKDamageRateSpline vs deploy_power/MaxDeployPower
    /* 0x15d8 */ CarSpline mguk_damage_mguk_harvest_efficiency_spline; // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446d046c Common/MGUKDamageMGUKHarvestEfficiencySpline
    /* 0x1880 */ CarErsMode modes[4];                         // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446d04bc Mode[0..3] (None, Medium, Overtake, Hotlap) read by 0x1446cf590 -> 0x155477130; stride 0x40; extra modes log WARN "Too many ERS modes"
    /* 0x1980 */ float medium_deploy_factor;                  // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446d06bc TrackSettings/Track[name]/MediumDeployFactor (only when the current track has a block)
    /* 0x1984 */ float hotlap_deploy_factor;                  // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446d06da TrackSettings/HotLapDeployFactor
    /* 0x1988 */ float harvest_factor_mguh;                   // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446d06f8 TrackSettings/HarvestFactorMGUH
    /* 0x198c */ float medium_derate_factor;                  // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446d06cb TrackSettings/MediumDerateFactor
    /* 0x1990 */ float hotlap_derate_factor;                  // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446d06e9 TrackSettings/HotLapDerateFactor
    /* 0x1994 */ float override_detection_lap_progress;       // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446d0707 TrackSettings/OverrideDetectionLapProgress
    /* 0x1998 */ float override_activation_lap_progress;      // L2 decomp:re/decomp/1446cfcd0.c;xref:0x1446d0716 TrackSettings/OverrideActivationLapProgress
    /* 0x19a0 */ uint8_t track_settings_found;                // L2 decomp:re/decomp/1446cfcd0.c;decomp:re/decomp/1446e2290.c;xref:0x1446d07f0 1 when TrackSettings has the current track (name from [[[g+0xd0]+0xe8]+0xa0]); Car_Powertrain_Step then uses +0x1980.. else the defaults at +0x19a8..
    /* 0x19c8 */ float harvest_lap_mguk;                      // L2 decomp:re/decomp/1554791c0.c;xref:0x1446d1150,0x1554791da J harvested this lap (channel); Reset 0x210 zeroes the lap counters
    /* 0x19cc */ float harvest_lap_mguh;                      // L2 decomp:re/decomp/1554791c0.c;xref:0x1446d1160,0x1554791da J this lap (channel)
    /* 0x19d0 */ float deploy_lap;                            // L2 decomp:re/decomp/1554791c0.c;xref:0x1446d1170,0x1554791da J deployed this lap (channel); deploy_lap_proportion = / deploy_limit_mguk
    /* 0x19f0 */ float energy;                                // L3 decomp:re/decomp/1554791c0.c;decomp:re/decomp/1446d1210.c;xref:0x1446d1120,0x1554791f7;trace:T125/2026-10-28-gp:16 J in the store (channel "energy"); Reset = energy_store_capacity; capped by the wear step Live: 4e6 J (full) on the grid, 3.942e6 J after about 40 s.
    /* 0x19f4 */ float lap_harvest_limit;                     // L2 decomp:re/decomp/1554791c0.c;xref:0x1446d1180,0x1554791ff J per lap harvest limit in force (Reset = HarvestLimitMGUK); harvest_lap_mguk_proportion divisor
    /* 0x1a00 */ float harvest_power_mguk;                    // L2 decomp:re/decomp/1554791c0.c;xref:0x1446d11b0,0x155479212 W (channel)
    /* 0x1a04 */ float harvest_power_mguh;                    // L2 decomp:re/decomp/1554791c0.c;xref:0x1446d11c0,0x155479212 W (channel)
    /* 0x1a14 */ float deploy_power;                          // L2 decomp:re/decomp/1446d1210.c;xref:0x1446d11d0 W (channel); MGU-K wear input
    /* 0x1a68 */ int32_t ers_mode;                            // L3 decomp:re/decomp/1554791c0.c;decomp:re/decomp/1446e2290.c;xref:0x155479223;trace:T125/2026-10-28-gp:16;trace:T125/2026-10-28-gp:20 0 None, 1 Medium, 2 Overtake, 3 Hotlap (index into modes); Reset sets 1 Live: 1 (Medium) or 2 (Overtake) for AI cars on track; the MFD link (T125) still needs a driver.
};
