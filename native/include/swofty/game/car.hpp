#pragma once
#include <swofty/game/base.hpp>

namespace swofty::game {

struct CarBrakeEcu {
    uint8_t _pad_0x0[0x21d8];
    float brake_bias; // L3 stable
    float brake_pressure; // L3 stable
};
static_assert(offsetof(CarBrakeEcu, brake_bias) == 0x21d8);
static_assert(offsetof(CarBrakeEcu, brake_pressure) == 0x21dc);

struct CarChassis {
    uint8_t _pad_0x0[0x150];
    float mass; // L3 stable
};
static_assert(offsetof(CarChassis, mass) == 0x150);

struct CarDamageFrontWing {
    uint8_t _pad_0x0[0xa8];
    void *parts_allocator; // L2 experimental
    void **parts_begin; // L2 experimental
    void **parts_end; // L2 experimental
    uint64_t parts_capacity; // L2 experimental
};
static_assert(offsetof(CarDamageFrontWing, parts_allocator) == 0xa8);
static_assert(offsetof(CarDamageFrontWing, parts_begin) == 0xb0);
static_assert(offsetof(CarDamageFrontWing, parts_end) == 0xb8);
static_assert(offsetof(CarDamageFrontWing, parts_capacity) == 0xc0);

struct CarObfFloat {
};

struct CarEngine {
    uint8_t _pad_0x0[0x2528];
    float limiter_rotation_rate; // L3 stable
    uint8_t _pad_0x252c[0xc];
    float rotation_rate; // L3 stable
    uint8_t _pad_0x253c[0x38];
    float throttle; // L3 stable
};
static_assert(offsetof(CarEngine, limiter_rotation_rate) == 0x2528);
static_assert(offsetof(CarEngine, rotation_rate) == 0x2538);
static_assert(offsetof(CarEngine, throttle) == 0x2574);

struct CarFuelEcu {
    uint8_t _pad_0x0[0x128];
    int32_t fuel_mode; // L3 stable
};
static_assert(offsetof(CarFuelEcu, fuel_mode) == 0x128);

struct CarFuelTank {
    uint8_t _pad_0x0[0x100];
    float fuel_mass; // L3 stable
    uint8_t _pad_0x104[0x4];
    float fuel_flow; // L3 stable
};
static_assert(offsetof(CarFuelTank, fuel_mass) == 0x100);
static_assert(offsetof(CarFuelTank, fuel_flow) == 0x108);

struct CarGearBox {
    uint8_t _pad_0x0[0x680];
    uint32_t gear_index; // L3 stable
    uint32_t target_gear_index; // L2 experimental
};
static_assert(offsetof(CarGearBox, gear_index) == 0x680);
static_assert(offsetof(CarGearBox, target_gear_index) == 0x684);

struct CarLive {
    char *teleport_tag; // L2 experimental
    int32_t teleport_priority; // L2 experimental
    uint8_t _pad_0xc[0x4];
    float teleport_prev_mtx[16]; // L2 experimental
    float teleport_new_mtx[16]; // L2 experimental
    float teleport_dt; // L2 experimental
    uint32_t teleport_flags; // L2 experimental
    uint8_t _pad_0x98[0x50];
    uint8_t vehicle_id; // L3 stable
    uint8_t _pad_0xe9[0x2af];
    uint32_t system_count; // L3 stable
    uint8_t _pad_0x39c[0x194];
    void *engine; // L3 stable
    void *gearbox; // L3 stable
    void *fuel_tank; // L3 stable
    void *fuel_ecu; // L3 stable
    void *brake_ecu; // L3 stable
    void *chassis; // L3 stable
    void *steering; // L3 stable
    void *core_aero; // L3 stable
    void *f1_aero; // L3 stable
    void *differential; // L3 stable
    void *suspension; // L3 stable
    uint8_t _pad_0x588[0x8];
    void *f1_tuning; // L3 stable
    void *f1_assists; // L3 stable
    void *turbo; // L3 stable
    void *ers; // L3 stable
    uint8_t _pad_0x5b0[0x9990];
    uint32_t handling_version; // L3 stable
};
static_assert(offsetof(CarLive, teleport_tag) == 0x0);
static_assert(offsetof(CarLive, teleport_priority) == 0x8);
static_assert(offsetof(CarLive, teleport_prev_mtx) == 0x10);
static_assert(offsetof(CarLive, teleport_new_mtx) == 0x50);
static_assert(offsetof(CarLive, teleport_dt) == 0x90);
static_assert(offsetof(CarLive, teleport_flags) == 0x94);
static_assert(offsetof(CarLive, vehicle_id) == 0xe8);
static_assert(offsetof(CarLive, system_count) == 0x398);
static_assert(offsetof(CarLive, engine) == 0x530);
static_assert(offsetof(CarLive, gearbox) == 0x538);
static_assert(offsetof(CarLive, fuel_tank) == 0x540);
static_assert(offsetof(CarLive, fuel_ecu) == 0x548);
static_assert(offsetof(CarLive, brake_ecu) == 0x550);
static_assert(offsetof(CarLive, chassis) == 0x558);
static_assert(offsetof(CarLive, steering) == 0x560);
static_assert(offsetof(CarLive, core_aero) == 0x568);
static_assert(offsetof(CarLive, f1_aero) == 0x570);
static_assert(offsetof(CarLive, differential) == 0x578);
static_assert(offsetof(CarLive, suspension) == 0x580);
static_assert(offsetof(CarLive, f1_tuning) == 0x590);
static_assert(offsetof(CarLive, f1_assists) == 0x598);
static_assert(offsetof(CarLive, turbo) == 0x5a0);
static_assert(offsetof(CarLive, ers) == 0x5a8);
static_assert(offsetof(CarLive, handling_version) == 0x9f40);

inline constexpr address<"Car_Assets_CreateSystemForPerformanceMode"> Car_Assets_CreateSystemForPerformanceMode{}; // L3 stable
inline constexpr address<"Car_Build_VtfComponents"> Car_Build_VtfComponents{}; // L2 experimental
inline constexpr address<"Car_Chassis_Build"> Car_Chassis_Build{}; // L3 stable
using Car_Create_DetachablePart_t = void *(void *part_manager, void **cb, uint32_t *name_id, int32_t *params, float k);
inline constexpr fn<"Car_Create_DetachablePart", Car_Create_DetachablePart_t> Car_Create_DetachablePart{}; // L2 experimental
using Car_FrontWing_Init_t = void(void *front_wing, char reset);
inline constexpr fn<"Car_FrontWing_Init", Car_FrontWing_Init_t> Car_FrontWing_Init{}; // L2 experimental, game::FrontWing
inline constexpr address<"Car_FrontWing_OnCollision"> Car_FrontWing_OnCollision{}; // L2 experimental
inline constexpr address<"Car_Load_Vtf"> Car_Load_Vtf{}; // L2 experimental
inline constexpr address<"Car_Physics_PostVehicleUpdate"> Car_Physics_PostVehicleUpdate{}; // L2 experimental
inline constexpr address<"Car_Read_Engine"> Car_Read_Engine{}; // L2 experimental
inline constexpr address<"Car_Read_F1Aero"> Car_Read_F1Aero{}; // L2 experimental
inline constexpr address<"Car_Read_F1Assists"> Car_Read_F1Assists{}; // L2 experimental
inline constexpr address<"Car_Read_F1Tuning"> Car_Read_F1Tuning{}; // L2 experimental
inline constexpr address<"Car_Read_GearBox"> Car_Read_GearBox{}; // L2 experimental
using Car_Register_DetachablePart_Thunk_t = void(void *front_wing, void **cb, uint32_t *name_id, void *params, float k);
inline constexpr fn<"Car_Register_DetachablePart_Thunk", Car_Register_DetachablePart_Thunk_t> Car_Register_DetachablePart_Thunk{}; // L2 experimental
using Car_Reset_LastFittedUpgrade_t = void(void *inst);
inline constexpr fn<"Car_Reset_LastFittedUpgrade", Car_Reset_LastFittedUpgrade_t> Car_Reset_LastFittedUpgrade{}; // L2 experimental
inline constexpr address<"Car_Setup_ApplyTuning"> Car_Setup_ApplyTuning{}; // L3 stable
inline constexpr address<"Car_Tuning_SetChassisZCom"> Car_Tuning_SetChassisZCom{}; // L2 experimental
inline constexpr address<"Car_Tyre_GripScale"> Car_Tyre_GripScale{}; // L2 experimental
inline constexpr address<"Car_Tyre_MagicFormula"> Car_Tyre_MagicFormula{}; // L2 experimental
inline constexpr address<"Car_Tyre_UpdateWear"> Car_Tyre_UpdateWear{}; // L2 experimental
inline constexpr address<"Car_Upgrades_ApplyToVehicle"> Car_Upgrades_ApplyToVehicle{}; // L3 experimental
inline constexpr address<"Car_Upgrades_CalcNonLinearMass"> Car_Upgrades_CalcNonLinearMass{}; // L2 experimental
inline constexpr address<"Car_Write_Differential"> Car_Write_Differential{}; // L2 experimental
inline constexpr address<"Car_Write_FuelTank"> Car_Write_FuelTank{}; // L2 experimental
inline constexpr address<"Car_Write_GearBox"> Car_Write_GearBox{}; // L2 experimental
inline constexpr address<"Car_Write_Turbo"> Car_Write_Turbo{}; // L2 experimental
inline constexpr site<"site_Car_AeroStoreRearDownforce"> site_Car_AeroStoreRearDownforce{}; // L2 experimental
inline constexpr site<"site_Car_AeroStoreTotalDrag"> site_Car_AeroStoreTotalDrag{}; // L2 experimental
inline constexpr site<"site_Car_AeroStoreTotalLift"> site_Car_AeroStoreTotalLift{}; // L2 experimental
inline constexpr site<"site_Car_EngineDriveTorque"> site_Car_EngineDriveTorque{}; // L2 experimental
inline constexpr site<"site_Car_ErsGearModifierLoad"> site_Car_ErsGearModifierLoad{}; // L2 experimental
inline constexpr site<"site_Car_FuelFlowStore"> site_Car_FuelFlowStore{}; // L2 experimental
inline constexpr site<"site_Car_FuelMassStore"> site_Car_FuelMassStore{}; // L2 experimental
inline constexpr site<"site_Car_LiveCarBuilt"> site_Car_LiveCarBuilt{}; // L3 stable
inline constexpr site<"site_Car_RegisterVehicleId"> site_Car_RegisterVehicleId{}; // L3 experimental
inline constexpr site<"site_Car_SetupTuningApplied"> site_Car_SetupTuningApplied{}; // L3 stable
inline constexpr site<"site_Car_UpgradeSectionApplyCall"> site_Car_UpgradeSectionApplyCall{}; // L2 experimental
inline constexpr site<"site_Car_VehicleTableRemove"> site_Car_VehicleTableRemove{}; // L3 stable

}
