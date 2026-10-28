// struct: NePropertyTypeVtbl
// subsystem: core
// size: ?
// notes: vtable of a property value type object (registered by 0x1439532f0); k_Ne_PropertyType_Bag is the bag type
struct NePropertyTypeVtbl {
    /* 0x010 */ void *id;                           // L3 decomp:re/decomp/1439340c0.c;xref:0x1439342b0,0x1439342c6;trace:T010/run2:86288;trace:T010/run3 int (*)(type): the type id written before each value; equal ids mean the same type
    /* 0x020 */ void *key_offset;                   // L3 decomp:re/decomp/14364f3a0.c;xref:0x14364fe7d,0x14364ff56;trace:T010/run2:86288;trace:T010/run3 size_t (*)(type): offset in the property of its u64 key hash
};
