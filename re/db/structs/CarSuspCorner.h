// struct: CarSuspCorner
// subsystem: car
// size: ?  L1 decomp:re/decomp/1446aa090.c
// notes: VTF SuspensionSystem/Suspension[name] record (CarSuspension.suspension_types[i]), plain floats: geometry, ride
// height, bump stops, springs, dampers, anti-roll. The 0x1670 axle objects at CarSuspension+0x90/+0x98 appear to hold one of these
// at +8 (StepVehicle reads axle+0x15e4 = RideHeightOffset in the ride-height average): L1, needs a trace.
struct CarSuspCorner {
    /* 0x0000 */ char name[16];                               // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension Suspension name attribute
    /* 0x0010 */ char tyre_type[16];                          // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/TyreType
    /* 0x0020 */ char brake_type[16];                         // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/BrakeType
    /* 0x0030 */ float camber;                                // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/Camber
    /* 0x0034 */ float castor;                                // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/Castor
    /* 0x0038 */ float toe;                                   // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/Toe
    /* 0x003c */ float steer_pivot_offset_inboard;            // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/SteerPivotOffsetInboard
    /* 0x0040 */ float steer_pivot_offset_fwd;                // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/SteerPivotOffsetFwd
    /* 0x0044 */ float steer_inclination;                     // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/SteerInclination
    /* 0x0048 */ CarSpline height_to_xoffset;                 // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/HeightToXOffset
    /* 0x02f0 */ CarSpline height_to_zoffset;                 // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/HeightToZOffset
    /* 0x0598 */ CarSpline height_to_toe_offset;              // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/HeightToToeOffset
    /* 0x0840 */ CarSpline height_to_camber_offset;           // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/HeightToCamberOffset
    /* 0x0ae8 */ CarSpline height_to_hub_rotation;            // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/HeightToHubRotation
    /* 0x0d90 */ CarSpline height_to_spring_motion_ratio;     // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/HeightToSpringMotionRatio
    /* 0x1038 */ CarSpline height_to3rd_spring_motion_ratio;  // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/HeightTo3rdSpringMotionRatio
    /* 0x12e0 */ CarSpline height_to_anti_roll_motion_ratio;  // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/HeightToAntiRollMotionRatio
    /* 0x1588 */ float local_rest_position_x;                 // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/LocalRestPositionX
    /* 0x158c */ float local_rest_position_y;                 // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/LocalRestPositionY
    /* 0x1590 */ float local_rest_position_z;                 // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/LocalRestPositionZ
    /* 0x1594 */ uint8_t use_model_positions;                 // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/UseModelPositions
    /* 0x1598 */ float local_render_position_x;               // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/LocalRenderPositionX
    /* 0x15a0 */ float local_render_position_z;               // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/LocalRenderPositionZ
    /* 0x15b0 */ float min_height;                            // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/MinHeight
    /* 0x15b4 */ float max_height;                            // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/MaxHeight
    /* 0x15b8 */ float rest_length;                           // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/RestLength
    /* 0x15bc */ float ride_height;                           // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/RideHeight
    /* 0x15c4 */ float bump_stop_height;                      // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/BumpStopHeight
    /* 0x15c8 */ float bump_stop_initial_force;               // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/BumpStopInitialForce
    /* 0x15cc */ float bump_stop_spring_const;                // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/BumpStopSpringConst
    /* 0x15d0 */ float bump_stop_rising_spring;               // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/BumpStopRisingSpring
    /* 0x15d4 */ float bump_stop_damping;                     // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/BumpStopDamping
    /* 0x15d8 */ float bump_stop_rising_damping;              // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/BumpStopRisingDamping
    /* 0x15dc */ float ride_height_offset;                    // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/RideHeightOffset
    /* 0x15e0 */ float wheel_linear_factor;                   // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/WheelLinearFactor
    /* 0x15e4 */ float wheel_quadratic_factor;                // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/WheelQuadraticFactor
    /* 0x15e8 */ float wheel_camber_factor;                   // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/WheelCamberFactor
    /* 0x15ec */ float wheel_linear_factor_neg;               // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/WheelLinearFactorNeg
    /* 0x15f0 */ float wheel_quadratic_factor_neg;            // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/WheelQuadraticFactorNeg
    /* 0x15f4 */ float wheel_camber_factor_neg;               // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/WheelCamberFactorNeg
    /* 0x15f8 */ float wheel_visual_camber;                   // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/WheelVisualCamber
    /* 0x15fc */ float wheel_visual_toe;                      // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/WheelVisualToe
    /* 0x1600 */ float roll_centre;                           // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/RollCentre
    /* 0x1604 */ float spring_constant;                       // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/SpringConstant
    /* 0x1608 */ float third_spring_constant;                 // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/ThirdSpringConstant
    /* 0x160c */ float third_spring_damping;                  // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/ThirdSpringDamping
    /* 0x1610 */ float slow_bump_damping;                     // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/SlowBumpDamping
    /* 0x1614 */ float fast_bump_damping;                     // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/FastBumpDamping
    /* 0x1618 */ float fast_rebound_damping;                  // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/FastReboundDamping
    /* 0x161c */ float slow_rebound_damping;                  // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/SlowReboundDamping
    /* 0x1620 */ float damping_zone_division;                 // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/DampingZoneDivision
    /* 0x1624 */ float anti_roll_constant;                    // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Suspension/AntiRollConstant
};
