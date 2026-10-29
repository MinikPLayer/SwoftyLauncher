// struct: CarSteering
// subsystem: car
// size: 0x140  L2 decomp:re/decomp/14474e650.c (alloc 0x140)
// notes: live Steering system at CarLive+0x560. Reader Car_Read_Steering 0x1446a7070 (thunk to .data1 0x15541f940).
// The VTF Upgrade[very_high] block overrides MaxSteeringLock 0.41 / AckermannSteering -0.04363 (assist level block).
struct CarSteering {
    /* 0x0000 */ void *vtable;                                // L2 decomp:re/decomp/14474e650.c;kb:subsystems/car.md#layout system vtable 0x145bfb400
    /* 0x0008 */ CarLive *car;                                // L2 decomp:re/decomp/14474e650.c;kb:subsystems/car.md#layout owning car (factory argument); StepVehicle reads [sys+8]+0x568/+0x580
    /* 0x0010 */ void *allocator;                             // L2 decomp:re/decomp/14474e650.c;kb:subsystems/car.md#layout car allocator [car+0xd0]
    /* 0x0018 */ void *data_vtable;                           // L2 decomp:re/decomp/14474e650.c;kb:subsystems/car.md#layout embedded data object vtable 0x145bfb310 {dtor, GetName, Car_Read_<X>, Car_Write_<X>}
    /* 0x0028 */ CarObfFloat max_steering_lock;               // L2 decomp:re/decomp/1446a7070.c;kb:subsystems/car.md#steering VTF Steering/MaxSteeringLock rad (2025 0.34034)
    /* 0x0038 */ CarObfFloat ackermann_steering;              // L2 decomp:re/decomp/1446a7070.c;kb:subsystems/car.md#steering VTF Steering/AckermannSteering
    /* 0x0048 */ CarObfFloat visual_turn_rate;                // L2 decomp:re/decomp/1446a7070.c;kb:subsystems/car.md#steering VTF Steering/VisualTurnRate
    /* 0x0058 */ CarObfFloat min_speed_for_wobble;            // L2 decomp:re/decomp/1446a7070.c;kb:subsystems/car.md#steering VTF Steering/MinSpeedForWobble m/s
    /* 0x0068 */ CarObfFloat max_speed_for_wobble;            // L2 decomp:re/decomp/1446a7070.c;kb:subsystems/car.md#steering VTF Steering/MaxSpeedForWobble m/s
    /* 0x0078 */ CarObfFloat min_damage_for_wobble;           // L2 decomp:re/decomp/1446a7070.c;kb:subsystems/car.md#steering VTF Steering/MinDamageForWobble
    /* 0x0088 */ CarObfFloat max_damage_for_wobble;           // L2 decomp:re/decomp/1446a7070.c;kb:subsystems/car.md#steering VTF Steering/MaxDamageForWobble
    /* 0x0098 */ CarObfFloat damage_wobble_rate_a;            // L2 decomp:re/decomp/1446a7070.c;kb:subsystems/car.md#steering VTF Steering/DamageWobbleRateA
    /* 0x00a8 */ CarObfFloat damage_wobble_rate_b;            // L2 decomp:re/decomp/1446a7070.c;kb:subsystems/car.md#steering VTF Steering/DamageWobbleRateB
    /* 0x00b8 */ CarObfFloat damage_wobble_angle;             // L2 decomp:re/decomp/1446a7070.c;kb:subsystems/car.md#steering VTF Steering/DamageWobbleAngle rad
};
