// struct: CarDamageSuspension
// subsystem: car
// size: ? L2 decomp:re/decomp/1446533e0.c;decomp:re/decomp/144653070.c
// notes: the F1SuspensionDamage system (CarLive+0x5d0), offsets from the system pointer (data object at +0x18, reader offset = field - 0x18). Data-class vtable 0x145bf8af0: slot 1
// 0x144653060 (type name), slot 2 Car_Read_F1SuspensionDamage 0x1446533e0, slot 3 Car_Write_F1SuspensionDamage
// 0x144653070. +0x28..+0x70 are XOR pairs (value = u32 at off ^ u32 at off+8); the fracture limits are plain.
// Linkage list: one {Node name, Type} per VTF Linkage, Type "ball" / "rod" / "chickenwing" -> enum.
struct CarDamageSuspension {
    /* 0x0028 */ uint32_t speed_threshold_obf;     // L2 decomp:re/decomp/1446533e0.c;decomp:re/decomp/144653070.c;kb:subsystems/car.md#damage VTF F1SuspensionDamage/SpeedThreshold (2025: 7), XOR with 0x30
    /* 0x0030 */ uint32_t speed_threshold_key;     // L2 decomp:re/decomp/1446533e0.c;decomp:re/decomp/144653070.c;kb:subsystems/car.md#damage XOR key
    /* 0x0038 */ uint32_t front_damage_scale_obf;  // L2 decomp:re/decomp/1446533e0.c;decomp:re/decomp/144653070.c;kb:subsystems/car.md#damage VTF FrontDamageScale (2025: 0.001), XOR with 0x40
    /* 0x0040 */ uint32_t front_damage_scale_key;  // L2 decomp:re/decomp/1446533e0.c;decomp:re/decomp/144653070.c;kb:subsystems/car.md#damage XOR key
    /* 0x0048 */ uint32_t rear_damage_scale_obf;   // L2 decomp:re/decomp/1446533e0.c;decomp:re/decomp/144653070.c;kb:subsystems/car.md#damage VTF RearDamageScale (2025: 0.0001), XOR with 0x50
    /* 0x0050 */ uint32_t rear_damage_scale_key;   // L2 decomp:re/decomp/1446533e0.c;decomp:re/decomp/144653070.c;kb:subsystems/car.md#damage XOR key
    /* 0x0058 */ uint32_t wheel_detach_scale_obf;  // L2 decomp:re/decomp/1446533e0.c;decomp:re/decomp/144653070.c;kb:subsystems/car.md#damage VTF WheelDetachScale (2025: 1), XOR with 0x60
    /* 0x0060 */ uint32_t wheel_detach_scale_key;  // L2 decomp:re/decomp/1446533e0.c;decomp:re/decomp/144653070.c;kb:subsystems/car.md#damage XOR key
    /* 0x0068 */ uint32_t parts_detach_scale_obf;  // L2 decomp:re/decomp/1446533e0.c;decomp:re/decomp/144653070.c;kb:subsystems/car.md#damage raised by the reader to max(1.5 x wheel_detach_scale, value); VTF PartsDetachScale (2025: 1), XOR with 0x70
    /* 0x0070 */ uint32_t parts_detach_scale_key;  // L2 decomp:re/decomp/1446533e0.c;decomp:re/decomp/144653070.c;kb:subsystems/car.md#damage XOR key
    /* 0x0078 */ float linear_fracture_limit;      // L2 decomp:re/decomp/1446533e0.c;decomp:re/decomp/144653070.c;kb:subsystems/car.md#damage VTF LinearFractureLimit (2025: 50), plain
    /* 0x007c */ float angular_fracture_limit;     // L2 decomp:re/decomp/1446533e0.c;decomp:re/decomp/144653070.c;kb:subsystems/car.md#damage VTF AngularFractureLimit (2025: 30), plain
    /* 0x138 */ float front_left_damage;   // L2 decomp:re/decomp/14465fdc0.c;xref:0x14466013a front-left suspension damage; Car_FrontWing_OnCollision adds wing overflow * FrontDamageScale (XOR +0x38/+0x40) here
    /* 0x13c */ float front_right_damage;  // L2 decomp:re/decomp/14465fdc0.c;xref:0x1446601fc front-right suspension damage (same path, right side)
};
