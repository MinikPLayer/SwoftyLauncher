// struct: CarSuspBrakeType
// subsystem: car
// size: ?  L1 decomp:re/decomp/1446aa090.c
// notes: VTF SuspensionSystem/Brake[name] record (CarSuspension.brake_types[i]); plain floats except the seven XOR pairs at +0x30..+0x90
// (Density, Conductivity, RotorOutsideRadius, RotorInsideRadius, RotorThickness, SurfaceLayerThickness, CoolingHoleRadius; V1285).
struct CarSuspBrakeType {
    /* 0x0000 */ char name[16];                               // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension Brake name attribute
    /* 0x0010 */ uint8_t handbrake_support;                   // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Brake/HandbrakeSupport
    /* 0x0014 */ float handbrake_brake_multiple;              // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Brake/HandbrakeBrakeMultiple
    /* 0x0018 */ float handbrake_grip_multiple;               // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Brake/HandbrakeGripMultiple
    /* 0x001c */ float specific_heat_capacity;                // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Brake/SpecificHeatCapacity
    /* 0x0020 */ float coef_heat_transfer_constant;           // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Brake/CoefHeatTransferConstant; addon radUpgradeBrakeCooling * 1.25
    /* 0x0024 */ float coef_heat_transfer_linear;             // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Brake/CoefHeatTransferLinear; addon radUpgradeBrakeCooling * 1.25
    /* 0x0028 */ float coef_heat_transfer_quadratic;          // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Brake/CoefHeatTransferQuadratic
    /* 0x0030 */ CarObfFloat density;                      // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Brake/Density; XOR pair (V1285)
    /* 0x0040 */ CarObfFloat conductivity;                 // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Brake/Conductivity; XOR pair (V1285)
    /* 0x0050 */ CarObfFloat rotor_outside_radius;         // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Brake/RotorOutsideRadius; XOR pair (V1285)
    /* 0x0060 */ CarObfFloat rotor_inside_radius;          // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Brake/RotorInsideRadius; XOR pair (V1285)
    /* 0x0070 */ CarObfFloat rotor_thickness;              // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Brake/RotorThickness; XOR pair (V1285)
    /* 0x0080 */ CarObfFloat surface_layer_thickness;      // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Brake/SurfaceLayerThickness; XOR pair (V1285)
    /* 0x0090 */ CarObfFloat cooling_hole_radius;          // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Brake/CoolingHoleRadius; XOR pair (V1285)
    /* 0x00a0 */ CarSpline temp_efficiency_spline;            // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Brake/TempEfficiencySpline
    /* 0x0348 */ float min_damage_brake_loss;                 // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Brake/MinDamageBrakeLoss
    /* 0x034c */ float max_brake_loss_percent;                // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Brake/MaxBrakeLossPercent
    /* 0x0350 */ float damage_accretion_rate;                 // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/Brake/DamageAccretionRate
};
