#include "../runtime.hpp"
#include "../registry/table.hpp"

#include <swofty/game.hpp>
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "lineup_plan.hpp"
#include "number_plan.hpp"
#include "seat_plan.hpp"
#include "start_teams.hpp"
#include "stats.hpp"
#include "teams_plan.hpp"
#include "teams_store.hpp"
#include "../apply/content/teams.hpp"
#include "../registry/table.hpp"
#include "track_fuel.hpp"

std::string settings_addon(const void *from, std::string &dll);

// The core's career operations (standard library 1.6, SwoftyCareerOps): apply_lineup, set_driver_ratings and
// swap_drivers, (table version 2) report_season, which the career snapshots answer (snapshots/runtime.cpp), and
// (table version 4) set_seat, which runs one seat change through apply_lineup's executor, set_car_number (Career
// Mode Editor's drivers.number.set) and set_track_fuel (track_fuel.cpp). The line-up executor is Progressive Career's season batch (addons/progressive/native/src/
// season.cpp apply(), seats.cpp) moved into the core: one batch of entrant writes through the Driver Career
// sequence, read back and written back on a mismatch, the seated drivers' contracts, numbers and one
// finalise. Every binding resolves when an operation first needs it; a missing one makes the operation
// report unavailable and log its id.

namespace {

namespace g = swofty::game;
namespace mem = swofty::mem;

using g::EntrantRecord;
using g::EntrantWriteData;

constexpr uint32_t kNoKey = 0x3fff0000u;
constexpr uint32_t kHumanBit = 0x10000000u;
constexpr uint8_t kNoNumber = 0xff;
constexpr size_t kMaxEntrants = 256;
constexpr size_t kIdentitySize = offsetof(EntrantRecord, id_lo);
constexpr uint8_t kReasonSigned = 0;
constexpr uint8_t kReasonReleased = 1;
constexpr uintptr_t kVtWeekendIndex = 0x108;
constexpr uintptr_t kVtRegistryValue = 0x108;
constexpr uint32_t kMinutesPerDay = 1440;
constexpr uint8_t kContractReleased = 3;
constexpr uint8_t kContractRenewed = 4;
constexpr uint8_t kContractLeft = 5;
constexpr size_t kContractSlot = 16;
constexpr size_t kContractRecord = offsetof(g::DriverContract, active);
static_assert(sizeof(EntrantRecord) == 0x88 && sizeof(EntrantWriteData) == 0x2c);
static_assert(kContractRecord == 0x2c && sizeof(g::DriverContractChangedMessage) == 0x78);

using WeekendFn = uint32_t (*)(void *self);
using RegistryValueFn = uint32_t (*)(void *registry);

std::mutex g_lock;
std::map<std::string, std::vector<uint32_t>> g_slots;
std::atomic<uint32_t> g_flow_tid{0};

std::mutex g_bind_lock;
std::unordered_map<std::string, uintptr_t> g_bound;

uintptr_t bind(const char *id)
{
    std::lock_guard<std::mutex> l(g_bind_lock);
    auto it = g_bound.find(id);
    if (it != g_bound.end())
        return it->second;
    uintptr_t a = swofty::api && swofty::api->resolve ? swofty::api->resolve(id) : 0;
    if (a || runtime::code_ready())
        g_bound[id] = a;
    return a;
}

struct Needs {
    std::string missing;
    bool ok(std::initializer_list<const char *> ids)
    {
        for (const char *id : ids)
            if (!bind(id))
                missing += (missing.empty() ? "" : ", ") + std::string(id);
        return missing.empty();
    }
};

constexpr const char *kLineupBindings[] = {
    "Career_Set_EntrantData_Thunk", "vt_Career_MessageDriverMoveConfirmed", "Career_Set_DriverContractState",
    "Career_Build_DriverContract", "vt_Career_MessageDriverContractChanged", "Career_Build_LineupChanges",
    "vt_Career_MessageDriverMovesFinalisedUpgrades", "vt_Career_MessageDriverMovesFinalisedRecognition",
    "vt_Career_MessageDriverMovesFinalised", "Career_DriverNumbers_CheckExisting",
    "k_Career_DriverCareerContractsManagerComponentDesc"};

bool lineup_bound(std::string &missing)
{
    Needs n;
    for (const char *id : kLineupBindings)
        n.ok({id});
    missing = n.missing;
    return missing.empty();
}

bool on_flow_thread()
{
    uint32_t t = g_flow_tid.load();
    return !t || t == GetCurrentThreadId();
}

void frame(double, void *)
{
    g_flow_tid = GetCurrentThreadId();
}

template <class T> T rdv(uintptr_t a, T def = T{})
{
    T v{};
    return mem::read(a, v) ? v : def;
}

bool writable(uintptr_t p, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<const void *>(p), &mbi, sizeof mbi) != sizeof mbi)
        return false;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD))
        return false;
    DWORD w = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (!(mbi.Protect & w))
        return false;
    return p + n <= reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
}

int post(void *msg)
{
    const SwoftyCareer *c = runtime::career_table();
    return c && c->post ? c->post(msg) : SWOFTY_ASSET_UNAVAILABLE;
}

uintptr_t component(const char *desc_id)
{
    const SwoftyCareer *c = runtime::career_table();
    uintptr_t desc = bind(desc_id);
    auto p = desc && c && c->component_by_desc ? reinterpret_cast<uintptr_t>(c->component_by_desc(reinterpret_cast<void *>(desc))) : 0;
    return p && mem::object_like(p) ? p : 0;
}

uintptr_t entity()
{
    const SwoftyCareer *c = runtime::career_table();
    auto e = c && c->entity ? reinterpret_cast<uintptr_t>(c->entity()) : 0;
    g::CareerEntity ent{};
    if (!e || !mem::object_like(e) || !mem::read(e, ent) || !mem::in_image(reinterpret_cast<uintptr_t>(ent.component_registry)))
        return 0;
    return e;
}

struct Row {
    uintptr_t at = 0;
    EntrantRecord rec{};
};

uint32_t driver_of(const EntrantRecord &r)
{
    uint32_t v;
    std::memcpy(&v, &r.id_lo, sizeof v);
    return v;
}

uint32_t stat_of(uint32_t key) { return key >> 16; }
bool none(uint32_t key) { return (key & 0xffff0000u) == kNoKey; }
bool has_car(const EntrantRecord &r) { return !none(r.vehicle_ref); }
bool teamless(const EntrantRecord &r) { return none(r.team_stat_id); }
bool human(const EntrantRecord &r) { return (r.ordinal_flags & kHumanBit) != 0; }
int position_of(const EntrantRecord &r) { return (r.team_position >> 8) ? (r.team_position & 0xff) : -1; }

bool entrant_rows(uintptr_t e, std::vector<Row> &out)
{
    out.clear();
    uintptr_t b = rdv<uintptr_t>(e + offsetof(g::CareerEntity, entrants_begin));
    uintptr_t en = rdv<uintptr_t>(e + offsetof(g::CareerEntity, entrants_end));
    if (!e || !b || en <= b || (en - b) % sizeof(EntrantRecord) || (en - b) / sizeof(EntrantRecord) > kMaxEntrants ||
        !mem::readable(b, en - b))
        return false;
    size_t n = (en - b) / sizeof(EntrantRecord);
    out.resize(n);
    for (size_t i = 0; i < n; ++i) {
        out[i].at = b + i * sizeof(EntrantRecord);
        std::memcpy(&out[i].rec, reinterpret_cast<const void *>(out[i].at), sizeof(EntrantRecord));
    }
    return true;
}

bool entity_busy(uintptr_t e)
{
    return rdv<uint8_t>(e + offsetof(g::CareerEntity, entrant_update_busy)) != 0;
}

bool unique_identity(uintptr_t e, const Row &id_row)
{
    std::vector<Row> rows;
    if (!entrant_rows(e, rows))
        return false;
    uint32_t id = id_row.rec.ordinal_flags;
    bool want_human = id & kHumanBit;
    size_t hits = 0;
    uintptr_t at = 0;
    for (auto &r : rows) {
        uint32_t v = r.rec.ordinal_flags;
        bool match = want_human ? (v & kHumanBit) != 0 : !(v & kHumanBit) && v == id;
        if (match) {
            ++hits;
            at = r.at;
        }
    }
    return hits == 1 && at == id_row.at;
}

EntrantWriteData seat_data(const Row &driver_row, const Row &seat_row)
{
    EntrantWriteData d{};
    d.driver = driver_of(driver_row.rec);
    d.driver_customisation = driver_row.rec.driver_customisation;
    d.team_stat_id = seat_row.rec.team_stat_id;
    d.vehicle_ref = seat_row.rec.vehicle_ref;
    d.driver_reg_value = seat_row.rec.driver_reg_value;
    d.vehicle_customisation = seat_row.rec.vehicle_customisation;
    d.badge_stat_id = seat_row.rec.badge_stat_id;
    d.seat_value = seat_row.rec.const_ffff0012;
    return d;
}

EntrantWriteData own_data(const Row &r)
{
    return seat_data(r, r);
}

EntrantWriteData released_data(const Row &r)
{
    EntrantWriteData d{};
    d.driver = driver_of(r.rec);
    d.driver_customisation = r.rec.driver_customisation;
    d.team_stat_id = kNoKey;
    d.vehicle_ref = kNoKey;
    d.driver_reg_value = kNoKey;
    d.vehicle_customisation = kNoKey;
    d.badge_stat_id = kNoKey;
    d.seat_value = 0xffff0012u;
    return d;
}

bool announce(uintptr_t manager, uint32_t driver, uint32_t old_team, uint32_t new_team, uint8_t reason)
{
    using M = g::DriverCareerContractsManagerComponent;
    uintptr_t vt_msg = bind("vt_Career_MessageDriverMoveConfirmed");
    if (!manager || !vt_msg)
        return false;
    uintptr_t src = rdv<uintptr_t>(manager + offsetof(M, weekend_source));
    uintptr_t time = rdv<uintptr_t>(manager + offsetof(M, career_time));
    uintptr_t vt = src ? rdv<uintptr_t>(src) : 0;
    uintptr_t fn = vt ? rdv<uintptr_t>(vt + kVtWeekendIndex) : 0;
    if (!mem::in_image(fn) || !time)
        return false;
    g::DriverMoveConfirmedMessage m{};
    m.vtable = reinterpret_cast<void **>(vt_msg);
    m.move.driver = driver;
    m.move.old_team = old_team;
    m.move.new_team = new_team;
    m.move.reason = reason;
    m.move.weekend_index = reinterpret_cast<WeekendFn>(fn)(reinterpret_cast<void *>(src));
    m.move.day_index = static_cast<uint16_t>(rdv<uint32_t>(time + offsetof(g::CareerTimeComponent, current_minutes)) / kMinutesPerDay);
    return post(&m) == SWOFTY_ASSET_OK;
}

bool write_row(uintptr_t e, const Row &id_row, const EntrantWriteData &data, uint16_t position, uint8_t number)
{
    auto set = reinterpret_cast<g::Career_Set_EntrantData_Thunk_t *>(bind("Career_Set_EntrantData_Thunk"));
    if (!set || entity_busy(e) || !unique_identity(e, id_row))
        return false;
    uint8_t id[kIdentitySize];
    std::memcpy(id, &id_row.rec, kIdentitySize);
    set(reinterpret_cast<void *>(e), id, reinterpret_cast<const uint32_t *>(&data), position, number);
    return !entity_busy(e);
}

bool same_key(uint32_t a, uint32_t b)
{
    return ((a ^ b) & 0xffff00ffu) == 0;
}

bool contract_clock(uintptr_t e, uintptr_t manager, uint16_t &day, uint32_t &value)
{
    uintptr_t time = rdv<uintptr_t>(manager + offsetof(g::DriverCareerContractsManagerComponent, career_time));
    uintptr_t registry = e + offsetof(g::CareerEntity, component_registry);
    uintptr_t vt = rdv<uintptr_t>(registry);
    uintptr_t fn = vt ? rdv<uintptr_t>(vt + kVtRegistryValue) : 0;
    if (!time || !mem::in_image(fn))
        return false;
    value = reinterpret_cast<RegistryValueFn>(fn)(reinterpret_cast<void *>(registry));
    day = static_cast<uint16_t>(rdv<uint32_t>(time + offsetof(g::CareerTimeComponent, current_minutes)) / kMinutesPerDay);
    return true;
}

bool contract_objects(uintptr_t manager, std::vector<uintptr_t> &out)
{
    out.clear();
    uintptr_t owner = manager ? rdv<uintptr_t>(manager + offsetof(g::DriverCareerContractsManagerComponent, contract_owner)) : 0;
    uintptr_t b = owner ? rdv<uintptr_t>(owner + offsetof(g::DriverCareerContractOwner, contracts_begin)) : 0;
    uintptr_t en = owner ? rdv<uintptr_t>(owner + offsetof(g::DriverCareerContractOwner, contracts_end)) : 0;
    if (!b || en < b || (en - b) % kContractSlot || (en - b) / kContractSlot > 4096 || !mem::readable(b, en - b))
        return false;
    for (uintptr_t p = b; p < en; p += kContractSlot)
        if (uintptr_t c = rdv<uintptr_t>(p + sizeof(void *)); c && mem::readable(c, sizeof(g::DriverContract)))
            out.push_back(c);
    return true;
}

uintptr_t contract_of(const std::vector<uintptr_t> &objects, uint32_t driver)
{
    for (uintptr_t c : objects)
        if (same_key(rdv<uint32_t>(c + offsetof(g::DriverContract, driver)), driver))
            return c;
    return 0;
}

bool contract_fits(uintptr_t contract, uint32_t team)
{
    return contract && rdv<uint8_t>(contract + offsetof(g::DriverContract, active)) &&
           same_key(rdv<uint32_t>(contract + offsetof(g::DriverContract, team)), team);
}

int32_t recommended_target(uintptr_t manager, int32_t base)
{
    struct Range {
        uint32_t min, max, minus, plus;
        int32_t recommended;
    };
    static_assert(sizeof(Range) == 20);
    constexpr uintptr_t kRatingRanges = 0x318;
    if (base < 0)
        base = 0;
    uintptr_t b = rdv<uintptr_t>(manager + kRatingRanges), e = rdv<uintptr_t>(manager + kRatingRanges + 8);
    if (b && e > b && (e - b) % sizeof(Range) == 0 && (e - b) / sizeof(Range) < 64 && mem::readable(b, e - b))
        for (uintptr_t p = b; p < e; p += sizeof(Range)) {
            Range r = rdv<Range>(p);
            if (uint32_t(base) < r.min || uint32_t(base) > r.max)
                continue;
            int64_t lo = std::max<int64_t>(0, int64_t(base) - r.minus);
            int64_t hi = std::min<int64_t>(100, int64_t(base) + r.plus);
            return int32_t(std::clamp<int64_t>(int64_t(base) + r.recommended, std::min(lo, hi), hi));
        }
    return std::min(base, 100);
}

struct Signing {
    uint32_t driver = 0;
    uintptr_t contract = 0;
    uint8_t offer[kContractRecord] = {};
};

const char *plan_signing(uintptr_t manager, const std::vector<uintptr_t> &objects, uint32_t driver, uint32_t team, Signing &s)
{
    constexpr uintptr_t kBuiltRating = 0x18;
    auto build = reinterpret_cast<g::Career_Build_DriverContract_t *>(bind("Career_Build_DriverContract"));
    s.driver = driver;
    s.contract = contract_of(objects, driver);
    if (!s.contract)
        return "the game keeps no contract for him";
    if (!writable(s.contract, offsetof(g::DriverContract, active) + 1))
        return "his contract is not writable";
    if (!manager || !build)
        return "no contracts manager to build the terms";
    alignas(16) uint8_t buf[0x80] = {};
    build(reinterpret_cast<void *>(manager), buf, driver, team, 0, 0, 1);
    int32_t base;
    std::memcpy(&base, buf + kBuiltRating, sizeof base);
    int32_t target = recommended_target(manager, base);
    std::memset(buf, 0, sizeof buf);
    build(reinterpret_cast<void *>(manager), buf, driver, team, 0, target, 1);
    uint32_t built;
    std::memcpy(&built, buf + offsetof(g::DriverContract, team), sizeof built);
    if (!same_key(built, team))
        return "the built terms name another team";
    std::memcpy(s.offer, buf, sizeof s.offer);
    return nullptr;
}

bool sign_contract(uintptr_t e, uintptr_t manager, const Signing &s)
{
    using C = g::DriverContract;
    auto set_state = reinterpret_cast<g::Career_Set_DriverContractState_t *>(bind("Career_Set_DriverContractState"));
    uintptr_t vt_changed = bind("vt_Career_MessageDriverContractChanged");
    uint16_t day;
    uint32_t value;
    if (!set_state || !vt_changed || !contract_clock(e, manager, day, value) || !writable(s.contract, offsetof(C, active) + 1))
        return false;
    uint8_t offer[kContractRecord];
    std::memcpy(offer, s.offer, sizeof offer);
    std::memcpy(offer + offsetof(C, signed_day), &day, sizeof day);
    uint32_t team;
    std::memcpy(&team, offer + offsetof(C, team), sizeof team);
    bool same = same_key(rdv<uint32_t>(s.contract + offsetof(C, team)), team);
    set_state(reinterpret_cast<void *>(s.contract), day, same ? kContractRenewed : kContractLeft, value);
    if (rdv<uint8_t>(s.contract + offsetof(C, active)))
        return false;
    g::DriverContractChangedMessage m{};
    m.vtable = reinterpret_cast<void **>(vt_changed);
    m.driver = rdv<uint32_t>(s.contract + offsetof(C, driver));
    std::memcpy(m.before, reinterpret_cast<const void *>(s.contract), sizeof m.before);
    std::memcpy(reinterpret_cast<void *>(s.contract), offer, sizeof offer);
    *reinterpret_cast<uint8_t *>(s.contract + offsetof(C, active)) = 1;
    std::memcpy(m.after, offer, sizeof m.after);
    m.kind = offer[offsetof(C, kind)];
    return post(&m) == SWOFTY_ASSET_OK;
}

bool release_contract(uintptr_t e, uintptr_t manager, uint32_t driver, bool &found)
{
    found = false;
    auto set_state = reinterpret_cast<g::Career_Set_DriverContractState_t *>(bind("Career_Set_DriverContractState"));
    std::vector<uintptr_t> objects;
    uint16_t day;
    uint32_t value;
    if (!set_state || !contract_objects(manager, objects) || !contract_clock(e, manager, day, value))
        return false;
    uintptr_t contract = contract_of(objects, driver);
    if (!contract)
        return true;
    set_state(reinterpret_cast<void *>(contract), day, kContractReleased, value);
    found = true;
    return true;
}

uintptr_t car_numbers(uintptr_t e, uintptr_t manager)
{
    uintptr_t c = manager ? rdv<uintptr_t>(manager + offsetof(g::DriverCareerContractsManagerComponent, car_numbers)) : 0;
    return c && mem::object_like(c) && rdv<uintptr_t>(c + offsetof(g::CarNumberComponent, entity)) == e ? c : 0;
}

class Baseline {
  public:
    explicit Baseline(const std::vector<Row> &rows)
    {
        for (auto &r : rows)
            rows_.push_back(r.rec);
        vec_.begin = rows_.data();
        vec_.end = rows_.data() + rows_.size();
        vec_.capacity = vec_.end;
    }
    Baseline(const Baseline &) = delete;
    Baseline &operator=(const Baseline &) = delete;
    const void *vector() const { return &vec_; }

  private:
    struct Vec {
        void *allocator = nullptr;
        const EntrantRecord *begin = nullptr;
        const EntrantRecord *end = nullptr;
        const EntrantRecord *capacity = nullptr;
    };
    std::vector<EntrantRecord> rows_;
    Vec vec_;
};

bool finalise(uintptr_t e, const Baseline &baseline, uint64_t &changes)
{
    changes = 0;
    auto build = reinterpret_cast<g::Career_Build_LineupChanges_t *>(bind("Career_Build_LineupChanges"));
    if (!build)
        return false;
    uintptr_t current = e + offsetof(g::CareerEntity, entrants_begin) - sizeof(void *);
    g::DriverMovesFinalisedMessage m{};
    build(&m.changes, reinterpret_cast<void *>(current), baseline.vector());
    if (m.count > 40)
        return false;
    changes = m.count;
    uintptr_t vts[] = {bind("vt_Career_MessageDriverMovesFinalisedUpgrades"), bind("vt_Career_MessageDriverMovesFinalisedRecognition"),
                       bind("vt_Career_MessageDriverMovesFinalised")};
    for (uintptr_t v : vts) {
        g::DriverMovesFinalisedMessage copy = m;
        copy.vtable = reinterpret_cast<void **>(v);
        copy.reserved = 0;
        if (!v || post(&copy) != SWOFTY_ASSET_OK)
            return false;
    }
    return true;
}

struct Labels {
    bool loaded = false;
    std::unordered_map<std::string, uint32_t> driver_by_label;
    std::unordered_map<std::string, uint32_t> look_by_label;
    std::unordered_map<uint32_t, std::string> driver_by_stat;
    std::unordered_map<uint32_t, std::string> team_by_stat;
};

Labels g_labels;

bool db_int(const SwoftyDb *db, const std::string &path, int64_t &out)
{
    return db->int_at(path.c_str(), &out) == SWOFTY_ASSET_OK;
}

bool db_text(const SwoftyDb *db, const std::string &path, std::string &out)
{
    const char *v = nullptr;
    if (db->string_at(path.c_str(), &v) != SWOFTY_ASSET_OK || !v)
        return false;
    size_t n = strnlen(v, 128);
    if (!n || n >= 128 || !mem::printable_utf8(v, n))
        return false;
    out.assign(v, n);
    return true;
}

template <class F> void each_row(const SwoftyDb *db, const std::string &table, const std::string &stem, F f)
{
    int64_t count = 0;
    if (!db_int(db, table + "._" + stem + "_instancecount_", count) || count <= 0 || count > 8192)
        return;
    for (int64_t i = 0; i < count; ++i) {
        std::string row = table + "." + stem + "_" + std::to_string(i), name;
        int64_t id = -1;
        if (db_int(db, row + ".stat_id", id) && id >= 0 && id < 0x3fff && db_text(db, row + "._metadata_", name))
            f(static_cast<uint32_t>(id), name);
    }
}

bool labels()
{
    std::lock_guard<std::mutex> l(g_lock);
    if (g_labels.loaded)
        return true;
    const SwoftyDb *db = runtime::db_table();
    if (!db || !db->available || !db->available())
        return false;
    Labels t;
    each_row(db, "entrants.drivers", "driver", [&](uint32_t s, const std::string &n) {
        t.driver_by_label.emplace(n, s);
        t.driver_by_stat.emplace(s, n);
    });
    for (int folder = 0; folder < 4; ++folder)
        each_row(db, "entrants.driver_customisations.folder_" + std::to_string(folder), "preloaded_driver_customisation_properties",
                 [&](uint32_t s, const std::string &n) { t.look_by_label.emplace(n, s); });
    each_row(db, "entrants.teams", "team", [&](uint32_t s, const std::string &n) { t.team_by_stat.emplace(s, n); });
    t.loaded = !t.driver_by_label.empty();
    if (t.loaded)
        g_labels = std::move(t);
    return t.loaded || g_labels.loaded;
}

std::string driver_name(uint32_t stat)
{
    auto it = g_labels.driver_by_stat.find(stat);
    return (it == g_labels.driver_by_stat.end() ? std::string("?") : it->second) + "(" + std::to_string(stat) + ")";
}

std::string team_name(uint32_t stat)
{
    if (stat == stat_of(kNoKey))
        return "no team";
    auto it = g_labels.team_by_stat.find(stat);
    return (it == g_labels.team_by_stat.end() ? std::string("?") : it->second) + "(" + std::to_string(stat) + ")";
}

std::string record_label_of(const char *registry, const std::string &id)
{
    auto rec = swofty_core::registry::store().get(registry, id);
    if (rec.status == swofty_core::registry::Status::ok && rec.value["label"].is_string() &&
        !rec.value["label"].as_string().empty())
        return std::string(rec.value["label"].as_string());
    size_t slash = id.rfind('/');
    return slash == std::string::npos ? id : id.substr(slash + 1);
}

std::string person_driver_label(const std::string &ref)
{
    auto &st = swofty_core::registry::store();
    std::string found;
    for (auto &id : st.list("driver.identity", "")) {
        if (id.rfind("game/", 0) == 0)
            continue;
        auto rec = st.get("driver.identity", id);
        if (rec.status == swofty_core::registry::Status::ok && rec.value["person"].is_string() &&
            rec.value["person"].as_string() == ref)
            found = record_label_of("driver.identity", id);
    }
    return found;
}

int resolve_driver(const std::string &ref)
{
    size_t slash = ref.find('/');
    if (ref.empty() || slash == std::string::npos || !labels())
        return -1;
    std::string ns = ref.substr(0, slash), label = ref.substr(slash + 1);
    if (ns == "person") {
        std::string own = person_driver_label(ref);
        if (!own.empty())
            label = own;
    } else if (ns != "game") {
        label = record_label_of("driver.identity", ref);
    }
    std::lock_guard<std::mutex> l(g_lock);
    auto it = g_labels.driver_by_label.find(label);
    return it == g_labels.driver_by_label.end() ? -1 : static_cast<int>(it->second);
}

int resolve_look(const std::string &ref)
{
    size_t slash = ref.find('/');
    if (ref.empty() || slash == std::string::npos || !labels())
        return -1;
    std::string label = ref.substr(0, slash) == "game" ? ref.substr(slash + 1) : record_label_of("driver.look", ref);
    std::lock_guard<std::mutex> l(g_lock);
    auto it = g_labels.look_by_label.find(label);
    return it == g_labels.look_by_label.end() ? -1 : static_cast<int>(it->second);
}

std::string quote(const std::string &s)
{
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += char(c);
        } else if (c < 0x20) {
            char b[8];
            std::snprintf(b, sizeof b, "\\u%04x", c);
            out += b;
        } else {
            out += char(c);
        }
    }
    return out + "\"";
}

struct Report {
    std::string result = "ok";
    size_t changed = 0, unchanged = 0, refused = 0, written = 0, numbers = 0, contracts = 0, released = 0;
    std::vector<std::string> refusals;
    std::string finalise = "not needed";
    uint64_t changes = 0;
    std::string extra;

    std::string json() const
    {
        std::string r = "{\"result\":" + quote(result) + ",\"changed\":" + std::to_string(changed) +
                        ",\"unchanged\":" + std::to_string(unchanged) + ",\"refused\":" + std::to_string(refused) +
                        ",\"refusals\":[";
        for (size_t i = 0; i < refusals.size(); ++i)
            r += (i ? "," : "") + quote(refusals[i]);
        r += "],\"written\":" + std::to_string(written) + ",\"released\":" + std::to_string(released) +
             ",\"numbers\":" + std::to_string(numbers) + ",\"finalise\":" + quote(finalise) +
             ",\"line_up_changes\":" + std::to_string(changes) + ",\"contracts_signed\":" + std::to_string(contracts) +
             extra + "}";
        return r;
    }
};

int put(const std::string &text, char *out, size_t cap, size_t *len)
{
    if (len)
        *len = text.size();
    if (!out && !cap)
        return SWOFTY_ASSET_OK;
    if (!out || cap < text.size() + 1)
        return SWOFTY_TOO_SMALL;
    std::memcpy(out, text.c_str(), text.size() + 1);
    return SWOFTY_ASSET_OK;
}

struct Lineup {
    int year = 0;
    std::string why;
    bool visuals = false;
    int player_team = -1;
    struct Move {
        uint32_t driver;
        int team, seat, car;
        int holder = -1, mate = -1;
    };
    struct Reposition {
        uint32_t driver;
        int seat;
    };
    struct Value {
        uint32_t driver;
        int value;
    };
    std::vector<Move> moves;
    std::vector<Reposition> repositions;
    std::vector<uint32_t> releases;
    std::vector<Value> numbers, cars, looks;
};

int number(const json::Value &v, int def = -1)
{
    return v.type == json::Value::Num ? static_cast<int>(v.n) : def;
}

bool read_lineup(const json::Value &j, Lineup &l, std::string &err)
{
    if (j.type != json::Value::Obj) {
        err = "\"lineup\" is not an object";
        return false;
    }
    l.year = number(j["year"], 0);
    l.why = j["why"].str();
    l.visuals = j["visuals"].type == json::Value::Bool && j["visuals"].b;
    l.player_team = number(j["player_team"]);
    auto stat = [&](const json::Value &v, const char *what) -> int {
        int s = number(v);
        if (s < 0 || s >= 0x3fff)
            err = std::string(what) + ": a stat id must be 0..16382";
        return s;
    };
    for (auto &m : j["moves"].a) {
        int d = stat(m["driver"], "moves[].driver"), t = stat(m["team"], "moves[].team"), k = number(m["seat"]);
        if (k < 0 || k > 1)
            err = "moves[].seat must be 0 or 1";
        l.moves.push_back({static_cast<uint32_t>(d), t, k, number(m["car"]), number(m["holder"]), number(m["mate"])});
    }
    for (auto &m : j["repositions"].a) {
        int d = stat(m["driver"], "repositions[].driver"), k = number(m["seat"]);
        if (k < 0 || k > 1)
            err = "repositions[].seat must be 0 or 1";
        l.repositions.push_back({static_cast<uint32_t>(d), k});
    }
    for (auto &v : j["releases"].a)
        l.releases.push_back(static_cast<uint32_t>(stat(v, "releases[]")));
    for (auto &v : j["numbers"].a) {
        int n = number(v["number"]);
        if (n < 0 || n > 0xff)
            err = "numbers[].number must be 0..255";
        l.numbers.push_back({static_cast<uint32_t>(stat(v["driver"], "numbers[].driver")), n});
    }
    for (auto &v : j["cars"].a)
        l.cars.push_back({static_cast<uint32_t>(stat(v["driver"], "cars[].driver")), stat(v["car"], "cars[].car")});
    for (auto &v : j["looks"].a)
        l.looks.push_back({static_cast<uint32_t>(stat(v["driver"], "looks[].driver")), stat(v["look"], "looks[].look")});
    return err.empty();
}

int row_index(const std::vector<Row> &rows, uint32_t stat)
{
    int hit = -1;
    for (size_t i = 0; i < rows.size(); ++i)
        if (stat_of(driver_of(rows[i].rec)) == stat && (driver_of(rows[i].rec) & 0xff) == 0) {
            if (hit >= 0)
                return -2;
            hit = static_cast<int>(i);
        }
    return hit;
}

int holder_of(const std::vector<Row> &rows, uint32_t team, int seat)
{
    for (size_t i = 0; i < rows.size(); ++i)
        if (has_car(rows[i].rec) && !teamless(rows[i].rec) && stat_of(rows[i].rec.team_stat_id) == team &&
            position_of(rows[i].rec) == seat)
            return static_cast<int>(i);
    return -1;
}

struct Write {
    int row = -1;
    EntrantWriteData data{};
    uint16_t position = 0;
    uint8_t number = kNoNumber;
    bool move = false, release = false;
    uint32_t old_team = kNoKey, new_team = kNoKey;
    std::string why;
};

EntrantRecord expected(const Row &r, const Write &w)
{
    EntrantRecord x = r.rec;
    x.driver_customisation = w.data.driver_customisation;
    x.team_stat_id = w.data.team_stat_id;
    x.vehicle_ref = w.data.vehicle_ref;
    x.driver_reg_value = w.data.driver_reg_value;
    x.vehicle_customisation = w.data.vehicle_customisation;
    x.badge_stat_id = w.data.badge_stat_id;
    x.const_ffff0012 = w.data.seat_value;
    x.team_position = w.position;
    x.car_number = w.number;
    return x;
}

int named_or_holder(const std::vector<Row> &rows, int named, uint32_t team, int seat)
{
    if (named >= 0) {
        int idx = row_index(rows, static_cast<uint32_t>(named));
        if (idx >= 0)
            return idx;
    }
    return holder_of(rows, team, seat);
}

bool seat_template(const std::vector<Row> &rows, const Row &driver, uint32_t team, int seat, int car, int holder, int mate_of,
                   EntrantWriteData &data, uint16_t &position, std::string &why)
{
    int h = named_or_holder(rows, holder, team, seat);
    if (h >= 0) {
        data = seat_data(driver, rows[h]);
        position = rows[h].rec.team_position;
        return true;
    }
    const Row *tpl = nullptr;
    int mate = named_or_holder(rows, mate_of, team, 1 - seat);
    if (mate >= 0)
        tpl = &rows[mate];
    for (size_t i = 0; !tpl && i < rows.size(); ++i)
        if (!teamless(rows[i].rec) && stat_of(rows[i].rec.team_stat_id) == team)
            tpl = &rows[i];
    data = tpl ? seat_data(driver, *tpl) : released_data(driver);
    if (!tpl)
        data.team_stat_id = team << 16;
    if (!tpl || !has_car(tpl->rec)) {
        if (car < 0) {
            why = "no row of its team has a car and the request names no car";
            return false;
        }
        data.vehicle_ref = static_cast<uint32_t>(car) << 16;
    }
    position = static_cast<uint16_t>(0x100 | seat);
    return true;
}

std::string seat_text(uint32_t team, int seat)
{
    return team_name(team) + " seat " + std::to_string(seat + 1);
}

int execute(const Lineup &p, Report &rep, std::string &summary)
{
    std::string missing;
    if (!lineup_bound(missing)) {
        rep.result = "failed";
        summary = "unavailable: binding(s) not resolved: " + missing;
        return SWOFTY_ASSET_UNAVAILABLE;
    }
    uintptr_t e = entity();
    std::vector<Row> rows;
    if (!e || !entrant_rows(e, rows)) {
        rep.result = "failed";
        summary = "no career entity or its entrant rows are unreadable";
        return SWOFTY_ASSET_LATE;
    }
    uintptr_t manager = component("k_Career_DriverCareerContractsManagerComponentDesc");
    if (!manager) {
        rep.result = "failed";
        summary = "no DriverCareerContractsManagerComponent (not a Driver Career)";
        return SWOFTY_ASSET_UNAVAILABLE;
    }
    std::vector<uintptr_t> objects;
    if (!contract_objects(manager, objects)) {
        rep.result = "failed";
        summary = "the contract list is unreadable";
        return SWOFTY_ASSET_LATE;
    }
    labels();
    std::map<int, Write> writes;
    auto write_for = [&](int idx) -> Write & {
        auto it = writes.find(idx);
        if (it != writes.end())
            return it->second;
        Write w;
        w.row = idx;
        w.data = own_data(rows[idx]);
        w.position = rows[idx].rec.team_position;
        w.number = rows[idx].rec.car_number;
        return writes.emplace(idx, w).first->second;
    };
    for (auto &m : p.moves) {
        int idx = row_index(rows, m.driver);
        if (idx < 0) {
            rep.result = "refused";
            summary = "driver " + driver_name(m.driver) + " has no single row";
            return SWOFTY_ASSET_INVALID;
        }
        EntrantWriteData data{};
        uint16_t position = 0;
        std::string bad;
        if (!seat_template(rows, rows[idx], static_cast<uint32_t>(m.team), m.seat, m.car, m.holder, m.mate, data, position, bad)) {
            rep.result = "refused";
            summary = seat_text(static_cast<uint32_t>(m.team), m.seat) + " is empty and cannot be written: " + bad;
            return SWOFTY_ASSET_INVALID;
        }
        Write &w = write_for(idx);
        int from = position_of(rows[idx].rec);
        w.data = data;
        w.position = position;
        w.move = true;
        w.old_team = rows[idx].rec.team_stat_id;
        w.new_team = data.team_stat_id;
        w.why = (teamless(rows[idx].rec) || !has_car(rows[idx].rec) ? std::string("free agent")
                                                                      : seat_text(stat_of(rows[idx].rec.team_stat_id), from)) +
                " -> " + seat_text(static_cast<uint32_t>(m.team), m.seat) +
                (named_or_holder(rows, m.holder, static_cast<uint32_t>(m.team), m.seat) < 0 ? " (an empty seat)" : "");
    }
    for (auto &m : p.repositions) {
        int idx = row_index(rows, m.driver);
        if (idx < 0)
            continue;
        Write &w = write_for(idx);
        w.position = static_cast<uint16_t>(0x100 | m.seat);
        w.why = "race seat position " + std::to_string(position_of(rows[idx].rec)) + " -> " + std::to_string(m.seat);
    }
    for (uint32_t d : p.releases) {
        int idx = row_index(rows, d);
        if (idx < 0)
            continue;
        Write &w = write_for(idx);
        w.data = released_data(rows[idx]);
        w.position = 0;
        w.release = true;
        w.old_team = rows[idx].rec.team_stat_id;
        w.why = "released";
    }
    for (auto &n : p.numbers) {
        int idx = row_index(rows, n.driver);
        if (idx >= 0 && !human(rows[idx].rec))
            write_for(idx).number = static_cast<uint8_t>(n.value);
    }
    if (p.visuals) {
        for (auto &c : p.cars) {
            int idx = row_index(rows, c.driver);
            if (idx < 0 || c.value < 0)
                continue;
            Write &w = write_for(idx);
            w.data.vehicle_ref = (static_cast<uint32_t>(c.value) << 16) | (w.data.vehicle_ref & 0xffffu);
        }
        for (auto &l : p.looks) {
            int idx = row_index(rows, l.driver);
            if (idx >= 0 && l.value > 0)
                write_for(idx).data.driver_customisation = static_cast<uint32_t>(l.value) << 16;
        }
    }
    std::vector<int> order;
    for (auto &[idx, w] : writes) {
        EntrantRecord x = expected(rows[idx], w);
        if (std::memcmp(&rows[idx].rec, &x, sizeof x) != 0)
            order.push_back(idx);
        else
            ++rep.unchanged;
    }
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        auto rank = [&](int i) { return writes[i].move ? 0 : writes[i].release ? 1 : 2; };
        return rank(a) < rank(b);
    });
    std::vector<std::string> warnings;
    auto warn = [&](const std::string &s) { warnings.push_back(s); };
    auto sign_seats = [&]() {
        std::vector<Row> now;
        std::vector<uintptr_t> live;
        size_t need = 0;
        if (!entrant_rows(e, now) || !contract_objects(manager, live)) {
            warn("contracts: the line-up or contract list is unreadable");
            return;
        }
        for (auto &r : now) {
            if (!has_car(r.rec) || teamless(r.rec))
                continue;
            uint32_t d = driver_of(r.rec);
            uintptr_t own = contract_of(live, d);
            if (contract_fits(own, r.rec.team_stat_id))
                continue;
            if (human(r.rec)) {
                int at = static_cast<int>(&r - now.data());
                bool moved_player = writes.count(at) && writes[at].move;
                g::DriverContract k{};
                bool stale = own && mem::read(own, k) && k.active;
                if (!moved_player && !stale)
                    continue;
                if (p.player_team < 0 || static_cast<int>(stat_of(r.rec.team_stat_id)) != p.player_team) {
                    warn("player contract left alone: " + driver_name(stat_of(d)) + " sits at " +
                         team_name(stat_of(r.rec.team_stat_id)) + ", which is not the seat the line-up gives him (" +
                         (p.player_team < 0 ? std::string("no seat") : team_name(static_cast<uint32_t>(p.player_team))) + ")");
                    continue;
                }
            }
            ++need;
            Signing s;
            if (const char *why = plan_signing(manager, live, d, r.rec.team_stat_id, s))
                warn(driver_name(stat_of(d)) + " has no contract with " + team_name(stat_of(r.rec.team_stat_id)) + ": " + why);
            else if (!sign_contract(e, manager, s))
                warn(driver_name(stat_of(d)) + "'s contract with " + team_name(stat_of(r.rec.team_stat_id)) + " was not signed");
            else
                ++rep.contracts;
        }
        if (need && rep.contracts != need)
            warn("contracts: " + std::to_string(rep.contracts) + "/" + std::to_string(need) + " signed");
    };
    auto finish = [&](const std::string &head) {
        summary = head;
        for (auto &w : warnings)
            swofty::log("[career-ops]   WARNING %s", w.c_str());
    };
    if (order.empty()) {
        sign_seats();
        finish("nothing to write (" + std::to_string(rep.unchanged) + " row(s) already as planned)");
        return SWOFTY_ASSET_OK;
    }
    std::string rows_text;
    for (int idx : order) {
        const Write &w = writes[idx];
        rows_text += (rows_text.empty() ? "" : "; ") + std::string(w.move ? "move " : w.release ? "release " : "update ") +
                     driver_name(stat_of(driver_of(rows[idx].rec))) + (w.why.empty() ? "" : " " + w.why);
    }
    Baseline baseline(rows);
    std::vector<int> written;
    auto back = [&]() {
        size_t ok = 0;
        for (auto it = written.rbegin(); it != written.rend(); ++it) {
            std::vector<Row> now;
            const Row *r = entrant_rows(e, now) && now.size() > static_cast<size_t>(*it) ? &now[*it] : nullptr;
            if (r && write_row(e, *r, own_data(rows[*it]), rows[*it].rec.team_position, rows[*it].rec.car_number))
                ++ok;
        }
        warn("wrote back " + std::to_string(ok) + "/" + std::to_string(written.size()) + " row(s)" +
             (ok == written.size() ? "" : "; RELOAD THE SAVE without saving"));
        written.clear();
    };
    for (int idx : order) {
        const Write &w = writes[idx];
        std::vector<Row> now;
        if (!entrant_rows(e, now) || now.size() != rows.size() || now[idx].at != rows[idx].at) {
            back();
            rep.result = "failed";
            finish("FAILED: the line-up changed during the batch");
            return SWOFTY_ASSET_INVALID;
        }
        uint32_t d = driver_of(rows[idx].rec);
        bool found = false;
        if (w.release && !release_contract(e, manager, d, found))
            warn(driver_name(stat_of(d)) + ": contract not released");
        if ((w.move || w.release) &&
            !announce(manager, d, w.old_team, w.release ? kNoKey : w.new_team, w.release ? kReasonReleased : kReasonSigned))
            warn(driver_name(stat_of(d)) + ": MessageDriverMoveConfirmed not posted");
        if (!write_row(e, now[idx], w.data, w.position, w.number)) {
            back();
            rep.result = "failed";
            finish("FAILED writing " + driver_name(stat_of(d)));
            return SWOFTY_ASSET_INVALID;
        }
        written.push_back(idx);
    }
    std::vector<Row> after;
    bool ok = entrant_rows(e, after) && after.size() == rows.size();
    for (size_t i = 0; ok && i < rows.size(); ++i) {
        auto it = writes.find(static_cast<int>(i));
        bool mine = it != writes.end() && std::find(written.begin(), written.end(), static_cast<int>(i)) != written.end();
        const EntrantRecord &got = after[i].rec;
        if (!mine) {
            if (std::memcmp(&got, &rows[i].rec, sizeof got) != 0) {
                warn(driver_name(stat_of(driver_of(rows[i].rec))) + " changed though it was not written");
                ok = false;
            }
            continue;
        }
        EntrantRecord want = expected(rows[i], it->second);
        if (got.team_stat_id != want.team_stat_id || got.vehicle_ref != want.vehicle_ref ||
            got.driver_customisation != want.driver_customisation || got.car_number != want.car_number ||
            driver_of(got) != driver_of(want)) {
            warn(driver_name(stat_of(driver_of(rows[i].rec))) + " did not read back as planned");
            ok = false;
        }
    }
    if (!ok) {
        back();
        rep.result = "failed";
        finish("FAILED: the batch did not read back as planned; " + rows_text);
        return SWOFTY_ASSET_INVALID;
    }
    rep.written = written.size();
    for (int idx : written)
        rep.released += writes[idx].release;
    sign_seats();
    auto check = reinterpret_cast<g::Career_DriverNumbers_CheckExisting_t *>(bind("Career_DriverNumbers_CheckExisting"));
    if (uintptr_t cn = car_numbers(e, manager)) {
        std::vector<Row> now;
        if (check && entrant_rows(e, now))
            for (auto &r : now)
                if (has_car(r.rec) && r.rec.car_number == kNoNumber && !entity_busy(e) && unique_identity(e, r)) {
                    check(reinterpret_cast<void *>(cn), driver_of(r.rec));
                    ++rep.numbers;
                }
    }
    bool fin = true;
    if (!p.moves.empty() || !p.releases.empty()) {
        fin = finalise(e, baseline, rep.changes);
        rep.finalise = fin ? "posted" : "failed";
        if (!fin)
            rep.result = "failed";
    }
    finish(std::to_string(written.size()) + " row(s) written, " + std::to_string(rep.contracts) + " contract(s) signed, " +
           std::to_string(rep.numbers) + " number(s) drawn, finalise " + rep.finalise + " (" + std::to_string(rep.changes) +
           " line-up change(s)): " + rows_text);
    return fin ? SWOFTY_ASSET_OK : SWOFTY_ASSET_INVALID;
}

std::vector<career_ops::plan::Row> plan_rows(const std::vector<Row> &rows)
{
    std::vector<career_ops::plan::Row> out;
    for (auto &r : rows)
        out.push_back({driver_of(r.rec), teamless(r.rec) ? -1 : static_cast<int>(stat_of(r.rec.team_stat_id)), position_of(r.rec),
                       has_car(r.rec) && !teamless(r.rec), human(r.rec)});
    return out;
}

bool read_season(const json::Value &s, career_ops::plan::Input &in, int &year, std::string &err)
{
    if (s.type != json::Value::Obj) {
        err = "\"season\" is not an object";
        return false;
    }
    in.data_group = s["data_group"].str();
    year = number(s["year"], 0);
    {
        std::lock_guard<std::mutex> l(g_lock);
        auto it = g_slots.find(in.data_group);
        in.have_slots = it != g_slots.end();
        if (in.have_slots)
            in.slot_teams = it->second;
    }
    for (auto &t : s["teams"].a) {
        career_ops::plan::Team team;
        team.slot = number(t["slot"], 0);
        const json::Value &seats = t["seats"];
        for (size_t k = 0; k < career_ops::plan::kSeats && k < seats.a.size(); ++k) {
            team.refs[k] = seats.a[k].str();
            team.drivers[k] = team.refs[k].empty() ? -1 : resolve_driver(team.refs[k]);
        }
        in.teams.push_back(team);
    }
    return true;
}

int api_available()
{
    return runtime::enabled("career", nullptr) ? 1 : 0;
}

int api_driver_stat(const char *ref, uint32_t *out)
{
    if (!ref || !out)
        return SWOFTY_ASSET_INVALID;
    if (!labels())
        return SWOFTY_ASSET_LATE;
    int s = resolve_driver(ref);
    if (s < 0)
        return SWOFTY_ASSET_INVALID;
    *out = static_cast<uint32_t>(s);
    return SWOFTY_ASSET_OK;
}

int api_set_slots(const char *group, const uint32_t *teams, size_t count)
{
    if (!group || !*group || (count && !teams) || count > 64)
        return SWOFTY_ASSET_INVALID;
    std::vector<uint32_t> v(teams, teams + count);
    std::string list;
    for (size_t i = 0; i < count; ++i) {
        if (teams[i] >= 0x3fff)
            return SWOFTY_ASSET_INVALID;
        list += (i ? " " : "") + std::to_string(teams[i]);
    }
    {
        std::lock_guard<std::mutex> l(g_lock);
        g_slots[group] = std::move(v);
    }
    swofty::log("[career-ops] set_slots %s: %zu slot(s): %s", group, count, list.c_str());
    return SWOFTY_ASSET_OK;
}

int api_apply_lineup(SwoftyText request, char *out, size_t cap, size_t *len)
{
    Report rep;
    std::string summary, head = "apply_lineup";
    int r = SWOFTY_ASSET_OK;
    json::Value j;
    std::string text = request.data ? std::string(request.data, request.size) : std::string();
    if (!on_flow_thread()) {
        rep.result = "failed";
        summary = "late: called off the flow thread";
        r = SWOFTY_ASSET_LATE;
    } else if (!json::parse(text, j) || j.type != json::Value::Obj) {
        rep.result = "refused";
        summary = "the request is not a JSON object";
        r = SWOFTY_ASSET_INVALID;
    } else if (j["lineup"].type == json::Value::Obj) {
        Lineup l;
        std::string err;
        head = "apply_lineup " + std::to_string(number(j["lineup"]["year"], 0)) + " (lineup";
        head += j["lineup"]["why"].str().empty() ? ")" : ": " + j["lineup"]["why"].str() + ")";
        if (!read_lineup(j["lineup"], l, err)) {
            rep.result = "refused";
            summary = err;
            r = SWOFTY_ASSET_INVALID;
        } else {
            r = execute(l, rep, summary);
            rep.changed = rep.written;
        }
    } else if (j["season"].type == json::Value::Obj) {
        career_ops::plan::Input in;
        int year = 0;
        std::string err;
        read_season(j["season"], in, year, err);
        head = "apply_lineup " + std::to_string(year) + " (season, " + (in.data_group.empty() ? "no data group" : in.data_group) + ")";
        std::string missing;
        uintptr_t e = 0;
        std::vector<Row> rows;
        if (!lineup_bound(missing)) {
            rep.result = "failed";
            summary = "unavailable: binding(s) not resolved: " + missing;
            r = SWOFTY_ASSET_UNAVAILABLE;
        } else if (!labels()) {
            rep.result = "failed";
            summary = "the drivers table is not loaded yet";
            r = SWOFTY_ASSET_LATE;
        } else if (!(e = entity()) || !entrant_rows(e, rows)) {
            rep.result = "failed";
            summary = "no career entity or its entrant rows are unreadable";
            r = SWOFTY_ASSET_LATE;
        } else {
            career_ops::plan::Plan p = career_ops::plan::plan(in, plan_rows(rows));
            Lineup l;
            l.year = year;
            l.why = "season record";
            l.player_team = p.player_team;
            for (auto &m : p.moves)
                l.moves.push_back({m.driver, m.team, m.seat, -1, -1, -1});
            l.releases = p.releases;
            rep.refusals = p.refusals;
            rep.refused = p.refusals.size();
            Report run;
            r = execute(l, run, summary);
            rep.result = run.result;
            rep.written = run.written;
            rep.released = run.released;
            rep.numbers = run.numbers;
            rep.contracts = run.contracts;
            rep.finalise = run.finalise;
            rep.changes = run.changes;
            rep.unchanged = p.unchanged;
            rep.changed = r == SWOFTY_ASSET_OK ? p.moves.size() : 0;
        }
    } else {
        rep.result = "refused";
        summary = "the request has neither \"season\" nor \"lineup\"";
        r = SWOFTY_ASSET_INVALID;
    }
    rep.refused = std::max(rep.refused, rep.refusals.size());
    swofty::log("[career-ops] %s: %zu seat(s) set, %zu unchanged, %zu refused; %s", head.c_str(), rep.changed, rep.unchanged,
                rep.refused, summary.c_str());
    for (auto &why : rep.refusals)
        swofty::log("[career-ops]   refused: %s", why.c_str());
    if (rep.result == "ok" && r != SWOFTY_ASSET_OK)
        rep.result = "failed";
    int w = put(rep.json(), out, cap, len);
    return w == SWOFTY_TOO_SMALL && r == SWOFTY_ASSET_OK ? SWOFTY_TOO_SMALL : r;
}

struct StartWorn {
    int slot = 0;
    int64_t stat = -1;
    std::string row, team;
};

struct StartState {
    int year = 0;
    std::vector<StartWorn> worn;
};

std::map<std::string, StartState> g_start;

std::vector<std::string> slot_rows_of(const std::vector<uint32_t> &slot_teams)
{
    std::vector<std::string> rows;
    std::lock_guard<std::mutex> l(g_lock);
    for (uint32_t stat : slot_teams) {
        auto name = g_labels.team_by_stat.find(stat);
        rows.push_back(name == g_labels.team_by_stat.end() ? std::string() : name->second);
    }
    return rows;
}

StartState take_start(const std::string &group)
{
    std::lock_guard<std::mutex> l(g_lock);
    auto it = g_start.find(group);
    if (it == g_start.end())
        return {};
    StartState s = std::move(it->second);
    g_start.erase(it);
    return s;
}

int start_teams(const json::Value &s, Report &rep, std::string &head, std::string &summary)
{
    namespace ct = career_ops::teams;
    std::string group = s["data_group"].str();
    int year = number(s["year"], 0);
    head = "apply_teams " + std::to_string(year) + " (start, " + (group.empty() ? "no data group" : group) + ")";
    if (group.empty()) {
        rep.result = "refused";
        summary = "the request names no data group";
        return SWOFTY_ASSET_INVALID;
    }
    std::vector<uint32_t> slot_teams;
    bool have_slots = false;
    {
        std::lock_guard<std::mutex> l(g_lock);
        auto it = g_slots.find(group);
        if (it != g_slots.end()) {
            have_slots = true;
            slot_teams = it->second;
        }
    }
    ct::StartPlan plan;
    if (year) {
        if (!have_slots) {
            rep.result = "refused";
            summary = "the data group has no slot table yet (set_slots)";
            return SWOFTY_REFUSED;
        }
        if (!labels()) {
            rep.result = "failed";
            summary = "the teams table is not loaded yet";
            return SWOFTY_ASSET_LATE;
        }
        plan = ct::start_plan(group, ct::seasons_of(swofty_core::registry::store(), group), slot_rows_of(slot_teams));
    }
    const ct::StartYear *want = year ? plan.find(year) : nullptr;
    StartState was = take_start(group), now;
    now.year = year;
    size_t dressed = 0, undressed = 0, kept = 0;
    auto wanted = [&](const StartWorn &w) {
        if (!want)
            return false;
        for (auto &d : want->dresses)
            if (d.slot == w.slot && d.team == w.team)
                return true;
        return false;
    };
    for (auto &w : was.worn) {
        if (wanted(w))
            continue;
        std::string row = ct::identity_id(w.row);
        if (ct::dresser() && ct::dresser()(group.c_str(), w.slot, row.c_str(), row.c_str(), w.stat))
            ++undressed;
    }
    if (want) {
        for (auto &d : want->dresses) {
            auto i = static_cast<size_t>(d.slot - 1);
            int64_t stat = i < slot_teams.size() ? static_cast<int64_t>(slot_teams[i]) : -1;
            std::string row = ct::identity_id(d.row), team = ct::identity_id(d.team);
            bool before = std::any_of(was.worn.begin(), was.worn.end(), [&](const StartWorn &w) { return w.slot == d.slot && w.team == d.team; });
            if (ct::dresser() && ct::dresser()(group.c_str(), d.slot, row.c_str(), team.c_str(), stat)) {
                now.worn.push_back({d.slot, stat, d.row, d.team});
                ++(before ? kept : dressed);
                swofty::log("[career-ops] apply_teams slot %d: team row %s (stat id %lld) wears %s in the new-career wizard of %d",
                            d.slot, d.row.c_str(), static_cast<long long>(stat), team.c_str(), year);
            } else {
                rep.refusals.push_back("slot " + std::to_string(d.slot) + ": team row " + d.row + " (stat id " + std::to_string(stat) +
                                       ") could not be dressed as " + team + " yet: the loaded team record or the identity is not reachable");
            }
        }
        for (auto &n : want->notes)
            swofty::log("[career-ops]   %s", n.c_str());
    }
    if (!now.worn.empty()) {
        std::lock_guard<std::mutex> l(g_lock);
        g_start[group] = now;
    }
    rep.changed = dressed + kept;
    rep.refused = rep.refusals.size();
    rep.extra = ",\"dressed\":" + std::to_string(dressed + kept) + ",\"undressed\":" + std::to_string(undressed);
    if (!year)
        summary = std::to_string(undressed) + " slot row(s) back in their own names and colours; the new-career wizard's dress is off";
    else if (!want)
        summary = "no career.season record of " + std::to_string(year) + " names an added team" +
                  (undressed ? "; " + std::to_string(undressed) + " slot row(s) back in their own names and colours" : std::string());
    else
        summary = std::to_string(dressed) + " slot row(s) dressed, " + std::to_string(kept) + " still dressed, " +
                  std::to_string(undressed) + " undressed: Team Select lists the " + std::to_string(year) + " added team(s) on their slot rows";
    return SWOFTY_ASSET_OK;
}

int api_apply_teams(SwoftyText request, char *out, size_t cap, size_t *len)
{
    namespace ct = career_ops::teams;
    Report rep;
    std::string summary, head = "apply_teams";
    int r = SWOFTY_ASSET_OK;
    json::Value j;
    std::string text = request.data ? std::string(request.data, request.size) : std::string();
    ct::Plan tp;
    size_t dressed = 0, dress_pending = 0;
    bool parsed = !on_flow_thread() ? false : json::parse(text, j) && j.type == json::Value::Obj;
    if (parsed && j["start"].type == json::Value::Obj) {
        r = start_teams(j["start"], rep, head, summary);
        swofty::log("[career-ops] %s: %s", head.c_str(), summary.c_str());
        for (auto &why : rep.refusals)
            swofty::log("[career-ops]   refused: %s", why.c_str());
        if (rep.result == "ok" && r != SWOFTY_ASSET_OK)
            rep.result = "failed";
        int w = put(rep.json(), out, cap, len);
        return w == SWOFTY_TOO_SMALL && r == SWOFTY_ASSET_OK ? SWOFTY_TOO_SMALL : r;
    }
    if (!on_flow_thread()) {
        rep.result = "failed";
        summary = "late: called off the flow thread";
        r = SWOFTY_ASSET_LATE;
    } else if (!parsed || j["season"].type != json::Value::Obj) {
        rep.result = "refused";
        summary = "the request is not a JSON object with a \"season\" record";
        r = SWOFTY_ASSET_INVALID;
    } else {
        const json::Value &s = j["season"];
        std::string group = s["data_group"].str();
        ct::SeasonIn cur;
        cur.year = number(s["year"], 0);
        for (auto &t : s["teams"].a) {
            ct::SlotIn x;
            x.slot = number(t["slot"], 0);
            x.team = t["team"].str();
            for (size_t k = 0; k < static_cast<size_t>(ct::kSeats) && k < t["seats"].a.size(); ++k)
                x.seats[k] = t["seats"].a[k].str();
            x.car = number(t["car"], -1);
            cur.slots.push_back(x);
        }
        head = "apply_teams " + std::to_string(cur.year) + " (season, " + (group.empty() ? "no data group" : group) + ")";
        std::vector<ct::SeasonIn> all = ct::seasons_of(swofty_core::registry::store(), group);
        all.erase(std::remove_if(all.begin(), all.end(), [&](const ct::SeasonIn &x) { return x.year == cur.year; }), all.end());
        all.push_back(cur);
        bool my_team = false;
        if (const SwoftyCareer *c = runtime::career_table(); c && c->state) {
            SwoftyCareerState st{};
            if (c->state(&st) == SWOFTY_ASSET_OK)
                my_team = st.mode == SWOFTY_CAREER_MY_TEAM;
        }
        ct::Table table = ct::build_table(std::move(all), my_team);
        std::vector<uint32_t> slot_teams;
        bool have_slots = false;
        {
            std::lock_guard<std::mutex> l(g_lock);
            auto it = g_slots.find(group);
            if (it != g_slots.end()) {
                have_slots = true;
                slot_teams = it->second;
            }
        }
        if (have_slots && labels()) {
            std::vector<std::string> physical;
            std::lock_guard<std::mutex> l(g_lock);
            for (uint32_t stat : slot_teams) {
                auto name = g_labels.team_by_stat.find(stat);
                physical.push_back(name == g_labels.team_by_stat.end() ? std::string() : name->second);
            }
            ct::anchor_rows(table, physical);
        }
        tp = ct::plan_season(table, cur);
        std::string missing;
        uintptr_t e = 0;
        std::vector<Row> rows;
        if (!tp.ok()) {
            rep.result = "refused";
            rep.refusals = tp.refusals;
            summary = "refused: " + (tp.refusals.empty() ? std::string("no plan") : tp.refusals.front());
            r = SWOFTY_REFUSED;
        } else if (!have_slots || slot_teams.size() < static_cast<size_t>(table.count())) {
            rep.result = "refused";
            summary = "the slot table of data group " + (group.empty() ? std::string("(none)") : group) + " has " +
                      std::to_string(slot_teams.size()) + " slot(s), the records need " + std::to_string(table.count());
            r = SWOFTY_REFUSED;
        } else if (!lineup_bound(missing)) {
            rep.result = "failed";
            summary = "unavailable: binding(s) not resolved: " + missing;
            r = SWOFTY_ASSET_UNAVAILABLE;
        } else if (!labels()) {
            rep.result = "failed";
            summary = "the drivers table is not loaded yet";
            r = SWOFTY_ASSET_LATE;
        } else if (!(e = entity()) || !entrant_rows(e, rows)) {
            rep.result = "failed";
            summary = "no career entity or its entrant rows are unreadable";
            r = SWOFTY_ASSET_LATE;
        } else {
            career_ops::plan::Input in;
            in.data_group = group;
            in.have_slots = true;
            in.slot_teams = slot_teams;
            std::map<int, int> car_of_team;
            for (auto &a : tp.actions) {
                if (a.kind == ct::Kind::closed)
                    continue;
                if (a.car >= 0)
                    car_of_team[static_cast<int>(slot_teams[static_cast<size_t>(a.slot - 1)])] = a.car;
                auto cs = std::find_if(cur.slots.begin(), cur.slots.end(), [&](const ct::SlotIn &x) { return x.slot == a.slot; });
                career_ops::plan::Team team;
                team.slot = a.slot;
                for (size_t k = 0; k < static_cast<size_t>(ct::kSeats); ++k) {
                    team.refs[k] = cs->seats[k];
                    team.drivers[k] = team.refs[k].empty() ? -1 : resolve_driver(team.refs[k]);
                }
                in.teams.push_back(team);
            }
            career_ops::plan::Plan p = career_ops::plan::plan(in, plan_rows(rows));
            std::vector<ct::SeatRow> seat_rows;
            for (auto &x : plan_rows(rows))
                seat_rows.push_back({stat_of(x.driver), x.team, x.position, x.car, x.human});
            ct::Closing closing = ct::close_slots(tp, slot_teams, seat_rows);
            Lineup l;
            l.year = cur.year;
            l.why = "season record";
            if (j["lineup"].type == json::Value::Obj) {
                std::string err;
                if (!read_lineup(j["lineup"], l, err)) {
                    rep.result = "refused";
                    summary = err;
                    r = SWOFTY_ASSET_INVALID;
                }
                l.year = cur.year;
                if (l.why.empty())
                    l.why = "season record";
            }
            if (r == SWOFTY_ASSET_OK) {
                if (l.player_team < 0)
                    l.player_team = p.player_team;
                std::vector<career_ops::plan::Given> given;
                for (auto &m : l.moves)
                    given.push_back({m.driver, m.team, m.seat});
                for (auto &m : l.repositions)
                    for (auto &x : plan_rows(rows))
                        if (career_ops::plan::stat_of(x.driver) == m.driver && x.car)
                            given.push_back({m.driver, x.team, m.seat});
                career_ops::plan::defer_to(p, given);
                for (auto &m : p.moves) {
                    auto c = car_of_team.find(m.team);
                    l.moves.push_back({m.driver, m.team, m.seat, c == car_of_team.end() ? -1 : c->second, -1, -1});
                }
                for (uint32_t d : p.releases)
                    if (std::find(l.releases.begin(), l.releases.end(), d) == l.releases.end())
                        l.releases.push_back(d);
                for (uint32_t d : closing.releases)
                    if (std::find(l.releases.begin(), l.releases.end(), d) == l.releases.end())
                        l.releases.push_back(d);
                rep.refusals = p.refusals;
                rep.refusals.insert(rep.refusals.end(), closing.refusals.begin(), closing.refusals.end());
                rep.refused = rep.refusals.size();
                StartState wizard = take_start(group);
                for (auto &a : tp.actions) {
                    int64_t row_stat = static_cast<int64_t>(slot_teams[static_cast<size_t>(a.slot - 1)]);
                    std::string row = ct::identity_id(a.row);
                    if (a.kind == ct::Kind::closed) {
                        if (ct::dresser())
                            ct::dresser()(group.c_str(), a.slot, row.c_str(), row.c_str(), row_stat);
                        continue;
                    }
                    if (a.kind == ct::Kind::open) {
                        if (ct::dresser())
                            ct::dresser()(group.c_str(), a.slot, row.c_str(), row.c_str(), row_stat);
                        continue;
                    }
                    std::string team = ct::identity_id(a.team);
                    bool from_wizard = std::any_of(wizard.worn.begin(), wizard.worn.end(), [&](const StartWorn &w) {
                        return w.slot == a.slot && w.stat == row_stat && ct::identity_id(w.team) == team;
                    });
                    if (ct::dresser() && ct::dresser()(group.c_str(), a.slot, row.c_str(), team.c_str(), row_stat)) {
                        ++dressed;
                        swofty::log("[career-ops] apply_teams slot %d: team row %s (stat id %lld) is dressed as %s%s", a.slot, a.row.c_str(),
                                    static_cast<long long>(row_stat), team.c_str(),
                                    from_wizard ? " (the same row and dress as the new-career wizard's)" : "");
                    } else {
                        ++dress_pending;
                        rep.refusals.push_back("slot " + std::to_string(a.slot) + ": team row " + a.row + " (stat id " +
                                               std::to_string(row_stat) + ") could not be dressed as " + team +
                                               " yet: the loaded team record or the identity is not reachable");
                    }
                }
                rep.refused = rep.refusals.size();
                Report run;
                r = execute(l, run, summary);
                rep.result = run.result;
                rep.written = run.written;
                rep.released = run.released;
                rep.numbers = run.numbers;
                rep.contracts = run.contracts;
                rep.finalise = run.finalise;
                rep.changes = run.changes;
                rep.unchanged = p.unchanged;
                rep.changed = r == SWOFTY_ASSET_OK ? p.moves.size() : 0;
            }
        }
        rep.extra = ",\"slots\":" + std::to_string(tp.actions.size()) + ",\"open\":" + std::to_string(tp.count(ct::Kind::open)) +
                    ",\"closed\":" + std::to_string(tp.count(ct::Kind::closed)) +
                    ",\"dressed\":" + std::to_string(dressed) + ",\"dress_pending\":" + std::to_string(dress_pending);
    }
    rep.refused = std::max(rep.refused, rep.refusals.size());
    swofty::log("[career-ops] %s: %s; %zu seat(s) set, %zu unchanged, %zu released, %zu refused, %zu dressed%s; %s", head.c_str(),
                tp.actions.empty() ? "no plan" : ct::summary(tp).c_str(), rep.changed, rep.unchanged, rep.released, rep.refused,
                dressed, dress_pending ? (", " + std::to_string(dress_pending) + " waiting for the team applier").c_str() : "",
                summary.c_str());
    for (auto &why : rep.refusals)
        swofty::log("[career-ops]   refused: %s", why.c_str());
    if (rep.result == "ok" && r != SWOFTY_ASSET_OK)
        rep.result = "failed";
    int w = put(rep.json(), out, cap, len);
    return w == SWOFTY_TOO_SMALL && r == SWOFTY_ASSET_OK ? SWOFTY_TOO_SMALL : r;
}

int api_set_driver_ratings(uint32_t driver, const uint16_t *ids, const float *values, size_t count)
{
    if (!ids || !values || !count || count > 16 || driver >= 0x3fff)
        return SWOFTY_ASSET_INVALID;
    if (!on_flow_thread()) {
        swofty::log("[career-ops] set_driver_ratings %u: late: called off the flow thread", driver);
        return SWOFTY_ASSET_LATE;
    }
    labels();
    std::string text;
    int r = career_ops::stats::set(driver, ids, values, count, text);
    swofty::log("[career-ops] set_driver_ratings %s: %s", driver_name(driver).c_str(), text.c_str());
    return r;
}

int api_swap_drivers(uint32_t a, uint32_t b)
{
    std::string summary;
    int r = SWOFTY_ASSET_OK;
    Report rep;
    labels();
    std::string head = "swap_drivers " + driver_name(a) + " <-> " + driver_name(b);
    std::string missing;
    uintptr_t e = 0;
    std::vector<Row> rows;
    if (!on_flow_thread()) {
        summary = "late: called off the flow thread";
        r = SWOFTY_ASSET_LATE;
    } else if (a >= 0x3fff || b >= 0x3fff || a == b) {
        summary = "two different driver stat ids are needed";
        r = SWOFTY_ASSET_INVALID;
    } else if (!lineup_bound(missing)) {
        summary = "unavailable: binding(s) not resolved: " + missing;
        r = SWOFTY_ASSET_UNAVAILABLE;
    } else if (!(e = entity()) || !entrant_rows(e, rows)) {
        summary = "no career entity or its entrant rows are unreadable";
        r = SWOFTY_ASSET_LATE;
    } else {
        int ia = row_index(rows, a), ib = row_index(rows, b);
        auto seated = [&](int i) { return i >= 0 && has_car(rows[i].rec) && !teamless(rows[i].rec) && position_of(rows[i].rec) >= 0; };
        if (ia < 0 || ib < 0) {
            summary = "refused: " + driver_name(ia < 0 ? a : b) + " has no single career row";
            r = SWOFTY_ASSET_INVALID;
        } else if (human(rows[ia].rec) || human(rows[ib].rec)) {
            summary = "refused: the player is never swapped";
            r = SWOFTY_ASSET_INVALID;
        } else if (!seated(ia) || !seated(ib)) {
            summary = "refused: " + driver_name(seated(ia) ? b : a) + " has no race seat";
            r = SWOFTY_ASSET_INVALID;
        } else {
            Lineup l;
            l.why = "swap";
            for (auto &row : rows)
                if (human(row.rec) && !teamless(row.rec))
                    l.player_team = static_cast<int>(stat_of(row.rec.team_stat_id));
            l.moves.push_back({a, static_cast<int>(stat_of(rows[ib].rec.team_stat_id)), position_of(rows[ib].rec), -1,
                               static_cast<int>(b), -1});
            l.moves.push_back({b, static_cast<int>(stat_of(rows[ia].rec.team_stat_id)), position_of(rows[ia].rec), -1,
                               static_cast<int>(a), -1});
            r = execute(l, rep, summary);
        }
    }
    swofty::log("[career-ops] %s: %s", head.c_str(), summary.c_str());
    return r;
}

int api_report_season(int32_t year, int32_t season_index)
{
    std::string dll, who = settings_addon(__builtin_return_address(0), dll);
    if (who.empty())
        who = dll.empty() ? std::string("an unknown module") : dll;
    if (!on_flow_thread()) {
        swofty::log("[career-ops] report_season(%d, %d) from %s: late: called off the flow thread", year, season_index,
                    who.c_str());
        return SWOFTY_ASSET_LATE;
    }
    return runtime::snapshots_report_season(who, year, season_index);
}

std::string caller(const void *from)
{
    std::string dll, who = settings_addon(from, dll);
    return who.empty() ? (dll.empty() ? std::string("an unknown module") : dll) : who;
}

std::string loaded_data_group()
{
    char group[128] = {};
    if (auto t = runtime::career_content_table(); t && t->data_group && t->data_group(nullptr, group, sizeof group) == SWOFTY_ASSET_OK)
        return group;
    return {};
}

int api_set_seat(SwoftyText request, char *out, size_t cap, size_t *len)
{
    Report rep;
    std::string summary, head = "set_seat";
    int r = SWOFTY_ASSET_OK;
    json::Value j;
    std::string text = request.data ? std::string(request.data, request.size) : std::string();
    career_ops::seat::Plan sp;
    labels();
    if (!on_flow_thread()) {
        rep.result = "failed";
        summary = "late: called off the flow thread";
        r = SWOFTY_ASSET_LATE;
    } else if (!json::parse(text, j) || j.type != json::Value::Obj) {
        rep.result = "refused";
        summary = "the request is not a JSON object";
        r = SWOFTY_ASSET_INVALID;
    } else {
        int driver = number(j["driver"]), team = number(j["team"]), slot = number(j["slot"]), seat = number(j["seat"]);
        std::string group;
        if (team < 0 && slot >= 1) {
            group = loaded_data_group();
            std::lock_guard<std::mutex> l(g_lock);
            auto it = g_slots.find(group);
            if (it != g_slots.end() && static_cast<size_t>(slot) <= it->second.size())
                team = static_cast<int>(it->second[slot - 1]);
        }
        head = "set_seat " + (driver >= 0 ? driver_name(static_cast<uint32_t>(driver)) : std::string("?")) + " -> " +
               (team >= 0 ? seat_text(static_cast<uint32_t>(team), seat) : "slot " + std::to_string(slot) + " seat " + std::to_string(seat + 1));
        std::string missing;
        uintptr_t e = 0;
        std::vector<Row> rows;
        if (driver < 0 || driver >= 0x3fff) {
            rep.result = "refused";
            summary = "\"driver\" must be a driver stat id 0..16382";
            r = SWOFTY_ASSET_INVALID;
        } else if (team < 0) {
            rep.result = "refused";
            summary = slot >= 1 ? "slot " + std::to_string(slot) + " is not in the slot table of data group " +
                                      (group.empty() ? std::string("(none loaded)") : group)
                                : std::string("the request names neither \"team\" nor \"slot\"");
            r = SWOFTY_ASSET_INVALID;
        } else if (!lineup_bound(missing)) {
            rep.result = "failed";
            summary = "unavailable: binding(s) not resolved: " + missing;
            r = SWOFTY_ASSET_UNAVAILABLE;
        } else if (!(e = entity()) || !entrant_rows(e, rows)) {
            rep.result = "failed";
            summary = "no career entity or its entrant rows are unreadable";
            r = SWOFTY_ASSET_LATE;
        } else {
            sp = career_ops::seat::plan(plan_rows(rows), static_cast<uint32_t>(driver), team, seat);
            if (!sp.refusal.empty()) {
                rep.result = "refused";
                rep.refusals.push_back(sp.refusal);
                rep.refused = 1;
                summary = "refused: " + sp.refusal;
                r = SWOFTY_ASSET_INVALID;
            } else if (sp.unchanged) {
                rep.unchanged = 1;
                summary = "already on that seat";
            } else {
                Lineup l;
                l.why = "set_seat";
                l.player_team = sp.player_team;
                l.moves.push_back({sp.driver, sp.team, sp.seat, number(j["car"]), sp.holder, -1});
                if (sp.what == career_ops::seat::Holder::swapped)
                    l.moves.push_back({static_cast<uint32_t>(sp.holder), sp.from_team, sp.from_seat, -1, static_cast<int>(sp.driver), -1});
                else if (sp.what == career_ops::seat::Holder::released)
                    l.releases.push_back(static_cast<uint32_t>(sp.holder));
                r = execute(l, rep, summary);
                rep.changed = r == SWOFTY_ASSET_OK ? 1 : 0;
            }
        }
    }
    swofty::log("[career-ops] %s: %s", head.c_str(), summary.c_str());
    if (rep.result == "ok" && r != SWOFTY_ASSET_OK)
        rep.result = "failed";
    std::string body = rep.json();
    body.insert(body.size() - 1, ",\"holder\":" + quote(career_ops::seat::name(sp.what)));
    int w = put(body, out, cap, len);
    return w == SWOFTY_TOO_SMALL && r == SWOFTY_ASSET_OK ? SWOFTY_TOO_SMALL : r;
}

constexpr uintptr_t kVtCurrentWeekendOf = 0xa8;
constexpr uintptr_t kWeekendRowsBegin = 0x20;
constexpr uintptr_t kWeekendRowsEnd = 0x28;

using CurrentWeekendFn = uintptr_t (*)(void *registry);

uintptr_t weekend_row(uintptr_t e, const EntrantRecord &row)
{
    uintptr_t registry = e + offsetof(g::CareerEntity, component_registry);
    uintptr_t vt = rdv<uintptr_t>(registry);
    uintptr_t fn = vt ? rdv<uintptr_t>(vt + kVtCurrentWeekendOf) : 0;
    if (!mem::in_image(fn))
        return 0;
    uintptr_t weekend = reinterpret_cast<CurrentWeekendFn>(fn)(reinterpret_cast<void *>(registry));
    uintptr_t b = weekend ? rdv<uintptr_t>(weekend + kWeekendRowsBegin) : 0;
    uintptr_t en = weekend ? rdv<uintptr_t>(weekend + kWeekendRowsEnd) : 0;
    if (!b || en < b || (en - b) % sizeof(EntrantRecord) || (en - b) / sizeof(EntrantRecord) > kMaxEntrants)
        return 0;
    for (uintptr_t p = b; p < en; p += sizeof(EntrantRecord)) {
        uint32_t v = rdv<uint32_t>(p + offsetof(EntrantRecord, ordinal_flags));
        bool want_human = row.ordinal_flags & kHumanBit;
        if (want_human ? (v & kHumanBit) != 0 : !(v & kHumanBit) && v == row.ordinal_flags)
            return driver_of(rdv<EntrantRecord>(p)) == driver_of(row) ? p : 0;
    }
    return 0;
}

bool write_number(uintptr_t e, const Row &row, uint8_t n, bool &weekend)
{
    weekend = false;
    auto set = reinterpret_cast<g::Career_Set_EntrantData_Thunk_t *>(bind("Career_Set_EntrantData_Thunk"));
    if (!set || entity_busy(e) || (!human(row.rec) && !unique_identity(e, row)))
        return false;
    uint8_t id[kIdentitySize];
    std::memcpy(id, &row.rec, kIdentitySize);
    EntrantWriteData d = own_data(row);
    set(reinterpret_cast<void *>(e), id, reinterpret_cast<const uint32_t *>(&d), row.rec.team_position, n);
    if (uintptr_t w = weekend_row(e, row.rec); w && writable(w + offsetof(EntrantRecord, car_number), 1)) {
        *reinterpret_cast<uint8_t *>(w + offsetof(EntrantRecord, car_number)) = n;
        weekend = true;
    }
    return true;
}

std::string number_text(uint8_t n)
{
    return n == kNoNumber ? std::string("none") : "#" + std::to_string(n);
}

int api_set_car_number(uint32_t driver, int32_t want_number, uint32_t flags)
{
    labels();
    std::string head = "set_car_number " + driver_name(driver) + " #" + std::to_string(want_number) +
                       ((flags & SWOFTY_NUMBER_SWAP) ? " (swap)" : "");
    std::string summary;
    int r = SWOFTY_ASSET_OK;
    uintptr_t e = 0;
    std::vector<Row> rows;
    if (!on_flow_thread()) {
        summary = "late: called off the flow thread";
        r = SWOFTY_ASSET_LATE;
    } else if (driver >= 0x3fff || (flags & ~uint32_t(SWOFTY_NUMBER_SWAP))) {
        summary = "a driver stat id 0..16382 and flags 0 or SWOFTY_NUMBER_SWAP are needed";
        r = SWOFTY_ASSET_INVALID;
    } else if (!bind("Career_Set_EntrantData_Thunk")) {
        summary = "unavailable: binding(s) not resolved: Career_Set_EntrantData_Thunk";
        r = SWOFTY_ASSET_UNAVAILABLE;
    } else if (!(e = entity()) || !entrant_rows(e, rows)) {
        summary = "no career entity or its entrant rows are unreadable";
        r = SWOFTY_ASSET_LATE;
    } else if (entity_busy(e)) {
        summary = "the game is in the middle of an entrant update";
        r = SWOFTY_ASSET_LATE;
    } else {
        std::vector<career_ops::numbers::Row> view;
        size_t humans = 0;
        for (auto &row : rows) {
            view.push_back({driver_of(row.rec), row.rec.car_number, !teamless(row.rec)});
            humans += human(row.rec);
        }
        int idx = row_index(rows, driver);
        uint32_t key = idx >= 0 ? driver_of(rows[idx].rec) : driver << 16;
        career_ops::numbers::Plan np = career_ops::numbers::plan(view, key, want_number, flags & SWOFTY_NUMBER_SWAP);
        if (np.error != career_ops::numbers::Error::none) {
            summary = "refused: " + np.why;
            r = np.error == career_ops::numbers::Error::taken ? SWOFTY_REFUSED : SWOFTY_ASSET_INVALID;
        } else if (np.unchanged) {
            summary = "already " + number_text(rows[np.row].rec.car_number);
        } else {
            Row row = rows[np.row];
            const Row *holder = np.swap_with >= 0 ? &rows[np.swap_with] : nullptr;
            Row other = holder ? *holder : Row{};
            uint8_t had = row.rec.car_number, want = static_cast<uint8_t>(want_number);
            bool unique = true;
            for (const Row *x : {&row, holder ? &other : nullptr})
                if (x && (human(x->rec) ? humans != 1 : !unique_identity(e, *x)))
                    unique = false;
            if (!unique) {
                summary = "refused: an entrant row's identity is not unique in the line-up; nothing written";
                r = SWOFTY_ASSET_INVALID;
            } else {
                bool week_a = false, week_b = false;
                bool ok = write_number(e, row, want, week_a);
                ok = ok && (!holder || write_number(e, other, had, week_b));
                std::vector<Row> now;
                ok = ok && entrant_rows(e, now) && now.size() == rows.size();
                for (size_t i = 0; ok && i < rows.size(); ++i) {
                    EntrantRecord expect = rows[i].rec;
                    if (rows[i].at == row.at)
                        expect.car_number = want;
                    else if (holder && rows[i].at == other.at)
                        expect.car_number = had;
                    ok = now[i].at == rows[i].at && std::memcmp(&now[i].rec, &expect, sizeof expect) == 0;
                }
                if (!ok) {
                    bool wa, wb;
                    bool back = (!holder || write_number(e, other, want, wb)) && write_number(e, row, had, wa);
                    summary = std::string("FAILED: the entrant rows did not read back with the new number; ") +
                              (back ? "restored the old numbers" : "COULD NOT restore every row, RELOAD THE SAVE without saving");
                    r = SWOFTY_ASSET_INVALID;
                } else {
                    summary = number_text(had) + " -> " + number_text(want) +
                              (holder ? "; " + driver_name(stat_of(driver_of(other.rec))) + " " + number_text(want) + " -> " +
                                            number_text(had)
                                      : std::string()) +
                              " (weekend row " + (week_a && (!holder || week_b) ? "updated" : "not found") + "; " +
                              std::to_string(rows.size() - (holder ? 2 : 1)) + " other row(s) unchanged)";
                }
            }
        }
    }
    swofty::log("[career-ops] %s: %s", head.c_str(), summary.c_str());
    return r;
}

int api_set_track_fuel(SwoftyText request, char *out, size_t cap, size_t *len)
{
    std::string who = caller(__builtin_return_address(0));
    if (!on_flow_thread()) {
        swofty::log("[career-ops] set_track_fuel from %s: late: called off the flow thread", who.c_str());
        return SWOFTY_ASSET_LATE;
    }
    return career_ops::set_track_fuel(who, request, out, cap, len);
}

int api_set_logo(const char *team_id, const char *logo_id)
{
    if (!team_id || !logo_id || !*team_id || !*logo_id)
        return SWOFTY_ASSET_INVALID;
    namespace reg = swofty_core::registry;
    reg::Store &st = reg::store();
    bool stock = std::string_view(team_id).rfind("game/", 0) == 0;
    if ((!stock && st.get("team.identity", team_id).status != reg::Status::ok) ||
        st.get("team.logo", logo_id).status != reg::Status::ok) {
        swofty::log("[career-ops] set_logo %s -> %s: refused: no such %s record", team_id, logo_id,
                    !stock && st.get("team.identity", team_id).status != reg::Status::ok ? "team.identity" : "team.logo");
        return SWOFTY_REFUSED;
    }
    swofty_core::apply::content::live().logo_override[team_id] = logo_id;
    swofty::log("[career-ops] set_logo %s -> %s: the team's logo for this session", team_id, logo_id);
    return SWOFTY_ASSET_OK;
}

int api_set_look(const char *driver_ref, const char *look_ref)
{
    if (!driver_ref || !look_ref || !*driver_ref || !*look_ref)
        return SWOFTY_ASSET_INVALID;
    if (!on_flow_thread()) {
        swofty::log("[career-ops] set_look %s %s: late: called off the flow thread", driver_ref, look_ref);
        return SWOFTY_ASSET_LATE;
    }
    if (!labels()) {
        swofty::log("[career-ops] set_look %s %s: the drivers table is not loaded yet", driver_ref, look_ref);
        return SWOFTY_ASSET_LATE;
    }
    int driver = resolve_driver(driver_ref), look = resolve_look(look_ref);
    if (driver < 0 || look <= 0) {
        swofty::log("[career-ops] set_look %s %s: refused: %s is not a driver or look the game loaded", driver_ref, look_ref,
                    driver < 0 ? driver_ref : look_ref);
        return SWOFTY_ASSET_INVALID;
    }
    std::string request = "{\"lineup\":{\"visuals\":true,\"why\":\"set_look\",\"looks\":[{\"driver\":" +
                          std::to_string(driver) + ",\"look\":" + std::to_string(look) + "}]}}";
    char report[512];
    size_t len = 0;
    int r = api_apply_lineup({request.data(), request.size()}, report, sizeof report, &len);
    swofty::log("[career-ops] set_look %s -> look %s: %s", driver_name(static_cast<uint32_t>(driver)).c_str(), look_ref,
                r == SWOFTY_ASSET_OK ? "applied" : "not applied");
    return r == SWOFTY_TOO_SMALL ? SWOFTY_ASSET_OK : r;
}

const SwoftyCareerOps g_table = {
    SWOFTY_CAREER_OPS_VERSION, api_available,  api_driver_stat, api_set_slots, api_apply_lineup, api_set_driver_ratings,
    api_swap_drivers,          api_report_season, api_apply_teams, api_set_seat,  api_set_car_number, api_set_track_fuel,
    api_set_logo, api_set_look,
};

void setup()
{
    const SwoftyEvents *ev = runtime::events_table();
    uint32_t token = 0;
    if (ev && ev->on_frame)
        ev->on_frame(frame, nullptr, &token);
    career_ops::track_fuel_setup();
}

}

extern "C" __declspec(dllexport) const SwoftyCareerOps *SwoftyCoreCareerOps(void)
{
    return &g_table;
}

SWOFTY_ON_LOAD
{
    runtime::when_code_ready("career operations: flow thread", setup);
}
