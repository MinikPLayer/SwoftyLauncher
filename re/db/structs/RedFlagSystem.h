// struct: RedFlagSystem
// subsystem: session
// size: ?
// notes: cxx=RedFlagSystem, vtable vt_Sess_RedFlagSystem 0x14584b0d0. Deploy 0x143583b60, update 0x143583fb0.
// Raised by vmknolle (red-flag-pit).
struct RedFlagSystem {
    /* 0x000 */ void **vtable;          // L1 pat:flow-state-type-record type record 0x1460fde00
    /* 0x040 */ uint8_t state;          // L2 decomp:re/decomp/143583fb0.c;xref:0x143583b60 0 idle, 1 countdown after deploy, 2 suspended (status message value 2)
    /* 0x044 */ float countdown_length; // L2 decomp:re/decomp/143583b60.c;xref:0x143583b60 copied to +0x48 on deploy (0 when restore)
    /* 0x048 */ float countdown;        // L2 decomp:re/decomp/143583fb0.c;xref:0x143583fb0 seconds left in state 1
    /* 0x0e0 */ uint8_t *car_status;    // L2 decomp:re/decomp/143583fb0.c;xref:0x143583cb0 2 bytes per car: status (NotReady/Virtualised/Teleported/Repaired/Ready), dirty
    /* 0x0e8 */ uint8_t *car_status_end; // L2 decomp:re/decomp/143583cb0.c;xref:0x143583fb0
    /* 0x130 */ uint8_t countdown_low;  // L1 decomp:re/decomp/143583fb0.c set when countdown <= +0x138; reader unknown
    /* 0x131 */ uint8_t countdown_low_timer_on; // L1 decomp:re/decomp/143583fb0.c clears +0x130/+0x131 after +0x134 reaches +0x13c
    /* 0x134 */ float countdown_low_timer; // L1 decomp:re/decomp/143583fb0.c
    /* 0x138 */ float countdown_low_at;  // L1 decomp:re/decomp/143583fb0.c
    /* 0x13c */ float countdown_low_length; // L1 decomp:re/decomp/143583fb0.c
    /* 0x148 */ void *controller_system; // L2 decomp:re/decomp/143583b60.c;xref:0x143583b60 VehicleControllerSystem for setController
    /* 0x150 */ void *session;          // L1 decomp:re/decomp/143583b60.c car list via +0x88 -> +0x260 vtable 0xa0
    /* 0x158 */ void *participants;     // L1 decomp:re/decomp/143583cb0.c cars +0x108..+0x110 (8 bytes each, id byte at +0)
};
