// struct: TrackDataSystem
// subsystem: game
// size: 0x230  L1 xref:0x1407e70f8 (alloc size in GameModule init 0x1407e5420)
// notes: the TRACK_DATA system: every track_data.game_setup.TrackDataSet in memory, built by the reader 0x142e44b50 and queried by Game_TrackData_GetTrack 0x142e60e00. GameModule+0x280 and CareerEntity+0xf58 point at it (0x1407e712f, 0x14036fe70; the entity base ctor 0x14079d540 stores it at 0x14079dc29 from the systems map under the key vt_Game_TrackDataSystem slot 0 returns, 0x146080ce8). +0x20..+0x38 is the track list {allocator, begin, end, capacity} of 16-byte items {allocator, TrackDataRecord *} indexed by the track statId, filled by 0x142e57740 from the tracks table loader 0x142e4c640, which 0x142e58f20 runs on this+0x20 (0x142e5b8e5); Sess_Apply_ContentAvailability passes the system there from its +0x88 (resolved by 0x1413ec1a0 at 0x1413ec332). Used by addons/progressive/native/src/fuel_logic.hpp.
struct TrackDataSystem {
    /* 0x000 */ void **vtable;                // L1 xref:0x142e58ab6 vt_Game_TrackDataSystem 0x1457ed380
    /* 0x010 */ void *arena;                  // L1 xref:0x142e58a45 the TRACK_DATA memory arena the ctor is given
    /* 0x028 */ void *records_begin;          // L1 decomp:re/decomp/142e57740.c;decomp:re/decomp/142e58f20.c;decomp:re/decomp/142e44b50.c;xref:0x142e5b8e5,0x142e57a3c,0x142e57a98,0x142e458e4 16-byte items {allocator, TrackDataRecord *} by track statId (TrackDataRecord +0x01)
    /* 0x030 */ void *records_end;            // L1 decomp:re/decomp/142e57740.c;decomp:re/decomp/142e44b50.c;xref:0x142e57a07,0x142e57a9c,0x142e458e8 end of the track list
    /* 0x068 */ TrackDataSet *sets_begin;     // L2 decomp:re/decomp/142e44b50.c;decomp:re/decomp/142e5e010.c;decomp:re/decomp/142e60e00.c;xref:0x142e5e05c,0x142e44e1e,0x142e4529b,0x142e60e0e the track data sets, 0x30 bytes each, in database row order (allocator +0x60, capacity +0x78); verified V1154
    /* 0x070 */ TrackDataSet *sets_end;       // L2 decomp:re/decomp/142e44b50.c;decomp:re/decomp/142e60e00.c;xref:0x142e44e25,0x142e452aa,0x142e45037,0x142e60e12 end of the sets; verified V1155
};
