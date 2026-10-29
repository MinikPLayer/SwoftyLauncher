// struct: CarTuning
// subsystem: car
// size: ? L2 decomp:re/decomp/1446d1660.c;decomp:re/decomp/140c11550.c (data ends at +0x20c, runtime slider state from +0x210)
// notes: the F1Tuning system at CarLive+0x590, offsets from the system pointer (CarComponentBase head: +0 vtable, +8 CarLive,
// +0x10 allocator, +0x18 data object; reader offset = field - 0x18). Data-class vtable vt_Car_F1TuningData 0x145bfe030: slot 1
// 0x1446d7570 (type name "F1Tuning"), slot 2 Car_Read_F1Tuning 0x1446d1660, slot 3 Car_Write_F1Tuning 0x1446d4470. Every data
// field is a PLAIN float (no XOR pair), a Min/Max or Lower/UpperBound pair: the range a setup slider is lerped over. A key the
// VTF omits keeps its constructor default and logs 'Float "%s" NOT found.' (VEHICLE_ASSETS); the reader only checks Min <= Max
// (errno-trace ring), it never swaps. When DifferentialRearTorqueMin is present (older VTFs) it derives +0x30..+0x3c from
// DifferentialRearTorqueMin/Max and DifferentialOffPowerMultipleMin/Max (x0.01). The 2025 F1 VTFs carry 44 of these keys;
// the rest take defaults. From +0x210 Car_Setup_ApplyTuning keeps each slider's 0..1 fraction (or clamped value). Slider ->
// physics map: subsystems/car.md#tuning.
struct CarTuning {
    /* 0x0028 */ float chassis_comz_min; // L2 decomp:re/decomp/1446d1660.c;decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning VTF F1Tuning/ChassisCOMZMin, plain float; Car_Setup_ApplyTuning: chassis z-com slider (Chassis+0x158)
    /* 0x002c */ float chassis_comz_max; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/ChassisCOMZMax, plain float
    /* 0x0030 */ float differential_on_throttle_multiple_min; // L2 decomp:re/decomp/1446d1660.c;decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning VTF F1Tuning/DifferentialOnThrottleMultipleMin, plain float; Car_Setup_ApplyTuning: diff on-throttle slider -> Differential+0xf0
    /* 0x0034 */ float differential_on_throttle_multiple_max; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/DifferentialOnThrottleMultipleMax, plain float
    /* 0x0038 */ float differential_off_throttle_multiple_min; // L2 decomp:re/decomp/1446d1660.c;decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning VTF F1Tuning/DifferentialOffThrottleMultipleMin, plain float; Car_Setup_ApplyTuning: diff off-throttle slider -> Differential+0xec
    /* 0x003c */ float differential_off_throttle_multiple_max; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/DifferentialOffThrottleMultipleMax, plain float
    /* 0x0040 */ float differential_mid_corner_multiple_min; // L2 decomp:re/decomp/1446d1660.c;decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning VTF F1Tuning/DifferentialMidCornerMultipleMin, plain float; Car_Setup_ApplyTuning: diff mid-corner slider -> Differential+0xf4
    /* 0x0044 */ float differential_mid_corner_multiple_max; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/DifferentialMidCornerMultipleMax, plain float
    /* 0x0048 */ float differential_high_speed_kph_min; // L2 decomp:re/decomp/1446d1660.c;decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning VTF F1Tuning/DifferentialHighSpeedKPHMin, plain float; Car_Setup_ApplyTuning: clamp of slider 0x14 -> Differential+0xf8
    /* 0x004c */ float differential_high_speed_kph_max; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/DifferentialHighSpeedKPHMax, plain float
    /* 0x0050 */ float differential_preload_min; // L2 decomp:re/decomp/1446d1660.c;decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning VTF F1Tuning/DifferentialPreloadMin, plain float; Car_Setup_ApplyTuning: clamp of slider 0x18 -> Differential+0xfc
    /* 0x0054 */ float differential_preload_max; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/DifferentialPreloadMax, plain float
    /* 0x0058 */ float engine_braking_min; // L2 decomp:re/decomp/1446d1660.c;decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning VTF F1Tuning/EngineBrakingMin, plain float; Car_Setup_ApplyTuning: engine braking slider -> Engine+0x252c
    /* 0x005c */ float engine_braking_max; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/EngineBrakingMax, plain float
    /* 0x0060 */ float radiator_opening_min; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/RadiatorOpeningMin, plain float
    /* 0x0064 */ float radiator_opening_max; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/RadiatorOpeningMax, plain float
    /* 0x0068 */ float front_suspension_camber_min; // L2 decomp:re/decomp/1446d1660.c;decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning VTF F1Tuning/FrontSuspensionCamberMin, plain float; Car_Setup_ApplyTuning: front camber slider -> front axle +0x1650
    /* 0x006c */ float front_suspension_camber_max; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/FrontSuspensionCamberMax, plain float
    /* 0x0070 */ float rear_suspension_camber_min; // L2 decomp:re/decomp/1446d1660.c;decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning VTF F1Tuning/RearSuspensionCamberMin, plain float; Car_Setup_ApplyTuning: rear camber slider -> rear axle +0x1650
    /* 0x0074 */ float rear_suspension_camber_max; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/RearSuspensionCamberMax, plain float
    /* 0x0078 */ float front_suspension_toe_min; // L2 decomp:re/decomp/1446d1660.c;decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning VTF F1Tuning/FrontSuspensionToeMin, plain float; Car_Setup_ApplyTuning: front toe slider -> front axle +0x1654
    /* 0x007c */ float front_suspension_toe_max; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/FrontSuspensionToeMax, plain float
    /* 0x0080 */ float rear_suspension_toe_min; // L2 decomp:re/decomp/1446d1660.c;decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning VTF F1Tuning/RearSuspensionToeMin, plain float; Car_Setup_ApplyTuning: rear toe slider -> rear axle +0x1654
    /* 0x0084 */ float rear_suspension_toe_max; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/RearSuspensionToeMax, plain float
    /* 0x0088 */ float front_suspension_height_min; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/FrontSuspensionHeightMin, plain float
    /* 0x008c */ float front_suspension_height_max; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/FrontSuspensionHeightMax, plain float
    /* 0x0090 */ float rear_suspension_height_min; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/RearSuspensionHeightMin, plain float
    /* 0x0094 */ float rear_suspension_height_max; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/RearSuspensionHeightMax, plain float
    /* 0x0098 */ float front_suspension_rest_length_min; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/FrontSuspensionRestLengthMin, plain float
    /* 0x009c */ float front_suspension_rest_length_max; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/FrontSuspensionRestLengthMax, plain float
    /* 0x00a0 */ float rear_suspension_rest_length_min; // L1 decomp:re/decomp/1446d1660.c (destination lost in the decomp; offset from the read order) VTF F1Tuning/RearSuspensionRestLengthMin, plain float
    /* 0x00a4 */ float rear_suspension_rest_length_max; // L1 decomp:re/decomp/1446d1660.c (destination lost in the decomp; offset from the read order) VTF F1Tuning/RearSuspensionRestLengthMax, plain float
    /* 0x00a8 */ float front_ride_height_min; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/FrontRideHeightMin, plain float
    /* 0x00ac */ float front_ride_height_max; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/FrontRideHeightMax, plain float
    /* 0x00b0 */ float rear_ride_height_min; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/RearRideHeightMin, plain float
    /* 0x00b4 */ float rear_ride_height_max; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/RearRideHeightMax, plain float
    /* 0x00b8 */ float front_suspension_spring_constant_min; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/FrontSuspensionSpringConstantMin, plain float
    /* 0x00bc */ float front_suspension_spring_constant_max; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/FrontSuspensionSpringConstantMax, plain float
    /* 0x00c0 */ float rear_suspension_spring_constant_min; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/RearSuspensionSpringConstantMin, plain float
    /* 0x00c4 */ float rear_suspension_spring_constant_max; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/RearSuspensionSpringConstantMax, plain float
    /* 0x00c8 */ float front_suspension_third_spring_min; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/FrontSuspensionThirdSpringMin, plain float
    /* 0x00cc */ float front_suspension_third_spring_max; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/FrontSuspensionThirdSpringMax, plain float
    /* 0x00d0 */ float rear_suspension_third_spring_min; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/RearSuspensionThirdSpringMin, plain float
    /* 0x00d4 */ float rear_suspension_third_spring_max; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/RearSuspensionThirdSpringMax, plain float
    /* 0x00d8 */ float front_suspension_anti_roll_bar_min; // L2 decomp:re/decomp/1446d1660.c;decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning VTF F1Tuning/FrontSuspensionAntiRollBarMin, plain float; Car_Setup_ApplyTuning: front ARB slider -> front axle +0x1664
    /* 0x00dc */ float front_suspension_anti_roll_bar_max; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/FrontSuspensionAntiRollBarMax, plain float
    /* 0x00e0 */ float rear_suspension_anti_roll_bar_min; // L2 decomp:re/decomp/1446d1660.c;decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning VTF F1Tuning/RearSuspensionAntiRollBarMin, plain float; Car_Setup_ApplyTuning: rear ARB slider -> rear axle +0x1664
    /* 0x00e4 */ float rear_suspension_anti_roll_bar_max; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/RearSuspensionAntiRollBarMax, plain float
    /* 0x00e8 */ float brake_pressure_min; // L2 decomp:re/decomp/1446d1660.c;decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning VTF F1Tuning/BrakePressureMin, plain float; Car_Setup_ApplyTuning: brake pressure slider -> BrakeECU+0x21dc
    /* 0x00ec */ float brake_pressure_max; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/BrakePressureMax, plain float
    /* 0x00f0 */ float brake_migration_min; // L2 decomp:re/decomp/1446d1660.c;decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning VTF F1Tuning/BrakeMigrationMin, plain float; Car_Setup_ApplyTuning: brake migration slider -> BrakeECU+0x21fc
    /* 0x00f4 */ float brake_migration_max; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/BrakeMigrationMax, plain float
    /* 0x00f8 */ float brake_migration_input_start_min; // L2 decomp:re/decomp/1446d1660.c;decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning VTF F1Tuning/BrakeMigrationInputStartMin, plain float; Car_Setup_ApplyTuning: migration ramp start -> BrakeECU+0x2200
    /* 0x00fc */ float brake_migration_input_start_max; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/BrakeMigrationInputStartMax, plain float
    /* 0x0100 */ float brake_migration_input_end_min; // L2 decomp:re/decomp/1446d1660.c;decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning VTF F1Tuning/BrakeMigrationInputEndMin, plain float; Car_Setup_ApplyTuning: migration ramp end -> BrakeECU+0x2204
    /* 0x0104 */ float brake_migration_input_end_max; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/BrakeMigrationInputEndMax, plain float
    /* 0x0108 */ float brake_cooling_holes_front_min; // L2 decomp:re/decomp/1446d1660.c;decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning VTF F1Tuning/BrakeCoolingHolesFrontMin, plain float; Car_Setup_ApplyTuning: front cooling holes -> front wheels +0x134
    /* 0x010c */ float brake_cooling_holes_front_max; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/BrakeCoolingHolesFrontMax, plain float
    /* 0x0110 */ float brake_cooling_holes_rear_min; // L2 decomp:re/decomp/1446d1660.c;decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning VTF F1Tuning/BrakeCoolingHolesRearMin, plain float; Car_Setup_ApplyTuning: rear cooling holes -> rear wheels +0x134
    /* 0x0114 */ float brake_cooling_holes_rear_max; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/BrakeCoolingHolesRearMax, plain float
    /* 0x0118 */ float brake_duct_factor_front_min; // L2 decomp:re/decomp/1446d1660.c;decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning VTF F1Tuning/BrakeDuctFactorFrontMin, plain float; Car_Setup_ApplyTuning: front brake duct -> front wheels +0x130
    /* 0x011c */ float brake_duct_factor_front_max; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/BrakeDuctFactorFrontMax, plain float
    /* 0x0120 */ float brake_duct_factor_rear_min; // L2 decomp:re/decomp/1446d1660.c;decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning VTF F1Tuning/BrakeDuctFactorRearMin, plain float; Car_Setup_ApplyTuning: rear brake duct -> rear wheels +0x130
    /* 0x0124 */ float brake_duct_factor_rear_max; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/BrakeDuctFactorRearMax, plain float
    /* 0x0128 */ float front_tyre_pressure_config; // L1 decomp:re/decomp/1446d1660.c (destination lost in the decomp; offset from the read order) VTF F1Tuning/FrontTyrePressureConfig, plain float
    /* 0x012c */ float front_tyre_pressure_min; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/FrontTyrePressureMin, plain float
    /* 0x0130 */ float front_tyre_pressure_max; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/FrontTyrePressureMax, plain float
    /* 0x0134 */ float rear_tyre_pressure_config; // L1 decomp:re/decomp/1446d1660.c (destination lost in the decomp; offset from the read order) VTF F1Tuning/RearTyrePressureConfig, plain float
    /* 0x0138 */ float rear_tyre_pressure_min; // L1 decomp:re/decomp/1446d1660.c (destination lost in the decomp; offset from the read order) VTF F1Tuning/RearTyrePressureMin, plain float
    /* 0x013c */ float rear_tyre_pressure_max; // L1 decomp:re/decomp/1446d1660.c (destination lost in the decomp; offset from the read order) VTF F1Tuning/RearTyrePressureMax, plain float
    /* 0x0140 */ float front_tyre_spring_constant_coefficient_at_min_pressure; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/FrontTyreSpringConstantCoefficientAtMinPressure, plain float
    /* 0x0144 */ float front_tyre_spring_constant_coefficient_at_max_pressure; // L1 decomp:re/decomp/1446d1660.c (destination lost in the decomp; offset from the read order) VTF F1Tuning/FrontTyreSpringConstantCoefficientAtMaxPressure, plain float
    /* 0x0148 */ float rear_tyre_spring_constant_coefficient_at_min_pressure; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/RearTyreSpringConstantCoefficientAtMinPressure, plain float
    /* 0x014c */ float rear_tyre_spring_constant_coefficient_at_max_pressure; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/RearTyreSpringConstantCoefficientAtMaxPressure, plain float
    /* 0x0150 */ float front_spring_damping_coefficient_at_min_pressure; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/FrontSpringDampingCoefficientAtMinPressure, plain float
    /* 0x0154 */ float front_spring_damping_coefficient_at_max_pressure; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/FrontSpringDampingCoefficientAtMaxPressure, plain float
    /* 0x0158 */ float rear_spring_damping_coefficient_at_min_pressure; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/RearSpringDampingCoefficientAtMinPressure, plain float
    /* 0x015c */ float rear_spring_damping_coefficient_at_max_pressure; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/RearSpringDampingCoefficientAtMaxPressure, plain float
    /* 0x0160 */ float aero_coeff_drag_min_lower_bound; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/AeroCoeffDragMinLowerBound, plain float
    /* 0x0164 */ float aero_coeff_drag_min_upper_bound; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/AeroCoeffDragMinUpperBound, plain float
    /* 0x0168 */ float aero_coeff_drag_max_lower_bound; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/AeroCoeffDragMaxLowerBound, plain float
    /* 0x016c */ float aero_coeff_drag_max_upper_bound; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/AeroCoeffDragMaxUpperBound, plain float
    /* 0x0170 */ float aero_front_coeff_lift_max_front_max_rear_lower_bound; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/AeroFrontCoeffLiftMaxFrontMaxRearLowerBound, plain float
    /* 0x0174 */ float aero_front_coeff_lift_max_front_max_rear_upper_bound; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/AeroFrontCoeffLiftMaxFrontMaxRearUpperBound, plain float
    /* 0x0178 */ float aero_front_coeff_lift_max_front_min_rear_lower_bound; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/AeroFrontCoeffLiftMaxFrontMinRearLowerBound, plain float
    /* 0x017c */ float aero_front_coeff_lift_max_front_min_rear_upper_bound; // L1 decomp:re/decomp/1446d1660.c (destination lost in the decomp; offset from the read order) VTF F1Tuning/AeroFrontCoeffLiftMaxFrontMinRearUpperBound, plain float
    /* 0x0180 */ float aero_front_coeff_lift_min_front_max_rear_lower_bound; // L1 decomp:re/decomp/1446d1660.c (destination lost in the decomp; offset from the read order) VTF F1Tuning/AeroFrontCoeffLiftMinFrontMaxRearLowerBound, plain float
    /* 0x0184 */ float aero_front_coeff_lift_min_front_max_rear_upper_bound; // L1 decomp:re/decomp/1446d1660.c (destination lost in the decomp; offset from the read order) VTF F1Tuning/AeroFrontCoeffLiftMinFrontMaxRearUpperBound, plain float
    /* 0x0188 */ float aero_front_coeff_lift_min_front_min_rear_lower_bound; // L1 decomp:re/decomp/1446d1660.c (destination lost in the decomp; offset from the read order) VTF F1Tuning/AeroFrontCoeffLiftMinFrontMinRearLowerBound, plain float
    /* 0x018c */ float aero_front_coeff_lift_min_front_min_rear_upper_bound; // L1 decomp:re/decomp/1446d1660.c (destination lost in the decomp; offset from the read order) VTF F1Tuning/AeroFrontCoeffLiftMinFrontMinRearUpperBound, plain float
    /* 0x0190 */ float aero_rear_coeff_lift_max_front_max_rear_lower_bound; // L1 decomp:re/decomp/1446d1660.c (destination lost in the decomp; offset from the read order) VTF F1Tuning/AeroRearCoeffLiftMaxFrontMaxRearLowerBound, plain float
    /* 0x0194 */ float aero_rear_coeff_lift_max_front_max_rear_upper_bound; // L1 decomp:re/decomp/1446d1660.c (destination lost in the decomp; offset from the read order) VTF F1Tuning/AeroRearCoeffLiftMaxFrontMaxRearUpperBound, plain float
    /* 0x0198 */ float aero_rear_coeff_lift_max_front_min_rear_lower_bound; // L1 decomp:re/decomp/1446d1660.c (destination lost in the decomp; offset from the read order) VTF F1Tuning/AeroRearCoeffLiftMaxFrontMinRearLowerBound, plain float
    /* 0x019c */ float aero_rear_coeff_lift_max_front_min_rear_upper_bound; // L1 decomp:re/decomp/1446d1660.c (destination lost in the decomp; offset from the read order) VTF F1Tuning/AeroRearCoeffLiftMaxFrontMinRearUpperBound, plain float
    /* 0x01a0 */ float aero_rear_coeff_lift_min_front_max_rear_lower_bound; // L1 decomp:re/decomp/1446d1660.c (destination lost in the decomp; offset from the read order) VTF F1Tuning/AeroRearCoeffLiftMinFrontMaxRearLowerBound, plain float
    /* 0x01a4 */ float aero_rear_coeff_lift_min_front_max_rear_upper_bound; // L1 decomp:re/decomp/1446d1660.c (destination lost in the decomp; offset from the read order) VTF F1Tuning/AeroRearCoeffLiftMinFrontMaxRearUpperBound, plain float
    /* 0x01a8 */ float aero_rear_coeff_lift_min_front_min_rear_lower_bound; // L1 decomp:re/decomp/1446d1660.c (destination lost in the decomp; offset from the read order) VTF F1Tuning/AeroRearCoeffLiftMinFrontMinRearLowerBound, plain float
    /* 0x01ac */ float aero_rear_coeff_lift_min_front_min_rear_upper_bound; // L1 decomp:re/decomp/1446d1660.c (destination lost in the decomp; offset from the read order) VTF F1Tuning/AeroRearCoeffLiftMinFrontMinRearUpperBound, plain float
    /* 0x01b0 */ float gear_box_final_drive_ratio_lower_bound; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/GearBoxFinalDriveRatioLowerBound, plain float
    /* 0x01b4 */ float gear_box_final_drive_ratio_upper_bound; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/GearBoxFinalDriveRatioUpperBound, plain float
    /* 0x01b8 */ float gear_box_ratio_reverse_lower_bound; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/GearBoxRatioReverseLowerBound, plain float
    /* 0x01bc */ float gear_box_ratio_neutral_lower_bound; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/GearBoxRatioNeutralLowerBound, plain float
    /* 0x01c0 */ float gear_box_ratio1st_lower_bound; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/GearBoxRatio1stLowerBound, plain float
    /* 0x01c4 */ float gear_box_ratio2nd_lower_bound; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/GearBoxRatio2ndLowerBound, plain float
    /* 0x01c8 */ float gear_box_ratio3rd_lower_bound; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/GearBoxRatio3rdLowerBound, plain float
    /* 0x01cc */ float gear_box_ratio4th_lower_bound; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/GearBoxRatio4thLowerBound, plain float
    /* 0x01d0 */ float gear_box_ratio5th_lower_bound; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/GearBoxRatio5thLowerBound, plain float
    /* 0x01d4 */ float gear_box_ratio6th_lower_bound; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/GearBoxRatio6thLowerBound, plain float
    /* 0x01d8 */ float gear_box_ratio7th_lower_bound; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/GearBoxRatio7thLowerBound, plain float
    /* 0x01dc */ float gear_box_ratio8th_lower_bound; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/GearBoxRatio8thLowerBound, plain float
    /* 0x01e0 */ float gear_box_ratio9th_lower_bound; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/GearBoxRatio9thLowerBound, plain float
    /* 0x01e4 */ float gear_box_ratio_reverse_upper_bound; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/GearBoxRatioReverseUpperBound, plain float
    /* 0x01e8 */ float gear_box_ratio_neutral_upper_bound; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/GearBoxRatioNeutralUpperBound, plain float
    /* 0x01ec */ float gear_box_ratio1st_upper_bound; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/GearBoxRatio1stUpperBound, plain float
    /* 0x01f0 */ float gear_box_ratio2nd_upper_bound; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/GearBoxRatio2ndUpperBound, plain float
    /* 0x01f4 */ float gear_box_ratio3rd_upper_bound; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/GearBoxRatio3rdUpperBound, plain float
    /* 0x01f8 */ float gear_box_ratio4th_upper_bound; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/GearBoxRatio4thUpperBound, plain float
    /* 0x01fc */ float gear_box_ratio5th_upper_bound; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/GearBoxRatio5thUpperBound, plain float
    /* 0x0200 */ float gear_box_ratio6th_upper_bound; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/GearBoxRatio6thUpperBound, plain float
    /* 0x0204 */ float gear_box_ratio7th_upper_bound; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/GearBoxRatio7thUpperBound, plain float
    /* 0x0208 */ float gear_box_ratio8th_upper_bound; // L2 decomp:re/decomp/1446d1660.c;kb:subsystems/car.md#tuning VTF F1Tuning/GearBoxRatio8thUpperBound, plain float
    /* 0x020c */ float gear_box_ratio9th_upper_bound; // L1 decomp:re/decomp/1446d1660.c (destination lost in the decomp; offset from the read order) VTF F1Tuning/GearBoxRatio9thUpperBound, plain float
    /* 0x210 */ float chassis_com_z_frac;        // L2 decomp:re/decomp/140c11550.c;xref:0x140c134f3 chassis z-com slider fraction -> Chassis+0x158
    /* 0x214 */ float diff_on_throttle_frac;     // L2 decomp:re/decomp/140c11550.c;xref:0x140c11845 differential on-throttle slider fraction -> Differential+0xf0
    /* 0x218 */ float diff_off_throttle_frac;    // L2 decomp:re/decomp/140c11550.c;xref:0x140c1195e differential off-throttle slider fraction -> Differential+0xec
    /* 0x21c */ float diff_mid_corner_frac;      // L2 decomp:re/decomp/140c11550.c;xref:0x140c11a7e differential mid-corner slider fraction -> Differential+0xf4
    /* 0x220 */ float engine_braking_frac;       // L2 decomp:re/decomp/140c11550.c;xref:0x140c12cea engine braking slider fraction -> Engine+0x252c
    /* 0x228 */ float front_camber_frac;         // L2 decomp:re/decomp/140c11550.c;xref:0x140c11cf7 front camber slider fraction -> front axle +0x1650
    /* 0x22c */ float rear_camber_frac;          // L2 decomp:re/decomp/140c11550.c;xref:0x140c11e43 rear camber slider fraction -> rear axle +0x1650
    /* 0x230 */ float front_toe_frac;            // L2 decomp:re/decomp/140c11550.c;xref:0x140c11f83 front toe slider fraction -> front axle +0x1654
    /* 0x234 */ float rear_toe_frac;             // L2 decomp:re/decomp/140c11550.c;xref:0x140c120c3 rear toe slider fraction -> rear axle +0x1654
    /* 0x238 */ float front_spring_frac; // L2 decomp:re/decomp/1446d7580.c;kb:subsystems/car.md#tuning front suspension slider fraction -> front axle +0x165c spring (lerp FrontSuspensionSpringConstant) and +0x1660 third spring (lerp FrontSuspensionThirdSpring; handling_version < 7 keeps axle +0x1610)
    /* 0x23c */ float rear_spring_frac; // L2 decomp:re/decomp/1446d7780.c;kb:subsystems/car.md#tuning rear suspension slider fraction -> rear axle +0x165c / +0x1660
    /* 0x240 */ float front_rest_length; // L2 decomp:re/decomp/1446d7980.c;kb:subsystems/car.md#tuning handling_version < 7 front ride-height slider: rest length written to front axle +0x1648
    /* 0x244 */ float rear_rest_length; // L2 decomp:re/decomp/1446d7b00.c;kb:subsystems/car.md#tuning handling_version < 7 rear ride-height slider: rest length -> rear axle +0x1648
    /* 0x248 */ float front_ride_height; // L2 decomp:re/decomp/1446d7c80.c;kb:subsystems/car.md#tuning handling_version >= 7 front ride height -> front axle +0x164c (solved from springs, Chassis mass XOR +0x98/+0xa0 and COM z +0x158)
    /* 0x24c */ float rear_ride_height; // L2 decomp:re/decomp/1446d8080.c;kb:subsystems/car.md#tuning handling_version >= 7 rear ride height -> rear axle +0x164c
    /* 0x250 */ float front_arb_frac;            // L2 decomp:re/decomp/140c11550.c;xref:0x140c1254d front anti-roll bar slider fraction -> front axle +0x1664
    /* 0x254 */ float rear_arb_frac;             // L2 decomp:re/decomp/140c11550.c;xref:0x140c12693 rear anti-roll bar slider fraction -> rear axle +0x1664
    /* 0x2a0 */ float diff_high_speed_kph;       // L2 decomp:re/decomp/140c11550.c;xref:0x140c11b87 slider value clamped to [DifferentialHighSpeedKPHMin, Max], also written to Differential+0xf8
    /* 0x2a4 */ float diff_preload;              // L2 decomp:re/decomp/140c11550.c;xref:0x140c11c75 slider value clamped to [DifferentialPreloadMin, Max], also written to Differential+0xfc
};
