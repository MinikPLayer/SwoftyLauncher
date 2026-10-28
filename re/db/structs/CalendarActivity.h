// struct: CalendarActivity
// subsystem: game
// size: 0x20  L2 decomp:re/decomp/14128ec00.c (a driver calendar's vtable +0x8, 0x14128ec00, allocates 0x20 bytes, align 0x10, from the calendar's allocator at +0x8)
// notes: one activity instance on a career calendar (DriverCalendar activity slots). The driver calendar's creator 0x14128ec00 writes vtable 0x1456d10d0 at +0x00, byte 1 at +0x08, the day at +0x0a, byte 0 at +0x0c, the definition at +0x10 and the calendar's driver key at +0x18. Raised by laneS.
struct CalendarActivity {
    /* 0x00a */ uint16_t day;            // L2 decomp:re/decomp/14128ec00.c;decomp:re/decomp/141267e30.c;decomp:re/decomp/141267cf0.c;xref:0x14128ec3a,0x141267ece,0x141267d69 the career day the activity is on (career clock minutes / 1440): written by the creator (0x14128ec3a), matched by the remover (0x141267ece) and compared by the adder's sorted insert (0x141267d69)
    /* 0x010 */ ActivityDef *activity;   // L2 decomp:re/decomp/14128ec00.c;decomp:re/decomp/141267e30.c;xref:0x14128ec42,0x141267ed9 the activity definition: written by the creator (0x14128ec42); the remover compares its id (+0x08) with the definition it is given (0x141267ed9, 0x141267edd)
};
