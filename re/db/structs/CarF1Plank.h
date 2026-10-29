// struct: CarF1Plank
// subsystem: car
// size: ?  L2 decomp:re/decomp/144652aa0.c
// notes: the F1Plank system (CarLive+0x5c0). Offsets are from the SYSTEM pointer; the section data object
// starts at system+0x18 (data vtable 0x145bf8a50: +8 name, +0x10 reader 0x144652aa0, +0x18 writer
// 0x144652930), so reader offset = field offset - 0x18. Plain floats (Game_Read_VtfFloat), no XOR. The
// ExtentX store offset is itself computed at runtime from a .data1 constant (0x151687bf4 ^ 0x243caf2f).
// VTF keys F1Plank/Offset{X,Y,Z}, F1Plank/Extent{X,Y,Z} (metres, plank box). Upgrade blocks apply op
// arithmetic through the same reader (Game_Read_VtfFloat on an Upgrade node).
struct CarF1Plank {
    /* 0x0000 */ void **vtable;     // L2 decomp:re/decomp/144652aa0.c;xref:0x1555f146c system vtable 0x145bf8aa0 (phase-0 callback 0x144652e80)
    /* 0x0018 */ void **data_vtable; // L2 decomp:re/decomp/144652aa0.c;xref:0x1555f147a data vtable 0x145bf8a50
    /* 0x002c */ float offset_x;    // L2 decomp:re/decomp/144652aa0.c;xref:0x1553454ca F1Plank/OffsetX
    /* 0x0030 */ float offset_y;    // L2 decomp:re/decomp/144652aa0.c;xref:0x1553454dd F1Plank/OffsetY
    /* 0x0034 */ float offset_z;    // L2 decomp:re/decomp/144652aa0.c;xref:0x1553454f0 F1Plank/OffsetZ (the 2025 teams differ here)
    /* 0x0038 */ float extent_x;    // L2 decomp:re/decomp/144652aa0.c;xref:0x15534550f F1Plank/ExtentX (store offset decoded from .data1 at runtime)
    /* 0x003c */ float extent_y;    // L2 decomp:re/decomp/144652aa0.c;xref:0x155345522 F1Plank/ExtentY
    /* 0x0040 */ float extent_z;    // L2 decomp:re/decomp/144652aa0.c;xref:0x155345527,0x15534553e F1Plank/ExtentZ (tail jmp into Game_Read_VtfFloat)
};
