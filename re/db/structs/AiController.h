// struct: AiController
// subsystem: session
// size: 0x28 L2 decomp:re/decomp/1438968e0.c;xref:0x143896a90
// notes: vtable vt_Sess_AiController (0x145882150): slot 0 destructor, slot 1 update (Sess_AiController_Update), slot 2 returns the input record (called as c->vtable[2] by Sess_ControllerSystem_ApplyInputs), slot 3. The human controller (vt_Sess_HumanController, 0x40 bytes) has the same +8 input record.
struct AiController {
    /* 0x00 */ void **vtable;                  // L2 decomp:re/decomp/1438968e0.c;xref:0x143896a90
    /* 0x08 */ struct InputRecord *input;      // L2 decomp:re/decomp/143895940.c;xref:0x1438959a0 0x20-byte heap record
    /* 0x10 */ void *owner;                    // L2 decomp:re/decomp/143895940.c;xref:0x143895944 controller system (set +0x58): +0x38 AI driver data, +0x88/+0x90 vehicle masks
    /* 0x18 */ uint8_t vehicle;                // L2 decomp:re/decomp/143895940.c;xref:0x14389594c vehicle index
    /* 0x20 */ void *allocator;                // L2 decomp:re/decomp/1438968e0.c;xref:0x1438971e0
};
