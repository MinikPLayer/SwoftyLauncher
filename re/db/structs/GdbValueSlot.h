// struct: GdbValueSlot
// subsystem: gamedb
// size: 0x10  L2 decomp:re/decomp/1445a66c0.c;decomp:re/decomp/1445a22f0.c (slot stride 0x10)
// notes: one bucket of GdbValueMap.keyvals. Gdb_Lookup_Key returns &slot.value. A rehash copies the pair and then destroys and frees the record the old slot points at, so the copy dangles (kb:subsystems/gamedb.md#value-map).
struct GdbValueSlot {
    /* 0x000 */ uint64_t key;                        // L2 decomp:re/decomp/1445a22f0.c;decomp:re/decomp/1445a12d0.c;xref:0x1445a2403 fnv1a64 of the dotted path
    /* 0x008 */ void *value;                         // L2 decomp:re/decomp/1445a22f0.c;decomp:re/decomp/1445a66c0.c;xref:0x1445a2403,0x1445a6be7 the record (vtable per tag, value at +8 for scalars); 0 while a new key's record is being made
};
