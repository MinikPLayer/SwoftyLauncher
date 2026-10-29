// struct: CarChassis
// subsystem: car
// size: ?  L1 decomp:re/decomp/14474c060.c
// notes: live Chassis system at CarLive+0x558 (Car_Create_ChassisSystem 0x14474c060). Data object +0x18 read by
// Car_Read_Chassis 0x144676730 (thunk to .data1 0x1553bbbc0). Live mass/COM/MOI are re-derived by the OnReset slot
// (Car_Tuning_SetChassisZCom 0x144676d40, system vtable +0x18): editing the CarObfFloat raw fields alone does not
// change the simulated body until OnReset runs with flag 0x200 (or +0x150..+0x168 are written directly).
struct CarChassis {
    /* 0x0000 */ void *vtable;                                // L2 decomp:re/decomp/14474c060.c;kb:subsystems/car.md#layout system vtable 0x145bf9ca0
    /* 0x0008 */ CarLive *car;                                // L2 decomp:re/decomp/14474c060.c;kb:subsystems/car.md#layout owning car (factory argument); StepVehicle reads [sys+8]+0x568/+0x580
    /* 0x0010 */ void *allocator;                             // L2 decomp:re/decomp/14474c060.c;kb:subsystems/car.md#layout car allocator [car+0xd0]
    /* 0x0018 */ void *data_vtable;                           // L2 decomp:re/decomp/14474c060.c;kb:subsystems/car.md#layout embedded data object vtable 0x145bf9c10 {dtor, GetName, Car_Read_<X>, Car_Write_<X>}
    /* 0x0028 */ CarObfFloat mass;                            // L2 decomp:re/decomp/144676730.c;kb:subsystems/car.md#chassis VTF Chassis/Mass kg (2025 Ferrari 801); radUpgradeWeightReduction op - 18
    /* 0x0038 */ CarObfFloat moix;                            // L2 decomp:re/decomp/144676730.c;kb:subsystems/car.md#chassis VTF Chassis/MOIX kg m^2
    /* 0x0048 */ CarObfFloat moiy;                            // L2 decomp:re/decomp/144676730.c;kb:subsystems/car.md#chassis VTF Chassis/MOIY kg m^2
    /* 0x0058 */ CarObfFloat moiz;                            // L2 decomp:re/decomp/144676730.c;kb:subsystems/car.md#chassis VTF Chassis/MOIZ kg m^2
    /* 0x0068 */ CarObfFloat comy;                            // L2 decomp:re/decomp/144676730.c;kb:subsystems/car.md#chassis VTF Chassis/COMY m; radUpgradeWeightRedistribution op + 0.025
    /* 0x0078 */ CarObfFloat comz;                            // L2 decomp:re/decomp/144676730.c;kb:subsystems/car.md#chassis VTF Chassis/COMZ m base; the live COM z is +0x158
    /* 0x0088 */ CarObfFloat ffbscale;                        // L2 decomp:re/decomp/144676730.c;kb:subsystems/car.md#chassis VTF Chassis/FFBScale
    /* 0x0098 */ CarObfFloat sqrt_mass;                       // L2 decomp:re/decomp/144676730.c;kb:subsystems/car.md#chassis derived in the reader: sqrt(Mass), recomputed on every Read (base and upgrade blocks)
    /* 0x00a8 */ CarObfFloat front_visual_height_offset;      // L2 decomp:re/decomp/144676730.c;kb:subsystems/car.md#chassis VTF Chassis/FrontVisualHeightOffset m, visual only
    /* 0x00b8 */ CarObfFloat rear_visual_height_offset;       // L2 decomp:re/decomp/144676730.c;kb:subsystems/car.md#chassis VTF Chassis/RearVisualHeightOffset m, visual only
    /* 0x00c8 */ uint8_t car_code[0x28];                      // L2 decomp:re/decomp/144676730.c;kb:subsystems/car.md#chassis VTF Chassis/CarCode string object (0x1446506e0)
    /* 0x00f0 */ uint8_t car_handling_version_name[0x28];     // L2 decomp:re/decomp/144676730.c;kb:subsystems/car.md#chassis VTF Chassis/CarHandlingVersionName string object (0x1446506e0)
    /* 0x0118 */ uint8_t tyre_physics_group[0x28];            // L2 decomp:re/decomp/144676730.c;kb:subsystems/car.md#chassis VTF Chassis/TyrePhysicsGroup string object (0x1446506e0)
    /* 0x0150 */ float mass;                                  // L3 decomp:re/decomp/144676d40.c;kb:subsystems/car.md#chassis;xref:0x145bf9cb8;trace:T118/2026-10-28-gp:15 live mass in kg INCLUDING the fuel: VTF Mass (sqrt_mass^2) + FuelTank.fuel_mass (816.21 = 803 + 13.217 on the grid, 815.76 = 803 + 12.766 after about 40 s; T118). OnReset (vtable +0x18) flag 0x200 sets sqrt_mass^2; the writer that adds the fuel is not mapped yet
    /* 0x0154 */ float inv_mass;                              // L2 decomp:re/decomp/144676d40.c;kb:subsystems/car.md#chassis;xref:0x145bf9cb8 1/mass, same site
    /* 0x0158 */ float com_z;                                 // L2 decomp:re/decomp/144676d40.c;kb:subsystems/car.md#chassis;xref:0x145bf9cb8 live COM z: lerp(F1Tuning chassis COM-Z range, slider) on flag 0x2000; logged "Setting chassis z-com"
    /* 0x0160 */ float moi_x;                                 // L2 decomp:re/decomp/144676d40.c;kb:subsystems/car.md#chassis;xref:0x145bf9cb8 copy of MOIX (flag 0x200)
    /* 0x0164 */ float moi_y;                                 // L2 decomp:re/decomp/144676d40.c;kb:subsystems/car.md#chassis;xref:0x145bf9cb8 copy of MOIY
    /* 0x0168 */ float moi_z;                                 // L2 decomp:re/decomp/144676d40.c;kb:subsystems/car.md#chassis;xref:0x145bf9cb8 copy of MOIZ
};
