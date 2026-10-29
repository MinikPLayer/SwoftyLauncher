// struct: InputRecord
// subsystem: session
// size: 0x20 L2 decomp:re/decomp/1438968e0.c;xref:0x143896a90
// notes: the per-car driving input both controller classes fill and Sess_ControllerSystem_ApplyRecord consumes. Initial value dword[0] = 0x0000000b00000001 (two dwords), +8 = 1, the rest 0. Field names throttle/steering/brake come from addon developers; ApplyRecord clamps +0xc and +0x10 to 0..1, +0x14 to -1 (negated) and +0x18 to 0..1.
struct InputRecord {
    /* 0x00 */ uint32_t unk_0x00;              // L2 decomp:re/decomp/143895940.c;xref:0x143895940 copied by the AI update
    /* 0x04 */ int32_t gear_request;           // L2 decomp:re/decomp/14389a2b0.c;xref:0x14389a5a0 stored to the car (+0x1b4); not copied by the AI update
    /* 0x08 */ uint32_t unk_0x08;              // L2 decomp:re/decomp/143895940.c;xref:0x143895940
    /* 0x0c */ float throttle;                 // L1 decomp:re/decomp/14389a2b0.c;xref:0x14389a470 clamped 0..1; human: device+0x4dc
    /* 0x10 */ float pedal2;                   // L1 decomp:re/decomp/14389a2b0.c;xref:0x14389a540 clamped 0..1; human: max of two analog actions
    /* 0x14 */ float steering;                 // L1 decomp:re/decomp/14389a2b0.c;xref:0x14389a5b0 clamped to -1, negated; human: device+0x4e0
    /* 0x18 */ float brake;                    // L1 decomp:re/decomp/14389a2b0.c;xref:0x14389a640 clamped 0..1; human: device+0x4d8
    /* 0x1c */ uint32_t flags;                 // L2 decomp:re/decomp/14389a2b0.c;xref:0x14389a2b0 bit flags (pit limiter, DRS, ERS-style buttons; bit 0x200 at +0x1d tested)
};
