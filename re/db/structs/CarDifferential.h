// struct: CarDifferential
// subsystem: car
// size: 0x100  L2 decomp:re/decomp/1447518c0.c (alloc 0x100)
// notes: the Differential system at CarLive+0x578 (Car_Create_DifferentialSystem 0x1447518c0). Data at +0x18 (reader 0x14467ddf0 ->
// 0x1553d2720). DiffRearTorque is read into a temporary and dropped. Setup on/off-throttle diff: Reset 0x14467e010 -> 0x1553d6f70 (flag 0x2000) writes +0xec..+0xfc from F1Tuning.
struct CarDifferential {
    /* 0x0000 */ void *vtable;                                // L2 decomp:re/decomp/1447518c0.c;xref:0x1447518d4,0x144751907 system vtable 0x145bfa120 (CarComponentBase)
    /* 0x0008 */ CarLive *car;                                // L2 decomp:re/decomp/1447518c0.c;xref:0x1447518f8 owning car
    /* 0x0010 */ void *allocator;                             // L2 decomp:re/decomp/1447518c0.c;xref:0x1447518fc car allocator
    /* 0x0018 */ void *data_vtable;                           // L2 decomp:re/decomp/1447518c0.c;xref:0x1447518e2,0x144751915 DifferentialData vtable 0x145bfa040 {dtor, GetName "Differential", Car_Read_Differential, Car_Write_Differential}; reader offsets = field - 0x18
    /* 0x0030 */ CarObfFloat rear_power_proportion;           // L2 decomp:re/decomp/1553d2720.c;xref:0x1553d27af,0x1553d27db VTF Differential/RearPowerProportion percent, Ferrari 100 (obf)
    /* 0x0040 */ CarObfFloat diff_rear_no_effect;             // L2 decomp:re/decomp/1553d2720.c;xref:0x1553d27de,0x1553d2803 VTF Differential/DiffRearNoEffect Ferrari -2.4 (obf)
    /* 0x0050 */ CarObfFloat diff_rear_full_effect;           // L2 decomp:re/decomp/1553d2720.c;xref:0x1553d2806,0x1553d282b VTF Differential/DiffRearFullEffect Ferrari 2.2 (bumped +1 if equal to NoEffect) (obf)
    /* 0x0060 */ CarObfFloat inv_effect_range;                // L2 decomp:re/decomp/1553d2720.c;xref:0x1553d291c derived in the reader: 1/(DiffRearFullEffect - DiffRearNoEffect)
    /* 0x0070 */ CarObfFloat diff_rear_max_locking_torque;    // L2 decomp:re/decomp/1553d2720.c;xref:0x1553d282e,0x1553d2853 VTF Differential/DiffRearMaxLockingTorque Nm, Ferrari 1500 (obf)
    /* 0x0080 */ CarObfFloat diff_mid_corner_g_force;         // L2 decomp:re/decomp/1553d2720.c;xref:0x1553d2856,0x1553d287b VTF Differential/DiffMidCornerGForce (obf)
    /* 0x0090 */ CarObfFloat diff_g_force_blend_slope;        // L2 decomp:re/decomp/1553d2720.c;xref:0x1553d287e,0x1553d28a9 VTF Differential/DiffGForceBlendSlope (obf)
    /* 0x00a0 */ uint8_t ramp_differential;                   // L2 decomp:re/decomp/1553d2720.c;xref:0x1553d28c0 VTF Differential/RampDifferential (bool reader 0x14464c220)
    /* 0x00e0 */ float rear_lock_amount;                      // L2 decomp:re/decomp/1447518c0.c;xref:0x14467e720,0x144751bab channel "rear_lock_amount" (handling_version >= 7)
    /* 0x00ec */ float diff_off_throttle;                     // L2 decomp:re/decomp/1553d6f70.c;xref:0x1553d7010 setup diff off-throttle multiple = lerp(F1Tuning data +0x20..+0x24, slider F1Tuning+0x218) in Reset 0x2000 (logs F1_TUNING "Setting diff off throttle multiple")
    /* 0x00f0 */ float diff_on_throttle;                      // L2 decomp:re/decomp/1553d6f70.c;xref:0x1553d70ec setup diff on-throttle multiple = lerp(F1Tuning data +0x18..+0x1c, slider F1Tuning+0x214) (logs "Setting diff on throttle multiple")
    /* 0x00f4 */ float setup_diff_param_f4;                   // L2 decomp:re/decomp/1553d6f70.c;xref:0x1553d71c3 lerp(F1Tuning data +0x28..+0x2c, slider F1Tuning+0x21c); role not identified
    /* 0x00f8 */ float setup_diff_param_f8;                   // L2 decomp:re/decomp/1553d6f70.c;xref:0x1553d7208 F1Tuning+0x2a0 clamped to F1Tuning data +0x30..+0x34; role not identified
    /* 0x00fc */ float setup_diff_param_fc;                   // L2 decomp:re/decomp/1553d6f70.c;xref:0x1553d724d F1Tuning+0x2a4 clamped to F1Tuning data +0x38..+0x3c; role not identified
};
