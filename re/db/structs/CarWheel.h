// struct: CarWheel
// subsystem: car
// size: 0x480  L2 decomp:re/decomp/144753050.c (alloc 0x480, 0x10 aligned)
// notes: one of the four wheels at CarSuspension+0x70+i*8 (i: 0 RL, 1 RR, 2 FL, 3 FR). Fields below +0x3b0 from
// Car_Tyre_GripScale; the +0x3b0.. block is the telemetry row order (older L1 note) and needs a trace.
struct CarWheel {
    /* 0x00f8 */ CarObfFloat max_brake_torque;                // L2 decomp:re/decomp/144676250.c;xref:0x140619eb9 written by Car_BrakeEcu_ApplyBias: axle torque (bias blend) x brake pressure
    /* 0x0130 */ float duct_factor;                           // L1 decomp:re/decomp/1446dceb0.c per-wheel factor multiplied into BrakeDuct*CoeffLift/Drag (mean of the axle pair); meaning needs a trace
    /* 0x0138 */ uint8_t brake_locked_out;                    // L1 decomp:re/decomp/144676250.c non-zero = ApplyBias skips this wheel (failed/disabled brake?)
    /* 0x0188 */ float spin_rate;                             // L2 decomp:re/decomp/1446b7240.c;xref:0x1446df89a,0x1446df8ce,0x1446df901,0x1446df949 wheel angular velocity (abs taken), rad/s
    /* 0x03a8 */ float contact_vel_x;                         // L2 decomp:re/decomp/1446b7240.c;xref:0x1446df89a,0x1446df8ce,0x1446df901,0x1446df949 contact-patch velocity x; |(x,z)|/TyreRadius vs SpeedDownslopeRotRate
    /* 0x03ac */ float contact_vel_z;                         // L2 decomp:re/decomp/1446b7240.c;xref:0x1446df89a,0x1446df8ce,0x1446df901,0x1446df949 contact-patch velocity z
    /* 0x03b0 */ float slip_angle;                            // L1 decomp:re/decomp/1446e4330.c published by Car_Suspension_PublishWheelState
    /* 0x03b4 */ float slip_ratio;                            // L1 decomp:re/decomp/1446e4330.c <- solver +0x760
    /* 0x03c0 */ float lateral_force;                         // L1 decomp:re/decomp/1446e4330.c
    /* 0x03c4 */ float longitudinal_force;                    // L1 decomp:re/decomp/1446e4330.c <- solver +0x7e0
};
