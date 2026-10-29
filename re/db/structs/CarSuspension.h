// struct: CarSuspension
// subsystem: car
// size: 0xd8  L2 decomp:re/decomp/144753050.c (alloc 0xd8)
// notes: live SuspensionSystem at CarLive+0x580 (Car_Create_SuspensionSystem 0x144753050). The VTF reader
// Car_Read_SuspensionSystem 0x1446aa090 (data object +0x18) fills per-name type records with PLAIN floats (no XOR).
struct CarSuspension {
    /* 0x0000 */ void *vtable;                                // L2 decomp:re/decomp/144753050.c;kb:subsystems/car.md#layout system vtable 0x145bfbab0
    /* 0x0008 */ CarLive *car;                                // L2 decomp:re/decomp/144753050.c;kb:subsystems/car.md#layout owning car (factory argument); StepVehicle reads [sys+8]+0x568/+0x580
    /* 0x0010 */ void *allocator;                             // L2 decomp:re/decomp/144753050.c;kb:subsystems/car.md#layout car allocator [car+0xd0]
    /* 0x0018 */ void *data_vtable;                           // L2 decomp:re/decomp/144753050.c;kb:subsystems/car.md#layout embedded data object vtable 0x145bfba10 {dtor, GetName, Car_Read_<X>, Car_Write_<X>}
    /* 0x0028 */ uint32_t num_tyre_types;                     // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem numTyreTypes (max of read and current)
    /* 0x002c */ uint32_t num_brake_types;                    // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension numBrakeTypes
    /* 0x0030 */ uint32_t num_suspension_types;               // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension numSuspensionTypes
    /* 0x0038 */ CarSuspTyreType *tyre_types[2];              // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF Tyre name=Front/Rear records, matched by name, appended in VTF order (2025 VTFs: Rear first)
    /* 0x0048 */ CarSuspBrakeType *brake_types[2];            // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF Brake name=... records
    /* 0x0058 */ CarSuspCorner *suspension_types[2];          // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF Suspension name=... records (TyreType/BrakeType link by short name)
    /* 0x0068 */ uint8_t loaded;                              // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension set 1 at the end of the read
    /* 0x0069 */ uint8_t use_legacy_zero_force_height;        // L2 decomp:re/decomp/1446aa090.c;kb:subsystems/car.md#suspension VTF SuspensionSystem/UseLegacyZeroForceHeight bool
    /* 0x0070 */ CarWheel *wheels[4];                         // L2 decomp:re/decomp/144753050.c;xref:0x1446dceb0 0x480-byte wheels built by 0x1446af9c0(wheel, car, axle, side, ?): +0x70/+0x78 on axle +0x90 (RL, RR), +0x80/+0x88 on axle +0x98 (FL, FR)
    /* 0x0090 */ CarSuspAxle *axle0;                          // L2 decomp:re/decomp/144753050.c;kb:subsystems/car.md#suspension 0x1670 axle (ctor 0x1446a7a50 at +8); wheels 0/1 (RL, RR on 2025 cars). Setup picks the rear axle by LocalRestPositionZ <= 0, not by slot
    /* 0x0098 */ CarSuspAxle *axle1;                          // L2 decomp:re/decomp/144753050.c;kb:subsystems/car.md#suspension same type; wheels 2/3 (FL, FR)
};
