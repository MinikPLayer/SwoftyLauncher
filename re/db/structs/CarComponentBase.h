// struct: CarComponentBase
// subsystem: car
// size: ?  L2 decomp:re/decomp/1447484f0.c (Engine system: alloc 0x25b8 from the car allocator)
// notes: common head of every CarLive system (the 23 VTF section systems in CarLive+0x3a0, created by
// Car_Create_<X>System from Car_Systems_FindOrCreate). The system vtable (vt_Car_EngineSystem layout) is
// {+0 dtor, +8 Read(node), +0x10 Write(node), +0x18 OnReset(flags) (-1 at create = everything; Engine Car_Tuning_SetEngineBrakingAndRadiator, Chassis
// Car_Tuning_SetChassisZCom, GearBox Car_Tuning_SetFinalDrive: setup values and derived fields are applied here), +0x20 ?, +0x28 GetPhaseCallback(out,
// phase), +0x30 Init(0), +0x38 GetData, +0x40 GetData}. Read/Write forward to the embedded data object at
// +0x18 (ConvexHull: +0x20, V1337; fetch it through the system vt+0x38, not a fixed offset), whose own 4-slot vtable is {+0 ?, +8 GetName -> VTF section name, +0x10 Car_Read_<X>(node),
// +0x18 Car_Write_<X>(node)}. The per-component data fields start inside that data object (system+0x18);
// lane structs (CarEngine.h etc.) give offsets from the system pointer stored in CarLive.
struct CarComponentBase {
    /* 0x0000 */ void **vtable;      // L2 decomp:re/decomp/1447484f0.c;xref:0x144748530 system vtable (Engine: 0x145bfa630)
    /* 0x0008 */ CarLive *car;       // L2 decomp:re/decomp/1447484f0.c;xref:0x144748528 owning car (factory argument)
    /* 0x0010 */ void *allocator;    // L2 decomp:re/decomp/1447484f0.c;xref:0x144748521,0x14474852c car allocator [car+0xd0]
    /* 0x0018 */ void **data_vtable; // L2 decomp:re/decomp/1447484f0.c;xref:0x1446878c0 embedded component data object (system vt+0x38/+0x40 return this+0x18; ConvexHull keeps it at +0x20 and its vt+0x38 returns [sys+0xa0] = sys+0x20, so fetch it through vt+0x38); its vtable +8 = name, +0x10 = Car_Read_<X>, +0x18 = Car_Write_<X>
};
