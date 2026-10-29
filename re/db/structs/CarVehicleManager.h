// struct: CarVehicleManager
// subsystem: car
// size: ?  L2 decomp:re/decomp/142e90560.c
// notes: the per-frame vehicle system that Sess_Veh_UpdateWheelMap (0x142e90560) runs on. Its vehicle
// vector holds one per-vehicle object per car on track; per item: +0x20 = CarPhysicsRecord (the chassis
// runtime component), [+0x20]+0x58 = CarLive, CarLive+0xe8 = vehicle_id (<= 0x1f checked). The global
// dense table g_Sess_VehicleTable[vehicle_id] (0x146ce86e0) holds the same CarLive pointers; Car_Destroy_Live
// removes a car by moving the LAST car into the freed slot and rewriting its +0xe8, so vehicle_id is a
// slot index, stable only while no car is destroyed. Participant index -> vehicle_id: equal when the grid's
// cars are created in participant order at session load (needs trace T-file filed by car-a).
struct CarVehicleManager {
    /* 0x0110 */ void **vehicles_begin;   // L2 decomp:re/decomp/142e90560.c;xref:0x142e905e4,0x142e9084d per-vehicle objects (stride 8); item+0x20 = CarPhysicsRecord
    /* 0x0118 */ void **vehicles_end;     // L2 decomp:re/decomp/142e90560.c;xref:0x142e905eb,0x142e90854 end
    /* 0x0148 */ uint8_t wheel_map[0x18]; // L2 decomp:re/decomp/142e90560.c;xref:0x142e90864 robin_hood map<u8 vehicle_id, float> of the 4-wheel average (keyvals +0x160); 26th insert rehashes 32->64
};
