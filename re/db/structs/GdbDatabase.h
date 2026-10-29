// struct: GdbDatabase
// subsystem: gamedb
// size: ?
// notes: the game value database object built by Gdb_Load_GameDatabase (its constructor and loader) and held in g_Gdb_Database. Core: gamedb.cpp DB_ALLOC 0x8, DB_MAP 0xa8, DB_LOADED 0xf0.
struct GdbDatabase {
    /* 0x000 */ void **vtable;                       // L2 decomp:re/decomp/1445a0350.c;xref:0x1445a0370 0x145bee5d0
    /* 0x008 */ void **record_alloc_vtable;          // L2 decomp:re/decomp/1445a0350.c;decomp:re/decomp/1445a6310.c;xref:0x1445a04c4,0x1445a0562 start of the embedded record allocator (vtable 0x145bee810, name GVDB_READ_ONLY_REDIRECT at +0x60); its slots forward to the heap at +0x90; g_Gdb_RecordAllocator points here
    /* 0x090 */ void *heap;                          // L2 decomp:re/decomp/1445a0350.c;decomp:re/decomp/1445a6310.c;decomp:re/decomp/1445a6370.c;xref:0x1445a04b5,0x1445a6310 the GAME_VALUE_DB heap (memory manager lookup by name); record_alloc vt+0x10 and vt+0x38 jump through it
    /* 0x0a8 */ GdbValueMap map;                     // L2 decomp:re/decomp/1445a0350.c;xref:0x1445a0a98,0x1445a12d0 &map is the Gdb_HashMap_Rehash this pointer
    /* 0x0f0 */ uint8_t loaded;                      // L2 decomp:re/decomp/1445a0350.c;xref:0x1445a0535,0x1445a0f41 0 during the load, 1 after every record was inserted
};
