// struct: ControllerSet
// subsystem: session
// size: 0x60 L2 decomp:re/decomp/143896ed0.c;xref:0x143897a5d
// notes: one per car (and per spectator-list entry), made by Game_ControllerSet_Create inside Sess_ControllerSystem build-all (vt slot 11), freed by Game_ControllerSet_Destroy (destroy-all, vt slot 24). Both controllers are heap objects: Game_ControllerSet_CreateController / DestroyController replace them on a driver swap or a drop-in, so a kept pointer to [+0x20] or [+0x28] can dangle even when the set itself lives. Mode = dword[+0x10] ^ dword[+0x18]: 0 HUMAN, 1 AI, any other value is INVALID (log text). Both dwords are re-randomised on every change (anti-tamper pair); write a mode only through Game_Set_VehicleController_Thunk.
struct ControllerSet {
    /* 0x00 */ void *allocator;                // L2 decomp:re/decomp/1438968e0.c;xref:0x143896ed0 source of controller memory (vtable +0x18 alloc, +0x38 free)
    /* 0x08 */ uint8_t unk_0x08;               // L2 decomp:re/decomp/1438968e0.c;xref:0x1438968e0 set to 1 at creation
    /* 0x10 */ uint32_t mode_masked;           // L2 decomp:re/decomp/1438974a0.c;xref:0x1438974a0 mode ^ rnd, see notes
    /* 0x18 */ uint32_t mode_key;              // L2 decomp:re/decomp/1438974a0.c;xref:0x1438974a0 rnd; mode = [+0x10] ^ [+0x18]
    /* 0x20 */ void *human_controller;         // L2 decomp:re/decomp/1438968e0.c;xref:0x14389be42 0x40-byte object, vtable vt_Sess_HumanController, or null
    /* 0x28 */ void *ai_controller;            // L2 decomp:re/decomp/1438968e0.c;xref:0x14389be42 0x28-byte object, vtable vt_Sess_AiController, or null
    /* 0x30 */ uint32_t flags;                 // L2 decomp:re/decomp/143897e10.c;xref:0x1519eb3b1 bit n = controller flag n (PIT_LANE 0 ... PDS 17); non-zero keeps a requested-human car on AI
    /* 0x34 */ uint8_t vehicle;                // L2 decomp:re/decomp/1438968e0.c;xref:0x1438968e0 vehicle index
};
