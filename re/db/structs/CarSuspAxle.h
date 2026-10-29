// struct: CarSuspAxle
// subsystem: car
// size: 0x1670  L2 decomp:re/decomp/144753050.c (alloc 0x1670 per axle)
// notes: axle object at CarSuspension+0x90/+0x98. Setup (Car_Tuning_SetSuspension 0x1446a7be0, F1Tuning sliders lerped over
// the F1Tuning ranges) writes camber/toe/ARB here, separate from the VTF Suspension record values.
struct CarSuspAxle {
    /* 0x1598 */ float local_rest_position_z;                 // L2 decomp:re/decomp/1446a7be0.c;xref:0x15543f4d2,0x15543f4e0 sign selects rear (<= 0) vs front for the setup writes
    /* 0x15e4 */ float ride_height_offset;                    // L1 decomp:re/decomp/1446dceb0.c StepVehicle adds it to the axle wheel heights (aero ride height)
    /* 0x1650 */ float setup_camber;                          // L2 decomp:re/decomp/1446a7be0.c;xref:0x15543f4d2,0x15543f4e0 live camber from the setup slider ("Setting front/rear camber to %f.")
    /* 0x1654 */ float setup_toe;                             // L2 decomp:re/decomp/1446a7be0.c;xref:0x15543f4d2,0x15543f4e0 live toe ("Setting front/rear toe")
    /* 0x1664 */ float setup_anti_roll;                       // L2 decomp:re/decomp/1446a7be0.c;xref:0x15543f4d2,0x15543f4e0 live anti-roll bar constant ("Setting front/rear suspension's anti-roll bar constant")
};
