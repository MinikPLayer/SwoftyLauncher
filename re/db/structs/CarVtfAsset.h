// struct: CarVtfAsset
// subsystem: car
// size: 0x50  L2 decomp:re/decomp/14475a250.c;xref:0x14475a278 (alloc 0x50)
// notes: the loaded VTF resource (one per handling file, shared by every car built from it). Built by
// Car_Load_Vtf (resource loader slot 0x145c04c70); CarLive+0x4a8 points here. Section descriptors are
// 0x40 bytes: +0x10 char *name (inline buffer at +0x18), +0x38 factory void*(CarLive*).
struct CarVtfAsset {
    /* 0x0000 */ void **vtable;           // L2 decomp:re/decomp/14475a250.c;xref:0x14475a2b0 vt_Car_VtfAsset 0x145c04a30
    /* 0x0008 */ void *allocator;         // L2 decomp:re/decomp/14475a250.c;xref:0x14475a2b0 loader allocator
    /* 0x0010 */ void *document;          // L2 decomp:re/decomp/14475a250.c;xref:0x14475a462 parsed BXML document (vt 0x145899c20); vt+8 gets the root, root vt+0x20 finds a section by name
    /* 0x0018 */ int32_t handling_version; // L2 decomp:re/decomp/14475a250.c;xref:0x14475a2bb,0x14475a577 Chassis/HandlingVersion: 'pro' -> 100, 'handlingYYYY' -> clamp(YYYY,2018,4048)-2017, else 1; copied to CarLive+0x9f40
    /* 0x0020 */ void *resource_id;       // L2 decomp:re/decomp/14475a250.c;xref:0x14475a2be param_2+8; copied to CarLive+0x9f48
    /* 0x0028 */ void *section_allocator; // L2 decomp:re/decomp/14475a250.c;xref:0x14475a2b0 allocator of the descriptor vector
    /* 0x0030 */ void *sections_begin;    // L2 decomp:re/decomp/14475a250.c;xref:0x14475aa16,0x1447427db section descriptor vector (stride 0x40) read by Car_Assets_CreateSystemForPerformanceMode
    /* 0x0038 */ void *sections_end;      // L2 decomp:re/decomp/14475a250.c;xref:0x14475aa1a,0x1447427df end
    /* 0x0040 */ uint64_t section_count;  // L2 decomp:re/decomp/14475a250.c;xref:0x14475aa1e (end-begin)>>6
    /* 0x0048 */ void *base_asset;        // L2 decomp:re/decomp/144742790.c;xref:0x14474287e equal-performance base handle; [base_asset+0x28] is the base CarVtfAsset whose sections replace the team's
};
