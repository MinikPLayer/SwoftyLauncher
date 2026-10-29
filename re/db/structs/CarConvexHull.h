// struct: CarConvexHull
// subsystem: car
// size: ?  L1 decomp:re/decomp/14464aaa0.c
// notes: the ConvexHull system (CarLive+0x5b0), collision hull. Offsets from the system pointer; data
// object at +0x18 (data vtable 0x145bf8620: +8 name 0x14464a7e0, +0x10 reader 0x14464aaa0, +0x18
// Car_Write_ConvexHull 0x14464a7f0). Keys: ConvexHullData/HullVertex{x,y,z} (vertex list), numVertices,
// generated, BoxifyFrontWing, BoxifyRearWing, Offset{X,Y,Z}. Field placement below is from the reader's
// access order only (916-line reader not fully read); the offsets are XOR pairs 8 bytes apart.
struct CarConvexHull {
    /* 0x0000 */ void **vtable;          // L1 decomp:re/decomp/14464aaa0.c system vtable 0x145bf8760 (phase-0 callback 0x14464dbe0)
    /* 0x0028 */ uint8_t boxify_front_wing; // L1 decomp:re/decomp/14464aaa0.c ConvexHull/BoxifyFrontWing (data+0x10)
    /* 0x0029 */ uint8_t boxify_rear_wing;  // L1 decomp:re/decomp/14464aaa0.c ConvexHull/BoxifyRearWing (data+0x11)
    /* 0x0030 */ CarObfFloat offset_x;   // L1 decomp:re/decomp/14464aaa0.c ConvexHull/OffsetX (data+0x18/+0x20)
    /* 0x0040 */ CarObfFloat offset_y;   // L1 decomp:re/decomp/14464aaa0.c ConvexHull/OffsetY (data+0x28/+0x30)
    /* 0x0050 */ CarObfFloat offset_z;   // L1 decomp:re/decomp/14464aaa0.c ConvexHull/OffsetZ (data+0x38/+0x40)
};
