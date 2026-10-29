// struct: CarTyreMfCurve
// subsystem: car
// size: ?  L1 decomp:re/decomp/1446cf080.c
// notes: Magic Formula curve object read by Car_Tyre_MagicFormula 0x1446cf080 (VCMPD TyreModel ParametricData).
struct CarTyreMfCurve {
    /* 0x0010 */ void *load_rows_begin;                       // L2 decomp:re/decomp/1446cf080.c;xref:0x1446e10e0,0x1446e10fd,0x1446e111b,0x1446e1136 32-byte rows {Load, Peak, PeakMin, PeakMax, DownSlope(C), UpSlope(B, per degree), Shape(E), derived peak slip +0x1c} sorted by Load, lerped by wheel load
    /* 0x0018 */ void *load_rows_end;                         // L2 decomp:re/decomp/1446cf080.c;xref:0x1446e10e0,0x1446e10fd,0x1446e111b,0x1446e1136
    /* 0x0030 */ void *camber_rows_begin;                     // L2 decomp:re/decomp/1446cf080.c;xref:0x1446e10e0,0x1446e10fd,0x1446e111b,0x1446e1136 8-byte rows {Camber, GripGain}; only evaluated at camber 0 (D = -Peak*(1+GripGain(0)))
    /* 0x0038 */ void *camber_rows_end;                       // L2 decomp:re/decomp/1446cf080.c;xref:0x1446e10e0,0x1446e10fd,0x1446e111b,0x1446e1136
    /* 0x0088 */ float load_scale_a;                          // L1 decomp:re/decomp/1446dee60.c solver divides by it before the MF call (with car+0x88)
    /* 0x008c */ float load_scale_b;                          // L1 decomp:re/decomp/1446dee60.c
    /* 0x0210 */ float peak_slip[11];                         // L2 decomp:re/decomp/144743900.c;xref:0x144745949,0x144745989 derived at parse: per load row the slip angle of peak force (step search with 9 MF calls), also stored in each row at +0x1c; count = load rows (11 in 2025 VCMPDs)
};
