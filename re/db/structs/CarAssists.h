// struct: CarAssists
// subsystem: car
// size: ? L2 decomp:re/decomp/1446cb880.c (highest field 0x20e0)
// notes: the F1Assists system (CarLive+0x598), offsets from the system pointer (data object at +0x18, reader offset = field - 0x18). Data-class vtable 0x145bfcbe0: slot 1 0x1446cb870
// (type name), slot 2 Car_Read_F1Assists 0x1446cb880 (reader; it also loads the pad steering splines),
// slot 3 Car_Write_F1Assists 0x1446cc750. Splines are 0x2a8-byte CarSpline objects (Car_Assets_FindSpline),
// scalars are XOR pairs (value = u32 at off ^ u32 at off+8). These are the pad-steering and traction /
// anti-lock assist curves: Throttle*VSRearPeakSlip* scale the allowed rear slip under throttle (TC),
// BrakeVSRearPeakSlipRatio* under braking (ABS), with wet variants. The SteerModel{Speed,MaxLock,
// SteerInertia,InputStiffness,CounterSteerStiffness}0..2 keys and the Wheel*Saturation/Deadzone keys are
// read by name-formatted loops after 0x20c8 (offsets not pinned).
struct CarAssists {
    /* 0x0028 */ uint8_t steer_out_rate_vs_speed_spline[0x2a8]; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists VTF F1Assists/SteerOutRateVSSpeedSpline (spline)
    /* 0x02d0 */ uint8_t steer_in_rate_vs_speed_spline[0x2a8]; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists VTF F1Assists/SteerInRateVSSpeedSpline (spline)
    /* 0x0578 */ uint8_t lock_proportion_vs_speed_spline[0x2a8]; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists VTF F1Assists/LockProportionVSSpeedSpline (spline)
    /* 0x0820 */ uint8_t reduced_lock_given_back_vs_rear_slip_angle_spline[0x2a8]; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists VTF F1Assists/ReducedLockGivenBackVSRearSlipAngleSpline (spline)
    /* 0x0ac8 */ uint8_t throttle_vs_rear_peak_slip_ratio_proportion_spline[0x2a8]; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists VTF F1Assists/ThrottleVSRearPeakSlipRatioProportionSpline (spline)
    /* 0x0d70 */ uint8_t throttle_vs_rear_peak_slip_angle_proportion_spline[0x2a8]; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists VTF F1Assists/ThrottleVSRearPeakSlipAngleProportionSpline (spline)
    /* 0x1018 */ uint8_t brake_vs_rear_peak_slip_ratio_proportion_spline[0x2a8]; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists VTF F1Assists/BrakeVSRearPeakSlipRatioProportionSpline (spline)
    /* 0x12c0 */ uint8_t throttle_vs_rear_peak_slip_ratio_proportion_wet_spline[0x2a8]; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists VTF F1Assists/ThrottleVSRearPeakSlipRatioProportionWetSpline (spline)
    /* 0x1568 */ uint8_t throttle_vs_rear_peak_slip_angle_proportion_wet_spline[0x2a8]; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists VTF F1Assists/ThrottleVSRearPeakSlipAngleProportionWetSpline (spline)
    /* 0x1810 */ uint8_t brake_vs_rear_peak_slip_ratio_proportion_wet_spline[0x2a8]; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists VTF F1Assists/BrakeVSRearPeakSlipRatioProportionWetSpline (spline)
    /* 0x1ab8 */ uint8_t drift_throttle_prop_vs_rear_peak_slip_angle_prop_spline[0x2a8]; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists VTF F1Assists/DriftThrottlePropVSRearPeakSlipAnglePropSpline (spline)
    /* 0x1d60 */ uint8_t drift_throttle_prop_vs_speed_spline[0x2a8]; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists VTF F1Assists/DriftThrottlePropVSSpeedSpline (spline)
    /* 0x2008 */ uint32_t minimum_steering_lock_obf; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists VTF F1Assists/MinimumSteeringLock, XOR float: value = this ^ u32 at 0x2010
    /* 0x2010 */ uint32_t minimum_steering_lock_key; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists XOR key of MinimumSteeringLock
    /* 0x2018 */ uint32_t steer_deadzone_obf; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists VTF F1Assists/SteerDeadzone, XOR float: value = this ^ u32 at 0x2020
    /* 0x2020 */ uint32_t steer_deadzone_key; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists XOR key of SteerDeadzone
    /* 0x2028 */ uint32_t steer_out_multiple_obf; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists VTF F1Assists/SteerOutMultiple, XOR float: value = this ^ u32 at 0x2030
    /* 0x2030 */ uint32_t steer_out_multiple_key; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists XOR key of SteerOutMultiple
    /* 0x2038 */ uint32_t steer_in_multiple_obf; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists VTF F1Assists/SteerInMultiple, XOR float: value = this ^ u32 at 0x2040
    /* 0x2040 */ uint32_t steer_in_multiple_key; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists XOR key of SteerInMultiple
    /* 0x2048 */ uint32_t lock_reduction_weight_speed_vs_slip_obf; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists VTF F1Assists/LockReductionWeightSpeedVSSlip, XOR float: value = this ^ u32 at 0x2050
    /* 0x2050 */ uint32_t lock_reduction_weight_speed_vs_slip_key; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists XOR key of LockReductionWeightSpeedVSSlip
    /* 0x2058 */ uint32_t lock_extension_reduction_rate_obf; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists VTF F1Assists/LockExtensionReductionRate, XOR float: value = this ^ u32 at 0x2060
    /* 0x2060 */ uint32_t lock_extension_reduction_rate_key; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists XOR key of LockExtensionReductionRate
    /* 0x2068 */ uint32_t linear_vs_non_linear_steering_weight_min_obf; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists VTF F1Assists/LinearVSNonLinearSteeringWeightMin, XOR float: value = this ^ u32 at 0x2070
    /* 0x2070 */ uint32_t linear_vs_non_linear_steering_weight_min_key; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists XOR key of LinearVSNonLinearSteeringWeightMin
    /* 0x2078 */ uint32_t linear_vs_non_linear_steering_weight_max_obf; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists VTF F1Assists/LinearVSNonLinearSteeringWeightMax, XOR float: value = this ^ u32 at 0x2080
    /* 0x2080 */ uint32_t linear_vs_non_linear_steering_weight_max_key; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists XOR key of LinearVSNonLinearSteeringWeightMax
    /* 0x2088 */ uint32_t speed_for_steering_weight_min_obf; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists VTF F1Assists/SpeedForSteeringWeightMin, XOR float: value = this ^ u32 at 0x2090
    /* 0x2090 */ uint32_t speed_for_steering_weight_min_key; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists XOR key of SpeedForSteeringWeightMin
    /* 0x2098 */ uint32_t speed_for_steering_weight_max_obf; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists VTF F1Assists/SpeedForSteeringWeightMax, XOR float: value = this ^ u32 at 0x20a0
    /* 0x20a0 */ uint32_t speed_for_steering_weight_max_key; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists XOR key of SpeedForSteeringWeightMax
    /* 0x20a8 */ uint32_t exponent_for_non_linear_steering_obf; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists VTF F1Assists/ExponentForNonLinearSteering, XOR float: value = this ^ u32 at 0x20b0
    /* 0x20b0 */ uint32_t exponent_for_non_linear_steering_key; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists XOR key of ExponentForNonLinearSteering
    /* 0x20b8 */ uint32_t multiple_to_reach_needed_engine_torque_obf; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists VTF F1Assists/MultipleToReachNeededEngineTorque, XOR float: value = this ^ u32 at 0x20c0
    /* 0x20c0 */ uint32_t multiple_to_reach_needed_engine_torque_key; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists XOR key of MultipleToReachNeededEngineTorque
    /* 0x20c8 */ uint32_t wheel_inertia_obf; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists VTF F1Assists/WheelInertia, XOR float: value = this ^ u32 at 0x20d0
    /* 0x20d0 */ uint32_t wheel_inertia_key; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists XOR key of WheelInertia
    /* 0x20d8 */ uint32_t wheel_steering_deadzone_increment_obf; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists VTF F1Assists/WheelSteeringDeadzoneIncrement, XOR float: value = this ^ u32 at 0x20e0
    /* 0x20e0 */ uint32_t wheel_steering_deadzone_increment_key; // L2 decomp:re/decomp/1446cb880.c;decomp:re/decomp/1446cc750.c;kb:subsystems/car.md#assists XOR key of WheelSteeringDeadzoneIncrement
};
