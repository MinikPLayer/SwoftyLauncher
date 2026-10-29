// struct: CarPhysicsRecord
// subsystem: car
// size: ? L2 decomp:re/decomp/142e90560.c;decomp:re/decomp/144628190.c
// notes: the chassis-runtime entity component (vt 0x145bf73a0) at per-vehicle item +0x20 (item = *(mgr+0x110..)).
// Car_Chassis_Build (its vtable slot 7) creates the CarLive and stores it at +0x58. The career vehicle-upgrade
// component reaches it as [comp+0x18]. Distinct from CarPerfVehicle (the career Upgrades object).
struct CarPhysicsRecord {
    /* 0x0058 */ void *car;  // L2 decomp:re/decomp/142e90560.c;decomp:re/decomp/144628190.c;kb:subsystems/car.md#live -> CarLive (was read as CarWheelState)
};
