// struct: CarDamageFrontWing
// subsystem: car
// size: ? L2 decomp:re/decomp/14465fdc0.c
// notes: the F1WingDamage SYSTEM at CarLive+0x5c8 (cxx game::FrontWing, Car_FrontWing_Init 0x14465f620 sets it
// up), offsets from the system pointer: the F1WingDamage data object is embedded at +0x18 (reader offset = field - 0x18;
// data-class vtable vt_Car_F1WingDamageData 0x145bf9030, reader Car_Read_F1WingDamage_Thunk, writer Car_Write_F1WingDamage). Car_FrontWing_OnCollision 0x14465fdc0 is the impact callback: gated by
// CarLive+0x130 bit 0, ignores near-vertical normals (|n.up| > 0.8) and impacts below +0x28/+0x40,
// then damage = surface x +0x40 x impulse * +0x2c (V1300), added to the left or right side by the impact's lateral sign and clamped
// to +0x38; overflow beyond the clamp goes to the F1SuspensionDamage system (CarLive+0x5d0) +0x138 left / +0x13c
// right, scaled by that system's XOR pair +0x38/+0x40. When CarLive+0x130 bit 1 is set it writes
// clamp((left+right)/max, 0, 1) to F1Aero+0x199c (the front wing aero loss factor Car_Physics_StepVehicle reads).
// Detachable parts list at +0xb0..+0xb8 (Car_Register_DetachablePart_Thunk; data flow in car.md#parts).
struct CarDamageFrontWing {
    /* 0x08 */ CarLive *car;               // L2 decomp:re/decomp/14465fdc0.c;decomp:re/decomp/14465f620.c;kb:subsystems/car.md#damage owning car (+0x130 flags read through it)
    /* 0x28 */ float speed_threshold;      // L2 decomp:re/decomp/14465fdc0.c;decomp:re/decomp/14465f580.c;decomp:re/decomp/14465f450.c;xref:0x14465ff05 VTF F1WingDamage/SpeedThreshold (2025: 3), plain (reader +0x10); impacts below +0x28/+0x40 are ignored
    /* 0x2c */ float damage_scale;         // L2 decomp:re/decomp/14465fdc0.c;decomp:re/decomp/14465f580.c;decomp:re/decomp/14465f450.c;xref:0x144660096 VTF F1WingDamage/DamageScale (2025: 0.0001), plain (reader +0x14); impulse -> damage
    /* 0x30 */ float chassis_damage_multiple; // L2 decomp:re/decomp/14465fdc0.c;decomp:re/decomp/14465f580.c;decomp:re/decomp/14465f450.c;xref:0x14466005b VTF F1WingDamage/ChassisDamageMultiple (2025: 0.3), plain (reader +0x18); gain into +0x50
    /* 0x34 */ float side_detach_scale;    // L2 decomp:re/decomp/144631a50.c;decomp:re/decomp/14465f580.c;decomp:re/decomp/14465f450.c;xref:0x144631ab0 VTF F1WingDamage/SideDetachScale (2025: 0.5), plain (reader +0x1c); one side counts as broken above this (visual state 3/7)
    /* 0x38 */ float full_detach_scale;    // L2 decomp:re/decomp/14465fdc0.c;decomp:re/decomp/144631a50.c;decomp:re/decomp/14465f580.c;decomp:re/decomp/14465f450.c;xref:0x1446600d6,0x144631ac6 VTF F1WingDamage/FullDetachScale (2025: 1), plain (reader +0x20); clamp of each side, (left+right)/this is the aero factor
    /* 0x40 */ float impact_divisor;        // L2 decomp:re/decomp/14465fdc0.c;xref:0x14465ff0a divisor of the threshold +0x28
    /* 0x48 */ float left_damage;           // L2 decomp:re/decomp/14465fdc0.c;decomp:re/decomp/144631a50.c;xref:0x1446600db,0x144631aaa accumulated left front wing damage
    /* 0x4c */ float right_damage;          // L2 decomp:re/decomp/14465fdc0.c;decomp:re/decomp/144631a50.c;xref:0x1446600e0,0x144631abc accumulated right front wing damage
    /* 0x50 */ float chassis_damage;        // L2 decomp:re/decomp/14465fdc0.c;xref:0x1446601aa accumulated chassis/nose damage (impact * +0x30)
    /* 0x90 */ void *f1_aero;               // L2 decomp:re/decomp/14465fdc0.c;decomp:re/decomp/14465f620.c;xref:0x144660249,0x14465f6ba F1Aero system (CarLive+0x570, copied by Car_FrontWing_Init); +0x199c gets the wing damage ratio
    /* 0x98 */ void *wing_body;             // L2 decomp:re/decomp/14465fdc0.c;decomp:re/decomp/14465f620.c;xref:0x14465f71b;kb:subsystems/car.md#parts physics body of the wing (collision filter, flag +0x170 bit 1 = attached): Car_FrontWing_Init copies +0x98 of the ConvexHull system's (CarLive+0x5b0) entry named "FrontWing", 0 when there is none
    /* 0xa0 */ void *nose_body;             // L2 decomp:re/decomp/14465fdc0.c;decomp:re/decomp/14465f620.c;xref:0x14465f731 second body accepted by the filter; Car_FrontWing_Init copies [CarLive+0x558 Chassis]+0x3f0
    /* 0xa8 */ void *parts_allocator;       // L2 decomp:re/decomp/1446612a0.c;xref:0x15538e560;kb:subsystems/car.md#parts allocator that grows the part vector (vtable +0x18 alloc, +0x38 free)
    /* 0xb0 */ void **parts_begin;          // L2 decomp:re/decomp/1446612a0.c;decomp:re/decomp/14465f620.c;xref:0x15538e527,0x14465f747;kb:subsystems/car.md#parts detachable part pointers added by Car_Add_DetachablePart_Thunk; Car_FrontWing_Init registers the 11 pieces only while begin == end
    /* 0xb8 */ void **parts_end;            // L2 decomp:re/decomp/1446612a0.c;decomp:re/decomp/14465f620.c;xref:0x15538e51d,0x14465f740;kb:subsystems/car.md#parts end; [end-8] is the part added last
    /* 0xc0 */ uint64_t parts_capacity;     // L2 decomp:re/decomp/1446612a0.c;xref:0x15538e532;kb:subsystems/car.md#parts capacity in pointers (grown x1.5)
};
