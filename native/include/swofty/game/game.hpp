#pragma once
#include <swofty/game/base.hpp>

namespace swofty::game {

struct ActivityDef {
    uint8_t _pad_0x0[0x10];
    uint8_t type; // L2 experimental
};
static_assert(offsetof(ActivityDef, type) == 0x10);

struct CalendarActivity {
    uint8_t _pad_0x0[0xa];
    uint16_t day; // L2 experimental
    uint8_t _pad_0xc[0x4];
    ActivityDef *activity; // L2 experimental
    uint8_t _pad_0x18[0x8];
};
static_assert(offsetof(CalendarActivity, day) == 0xa);
static_assert(offsetof(CalendarActivity, activity) == 0x10);
static_assert(sizeof(CalendarActivity) == 0x20);

struct ChampionshipHolder {
    uint8_t _pad_0x0[0x260];
    CareerEntity *entity; // L2 experimental
};
static_assert(offsetof(ChampionshipHolder, entity) == 0x260);

struct DriverCalendar {
    uint8_t _pad_0x0[0x50];
    void *activities_begin; // L2 experimental
    void *activities_end; // L2 experimental
    uint8_t _pad_0x60[0x60];
    uint32_t driver; // L2 experimental
    uint8_t _pad_0xc4[0x4];
};
static_assert(offsetof(DriverCalendar, activities_begin) == 0x50);
static_assert(offsetof(DriverCalendar, activities_end) == 0x58);
static_assert(offsetof(DriverCalendar, driver) == 0xc0);
static_assert(sizeof(DriverCalendar) == 0xc8);

struct DriverSecretMeetings {
    uint8_t _pad_0x0[0x10];
    void *current_allocator; // L2 experimental
    SecretMeeting *current; // L2 experimental
    uint8_t _pad_0x20[0x8];
    void *previous_begin; // L2 experimental
    void *previous_end; // L2 experimental
    uint8_t _pad_0x38[0x8];
    uint32_t driver; // L2 experimental
    uint8_t _pad_0x44[0x4];
};
static_assert(offsetof(DriverSecretMeetings, current_allocator) == 0x10);
static_assert(offsetof(DriverSecretMeetings, current) == 0x18);
static_assert(offsetof(DriverSecretMeetings, previous_begin) == 0x28);
static_assert(offsetof(DriverSecretMeetings, previous_end) == 0x30);
static_assert(offsetof(DriverSecretMeetings, driver) == 0x40);
static_assert(sizeof(DriverSecretMeetings) == 0x48);

struct GameManager {
    uint8_t _pad_0x0[0xc0];
    GameModeContext *mode_context; // L2 experimental
};
static_assert(offsetof(GameManager, mode_context) == 0xc0);

struct GameMessageBusHolder {
    uint8_t _pad_0x0[0x8];
    NeMessageBus *bus; // L2 experimental
};
static_assert(offsetof(GameMessageBusHolder, bus) == 0x8);

struct GameModeContext {
    uint8_t _pad_0x0[0x88];
    ChampionshipHolder *championship_holder; // L2 experimental
};
static_assert(offsetof(GameModeContext, championship_holder) == 0x88);

struct ScheduleComponent {
    uint8_t _pad_0x0[0x410];
    ScheduleSeason *seasons_begin; // L2 experimental
    ScheduleSeason *seasons_end; // L2 experimental
};
static_assert(offsetof(ScheduleComponent, seasons_begin) == 0x410);
static_assert(offsetof(ScheduleComponent, seasons_end) == 0x418);

struct ScheduleSeason {
    uint8_t _pad_0x0[0x40];
    DriverCalendar *driver_calendars_begin; // L2 experimental
    DriverCalendar *driver_calendars_end; // L2 experimental
    uint8_t _pad_0x50[0x30];
};
static_assert(offsetof(ScheduleSeason, driver_calendars_begin) == 0x40);
static_assert(offsetof(ScheduleSeason, driver_calendars_end) == 0x48);
static_assert(sizeof(ScheduleSeason) == 0x80);

struct SecretMeeting {
    uint32_t id; // L2 experimental
    uint8_t _pad_0x4[0x10];
    uint8_t reason; // L2 experimental
    uint8_t _pad_0x15[0xb];
    uint32_t team; // L2 experimental
};
static_assert(offsetof(SecretMeeting, id) == 0x0);
static_assert(offsetof(SecretMeeting, reason) == 0x14);
static_assert(offsetof(SecretMeeting, team) == 0x20);
static_assert(sizeof(SecretMeeting) == 0x24);

struct TrackDataRecord {
    uint8_t _pad_0x0[0x1];
    uint8_t index; // L2 experimental
    uint8_t _pad_0x2[0xce];
    char *name; // L2 experimental
};
static_assert(offsetof(TrackDataRecord, index) == 0x1);
static_assert(offsetof(TrackDataRecord, name) == 0xd0);

struct TrackDataSet {
    uint8_t _pad_0x0[0x8];
    void *tracks_begin; // L2 experimental
    void *tracks_end; // L2 experimental
    uint8_t _pad_0x18[0x8];
    uint32_t mode_mask; // L2 experimental
    uint32_t season_mask; // L2 experimental
    uint32_t format_mask; // L2 experimental
};
static_assert(offsetof(TrackDataSet, tracks_begin) == 0x8);
static_assert(offsetof(TrackDataSet, tracks_end) == 0x10);
static_assert(offsetof(TrackDataSet, mode_mask) == 0x20);
static_assert(offsetof(TrackDataSet, season_mask) == 0x24);
static_assert(offsetof(TrackDataSet, format_mask) == 0x28);

struct TrackDataSystem {
    uint8_t _pad_0x0[0x68];
    TrackDataSet *sets_begin; // L2 experimental
    TrackDataSet *sets_end; // L2 experimental
};
static_assert(offsetof(TrackDataSystem, sets_begin) == 0x68);
static_assert(offsetof(TrackDataSystem, sets_end) == 0x70);

struct TrackDataTrack {
    TrackDataRecord *record; // L2 experimental
    uint8_t _pad_0x8[0xe8];
    float fuel_burn_per_lap; // L2 experimental
    uint8_t _pad_0xf4[0xc];
    uint8_t race_fuel[156]; // L2 experimental
    uint8_t _pad_0x19c[0x4];
    uint64_t race_fuel_count; // L2 experimental
    uint8_t _pad_0x1a8[0xc];
    float qualifying_one_shot; // L2 experimental
    uint8_t _pad_0x1b8[0x4];
    float qualifying_short; // L2 experimental
    float qualifying_full; // L2 experimental
};
static_assert(offsetof(TrackDataTrack, record) == 0x0);
static_assert(offsetof(TrackDataTrack, fuel_burn_per_lap) == 0xf0);
static_assert(offsetof(TrackDataTrack, race_fuel) == 0x100);
static_assert(offsetof(TrackDataTrack, race_fuel_count) == 0x1a0);
static_assert(offsetof(TrackDataTrack, qualifying_one_shot) == 0x1b4);
static_assert(offsetof(TrackDataTrack, qualifying_short) == 0x1bc);
static_assert(offsetof(TrackDataTrack, qualifying_full) == 0x1c0);

using Game_CareerActivities_AddActivity_t = void(void *calendar, const void *activity, uint32_t day);
inline constexpr fn<"Game_CareerActivities_AddActivity", Game_CareerActivities_AddActivity_t> Game_CareerActivities_AddActivity{}; // L2 experimental
using Game_CareerActivities_RemoveActivity_t = void(void *calendar, const void *activity, uint16_t day);
inline constexpr fn<"Game_CareerActivities_RemoveActivity", Game_CareerActivities_RemoveActivity_t> Game_CareerActivities_RemoveActivity{}; // L2 experimental
using Game_CareerSetup_ResetEntrants_t = void(void *setup);
inline constexpr fn<"Game_CareerSetup_ResetEntrants", Game_CareerSetup_ResetEntrants_t> Game_CareerSetup_ResetEntrants{}; // L2 experimental
using Game_CareerWizard_PushNextStep_t = void(void *wizard);
inline constexpr fn<"Game_CareerWizard_PushNextStep", Game_CareerWizard_PushNextStep_t> Game_CareerWizard_PushNextStep{}; // L2 experimental
using Game_CareerWizard_StartsInF1_t = bool(void *entrants, void *wizard_data, const char *entrant_list);
inline constexpr fn<"Game_CareerWizard_StartsInF1", Game_CareerWizard_StartsInF1_t> Game_CareerWizard_StartsInF1{}; // L2 experimental
using Game_Career_IsDriverCareerMode_t = bool(const void *championship);
inline constexpr fn<"Game_Career_IsDriverCareerMode", Game_Career_IsDriverCareerMode_t> Game_Career_IsDriverCareerMode{}; // L2 experimental
using Game_ContractTeamSelect_OnEnter_t = uint64_t(void *state);
inline constexpr fn<"Game_ContractTeamSelect_OnEnter", Game_ContractTeamSelect_OnEnter_t> Game_ContractTeamSelect_OnEnter{}; // L2 experimental
using Game_ContractTeamSelect_OnExit_t = uint64_t(void *state);
inline constexpr fn<"Game_ContractTeamSelect_OnExit", Game_ContractTeamSelect_OnExit_t> Game_ContractTeamSelect_OnExit{}; // L2 experimental
using Game_ContractTeamSelect_OnUpdate_t = uint32_t *(void *state, uint32_t *link);
inline constexpr fn<"Game_ContractTeamSelect_OnUpdate", Game_ContractTeamSelect_OnUpdate_t> Game_ContractTeamSelect_OnUpdate{}; // L2 experimental
using Game_CreateSessionDlcSelectTab_BuildTiles_t = void(void *view_model);
inline constexpr fn<"Game_CreateSessionDlcSelectTab_BuildTiles", Game_CreateSessionDlcSelectTab_BuildTiles_t> Game_CreateSessionDlcSelectTab_BuildTiles{}; // L2 experimental
using Game_CreateSessionDlcSelectTab_FillTile_t = void *(void *tile, void *node, const char *title, const char *command, const char *image);
inline constexpr fn<"Game_CreateSessionDlcSelectTab_FillTile", Game_CreateSessionDlcSelectTab_FillTile_t> Game_CreateSessionDlcSelectTab_FillTile{}; // L2 experimental
using Game_CreateSessionDlcSelectTab_HandleCommand_t = char *(void *view_model, void *command);
inline constexpr fn<"Game_CreateSessionDlcSelectTab_HandleCommand", Game_CreateSessionDlcSelectTab_HandleCommand_t> Game_CreateSessionDlcSelectTab_HandleCommand{}; // L2 experimental
using Game_DriverContracts_AdvanceSecretMeeting_t = void(void *contracts, uint32_t driver, uint8_t notify);
inline constexpr fn<"Game_DriverContracts_AdvanceSecretMeeting", Game_DriverContracts_AdvanceSecretMeeting_t> Game_DriverContracts_AdvanceSecretMeeting{}; // L2 experimental
using Game_DriverContracts_BuildOffers_t = void(void *contracts, uint32_t driver);
inline constexpr fn<"Game_DriverContracts_BuildOffers", Game_DriverContracts_BuildOffers_t> Game_DriverContracts_BuildOffers{}; // L2 experimental
using Game_DriverContracts_OfferSecretMeeting_t = void(void *contracts, const void *meeting, uint32_t team, uint32_t driver, uint8_t reason, uint8_t notify);
inline constexpr fn<"Game_DriverContracts_OfferSecretMeeting", Game_DriverContracts_OfferSecretMeeting_t> Game_DriverContracts_OfferSecretMeeting{}; // L2 experimental
using Game_DriverSelectHeader_SetTabState_t = void(void *header, uint64_t tab, uint8_t content_visible, uint8_t enabled);
inline constexpr fn<"Game_DriverSelectHeader_SetTabState", Game_DriverSelectHeader_SetTabState_t> Game_DriverSelectHeader_SetTabState{}; // L2 experimental
using Game_EngineMgmt_AddGridPenalty_t = void(void *engine_management, const uint32_t *driver, uint32_t places, uint32_t reason);
inline constexpr fn<"Game_EngineMgmt_AddGridPenalty", Game_EngineMgmt_AddGridPenalty_t> Game_EngineMgmt_AddGridPenalty{}; // L2 experimental
using Game_EngineMgmt_ApplySessionWear_t = void(void *engine_management, uint32_t driver, float amount, uint8_t flag);
inline constexpr fn<"Game_EngineMgmt_ApplySessionWear", Game_EngineMgmt_ApplySessionWear_t> Game_EngineMgmt_ApplySessionWear{}; // L2 experimental
using Game_EntrantsUi_BuildCharacterRef_t = void *(void *out, const uint8_t *character, void *node);
inline constexpr fn<"Game_EntrantsUi_BuildCharacterRef", Game_EntrantsUi_BuildCharacterRef_t> Game_EntrantsUi_BuildCharacterRef{}; // L2 experimental
using Game_EntrantsUi_OnCharacterListLoaded_t = int64_t(void *ctx, const void *message);
inline constexpr fn<"Game_EntrantsUi_OnCharacterListLoaded", Game_EntrantsUi_OnCharacterListLoaded_t> Game_EntrantsUi_OnCharacterListLoaded{}; // L2 experimental
using Game_F1WorldActivityContainer_SelectEntrantList_t = void(void *container, const char *entrant_list);
inline constexpr fn<"Game_F1WorldActivityContainer_SelectEntrantList", Game_F1WorldActivityContainer_SelectEntrantList_t> Game_F1WorldActivityContainer_SelectEntrantList{}; // L2 experimental
using Game_Get_ModelNodeId_t = uint32_t(const char *name);
inline constexpr fn<"Game_Get_ModelNodeId", Game_Get_ModelNodeId_t> Game_Get_ModelNodeId{}; // L2 experimental
using Game_Handle_GarageCrewEvent_Thunk_t = uint64_t(void **cars, void *event);
inline constexpr fn<"Game_Handle_GarageCrewEvent_Thunk", Game_Handle_GarageCrewEvent_Thunk_t> Game_Handle_GarageCrewEvent_Thunk{}; // L2 experimental
using Game_Handle_SpecGarageCrewEvent_Thunk_t = uint64_t(void *self, void *event);
inline constexpr fn<"Game_Handle_SpecGarageCrewEvent_Thunk", Game_Handle_SpecGarageCrewEvent_Thunk_t> Game_Handle_SpecGarageCrewEvent_Thunk{}; // L2 experimental
using Game_Network_CategoryOptionUsing_t = void(void *self);
inline constexpr fn<"Game_Network_CategoryOptionUsing", Game_Network_CategoryOptionUsing_t> Game_Network_CategoryOptionUsing{}; // L2 experimental
using Game_Network_SendingRedFlagVehicleStatusUpdate_t = void(void *red_flag_state, const double *dt);
inline constexpr fn<"Game_Network_SendingRedFlagVehicleStatusUpdate", Game_Network_SendingRedFlagVehicleStatusUpdate_t> Game_Network_SendingRedFlagVehicleStatusUpdate{}; // L2 experimental
using Game_Network_SetScenario_t = void(void *session, uint8_t stat_id, const void *spec);
inline constexpr fn<"Game_Network_SetScenario", Game_Network_SetScenario_t> Game_Network_SetScenario{}; // L2 experimental
using Game_OptionsStore_FindOption_t = uint64_t(void *map, const char **name);
inline constexpr fn<"Game_OptionsStore_FindOption", Game_OptionsStore_FindOption_t> Game_OptionsStore_FindOption{}; // L2 experimental
using Game_PitLane_MoveDownSpline_t = void(void *pitlane, uint8_t car, void *teleport_req, bool alt_spline, float dt, bool p6, bool blend, bool p8, bool p9);
inline constexpr fn<"Game_PitLane_MoveDownSpline", Game_PitLane_MoveDownSpline_t> Game_PitLane_MoveDownSpline{}; // L2 experimental
inline constexpr address<"Game_PitLane_VehicleEnteringInternal"> Game_PitLane_VehicleEnteringInternal{}; // L2 experimental
inline constexpr address<"Game_Pitlane_Multiplayer_teleportToGarage"> Game_Pitlane_Multiplayer_teleportToGarage{}; // L2 experimental
inline constexpr address<"Game_Pitlane_SetVehicleStatus"> Game_Pitlane_SetVehicleStatus{}; // L2 experimental
using Game_Place_Vehicle_t = void(void *teleport_req, const char *tag, int priority, const float *mtx44_a, const float *mtx44_b, float dt, uint32_t flags);
inline constexpr fn<"Game_Place_Vehicle", Game_Place_Vehicle_t> Game_Place_Vehicle{}; // L2 experimental
using Game_PracticeTrackAcclimatisation_RefreshState_t = void(void *self);
inline constexpr fn<"Game_PracticeTrackAcclimatisation_RefreshState", Game_PracticeTrackAcclimatisation_RefreshState_t> Game_PracticeTrackAcclimatisation_RefreshState{}; // L2 experimental
using Game_Predict_StrategyWetness_t = float(void *self, const float *window3);
inline constexpr fn<"Game_Predict_StrategyWetness", Game_Predict_StrategyWetness_t> Game_Predict_StrategyWetness{}; // L2 experimental
using Game_RaceMarshalSystem_Update_t = void(void *system, double *dt);
inline constexpr fn<"Game_RaceMarshalSystem_Update", Game_RaceMarshalSystem_Update_t> Game_RaceMarshalSystem_Update{}; // L2 experimental
using Game_RaceStrategyVm_Populate_t = void(void *vm);
inline constexpr fn<"Game_RaceStrategyVm_Populate", Game_RaceStrategyVm_Populate_t> Game_RaceStrategyVm_Populate{}; // L2 experimental
using Game_Refresh_PerformanceComparison_t = void(void *comparison);
inline constexpr fn<"Game_Refresh_PerformanceComparison", Game_Refresh_PerformanceComparison_t> Game_Refresh_PerformanceComparison{}; // L2 experimental
using Game_Rnd_CalcUpgradeTypeProgress_t = float(void *const *instances, uint8_t type, uint8_t state, const void *meta);
inline constexpr fn<"Game_Rnd_CalcUpgradeTypeProgress", Game_Rnd_CalcUpgradeTypeProgress_t> Game_Rnd_CalcUpgradeTypeProgress{}; // L2 experimental
using Game_Rnd_FillVehicleUpgrades_t = void(void *component, uint8_t car, uint8_t a3, uint8_t a4);
inline constexpr fn<"Game_Rnd_FillVehicleUpgrades", Game_Rnd_FillVehicleUpgrades_t> Game_Rnd_FillVehicleUpgrades{}; // L2 experimental
using Game_SaveData_DeserialiseContainer_t = int *(SaveDataContainer *self, int *result, void *bag, uint8_t reset_on_error, uint64_t groups);
inline constexpr fn<"Game_SaveData_DeserialiseContainer", Game_SaveData_DeserialiseContainer_t> Game_SaveData_DeserialiseContainer{}; // L3 stable
using Game_SaveData_SerialiseContainer_t = int *(SaveDataContainer *self, int *result, void *bag, uint64_t groups);
inline constexpr fn<"Game_SaveData_SerialiseContainer", Game_SaveData_SerialiseContainer_t> Game_SaveData_SerialiseContainer{}; // L2 experimental
using Game_SaveData_WriteBag_t = int(NeBagCodec *codec, void *bag);
inline constexpr fn<"Game_SaveData_WriteBag", Game_SaveData_WriteBag_t> Game_SaveData_WriteBag{}; // L3 stable
using Game_SceneManagerSpawn_SpawnScene_t = void *(void *mgr, const uint8_t *opts, void *scene, const float *mtx44, void *ov_a, void *ov_b, uint64_t p7);
inline constexpr fn<"Game_SceneManagerSpawn_SpawnScene", Game_SceneManagerSpawn_SpawnScene_t> Game_SceneManagerSpawn_SpawnScene{}; // L2 experimental
using Game_SeasonLength_ApplyPreset_t = void(void *component, uint8_t preset);
inline constexpr fn<"Game_SeasonLength_ApplyPreset", Game_SeasonLength_ApplyPreset_t> Game_SeasonLength_ApplyPreset{}; // L2 experimental
using Game_SeasonLength_PushCalendarEntry_t = uint64_t(void *calendar, const void *entry);
inline constexpr fn<"Game_SeasonLength_PushCalendarEntry", Game_SeasonLength_PushCalendarEntry_t> Game_SeasonLength_PushCalendarEntry{}; // L2 experimental
using Game_Set_VehicleController_Thunk_t = void(void *system, uint64_t vehicle, uint32_t ai, int32_t flag);
inline constexpr fn<"Game_Set_VehicleController_Thunk", Game_Set_VehicleController_Thunk_t> Game_Set_VehicleController_Thunk{}; // L2 experimental
using Game_SpeechSystem_PostLine_t = void(void *speech, void *line, uint8_t retrigger);
inline constexpr fn<"Game_SpeechSystem_PostLine", Game_SpeechSystem_PostLine_t> Game_SpeechSystem_PostLine{}; // L2 experimental
using Game_Teleport_Vehicle_t = void(void *car, const char *name, float *prev_mtx, float *new_mtx, float dt, uint32_t flags);
inline constexpr fn<"Game_Teleport_Vehicle", Game_Teleport_Vehicle_t> Game_Teleport_Vehicle{}; // L2 experimental
inline constexpr data<"g_Game_FlowLinkStay", uint32_t> g_Game_FlowLinkStay{}; // L2 experimental
inline constexpr data<"g_Game_Manager", void *> g_Game_Manager{}; // L2 experimental
inline constexpr data<"g_Game_MessageBus", void *> g_Game_MessageBus{}; // L2 experimental
inline constexpr data<"g_Game_OptionsStore", void *> g_Game_OptionsStore{}; // L2 experimental
inline constexpr data<"g_Game_RaceStartSystem", void*> g_Game_RaceStartSystem{}; // L2 experimental
inline constexpr data<"g_Game_Root", void *> g_Game_Root{}; // L2 experimental
inline constexpr data<"g_Game_Scenarios", void *> g_Game_Scenarios{}; // L2 experimental
inline constexpr data<"g_Game_TrackLength", float> g_Game_TrackLength{}; // L2 experimental
inline constexpr data<"g_Game_VehicleRecordManager", void *> g_Game_VehicleRecordManager{}; // L2 experimental
inline constexpr data<"g_Game_VehicleStateRoot", void *> g_Game_VehicleStateRoot{}; // L2 experimental
inline constexpr data<"k_Game_TrackListComponentDesc", void *> k_Game_TrackListComponentDesc{}; // L2 experimental, TrackListComponent
inline constexpr call<"site_Game_CareerContinue_DriverSlotTitle"> site_Game_CareerContinue_DriverSlotTitle{}; // L2 experimental
inline constexpr site<"site_Game_CareerWizardConfirm_NextRoute"> site_Game_CareerWizardConfirm_NextRoute{}; // L2 experimental
inline constexpr site<"site_Game_CareerWizard_BackRoute"> site_Game_CareerWizard_BackRoute{}; // L2 experimental
inline constexpr call<"site_Game_DriverSelectHeader_BuildTabs"> site_Game_DriverSelectHeader_BuildTabs{}; // L2 experimental
inline constexpr site<"site_Game_DriverSelect_BackRoute"> site_Game_DriverSelect_BackRoute{}; // L2 experimental
inline constexpr call<"site_Game_DriverSelect_BadgeFormat"> site_Game_DriverSelect_BadgeFormat{}; // L2 experimental
inline constexpr site<"site_Game_DriverSelect_NumberResult"> site_Game_DriverSelect_NumberResult{}; // L2 experimental
inline constexpr call<"site_Game_DriverSelect_TeamLogoFormat"> site_Game_DriverSelect_TeamLogoFormat{}; // L2 experimental
inline constexpr site<"site_Game_ErsNewLap_NextVehicle"> site_Game_ErsNewLap_NextVehicle{}; // L2 experimental
inline constexpr site<"site_Game_ErsNewLap_VehicleLookup"> site_Game_ErsNewLap_VehicleLookup{}; // L2 experimental
inline constexpr site<"site_Game_SaveData_ContainersChildren"> site_Game_SaveData_ContainersChildren{}; // L3 stable
inline constexpr site<"site_Game_SaveData_ContainersCount"> site_Game_SaveData_ContainersCount{}; // L3 stable
inline constexpr site<"site_Game_SaveData_ContainersDone"> site_Game_SaveData_ContainersDone{}; // L2 experimental
inline constexpr site<"site_Game_SaveData_UnclaimedChild"> site_Game_SaveData_UnclaimedChild{}; // L3 stable
inline constexpr site<"site_Game_TeamSelect_NumberResult"> site_Game_TeamSelect_NumberResult{}; // L2 experimental
inline constexpr call<"site_Game_TeamSelect_TeamLogoFormat"> site_Game_TeamSelect_TeamLogoFormat{}; // L2 experimental
inline constexpr vtable<"vt_Game_CareerDLCSelectViewModel", 19> vt_Game_CareerDLCSelectViewModel{}; // L2 experimental
inline constexpr vtable<"vt_Game_CareerWizardSettingsSelectViewModel", 19> vt_Game_CareerWizardSettingsSelectViewModel{}; // L2 experimental, CareerWizardSettingsSelectViewModel.
inline constexpr vtable<"vt_Game_DriverCareerDriverSelectViewModel", 22> vt_Game_DriverCareerDriverSelectViewModel{}; // L2 experimental
inline constexpr vtable<"vt_Game_DriverCareerSlotPredicate", 6> vt_Game_DriverCareerSlotPredicate{}; // L2 experimental
inline constexpr vtable<"vt_Game_SaveDataRootContainer", 7> vt_Game_SaveDataRootContainer{}; // L3 stable
inline constexpr vtable<"vt_Game_TrackListComponent", 79> vt_Game_TrackListComponent{}; // L2 experimental, TrackListComponent

}
