// struct: TodCurveBinding
// subsystem: game
// size: 0x30 L1 decomp:re/decomp/14029c310.c bindings sit 0x30 apart (weather state +0xd48 + slot*0x30)
// notes: one TimeOfDay parameter (e.g. "weather/track/wetness", "weather/rainfall/intensity") bound to a curve.
// Evaluated by Game_TodCurveBinding_EvalNow 0x14029c310 / _EvalAt 0x140b12c10. Raised by vmknolle (wet-track).
struct TodCurveBinding {
    /* 0x000 */ void *curve;             // L1 decomp:re/decomp/14029c310.c null -> the constant +0x10 is returned
    /* 0x008 */ void *owner;             // L1 decomp:re/decomp/14029c310.c TimeOfDay clock: +0x68/+0x70 time (double), +0x80 scale, +0x84 length, +0x90 fraction, +0x22 24 h flag, +0x268 lock, +0x270 version
    /* 0x010 */ float constant;          // L1 decomp:re/decomp/14029c310.c value when there is no curve
    /* 0x020 */ int32_t key_a;           // L1 decomp:re/decomp/14029c3f0.c curve lookup key in owner+0x28 (32-byte records)
    /* 0x024 */ int32_t key_b;           // L1 decomp:re/decomp/14029c3f0.c
    /* 0x028 */ int32_t mode;            // L1 decomp:re/decomp/14029c310.c 1 = sample at owner+0x90
    /* 0x02c */ int32_t cached_version;  // L1 decomp:re/decomp/14029c3f0.c compared with owner+0x270
};
