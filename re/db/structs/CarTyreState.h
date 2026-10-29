// struct: CarTyreState
// subsystem: car
// size: ?  L1 decomp:re/decomp/1446dee60.c
// notes: per-wheel tyre object: the physics vehicle (Car_Suspension_SolveWheels param_1) holds four at +0x60/+0x68 (rear)
// /+0x70/+0x78 (front); Car_Tyre_GripScale gets this+8. Offsets here are from the object itself.
struct CarTyreState {
    /* 0x0008 */ CarSuspTyreType *tyre_type;                  // L2 decomp:re/decomp/1446b7240.c;xref:0x1446df89a GripScale *param_1
    /* 0x0010 */ CarLive *car;                                // L2 decomp:re/decomp/1446b7240.c;xref:0x1446df89a GripScale param_1[1]; car+0x124 == 1 selects the AI downslope factors
    /* 0x007c */ float work_energy;                           // L1 decomp:re/decomp/1446b79f0.c;xref:0x1446b5a20 wear numerator term (/1e6); temperature step uses it /(dt*75)
    /* 0x0080 */ float wear_scale;                            // L2 decomp:re/decomp/1446b79f0.c;xref:0x1446b5a20 multiplies every wear increment (with TyreType.upgrade_wear_factor); Car_Tyre_SetWearFactor candidate writer
    /* 0x0084 */ float ai_wear_multiplier;                    // L2 decomp:re/decomp/1446b79f0.c;xref:0x1446b5a20 wear increment x this only when car+0x124 == 1 (AI); player uses 1.0
    /* 0x0088 */ float wear;                                  // L3 decomp:re/decomp/1446b79f0.c;xref:0x1446b5a20;trace:T117/2026-10-28-gp:14 tyre wear 0..1, accumulated and clamped by Car_Tyre_UpdateWear Live: rises from 0 during the race (0.0011..0.0029 per wheel after about 40 s); not yet compared with UDP (T117).
    /* 0x008c */ float wetness;                               // L1 decomp:re/decomp/1446b7240.c GripScale x/0.2 clamp: water depth or wetness (needs trace)
    /* 0x009c */ float pressure;                              // L1 decomp:re/decomp/1446b79f0.c;xref:0x1446b5a20 lerps WearRateMin/Max (compound +0x120/+0x124) by (p - PressureMin)/(PressureMax - PressureMin) (compound +0xfc/+0x100)
    /* 0x00c0 */ CarTyreCompound *compound;                   // L2 decomp:re/decomp/1446dee60.c;xref:0x1446df89a,0x1446e10e0 active compound runtime; +0x1438 = Magic Formula curve
    /* 0x00d0 */ uint32_t wheel_index;                        // L2 decomp:re/decomp/1446b7240.c;xref:0x1446df89a index into CarSuspension.wheels
    /* 0x0140 */ void *axle;                                  // L1 decomp:re/decomp/1446dceb0.c StepVehicle reads +0x15e4 of it (RideHeightOffset if axle = 8-byte head + CarSuspCorner)
};
