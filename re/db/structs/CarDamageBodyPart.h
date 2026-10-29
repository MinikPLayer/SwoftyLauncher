// struct: CarDamageBodyPart
// subsystem: car
// size: ? L1 decomp:re/decomp/144650010.c;decomp:re/decomp/14464fe90.c
// notes: one F1BodyDamage Part record (data object of the F1BodyDamage system, CarLive+0x5e0; data-class
// vtable 0x145bf89a0: slot 1 0x14464fe80, slot 2 Car_Read_F1BodyDamage 0x144650010, slot 3
// Car_Write_F1BodyDamage 0x14464fe90). 2025 parts: nosecone, sidepod_fl/fr, sidewing_fl/fr, topcanopy_bm,
// rearwing_bl/br/top, diffuser_bm, camera_wing, wing_mirror_l/r, bargeboard_fl/fr, floor_damage_l/r. The
// live damage state per part (u16 current/max) is Car_DamageMeshes_Init's mesh list at system+0x58.
struct CarDamageBodyPart {
    /* 0x10 */ void *name;          // L2 decomp:re/decomp/144650010.c;decomp:re/decomp/14464fe90.c;kb:subsystems/car.md#damage VTF F1BodyDamage/Part/Name (string object, written with attribute writer vt+0x70)
    /* 0x2c */ float aero_loss;     // L2 decomp:re/decomp/144650010.c;decomp:re/decomp/14464fe90.c;kb:subsystems/car.md#damage VTF Part/AeroLoss: downforce fraction lost at full damage (2025: diffuser 0.1, sidepods/wings/floor 0.05)
    /* 0x30 */ float drag_gain;     // L2 decomp:re/decomp/144650010.c;decomp:re/decomp/14464fe90.c;kb:subsystems/car.md#damage VTF Part/DragGain: drag fraction gained (2025: sidepods/bargeboards 0.01, mirrors -0.001)
    /* 0x34 */ float coolant_loss;  // L2 decomp:re/decomp/144650010.c;decomp:re/decomp/14464fe90.c;kb:subsystems/car.md#damage VTF Part/CoolantLoss (2025: sidepods 0.25, others 0)
};
