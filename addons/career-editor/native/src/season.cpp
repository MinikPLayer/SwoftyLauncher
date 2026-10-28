// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Career Mode Editor: season info, standings and calendar (protocol/season.md).
//
// The career's championship (vt_Career_Championship; ce::comp::get(Championship), which reads
// CareerEntity.current_championship) saves `completion` (+0x538), `season_index` and
// `seasons[]`: a vector of 16-byte entries at +0x548..+0x550 whose +8 is a Season, with
// the current index at +0x560 (its deserialiser clamps it to the vector). Seasons 3+
// are just later entries of the same vector.
//
// A Season (vt_Career_Season) saves `name`, `completion_state` (+0x538),
// `current_weekend` (+0x2a0) and `weekend_names[]`: one Weekend (vt_Career_Weekend,
// 0x4f0 bytes) per round in the vector at +0x288..+0x290, each named by the char* at
// weekend+0x28 (what the serialiser writes). Its deserialiser rebuilds every Weekend
// from that name, so round order and presence live in the save by name only.
//
// The standings model (F1SeasonStandings, embedded at season+0x2a8) is derived from
// session results by Season::applyResultsToStandings and rebuilt from them when a
// season loads:
//   +0x20..+0x28  points:  per round {alloc, begin, end, cap} -> per session 0x28
//                 {alloc, begin, end, cap, u32 session id} -> {u32 key, i32 points}
//   +0xd0..+0xd8  results: same shape -> {u32 key, i32 pos (0-based), i32 fastest-lap
//                 rank, u32 status}; the model scores status 0/1 only
// The points records already carry the points table, the shortened-race scaling and
// the fastest-lap bonus (the model's own recompute writes them), so the snapshot only
// sums them.
//
// Weekend sessions (0xa0 each, inline at weekend+0x220..+0x228) carry the session id
// at +0x08 and the completion state (u32, 2 = complete) at +0x10 once the game has
// built them. weekend+0x278 is the weekend's track_stat_id (u8; 0xff = none, a
// placeholder that some readers index the track table with unchecked).
//
// Points adjustments go through the game's own standings rebuild. Its season step
// (Career_Rebuild_SeasonStandings) takes four by-value optional vectors
// {alloc*, begin, end, cap, u8 engaged @+0x20}; the third is a {u32 entrant key,
// i32 points} extra-points list that the standings init matches to the entrants and
// keeps at standings+0xa8, the points recompute adds as one more points entry, and
// the constructors' standings sum per team. The callee frees every engaged list with
// its own allocator, so ours is built with the game's default allocator
// (g_Ne_DefaultAllocator) and never touched again. The fourth list (extra_e) belongs to
// the constructors side and is passed through untouched. The re-init that consumes the
// lists is skipped when the season's first weekend has no sessions yet; an adjustment
// is then reported as pending. Nothing saves the list, and the game rebuilds the
// standings when a season starts or loads, so the adjustments live in the career's sidecar
// (careers/<career id>.season.json) and the WRAP below merges them into the list on
// every rebuild. The op stores the adjustment and runs the championship-wide rebuild
// (Career_Rebuild_ChampionshipStandings, which builds the per-season order lists
// itself). Without this addon the adjustment simply disappears.
//
// Calendar edits on rounds that have not started (weekend+0x23c == 0) and lie after
// the current round run the sequence of the game's own RebuildNotStartedWeekends
// (Career_Add_TrackSetToSeason): remove = Career_Teardown_Weekend, Ne_Detach_ChildNode,
// erase from the vector, the weekend's own destructor (vtable slot 0, no free) and a
// free through the season allocator (+0x280, slot +0x38); add =
// Career_Create_SeasonWeekend(name, the weekend's real track_stat_id, taken from an
// existing weekend of that name or from the launcher's gamedb lookup; refused when
// unresolved) and Career_Add_SeasonWeekend, then the pointer is moved from the end to
// its round.
//
// The round count stays at the season's race weekends: the career clock stops at the
// RaceWeekend activities the ScheduleComponent builds from the gamedb event list (not
// saved, rebuilt at load) and races Season.weekends[current_weekend] at each one, and
// the season completes only after its last weekend (calendar_rules.hpp). add_round and
// remove_round refuse to move the count away from that number; season.calendar.repair
// brings an edited season back to it.
//
// Every pointer is region-checked through ce::comp::read; objects are accepted only
// with the expected vtable. Nothing game-side is cached across frames: each section,
// op and rebuild resolves the season again through the component registry.

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <swofty/game.hpp>
#include <swofty/mixin.hpp>
#include <swofty/storage.hpp>

#include "calendar_rules.hpp"
#include "ce.hpp"
#include "components.hpp"

using namespace swofty::game;

namespace {

// Field offsets and element sizes come from the KB structs (re/db/structs/, generated into
// swofty/game/career.hpp and core.hpp); every read still goes through the region-checked rd().

constexpr uintptr_t kChampCompletion = offsetof(Championship, completion);
constexpr uintptr_t kChampSeasons = offsetof(Championship, seasons_begin);
constexpr uintptr_t kChampSeasonIndex = offsetof(Championship, season_index);
constexpr uintptr_t kChampList = offsetof(Championship, list_vtable);
constexpr size_t kChampSeasonStride = sizeof(ChampionshipSeasonEntry);
constexpr uintptr_t kChampSeasonPtr = offsetof(ChampionshipSeasonEntry, season);

constexpr uintptr_t kSeasonName = offsetof(Season, name);
constexpr uintptr_t kSeasonWeekends = offsetof(Season, weekends_begin);
constexpr uintptr_t kSeasonCurrent = offsetof(Season, current_weekend);
constexpr uintptr_t kSeasonStandings = offsetof(Season, standings_vtable);
constexpr uintptr_t kSeasonCompletion = offsetof(Season, completion_state);
constexpr uintptr_t kSeasonAlloc = offsetof(Season, allocator);
constexpr uintptr_t kSeasonWeekendsEnd = offsetof(Season, weekends_end);
constexpr size_t kWeekendPtr = sizeof(Weekend *);

constexpr uintptr_t kWeekendName = offsetof(Weekend, name);
constexpr uintptr_t kWeekendSessions = offsetof(Weekend, sessions_begin);
constexpr uintptr_t kWeekendState = offsetof(Weekend, progress_state);
constexpr uintptr_t kWeekendTrack = offsetof(Weekend, track_stat_id);

constexpr size_t kSessionStride = sizeof(Session);
constexpr uintptr_t kSessionId = offsetof(Session, session_id);
constexpr uintptr_t kSessionCompletion = offsetof(Session, completion);
constexpr uintptr_t kSessionParticipants = offsetof(Session, participants_begin);

constexpr size_t kEntrantStride = sizeof(EntrantRecord);
// The packed entrant key is the dword starting at id_lo (driver stat id = id_hi, its high word).
constexpr uintptr_t kEntrantKey = offsetof(EntrantRecord, id_lo);

constexpr uintptr_t kStPoints = offsetof(F1SeasonStandings, points_begin);
constexpr uintptr_t kStExtra = offsetof(F1SeasonStandings, extra_points_begin);
constexpr uintptr_t kStResults = offsetof(F1SeasonStandings, results_begin);
constexpr size_t kStRoundStride = sizeof(StandingsRound);
constexpr uintptr_t kStRoundSessions = offsetof(StandingsRound, sessions_begin);
constexpr size_t kStSessionStride = sizeof(StandingsSession);
constexpr uintptr_t kStSessionRecords = offsetof(StandingsSession, records_begin);
constexpr uintptr_t kStSessionId = offsetof(StandingsSession, session_id);
constexpr size_t kStPointRec = sizeof(StandingsPoints);
constexpr size_t kStResultRec = sizeof(StandingsResult);

constexpr uintptr_t kCareerTimeMinutes = offsetof(CareerTimeComponent, current_minutes);

constexpr uintptr_t kSchedSeasons = offsetof(ScheduleComponent, seasons_begin);
constexpr size_t kSchedSeasonStride = sizeof(ScheduleSeason);
constexpr uintptr_t kSchedDrivers = offsetof(ScheduleSeason, driver_calendars_begin);
constexpr size_t kCalendarStride = sizeof(DriverCalendar);
constexpr uintptr_t kCalendarActivities = offsetof(DriverCalendar, activities_begin);
constexpr uintptr_t kActivityDef = offsetof(CalendarActivity, activity);
constexpr uintptr_t kDefType = offsetof(ActivityDef, type);

constexpr uintptr_t kAllocVtable = offsetof(NeAllocator, vtable);
constexpr uintptr_t kAllocFnAlloc = offsetof(NeAllocatorVtbl, alloc);
constexpr uintptr_t kAllocFnFree = offsetof(NeAllocatorVtbl, free);

// Not fields: the track placeholder value, vtable slot 0 (scalar destructor) and limits.
constexpr uint8_t kNoTrack = 0xff;
constexpr uintptr_t kVecEnd = sizeof(void *); // every game vector here is {begin, end, ...}: end follows begin
constexpr uintptr_t kDtorSlot = 0;
constexpr size_t kMaxAdjust = 64;
constexpr int kMaxDelta = 500;

constexpr size_t kMaxSeasons = 64;
constexpr size_t kMaxRounds = 64;
constexpr size_t kMaxSessions = 16;
constexpr size_t kMaxRecs = 64;
constexpr int kMaxPos = 28;
constexpr size_t kMaxCalendars = 256;
constexpr size_t kMaxActivities = 2048;
constexpr size_t kActivitySlot = 2 * sizeof(void *); // calendar activity slots: {allocator, CalendarActivity *}

template <class T> bool rd(uintptr_t a, T &out)
{
    return a && ce::comp::read(a, out);
}

bool vec(uintptr_t at, size_t stride, size_t maxn, uintptr_t &begin, size_t &n)
{
    uintptr_t b = 0, e = 0;
    if (!rd(at, b) || !rd(at + kVecEnd, e))
        return false;
    if (b == e) {
        begin = b;
        n = 0;
        return true;
    }
    if (!b || e < b || (e - b) % stride || (e - b) / stride > maxn)
        return false;
    if (!ce::comp::readable(b, e - b))
        return false;
    begin = b;
    n = (e - b) / stride;
    return true;
}

std::string cstr(uintptr_t p, size_t max)
{
    std::string s;
    if (!p)
        return s;
    for (size_t i = 0; i < max; ++i) {
        char c = 0;
        if (!rd(p + i, c) || c == 0)
            break;
        if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7e)
            return {};
        s += c;
    }
    return s;
}

bool has_vtable(uintptr_t obj, void **vt)
{
    uintptr_t v = 0;
    return vt && rd(obj, v) && v == reinterpret_cast<uintptr_t>(vt);
}

// ---------------------------------------------------------------------------
// Locating the current season: ce::comp::get(Championship), checked against
// vt_Career_Championship (or vt_Career_Season if the registry hands out the season).

struct Where {
    uintptr_t champ = 0;
    uintptr_t season = 0;
    uint32_t index = 0;
    uint32_t count = 0;
    const char *reason = nullptr;
};

struct Found {
    uintptr_t champ = 0;
    const char *via = nullptr;
    const char *reason = nullptr;
};

// The championship: ce::comp::get(Championship), which takes CareerEntity.current_championship
// (and its other candidates) and logs each step; accepted only with vt_Career_Championship.
Found find_championship()
{
    Found f;
    uintptr_t comp = ce::comp::get(ce::comp::Id::Championship);
    if (comp && has_vtable(comp, vt_Career_Championship.get())) {
        f.champ = comp;
        f.via = "comp::get";
        return f;
    }
    f.reason = !ce::comp::entity() ? "no_entity" : comp ? "championship_vtable" : "no_championship";
    static const char *s_logged = nullptr;
    if (s_logged != f.reason) {
        s_logged = f.reason;
        uintptr_t vt = 0;
        rd(comp, vt);
        ce::log("season: championship not found (%s): comp::get 0x%llx vt 0x%llx, entity 0x%llx", f.reason,
                (unsigned long long)comp, (unsigned long long)vt, (unsigned long long)ce::comp::entity());
    }
    return f;
}

bool locate(Where &w)
{
    if (!ce::in_career()) {
        w.reason = "no_career";
        return false;
    }
    Found f = find_championship();
    if (!f.champ) {
        w.reason = f.reason;
        return false;
    }
    uintptr_t comp = f.champ;
    uintptr_t b = 0;
    size_t n = 0;
    uint32_t idx = 0;
    if (!vec(comp + kChampSeasons, kChampSeasonStride, kMaxSeasons, b, n) || n == 0 ||
        !rd(comp + kChampSeasonIndex, idx) || idx >= n) {
        w.reason = "no_season";
        return false;
    }
    uintptr_t season = 0;
    if (!rd(b + idx * kChampSeasonStride + kChampSeasonPtr, season) || !has_vtable(season, vt_Career_Season.get())) {
        w.reason = "bad_season";
        return false;
    }
    w.champ = comp;
    w.season = season;
    w.index = idx;
    w.count = static_cast<uint32_t>(n);
    static uintptr_t s_logged_season = 0;
    if (season != s_logged_season) {
        s_logged_season = season;
        ce::log("season: championship via %s 0x%llx; season %u/%u at 0x%llx", f.via, (unsigned long long)comp, idx + 1,
                (unsigned)n, (unsigned long long)season);
    }
    return true;
}

struct Round {
    uintptr_t weekend = 0;
    std::string name;
};

bool rounds(uintptr_t season, std::vector<Round> &out, uint32_t &current)
{
    out.clear();
    uintptr_t b = 0;
    size_t n = 0;
    if (!vec(season + kSeasonWeekends, kWeekendPtr, kMaxRounds, b, n) || !rd(season + kSeasonCurrent, current))
        return false;
    for (size_t i = 0; i < n; ++i) {
        Round r;
        if (!rd(b + i * kWeekendPtr, r.weekend) || !has_vtable(r.weekend, vt_Career_Weekend.get()))
            return false;
        uintptr_t name = 0;
        rd(r.weekend + kWeekendName, name);
        r.name = cstr(name, 0x40);
        out.push_back(r);
    }
    return true;
}

// The race-weekend activities of this season on the game's career calendars: the ScheduleComponent's
// per-season record holds every driver's calendar, which the daily tick refills with the whole season's
// event-list activities (0x14126a1a0). -1 when none can be read.
int race_weekends(uint32_t season_index)
{
    uintptr_t sched = ce::comp::get(ce::comp::Id::Schedule);
    uintptr_t sb = 0, cb = 0;
    size_t sn = 0, cn = 0;
    if (!sched || !vec(sched + kSchedSeasons, kSchedSeasonStride, kMaxSeasons, sb, sn) || season_index >= sn ||
        !vec(sb + season_index * kSchedSeasonStride + kSchedDrivers, kCalendarStride, kMaxCalendars, cb, cn))
        return -1;
    int best = 0;
    for (size_t c = 0; c < cn; ++c) {
        uintptr_t ab = 0;
        size_t an = 0;
        if (!vec(cb + c * kCalendarStride + kCalendarActivities, kActivitySlot, kMaxActivities, ab, an))
            continue;
        int count = 0;
        for (size_t i = 0; i < an; ++i) {
            uintptr_t act = 0, def = 0;
            uint8_t type = 0;
            if (rd(ab + i * kActivitySlot + sizeof(void *), act) && rd(act + kActivityDef, def) &&
                rd(def + kDefType, type) && type == ce::cal::kRaceWeekend)
                ++count;
        }
        best = std::max(best, count);
    }
    return best > 0 ? best : -1;
}

std::string track_of(const std::string &weekend)
{
    std::string t = weekend;
    if (t.rfind("rw_", 0) == 0)
        t = t.substr(3);
    if (t.size() > 3 && t.compare(t.size() - 3, 3, "_26") == 0)
        t.resize(t.size() - 3);
    return t;
}

// ---------------------------------------------------------------------------
// Standings model reads.

struct Result {
    int round = 0;
    uint32_t session = 0;
    bool race = false;
    int pos = -1;
    int fl = -1;
    uint32_t status = 0;
    bool placed = false;
    bool extra = false;
    int points = 0;
};

struct Driver {
    uint32_t key = 0;
    uint16_t stat = 0;
    int team = -1;
    int points = 0;
    int wins = 0;
    int podiums = 0;
    int position = 0;
    std::array<int, kMaxPos + 1> count{};
    std::vector<Result> results;
};

struct Table {
    std::map<uint16_t, Driver> drivers;
    std::vector<int> sessions_per_round;
    int rounds_scored = 0;
    bool ok = false;
};

template <class Fn> bool walk(uintptr_t tree, size_t rec, Fn &&fn)
{
    uintptr_t rb = 0;
    size_t rn = 0;
    if (!vec(tree, kStRoundStride, kMaxRounds, rb, rn))
        return false;
    for (size_t r = 0; r < rn; ++r) {
        uintptr_t sb = 0;
        size_t sn = 0;
        if (!vec(rb + r * kStRoundStride + kStRoundSessions, kStSessionStride, kMaxSessions, sb, sn))
            return false;
        for (size_t s = 0; s < sn; ++s) {
            uintptr_t se = sb + s * kStSessionStride;
            uint32_t id = 0;
            uintptr_t cb = 0;
            size_t cn = 0;
            if (!rd(se + kStSessionId, id) || !vec(se + kStSessionRecords, rec, kMaxRecs, cb, cn))
                return false;
            for (size_t c = 0; c < cn; ++c)
                fn(static_cast<int>(r), static_cast<int>(s), static_cast<int>(sn), id, cb + c * rec);
        }
    }
    return true;
}

Table read_table(uintptr_t season)
{
    Table t;
    uintptr_t st = season + kSeasonStandings;
    std::map<int, uint32_t> race_session;
    std::map<int, std::vector<uint32_t>> scoring;
    bool ok_r = walk(st + kStResults, kStResultRec, [&](int r, int, int, uint32_t id, uintptr_t at) {
        if (id == 0)
            return;
        auto &v = scoring[r];
        if (std::find(v.begin(), v.end(), id) == v.end())
            v.push_back(id);
        race_session[r] = v.back();
        uint32_t key = 0, status = 0;
        int32_t pos = 0, fl = 0;
        if (!rd(at + offsetof(StandingsResult, key), key) || !rd(at + offsetof(StandingsResult, position), pos) ||
            !rd(at + offsetof(StandingsResult, fastest_lap_rank), fl) || !rd(at + offsetof(StandingsResult, status), status))
            return;
        Driver &d = t.drivers[static_cast<uint16_t>(key >> 16)];
        d.key = key;
        d.stat = static_cast<uint16_t>(key >> 16);
        Result res;
        res.round = r;
        res.session = id;
        res.pos = pos;
        res.fl = fl;
        res.status = status;
        res.placed = pos >= 0 && pos < kMaxPos;
        d.results.push_back(res);
    });
    bool ok_p = walk(st + kStPoints, kStPointRec, [&](int r, int, int, uint32_t id, uintptr_t at) {
        uint32_t key = 0;
        int32_t pts = 0;
        if (!rd(at + offsetof(StandingsPoints, key), key) || !rd(at + offsetof(StandingsPoints, points), pts))
            return;
        Driver &d = t.drivers[static_cast<uint16_t>(key >> 16)];
        if (!d.key) {
            d.key = key;
            d.stat = static_cast<uint16_t>(key >> 16);
        }
        d.points += pts;
        t.rounds_scored = std::max(t.rounds_scored, r + 1);
        for (auto &res : d.results)
            if (res.round == r && res.session == id) {
                res.points += pts;
                return;
            }
        Result extra;
        extra.round = r;
        extra.session = id;
        extra.points = pts;
        extra.extra = true;
        d.results.push_back(extra);
    });
    for (auto &[r, v] : scoring) {
        if (static_cast<size_t>(r) >= t.sessions_per_round.size())
            t.sessions_per_round.resize(static_cast<size_t>(r) + 1, 0);
        t.sessions_per_round[static_cast<size_t>(r)] = static_cast<int>(v.size());
    }
    for (auto &[stat, d] : t.drivers) {
        for (auto &res : d.results) {
            res.race = race_session.count(res.round) && race_session[res.round] == res.session;
            if (!res.race || !res.placed)
                continue;
            if (res.status < 6 && ((0x27u >> res.status) & 1u))
                d.count[static_cast<size_t>(res.pos)]++;
            if (res.status < 2 && res.pos == 0)
                d.wins++;
            if (res.status < 2 && res.pos <= 2)
                d.podiums++;
        }
        std::sort(d.results.begin(), d.results.end(), [](const Result &a, const Result &b) {
            return a.round != b.round ? a.round < b.round : a.race < b.race;
        });
    }
    std::vector<Driver *> order;
    for (auto &[stat, d] : t.drivers)
        order.push_back(&d);
    std::sort(order.begin(), order.end(), [](const Driver *a, const Driver *b) {
        if (a->points != b->points)
            return a->points > b->points;
        for (int p = 0; p < kMaxPos; ++p)
            if (a->count[static_cast<size_t>(p)] != b->count[static_cast<size_t>(p)])
                return a->count[static_cast<size_t>(p)] > b->count[static_cast<size_t>(p)];
        return a->stat < b->stat;
    });
    for (size_t i = 0; i < order.size(); ++i)
        order[i]->position = static_cast<int>(i) + 1;
    t.ok = ok_r && ok_p;
    return t;
}

void team_map(Table &t)
{
    for (auto &e : ce::comp::entrants()) {
        auto it = t.drivers.find(static_cast<uint16_t>(e.driver >> 16));
        if (it != t.drivers.end())
            it->second.team = static_cast<int>(e.team >> 16);
    }
}

void not_ready(ce::Writer &w, const char *reason)
{
    w.begin_object();
    w.key("ready");
    w.boolean(false);
    w.key("reason");
    w.str(reason ? reason : "unknown");
    w.end_object();
}

int base_year(const std::string &name, const std::vector<Round> &rs)
{
    if (name.find("26") != std::string::npos)
        return 2026;
    if (name.find("25") != std::string::npos)
        return 2025;
    for (auto &r : rs)
        if (r.name.size() > 3 && r.name.compare(r.name.size() - 3, 3, "_26") == 0)
            return 2026;
    return 0;
}

// ---------------------------------------------------------------------------
// Points adjustments: sidecar store, injection into the game's rebuild, reconcile.

struct Adj {
    uint16_t stat = 0;
    uint32_t key = 0;
    int32_t points = 0;
};

struct SeasonAdj {
    uint32_t index = 0;
    std::string name;
    std::vector<Adj> drivers;
};

struct Store {
    std::string career;
    std::string file;
    bool loaded = false;
    std::vector<SeasonAdj> seasons;
};

std::mutex g_mu;
Store g_store;
uint64_t g_reconcile_sig = 0;

std::string sidecar_path(const std::string &career)
{
    std::string safe;
    for (char c : career)
        safe += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.') ? c : '_';
    return "careers/" + safe + ".season.json";
}

void store_load(const std::string &career)
{
    if (g_store.loaded && g_store.career == career)
        return;
    g_store = Store{};
    g_store.career = career;
    g_store.loaded = true;
    if (career.empty())
        return;
    g_store.file = sidecar_path(career);
    std::string text;
    if (swofty::storage::read(g_store.file, text) != swofty::storage::Result::ok)
        return;
    ce::Json root;
    if (!json::parse(text, root) || root.type != ce::Json::Obj)
        return;
    for (auto &sv : root["seasons"].a) {
        SeasonAdj sa;
        sa.index = static_cast<uint32_t>(sv["index"].num());
        sa.name = sv["name"].str();
        for (auto &dv : sv["drivers"].a) {
            Adj a;
            a.stat = static_cast<uint16_t>(dv["driver_stat_id"].num());
            a.key = static_cast<uint32_t>(dv["key"].num());
            a.points = static_cast<int32_t>(dv["points"].num());
            if (a.points && sa.drivers.size() < kMaxAdjust)
                sa.drivers.push_back(a);
        }
        if (!sa.drivers.empty())
            g_store.seasons.push_back(sa);
    }
    ce::log("season: loaded %zu season(s) of points adjustments from %s", g_store.seasons.size(), g_store.file.c_str());
}

bool store_save()
{
    if (g_store.file.empty())
        return false;
    ce::Writer w;
    w.begin_object();
    w.key("version");
    w.integer(1);
    w.key("career");
    w.str(g_store.career);
    w.key("seasons");
    w.begin_array();
    for (auto &sa : g_store.seasons) {
        if (sa.drivers.empty())
            continue;
        w.begin_object();
        w.key("index");
        w.integer(sa.index);
        w.key("name");
        w.str(sa.name);
        w.key("drivers");
        w.begin_array();
        for (auto &a : sa.drivers) {
            w.begin_object();
            w.key("driver_stat_id");
            w.integer(a.stat);
            w.key("key");
            w.integer(a.key);
            w.key("points");
            w.integer(a.points);
            w.end_object();
        }
        w.end_array();
        w.end_object();
    }
    w.end_array();
    w.end_object();
    return swofty::storage::write(g_store.file, w.text()) == swofty::storage::Result::ok;
}

SeasonAdj *store_season(uint32_t index, bool create, const std::string &name)
{
    for (auto &sa : g_store.seasons)
        if (sa.index == index)
            return &sa;
    if (!create)
        return nullptr;
    SeasonAdj sa;
    sa.index = index;
    sa.name = name;
    g_store.seasons.push_back(sa);
    return &g_store.seasons.back();
}

std::string season_name(uintptr_t season)
{
    uintptr_t name = 0;
    rd(season + kSeasonName, name);
    return cstr(name, 0x40);
}

bool season_index_of(uintptr_t season, uint32_t &index)
{
    uintptr_t comp = find_championship().champ;
    if (!comp)
        return false;
    uintptr_t b = 0;
    size_t n = 0;
    if (!vec(comp + kChampSeasons, kChampSeasonStride, kMaxSeasons, b, n))
        return false;
    for (size_t i = 0; i < n; ++i) {
        uintptr_t p = 0;
        if (rd(b + i * kChampSeasonStride + kChampSeasonPtr, p) && p == season) {
            index = static_cast<uint32_t>(i);
            return true;
        }
    }
    return false;
}

std::vector<Adj> adjustments_for(uintptr_t season)
{
    std::lock_guard<std::mutex> lock(g_mu);
    store_load(ce::career_id());
    if (g_store.seasons.empty())
        return {};
    uint32_t index = 0;
    if (season_index_of(season, index)) {
        SeasonAdj *sa = store_season(index, false, {});
        return sa ? sa->drivers : std::vector<Adj>{};
    }
    std::string name = season_name(season);
    SeasonAdj *hit = nullptr;
    int matches = 0;
    for (auto &sa : g_store.seasons)
        if (!name.empty() && sa.name == name) {
            hit = &sa;
            ++matches;
        }
    return matches == 1 ? hit->drivers : std::vector<Adj>{};
}


void *alloc_call(void *alloc, uint64_t size, uint64_t align)
{
    uintptr_t vt = 0, fn = 0;
    if (!rd(reinterpret_cast<uintptr_t>(alloc) + kAllocVtable, vt) || !rd(vt + kAllocFnAlloc, fn) || !fn)
        return nullptr;
    return reinterpret_cast<void *(*)(void *, uint64_t, uint64_t)>(fn)(alloc, size, align);
}

void free_call(void *alloc, void *p)
{
    uintptr_t vt = 0, fn = 0;
    if (!p || !rd(reinterpret_cast<uintptr_t>(alloc) + kAllocVtable, vt) || !rd(vt + kAllocFnFree, fn) || !fn)
        return;
    reinterpret_cast<void (*)(void *, void *)>(fn)(alloc, p);
}

std::atomic<int> g_inject_log{0};

}

// Career_Rebuild_SeasonStandings: the only path into the standings init. When the
// sidecar holds adjustments for this season, extra_points is replaced by a list from
// the game's default allocator holding the game's entries plus ours; the callee copies
// it and frees it (alloc vt+0x38), so it is never touched again here. The game's own
// list, if engaged, is then released exactly as the callee would have released it.
// extra_e (constructors side) is passed through.
SWOFTY_WRAP(Career_Rebuild_SeasonStandings, void, void *season, void *order_a, void *order_b, void *extra_points,
            void *extra_e)
{
    static const uint32_t probe = ce::perf_probe("wrap Career_Rebuild_SeasonStandings");
    ce::Timed timed(probe);
    ce::ensure_frame();
    uintptr_t s = reinterpret_cast<uintptr_t>(season);
    std::vector<Adj> add;
    if (has_vtable(s, vt_Career_Season.get()))
        add = adjustments_for(s);
    if (add.empty()) {
        ce::GameCall in_game;
        return orig(season, order_a, order_b, extra_points, extra_e);
    }

    NeOptionalVec *game = static_cast<NeOptionalVec *>(extra_points);
    std::vector<std::pair<uint32_t, int32_t>> merged;
    bool game_engaged = game && game->engaged;
    uintptr_t gb = game_engaged ? reinterpret_cast<uintptr_t>(game->begin) : 0;
    uintptr_t ge = game_engaged ? reinterpret_cast<uintptr_t>(game->end) : 0;
    if (gb && ge > gb && (ge - gb) % kStPointRec == 0 && (ge - gb) / kStPointRec <= 256 && ce::comp::readable(gb, ge - gb)) {
        for (uintptr_t p = gb; p < ge; p += kStPointRec) {
            uint32_t k = 0;
            int32_t v = 0;
            rd(p + offsetof(StandingsPoints, key), k);
            rd(p + offsetof(StandingsPoints, points), v);
            merged.emplace_back(k, v);
        }
    }
    for (auto &a : add) {
        bool hit = false;
        for (auto &m : merged)
            if ((m.first >> 16) == a.stat) {
                m.second += a.points;
                hit = true;
                break;
            }
        if (!hit)
            merged.emplace_back(a.key, a.points);
    }
    void *alloc = nullptr;
    if (g_Ne_DefaultAllocator.get())
        rd(reinterpret_cast<uintptr_t>(g_Ne_DefaultAllocator.get()), alloc);
    void *buf = alloc ? alloc_call(alloc, merged.size() * kStPointRec, alignof(StandingsPoints)) : nullptr;
    if (!buf) {
        ce::log("season: standings rebuild: no allocator for %zu adjustment(s); rebuilt without them", add.size());
        ce::GameCall in_game;
        return orig(season, order_a, order_b, extra_points, extra_e);
    }
    auto *recs = static_cast<StandingsPoints *>(buf);
    for (size_t i = 0; i < merged.size(); ++i) {
        recs[i].key = merged[i].first;
        recs[i].points = merged[i].second;
    }
    NeOptionalVec mine{};
    mine.alloc = static_cast<NeAllocator *>(alloc);
    mine.begin = buf;
    mine.end = recs + merged.size();
    mine.count = merged.size();
    mine.engaged = 1;
    {
        ce::GameCall in_game;
        orig(season, order_a, order_b, &mine, extra_e);
    }
    if (game_engaged) {
        void *b = game->begin;
        game->end = b;
        if (b)
            free_call(game->alloc, b);
        game->begin = nullptr;
        game->end = nullptr;
        game->count = 0;
    }
    if (g_inject_log.fetch_add(1) < 64)
        ce::log("season: standings rebuild of season 0x%llx: %zu adjustment(s) merged into the extra-points list "
                "(%zu entr%s, game list %s)",
                (unsigned long long)s, add.size(), merged.size(), merged.size() == 1 ? "y" : "ies",
                game_engaged ? "engaged" : "empty");
}

namespace {

bool rebuild_championship(uintptr_t champ)
{
    if (!champ || !has_vtable(champ, vt_Career_Championship.get()))
        return false;
    NeOptionalVec none_a{}, none_b{};
    Career_Rebuild_ChampionshipStandings(reinterpret_cast<void *>(champ + kChampList), &none_a, &none_b);
    return true;
}

bool reinit_possible(uintptr_t season)
{
    uintptr_t b = 0, w = 0, sb = 0;
    size_t n = 0, sn = 0;
    return vec(season + kSeasonWeekends, kWeekendPtr, kMaxRounds, b, n) && n > 0 && rd(b, w) &&
           vec(w + kWeekendSessions, kSessionStride, kMaxSessions, sb, sn) && sn > 0;
}

std::map<uint16_t, int32_t> live_extras(uintptr_t season)
{
    std::map<uint16_t, int32_t> out;
    uintptr_t b = 0;
    size_t n = 0;
    if (!vec(season + kSeasonStandings + kStExtra, kStPointRec, 256, b, n))
        return out;
    for (size_t i = 0; i < n; ++i) {
        uint32_t k = 0;
        int32_t v = 0;
        uintptr_t at = b + i * kStPointRec;
        if (rd(at + offsetof(StandingsPoints, key), k) && rd(at + offsetof(StandingsPoints, points), v))
            out[static_cast<uint16_t>(k >> 16)] += v;
    }
    return out;
}

void reconcile(const Where &wh)
{
    if (!wh.champ || !ce::safe() || !reinit_possible(wh.season))
        return;
    std::vector<Adj> want = adjustments_for(wh.season);
    std::map<uint16_t, int32_t> live = live_extras(wh.season);
    bool same = true;
    for (auto &a : want) {
        auto it = live.find(a.stat);
        if (it == live.end() || it->second != a.points) {
            same = false;
            break;
        }
    }
    if (same)
        return;
    uint64_t sig = wh.season ^ (static_cast<uint64_t>(want.size()) << 48);
    for (auto &a : want)
        sig = sig * 0x100000001b3ull ^ (static_cast<uint64_t>(a.stat) << 32 | static_cast<uint32_t>(a.points));
    if (sig == g_reconcile_sig)
        return;
    g_reconcile_sig = sig;
    ce::log("season: %zu stored adjustment(s) missing from the live standings of season %u; running the game's "
            "standings rebuild",
            want.size(), wh.index + 1);
    rebuild_championship(wh.champ);
}

void write_adjustments(ce::Writer &w, const Where &wh)
{
    std::vector<Adj> list = adjustments_for(wh.season);
    std::map<uint16_t, int32_t> live = live_extras(wh.season);
    bool can = reinit_possible(wh.season);
    w.key("adjustments_pending");
    w.boolean(!can && !list.empty());
    w.key("adjustments");
    w.begin_array();
    for (auto &a : list) {
        w.begin_object();
        w.key("driver_stat_id");
        w.integer(a.stat);
        w.key("key");
        w.integer(a.key);
        w.key("points");
        w.integer(a.points);
        w.key("live");
        w.boolean(live.count(a.stat) != 0);
        w.key("pending");
        w.boolean(!can || live.count(a.stat) == 0);
        w.end_object();
    }
    w.end_array();
    w.key("career_id");
    std::string id = ce::career_id();
    if (id.empty())
        w.null();
    else
        w.str(id);
}

// ---------------------------------------------------------------------------
// Snapshot sections.

void section_season(ce::Writer &w)
{
    Where wh;
    if (!locate(wh))
        return not_ready(w, wh.reason);
    std::vector<Round> rs;
    uint32_t cur = 0;
    if (!rounds(wh.season, rs, cur))
        return not_ready(w, "bad_calendar");
    uintptr_t name = 0;
    rd(wh.season + kSeasonName, name);
    std::string sname = cstr(name, 0x40);
    uint32_t champ_completion = 0, season_completion = 0, minutes = 0;
    if (wh.champ)
        rd(wh.champ + kChampCompletion, champ_completion);
    rd(wh.season + kSeasonCompletion, season_completion);
    uintptr_t ct = ce::comp::get(ce::comp::Id::CareerTime);
    bool have_time = ct && rd(ct + kCareerTimeMinutes, minutes);

    w.begin_object();
    w.key("ready");
    w.boolean(true);
    w.key("season_index");
    w.integer(wh.index);
    w.key("season_number");
    w.integer(wh.index + 1);
    w.key("seasons");
    w.integer(wh.count);
    w.key("name");
    w.str(sname);
    w.key("year");
    if (int y = base_year(sname, rs))
        w.integer(y + static_cast<int>(wh.index));
    else
        w.null();
    w.key("round");
    w.integer(static_cast<int64_t>(cur) + 1);
    w.key("rounds");
    w.integer(static_cast<int64_t>(rs.size()));
    w.key("weekend");
    w.str(cur < rs.size() ? rs[cur].name : std::string());
    int stops = race_weekends(wh.index);
    w.key("race_weekends");
    if (stops > 0)
        w.integer(stops);
    else
        w.null();
    w.key("completion");
    w.integer(champ_completion);
    w.key("season_completion");
    w.integer(season_completion & 0xff);
    w.key("current_minutes");
    if (have_time)
        w.integer(minutes);
    else
        w.null();
    w.key("day");
    if (have_time)
        w.integer(minutes / 1440);
    else
        w.null();
    w.end_object();
}

void section_standings(ce::Writer &w)
{
    Where wh;
    if (!locate(wh))
        return not_ready(w, wh.reason);
    reconcile(wh);
    Table t = read_table(wh.season);
    if (!t.ok)
        return not_ready(w, "bad_standings");
    team_map(t);

    struct Team {
        int stat = 0;
        int points = 0;
        int wins = 0;
        int best = kMaxPos;
        int position = 0;
    };
    std::map<int, Team> teams;
    for (auto &[stat, d] : t.drivers) {
        if (d.team < 0)
            continue;
        Team &tm = teams[d.team];
        tm.stat = d.team;
        tm.points += d.points;
        tm.wins += d.wins;
        tm.best = std::min(tm.best, d.position);
    }
    std::vector<Team *> torder;
    for (auto &[k, tm] : teams)
        torder.push_back(&tm);
    std::sort(torder.begin(), torder.end(), [](const Team *a, const Team *b) {
        if (a->points != b->points)
            return a->points > b->points;
        if (a->wins != b->wins)
            return a->wins > b->wins;
        return a->best < b->best;
    });
    for (size_t i = 0; i < torder.size(); ++i)
        torder[i]->position = static_cast<int>(i) + 1;

    std::vector<const Driver *> dorder;
    for (auto &[stat, d] : t.drivers)
        dorder.push_back(&d);
    std::sort(dorder.begin(), dorder.end(), [](const Driver *a, const Driver *b) { return a->position < b->position; });

    w.begin_object();
    w.key("ready");
    w.boolean(true);
    w.key("season_index");
    w.integer(wh.index);
    w.key("rounds_scored");
    w.integer(t.rounds_scored);
    w.key("drivers");
    w.begin_array();
    for (const Driver *d : dorder) {
        w.begin_object();
        w.key("key");
        w.integer(d->key);
        w.key("driver_stat_id");
        w.integer(d->stat);
        w.key("team_stat_id");
        if (d->team >= 0)
            w.integer(d->team);
        else
            w.null();
        w.key("position");
        w.integer(d->position);
        w.key("points");
        w.integer(d->points);
        w.key("wins");
        w.integer(d->wins);
        w.key("podiums");
        w.integer(d->podiums);
        w.key("results");
        w.begin_array();
        for (const Result &r : d->results) {
            w.begin_object();
            w.key("round");
            w.integer(r.round + 1);
            w.key("session");
            w.integer(r.session);
            w.key("race");
            w.boolean(r.race);
            w.key("pos");
            if (r.placed)
                w.integer(r.pos + 1);
            else
                w.null();
            w.key("status");
            w.integer(r.status);
            w.key("scored");
            w.boolean(r.placed && r.status < 2);
            w.key("fastest_lap");
            w.boolean(r.placed && r.fl == 0);
            w.key("adjustment");
            w.boolean(r.extra);
            w.key("points");
            w.integer(r.points);
            w.end_object();
        }
        w.end_array();
        w.end_object();
    }
    w.end_array();
    write_adjustments(w, wh);
    w.key("teams");
    w.begin_array();
    for (const Team *tm : torder) {
        w.begin_object();
        w.key("team_stat_id");
        w.integer(tm->stat);
        w.key("position");
        w.integer(tm->position);
        w.key("points");
        w.integer(tm->points);
        w.key("wins");
        w.integer(tm->wins);
        w.end_object();
    }
    w.end_array();
    w.end_object();
}

void section_calendar(ce::Writer &w)
{
    Where wh;
    if (!locate(wh))
        return not_ready(w, wh.reason);
    std::vector<Round> rs;
    uint32_t cur = 0;
    if (!rounds(wh.season, rs, cur))
        return not_ready(w, "bad_calendar");
    Table t = read_table(wh.season);

    w.begin_object();
    w.key("ready");
    w.boolean(true);
    w.key("season_index");
    w.integer(wh.index);
    w.key("current_round");
    w.integer(static_cast<int64_t>(cur) + 1);
    w.key("editable_from");
    if (static_cast<size_t>(cur) + 1 < rs.size())
        w.integer(static_cast<int64_t>(cur) + 2);
    else
        w.null();
    int stops = race_weekends(wh.index);
    w.key("race_weekends");
    if (stops > 0)
        w.integer(stops);
    else
        w.null();
    w.key("round_count_ok");
    if (stops > 0)
        w.boolean(static_cast<size_t>(stops) == rs.size());
    else
        w.null();
    w.key("rounds");
    w.begin_array();
    for (size_t i = 0; i < rs.size(); ++i) {
        w.begin_object();
        w.key("round");
        w.integer(static_cast<int64_t>(i) + 1);
        w.key("weekend");
        w.str(rs[i].name);
        w.key("track");
        w.str(track_of(rs[i].name));
        uint8_t track = kNoTrack;
        w.key("track_stat_id");
        if (rd(rs[i].weekend + kWeekendTrack, track) && track != kNoTrack)
            w.integer(track);
        else
            w.null();
        w.key("completed");
        w.boolean(i < cur);
        w.key("current");
        w.boolean(i == cur);
        uint32_t state = 0;
        rd(rs[i].weekend + kWeekendState, state);
        w.key("state");
        w.integer(state);
        w.key("editable");
        w.boolean(i > cur && state == 0);
        w.key("sprint");
        int scoring = t.ok && i < t.sessions_per_round.size() ? t.sessions_per_round[i] : 0;
        if (scoring >= 2)
            w.boolean(true);
        else if (scoring == 1)
            w.boolean(false);
        else
            w.null();
        w.key("sessions");
        w.begin_array();
        uintptr_t sb = 0;
        size_t sn = 0;
        if (vec(rs[i].weekend + kWeekendSessions, kSessionStride, kMaxSessions, sb, sn)) {
            for (size_t s = 0; s < sn; ++s) {
                uint32_t id = 0;
                uint32_t done = 0;
                if (!rd(sb + s * kSessionStride + kSessionId, id) || !rd(sb + s * kSessionStride + kSessionCompletion, done))
                    break;
                w.begin_object();
                w.key("session");
                w.integer(id);
                w.key("completion");
                w.integer(done);
                w.end_object();
            }
        }
        w.end_array();
        w.end_object();
    }
    w.end_array();
    w.end_object();
}

// ---------------------------------------------------------------------------
// Ops.

std::string rounds_json(const std::vector<Round> &rs)
{
    ce::Writer w;
    w.begin_object();
    w.key("rounds");
    w.begin_array();
    for (auto &r : rs)
        w.str(r.name);
    w.end_array();
    w.key("tracks");
    w.begin_array();
    for (auto &r : rs) {
        uint8_t t = kNoTrack;
        if (rd(r.weekend + kWeekendTrack, t) && t != kNoTrack)
            w.integer(t);
        else
            w.null();
    }
    w.end_array();
    w.end_object();
    return w.text();
}

ce::OpResult fail(const char *error)
{
    ce::OpResult r;
    r.error = error;
    return r;
}

ce::OpResult unsupported(const char *op, const char *detail)
{
    ce::OpResult r;
    r.error = "unsupported";
    ce::Writer w;
    w.begin_object();
    w.key("detail");
    w.str(detail);
    w.end_object();
    r.after = w.text();
    ce::log("%s refused: unsupported (%s)", op, detail);
    return r;
}

// season.calendar.reorder: permutes the Season's own weekend vector for the rounds after
// the current one. The serialiser writes weekend_names[] in vector order and the season's
// name map is keyed by each weekend's own name, so only the order changes.
ce::OpResult op_calendar_reorder(const ce::Json &args)
{
    Where wh;
    if (!locate(wh))
        return fail("not_ready");
    const ce::Json &order = args["order"];
    if (order.type != ce::Json::Arr)
        return fail("bad_args");
    std::vector<Round> rs;
    uint32_t cur = 0;
    if (!rounds(wh.season, rs, cur))
        return fail("not_ready");
    size_t first = static_cast<size_t>(cur) + 1;
    if (first >= rs.size() || order.a.size() != rs.size() - first)
        return fail(first >= rs.size() ? "not_future" : "bad_args");
    std::vector<uintptr_t> next;
    std::vector<bool> used(rs.size(), false);
    for (auto &v : order.a) {
        if (v.type != ce::Json::Str)
            return fail("bad_args");
        size_t hit = rs.size();
        for (size_t i = 0; i < rs.size(); ++i)
            if (!used[i] && rs[i].name == v.s) {
                hit = i;
                break;
            }
        if (hit == rs.size())
            return fail("bad_args");
        uint32_t state = 0;
        if (hit < first || !rd(rs[hit].weekend + kWeekendState, state) || state != 0)
            return fail("not_future");
        used[hit] = true;
        next.push_back(rs[hit].weekend);
    }
    std::string before = rounds_json(rs);
    uintptr_t b = 0;
    size_t n = 0;
    if (!vec(wh.season + kSeasonWeekends, kWeekendPtr, kMaxRounds, b, n) || n != rs.size())
        return fail("game_refused");
    for (size_t i = 0; i < next.size(); ++i) {
        uintptr_t slot = b + (first + i) * kWeekendPtr;
        std::memcpy(reinterpret_cast<void *>(slot), &next[i], sizeof(uintptr_t));
    }
    std::vector<Round> back;
    uint32_t cur2 = 0;
    if (!rounds(wh.season, back, cur2))
        return fail("game_refused");
    ce::OpResult r;
    r.ok = true;
    r.before = before;
    r.after = rounds_json(back);
    ce::log("season.calendar.reorder season %u rounds %zu..%zu %s -> %s", wh.index + 1, first + 1,
            rs.size(), r.before.c_str(), r.after.c_str());
    return r;
}

// The game names the weekends it builds from a track set by their index ("0", "1", ...,
// Career_Create_SeasonWeekend's "%I64u"), as in a Driver Career. Such a name says nothing
// about the track, so it is accepted only with an explicit track id.
bool index_weekend_name(const std::string &n)
{
    return !n.empty() && n.size() <= 3 &&
           std::all_of(n.begin(), n.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
}

bool valid_weekend_name(const std::string &n)
{
    if (index_weekend_name(n))
        return true;
    if (n.size() < 3 || n.size() > 31)
        return false;
    for (char c : n)
        if (!(std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '_'))
            return false;
    return true;
}

bool known_weekend(uintptr_t champ, const std::string &n, uint8_t &track)
{
    track = kNoTrack;
    uintptr_t b = 0;
    size_t count = 0;
    if (!champ || !vec(champ + kChampSeasons, kChampSeasonStride, kMaxSeasons, b, count))
        return false;
    for (size_t i = 0; i < count; ++i) {
        uintptr_t season = 0;
        if (!rd(b + i * kChampSeasonStride + kChampSeasonPtr, season) || !has_vtable(season, vt_Career_Season.get()))
            continue;
        std::vector<Round> rs;
        uint32_t cur = 0;
        if (!rounds(season, rs, cur))
            continue;
        for (auto &r : rs) {
            uint8_t t = kNoTrack;
            if (r.name == n && rd(r.weekend + kWeekendTrack, t) && t != kNoTrack) {
                track = t;
                return true;
            }
        }
    }
    return false;
}

const char *check_weekend(const ce::Json &args, uintptr_t champ, const std::string &name, const std::vector<Round> &rs,
                          uint8_t &track)
{
    track = kNoTrack;
    if (!valid_weekend_name(name))
        return "bad_args";
    for (auto &r : rs)
        if (r.name == name)
            return "duplicate_weekend";
    if (!index_weekend_name(name) && known_weekend(champ, name, track))
        return nullptr;
    const ce::Json &v = args["verified"];
    if (!(v.type == ce::Json::Bool && v.b))
        return "unknown_weekend";
    long long t = args["track_stat_id"].num(-1);
    if (t < 0 || t >= kNoTrack)
        return "unknown_track";
    track = static_cast<uint8_t>(t);
    return nullptr;
}

bool future_round(const std::vector<Round> &rs, uint32_t cur, size_t idx)
{
    uint32_t state = 0;
    return idx > cur && idx < rs.size() && rd(rs[idx].weekend + kWeekendState, state) && state == 0;
}

void destroy_weekend(uintptr_t season, uintptr_t w)
{
    uintptr_t vt = 0, dtor = 0, alloc = 0;
    if (rd(w, vt) && rd(vt + kDtorSlot * sizeof(void *), dtor) && dtor)
        reinterpret_cast<void (*)(void *, int)>(dtor)(reinterpret_cast<void *>(w), 0);
    if (rd(season + kSeasonAlloc, alloc) && alloc)
        free_call(reinterpret_cast<void *>(alloc), reinterpret_cast<void *>(w));
}

bool remove_at(uintptr_t season, size_t idx)
{
    uintptr_t b = 0;
    size_t n = 0;
    uintptr_t w = 0;
    if (!vec(season + kSeasonWeekends, kWeekendPtr, kMaxRounds, b, n) || idx >= n || !rd(b + idx * kWeekendPtr, w) ||
        !has_vtable(w, vt_Career_Weekend.get()))
        return false;
    Career_Teardown_Weekend(reinterpret_cast<void *>(w));
    Ne_Detach_ChildNode(reinterpret_cast<void *>(season), reinterpret_cast<void *>(w));
    if (!vec(season + kSeasonWeekends, kWeekendPtr, kMaxRounds, b, n))
        return false;
    size_t j = n;
    for (size_t i = 0; i < n; ++i) {
        uintptr_t p = 0;
        if (rd(b + i * kWeekendPtr, p) && p == w) {
            j = i;
            break;
        }
    }
    if (j == n)
        return false;
    if (j + 1 < n)
        std::memmove(reinterpret_cast<void *>(b + j * kWeekendPtr), reinterpret_cast<void *>(b + (j + 1) * kWeekendPtr),
                     (n - j - 1) * kWeekendPtr);
    uintptr_t end = b + (n - 1) * kWeekendPtr;
    std::memcpy(reinterpret_cast<void *>(season + kSeasonWeekendsEnd), &end, sizeof end);
    destroy_weekend(season, w);
    return true;
}

bool insert_at(uintptr_t season, size_t idx, const std::string &name, uint8_t track, uint64_t &rc)
{
    rc = 0;
    if (track == kNoTrack)
        return false;
    void *w = Career_Create_SeasonWeekend(reinterpret_cast<void *>(season), 0, track, name.c_str());
    if (!w)
        return false;
    rc = Career_Add_SeasonWeekend(reinterpret_cast<void *>(season), w);
    if (rc != 0) {
        destroy_weekend(season, reinterpret_cast<uintptr_t>(w));
        return false;
    }
    uintptr_t b = 0;
    size_t n = 0;
    if (!vec(season + kSeasonWeekends, kWeekendPtr, kMaxRounds, b, n) || n == 0)
        return false;
    uintptr_t last = 0;
    rd(b + (n - 1) * kWeekendPtr, last);
    if (last != reinterpret_cast<uintptr_t>(w))
        return false;
    if (idx < n - 1) {
        std::memmove(reinterpret_cast<void *>(b + (idx + 1) * kWeekendPtr), reinterpret_cast<void *>(b + idx * kWeekendPtr),
                     (n - 1 - idx) * kWeekendPtr);
        std::memcpy(reinterpret_cast<void *>(b + idx * kWeekendPtr), &last, sizeof last);
    }
    return true;
}

struct CalCtx {
    Where wh;
    std::vector<Round> rs;
    uint32_t cur = 0;
};

const char *cal_ctx(CalCtx &c)
{
    if (!locate(c.wh) || !c.wh.champ)
        return "not_ready";
    if (!rounds(c.wh.season, c.rs, c.cur))
        return "not_ready";
    return nullptr;
}

ce::OpResult cal_done(const char *op, const CalCtx &c, const std::string &before, const char *what)
{
    std::vector<Round> back;
    uint32_t cur2 = 0;
    ce::OpResult r;
    if (!rounds(c.wh.season, back, cur2))
        return fail("game_refused");
    r.ok = true;
    r.before = before;
    r.after = rounds_json(back);
    ce::log("%s season %u %s %s -> %s", op, c.wh.index + 1, what, r.before.c_str(), r.after.c_str());
    return r;
}

ce::OpResult count_refused(const char *op, const CalCtx &c, int stops, const std::string &rule)
{
    ce::OpResult r;
    r.error = rule;
    size_t colon = rule.find(':');
    std::string detail = colon == std::string::npos ? std::string() : rule.substr(colon + 2);
    ce::Writer w;
    w.begin_object();
    w.key("rounds");
    w.integer(static_cast<int64_t>(c.rs.size()));
    w.key("race_weekends");
    if (stops > 0)
        w.integer(stops);
    else
        w.null();
    w.key("detail");
    w.str(detail);
    w.end_object();
    r.after = w.text();
    ce::log("%s refused: %s (season %u, %zu rounds, %d race weekend(s) on the game's calendar)", op, rule.c_str(),
            c.wh.index + 1, c.rs.size(), stops);
    return r;
}

std::vector<ce::cal::Candidate> known_weekends(uintptr_t champ, uint32_t index)
{
    std::vector<ce::cal::Candidate> out;
    uintptr_t b = 0;
    size_t n = 0;
    if (!champ || !vec(champ + kChampSeasons, kChampSeasonStride, kMaxSeasons, b, n))
        return out;
    std::vector<size_t> order;
    for (size_t i = index; i-- > 0;)
        order.push_back(i);
    for (size_t i = static_cast<size_t>(index) + 1; i < n; ++i)
        order.push_back(i);
    for (size_t i : order) {
        uintptr_t season = 0;
        if (!rd(b + i * kChampSeasonStride + kChampSeasonPtr, season) || !has_vtable(season, vt_Career_Season.get()))
            continue;
        std::vector<Round> rs;
        uint32_t cur = 0;
        if (!rounds(season, rs, cur))
            continue;
        for (auto &r : rs) {
            uint8_t t = kNoTrack;
            if (rd(r.weekend + kWeekendTrack, t) && t != kNoTrack && valid_weekend_name(r.name))
                out.push_back({r.name, t});
        }
    }
    return out;
}

ce::OpResult op_calendar_add_round(const ce::Json &args)
{
    CalCtx c;
    if (const char *e = cal_ctx(c))
        return fail(e);
    std::string name = args["weekend"].str();
    long long at = args["at"].type == ce::Json::Null ? static_cast<long long>(c.rs.size()) + 1 : args["at"].num(-1);
    if (at < 1 || static_cast<size_t>(at) > c.rs.size() + 1 || c.rs.size() >= kMaxRounds)
        return fail("bad_args");
    size_t idx = static_cast<size_t>(at - 1);
    if (idx <= c.cur)
        return fail("not_future");
    if (idx < c.rs.size()) {
        uint32_t state = 0;
        if (!rd(c.rs[idx].weekend + kWeekendState, state) || state != 0)
            return fail("not_future");
    }
    int stops = race_weekends(c.wh.index);
    std::string rule = ce::cal::count_change(static_cast<int>(c.rs.size()), static_cast<int>(c.rs.size()) + 1, stops);
    if (!rule.empty())
        return count_refused("season.calendar.add_round", c, stops, rule);
    uint8_t track = kNoTrack;
    if (const char *e = check_weekend(args, c.wh.champ, name, c.rs, track))
        return fail(e);
    std::string before = rounds_json(c.rs);
    uint64_t rc = 0;
    if (!insert_at(c.wh.season, idx, name, track, rc)) {
        ce::log("season.calendar.add_round \"%s\" track %u at %lld refused by the game (code 0x%llx)", name.c_str(),
                track, at, (unsigned long long)rc);
        return fail("game_refused");
    }
    char what[112];
    std::snprintf(what, sizeof what, "add \"%s\" (track %u) at round %lld", name.c_str(), track, at);
    return cal_done("season.calendar.add_round", c, before, what);
}

ce::OpResult op_calendar_remove_round(const ce::Json &args)
{
    CalCtx c;
    if (const char *e = cal_ctx(c))
        return fail(e);
    long long round = args["round"].num(-1);
    if (round < 1 || static_cast<size_t>(round) > c.rs.size())
        return fail("bad_args");
    size_t idx = static_cast<size_t>(round - 1);
    if (!future_round(c.rs, c.cur, idx))
        return fail("not_future");
    int stops = race_weekends(c.wh.index);
    std::string rule = ce::cal::count_change(static_cast<int>(c.rs.size()), static_cast<int>(c.rs.size()) - 1, stops);
    if (!rule.empty())
        return count_refused("season.calendar.remove_round", c, stops, rule);
    std::string before = rounds_json(c.rs);
    std::string name = c.rs[idx].name;
    uint8_t track = kNoTrack;
    rd(c.rs[idx].weekend + kWeekendTrack, track);
    if (!remove_at(c.wh.season, idx))
        return fail("game_refused");
    char what[112];
    std::snprintf(what, sizeof what, "remove round %lld \"%s\" (track %u)", round, name.c_str(), track);
    return cal_done("season.calendar.remove_round", c, before, what);
}

ce::OpResult op_calendar_set_round(const ce::Json &args)
{
    CalCtx c;
    if (const char *e = cal_ctx(c))
        return fail(e);
    long long round = args["round"].num(-1);
    std::string name = args["weekend"].str();
    if (round < 1 || static_cast<size_t>(round) > c.rs.size())
        return fail("bad_args");
    size_t idx = static_cast<size_t>(round - 1);
    if (!future_round(c.rs, c.cur, idx))
        return fail("not_future");
    std::string old = c.rs[idx].name;
    uint8_t old_track = kNoTrack;
    rd(c.rs[idx].weekend + kWeekendTrack, old_track);
    if (old == name)
        return fail("bad_args");
    if (old_track == kNoTrack)
        return fail("unknown_track");
    uint8_t track = kNoTrack;
    if (const char *e = check_weekend(args, c.wh.champ, name, c.rs, track))
        return fail(e);
    std::string before = rounds_json(c.rs);
    if (!remove_at(c.wh.season, idx))
        return fail("game_refused");
    uint64_t rc = 0;
    if (!insert_at(c.wh.season, idx, name, track, rc)) {
        uint64_t rc2 = 0;
        bool restored = insert_at(c.wh.season, idx, old, old_track, rc2);
        ce::log("season.calendar.set_round round %lld \"%s\" refused by the game (code 0x%llx); \"%s\" %s", round,
                name.c_str(), (unsigned long long)rc, old.c_str(), restored ? "restored" : "NOT restored");
        return fail("game_refused");
    }
    char what[144];
    std::snprintf(what, sizeof what, "round %lld \"%s\" (track %u) -> \"%s\" (track %u)", round, old.c_str(), old_track,
                  name.c_str(), track);
    return cal_done("season.calendar.set_round", c, before, what);
}

// season.calendar.repair: brings the round count back to the race weekends on the game's calendar, for
// careers edited before add_round / remove_round kept it. Extra rounds: the last future, not-started
// ones go. Missing rounds: the given weekends, or tracks of this championship's other seasons that are
// not on this calendar, become new last rounds.
ce::OpResult op_calendar_repair(const ce::Json &args)
{
    CalCtx c;
    if (const char *e = cal_ctx(c))
        return fail(e);
    int stops = race_weekends(c.wh.index);
    std::vector<ce::cal::RoundState> states;
    for (auto &r : c.rs) {
        ce::cal::RoundState st;
        st.name = r.name;
        rd(r.weekend + kWeekendTrack, st.track);
        uint32_t state = 0;
        st.not_started = rd(r.weekend + kWeekendState, state) && state == 0;
        states.push_back(st);
    }
    std::vector<ce::cal::Candidate> given;
    const ce::Json &list = args["weekends"];
    if (list.type == ce::Json::Arr) {
        for (auto &v : list.a) {
            std::string name = v["weekend"].str();
            uint8_t track = kNoTrack;
            if (const char *e = check_weekend(v, c.wh.champ, name, c.rs, track))
                return fail(e);
            given.push_back({name, track});
        }
    } else if (list.type != ce::Json::Null) {
        return fail("bad_args");
    }
    ce::cal::Repair plan = ce::cal::repair(states, c.cur, stops, given, known_weekends(c.wh.champ, c.wh.index));
    if (!plan.error.empty())
        return count_refused("season.calendar.repair", c, stops, plan.error);
    std::string before = rounds_json(c.rs);
    if (plan.unchanged) {
        char what[96];
        std::snprintf(what, sizeof what, "%zu rounds match %d race weekend(s), nothing to do", c.rs.size(), stops);
        return cal_done("season.calendar.repair", c, before, what);
    }
    std::string done;
    for (size_t idx : plan.remove) {
        std::string name = c.rs[idx].name;
        if (!remove_at(c.wh.season, idx)) {
            ce::log("season.calendar.repair: removing round %zu \"%s\" refused by the game%s", idx + 1, name.c_str(),
                    done.c_str());
            return fail("game_refused");
        }
        done += " removed round " + std::to_string(idx + 1) + " \"" + name + "\";";
    }
    for (auto &a : plan.add) {
        uintptr_t b = 0;
        size_t n = 0;
        uint64_t rc = 0;
        if (!vec(c.wh.season + kSeasonWeekends, kWeekendPtr, kMaxRounds, b, n) ||
            !insert_at(c.wh.season, n, a.name, a.track, rc)) {
            ce::log("season.calendar.repair: adding \"%s\" (track %u) refused by the game (code 0x%llx);%s",
                    a.name.c_str(), a.track, (unsigned long long)rc, done.c_str());
            return fail("game_refused");
        }
        done += " added \"" + a.name + "\" (track " + std::to_string(a.track) + ") as round " + std::to_string(n + 1) +
                ";";
    }
    std::string what = std::to_string(c.rs.size()) + " rounds -> " + std::to_string(stops) + " (race weekends on the " +
                       "game's calendar):" + done;
    return cal_done("season.calendar.repair", c, before, what.c_str());
}

ce::OpResult op_calendar_toggle_sprint(const ce::Json &)
{
    return unsupported("season.calendar.toggle_sprint",
                       "a loaded career keeps weekend names only; the format comes from the gamedb track list "
                       "(new careers: DB preset)");
}

uint32_t key_for(uintptr_t season, uint16_t stat, const Table &t)
{
    auto it = t.drivers.find(stat);
    if (it != t.drivers.end() && it->second.key)
        return it->second.key;
    std::vector<Round> rs;
    uint32_t cur = 0;
    if (!rounds(season, rs, cur))
        return 0;
    for (auto &r : rs) {
        uintptr_t sb = 0;
        size_t sn = 0;
        if (!vec(r.weekend + kWeekendSessions, kSessionStride, kMaxSessions, sb, sn))
            continue;
        for (size_t s = 0; s < sn; ++s) {
            uintptr_t pb = 0;
            size_t pn = 0;
            if (!vec(sb + s * kSessionStride + kSessionParticipants, kEntrantStride, kMaxRecs, pb, pn))
                continue;
            for (size_t i = 0; i < pn; ++i) {
                uint32_t k = 0;
                if (rd(pb + i * kEntrantStride + kEntrantKey, k) && (k >> 16) == stat)
                    return k;
            }
        }
    }
    return 0;
}

std::string driver_json(uint16_t stat, uint32_t key, int points, int adjustment, bool pending = false)
{
    ce::Writer w;
    w.begin_object();
    w.key("driver_stat_id");
    w.integer(stat);
    w.key("key");
    w.integer(key);
    w.key("points");
    w.integer(points);
    w.key("adjustment");
    w.integer(adjustment);
    w.key("pending");
    w.boolean(pending);
    w.end_object();
    return w.text();
}

int stored_adjustment(uint32_t index, uint16_t stat)
{
    SeasonAdj *sa = store_season(index, false, {});
    if (!sa)
        return 0;
    for (auto &a : sa->drivers)
        if (a.stat == stat)
            return a.points;
    return 0;
}

void set_adjustment(uint32_t index, const std::string &name, uint16_t stat, uint32_t key, int value)
{
    SeasonAdj *sa = store_season(index, true, name);
    for (size_t i = 0; i < sa->drivers.size(); ++i)
        if (sa->drivers[i].stat == stat) {
            if (value)
                sa->drivers[i].points = value, sa->drivers[i].key = key;
            else
                sa->drivers.erase(sa->drivers.begin() + static_cast<long>(i));
            return;
        }
    if (value) {
        Adj a;
        a.stat = stat;
        a.key = key;
        a.points = value;
        sa->drivers.push_back(a);
    }
}

int points_of(const Table &t, uint16_t stat)
{
    auto it = t.drivers.find(stat);
    return it == t.drivers.end() ? 0 : it->second.points;
}

// season.points.adjust: stores the driver's adjustment in the sidecar, then runs the
// game's championship standings rebuild, whose season step merges it (WRAP above).
ce::OpResult op_points_adjust(const ce::Json &args)
{
    if (args["team"].type != ce::Json::Null)
        return unsupported("season.points.adjust",
                           "the game's extra-points list is per driver; constructors follow their drivers");
    std::string career = ce::career_id();
    if (career.empty())
        return fail("no_career_id");
    Where wh;
    if (!locate(wh) || !wh.champ)
        return fail("not_ready");
    long long drv = args["driver"].num(-1);
    long long delta = args["delta"].num(0);
    if (drv <= 0 || drv > 0xffffffffll || delta == 0 || delta < -kMaxDelta || delta > kMaxDelta)
        return fail("bad_args");
    uint16_t stat = static_cast<uint16_t>(drv > 0xffff ? (static_cast<uint32_t>(drv) >> 16) : static_cast<uint32_t>(drv));
    Table t = read_table(wh.season);
    if (!t.ok)
        return fail("not_ready");
    uint32_t key = key_for(wh.season, stat, t);
    if (!key)
        return fail("no_driver");
    std::map<uint16_t, int32_t> live = live_extras(wh.season);
    int before_pts = points_of(t, stat);
    int live_extra = live.count(stat) ? live[stat] : 0;
    int before_adj = 0, after_adj = 0;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        store_load(career);
        before_adj = stored_adjustment(wh.index, stat);
        after_adj = before_adj + static_cast<int>(delta);
        if (after_adj < -1000 || after_adj > 1000)
            return fail("bad_args");
        set_adjustment(wh.index, season_name(wh.season), stat, key, after_adj);
        if (!store_save()) {
            set_adjustment(wh.index, season_name(wh.season), stat, key, before_adj);
            return fail("storage_failed");
        }
    }
    bool pending = !reinit_possible(wh.season);
    if (!pending)
        rebuild_championship(wh.champ);
    Table t2 = read_table(wh.season);
    int after_pts = points_of(t2, stat);
    int expected = before_pts - live_extra + after_adj;
    ce::OpResult r;
    r.before = driver_json(stat, key, before_pts, before_adj);
    r.after = driver_json(stat, key, after_pts, after_adj, pending);
    r.ok = pending || (t2.ok && after_pts == expected);
    ce::log("season.points.adjust season %u driver %u key 0x%x delta %+lld: points %d (adj %d) -> %d (adj %d)%s",
            wh.index + 1, stat, key, delta, before_pts, before_adj, after_pts, after_adj,
            pending ? " PENDING (round 1 has no sessions yet; applies at the next standings rebuild)"
            : r.ok  ? ""
                    : " MISMATCH (expected a different total)");
    if (!r.ok)
        r.error = "game_refused";
    return r;
}

ce::OpResult op_points_clear(const ce::Json &args)
{
    std::string career = ce::career_id();
    if (career.empty())
        return fail("no_career_id");
    Where wh;
    if (!locate(wh) || !wh.champ)
        return fail("not_ready");
    long long drv = args["driver"].num(0);
    uint16_t stat = static_cast<uint16_t>(drv > 0xffff ? (static_cast<uint32_t>(drv) >> 16) : static_cast<uint32_t>(drv));
    std::vector<Adj> before;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        store_load(career);
        SeasonAdj *sa = store_season(wh.index, false, {});
        if (sa) {
            before = sa->drivers;
            if (drv > 0)
                std::erase_if(sa->drivers, [&](const Adj &a) { return a.stat == stat; });
            else
                sa->drivers.clear();
        }
        if (!store_save()) {
            if (sa)
                sa->drivers = before;
            return fail("storage_failed");
        }
    }
    rebuild_championship(wh.champ);
    auto list_json = [](const std::vector<Adj> &v) {
        ce::Writer w;
        w.begin_object();
        w.key("adjustments");
        w.begin_array();
        for (auto &a : v) {
            w.begin_object();
            w.key("driver_stat_id");
            w.integer(a.stat);
            w.key("key");
            w.integer(a.key);
            w.key("points");
            w.integer(a.points);
            w.end_object();
        }
        w.end_array();
        w.end_object();
        return w.text();
    };
    ce::OpResult r;
    r.ok = true;
    r.before = list_json(before);
    r.after = list_json(adjustments_for(wh.season));
    ce::log("season.points.clear season %u driver %u: %s -> %s", wh.index + 1, stat, r.before.c_str(), r.after.c_str());
    return r;
}

ce::OpResult op_result_set(const ce::Json &)
{
    return unsupported("season.result.set", "the saved session results_list layout is not bound (phase 3)");
}

} // namespace

CE_SECTION("season", section_season);
CE_SECTION("standings", section_standings);
CE_SECTION("calendar", section_calendar);

CE_OP("season.calendar.reorder", op_calendar_reorder);
CE_OP("season.calendar.set_round", op_calendar_set_round);
CE_OP("season.calendar.toggle_sprint", op_calendar_toggle_sprint);
CE_OP("season.calendar.add_round", op_calendar_add_round);
CE_OP("season.calendar.remove_round", op_calendar_remove_round);
CE_OP("season.calendar.repair", op_calendar_repair);
CE_OP("season.points.adjust", op_points_adjust);
CE_OP("season.points.clear", op_points_clear);
CE_OP("season.result.set", op_result_set);
