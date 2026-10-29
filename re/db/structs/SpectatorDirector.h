// struct: SpectatorDirector
// subsystem: session
// size: ? L2 decomp:re/decomp/142e18650.c
// notes: game::SpectatorDirector. Reached from the session state at +0xd70 as the session object whose +0xd1 is the 'spectator director active' flag (RETIREMENT_SEQUENCE log) and from HudSpectatorScreenViewModel at +0x278. Camera values: 1 TV pod, 2 broadcast (the HUD command toggles the request between them), 0 is also compared as a camera value by the update. 0xff driver = none.
struct SpectatorDirector {
    /* 0x30 */ void *fade_state;               // L1 decomp:re/decomp/142e18650.c bit 8 of [+0x54] cleared on deactivate
    /* 0x40 */ void *vehicle_manager;          // L1 decomp:re/decomp/142e18650.c entity list [+0x28..+0x30]
    /* 0xa8 */ void *screen_node;              // L2 decomp:re/decomp/142e18d10.c;xref:0x142e18d10 view-model node set by the UI-flag thunk
    /* 0xd0 */ uint8_t ui_active;              // L2 decomp:re/decomp/142e18d10.c;xref:0x142e18d10 UI flag session.spectator_mode.active; MessageSpectatorStateChanged carries it
    /* 0xd1 */ uint8_t active;                 // L2 decomp:re/decomp/142e18650.c;xref:0x142e18693 setActive writes it; read as 'spectatorDirector active' in 0x142d8a440
    /* 0xd2 */ uint8_t blocked;                // L1 decomp:re/decomp/142e18dd0.c update and RequestDriver return early when set
    /* 0xd3 */ uint8_t should_quit;            // L2 decomp:re/decomp/142e18650.c;xref:0x142d6a590 cleared on activate; tested by SessionStateInGameSpectating (getShouldQuit)
    /* 0xd6 */ uint8_t current_driver;         // L2 decomp:re/decomp/142e19d50.c;xref:0x142e19d50 written by SetCurrent; 0xff none
    /* 0xd8 */ int32_t current_camera;         // L2 decomp:re/decomp/142e19d50.c;xref:0x142e19d50 written by SetCurrent; -1 none
    /* 0xdc */ uint8_t requested_driver;       // L2 decomp:re/decomp/142e19950.c;xref:0x142e18650 written by SetActive, the HUD next/previous commands (0x141c24c63, 0x141c26725) and RequestDriver (which only steps off the current driver, V1535); 0xff none
    /* 0xe0 */ int32_t requested_camera;       // L2 decomp:re/decomp/142e18650.c;xref:0x141c244e0 SetActive writes 2 (broadcast); the HUD command toggles 1/2
};
