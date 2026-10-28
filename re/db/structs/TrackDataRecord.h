// struct: TrackDataRecord
// subsystem: game
// size: 0xd08  L1 decomp:re/decomp/142e57740.c;xref:0x142e57a16,0x142e578df (alloc 0xd08 align 0x10 per record)
// notes: one tracks.Track row in memory (TrackManager), built by the tracks table loader 0x142e4c640 (KB name Game_Read_ERSSettingsConfig) as a local record and stored by 0x142e57740 as a 0xd08-byte copy (copy ctor 0x142ee6710) in the TRACK_DATA system's track list (TrackDataSystem +0x28..+0x30, 16-byte items {allocator, TrackDataRecord *} indexed by +0x01, in-range store 0x142e57a49/0x142e57a70, append 0x142e579f8/0x142e579fb). The TrackData reader matches a TrackData row's track field against +0xd0. +0x00 is set to 1 by the loader.
struct TrackDataRecord {
    /* 0x001 */ uint8_t index;                // L2 decomp:re/decomp/142e4c640.c;decomp:re/decomp/142e57740.c;decomp:re/decomp/142ee6710.c;decomp:re/decomp/142e44b50.c;decomp:re/decomp/142e60e00.c;xref:0x142e4c7ff,0x142e53c4d,0x142e5775e,0x142ee6743,0x142e45939,0x142e60eb1 the track statId (low byte): its slot in the system's track list and in every TrackDataSet's items; verified V1171
    /* 0x0d0 */ char *name;                   // L2 decomp:re/decomp/142e4c640.c;decomp:re/decomp/142ee6710.c;decomp:re/decomp/142e44b50.c;xref:0x142e4cd24,0x142e53e1a,0x142ee67f5,0x142e45926 the track's instanceName ("melbourne"), the char * of the 0x40-byte string object at +0xc0 (inline chars at +0xd8); compared at 0x142e45930; verified V1172
};
