// struct: GdbValueMap
// subsystem: gamedb
// size: 0x48  L2 decomp:re/decomp/1445a0350.c;decomp:re/decomp/1445a66c0.c (init writes 0x1445a04dd..0x1445a0535 end at db+0xec; the next field db+0xf0 is GdbDatabase.loaded)
// notes: the game value database map, embedded at GdbDatabase+0xa8: a martinus robin_hood flat Table<uint64_t key, record *> with an allocator pointer, MaxLoadFactor100 = 80. Slots are GdbValueSlot[mask+1+min(max_num_elements_allowed,0xff)] followed in the same block by one info byte per slot plus a sentinel; block size (n+min(n*80/100,0xff))*17+8 for n buckets (2^20: 17,830,135 B; 2^21: 35,655,927 B; 2^22: 71,307,511 B). Bucket of a key: h = key*hash_multiplier; h ^= h>>33; index = (h>>5)&mask; info = info_inc + ((h&0x1f)>>info_hash_shift). Growth = Gdb_HashMap_Rehash, which frees every record of a non-empty map (kb:subsystems/gamedb.md#value-map).
struct GdbValueMap {
    /* 0x008 */ void *allocator;                     // L2 decomp:re/decomp/1445a0350.c;decomp:re/decomp/1445a66c0.c;xref:0x1445a04dd,0x1445a67d6 = &GdbDatabase.record_alloc; bucket block alloc vt+0x10, free vt+0x38
    /* 0x010 */ uint64_t hash_multiplier;            // L2 decomp:re/decomp/1445a0350.c;decomp:re/decomp/1445a22f0.c;xref:0x1445a04ef,0x1445a233f initial 0xc4ceb9fe1a85ec53; Gdb_HashMap_Insert adds 0xc4ceb9fe1a85ec54 before a same-size rehash
    /* 0x018 */ GdbValueSlot *keyvals;              // L2 decomp:re/decomp/1445a0350.c;decomp:re/decomp/1445a66c0.c;xref:0x1445a04ff,0x1445a672e,0x1445a680f points at mask while empty (no storage)
    /* 0x020 */ uint8_t *info;                       // L2 decomp:re/decomp/1445a0350.c;decomp:re/decomp/1445a66c0.c;xref:0x1445a0507,0x1445a6732,0x1445a6816 = keyvals + slots*16; points at mask while empty
    /* 0x028 */ uint64_t num_elements;               // L2 decomp:re/decomp/1445a0350.c;decomp:re/decomp/1445a66c0.c;xref:0x1445a0513,0x1445a09db,0x1445a6746 record count (core runtime::Robin::count); 742,857 after the stock load
    /* 0x030 */ uint64_t mask;                       // L2 decomp:re/decomp/1445a0350.c;decomp:re/decomp/1445a66c0.c;xref:0x1445a0513,0x1445a0a8d,0x1445a66e0,0x1445a6753 bucket count - 1 (0xfffff after the stock load); 0 = no storage
    /* 0x038 */ uint64_t max_num_elements_allowed;   // L2 decomp:re/decomp/1445a0350.c;decomp:re/decomp/1445a66c0.c;xref:0x1445a051d,0x1445a67af (mask+1)*80/100, the growth point (838,860 for 2^20); set to 0 by an info-byte overflow so the next new key runs increase_size
    /* 0x040 */ uint32_t info_inc;                   // L2 decomp:re/decomp/1445a0350.c;decomp:re/decomp/1445a22f0.c;xref:0x1445a0529,0x1445a2354,0x1445a682c 0x20 after init/rehash; halves (try_increase_info) on info overflow; below 3 the next overflow rehashes
    /* 0x044 */ uint32_t info_hash_shift;            // L2 decomp:re/decomp/1445a0350.c;decomp:re/decomp/1445a22f0.c;xref:0x1445a0529,0x1445a2360 0 after init; +1 per info_inc halving
};
