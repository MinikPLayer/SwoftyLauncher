// struct: SaveSlotInfo
// subsystem: game
// size: ?
// notes: one career save slot of a mode's SaveSlotGameModeComponent (Game_SaveSlotManager_Construct); the manager lists them at +0x268..+0x270 and keeps the loaded one at +0x280 (SaveSlotManager.h). Info keys read by Game_SaveSlotManager_ReadSlotInfo, written by 0x1429d2bc0.
struct SaveSlotInfo {
    /* 0x000 */ uint64_t slot_id;                   // L2 decomp:re/decomp/1429d4a30.c;decomp:re/decomp/1429d0b90.c;xref:0x1429d4ab3,0x1429d0bc3 key slot_id (uint64): written from the info bag at 0x1429d4ab3, matched by Game_SaveSlotManager_SelectSlot at 0x1429d0bc3; unique within the mode's manager, the manager saves highest_slot_id and active_slot_id beside the slots (0x1429d6a1d, 0x1429d6aae)
    /* 0x008 */ uint64_t slot_guid;                 // L2 decomp:re/decomp/1429d4a30.c;decomp:re/decomp/1429d2bc0.c;xref:0x1429d4b11,0x1429d4ab9,0x1429d2c57 key slot_guid (uint64): copied verbatim from the info bag at 0x1429d4b11 and written back under the same key by 0x1429d2bc0 (key ref 0x1429d2c57); where a new slot gets its value is not found yet
    /* 0x020 */ char *name;                         // L1 decomp:re/decomp/1429d0b90.c slot name, logged 'Slot %s : %d' by Game_SaveSlotManager_SelectSlot
    /* 0x178 */ void *blob;                         // L2 decomp:re/decomp/1429d0b90.c;xref:0x1429d0c47 the career data blob; null means 'Load postponed due to empty data'
};
