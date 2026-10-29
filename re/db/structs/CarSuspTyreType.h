// struct: CarSuspTyreType
// subsystem: car
// size: ?  L1 decomp:re/decomp/1446aa090.c
// notes: VTF SuspensionSystem/Tyre[name] record (CarSuspension.tyre_types[i]); plain floats except the six Debris* XOR pairs at +0x70..+0xc0 (V1285). Also the first qword
// of each CarTyreState (Car_Tyre_GripScale reads +0x1c, +0x638..+0x64c). SlipAngleLimit*/SlipRatio*Limit* keys are read
// into locals and stored converted (offsets not mapped).
struct CarSuspTyreType {
    /* 0x0000 */ char name[16];                               // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension Tyre name attribute (Front/Rear), compared to find the record
    /* 0x0010 */ float mass;                                  // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/Mass
    /* 0x0014 */ float inertia;                               // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/Inertia
    /* 0x0018 */ float tyre_width;                            // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/TyreWidth
    /* 0x001c */ float tyre_radius;                           // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension;xref:0x1446df89a VTF SuspensionSystem/Tyre/TyreRadius; GripScale divides contact speed by it
    /* 0x0020 */ float alloy_radius;                          // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/AlloyRadius
    /* 0x0024 */ float min_grip_multiplier;                   // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/MinGripMultiplier
    /* 0x0028 */ float max_grip_multiplier;                   // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/MaxGripMultiplier
    /* 0x002c */ float grip_multiplier;                       // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/GripMultiplier
    /* 0x0030 */ float slip_angle_scale;                      // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/SlipAngleScale
    /* 0x0034 */ float slip_ratio_scale;                      // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/SlipRatioScale
    /* 0x0038 */ float coef_rolling_resistance;               // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/CoefRollingResistance
    /* 0x0040 */ float max_compression;                       // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/MaxCompression
    /* 0x0044 */ float tyre_load_max;                         // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/TyreLoadMax
    /* 0x0048 */ float puncture_wear_min;                     // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/PunctureWearMin
    /* 0x004c */ float puncture_wear_max;                     // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/PunctureWearMax
    /* 0x0050 */ float puncture_spring_constant_multiplier;   // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/PunctureSpringConstantMultiplier
    /* 0x0054 */ float puncture_spring_damping_multiplier;    // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/PunctureSpringDampingMultiplier
    /* 0x0058 */ float puncture_radius_multiplier;            // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/PunctureRadiusMultiplier
    /* 0x005c */ float puncture_grip_multiplier;              // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/PunctureGripMultiplier
    /* 0x0060 */ float puncture_resistance_multiplier;        // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/PunctureResistanceMultiplier
    /* 0x0064 */ float blowout_min_time;                      // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/BlowoutMinTime
    /* 0x0068 */ float blowout_max_time;                      // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/BlowoutMaxTime
    /* 0x006c */ float burst_grip_multiplier;                 // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/BurstGripMultiplier
    /* 0x0070 */ CarObfFloat debris_puncture_min_speed;     // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/DebrisPunctureMinSpeed; XOR pair (V1285)
    /* 0x0080 */ CarObfFloat debris_puncture_max_speed;     // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/DebrisPunctureMaxSpeed; XOR pair (V1285)
    /* 0x0090 */ CarObfFloat debris_puncture_probability_at_min_speed; // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/DebrisPunctureProbabilityAtMinSpeed; XOR pair (V1285)
    /* 0x00a0 */ CarObfFloat debris_puncture_probability_at_max_speed; // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/DebrisPunctureProbabilityAtMaxSpeed; XOR pair (V1285)
    /* 0x00b0 */ CarObfFloat debris_puncture_invulnerability_period; // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/DebrisPunctureInvulnerabilityPeriod; XOR pair (V1285)
    /* 0x00c0 */ CarObfFloat debris_next_gen_puncture_probability_multiplier; // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/DebrisNextGenPunctureProbabilityMultiplier; XOR pair (V1285)
    /* 0x00d0 */ CarSpline collision_speed_tyre_damage_rate_spline; // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/CollisionSpeedTyreDamageRateSpline
    /* 0x0378 */ CarSpline puncture_speed_tyre_damage_rate_spline; // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/PunctureSpeedTyreDamageRateSpline
    /* 0x0630 */ float upgrade_wear_factor;                   // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/UpgradeWearFactor; radUpgradeTyreWear op * 0.825 (and radUpgradeTyreLife)
    /* 0x0634 */ float upgrade_temp_factor;                   // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/UpgradeTempFactor; addon radUpgradeTyreThermal * 0.85
    /* 0x0638 */ float wetness_downslope_factor;              // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension;xref:0x1446df89a VTF SuspensionSystem/Tyre/WetnessDownslopeFactor; GripScale wet loss k (player)
    /* 0x063c */ float wetness_downslope_factor_ai;           // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension;xref:0x1446df89a VTF SuspensionSystem/Tyre/WetnessDownslopeFactorAI; GripScale wet loss k when car+0x124 == 1 (AI)
    /* 0x0640 */ float speed_downslope_factor;                // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension;xref:0x1446df89a VTF SuspensionSystem/Tyre/SpeedDownslopeFactor; GripScale speed loss k (player)
    /* 0x0644 */ float speed_downslope_factor_ai;             // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension;xref:0x1446df89a VTF SuspensionSystem/Tyre/SpeedDownslopeFactorAI; AI variant
    /* 0x0648 */ float speed_downslope_rot_rate;              // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension;xref:0x1446df89a VTF SuspensionSystem/Tyre/SpeedDownslopeRotRate; GripScale rot-rate threshold v0
    /* 0x064c */ float downslope_scale_minimum;               // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension;xref:0x1446df89a VTF SuspensionSystem/Tyre/DownslopeScaleMinimum; GripScale floor
    /* 0x0650 */ float contact_patch_thickness;               // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/ContactPatchThickness
    /* 0x0654 */ float temperature_wear_factor_offset;        // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/TemperatureWearFactorOffset
    /* 0x0658 */ float temperature_wear_factor_maximum;       // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Tyre/TemperatureWearFactorMaximum
};
