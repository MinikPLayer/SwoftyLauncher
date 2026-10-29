// struct: CarSetupValues
// subsystem: car
// size: 0xa8 L1 decomp:re/decomp/140c11550.c (two copies at setup+0x08 and setup+0xb0; selector byte setup+0x158)
// notes: the car-setup slider block Car_Setup_ApplyTuning (arg 3) applies. The function reads setup+0xb0
// when the byte setup+0x158 is set, else setup+0x08; offsets below are relative to that block. Each value
// is an int step; the fraction is step / [global step-count object +8] (one global per slider at
// 0x14605a6d8.. , e.g. 0x14605a6d8 diff on-throttle, 0x14605a700 front camber) clamped to 0..1. Arg 2 is a
// u64 skip mask: bit set = slider not applied (bit per slider in the order below). The wing levels are
// raw ints clamped to the F1Aero maximum level (XOR pair F1Aero+0x1748/+0x1750 front, +0x17b8/+0x17c0 rear).
struct CarSetupValues {
    /* 0x00 */ uint32_t front_wing;            // L2 decomp:re/decomp/140c11550.c;xref:0x140c115e2 mask bit 0x1; -> F1Aero+0x19b4 level, +0x19b8/+0x19bc = (x*c2)^2 + x*c1 + c0 from XOR pairs F1Aero+0x1758..+0x17b0
    /* 0x04 */ uint32_t rear_wing;             // L2 decomp:re/decomp/140c11550.c;xref:0x140c116d1 mask bit 0x2; -> F1Aero+0x19c0 level, +0x19c4/+0x19c8 from XOR pairs F1Aero+0x17c8..+0x1820
    /* 0x08 */ int32_t diff_on_throttle;       // L2 decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning mask 0x4; -> CarTuning+0x214
    /* 0x0c */ int32_t diff_off_throttle;      // L2 decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning mask 0x8; -> CarTuning+0x218
    /* 0x10 */ int32_t diff_mid_corner;        // L2 decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning mask 0x10; -> CarTuning+0x21c
    /* 0x14 */ int32_t diff_high_speed;        // L2 decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning mask 0x20; -> Differential+0xf8 (clamped)
    /* 0x18 */ int32_t diff_preload;           // L2 decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning mask 0x40; -> Differential+0xfc (clamped)
    /* 0x1c */ int32_t front_camber;           // L2 decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning mask 0x80; -> front axle +0x1650
    /* 0x20 */ int32_t rear_camber;            // L2 decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning mask 0x100; -> rear axle +0x1650
    /* 0x24 */ int32_t front_toe;              // L2 decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning mask 0x200; -> front axle +0x1654
    /* 0x28 */ int32_t rear_toe;               // L2 decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning mask 0x400; -> rear axle +0x1654
    /* 0x2c */ int32_t front_suspension;       // L2 decomp:re/decomp/140c11550.c;decomp:re/decomp/1446d7580.c;kb:subsystems/car.md#tuning mask 0x800; -> thunk 0x1446d7580: CarTuning+0x238, front axle +0x165c spring, +0x1660 third spring
    /* 0x30 */ int32_t rear_suspension;        // L2 decomp:re/decomp/140c11550.c;decomp:re/decomp/1446d7780.c;kb:subsystems/car.md#tuning mask 0x1000; -> thunk 0x1446d7780: rear axle +0x165c / +0x1660
    /* 0x34 */ int32_t front_ride_height;      // L2 decomp:re/decomp/140c11550.c;decomp:re/decomp/1446d7980.c;decomp:re/decomp/1446d7c80.c;kb:subsystems/car.md#tuning mask 0x2000; < 7 rest length axle +0x1648, >= 7 ride height axle +0x164c; -> thunk 0x1446d7980 when CarLive.handling_version < 7, else 0x1446d7c80
    /* 0x38 */ int32_t rear_ride_height;       // L2 decomp:re/decomp/140c11550.c;decomp:re/decomp/1446d7b00.c;decomp:re/decomp/1446d8080.c;kb:subsystems/car.md#tuning mask 0x4000; -> thunk 0x1446d7b00 when handling_version < 7, else 0x1446d8080
    /* 0x3c */ int32_t front_arb;              // L2 decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning mask 0x8000; -> front axle +0x1664
    /* 0x40 */ int32_t rear_arb;               // L2 decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning mask 0x10000; -> rear axle +0x1664
    /* 0x44 */ int32_t brake_pressure;         // L1 decomp:re/decomp/140c11550.c mask 0x60000 (pair with 0x48); -> BrakeECU+0x21dc = lerp(BrakePressureMin, Max); which of 0x44/0x48 is pressure vs bias needs the log
    /* 0x48 */ int32_t brake_bias;             // L1 decomp:re/decomp/140c11550.c mask 0x60000; -> BrakeECU+0x21d8 fraction
    /* 0x4c */ int32_t brake_migration;        // L2 decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning mask 0x80000; -> BrakeECU+0x21fc
    /* 0x54 */ int32_t tyre_pressure[4];       // L2 decomp:re/decomp/140c11550.c;decomp:re/decomp/1446d8620.c;kb:subsystems/car.md#tuning lerp Front/RearTyrePressureMin/Max (CarTuning +0x114/+0x118, +0x120/+0x124); mask 0x200000..0x1000000; -> thunk 0x1446d8620(sys, wheel 3..0, 0)
    /* 0x64 */ int32_t chassis_com_z;          // L2 decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning mask 0x2000000; -> Chassis+0x158
    /* 0x68 */ int32_t fuel_load;              // L1 decomp:re/decomp/140c11550.c mask 0x4000000; -> FuelTank+0x100 clamped to FuelCapacity (XOR FuelTank+0x98/+0xa0), skipped when FuelTank+0xf8 is set
    /* 0x6c */ int32_t engine_braking;         // L2 decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning mask 0x8000000; -> CarTuning+0x220, Engine+0x252c
    /* 0x94 */ int32_t brake_cooling_front;    // L2 decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning -> front wheels (SuspensionSystem+0x80/+0x88) +0x134
    /* 0x98 */ int32_t brake_cooling_rear;     // L2 decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning -> rear wheels (SuspensionSystem+0x70/+0x78) +0x134
    /* 0x9c */ int32_t brake_duct_front;       // L2 decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning -> front wheels +0x130
    /* 0xa0 */ int32_t brake_duct_rear;        // L2 decomp:re/decomp/140c11550.c;kb:subsystems/car.md#tuning -> rear wheels +0x130
};
