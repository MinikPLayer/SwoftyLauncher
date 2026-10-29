// struct: CarBrakeEcu
// subsystem: car
// size: 0x2208  L2 decomp:re/decomp/1555d2450.c (alloc 0x2208)
// notes: live BrakeECU system at CarLive+0x550 (ctor .data1 0x1555d2450, data ctor 0x14466e570). Reader
// Car_Read_BrakeECU 0x1446752f0. Brake torques are CarObfFloat; the ABS/ATC rates are plain floats.
struct CarBrakeEcu {
    /* 0x0000 */ void *vtable;                                // L2 decomp:re/decomp/1555d2450.c;kb:subsystems/car.md#layout system vtable 0x145bf9bb0
    /* 0x0008 */ CarLive *car;                                // L2 decomp:re/decomp/1555d2450.c;kb:subsystems/car.md#layout owning car (factory argument); StepVehicle reads [sys+8]+0x568/+0x580
    /* 0x0010 */ void *allocator;                             // L2 decomp:re/decomp/1555d2450.c;kb:subsystems/car.md#layout car allocator [car+0xd0]
    /* 0x0018 */ void *data_vtable;                           // L2 decomp:re/decomp/1555d2450.c;kb:subsystems/car.md#layout embedded data object vtable 0x145bf9730 {dtor, GetName, Car_Read_<X>, Car_Write_<X>}
    /* 0x0028 */ CarSpline speed_abspeak_slip_factor_spline_front; // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/SpeedABSPeakSlipFactorSplineFront spline (fallback key without Front/Rear suffix)
    /* 0x02d0 */ CarSpline speed_abspeak_slip_factor_spline_rear; // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/SpeedABSPeakSlipFactorSplineRear spline
    /* 0x0578 */ CarSpline speed_absmax_braking_scalar_spline; // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/SpeedABSMaxBrakingScalarSpline spline
    /* 0x0820 */ CarSpline speed_atcpeak_slip_factor_spline;  // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/SpeedATCPeakSlipFactorSpline spline
    /* 0x0ac8 */ CarSpline atcstrength_throttle_factor_spline; // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/ATCStrengthThrottleFactorSpline spline
    /* 0x0d70 */ CarSpline atcstrength_peak_slip_factor_spline; // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/ATCStrengthPeakSlipFactorSpline spline
    /* 0x1018 */ CarSpline speed_abspeak_slip_factor_spline_wet_front; // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/SpeedABSPeakSlipFactorSplineWetFront spline (fallback key without Front/Rear suffix)
    /* 0x12c0 */ CarSpline speed_abspeak_slip_factor_spline_wet_rear; // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/SpeedABSPeakSlipFactorSplineWetRear spline
    /* 0x1568 */ CarSpline speed_absmax_braking_scalar_spline_wet; // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/SpeedABSMaxBrakingScalarSplineWet spline
    /* 0x1810 */ CarSpline speed_atcpeak_slip_factor_spline_wet; // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/SpeedATCPeakSlipFactorSplineWet spline
    /* 0x1ab8 */ CarSpline atcstrength_throttle_factor_spline_wet; // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/ATCStrengthThrottleFactorSplineWet spline
    /* 0x1d60 */ CarSpline atcstrength_peak_slip_factor_spline_wet; // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/ATCStrengthPeakSlipFactorSplineWet spline
    /* 0x2008 */ CarObfFloat brake_front;                     // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/BrakeFront N m brake torque (2025 3105); radUpgradeBrakes op * 1.4
    /* 0x2018 */ CarObfFloat brake_rear;                      // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/BrakeRear N m (2295); radUpgradeBrakes * 1.4
    /* 0x2028 */ CarObfFloat brake_front_backwards;           // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/BrakeFrontBackwards
    /* 0x2038 */ CarObfFloat brake_rear_backwards;            // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/BrakeRearBackwards
    /* 0x2048 */ CarObfFloat brake_front_forwards;            // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/BrakeFrontForwards
    /* 0x2058 */ CarObfFloat brake_rear_forwards;             // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/BrakeRearForwards
    /* 0x2068 */ CarObfFloat brake_migration_torque;          // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/BrakeMigrationTorque
    /* 0x2078 */ CarObfFloat brake_migration_torque_ai;       // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/BrakeMigrationTorqueAI AI-only value
    /* 0x2088 */ CarObfFloat brake_pressure;                  // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/BrakePressure
    /* 0x2098 */ CarObfFloat brake_atcstrength;               // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/BrakeATCStrength
    /* 0x20a8 */ CarObfFloat atcmedium_grip_factor_player;    // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/ATCMediumGripFactorPlayer
    /* 0x20b8 */ CarObfFloat atcmedium_grip_factor_player_wet; // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/ATCMediumGripFactorPlayerWet
    /* 0x20c8 */ CarObfFloat brake_absstrength;               // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/BrakeABSStrength
    /* 0x20d8 */ CarObfFloat abson_grip_factor_player;        // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/ABSOnGripFactorPlayer
    /* 0x20e8 */ CarObfFloat abson_grip_factor_player_wet;    // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/ABSOnGripFactorPlayerWet
    /* 0x20f8 */ CarObfFloat ascstrength_on_full_atc;         // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/ASCStrengthOnFullATC
    /* 0x2108 */ CarObfFloat ascmax_brake;                    // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/ASCMaxBrake
    /* 0x2118 */ CarObfFloat ascmin_yaw_ratio;                // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/ASCMinYawRatio
    /* 0x2128 */ CarObfFloat ascmax_yaw_ratio;                // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/ASCMaxYawRatio
    /* 0x2138 */ CarObfFloat ascstrength_on_full_atcwet;      // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/ASCStrengthOnFullATCWet
    /* 0x2148 */ CarObfFloat ascmax_brake_wet;                // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/ASCMaxBrakeWet
    /* 0x2158 */ CarObfFloat ascmin_yaw_ratio_wet;            // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/ASCMinYawRatioWet
    /* 0x2168 */ CarObfFloat ascmax_yaw_ratio_wet;            // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/ASCMaxYawRatioWet
    /* 0x2178 */ CarObfFloat ascstrength_for_ai;              // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/ASCStrengthForAI AI
    /* 0x2188 */ CarObfFloat ascmax_brake_ai;                 // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/ASCMaxBrakeAI AI
    /* 0x2198 */ CarObfFloat ascmin_yaw_ratio_ai;             // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/ASCMinYawRatioAI AI
    /* 0x21a8 */ CarObfFloat ascmax_yaw_ratio_ai;             // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/ASCMaxYawRatioAI AI
    /* 0x21b8 */ float atcactivation_rate;                    // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/ATCActivationRate (plain float)
    /* 0x21bc */ float atcde_activation_rate;                 // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/ATCDeActivationRate (plain float)
    /* 0x21c0 */ float absactivation_rate;                    // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/ABSActivationRate (plain float)
    /* 0x21c4 */ float absde_activation_rate;                 // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/ABSDeActivationRate (plain float)
    /* 0x21c8 */ float abssymmetry_factor;                    // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/ABSSymmetryFactor (plain float)
    /* 0x21cc */ float puncture_throttle_max;                 // L2 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/PunctureThrottleMax (plain float)
    /* 0x21d0 */ float burst_throttle_max;                    // L1 decomp:re/decomp/1446752f0.c;kb:subsystems/car.md#brakeecu VTF BrakeECU/BurstThrottleMax (plain, inlined read)
    /* 0x21d8 */ float brake_bias;                            // L3 decomp:re/decomp/144676250.c;xref:0x140619eb9;trace:T113/2026-10-28-gp:84;trace:T113/2026-10-28-gp:86 0..1, 0.5 = BrakeFront/BrakeRear, below blends to the *Backwards torques, above to *Forwards (Car_BrakeEcu_ApplyBias); setup target per car-d T114 Live: 0.40 for every car after Car_Setup_ApplyTuning.
    /* 0x21dc */ float brake_pressure;                        // L3 decomp:re/decomp/144676250.c;xref:0x140619eb9;trace:T113/2026-10-28-gp:84;trace:T113/2026-10-28-gp:86 multiplies both axle torques in Car_BrakeEcu_ApplyBias Live: 1.0 with the AI setup, 0.97 with the player car's own setup.
    /* 0x21e0 */ float unk_0x21e0;                            // L1 decomp:re/decomp/1555d2450.c ctor 1.0
    /* 0x21e4 */ float unk_0x21e4;                            // L1 decomp:re/decomp/1555d2450.c ctor 1.0
    /* 0x21ec */ uint8_t unk_0x21ec;                          // L1 decomp:re/decomp/1555d2450.c ctor 1
};
