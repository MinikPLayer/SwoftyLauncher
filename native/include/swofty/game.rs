#![allow(
    non_camel_case_types,
    non_snake_case,
    non_upper_case_globals,
    dead_code,
    unused_imports
)]
use core::ffi::{c_char, c_int, c_uint, c_void};

pub const BUILD_SHA256: &str = "d873a1c8d2044cef2fd76c99c9d5a97649c5b815d055540995105f46619865ae";

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Status {
    Stable,
    Experimental,
}

#[derive(Clone, Copy, Debug)]
pub struct Binding {
    pub id: &'static str,
    pub kind: &'static str,
    pub status: Status,
}

#[repr(C)]
pub struct AiHandlingConfig {
    _opaque: [u8; 0],
}

#[repr(C)]
pub struct CarPerfVehicle {
    _opaque: [u8; 0],
}

#[repr(C)]
pub struct CarPhysicsRecord {
    _opaque: [u8; 0],
}

#[repr(C)]
pub struct CarWheelState {
    _opaque: [u8; 0],
}

#[repr(C)]
pub struct DepartmentData {
    _opaque: [u8; 0],
}

#[repr(C)]
pub struct EntrantSetupMode {
    _opaque: [u8; 0],
}

#[repr(C)]
pub struct EntrantsManager {
    _opaque: [u8; 0],
}

#[repr(C)]
pub struct FEHomeViewModel {
    _opaque: [u8; 0],
}

#[repr(C)]
pub struct GdbTableCursor {
    _opaque: [u8; 0],
}

#[repr(C)]
pub struct NeStr {
    _opaque: [u8; 0],
}

#[repr(C)]
pub struct RedFlagSystem {
    _opaque: [u8; 0],
}

#[repr(C)]
pub struct RnDDepartmentConfig {
    _opaque: [u8; 0],
}

#[repr(C)]
pub struct TodCurveBinding {
    _opaque: [u8; 0],
}

#[repr(C)]
pub struct UiLayoutStep {
    _opaque: [u8; 0],
}

#[repr(C)]
pub struct UiSignal {
    _opaque: [u8; 0],
}

#[repr(C)]
pub struct UiTemplateRecord {
    _opaque: [u8; 0],
}

#[repr(C)]
pub struct UpgradeEffectDef {
    _opaque: [u8; 0],
}

#[repr(C)]
pub struct UpgradeRecord {
    _opaque: [u8; 0],
}

#[repr(C)]
pub struct VehicleIdentity {
    _opaque: [u8; 0],
}

#[repr(C)]
pub struct VehiclePackageRow {
    _opaque: [u8; 0],
}

#[repr(C)]
pub struct Action {
    pub r#type: i32,
    _pad_0x4: [u8; 0x4],
    pub str: *mut NeStr,
}
const _: () = assert!(core::mem::offset_of!(Action, r#type) == 0x0);
const _: () = assert!(core::mem::offset_of!(Action, str) == 0x8);

#[repr(C)]
pub struct ActivityDef {
    _pad_0x0: [u8; 0x10],
    pub r#type: u8,
}
const _: () = assert!(core::mem::offset_of!(ActivityDef, r#type) == 0x10);

#[repr(C)]
pub struct BankAccount {
    _pad_0x0: [u8; 0x10],
    pub subs_begin: *mut BankSubAccount,
    pub subs_end: *mut BankSubAccount,
    _pad_0x20: [u8; 0x8],
    pub key: u32,
    _pad_0x2c: [u8; 0x4],
}
const _: () = assert!(core::mem::offset_of!(BankAccount, subs_begin) == 0x10);
const _: () = assert!(core::mem::offset_of!(BankAccount, subs_end) == 0x18);
const _: () = assert!(core::mem::offset_of!(BankAccount, key) == 0x28);
const _: () = assert!(core::mem::size_of::<BankAccount>() == 0x30);

#[repr(C)]
pub struct BankSubAccount {
    _pad_0x0: [u8; 0xc],
    pub category: u8,
    _pad_0xd: [u8; 0x3],
    pub balance: i32,
    pub minimum: i32,
    _pad_0x18: [u8; 0x28],
    pub history_begin: *mut BankTransaction,
    pub history_end: *mut BankTransaction,
    _pad_0x50: [u8; 0x160],
}
const _: () = assert!(core::mem::offset_of!(BankSubAccount, category) == 0xc);
const _: () = assert!(core::mem::offset_of!(BankSubAccount, balance) == 0x10);
const _: () = assert!(core::mem::offset_of!(BankSubAccount, minimum) == 0x14);
const _: () = assert!(core::mem::offset_of!(BankSubAccount, history_begin) == 0x40);
const _: () = assert!(core::mem::offset_of!(BankSubAccount, history_end) == 0x48);
const _: () = assert!(core::mem::size_of::<BankSubAccount>() == 0x1b0);

#[repr(C)]
pub struct BankTransaction {
    pub r#type: u8,
    _pad_0x1: [u8; 0x3],
    pub amount: i32,
    _pad_0x8: [u8; 0x4],
    pub time: u32,
    pub flow: u8,
    _pad_0x11: [u8; 0x3],
}
const _: () = assert!(core::mem::offset_of!(BankTransaction, r#type) == 0x0);
const _: () = assert!(core::mem::offset_of!(BankTransaction, amount) == 0x4);
const _: () = assert!(core::mem::offset_of!(BankTransaction, time) == 0xc);
const _: () = assert!(core::mem::offset_of!(BankTransaction, flow) == 0x10);
const _: () = assert!(core::mem::size_of::<BankTransaction>() == 0x14);

#[repr(C)]
pub struct BankTransactionMessage {
    pub vtable: *mut *mut c_void,
    _pad_0x8: [u8; 0x8],
    pub account: u16,
    _pad_0x12: [u8; 0x2],
    pub r#type: u8,
    _pad_0x15: [u8; 0x3],
    pub amount: i32,
    pub created: u32,
    pub time: u32,
    pub flow: u8,
    pub check_funds: u8,
    _pad_0x26: [u8; 0x2],
    pub category: u8,
}
const _: () = assert!(core::mem::offset_of!(BankTransactionMessage, vtable) == 0x0);
const _: () = assert!(core::mem::offset_of!(BankTransactionMessage, account) == 0x10);
const _: () = assert!(core::mem::offset_of!(BankTransactionMessage, r#type) == 0x14);
const _: () = assert!(core::mem::offset_of!(BankTransactionMessage, amount) == 0x18);
const _: () = assert!(core::mem::offset_of!(BankTransactionMessage, created) == 0x1c);
const _: () = assert!(core::mem::offset_of!(BankTransactionMessage, time) == 0x20);
const _: () = assert!(core::mem::offset_of!(BankTransactionMessage, flow) == 0x24);
const _: () = assert!(core::mem::offset_of!(BankTransactionMessage, check_funds) == 0x25);
const _: () = assert!(core::mem::offset_of!(BankTransactionMessage, category) == 0x28);

#[repr(C)]
pub struct CalendarActivity {
    _pad_0x0: [u8; 0xa],
    pub day: u16,
    _pad_0xc: [u8; 0x4],
    pub activity: *mut ActivityDef,
    _pad_0x18: [u8; 0x8],
}
const _: () = assert!(core::mem::offset_of!(CalendarActivity, day) == 0xa);
const _: () = assert!(core::mem::offset_of!(CalendarActivity, activity) == 0x10);
const _: () = assert!(core::mem::size_of::<CalendarActivity>() == 0x20);

#[repr(C)]
pub struct CarNumberComponent {
    _pad_0x0: [u8; 0x278],
    pub entity: *mut CareerEntity,
    _pad_0x280: [u8; 0x38],
}
const _: () = assert!(core::mem::offset_of!(CarNumberComponent, entity) == 0x278);
const _: () = assert!(core::mem::size_of::<CarNumberComponent>() == 0x2b8);

#[repr(C)]
pub struct CareerBankComponent {
    _pad_0x0: [u8; 0x298],
    pub team_accounts_begin: *mut BankAccount,
    pub team_accounts_end: *mut BankAccount,
}
const _: () = assert!(core::mem::offset_of!(CareerBankComponent, team_accounts_begin) == 0x298);
const _: () = assert!(core::mem::offset_of!(CareerBankComponent, team_accounts_end) == 0x2a0);

#[repr(C)]
pub struct CareerDriverContractRenewalComponent {
    _pad_0x0: [u8; 0x3a1],
    pub disable_driver_moves: u8,
    _pad_0x3a2: [u8; 0x6],
    pub driver_moves_allocator: *mut c_void,
    pub driver_moves_begin: *mut DriverMoveRecord,
    pub driver_moves_end: *mut DriverMoveRecord,
    pub driver_moves_capacity: u64,
}
const _: () =
    assert!(core::mem::offset_of!(CareerDriverContractRenewalComponent, disable_driver_moves) == 0x3a1);
const _: () =
    assert!(core::mem::offset_of!(CareerDriverContractRenewalComponent, driver_moves_allocator) == 0x3a8);
const _: () =
    assert!(core::mem::offset_of!(CareerDriverContractRenewalComponent, driver_moves_begin) == 0x3b0);
const _: () = assert!(core::mem::offset_of!(CareerDriverContractRenewalComponent, driver_moves_end) == 0x3b8);
const _: () =
    assert!(core::mem::offset_of!(CareerDriverContractRenewalComponent, driver_moves_capacity) == 0x3c0);

#[repr(C)]
pub struct CareerEntity {
    pub vtable: *mut *mut c_void,
    _pad_0x8: [u8; 0x2d8],
    pub entrants_begin: *mut EntrantRecord,
    pub entrants_end: *mut EntrantRecord,
    _pad_0x2f0: [u8; 0x8],
    pub entrant_update_busy: u8,
    _pad_0x2f9: [u8; 0xc5f],
    pub track_data: *mut TrackDataSystem,
    _pad_0xf60: [u8; 0xd0],
    pub component_registry: *mut *mut c_void,
    _pad_0x1038: [u8; 0x8],
    pub championships_begin: *mut c_void,
    pub championships_end: *mut c_void,
    _pad_0x1050: [u8; 0x8],
    pub current_championship: *mut c_void,
    _pad_0x1060: [u8; 0x8],
    pub championship_order_begin: *mut *mut c_void,
    pub championship_order_end: *mut *mut c_void,
}
const _: () = assert!(core::mem::offset_of!(CareerEntity, vtable) == 0x0);
const _: () = assert!(core::mem::offset_of!(CareerEntity, entrants_begin) == 0x2e0);
const _: () = assert!(core::mem::offset_of!(CareerEntity, entrants_end) == 0x2e8);
const _: () = assert!(core::mem::offset_of!(CareerEntity, entrant_update_busy) == 0x2f8);
const _: () = assert!(core::mem::offset_of!(CareerEntity, track_data) == 0xf58);
const _: () = assert!(core::mem::offset_of!(CareerEntity, component_registry) == 0x1030);
const _: () = assert!(core::mem::offset_of!(CareerEntity, championships_begin) == 0x1040);
const _: () = assert!(core::mem::offset_of!(CareerEntity, championships_end) == 0x1048);
const _: () = assert!(core::mem::offset_of!(CareerEntity, current_championship) == 0x1058);
const _: () = assert!(core::mem::offset_of!(CareerEntity, championship_order_begin) == 0x1068);
const _: () = assert!(core::mem::offset_of!(CareerEntity, championship_order_end) == 0x1070);

#[repr(C)]
pub struct CareerSponsorsComponent {
    _pad_0x0: [u8; 0x298],
    pub teams_begin: *mut SponsorTeamRecord,
    pub teams_end: *mut SponsorTeamRecord,
}
const _: () = assert!(core::mem::offset_of!(CareerSponsorsComponent, teams_begin) == 0x298);
const _: () = assert!(core::mem::offset_of!(CareerSponsorsComponent, teams_end) == 0x2a0);

#[repr(C)]
pub struct CareerTimeComponent {
    _pad_0x0: [u8; 0x290],
    pub season_dates: u8,
    _pad_0x291: [u8; 0x1f],
    pub current_minutes: u32,
}
const _: () = assert!(core::mem::offset_of!(CareerTimeComponent, season_dates) == 0x290);
const _: () = assert!(core::mem::offset_of!(CareerTimeComponent, current_minutes) == 0x2b0);

#[repr(C)]
pub struct Championship {
    pub vtable: *mut *mut c_void,
    _pad_0x8: [u8; 0x200],
    pub list_vtable: *mut *mut c_void,
    _pad_0x210: [u8; 0x328],
    pub completion: u32,
    _pad_0x53c: [u8; 0xc],
    pub seasons_begin: *mut ChampionshipSeasonEntry,
    pub seasons_end: *mut ChampionshipSeasonEntry,
    _pad_0x558: [u8; 0x8],
    pub season_index: u32,
    _pad_0x564: [u8; 0x4c],
}
const _: () = assert!(core::mem::offset_of!(Championship, vtable) == 0x0);
const _: () = assert!(core::mem::offset_of!(Championship, list_vtable) == 0x208);
const _: () = assert!(core::mem::offset_of!(Championship, completion) == 0x538);
const _: () = assert!(core::mem::offset_of!(Championship, seasons_begin) == 0x548);
const _: () = assert!(core::mem::offset_of!(Championship, seasons_end) == 0x550);
const _: () = assert!(core::mem::offset_of!(Championship, season_index) == 0x560);
const _: () = assert!(core::mem::size_of::<Championship>() == 0x5b0);

#[repr(C)]
pub struct ChampionshipHolder {
    _pad_0x0: [u8; 0x260],
    pub entity: *mut CareerEntity,
}
const _: () = assert!(core::mem::offset_of!(ChampionshipHolder, entity) == 0x260);

#[repr(C)]
pub struct ChampionshipSeasonEntry {
    _pad_0x0: [u8; 0x8],
    pub season: *mut Season,
}
const _: () = assert!(core::mem::offset_of!(ChampionshipSeasonEntry, season) == 0x8);
const _: () = assert!(core::mem::size_of::<ChampionshipSeasonEntry>() == 0x10);

#[repr(C)]
pub struct ContextStatSet {
    _pad_0x0: [u8; 0x10],
    pub stats_begin: *mut DriverStatistic,
    pub stats_end: *mut DriverStatistic,
    _pad_0x20: [u8; 0x8],
    pub mask_a: u32,
    pub mask_b: u32,
    _pad_0x30: [u8; 0x8],
}
const _: () = assert!(core::mem::offset_of!(ContextStatSet, stats_begin) == 0x10);
const _: () = assert!(core::mem::offset_of!(ContextStatSet, stats_end) == 0x18);
const _: () = assert!(core::mem::offset_of!(ContextStatSet, mask_a) == 0x28);
const _: () = assert!(core::mem::offset_of!(ContextStatSet, mask_b) == 0x2c);
const _: () = assert!(core::mem::size_of::<ContextStatSet>() == 0x38);

#[repr(C)]
pub struct ContractRecord {
    _pad_0x0: [u8; 0x28],
    pub clause_stat: u8,
    _pad_0x29: [u8; 0x3],
    pub clause_target: i32,
    _pad_0x30: [u8; 0x4],
    pub salary: i32,
    pub signing_bonus: i32,
    pub team_perf_money: i32,
    pub team_perf_race_value: i32,
    pub success_chance: f32,
    pub negotiation_bonus: i32,
    pub is_counteroffer: u8,
    _pad_0x4d: [u8; 0x3],
    pub driver: u32,
    pub old_team: u32,
    pub new_team: u32,
    pub driver_move_reason: u8,
    _pad_0x5d: [u8; 0x3],
    pub weekend_index: u32,
    pub career_day_index: u16,
    _pad_0x66: [u8; 0x2],
    pub day_contract_sent: u16,
    pub day_contract_decided: u16,
    pub accepted: u8,
    pub acknowledged: u8,
    _pad_0x6e: [u8; 0x2],
}
const _: () = assert!(core::mem::offset_of!(ContractRecord, clause_stat) == 0x28);
const _: () = assert!(core::mem::offset_of!(ContractRecord, clause_target) == 0x2c);
const _: () = assert!(core::mem::offset_of!(ContractRecord, salary) == 0x34);
const _: () = assert!(core::mem::offset_of!(ContractRecord, signing_bonus) == 0x38);
const _: () = assert!(core::mem::offset_of!(ContractRecord, team_perf_money) == 0x3c);
const _: () = assert!(core::mem::offset_of!(ContractRecord, team_perf_race_value) == 0x40);
const _: () = assert!(core::mem::offset_of!(ContractRecord, success_chance) == 0x44);
const _: () = assert!(core::mem::offset_of!(ContractRecord, negotiation_bonus) == 0x48);
const _: () = assert!(core::mem::offset_of!(ContractRecord, is_counteroffer) == 0x4c);
const _: () = assert!(core::mem::offset_of!(ContractRecord, driver) == 0x50);
const _: () = assert!(core::mem::offset_of!(ContractRecord, old_team) == 0x54);
const _: () = assert!(core::mem::offset_of!(ContractRecord, new_team) == 0x58);
const _: () = assert!(core::mem::offset_of!(ContractRecord, driver_move_reason) == 0x5c);
const _: () = assert!(core::mem::offset_of!(ContractRecord, weekend_index) == 0x60);
const _: () = assert!(core::mem::offset_of!(ContractRecord, career_day_index) == 0x64);
const _: () = assert!(core::mem::offset_of!(ContractRecord, day_contract_sent) == 0x68);
const _: () = assert!(core::mem::offset_of!(ContractRecord, day_contract_decided) == 0x6a);
const _: () = assert!(core::mem::offset_of!(ContractRecord, accepted) == 0x6c);
const _: () = assert!(core::mem::offset_of!(ContractRecord, acknowledged) == 0x6d);
const _: () = assert!(core::mem::size_of::<ContractRecord>() == 0x70);

#[repr(C)]
pub struct CostCapComponent {
    _pad_0x0: [u8; 0x2d8],
    pub team_slots: *mut *mut CostCapTeam,
    pub team_ctrl: *mut u8,
}
const _: () = assert!(core::mem::offset_of!(CostCapComponent, team_slots) == 0x2d8);
const _: () = assert!(core::mem::offset_of!(CostCapComponent, team_ctrl) == 0x2e0);

#[repr(C)]
pub struct CostCapSeason {
    pub season_id: i32,
    _pad_0x4: [u8; 0x4],
    pub cached_expenses: i32,
    pub cost_cap: i32,
    pub cached_modifier: f32,
}
const _: () = assert!(core::mem::offset_of!(CostCapSeason, season_id) == 0x0);
const _: () = assert!(core::mem::offset_of!(CostCapSeason, cached_expenses) == 0x8);
const _: () = assert!(core::mem::offset_of!(CostCapSeason, cost_cap) == 0xc);
const _: () = assert!(core::mem::offset_of!(CostCapSeason, cached_modifier) == 0x10);

#[repr(C)]
pub struct CostCapTeam {
    pub account: u16,
    _pad_0x2: [u8; 0x2e],
    pub season_slots: *mut *mut CostCapSeason,
    pub season_ctrl: *mut u8,
}
const _: () = assert!(core::mem::offset_of!(CostCapTeam, account) == 0x0);
const _: () = assert!(core::mem::offset_of!(CostCapTeam, season_slots) == 0x30);
const _: () = assert!(core::mem::offset_of!(CostCapTeam, season_ctrl) == 0x38);

#[repr(C)]
pub struct CurvePoint {
    pub x: f32,
    pub y: f32,
}
const _: () = assert!(core::mem::offset_of!(CurvePoint, x) == 0x0);
const _: () = assert!(core::mem::offset_of!(CurvePoint, y) == 0x4);
const _: () = assert!(core::mem::size_of::<CurvePoint>() == 0x8);

#[repr(C)]
pub struct DriverCalendar {
    _pad_0x0: [u8; 0x50],
    pub activities_begin: *mut c_void,
    pub activities_end: *mut c_void,
    _pad_0x60: [u8; 0x60],
    pub driver: u32,
    _pad_0xc4: [u8; 0x4],
}
const _: () = assert!(core::mem::offset_of!(DriverCalendar, activities_begin) == 0x50);
const _: () = assert!(core::mem::offset_of!(DriverCalendar, activities_end) == 0x58);
const _: () = assert!(core::mem::offset_of!(DriverCalendar, driver) == 0xc0);
const _: () = assert!(core::mem::size_of::<DriverCalendar>() == 0xc8);

#[repr(C)]
pub struct DriverCareerContractOwner {
    _pad_0x0: [u8; 0x298],
    pub contracts_begin: *mut c_void,
    pub contracts_end: *mut c_void,
}
const _: () = assert!(core::mem::offset_of!(DriverCareerContractOwner, contracts_begin) == 0x298);
const _: () = assert!(core::mem::offset_of!(DriverCareerContractOwner, contracts_end) == 0x2a0);

#[repr(C)]
pub struct DriverCareerContractsManagerComponent {
    _pad_0x0: [u8; 0x270],
    pub weekend_source: *mut c_void,
    pub entity: *mut CareerEntity,
    _pad_0x280: [u8; 0xd0],
    pub contract_owner: *mut DriverCareerContractOwner,
    pub career_time: *mut CareerTimeComponent,
    _pad_0x360: [u8; 0x8],
    pub car_numbers: *mut CarNumberComponent,
    pub schedule: *mut ScheduleComponent,
    _pad_0x378: [u8; 0x30],
    pub secret_meetings_begin: *mut c_void,
    pub secret_meetings_end: *mut c_void,
}
const _: () = assert!(core::mem::offset_of!(DriverCareerContractsManagerComponent, weekend_source) == 0x270);
const _: () = assert!(core::mem::offset_of!(DriverCareerContractsManagerComponent, entity) == 0x278);
const _: () = assert!(core::mem::offset_of!(DriverCareerContractsManagerComponent, contract_owner) == 0x350);
const _: () = assert!(core::mem::offset_of!(DriverCareerContractsManagerComponent, career_time) == 0x358);
const _: () = assert!(core::mem::offset_of!(DriverCareerContractsManagerComponent, car_numbers) == 0x368);
const _: () = assert!(core::mem::offset_of!(DriverCareerContractsManagerComponent, schedule) == 0x370);
const _: () =
    assert!(core::mem::offset_of!(DriverCareerContractsManagerComponent, secret_meetings_begin) == 0x3a8);
const _: () =
    assert!(core::mem::offset_of!(DriverCareerContractsManagerComponent, secret_meetings_end) == 0x3b0);

#[repr(C)]
pub struct DriverContract {
    pub team: u32,
    _pad_0x4: [u8; 0x8],
    pub signed_day: u16,
    pub kind: u8,
    _pad_0xf: [u8; 0x1],
    pub end_day: u16,
    pub state: u8,
    _pad_0x13: [u8; 0x1],
    pub end_value: u32,
    _pad_0x18: [u8; 0x14],
    pub active: u8,
    _pad_0x2d: [u8; 0xb],
    pub history_begin: *mut c_void,
    pub history_end: *mut c_void,
    _pad_0x48: [u8; 0x8],
    pub driver: u32,
}
const _: () = assert!(core::mem::offset_of!(DriverContract, team) == 0x0);
const _: () = assert!(core::mem::offset_of!(DriverContract, signed_day) == 0xc);
const _: () = assert!(core::mem::offset_of!(DriverContract, kind) == 0xe);
const _: () = assert!(core::mem::offset_of!(DriverContract, end_day) == 0x10);
const _: () = assert!(core::mem::offset_of!(DriverContract, state) == 0x12);
const _: () = assert!(core::mem::offset_of!(DriverContract, end_value) == 0x14);
const _: () = assert!(core::mem::offset_of!(DriverContract, active) == 0x2c);
const _: () = assert!(core::mem::offset_of!(DriverContract, history_begin) == 0x38);
const _: () = assert!(core::mem::offset_of!(DriverContract, history_end) == 0x40);
const _: () = assert!(core::mem::offset_of!(DriverContract, driver) == 0x50);

#[repr(C)]
pub struct DriverContractChangedMessage {
    pub vtable: *mut *mut c_void,
    pub reserved: u64,
    pub driver: u32,
    pub before: [u8; 48],
    pub after: [u8; 44],
    pub kind: u8,
    _pad_0x71: [u8; 0x7],
}
const _: () = assert!(core::mem::offset_of!(DriverContractChangedMessage, vtable) == 0x0);
const _: () = assert!(core::mem::offset_of!(DriverContractChangedMessage, reserved) == 0x8);
const _: () = assert!(core::mem::offset_of!(DriverContractChangedMessage, driver) == 0x10);
const _: () = assert!(core::mem::offset_of!(DriverContractChangedMessage, before) == 0x14);
const _: () = assert!(core::mem::offset_of!(DriverContractChangedMessage, after) == 0x44);
const _: () = assert!(core::mem::offset_of!(DriverContractChangedMessage, kind) == 0x70);
const _: () = assert!(core::mem::size_of::<DriverContractChangedMessage>() == 0x78);

#[repr(C)]
pub struct DriverContractsMyTeamComponent {
    _pad_0x0: [u8; 0x2e8],
    pub car_numbers: *mut CarNumberComponent,
    _pad_0x2f0: [u8; 0x4a8],
    pub pending_contracts_begin: *mut ContractRecord,
    _pad_0x7a0: [u8; 0x18],
    pub decided_contracts_begin: *mut ContractRecord,
    _pad_0x7c0: [u8; 0x18],
    pub pending_moves_begin: *mut ContractRecord,
    pub pending_moves_end: *mut ContractRecord,
    _pad_0x7e8: [u8; 0x10],
    pub last_season_signings_begin: *mut ContractRecord,
    pub last_season_signings_end: *mut ContractRecord,
}
const _: () = assert!(core::mem::offset_of!(DriverContractsMyTeamComponent, car_numbers) == 0x2e8);
const _: () =
    assert!(core::mem::offset_of!(DriverContractsMyTeamComponent, pending_contracts_begin) == 0x798);
const _: () =
    assert!(core::mem::offset_of!(DriverContractsMyTeamComponent, decided_contracts_begin) == 0x7b8);
const _: () = assert!(core::mem::offset_of!(DriverContractsMyTeamComponent, pending_moves_begin) == 0x7d8);
const _: () = assert!(core::mem::offset_of!(DriverContractsMyTeamComponent, pending_moves_end) == 0x7e0);
const _: () =
    assert!(core::mem::offset_of!(DriverContractsMyTeamComponent, last_season_signings_begin) == 0x7f8);
const _: () =
    assert!(core::mem::offset_of!(DriverContractsMyTeamComponent, last_season_signings_end) == 0x800);

#[repr(C)]
pub struct DriverMoveRecord {
    pub driver: u32,
    pub old_team: u32,
    pub new_team: u32,
    pub reason: u8,
    _pad_0xd: [u8; 0x3],
    pub weekend_index: u32,
    pub day_index: u16,
    _pad_0x16: [u8; 0x2],
}
const _: () = assert!(core::mem::offset_of!(DriverMoveRecord, driver) == 0x0);
const _: () = assert!(core::mem::offset_of!(DriverMoveRecord, old_team) == 0x4);
const _: () = assert!(core::mem::offset_of!(DriverMoveRecord, new_team) == 0x8);
const _: () = assert!(core::mem::offset_of!(DriverMoveRecord, reason) == 0xc);
const _: () = assert!(core::mem::offset_of!(DriverMoveRecord, weekend_index) == 0x10);
const _: () = assert!(core::mem::offset_of!(DriverMoveRecord, day_index) == 0x14);
const _: () = assert!(core::mem::size_of::<DriverMoveRecord>() == 0x18);

#[repr(C)]
pub struct DriverMoveConfirmedMessage {
    pub vtable: *mut *mut c_void,
    pub reserved: u64,
    pub r#move: DriverMoveRecord,
}
const _: () = assert!(core::mem::offset_of!(DriverMoveConfirmedMessage, vtable) == 0x0);
const _: () = assert!(core::mem::offset_of!(DriverMoveConfirmedMessage, reserved) == 0x8);
const _: () = assert!(core::mem::offset_of!(DriverMoveConfirmedMessage, r#move) == 0x10);
const _: () = assert!(core::mem::size_of::<DriverMoveConfirmedMessage>() == 0x28);

#[repr(C)]
pub struct LineupChange {
    pub driver: u32,
    pub old_team: u32,
    pub new_team: u32,
    _pad_0xc: [u8; 0x4],
}
const _: () = assert!(core::mem::offset_of!(LineupChange, driver) == 0x0);
const _: () = assert!(core::mem::offset_of!(LineupChange, old_team) == 0x4);
const _: () = assert!(core::mem::offset_of!(LineupChange, new_team) == 0x8);
const _: () = assert!(core::mem::size_of::<LineupChange>() == 0x10);

#[repr(C)]
pub struct DriverMovesFinalisedMessage {
    pub vtable: *mut *mut c_void,
    pub reserved: u64,
    pub changes: [LineupChange; 40],
    pub count: u64,
}
const _: () = assert!(core::mem::offset_of!(DriverMovesFinalisedMessage, vtable) == 0x0);
const _: () = assert!(core::mem::offset_of!(DriverMovesFinalisedMessage, reserved) == 0x8);
const _: () = assert!(core::mem::offset_of!(DriverMovesFinalisedMessage, changes) == 0x10);
const _: () = assert!(core::mem::offset_of!(DriverMovesFinalisedMessage, count) == 0x290);
const _: () = assert!(core::mem::size_of::<DriverMovesFinalisedMessage>() == 0x298);

#[repr(C)]
pub struct DriverSecretMeetings {
    _pad_0x0: [u8; 0x10],
    pub current_allocator: *mut c_void,
    pub current: *mut SecretMeeting,
    _pad_0x20: [u8; 0x8],
    pub previous_begin: *mut c_void,
    pub previous_end: *mut c_void,
    _pad_0x38: [u8; 0x8],
    pub driver: u32,
    _pad_0x44: [u8; 0x4],
}
const _: () = assert!(core::mem::offset_of!(DriverSecretMeetings, current_allocator) == 0x10);
const _: () = assert!(core::mem::offset_of!(DriverSecretMeetings, current) == 0x18);
const _: () = assert!(core::mem::offset_of!(DriverSecretMeetings, previous_begin) == 0x28);
const _: () = assert!(core::mem::offset_of!(DriverSecretMeetings, previous_end) == 0x30);
const _: () = assert!(core::mem::offset_of!(DriverSecretMeetings, driver) == 0x40);
const _: () = assert!(core::mem::size_of::<DriverSecretMeetings>() == 0x48);

#[repr(C)]
pub struct EvolutionCurve {
    pub vtable: *mut *mut c_void,
    pub inv_x_range: f32,
    pub min_x: f32,
    pub y_step: f32,
    pub end_value: f32,
    _pad_0x18: [u8; 0x4],
    pub min_y: f32,
    pub max_y: f32,
    pub lut: [u8; 65],
    _pad_0x65: [u8; 0x3],
    pub allocator: *mut c_void,
    pub points_begin: *mut CurvePoint,
    _pad_0x78: [u8; 0x8],
    pub points_capacity: u64,
}
const _: () = assert!(core::mem::offset_of!(EvolutionCurve, vtable) == 0x0);
const _: () = assert!(core::mem::offset_of!(EvolutionCurve, inv_x_range) == 0x8);
const _: () = assert!(core::mem::offset_of!(EvolutionCurve, min_x) == 0xc);
const _: () = assert!(core::mem::offset_of!(EvolutionCurve, y_step) == 0x10);
const _: () = assert!(core::mem::offset_of!(EvolutionCurve, end_value) == 0x14);
const _: () = assert!(core::mem::offset_of!(EvolutionCurve, min_y) == 0x1c);
const _: () = assert!(core::mem::offset_of!(EvolutionCurve, max_y) == 0x20);
const _: () = assert!(core::mem::offset_of!(EvolutionCurve, lut) == 0x24);
const _: () = assert!(core::mem::offset_of!(EvolutionCurve, allocator) == 0x68);
const _: () = assert!(core::mem::offset_of!(EvolutionCurve, points_begin) == 0x70);
const _: () = assert!(core::mem::offset_of!(EvolutionCurve, points_capacity) == 0x80);
const _: () = assert!(core::mem::size_of::<EvolutionCurve>() == 0x88);

#[repr(C)]
pub struct DriverStatistic {
    _pad_0x0: [u8; 0x10],
    pub info_stat_id: u16,
    _pad_0x12: [u8; 0x6],
    pub evolution_graph: EvolutionCurve,
    pub patched_evolution_graph: EvolutionCurve,
    pub has_patched_graph: u8,
    _pad_0x129: [u8; 0x7],
    pub temporary_boost_value: f32,
    pub is_dynamic_stat: u8,
    _pad_0x135: [u8; 0x3],
    pub dynamic_level: f32,
    _pad_0x13c: [u8; 0x4],
}
const _: () = assert!(core::mem::offset_of!(DriverStatistic, info_stat_id) == 0x10);
const _: () = assert!(core::mem::offset_of!(DriverStatistic, evolution_graph) == 0x18);
const _: () = assert!(core::mem::offset_of!(DriverStatistic, patched_evolution_graph) == 0xa0);
const _: () = assert!(core::mem::offset_of!(DriverStatistic, has_patched_graph) == 0x128);
const _: () = assert!(core::mem::offset_of!(DriverStatistic, temporary_boost_value) == 0x130);
const _: () = assert!(core::mem::offset_of!(DriverStatistic, is_dynamic_stat) == 0x134);
const _: () = assert!(core::mem::offset_of!(DriverStatistic, dynamic_level) == 0x138);
const _: () = assert!(core::mem::size_of::<DriverStatistic>() == 0x140);

#[repr(C)]
pub struct DriverStatisticsComponent {
    _pad_0x0: [u8; 0x278],
    pub stat_context: *mut c_void,
    _pad_0x280: [u8; 0x10],
    pub career_time: *mut CareerTimeComponent,
    _pad_0x298: [u8; 0x10],
    pub stats_data: u8,
    pub use_story_mode_data: u8,
    _pad_0x2aa: [u8; 0x2],
    pub last_patch_applied_checksum: u32,
    pub last_patch_declined_checksum: u32,
    _pad_0x2b4: [u8; 0x1c],
    pub stat_infos_begin: *mut c_void,
    pub stat_infos_end: *mut c_void,
    _pad_0x2e0: [u8; 0x30],
    pub driver_data_begin: *mut DriverStatsEntry,
    pub driver_data_end: *mut DriverStatsEntry,
}
const _: () = assert!(core::mem::offset_of!(DriverStatisticsComponent, stat_context) == 0x278);
const _: () = assert!(core::mem::offset_of!(DriverStatisticsComponent, career_time) == 0x290);
const _: () = assert!(core::mem::offset_of!(DriverStatisticsComponent, stats_data) == 0x2a8);
const _: () = assert!(core::mem::offset_of!(DriverStatisticsComponent, use_story_mode_data) == 0x2a9);
const _: () = assert!(core::mem::offset_of!(DriverStatisticsComponent, last_patch_applied_checksum) == 0x2ac);
const _: () =
    assert!(core::mem::offset_of!(DriverStatisticsComponent, last_patch_declined_checksum) == 0x2b0);
const _: () = assert!(core::mem::offset_of!(DriverStatisticsComponent, stat_infos_begin) == 0x2d0);
const _: () = assert!(core::mem::offset_of!(DriverStatisticsComponent, stat_infos_end) == 0x2d8);
const _: () = assert!(core::mem::offset_of!(DriverStatisticsComponent, driver_data_begin) == 0x310);
const _: () = assert!(core::mem::offset_of!(DriverStatisticsComponent, driver_data_end) == 0x318);

#[repr(C)]
pub struct DriverStatsEntry {
    pub kind: u8,
    _pad_0x1: [u8; 0x1],
    pub stat_id: u16,
    _pad_0x4: [u8; 0x14],
    pub stats_begin: *mut DriverStatistic,
    pub stats_end: *mut DriverStatistic,
    _pad_0x28: [u8; 0x10],
    pub context_sets_begin: *mut ContextStatSet,
    pub context_sets_end: *mut ContextStatSet,
    _pad_0x48: [u8; 0x8],
}
const _: () = assert!(core::mem::offset_of!(DriverStatsEntry, kind) == 0x0);
const _: () = assert!(core::mem::offset_of!(DriverStatsEntry, stat_id) == 0x2);
const _: () = assert!(core::mem::offset_of!(DriverStatsEntry, stats_begin) == 0x18);
const _: () = assert!(core::mem::offset_of!(DriverStatsEntry, stats_end) == 0x20);
const _: () = assert!(core::mem::offset_of!(DriverStatsEntry, context_sets_begin) == 0x38);
const _: () = assert!(core::mem::offset_of!(DriverStatsEntry, context_sets_end) == 0x40);
const _: () = assert!(core::mem::size_of::<DriverStatsEntry>() == 0x50);

#[repr(C)]
pub struct EntrantRecord {
    pub vtable: *mut *mut c_void,
    _pad_0x8: [u8; 0x40],
    pub ordinal_flags: u32,
    _pad_0x4c: [u8; 0x4],
    pub id_lo: u8,
    _pad_0x51: [u8; 0x1],
    pub id_hi: u16,
    pub driver_customisation: u32,
    pub team_stat_id: u32,
    pub vehicle_ref: u32,
    pub driver_reg_value: u32,
    pub vehicle_customisation: u32,
    pub badge_stat_id: u32,
    _pad_0x6c: [u8; 0x4],
    pub subobj_vtable: *mut *mut c_void,
    pub const_ffff0012: u32,
    _pad_0x7c: [u8; 0x4],
    pub team_position: u16,
    _pad_0x82: [u8; 0x1],
    pub car_number: u8,
    _pad_0x84: [u8; 0x4],
}
const _: () = assert!(core::mem::offset_of!(EntrantRecord, vtable) == 0x0);
const _: () = assert!(core::mem::offset_of!(EntrantRecord, ordinal_flags) == 0x48);
const _: () = assert!(core::mem::offset_of!(EntrantRecord, id_lo) == 0x50);
const _: () = assert!(core::mem::offset_of!(EntrantRecord, id_hi) == 0x52);
const _: () = assert!(core::mem::offset_of!(EntrantRecord, driver_customisation) == 0x54);
const _: () = assert!(core::mem::offset_of!(EntrantRecord, team_stat_id) == 0x58);
const _: () = assert!(core::mem::offset_of!(EntrantRecord, vehicle_ref) == 0x5c);
const _: () = assert!(core::mem::offset_of!(EntrantRecord, driver_reg_value) == 0x60);
const _: () = assert!(core::mem::offset_of!(EntrantRecord, vehicle_customisation) == 0x64);
const _: () = assert!(core::mem::offset_of!(EntrantRecord, badge_stat_id) == 0x68);
const _: () = assert!(core::mem::offset_of!(EntrantRecord, subobj_vtable) == 0x70);
const _: () = assert!(core::mem::offset_of!(EntrantRecord, const_ffff0012) == 0x78);
const _: () = assert!(core::mem::offset_of!(EntrantRecord, team_position) == 0x80);
const _: () = assert!(core::mem::offset_of!(EntrantRecord, car_number) == 0x83);
const _: () = assert!(core::mem::size_of::<EntrantRecord>() == 0x88);

#[repr(C)]
pub struct EntrantWriteData {
    pub driver: u32,
    pub driver_customisation: u32,
    pub team_stat_id: u32,
    pub vehicle_ref: u32,
    pub driver_reg_value: u32,
    pub vehicle_customisation: u32,
    pub badge_stat_id: u32,
    _pad_0x1c: [u8; 0xc],
    pub seat_value: u32,
}
const _: () = assert!(core::mem::offset_of!(EntrantWriteData, driver) == 0x0);
const _: () = assert!(core::mem::offset_of!(EntrantWriteData, driver_customisation) == 0x4);
const _: () = assert!(core::mem::offset_of!(EntrantWriteData, team_stat_id) == 0x8);
const _: () = assert!(core::mem::offset_of!(EntrantWriteData, vehicle_ref) == 0xc);
const _: () = assert!(core::mem::offset_of!(EntrantWriteData, driver_reg_value) == 0x10);
const _: () = assert!(core::mem::offset_of!(EntrantWriteData, vehicle_customisation) == 0x14);
const _: () = assert!(core::mem::offset_of!(EntrantWriteData, badge_stat_id) == 0x18);
const _: () = assert!(core::mem::offset_of!(EntrantWriteData, seat_value) == 0x28);
const _: () = assert!(core::mem::size_of::<EntrantWriteData>() == 0x2c);

#[repr(C)]
pub struct F1SeasonStandings {
    pub vtable: *mut *mut c_void,
    _pad_0x8: [u8; 0x18],
    pub points_begin: *mut StandingsRound,
    pub points_end: *mut StandingsRound,
    _pad_0x30: [u8; 0x78],
    pub extra_points_begin: *mut StandingsPoints,
    pub extra_points_end: *mut StandingsPoints,
    _pad_0xb8: [u8; 0x18],
    pub results_begin: *mut StandingsRound,
    pub results_end: *mut StandingsRound,
}
const _: () = assert!(core::mem::offset_of!(F1SeasonStandings, vtable) == 0x0);
const _: () = assert!(core::mem::offset_of!(F1SeasonStandings, points_begin) == 0x20);
const _: () = assert!(core::mem::offset_of!(F1SeasonStandings, points_end) == 0x28);
const _: () = assert!(core::mem::offset_of!(F1SeasonStandings, extra_points_begin) == 0xa8);
const _: () = assert!(core::mem::offset_of!(F1SeasonStandings, extra_points_end) == 0xb0);
const _: () = assert!(core::mem::offset_of!(F1SeasonStandings, results_begin) == 0xd0);
const _: () = assert!(core::mem::offset_of!(F1SeasonStandings, results_end) == 0xd8);

#[repr(C)]
pub struct FacilityNode {
    pub vtable: *mut *mut c_void,
    pub state: u8,
    _pad_0x9: [u8; 0x3],
    pub stat_id: u32,
    _pad_0x10: [u8; 0x10],
    pub children_begin: *mut c_void,
    pub children_end: *mut c_void,
    _pad_0x30: [u8; 0x10],
    pub phase: i32,
    _pad_0x44: [u8; 0x3c],
    pub cost: i32,
    _pad_0x84: [u8; 0x4],
    pub name_loc_tag: *mut c_char,
    _pad_0x90: [u8; 0x18],
    pub days_to_complete: i32,
    pub days_remaining: i32,
    pub purchased: u8,
    _pad_0xb1: [u8; 0x2],
    pub seen_completed: u8,
    _pad_0xb4: [u8; 0x28],
    pub ui_level: i32,
    pub ui_max_level: i32,
    _pad_0xe4: [u8; 0x4],
}
const _: () = assert!(core::mem::offset_of!(FacilityNode, vtable) == 0x0);
const _: () = assert!(core::mem::offset_of!(FacilityNode, state) == 0x8);
const _: () = assert!(core::mem::offset_of!(FacilityNode, stat_id) == 0xc);
const _: () = assert!(core::mem::offset_of!(FacilityNode, children_begin) == 0x20);
const _: () = assert!(core::mem::offset_of!(FacilityNode, children_end) == 0x28);
const _: () = assert!(core::mem::offset_of!(FacilityNode, phase) == 0x40);
const _: () = assert!(core::mem::offset_of!(FacilityNode, cost) == 0x80);
const _: () = assert!(core::mem::offset_of!(FacilityNode, name_loc_tag) == 0x88);
const _: () = assert!(core::mem::offset_of!(FacilityNode, days_to_complete) == 0xa8);
const _: () = assert!(core::mem::offset_of!(FacilityNode, days_remaining) == 0xac);
const _: () = assert!(core::mem::offset_of!(FacilityNode, purchased) == 0xb0);
const _: () = assert!(core::mem::offset_of!(FacilityNode, seen_completed) == 0xb3);
const _: () = assert!(core::mem::offset_of!(FacilityNode, ui_level) == 0xdc);
const _: () = assert!(core::mem::offset_of!(FacilityNode, ui_max_level) == 0xe0);
const _: () = assert!(core::mem::size_of::<FacilityNode>() == 0xe8);

#[repr(C)]
pub struct FacilityPerkTree {
    _pad_0x0: [u8; 0x8],
    pub nodes_begin: *mut c_void,
    pub nodes_end: *mut c_void,
    _pad_0x18: [u8; 0x10],
}
const _: () = assert!(core::mem::offset_of!(FacilityPerkTree, nodes_begin) == 0x8);
const _: () = assert!(core::mem::offset_of!(FacilityPerkTree, nodes_end) == 0x10);
const _: () = assert!(core::mem::size_of::<FacilityPerkTree>() == 0x28);

#[repr(C)]
pub struct FacilityTeamData {
    _pad_0x0: [u8; 0x10],
    pub facilities_begin: *mut c_void,
    pub facilities_end: *mut c_void,
    _pad_0x20: [u8; 0x8],
    pub team_key: u32,
    _pad_0x2c: [u8; 0x54],
}
const _: () = assert!(core::mem::offset_of!(FacilityTeamData, facilities_begin) == 0x10);
const _: () = assert!(core::mem::offset_of!(FacilityTeamData, facilities_end) == 0x18);
const _: () = assert!(core::mem::offset_of!(FacilityTeamData, team_key) == 0x28);
const _: () = assert!(core::mem::size_of::<FacilityTeamData>() == 0x80);

#[repr(C)]
pub struct FacilityTeamEntry {
    _pad_0x0: [u8; 0x8],
    pub data: *mut FacilityTeamData,
}
const _: () = assert!(core::mem::offset_of!(FacilityTeamEntry, data) == 0x8);

#[repr(C)]
pub struct FlowState {
    _pad_0x0: [u8; 0x20],
    pub name_size: u64,
    pub name: *mut c_char,
    _pad_0x30: [u8; 0x80],
    pub name_hash: u32,
    _pad_0xb4: [u8; 0x4],
    pub status: i32,
}
const _: () = assert!(core::mem::offset_of!(FlowState, name_size) == 0x20);
const _: () = assert!(core::mem::offset_of!(FlowState, name) == 0x28);
const _: () = assert!(core::mem::offset_of!(FlowState, name_hash) == 0xb0);
const _: () = assert!(core::mem::offset_of!(FlowState, status) == 0xb8);

#[repr(C)]
pub struct GameManager {
    _pad_0x0: [u8; 0xc0],
    pub mode_context: *mut GameModeContext,
}
const _: () = assert!(core::mem::offset_of!(GameManager, mode_context) == 0xc0);

#[repr(C)]
pub struct GameMessageBusHolder {
    _pad_0x0: [u8; 0x8],
    pub bus: *mut NeMessageBus,
}
const _: () = assert!(core::mem::offset_of!(GameMessageBusHolder, bus) == 0x8);

#[repr(C)]
pub struct GameModeContext {
    _pad_0x0: [u8; 0x88],
    pub championship_holder: *mut ChampionshipHolder,
}
const _: () = assert!(core::mem::offset_of!(GameModeContext, championship_holder) == 0x88);

#[repr(C)]
pub struct GameWeatherSystem {
    _pad_0x0: [u8; 0x1d98],
    pub tod: *mut TimeOfDaySystem,
    _pad_0x1da0: [u8; 0x8],
    pub tod_owner: *mut TimeOfDayOwner,
    _pad_0x1db0: [u8; 0x152],
    pub weekend_valid: u8,
    _pad_0x1f03: [u8; 0x1],
    pub season_index: u32,
    pub weekend_index: u32,
    _pad_0x1f0c: [u8; 0x4],
    pub base_seed: u32,
    pub forced_seed: u32,
    pub weekend: u32,
    _pad_0x1f1c: [u8; 0x101e],
    pub dataset_dynamic: [u8; 8],
}
const _: () = assert!(core::mem::offset_of!(GameWeatherSystem, tod) == 0x1d98);
const _: () = assert!(core::mem::offset_of!(GameWeatherSystem, tod_owner) == 0x1da8);
const _: () = assert!(core::mem::offset_of!(GameWeatherSystem, weekend_valid) == 0x1f02);
const _: () = assert!(core::mem::offset_of!(GameWeatherSystem, season_index) == 0x1f04);
const _: () = assert!(core::mem::offset_of!(GameWeatherSystem, weekend_index) == 0x1f08);
const _: () = assert!(core::mem::offset_of!(GameWeatherSystem, base_seed) == 0x1f10);
const _: () = assert!(core::mem::offset_of!(GameWeatherSystem, forced_seed) == 0x1f14);
const _: () = assert!(core::mem::offset_of!(GameWeatherSystem, weekend) == 0x1f18);
const _: () = assert!(core::mem::offset_of!(GameWeatherSystem, dataset_dynamic) == 0x2f3a);

#[repr(C)]
pub struct GdbStringRecord {
    _pad_0x0: [u8; 0x10],
    pub enc_size: u64,
    pub enc: *mut u8,
}
const _: () = assert!(core::mem::offset_of!(GdbStringRecord, enc_size) == 0x10);
const _: () = assert!(core::mem::offset_of!(GdbStringRecord, enc) == 0x18);

#[repr(C)]
pub struct VmPropHolder {
    _pad_0x0: [u8; 0x8],
    pub node: *mut c_void,
}
const _: () = assert!(core::mem::offset_of!(VmPropHolder, node) == 0x8);

#[repr(C)]
pub struct HomeItem {
    pub vtable: *mut ItemVtbl,
    _pad_0x8: [u8; 0x8],
    pub id: *mut NeStr,
    pub button_node: VmPropHolder,
}
const _: () = assert!(core::mem::offset_of!(HomeItem, vtable) == 0x0);
const _: () = assert!(core::mem::offset_of!(HomeItem, id) == 0x10);
const _: () = assert!(core::mem::offset_of!(HomeItem, button_node) == 0x18);

#[repr(C)]
pub struct ItemVtbl {
    _pad_0x0: [u8; 0x20],
    pub populate: *mut c_void,
    pub get_action: *mut c_void,
}
const _: () = assert!(core::mem::offset_of!(ItemVtbl, populate) == 0x20);
const _: () = assert!(core::mem::offset_of!(ItemVtbl, get_action) == 0x28);

#[repr(C)]
pub struct MyTeamFacilitiesComponent {
    _pad_0x0: [u8; 0x2b8],
    pub team_slots: *mut *mut FacilityTeamEntry,
    pub team_ctrl: *mut u8,
    pub team_count: u64,
}
const _: () = assert!(core::mem::offset_of!(MyTeamFacilitiesComponent, team_slots) == 0x2b8);
const _: () = assert!(core::mem::offset_of!(MyTeamFacilitiesComponent, team_ctrl) == 0x2c0);
const _: () = assert!(core::mem::offset_of!(MyTeamFacilitiesComponent, team_count) == 0x2c8);

#[repr(C)]
pub struct MyTeamFacility {
    pub facility_type: u8,
    _pad_0x1: [u8; 0x7],
    pub tree: *mut FacilityPerkTree,
    _pad_0x10: [u8; 0x40],
    pub workforce_cap: i32,
    pub workforce_cap_set: u8,
    _pad_0x55: [u8; 0x183],
    pub highest_phase_seen: i32,
    _pad_0x1dc: [u8; 0x1c],
}
const _: () = assert!(core::mem::offset_of!(MyTeamFacility, facility_type) == 0x0);
const _: () = assert!(core::mem::offset_of!(MyTeamFacility, tree) == 0x8);
const _: () = assert!(core::mem::offset_of!(MyTeamFacility, workforce_cap) == 0x50);
const _: () = assert!(core::mem::offset_of!(MyTeamFacility, workforce_cap_set) == 0x54);
const _: () = assert!(core::mem::offset_of!(MyTeamFacility, highest_phase_seen) == 0x1d8);
const _: () = assert!(core::mem::size_of::<MyTeamFacility>() == 0x1f8);

#[repr(C)]
pub struct NeAllocator {
    pub vtable: *mut NeAllocatorVtbl,
}
const _: () = assert!(core::mem::offset_of!(NeAllocator, vtable) == 0x0);

#[repr(C)]
pub struct NeAllocatorVtbl {
    _pad_0x0: [u8; 0x18],
    pub alloc: *mut c_void,
    _pad_0x20: [u8; 0x18],
    pub free: *mut c_void,
}
const _: () = assert!(core::mem::offset_of!(NeAllocatorVtbl, alloc) == 0x18);
const _: () = assert!(core::mem::offset_of!(NeAllocatorVtbl, free) == 0x38);

#[repr(C)]
pub struct NeBagCodec {
    pub vtable: *mut *mut c_void,
    pub stream: *mut c_void,
}
const _: () = assert!(core::mem::offset_of!(NeBagCodec, vtable) == 0x0);
const _: () = assert!(core::mem::offset_of!(NeBagCodec, stream) == 0x8);

#[repr(C)]
pub struct NeCountingStream {
    pub vtable: *mut *mut c_void,
    _pad_0x8: [u8; 0x20],
    pub count: u64,
}
const _: () = assert!(core::mem::offset_of!(NeCountingStream, vtable) == 0x0);
const _: () = assert!(core::mem::offset_of!(NeCountingStream, count) == 0x28);

#[repr(C)]
pub struct NeLogGate {
    pub channel_hashes: [u32; 373],
    pub enabled: u8,
}
const _: () = assert!(core::mem::offset_of!(NeLogGate, channel_hashes) == 0x0);
const _: () = assert!(core::mem::offset_of!(NeLogGate, enabled) == 0x5d4);

#[repr(C)]
pub struct NeMemWriteStream {
    pub vtable: *mut *mut c_void,
    pub data: *mut u8,
    pub capacity: u64,
    pub position: u64,
    pub size: u64,
}
const _: () = assert!(core::mem::offset_of!(NeMemWriteStream, vtable) == 0x0);
const _: () = assert!(core::mem::offset_of!(NeMemWriteStream, data) == 0x8);
const _: () = assert!(core::mem::offset_of!(NeMemWriteStream, capacity) == 0x10);
const _: () = assert!(core::mem::offset_of!(NeMemWriteStream, position) == 0x18);
const _: () = assert!(core::mem::offset_of!(NeMemWriteStream, size) == 0x20);

#[repr(C)]
pub struct NeMessageBus {
    _pad_0x0: [u8; 0x68],
    pub active: u8,
}
const _: () = assert!(core::mem::offset_of!(NeMessageBus, active) == 0x68);

#[repr(C)]
pub struct NeOptionalVec {
    pub alloc: *mut NeAllocator,
    pub begin: *mut c_void,
    pub end: *mut c_void,
    pub count: u64,
    pub engaged: u8,
    _pad_0x21: [u8; 0x7],
}
const _: () = assert!(core::mem::offset_of!(NeOptionalVec, alloc) == 0x0);
const _: () = assert!(core::mem::offset_of!(NeOptionalVec, begin) == 0x8);
const _: () = assert!(core::mem::offset_of!(NeOptionalVec, end) == 0x10);
const _: () = assert!(core::mem::offset_of!(NeOptionalVec, count) == 0x18);
const _: () = assert!(core::mem::offset_of!(NeOptionalVec, engaged) == 0x20);
const _: () = assert!(core::mem::size_of::<NeOptionalVec>() == 0x28);

#[repr(C)]
pub struct NePropertyTypeVtbl {
    _pad_0x0: [u8; 0x10],
    pub id: *mut c_void,
    _pad_0x18: [u8; 0x8],
    pub key_offset: *mut c_void,
}
const _: () = assert!(core::mem::offset_of!(NePropertyTypeVtbl, id) == 0x10);
const _: () = assert!(core::mem::offset_of!(NePropertyTypeVtbl, key_offset) == 0x20);

#[repr(C)]
pub struct NePropertyVtbl {
    _pad_0x0: [u8; 0x8],
    pub r#type: *mut c_void,
    _pad_0x10: [u8; 0x28],
    pub value: *mut c_void,
}
const _: () = assert!(core::mem::offset_of!(NePropertyVtbl, r#type) == 0x8);
const _: () = assert!(core::mem::offset_of!(NePropertyVtbl, value) == 0x38);

#[repr(C)]
pub struct NeSharedCtrl {
    pub vtable: *mut *mut c_void,
    pub strong: i32,
    pub weak: i32,
}
const _: () = assert!(core::mem::offset_of!(NeSharedCtrl, vtable) == 0x0);
const _: () = assert!(core::mem::offset_of!(NeSharedCtrl, strong) == 0x8);
const _: () = assert!(core::mem::offset_of!(NeSharedCtrl, weak) == 0xc);

#[repr(C)]
pub struct NeStreamVtbl {
    _pad_0x0: [u8; 0x48],
    pub write: *mut c_void,
    pub write_length: *mut c_void,
}
const _: () = assert!(core::mem::offset_of!(NeStreamVtbl, write) == 0x48);
const _: () = assert!(core::mem::offset_of!(NeStreamVtbl, write_length) == 0x50);

#[repr(C)]
pub struct RenownAwardedMessage {
    pub vtable: *mut *mut c_void,
    _pad_0x8: [u8; 0x8],
    pub team: u32,
    pub amount: f32,
}
const _: () = assert!(core::mem::offset_of!(RenownAwardedMessage, vtable) == 0x0);
const _: () = assert!(core::mem::offset_of!(RenownAwardedMessage, team) == 0x10);
const _: () = assert!(core::mem::offset_of!(RenownAwardedMessage, amount) == 0x14);

#[repr(C)]
pub struct RenownConfig {
    _pad_0x0: [u8; 0x8],
    pub tiers_begin: *mut u32,
    pub tiers_end: *mut u32,
}
const _: () = assert!(core::mem::offset_of!(RenownConfig, tiers_begin) == 0x8);
const _: () = assert!(core::mem::offset_of!(RenownConfig, tiers_end) == 0x10);

#[repr(C)]
pub struct ResourcePointsComponent {
    _pad_0x0: [u8; 0x4a8],
    pub wallet_entity: *mut CareerEntity,
    pub wallet_mode: u8,
    _pad_0x4b1: [u8; 0x7],
    pub driver_wallets: *mut c_void,
    _pad_0x4c0: [u8; 0x68],
    pub team_wallets: *mut c_void,
}
const _: () = assert!(core::mem::offset_of!(ResourcePointsComponent, wallet_entity) == 0x4a8);
const _: () = assert!(core::mem::offset_of!(ResourcePointsComponent, wallet_mode) == 0x4b0);
const _: () = assert!(core::mem::offset_of!(ResourcePointsComponent, driver_wallets) == 0x4b8);
const _: () = assert!(core::mem::offset_of!(ResourcePointsComponent, team_wallets) == 0x528);

#[repr(C)]
pub struct SaveDataContainer {
    pub vtable: *mut *mut c_void,
    _pad_0x8: [u8; 0x78],
    pub name: *const c_char,
    _pad_0x88: [u8; 0x78],
    pub retain_unknown: u8,
    _pad_0x101: [u8; 0x7],
    pub retain_unknown_key: u64,
    _pad_0x110: [u8; 0xb8],
    pub retained_count: u64,
}
const _: () = assert!(core::mem::offset_of!(SaveDataContainer, vtable) == 0x0);
const _: () = assert!(core::mem::offset_of!(SaveDataContainer, name) == 0x80);
const _: () = assert!(core::mem::offset_of!(SaveDataContainer, retain_unknown) == 0x100);
const _: () = assert!(core::mem::offset_of!(SaveDataContainer, retain_unknown_key) == 0x108);
const _: () = assert!(core::mem::offset_of!(SaveDataContainer, retained_count) == 0x1c8);

#[repr(C)]
pub struct ScheduleComponent {
    _pad_0x0: [u8; 0x410],
    pub seasons_begin: *mut ScheduleSeason,
    pub seasons_end: *mut ScheduleSeason,
}
const _: () = assert!(core::mem::offset_of!(ScheduleComponent, seasons_begin) == 0x410);
const _: () = assert!(core::mem::offset_of!(ScheduleComponent, seasons_end) == 0x418);

#[repr(C)]
pub struct ScheduleSeason {
    _pad_0x0: [u8; 0x40],
    pub driver_calendars_begin: *mut DriverCalendar,
    pub driver_calendars_end: *mut DriverCalendar,
    _pad_0x50: [u8; 0x30],
}
const _: () = assert!(core::mem::offset_of!(ScheduleSeason, driver_calendars_begin) == 0x40);
const _: () = assert!(core::mem::offset_of!(ScheduleSeason, driver_calendars_end) == 0x48);
const _: () = assert!(core::mem::size_of::<ScheduleSeason>() == 0x80);

#[repr(C)]
pub struct Season {
    pub vtable: *mut *mut c_void,
    _pad_0x8: [u8; 0x218],
    pub name: *mut c_char,
    _pad_0x228: [u8; 0x58],
    pub allocator: *mut NeAllocator,
    pub weekends_begin: *mut *mut Weekend,
    pub weekends_end: *mut *mut Weekend,
    _pad_0x298: [u8; 0x8],
    pub current_weekend: u32,
    _pad_0x2a4: [u8; 0x4],
    pub standings_vtable: *mut *mut c_void,
    _pad_0x2b0: [u8; 0x288],
    pub completion_state: u32,
    _pad_0x53c: [u8; 0x14],
}
const _: () = assert!(core::mem::offset_of!(Season, vtable) == 0x0);
const _: () = assert!(core::mem::offset_of!(Season, name) == 0x220);
const _: () = assert!(core::mem::offset_of!(Season, allocator) == 0x280);
const _: () = assert!(core::mem::offset_of!(Season, weekends_begin) == 0x288);
const _: () = assert!(core::mem::offset_of!(Season, weekends_end) == 0x290);
const _: () = assert!(core::mem::offset_of!(Season, current_weekend) == 0x2a0);
const _: () = assert!(core::mem::offset_of!(Season, standings_vtable) == 0x2a8);
const _: () = assert!(core::mem::offset_of!(Season, completion_state) == 0x538);
const _: () = assert!(core::mem::size_of::<Season>() == 0x550);

#[repr(C)]
pub struct SecretMeeting {
    pub id: u32,
    _pad_0x4: [u8; 0x10],
    pub reason: u8,
    _pad_0x15: [u8; 0xb],
    pub team: u32,
}
const _: () = assert!(core::mem::offset_of!(SecretMeeting, id) == 0x0);
const _: () = assert!(core::mem::offset_of!(SecretMeeting, reason) == 0x14);
const _: () = assert!(core::mem::offset_of!(SecretMeeting, team) == 0x20);
const _: () = assert!(core::mem::size_of::<SecretMeeting>() == 0x24);

#[repr(C)]
pub struct Session {
    _pad_0x0: [u8; 0x8],
    pub session_id: u32,
    _pad_0xc: [u8; 0x4],
    pub completion: u32,
    _pad_0x14: [u8; 0x4],
    pub participants_allocator: *mut NeAllocator,
    pub participants_begin: *mut EntrantRecord,
    pub participants_end: *mut EntrantRecord,
    pub participants_capacity: u64,
    _pad_0x38: [u8; 0x68],
}
const _: () = assert!(core::mem::offset_of!(Session, session_id) == 0x8);
const _: () = assert!(core::mem::offset_of!(Session, completion) == 0x10);
const _: () = assert!(core::mem::offset_of!(Session, participants_allocator) == 0x18);
const _: () = assert!(core::mem::offset_of!(Session, participants_begin) == 0x20);
const _: () = assert!(core::mem::offset_of!(Session, participants_end) == 0x28);
const _: () = assert!(core::mem::offset_of!(Session, participants_capacity) == 0x30);
const _: () = assert!(core::mem::size_of::<Session>() == 0xa0);

#[repr(C)]
pub struct SponsorEntry {
    _pad_0x0: [u8; 0x10],
    pub name: *const c_char,
    _pad_0x18: [u8; 0x10],
    pub stat_id: u16,
    _pad_0x2a: [u8; 0x6e],
    pub levels_begin: *mut SponsorLoyaltyLevel,
    pub levels_end: *mut SponsorLoyaltyLevel,
    _pad_0xa8: [u8; 0x30],
    pub slot: u8,
    _pad_0xd9: [u8; 0x3b],
    pub loyalty_xp: i32,
    _pad_0x118: [u8; 0x40],
    pub active: u8,
    _pad_0x159: [u8; 0x7],
}
const _: () = assert!(core::mem::offset_of!(SponsorEntry, name) == 0x10);
const _: () = assert!(core::mem::offset_of!(SponsorEntry, stat_id) == 0x28);
const _: () = assert!(core::mem::offset_of!(SponsorEntry, levels_begin) == 0x98);
const _: () = assert!(core::mem::offset_of!(SponsorEntry, levels_end) == 0xa0);
const _: () = assert!(core::mem::offset_of!(SponsorEntry, slot) == 0xd8);
const _: () = assert!(core::mem::offset_of!(SponsorEntry, loyalty_xp) == 0x114);
const _: () = assert!(core::mem::offset_of!(SponsorEntry, active) == 0x158);
const _: () = assert!(core::mem::size_of::<SponsorEntry>() == 0x160);

#[repr(C)]
pub struct SponsorLoyaltyLevel {
    pub claimed: u8,
    _pad_0x1: [u8; 0x3],
    pub xp_threshold: i32,
    _pad_0x8: [u8; 0x70],
}
const _: () = assert!(core::mem::offset_of!(SponsorLoyaltyLevel, claimed) == 0x0);
const _: () = assert!(core::mem::offset_of!(SponsorLoyaltyLevel, xp_threshold) == 0x4);
const _: () = assert!(core::mem::size_of::<SponsorLoyaltyLevel>() == 0x78);

#[repr(C)]
pub struct SponsorLoyaltyXpMessage {
    pub vtable: *mut *mut c_void,
    _pad_0x8: [u8; 0x8],
    pub team: u32,
    pub amount: i32,
    pub reason: u8,
}
const _: () = assert!(core::mem::offset_of!(SponsorLoyaltyXpMessage, vtable) == 0x0);
const _: () = assert!(core::mem::offset_of!(SponsorLoyaltyXpMessage, team) == 0x10);
const _: () = assert!(core::mem::offset_of!(SponsorLoyaltyXpMessage, amount) == 0x14);
const _: () = assert!(core::mem::offset_of!(SponsorLoyaltyXpMessage, reason) == 0x18);

#[repr(C)]
pub struct SponsorTeamRecord {
    _pad_0x0: [u8; 0x8],
    pub team: u32,
    _pad_0xc: [u8; 0xc],
    pub sponsors_begin: *mut SponsorEntry,
    pub sponsors_end: *mut SponsorEntry,
    _pad_0x28: [u8; 0x60],
}
const _: () = assert!(core::mem::offset_of!(SponsorTeamRecord, team) == 0x8);
const _: () = assert!(core::mem::offset_of!(SponsorTeamRecord, sponsors_begin) == 0x18);
const _: () = assert!(core::mem::offset_of!(SponsorTeamRecord, sponsors_end) == 0x20);
const _: () = assert!(core::mem::size_of::<SponsorTeamRecord>() == 0x88);

#[repr(C)]
pub struct StandingsPoints {
    pub key: u32,
    pub points: i32,
}
const _: () = assert!(core::mem::offset_of!(StandingsPoints, key) == 0x0);
const _: () = assert!(core::mem::offset_of!(StandingsPoints, points) == 0x4);
const _: () = assert!(core::mem::size_of::<StandingsPoints>() == 0x8);

#[repr(C)]
pub struct StandingsResult {
    pub key: u32,
    pub position: i32,
    pub fastest_lap_rank: i32,
    pub status: u32,
}
const _: () = assert!(core::mem::offset_of!(StandingsResult, key) == 0x0);
const _: () = assert!(core::mem::offset_of!(StandingsResult, position) == 0x4);
const _: () = assert!(core::mem::offset_of!(StandingsResult, fastest_lap_rank) == 0x8);
const _: () = assert!(core::mem::offset_of!(StandingsResult, status) == 0xc);
const _: () = assert!(core::mem::size_of::<StandingsResult>() == 0x10);

#[repr(C)]
pub struct StandingsRound {
    _pad_0x0: [u8; 0x8],
    pub sessions_begin: *mut StandingsSession,
    pub sessions_end: *mut StandingsSession,
    _pad_0x18: [u8; 0x8],
}
const _: () = assert!(core::mem::offset_of!(StandingsRound, sessions_begin) == 0x8);
const _: () = assert!(core::mem::offset_of!(StandingsRound, sessions_end) == 0x10);
const _: () = assert!(core::mem::size_of::<StandingsRound>() == 0x20);

#[repr(C)]
pub struct StandingsSession {
    _pad_0x0: [u8; 0x8],
    pub records_begin: *mut c_void,
    pub records_end: *mut c_void,
    _pad_0x18: [u8; 0x8],
    pub session_id: u32,
    _pad_0x24: [u8; 0x4],
}
const _: () = assert!(core::mem::offset_of!(StandingsSession, records_begin) == 0x8);
const _: () = assert!(core::mem::offset_of!(StandingsSession, records_end) == 0x10);
const _: () = assert!(core::mem::offset_of!(StandingsSession, session_id) == 0x20);
const _: () = assert!(core::mem::size_of::<StandingsSession>() == 0x28);

#[repr(C)]
pub struct StatInfo {
    _pad_0x0: [u8; 0x8],
    pub stat_id: u16,
    _pad_0xa: [u8; 0x126],
    pub weight: f32,
    pub default_value: f32,
}
const _: () = assert!(core::mem::offset_of!(StatInfo, stat_id) == 0x8);
const _: () = assert!(core::mem::offset_of!(StatInfo, weight) == 0x130);
const _: () = assert!(core::mem::offset_of!(StatInfo, default_value) == 0x134);

#[repr(C)]
pub struct TeamRenownData {
    pub team: u32,
    pub points: f32,
    _pad_0x8: [u8; 0x10],
    pub config: *mut RenownConfig,
    _pad_0x20: [u8; 0x120],
}
const _: () = assert!(core::mem::offset_of!(TeamRenownData, team) == 0x0);
const _: () = assert!(core::mem::offset_of!(TeamRenownData, points) == 0x4);
const _: () = assert!(core::mem::offset_of!(TeamRenownData, config) == 0x18);
const _: () = assert!(core::mem::size_of::<TeamRenownData>() == 0x140);

#[repr(C)]
pub struct Tile {
    _pad_0x0: [u8; 0xd0],
    pub item: *mut HomeItem,
}
const _: () = assert!(core::mem::offset_of!(Tile, item) == 0xd0);

#[repr(C)]
pub struct TileEntry {
    _pad_0x0: [u8; 0x8],
    pub tile: *mut Tile,
}
const _: () = assert!(core::mem::offset_of!(TileEntry, tile) == 0x8);

#[repr(C)]
pub struct TileList {
    _pad_0x0: [u8; 0x78],
    pub items_begin: *mut TileEntry,
    pub items_end: *mut TileEntry,
}
const _: () = assert!(core::mem::offset_of!(TileList, items_begin) == 0x78);
const _: () = assert!(core::mem::offset_of!(TileList, items_end) == 0x80);

#[repr(C)]
pub struct TimeOfDayClock {
    _pad_0x0: [u8; 0x68],
    pub time_a: f64,
    pub time_b: f64,
    _pad_0x78: [u8; 0x8],
    pub time_scale: f32,
    pub length: f32,
    _pad_0x88: [u8; 0x200],
}
const _: () = assert!(core::mem::offset_of!(TimeOfDayClock, time_a) == 0x68);
const _: () = assert!(core::mem::offset_of!(TimeOfDayClock, time_b) == 0x70);
const _: () = assert!(core::mem::offset_of!(TimeOfDayClock, time_scale) == 0x80);
const _: () = assert!(core::mem::offset_of!(TimeOfDayClock, length) == 0x84);
const _: () = assert!(core::mem::size_of::<TimeOfDayClock>() == 0x288);

#[repr(C)]
pub struct TimeOfDayOwner {
    _pad_0x0: [u8; 0x2d0],
    pub tod: *mut TimeOfDaySystem,
}
const _: () = assert!(core::mem::offset_of!(TimeOfDayOwner, tod) == 0x2d0);

#[repr(C)]
pub struct WeatherState {
    pub r#type: u32,
    pub intensity: f32,
    pub end: f32,
    pub transition: f32,
}
const _: () = assert!(core::mem::offset_of!(WeatherState, r#type) == 0x0);
const _: () = assert!(core::mem::offset_of!(WeatherState, intensity) == 0x4);
const _: () = assert!(core::mem::offset_of!(WeatherState, end) == 0x8);
const _: () = assert!(core::mem::offset_of!(WeatherState, transition) == 0xc);
const _: () = assert!(core::mem::size_of::<WeatherState>() == 0x10);

#[repr(C)]
pub struct WeatherDataset {
    _pad_0x0: [u8; 0xb90],
    pub states: [WeatherState; 38],
    pub state_total: u32,
    _pad_0xdf4: [u8; 0x4],
    pub state_count: u32,
    _pad_0xdfc: [u8; 0x4],
}
const _: () = assert!(core::mem::offset_of!(WeatherDataset, states) == 0xb90);
const _: () = assert!(core::mem::offset_of!(WeatherDataset, state_total) == 0xdf0);
const _: () = assert!(core::mem::offset_of!(WeatherDataset, state_count) == 0xdf8);
const _: () = assert!(core::mem::size_of::<WeatherDataset>() == 0xe00);

#[repr(C)]
pub struct WeatherTypeRange {
    pub weight: f32,
    _pad_0x4: [u8; 0x8],
    pub intensity_min: f32,
    pub intensity_max: f32,
}
const _: () = assert!(core::mem::offset_of!(WeatherTypeRange, weight) == 0x0);
const _: () = assert!(core::mem::offset_of!(WeatherTypeRange, intensity_min) == 0xc);
const _: () = assert!(core::mem::offset_of!(WeatherTypeRange, intensity_max) == 0x10);
const _: () = assert!(core::mem::size_of::<WeatherTypeRange>() == 0x14);

#[repr(C)]
pub struct TimeOfDaySystem {
    _pad_0x0: [u8; 0x2c8],
    pub clocks: [TimeOfDayClock; 10],
    _pad_0x1c18: [u8; 0xa3c],
    pub scripted_scenario: u8,
    _pad_0x2655: [u8; 0x6f],
    pub active_dataset: u8,
    _pad_0x26c5: [u8; 0x13],
    pub datasets: [WeatherDataset; 10],
    _pad_0xb2d8: [u8; 0x64],
    pub seed: u32,
    _pad_0xb340: [u8; 0x10],
    pub dataset_length: [f32; 10],
    pub type_ranges: [WeatherTypeRange; 6],
    _pad_0xb3f0: [u8; 0x36],
    pub exact_timing: u8,
    _pad_0xb427: [u8; 0x2],
    pub ready_a: u8,
    pub ready_b: u8,
    _pad_0xb42b: [u8; 0x2],
    pub random_override: u8,
    _pad_0xb42e: [u8; 0x4],
    pub busy: u8,
    _pad_0xb433: [u8; 0x3],
    pub override_active: u8,
}
const _: () = assert!(core::mem::offset_of!(TimeOfDaySystem, clocks) == 0x2c8);
const _: () = assert!(core::mem::offset_of!(TimeOfDaySystem, scripted_scenario) == 0x2654);
const _: () = assert!(core::mem::offset_of!(TimeOfDaySystem, active_dataset) == 0x26c4);
const _: () = assert!(core::mem::offset_of!(TimeOfDaySystem, datasets) == 0x26d8);
const _: () = assert!(core::mem::offset_of!(TimeOfDaySystem, seed) == 0xb33c);
const _: () = assert!(core::mem::offset_of!(TimeOfDaySystem, dataset_length) == 0xb350);
const _: () = assert!(core::mem::offset_of!(TimeOfDaySystem, type_ranges) == 0xb378);
const _: () = assert!(core::mem::offset_of!(TimeOfDaySystem, exact_timing) == 0xb426);
const _: () = assert!(core::mem::offset_of!(TimeOfDaySystem, ready_a) == 0xb429);
const _: () = assert!(core::mem::offset_of!(TimeOfDaySystem, ready_b) == 0xb42a);
const _: () = assert!(core::mem::offset_of!(TimeOfDaySystem, random_override) == 0xb42d);
const _: () = assert!(core::mem::offset_of!(TimeOfDaySystem, busy) == 0xb432);
const _: () = assert!(core::mem::offset_of!(TimeOfDaySystem, override_active) == 0xb436);

#[repr(C)]
pub struct TrackDataRecord {
    _pad_0x0: [u8; 0x1],
    pub index: u8,
    _pad_0x2: [u8; 0xce],
    pub name: *mut c_char,
}
const _: () = assert!(core::mem::offset_of!(TrackDataRecord, index) == 0x1);
const _: () = assert!(core::mem::offset_of!(TrackDataRecord, name) == 0xd0);

#[repr(C)]
pub struct TrackDataSet {
    _pad_0x0: [u8; 0x8],
    pub tracks_begin: *mut c_void,
    pub tracks_end: *mut c_void,
    _pad_0x18: [u8; 0x8],
    pub mode_mask: u32,
    pub season_mask: u32,
    pub format_mask: u32,
}
const _: () = assert!(core::mem::offset_of!(TrackDataSet, tracks_begin) == 0x8);
const _: () = assert!(core::mem::offset_of!(TrackDataSet, tracks_end) == 0x10);
const _: () = assert!(core::mem::offset_of!(TrackDataSet, mode_mask) == 0x20);
const _: () = assert!(core::mem::offset_of!(TrackDataSet, season_mask) == 0x24);
const _: () = assert!(core::mem::offset_of!(TrackDataSet, format_mask) == 0x28);

#[repr(C)]
pub struct TrackDataSystem {
    _pad_0x0: [u8; 0x68],
    pub sets_begin: *mut TrackDataSet,
    pub sets_end: *mut TrackDataSet,
}
const _: () = assert!(core::mem::offset_of!(TrackDataSystem, sets_begin) == 0x68);
const _: () = assert!(core::mem::offset_of!(TrackDataSystem, sets_end) == 0x70);

#[repr(C)]
pub struct TrackDataTrack {
    pub record: *mut TrackDataRecord,
    _pad_0x8: [u8; 0xe8],
    pub fuel_burn_per_lap: f32,
    _pad_0xf4: [u8; 0xc],
    pub race_fuel: [u8; 156],
    _pad_0x19c: [u8; 0x4],
    pub race_fuel_count: u64,
    _pad_0x1a8: [u8; 0xc],
    pub qualifying_one_shot: f32,
    _pad_0x1b8: [u8; 0x4],
    pub qualifying_short: f32,
    pub qualifying_full: f32,
}
const _: () = assert!(core::mem::offset_of!(TrackDataTrack, record) == 0x0);
const _: () = assert!(core::mem::offset_of!(TrackDataTrack, fuel_burn_per_lap) == 0xf0);
const _: () = assert!(core::mem::offset_of!(TrackDataTrack, race_fuel) == 0x100);
const _: () = assert!(core::mem::offset_of!(TrackDataTrack, race_fuel_count) == 0x1a0);
const _: () = assert!(core::mem::offset_of!(TrackDataTrack, qualifying_one_shot) == 0x1b4);
const _: () = assert!(core::mem::offset_of!(TrackDataTrack, qualifying_short) == 0x1bc);
const _: () = assert!(core::mem::offset_of!(TrackDataTrack, qualifying_full) == 0x1c0);

#[repr(C)]
pub struct UpgradeInstance {
    _pad_0x0: [u8; 0x10],
    pub name: *mut c_char,
    _pad_0x18: [u8; 0x40],
    pub node_hash: u32,
    pub cost: i32,
    pub cost_to_develop: i32,
    _pad_0x64: [u8; 0x4],
    pub num_days_to_research: i32,
    pub num_days_to_develop: i32,
    _pad_0x70: [u8; 0x2],
    pub department: u8,
    _pad_0x73: [u8; 0x1],
    pub unlock_at_facility_level: i32,
    _pad_0x78: [u8; 0x4],
    pub row_flag: u8,
    _pad_0x7d: [u8; 0x1],
    pub research_start_day: u16,
    pub research_end_day: u16,
    _pad_0x82: [u8; 0x16],
    pub develop_start_day: u16,
    pub develop_end_day: u16,
    _pad_0x9c: [u8; 0x24],
    pub states_begin: *mut UpgradeStateEntry,
    pub states_end: *mut UpgradeStateEntry,
    _pad_0xd0: [u8; 0x10],
}
const _: () = assert!(core::mem::offset_of!(UpgradeInstance, name) == 0x10);
const _: () = assert!(core::mem::offset_of!(UpgradeInstance, node_hash) == 0x58);
const _: () = assert!(core::mem::offset_of!(UpgradeInstance, cost) == 0x5c);
const _: () = assert!(core::mem::offset_of!(UpgradeInstance, cost_to_develop) == 0x60);
const _: () = assert!(core::mem::offset_of!(UpgradeInstance, num_days_to_research) == 0x68);
const _: () = assert!(core::mem::offset_of!(UpgradeInstance, num_days_to_develop) == 0x6c);
const _: () = assert!(core::mem::offset_of!(UpgradeInstance, department) == 0x72);
const _: () = assert!(core::mem::offset_of!(UpgradeInstance, unlock_at_facility_level) == 0x74);
const _: () = assert!(core::mem::offset_of!(UpgradeInstance, row_flag) == 0x7c);
const _: () = assert!(core::mem::offset_of!(UpgradeInstance, research_start_day) == 0x7e);
const _: () = assert!(core::mem::offset_of!(UpgradeInstance, research_end_day) == 0x80);
const _: () = assert!(core::mem::offset_of!(UpgradeInstance, develop_start_day) == 0x98);
const _: () = assert!(core::mem::offset_of!(UpgradeInstance, develop_end_day) == 0x9a);
const _: () = assert!(core::mem::offset_of!(UpgradeInstance, states_begin) == 0xc0);
const _: () = assert!(core::mem::offset_of!(UpgradeInstance, states_end) == 0xc8);
const _: () = assert!(core::mem::size_of::<UpgradeInstance>() == 0xe0);

#[repr(C)]
pub struct UpgradeStateEntry {
    pub owner: u32,
    pub day: u16,
    _pad_0x6: [u8; 0x2],
    pub state: u8,
    pub adaptation: u8,
    _pad_0xa: [u8; 0x2],
    pub enabled: u8,
    pub facility_locked: u8,
    _pad_0xe: [u8; 0x2],
}
const _: () = assert!(core::mem::offset_of!(UpgradeStateEntry, owner) == 0x0);
const _: () = assert!(core::mem::offset_of!(UpgradeStateEntry, day) == 0x4);
const _: () = assert!(core::mem::offset_of!(UpgradeStateEntry, state) == 0x8);
const _: () = assert!(core::mem::offset_of!(UpgradeStateEntry, adaptation) == 0x9);
const _: () = assert!(core::mem::offset_of!(UpgradeStateEntry, enabled) == 0xc);
const _: () = assert!(core::mem::offset_of!(UpgradeStateEntry, facility_locked) == 0xd);
const _: () = assert!(core::mem::size_of::<UpgradeStateEntry>() == 0x10);

#[repr(C)]
pub struct UpgradeTree {
    _pad_0x0: [u8; 0x10],
    pub instances_begin: *mut UpgradeInstance,
    pub instances_end: *mut UpgradeInstance,
    _pad_0x20: [u8; 0xc20],
}
const _: () = assert!(core::mem::offset_of!(UpgradeTree, instances_begin) == 0x10);
const _: () = assert!(core::mem::offset_of!(UpgradeTree, instances_end) == 0x18);
const _: () = assert!(core::mem::size_of::<UpgradeTree>() == 0xc40);

#[repr(C)]
pub struct Upgrades {
    _pad_0x0: [u8; 0x8],
    pub mode: u8,
    _pad_0x9: [u8; 0x3],
    pub team_key: u32,
    _pad_0x10: [u8; 0xd0],
    pub trees: [UpgradeTree; 2],
    pub tree_count: u64,
    _pad_0x1968: [u8; 0x140],
    pub owner_cache: u32,
    _pad_0x1aac: [u8; 0x64],
}
const _: () = assert!(core::mem::offset_of!(Upgrades, mode) == 0x8);
const _: () = assert!(core::mem::offset_of!(Upgrades, team_key) == 0xc);
const _: () = assert!(core::mem::offset_of!(Upgrades, trees) == 0xe0);
const _: () = assert!(core::mem::offset_of!(Upgrades, tree_count) == 0x1960);
const _: () = assert!(core::mem::offset_of!(Upgrades, owner_cache) == 0x1aa8);
const _: () = assert!(core::mem::size_of::<Upgrades>() == 0x1b10);

#[repr(C)]
pub struct UpgradesComponent {
    _pad_0x0: [u8; 0x298],
    pub upgrades_begin: *mut *mut Upgrades,
    pub upgrades_end: *mut *mut Upgrades,
    _pad_0x2a8: [u8; 0x18],
    pub career_time: *mut CareerTimeComponent,
}
const _: () = assert!(core::mem::offset_of!(UpgradesComponent, upgrades_begin) == 0x298);
const _: () = assert!(core::mem::offset_of!(UpgradesComponent, upgrades_end) == 0x2a0);
const _: () = assert!(core::mem::offset_of!(UpgradesComponent, career_time) == 0x2c0);

#[repr(C)]
pub struct WeatherAndTimeOfDayComponent {
    _pad_0x0: [u8; 0x2a0],
    pub weather_system: *mut GameWeatherSystem,
    _pad_0x2a8: [u8; 0x1c],
    pub random_seed: u32,
}
const _: () = assert!(core::mem::offset_of!(WeatherAndTimeOfDayComponent, weather_system) == 0x2a0);
const _: () = assert!(core::mem::offset_of!(WeatherAndTimeOfDayComponent, random_seed) == 0x2c4);

#[repr(C)]
pub struct WeatherSessionSettings {
    _pad_0x0: [u8; 0x4],
    pub session_type: u32,
    _pad_0x8: [u8; 0xb0c],
    pub mode: u32,
    pub exact_timing: u8,
}
const _: () = assert!(core::mem::offset_of!(WeatherSessionSettings, session_type) == 0x4);
const _: () = assert!(core::mem::offset_of!(WeatherSessionSettings, mode) == 0xb14);
const _: () = assert!(core::mem::offset_of!(WeatherSessionSettings, exact_timing) == 0xb18);

#[repr(C)]
pub struct Weekend {
    pub vtable: *mut *mut c_void,
    _pad_0x8: [u8; 0x20],
    pub name: *mut c_char,
    _pad_0x30: [u8; 0x1f0],
    pub sessions_begin: *mut Session,
    pub sessions_end: *mut Session,
    _pad_0x230: [u8; 0x8],
    pub current_session: u32,
    pub progress_state: u32,
    _pad_0x240: [u8; 0x38],
    pub track_stat_id: u8,
    _pad_0x279: [u8; 0x277],
}
const _: () = assert!(core::mem::offset_of!(Weekend, vtable) == 0x0);
const _: () = assert!(core::mem::offset_of!(Weekend, name) == 0x28);
const _: () = assert!(core::mem::offset_of!(Weekend, sessions_begin) == 0x220);
const _: () = assert!(core::mem::offset_of!(Weekend, sessions_end) == 0x228);
const _: () = assert!(core::mem::offset_of!(Weekend, current_session) == 0x238);
const _: () = assert!(core::mem::offset_of!(Weekend, progress_state) == 0x23c);
const _: () = assert!(core::mem::offset_of!(Weekend, track_stat_id) == 0x278);
const _: () = assert!(core::mem::size_of::<Weekend>() == 0x4f0);

pub type Car_Reset_LastFittedUpgrade_t = unsafe extern "C" fn(inst: *mut c_void);
pub type Career_Add_SeasonWeekend_t = unsafe extern "C" fn(season: *mut c_void, weekend: *mut c_void) -> u64;
pub type Career_Add_TeamModifier_t = unsafe extern "C" fn(list: *mut c_void, key: u32, value: f32);
pub type Career_Adjust_RpDriverWallet_t =
    unsafe extern "C" fn(wallets: *mut c_void, driver_key: u32, delta: i32);
pub type Career_Adjust_RpTeamWallet_t = unsafe extern "C" fn(wallets: *mut c_void, team_key: u32, delta: i32);
pub type Career_Build_DriverContract_t = unsafe extern "C" fn(
    contracts: *mut c_void,
    contract: *mut c_void,
    driver_key: u32,
    team_key: u32,
    r#type: u8,
    target: i32,
    seasons: i32,
) -> *mut c_void;
pub type Career_Build_LineupChanges_t = unsafe extern "C" fn(
    out: *mut c_void,
    current_entrants: *const c_void,
    baseline_entrants: *const c_void,
) -> *mut c_void;
pub type Career_Calc_UpgradeCost_t =
    unsafe extern "C" fn(list: *mut c_void, inst: *mut c_void, mods: *mut c_void, option: u32) -> i32;
pub type Career_Cancel_UpgradeDevelopment_t =
    unsafe extern "C" fn(upgrades: *mut c_void, owner: u32, inst: *mut c_void, day: u32);
pub type Career_Cancel_UpgradeResearch_t =
    unsafe extern "C" fn(upgrades: *mut c_void, owner: u32, inst: *mut c_void, day: u32);
pub type Career_Complete_PerkTreeNode_t = unsafe extern "C" fn(node: *mut c_void) -> u8;
pub type Career_Construct_RnDDeptCard_t = unsafe extern "C" fn(
    card: *mut c_void,
    ctx: *mut c_void,
    builder: *mut c_void,
    allocator: *mut c_void,
    name: *const c_char,
    icon: *const c_char,
    dept: u8,
) -> *mut c_void;
pub type Career_ContractIntro_OnEnter_t = unsafe extern "C" fn(state: *mut c_void) -> u64;
pub type Career_ContractIntro_OnUpdate_t =
    unsafe extern "C" fn(state: *mut c_void, link: *mut u32) -> *mut u32;
pub type Career_ContractOutro_OnEnter_t = unsafe extern "C" fn(state: *mut c_void) -> u64;
pub type Career_ContractOutro_OnUpdate_t =
    unsafe extern "C" fn(state: *mut c_void, link: *mut u32) -> *mut u32;
pub type Career_ContractUpdate_OnEnter_t = unsafe extern "C" fn(state: *mut c_void) -> u64;
pub type Career_ContractUpdate_OnExit_t = unsafe extern "C" fn(state: *mut c_void) -> u64;
pub type Career_ContractUpdate_OnUpdate_t =
    unsafe extern "C" fn(state: *mut c_void, link: *mut u32) -> *mut u32;
pub type Career_Create_SeasonWeekend_t =
    unsafe extern "C" fn(season: *mut c_void, index: u64, track: u8, name: *const c_char) -> *mut c_void;
pub type Career_Decide_ContractNegotiation_t =
    unsafe extern "C" fn(state: *mut c_void, link: *mut u32) -> *mut u32;
pub type Career_DriverNumbers_CheckExisting_t = unsafe extern "C" fn(car_numbers: *mut c_void, driver: u32);
pub type Career_Eval_Curve_t = unsafe extern "C" fn(curve: *mut c_void, x: f32) -> f32;
pub type Career_Get_CareerLengthDays_Thunk_t = unsafe extern "C" fn(time: *mut c_void) -> u32;
pub type Career_Get_DriverOverallRating_t =
    unsafe extern "C" fn(self_: *mut c_void, data: *mut c_void, driver: *const u32, round: bool) -> f32;
pub type Career_Get_RpWallet_t = unsafe extern "C" fn(wallets: *mut c_void, out: *mut u64, driver_key: u32);
pub type Career_Get_TeamRenownData_t =
    unsafe extern "C" fn(rf: *mut c_void, out_weak: *mut c_void, team_key: u32) -> *mut c_void;
pub type Career_Needs_DriverContract_t = unsafe extern "C" fn(contracts: *mut c_void) -> bool;
pub type Career_Post_UpgradeFittedRatio_t =
    unsafe extern "C" fn(upgrades: *mut c_void, inst: *mut c_void, fitted: u8);
pub type Career_Rebuild_ChampionshipStandings_t =
    unsafe extern "C" fn(champ_list: *mut c_void, extra_points: *mut c_void, extra_e: *mut c_void);
pub type Career_Rebuild_SeasonStandings_t = unsafe extern "C" fn(
    season: *mut c_void,
    order_a: *mut c_void,
    order_b: *mut c_void,
    extra_points: *mut c_void,
    extra_e: *mut c_void,
);
pub type Career_Record_DriverMove_t =
    unsafe extern "C" fn(renewal: *mut c_void, driver: u32, old_team: u32, new_team: u32, reason: u8);
pub type Career_Refresh_RnDDeptMode_t = unsafe extern "C" fn(holder: *mut usize, mode: u32);
pub type Career_Refresh_UpgradeAvailability_t = unsafe extern "C" fn(upgrades: *mut c_void, day: u16);
pub type Career_Set_DriverContractState_t =
    unsafe extern "C" fn(contract: *mut c_void, day: u16, state: u8, value: u32);
pub type Career_Set_EntrantData_Thunk_t = unsafe extern "C" fn(
    entity: *mut c_void,
    entrant_id: *const c_void,
    data: *const u32,
    position: u16,
    flag: u8,
);
pub type Career_Set_UpgradeFacilityLocked_t = unsafe extern "C" fn(inst: *mut c_void, locked: u8, day: u16);
pub type Career_Set_UpgradesOwner_t = unsafe extern "C" fn(upgrades: *mut c_void, owner: u32);
pub type Career_Sign_DriverContract_t = unsafe extern "C" fn(
    contracts: *mut c_void,
    contract: *mut c_void,
    driver_key: u64,
    replaced_key: u32,
    notify: u8,
);
pub type Career_Start_UpgradeDevelopment_t = unsafe extern "C" fn(
    upgrades: *mut c_void,
    owner: u32,
    inst: *mut c_void,
    start_day: u16,
    end_day: u16,
    option: u8,
    dual: u8,
    roll_failure: u8,
    rng: *mut c_void,
    store_duration: u8,
);
pub type Career_Start_UpgradeResearch_t = unsafe extern "C" fn(
    upgrades: *mut c_void,
    owner: u32,
    inst: *mut c_void,
    start_day: u32,
    end_day: u16,
    option: u8,
    roll_failure: u8,
    rng: *mut c_void,
    store_duration: u8,
);
pub type Career_Teardown_Weekend_t = unsafe extern "C" fn(weekend: *mut c_void);
pub type FeHome_Add_CareerTile_t = unsafe extern "C" fn(list: *mut TileList, index: u8);
pub type FeHome_Handle_Command_t =
    unsafe extern "C" fn(vm: *mut FEHomeViewModel, cmd: *mut *mut NeStr) -> u64;
pub type FeTile_Add_EraTile_t = unsafe extern "C" fn(
    tiles: *mut c_void,
    alloc: *mut c_void,
    index: u8,
    node: *const c_char,
    name: *const c_char,
    enabled: u8,
    title: *const c_char,
    command: *const c_char,
    image: *const c_char,
    is_dlc: u8,
);
pub type FeTile_Build_List_t = unsafe extern "C" fn(
    self_: *mut TileList,
    ctx: *mut c_void,
    a3: *mut c_void,
    a4: *mut c_void,
    menu_kind: u8,
    sub_kind: u8,
    a7: u32,
);
pub type FeTile_CareerDlcItem_Populate_t =
    unsafe extern "C" fn(item: *mut HomeItem, node: *mut c_void, a7: u32);
pub type FeTile_Clear_EraTileSet_Thunk_t = unsafe extern "C" fn(tiles: *mut c_void);
pub type FeTile_Construct_CareerItem_t =
    unsafe extern "C" fn(item: *mut HomeItem, alloc: *mut NeAllocator, id: *const c_char) -> *mut HomeItem;
pub type FeTile_Is_EraScenarioAvailable_Thunk_t =
    unsafe extern "C" fn(mode: *mut c_void, setup_mode: *const c_char, entrant_list: *const c_char) -> u8;
pub type FeTile_ItemBase_Populate_t = unsafe extern "C" fn(item: *mut HomeItem, node: *mut c_void, a7: u32);
pub type FeTile_Press_EraTile_t =
    unsafe extern "C" fn(view_model: *mut c_void, command: *mut c_void) -> *mut c_char;
pub type FeTile_Route_CareerItem_t =
    unsafe extern "C" fn(item: *mut HomeItem, out: *mut Action) -> *mut Action;
pub type Game_CareerActivities_AddActivity_t =
    unsafe extern "C" fn(calendar: *mut c_void, activity: *const c_void, day: u32);
pub type Game_CareerActivities_RemoveActivity_t =
    unsafe extern "C" fn(calendar: *mut c_void, activity: *const c_void, day: u16);
pub type Game_CareerSetup_ResetEntrants_t = unsafe extern "C" fn(setup: *mut c_void);
pub type Game_CareerWizard_PushNextStep_t = unsafe extern "C" fn(wizard: *mut c_void);
pub type Game_CareerWizard_StartsInF1_t = unsafe extern "C" fn(
    entrants: *mut c_void,
    wizard_data: *mut c_void,
    entrant_list: *const c_char,
) -> bool;
pub type Game_Career_IsDriverCareerMode_t = unsafe extern "C" fn(championship: *const c_void) -> bool;
pub type Game_ContractTeamSelect_OnEnter_t = unsafe extern "C" fn(state: *mut c_void) -> u64;
pub type Game_ContractTeamSelect_OnExit_t = unsafe extern "C" fn(state: *mut c_void) -> u64;
pub type Game_ContractTeamSelect_OnUpdate_t =
    unsafe extern "C" fn(state: *mut c_void, link: *mut u32) -> *mut u32;
pub type Game_CreateSessionDlcSelectTab_BuildTiles_t = unsafe extern "C" fn(view_model: *mut c_void);
pub type Game_CreateSessionDlcSelectTab_FillTile_t = unsafe extern "C" fn(
    tile: *mut c_void,
    node: *mut c_void,
    title: *const c_char,
    command: *const c_char,
    image: *const c_char,
) -> *mut c_void;
pub type Game_CreateSessionDlcSelectTab_HandleCommand_t =
    unsafe extern "C" fn(view_model: *mut c_void, command: *mut c_void) -> *mut c_char;
pub type Game_DriverContracts_AdvanceSecretMeeting_t =
    unsafe extern "C" fn(contracts: *mut c_void, driver: u32, notify: u8);
pub type Game_DriverContracts_BuildOffers_t = unsafe extern "C" fn(contracts: *mut c_void, driver: u32);
pub type Game_DriverContracts_OfferSecretMeeting_t = unsafe extern "C" fn(
    contracts: *mut c_void,
    meeting: *const c_void,
    team: u32,
    driver: u32,
    reason: u8,
    notify: u8,
);
pub type Game_DriverSelectHeader_SetTabState_t =
    unsafe extern "C" fn(header: *mut c_void, tab: u64, content_visible: u8, enabled: u8);
pub type Game_EngineMgmt_AddGridPenalty_t =
    unsafe extern "C" fn(engine_management: *mut c_void, driver: *const u32, places: u32, reason: u32);
pub type Game_EngineMgmt_ApplySessionWear_t =
    unsafe extern "C" fn(engine_management: *mut c_void, driver: u32, amount: f32, flag: u8);
pub type Game_EntrantsUi_BuildCharacterRef_t =
    unsafe extern "C" fn(out: *mut c_void, character: *const u8, node: *mut c_void) -> *mut c_void;
pub type Game_EntrantsUi_OnCharacterListLoaded_t =
    unsafe extern "C" fn(ctx: *mut c_void, message: *const c_void) -> i64;
pub type Game_F1WorldActivityContainer_SelectEntrantList_t =
    unsafe extern "C" fn(container: *mut c_void, entrant_list: *const c_char);
pub type Game_Get_ModelNodeId_t = unsafe extern "C" fn(name: *const c_char) -> u32;
pub type Game_Handle_GarageCrewEvent_Thunk_t =
    unsafe extern "C" fn(cars: *mut *mut c_void, event: *mut c_void) -> u64;
pub type Game_Handle_SpecGarageCrewEvent_Thunk_t =
    unsafe extern "C" fn(self_: *mut c_void, event: *mut c_void) -> u64;
pub type Game_Network_CategoryOptionUsing_t = unsafe extern "C" fn(self_: *mut c_void);
pub type Game_Network_SendingRedFlagVehicleStatusUpdate_t =
    unsafe extern "C" fn(red_flag_state: *mut c_void, dt: *const f64);
pub type Game_Network_SetScenario_t =
    unsafe extern "C" fn(session: *mut c_void, stat_id: u8, spec: *const c_void);
pub type Game_OptionsStore_FindOption_t =
    unsafe extern "C" fn(map: *mut c_void, name: *mut *const c_char) -> u64;
pub type Game_PitLane_MoveDownSpline_t = unsafe extern "C" fn(
    pitlane: *mut c_void,
    car: u8,
    teleport_req: *mut c_void,
    alt_spline: bool,
    dt: f32,
    p6: bool,
    blend: bool,
    p8: bool,
    p9: bool,
);
pub type Game_Place_Vehicle_t = unsafe extern "C" fn(
    teleport_req: *mut c_void,
    tag: *const c_char,
    priority: c_int,
    mtx44_a: *const f32,
    mtx44_b: *const f32,
    dt: f32,
    flags: u32,
);
pub type Game_PracticeTrackAcclimatisation_RefreshState_t = unsafe extern "C" fn(self_: *mut c_void);
pub type Game_Predict_StrategyWetness_t =
    unsafe extern "C" fn(self_: *mut c_void, window3: *const f32) -> f32;
pub type Game_RaceMarshalSystem_Update_t = unsafe extern "C" fn(system: *mut c_void, dt: *mut f64);
pub type Game_RaceStrategyVm_Populate_t = unsafe extern "C" fn(vm: *mut c_void);
pub type Game_Refresh_PerformanceComparison_t = unsafe extern "C" fn(comparison: *mut c_void);
pub type Game_Rnd_CalcUpgradeTypeProgress_t =
    unsafe extern "C" fn(instances: *mut *mut c_void, r#type: u8, state: u8, meta: *const c_void) -> f32;
pub type Game_Rnd_FillVehicleUpgrades_t =
    unsafe extern "C" fn(component: *mut c_void, car: u8, a3: u8, a4: u8);
pub type Game_SaveData_DeserialiseContainer_t = unsafe extern "C" fn(
    self_: *mut SaveDataContainer,
    result: *mut c_int,
    bag: *mut c_void,
    reset_on_error: u8,
    groups: u64,
) -> *mut c_int;
pub type Game_SaveData_SerialiseContainer_t = unsafe extern "C" fn(
    self_: *mut SaveDataContainer,
    result: *mut c_int,
    bag: *mut c_void,
    groups: u64,
) -> *mut c_int;
pub type Game_SaveData_WriteBag_t = unsafe extern "C" fn(codec: *mut NeBagCodec, bag: *mut c_void) -> c_int;
pub type Game_SceneManagerSpawn_SpawnScene_t = unsafe extern "C" fn(
    mgr: *mut c_void,
    opts: *const u8,
    scene: *mut c_void,
    mtx44: *const f32,
    ov_a: *mut c_void,
    ov_b: *mut c_void,
    p7: u64,
) -> *mut c_void;
pub type Game_SeasonLength_ApplyPreset_t = unsafe extern "C" fn(component: *mut c_void, preset: u8);
pub type Game_SeasonLength_PushCalendarEntry_t =
    unsafe extern "C" fn(calendar: *mut c_void, entry: *const c_void) -> u64;
pub type Game_Set_VehicleController_Thunk_t =
    unsafe extern "C" fn(system: *mut c_void, vehicle: u64, ai: u32, flag: i32);
pub type Game_SpeechSystem_PostLine_t =
    unsafe extern "C" fn(speech: *mut c_void, line: *mut c_void, retrigger: u8);
pub type Game_Teleport_Vehicle_t = unsafe extern "C" fn(
    car: *mut c_void,
    name: *const c_char,
    prev_mtx: *mut f32,
    new_mtx: *mut f32,
    dt: f32,
    flags: u32,
);
pub type Gdb_Lookup_Key_t = unsafe extern "C" fn(path: *const c_char) -> *mut *mut c_void;
pub type Ne_Compare_StringNoCase_t = unsafe extern "C" fn(a: *const c_char, b: *const c_char) -> i32;
pub type Ne_Detach_ChildNode_t = unsafe extern "C" fn(parent: *mut c_void, child: *mut c_void) -> c_int;
pub type Ne_Entity_FindComponent_t =
    unsafe extern "C" fn(entity: *mut c_void, r#type: *const c_void) -> *mut c_void;
pub type Ne_Format_String_t = unsafe extern "C" fn(out: *mut *mut NeStr, fmt: *const c_char, ...) -> i64;
pub type Ne_Get_SettingBool_t =
    unsafe extern "C" fn(node: *mut c_void, name: *const c_char, out: *mut u8, def: u8) -> u64;
pub type Ne_Get_SettingU32_t =
    unsafe extern "C" fn(node: *mut c_void, name: *const c_char, out: *mut u32, def: u32) -> u64;
pub type Ne_Log_Thunk_t = unsafe extern "C" fn(channel: *const c_char, fmt: *const c_char, ...);
pub type Ne_Make_StrFromCStr_t =
    unsafe extern "C" fn(out: *mut *mut NeStr, s: *const c_char, flags: u32, len: usize) -> u32;
pub type Ne_Parse_Long_t = unsafe extern "C" fn(text: *const c_char, end: *mut *mut c_char, base: i32) -> i32;
pub type Ne_Play_FullscreenVideo_t = unsafe extern "C" fn(
    feature_system: *mut c_void,
    path: *mut *mut NeStr,
    on_end: *const c_void,
    frame_callbacks: *const c_void,
    layer: i32,
);
pub type Ne_Post_BusMessage_t = unsafe extern "C" fn(bus: *mut c_void, msg: *mut c_void) -> u64;
pub type Ne_StringTable_Lookup_t = unsafe extern "C" fn(table: *mut c_void, hash: u64) -> *const c_char;
pub type Sess_Build_GroupResult_t =
    unsafe extern "C" fn(weekend: *mut c_void, group: u8, result: *mut c_void);
pub type Sess_Find_GroupRowPosition_t =
    unsafe extern "C" fn(weekend: *mut c_void, result: *mut c_void, driver_key: u32) -> *mut i32;
pub type Sess_FormationLap_Update_t =
    unsafe extern "C" fn(state: *mut c_void, route: *mut u32, dt: f64) -> *mut u32;
pub type Sess_GameWeatherSystem_SetupSession_t =
    unsafe extern "C" fn(ws: *mut c_void, settings: *mut c_void, config: *mut c_void);
pub type Sess_GameWeatherSystem_UpdateRandomSeed_t =
    unsafe extern "C" fn(ws: *mut c_void, seed: u32, weekend: u32);
pub type Sess_Get_PlayerVehicleIndex_t = unsafe extern "C" fn(ctx: *mut c_void, local_player: c_int) -> u8;
pub type Sess_GridRundown_Update_t =
    unsafe extern "C" fn(state: *mut c_void, route: *mut u32, dt: f64) -> *mut u32;
pub type Sess_InGamePQ_Update_t =
    unsafe extern "C" fn(state: *mut c_void, route: *mut u32, dt: f64) -> *mut u32;
pub type Sess_InGame_UpdateBase_t =
    unsafe extern "C" fn(state: *mut c_void, route: *mut u32, dt: f64) -> *mut u32;
pub type Sess_Invalidate_Lap_Thunk_t =
    unsafe extern "C" fn(rules: *mut c_void, vehicle: u32, reason: u32, mode: c_char);
pub type Sess_Issue_DriveThrough_t = unsafe extern "C" fn(rules: *mut c_void, incident: *mut u8, reason: u32);
pub type Sess_Issue_TimePenalty_t =
    unsafe extern "C" fn(rules: *mut c_void, incident: *mut u8, reason: u32, seconds: u32);
pub type Sess_Issue_Warning_t = unsafe extern "C" fn(rules: *mut c_void, incident: *mut u8, reason: u32);
pub type Sess_PreRaceToRace_Update_t = unsafe extern "C" fn(state: *mut c_void, route: *mut u32) -> *mut u32;
pub type Sess_PreRace_Update_t =
    unsafe extern "C" fn(state: *mut c_void, route: *mut u32, dt: f64) -> *mut u32;
pub type Sess_Record_GroupTimes_t =
    unsafe extern "C" fn(weekend: *mut c_void, group: u8, entries: *mut c_void);
pub type Sess_RedFlagRaceStrategy_Enter_t = unsafe extern "C" fn(state: *mut c_void) -> u64;
pub type Sess_RedFlagRaceStrategy_Update_t =
    unsafe extern "C" fn(state: *mut c_void, route: *mut u32) -> *mut u32;
pub type Sess_SafetyCarTether_UpdateProgress_t = unsafe extern "C" fn(tether_entry: *mut c_void);
pub type Sess_SafetyCarTether_UpdateTarget_t = unsafe extern "C" fn(tether_entry: *mut c_void, gap: f32);
pub type Sess_ScDebrisAccumulator_Update_t = unsafe extern "C" fn(acc: *mut c_void);
pub type Sess_Set_SafetyCarDeploymentType_t =
    unsafe extern "C" fn(sc_state: *mut c_void, sc_deployment_msg: *const c_void);
pub type Sess_Tod_BuildSessionDataset_Thunk_t = unsafe extern "C" fn(
    tod_owner: *mut c_void,
    types: *const u8,
    hold_s: *const f32,
    trans_s: *const f32,
    random: *const u8,
    count: u32,
    len_s: f32,
    session: u16,
    use_count: u8,
);
pub type Sess_TyreAllocation_OnSessionStart_t = unsafe extern "C" fn(component: *mut c_void);
pub type Sess_TyreClass_FromName_t =
    unsafe extern "C" fn(classes: *mut c_void, name: *const c_void, check_weekend_format: u8) -> *mut c_void;
pub type Sess_Tyres_ClosestAvailableCompound_t = unsafe extern "C" fn(
    tyre_rules: *mut c_void,
    vehicle: u8,
    preference: *const u32,
    wear_scale: f32,
) -> u32;
pub type Sess_WeatherComponent_SetSystemRandomSeed_t = unsafe extern "C" fn(component: *mut c_void);
pub type Sess_Weekend_Start_t =
    unsafe extern "C" fn(weekend: *mut c_void, entrants: *const c_void, begin: bool);
pub type Vm_GetOrCreate_ListChild_t =
    unsafe extern "C" fn(registry: *mut c_void, parent: *mut c_void, hash: *const u32) -> *mut *mut c_void;
pub type Vm_List_AppendChild_t = unsafe extern "C" fn(list: *mut c_void) -> *mut *mut c_void;
pub type Vm_Release_Node_t = unsafe extern "C" fn(registry: *mut c_void, node: *mut c_void);
pub type Vm_Resolve_RegistryNode_t = unsafe extern "C" fn(
    registry: *mut c_void,
    path: *const c_char,
    base: *const c_void,
    create: *mut c_void,
) -> *mut c_void;
pub type Vm_Set_Bool_t = unsafe extern "C" fn(
    registry: *mut c_void,
    parent: *mut c_void,
    name: *const c_char,
    value: *mut u8,
    overwrite: c_int,
) -> *mut c_void;
pub type Vm_Set_NodeRef_t = unsafe extern "C" fn(
    registry: *mut c_void,
    parent: *mut c_void,
    name: *const c_char,
    value: *mut c_void,
    overwrite: c_int,
) -> *mut *mut c_void;
pub type Vm_Set_String_t = unsafe extern "C" fn(
    registry: *mut c_void,
    parent: *mut c_void,
    name: *const c_char,
    value: *mut *mut NeStr,
    overwrite: c_int,
) -> *mut c_void;
pub type Vm_Set_TabEnabled_t = unsafe extern "C" fn(tabs: usize, node: usize, enabled: u32);

pub mod bindings {
    use super::{Binding, Status};
    pub const Car_Reset_LastFittedUpgrade: Binding = Binding {
        id: "Car_Reset_LastFittedUpgrade",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_Add_SeasonWeekend: Binding = Binding {
        id: "Career_Add_SeasonWeekend",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_Add_TeamModifier: Binding = Binding {
        id: "Career_Add_TeamModifier",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_Adjust_RpDriverWallet: Binding = Binding {
        id: "Career_Adjust_RpDriverWallet",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_Adjust_RpTeamWallet: Binding = Binding {
        id: "Career_Adjust_RpTeamWallet",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_Build_DriverContract: Binding = Binding {
        id: "Career_Build_DriverContract",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_Build_LineupChanges: Binding = Binding {
        id: "Career_Build_LineupChanges",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_Calc_UpgradeCost: Binding = Binding {
        id: "Career_Calc_UpgradeCost",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_Cancel_UpgradeDevelopment: Binding = Binding {
        id: "Career_Cancel_UpgradeDevelopment",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_Cancel_UpgradeResearch: Binding = Binding {
        id: "Career_Cancel_UpgradeResearch",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_Complete_PerkTreeNode: Binding = Binding {
        id: "Career_Complete_PerkTreeNode",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_Construct_RnDDeptCard: Binding = Binding {
        id: "Career_Construct_RnDDeptCard",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_ContractIntro_OnEnter: Binding = Binding {
        id: "Career_ContractIntro_OnEnter",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_ContractIntro_OnUpdate: Binding = Binding {
        id: "Career_ContractIntro_OnUpdate",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_ContractOutro_OnEnter: Binding = Binding {
        id: "Career_ContractOutro_OnEnter",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_ContractOutro_OnUpdate: Binding = Binding {
        id: "Career_ContractOutro_OnUpdate",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_ContractUpdate_OnEnter: Binding = Binding {
        id: "Career_ContractUpdate_OnEnter",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_ContractUpdate_OnExit: Binding = Binding {
        id: "Career_ContractUpdate_OnExit",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_ContractUpdate_OnUpdate: Binding = Binding {
        id: "Career_ContractUpdate_OnUpdate",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_Create_SeasonWeekend: Binding = Binding {
        id: "Career_Create_SeasonWeekend",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_Decide_ContractNegotiation: Binding = Binding {
        id: "Career_Decide_ContractNegotiation",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_DriverNumbers_CheckExisting: Binding = Binding {
        id: "Career_DriverNumbers_CheckExisting",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_Eval_Curve: Binding = Binding {
        id: "Career_Eval_Curve",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_Get_CareerLengthDays_Thunk: Binding = Binding {
        id: "Career_Get_CareerLengthDays_Thunk",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_Get_DriverOverallRating: Binding = Binding {
        id: "Career_Get_DriverOverallRating",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_Get_RpWallet: Binding = Binding {
        id: "Career_Get_RpWallet",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_Get_TeamRenownData: Binding = Binding {
        id: "Career_Get_TeamRenownData",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_Needs_DriverContract: Binding = Binding {
        id: "Career_Needs_DriverContract",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_Post_UpgradeFittedRatio: Binding = Binding {
        id: "Career_Post_UpgradeFittedRatio",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_Rebuild_ChampionshipStandings: Binding = Binding {
        id: "Career_Rebuild_ChampionshipStandings",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_Rebuild_SeasonStandings: Binding = Binding {
        id: "Career_Rebuild_SeasonStandings",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_Record_DriverMove: Binding = Binding {
        id: "Career_Record_DriverMove",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_Refresh_RnDDeptMode: Binding = Binding {
        id: "Career_Refresh_RnDDeptMode",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_Refresh_UpgradeAvailability: Binding = Binding {
        id: "Career_Refresh_UpgradeAvailability",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_Set_DriverContractState: Binding = Binding {
        id: "Career_Set_DriverContractState",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_Set_EntrantData_Thunk: Binding = Binding {
        id: "Career_Set_EntrantData_Thunk",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_Set_UpgradeFacilityLocked: Binding = Binding {
        id: "Career_Set_UpgradeFacilityLocked",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_Set_UpgradesOwner: Binding = Binding {
        id: "Career_Set_UpgradesOwner",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_Sign_DriverContract: Binding = Binding {
        id: "Career_Sign_DriverContract",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_Start_UpgradeDevelopment: Binding = Binding {
        id: "Career_Start_UpgradeDevelopment",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_Start_UpgradeResearch: Binding = Binding {
        id: "Career_Start_UpgradeResearch",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Career_Teardown_Weekend: Binding = Binding {
        id: "Career_Teardown_Weekend",
        kind: "func",
        status: Status::Experimental,
    };
    pub const FeFlow_Set_StateStatus: Binding = Binding {
        id: "FeFlow_Set_StateStatus",
        kind: "func",
        status: Status::Experimental,
    };
    pub const FeHome_Add_CareerTile: Binding = Binding {
        id: "FeHome_Add_CareerTile",
        kind: "func",
        status: Status::Stable,
    };
    pub const FeHome_Handle_Command: Binding = Binding {
        id: "FeHome_Handle_Command",
        kind: "func",
        status: Status::Stable,
    };
    pub const FeTile_Add_EraTile: Binding = Binding {
        id: "FeTile_Add_EraTile",
        kind: "func",
        status: Status::Experimental,
    };
    pub const FeTile_Build_List: Binding = Binding {
        id: "FeTile_Build_List",
        kind: "func",
        status: Status::Stable,
    };
    pub const FeTile_CareerDlcItem_Populate: Binding = Binding {
        id: "FeTile_CareerDlcItem_Populate",
        kind: "func",
        status: Status::Experimental,
    };
    pub const FeTile_Clear_EraTileSet_Thunk: Binding = Binding {
        id: "FeTile_Clear_EraTileSet_Thunk",
        kind: "func",
        status: Status::Experimental,
    };
    pub const FeTile_Construct_CareerItem: Binding = Binding {
        id: "FeTile_Construct_CareerItem",
        kind: "func",
        status: Status::Experimental,
    };
    pub const FeTile_Is_EraScenarioAvailable_Thunk: Binding = Binding {
        id: "FeTile_Is_EraScenarioAvailable_Thunk",
        kind: "func",
        status: Status::Experimental,
    };
    pub const FeTile_ItemBase_Populate: Binding = Binding {
        id: "FeTile_ItemBase_Populate",
        kind: "func",
        status: Status::Stable,
    };
    pub const FeTile_Press_EraTile: Binding = Binding {
        id: "FeTile_Press_EraTile",
        kind: "func",
        status: Status::Experimental,
    };
    pub const FeTile_Route_CareerItem: Binding = Binding {
        id: "FeTile_Route_CareerItem",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_CareerActivities_AddActivity: Binding = Binding {
        id: "Game_CareerActivities_AddActivity",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_CareerActivities_RemoveActivity: Binding = Binding {
        id: "Game_CareerActivities_RemoveActivity",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_CareerSetup_ResetEntrants: Binding = Binding {
        id: "Game_CareerSetup_ResetEntrants",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_CareerWizard_PushNextStep: Binding = Binding {
        id: "Game_CareerWizard_PushNextStep",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_CareerWizard_StartsInF1: Binding = Binding {
        id: "Game_CareerWizard_StartsInF1",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_Career_IsDriverCareerMode: Binding = Binding {
        id: "Game_Career_IsDriverCareerMode",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_ContractTeamSelect_OnEnter: Binding = Binding {
        id: "Game_ContractTeamSelect_OnEnter",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_ContractTeamSelect_OnExit: Binding = Binding {
        id: "Game_ContractTeamSelect_OnExit",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_ContractTeamSelect_OnUpdate: Binding = Binding {
        id: "Game_ContractTeamSelect_OnUpdate",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_CreateSessionDlcSelectTab_BuildTiles: Binding = Binding {
        id: "Game_CreateSessionDlcSelectTab_BuildTiles",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_CreateSessionDlcSelectTab_FillTile: Binding = Binding {
        id: "Game_CreateSessionDlcSelectTab_FillTile",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_CreateSessionDlcSelectTab_HandleCommand: Binding = Binding {
        id: "Game_CreateSessionDlcSelectTab_HandleCommand",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_DriverContracts_AdvanceSecretMeeting: Binding = Binding {
        id: "Game_DriverContracts_AdvanceSecretMeeting",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_DriverContracts_BuildOffers: Binding = Binding {
        id: "Game_DriverContracts_BuildOffers",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_DriverContracts_OfferSecretMeeting: Binding = Binding {
        id: "Game_DriverContracts_OfferSecretMeeting",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_DriverSelectHeader_SetTabState: Binding = Binding {
        id: "Game_DriverSelectHeader_SetTabState",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_EngineMgmt_AddGridPenalty: Binding = Binding {
        id: "Game_EngineMgmt_AddGridPenalty",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_EngineMgmt_ApplySessionWear: Binding = Binding {
        id: "Game_EngineMgmt_ApplySessionWear",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_EntrantsUi_BuildCharacterRef: Binding = Binding {
        id: "Game_EntrantsUi_BuildCharacterRef",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_EntrantsUi_OnCharacterListLoaded: Binding = Binding {
        id: "Game_EntrantsUi_OnCharacterListLoaded",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_F1WorldActivityContainer_SelectEntrantList: Binding = Binding {
        id: "Game_F1WorldActivityContainer_SelectEntrantList",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_Get_ModelNodeId: Binding = Binding {
        id: "Game_Get_ModelNodeId",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_Handle_GarageCrewEvent_Thunk: Binding = Binding {
        id: "Game_Handle_GarageCrewEvent_Thunk",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_Handle_SpecGarageCrewEvent_Thunk: Binding = Binding {
        id: "Game_Handle_SpecGarageCrewEvent_Thunk",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_Network_CategoryOptionUsing: Binding = Binding {
        id: "Game_Network_CategoryOptionUsing",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_Network_SendingRedFlagVehicleStatusUpdate: Binding = Binding {
        id: "Game_Network_SendingRedFlagVehicleStatusUpdate",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_Network_SetScenario: Binding = Binding {
        id: "Game_Network_SetScenario",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_OptionsStore_FindOption: Binding = Binding {
        id: "Game_OptionsStore_FindOption",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_PitLane_MoveDownSpline: Binding = Binding {
        id: "Game_PitLane_MoveDownSpline",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_PitLane_VehicleEnteringInternal: Binding = Binding {
        id: "Game_PitLane_VehicleEnteringInternal",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_Pitlane_Multiplayer_teleportToGarage: Binding = Binding {
        id: "Game_Pitlane_Multiplayer_teleportToGarage",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_Pitlane_SetVehicleStatus: Binding = Binding {
        id: "Game_Pitlane_SetVehicleStatus",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_Place_Vehicle: Binding = Binding {
        id: "Game_Place_Vehicle",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_PracticeTrackAcclimatisation_RefreshState: Binding = Binding {
        id: "Game_PracticeTrackAcclimatisation_RefreshState",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_Predict_StrategyWetness: Binding = Binding {
        id: "Game_Predict_StrategyWetness",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_RaceMarshalSystem_Update: Binding = Binding {
        id: "Game_RaceMarshalSystem_Update",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_RaceStrategyVm_Populate: Binding = Binding {
        id: "Game_RaceStrategyVm_Populate",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_Refresh_PerformanceComparison: Binding = Binding {
        id: "Game_Refresh_PerformanceComparison",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_Rnd_CalcUpgradeTypeProgress: Binding = Binding {
        id: "Game_Rnd_CalcUpgradeTypeProgress",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_Rnd_FillVehicleUpgrades: Binding = Binding {
        id: "Game_Rnd_FillVehicleUpgrades",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_SaveData_DeserialiseContainer: Binding = Binding {
        id: "Game_SaveData_DeserialiseContainer",
        kind: "func",
        status: Status::Stable,
    };
    pub const Game_SaveData_SerialiseContainer: Binding = Binding {
        id: "Game_SaveData_SerialiseContainer",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_SaveData_WriteBag: Binding = Binding {
        id: "Game_SaveData_WriteBag",
        kind: "func",
        status: Status::Stable,
    };
    pub const Game_SceneManagerSpawn_SpawnScene: Binding = Binding {
        id: "Game_SceneManagerSpawn_SpawnScene",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_SeasonLength_ApplyPreset: Binding = Binding {
        id: "Game_SeasonLength_ApplyPreset",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_SeasonLength_PushCalendarEntry: Binding = Binding {
        id: "Game_SeasonLength_PushCalendarEntry",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_Set_VehicleController_Thunk: Binding = Binding {
        id: "Game_Set_VehicleController_Thunk",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_SpeechSystem_PostLine: Binding = Binding {
        id: "Game_SpeechSystem_PostLine",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Game_Teleport_Vehicle: Binding = Binding {
        id: "Game_Teleport_Vehicle",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Gdb_Lookup_Key: Binding = Binding {
        id: "Gdb_Lookup_Key",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Ne_Compare_StringNoCase: Binding = Binding {
        id: "Ne_Compare_StringNoCase",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Ne_Detach_ChildNode: Binding = Binding {
        id: "Ne_Detach_ChildNode",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Ne_Entity_FindComponent: Binding = Binding {
        id: "Ne_Entity_FindComponent",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Ne_Format_String: Binding = Binding {
        id: "Ne_Format_String",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Ne_Get_SettingBool: Binding = Binding {
        id: "Ne_Get_SettingBool",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Ne_Get_SettingU32: Binding = Binding {
        id: "Ne_Get_SettingU32",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Ne_Log_Thunk: Binding = Binding {
        id: "Ne_Log_Thunk",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Ne_Make_StrFromCStr: Binding = Binding {
        id: "Ne_Make_StrFromCStr",
        kind: "func",
        status: Status::Stable,
    };
    pub const Ne_Parse_Long: Binding = Binding {
        id: "Ne_Parse_Long",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Ne_Play_FullscreenVideo: Binding = Binding {
        id: "Ne_Play_FullscreenVideo",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Ne_Post_BusMessage: Binding = Binding {
        id: "Ne_Post_BusMessage",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Ne_StringTable_Lookup: Binding = Binding {
        id: "Ne_StringTable_Lookup",
        kind: "func",
        status: Status::Stable,
    };
    pub const Sess_Build_GroupResult: Binding = Binding {
        id: "Sess_Build_GroupResult",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Sess_Find_GroupRowPosition: Binding = Binding {
        id: "Sess_Find_GroupRowPosition",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Sess_Flow_ParseXml: Binding = Binding {
        id: "Sess_Flow_ParseXml",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Sess_FormationLap_Update: Binding = Binding {
        id: "Sess_FormationLap_Update",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Sess_GameWeatherSystem_SetupSession: Binding = Binding {
        id: "Sess_GameWeatherSystem_SetupSession",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Sess_GameWeatherSystem_UpdateRandomSeed: Binding = Binding {
        id: "Sess_GameWeatherSystem_UpdateRandomSeed",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Sess_Get_PlayerVehicleIndex: Binding = Binding {
        id: "Sess_Get_PlayerVehicleIndex",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Sess_GridRundown_Update: Binding = Binding {
        id: "Sess_GridRundown_Update",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Sess_InGamePQ_Update: Binding = Binding {
        id: "Sess_InGamePQ_Update",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Sess_InGame_UpdateBase: Binding = Binding {
        id: "Sess_InGame_UpdateBase",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Sess_Invalidate_Lap_Thunk: Binding = Binding {
        id: "Sess_Invalidate_Lap_Thunk",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Sess_Issue_DriveThrough: Binding = Binding {
        id: "Sess_Issue_DriveThrough",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Sess_Issue_TimePenalty: Binding = Binding {
        id: "Sess_Issue_TimePenalty",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Sess_Issue_Warning: Binding = Binding {
        id: "Sess_Issue_Warning",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Sess_PreRaceToRace_Update: Binding = Binding {
        id: "Sess_PreRaceToRace_Update",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Sess_PreRace_Update: Binding = Binding {
        id: "Sess_PreRace_Update",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Sess_Record_GroupTimes: Binding = Binding {
        id: "Sess_Record_GroupTimes",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Sess_RedFlagRaceStrategy_Enter: Binding = Binding {
        id: "Sess_RedFlagRaceStrategy_Enter",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Sess_RedFlagRaceStrategy_Update: Binding = Binding {
        id: "Sess_RedFlagRaceStrategy_Update",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Sess_Rules_Disqualify: Binding = Binding {
        id: "Sess_Rules_Disqualify",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Sess_SafetyCarTether_UpdateProgress: Binding = Binding {
        id: "Sess_SafetyCarTether_UpdateProgress",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Sess_SafetyCarTether_UpdateTarget: Binding = Binding {
        id: "Sess_SafetyCarTether_UpdateTarget",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Sess_ScDebrisAccumulator_Update: Binding = Binding {
        id: "Sess_ScDebrisAccumulator_Update",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Sess_Set_SafetyCarDeploymentType: Binding = Binding {
        id: "Sess_Set_SafetyCarDeploymentType",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Sess_Tod_BuildSessionDataset_Thunk: Binding = Binding {
        id: "Sess_Tod_BuildSessionDataset_Thunk",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Sess_TyreAllocation_OnSessionStart: Binding = Binding {
        id: "Sess_TyreAllocation_OnSessionStart",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Sess_TyreClass_FromName: Binding = Binding {
        id: "Sess_TyreClass_FromName",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Sess_Tyres_ClosestAvailableCompound: Binding = Binding {
        id: "Sess_Tyres_ClosestAvailableCompound",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Sess_WeatherComponent_SetSystemRandomSeed: Binding = Binding {
        id: "Sess_WeatherComponent_SetSystemRandomSeed",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Sess_Weekend_Start: Binding = Binding {
        id: "Sess_Weekend_Start",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Vm_GetOrCreate_ListChild: Binding = Binding {
        id: "Vm_GetOrCreate_ListChild",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Vm_List_AppendChild: Binding = Binding {
        id: "Vm_List_AppendChild",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Vm_Release_Node: Binding = Binding {
        id: "Vm_Release_Node",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Vm_Resolve_RegistryNode: Binding = Binding {
        id: "Vm_Resolve_RegistryNode",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Vm_Set_Bool: Binding = Binding {
        id: "Vm_Set_Bool",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Vm_Set_NodeRef: Binding = Binding {
        id: "Vm_Set_NodeRef",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Vm_Set_String: Binding = Binding {
        id: "Vm_Set_String",
        kind: "func",
        status: Status::Experimental,
    };
    pub const Vm_Set_TabEnabled: Binding = Binding {
        id: "Vm_Set_TabEnabled",
        kind: "func",
        status: Status::Experimental,
    };
    pub const g_Career_FlowLinkContinue: Binding = Binding {
        id: "g_Career_FlowLinkContinue",
        kind: "data",
        status: Status::Experimental,
    };
    pub const g_Career_RnDOverviewSelectedDept: Binding = Binding {
        id: "g_Career_RnDOverviewSelectedDept",
        kind: "data",
        status: Status::Experimental,
    };
    pub const g_Career_UpgradeLifecycle: Binding = Binding {
        id: "g_Career_UpgradeLifecycle",
        kind: "data",
        status: Status::Experimental,
    };
    pub const g_FeTile_CustomisationSystem: Binding = Binding {
        id: "g_FeTile_CustomisationSystem",
        kind: "data",
        status: Status::Experimental,
    };
    pub const g_FeTile_SelectedCareerContent: Binding = Binding {
        id: "g_FeTile_SelectedCareerContent",
        kind: "data",
        status: Status::Experimental,
    };
    pub const g_Game_FlowLinkStay: Binding = Binding {
        id: "g_Game_FlowLinkStay",
        kind: "data",
        status: Status::Experimental,
    };
    pub const g_Game_Manager: Binding = Binding {
        id: "g_Game_Manager",
        kind: "data",
        status: Status::Experimental,
    };
    pub const g_Game_MessageBus: Binding = Binding {
        id: "g_Game_MessageBus",
        kind: "data",
        status: Status::Experimental,
    };
    pub const g_Game_OptionsStore: Binding = Binding {
        id: "g_Game_OptionsStore",
        kind: "data",
        status: Status::Experimental,
    };
    pub const g_Game_RaceStartSystem: Binding = Binding {
        id: "g_Game_RaceStartSystem",
        kind: "data",
        status: Status::Experimental,
    };
    pub const g_Game_Root: Binding = Binding {
        id: "g_Game_Root",
        kind: "data",
        status: Status::Experimental,
    };
    pub const g_Game_Scenarios: Binding = Binding {
        id: "g_Game_Scenarios",
        kind: "data",
        status: Status::Experimental,
    };
    pub const g_Game_TrackLength: Binding = Binding {
        id: "g_Game_TrackLength",
        kind: "data",
        status: Status::Experimental,
    };
    pub const g_Game_VehicleRecordManager: Binding = Binding {
        id: "g_Game_VehicleRecordManager",
        kind: "data",
        status: Status::Experimental,
    };
    pub const g_Game_VehicleStateRoot: Binding = Binding {
        id: "g_Game_VehicleStateRoot",
        kind: "data",
        status: Status::Experimental,
    };
    pub const g_Ne_DefaultAllocator: Binding = Binding {
        id: "g_Ne_DefaultAllocator",
        kind: "data",
        status: Status::Experimental,
    };
    pub const g_Ne_LogGate: Binding = Binding {
        id: "g_Ne_LogGate",
        kind: "data",
        status: Status::Experimental,
    };
    pub const g_Ne_StringTable: Binding = Binding {
        id: "g_Ne_StringTable",
        kind: "data",
        status: Status::Stable,
    };
    pub const g_Sess_TyreRulesActive: Binding = Binding {
        id: "g_Sess_TyreRulesActive",
        kind: "data",
        status: Status::Experimental,
    };
    pub const g_Sess_TyreRulesMode: Binding = Binding {
        id: "g_Sess_TyreRulesMode",
        kind: "data",
        status: Status::Experimental,
    };
    pub const g_Sess_TyreRulesSessionType: Binding = Binding {
        id: "g_Sess_TyreRulesSessionType",
        kind: "data",
        status: Status::Experimental,
    };
    pub const g_Vm_Registry: Binding = Binding {
        id: "g_Vm_Registry",
        kind: "data",
        status: Status::Stable,
    };
    pub const k_Career_CareerBankComponentDesc: Binding = Binding {
        id: "k_Career_CareerBankComponentDesc",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Career_CareerComponentDesc: Binding = Binding {
        id: "k_Career_CareerComponentDesc",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Career_CareerDriverContractRenewalComponentDesc: Binding = Binding {
        id: "k_Career_CareerDriverContractRenewalComponentDesc",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Career_CareerDriverMarketComponentDesc: Binding = Binding {
        id: "k_Career_CareerDriverMarketComponentDesc",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Career_CareerFacilitiesComponentDesc: Binding = Binding {
        id: "k_Career_CareerFacilitiesComponentDesc",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Career_CareerSetupComponentDesc: Binding = Binding {
        id: "k_Career_CareerSetupComponentDesc",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Career_CareerSponsorsComponentDesc: Binding = Binding {
        id: "k_Career_CareerSponsorsComponentDesc",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Career_CareerTimeComponentDesc: Binding = Binding {
        id: "k_Career_CareerTimeComponentDesc",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Career_ChampionshipComponentDesc: Binding = Binding {
        id: "k_Career_ChampionshipComponentDesc",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Career_ChampionshipDesc: Binding = Binding {
        id: "k_Career_ChampionshipDesc",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Career_CostCapComponentDesc: Binding = Binding {
        id: "k_Career_CostCapComponentDesc",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Career_DriverCareerContractsManagerComponentDesc: Binding = Binding {
        id: "k_Career_DriverCareerContractsManagerComponentDesc",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Career_DriverCareerPlayerDriverStatisticsComponentDesc: Binding = Binding {
        id: "k_Career_DriverCareerPlayerDriverStatisticsComponentDesc",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Career_DriverContractsMyTeamComponentDesc: Binding = Binding {
        id: "k_Career_DriverContractsMyTeamComponentDesc",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Career_DriverStatisticsComponentDesc: Binding = Binding {
        id: "k_Career_DriverStatisticsComponentDesc",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Career_EngineSuppliersComponentDesc: Binding = Binding {
        id: "k_Career_EngineSuppliersComponentDesc",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Career_MyTeamAttributesComponentDesc: Binding = Binding {
        id: "k_Career_MyTeamAttributesComponentDesc",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Career_MyTeamFacilitiesComponentDesc: Binding = Binding {
        id: "k_Career_MyTeamFacilitiesComponentDesc",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Career_MyTeamRFComponentDesc: Binding = Binding {
        id: "k_Career_MyTeamRFComponentDesc",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Career_MyTeamStatsComponentDesc: Binding = Binding {
        id: "k_Career_MyTeamStatsComponentDesc",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Career_OwnerPerksComponentDesc: Binding = Binding {
        id: "k_Career_OwnerPerksComponentDesc",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Career_OwnerSetupComponentDesc: Binding = Binding {
        id: "k_Career_OwnerSetupComponentDesc",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Career_ResourcePointsComponentDesc: Binding = Binding {
        id: "k_Career_ResourcePointsComponentDesc",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Career_RnDCardColourTable: Binding = Binding {
        id: "k_Career_RnDCardColourTable",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Career_RnDDeptColourTable: Binding = Binding {
        id: "k_Career_RnDDeptColourTable",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Career_RnDErrorDeptName: Binding = Binding {
        id: "k_Career_RnDErrorDeptName",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Career_ScheduleComponentDesc: Binding = Binding {
        id: "k_Career_ScheduleComponentDesc",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Career_TeamHQControllerComponentDesc: Binding = Binding {
        id: "k_Career_TeamHQControllerComponentDesc",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Career_UpgradeBlockNameTable: Binding = Binding {
        id: "k_Career_UpgradeBlockNameTable",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Career_UpgradeTypeOrder: Binding = Binding {
        id: "k_Career_UpgradeTypeOrder",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Career_UpgradesComponentDesc: Binding = Binding {
        id: "k_Career_UpgradesComponentDesc",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Career_WeatherAndTimeOfDayComponentDesc: Binding = Binding {
        id: "k_Career_WeatherAndTimeOfDayComponentDesc",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Game_TrackListComponentDesc: Binding = Binding {
        id: "k_Game_TrackListComponentDesc",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Gdb_StringXorKey: Binding = Binding {
        id: "k_Gdb_StringXorKey",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Ne_BagCodecVtbl: Binding = Binding {
        id: "k_Ne_BagCodecVtbl",
        kind: "vtable",
        status: Status::Stable,
    };
    pub const k_Ne_CountingStreamVtbl: Binding = Binding {
        id: "k_Ne_CountingStreamVtbl",
        kind: "vtable",
        status: Status::Stable,
    };
    pub const k_Ne_MemWriteStreamVtbl: Binding = Binding {
        id: "k_Ne_MemWriteStreamVtbl",
        kind: "vtable",
        status: Status::Stable,
    };
    pub const k_Ne_PropertyType_Bag: Binding = Binding {
        id: "k_Ne_PropertyType_Bag",
        kind: "data",
        status: Status::Stable,
    };
    pub const k_Ne_TypeChainEnd: Binding = Binding {
        id: "k_Ne_TypeChainEnd",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Ne_TypeRoot: Binding = Binding {
        id: "k_Ne_TypeRoot",
        kind: "data",
        status: Status::Experimental,
    };
    pub const k_Sess_TyreComponentType: Binding = Binding {
        id: "k_Sess_TyreComponentType",
        kind: "data",
        status: Status::Experimental,
    };
    pub const site_Career_ApplyUpgradeBlockNameDefault: Binding = Binding {
        id: "site_Career_ApplyUpgradeBlockNameDefault",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_ApplyUpgradeBlockNameJoin: Binding = Binding {
        id: "site_Career_ApplyUpgradeBlockNameJoin",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_ApplyUpgradeKeyCall: Binding = Binding {
        id: "site_Career_ApplyUpgradeKeyCall",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_ApplyUpgradeKeyFilter: Binding = Binding {
        id: "site_Career_ApplyUpgradeKeyFilter",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_DeserialiseUpgradesEnd: Binding = Binding {
        id: "site_Career_DeserialiseUpgradesEnd",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_LoadUpgradeRows_DeptPath: Binding = Binding {
        id: "site_Career_LoadUpgradeRows_DeptPath",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDBoxGridFirstTab: Binding = Binding {
        id: "site_Career_RnDBoxGridFirstTab",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDBoxGridNameClamp: Binding = Binding {
        id: "site_Career_RnDBoxGridNameClamp",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDBoxGridNameIndex: Binding = Binding {
        id: "site_Career_RnDBoxGridNameIndex",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDBoxGridNameRead: Binding = Binding {
        id: "site_Career_RnDBoxGridNameRead",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDCardArtImage0: Binding = Binding {
        id: "site_Career_RnDCardArtImage0",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDCardArtImage1: Binding = Binding {
        id: "site_Career_RnDCardArtImage1",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDCardArtImage2: Binding = Binding {
        id: "site_Career_RnDCardArtImage2",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDCardArtMask0: Binding = Binding {
        id: "site_Career_RnDCardArtMask0",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDCardArtMask1: Binding = Binding {
        id: "site_Career_RnDCardArtMask1",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDCardArtMask2: Binding = Binding {
        id: "site_Career_RnDCardArtMask2",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDCardColourPick0: Binding = Binding {
        id: "site_Career_RnDCardColourPick0",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDCardColourPick1: Binding = Binding {
        id: "site_Career_RnDCardColourPick1",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDCardColourPick2: Binding = Binding {
        id: "site_Career_RnDCardColourPick2",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDCardColourPick3: Binding = Binding {
        id: "site_Career_RnDCardColourPick3",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDCardColourPick4: Binding = Binding {
        id: "site_Career_RnDCardColourPick4",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDCardDeptIcon0: Binding = Binding {
        id: "site_Career_RnDCardDeptIcon0",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDCardDeptIcon1: Binding = Binding {
        id: "site_Career_RnDCardDeptIcon1",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDCardDeptIcon2: Binding = Binding {
        id: "site_Career_RnDCardDeptIcon2",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDCompareDept: Binding = Binding {
        id: "site_Career_RnDCompareDept",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDContainerOpenMaint: Binding = Binding {
        id: "site_Career_RnDContainerOpenMaint",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDContainerOpenTail: Binding = Binding {
        id: "site_Career_RnDContainerOpenTail",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDDeptBoxGridEntry: Binding = Binding {
        id: "site_Career_RnDDeptBoxGridEntry",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDDeptBoxGridInject: Binding = Binding {
        id: "site_Career_RnDDeptBoxGridInject",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDDeptBoxLayoutProbe: Binding = Binding {
        id: "site_Career_RnDDeptBoxLayoutProbe",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDDeptPagePreQuery: Binding = Binding {
        id: "site_Career_RnDDeptPagePreQuery",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDDeptPageQueryDone: Binding = Binding {
        id: "site_Career_RnDDeptPageQueryDone",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDDeptPageTypeLoop: Binding = Binding {
        id: "site_Career_RnDDeptPageTypeLoop",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDDeptRowCategoryIcon: Binding = Binding {
        id: "site_Career_RnDDeptRowCategoryIcon",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDDeptRowTypeIcon: Binding = Binding {
        id: "site_Career_RnDDeptRowTypeIcon",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDDeptRowTypeIconJoin: Binding = Binding {
        id: "site_Career_RnDDeptRowTypeIconJoin",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDDeptRowTypeLabel: Binding = Binding {
        id: "site_Career_RnDDeptRowTypeLabel",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDDeptRowTypeLabelJoin: Binding = Binding {
        id: "site_Career_RnDDeptRowTypeLabelJoin",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDInProgressColourStore: Binding = Binding {
        id: "site_Career_RnDInProgressColourStore",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDInProgressPartIcon: Binding = Binding {
        id: "site_Career_RnDInProgressPartIcon",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDInProgressStrip: Binding = Binding {
        id: "site_Career_RnDInProgressStrip",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDInProgressTypeIcon: Binding = Binding {
        id: "site_Career_RnDInProgressTypeIcon",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDInProgressTypeIconJoin: Binding = Binding {
        id: "site_Career_RnDInProgressTypeIconJoin",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDInProgressTypeLabel: Binding = Binding {
        id: "site_Career_RnDInProgressTypeLabel",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDInProgressTypeLabelJoin: Binding = Binding {
        id: "site_Career_RnDInProgressTypeLabelJoin",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDListRowColourStore: Binding = Binding {
        id: "site_Career_RnDListRowColourStore",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDListRowDeptIcon: Binding = Binding {
        id: "site_Career_RnDListRowDeptIcon",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDListRowLightColourStore: Binding = Binding {
        id: "site_Career_RnDListRowLightColourStore",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPageModifierList: Binding = Binding {
        id: "site_Career_RnDPageModifierList",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPanelModifierList: Binding = Binding {
        id: "site_Career_RnDPanelModifierList",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPanelResearchLimit: Binding = Binding {
        id: "site_Career_RnDPanelResearchLimit",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPerfChildName: Binding = Binding {
        id: "site_Career_RnDPerfChildName",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPerfDeptColour: Binding = Binding {
        id: "site_Career_RnDPerfDeptColour",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPerfDeptLoopEnd: Binding = Binding {
        id: "site_Career_RnDPerfDeptLoopEnd",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPerfDeptName: Binding = Binding {
        id: "site_Career_RnDPerfDeptName",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPerfGotoName: Binding = Binding {
        id: "site_Career_RnDPerfGotoName",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPerfViewModelSlotDtor: Binding = Binding {
        id: "site_Career_RnDPerfViewModelSlotDtor",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPerksTitleDeptIcon: Binding = Binding {
        id: "site_Career_RnDPerksTitleDeptIcon",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPieBufSize: Binding = Binding {
        id: "site_Career_RnDPieBufSize",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPieCtorRet: Binding = Binding {
        id: "site_Career_RnDPieCtorRet",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPieFocusJoin: Binding = Binding {
        id: "site_Career_RnDPieFocusJoin",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPieFocusNameCmp: Binding = Binding {
        id: "site_Career_RnDPieFocusNameCmp",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPieFocusRestore: Binding = Binding {
        id: "site_Career_RnDPieFocusRestore",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPieMoveAssignSlotDtor: Binding = Binding {
        id: "site_Career_RnDPieMoveAssignSlotDtor",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPieMovedFromSlotDtor: Binding = Binding {
        id: "site_Career_RnDPieMovedFromSlotDtor",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPiePopulateDeptGuard: Binding = Binding {
        id: "site_Career_RnDPiePopulateDeptGuard",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPiePopulateGate: Binding = Binding {
        id: "site_Career_RnDPiePopulateGate",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPiePopulateNextCode: Binding = Binding {
        id: "site_Career_RnDPiePopulateNextCode",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPiePopulateSlotCap: Binding = Binding {
        id: "site_Career_RnDPiePopulateSlotCap",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPieSliceDefaultColour: Binding = Binding {
        id: "site_Career_RnDPieSliceDefaultColour",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPieSliceDescFormat: Binding = Binding {
        id: "site_Career_RnDPieSliceDescFormat",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPieSliceIconArgs: Binding = Binding {
        id: "site_Career_RnDPieSliceIconArgs",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPieSliceNameJoin: Binding = Binding {
        id: "site_Career_RnDPieSliceNameJoin",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPieTeardownSlotDtor: Binding = Binding {
        id: "site_Career_RnDPieTeardownSlotDtor",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPopupCategoryIcon: Binding = Binding {
        id: "site_Career_RnDPopupCategoryIcon",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPopupColourStore: Binding = Binding {
        id: "site_Career_RnDPopupColourStore",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPotentialPieEntry: Binding = Binding {
        id: "site_Career_RnDPotentialPieEntry",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPurchaseBackOpen: Binding = Binding {
        id: "site_Career_RnDPurchaseBackOpen",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPurchaseOptionsEntry: Binding = Binding {
        id: "site_Career_RnDPurchaseOptionsEntry",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPurchasePanelTypeLabel: Binding = Binding {
        id: "site_Career_RnDPurchasePanelTypeLabel",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPurchasePanelTypeLabelJoin: Binding = Binding {
        id: "site_Career_RnDPurchasePanelTypeLabelJoin",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPurchaseRequestEntry: Binding = Binding {
        id: "site_Career_RnDPurchaseRequestEntry",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPurchaseScreenTypeLabel: Binding = Binding {
        id: "site_Career_RnDPurchaseScreenTypeLabel",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDPurchaseScreenTypeLabelJoin: Binding = Binding {
        id: "site_Career_RnDPurchaseScreenTypeLabelJoin",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDResearchFocusNameCmp: Binding = Binding {
        id: "site_Career_RnDResearchFocusNameCmp",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDResearchRowCategoryIcon: Binding = Binding {
        id: "site_Career_RnDResearchRowCategoryIcon",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDResearchRowTypeIcon: Binding = Binding {
        id: "site_Career_RnDResearchRowTypeIcon",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDResearchRowTypeIconJoin: Binding = Binding {
        id: "site_Career_RnDResearchRowTypeIconJoin",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDResearchRowTypeLabel: Binding = Binding {
        id: "site_Career_RnDResearchRowTypeLabel",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDResearchRowTypeLabelJoin: Binding = Binding {
        id: "site_Career_RnDResearchRowTypeLabelJoin",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDResearchTypeLoop: Binding = Binding {
        id: "site_Career_RnDResearchTypeLoop",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDResultTypeIcon: Binding = Binding {
        id: "site_Career_RnDResultTypeIcon",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDResultTypeIconJoin: Binding = Binding {
        id: "site_Career_RnDResultTypeIconJoin",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDResultTypeLabel: Binding = Binding {
        id: "site_Career_RnDResultTypeLabel",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDResultTypeLabelJoin: Binding = Binding {
        id: "site_Career_RnDResultTypeLabelJoin",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDTileCountBody: Binding = Binding {
        id: "site_Career_RnDTileCountBody",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDTileCountExit: Binding = Binding {
        id: "site_Career_RnDTileCountExit",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDTileCountGuards: Binding = Binding {
        id: "site_Career_RnDTileCountGuards",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDTileCountIndexGuard: Binding = Binding {
        id: "site_Career_RnDTileCountIndexGuard",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDTodoTypeLabel: Binding = Binding {
        id: "site_Career_RnDTodoTypeLabel",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDTodoTypeLabelJoin: Binding = Binding {
        id: "site_Career_RnDTodoTypeLabelJoin",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDTreeRowCountProbe: Binding = Binding {
        id: "site_Career_RnDTreeRowCountProbe",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDUpgradeInfoTypeLabel: Binding = Binding {
        id: "site_Career_RnDUpgradeInfoTypeLabel",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDUpgradeInfoTypeLabelJoin: Binding = Binding {
        id: "site_Career_RnDUpgradeInfoTypeLabelJoin",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_RnDUpgradeListDeptIcon: Binding = Binding {
        id: "site_Career_RnDUpgradeListDeptIcon",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_TeamDeptEnumStore: Binding = Binding {
        id: "site_Career_TeamDeptEnumStore",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_TeamHqIsMyTeamResult: Binding = Binding {
        id: "site_Career_TeamHqIsMyTeamResult",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_TeamHqRnDPopupTypeIcon: Binding = Binding {
        id: "site_Career_TeamHqRnDPopupTypeIcon",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_TeamHqRnDPopupTypeIconJoin: Binding = Binding {
        id: "site_Career_TeamHqRnDPopupTypeIconJoin",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_TeleFailedTypeName: Binding = Binding {
        id: "site_Career_TeleFailedTypeName",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_TeleFailedTypeNameJoin: Binding = Binding {
        id: "site_Career_TeleFailedTypeNameJoin",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_TeleRecordTypeName: Binding = Binding {
        id: "site_Career_TeleRecordTypeName",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_TeleRecordTypeNameCall: Binding = Binding {
        id: "site_Career_TeleRecordTypeNameCall",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Career_TeleResearchTypeName: Binding = Binding {
        id: "site_Career_TeleResearchTypeName",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_TeleResearchTypeNameCall: Binding = Binding {
        id: "site_Career_TeleResearchTypeNameCall",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Career_Unk141814320TypeLabel: Binding = Binding {
        id: "site_Career_Unk141814320TypeLabel",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_Unk141814320TypeLabelJoin: Binding = Binding {
        id: "site_Career_Unk141814320TypeLabelJoin",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_Unk1418169f0TypeLabel: Binding = Binding {
        id: "site_Career_Unk1418169f0TypeLabel",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_Unk1418169f0TypeLabelJoin: Binding = Binding {
        id: "site_Career_Unk1418169f0TypeLabelJoin",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_UpgradeAutoFitBody: Binding = Binding {
        id: "site_Career_UpgradeAutoFitBody",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_UpgradeAutoFitDeptGate: Binding = Binding {
        id: "site_Career_UpgradeAutoFitDeptGate",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_UpgradeBlockReplayEntry: Binding = Binding {
        id: "site_Career_UpgradeBlockReplayEntry",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_UpgradeCardDeptIcon: Binding = Binding {
        id: "site_Career_UpgradeCardDeptIcon",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_UpgradeCardTypeLabel: Binding = Binding {
        id: "site_Career_UpgradeCardTypeLabel",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_UpgradeCardTypeLabelJoin: Binding = Binding {
        id: "site_Career_UpgradeCardTypeLabelJoin",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_UpgradeGraphHubTest: Binding = Binding {
        id: "site_Career_UpgradeGraphHubTest",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_UpgradeGraphRegularNode: Binding = Binding {
        id: "site_Career_UpgradeGraphRegularNode",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_UpgradeInitFakeDeptBranch: Binding = Binding {
        id: "site_Career_UpgradeInitFakeDeptBranch",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_UpgradeInitNormal: Binding = Binding {
        id: "site_Career_UpgradeInitNormal",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_UpgradeMetaTypeJoin: Binding = Binding {
        id: "site_Career_UpgradeMetaTypeJoin",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_UpgradePushStateBody: Binding = Binding {
        id: "site_Career_UpgradePushStateBody",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_UpgradePushStateDeptGate: Binding = Binding {
        id: "site_Career_UpgradePushStateDeptGate",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_UpgradeRowTypeJoin: Binding = Binding {
        id: "site_Career_UpgradeRowTypeJoin",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_UpgradeSetEnabledBody: Binding = Binding {
        id: "site_Career_UpgradeSetEnabledBody",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_UpgradeSetEnabledDeptGate: Binding = Binding {
        id: "site_Career_UpgradeSetEnabledDeptGate",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_UpgradeSetFittedBody: Binding = Binding {
        id: "site_Career_UpgradeSetFittedBody",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_UpgradeSetFittedDeptGate: Binding = Binding {
        id: "site_Career_UpgradeSetFittedDeptGate",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_UpgradeSetLockedBody: Binding = Binding {
        id: "site_Career_UpgradeSetLockedBody",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_UpgradeSetLockedDeptGate: Binding = Binding {
        id: "site_Career_UpgradeSetLockedDeptGate",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_VehicleFillOrderEnd: Binding = Binding {
        id: "site_Career_VehicleFillOrderEnd",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_VehicleFillOrderStart: Binding = Binding {
        id: "site_Career_VehicleFillOrderStart",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_WeekendObjectiveNext: Binding = Binding {
        id: "site_Career_WeekendObjectiveNext",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Career_WeekendObjectiveType: Binding = Binding {
        id: "site_Career_WeekendObjectiveType",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_FeFlow_SetStateStatus: Binding = Binding {
        id: "site_FeFlow_SetStateStatus",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_FeHome_AfterCareerCall: Binding = Binding {
        id: "site_FeHome_AfterCareerCall",
        kind: "site",
        status: Status::Stable,
    };
    pub const site_FeTile_CareerContentEnd: Binding = Binding {
        id: "site_FeTile_CareerContentEnd",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_FeTile_EraTilesBuilt: Binding = Binding {
        id: "site_FeTile_EraTilesBuilt",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Game_CareerContinue_DriverSlotTitle: Binding = Binding {
        id: "site_Game_CareerContinue_DriverSlotTitle",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Game_CareerWizardConfirm_NextRoute: Binding = Binding {
        id: "site_Game_CareerWizardConfirm_NextRoute",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Game_CareerWizard_BackRoute: Binding = Binding {
        id: "site_Game_CareerWizard_BackRoute",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Game_DriverSelectHeader_BuildTabs: Binding = Binding {
        id: "site_Game_DriverSelectHeader_BuildTabs",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Game_DriverSelect_BackRoute: Binding = Binding {
        id: "site_Game_DriverSelect_BackRoute",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Game_DriverSelect_BadgeFormat: Binding = Binding {
        id: "site_Game_DriverSelect_BadgeFormat",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Game_DriverSelect_NumberResult: Binding = Binding {
        id: "site_Game_DriverSelect_NumberResult",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Game_DriverSelect_TeamLogoFormat: Binding = Binding {
        id: "site_Game_DriverSelect_TeamLogoFormat",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Game_ErsNewLap_NextVehicle: Binding = Binding {
        id: "site_Game_ErsNewLap_NextVehicle",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Game_ErsNewLap_VehicleLookup: Binding = Binding {
        id: "site_Game_ErsNewLap_VehicleLookup",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Game_SaveData_ContainersChildren: Binding = Binding {
        id: "site_Game_SaveData_ContainersChildren",
        kind: "site",
        status: Status::Stable,
    };
    pub const site_Game_SaveData_ContainersCount: Binding = Binding {
        id: "site_Game_SaveData_ContainersCount",
        kind: "site",
        status: Status::Stable,
    };
    pub const site_Game_SaveData_ContainersDone: Binding = Binding {
        id: "site_Game_SaveData_ContainersDone",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Game_SaveData_UnclaimedChild: Binding = Binding {
        id: "site_Game_SaveData_UnclaimedChild",
        kind: "site",
        status: Status::Stable,
    };
    pub const site_Game_TeamSelect_NumberResult: Binding = Binding {
        id: "site_Game_TeamSelect_NumberResult",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Game_TeamSelect_TeamLogoFormat: Binding = Binding {
        id: "site_Game_TeamSelect_TeamLogoFormat",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Gdb_DeptCountStore: Binding = Binding {
        id: "site_Gdb_DeptCountStore",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Ne_Format_String_Entry: Binding = Binding {
        id: "site_Ne_Format_String_Entry",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Ne_HwBool_CrowdInShadows: Binding = Binding {
        id: "site_Ne_HwBool_CrowdInShadows",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Ne_HwBool_GodRaysPost: Binding = Binding {
        id: "site_Ne_HwBool_GodRaysPost",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Ne_HwBool_GrassFlattening: Binding = Binding {
        id: "site_Ne_HwBool_GrassFlattening",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Ne_HwBool_GroundCoverEnabled: Binding = Binding {
        id: "site_Ne_HwBool_GroundCoverEnabled",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Ne_HwBool_GroundCoverMeshes: Binding = Binding {
        id: "site_Ne_HwBool_GroundCoverMeshes",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Ne_HwBool_LensStreak: Binding = Binding {
        id: "site_Ne_HwBool_LensStreak",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Ne_HwBool_MotionBlurV2: Binding = Binding {
        id: "site_Ne_HwBool_MotionBlurV2",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Ne_HwBool_NightShadowCharCast: Binding = Binding {
        id: "site_Ne_HwBool_NightShadowCharCast",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Ne_HwBool_RainBeads: Binding = Binding {
        id: "site_Ne_HwBool_RainBeads",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Ne_HwBool_RainHigh: Binding = Binding {
        id: "site_Ne_HwBool_RainHigh",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Ne_HwBool_RainSheets: Binding = Binding {
        id: "site_Ne_HwBool_RainSheets",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Ne_HwBool_RainSplashes: Binding = Binding {
        id: "site_Ne_HwBool_RainSplashes",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Ne_HwBool_SkyShadowLowLod: Binding = Binding {
        id: "site_Ne_HwBool_SkyShadowLowLod",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Ne_HwBool_SkyShadowMap32Bits: Binding = Binding {
        id: "site_Ne_HwBool_SkyShadowMap32Bits",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Ne_HwBool_TrackHeatHaze: Binding = Binding {
        id: "site_Ne_HwBool_TrackHeatHaze",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Ne_HwBool_VolumetricFog: Binding = Binding {
        id: "site_Ne_HwBool_VolumetricFog",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Ne_HwConfigParseCall: Binding = Binding {
        id: "site_Ne_HwConfigParseCall",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Ne_HwInt_CrowdBillboardDistance: Binding = Binding {
        id: "site_Ne_HwInt_CrowdBillboardDistance",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Ne_HwInt_NightShadowMapSize: Binding = Binding {
        id: "site_Ne_HwInt_NightShadowMapSize",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Ne_HwInt_NightShadowSourceCount: Binding = Binding {
        id: "site_Ne_HwInt_NightShadowSourceCount",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Ne_HwInt_ShadowSampling: Binding = Binding {
        id: "site_Ne_HwInt_ShadowSampling",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Ne_HwInt_SkyShadowCascadeCount: Binding = Binding {
        id: "site_Ne_HwInt_SkyShadowCascadeCount",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Ne_HwInt_SkyShadowMapSize: Binding = Binding {
        id: "site_Ne_HwInt_SkyShadowMapSize",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Ne_HwInt_SpotShadowMapSize: Binding = Binding {
        id: "site_Ne_HwInt_SpotShadowMapSize",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Ne_HwInt_SpotShadowSampling: Binding = Binding {
        id: "site_Ne_HwInt_SpotShadowSampling",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Ne_HwInt_TexStreamSizeInMiB: Binding = Binding {
        id: "site_Ne_HwInt_TexStreamSizeInMiB",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Ne_HwInt_VolFogMapHeight: Binding = Binding {
        id: "site_Ne_HwInt_VolFogMapHeight",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Ne_HwInt_VolFogMapWidth: Binding = Binding {
        id: "site_Ne_HwInt_VolFogMapWidth",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Ne_HwInt_WorldShadowMapSize: Binding = Binding {
        id: "site_Ne_HwInt_WorldShadowMapSize",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Ne_HwXmlParseCall: Binding = Binding {
        id: "site_Ne_HwXmlParseCall",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Ne_HwXmlWriteCall: Binding = Binding {
        id: "site_Ne_HwXmlWriteCall",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Ne_ReplayAfterRecord: Binding = Binding {
        id: "site_Ne_ReplayAfterRecord",
        kind: "site",
        status: Status::Experimental,
    };
    pub const site_Sess_AiVehicleRegisterCall: Binding = Binding {
        id: "site_Sess_AiVehicleRegisterCall",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Sess_InGamePlayerFinishedAiCall: Binding = Binding {
        id: "site_Sess_InGamePlayerFinishedAiCall",
        kind: "call",
        status: Status::Experimental,
    };
    pub const site_Sess_InGameSessionCompleteAiCall: Binding = Binding {
        id: "site_Sess_InGameSessionCompleteAiCall",
        kind: "call",
        status: Status::Experimental,
    };
    pub const vt_Career_BankTransactionMessage: Binding = Binding {
        id: "vt_Career_BankTransactionMessage",
        kind: "vtable",
        status: Status::Experimental,
    };
    pub const vt_Career_Championship: Binding = Binding {
        id: "vt_Career_Championship",
        kind: "vtable",
        status: Status::Experimental,
    };
    pub const vt_Career_FacilityNode: Binding = Binding {
        id: "vt_Career_FacilityNode",
        kind: "vtable",
        status: Status::Experimental,
    };
    pub const vt_Career_MessageDriverContractChanged: Binding = Binding {
        id: "vt_Career_MessageDriverContractChanged",
        kind: "vtable",
        status: Status::Experimental,
    };
    pub const vt_Career_MessageDriverMoveConfirmed: Binding = Binding {
        id: "vt_Career_MessageDriverMoveConfirmed",
        kind: "vtable",
        status: Status::Experimental,
    };
    pub const vt_Career_MessageDriverMovesFinalised: Binding = Binding {
        id: "vt_Career_MessageDriverMovesFinalised",
        kind: "vtable",
        status: Status::Experimental,
    };
    pub const vt_Career_MessageDriverMovesFinalisedRecognition: Binding = Binding {
        id: "vt_Career_MessageDriverMovesFinalisedRecognition",
        kind: "vtable",
        status: Status::Experimental,
    };
    pub const vt_Career_MessageDriverMovesFinalisedUpgrades: Binding = Binding {
        id: "vt_Career_MessageDriverMovesFinalisedUpgrades",
        kind: "vtable",
        status: Status::Experimental,
    };
    pub const vt_Career_RenownAwardedMessage: Binding = Binding {
        id: "vt_Career_RenownAwardedMessage",
        kind: "vtable",
        status: Status::Experimental,
    };
    pub const vt_Career_RnDDeptCard: Binding = Binding {
        id: "vt_Career_RnDDeptCard",
        kind: "vtable",
        status: Status::Experimental,
    };
    pub const vt_Career_Season: Binding = Binding {
        id: "vt_Career_Season",
        kind: "vtable",
        status: Status::Experimental,
    };
    pub const vt_Career_SponsorLoyaltyXpMessage: Binding = Binding {
        id: "vt_Career_SponsorLoyaltyXpMessage",
        kind: "vtable",
        status: Status::Experimental,
    };
    pub const vt_Career_Weekend: Binding = Binding {
        id: "vt_Career_Weekend",
        kind: "vtable",
        status: Status::Experimental,
    };
    pub const vt_Game_CareerDLCSelectViewModel: Binding = Binding {
        id: "vt_Game_CareerDLCSelectViewModel",
        kind: "vtable",
        status: Status::Experimental,
    };
    pub const vt_Game_CareerWizardSettingsSelectViewModel: Binding = Binding {
        id: "vt_Game_CareerWizardSettingsSelectViewModel",
        kind: "vtable",
        status: Status::Experimental,
    };
    pub const vt_Game_DriverCareerDriverSelectViewModel: Binding = Binding {
        id: "vt_Game_DriverCareerDriverSelectViewModel",
        kind: "vtable",
        status: Status::Experimental,
    };
    pub const vt_Game_DriverCareerSlotPredicate: Binding = Binding {
        id: "vt_Game_DriverCareerSlotPredicate",
        kind: "vtable",
        status: Status::Experimental,
    };
    pub const vt_Game_SaveDataRootContainer: Binding = Binding {
        id: "vt_Game_SaveDataRootContainer",
        kind: "vtable",
        status: Status::Stable,
    };
    pub const vt_Game_TrackListComponent: Binding = Binding {
        id: "vt_Game_TrackListComponent",
        kind: "vtable",
        status: Status::Experimental,
    };
    pub const vt_Sess_ScCanReturn: Binding = Binding {
        id: "vt_Sess_ScCanReturn",
        kind: "vtable",
        status: Status::Experimental,
    };
    pub const vt_Sess_ScRaceLapsRemaining: Binding = Binding {
        id: "vt_Sess_ScRaceLapsRemaining",
        kind: "vtable",
        status: Status::Experimental,
    };
    pub const vt_Sess_ScTriggerRedFlag: Binding = Binding {
        id: "vt_Sess_ScTriggerRedFlag",
        kind: "vtable",
        status: Status::Experimental,
    };
}
