#pragma once
#include <swofty/game/base.hpp>

namespace swofty::game {

struct BankAccount {
    uint8_t _pad_0x0[0x10];
    BankSubAccount *subs_begin; // L2 experimental
    BankSubAccount *subs_end; // L2 experimental
    uint8_t _pad_0x20[0x8];
    uint32_t key; // L2 experimental
    uint8_t _pad_0x2c[0x4];
};
static_assert(offsetof(BankAccount, subs_begin) == 0x10);
static_assert(offsetof(BankAccount, subs_end) == 0x18);
static_assert(offsetof(BankAccount, key) == 0x28);
static_assert(sizeof(BankAccount) == 0x30);

struct BankSubAccount {
    uint8_t _pad_0x0[0xc];
    uint8_t category; // L2 experimental
    uint8_t _pad_0xd[0x3];
    int32_t balance; // L2 experimental
    int32_t minimum; // L2 experimental
    uint8_t _pad_0x18[0x28];
    BankTransaction *history_begin; // L2 experimental
    BankTransaction *history_end; // L2 experimental
    uint8_t _pad_0x50[0x160];
};
static_assert(offsetof(BankSubAccount, category) == 0xc);
static_assert(offsetof(BankSubAccount, balance) == 0x10);
static_assert(offsetof(BankSubAccount, minimum) == 0x14);
static_assert(offsetof(BankSubAccount, history_begin) == 0x40);
static_assert(offsetof(BankSubAccount, history_end) == 0x48);
static_assert(sizeof(BankSubAccount) == 0x1b0);

struct BankTransaction {
    uint8_t type; // L2 experimental
    uint8_t _pad_0x1[0x3];
    int32_t amount; // L2 experimental
    uint8_t _pad_0x8[0x4];
    uint32_t time; // L2 experimental
    uint8_t flow; // L2 experimental
    uint8_t _pad_0x11[0x3];
};
static_assert(offsetof(BankTransaction, type) == 0x0);
static_assert(offsetof(BankTransaction, amount) == 0x4);
static_assert(offsetof(BankTransaction, time) == 0xc);
static_assert(offsetof(BankTransaction, flow) == 0x10);
static_assert(sizeof(BankTransaction) == 0x14);

struct BankTransactionMessage {
    void **vtable; // L2 experimental
    uint8_t _pad_0x8[0x8];
    uint16_t account; // L2 experimental
    uint8_t _pad_0x12[0x2];
    uint8_t type; // L2 experimental
    uint8_t _pad_0x15[0x3];
    int32_t amount; // L2 experimental
    uint32_t created; // L2 experimental
    uint32_t time; // L2 experimental
    uint8_t flow; // L2 experimental
    uint8_t check_funds; // L2 experimental
    uint8_t _pad_0x26[0x2];
    uint8_t category; // L2 experimental
};
static_assert(offsetof(BankTransactionMessage, vtable) == 0x0);
static_assert(offsetof(BankTransactionMessage, account) == 0x10);
static_assert(offsetof(BankTransactionMessage, type) == 0x14);
static_assert(offsetof(BankTransactionMessage, amount) == 0x18);
static_assert(offsetof(BankTransactionMessage, created) == 0x1c);
static_assert(offsetof(BankTransactionMessage, time) == 0x20);
static_assert(offsetof(BankTransactionMessage, flow) == 0x24);
static_assert(offsetof(BankTransactionMessage, check_funds) == 0x25);
static_assert(offsetof(BankTransactionMessage, category) == 0x28);

struct CarNumberComponent {
    uint8_t _pad_0x0[0x278];
    CareerEntity *entity; // L2 experimental
    uint8_t _pad_0x280[0x38];
};
static_assert(offsetof(CarNumberComponent, entity) == 0x278);
static_assert(sizeof(CarNumberComponent) == 0x2b8);

struct CareerBankComponent {
    uint8_t _pad_0x0[0x298];
    BankAccount *team_accounts_begin; // L2 experimental
    BankAccount *team_accounts_end; // L2 experimental
};
static_assert(offsetof(CareerBankComponent, team_accounts_begin) == 0x298);
static_assert(offsetof(CareerBankComponent, team_accounts_end) == 0x2a0);

struct CareerDriverContractRenewalComponent {
    uint8_t _pad_0x0[0x3a1];
    uint8_t disable_driver_moves; // L2 experimental
    uint8_t _pad_0x3a2[0x6];
    void *driver_moves_allocator; // L2 experimental
    DriverMoveRecord *driver_moves_begin; // L2 experimental
    DriverMoveRecord *driver_moves_end; // L2 experimental
    uint64_t driver_moves_capacity; // L2 experimental
};
static_assert(offsetof(CareerDriverContractRenewalComponent, disable_driver_moves) == 0x3a1);
static_assert(offsetof(CareerDriverContractRenewalComponent, driver_moves_allocator) == 0x3a8);
static_assert(offsetof(CareerDriverContractRenewalComponent, driver_moves_begin) == 0x3b0);
static_assert(offsetof(CareerDriverContractRenewalComponent, driver_moves_end) == 0x3b8);
static_assert(offsetof(CareerDriverContractRenewalComponent, driver_moves_capacity) == 0x3c0);

struct CareerEntity {
    void **vtable; // L2 experimental
    uint8_t _pad_0x8[0x2d8];
    EntrantRecord *entrants_begin; // L2 experimental
    EntrantRecord *entrants_end; // L2 experimental
    uint8_t _pad_0x2f0[0x8];
    uint8_t entrant_update_busy; // L2 experimental
    uint8_t _pad_0x2f9[0xc5f];
    TrackDataSystem *track_data; // L2 experimental
    uint8_t _pad_0xf60[0xd0];
    void **component_registry; // L2 experimental
    uint8_t _pad_0x1038[0x8];
    void *championships_begin; // L2 experimental
    void *championships_end; // L2 experimental
    uint8_t _pad_0x1050[0x8];
    void *current_championship; // L2 experimental
    uint8_t _pad_0x1060[0x8];
    void **championship_order_begin; // L2 experimental
    void **championship_order_end; // L2 experimental
};
static_assert(offsetof(CareerEntity, vtable) == 0x0);
static_assert(offsetof(CareerEntity, entrants_begin) == 0x2e0);
static_assert(offsetof(CareerEntity, entrants_end) == 0x2e8);
static_assert(offsetof(CareerEntity, entrant_update_busy) == 0x2f8);
static_assert(offsetof(CareerEntity, track_data) == 0xf58);
static_assert(offsetof(CareerEntity, component_registry) == 0x1030);
static_assert(offsetof(CareerEntity, championships_begin) == 0x1040);
static_assert(offsetof(CareerEntity, championships_end) == 0x1048);
static_assert(offsetof(CareerEntity, current_championship) == 0x1058);
static_assert(offsetof(CareerEntity, championship_order_begin) == 0x1068);
static_assert(offsetof(CareerEntity, championship_order_end) == 0x1070);

struct CareerSponsorsComponent {
    uint8_t _pad_0x0[0x298];
    SponsorTeamRecord *teams_begin; // L2 experimental
    SponsorTeamRecord *teams_end; // L2 experimental
};
static_assert(offsetof(CareerSponsorsComponent, teams_begin) == 0x298);
static_assert(offsetof(CareerSponsorsComponent, teams_end) == 0x2a0);

struct CareerTimeComponent {
    uint8_t _pad_0x0[0x290];
    uint8_t season_dates; // L2 experimental
    uint8_t _pad_0x291[0x1f];
    uint32_t current_minutes; // L2 experimental
};
static_assert(offsetof(CareerTimeComponent, season_dates) == 0x290);
static_assert(offsetof(CareerTimeComponent, current_minutes) == 0x2b0);

struct Championship {
    void **vtable; // L2 experimental
    uint8_t _pad_0x8[0x200];
    void **list_vtable; // L2 experimental
    uint8_t _pad_0x210[0x328];
    uint32_t completion; // L2 experimental
    uint8_t _pad_0x53c[0xc];
    ChampionshipSeasonEntry *seasons_begin; // L2 experimental
    ChampionshipSeasonEntry *seasons_end; // L2 experimental
    uint8_t _pad_0x558[0x8];
    uint32_t season_index; // L2 experimental
    uint8_t _pad_0x564[0x4c];
};
static_assert(offsetof(Championship, vtable) == 0x0);
static_assert(offsetof(Championship, list_vtable) == 0x208);
static_assert(offsetof(Championship, completion) == 0x538);
static_assert(offsetof(Championship, seasons_begin) == 0x548);
static_assert(offsetof(Championship, seasons_end) == 0x550);
static_assert(offsetof(Championship, season_index) == 0x560);
static_assert(sizeof(Championship) == 0x5b0);

struct ChampionshipSeasonEntry {
    uint8_t _pad_0x0[0x8];
    Season *season; // L2 experimental
};
static_assert(offsetof(ChampionshipSeasonEntry, season) == 0x8);
static_assert(sizeof(ChampionshipSeasonEntry) == 0x10);

struct ContextStatSet {
    uint8_t _pad_0x0[0x10];
    DriverStatistic *stats_begin; // L2 experimental
    DriverStatistic *stats_end; // L2 experimental
    uint8_t _pad_0x20[0x8];
    uint32_t mask_a; // L2 experimental
    uint32_t mask_b; // L2 experimental
    uint8_t _pad_0x30[0x8];
};
static_assert(offsetof(ContextStatSet, stats_begin) == 0x10);
static_assert(offsetof(ContextStatSet, stats_end) == 0x18);
static_assert(offsetof(ContextStatSet, mask_a) == 0x28);
static_assert(offsetof(ContextStatSet, mask_b) == 0x2c);
static_assert(sizeof(ContextStatSet) == 0x38);

struct ContractRecord {
    uint8_t _pad_0x0[0x28];
    uint8_t clause_stat; // L2 experimental
    uint8_t _pad_0x29[0x3];
    int32_t clause_target; // L2 experimental
    uint8_t _pad_0x30[0x4];
    int32_t salary; // L2 experimental
    int32_t signing_bonus; // L2 experimental
    int32_t team_perf_money; // L2 experimental
    int32_t team_perf_race_value; // L2 experimental
    float success_chance; // L2 experimental
    int32_t negotiation_bonus; // L2 experimental
    uint8_t is_counteroffer; // L2 experimental
    uint8_t _pad_0x4d[0x3];
    uint32_t driver; // L2 experimental
    uint32_t old_team; // L2 experimental
    uint32_t new_team; // L2 experimental
    uint8_t driver_move_reason; // L2 experimental
    uint8_t _pad_0x5d[0x3];
    uint32_t weekend_index; // L2 experimental
    uint16_t career_day_index; // L2 experimental
    uint8_t _pad_0x66[0x2];
    uint16_t day_contract_sent; // L2 experimental
    uint16_t day_contract_decided; // L2 experimental
    uint8_t accepted; // L2 experimental
    uint8_t acknowledged; // L2 experimental
    uint8_t _pad_0x6e[0x2];
};
static_assert(offsetof(ContractRecord, clause_stat) == 0x28);
static_assert(offsetof(ContractRecord, clause_target) == 0x2c);
static_assert(offsetof(ContractRecord, salary) == 0x34);
static_assert(offsetof(ContractRecord, signing_bonus) == 0x38);
static_assert(offsetof(ContractRecord, team_perf_money) == 0x3c);
static_assert(offsetof(ContractRecord, team_perf_race_value) == 0x40);
static_assert(offsetof(ContractRecord, success_chance) == 0x44);
static_assert(offsetof(ContractRecord, negotiation_bonus) == 0x48);
static_assert(offsetof(ContractRecord, is_counteroffer) == 0x4c);
static_assert(offsetof(ContractRecord, driver) == 0x50);
static_assert(offsetof(ContractRecord, old_team) == 0x54);
static_assert(offsetof(ContractRecord, new_team) == 0x58);
static_assert(offsetof(ContractRecord, driver_move_reason) == 0x5c);
static_assert(offsetof(ContractRecord, weekend_index) == 0x60);
static_assert(offsetof(ContractRecord, career_day_index) == 0x64);
static_assert(offsetof(ContractRecord, day_contract_sent) == 0x68);
static_assert(offsetof(ContractRecord, day_contract_decided) == 0x6a);
static_assert(offsetof(ContractRecord, accepted) == 0x6c);
static_assert(offsetof(ContractRecord, acknowledged) == 0x6d);
static_assert(sizeof(ContractRecord) == 0x70);

struct CostCapComponent {
    uint8_t _pad_0x0[0x2d8];
    CostCapTeam **team_slots; // L2 experimental
    uint8_t *team_ctrl; // L2 experimental
};
static_assert(offsetof(CostCapComponent, team_slots) == 0x2d8);
static_assert(offsetof(CostCapComponent, team_ctrl) == 0x2e0);

struct CostCapSeason {
    int32_t season_id; // L2 experimental
    uint8_t _pad_0x4[0x4];
    int32_t cached_expenses; // L2 experimental
    int32_t cost_cap; // L2 experimental
    float cached_modifier; // L2 experimental
};
static_assert(offsetof(CostCapSeason, season_id) == 0x0);
static_assert(offsetof(CostCapSeason, cached_expenses) == 0x8);
static_assert(offsetof(CostCapSeason, cost_cap) == 0xc);
static_assert(offsetof(CostCapSeason, cached_modifier) == 0x10);

struct CostCapTeam {
    uint16_t account; // L2 experimental
    uint8_t _pad_0x2[0x2e];
    CostCapSeason **season_slots; // L2 experimental
    uint8_t *season_ctrl; // L2 experimental
};
static_assert(offsetof(CostCapTeam, account) == 0x0);
static_assert(offsetof(CostCapTeam, season_slots) == 0x30);
static_assert(offsetof(CostCapTeam, season_ctrl) == 0x38);

struct CurvePoint {
    float x; // L2 experimental
    float y; // L2 experimental
};
static_assert(offsetof(CurvePoint, x) == 0x0);
static_assert(offsetof(CurvePoint, y) == 0x4);
static_assert(sizeof(CurvePoint) == 0x8);

struct DriverCareerContractOwner {
    uint8_t _pad_0x0[0x298];
    void *contracts_begin; // L2 experimental
    void *contracts_end; // L2 experimental
};
static_assert(offsetof(DriverCareerContractOwner, contracts_begin) == 0x298);
static_assert(offsetof(DriverCareerContractOwner, contracts_end) == 0x2a0);

struct DriverCareerContractsManagerComponent {
    uint8_t _pad_0x0[0x270];
    void *weekend_source; // L2 experimental
    CareerEntity *entity; // L2 experimental
    uint8_t _pad_0x280[0xd0];
    DriverCareerContractOwner *contract_owner; // L2 experimental
    CareerTimeComponent *career_time; // L2 experimental
    uint8_t _pad_0x360[0x8];
    CarNumberComponent *car_numbers; // L2 experimental
    ScheduleComponent *schedule; // L2 experimental
    uint8_t _pad_0x378[0x30];
    void *secret_meetings_begin; // L2 experimental
    void *secret_meetings_end; // L2 experimental
};
static_assert(offsetof(DriverCareerContractsManagerComponent, weekend_source) == 0x270);
static_assert(offsetof(DriverCareerContractsManagerComponent, entity) == 0x278);
static_assert(offsetof(DriverCareerContractsManagerComponent, contract_owner) == 0x350);
static_assert(offsetof(DriverCareerContractsManagerComponent, career_time) == 0x358);
static_assert(offsetof(DriverCareerContractsManagerComponent, car_numbers) == 0x368);
static_assert(offsetof(DriverCareerContractsManagerComponent, schedule) == 0x370);
static_assert(offsetof(DriverCareerContractsManagerComponent, secret_meetings_begin) == 0x3a8);
static_assert(offsetof(DriverCareerContractsManagerComponent, secret_meetings_end) == 0x3b0);

struct DriverContract {
    uint32_t team; // L2 experimental
    uint8_t _pad_0x4[0x8];
    uint16_t signed_day; // L2 experimental
    uint8_t kind; // L2 experimental
    uint8_t _pad_0xf[0x1];
    uint16_t end_day; // L2 experimental
    uint8_t state; // L2 experimental
    uint8_t _pad_0x13[0x1];
    uint32_t end_value; // L2 experimental
    uint8_t _pad_0x18[0x14];
    uint8_t active; // L2 experimental
    uint8_t _pad_0x2d[0xb];
    void *history_begin; // L2 experimental
    void *history_end; // L2 experimental
    uint8_t _pad_0x48[0x8];
    uint32_t driver; // L2 experimental
};
static_assert(offsetof(DriverContract, team) == 0x0);
static_assert(offsetof(DriverContract, signed_day) == 0xc);
static_assert(offsetof(DriverContract, kind) == 0xe);
static_assert(offsetof(DriverContract, end_day) == 0x10);
static_assert(offsetof(DriverContract, state) == 0x12);
static_assert(offsetof(DriverContract, end_value) == 0x14);
static_assert(offsetof(DriverContract, active) == 0x2c);
static_assert(offsetof(DriverContract, history_begin) == 0x38);
static_assert(offsetof(DriverContract, history_end) == 0x40);
static_assert(offsetof(DriverContract, driver) == 0x50);

struct DriverContractChangedMessage {
    void **vtable; // L2 experimental
    uint64_t reserved; // L2 experimental
    uint32_t driver; // L2 experimental
    uint8_t before[48]; // L2 experimental
    uint8_t after[44]; // L2 experimental
    uint8_t kind; // L2 experimental
    uint8_t _pad_0x71[0x7];
};
static_assert(offsetof(DriverContractChangedMessage, vtable) == 0x0);
static_assert(offsetof(DriverContractChangedMessage, reserved) == 0x8);
static_assert(offsetof(DriverContractChangedMessage, driver) == 0x10);
static_assert(offsetof(DriverContractChangedMessage, before) == 0x14);
static_assert(offsetof(DriverContractChangedMessage, after) == 0x44);
static_assert(offsetof(DriverContractChangedMessage, kind) == 0x70);
static_assert(sizeof(DriverContractChangedMessage) == 0x78);

struct DriverContractsMyTeamComponent {
    uint8_t _pad_0x0[0x2e8];
    CarNumberComponent *car_numbers; // L2 experimental
    uint8_t _pad_0x2f0[0x4a8];
    ContractRecord *pending_contracts_begin; // L2 experimental
    uint8_t _pad_0x7a0[0x18];
    ContractRecord *decided_contracts_begin; // L2 experimental
    uint8_t _pad_0x7c0[0x18];
    ContractRecord *pending_moves_begin; // L2 experimental
    ContractRecord *pending_moves_end; // L2 experimental
    uint8_t _pad_0x7e8[0x10];
    ContractRecord *last_season_signings_begin; // L2 experimental
    ContractRecord *last_season_signings_end; // L2 experimental
};
static_assert(offsetof(DriverContractsMyTeamComponent, car_numbers) == 0x2e8);
static_assert(offsetof(DriverContractsMyTeamComponent, pending_contracts_begin) == 0x798);
static_assert(offsetof(DriverContractsMyTeamComponent, decided_contracts_begin) == 0x7b8);
static_assert(offsetof(DriverContractsMyTeamComponent, pending_moves_begin) == 0x7d8);
static_assert(offsetof(DriverContractsMyTeamComponent, pending_moves_end) == 0x7e0);
static_assert(offsetof(DriverContractsMyTeamComponent, last_season_signings_begin) == 0x7f8);
static_assert(offsetof(DriverContractsMyTeamComponent, last_season_signings_end) == 0x800);

struct DriverMoveRecord {
    uint32_t driver; // L2 experimental
    uint32_t old_team; // L2 experimental
    uint32_t new_team; // L2 experimental
    uint8_t reason; // L2 experimental
    uint8_t _pad_0xd[0x3];
    uint32_t weekend_index; // L2 experimental
    uint16_t day_index; // L2 experimental
    uint8_t _pad_0x16[0x2];
};
static_assert(offsetof(DriverMoveRecord, driver) == 0x0);
static_assert(offsetof(DriverMoveRecord, old_team) == 0x4);
static_assert(offsetof(DriverMoveRecord, new_team) == 0x8);
static_assert(offsetof(DriverMoveRecord, reason) == 0xc);
static_assert(offsetof(DriverMoveRecord, weekend_index) == 0x10);
static_assert(offsetof(DriverMoveRecord, day_index) == 0x14);
static_assert(sizeof(DriverMoveRecord) == 0x18);

struct DriverMoveConfirmedMessage {
    void **vtable; // L2 experimental
    uint64_t reserved; // L2 experimental
    DriverMoveRecord move; // L2 experimental
};
static_assert(offsetof(DriverMoveConfirmedMessage, vtable) == 0x0);
static_assert(offsetof(DriverMoveConfirmedMessage, reserved) == 0x8);
static_assert(offsetof(DriverMoveConfirmedMessage, move) == 0x10);
static_assert(sizeof(DriverMoveConfirmedMessage) == 0x28);

struct LineupChange {
    uint32_t driver; // L2 experimental
    uint32_t old_team; // L2 experimental
    uint32_t new_team; // L2 experimental
    uint8_t _pad_0xc[0x4];
};
static_assert(offsetof(LineupChange, driver) == 0x0);
static_assert(offsetof(LineupChange, old_team) == 0x4);
static_assert(offsetof(LineupChange, new_team) == 0x8);
static_assert(sizeof(LineupChange) == 0x10);

struct DriverMovesFinalisedMessage {
    void **vtable; // L2 experimental
    uint64_t reserved; // L2 experimental
    LineupChange changes[40]; // L2 experimental
    uint64_t count; // L2 experimental
};
static_assert(offsetof(DriverMovesFinalisedMessage, vtable) == 0x0);
static_assert(offsetof(DriverMovesFinalisedMessage, reserved) == 0x8);
static_assert(offsetof(DriverMovesFinalisedMessage, changes) == 0x10);
static_assert(offsetof(DriverMovesFinalisedMessage, count) == 0x290);
static_assert(sizeof(DriverMovesFinalisedMessage) == 0x298);

struct EvolutionCurve {
    void **vtable; // L2 experimental
    float inv_x_range; // L2 experimental
    float min_x; // L2 experimental
    float y_step; // L2 experimental
    float end_value; // L2 experimental
    uint8_t _pad_0x18[0x4];
    float min_y; // L2 experimental
    float max_y; // L2 experimental
    uint8_t lut[65]; // L2 experimental
    uint8_t _pad_0x65[0x3];
    void *allocator; // L2 experimental
    CurvePoint *points_begin; // L2 experimental
    uint8_t _pad_0x78[0x8];
    uint64_t points_capacity; // L2 experimental
};
static_assert(offsetof(EvolutionCurve, vtable) == 0x0);
static_assert(offsetof(EvolutionCurve, inv_x_range) == 0x8);
static_assert(offsetof(EvolutionCurve, min_x) == 0xc);
static_assert(offsetof(EvolutionCurve, y_step) == 0x10);
static_assert(offsetof(EvolutionCurve, end_value) == 0x14);
static_assert(offsetof(EvolutionCurve, min_y) == 0x1c);
static_assert(offsetof(EvolutionCurve, max_y) == 0x20);
static_assert(offsetof(EvolutionCurve, lut) == 0x24);
static_assert(offsetof(EvolutionCurve, allocator) == 0x68);
static_assert(offsetof(EvolutionCurve, points_begin) == 0x70);
static_assert(offsetof(EvolutionCurve, points_capacity) == 0x80);
static_assert(sizeof(EvolutionCurve) == 0x88);

struct DriverStatistic {
    uint8_t _pad_0x0[0x10];
    uint16_t info_stat_id; // L2 experimental
    uint8_t _pad_0x12[0x6];
    EvolutionCurve evolution_graph; // L2 experimental
    EvolutionCurve patched_evolution_graph; // L2 experimental
    uint8_t has_patched_graph; // L2 experimental
    uint8_t _pad_0x129[0x7];
    float temporary_boost_value; // L2 experimental
    uint8_t is_dynamic_stat; // L2 experimental
    uint8_t _pad_0x135[0x3];
    float dynamic_level; // L2 experimental
    uint8_t _pad_0x13c[0x4];
};
static_assert(offsetof(DriverStatistic, info_stat_id) == 0x10);
static_assert(offsetof(DriverStatistic, evolution_graph) == 0x18);
static_assert(offsetof(DriverStatistic, patched_evolution_graph) == 0xa0);
static_assert(offsetof(DriverStatistic, has_patched_graph) == 0x128);
static_assert(offsetof(DriverStatistic, temporary_boost_value) == 0x130);
static_assert(offsetof(DriverStatistic, is_dynamic_stat) == 0x134);
static_assert(offsetof(DriverStatistic, dynamic_level) == 0x138);
static_assert(sizeof(DriverStatistic) == 0x140);

struct DriverStatisticsComponent {
    uint8_t _pad_0x0[0x278];
    void *stat_context; // L2 experimental
    uint8_t _pad_0x280[0x10];
    CareerTimeComponent *career_time; // L2 experimental
    uint8_t _pad_0x298[0x10];
    uint8_t stats_data; // L2 experimental
    uint8_t use_story_mode_data; // L2 experimental
    uint8_t _pad_0x2aa[0x2];
    uint32_t last_patch_applied_checksum; // L2 experimental
    uint32_t last_patch_declined_checksum; // L2 experimental
    uint8_t _pad_0x2b4[0x1c];
    void *stat_infos_begin; // L2 experimental
    void *stat_infos_end; // L2 experimental
    uint8_t _pad_0x2e0[0x30];
    DriverStatsEntry *driver_data_begin; // L2 experimental
    DriverStatsEntry *driver_data_end; // L2 experimental
};
static_assert(offsetof(DriverStatisticsComponent, stat_context) == 0x278);
static_assert(offsetof(DriverStatisticsComponent, career_time) == 0x290);
static_assert(offsetof(DriverStatisticsComponent, stats_data) == 0x2a8);
static_assert(offsetof(DriverStatisticsComponent, use_story_mode_data) == 0x2a9);
static_assert(offsetof(DriverStatisticsComponent, last_patch_applied_checksum) == 0x2ac);
static_assert(offsetof(DriverStatisticsComponent, last_patch_declined_checksum) == 0x2b0);
static_assert(offsetof(DriverStatisticsComponent, stat_infos_begin) == 0x2d0);
static_assert(offsetof(DriverStatisticsComponent, stat_infos_end) == 0x2d8);
static_assert(offsetof(DriverStatisticsComponent, driver_data_begin) == 0x310);
static_assert(offsetof(DriverStatisticsComponent, driver_data_end) == 0x318);

struct DriverStatsEntry {
    uint8_t kind; // L2 experimental
    uint8_t _pad_0x1[0x1];
    uint16_t stat_id; // L2 experimental
    uint8_t _pad_0x4[0x14];
    DriverStatistic *stats_begin; // L2 experimental
    DriverStatistic *stats_end; // L2 experimental
    uint8_t _pad_0x28[0x10];
    ContextStatSet *context_sets_begin; // L2 experimental
    ContextStatSet *context_sets_end; // L2 experimental
    uint8_t _pad_0x48[0x8];
};
static_assert(offsetof(DriverStatsEntry, kind) == 0x0);
static_assert(offsetof(DriverStatsEntry, stat_id) == 0x2);
static_assert(offsetof(DriverStatsEntry, stats_begin) == 0x18);
static_assert(offsetof(DriverStatsEntry, stats_end) == 0x20);
static_assert(offsetof(DriverStatsEntry, context_sets_begin) == 0x38);
static_assert(offsetof(DriverStatsEntry, context_sets_end) == 0x40);
static_assert(sizeof(DriverStatsEntry) == 0x50);

struct F1SeasonStandings {
    void **vtable; // L2 experimental
    uint8_t _pad_0x8[0x18];
    StandingsRound *points_begin; // L2 experimental
    StandingsRound *points_end; // L2 experimental
    uint8_t _pad_0x30[0x78];
    StandingsPoints *extra_points_begin; // L2 experimental
    StandingsPoints *extra_points_end; // L2 experimental
    uint8_t _pad_0xb8[0x18];
    StandingsRound *results_begin; // L2 experimental
    StandingsRound *results_end; // L2 experimental
};
static_assert(offsetof(F1SeasonStandings, vtable) == 0x0);
static_assert(offsetof(F1SeasonStandings, points_begin) == 0x20);
static_assert(offsetof(F1SeasonStandings, points_end) == 0x28);
static_assert(offsetof(F1SeasonStandings, extra_points_begin) == 0xa8);
static_assert(offsetof(F1SeasonStandings, extra_points_end) == 0xb0);
static_assert(offsetof(F1SeasonStandings, results_begin) == 0xd0);
static_assert(offsetof(F1SeasonStandings, results_end) == 0xd8);

struct FacilityNode {
    void **vtable; // L2 experimental
    uint8_t state; // L2 experimental
    uint8_t _pad_0x9[0x3];
    uint32_t stat_id; // L2 experimental
    uint8_t _pad_0x10[0x10];
    void *children_begin; // L2 experimental
    void *children_end; // L2 experimental
    uint8_t _pad_0x30[0x10];
    int32_t phase; // L2 experimental
    uint8_t _pad_0x44[0x3c];
    int32_t cost; // L2 experimental
    uint8_t _pad_0x84[0x4];
    char *name_loc_tag; // L2 experimental
    uint8_t _pad_0x90[0x18];
    int32_t days_to_complete; // L2 experimental
    int32_t days_remaining; // L2 experimental
    uint8_t purchased; // L2 experimental
    uint8_t _pad_0xb1[0x2];
    uint8_t seen_completed; // L2 experimental
    uint8_t _pad_0xb4[0x28];
    int32_t ui_level; // L2 experimental
    int32_t ui_max_level; // L2 experimental
    uint8_t _pad_0xe4[0x4];
};
static_assert(offsetof(FacilityNode, vtable) == 0x0);
static_assert(offsetof(FacilityNode, state) == 0x8);
static_assert(offsetof(FacilityNode, stat_id) == 0xc);
static_assert(offsetof(FacilityNode, children_begin) == 0x20);
static_assert(offsetof(FacilityNode, children_end) == 0x28);
static_assert(offsetof(FacilityNode, phase) == 0x40);
static_assert(offsetof(FacilityNode, cost) == 0x80);
static_assert(offsetof(FacilityNode, name_loc_tag) == 0x88);
static_assert(offsetof(FacilityNode, days_to_complete) == 0xa8);
static_assert(offsetof(FacilityNode, days_remaining) == 0xac);
static_assert(offsetof(FacilityNode, purchased) == 0xb0);
static_assert(offsetof(FacilityNode, seen_completed) == 0xb3);
static_assert(offsetof(FacilityNode, ui_level) == 0xdc);
static_assert(offsetof(FacilityNode, ui_max_level) == 0xe0);
static_assert(sizeof(FacilityNode) == 0xe8);

struct FacilityPerkTree {
    uint8_t _pad_0x0[0x8];
    void *nodes_begin; // L2 experimental
    void *nodes_end; // L2 experimental
    uint8_t _pad_0x18[0x10];
};
static_assert(offsetof(FacilityPerkTree, nodes_begin) == 0x8);
static_assert(offsetof(FacilityPerkTree, nodes_end) == 0x10);
static_assert(sizeof(FacilityPerkTree) == 0x28);

struct FacilityTeamData {
    uint8_t _pad_0x0[0x10];
    void *facilities_begin; // L2 experimental
    void *facilities_end; // L2 experimental
    uint8_t _pad_0x20[0x8];
    uint32_t team_key; // L2 experimental
    uint8_t _pad_0x2c[0x54];
};
static_assert(offsetof(FacilityTeamData, facilities_begin) == 0x10);
static_assert(offsetof(FacilityTeamData, facilities_end) == 0x18);
static_assert(offsetof(FacilityTeamData, team_key) == 0x28);
static_assert(sizeof(FacilityTeamData) == 0x80);

struct FacilityTeamEntry {
    uint8_t _pad_0x0[0x8];
    FacilityTeamData *data; // L2 experimental
};
static_assert(offsetof(FacilityTeamEntry, data) == 0x8);

struct MyTeamFacilitiesComponent {
    uint8_t _pad_0x0[0x2b8];
    FacilityTeamEntry **team_slots; // L2 experimental
    uint8_t *team_ctrl; // L2 experimental
    uint64_t team_count; // L2 experimental
};
static_assert(offsetof(MyTeamFacilitiesComponent, team_slots) == 0x2b8);
static_assert(offsetof(MyTeamFacilitiesComponent, team_ctrl) == 0x2c0);
static_assert(offsetof(MyTeamFacilitiesComponent, team_count) == 0x2c8);

struct MyTeamFacility {
    uint8_t facility_type; // L2 experimental
    uint8_t _pad_0x1[0x7];
    FacilityPerkTree *tree; // L2 experimental
    uint8_t _pad_0x10[0x40];
    int32_t workforce_cap; // L2 experimental
    uint8_t workforce_cap_set; // L2 experimental
    uint8_t _pad_0x55[0x183];
    int32_t highest_phase_seen; // L2 experimental
    uint8_t _pad_0x1dc[0x1c];
};
static_assert(offsetof(MyTeamFacility, facility_type) == 0x0);
static_assert(offsetof(MyTeamFacility, tree) == 0x8);
static_assert(offsetof(MyTeamFacility, workforce_cap) == 0x50);
static_assert(offsetof(MyTeamFacility, workforce_cap_set) == 0x54);
static_assert(offsetof(MyTeamFacility, highest_phase_seen) == 0x1d8);
static_assert(sizeof(MyTeamFacility) == 0x1f8);

struct RenownAwardedMessage {
    void **vtable; // L2 experimental
    uint8_t _pad_0x8[0x8];
    uint32_t team; // L2 experimental
    float amount; // L2 experimental
};
static_assert(offsetof(RenownAwardedMessage, vtable) == 0x0);
static_assert(offsetof(RenownAwardedMessage, team) == 0x10);
static_assert(offsetof(RenownAwardedMessage, amount) == 0x14);

struct RenownConfig {
    uint8_t _pad_0x0[0x8];
    uint32_t *tiers_begin; // L2 experimental
    uint32_t *tiers_end; // L2 experimental
};
static_assert(offsetof(RenownConfig, tiers_begin) == 0x8);
static_assert(offsetof(RenownConfig, tiers_end) == 0x10);

struct ResourcePointsComponent {
    uint8_t _pad_0x0[0x4a8];
    CareerEntity *wallet_entity; // L2 experimental
    uint8_t wallet_mode; // L2 experimental
    uint8_t _pad_0x4b1[0x7];
    void *driver_wallets; // L2 experimental
    uint8_t _pad_0x4c0[0x68];
    void *team_wallets; // L2 experimental
};
static_assert(offsetof(ResourcePointsComponent, wallet_entity) == 0x4a8);
static_assert(offsetof(ResourcePointsComponent, wallet_mode) == 0x4b0);
static_assert(offsetof(ResourcePointsComponent, driver_wallets) == 0x4b8);
static_assert(offsetof(ResourcePointsComponent, team_wallets) == 0x528);

struct Season {
    void **vtable; // L2 experimental
    uint8_t _pad_0x8[0x218];
    char *name; // L2 experimental
    uint8_t _pad_0x228[0x58];
    NeAllocator *allocator; // L2 experimental
    Weekend **weekends_begin; // L2 experimental
    Weekend **weekends_end; // L2 experimental
    uint8_t _pad_0x298[0x8];
    uint32_t current_weekend; // L2 experimental
    uint8_t _pad_0x2a4[0x4];
    void **standings_vtable; // L2 experimental
    uint8_t _pad_0x2b0[0x288];
    uint32_t completion_state; // L2 experimental
    uint8_t _pad_0x53c[0x14];
};
static_assert(offsetof(Season, vtable) == 0x0);
static_assert(offsetof(Season, name) == 0x220);
static_assert(offsetof(Season, allocator) == 0x280);
static_assert(offsetof(Season, weekends_begin) == 0x288);
static_assert(offsetof(Season, weekends_end) == 0x290);
static_assert(offsetof(Season, current_weekend) == 0x2a0);
static_assert(offsetof(Season, standings_vtable) == 0x2a8);
static_assert(offsetof(Season, completion_state) == 0x538);
static_assert(sizeof(Season) == 0x550);

struct Session {
    uint8_t _pad_0x0[0x8];
    uint32_t session_id; // L2 experimental
    uint8_t _pad_0xc[0x4];
    uint32_t completion; // L2 experimental
    uint8_t _pad_0x14[0x4];
    NeAllocator *participants_allocator; // L2 experimental
    EntrantRecord *participants_begin; // L2 experimental
    EntrantRecord *participants_end; // L2 experimental
    uint64_t participants_capacity; // L2 experimental
    uint8_t _pad_0x38[0x68];
};
static_assert(offsetof(Session, session_id) == 0x8);
static_assert(offsetof(Session, completion) == 0x10);
static_assert(offsetof(Session, participants_allocator) == 0x18);
static_assert(offsetof(Session, participants_begin) == 0x20);
static_assert(offsetof(Session, participants_end) == 0x28);
static_assert(offsetof(Session, participants_capacity) == 0x30);
static_assert(sizeof(Session) == 0xa0);

struct SponsorEntry {
    uint8_t _pad_0x0[0x10];
    const char *name; // L2 experimental
    uint8_t _pad_0x18[0x10];
    uint16_t stat_id; // L2 experimental
    uint8_t _pad_0x2a[0x6e];
    SponsorLoyaltyLevel *levels_begin; // L2 experimental
    SponsorLoyaltyLevel *levels_end; // L2 experimental
    uint8_t _pad_0xa8[0x30];
    uint8_t slot; // L2 experimental
    uint8_t _pad_0xd9[0x3b];
    int32_t loyalty_xp; // L2 experimental
    uint8_t _pad_0x118[0x40];
    uint8_t active; // L2 experimental
    uint8_t _pad_0x159[0x7];
};
static_assert(offsetof(SponsorEntry, name) == 0x10);
static_assert(offsetof(SponsorEntry, stat_id) == 0x28);
static_assert(offsetof(SponsorEntry, levels_begin) == 0x98);
static_assert(offsetof(SponsorEntry, levels_end) == 0xa0);
static_assert(offsetof(SponsorEntry, slot) == 0xd8);
static_assert(offsetof(SponsorEntry, loyalty_xp) == 0x114);
static_assert(offsetof(SponsorEntry, active) == 0x158);
static_assert(sizeof(SponsorEntry) == 0x160);

struct SponsorLoyaltyLevel {
    uint8_t claimed; // L2 experimental
    uint8_t _pad_0x1[0x3];
    int32_t xp_threshold; // L2 experimental
    uint8_t _pad_0x8[0x70];
};
static_assert(offsetof(SponsorLoyaltyLevel, claimed) == 0x0);
static_assert(offsetof(SponsorLoyaltyLevel, xp_threshold) == 0x4);
static_assert(sizeof(SponsorLoyaltyLevel) == 0x78);

struct SponsorLoyaltyXpMessage {
    void **vtable; // L2 experimental
    uint8_t _pad_0x8[0x8];
    uint32_t team; // L2 experimental
    int32_t amount; // L2 experimental
    uint8_t reason; // L2 experimental
};
static_assert(offsetof(SponsorLoyaltyXpMessage, vtable) == 0x0);
static_assert(offsetof(SponsorLoyaltyXpMessage, team) == 0x10);
static_assert(offsetof(SponsorLoyaltyXpMessage, amount) == 0x14);
static_assert(offsetof(SponsorLoyaltyXpMessage, reason) == 0x18);

struct SponsorTeamRecord {
    uint8_t _pad_0x0[0x8];
    uint32_t team; // L2 experimental
    uint8_t _pad_0xc[0xc];
    SponsorEntry *sponsors_begin; // L2 experimental
    SponsorEntry *sponsors_end; // L2 experimental
    uint8_t _pad_0x28[0x60];
};
static_assert(offsetof(SponsorTeamRecord, team) == 0x8);
static_assert(offsetof(SponsorTeamRecord, sponsors_begin) == 0x18);
static_assert(offsetof(SponsorTeamRecord, sponsors_end) == 0x20);
static_assert(sizeof(SponsorTeamRecord) == 0x88);

struct StandingsPoints {
    uint32_t key; // L2 experimental
    int32_t points; // L2 experimental
};
static_assert(offsetof(StandingsPoints, key) == 0x0);
static_assert(offsetof(StandingsPoints, points) == 0x4);
static_assert(sizeof(StandingsPoints) == 0x8);

struct StandingsResult {
    uint32_t key; // L2 experimental
    int32_t position; // L2 experimental
    int32_t fastest_lap_rank; // L2 experimental
    uint32_t status; // L2 experimental
};
static_assert(offsetof(StandingsResult, key) == 0x0);
static_assert(offsetof(StandingsResult, position) == 0x4);
static_assert(offsetof(StandingsResult, fastest_lap_rank) == 0x8);
static_assert(offsetof(StandingsResult, status) == 0xc);
static_assert(sizeof(StandingsResult) == 0x10);

struct StandingsRound {
    uint8_t _pad_0x0[0x8];
    StandingsSession *sessions_begin; // L2 experimental
    StandingsSession *sessions_end; // L2 experimental
    uint8_t _pad_0x18[0x8];
};
static_assert(offsetof(StandingsRound, sessions_begin) == 0x8);
static_assert(offsetof(StandingsRound, sessions_end) == 0x10);
static_assert(sizeof(StandingsRound) == 0x20);

struct StandingsSession {
    uint8_t _pad_0x0[0x8];
    void *records_begin; // L2 experimental
    void *records_end; // L2 experimental
    uint8_t _pad_0x18[0x8];
    uint32_t session_id; // L2 experimental
    uint8_t _pad_0x24[0x4];
};
static_assert(offsetof(StandingsSession, records_begin) == 0x8);
static_assert(offsetof(StandingsSession, records_end) == 0x10);
static_assert(offsetof(StandingsSession, session_id) == 0x20);
static_assert(sizeof(StandingsSession) == 0x28);

struct StatInfo {
    uint8_t _pad_0x0[0x8];
    uint16_t stat_id; // L2 experimental
    uint8_t _pad_0xa[0x126];
    float weight; // L2 experimental
    float default_value; // L2 experimental
};
static_assert(offsetof(StatInfo, stat_id) == 0x8);
static_assert(offsetof(StatInfo, weight) == 0x130);
static_assert(offsetof(StatInfo, default_value) == 0x134);

struct TeamRenownData {
    uint32_t team; // L2 experimental
    float points; // L2 experimental
    uint8_t _pad_0x8[0x10];
    RenownConfig *config; // L2 experimental
    uint8_t _pad_0x20[0x120];
};
static_assert(offsetof(TeamRenownData, team) == 0x0);
static_assert(offsetof(TeamRenownData, points) == 0x4);
static_assert(offsetof(TeamRenownData, config) == 0x18);
static_assert(sizeof(TeamRenownData) == 0x140);

struct UpgradeInstance {
    uint8_t _pad_0x0[0x10];
    char *name; // L2 experimental
    uint8_t _pad_0x18[0x40];
    uint32_t node_hash; // L2 experimental
    int32_t cost; // L2 experimental
    int32_t cost_to_develop; // L2 experimental
    uint8_t _pad_0x64[0x4];
    int32_t num_days_to_research; // L2 experimental
    int32_t num_days_to_develop; // L2 experimental
    uint8_t _pad_0x70[0x2];
    uint8_t department; // L2 experimental
    uint8_t _pad_0x73[0x1];
    int32_t unlock_at_facility_level; // L2 experimental
    uint8_t _pad_0x78[0x4];
    uint8_t row_flag; // L2 experimental
    uint8_t _pad_0x7d[0x1];
    uint16_t research_start_day; // L2 experimental
    uint16_t research_end_day; // L2 experimental
    uint8_t _pad_0x82[0x16];
    uint16_t develop_start_day; // L2 experimental
    uint16_t develop_end_day; // L2 experimental
    uint8_t _pad_0x9c[0x24];
    UpgradeStateEntry *states_begin; // L2 experimental
    UpgradeStateEntry *states_end; // L2 experimental
    uint8_t _pad_0xd0[0x10];
};
static_assert(offsetof(UpgradeInstance, name) == 0x10);
static_assert(offsetof(UpgradeInstance, node_hash) == 0x58);
static_assert(offsetof(UpgradeInstance, cost) == 0x5c);
static_assert(offsetof(UpgradeInstance, cost_to_develop) == 0x60);
static_assert(offsetof(UpgradeInstance, num_days_to_research) == 0x68);
static_assert(offsetof(UpgradeInstance, num_days_to_develop) == 0x6c);
static_assert(offsetof(UpgradeInstance, department) == 0x72);
static_assert(offsetof(UpgradeInstance, unlock_at_facility_level) == 0x74);
static_assert(offsetof(UpgradeInstance, row_flag) == 0x7c);
static_assert(offsetof(UpgradeInstance, research_start_day) == 0x7e);
static_assert(offsetof(UpgradeInstance, research_end_day) == 0x80);
static_assert(offsetof(UpgradeInstance, develop_start_day) == 0x98);
static_assert(offsetof(UpgradeInstance, develop_end_day) == 0x9a);
static_assert(offsetof(UpgradeInstance, states_begin) == 0xc0);
static_assert(offsetof(UpgradeInstance, states_end) == 0xc8);
static_assert(sizeof(UpgradeInstance) == 0xe0);

struct UpgradeStateEntry {
    uint32_t owner; // L2 experimental
    uint16_t day; // L2 experimental
    uint8_t _pad_0x6[0x2];
    uint8_t state; // L2 experimental
    uint8_t adaptation; // L2 experimental
    uint8_t _pad_0xa[0x2];
    uint8_t enabled; // L2 experimental
    uint8_t facility_locked; // L2 experimental
    uint8_t _pad_0xe[0x2];
};
static_assert(offsetof(UpgradeStateEntry, owner) == 0x0);
static_assert(offsetof(UpgradeStateEntry, day) == 0x4);
static_assert(offsetof(UpgradeStateEntry, state) == 0x8);
static_assert(offsetof(UpgradeStateEntry, adaptation) == 0x9);
static_assert(offsetof(UpgradeStateEntry, enabled) == 0xc);
static_assert(offsetof(UpgradeStateEntry, facility_locked) == 0xd);
static_assert(sizeof(UpgradeStateEntry) == 0x10);

struct UpgradeTree {
    uint8_t _pad_0x0[0x10];
    UpgradeInstance *instances_begin; // L2 experimental
    UpgradeInstance *instances_end; // L2 experimental
    uint8_t _pad_0x20[0xc20];
};
static_assert(offsetof(UpgradeTree, instances_begin) == 0x10);
static_assert(offsetof(UpgradeTree, instances_end) == 0x18);
static_assert(sizeof(UpgradeTree) == 0xc40);

struct Upgrades {
    uint8_t _pad_0x0[0x8];
    uint8_t mode; // L2 experimental
    uint8_t _pad_0x9[0x3];
    uint32_t team_key; // L2 experimental
    uint8_t _pad_0x10[0xd0];
    UpgradeTree trees[2]; // L2 experimental
    uint64_t tree_count; // L2 experimental
    uint8_t _pad_0x1968[0x140];
    uint32_t owner_cache; // L2 experimental
    uint8_t _pad_0x1aac[0x64];
};
static_assert(offsetof(Upgrades, mode) == 0x8);
static_assert(offsetof(Upgrades, team_key) == 0xc);
static_assert(offsetof(Upgrades, trees) == 0xe0);
static_assert(offsetof(Upgrades, tree_count) == 0x1960);
static_assert(offsetof(Upgrades, owner_cache) == 0x1aa8);
static_assert(sizeof(Upgrades) == 0x1b10);

struct UpgradesComponent {
    uint8_t _pad_0x0[0x298];
    Upgrades **upgrades_begin; // L2 experimental
    Upgrades **upgrades_end; // L2 experimental
    uint8_t _pad_0x2a8[0x18];
    CareerTimeComponent *career_time; // L2 experimental
};
static_assert(offsetof(UpgradesComponent, upgrades_begin) == 0x298);
static_assert(offsetof(UpgradesComponent, upgrades_end) == 0x2a0);
static_assert(offsetof(UpgradesComponent, career_time) == 0x2c0);

struct Weekend {
    void **vtable; // L2 experimental
    uint8_t _pad_0x8[0x20];
    char *name; // L2 experimental
    uint8_t _pad_0x30[0x1f0];
    Session *sessions_begin; // L2 experimental
    Session *sessions_end; // L2 experimental
    uint8_t _pad_0x230[0x8];
    uint32_t current_session; // L2 experimental
    uint32_t progress_state; // L2 experimental
    uint8_t _pad_0x240[0x38];
    uint8_t track_stat_id; // L2 experimental
    uint8_t _pad_0x279[0x277];
};
static_assert(offsetof(Weekend, vtable) == 0x0);
static_assert(offsetof(Weekend, name) == 0x28);
static_assert(offsetof(Weekend, sessions_begin) == 0x220);
static_assert(offsetof(Weekend, sessions_end) == 0x228);
static_assert(offsetof(Weekend, current_session) == 0x238);
static_assert(offsetof(Weekend, progress_state) == 0x23c);
static_assert(offsetof(Weekend, track_stat_id) == 0x278);
static_assert(sizeof(Weekend) == 0x4f0);

using Career_Add_SeasonWeekend_t = uint64_t(void *season, void *weekend);
inline constexpr fn<"Career_Add_SeasonWeekend", Career_Add_SeasonWeekend_t> Career_Add_SeasonWeekend{}; // L2 experimental
using Career_Add_TeamModifier_t = void(void *list, uint32_t key, float value);
inline constexpr fn<"Career_Add_TeamModifier", Career_Add_TeamModifier_t> Career_Add_TeamModifier{}; // L2 experimental
using Career_Adjust_RpDriverWallet_t = void(void *wallets, uint32_t driver_key, int32_t delta);
inline constexpr fn<"Career_Adjust_RpDriverWallet", Career_Adjust_RpDriverWallet_t> Career_Adjust_RpDriverWallet{}; // L2 experimental
using Career_Adjust_RpTeamWallet_t = void(void *wallets, uint32_t team_key, int32_t delta);
inline constexpr fn<"Career_Adjust_RpTeamWallet", Career_Adjust_RpTeamWallet_t> Career_Adjust_RpTeamWallet{}; // L2 experimental
using Career_Build_DriverContract_t = void *(void *contracts, void *contract, uint32_t driver_key, uint32_t team_key, uint8_t type, int32_t target, int32_t seasons);
inline constexpr fn<"Career_Build_DriverContract", Career_Build_DriverContract_t> Career_Build_DriverContract{}; // L2 experimental
using Career_Build_LineupChanges_t = void *(void *out, const void *current_entrants, const void *baseline_entrants);
inline constexpr fn<"Career_Build_LineupChanges", Career_Build_LineupChanges_t> Career_Build_LineupChanges{}; // L2 experimental
using Career_Calc_UpgradeCost_t = int32_t(void *list, void *inst, void *mods, uint32_t option);
inline constexpr fn<"Career_Calc_UpgradeCost", Career_Calc_UpgradeCost_t> Career_Calc_UpgradeCost{}; // L2 experimental
using Career_Cancel_UpgradeDevelopment_t = void(void *upgrades, uint32_t owner, void *inst, uint32_t day);
inline constexpr fn<"Career_Cancel_UpgradeDevelopment", Career_Cancel_UpgradeDevelopment_t> Career_Cancel_UpgradeDevelopment{}; // L2 experimental
using Career_Cancel_UpgradeResearch_t = void(void *upgrades, uint32_t owner, void *inst, uint32_t day);
inline constexpr fn<"Career_Cancel_UpgradeResearch", Career_Cancel_UpgradeResearch_t> Career_Cancel_UpgradeResearch{}; // L2 experimental
using Career_Complete_PerkTreeNode_t = uint8_t(void *node);
inline constexpr fn<"Career_Complete_PerkTreeNode", Career_Complete_PerkTreeNode_t> Career_Complete_PerkTreeNode{}; // L2 experimental
using Career_Construct_RnDDeptCard_t = void *(void *card, void *ctx, void *builder, void *allocator, const char *name, const char *icon, uint8_t dept);
inline constexpr fn<"Career_Construct_RnDDeptCard", Career_Construct_RnDDeptCard_t> Career_Construct_RnDDeptCard{}; // L2 experimental
using Career_ContractIntro_OnEnter_t = uint64_t(void *state);
inline constexpr fn<"Career_ContractIntro_OnEnter", Career_ContractIntro_OnEnter_t> Career_ContractIntro_OnEnter{}; // L2 experimental
using Career_ContractIntro_OnUpdate_t = uint32_t *(void *state, uint32_t *link);
inline constexpr fn<"Career_ContractIntro_OnUpdate", Career_ContractIntro_OnUpdate_t> Career_ContractIntro_OnUpdate{}; // L2 experimental
using Career_ContractOutro_OnEnter_t = uint64_t(void *state);
inline constexpr fn<"Career_ContractOutro_OnEnter", Career_ContractOutro_OnEnter_t> Career_ContractOutro_OnEnter{}; // L2 experimental
using Career_ContractOutro_OnUpdate_t = uint32_t *(void *state, uint32_t *link);
inline constexpr fn<"Career_ContractOutro_OnUpdate", Career_ContractOutro_OnUpdate_t> Career_ContractOutro_OnUpdate{}; // L2 experimental
using Career_ContractUpdate_OnEnter_t = uint64_t(void *state);
inline constexpr fn<"Career_ContractUpdate_OnEnter", Career_ContractUpdate_OnEnter_t> Career_ContractUpdate_OnEnter{}; // L2 experimental
using Career_ContractUpdate_OnExit_t = uint64_t(void *state);
inline constexpr fn<"Career_ContractUpdate_OnExit", Career_ContractUpdate_OnExit_t> Career_ContractUpdate_OnExit{}; // L2 experimental
using Career_ContractUpdate_OnUpdate_t = uint32_t *(void *state, uint32_t *link);
inline constexpr fn<"Career_ContractUpdate_OnUpdate", Career_ContractUpdate_OnUpdate_t> Career_ContractUpdate_OnUpdate{}; // L2 experimental
using Career_Create_SeasonWeekend_t = void *(void *season, uint64_t index, uint8_t track, const char *name);
inline constexpr fn<"Career_Create_SeasonWeekend", Career_Create_SeasonWeekend_t> Career_Create_SeasonWeekend{}; // L2 experimental
using Career_Decide_ContractNegotiation_t = uint32_t *(void *state, uint32_t *link);
inline constexpr fn<"Career_Decide_ContractNegotiation", Career_Decide_ContractNegotiation_t> Career_Decide_ContractNegotiation{}; // L2 experimental
using Career_DriverNumbers_CheckExisting_t = void(void *car_numbers, uint32_t driver);
inline constexpr fn<"Career_DriverNumbers_CheckExisting", Career_DriverNumbers_CheckExisting_t> Career_DriverNumbers_CheckExisting{}; // L2 experimental
using Career_Eval_Curve_t = float(void *curve, float x);
inline constexpr fn<"Career_Eval_Curve", Career_Eval_Curve_t> Career_Eval_Curve{}; // L2 experimental
using Career_Get_CareerLengthDays_Thunk_t = uint32_t(void *time);
inline constexpr fn<"Career_Get_CareerLengthDays_Thunk", Career_Get_CareerLengthDays_Thunk_t> Career_Get_CareerLengthDays_Thunk{}; // L2 experimental
using Career_Get_DriverOverallRating_t = float(void *self, void *data, const uint32_t *driver, bool round);
inline constexpr fn<"Career_Get_DriverOverallRating", Career_Get_DriverOverallRating_t> Career_Get_DriverOverallRating{}; // L2 experimental, game::championship::DriverStatisticsComponent
using Career_Get_RpWallet_t = void(void *wallets, uint64_t *out, uint32_t driver_key);
inline constexpr fn<"Career_Get_RpWallet", Career_Get_RpWallet_t> Career_Get_RpWallet{}; // L2 experimental
using Career_Get_TeamRenownData_t = void *(void *rf, void *out_weak, uint32_t team_key);
inline constexpr fn<"Career_Get_TeamRenownData", Career_Get_TeamRenownData_t> Career_Get_TeamRenownData{}; // L2 experimental
using Career_Needs_DriverContract_t = bool(void *contracts);
inline constexpr fn<"Career_Needs_DriverContract", Career_Needs_DriverContract_t> Career_Needs_DriverContract{}; // L2 experimental
using Career_Post_UpgradeFittedRatio_t = void(void *upgrades, void *inst, uint8_t fitted);
inline constexpr fn<"Career_Post_UpgradeFittedRatio", Career_Post_UpgradeFittedRatio_t> Career_Post_UpgradeFittedRatio{}; // L2 experimental
using Career_Rebuild_ChampionshipStandings_t = void(void *champ_list, void *extra_points, void *extra_e);
inline constexpr fn<"Career_Rebuild_ChampionshipStandings", Career_Rebuild_ChampionshipStandings_t> Career_Rebuild_ChampionshipStandings{}; // L2 experimental
using Career_Rebuild_SeasonStandings_t = void(void *season, void *order_a, void *order_b, void *extra_points, void *extra_e);
inline constexpr fn<"Career_Rebuild_SeasonStandings", Career_Rebuild_SeasonStandings_t> Career_Rebuild_SeasonStandings{}; // L2 experimental
using Career_Record_DriverMove_t = void(void *renewal, uint32_t driver, uint32_t old_team, uint32_t new_team, uint8_t reason);
inline constexpr fn<"Career_Record_DriverMove", Career_Record_DriverMove_t> Career_Record_DriverMove{}; // L2 experimental
using Career_Refresh_RnDDeptMode_t = void(uintptr_t *holder, uint32_t mode);
inline constexpr fn<"Career_Refresh_RnDDeptMode", Career_Refresh_RnDDeptMode_t> Career_Refresh_RnDDeptMode{}; // L2 experimental
using Career_Refresh_UpgradeAvailability_t = void(void *upgrades, uint16_t day);
inline constexpr fn<"Career_Refresh_UpgradeAvailability", Career_Refresh_UpgradeAvailability_t> Career_Refresh_UpgradeAvailability{}; // L2 experimental
using Career_Set_DriverContractState_t = void(void *contract, uint16_t day, uint8_t state, uint32_t value);
inline constexpr fn<"Career_Set_DriverContractState", Career_Set_DriverContractState_t> Career_Set_DriverContractState{}; // L2 experimental
using Career_Set_EntrantData_Thunk_t = void(void *entity, const void *entrant_id, const uint32_t *data, uint16_t position, uint8_t flag);
inline constexpr fn<"Career_Set_EntrantData_Thunk", Career_Set_EntrantData_Thunk_t> Career_Set_EntrantData_Thunk{}; // L2 experimental
using Career_Set_UpgradeFacilityLocked_t = void(void *inst, uint8_t locked, uint16_t day);
inline constexpr fn<"Career_Set_UpgradeFacilityLocked", Career_Set_UpgradeFacilityLocked_t> Career_Set_UpgradeFacilityLocked{}; // L2 experimental
using Career_Set_UpgradesOwner_t = void(void *upgrades, uint32_t owner);
inline constexpr fn<"Career_Set_UpgradesOwner", Career_Set_UpgradesOwner_t> Career_Set_UpgradesOwner{}; // L2 experimental
using Career_Sign_DriverContract_t = void(void *contracts, void *contract, uint64_t driver_key, uint32_t replaced_key, uint8_t notify);
inline constexpr fn<"Career_Sign_DriverContract", Career_Sign_DriverContract_t> Career_Sign_DriverContract{}; // L2 experimental
using Career_Start_UpgradeDevelopment_t = void(void *upgrades, uint32_t owner, void *inst, uint16_t start_day, uint16_t end_day, uint8_t option, uint8_t dual, uint8_t roll_failure, void *rng, uint8_t store_duration);
inline constexpr fn<"Career_Start_UpgradeDevelopment", Career_Start_UpgradeDevelopment_t> Career_Start_UpgradeDevelopment{}; // L2 experimental
using Career_Start_UpgradeResearch_t = void(void *upgrades, uint32_t owner, void *inst, uint32_t start_day, uint16_t end_day, uint8_t option, uint8_t roll_failure, void *rng, uint8_t store_duration);
inline constexpr fn<"Career_Start_UpgradeResearch", Career_Start_UpgradeResearch_t> Career_Start_UpgradeResearch{}; // L2 experimental
using Career_Teardown_Weekend_t = void(void *weekend);
inline constexpr fn<"Career_Teardown_Weekend", Career_Teardown_Weekend_t> Career_Teardown_Weekend{}; // L2 experimental
inline constexpr data<"g_Career_FlowLinkContinue", uint32_t> g_Career_FlowLinkContinue{}; // L2 experimental
inline constexpr data<"g_Career_RnDOverviewSelectedDept", uint8_t> g_Career_RnDOverviewSelectedDept{}; // L2 experimental
inline constexpr data<"g_Career_UpgradeLifecycle", uint8_t> g_Career_UpgradeLifecycle{}; // L2 experimental
inline constexpr data<"k_Career_CareerBankComponentDesc", void *> k_Career_CareerBankComponentDesc{}; // L2 experimental
inline constexpr data<"k_Career_CareerComponentDesc", void *> k_Career_CareerComponentDesc{}; // L2 experimental
inline constexpr data<"k_Career_CareerDriverContractRenewalComponentDesc", void *> k_Career_CareerDriverContractRenewalComponentDesc{}; // L2 experimental
inline constexpr data<"k_Career_CareerDriverMarketComponentDesc", void *> k_Career_CareerDriverMarketComponentDesc{}; // L2 experimental
inline constexpr data<"k_Career_CareerFacilitiesComponentDesc", void *> k_Career_CareerFacilitiesComponentDesc{}; // L2 experimental
inline constexpr data<"k_Career_CareerSetupComponentDesc", void *> k_Career_CareerSetupComponentDesc{}; // L2 experimental
inline constexpr data<"k_Career_CareerSponsorsComponentDesc", void *> k_Career_CareerSponsorsComponentDesc{}; // L2 experimental
inline constexpr data<"k_Career_CareerTimeComponentDesc", void *> k_Career_CareerTimeComponentDesc{}; // L2 experimental
inline constexpr data<"k_Career_ChampionshipComponentDesc", void *> k_Career_ChampionshipComponentDesc{}; // L2 experimental
inline constexpr data<"k_Career_ChampionshipDesc", void *> k_Career_ChampionshipDesc{}; // L2 experimental
inline constexpr data<"k_Career_CostCapComponentDesc", void *> k_Career_CostCapComponentDesc{}; // L2 experimental
inline constexpr data<"k_Career_DriverCareerContractsManagerComponentDesc", void *> k_Career_DriverCareerContractsManagerComponentDesc{}; // L2 experimental
inline constexpr data<"k_Career_DriverCareerPlayerDriverStatisticsComponentDesc", void *> k_Career_DriverCareerPlayerDriverStatisticsComponentDesc{}; // L2 experimental
inline constexpr data<"k_Career_DriverContractsMyTeamComponentDesc", void *> k_Career_DriverContractsMyTeamComponentDesc{}; // L2 experimental
inline constexpr data<"k_Career_DriverStatisticsComponentDesc", void *> k_Career_DriverStatisticsComponentDesc{}; // L2 experimental
inline constexpr data<"k_Career_EngineSuppliersComponentDesc", void *> k_Career_EngineSuppliersComponentDesc{}; // L2 experimental
inline constexpr data<"k_Career_MyTeamAttributesComponentDesc", void *> k_Career_MyTeamAttributesComponentDesc{}; // L2 experimental
inline constexpr data<"k_Career_MyTeamFacilitiesComponentDesc", void *> k_Career_MyTeamFacilitiesComponentDesc{}; // L2 experimental
inline constexpr data<"k_Career_MyTeamRFComponentDesc", void *> k_Career_MyTeamRFComponentDesc{}; // L2 experimental
inline constexpr data<"k_Career_MyTeamStatsComponentDesc", void *> k_Career_MyTeamStatsComponentDesc{}; // L2 experimental
inline constexpr data<"k_Career_OwnerPerksComponentDesc", void *> k_Career_OwnerPerksComponentDesc{}; // L2 experimental
inline constexpr data<"k_Career_OwnerSetupComponentDesc", void *> k_Career_OwnerSetupComponentDesc{}; // L2 experimental
inline constexpr data<"k_Career_ResourcePointsComponentDesc", void *> k_Career_ResourcePointsComponentDesc{}; // L2 experimental
inline constexpr data<"k_Career_RnDCardColourTable", uint32_t[6]> k_Career_RnDCardColourTable{}; // L2 experimental
inline constexpr data<"k_Career_RnDDeptColourTable", uint32_t[6]> k_Career_RnDDeptColourTable{}; // L2 experimental
inline constexpr data<"k_Career_RnDErrorDeptName", char[17]> k_Career_RnDErrorDeptName{}; // L2 experimental
inline constexpr data<"k_Career_ScheduleComponentDesc", void *> k_Career_ScheduleComponentDesc{}; // L2 experimental
inline constexpr data<"k_Career_TeamHQControllerComponentDesc", void *> k_Career_TeamHQControllerComponentDesc{}; // L2 experimental
inline constexpr data<"k_Career_UpgradeBlockNameTable", int32_t[18]> k_Career_UpgradeBlockNameTable{}; // L2 experimental
inline constexpr data<"k_Career_UpgradeTypeOrder", uint8_t[19]> k_Career_UpgradeTypeOrder{}; // L2 experimental
inline constexpr data<"k_Career_UpgradesComponentDesc", void *> k_Career_UpgradesComponentDesc{}; // L2 experimental
inline constexpr data<"k_Career_WeatherAndTimeOfDayComponentDesc", void *> k_Career_WeatherAndTimeOfDayComponentDesc{}; // L2 experimental
inline constexpr site<"site_Career_ApplyUpgradeBlockNameDefault"> site_Career_ApplyUpgradeBlockNameDefault{}; // L2 experimental
inline constexpr site<"site_Career_ApplyUpgradeBlockNameJoin"> site_Career_ApplyUpgradeBlockNameJoin{}; // L2 experimental
inline constexpr site<"site_Career_ApplyUpgradeKeyCall"> site_Career_ApplyUpgradeKeyCall{}; // L2 experimental
inline constexpr site<"site_Career_ApplyUpgradeKeyFilter"> site_Career_ApplyUpgradeKeyFilter{}; // L2 experimental
inline constexpr site<"site_Career_DeserialiseUpgradesEnd"> site_Career_DeserialiseUpgradesEnd{}; // L2 experimental
inline constexpr site<"site_Career_LoadUpgradeRows_DeptPath"> site_Career_LoadUpgradeRows_DeptPath{}; // L2 experimental
inline constexpr site<"site_Career_RnDBoxGridFirstTab"> site_Career_RnDBoxGridFirstTab{}; // L2 experimental
inline constexpr site<"site_Career_RnDBoxGridNameClamp"> site_Career_RnDBoxGridNameClamp{}; // L2 experimental
inline constexpr site<"site_Career_RnDBoxGridNameIndex"> site_Career_RnDBoxGridNameIndex{}; // L2 experimental
inline constexpr site<"site_Career_RnDBoxGridNameRead"> site_Career_RnDBoxGridNameRead{}; // L2 experimental
inline constexpr site<"site_Career_RnDCardArtImage0"> site_Career_RnDCardArtImage0{}; // L2 experimental
inline constexpr site<"site_Career_RnDCardArtImage1"> site_Career_RnDCardArtImage1{}; // L2 experimental
inline constexpr site<"site_Career_RnDCardArtImage2"> site_Career_RnDCardArtImage2{}; // L2 experimental
inline constexpr site<"site_Career_RnDCardArtMask0"> site_Career_RnDCardArtMask0{}; // L2 experimental
inline constexpr site<"site_Career_RnDCardArtMask1"> site_Career_RnDCardArtMask1{}; // L2 experimental
inline constexpr site<"site_Career_RnDCardArtMask2"> site_Career_RnDCardArtMask2{}; // L2 experimental
inline constexpr site<"site_Career_RnDCardColourPick0"> site_Career_RnDCardColourPick0{}; // L2 experimental
inline constexpr site<"site_Career_RnDCardColourPick1"> site_Career_RnDCardColourPick1{}; // L2 experimental
inline constexpr site<"site_Career_RnDCardColourPick2"> site_Career_RnDCardColourPick2{}; // L2 experimental
inline constexpr site<"site_Career_RnDCardColourPick3"> site_Career_RnDCardColourPick3{}; // L2 experimental
inline constexpr site<"site_Career_RnDCardColourPick4"> site_Career_RnDCardColourPick4{}; // L2 experimental
inline constexpr site<"site_Career_RnDCardDeptIcon0"> site_Career_RnDCardDeptIcon0{}; // L2 experimental
inline constexpr site<"site_Career_RnDCardDeptIcon1"> site_Career_RnDCardDeptIcon1{}; // L2 experimental
inline constexpr site<"site_Career_RnDCardDeptIcon2"> site_Career_RnDCardDeptIcon2{}; // L2 experimental
inline constexpr site<"site_Career_RnDCompareDept"> site_Career_RnDCompareDept{}; // L2 experimental
inline constexpr site<"site_Career_RnDContainerOpenMaint"> site_Career_RnDContainerOpenMaint{}; // L2 experimental
inline constexpr site<"site_Career_RnDContainerOpenTail"> site_Career_RnDContainerOpenTail{}; // L2 experimental
inline constexpr site<"site_Career_RnDDeptBoxGridEntry"> site_Career_RnDDeptBoxGridEntry{}; // L2 experimental
inline constexpr site<"site_Career_RnDDeptBoxGridInject"> site_Career_RnDDeptBoxGridInject{}; // L2 experimental
inline constexpr site<"site_Career_RnDDeptBoxLayoutProbe"> site_Career_RnDDeptBoxLayoutProbe{}; // L2 experimental
inline constexpr site<"site_Career_RnDDeptPagePreQuery"> site_Career_RnDDeptPagePreQuery{}; // L2 experimental
inline constexpr site<"site_Career_RnDDeptPageQueryDone"> site_Career_RnDDeptPageQueryDone{}; // L2 experimental
inline constexpr site<"site_Career_RnDDeptPageTypeLoop"> site_Career_RnDDeptPageTypeLoop{}; // L2 experimental
inline constexpr site<"site_Career_RnDDeptRowCategoryIcon"> site_Career_RnDDeptRowCategoryIcon{}; // L2 experimental
inline constexpr site<"site_Career_RnDDeptRowTypeIcon"> site_Career_RnDDeptRowTypeIcon{}; // L2 experimental
inline constexpr site<"site_Career_RnDDeptRowTypeIconJoin"> site_Career_RnDDeptRowTypeIconJoin{}; // L2 experimental
inline constexpr site<"site_Career_RnDDeptRowTypeLabel"> site_Career_RnDDeptRowTypeLabel{}; // L2 experimental
inline constexpr site<"site_Career_RnDDeptRowTypeLabelJoin"> site_Career_RnDDeptRowTypeLabelJoin{}; // L2 experimental
inline constexpr site<"site_Career_RnDInProgressColourStore"> site_Career_RnDInProgressColourStore{}; // L2 experimental
inline constexpr site<"site_Career_RnDInProgressPartIcon"> site_Career_RnDInProgressPartIcon{}; // L2 experimental
inline constexpr site<"site_Career_RnDInProgressStrip"> site_Career_RnDInProgressStrip{}; // L2 experimental
inline constexpr site<"site_Career_RnDInProgressTypeIcon"> site_Career_RnDInProgressTypeIcon{}; // L2 experimental
inline constexpr site<"site_Career_RnDInProgressTypeIconJoin"> site_Career_RnDInProgressTypeIconJoin{}; // L2 experimental
inline constexpr site<"site_Career_RnDInProgressTypeLabel"> site_Career_RnDInProgressTypeLabel{}; // L2 experimental
inline constexpr site<"site_Career_RnDInProgressTypeLabelJoin"> site_Career_RnDInProgressTypeLabelJoin{}; // L2 experimental
inline constexpr site<"site_Career_RnDListRowColourStore"> site_Career_RnDListRowColourStore{}; // L2 experimental
inline constexpr site<"site_Career_RnDListRowDeptIcon"> site_Career_RnDListRowDeptIcon{}; // L2 experimental
inline constexpr site<"site_Career_RnDListRowLightColourStore"> site_Career_RnDListRowLightColourStore{}; // L2 experimental
inline constexpr site<"site_Career_RnDPageModifierList"> site_Career_RnDPageModifierList{}; // L2 experimental
inline constexpr site<"site_Career_RnDPanelModifierList"> site_Career_RnDPanelModifierList{}; // L2 experimental
inline constexpr site<"site_Career_RnDPanelResearchLimit"> site_Career_RnDPanelResearchLimit{}; // L2 experimental
inline constexpr site<"site_Career_RnDPerfChildName"> site_Career_RnDPerfChildName{}; // L2 experimental
inline constexpr site<"site_Career_RnDPerfDeptColour"> site_Career_RnDPerfDeptColour{}; // L2 experimental
inline constexpr site<"site_Career_RnDPerfDeptLoopEnd"> site_Career_RnDPerfDeptLoopEnd{}; // L2 experimental
inline constexpr site<"site_Career_RnDPerfDeptName"> site_Career_RnDPerfDeptName{}; // L2 experimental
inline constexpr site<"site_Career_RnDPerfGotoName"> site_Career_RnDPerfGotoName{}; // L2 experimental
inline constexpr call<"site_Career_RnDPerfViewModelSlotDtor"> site_Career_RnDPerfViewModelSlotDtor{}; // L2 experimental
inline constexpr site<"site_Career_RnDPerksTitleDeptIcon"> site_Career_RnDPerksTitleDeptIcon{}; // L2 experimental
inline constexpr site<"site_Career_RnDPieBufSize"> site_Career_RnDPieBufSize{}; // L2 experimental
inline constexpr site<"site_Career_RnDPieCtorRet"> site_Career_RnDPieCtorRet{}; // L2 experimental
inline constexpr site<"site_Career_RnDPieFocusJoin"> site_Career_RnDPieFocusJoin{}; // L2 experimental
inline constexpr site<"site_Career_RnDPieFocusNameCmp"> site_Career_RnDPieFocusNameCmp{}; // L2 experimental
inline constexpr site<"site_Career_RnDPieFocusRestore"> site_Career_RnDPieFocusRestore{}; // L2 experimental
inline constexpr call<"site_Career_RnDPieMoveAssignSlotDtor"> site_Career_RnDPieMoveAssignSlotDtor{}; // L2 experimental
inline constexpr call<"site_Career_RnDPieMovedFromSlotDtor"> site_Career_RnDPieMovedFromSlotDtor{}; // L2 experimental
inline constexpr site<"site_Career_RnDPiePopulateDeptGuard"> site_Career_RnDPiePopulateDeptGuard{}; // L2 experimental
inline constexpr site<"site_Career_RnDPiePopulateGate"> site_Career_RnDPiePopulateGate{}; // L2 experimental
inline constexpr site<"site_Career_RnDPiePopulateNextCode"> site_Career_RnDPiePopulateNextCode{}; // L2 experimental
inline constexpr site<"site_Career_RnDPiePopulateSlotCap"> site_Career_RnDPiePopulateSlotCap{}; // L2 experimental
inline constexpr site<"site_Career_RnDPieSliceDefaultColour"> site_Career_RnDPieSliceDefaultColour{}; // L2 experimental
inline constexpr call<"site_Career_RnDPieSliceDescFormat"> site_Career_RnDPieSliceDescFormat{}; // L2 experimental
inline constexpr site<"site_Career_RnDPieSliceIconArgs"> site_Career_RnDPieSliceIconArgs{}; // L2 experimental
inline constexpr site<"site_Career_RnDPieSliceNameJoin"> site_Career_RnDPieSliceNameJoin{}; // L2 experimental
inline constexpr call<"site_Career_RnDPieTeardownSlotDtor"> site_Career_RnDPieTeardownSlotDtor{}; // L2 experimental
inline constexpr site<"site_Career_RnDPopupCategoryIcon"> site_Career_RnDPopupCategoryIcon{}; // L2 experimental
inline constexpr site<"site_Career_RnDPopupColourStore"> site_Career_RnDPopupColourStore{}; // L2 experimental
inline constexpr site<"site_Career_RnDPotentialPieEntry"> site_Career_RnDPotentialPieEntry{}; // L2 experimental
inline constexpr site<"site_Career_RnDPurchaseBackOpen"> site_Career_RnDPurchaseBackOpen{}; // L2 experimental
inline constexpr site<"site_Career_RnDPurchaseOptionsEntry"> site_Career_RnDPurchaseOptionsEntry{}; // L2 experimental
inline constexpr site<"site_Career_RnDPurchasePanelTypeLabel"> site_Career_RnDPurchasePanelTypeLabel{}; // L2 experimental
inline constexpr site<"site_Career_RnDPurchasePanelTypeLabelJoin"> site_Career_RnDPurchasePanelTypeLabelJoin{}; // L2 experimental
inline constexpr site<"site_Career_RnDPurchaseRequestEntry"> site_Career_RnDPurchaseRequestEntry{}; // L2 experimental
inline constexpr site<"site_Career_RnDPurchaseScreenTypeLabel"> site_Career_RnDPurchaseScreenTypeLabel{}; // L2 experimental
inline constexpr site<"site_Career_RnDPurchaseScreenTypeLabelJoin"> site_Career_RnDPurchaseScreenTypeLabelJoin{}; // L2 experimental
inline constexpr site<"site_Career_RnDResearchFocusNameCmp"> site_Career_RnDResearchFocusNameCmp{}; // L2 experimental
inline constexpr site<"site_Career_RnDResearchRowCategoryIcon"> site_Career_RnDResearchRowCategoryIcon{}; // L2 experimental
inline constexpr site<"site_Career_RnDResearchRowTypeIcon"> site_Career_RnDResearchRowTypeIcon{}; // L2 experimental
inline constexpr site<"site_Career_RnDResearchRowTypeIconJoin"> site_Career_RnDResearchRowTypeIconJoin{}; // L2 experimental
inline constexpr site<"site_Career_RnDResearchRowTypeLabel"> site_Career_RnDResearchRowTypeLabel{}; // L2 experimental
inline constexpr site<"site_Career_RnDResearchRowTypeLabelJoin"> site_Career_RnDResearchRowTypeLabelJoin{}; // L2 experimental
inline constexpr site<"site_Career_RnDResearchTypeLoop"> site_Career_RnDResearchTypeLoop{}; // L2 experimental
inline constexpr site<"site_Career_RnDResultTypeIcon"> site_Career_RnDResultTypeIcon{}; // L2 experimental
inline constexpr site<"site_Career_RnDResultTypeIconJoin"> site_Career_RnDResultTypeIconJoin{}; // L2 experimental
inline constexpr site<"site_Career_RnDResultTypeLabel"> site_Career_RnDResultTypeLabel{}; // L2 experimental
inline constexpr site<"site_Career_RnDResultTypeLabelJoin"> site_Career_RnDResultTypeLabelJoin{}; // L2 experimental
inline constexpr site<"site_Career_RnDTileCountBody"> site_Career_RnDTileCountBody{}; // L2 experimental
inline constexpr site<"site_Career_RnDTileCountExit"> site_Career_RnDTileCountExit{}; // L2 experimental
inline constexpr site<"site_Career_RnDTileCountGuards"> site_Career_RnDTileCountGuards{}; // L2 experimental
inline constexpr site<"site_Career_RnDTileCountIndexGuard"> site_Career_RnDTileCountIndexGuard{}; // L2 experimental
inline constexpr site<"site_Career_RnDTodoTypeLabel"> site_Career_RnDTodoTypeLabel{}; // L2 experimental
inline constexpr site<"site_Career_RnDTodoTypeLabelJoin"> site_Career_RnDTodoTypeLabelJoin{}; // L2 experimental
inline constexpr site<"site_Career_RnDTreeRowCountProbe"> site_Career_RnDTreeRowCountProbe{}; // L2 experimental
inline constexpr site<"site_Career_RnDUpgradeInfoTypeLabel"> site_Career_RnDUpgradeInfoTypeLabel{}; // L2 experimental
inline constexpr site<"site_Career_RnDUpgradeInfoTypeLabelJoin"> site_Career_RnDUpgradeInfoTypeLabelJoin{}; // L2 experimental
inline constexpr site<"site_Career_RnDUpgradeListDeptIcon"> site_Career_RnDUpgradeListDeptIcon{}; // L2 experimental
inline constexpr site<"site_Career_TeamDeptEnumStore"> site_Career_TeamDeptEnumStore{}; // L2 experimental
inline constexpr site<"site_Career_TeamHqIsMyTeamResult"> site_Career_TeamHqIsMyTeamResult{}; // L2 experimental
inline constexpr site<"site_Career_TeamHqRnDPopupTypeIcon"> site_Career_TeamHqRnDPopupTypeIcon{}; // L2 experimental
inline constexpr site<"site_Career_TeamHqRnDPopupTypeIconJoin"> site_Career_TeamHqRnDPopupTypeIconJoin{}; // L2 experimental
inline constexpr site<"site_Career_TeleFailedTypeName"> site_Career_TeleFailedTypeName{}; // L2 experimental
inline constexpr site<"site_Career_TeleFailedTypeNameJoin"> site_Career_TeleFailedTypeNameJoin{}; // L2 experimental
inline constexpr site<"site_Career_TeleRecordTypeName"> site_Career_TeleRecordTypeName{}; // L2 experimental
inline constexpr call<"site_Career_TeleRecordTypeNameCall"> site_Career_TeleRecordTypeNameCall{}; // L2 experimental
inline constexpr site<"site_Career_TeleResearchTypeName"> site_Career_TeleResearchTypeName{}; // L2 experimental
inline constexpr call<"site_Career_TeleResearchTypeNameCall"> site_Career_TeleResearchTypeNameCall{}; // L2 experimental
inline constexpr site<"site_Career_Unk141814320TypeLabel"> site_Career_Unk141814320TypeLabel{}; // L2 experimental
inline constexpr site<"site_Career_Unk141814320TypeLabelJoin"> site_Career_Unk141814320TypeLabelJoin{}; // L2 experimental
inline constexpr site<"site_Career_Unk1418169f0TypeLabel"> site_Career_Unk1418169f0TypeLabel{}; // L2 experimental
inline constexpr site<"site_Career_Unk1418169f0TypeLabelJoin"> site_Career_Unk1418169f0TypeLabelJoin{}; // L2 experimental
inline constexpr site<"site_Career_UpgradeAutoFitBody"> site_Career_UpgradeAutoFitBody{}; // L2 experimental
inline constexpr site<"site_Career_UpgradeAutoFitDeptGate"> site_Career_UpgradeAutoFitDeptGate{}; // L2 experimental
inline constexpr site<"site_Career_UpgradeBlockReplayEntry"> site_Career_UpgradeBlockReplayEntry{}; // L2 experimental
inline constexpr site<"site_Career_UpgradeCardDeptIcon"> site_Career_UpgradeCardDeptIcon{}; // L2 experimental
inline constexpr site<"site_Career_UpgradeCardTypeLabel"> site_Career_UpgradeCardTypeLabel{}; // L2 experimental
inline constexpr site<"site_Career_UpgradeCardTypeLabelJoin"> site_Career_UpgradeCardTypeLabelJoin{}; // L2 experimental
inline constexpr site<"site_Career_UpgradeGraphHubTest"> site_Career_UpgradeGraphHubTest{}; // L2 experimental
inline constexpr site<"site_Career_UpgradeGraphRegularNode"> site_Career_UpgradeGraphRegularNode{}; // L2 experimental
inline constexpr site<"site_Career_UpgradeInitFakeDeptBranch"> site_Career_UpgradeInitFakeDeptBranch{}; // L2 experimental
inline constexpr site<"site_Career_UpgradeInitNormal"> site_Career_UpgradeInitNormal{}; // L2 experimental
inline constexpr site<"site_Career_UpgradeMetaTypeJoin"> site_Career_UpgradeMetaTypeJoin{}; // L2 experimental
inline constexpr site<"site_Career_UpgradePushStateBody"> site_Career_UpgradePushStateBody{}; // L2 experimental
inline constexpr site<"site_Career_UpgradePushStateDeptGate"> site_Career_UpgradePushStateDeptGate{}; // L2 experimental
inline constexpr site<"site_Career_UpgradeRowTypeJoin"> site_Career_UpgradeRowTypeJoin{}; // L2 experimental
inline constexpr site<"site_Career_UpgradeSetEnabledBody"> site_Career_UpgradeSetEnabledBody{}; // L2 experimental
inline constexpr site<"site_Career_UpgradeSetEnabledDeptGate"> site_Career_UpgradeSetEnabledDeptGate{}; // L2 experimental
inline constexpr site<"site_Career_UpgradeSetFittedBody"> site_Career_UpgradeSetFittedBody{}; // L2 experimental
inline constexpr site<"site_Career_UpgradeSetFittedDeptGate"> site_Career_UpgradeSetFittedDeptGate{}; // L2 experimental
inline constexpr site<"site_Career_UpgradeSetLockedBody"> site_Career_UpgradeSetLockedBody{}; // L2 experimental
inline constexpr site<"site_Career_UpgradeSetLockedDeptGate"> site_Career_UpgradeSetLockedDeptGate{}; // L2 experimental
inline constexpr site<"site_Career_VehicleFillOrderEnd"> site_Career_VehicleFillOrderEnd{}; // L2 experimental
inline constexpr site<"site_Career_VehicleFillOrderStart"> site_Career_VehicleFillOrderStart{}; // L2 experimental
inline constexpr site<"site_Career_WeekendObjectiveNext"> site_Career_WeekendObjectiveNext{}; // L2 experimental
inline constexpr site<"site_Career_WeekendObjectiveType"> site_Career_WeekendObjectiveType{}; // L2 experimental
inline constexpr vtable<"vt_Career_BankTransactionMessage", 2> vt_Career_BankTransactionMessage{}; // L2 experimental
inline constexpr vtable<"vt_Career_Championship", 12> vt_Career_Championship{}; // L2 experimental
inline constexpr vtable<"vt_Career_FacilityNode", 11> vt_Career_FacilityNode{}; // L2 experimental
inline constexpr vtable<"vt_Career_MessageDriverContractChanged", 2> vt_Career_MessageDriverContractChanged{}; // L2 experimental
inline constexpr vtable<"vt_Career_MessageDriverMoveConfirmed", 2> vt_Career_MessageDriverMoveConfirmed{}; // L2 experimental
inline constexpr vtable<"vt_Career_MessageDriverMovesFinalised", 2> vt_Career_MessageDriverMovesFinalised{}; // L2 experimental
inline constexpr vtable<"vt_Career_MessageDriverMovesFinalisedRecognition", 2> vt_Career_MessageDriverMovesFinalisedRecognition{}; // L2 experimental
inline constexpr vtable<"vt_Career_MessageDriverMovesFinalisedUpgrades", 2> vt_Career_MessageDriverMovesFinalisedUpgrades{}; // L2 experimental
inline constexpr vtable<"vt_Career_RenownAwardedMessage", 2> vt_Career_RenownAwardedMessage{}; // L2 experimental
inline constexpr vtable<"vt_Career_RnDDeptCard", 0> vt_Career_RnDDeptCard{}; // L2 experimental
inline constexpr vtable<"vt_Career_Season", 7> vt_Career_Season{}; // L2 experimental
inline constexpr vtable<"vt_Career_SponsorLoyaltyXpMessage", 2> vt_Career_SponsorLoyaltyXpMessage{}; // L2 experimental
inline constexpr vtable<"vt_Career_Weekend", 7> vt_Career_Weekend{}; // L2 experimental

}
