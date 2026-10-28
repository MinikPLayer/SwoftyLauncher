// struct: NeStreamVtbl
// subsystem: core
// size: ?
// notes: output stream interface the save writers use (k_Ne_CountingStreamVtbl, k_Ne_MemWriteStreamVtbl and the profile save's stream)
struct NeStreamVtbl {
    /* 0x048 */ void *write;                        // L3 decomp:re/decomp/1439477a0.c;decomp:re/decomp/143651030.c;xref:0x1439477fb,0x143651bd5,0x143651c47;trace:T010/run2:86526;trace:T010/run3 int (*)(stream, const void *data, uint64_t size): 0 or an error code
    /* 0x050 */ void *write_length;                 // L3 decomp:re/decomp/1439477a0.c;xref:0x1439477d3,0x1445da490;trace:T010/run2:86526;trace:T010/run3 int (*)(stream, uint8_t len) (the low byte of edx): the string type's writer sends a key's length with it before writing len & 0xff characters through vt+0x48; the counting and memory streams write that one byte through vt+0x48
};
