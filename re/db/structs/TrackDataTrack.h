// struct: TrackDataTrack
// subsystem: game
// size: 0x290  L1 decomp:re/decomp/142e44b50.c (alloc 0x290 at 0x142e459b2)
// notes: one track of one TrackDataSet (TrackData row, sub-tables the set lacks come from its parent_data_set at read time). Fuel fields from the reader's TrackGeneralData / TrackFuelSettings reads; RaceFuelSettings row layout (0x34): +0x00 percent_100, +0x04 laps_1, +0x08 laps_3, +0x0c laps_5, +0x10 percent_25, +0x14 percent_35, +0x18 percent_50, +0x1c percent_100, +0x20 laps_10, +0x24 laps_15, +0x28 laps_20, +0x2c laps_2, +0x30 laps_9 (floats); the reader does not bound the row count (stock max 2). Qualifying fuel is a float array at +0x1b0 indexed by a qualifying-format byte k < 6 (else 4): [0] 10.0f default, [1] one_shot, [3] short, [4] full (0x140b781a2). Readers: Game_TrackData_GetTrack callers read +0xf0 (0x142d1d751, 0x1436aea77: fuel in tank -= laps * burn) and the race fuel rows and count (0x140b7814a, 0x140b78159).
struct TrackDataTrack {
    /* 0x000 */ TrackDataRecord *record;      // L2 decomp:re/decomp/142e44b50.c;xref:0x142e459c1,0x142d04eea the track's record from TrackDataSystem +0x28; verified V1161, V1170
    /* 0x0f0 */ float fuel_burn_per_lap;      // L2 decomp:re/decomp/142e44b50.c;decomp:re/decomp/1436ae7b0.c;decomp:re/decomp/142d1d650.c;xref:0x142e46fe7,0x1436aea77,0x142d1d751 TrackGeneralData.fuel_burn_per_lap; verified V1162
    /* 0x100 */ uint8_t race_fuel[0x9c];      // L2 decomp:re/decomp/142e44b50.c;decomp:re/decomp/140b78000.c;xref:0x142e47404,0x142e47412,0x140b78159 RaceFuelSettings rows, 0x34 bytes each, room for 3; verified V1163
    /* 0x1a0 */ uint64_t race_fuel_count;     // L2 decomp:re/decomp/142e44b50.c;decomp:re/decomp/140b78000.c;xref:0x142e46dac,0x142e47472,0x140b7814a RaceFuelSettings row count; verified V1164
    /* 0x1b4 */ float qualifying_one_shot;    // L2 decomp:re/decomp/142e44b50.c;decomp:re/decomp/140b78000.c;xref:0x142e471cd,0x140b781a2 TrackFuelSettings Qualifying.one_shot; verified V1165
    /* 0x1bc */ float qualifying_short;       // L2 decomp:re/decomp/142e44b50.c;decomp:re/decomp/140b78000.c;xref:0x142e4719b,0x140b781a2 TrackFuelSettings Qualifying.short; verified V1166
    /* 0x1c0 */ float qualifying_full;        // L2 decomp:re/decomp/142e44b50.c;decomp:re/decomp/140b78000.c;xref:0x142e4716f,0x140b781a2 TrackFuelSettings Qualifying.full; verified V1167
};
