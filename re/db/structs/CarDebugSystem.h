// struct: CarDebugSystem
// subsystem: car
// size: ?  L2 decomp:re/decomp/14467d5d0.c
// notes: the DebugSystem system (CarLive+0x588). Offsets from the system pointer; data object at +0x18
// (data vtable 0x145bf9f90: +8 name, +0x10 reader 0x14467d5d0, +0x18 writer 0x14467d490). Both tyre
// override strings are lower-cased after reading. No phase callback (slot +0x28 is the default).
struct CarDebugSystem {
    /* 0x0000 */ void **vtable;              // L2 decomp:re/decomp/14467d5d0.c;xref:0x1556106a8 system vtable 0x145bf9ff0
    /* 0x0028 */ uint32_t raise_car;         // L2 decomp:re/decomp/14467d5d0.c;xref:0x1553cdced,0x1553cdcfb DebugSystem/RaiseCar (read by 0x14464c220, bool/uint reader)
    /* 0x0030 */ uint8_t override_tyres_front[0x28]; // L2 decomp:re/decomp/14467d5d0.c;xref:0x1553cdd00,0x1553cdd0e DebugSystem/OverrideTyresFront string object (char* at +0x10 = data+0x28, lower-cased after the read)
    /* 0x0058 */ uint8_t override_tyres_rear[0x28];  // L2 decomp:re/decomp/14467d5d0.c;xref:0x1553cdd1f,0x1553cdd2d DebugSystem/OverrideTyresRear string object (store offset decoded at runtime from .data1 0x15218ddac + 0xb70c5131; its char* is read at data+0x50)
};
