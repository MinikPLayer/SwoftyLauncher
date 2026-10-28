// struct: NePropertyVtbl
// subsystem: core
// size: ?
// notes: vtable of a property-bag entry; a bag's entries are pointers at bag+0x8, their count a u16 at bag+0x10
struct NePropertyVtbl {
    /* 0x008 */ void *type;                         // L3 decomp:re/decomp/14364f3a0.c;decomp:re/decomp/1439340c0.c;xref:0x14364ff4d,0x14364fe74;trace:T010/run2:86288;trace:T010/run3 void *(*)(property): the value type object (NePropertyTypeVtbl)
    /* 0x038 */ void *value;                        // L3 decomp:re/decomp/14364f3a0.c;decomp:re/decomp/1439340c0.c;xref:0x14364ff1a;trace:T010/run2:86288;trace:T010/run3 void *(*)(property): the value; for a bag-typed entry the bag Game_SaveData_WriteBag recurses into
};
