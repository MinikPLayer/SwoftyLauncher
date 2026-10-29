// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Career Mode Editor: drivers (ratings, line-up swaps, contract money, the drivers snapshot).
//
// Layouts are KB structs (re/db/structs, generated into swofty/game/career.hpp and entrants.hpp):
//   DriverStatisticsComponent  the career's statistics (My Team; the Driver Career subclass
//                              DriverCareerPlayerDriverStatisticsComponent shares it), found
//                              through ce::comp. Its getters take &stats_data (the embedded
//                              driverStatistics::Data).
//   DriverStatsEntry           one driverData element: DriverStatId + default and context sets
//   ContextStatSet, DriverStatistic, EvolutionCurve, CurvePoint, StatInfo
//   CareerTimeComponent        clock and season dates for the curve progress
//   EntrantRecord, EntrantWriteData, CareerEntity   the championship line-up and its writer
//   CareerDriverContractRenewalComponent, DriverMoveRecord   the game's move log
//   DriverContractsMyTeamComponent, ContractRecord           My Team contract lists
//   DriverCareerContractOwner, DriverContract, DriverContractChangedMessage   Driver Career contracts
//   CarNumberComponent         the career's racing numbers, held by both contracts components
//   NeAllocatorVtbl            the allocators of the entrant vector and the contract list (free agent add)
//
// A rating edit rewrites the y of every point of the curve the getter uses (count and x kept, so
// nothing is reallocated), then rebuilds lut[] and the end value with the game's own evaluator
// (Career_Eval_Curve), exactly as the game's curve loader does after parsing points. Dynamic
// statistics have no curve: their dynamic_level moves instead. Everything is read back through the
// game's getter for the same driver, and every other driver's value is compared before and after.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <swofty/db.hpp>
#include <swofty/career.hpp>
#include <swofty/mem.hpp>
#include <swofty/game.hpp>
#include <swofty/mixin.hpp>

#include "ce.hpp"
#include "race_numbers.hpp"
#include "components.hpp"
#include "lineup.hpp"
#include "rating_solve.hpp"

using namespace swofty::game;

namespace {

using StatsComp = DriverStatisticsComponent;

constexpr uintptr_t kCompData = offsetof(StatsComp, stats_data);
constexpr uintptr_t kCompCtx = offsetof(StatsComp, stat_context);
constexpr uintptr_t kCompTime = offsetof(StatsComp, career_time);
constexpr uintptr_t kCompInfos = offsetof(StatsComp, stat_infos_begin);
constexpr uintptr_t kCompGate = offsetof(StatsComp, use_story_mode_data);
constexpr uintptr_t kCompApplied = offsetof(StatsComp, last_patch_applied_checksum);
constexpr uintptr_t kCompDeclined = offsetof(StatsComp, last_patch_declined_checksum);
constexpr uintptr_t kCompDrivers = offsetof(StatsComp, driver_data_begin);
constexpr uintptr_t kTimeDay = offsetof(CareerTimeComponent, current_minutes);
constexpr uintptr_t kTimeLength = offsetof(CareerTimeComponent, season_dates);

constexpr uintptr_t kDriverSize = sizeof(DriverStatsEntry);
constexpr uintptr_t kDriverSetStats = offsetof(DriverStatsEntry, stats_begin);
constexpr uintptr_t kDriverCtx = offsetof(DriverStatsEntry, context_sets_begin);
constexpr uintptr_t kCtxSize = sizeof(ContextStatSet);
constexpr uintptr_t kCtxMaskA = offsetof(ContextStatSet, mask_a);
constexpr uintptr_t kCtxMaskB = offsetof(ContextStatSet, mask_b);
constexpr uintptr_t kCtxStats = offsetof(ContextStatSet, stats_begin);

constexpr uintptr_t kStatSize = sizeof(DriverStatistic);
constexpr uintptr_t kStatInfo = offsetof(DriverStatistic, info_stat_id);
constexpr uintptr_t kStatCurve = offsetof(DriverStatistic, evolution_graph);
constexpr uintptr_t kStatPatched = offsetof(DriverStatistic, patched_evolution_graph);
constexpr uintptr_t kStatHasPatched = offsetof(DriverStatistic, has_patched_graph);
constexpr uintptr_t kStatBoost = offsetof(DriverStatistic, temporary_boost_value);
constexpr uintptr_t kStatDynamic = offsetof(DriverStatistic, is_dynamic_stat);
constexpr uintptr_t kStatLevel = offsetof(DriverStatistic, dynamic_level);

constexpr uintptr_t kInfoId = offsetof(StatInfo, stat_id);
constexpr uintptr_t kInfoWeight = offsetof(StatInfo, weight);
constexpr uintptr_t kInfoDefault = offsetof(StatInfo, default_value);

constexpr uintptr_t kCurveEnd = offsetof(EvolutionCurve, end_value);
constexpr uintptr_t kCurveMinY = offsetof(EvolutionCurve, min_y);
constexpr uintptr_t kCurveMaxY = offsetof(EvolutionCurve, max_y);
constexpr uintptr_t kCurveLut = offsetof(EvolutionCurve, lut);
constexpr uintptr_t kCurvePoints = offsetof(EvolutionCurve, points_begin);
constexpr size_t kLutSize = sizeof(EvolutionCurve::lut);
constexpr size_t kPointSize = sizeof(CurvePoint);
constexpr size_t kPointY = offsetof(CurvePoint, y);

// Vtable slot offsets of DriverStatisticsComponent and of its context provider (slot indexes, not
// fields): +0x268 min, +0x270 max, +0x278 fixed progress, +0x280 Career_Get_DriverStatValue.
constexpr uintptr_t kVtMin = 0x268;
constexpr uintptr_t kVtMax = 0x270;
constexpr uintptr_t kVtFixedProgress = 0x278;
constexpr uintptr_t kVtGetStat = 0x280;
constexpr uintptr_t kCtxVtFill = 0xc8;

// Sanity bounds for vectors read from game memory (not game fields).
constexpr size_t kMaxDrivers = 1024;
constexpr size_t kMaxStats = 64;
constexpr size_t kMaxSets = 32;
constexpr size_t kMaxPoints = 256;
constexpr size_t kMaxInfos = 64;
constexpr size_t kUndoCap = 64;
constexpr float kNear = 0.5f;       // read-back distance that sends an edit to the search
constexpr float kLevelTop = 100.0f; // highest dynamic level the search tries (the ratings scale)

using GetStatFn = float (*)(void *self, void *data, const uint32_t *driver, const uint16_t *info, const uint8_t *ctx,
                            float x, bool patched, bool clamp, bool round);
using BoundFn = float (*)(void *self);
using FixedFn = bool (*)(void *self, const uint32_t *driver);
using CtxFn = void (*)(void *self, uint8_t *out);

template <class T> bool rd(uintptr_t a, T &out)
{
    return swofty::mem::read(a, out);
}

template <class T> T rdv(uintptr_t a, T def = T{})
{
    T v{};
    return swofty::mem::read(a, v) ? v : def;
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

template <class T> bool wr(uintptr_t a, const T &v)
{
    if (!writable(a, sizeof v))
        return false;
    std::memcpy(reinterpret_cast<void *>(a), &v, sizeof v);
    return true;
}

uint32_t save_id(uint32_t key)
{
    return ((key & 0xff) << 14) | (key >> 16);
}

uint32_t key_from_save(uint32_t id)
{
    return ((id >> 14) & 0x3) | ((id & 0x3fff) << 16);
}

// ---------------------------------------------------------------------------
// The component and its vectors.

struct Comp {
    uintptr_t at = 0;
    uintptr_t data = 0;
    uintptr_t vt = 0;
    bool player_class = false;
};

bool vec(uintptr_t at, size_t stride, size_t cap, uintptr_t &b, size_t &n)
{
    uintptr_t e = 0;
    if (!rd(at, b) || !rd(at + 8, e))
        return false;
    if (!b || e < b || (e - b) % stride || (e - b) / stride > cap)
        return false;
    n = (e - b) / stride;
    return n == 0 || swofty::mem::readable(b, e - b);
}

bool find_comp(Comp &c, const char **why)
{
    c = {};
    uintptr_t p = ce::comp::get(ce::comp::Id::DriverCareerPlayerDriverStatistics);
    c.player_class = p != 0;
    if (!p)
        p = ce::comp::get(ce::comp::Id::DriverStatistics);
    if (!p) {
        *why = "no DriverStatisticsComponent on the championship entity";
        return false;
    }
    uintptr_t vt = 0;
    if (!rd(p, vt) || !swofty::mem::readable(vt, kVtGetStat + sizeof(void *))) {
        *why = "component vtable unreadable";
        return false;
    }
    uintptr_t b;
    size_t n;
    if (!vec(p + kCompDrivers, kDriverSize, kMaxDrivers, b, n)) {
        *why = "driver table unreadable";
        return false;
    }
    c.at = p;
    c.data = p + kCompData;
    c.vt = vt;
    return true;
}

template <class F> F slot(const Comp &c, uintptr_t off)
{
    uintptr_t fn = rdv<uintptr_t>(c.vt + off);
    return swofty::mem::in_image(fn) ? reinterpret_cast<F>(fn) : nullptr;
}

uintptr_t find_driver(const Comp &c, uint32_t key)
{
    uintptr_t b;
    size_t n;
    if (!vec(c.at + kCompDrivers, kDriverSize, kMaxDrivers, b, n))
        return 0;
    for (size_t i = 0; i < n; ++i) {
        uintptr_t e = b + i * kDriverSize;
        if (rdv<uint8_t>(e + offsetof(DriverStatsEntry, kind)) == (key & 0xff) &&
            rdv<uint16_t>(e + offsetof(DriverStatsEntry, stat_id)) == (key >> 16))
            return e;
    }
    return 0;
}

bool context(const Comp &c, uint8_t ctx[16])
{
    std::memset(ctx, 0, 16);
    uintptr_t obj = rdv<uintptr_t>(c.at + kCompCtx);
    uintptr_t vt = obj ? rdv<uintptr_t>(obj) : 0;
    uintptr_t fn = vt ? rdv<uintptr_t>(vt + kCtxVtFill) : 0;
    if (!swofty::mem::in_image(fn))
        return false;
    reinterpret_cast<CtxFn>(fn)(reinterpret_cast<void *>(obj), ctx);
    return true;
}

bool patched_gate(const Comp &c)
{
    return rdv<uint8_t>(c.at + kCompGate) == 0 && rdv<uint32_t>(c.at + kCompApplied) >= 2;
}

float progress(const Comp &c, uint32_t key)
{
    auto fixed = slot<FixedFn>(c, kVtFixedProgress);
    if (!fixed || fixed(reinterpret_cast<void *>(c.at), &key))
        return 0.0f;
    uintptr_t time = rdv<uintptr_t>(c.at + kCompTime);
    if (!time || !swofty::mem::readable(time + kTimeLength, kTimeDay - kTimeLength + sizeof(uint32_t)))
        return 0.0f;
    uint32_t clock = rdv<uint32_t>(time + kTimeDay);
    uint32_t len = Career_Get_CareerLengthDays_Thunk(reinterpret_cast<void *>(time + kTimeLength));
    if (!len)
        return 0.0f;
    return std::min(1.0f, static_cast<float>(clock) / static_cast<float>(len));
}

float stat_value(const Comp &c, uint32_t key, uint16_t info, bool clamp, bool round)
{
    auto get = slot<GetStatFn>(c, kVtGetStat);
    if (!get)
        return NAN;
    uint8_t ctx[16];
    context(c, ctx);
    return get(reinterpret_cast<void *>(c.at), reinterpret_cast<void *>(c.data), &key, &info, ctx,
               progress(c, key), patched_gate(c), clamp, round);
}

float overall(const Comp &c, uint32_t key)
{
    return Career_Get_DriverOverallRating(reinterpret_cast<void *>(c.at), reinterpret_cast<void *>(c.data), &key,
                                          true);
}

// Every statistic set of a driver: the default one first, then the context-aware ones.
std::vector<uintptr_t> sets_of(uintptr_t driver)
{
    std::vector<uintptr_t> out{driver + kDriverSetStats};
    uintptr_t b;
    size_t n;
    if (vec(driver + kDriverCtx, kCtxSize, kMaxSets, b, n))
        for (size_t i = 0; i < n; ++i)
            out.push_back(b + i * kCtxSize + kCtxStats);
    return out;
}

// The set the game's getter reads for this context (same match as the getter).
uintptr_t active_set(uintptr_t driver, const uint8_t ctx[16])
{
    uintptr_t b;
    size_t n;
    if (vec(driver + kDriverCtx, kCtxSize, kMaxSets, b, n)) {
        for (size_t i = 0; i < n; ++i) {
            uintptr_t e = b + i * kCtxSize;
            uint32_t ma = rdv<uint32_t>(e + kCtxMaskA), mb = rdv<uint32_t>(e + kCtxMaskB);
            bool a = !ctx[1] || (ctx[0] < 32 && (ma & (1u << ctx[0])));
            bool bb = !ctx[3] || (ctx[2] < 32 && (mb & (1u << ctx[2])));
            if (a && bb)
                return e + kCtxStats;
        }
    }
    return driver + kDriverSetStats;
}

uintptr_t find_stat(uintptr_t set, uint16_t info)
{
    uintptr_t b;
    size_t n;
    if (!vec(set, kStatSize, kMaxStats, b, n))
        return 0;
    for (size_t i = 0; i < n; ++i)
        if (rdv<uint16_t>(b + i * kStatSize + kStatInfo) == info)
            return b + i * kStatSize;
    return 0;
}

uintptr_t used_curve(const Comp &c, uintptr_t stat)
{
    if (patched_gate(c) && rdv<uint8_t>(stat + kStatHasPatched))
        return stat + kStatPatched;
    return stat + kStatCurve;
}

struct CurveView {
    uintptr_t pts = 0;
    size_t n = 0;
    float miny = 0, maxy = 100;
};

bool curve_view(uintptr_t curve, CurveView &v)
{
    if (!vec(curve + kCurvePoints, kPointSize, kMaxPoints, v.pts, v.n) || v.n < 4)
        return false;
    v.miny = rdv<float>(curve + kCurveMinY);
    v.maxy = rdv<float>(curve + kCurveMaxY);
    return std::isfinite(v.miny) && std::isfinite(v.maxy) && v.maxy > v.miny;
}

// Career_Eval_Curve returns 0 outside 0..1 instead of clamping, so x is clamped here.
float eval_curve(uintptr_t curve, float x)
{
    if (!(x >= 0.0f))
        x = 0.0f;
    if (x > 1.0f)
        x = 1.0f;
    return Career_Eval_Curve(reinterpret_cast<void *>(curve), x);
}

float curve_at(uintptr_t curve, const CurveView &v, float x)
{
    return eval_curve(curve, x) * (v.maxy - v.miny) + v.miny;
}

bool curve_rebuild(uintptr_t curve, const CurveView &v)
{
    if (!writable(curve + kCurveEnd, kCurveLut + kLutSize - kCurveEnd))
        return false;
    uint8_t lut[kLutSize];
    for (size_t i = 0; i < kLutSize; ++i) {
        float y = eval_curve(curve, static_cast<float>(i) * 0.015625f);
        int q = static_cast<int>(0.5f + y * 255.0f);
        lut[i] = static_cast<uint8_t>(q < 1 ? 0 : q > 254 ? 255 : q);
    }
    std::memcpy(reinterpret_cast<void *>(curve + kCurveLut), lut, sizeof lut);
    float end = rdv<float>(v.pts + (v.n - 2) * kPointSize + kPointY) * (v.maxy - v.miny) + v.miny;
    return wr(curve + kCurveEnd, end);
}

bool curve_read_y(const CurveView &v, std::vector<float> &ys)
{
    ys.resize(v.n);
    for (size_t i = 0; i < v.n; ++i)
        if (!rd(v.pts + i * kPointSize + kPointY, ys[i]))
            return false;
    return true;
}

bool curve_write_y(uintptr_t curve, const CurveView &v, const std::vector<float> &ys)
{
    if (ys.size() != v.n || !writable(v.pts, v.n * kPointSize))
        return false;
    for (size_t i = 0; i < v.n; ++i)
        std::memcpy(reinterpret_cast<void *>(v.pts + i * kPointSize + kPointY), &ys[i], sizeof(float));
    return curve_rebuild(curve, v);
}

// ---------------------------------------------------------------------------
// Names from the gamedb (entrants.drivers, entrants.teams, driver_statistics_info).
//
// Strings come from the db library's string_at (it decodes the game's encoded string records);
// integers from int_at. Every string is checked again here: 1..63 bytes of printable UTF-8, or no name.

constexpr size_t kNameMax = 64; // longest name the snapshot accepts (sanity bound, not a game field)

struct Names {
    bool loaded = false;
    std::unordered_map<uint32_t, std::string> driver, initials, team, stat;
    std::unordered_map<uint32_t, int> number;
};

Names g_names;

bool db_text(const std::string &path, std::string &out)
{
    out.clear();
    const char *v = nullptr;
    if (swofty::db::string_at(path, &v) != swofty::db::Result::ok || !v)
        return false;
    size_t n = strnlen(v, kNameMax);
    if (!n || n >= kNameMax || !swofty::mem::printable_utf8(v, n))
        return false;
    out.assign(v, n);
    return true;
}

bool db_int(const std::string &path, int64_t &out)
{
    return swofty::db::int_at(path, &out) == swofty::db::Result::ok;
}

template <class F> void each_row(const std::string &table, const std::string &stem, F f)
{
    int64_t count = 0;
    if (!db_int(table + "._" + stem + "_instancecount_", count) || count <= 0 || count > 4096)
        return;
    for (int64_t i = 0; i < count; ++i)
        f(table + "." + stem + "_" + std::to_string(i));
}

int g_name_tries = 0;

const Names &names()
{
    if (g_names.loaded || !swofty::db::available())
        return g_names;
    if (g_name_tries++ % 30 != 0)
        return g_names;
    each_row("entrants.drivers", "driver", [](const std::string &row) {
        int64_t id;
        if (!db_int(row + ".stat_id", id) || id < 0 || id > 0xffff)
            return;
        uint32_t k = static_cast<uint32_t>(id);
        std::string v;
        if (db_text(row + "._metadata_", v))
            g_names.driver[k] = v;
        if (db_text(row + ".initials", v))
            g_names.initials[k] = v;
        int64_t num;
        if (db_int(row + ".car_number", num) && num >= 0 && num < 1000)
            g_names.number[k] = static_cast<int>(num);
    });
    each_row("entrants.teams", "team", [](const std::string &row) {
        int64_t id;
        std::string v;
        if (db_int(row + ".stat_id", id) && id >= 0 && id <= 0xffff && db_text(row + "._metadata_", v))
            g_names.team[static_cast<uint32_t>(id)] = v;
    });
    each_row("driver_statistics.driver_statistics.driver_statistics_info", "StatisticInfo",
             [](const std::string &row) {
                 int64_t id;
                 std::string v;
                 if (db_int(row + ".stat_id", id) && id >= 0 && id <= 0xffff && db_text(row + "._metadata_", v))
                     g_names.stat[static_cast<uint32_t>(id)] = v;
             });
    g_names.loaded = !g_names.driver.empty();
    if (g_names.loaded || g_name_tries <= 90)
        ce::log("drivers: gamedb names loaded: %zu drivers, %zu teams, %zu statistics",
            g_names.driver.size(), g_names.team.size(), g_names.stat.size());
    return g_names;
}

std::string stat_name(uint16_t info)
{
    auto &m = names().stat;
    auto it = m.find(info);
    if (it != m.end() && !it->second.empty())
        return it->second;
    return "stat_" + std::to_string(info);
}

struct InfoRow {
    uint16_t id;
    float weight;
    float def;
};

std::vector<InfoRow> infos(const Comp &c)
{
    std::vector<InfoRow> out;
    uintptr_t b;
    size_t n;
    if (!vec(c.at + kCompInfos, 16, kMaxInfos, b, n))
        return out;
    for (size_t i = 0; i < n; ++i) {
        uintptr_t p = rdv<uintptr_t>(b + i * 16);
        if (!p || !swofty::mem::readable(p, kInfoDefault + sizeof(float)))
            continue;
        out.push_back({rdv<uint16_t>(p + kInfoId), rdv<float>(p + kInfoWeight), rdv<float>(p + kInfoDefault)});
    }
    return out;
}

std::vector<uint16_t> stat_ids(uintptr_t driver)
{
    std::vector<uint16_t> out;
    uintptr_t b;
    size_t n;
    if (vec(driver + kDriverSetStats, kStatSize, kMaxStats, b, n))
        for (size_t i = 0; i < n; ++i)
            out.push_back(rdv<uint16_t>(b + i * kStatSize + kStatInfo));
    return out;
}

bool resolve_stat(uintptr_t driver, const std::string &k, uint16_t &out)
{
    char *end = nullptr;
    long v = std::strtol(k.c_str(), &end, 0);
    bool numeric = end && *end == 0 && !k.empty();
    for (uint16_t id : stat_ids(driver)) {
        if ((numeric && id == v) || stat_name(id) == k) {
            out = id;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Undo memory: the exact y values (and dynamic level) before the last edits.

struct Saved {
    uint32_t key;
    uint16_t info;
    size_t set;
    bool patched;
    std::vector<float> ys;
    float level;
};

std::vector<Saved> g_undo;

void remember(Saved s)
{
    if (g_undo.size() >= kUndoCap)
        g_undo.erase(g_undo.begin());
    g_undo.push_back(std::move(s));
}

// ---------------------------------------------------------------------------
// JSON helpers.

void stat_json(ce::Writer &w, const Comp &c, uint32_t key, uintptr_t driver, uint16_t info, const uint8_t ctx[16],
               float x, bool detail)
{
    uintptr_t st = find_stat(active_set(driver, ctx), info);
    w.begin_object();
    w.key("value");
    float v = stat_value(c, key, info, true, true);
    if (std::isfinite(v))
        w.integer(static_cast<int64_t>(v));
    else
        w.null();
    bool dyn = st && rdv<uint8_t>(st + kStatDynamic) != 0;
    uintptr_t cv = st ? used_curve(c, st) : 0;
    w.key("base");
    CurveView view;
    if (st && !dyn && curve_view(cv, view))
        w.num(std::round(curve_at(cv, view, x) * 10.0) / 10.0);
    else
        w.null();
    w.key("curve");
    w.str(!st ? "none" : dyn ? "dynamic" : cv == st + kStatPatched ? "patched" : "base");
    if (detail && st) {
        w.key("dynamicLevel");
        w.num(rdv<float>(st + kStatLevel));
        w.key("boost");
        w.num(rdv<float>(st + kStatBoost));
    }
    w.end_object();
}

void ratings_json(ce::Writer &w, const Comp &c, uint32_t key, uintptr_t driver, const std::vector<uint16_t> &which,
                  bool full)
{
    uint8_t ctx[16];
    context(c, ctx);
    float x = progress(c, key);
    w.key("ratings");
    w.begin_object();
    for (uint16_t info : which) {
        w.key(stat_name(info).c_str());
        stat_json(w, c, key, driver, info, ctx, x, full);
    }
    w.end_object();
}

// ---------------------------------------------------------------------------
// Contracts (My Team, read only). DriverContractsMyTeamComponent keeps four lists of 0x70-byte
// contract/move records, each list {allocator, begin, end, cap} as the record serialiser
// (Career_Serialise_DriverContractList) reads them:
//   +0x790 pendingDriverContracts   +0x7b0 decidedDriverContracts
//   +0x7d0 pendingDriverMoves       +0x7f0 lastSeasonsSignings
// Record: +0x00 performance clause name (loc string), +0x28 u8 clause stat (5 = none),
//   +0x2c i32 clause target, +0x34 i32 salary, +0x38 signingBonus, +0x3c teamPerfMoney,
//   +0x40 teamPerfRaceValue, +0x44 f32 successChance, +0x48 negotiationBonus, +0x4c u8 isCounteroffer,
//   +0x50 driver key, +0x54 old team key, +0x58 new team key, +0x5c u8 driverMoveReason,
//   +0x60 u32 weekendIndex, +0x64 u16 careerDayIndex, +0x68 u16 dayContractSent,
//   +0x6a u16 dayContractDecided, +0x6c u8 accepted, +0x6d u8 acknowledged.

constexpr uintptr_t kRecSize = sizeof(ContractRecord);
constexpr size_t kMaxRecords = 512;
constexpr uint8_t kNoClause = 5; // ContractRecord::clause_stat value for "no performance clause"

// A DriverStatId / team key as stored (u32): kind in the low byte, stat id in the high word.
uint32_t key_at(uintptr_t a)
{
    return rdv<uint32_t>(a) & 0xffff00ffu;
}

void record_json(ce::Writer &w, uintptr_t p)
{
    ContractRecord r{};
    rd(p, r);
    w.begin_object();
    w.key("driverStatId");
    w.integer(save_id(r.driver));
    w.key("oldTeamStatId");
    w.integer(r.old_team >> 16);
    w.key("newTeamStatId");
    w.integer(r.new_team >> 16);
    w.key("salary");
    w.integer(r.salary);
    w.key("signingBonus");
    w.integer(r.signing_bonus);
    w.key("teamPerfMoney");
    w.integer(r.team_perf_money);
    w.key("teamPerfRaceValue");
    w.integer(r.team_perf_race_value);
    w.key("successChance");
    w.num(r.success_chance);
    w.key("negotiationBonus");
    w.integer(r.negotiation_bonus);
    w.key("isCounteroffer");
    w.boolean(r.is_counteroffer != 0);
    w.key("reason");
    w.integer(r.driver_move_reason);
    w.key("weekendIndex");
    w.integer(r.weekend_index);
    w.key("careerDayIndex");
    w.integer(r.career_day_index);
    w.key("dayContractSent");
    w.integer(r.day_contract_sent);
    w.key("dayContractDecided");
    w.integer(r.day_contract_decided);
    w.key("accepted");
    w.boolean(r.accepted != 0);
    w.key("acknowledged");
    w.boolean(r.acknowledged != 0);
    w.key("clause");
    if (r.clause_stat != kNoClause) {
        w.begin_object();
        w.key("stat");
        w.integer(r.clause_stat);
        w.key("target");
        w.integer(r.clause_target);
        w.end_object();
    } else {
        w.null();
    }
    w.end_object();
}

void list_json(ce::Writer &w, const char *name, uintptr_t begin_field)
{
    w.key(name);
    w.begin_array();
    uintptr_t b;
    size_t n;
    if (vec(begin_field, kRecSize, kMaxRecords, b, n))
        for (size_t i = 0; i < n; ++i)
            record_json(w, b + i * kRecSize);
    w.end_array();
}

void contracts_json(ce::Writer &w)
{
    using C = DriverContractsMyTeamComponent;
    w.key("contracts");
    uintptr_t c = ce::comp::get(ce::comp::Id::DriverContractsMyTeam);
    if (!c || !swofty::mem::readable(c + offsetof(C, pending_contracts_begin),
                                  offsetof(C, last_season_signings_end) + 8 - offsetof(C, pending_contracts_begin))) {
        w.null();
        return;
    }
    w.begin_object();
    list_json(w, "pending", c + offsetof(C, pending_contracts_begin));
    list_json(w, "decided", c + offsetof(C, decided_contracts_begin));
    list_json(w, "pendingMoves", c + offsetof(C, pending_moves_begin));
    list_json(w, "lastSeasonSignings", c + offsetof(C, last_season_signings_begin));
    w.end_object();
}

// Experimental (My Team, experimentalFreeAgent): the free agent takes over the seat's contract records.
// My Team's processor (Career_Process_PendingDriverMoves, vtable +0x188, run at the season change)
// writes every entrant row teamless and seats only the drivers named in pendingDriverMoves; the game's
// own signings, the contract window (0x140daec40) and the forced signing (0x140dbd0a0), push records
// there and seat nobody mid-season. Every record of the outgoing driver for the seat's team in
// pendingDriverMoves, decidedDriverContracts and lastSeasonsSignings gets the free agent's driver key,
// so the records name the driver the rows seat. Pending negotiations are left alone.
size_t take_over_records(uintptr_t comp, uint32_t outgoing, uint32_t incoming, uint32_t team)
{
    using C = DriverContractsMyTeamComponent;
    size_t n = 0;
    for (uintptr_t list : {offsetof(C, pending_moves_begin), offsetof(C, decided_contracts_begin),
                           offsetof(C, last_season_signings_begin)}) {
        uintptr_t b;
        size_t m;
        if (!vec(comp + list, kRecSize, kMaxRecords, b, m))
            continue;
        for (size_t i = 0; i < m; ++i) {
            uintptr_t rec = b + i * kRecSize;
            if (key_at(rec + offsetof(ContractRecord, driver)) == (outgoing & 0xffff00ffu) &&
                key_at(rec + offsetof(ContractRecord, new_team)) == (team & 0xffff00ffu) &&
                wr(rec + offsetof(ContractRecord, driver), incoming))
                ++n;
        }
    }
    return n;
}

// ---------------------------------------------------------------------------
// Line-up: the championship entity's entrant rows (entity+0x2e0..+0x2e8, 0x88 bytes each).
//   +0x00 vtable, +0x08..+0x4b identity (+0x48 bit 0x10000000 = human), +0x50 driver key,
//   +0x54 driver data, +0x58 team key, +0x5c..+0x6b vehicle keys (+0x5e == 0x3fff: no car),
//   +0x78 seat value, +0x80 u8 position, +0x81 has-position, +0x83 driver flag.
// Career_Set_EntrantData_Thunk rewrites one row (found by its identity) and posts the entity's
// entrant-change messages; Career_Record_DriverMove appends driverMoveData to the renewal
// component (+0x3a8 vector, 0x18 stride) and posts the driver-move message.

constexpr uint32_t kNoKey = 0x3fff0000u;   // stat id 0x3fff in the high word: no driver/team/vehicle
constexpr uint32_t kHumanBit = 0x10000000u; // EntrantRecord::ordinal_flags bit of the human entrant
constexpr size_t kMaxEntrants = 256;
constexpr uintptr_t kEntrantSize = sizeof(EntrantRecord);
constexpr uintptr_t kMoveSize = sizeof(DriverMoveRecord);
constexpr uint8_t kReasonSigned = 0;  // DriverMoveRecord::reason the game's signing writes for the incoming driver
constexpr uint8_t kReasonBenched = 3; // ... and for a driver benched in his own team
static_assert(sizeof(EntrantRecord) == 0x88);

// The driver key as the game stores and compares it: the whole dword from id_lo (id_lo, pad, id_hi).
uint32_t entrant_driver(const EntrantRecord &r)
{
    uint32_t v;
    std::memcpy(&v, &r.id_lo, sizeof v);
    return v;
}

bool has_car(const EntrantRecord &r)
{
    return (r.vehicle_ref & 0xffff0000u) != kNoKey;
}

bool has_car(uintptr_t row)
{
    EntrantRecord r{};
    return rd(row, r) && has_car(r);
}

void seat_json(ce::Writer &w, const EntrantRecord &r)
{
    w.begin_object();
    w.key("hasCar");
    w.boolean(has_car(r));
    w.key("position");
    if (r.team_position >> 8)
        w.integer(r.team_position & 0xff);
    else
        w.null();
    w.end_object();
}

void seat_json(ce::Writer &w, uintptr_t row)
{
    EntrantRecord r{};
    if (rd(row, r))
        seat_json(w, r);
    else
        w.null();
}

constexpr uint8_t kNoNumber = 0xff; // EntrantRecord::car_number of a row without a number

void race_number_json(ce::Writer &w, uintptr_t row)
{
    EntrantRecord r{};
    if (rd(row, r) && r.car_number != kNoNumber)
        w.integer(r.car_number);
    else
        w.null();
}

void moves_json(ce::Writer &w)
{
    w.key("recordedMoves");
    uintptr_t r = ce::comp::get(ce::comp::Id::CareerDriverContractRenewal);
    if (!r) {
        w.null();
        return;
    }
    w.begin_array();
    uintptr_t b;
    size_t n;
    if (r && vec(r + offsetof(CareerDriverContractRenewalComponent, driver_moves_begin), kMoveSize, 4096, b, n)) {
        for (size_t i = n > 20 ? n - 20 : 0; i < n; ++i) {
            DriverMoveRecord m{};
            if (!rd(b + i * kMoveSize, m))
                continue;
            w.begin_object();
            w.key("driverStatId");
            w.integer(save_id(m.driver));
            w.key("oldTeamStatId");
            w.integer(m.old_team >> 16);
            w.key("newTeamStatId");
            w.integer(m.new_team >> 16);
            w.key("reason");
            w.integer(m.reason);
            w.key("weekendIndex");
            w.integer(m.weekend_index);
            w.end_object();
        }
    }
    w.end_array();
}

// ---------------------------------------------------------------------------
// Snapshot section "drivers".

bool dc_contracts(std::unordered_map<uint32_t, uintptr_t> &out);
void contract_json(ce::Writer &w, uintptr_t contract);

void section(ce::Writer &w)
{
    Comp c;
    const char *why = "no career loaded";
    bool ok = ce::in_career() && find_comp(c, &why);
    w.begin_object();
    w.key("available");
    w.boolean(ok);
    w.key("reason");
    w.str(ok ? "" : why);
    if (!ok) {
        w.end_object();
        return;
    }
    w.key("component");
    w.str(c.player_class ? "DriverCareerPlayerDriverStatisticsComponent" : "DriverStatisticsComponent");
    w.key("patch");
    w.begin_object();
    w.key("appliedChecksum");
    w.integer(rdv<uint32_t>(c.at + kCompApplied));
    w.key("declinedChecksum");
    w.integer(rdv<uint32_t>(c.at + kCompDeclined));
    w.key("usesPatchedCurves");
    w.boolean(patched_gate(c));
    w.end_object();

    w.key("stats");
    w.begin_array();
    for (auto &i : infos(c)) {
        w.begin_object();
        w.key("infoStatId");
        w.integer(i.id);
        w.key("name");
        w.str(stat_name(i.id));
        w.key("weight");
        w.num(i.weight);
        w.key("default");
        w.num(i.def);
        w.end_object();
    }
    w.end_array();

    std::unordered_map<uint32_t, ce::comp::Entrant> seat;
    for (auto &e : ce::comp::entrants())
        seat[e.driver] = e;
    const Names &nm = names();
    std::unordered_map<uint32_t, uintptr_t> contract;
    bool dc = dc_contracts(contract);

    auto name_or_null = [&w](const std::unordered_map<uint32_t, std::string> &m, uint32_t key) {
        auto it = (key & 0xff) == 0 ? m.find(key >> 16) : m.end();
        if (it != m.end() && !it->second.empty() && it->second.size() < kNameMax)
            w.str(it->second);
        else
            w.null();
    };
    std::vector<InfoRow> stat_rows = infos(c);
    std::vector<uintptr_t> others;

    w.key("drivers");
    w.begin_array();
    uintptr_t b;
    size_t n;
    if (vec(c.at + kCompDrivers, kDriverSize, kMaxDrivers, b, n)) {
        for (size_t i = 0; i < n; ++i) {
            uintptr_t d = b + i * kDriverSize;
            uint32_t key = key_at(d + offsetof(DriverStatsEntry, kind));
            if ((key & 0xffff0000u) == kNoKey)
                continue;
            auto s = seat.find(key);
            if (s == seat.end()) {
                others.push_back(d);
                continue;
            }
            w.begin_object();
            w.key("driverStatId");
            w.integer(save_id(key));
            w.key("name");
            name_or_null(nm.driver, key);
            w.key("initials");
            name_or_null(nm.initials, key);
            auto num = (key & 0xff) == 0 ? nm.number.find(key >> 16) : nm.number.end();
            w.key("number");
            if (num != nm.number.end())
                w.integer(num->second);
            else
                w.null();
            w.key("raceNumber");
            race_number_json(w, s->second.at);
            uint32_t team = s->second.team;
            w.key("team");
            if ((team & 0xffff0000u) != kNoKey) {
                w.begin_object();
                w.key("teamStatId");
                w.integer(team >> 16);
                w.key("name");
                name_or_null(nm.team, team & 0xffff0000u);
                w.end_object();
            } else {
                w.null();
            }
            w.key("seat");
            seat_json(w, s->second.at);
            w.key("onGrid");
            w.boolean(has_car(s->second.at));
            w.key("player");
            w.boolean(s->second.human);
            if (dc) {
                auto k = contract.find(key);
                w.key("contract");
                contract_json(w, k == contract.end() ? 0 : k->second);
            }
            w.key("overall");
            float o = overall(c, key);
            if (std::isfinite(o))
                w.integer(static_cast<int64_t>(o));
            else
                w.null();
            ratings_json(w, c, key, d, stat_ids(d), false);
            w.end_object();
        }
    }
    w.end_array();

    w.key("otherDrivers");
    w.begin_array();
    for (uintptr_t d : others) {
        uint32_t key = key_at(d + offsetof(DriverStatsEntry, kind));
        w.begin_object();
        w.key("driverStatId");
        w.integer(save_id(key));
        w.key("name");
        name_or_null(nm.driver, key);
        w.key("initials");
        name_or_null(nm.initials, key);
        w.key("overall");
        float o = overall(c, key);
        if (std::isfinite(o))
            w.integer(static_cast<int64_t>(o));
        else
            w.null();
        w.key("values");
        w.begin_array();
        for (auto &info : stat_rows) {
            float v = find_stat(d + kDriverSetStats, info.id) ? stat_value(c, key, info.id, true, true) : NAN;
            if (std::isfinite(v))
                w.integer(static_cast<int64_t>(v));
            else
                w.null();
        }
        w.end_array();
        w.end_object();
    }
    w.end_array();
    w.key("progress");
    uint32_t first = n ? key_at(b + offsetof(DriverStatsEntry, kind)) : 0;
    w.num(n ? std::round(progress(c, first) * 10000.0) / 10000.0 : 0.0);
    contracts_json(w);
    moves_json(w);
    w.key("pendingSwaps");
    w.begin_array();
    w.end_array();
    w.end_object();
}

// ---------------------------------------------------------------------------
// Op drivers.ratings.set

struct Edit {
    uint16_t info;
    double target;
    bool has_expect;
    double expect;
};

ce::OpResult fail(const char *err, const std::string &detail = {})
{
    ce::OpResult r;
    r.ok = false;
    r.error = detail.empty() ? err : std::string(err) + ": " + detail;
    ce::log("drivers.ratings.set FAILED %s", r.error.c_str());
    return r;
}

std::string result_json(const Comp &c, uint32_t key, uintptr_t driver, const std::vector<uint16_t> &which)
{
    ce::Writer w;
    w.begin_object();
    w.key("driverStatId");
    w.integer(save_id(key));
    ratings_json(w, c, key, driver, which, true);
    w.key("overall");
    float o = overall(c, key);
    if (std::isfinite(o))
        w.integer(static_cast<int64_t>(o));
    else
        w.null();
    w.end_object();
    return w.text();
}

struct Neighbour {
    uint32_t key;
    uint16_t info;
    float value;
};

std::vector<Neighbour> neighbours(const Comp &c, uint32_t except, const std::vector<uint16_t> &which)
{
    std::vector<Neighbour> out;
    uintptr_t b;
    size_t n;
    if (!vec(c.at + kCompDrivers, kDriverSize, kMaxDrivers, b, n))
        return out;
    for (size_t i = 0; i < n; ++i) {
        uintptr_t d = b + i * kDriverSize;
        uint32_t key = key_at(d + offsetof(DriverStatsEntry, kind));
        if (key == except)
            continue;
        for (uint16_t info : which)
            out.push_back({key, info, stat_value(c, key, info, false, false)});
    }
    return out;
}

struct Undo {
    uintptr_t curve;
    uintptr_t stat;
    std::vector<float> ys;
    float level;
    bool curve_changed;
};

void roll_back(const std::vector<Undo> &log)
{
    for (auto it = log.rbegin(); it != log.rend(); ++it) {
        if (it->curve_changed) {
            CurveView v;
            if (curve_view(it->curve, v))
                curve_write_y(it->curve, v, it->ys);
        } else {
            wr(it->stat + kStatLevel, it->level);
        }
    }
}

std::string reach_error(uint16_t info, const ce::rating::Solve &s, float target, bool own)
{
    std::string who = own ? "your own " + stat_name(info) : stat_name(info);
    const char *when = own ? " right now" : " for this driver";
    auto n = [](float v) { return std::to_string(std::lround(std::clamp(v, 0.0f, 100.0f))); };
    if (target < s.low)
        return "out_of_range: " + who + " can't go below " + n(s.low) + when + ", pick " + n(s.low) + " or higher";
    if (target > s.high)
        return "out_of_range: " + who + " can't go above " + n(s.high) + when + ", pick " + n(s.high) + " or lower";
    return "write_failed: " + stat_name(info) + " reads " + std::to_string(s.value) + " after the write, wanted " +
           std::to_string(target);
}

bool apply_one(const Comp &c, uint32_t key, uintptr_t driver, const Edit &e, const std::string &mode, bool own,
               std::vector<Undo> &log, std::string &how, std::string &why)
{
    uint8_t ctx[16];
    context(c, ctx);
    float x = progress(c, key);
    uintptr_t active = active_set(driver, ctx);
    uintptr_t ast = find_stat(active, e.info);
    if (!ast)
        return false;
    float raw = stat_value(c, key, e.info, false, false);
    if (!std::isfinite(raw))
        return false;
    float target = static_cast<float>(e.target);
    bool dyn = rdv<uint8_t>(ast + kStatDynamic) != 0;
    std::vector<uintptr_t> sets = sets_of(driver);

    if (dyn) {
        struct Level {
            uintptr_t st;
            float old;
        };
        std::vector<Level> levels;
        float active_old = rdv<float>(ast + kStatLevel);
        for (uintptr_t set : sets) {
            uintptr_t st = find_stat(set, e.info);
            if (!st || !rdv<uint8_t>(st + kStatDynamic))
                continue;
            float old = rdv<float>(st + kStatLevel);
            log.push_back({0, st, {}, old, false});
            levels.push_back({st, old});
        }
        if (levels.empty())
            return false;
        auto put = [&](float delta) {
            for (auto &l : levels)
                if (!wr(l.st + kStatLevel, l.old + delta))
                    return NAN;
            return stat_value(c, key, e.info, false, false);
        };
        float delta = target - raw;
        float got = put(delta);
        if (std::isnan(got))
            return false;
        bool searched = false;
        if (std::fabs(got - target) > kNear) {
            auto s = ce::rating::solve(-active_old, kLevelTop - active_old, target, put);
            if (!s.reached) {
                why = reach_error(e.info, s, target, own);
                return false;
            }
            delta = s.p;
            if (std::isnan(put(delta)))
                return false;
            searched = true;
        }
        how = "dynamicLevel " + std::to_string(delta) + " in " + std::to_string(levels.size()) + " set(s)" +
              (searched ? ", searched" : "");
        return true;
    }

    uintptr_t acurve = used_curve(c, ast);
    CurveView av;
    if (!curve_view(acurve, av))
        return false;
    float cur_base = curve_at(acurve, av, x);
    float others = raw - cur_base;
    float target_base = target - others;
    float target_norm = std::clamp((target_base - av.miny) / (av.maxy - av.miny), 0.0f, 1.0f);
    float shift_norm = target_norm - eval_curve(acurve, x);
    bool patched = acurve == ast + kStatPatched;
    bool shift = mode == "shift";

    struct Curve {
        size_t set;
        uintptr_t at;
        CurveView v;
        std::vector<float> old;
        std::vector<float> saved;
        bool restored = false;
    };
    std::vector<Curve> curves;
    for (size_t si = 0; si < sets.size(); ++si) {
        uintptr_t st = find_stat(sets[si], e.info);
        if (!st || rdv<uint8_t>(st + kStatDynamic))
            continue;
        Curve cv{si, patched && rdv<uint8_t>(st + kStatHasPatched) ? st + kStatPatched : st + kStatCurve, {}, {}, {}};
        if (!curve_view(cv.at, cv.v) || !curve_read_y(cv.v, cv.old))
            continue;
        if (mode == "restore") {
            for (auto it = g_undo.rbegin(); it != g_undo.rend(); ++it) {
                if (it->key == key && it->info == e.info && it->set == si && it->patched == patched &&
                    it->ys.size() == cv.old.size()) {
                    cv.saved = it->ys;
                    cv.restored = true;
                    g_undo.erase(std::next(it).base());
                    break;
                }
            }
        }
        log.push_back({cv.at, st, cv.old, 0.0f, true});
        curves.push_back(std::move(cv));
    }
    if (curves.empty())
        return false;

    auto put = [&](float p) {
        for (auto &cv : curves) {
            std::vector<float> ys = cv.restored ? cv.saved : cv.old;
            if (!cv.restored)
                for (float &y : ys)
                    y = shift ? std::clamp(y + p, 0.0f, 1.0f) : p;
            if (!curve_write_y(cv.at, cv.v, ys))
                return NAN;
        }
        return stat_value(c, key, e.info, false, false);
    };
    float p = shift ? shift_norm : target_norm;
    float got = put(p);
    if (std::isnan(got))
        return false;
    bool searched = false;
    if (mode != "restore" && std::fabs(got - target) > kNear) {
        auto s = ce::rating::solve(shift ? -1.0f : 0.0f, 1.0f, target, put);
        if (!s.reached) {
            why = reach_error(e.info, s, target, own);
            return false;
        }
        p = s.p;
        if (std::isnan(put(p)))
            return false;
        searched = true;
    }
    for (auto &cv : curves)
        remember({key, e.info, cv.set, patched, cv.old, 0.0f});
    how = std::string(patched ? "patched" : "base") + " curve " + (mode.empty() ? "flat" : mode) + " in " +
          std::to_string(curves.size()) + " set(s)" + (searched ? ", searched" : "");
    return true;
}

ce::OpResult op_ratings_set(const ce::Json &args)
{
    Comp c;
    const char *why = "";
    if (!find_comp(c, &why))
        return fail("no_component", why);
    const ce::Json &id = args["driverStatId"];
    if (id.type != ce::Json::Num || id.n < 0 || id.n > 0xffff || id.n != std::floor(id.n))
        return fail("bad_args", "driverStatId must be an integer 0..65535");
    uint32_t key = key_from_save(static_cast<uint32_t>(id.n));
    uintptr_t driver = find_driver(c, key);
    if (!driver)
        return fail("unknown_driver", std::to_string(static_cast<long long>(id.n)));

    std::string mode = args["mode"].str("flat");
    if (mode != "flat" && mode != "shift" && mode != "restore")
        return fail("bad_args", "mode must be flat, shift or restore");

    const ce::Json &rat = args["ratings"];
    if (rat.type != ce::Json::Obj || rat.o.empty())
        return fail("bad_args", "ratings must be a non-empty object");
    auto bound_min = slot<BoundFn>(c, kVtMin);
    auto bound_max = slot<BoundFn>(c, kVtMax);
    float lo = bound_min ? bound_min(reinterpret_cast<void *>(c.at)) : 0.0f;
    float hi = bound_max ? bound_max(reinterpret_cast<void *>(c.at)) : 100.0f;
    if (!std::isfinite(lo) || !std::isfinite(hi) || hi <= lo)
        lo = 0.0f, hi = 100.0f;

    std::vector<Edit> edits;
    std::vector<uint16_t> which;
    for (auto &[k, v] : rat.o) {
        uint16_t info;
        if (!resolve_stat(driver, k, info))
            return fail("unknown_stat", k);
        if (v.type != ce::Json::Num || !std::isfinite(v.n) || v.n < 0 || v.n > 100)
            return fail("bad_value", k + " must be a number 0..100");
        Edit e{info, std::clamp(v.n, static_cast<double>(lo), static_cast<double>(hi)), false, 0};
        const ce::Json &ex = args["expect"][k.c_str()];
        if (ex.type == ce::Json::Num) {
            e.has_expect = true;
            e.expect = ex.n;
        }
        edits.push_back(e);
        which.push_back(info);
    }
    for (auto &e : edits) {
        if (!e.has_expect)
            continue;
        float live = stat_value(c, key, e.info, true, true);
        if (!std::isfinite(live) || std::fabs(live - e.expect) > 0.5)
            return fail("stale", stat_name(e.info) + " is " + std::to_string(live) + ", expected " +
                                     std::to_string(e.expect));
    }

    ce::OpResult r;
    r.before = result_json(c, key, driver, which);
    std::vector<Neighbour> nb = neighbours(c, key, which);
    std::vector<float> before_vals;
    for (auto &e : edits)
        before_vals.push_back(stat_value(c, key, e.info, true, true));
    float before_overall = overall(c, key);

    ce::comp::Entrant me;
    bool own = ce::comp::player(me) && me.driver == key;
    std::vector<Undo> log;
    std::string how_all;
    for (auto &e : edits) {
        std::string how, why;
        if (!apply_one(c, key, driver, e, mode, own, log, how, why)) {
            roll_back(log);
            if (!why.empty())
                return fail(why.c_str());
            return fail("write_failed", stat_name(e.info) + " (curve empty or memory not writable)");
        }
        how_all += (how_all.empty() ? "" : "; ") + stat_name(e.info) + ": " + how;
    }

    for (auto &n : nb) {
        float now = stat_value(c, n.key, n.info, false, false);
        if (!(std::fabs(now - n.value) < 1e-3f || (std::isnan(now) && std::isnan(n.value)))) {
            roll_back(log);
            return fail("write_failed", "driver " + std::to_string(save_id(n.key)) + " " + stat_name(n.info) +
                                            " moved; edit rolled back");
        }
    }
    std::string detail;
    for (size_t i = 0; i < edits.size(); ++i) {
        float after = stat_value(c, key, edits[i].info, true, true);
        if (mode != "restore" && (!std::isfinite(after) || std::fabs(after - std::round(edits[i].target)) > 1.0f)) {
            roll_back(log);
            return fail("write_failed", stat_name(edits[i].info) + " reads " + std::to_string(after) +
                                            " after the write, wanted " + std::to_string(edits[i].target));
        }
        char buf[128];
        std::snprintf(buf, sizeof buf, "%s%s %.0f -> %.0f", detail.empty() ? "" : ", ", stat_name(edits[i].info).c_str(),
                      before_vals[i], after);
        detail += buf;
    }
    r.after = result_json(c, key, driver, which);
    r.ok = true;
    ce::log("drivers.ratings.set driver %u (key 0x%x, %s) %s; overall %.0f -> %.0f (%s, mode %s, "
            "%zu neighbour reads unchanged)",
            save_id(key), key, c.player_class ? "DriverCareerPlayerDriverStatistics" : "DriverStatistics",
            detail.c_str(), before_overall, overall(c, key), how_all.c_str(), mode.c_str(), nb.size());
    return r;
}

ce::OpResult op_fail(const char *op, const char *err, const std::string &detail)
{
    ce::OpResult r;
    r.ok = false;
    r.error = detail.empty() ? err : std::string(err) + ": " + detail;
    ce::log("%s FAILED %s", op, r.error.c_str());
    return r;
}

// ---------------------------------------------------------------------------
// Op drivers.swap

struct Row {
    uintptr_t at = 0;
    EntrantRecord rec{};
};

// The bytes of an EntrantRecord before its driver id (vtable, name block, ordinal_flags) are the
// identity Career_Set_EntrantData_Thunk matches rows by.
constexpr size_t kIdentitySize = offsetof(EntrantRecord, id_lo);

bool entrant_rows(uintptr_t entity, std::vector<Row> &out)
{
    uintptr_t b;
    size_t n;
    out.clear();
    if (!entity || !vec(entity + offsetof(CareerEntity, entrants_begin), kEntrantSize, kMaxEntrants, b, n) || !n)
        return false;
    out.resize(n);
    for (size_t i = 0; i < n; ++i) {
        out[i].at = b + i * kEntrantSize;
        if (!rd(out[i].at, out[i].rec))
            return false;
    }
    return true;
}

const Row *row_of(const std::vector<Row> &rows, uint32_t driver)
{
    for (auto &r : rows)
        if (entrant_driver(r.rec) == driver)
            return &r;
    return nullptr;
}

// The data block Career_Set_EntrantData_Thunk takes, laid out the way Career_Sign_DriverToTeam
// builds it: driver fields from the driver's row, team and seat fields from the seat's row.
EntrantWriteData entrant_data(const Row &driver_row, const Row &seat_row)
{
    EntrantWriteData d{};
    d.driver = entrant_driver(driver_row.rec);
    d.driver_customisation = driver_row.rec.driver_customisation;
    d.team_stat_id = seat_row.rec.team_stat_id;
    d.vehicle_ref = seat_row.rec.vehicle_ref;
    d.driver_reg_value = seat_row.rec.driver_reg_value;
    d.vehicle_customisation = seat_row.rec.vehicle_customisation;
    d.badge_stat_id = seat_row.rec.badge_stat_id;
    d.seat_value = seat_row.rec.const_ffff0012;
    return d;
}

bool same_identity(const EntrantRecord &x, const EntrantRecord &y)
{
    return std::memcmp(reinterpret_cast<const uint8_t *>(&x) + sizeof(void *),
                       reinterpret_cast<const uint8_t *>(&y) + sizeof(void *), kIdentitySize - sizeof(void *)) == 0;
}

void seat_text(char *buf, size_t n, const Row &r)
{
    std::snprintf(buf, n, "team %u car 0x%x pos %d", r.rec.team_stat_id >> 16, r.rec.vehicle_ref,
                  (r.rec.team_position >> 8) ? (r.rec.team_position & 0xff) : -1);
}

std::string swap_json(const std::vector<Row> &rows, uint32_t a, uint32_t b)
{
    ce::Writer w;
    w.begin_object();
    for (uint32_t k : {a, b}) {
        const Row *r = row_of(rows, k);
        w.key(k == a ? "a" : "b");
        w.begin_object();
        w.key("driverStatId");
        w.integer(save_id(k));
        if (r) {
            w.key("teamStatId");
            if ((r->rec.team_stat_id & 0xffff0000u) != kNoKey)
                w.integer(r->rec.team_stat_id >> 16);
            else
                w.null();
            w.key("seat");
            seat_json(w, r->rec);
            w.key("raceNumber");
            if (r->rec.car_number != kNoNumber)
                w.integer(r->rec.car_number);
            else
                w.null();
        }
        w.end_object();
    }
    w.key("appliedNow");
    w.boolean(true);
    w.end_object();
    return w.text();
}

bool same_row(const Row &x)
{
    EntrantRecord live{};
    return rd(x.at, live) && std::memcmp(&live, &x.rec, kEntrantSize) == 0;
}

// Career_Set_EntrantData_Thunk posts its begin message before it searches, and only posts the end
// message (clearing CareerEntity::entrant_update_busy) when a row matched. A miss would leave the
// game mid entrant update, so the row must match first: the game's compare (0x1431146d0) requires
// the same human bit and, for AI rows, the same ordinal_flags. Exactly one row of the live table may
// match, and it must be the row being written.
bool unique_identity(uintptr_t entity, const Row &id_row)
{
    std::vector<Row> rows;
    if (!entrant_rows(entity, rows))
        return false;
    uint32_t id = id_row.rec.ordinal_flags;
    if (id & kHumanBit)
        return false;
    size_t hits = 0;
    uintptr_t at = 0;
    for (auto &r : rows) {
        uint32_t v = r.rec.ordinal_flags;
        if (!(v & kHumanBit) && v == id) {
            ++hits;
            at = r.at;
        }
    }
    return hits == 1 && at == id_row.at;
}

bool entity_busy(uintptr_t entity)
{
    return rdv<uint8_t>(entity + offsetof(CareerEntity, entrant_update_busy)) != 0;
}

// How the game announces a move. With a renewal component: Career_Record_DriverMove on it (it
// appends driverMoveData and posts MessageDriverMoveConfirmed). Driver Career has no renewal
// component and no move log: its signing (0x140d766b0, DriverCareerContractsManagerComponent) posts
// MessageDriverMoveConfirmed itself, built from its weekend source and career clock, right before
// each Career_Set_EntrantData_Thunk. The Upgrades component listens to it to reassign per-driver
// upgrade state. My Team careers have shown no renewal component either (every lookup in the
// registry the game's own renewal lookups use returned null, in both modes): there the moves
// follow My Team's processor, DriverContractsMyTeamComponent vtable slot +0x188
// (Career_Process_PendingDriverMoves 0x140db18e0, the slot the Driver Career signing fills in its
// own component). It posts no per-move message; its moves reach the listeners through the finalise
// messages alone, built against the line-up it copied before its first write.
struct MoveSink {
    uintptr_t renewal = 0; // CareerDriverContractRenewalComponent (My Team, when the career has one)
    uintptr_t manager = 0; // DriverCareerContractsManagerComponent (Driver Career)
    uintptr_t my_team = 0; // DriverContractsMyTeamComponent (My Team without a renewal component)
};

constexpr uintptr_t kVtWeekendIndex = 0x108; // vtable slot of the weekend source returning the weekend index
constexpr uint32_t kMinutesPerDay = 1440;

using WeekendFn = uint32_t (*)(void *self);

bool announce(const MoveSink &sink, uint32_t driver, uint32_t old_team, uint32_t new_team, uint8_t reason)
{
    if (sink.renewal) {
        Career_Record_DriverMove(reinterpret_cast<void *>(sink.renewal), driver, old_team, new_team, reason);
        return true;
    }
    if (sink.my_team)
        return true;
    using M = DriverCareerContractsManagerComponent;
    uintptr_t src = rdv<uintptr_t>(sink.manager + offsetof(M, weekend_source));
    uintptr_t time = rdv<uintptr_t>(sink.manager + offsetof(M, career_time));
    uintptr_t vt = src ? rdv<uintptr_t>(src) : 0;
    uintptr_t fn = vt ? rdv<uintptr_t>(vt + kVtWeekendIndex) : 0;
    if (!swofty::mem::in_image(fn) || !time || !vt_Career_MessageDriverMoveConfirmed.address())
        return false;
    DriverMoveConfirmedMessage m{};
    m.vtable = vt_Career_MessageDriverMoveConfirmed.get();
    m.move.driver = driver;
    m.move.old_team = old_team;
    m.move.new_team = new_team;
    m.move.reason = reason;
    m.move.weekend_index = reinterpret_cast<WeekendFn>(fn)(reinterpret_cast<void *>(src));
    m.move.day_index =
        static_cast<uint16_t>(rdv<uint32_t>(time + offsetof(CareerTimeComponent, current_minutes)) / kMinutesPerDay);
    return swofty::career::post(&m) == swofty::career::Result::ok;
}

bool apply_row(uintptr_t entity, const MoveSink &sink, const Row &id_row, uint32_t old_team, uint32_t new_team,
               uint8_t reason, const EntrantWriteData &data, uint16_t position)
{
    if (entity_busy(entity) || !unique_identity(entity, id_row))
        return false;
    uint8_t id[kIdentitySize];
    std::memcpy(id, &id_row.rec, kIdentitySize);
    if (!announce(sink, data.driver, old_team, new_team, reason))
        return false;
    Career_Set_EntrantData_Thunk(reinterpret_cast<void *>(entity), id, reinterpret_cast<const uint32_t *>(&data),
                                 position, id_row.rec.car_number);
    if (entity_busy(entity))
        ce::log("drivers.swap WARNING entity entrant-update flag still set after the write");
    return true;
}

bool teamless(const EntrantRecord &r)
{
    return (r.team_stat_id & 0xffff0000u) == kNoKey;
}

std::vector<ce::numbers::Row> number_rows(const std::vector<Row> &rows)
{
    std::vector<ce::numbers::Row> out;
    for (auto &x : rows)
        out.push_back({entrant_driver(x.rec), x.rec.car_number, !teamless(x.rec)});
    return out;
}

// Driver Career: release the outgoing driver's contract the way its signing does (state 3, the
// day, and the value the entity's component registry returns from vtable slot +0x108).
constexpr uintptr_t kVtRegistryValue = 0x108; // component registry vtable slot (not a field)
constexpr uint8_t kContractReleased = 3;     // Career_Set_DriverContractState state the replace path writes
constexpr uint8_t kReasonReleased = 1;       // MessageDriverMoveConfirmed reason for a driver taken off the grid
constexpr uint32_t kNoSeatValue = 0xffff0012u; // EntrantRecord::const_ffff0012 of a row without a seat
constexpr size_t kContractSlot = 16;          // DriverCareerContractOwner slots {?, DriverContract *}

using RegistryValueFn = uint32_t (*)(void *registry);

// Every driver's DriverContract object, from DriverCareerContractOwner's slots.
bool contract_objects(const MoveSink &sink, std::vector<uintptr_t> &out)
{
    out.clear();
    uintptr_t owner =
        sink.manager ? rdv<uintptr_t>(sink.manager + offsetof(DriverCareerContractsManagerComponent, contract_owner)) : 0;
    uintptr_t b;
    size_t n;
    if (!owner || !vec(owner + offsetof(DriverCareerContractOwner, contracts_begin), kContractSlot, 4096, b, n))
        return false;
    for (size_t i = 0; i < n; ++i)
        if (uintptr_t c = rdv<uintptr_t>(b + i * kContractSlot + sizeof(void *));
            c && swofty::mem::readable(c, sizeof(DriverContract)))
            out.push_back(c);
    return true;
}

uintptr_t contract_of(const std::vector<uintptr_t> &objects, uint32_t driver)
{
    for (uintptr_t c : objects)
        if (rdv<uint32_t>(c + offsetof(DriverContract, driver)) == driver)
            return c;
    return 0;
}

std::vector<ce::lineup::Contract> contract_views(const std::vector<uintptr_t> &objects)
{
    ce::comp::Entrant me;
    uint32_t human = ce::comp::player(me) ? me.driver : kNoKey;
    std::vector<ce::lineup::Contract> out;
    for (uintptr_t c : objects) {
        uint32_t d = rdv<uint32_t>(c + offsetof(DriverContract, driver));
        out.push_back({d, rdv<uint32_t>(c + offsetof(DriverContract, team)),
                       rdv<uint8_t>(c + offsetof(DriverContract, active)) != 0, d == human});
    }
    return out;
}

// The day and value the signing passes to Career_Set_DriverContractState.
bool contract_clock(uintptr_t entity, const MoveSink &sink, uint16_t &day, uint32_t &value)
{
    uintptr_t time = rdv<uintptr_t>(sink.manager + offsetof(DriverCareerContractsManagerComponent, career_time));
    uintptr_t registry = entity + offsetof(CareerEntity, component_registry);
    uintptr_t vt = rdv<uintptr_t>(registry);
    uintptr_t fn = vt ? rdv<uintptr_t>(vt + kVtRegistryValue) : 0;
    if (!time || !swofty::mem::in_image(fn))
        return false;
    value = reinterpret_cast<RegistryValueFn>(fn)(reinterpret_cast<void *>(registry));
    day = static_cast<uint16_t>(rdv<uint32_t>(time + offsetof(CareerTimeComponent, current_minutes)) / kMinutesPerDay);
    return true;
}

bool release_contract(uintptr_t entity, const MoveSink &sink, uint32_t driver, bool &found)
{
    found = false;
    std::vector<uintptr_t> objects;
    uint16_t day;
    uint32_t value;
    if (!contract_objects(sink, objects) || !contract_clock(entity, sink, day, value))
        return false;
    uintptr_t contract = contract_of(objects, driver);
    if (!contract)
        return true;
    Career_Set_DriverContractState(reinterpret_cast<void *>(contract), day, kContractReleased, value);
    found = true;
    return true;
}

bool dc_contracts(std::unordered_map<uint32_t, uintptr_t> &out)
{
    out.clear();
    MoveSink sink;
    sink.manager = ce::mode() == ce::Mode::DriverCareer ? ce::comp::get(ce::comp::Id::DriverCareerContractsManager) : 0;
    std::vector<uintptr_t> objects;
    if (!contract_objects(sink, objects))
        return false;
    for (uintptr_t c : objects)
        out[rdv<uint32_t>(c + offsetof(DriverContract, driver))] = c;
    return true;
}

void contract_json(ce::Writer &w, uintptr_t contract)
{
    if (!contract) {
        w.null();
        return;
    }
    uint32_t team = rdv<uint32_t>(contract + offsetof(DriverContract, team));
    w.begin_object();
    w.key("teamStatId");
    if ((team & 0xffff0000u) != kNoKey)
        w.integer(team >> 16);
    else
        w.null();
    w.key("active");
    w.boolean(rdv<uint8_t>(contract + offsetof(DriverContract, active)) != 0);
    w.end_object();
}

// The add-contract step of the Driver Career signing (0x140d77a70..0x140d77cbb). The offer gets the
// signing day, the incoming driver's own contract ends (state 4 for the same team, 5 for another), and
// when that leaves it inactive the offer is copied in, marked active and announced with
// MessageDriverContractChanged {driver, record before, record after, offer kind}. The editor's offer is
// the record of an active contract for the seat's team (lineup::terms_for).
constexpr size_t kRecordSize = offsetof(DriverContract, active); // the contract record before the active byte
constexpr uint8_t kContractRenewed = 4;
constexpr uint8_t kContractLeft = 5;
static_assert(kRecordSize == 0x2c && sizeof(DriverContractChangedMessage) == 0x78);

struct Signing {
    uint32_t driver = 0;
    uintptr_t contract = 0;
    uint8_t offer[kRecordSize] = {};
};

bool sign_contract(uintptr_t entity, const MoveSink &sink, const Signing &s)
{
    using C = DriverContract;
    uint16_t day;
    uint32_t value;
    if (!contract_clock(entity, sink, day, value) || !writable(s.contract, offsetof(C, active) + 1) ||
        !vt_Career_MessageDriverContractChanged.address())
        return false;
    uint8_t offer[kRecordSize];
    std::memcpy(offer, s.offer, sizeof offer);
    std::memcpy(offer + offsetof(C, signed_day), &day, sizeof day);
    uint32_t team;
    std::memcpy(&team, offer + offsetof(C, team), sizeof team);
    bool same = ((rdv<uint32_t>(s.contract + offsetof(C, team)) ^ team) & 0xffff00ffu) == 0;
    Career_Set_DriverContractState(reinterpret_cast<void *>(s.contract), day, same ? kContractRenewed : kContractLeft,
                                   value);
    if (rdv<uint8_t>(s.contract + offsetof(C, active)))
        return false;
    DriverContractChangedMessage m{};
    m.vtable = vt_Career_MessageDriverContractChanged.get();
    m.driver = rdv<uint32_t>(s.contract + offsetof(C, driver));
    std::memcpy(m.before, reinterpret_cast<const void *>(s.contract), sizeof m.before);
    std::memcpy(reinterpret_cast<void *>(s.contract), offer, sizeof offer);
    wr<uint8_t>(s.contract + offsetof(C, active), 1);
    std::memcpy(m.after, offer, sizeof m.after);
    m.kind = offer[offsetof(C, kind)];
    return swofty::career::post(&m) == swofty::career::Result::ok;
}

// Plans a signing before anything is written: the driver's own contract object and the record he
// signs for `team`.
const char *plan_signing(const std::vector<uintptr_t> &objects, uint32_t driver, uint32_t outgoing, uint32_t team,
                         Signing &s)
{
    s.driver = driver;
    s.contract = contract_of(objects, driver);
    if (!s.contract)
        return "the game keeps no contract for him";
    if (!writable(s.contract, offsetof(DriverContract, active) + 1))
        return "his contract is not writable";
    int t = ce::lineup::terms_for(contract_views(objects), outgoing, team);
    if (t < 0 || !rd(objects[t], s.offer))
        return "no driver holds an active contract with that team to copy the terms from";
    return nullptr;
}

std::vector<ce::lineup::Seat> seats_of(const std::vector<Row> &rows)
{
    std::vector<ce::lineup::Seat> out;
    for (auto &r : rows)
        out.push_back({entrant_driver(r.rec), r.rec.team_stat_id, r.rec.vehicle_ref});
    return out;
}

// The end of every signing batch: the changed drivers between the current line-up and a baseline
// (Career_Build_LineupChanges), posted as MessageDriverMovesFinalised_upgrades, _recognition, then
// MessageDriverMovesFinalised. The _upgrades handler creates the team modifiers a newly seated
// driver needs (the null-modifier crash of a bare swap) and moves the per-driver state. The baseline
// is the line-up from before the op's first write, as My Team's processor copies it (0x140db2f40,
// compared at 0x140db4e1d). The Driver Career signing compares with the weekend's line-up instead,
// which equals that copy only for the first signing batch of a weekend: against it a second edit in
// the same weekend announces the first again, and a swap back announces nothing.

// An entrant vector object as Career_Build_LineupChanges reads it (begin at +8, end at +0x10).
struct EntrantVector {
    void *allocator = nullptr;
    const EntrantRecord *begin = nullptr;
    const EntrantRecord *end = nullptr;
    const EntrantRecord *capacity = nullptr;
};

bool finalise_moves(uintptr_t entity, uintptr_t baseline, uint64_t &changes)
{
    changes = 0;
    if (!baseline || !Career_Build_LineupChanges.address())
        return false;
    // The entrant vector object {allocator, begin, end} starts one pointer before entrants_begin.
    uintptr_t current = entity + offsetof(CareerEntity, entrants_begin) - sizeof(void *);
    DriverMovesFinalisedMessage m{};
    Career_Build_LineupChanges(&m.changes, reinterpret_cast<void *>(current), reinterpret_cast<void *>(baseline));
    if (m.count > 40)
        return false;
    changes = m.count;
    void **vts[] = {vt_Career_MessageDriverMovesFinalisedUpgrades.get(),
                    vt_Career_MessageDriverMovesFinalisedRecognition.get(),
                    vt_Career_MessageDriverMovesFinalised.get()};
    for (void **v : vts) {
        DriverMovesFinalisedMessage copy = m;
        copy.vtable = v;
        copy.reserved = 0;
        if (swofty::career::post(&copy) != swofty::career::Result::ok)
            return false;
    }
    return true;
}

EntrantWriteData released_data(const Row &r)
{
    EntrantWriteData d{};
    d.driver = entrant_driver(r.rec);
    d.driver_customisation = r.rec.driver_customisation;
    d.team_stat_id = kNoKey;
    d.vehicle_ref = kNoKey;
    d.driver_reg_value = kNoKey;
    d.vehicle_customisation = kNoKey;
    d.badge_stat_id = kNoKey;
    d.seat_value = kNoSeatValue;
    return d;
}

bool row_matches(const EntrantRecord &now, const EntrantRecord &was, const EntrantWriteData &d)
{
    return now.team_stat_id == d.team_stat_id && now.vehicle_ref == d.vehicle_ref &&
           now.driver_reg_value == d.driver_reg_value && now.vehicle_customisation == d.vehicle_customisation &&
           now.badge_stat_id == d.badge_stat_id && entrant_driver(now) == d.driver && same_identity(now, was);
}

// Racing numbers after a swap. The game's signings give a row that has a team and no number one with
// Career_DriverNumbers_CheckExisting(CarNumberComponent, driver) after its entrant write: the Driver
// Career signing after the seat contract (0x140d7814e, the component from manager +0x368), My Team's
// Career_Assign_EntrantTeamSlot (0x140db7418, from DriverContractsMyTeamComponent +0x2e8). It draws
// a free number from the component's candidates and writes it with Career_Set_EntrantData_Thunk and
// on the current session's entrant row. A driver the undo of such a swap releases goes back to no number
// (clearNumbers), written the way drivers.number.set writes a number.
bool write_number(uintptr_t entity, const Row &row, uint8_t number, bool &weekend);

uintptr_t car_numbers(uintptr_t entity)
{
    bool dc = ce::mode() == ce::Mode::DriverCareer;
    uintptr_t owner =
        ce::comp::get(dc ? ce::comp::Id::DriverCareerContractsManager : ce::comp::Id::DriverContractsMyTeam);
    uintptr_t at = !owner ? 0
                   : dc   ? owner + offsetof(DriverCareerContractsManagerComponent, car_numbers)
                          : owner + offsetof(DriverContractsMyTeamComponent, car_numbers);
    uintptr_t c = at ? rdv<uintptr_t>(at) : 0;
    return c && swofty::mem::object_like(c) && rdv<uintptr_t>(c + offsetof(CarNumberComponent, entity)) == entity ? c
                                                                                                                  : 0;
}

std::string number_text(uint8_t n)
{
    return n == kNoNumber ? "none" : "#" + std::to_string(n);
}

// Gives and clears the numbers, then checks that only those rows' numbers changed; `out` gets the
// entrant rows afterwards.
void swap_numbers(uintptr_t entity, const std::vector<uint32_t> &moved, const std::vector<uint32_t> &clear,
                  std::vector<Row> &out)
{
    std::vector<Row> pre;
    if (!entrant_rows(entity, pre)) {
        ce::log("drivers.swap WARNING race numbers: the entrant table is unreadable; numbers unchanged");
        return;
    }
    std::vector<uint32_t> need;
    uintptr_t cn = 0;
    for (uint32_t d : ce::numbers::unnumbered(number_rows(pre), moved)) {
        if (!cn && !(cn = car_numbers(entity)))
            ce::log("drivers.swap WARNING race numbers: driver %u keeps no number (no CarNumberComponent on the "
                    "contracts component)",
                    save_id(d));
        else if (entity_busy(entity) || !unique_identity(entity, *row_of(pre, d)))
            ce::log("drivers.swap WARNING race numbers: driver %u keeps no number (his entrant row can't be written)",
                    save_id(d));
        else {
            Career_DriverNumbers_CheckExisting(reinterpret_cast<void *>(cn), d);
            need.push_back(d);
        }
    }
    std::vector<uint32_t> cleared;
    bool weekend = true;
    for (uint32_t d : clear) {
        const Row *r = row_of(pre, d);
        bool w = false;
        if (!r || r->rec.car_number == kNoNumber)
            continue;
        if (write_number(entity, *r, kNoNumber, w)) {
            cleared.push_back(d);
            weekend = weekend && w;
        } else
            ce::log("drivers.swap WARNING race numbers: driver %u keeps %s (his entrant row can't be written)",
                    save_id(d), number_text(r->rec.car_number).c_str());
    }
    if (!entrant_rows(entity, out) || out.size() != pre.size()) {
        ce::log("drivers.swap WARNING race numbers: the entrant table is unreadable after the numbers");
        out = pre;
        return;
    }
    size_t others = 0;
    for (size_t i = 0; i < pre.size(); ++i) {
        uint32_t d = entrant_driver(pre[i].rec);
        EntrantRecord expect = pre[i].rec;
        if (std::find(need.begin(), need.end(), d) != need.end() ||
            std::find(cleared.begin(), cleared.end(), d) != cleared.end())
            expect.car_number = out[i].rec.car_number;
        if (out[i].at != pre[i].at || std::memcmp(&out[i].rec, &expect, kEntrantSize) != 0)
            ++others;
    }
    if (others)
        ce::log("drivers.swap WARNING race numbers: %zu entrant row(s) changed beyond the numbers", others);
    auto held = number_rows(out);
    for (uint32_t d : need) {
        std::string why = ce::numbers::sole_holder(held, d);
        const Row *r = row_of(out, d);
        ce::log("drivers.swap %srace number: driver %u none -> %s (Career_DriverNumbers_CheckExisting; %s)",
                why.empty() ? "" : "WARNING ", save_id(d), number_text(r ? r->rec.car_number : kNoNumber).c_str(),
                why.empty() ? "no other entrant holds it" : why.c_str());
    }
    for (uint32_t d : cleared)
        ce::log("drivers.swap race number: driver %u %s -> none (clearNumbers, Career_Set_EntrantData_Thunk, "
                "weekend row %s)",
                save_id(d), number_text(row_of(pre, d)->rec.car_number).c_str(), weekend ? "updated" : "not found");
}

ce::OpResult op_swap(const ce::Json &args)
{
    const char *op = "drivers.swap";
    const ce::Json &ja = args["a"], &jb = args["b"];
    auto valid = [](const ce::Json &v) {
        return v.type == ce::Json::Num && v.n >= 0 && v.n <= 0xffff && v.n == std::floor(v.n);
    };
    if (!valid(ja) || !valid(jb))
        return op_fail(op, "bad_args", "a and b must be driverStatId integers 0..65535");
    uint32_t a = key_from_save(static_cast<uint32_t>(ja.n)), b = key_from_save(static_cast<uint32_t>(jb.n));
    if (a == b)
        return op_fail(op, "bad_args", "a and b are the same driver");

    uintptr_t entity = ce::comp::entity();
    if (!entity)
        return op_fail(op, "no_component", "no championship entity");
    bool my_team = ce::mode() == ce::Mode::MyTeam;
    MoveSink sink;
    sink.renewal = ce::comp::get(ce::comp::Id::CareerDriverContractRenewal);
    if (!sink.renewal && !my_team)
        sink.manager = ce::comp::get(ce::comp::Id::DriverCareerContractsManager);
    if (!sink.renewal && my_team)
        sink.my_team = ce::comp::get(ce::comp::Id::DriverContractsMyTeam);
    if (!sink.renewal && !sink.manager && !sink.my_team)
        return op_fail(op, "no_component",
                       my_team ? "My Team: neither CareerDriverContractRenewalComponent nor "
                                 "DriverContractsMyTeamComponent is on the championship entity"
                               : "Driver Career: neither CareerDriverContractRenewalComponent nor "
                                 "DriverCareerContractsManagerComponent is on the championship entity");
    if (entity_busy(entity))
        return op_fail(op, "busy", "the game is in the middle of an entrant update");
    if (sink.renewal &&
        rdv<uint8_t>(sink.renewal + offsetof(CareerDriverContractRenewalComponent, disable_driver_moves)) != 0)
        return op_fail(op, "refused", "the game has driver moves disabled for this career (disableDriverMoves)");

    std::vector<Row> rows;
    if (!entrant_rows(entity, rows))
        return op_fail(op, "no_component", "entrant table unreadable");
    const Row *pa = row_of(rows, a), *pb = row_of(rows, b);
    if (!pa || !pb)
        return op_fail(op, "no_entrant",
                       std::string("driver ") + std::to_string(save_id(!pa ? a : b)) +
                           " is not in this championship's line-up (no entrant row, listed under otherDrivers), so "
                           "it has no seat to swap; pick a driver from the drivers list");
    Row ra = *pa, rb = *pb;
    if ((ra.rec.ordinal_flags | rb.rec.ordinal_flags) & kHumanBit)
        return op_fail(op, "player_locked", "the player's own seat cannot be swapped");
    uint32_t ta = ra.rec.team_stat_id, tb = rb.rec.team_stat_id;
    bool car_a = has_car(ra.rec), car_b = has_car(rb.rec);
    bool free_a = teamless(ra.rec), free_b = teamless(rb.rec);
    if (!car_a && !car_b)
        return op_fail(op, "no_seat", "neither driver has a race seat");
    if (ta == tb && car_a == car_b)
        return op_fail(op, "same_team", "both drivers race for the same team; nothing to swap");
    bool replace = false; // a teamless driver takes a race seat, the seat's driver is released
    bool experimental = args["experimentalFreeAgent"].type == ce::Json::Bool && args["experimentalFreeAgent"].b;
    if (!sink.manager) {
        if ((free_a || free_b) && !(experimental && sink.my_team))
            return op_fail(op, "no_team",
                           std::string("driver ") + std::to_string(save_id(free_a ? a : b)) +
                               " has no team seat in this championship; in My Team swap two drivers who both "
                               "have seats (experimentalFreeAgent signs a free agent into a race seat)");
        if ((free_a || free_b) && (free_a == free_b || !(free_a ? car_b : car_a)))
            return op_fail(op, "no_seat", "a free agent signing in My Team takes a race seat: pick one free agent and "
                                          "one race driver");
        replace = free_a || free_b;
    } else {
        if ((free_a && car_a) || (free_b && car_b) || (!free_a && !car_a) || (!free_b && !car_b))
            return op_fail(op, "reserve_dc",
                           std::string("driver ") + std::to_string(save_id((!car_a && !free_a) || (free_a && car_a) ? a : b)) +
                               " is a reserve (a team but no car); in Driver Career only race drivers and "
                               "free agents can be swapped, because R&D there is per race driver");
        replace = free_a || free_b;
    }
    const ce::Json &ex = args["expect"];
    if (ex.type == ce::Json::Obj) {
        const ce::Json &ea = ex["aTeamStatId"], &eb = ex["bTeamStatId"];
        if ((ea.type == ce::Json::Num && static_cast<uint32_t>(ea.n) != ta >> 16) ||
            (eb.type == ce::Json::Num && static_cast<uint32_t>(eb.n) != tb >> 16))
            return op_fail(op, "stale", "a driver's team changed since the launcher read it");
    }

    // In a Driver Career replace the incoming (teamless) driver goes first, then the seat's driver
    // is released, as the game's signing orders it. Results keep the caller's a and b.
    const uint32_t arg_a = a, arg_b = b;
    if (replace && free_b) {
        std::swap(ra, rb);
        std::swap(a, b);
        std::swap(ta, tb);
        std::swap(car_a, car_b);
    }
    // clearNumbers (the undo of a replace that numbered a free agent): the driver this replace
    // releases goes back to no number.
    std::vector<uint32_t> clear;
    const ce::Json &jc = args["clearNumbers"];
    if (jc.type != ce::Json::Null && jc.type != ce::Json::Arr)
        return op_fail(op, "bad_args", "clearNumbers must be a list of driverStatIds");
    for (auto &v : jc.a) {
        if (!valid(v))
            return op_fail(op, "bad_args", "clearNumbers must be a list of driverStatIds");
        uint32_t k = key_from_save(static_cast<uint32_t>(v.n));
        if (!replace || k != b)
            return op_fail(op, "bad_args",
                           "clearNumbers names driver " + std::to_string(save_id(k)) +
                               ", who keeps a seat after this swap; only the driver a replace releases goes back "
                               "to no number");
        clear.push_back(k);
    }
    // Driver Career: each driver who takes a race seat signs that seat's contract, as the game's signing
    // does for its incoming driver; planned before anything is written.
    std::vector<Signing> signings;
    if (sink.manager) {
        std::vector<uintptr_t> objects;
        if (!contract_objects(sink, objects))
            return op_fail(op, "no_contract", "the Driver Career contract list is unreadable; nothing written");
        for (auto [d, out, team] : {std::tuple{a, b, tb}, std::tuple{b, a, ta}}) {
            if (replace && d == b)
                continue;
            Signing s;
            if (const char *why = plan_signing(objects, d, out, team, s))
                return op_fail(op, "no_contract",
                               "driver " + std::to_string(save_id(d)) + " can't sign for team " +
                                   std::to_string(team >> 16) + ": " + why + "; nothing written");
            signings.push_back(s);
        }
    }
    ce::OpResult r;
    r.before = swap_json(rows, arg_a, arg_b);
    char sa[96], sb[96];
    seat_text(sa, sizeof sa, ra);
    seat_text(sb, sizeof sb, rb);

    EntrantWriteData da = entrant_data(ra, rb);
    EntrantWriteData db = replace ? released_data(rb) : entrant_data(rb, ra);
    uint16_t pos_a = ra.rec.team_position, pos_b = rb.rec.team_position;
    uint8_t reason_a = car_b ? kReasonSigned : kReasonBenched;
    uint8_t reason_b = replace ? kReasonReleased : car_a ? kReasonSigned : kReasonBenched;
    uint32_t new_b = replace ? kNoKey : ta;
    if (!unique_identity(entity, ra) || !unique_identity(entity, rb))
        return op_fail(op, "refused", "an entrant row's identity is not unique in the line-up; nothing written");
    std::vector<EntrantRecord> before;
    for (auto &row : rows)
        before.push_back(row.rec);
    EntrantVector before_lineup{nullptr, before.data(), before.data() + before.size(), before.data() + before.size()};
    uintptr_t baseline = reinterpret_cast<uintptr_t>(&before_lineup);
    bool wrote_a = apply_row(entity, sink, ra, ta, tb, reason_a, da, pos_b);
    bool contract_released = false;
    if (wrote_a && replace && sink.manager && !release_contract(entity, sink, entrant_driver(rb.rec), contract_released))
        ce::log("drivers.swap WARNING the outgoing driver's contract could not be released");
    bool wrote_b = wrote_a && apply_row(entity, sink, rb, tb, new_b, reason_b, db, replace ? 0 : pos_a);

    std::vector<Row> now;
    bool ok = wrote_a && wrote_b && entrant_rows(entity, now) && now.size() == rows.size();
    const Row *na = ok ? row_of(now, a) : nullptr, *nb = ok ? row_of(now, b) : nullptr;
    ok = ok && na && nb && na->at == ra.at && nb->at == rb.at;
    std::string seats_lost;
    if (ok) {
        ok = row_matches(na->rec, ra.rec, da) && row_matches(nb->rec, rb.rec, db);
        for (size_t i = 0; ok && i < rows.size(); ++i)
            if (rows[i].at != ra.at && rows[i].at != rb.at && !same_row(rows[i]))
                ok = false;
        seats_lost = ce::lineup::seats_kept(seats_of(rows), seats_of(now));
        ok = ok && seats_lost.empty();
    }
    uint64_t finalised = 0;
    bool finalise_ok = true;
    if (!ok) {
        EntrantWriteData ua = entrant_data(ra, ra), ub = entrant_data(rb, rb);
        bool back_a = !wrote_a || apply_row(entity, sink, ra, tb, ta, car_a ? kReasonSigned : kReasonBenched, ua, pos_a);
        bool back_b = !wrote_b || apply_row(entity, sink, rb, new_b, tb, car_b ? kReasonSigned : kReasonBenched, ub, pos_b);
        if (!sink.renewal)
            finalise_moves(entity, baseline, finalised);
        return op_fail(op, "write_failed",
                       std::string(seats_lost.empty() ? "the entrant rows did not read back as swapped; "
                                                      : "the swap would change the grid (" + seats_lost + "); ") +
                           (back_a && back_b ? "restored both rows" : "COULD NOT restore every row, reload the save") +
                           (contract_released ? " (the released contract stays released)" : ""));
    }
    size_t signed_now = 0;
    for (auto &s : signings) {
        if (sign_contract(entity, sink, s))
            ++signed_now;
        else
            ce::log("drivers.swap WARNING driver %u's contract for his new seat could not be signed", save_id(s.driver));
    }
    char na_t[96], nb_t[96];
    seat_text(na_t, sizeof na_t, *na);
    seat_text(nb_t, sizeof nb_t, *nb);
    ce::log("drivers.swap driver %u [%s] <-> driver %u [%s] -> driver %u [%s], driver %u [%s] "
            "(%s; %zu other entrants unchanged, every team kept its cars, applied now)",
            save_id(a), sa, save_id(b), sb, save_id(a), na_t, save_id(b), nb_t,
            sink.renewal   ? "2 moves recorded"
            : sink.my_team ? (replace ? "My Team, experimental free agent signing: 2 rows written, the seat's contract "
                                        "records taken over, moves finalised against the line-up before the swap"
                                      : "My Team: 2 rows written, moves finalised against the line-up before the swap")
            : replace      ? (contract_released ? "Driver Career replace: incoming announced, outgoing contract released "
                                                  "and announced to no team"
                                                : "Driver Career replace: incoming announced, outgoing announced to no "
                                                  "team (no contract found)")
                           : "Driver Career: 2 moves announced",
            rows.size() - 2);
    if (sink.manager)
        ce::log("drivers.swap signed %zu/%zu seat contract(s) (MessageDriverContractChanged)", signed_now,
                signings.size());
    std::vector<Row> numbered = now;
    swap_numbers(entity, {a, b}, clear, numbered);
    if (!sink.renewal)
        finalise_ok = finalise_moves(entity, baseline, finalised);
    if (!finalise_ok)
        ce::log("drivers.swap WARNING the MessageDriverMovesFinalised messages could not be posted");
    r.after = swap_json(numbered, arg_a, arg_b);
    r.ok = true;
    if (!sink.renewal)
        ce::log("drivers.swap finalised %llu line-up change(s) (MessageDriverMovesFinalised_upgrades, _recognition, "
                "MessageDriverMovesFinalised)%s",
                static_cast<unsigned long long>(finalised), finalise_ok ? "" : " FAILED");
    if (replace && sink.my_team)
        ce::log("drivers.swap experimental: driver %u took over %zu contract record(s) of driver %u for team %u "
                "(pendingDriverMoves, decidedDriverContracts, lastSeasonsSignings)",
                save_id(a), take_over_records(sink.my_team, entrant_driver(rb.rec), a, tb), save_id(entrant_driver(rb.rec)),
                tb >> 16);
    return r;
}

// ---------------------------------------------------------------------------
// Op drivers.contracts.repair (Driver Career): every AI race driver whose contract is not an active one
// for the team he races for signs that team's contract, through the same add-contract step as a swap.

bool contract_fits(uintptr_t contract, uint32_t team)
{
    return contract && rdv<uint8_t>(contract + offsetof(DriverContract, active)) &&
           ((rdv<uint32_t>(contract + offsetof(DriverContract, team)) ^ team) & 0xffff00ffu) == 0;
}

std::string repair_json(const std::vector<Row> &rows, const std::vector<uint32_t> &which)
{
    std::vector<uintptr_t> objects;
    MoveSink sink;
    sink.manager = ce::comp::get(ce::comp::Id::DriverCareerContractsManager);
    contract_objects(sink, objects);
    ce::Writer w;
    w.begin_object();
    w.key("drivers");
    w.begin_array();
    for (uint32_t d : which) {
        const Row *r = row_of(rows, d);
        w.begin_object();
        w.key("driverStatId");
        w.integer(save_id(d));
        w.key("teamStatId");
        if (r && !teamless(r->rec))
            w.integer(r->rec.team_stat_id >> 16);
        else
            w.null();
        w.key("contract");
        contract_json(w, contract_of(objects, d));
        w.end_object();
    }
    w.end_array();
    w.end_object();
    return w.text();
}

ce::OpResult op_contracts_repair(const ce::Json &)
{
    const char *op = "drivers.contracts.repair";
    if (ce::mode() != ce::Mode::DriverCareer)
        return op_fail(op, "wrong_mode", "contract repair is Driver Career only");
    uintptr_t entity = ce::comp::entity();
    MoveSink sink;
    sink.manager = ce::comp::get(ce::comp::Id::DriverCareerContractsManager);
    std::vector<Row> rows;
    std::vector<uintptr_t> objects;
    if (!entity || !sink.manager)
        return op_fail(op, "no_component", "no championship entity or no DriverCareerContractsManagerComponent");
    if (!entrant_rows(entity, rows) || !contract_objects(sink, objects))
        return op_fail(op, "no_component", "entrant table or contract list unreadable");
    std::vector<uint32_t> need;
    for (auto &r : rows)
        if (!(r.rec.ordinal_flags & kHumanBit) && !teamless(r.rec) && has_car(r.rec) &&
            !contract_fits(contract_of(objects, entrant_driver(r.rec)), r.rec.team_stat_id))
            need.push_back(entrant_driver(r.rec));
    ce::OpResult res;
    res.before = repair_json(rows, need);
    std::string detail;
    size_t fixed = 0;
    for (uint32_t d : need) {
        const Row *r = row_of(rows, d);
        Signing s;
        const char *why = plan_signing(objects, d, d, r->rec.team_stat_id, s);
        bool ok = !why && sign_contract(entity, sink, s) && contract_fits(s.contract, r->rec.team_stat_id);
        fixed += ok;
        detail += (detail.empty() ? "" : ", ") + std::to_string(save_id(d)) + " (team " +
                  std::to_string(r->rec.team_stat_id >> 16) + ") " + (ok ? "signed" : why ? why : "signing failed");
    }
    res.after = repair_json(rows, need);
    res.ok = fixed == need.size();
    if (!res.ok)
        res.error = "write_failed: " + std::to_string(need.size() - fixed) + " of " + std::to_string(need.size()) +
                    " race driver(s) still have no contract with their team: " + detail;
    ce::log("drivers.contracts.repair %zu race driver(s) without a contract for their team, %zu signed%s%s", need.size(),
            fixed, detail.empty() ? "" : ": ", detail.c_str());
    return res;
}

// ---------------------------------------------------------------------------
// Op drivers.contract.set (unsafe: raw field write the record serialiser reads straight back)

struct MoneyField {
    const char *name;
    uintptr_t off;
};

constexpr MoneyField kMoney[] = {
    {"salary", offsetof(ContractRecord, salary)},
    {"signingBonus", offsetof(ContractRecord, signing_bonus)},
    {"teamPerfMoney", offsetof(ContractRecord, team_perf_money)},
    {"teamPerfRaceValue", offsetof(ContractRecord, team_perf_race_value)}};

ce::OpResult op_contract_set(const ce::Json &args)
{
    const char *op = "drivers.contract.set";
    uintptr_t c = ce::comp::get(ce::comp::Id::DriverContractsMyTeam);
    if (!c)
        return op_fail(op, "wrong_mode", "contracts are My Team only (no DriverContractsMyTeamComponent)");
    const ce::Json &id = args["driverStatId"];
    if (id.type != ce::Json::Num || id.n < 0 || id.n > 0xffff || id.n != std::floor(id.n))
        return op_fail(op, "bad_args", "driverStatId must be an integer 0..65535");
    std::string list = args["list"].str("decided");
    using C = DriverContractsMyTeamComponent;
    uintptr_t off = list == "decided" ? offsetof(C, decided_contracts_begin)
                    : list == "pending" ? offsetof(C, pending_contracts_begin)
                                        : 0;
    if (!off)
        return op_fail(op, "bad_args", "list must be decided or pending");
    uint32_t key = key_from_save(static_cast<uint32_t>(id.n));
    const ce::Json &team = args["newTeamStatId"];
    uintptr_t b;
    size_t n;
    if (!vec(c + off, kRecSize, kMaxRecords, b, n))
        return op_fail(op, "no_contract", "the " + list + " list is empty");
    uintptr_t rec = 0;
    int matches = 0;
    for (size_t i = 0; i < n; ++i) {
        uintptr_t p = b + i * kRecSize;
        if (key_at(p + offsetof(ContractRecord, driver)) != key)
            continue;
        if (team.type == ce::Json::Num &&
            (key_at(p + offsetof(ContractRecord, new_team)) >> 16) != static_cast<uint32_t>(team.n))
            continue;
        rec = p;
        ++matches;
    }
    if (!matches)
        return op_fail(op, "no_contract", "no " + list + " contract for driver " + std::to_string(static_cast<long long>(id.n)));
    if (matches > 1)
        return op_fail(op, "ambiguous", "several " + list + " contracts for this driver; pass newTeamStatId");

    int32_t want[4];
    bool set[4] = {};
    int count = 0;
    for (int i = 0; i < 4; ++i) {
        want[i] = rdv<int32_t>(rec + kMoney[i].off);
        const ce::Json &v = args[kMoney[i].name];
        if (v.type == ce::Json::Null)
            continue;
        if (v.type != ce::Json::Num || v.n < 0 || v.n > 1000000000.0 || v.n != std::floor(v.n))
            return op_fail(op, "bad_value", std::string(kMoney[i].name) + " must be an integer 0..1000000000");
        const ce::Json &e = args["expect"][kMoney[i].name];
        if (e.type == ce::Json::Num && static_cast<int64_t>(e.n) != want[i])
            return op_fail(op, "stale", std::string(kMoney[i].name) + " is " + std::to_string(want[i]));
        want[i] = static_cast<int32_t>(v.n);
        set[i] = true;
        ++count;
    }
    if (!count)
        return op_fail(op, "bad_args", "nothing to set (salary, signingBonus, teamPerfMoney, teamPerfRaceValue)");

    ce::Writer wb;
    record_json(wb, rec);
    ce::OpResult r;
    r.before = wb.text();
    std::string detail;
    for (int i = 0; i < 4; ++i) {
        if (!set[i])
            continue;
        int32_t old = rdv<int32_t>(rec + kMoney[i].off);
        if (!wr(rec + kMoney[i].off, want[i]) || rdv<int32_t>(rec + kMoney[i].off) != want[i])
            return op_fail(op, "write_failed", kMoney[i].name);
        detail += (detail.empty() ? "" : ", ") + std::string(kMoney[i].name) + " " + std::to_string(old) + " -> " +
                  std::to_string(want[i]);
    }
    ce::Writer wa;
    record_json(wa, rec);
    r.after = wa.text();
    r.ok = true;
    ce::log("drivers.contract.set driver %u %s contract (team %u): %s (raw record write, unsafe)",
            save_id(key), list.c_str(), key_at(rec + offsetof(ContractRecord, new_team)) >> 16, detail.c_str());
    return r;
}

// ---------------------------------------------------------------------------
// Op drivers.number.set: a driver's racing number, EntrantRecord::car_number (+0x83), which the save
// keeps with the row (Ent_Deserialise_EntrantVector) and the weekend's copy of the line-up carries
// into sessions. Written the way the game's own number writers do it (Career_DriverNumbers_AssignFree
// 0x140c01280, Career_DriverNumbers_ApplyPlayerNumber 0x140c00830): Career_Set_EntrantData_Thunk with
// the row's own data and position and the number as its flag, then the same byte on the matching row
// of the weekend that the entity registry's vtable slot +0xa8 returns. A number is free when no other
// row of the championship holds it, as the game's free-number list (Career_DriverNumbers_Add) counts.

constexpr uintptr_t kVtCurrentWeekendOf = 0xa8; // entity component registry vtable slot: the current weekend
constexpr uintptr_t kWeekendRowsBegin = 0x20;  // TODO(struct): Weekend: its line-up vector object at +0x18 {alloc, begin, end}
constexpr uintptr_t kWeekendRowsEnd = 0x28;

using CurrentWeekendFn = uintptr_t (*)(void *registry);

// The current weekend's copy of a row, matched the way the game's identity compare (0x1431146d0)
// matches it: the same human bit, and for AI rows the same ordinal_flags. 0 when there is none.
uintptr_t weekend_row(uintptr_t entity, const EntrantRecord &row)
{
    uintptr_t registry = entity + offsetof(CareerEntity, component_registry);
    uintptr_t vt = rdv<uintptr_t>(registry);
    uintptr_t fn = vt ? rdv<uintptr_t>(vt + kVtCurrentWeekendOf) : 0;
    if (!swofty::mem::in_image(fn))
        return 0;
    uintptr_t weekend = reinterpret_cast<CurrentWeekendFn>(fn)(reinterpret_cast<void *>(registry));
    uintptr_t b = weekend ? rdv<uintptr_t>(weekend + kWeekendRowsBegin) : 0;
    uintptr_t e = weekend ? rdv<uintptr_t>(weekend + kWeekendRowsEnd) : 0;
    if (!b || e < b || (e - b) % kEntrantSize || (e - b) / kEntrantSize > kMaxEntrants)
        return 0;
    for (uintptr_t p = b; p < e; p += kEntrantSize) {
        uint32_t v = rdv<uint32_t>(p + offsetof(EntrantRecord, ordinal_flags));
        bool human = row.ordinal_flags & kHumanBit;
        if (human ? (v & kHumanBit) != 0 : !(v & kHumanBit) && v == row.ordinal_flags)
            return entrant_driver(rdv<EntrantRecord>(p)) == entrant_driver(row) ? p : 0;
    }
    return 0;
}

// Career_Set_EntrantData_Thunk rewrites the whole row from the data block; with the row's own data,
// position and the new number only +0x83 changes. The human row matches by its human bit alone.
bool write_number(uintptr_t entity, const Row &row, uint8_t number, bool &weekend)
{
    weekend = false;
    if (entity_busy(entity) || (!(row.rec.ordinal_flags & kHumanBit) && !unique_identity(entity, row)))
        return false;
    uint8_t id[kIdentitySize];
    std::memcpy(id, &row.rec, kIdentitySize);
    EntrantWriteData d = entrant_data(row, row);
    Career_Set_EntrantData_Thunk(reinterpret_cast<void *>(entity), id, reinterpret_cast<const uint32_t *>(&d),
                                 row.rec.team_position, number);
    if (uintptr_t w = weekend_row(entity, row.rec); w && wr(w + offsetof(EntrantRecord, car_number), number))
        weekend = true;
    return true;
}

std::string number_json(const std::vector<Row> &rows, uint32_t driver, uint32_t other)
{
    ce::Writer w;
    w.begin_object();
    for (uint32_t k : {driver, other}) {
        if (k == kNoKey)
            continue;
        const Row *r = row_of(rows, k);
        w.key(k == driver ? "driver" : "swappedWith");
        w.begin_object();
        w.key("driverStatId");
        w.integer(save_id(k));
        w.key("raceNumber");
        if (r && r->rec.car_number != kNoNumber)
            w.integer(r->rec.car_number);
        else
            w.null();
        w.end_object();
    }
    w.end_object();
    return w.text();
}

const SwoftyCareerOps *core_number_ops()
{
    static const SwoftyCareerOps *ops = [] {
        HMODULE core = GetModuleHandleW(L"swofty_core.dll");
        auto get = core ? reinterpret_cast<SwoftyCoreCareerOpsFn>(GetProcAddress(core, "SwoftyCoreCareerOps")) : nullptr;
        const SwoftyCareerOps *t = get ? get() : nullptr;
        return t && t->version >= 4 && t->set_car_number ? t : nullptr;
    }();
    return ops;
}

ce::OpResult op_number_set(const ce::Json &args)
{
    const char *op = "drivers.number.set";
    const ce::Json &id = args["driverStatId"];
    if (id.type != ce::Json::Num || id.n < 0 || id.n > 0xffff || id.n != std::floor(id.n))
        return op_fail(op, "bad_args", "driverStatId must be an integer 0..65535");
    uint32_t key = key_from_save(static_cast<uint32_t>(id.n));
    uintptr_t entity = ce::comp::entity();
    std::vector<Row> rows;
    if (!entity || !entrant_rows(entity, rows))
        return op_fail(op, "no_component", "no championship entity or entrant table unreadable");
    if (entity_busy(entity))
        return op_fail(op, "busy", "the game is in the middle of an entrant update");
    const ce::Json &num = args["number"];
    ce::numbers::Plan plan = ce::numbers::plan(number_rows(rows), key, num.type == ce::Json::Num ? num.n : -1,
                                               args["swap"].type == ce::Json::Bool && args["swap"].b);
    if (!plan.error.empty()) {
        size_t colon = plan.error.find(':');
        return op_fail(op, plan.error.substr(0, colon).c_str(), plan.error.substr(colon + 2));
    }
    Row row = rows[plan.row];
    uint8_t had = row.rec.car_number, want = static_cast<uint8_t>(num.n);
    const ce::Json &ex = args["expect"];
    if (ex.type == ce::Json::Num && ex.n != (had == kNoNumber ? -1.0 : had))
        return op_fail(op, "stale", "his number changed since the launcher read it");
    ce::OpResult r;
    if (plan.unchanged) {
        r.ok = true;
        r.before = r.after = number_json(rows, key, kNoKey);
        return r;
    }
    const Row *holder = plan.swap_with >= 0 ? &rows[plan.swap_with] : nullptr;
    Row other = holder ? *holder : Row{};
    uint32_t other_key = holder ? entrant_driver(other.rec) : kNoKey;
    for (const Row *x : {&row, holder ? &other : nullptr})
        if (x && ((x->rec.ordinal_flags & kHumanBit) ? ce::comp::humans() != 1 : !unique_identity(entity, *x)))
            return op_fail(op, "refused", "an entrant row's identity is not unique in the line-up; nothing written");
    r.before = number_json(rows, key, other_key);

    if (const SwoftyCareerOps *core = core_number_ops()) {
        int cr = core->set_car_number(save_id(key), want, holder ? SWOFTY_NUMBER_SWAP : 0);
        std::vector<Row> after;
        if (cr != SWOFTY_ASSET_OK || !entrant_rows(entity, after))
            return op_fail(op, "write_failed", "the core's set_car_number did not write the number (see the core's log)");
        r.after = number_json(after, key, other_key);
        r.ok = true;
        ce::log("drivers.number.set driver %u #%u through the core's set_car_number", save_id(key), want);
        return r;
    }
    bool week_a = false, week_b = false;
    bool ok = write_number(entity, row, want, week_a);
    ok = ok && (!holder || write_number(entity, other, had, week_b));
    std::vector<Row> now;
    ok = ok && entrant_rows(entity, now) && now.size() == rows.size();
    for (size_t i = 0; ok && i < rows.size(); ++i) {
        EntrantRecord expect = rows[i].rec;
        if (rows[i].at == row.at)
            expect.car_number = want;
        else if (holder && rows[i].at == other.at)
            expect.car_number = had;
        ok = now[i].at == rows[i].at && std::memcmp(&now[i].rec, &expect, kEntrantSize) == 0;
    }
    if (!ok) {
        bool wa, wb;
        bool back = (!holder || write_number(entity, other, want, wb)) && write_number(entity, row, had, wa);
        return op_fail(op, "write_failed",
                       std::string("the entrant rows did not read back with the new number; ") +
                           (back ? "restored the old numbers" : "COULD NOT restore every row, reload the save"));
    }
    r.after = number_json(now, key, other_key);
    r.ok = true;
    ce::log("drivers.number.set driver %u #%s -> #%u%s (Career_Set_EntrantData_Thunk, weekend row %s; %zu other "
            "entrants unchanged)",
            save_id(key), had == kNoNumber ? "none" : std::to_string(had).c_str(), want,
            holder ? (", driver " + std::to_string(save_id(other_key)) + " #" + std::to_string(want) + " -> #" +
                      std::to_string(had))
                         .c_str()
                   : "",
            week_a && (!holder || week_b) ? "updated" : "not found", rows.size() - (holder ? 2 : 1));
    return r;
}

// ---------------------------------------------------------------------------
// Op drivers.freeAgent.add (Driver Career): a driver of the statistics table with no line-up row (a
// Driver Manager driver made after the career started) gets a teamless entrant row and a contract
// object, as the career setup makes them for a pool free agent. The setup builds the rows into the
// entity's entrant vector object {allocator +0x2d8, begin, end, capacity +0x2f0 (an element count)}
// (Game_CareerSetup_ResetEntrants 0x140c34960 through 0x140bd5100: grows through allocator vtable
// +0x18 (bytes, align 8) and frees the old buffer through +0x38). The contract rebuild (0x140d78fa0)
// gives every row a 0x58-byte DriverContract from the owner's allocator (+0x210), zeroed, with that
// allocator at +0x30 and the driver key at +0x50, pushed as a slot {allocator, contract} onto the
// owner's slot vector object {allocator +0x290, begin, end, capacity +0x2a8} (0x140d9dca0: x1.5
// growth); the contract loader (0x140d9e020) makes the same object for a saved contract without
// has_contract. The row and the contract are copied from an untouched pool free agent of the same
// line-up: a teamless AI row whose contract is inactive with a zero record and no history. Without
// one the op refuses. The new row is then rewritten with its own data through
// Career_Set_EntrantData_Thunk, which brackets the write with the entity's entrant-change messages
// (the end message carries the vector object), as the setup brackets its rebuild.
constexpr uintptr_t kEntityRowsAlloc = 0x2d8;  // TODO(struct): CareerEntity entrant vector object allocator
constexpr uintptr_t kEntityRowsCap = 0x2f0;    // TODO(struct): CareerEntity entrant vector capacity (element count)
constexpr uintptr_t kOwnerContractAlloc = 0x210; // TODO(struct): DriverCareerContractOwner allocator of the contracts
constexpr uintptr_t kOwnerSlotsAlloc = 0x290;  // TODO(struct): DriverCareerContractOwner slot vector allocator
constexpr uintptr_t kOwnerSlotsCap = 0x2a8;    // TODO(struct): DriverCareerContractOwner slot capacity (element count)
constexpr uintptr_t kContractAllocField = 0x30; // TODO(struct): DriverContract history allocator
constexpr size_t kContractBytes = 0x58;         // DriverContract allocation size (rebuild and loader)
constexpr size_t kContractAlign = 0x10;
constexpr size_t kVecAlign = 8;                 // alignment both vector growths pass
constexpr uint32_t kOrdinalMask = 0x00ffffffu;  // EntrantRecord::ordinal_flags running index
static_assert(kEntityRowsCap == kEntityRowsAlloc + 0x18 && kOwnerSlotsCap == kOwnerSlotsAlloc + 0x18 &&
              kEntityRowsAlloc + 8 == offsetof(CareerEntity, entrants_begin) &&
              kOwnerSlotsAlloc + 8 == offsetof(DriverCareerContractOwner, contracts_begin));
constexpr size_t kIdBlock = 0x08;               // TODO(struct): EntrantRecord name_block (+0x08..+0x47, L1)
constexpr size_t kRowConstFf = 0x82;            // TODO(struct): EntrantRecord unk_0x82, 0xff in every parsed and written row

using AllocFn = void *(*)(void *self, size_t size, size_t align);
using FreeFn = void (*)(void *self, void *p);

AllocFn alloc_fn(uintptr_t allocator)
{
    uintptr_t vt = allocator && swofty::mem::object_like(allocator) ? rdv<uintptr_t>(allocator) : 0;
    uintptr_t fn = vt ? rdv<uintptr_t>(vt + offsetof(NeAllocatorVtbl, alloc)) : 0;
    return swofty::mem::in_image(fn) ? reinterpret_cast<AllocFn>(fn) : nullptr;
}

FreeFn free_fn(uintptr_t allocator)
{
    uintptr_t vt = allocator && swofty::mem::object_like(allocator) ? rdv<uintptr_t>(allocator) : 0;
    uintptr_t fn = vt ? rdv<uintptr_t>(vt + offsetof(NeAllocatorVtbl, free)) : 0;
    return swofty::mem::in_image(fn) ? reinterpret_cast<FreeFn>(fn) : nullptr;
}

// A vector object {allocator, begin, end, capacity count} with elements of `stride` bytes.
struct VecObj {
    uintptr_t at = 0;
    uintptr_t allocator = 0;
    uintptr_t begin = 0, end = 0;
    size_t cap = 0;
    size_t stride = 0;
    size_t count() const { return (end - begin) / stride; }
};

bool vec_obj(uintptr_t at, size_t stride, size_t max, VecObj &v)
{
    v = {};
    v.at = at;
    v.stride = stride;
    if (!rd(at, v.allocator) || !rd(at + 8, v.begin) || !rd(at + 0x10, v.end) || !rd(at + 0x18, v.cap))
        return false;
    if (!v.begin || v.end < v.begin || (v.end - v.begin) % stride || v.count() > max || v.cap < v.count() ||
        v.cap > 4 * max || !alloc_fn(v.allocator) || !free_fn(v.allocator))
        return false;
    return writable(at + 8, 0x18) && (v.count() == 0 || swofty::mem::readable(v.begin, v.end - v.begin));
}

// Appends one element the way the game's pushes do: in place while the capacity allows, otherwise a
// buffer of x1.5 the count (at least one more) from the vector's allocator, the old elements moved,
// the old buffer freed. The vectors here hold plain records and owning slots whose moved-from
// pointers the game clears and then frees nothing for, so a byte copy is the move.
bool vec_push(VecObj &v, const void *elem)
{
    size_t n = v.count();
    if (n < v.cap) {
        if (!writable(v.end, v.stride))
            return false;
        std::memcpy(reinterpret_cast<void *>(v.end), elem, v.stride);
        v.end += v.stride;
        return wr(v.at + 0x10, v.end);
    }
    size_t cap = std::max(n + n / 2, n + 1);
    auto *nb = static_cast<uint8_t *>(alloc_fn(v.allocator)(reinterpret_cast<void *>(v.allocator), cap * v.stride, kVecAlign));
    if (!nb)
        return false;
    std::memcpy(nb, reinterpret_cast<const void *>(v.begin), n * v.stride);
    std::memcpy(nb + n * v.stride, elem, v.stride);
    uintptr_t old = v.begin;
    v.begin = reinterpret_cast<uintptr_t>(nb);
    v.end = v.begin + (n + 1) * v.stride;
    v.cap = cap;
    wr(v.at + 8, v.begin);
    wr(v.at + 0x10, v.end);
    wr(v.at + 0x18, v.cap);
    free_fn(v.allocator)(reinterpret_cast<void *>(v.allocator), reinterpret_cast<void *>(old));
    return true;
}

// Drops the last element when bytes [off, off + n) still match `expect` (the element vec_push added);
// the buffer stays.
bool vec_pop(VecObj &v, const void *expect, size_t off, size_t n)
{
    uintptr_t b = 0, e = 0;
    if (!rd(v.at + 8, b) || !rd(v.at + 0x10, e) || e < b + v.stride || (e - b) % v.stride ||
        !swofty::mem::readable(e - v.stride + off, n) ||
        std::memcmp(reinterpret_cast<const void *>(e - v.stride + off), static_cast<const uint8_t *>(expect) + off, n) != 0)
        return false;
    return wr(v.at + 0x10, e - v.stride);
}

struct ContractSlot {
    uintptr_t allocator = 0;
    uintptr_t contract = 0;
};
static_assert(sizeof(ContractSlot) == kContractSlot);

bool zero_bytes(uintptr_t p, size_t n)
{
    uint8_t buf[0x30] = {};
    uint8_t live[0x30];
    return n <= sizeof live && swofty::mem::readable(p, n) && rd(p, live) && std::memcmp(live, buf, n) == 0;
}

// A pool free agent nobody has touched: AI, teamless, no car, no seat value, no position or number, and
// a contract object that is inactive with a zero record and an empty history.
bool untouched_free_agent(const Row &r, const Row &first, uintptr_t contract)
{
    const EntrantRecord &x = r.rec;
    if ((x.ordinal_flags & kHumanBit) || !teamless(x) || x.vehicle_ref != kNoKey || x.driver_reg_value != kNoKey ||
        x.vehicle_customisation != kNoKey || x.badge_stat_id != kNoKey || x.const_ffff0012 != kNoSeatValue ||
        x.team_position != 0 || reinterpret_cast<const uint8_t *>(&x)[kRowConstFf] != 0xff || x.car_number != kNoNumber || x.vtable != first.rec.vtable ||
        x.subobj_vtable != first.rec.subobj_vtable)
        return false;
    return contract && zero_bytes(contract, kContractAllocField) &&
           rdv<uintptr_t>(contract + offsetof(DriverContract, history_begin)) ==
               rdv<uintptr_t>(contract + offsetof(DriverContract, history_end));
}

// The entrant id block the parser builds for every row (0x1441207d0 into +0x08..+0x47): kind byte 1,
// then three random 64-bit values, each stored twice (+0x10/+0x18, +0x20/+0x28, +0x30/+0x38), and a
// zero u16 at +0x40. Only the human row is matched by it (0x1431146d0); AI rows match by ordinal_flags.
void fresh_id_block(EntrantRecord &r)
{
    std::random_device rd;
    auto *p = reinterpret_cast<uint8_t *>(&r) + kIdBlock;
    std::memset(p, 0, 8);
    p[0] = 1;
    for (size_t i = 0; i < 3; ++i) {
        uint64_t v = (static_cast<uint64_t>(rd()) << 32) | rd();
        std::memcpy(p + 8 + i * 16, &v, sizeof v);
        std::memcpy(p + 16 + i * 16, &v, sizeof v);
    }
    std::memset(p + 0x38, 0, 2);
}

std::string free_agent_json(const std::vector<Row> &rows, const std::vector<uintptr_t> &objects, uint32_t driver)
{
    const Row *r = row_of(rows, driver);
    ce::Writer w;
    w.begin_object();
    w.key("driverStatId");
    w.integer(save_id(driver));
    w.key("seat");
    if (r)
        seat_json(w, r->rec);
    else
        w.null();
    w.key("contract");
    contract_json(w, contract_of(objects, driver));
    w.end_object();
    return w.text();
}

// The driver_customisation key the career setup packs into a pool free agent's row: the stat_id of the look
// that his entrant row's driver_customisation names, in the high word (look 229 is 0x00e50000). A Driver
// Manager driver's entrant row is dm_<id>, naming the look dm_<id>_look. A seated driver without it stops
// the next session at its loading screen.
bool entrant_look(uint32_t key, uint32_t &out, std::string &why)
{
    const Names &n = names();
    auto it = n.driver.find(save_id(key));
    if (it == n.driver.end()) {
        why = "the driver has no row in entrants.drivers";
        return false;
    }
    std::string look;
    each_row("entrants.entrant_lists.entrants", "entrant", [&](const std::string &row) {
        std::string v;
        if (look.empty() && db_text(row + ".driver", v) && v == it->second &&
            db_text(row + ".driver_customisation", v))
            look = v;
    });
    if (look.empty()) {
        why = "no entrant row names a driver_customisation for " + it->second;
        return false;
    }
    int64_t id = -1;
    each_row("entrants.driver_customisations", "folder", [&](const std::string &folder) {
        each_row(folder, "preloaded_driver_customisation_properties", [&](const std::string &row) {
            std::string v;
            int64_t s;
            if (id < 0 && db_text(row + "._metadata_", v) && v == look && db_int(row + ".stat_id", s))
                id = s;
        });
    });
    if (id <= 0 || id >= 0x3fff) {
        why = "no look row " + look + " with a stat_id";
        return false;
    }
    out = static_cast<uint32_t>(id) << 16;
    return true;
}

ce::OpResult op_free_agent_add(const ce::Json &args)
{
    const char *op = "drivers.freeAgent.add";
    const ce::Json &jd = args["driverStatId"];
    if (jd.type != ce::Json::Num || jd.n < 0 || jd.n > 0xffff || jd.n != std::floor(jd.n))
        return op_fail(op, "bad_args", "driverStatId must be an integer 0..65535");
    uint32_t key = key_from_save(static_cast<uint32_t>(jd.n));
    if (ce::mode() != ce::Mode::DriverCareer)
        return op_fail(op, "wrong_mode", "adding a free agent to the line-up is Driver Career only");
    uintptr_t entity = ce::comp::entity();
    MoveSink sink;
    sink.manager = ce::comp::get(ce::comp::Id::DriverCareerContractsManager);
    uintptr_t owner = sink.manager ? rdv<uintptr_t>(sink.manager + offsetof(DriverCareerContractsManagerComponent,
                                                                              contract_owner))
                                   : 0;
    if (!entity || !owner)
        return op_fail(op, "no_component", "no championship entity or no DriverCareerContractsManagerComponent");
    if (entity_busy(entity))
        return op_fail(op, "busy", "the game is in the middle of an entrant update");
    Comp c;
    const char *why = "";
    if (!find_comp(c, &why))
        return op_fail(op, "no_component", why);
    if (!find_driver(c, key))
        return op_fail(op, "unknown_driver",
                       "driver " + std::to_string(save_id(key)) + " is not in the career's statistics table");

    std::vector<Row> rows;
    std::vector<uintptr_t> objects;
    VecObj row_vec, slot_vec;
    if (!entrant_rows(entity, rows) || !contract_objects(sink, objects) ||
        !vec_obj(entity + kEntityRowsAlloc, kEntrantSize, kMaxEntrants, row_vec) ||
        !vec_obj(owner + kOwnerSlotsAlloc, kContractSlot, 4096, slot_vec) || row_vec.count() != rows.size())
        return op_fail(op, "no_component", "the line-up or the contract list is unreadable; nothing written");
    if (row_of(rows, key))
        return op_fail(op, "already_listed",
                       "driver " + std::to_string(save_id(key)) +
                           " already has a line-up row (listed under drivers); drivers.swap seats him");
    if (rows.size() + 1 > kMaxEntrants)
        return op_fail(op, "refused", "the line-up is full");
    uintptr_t own = contract_of(objects, key);
    if (own && rdv<uint8_t>(own + offsetof(DriverContract, active)))
        return op_fail(op, "refused", "the driver has an active contract but no line-up row; nothing written");

    uintptr_t contract_alloc = rdv<uintptr_t>(owner + kOwnerContractAlloc);
    const Row *tmpl = nullptr;
    uintptr_t tmpl_contract = 0;
    for (auto &r : rows) {
        uintptr_t k = contract_of(objects, entrant_driver(r.rec));
        if (untouched_free_agent(r, rows.front(), k)) {
            tmpl = &r;
            tmpl_contract = k;
            break;
        }
    }
    if (!tmpl)
        return op_fail(op, "refused",
                       "no untouched free agent in this line-up to copy the row and contract from (a teamless AI "
                       "row whose contract is inactive, zero and without history); nothing written");
    ContractSlot tmpl_slot{};
    for (uintptr_t p = slot_vec.begin; p < slot_vec.end; p += kContractSlot)
        if (rdv<uintptr_t>(p + sizeof(void *)) == tmpl_contract)
            rd(p, tmpl_slot);
    if (!alloc_fn(contract_alloc) || rdv<uintptr_t>(tmpl_contract + kContractAllocField) != contract_alloc ||
        tmpl_slot.allocator != contract_alloc ||
        rdv<uint32_t>(tmpl_contract + offsetof(DriverContract, driver)) != entrant_driver(tmpl->rec))
        return op_fail(op, "refused",
                       "the free agent's contract does not have the layout the game's contract rebuild makes "
                       "(allocator at +0x30 and in its slot, driver at +0x50); nothing written");

    uint32_t top = 0;
    for (auto &r : rows)
        top = std::max(top, r.rec.ordinal_flags & kOrdinalMask);
    if (top + 1 > kOrdinalMask)
        return op_fail(op, "refused", "no free entrant index");
    EntrantRecord rec = tmpl->rec;
    fresh_id_block(rec);
    rec.ordinal_flags = (tmpl->rec.ordinal_flags & ~kOrdinalMask) | (top + 1);
    std::memcpy(&rec.id_lo, &key, sizeof key);
    std::string no_look;
    if (!entrant_look(key, rec.driver_customisation, no_look))
        return op_fail(op, "refused", "the driver's look is not found (" + no_look + "); nothing written");
    for (auto &r : rows)
        if (r.rec.ordinal_flags == rec.ordinal_flags)
            return op_fail(op, "refused", "the new entrant index is taken; nothing written");

    ce::OpResult res;
    res.before = free_agent_json(rows, objects, key);
    uint8_t tmpl_bytes[kContractBytes] = {};
    rd(tmpl_contract, tmpl_bytes);
    std::string hex;
    for (uint8_t b : tmpl_bytes) {
        char h[3];
        std::snprintf(h, sizeof h, "%02x", b);
        hex += h;
    }
    ce::log("drivers.freeAgent.add driver %u: template free agent %u ordinal_flags 0x%08x driver_customisation 0x%08x "
            "contract %s",
            save_id(key), save_id(entrant_driver(tmpl->rec)), tmpl->rec.ordinal_flags, tmpl->rec.driver_customisation,
            hex.c_str());
    if (args["dryRun"].type == ce::Json::Bool && args["dryRun"].b) {
        ce::log("drivers.freeAgent.add driver %u: dry run, entrant index %u, look 0x%08x, contract %s; nothing written",
                save_id(key), rec.ordinal_flags & kOrdinalMask, rec.driver_customisation, own ? "kept" : "to create");
        res.after = res.before;
        res.ok = true;
        return res;
    }
    uint8_t made[kContractBytes] = {};
    std::memcpy(made + kContractAllocField, &contract_alloc, sizeof contract_alloc);
    std::memcpy(made + offsetof(DriverContract, driver), &key, sizeof key);
    ContractSlot slot{contract_alloc, own};
    if (!own) {
        void *p = alloc_fn(contract_alloc)(reinterpret_cast<void *>(contract_alloc), kContractBytes, kContractAlign);
        if (!p || !writable(reinterpret_cast<uintptr_t>(p), kContractBytes))
            return op_fail(op, "write_failed", "the contract allocator returned no memory; nothing written");
        std::memcpy(p, made, sizeof made);
        slot.contract = reinterpret_cast<uintptr_t>(p);
        if (!vec_push(slot_vec, &slot)) {
            free_fn(contract_alloc)(reinterpret_cast<void *>(contract_alloc), p);
            return op_fail(op, "write_failed", "the contract list could not grow; nothing written");
        }
    }
    auto drop_contract = [&]() {
        if (own)
            return true;
        if (!vec_pop(slot_vec, &slot, 0, sizeof slot))
            return false;
        free_fn(contract_alloc)(reinterpret_cast<void *>(contract_alloc), reinterpret_cast<void *>(slot.contract));
        return true;
    };
    if (!vec_push(row_vec, &rec)) {
        bool back = drop_contract();
        return op_fail(op, "write_failed", std::string("the line-up could not grow; ") +
                                               (back ? "the contract was removed again" : "COULD NOT remove the contract, reload the save"));
    }
    Row added{row_vec.end - kEntrantSize, rec};
    bool announced = !entity_busy(entity) && unique_identity(entity, added);
    if (announced) {
        uint8_t id[kIdentitySize];
        std::memcpy(id, &rec, kIdentitySize);
        EntrantWriteData d = entrant_data(added, added);
        Career_Set_EntrantData_Thunk(reinterpret_cast<void *>(entity), id, reinterpret_cast<const uint32_t *>(&d),
                                     rec.team_position, rec.car_number);
    }

    std::vector<Row> now;
    std::vector<uintptr_t> objects_now;
    bool ok = announced && !entity_busy(entity) && entrant_rows(entity, now) && now.size() == rows.size() + 1 &&
              contract_objects(sink, objects_now) && objects_now.size() == objects.size() + (own ? 0 : 1);
    std::string what = !announced ? "the new row's identity is not unique" : "the line-up or contract list changed size";
    for (size_t i = 0; ok && i < rows.size(); ++i)
        if (std::memcmp(&now[i].rec, &rows[i].rec, kEntrantSize) != 0) {
            ok = false;
            what = "another row changed";
        }
    if (ok && (now.back().at != row_vec.end - kEntrantSize || std::memcmp(&now.back().rec, &rec, kEntrantSize) != 0)) {
        ok = false;
        what = "the new row did not read back as written";
    }
    uint8_t live[kContractBytes] = {};
    if (ok && (contract_of(objects_now, key) != slot.contract ||
               (!own && (!rd(slot.contract, live) || std::memcmp(live, made, sizeof made) != 0)))) {
        ok = false;
        what = "the new contract did not read back as created";
    }
    if (!ok) {
        bool back = vec_pop(row_vec, &rec, offsetof(EntrantRecord, ordinal_flags), 0xc) && drop_contract();
        if (announced) {
            std::vector<Row> after;
            bool weekend = false;
            if (entrant_rows(entity, after) && row_of(after, entrant_driver(tmpl->rec)))
                write_number(entity, *row_of(after, entrant_driver(tmpl->rec)), tmpl->rec.car_number, weekend);
        }
        return op_fail(op, "write_failed",
                       what + "; " + (back ? "removed the row and the contract again" : "COULD NOT remove every write, reload the save"));
    }
    res.after = free_agent_json(now, objects_now, key);
    res.ok = true;
    char tt[96];
    seat_text(tt, sizeof tt, now.back());
    ce::log("drivers.freeAgent.add driver %u: entrant row added [%s, entrant index %u, look 0x%08x, copied from free agent %u], "
            "contract %s (inactive, zero record); %zu -> %zu rows, %zu -> %zu contracts, %zu other rows unchanged "
            "(Career_Set_EntrantData_Thunk announced the line-up)",
            save_id(key), tt, rec.ordinal_flags & kOrdinalMask, rec.driver_customisation, save_id(entrant_driver(tmpl->rec)),
            own ? "kept" : "created", rows.size(), now.size(), objects.size(), objects_now.size(), rows.size());
    return res;
}

} // namespace

CE_SECTION("drivers", section);
CE_OP("drivers.ratings.set", op_ratings_set);
CE_OP("drivers.number.set", op_number_set);
CE_OP("drivers.contract.set", op_contract_set);
CE_OP("drivers.swap", op_swap);
CE_OP("drivers.contracts.repair", op_contracts_repair);
CE_OP("drivers.freeAgent.add", op_free_agent_add);
