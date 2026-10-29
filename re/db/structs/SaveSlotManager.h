// struct: SaveSlotManager
// subsystem: game
// size: ?
// notes: a mode's SaveSlotGameModeComponent (Game_SaveSlotManager_Construct 0x1429cd140); its save-data interface is at +0x18 (Game_SaveSlotManager_Deserialise runs with this = manager+0x18).
struct SaveSlotManager {
    /* 0x268 */ SaveSlotInfo **slots_begin;         // L2 decomp:re/decomp/1429d0b90.c;xref:0x1429d0b9c the career slots of the mode
    /* 0x270 */ SaveSlotInfo **slots_end;           // L2 decomp:re/decomp/1429d0b90.c;xref:0x1429d0ba3
    /* 0x280 */ SaveSlotInfo *current;              // L2 decomp:re/decomp/1429d0b90.c;xref:0x1429d0c40,0x1429d0c6a the loaded career's slot, set by Game_SaveSlotManager_SelectSlot (site_Game_SaveSlot_Selected)
    /* 0x2c0 */ uint64_t highest_slot_id;           // L1 decomp:re/decomp/1429d6810.c;xref:0x1429d6aff key highest_slot_id, read at load into this+0x2a8 of the +0x18 interface
};
