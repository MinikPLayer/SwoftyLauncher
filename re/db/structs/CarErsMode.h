// struct: CarErsMode
// subsystem: car
// size: 0x40  L2 decomp:re/decomp/1446cfcd0.c (modes at data +0x1868/+0x18a8/+0x18e8/+0x1928)
// notes: one ERS deploy mode inside CarErs.modes (read by 0x155477130).
struct CarErsMode {
    /* 0x0000 */ uint8_t name[16];                            // L2 decomp:re/decomp/155477130.c;kb:subsystems/car.md#runtime VTF Mode/Name short name (None, Medium, Overtake, Hotlap)
    /* 0x0010 */ float gear_modifier[11];                     // L2 decomp:re/decomp/155477130.c;decomp:re/decomp/1446e2290.c;xref:0x155477152 deploy fraction per gear index (0 R, 1 N, 2 1st .. 9 8th): FirstGearModifier .. EighthGearModifier; Car_Powertrain_Step reads mode+0x10+gear*4
    /* 0x003c */ float derate_start_speed;                    // L2 decomp:re/decomp/155477130.c;kb:subsystems/car.md#runtime VTF Mode/DerateStartSpeed m/s (Medium 75)
};
