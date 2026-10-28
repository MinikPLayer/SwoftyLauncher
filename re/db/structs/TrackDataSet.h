// struct: TrackDataSet
// subsystem: game
// size: 0x30  L1 decomp:re/decomp/142e44b50.c (vector growth /0x30);xref:0x142e60e60
// notes: one track_data.game_setup.TrackDataSet row in memory (+0x00 allocator, +0x18 items capacity). Game_TrackData_GetTrack takes the first set whose masks hold the query's bits: mode bit = game setup mode stat_id (f1 0, f1_26 13), season bit = season statId (season_2025 7, season_2026 8), format bit = weekend format (default 0, sprint 1). The masks are built by 0x142a0ddb0 from the row's game_setup_modes, seasons and weekend_formats. The reader skips a set whose three masks equal an earlier set's.
struct TrackDataSet {
    /* 0x008 */ void *tracks_begin;           // L2 decomp:re/decomp/142e44b50.c;decomp:re/decomp/142e60e00.c;xref:0x142ed54eb,0x142e49d98,0x142e60eb5 16-byte items {?, TrackDataTrack *} by track index (TrackDataRecord +0x01); verified V1156
    /* 0x010 */ void *tracks_end;             // L2 decomp:re/decomp/142e44b50.c;decomp:re/decomp/142e60e00.c;xref:0x142ed54f6,0x142e455a6,0x142e60eb9 end of the items; verified V1157
    /* 0x020 */ uint32_t mode_mask;           // L2 decomp:re/decomp/142e44b50.c;decomp:re/decomp/142a0ddb0.c;decomp:re/decomp/142e60e00.c;xref:0x142e45024,0x142e45122,0x142e60e7b bits of the set's game_setup_modes stat ids; verified V1158
    /* 0x024 */ uint32_t season_mask;         // L2 decomp:re/decomp/142e44b50.c;decomp:re/decomp/142a0ddb0.c;decomp:re/decomp/142e60e00.c;xref:0x142e45024,0x142e60e90 bits of the set's seasons statIds; verified V1159
    /* 0x028 */ uint32_t format_mask;         // L2 decomp:re/decomp/142e44b50.c;decomp:re/decomp/142a0ddb0.c;decomp:re/decomp/142e60e00.c;xref:0x142e4502c,0x142e45119,0x142e60ea4 bit 1 for a sprint weekend format, bit 0 for any other; verified V1160
};
