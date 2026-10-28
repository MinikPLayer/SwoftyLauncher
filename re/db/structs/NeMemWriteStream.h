// struct: NeMemWriteStream
// subsystem: core
// size: 0x28 L2 decomp:re/decomp/1429ce380.c (stack object at frame+0x28: vtable, buffer, capacity, two zeroed qwords)
// notes: output stream into a fixed buffer; vtable k_Ne_MemWriteStreamVtbl; a write that does not fit returns 0x37 and writes nothing
struct NeMemWriteStream {
    /* 0x000 */ void **vtable;                      // L3 decomp:re/decomp/1429ce380.c;xref:0x1429ce7c0;trace:T010/run2:86288;trace:T010/run3 k_Ne_MemWriteStreamVtbl
    /* 0x008 */ uint8_t *data;                      // L3 decomp:re/decomp/1429ce380.c;decomp:re/decomp/1445da3d0.c;xref:0x1445da3f6;trace:T010/run2:86288;trace:T010/run3 destination buffer
    /* 0x010 */ uint64_t capacity;                  // L3 decomp:re/decomp/1429ce380.c;decomp:re/decomp/1445da910.c;xref:0x1445da910;trace:T010/run2:86288;trace:T010/run3 vt+0x100 returns capacity - position
    /* 0x018 */ uint64_t position;                  // L3 decomp:re/decomp/1429ce380.c;decomp:re/decomp/1445da3d0.c;xref:0x1445da3fa,0x1445da40d;trace:T010/run2:86288;trace:T010/run3 write offset, advanced by each write; 0x1429ce380 copies this many bytes
    /* 0x020 */ uint64_t size;                      // L2 decomp:re/decomp/1429ce380.c;decomp:re/decomp/1445da3d0.c;xref:0x1445da413,0x1445da419 high-water mark of position
};
