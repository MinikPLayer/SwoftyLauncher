// struct: CarTurbo
// subsystem: car
// size: 0x308  L2 decomp:re/decomp/1555ded40.c (alloc 0x308)
// notes: the Turbo system at CarLive+0x5a0 (Car_Create_TurboSystem 0x1447539f0 -> .data1 0x1555ded40). Data at +0x18 (reader
// 0x1446b6580 -> 0x15543fad0). Turbo numbers are stored plain (no XOR).
struct CarTurbo {
    /* 0x0000 */ void *vtable;                                // L2 decomp:re/decomp/1555ded40.c;xref:0x1555ded50,0x1555ded7f system vtable 0x145bfbe80 (CarComponentBase)
    /* 0x0008 */ CarLive *car;                                // L2 decomp:re/decomp/1555ded40.c;xref:0x1555ded70 owning car
    /* 0x0010 */ void *allocator;                             // L2 decomp:re/decomp/1555ded40.c;xref:0x1555ded74 car allocator
    /* 0x0018 */ void *data_vtable;                           // L2 decomp:re/decomp/1555ded40.c;xref:0x1555ded5e,0x1555ded8d TurboData vtable 0x145bfbdc0 {dtor, GetName "Turbo", Car_Read_Turbo, Car_Write_Turbo}; reader offsets = field - 0x18
    /* 0x0028 */ CarSpline boost_efficiency_spline;           // L2 decomp:re/decomp/15543fad0.c;xref:0x15543fadc VTF Turbo/BoostEfficiencySpline
    /* 0x02d0 */ float zero_bar_rate;                         // L2 decomp:re/decomp/15543fad0.c;xref:0x15543faef VTF Turbo/ZeroBarRate plain, Ferrari 500
    /* 0x02d4 */ float max_boost_rate;                        // L2 decomp:re/decomp/15543fad0.c;xref:0x15543fb05 VTF Turbo/MaxBoostRate plain, Ferrari 700
    /* 0x02d8 */ float max_boost;                             // L2 decomp:re/decomp/15543fad0.c;xref:0x15543fb1b VTF Turbo/MaxBoost bar, plain, Ferrari 5.5
    /* 0x02dc */ float relative_size;                         // L2 decomp:re/decomp/15543fad0.c;xref:0x15543fb31 VTF Turbo/RelativeSize plain, Ferrari 0.1
    /* 0x02e0 */ float acceleration;                          // L2 decomp:re/decomp/1446b6630.c;xref:0x1446b6757 VTF Turbo/Acceleration plain, Ferrari 500 (reader offset hidden by .data1 packing; writer 0x1446b6630 reads data+0x2c8)
    /* 0x02e4 */ float deceleration;                          // L2 decomp:re/decomp/1446b6630.c;xref:0x1446b6788 VTF Turbo/Deceleration plain, Ferrari 50 (writer reads data+0x2cc)
    /* 0x02e8 */ float inlet_pressure;                        // L2 decomp:re/decomp/155441c10.c;xref:0x1446b6cf0,0x155441c1a channel; Reset 0x210 = 1.0
    /* 0x02ec */ float inlet_waste;                           // L2 decomp:re/decomp/155441c10.c;xref:0x1446b6cd0,0x155441c1a channel; Reset = 0
    /* 0x02f0 */ float outlet_pressure;                       // L2 decomp:re/decomp/155441c10.c;xref:0x1446b6d00,0x155441c25 channel; boost_bar = outlet_pressure + const
    /* 0x02f4 */ float outlet_waste;                          // L2 decomp:re/decomp/155441c10.c;xref:0x1446b6ce0,0x155441c40 channel
    /* 0x02f8 */ float rotation_rate;                         // L2 decomp:re/decomp/155441c10.c;xref:0x1446b6c90,0x155441c40 turbo speed (channel)
    /* 0x02fc */ float engine_pressure;                       // L2 decomp:re/decomp/155441c10.c;xref:0x1446b6cc0,0x155441c39 channel; Reset = 1.0
    /* 0x0304 */ float efficiency_factor;                     // L2 decomp:re/decomp/155441c10.c;decomp:re/decomp/1446e2290.c;xref:0x144689180,0x155441c4b channel; multiplies drive torque in Car_Powertrain_Step when Engine TurboMode == Full
};
