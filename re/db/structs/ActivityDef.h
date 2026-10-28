// struct: ActivityDef
// subsystem: game
// size: ?
// notes: a career activity definition (ScheduleComponent's definitions map at +0x378..+0x388, 0x18-byte map slots with the definition pointer at +0x10), passed to Game_CareerActivities_AddActivity. +0x08 u16 id, +0x60 game mode mask (bit 1 Driver Career, bit 2 My Team). Raised by laneS.
struct ActivityDef {
    /* 0x010 */ uint8_t type; // L2 decomp:re/decomp/14126a1a0.c;decomp:re/decomp/140d792a0.c;decomp:re/decomp/141269180.c;decomp:re/decomp/1419a64b0.c;xref:0x14126b810,0x140d79422,0x14126927f the activity type, names at 0x145731eb0 (0x0f NCSContractRenewal, the contract event; 0x10 NSCSSM, the secret meeting); the daily tick looks the contract event up by type 0x0f (0x14126b810), the secret meeting adder by 0x10 (0x14126927f); the Team HQ advance card 0x1419a64b0 switches on it
};
