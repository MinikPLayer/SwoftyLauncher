// struct: ScheduleComponent
// subsystem: game
// size: ?
// notes: the career ScheduleComponent (desc k_Career_ScheduleComponentDesc 0x146063100; DriverCareerContractsManagerComponent +0x370 holds it, stored by 0x140d6f890 at 0x140d6f9d5 from the entity component registry get with that desc). Per-season calendars at +0x410..+0x418 (0x80-byte ScheduleSeason records), the current season index at +0x428, activity definitions in a hash map at +0x378..+0x388. Its vtable slot at 0x1456cdee8 is the daily tick 0x14126a1a0. Raised by laneS.
struct ScheduleComponent {
    /* 0x410 */ ScheduleSeason *seasons_begin; // L2 decomp:re/decomp/141269180.c;decomp:re/decomp/14126a1a0.c;xref:0x141269181,0x14126befc per-season calendar records, 0x80 each (indexed by +0x428 with shl 7 at 0x14126bf2a)
    /* 0x418 */ ScheduleSeason *seasons_end;   // L2 decomp:re/decomp/141269180.c;decomp:re/decomp/14126a1a0.c;xref:0x14126919b,0x14126bf13 end of the season records (count = (end - begin) >> 7)
};
