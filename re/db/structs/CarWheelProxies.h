// struct: CarWheelProxies
// subsystem: car
// size: ?  L2 decomp:re/decomp/14464e710.c
// notes: the WheelProxies system (CarLive+0x5b8). Offsets from the system pointer; data object at +0x18
// (data vtable 0x145bf88e0: +8 name, +0x10 reader 0x14464e710, +0x18 writer 0x14464e630). Plain floats.
// Phase callbacks: phase 0 0x14464f310, phase 5 0x14464f720.
struct CarWheelProxies {
    /* 0x0000 */ void **vtable;            // L2 decomp:re/decomp/14464e710.c;xref:0x1555df5a4 system vtable 0x145bf8950
    /* 0x0028 */ uint32_t create_proxies;  // L2 decomp:re/decomp/14464e710.c;xref:0x15532f77c,0x15532f78a WheelProxies/CreateProxies (bool reader 0x14464c220)
    /* 0x0030 */ float extra_width_front;  // L2 decomp:re/decomp/14464e710.c;xref:0x15532f78f,0x15532f79d WheelProxies/ExtraWidthFront
    /* 0x0034 */ float extra_width_rear;   // L2 decomp:re/decomp/14464e710.c;xref:0x15532f7a2,0x15532f7b9 WheelProxies/ExtraWidthRear (tail jmp into Game_Read_VtfFloat)
};
