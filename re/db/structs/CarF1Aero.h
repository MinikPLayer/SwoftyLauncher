// struct: CarF1Aero
// subsystem: car
// size: 0x1a08  L2 decomp:re/decomp/1555e9310.c (alloc 0x1a08 in Car_Create_F1AeroSystem 0x1555e9310)
// notes: live F1Aero system at CarLive+0x570 (offsets from the system pointer; CarComponentBase head). Data fields are the
// embedded data object (+0x18) read by Car_Read_F1Aero 0x1446c8560 at data offset = field - 0x18; every number is a
// CarObfFloat (value = a ^ b, key per instance from the car RNG). Runtime outputs are plain floats from +0x1960.
// Car_Physics_StepVehicle 0x1446dceb0 computes them each step. See subsystems/car.md#f1aero.
// Runtime 2026-10-28 (T115, gdb snapshots of a Grand Prix): +0x19f0 / +0x19ec are the front / rear body lift
// COEFFICIENTS (0.72 / 0.96 parked, 0.79 / 1.11 at speed, front share about 0.43), not forces, so they are named
// front_lift_coeff / rear_lift_coeff (were front_downforce / rear_downforce). +0x19e8 total_drag is the drag FORCE in N
// (-2402 at speed, about 0 parked); +0x19e4 total_lift read about 0 parked and -7720 at speed.
struct CarF1Aero {
    /* 0x0000 */ void *vtable;                                // L2 decomp:re/decomp/1555e9310.c;kb:subsystems/car.md#layout system vtable 0x145bfca70
    /* 0x0008 */ CarLive *car;                                // L2 decomp:re/decomp/1555e9310.c;kb:subsystems/car.md#layout owning car (factory argument); StepVehicle reads [sys+8]+0x568/+0x580
    /* 0x0010 */ void *allocator;                             // L2 decomp:re/decomp/1555e9310.c;kb:subsystems/car.md#layout car allocator [car+0xd0]
    /* 0x0018 */ void *data_vtable;                           // L2 decomp:re/decomp/1555e9310.c;kb:subsystems/car.md#layout embedded data object vtable 0x145bfc370 {dtor, GetName, Car_Read_<X>, Car_Write_<X>}
    /* 0x0028 */ CarObfFloat aero_front_cl_maxf_maxr;         // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/m_aero_front_cl_maxf_maxr
    /* 0x0038 */ CarObfFloat aero_front_cl_maxf_minr;         // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/m_aero_front_cl_maxf_minr
    /* 0x0048 */ CarObfFloat aero_front_cl_minf_maxr;         // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/m_aero_front_cl_minf_maxr
    /* 0x0058 */ CarObfFloat aero_front_cl_minf_minr;         // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/m_aero_front_cl_minf_minr
    /* 0x0068 */ CarObfFloat aero_front_wing_loss;            // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/m_aero_front_wing_loss
    /* 0x0078 */ CarObfFloat aero_reference_plane_height;     // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/AeroReferencePlaneHeight
    /* 0x0088 */ CarObfFloat aero_reference_offset_front_z;   // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/AeroReferenceOffsetFrontZ
    /* 0x0098 */ CarObfFloat aero_reference_offset_rear_z;    // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/AeroReferenceOffsetRearZ
    /* 0x00a8 */ CarObfFloat aero_max_front_height;           // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/m_aero_max_front_height; front ride-height clamp
    /* 0x00b8 */ CarObfFloat aero_min_front_height;           // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/m_aero_min_front_height; front ride-height clamp
    /* 0x00c8 */ CarObfFloat aero_max_rear_height;            // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/m_aero_max_rear_height; rear ride-height clamp
    /* 0x00d8 */ CarObfFloat aero_min_rear_height;            // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/m_aero_min_rear_height; rear ride-height clamp for the lift curves
    /* 0x00e8 */ CarObfFloat aero_slip_stream_air_density_scale_front_wing; // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/m_aeroSlipStreamAirDensityScaleFrontWing
    /* 0x00f8 */ CarObfFloat aero_slip_stream_air_density_scale_rear_wing; // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/m_aeroSlipStreamAirDensityScaleRearWing
    /* 0x0108 */ CarObfFloat aero_slip_stream_air_density_scale_lift; // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/m_aeroSlipStreamAirDensityScaleLift; lift x (k + (1-k)*core_aero.slipstream_density)
    /* 0x0118 */ CarObfFloat aero_slip_stream_air_density_scale_drag; // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/m_aeroSlipStreamAirDensityScaleDrag
    /* 0x0128 */ CarObfFloat aero_slip_stream_car_width;      // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/m_aeroSlipStreamCarWidth
    /* 0x0138 */ CarObfFloat aero_rear_cl_maxf_maxr;          // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/m_aero_rear_cl_maxf_maxr
    /* 0x0148 */ CarObfFloat aero_rear_cl_maxf_minr;          // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/m_aero_rear_cl_maxf_minr
    /* 0x0158 */ CarObfFloat aero_rear_cl_minf_maxr;          // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/m_aero_rear_cl_minf_maxr
    /* 0x0168 */ CarObfFloat aero_rear_cl_minf_minr;          // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/m_aero_rear_cl_minf_minr
    /* 0x0178 */ CarObfFloat aero_rear_wing_loss;             // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/m_aero_rear_wing_loss
    /* 0x0188 */ CarObfFloat aero_wing_front_drag;            // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/m_aero_wing_front_drag
    /* 0x0198 */ CarObfFloat aero_wing_front_force;           // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/m_aero_wing_front_force; front wing force x ((this-1)*wing_open+1)
    /* 0x01a8 */ CarObfFloat aero_wing_rear_drag;             // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/m_aero_wing_rear_drag; rear wing drag x ((this-1)*wing_open+1); radUpgradeDRS multiplies it
    /* 0x01b8 */ CarObfFloat aero_wing_rear_force;            // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/m_aero_wing_rear_force; rear wing force x ((this-1)*wing_open+1)
    /* 0x01c8 */ CarSpline force_velocity_multiplier;         // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/ForceVelocityMultiplier spline (0x2a8 bytes)
    /* 0x0470 */ CarSpline rear_coeff_lift_vs_ride_height;    // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/RearCoeffLiftVsRideHeight spline (0x2a8 bytes) (fallback key RearForceRideHeightMultiplier)
    /* 0x0718 */ CarSpline rear_coeff_lift_vs_chassis_rake;   // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/RearCoeffLiftVsChassisRake spline (0x2a8 bytes)
    /* 0x09c0 */ CarSpline front_coeff_lift_vs_ride_height;   // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/FrontCoeffLiftVsRideHeight spline (0x2a8 bytes) (fallback key FrontForceRideHeightMultiplier)
    /* 0x0c68 */ CarSpline front_coeff_lift_vs_chassis_rake;  // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/FrontCoeffLiftVsChassisRake spline (0x2a8 bytes)
    /* 0x0f10 */ CarSpline rear_force_direction_multiplier;   // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/RearForceDirectionMultiplier spline (0x2a8 bytes)
    /* 0x11b8 */ CarSpline front_force_direction_multiplier;  // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/FrontForceDirectionMultiplier spline (0x2a8 bytes)
    /* 0x1460 */ CarSpline distance_driven_cumulative_drsfailure_chance; // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/DistanceDrivenCumulativeDRSFailureChance spline (0x2a8 bytes)
    /* 0x1708 */ CarObfFloat front_coeff_lift_multiplier;     // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/FrontCoeffLiftMultiplier; StepVehicle front body lift (xref 0x1446ddda4 store)
    /* 0x1718 */ CarObfFloat rear_coeff_lift_multiplier;      // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/RearCoeffLiftMultiplier; StepVehicle rear body lift (xref 0x1446dda91 store)
    /* 0x1728 */ CarObfFloat body_drag_coeff_min;             // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/BodyDragCoeffMin (alias key m_aero_cd_min)
    /* 0x1738 */ CarObfFloat body_drag_coeff_max;             // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/BodyDragCoeffMax (alias key m_aero_cd_max)
    /* 0x1748 */ CarObfFloat num_front_wing_settings;         // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/NumFrontWingSettings (u32, Car_Assets_FindUInt)
    /* 0x1758 */ CarObfFloat front_wing_co_l0;                // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/FrontWingCoL0
    /* 0x1768 */ CarObfFloat front_wing_co_l1;                // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/FrontWingCoL1
    /* 0x1778 */ CarObfFloat front_wing_co_l2;                // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/FrontWingCoL2
    /* 0x1788 */ CarObfFloat front_wing_co_d0;                // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/FrontWingCoD0
    /* 0x1798 */ CarObfFloat front_wing_co_d1;                // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/FrontWingCoD1
    /* 0x17a8 */ CarObfFloat front_wing_co_d2;                // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/FrontWingCoD2
    /* 0x17b8 */ CarObfFloat num_rear_wing_settings;          // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/NumRearWingSettings (u32, Car_Assets_FindUInt)
    /* 0x17c8 */ CarObfFloat rear_wing_co_l0;                 // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/RearWingCoL0
    /* 0x17d8 */ CarObfFloat rear_wing_co_l1;                 // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/RearWingCoL1
    /* 0x17e8 */ CarObfFloat rear_wing_co_l2;                 // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/RearWingCoL2
    /* 0x17f8 */ CarObfFloat rear_wing_co_d0;                 // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/RearWingCoD0
    /* 0x1808 */ CarObfFloat rear_wing_co_d1;                 // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/RearWingCoD1
    /* 0x1818 */ CarObfFloat rear_wing_co_d2;                 // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/RearWingCoD2
    /* 0x1828 */ CarObfFloat front_co_lby_wing_damage_mult;   // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/FrontCoLByWingDamageMult; x front_wing_damage (+0x199c) subtracts from front lift
    /* 0x1838 */ CarObfFloat body_co_dby_wing_damage_mult;    // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/BodyCoDByWingDamageMult; drag x (1 + front_wing_damage*this)
    /* 0x1848 */ CarObfFloat drsrotation_angle_radians;       // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/DRSRotationAngleRadians
    /* 0x1858 */ CarObfFloat drsrotation_joint_angle_ratio;   // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/DRSRotationJointAngleRatio
    /* 0x1868 */ CarObfFloat drsopen_close_speed;             // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/DRSOpenCloseSpeed
    /* 0x1878 */ CarObfFloat drsfail_time_min;                // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/DRSFailTimeMin
    /* 0x1888 */ CarObfFloat drsfail_time_max;                // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/DRSFailTimeMax
    /* 0x1898 */ CarObfFloat front_wing_rotation_angle_radians; // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/FrontWingRotationAngleRadians
    /* 0x18a8 */ CarObfFloat centre_of_drag_yoffset;          // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/CentreOfDragYOffset
    /* 0x18b8 */ CarObfFloat centre_of_drag_zoffset;          // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/CentreOfDragZOffset
    /* 0x18c8 */ CarObfFloat drag_coefficient_side;           // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/DragCoefficientSide
    /* 0x18d8 */ CarObfFloat drag_coefficient_top;            // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/DragCoefficientTop
    /* 0x18e8 */ CarObfFloat brake_duct_front_coeff_lift;     // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/BrakeDuctFrontCoeffLift; x mean(FL,FR wheel+0x130) into front lift
    /* 0x18f8 */ CarObfFloat brake_duct_rear_coeff_lift;      // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/BrakeDuctRearCoeffLift; x mean(RL,RR wheel+0x130) into rear lift
    /* 0x1908 */ CarObfFloat brake_duct_front_coeff_drag;     // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/BrakeDuctFrontCoeffDrag
    /* 0x1918 */ CarObfFloat brake_duct_rear_coeff_drag;      // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/BrakeDuctRearCoeffDrag
    /* 0x1928 */ CarObfFloat radiator_opening_front_coeff_lift; // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/RadiatorOpeningFrontCoeffLift; into front lift
    /* 0x1938 */ CarObfFloat radiator_opening_rear_coeff_lift; // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/RadiatorOpeningRearCoeffLift; into rear lift
    /* 0x1948 */ CarObfFloat radiator_opening_coeff_drag;     // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/RadiatorOpeningCoeffDrag
    /* 0x1958 */ uint8_t adjustable_front_wing;               // L2 decomp:re/decomp/1446c8560.c;kb:subsystems/car.md#f1aero VTF F1Aero/AdjustableFrontWing bool (0x14464c220)
    /* 0x1960 */ float body_cd_min;                           // L2 decomp:re/decomp/1446c9680.c;xref:0x145bfca88 decoded BodyDragCoeffMin, copied by OnReset (flag 0x80); drag = lerp(min,max,ride-height ratio) + ducts + radiator
    /* 0x1964 */ float body_cd_max;                           // L2 decomp:re/decomp/1446c9680.c;xref:0x145bfca88 decoded BodyDragCoeffMax (OnReset flag 0x80)
    /* 0x1968 */ float front_cl_map[4];                       // L2 decomp:re/decomp/1446c9680.c;xref:0x145bfca88 decoded m_aero_front_cl_maxf_maxr/maxf_minr/minf_maxr/minf_minr (OnReset flag 0x80)
    /* 0x1978 */ float rear_cl_map[4];                        // L2 decomp:re/decomp/1446c9680.c;xref:0x145bfca88 decoded m_aero_rear_cl_* (OnReset flag 0x80)
    /* 0x1988 */ uint8_t drs_flags;                           // L1 decomp:re/decomp/1446c9680.c;xref:0x145bfca88 low 2 bits cleared on OnReset flag 0x200
    /* 0x198c */ float drs_fail_distance;                     // L1 decomp:re/decomp/1446c9680.c;xref:0x145bfca88 OnReset flag 0x200: -1 when DRSRotationJointAngleRatio is 0, else 0x1446c9930(sys)
    /* 0x1998 */ float rear_wing_damage;                      // L2 decomp:re/decomp/1446dceb0.c;xref:0x1446ddeae 0..1; rear wing force x (1 - this); written by Car_Damage_SetComponentDamage
    /* 0x199c */ float front_wing_damage;                     // L2 decomp:re/decomp/1446dceb0.c;xref:0x1446ddd98,0x14466029e 0..1; front wing force x (1 - this), front lift - FrontCoLByWingDamageMult x this; written by Car_FrontWing_OnCollision
    /* 0x19a0 */ float drag_scale;                            // L3 decomp:re/decomp/1555e9310.c;xref:0x1446ddffc;trace:T115/2026-10-28-gp:12 ctor 1.0, reset to 1.0 by OnReset flag 0x1000 (with both wing damages = 0); multiplies the whole body+duct+radiator drag coefficient Live: 1.0.
    /* 0x19a4 */ float lift_scale;                            // L3 decomp:re/decomp/1555e9310.c;xref:0x1446dda8d,0x1446ddd82;trace:T115/2026-10-28-gp:12 ctor 1.0; multiplies front and rear body lift (both stores) Live: 1.0.
    /* 0x19a8 */ float wing_open_fraction;                    // L1 decomp:re/decomp/1446ca5f0.c;xref:0x1446ca5f0 0..1 DRS / active-wing open fraction: blends m_aero_wing_*_force/_drag; trace T needed
    /* 0x19ac */ uint8_t rear_wing_fixed;                     // L1 decomp:re/decomp/1446ca5f0.c non-zero = rear wing ignores wing_open_fraction (front still blends)
    /* 0x19b4 */ uint32_t front_wing_setting;                 // L2 decomp:re/decomp/1446c9680.c;xref:0x145bfca88 integer wing angle s; front_wing_lift = FrontWingCoL0 + CoL1*s + (CoL2*s)^2 (OnReset flag 0x2200)
    /* 0x19b8 */ float front_wing_lift;                       // L2 decomp:re/decomp/1446c9680.c;xref:0x145bfca88 FrontWingCoL0 + CoL1*s + (CoL2*s)^2, s = front_wing_setting; x (1-front_wing_damage) x ((m_aero_wing_front_force-1)*wing_open+1)
    /* 0x19bc */ float front_wing_drag;                       // L2 decomp:re/decomp/1446c9680.c;xref:0x145bfca88 FrontWingCoD0 + CoD1*s + (CoD2*s)^2
    /* 0x19c0 */ uint32_t rear_wing_setting;                  // L2 decomp:re/decomp/1446c9680.c;xref:0x145bfca88 integer rear wing angle s, same formula with RearWingCo*
    /* 0x19c4 */ float rear_wing_lift;                        // L2 decomp:re/decomp/1446c9680.c;xref:0x145bfca88 RearWingCoL0 + CoL1*s + (CoL2*s)^2; x (1-rear_wing_damage)
    /* 0x19c8 */ float rear_wing_drag;                        // L2 decomp:re/decomp/1446c9680.c;xref:0x145bfca88 RearWingCoD0 + CoD1*s + (CoD2*s)^2
    /* 0x19d8 */ float drag_force;                            // L1 decomp:re/decomp/1446dceb0.c;xref:0x1446de3cf drag force magnitude (older note); stored at 0x1446de3cf
    /* 0x19e4 */ float total_lift;                            // L2 decomp:re/decomp/1446dceb0.c;xref:0x1446dda91,0x1446ddda4,0x1446ddf0d,0x1446de114 body front + rear + wing terms x air density x speed^2 (stored 0x1446ddf0d)
    /* 0x19e8 */ float total_drag;                            // L3 decomp:re/decomp/1446dceb0.c;xref:0x1446dda91,0x1446ddda4,0x1446ddf0d,0x1446de114;trace:T115/2026-10-28-gp:11 the total drag FORCE (N, sign = along the car's axis): drag coefficient x density x speed^2, one writer (0x1446de114), no reader found (the +0x19e8 reads at 0x142948f81/0x14296dbe2 are another object). Live: about 0 parked, -2402 N at speed for the player car.
    /* 0x19ec */ float rear_lift_coeff;                       // L3 decomp:re/decomp/1446dceb0.c;xref:0x1446dda91,0x1446ddda4,0x1446ddf0d,0x1446de114;trace:T115/2026-10-28-gp:11 rear BODY lift COEFFICIENT, not a force: (BrakeDuctRearCoeffLift x mean RL/RR wheel+0x130 + RadiatorOpeningRearCoeffLift + curves) x RearCoeffLiftMultiplier x lift_scale (stored 0x1446dda91). Live: 0.96 parked, 1.11 at speed (renamed from rear_downforce after T115). Older inbox note had front/rear swapped
    /* 0x19f0 */ float front_lift_coeff;                      // L3 decomp:re/decomp/1446dceb0.c;xref:0x1446dda91,0x1446ddda4,0x1446ddf0d,0x1446de114;trace:T115/2026-10-28-gp:11 front BODY lift COEFFICIENT, not a force, same with Front* keys x (1 - FrontCoLByWingDamageMult x front_wing_damage) (stored 0x1446ddda4). Live: 0.72 parked, 0.79 at speed, about 0.43 of front + rear (renamed from front_downforce after T115)
};
