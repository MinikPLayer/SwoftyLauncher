// struct: ScheduleSeason
// subsystem: game
// size: 0x80  L2 decomp:re/decomp/14126a1a0.c;decomp:re/decomp/141269180.c (ScheduleComponent +0x410 records indexed with shl 7 at 0x14126bf2a and 0x1412691b2)
// notes: one season's calendars in ScheduleComponent: the drivers' (0xc8-byte DriverCalendar objects, +0x40..+0x48) and the teams' (0xd8-byte objects, +0x60..+0x68). The daily tick adds the NCSContractRenewal activity to every one of both (0x14126bf2e..0x14126bf82). Raised by laneS.
struct ScheduleSeason {
    /* 0x040 */ DriverCalendar *driver_calendars_begin; // L2 decomp:re/decomp/14126a1a0.c;decomp:re/decomp/141269180.c;xref:0x14126bf2e,0x1412691b6 the drivers' calendars, 0xc8 each (stride at 0x14126bf4b)
    /* 0x048 */ DriverCalendar *driver_calendars_end;   // L2 decomp:re/decomp/14126a1a0.c;decomp:re/decomp/141269180.c;xref:0x14126bf33,0x1412691bb end of the drivers' calendars
};
