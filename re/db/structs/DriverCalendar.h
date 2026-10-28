// struct: DriverCalendar
// subsystem: game
// size: 0xc8  L2 decomp:re/decomp/14126a1a0.c;decomp:re/decomp/141269180.c (ScheduleSeason +0x40 elements, stride 0xc8 at 0x14126bf4b and 200 in 0x141269180)
// notes: a driver's career calendar: vtable at +0x00 (slot +0x8 makes an activity instance, +0x10 notes it), the game mode at +0x10, the activity list at +0x48 (+0x50..+0x58 16-byte slots {allocator, instance}, instance day u16 +0x0a), kept by Game_CareerActivities_AddActivity. Raised by laneS.
struct DriverCalendar {
    /* 0x050 */ void *activities_begin; // L2 decomp:re/decomp/141267cf0.c;decomp:re/decomp/141267e30.c;xref:0x141267d3b,0x141267e4b,0x141267e0c the activity list: 16-byte slots {allocator, CalendarActivity *}, kept sorted by day; read by Game_CareerActivities_AddActivity (0x141267d3b) and Game_CareerActivities_RemoveActivity (0x141267e4b)
    /* 0x058 */ void *activities_end;   // L2 decomp:re/decomp/141267cf0.c;decomp:re/decomp/141267e30.c;xref:0x141267d3f,0x141267e4f,0x141267f63 end of the activity list; the remover stores the new end at 0x141267f63
    /* 0x0c0 */ uint32_t driver; // L2 decomp:re/decomp/141269180.c;xref:0x1412691f0 the driver key the calendar belongs to (EntrantRecord +0x50 form): 0x141269180 finds a driver's calendar by it (0x1412691f0)
};
