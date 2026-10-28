// struct: NeBagCodec
// subsystem: core
// size: 0x10 L2 decomp:re/decomp/1429ce380.c (built on the stack at 0x1429ce715 with the stream pointer beside it)
// notes: bag stream codec; vtable k_Ne_BagCodecVtbl (+0x8 Game_SaveData_WriteBag, +0x10 the reader 0x143934620, +0x18 header value 1)
struct NeBagCodec {
    /* 0x000 */ void **vtable;                      // L3 decomp:re/decomp/1429ce380.c;xref:0x1429ce715;trace:T010/run2:86288;trace:T010/run3 k_Ne_BagCodecVtbl
    /* 0x008 */ void *stream;                       // L3 decomp:re/decomp/1439340c0.c;xref:0x1439340e4,0x14393411a;trace:T010/run2:86288;trace:T010/run3 every write of Game_SaveData_WriteBag goes to [codec+8]->vt+0x48
};
