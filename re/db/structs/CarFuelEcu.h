// struct: CarFuelEcu
// subsystem: car
// size: 0x170  L2 decomp:re/decomp/1555cc570.c (alloc 0x170)
// notes: the FuelECU system at CarLive+0x548 (Car_Create_FuelECUSystem 0x14474bf70 -> .data1 0x1555cc570). Data at +0x18
// (reader 0x144689fb0 -> 0x1553fb310). Reset (vtable +0x18 = 0x14468e550 -> 0x1553fdf60) copies mode fuel_mode into the
// active_* pairs and then re-runs the Engine derived init 0x144689a70 and the GearBox table 0x1446a5130.
struct CarFuelEcu {
    /* 0x0000 */ void *vtable;                                // L2 decomp:re/decomp/1555cc570.c;decomp:re/decomp/14468a2b0.c;kb:structs/CarComponentBase.h system vtable 0x145bfa870 (CarComponentBase)
    /* 0x0008 */ CarLive *car;                                // L2 decomp:re/decomp/1555cc570.c;decomp:re/decomp/14468a2b0.c;kb:structs/CarComponentBase.h owning car
    /* 0x0010 */ void *allocator;                             // L2 decomp:re/decomp/1555cc570.c;decomp:re/decomp/14468a2b0.c;kb:structs/CarComponentBase.h car allocator
    /* 0x0018 */ void *data_vtable;                           // L2 decomp:re/decomp/1555cc570.c;decomp:re/decomp/14468a2b0.c;kb:structs/CarComponentBase.h FuelECUData vtable 0x145bfa6f0 {dtor, GetName "FuelECU", Car_Read_FuelECU, Car_Write_FuelECU}; reader offsets = field - 0x18
    /* 0x0028 */ CarObfFloat fuel_burn_scale_lean;            // L2 decomp:re/decomp/1553fb310.c;xref:0x1553fb31c,0x1553fb341 VTF FuelECU/FuelBurnScaleLean mode table [Lean, Standard, Rich, Max] stride 0x10; Ferrari 2025 0.7; radUpgradeFuelConsumption x0.85 (obf)
    /* 0x0038 */ CarObfFloat fuel_burn_scale_standard;        // L2 decomp:re/decomp/1553fb310.c;xref:0x1553fb344,0x1553fb369 VTF FuelECU/FuelBurnScaleStandard (obf)
    /* 0x0048 */ CarObfFloat fuel_burn_scale_rich;            // L2 decomp:re/decomp/1553fb310.c;xref:0x1553fb36c,0x1553fb391 VTF FuelECU/FuelBurnScaleRich (obf)
    /* 0x0058 */ CarObfFloat fuel_burn_scale_max;             // L2 decomp:re/decomp/1553fb310.c;xref:0x1553fb394,0x1553fb3b9 VTF FuelECU/FuelBurnScaleMax (obf)
    /* 0x0068 */ CarObfFloat fuel_power_scale_lean;           // L2 decomp:re/decomp/1553fb310.c;xref:0x1553fb3bc,0x1553fb3e1 VTF FuelECU/FuelPowerScaleLean Ferrari 0.9 (others 1.0) (obf)
    /* 0x0078 */ CarObfFloat fuel_power_scale_standard;       // L2 decomp:re/decomp/1553fb310.c;xref:0x1553fb3e4,0x1553fb409 VTF FuelECU/FuelPowerScaleStandard (obf)
    /* 0x0088 */ CarObfFloat fuel_power_scale_rich;           // L2 decomp:re/decomp/1553fb310.c;xref:0x1553fb40c,0x1553fb438 VTF FuelECU/FuelPowerScaleRich (obf)
    /* 0x0098 */ CarObfFloat fuel_power_scale_max;            // L2 decomp:re/decomp/1553fb310.c;xref:0x1553fb43b,0x1553fb469 VTF FuelECU/FuelPowerScaleMax (obf)
    /* 0x00a8 */ CarObfFloat fuel_revs_scale_lean;            // L2 decomp:re/decomp/1553fb310.c;xref:0x1553fb46f,0x1553fb49d VTF FuelECU/FuelRevsScaleLean 1.0 (obf)
    /* 0x00b8 */ CarObfFloat fuel_revs_scale_standard;        // L2 decomp:re/decomp/1553fb310.c;xref:0x1553fb4a3,0x1553fb4d1 VTF FuelECU/FuelRevsScaleStandard (obf)
    /* 0x00c8 */ CarObfFloat fuel_revs_scale_rich;            // L2 decomp:re/decomp/1553fb310.c;xref:0x1553fb4d7,0x1553fb505 VTF FuelECU/FuelRevsScaleRich (obf)
    /* 0x00d8 */ CarObfFloat fuel_revs_scale_max;             // L2 decomp:re/decomp/1553fb310.c;xref:0x1553fb50b,0x1553fb539 VTF FuelECU/FuelRevsScaleMax (obf)
    /* 0x00e8 */ CarObfFloat temperature_scale_lean;          // L2 decomp:re/decomp/1553fb310.c;xref:0x1553fb53f,0x1553fb56d VTF FuelECU/TemperatureScaleLean 1.0 (obf)
    /* 0x00f8 */ CarObfFloat temperature_scale_standard;      // L2 decomp:re/decomp/1553fb310.c;xref:0x1553fb573,0x1553fb5a1 VTF FuelECU/TemperatureScaleStandard (obf)
    /* 0x0108 */ CarObfFloat temperature_scale_rich;          // L2 decomp:re/decomp/1553fb310.c;xref:0x1553fb5a7,0x1553fb5e0 VTF FuelECU/TemperatureScaleRich (obf)
    /* 0x0118 */ CarObfFloat temperature_scale_max;           // L2 decomp:re/decomp/1553fb310.c;xref:0x1553fb5e6,0x1553fb614 VTF FuelECU/TemperatureScaleMax (obf)
    /* 0x0128 */ int32_t fuel_mode;                           // L3 decomp:re/decomp/1553fdf60.c;xref:0x1553fdf6c,0x1553fe007;trace:T124/2026-10-28-gp:9;trace:T124/2026-10-28-gp:39 0 Lean, 1 Standard, 2 Rich, 3 Max; Reset applies it (flag 0x2000 forces Standard) Live: 1 (Standard) for every race car; the safety car read 2 on the grid and 1 in the race.
    /* 0x0130 */ CarObfFloat active_burn_scale;               // L2 decomp:re/decomp/1553fdf60.c;decomp:re/decomp/14468edb0.c;xref:0x1553fdf9d,0x1553fe038 copied from the mode table by Reset; fuel flow multiplier in the FuelTank step 0x14468edb0
    /* 0x0140 */ CarObfFloat active_power_scale;              // L2 decomp:re/decomp/1553fdf60.c;decomp:re/decomp/1446e2290.c;xref:0x1553fdfb5,0x1553fe04d engine power multiplier (Car_Powertrain_Step, engine Init)
    /* 0x0150 */ CarObfFloat active_revs_scale;               // L2 decomp:re/decomp/1553fdf60.c;decomp:re/decomp/1446e2290.c;xref:0x1553fdfd3,0x1553fe065 divides the RevsPowerSpline lookup and scales the limiter
    /* 0x0160 */ CarObfFloat active_temperature_scale;        // L2 decomp:re/decomp/1553fdf60.c;xref:0x1553fdff3,0x1553fe07d temperature scale of the active mode
};
