// struct: CarTyreCompound
// subsystem: car
// size: ?  L1 decomp:re/decomp/144743900.c
// notes: runtime VCMPD compound (Car_Parse_TyreModel 0x144743900 fills it from tyrecompounds/*.vcmpd, plain floats).
// Only the Magic Formula link is mapped; wear/temperature/pressure blocks are listed in subsystems/car.md#tyres.
struct CarTyreCompound {
    /* 0x00fc */ float pressure_min;                          // L1 decomp:re/decomp/1446b79f0.c;xref:0x1446b5a20 VCMPD Pressure/PressureMin (bar; 15)
    /* 0x0100 */ float pressure_max;                          // L1 decomp:re/decomp/1446b79f0.c;xref:0x1446b5a20 VCMPD Pressure/PressureMax (30)
    /* 0x0120 */ float wear_rate_min;                         // L1 decomp:re/decomp/1446b79f0.c;xref:0x1446b5a20 VCMPD Pressure/WearRateMin (lerped by pressure)
    /* 0x0124 */ float wear_rate_max;                         // L1 decomp:re/decomp/1446b79f0.c;xref:0x1446b5a20 VCMPD Pressure/WearRateMax
    /* 0x0690 */ float pressure_term_min;                     // L1 decomp:re/decomp/1446b7b70.c temperature step lerps +0x690..+0x694 by the pressure ratio (WorkEnergyRateMin/Max candidate)
    /* 0x0694 */ float pressure_term_max;                     // L1 decomp:re/decomp/1446b7b70.c
    /* 0x0c18 */ CarSpline wear_spline_c18;                   // L1 decomp:re/decomp/1446b79f0.c;xref:0x1446b5a20 evaluated at wetness in the wear step (WetnessWear candidate)
    /* 0x1168 */ CarSpline temp_spline_1168;                  // L1 decomp:re/decomp/1446b7b70.c evaluated at wetness in the temperature step
    /* 0x1438 */ CarTyreMfCurve *lateral_curve;               // L2 decomp:re/decomp/1446dee60.c;xref:0x1446df89a,0x1446e10e0 curve passed to Car_Tyre_MagicFormula by the wheel solver (4 calls 0x1446e10e0..)
};
