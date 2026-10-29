// struct: CarFuelTank
// subsystem: car
// size: 0x110  L2 decomp:re/decomp/144749420.c (alloc 0x110)
// notes: the FuelTank system at CarLive+0x540 (Car_Create_FuelTankSystem 0x144749420). Data at +0x18 (reader 0x14468ea90 ->
// 0x1554035d0). Fuel burn runs in the phase-4 callback 0x14468edb0 when CarLive+0x130 bit 6 and +0x132 bit 2 are set;
// fuel_mass is plain, so writing it takes effect next step.
struct CarFuelTank {
    /* 0x0000 */ void *vtable;                                // L2 decomp:re/decomp/144749420.c;xref:0x144749434,0x144749467 system vtable 0x145bfa980 (CarComponentBase)
    /* 0x0008 */ CarLive *car;                                // L2 decomp:re/decomp/144749420.c;xref:0x144749458 owning car
    /* 0x0010 */ void *allocator;                             // L2 decomp:re/decomp/144749420.c;xref:0x14474945c car allocator
    /* 0x0018 */ void *data_vtable;                           // L2 decomp:re/decomp/144749420.c;xref:0x144749442,0x144749475 FuelTankData vtable 0x145bfa8c0 {dtor, GetName "FuelTank", Car_Read_FuelTank, Car_Write_FuelTank}; reader offsets = field - 0x18
    /* 0x0028 */ CarObfFloat fuel_bsfc_max;                   // L2 decomp:re/decomp/1554035d0.c;xref:0x1554035dc,0x155403601 VTF FuelTank/FuelBsfcMax kg/J x1000 BSFC far from optimum, Ferrari 4.83e-5; radUpgradeEngine x0.895 (obf)
    /* 0x0038 */ CarObfFloat fuel_bsfc_min;                   // L2 decomp:re/decomp/1554035d0.c;xref:0x155403604,0x155403629 VTF FuelTank/FuelBsfcMin BSFC at optimum, Ferrari 4.75e-5; radUpgradeEngine x0.9 (obf)
    /* 0x0048 */ CarObfFloat fuel_lhv;                        // L2 decomp:re/decomp/1554035d0.c;xref:0x15540362c,0x155403651 VTF FuelTank/FuelLHV Ferrari 42500 (obf)
    /* 0x0058 */ CarObfFloat fuel_optimal_revs;               // L2 decomp:re/decomp/1554035d0.c;xref:0x155403654,0x155403679 VTF FuelTank/FuelOptimalRevs rad/s, Ferrari 1250 (obf)
    /* 0x0068 */ CarObfFloat fuel_optimal_torque;             // L2 decomp:re/decomp/1554035d0.c;xref:0x15540367c,0x1554036a8 VTF FuelTank/FuelOptimalTorque Nm, Ferrari 500 (radUpgradeEngine sets 550) (obf)
    /* 0x0078 */ CarObfFloat fuel_revs_linear;                // L2 decomp:re/decomp/1554035d0.c;xref:0x1554036ab,0x1554036db VTF FuelTank/FuelRevsLinear Ferrari 0.019 (obf)
    /* 0x0088 */ CarObfFloat fuel_torque_linear;              // L2 decomp:re/decomp/1554035d0.c;xref:0x1554036de,0x15540370e VTF FuelTank/FuelTorqueLinear Ferrari 0.056; radUpgradeEngine x1.4 (obf)
    /* 0x0098 */ CarObfFloat fuel_capacity;                   // L3 decomp:re/decomp/1554035d0.c;xref:0x155403711,0x155403749;trace:T124/2026-10-28-gp:8 VTF FuelTank/FuelCapacity kg, F1 110, F2 94; clamped >= 0.1 after read (obf) Live: 110 kg decoded.
    /* 0x00a8 */ CarObfFloat comy;                            // L2 decomp:re/decomp/1554035d0.c;xref:0x15540374f,0x15540377d VTF FuelTank/COMY (obf)
    /* 0x00b8 */ CarObfFloat comz;                            // L2 decomp:re/decomp/1554035d0.c;xref:0x155403783,0x1554037b1 VTF FuelTank/COMZ (obf)
    /* 0x00c8 */ CarObfFloat size_x;                          // L2 decomp:re/decomp/1554035d0.c;xref:0x1554037b7,0x1554037e5 VTF FuelTank/SizeX (obf)
    /* 0x00d8 */ CarObfFloat size_y;                          // L2 decomp:re/decomp/1554035d0.c;xref:0x1554037eb,0x155403819 VTF FuelTank/SizeY (obf)
    /* 0x00e8 */ CarObfFloat size_z;                          // L2 decomp:re/decomp/1554035d0.c;xref:0x15540381f,0x15540384d VTF FuelTank/SizeZ (obf)
    /* 0x00f8 */ uint8_t fixed_load;                          // L2 decomp:re/decomp/1554035d0.c;xref:0x155403853 VTF FuelTank/FixedLoad (bool reader 0x14464c220)
    /* 0x0100 */ float fuel_mass;                             // L3 decomp:re/decomp/14468edb0.c;xref:0x1446437f0,0x14468f04d,0x14468f05d;trace:T124/2026-10-28-gp:8 kg (channel "mass"); burned by the phase-4 step; Reset 0x40 = capacity x fill_fraction Live: 13.217 -> 12.766 kg for car 0 after about 40 s of racing.
    /* 0x0104 */ float fill_fraction;                         // L2 decomp:re/decomp/155404150.c;xref:0x155404165 start fill 0..1 used by Reset 0x40
    /* 0x0108 */ float fuel_flow;                             // L3 decomp:re/decomp/14468edb0.c;xref:0x14468f03c;trace:T124/2026-10-28-gp:21 kg/s = rad/s x Nm x BSFC(d) x active_burn_scale x 0.001 Live: non-zero on throttle (car 2: 0.0172 kg/s in the race snapshot), 0 idling.
    /* 0x010c */ uint8_t tank_flags;                          // L2 decomp:re/decomp/14468edb0.c;xref:0x14468f065,0x14468f076 bit1 = ran dry this step; Reset clears bit0
};
