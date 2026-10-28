// struct: NeCountingStream
// subsystem: core
// size: 0x30 L2 decomp:re/decomp/1429ce380.c (stack object at frame+0x138, +0x8..+0x2f zeroed before use at 0x1429ce6e4)
// notes: output stream that only counts bytes; vtable k_Ne_CountingStreamVtbl
struct NeCountingStream {
    /* 0x000 */ void **vtable;                      // L3 decomp:re/decomp/1429ce380.c;xref:0x1429ce6fa;trace:T010/run2:86288;trace:T010/run3 k_Ne_CountingStreamVtbl
    /* 0x028 */ uint64_t count;                     // L3 decomp:re/decomp/1429ce380.c;decomp:re/decomp/143658530.c;xref:0x143658530,0x1429ce750;trace:T010/run2:86288;trace:T010/run3 vt+0x48 adds each length; 0x1429ce380 reads it as the blob size
};
