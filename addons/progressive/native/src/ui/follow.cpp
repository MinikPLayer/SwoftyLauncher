// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career - Follow your driver's real career (progressive_follow.dll, DESIGN.md D8).
//
// The F1 1990 contract skip (f1-1990 native/src/contract_skip.cpp), aimed at the real calendar. When
// Career_Decide_ContractNegotiation routes to the contract scene in a Progressive career whose team
// moves are follow_driver, with one human, the player signs a one-season contract with the game's own
// Career_Build_DriverContract + Career_Sign_DriverContract (type 0, the screen's recommended target)
// and the whole contract scene passes straight through (scene_logic.hpp): at the end of his contract
// the team select (Game_ContractTeamSelect_*, entered first then) returns the continue link without
// its screen, the agent intro cutscene (Intro) and the outro skip, and the contract terms screen
// (Update) enters idle and leaves at once. The outro logs where it was skipped, the steps that passed
// through and the contract signed. The decision's link names the route (scene_logic.hpp): 0x5e3c3c2d
// (no contract yet, or one that runs on) starts at the agent intro, 0xd2985448 (his contract ends
// this season) at the team select. A scene step entered with no decision before it (team select,
// agent intro, contract terms or outro, whichever comes first) signs and skips the same way, once per
// scene; Team HQ, the stock outro's end and career.unloaded reset that. Before any of that, the
// contract event itself (the Team HQ card 'CONTRACT EVENT', a career calendar activity of type 0x0f
// NCSContractRenewal) is never added to the player's own driver calendar: the
// Game_CareerActivities_AddActivity wrap signs there instead, so there is no card and no scene, and
// the season goes on with the contract cached for the season change. When that signing fails the
// event is added as in the stock game and its scene is skipped as above. A new career's event (no
// contract yet) is added as in the stock game too: it comes while the career is created, before it
// has a weekend, and the game's signing reads the weekend source's (+0x270, vtable slot +0xa0)
// weekend for Career_Build_LineupChanges without a null check. Every signing checks that weekend
// first: Start Next Season adds the new season's contract event before its calendar is built, so
// that event also stays until a Team HQ with a weekend signs and removes it. A contract event already on his
// driver calendar (a new career's, a save from before this, a renegotiation added at the season start) is found at
// career.loaded (when progressive.dll's store already answers follow) and at every Team HQ: after
// signing it is removed with Game_CareerActivities_RemoveActivity, the game's own remover, each one
// logged. The team signed:
//   - no active contract (a new career, or a save from before its first contract): his real team of
//     the career's current year (ProgressiveRealTeam: start + season index), whatever seat the
//     save still shows; his current team when he did not race that year;
//   - a contract that ends this season: his real team of the next year, from progressive.dll
//     (ProgressiveFollowTeam, data/years.json pool). For another team the signing caches the
//     contract and the game's season processor (0x140d766b0) moves him at the season change.
// Past his real career (no seat in the next year, or after 2026) the stock negotiation stays, so
// moves are free from then on. A failed sign also leaves the stock scene. The mode is the career's
// store entry (ProgressiveFollowMode); before the first Team HQ of a new career, which has none yet,
// the profile option team_moves.
//
// Secret meetings (meetings_logic.hpp): a follow career's player gets none. Every Driver Career
// meeting is made by Game_DriverContracts_OfferSecretMeeting, which the daily tick (through
// Game_DriverContracts_AdvanceSecretMeeting, for a player whose contract ends this season, which a
// follow career's one-season contracts always do) and the contracts screen's talks command call; in
// a follow career with one human the wrap returns without making it, so no meeting, no calendar
// day and no ProcessSMMessage (the inbox mail). The first few are logged one by one, and every Team
// HQ logs the count since the last one. A meeting a save already has is declined at the Team HQ
// (idle, no time advance) the way the secret meetings screen's decline command does it: reason 0,
// then Game_DriverContracts_AdvanceSecretMeeting, which moves it to his previous meetings and offers
// no next one through the wrap; his previous meetings are then freed the way the season change
// (0x140d76570) frees them, each slot's NeAllocator free, and the list emptied. Choose careers keep
// the stock meetings.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

#include <swofty/career.hpp>
#include <swofty/career_content.hpp>
#include <swofty/events.hpp>
#include <swofty/game.hpp>
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include "../meetings_logic.hpp"
#include "../scene_logic.hpp"

SWOFTY_SETTING(std::string_view, team_moves, "follow_driver");

namespace {

using namespace swofty::game;

constexpr const char *kGroup = "f1_career_progressive";
constexpr uint32_t kLinkContinueFallback = 0x7c9b1ec4;
constexpr uint32_t kNoKey = 0x3fff0000u;
constexpr uint32_t kHumanBit = 0x10000000u;
constexpr uintptr_t kRatingRanges = 0x318; // DriverCareerContractsManagerComponent: rating ranges begin/end
constexpr uintptr_t kContractRating = 0x18; // built contract record: overall rating
constexpr uintptr_t kContractSeasons = 0x20;
constexpr uintptr_t kContractEnd = 0x24;
constexpr uintptr_t kVtCurrentWeekend = 0xa0; // weekend source vtable slot: the current Weekend, or null

struct RatingRange {
    uint32_t min, max, minus, plus;
    int32_t recommended;
};
static_assert(sizeof(RatingRange) == 20);

constexpr uintptr_t kCachedBegin = 0x408; // manager: next season's cached contracts, 0x38 entries
constexpr size_t kCachedEntry = 0x38;
constexpr uintptr_t kCachedKind = 0x2c;
constexpr uintptr_t kCachedStat = 0x2e;

namespace sc = progressive::scene;

sc::Skip g_scene;
bool g_no_weekend_said = false;

template <class T> T rdv(uintptr_t a, T def = T{})
{
    T v{};
    return swofty::mem::read(a, v) ? v : def;
}

bool same_key(uint32_t a, uint32_t b)
{
    return ((a ^ b) & 0xffff00ffu) == 0;
}

bool has_weekend(uintptr_t contracts)
{
    uintptr_t src = rdv<uintptr_t>(contracts + offsetof(DriverCareerContractsManagerComponent, weekend_source));
    uintptr_t vt = src ? rdv<uintptr_t>(src) : 0;
    uintptr_t fn = vt ? rdv<uintptr_t>(vt + kVtCurrentWeekend) : 0;
    return swofty::mem::in_image(fn) && reinterpret_cast<void *(*)(void *)>(fn)(reinterpret_cast<void *>(src));
}

uint32_t continue_link()
{
    uint32_t v = g_Career_FlowLinkContinue.address() ? *g_Career_FlowLinkContinue : 0;
    return v ? v : kLinkContinueFallback;
}

int32_t recommended_target(int32_t base, uintptr_t contracts)
{
    if (base < 0)
        base = 0;
    uintptr_t b = rdv<uintptr_t>(contracts + kRatingRanges), e = rdv<uintptr_t>(contracts + kRatingRanges + 8);
    if (b && e > b && (e - b) % sizeof(RatingRange) == 0 && (e - b) / sizeof(RatingRange) < 64 &&
        swofty::mem::readable(b, e - b))
        for (uintptr_t p = b; p < e; p += sizeof(RatingRange)) {
            RatingRange r = rdv<RatingRange>(p);
            if (uint32_t(base) < r.min || uint32_t(base) > r.max)
                continue;
            int64_t lo = std::max<int64_t>(0, int64_t(base) - r.minus);
            int64_t hi = std::min<int64_t>(100, int64_t(base) + r.plus);
            return int32_t(std::clamp<int64_t>(int64_t(base) + r.recommended, std::min(lo, hi), hi));
        }
    return std::min(base, 100);
}

bool human_row(uintptr_t contracts, EntrantRecord &out)
{
    uintptr_t entity = rdv<uintptr_t>(contracts + offsetof(DriverCareerContractsManagerComponent, entity));
    uintptr_t b = entity ? rdv<uintptr_t>(entity + offsetof(CareerEntity, entrants_begin)) : 0;
    uintptr_t e = entity ? rdv<uintptr_t>(entity + offsetof(CareerEntity, entrants_end)) : 0;
    if (!b || e <= b || (e - b) % sizeof(EntrantRecord) || (e - b) / sizeof(EntrantRecord) > 256)
        return false;
    int hits = 0;
    for (uintptr_t p = b; p < e; p += sizeof(EntrantRecord)) {
        EntrantRecord r = rdv<EntrantRecord>(p);
        if ((r.ordinal_flags & kHumanBit) && r.id_hi != 0x3fff) {
            out = r;
            ++hits;
        }
    }
    return hits == 1;
}

uintptr_t contract_record(uintptr_t contracts, uint32_t driver)
{
    uintptr_t owner = rdv<uintptr_t>(contracts + offsetof(DriverCareerContractsManagerComponent, contract_owner));
    uintptr_t b = owner ? rdv<uintptr_t>(owner + offsetof(DriverCareerContractOwner, contracts_begin)) : 0;
    uintptr_t e = owner ? rdv<uintptr_t>(owner + offsetof(DriverCareerContractOwner, contracts_end)) : 0;
    for (uintptr_t p = b; p && p < e; p += 16)
        if (uintptr_t rec = rdv<uintptr_t>(p + 8); rec && same_key(rdv<uint32_t>(rec + offsetof(DriverContract, driver)), driver))
            return rec;
    return 0;
}

uint32_t cached_team(uintptr_t contracts, uint32_t driver)
{
    uintptr_t b = rdv<uintptr_t>(contracts + kCachedBegin), e = rdv<uintptr_t>(contracts + kCachedBegin + 8);
    if (!b || e <= b || (e - b) % kCachedEntry || (e - b) / kCachedEntry > 256 || !swofty::mem::readable(b, e - b))
        return kNoKey;
    for (uintptr_t p = b; p < e; p += kCachedEntry)
        if (rdv<uint8_t>(p + kCachedKind) == (driver & 0xff) && rdv<uint16_t>(p + kCachedStat) == (driver >> 16))
            return rdv<uint32_t>(p + offsetof(DriverContract, team));
    return kNoKey;
}

int store_follow_mode()
{
    static int (*fn)() = nullptr;
    static bool looked = false;
    if (!looked) {
        HMODULE m = GetModuleHandleW(L"progressive.dll");
        fn = m ? reinterpret_cast<int (*)()>(reinterpret_cast<void *>(GetProcAddress(m, "ProgressiveFollowMode"))) : nullptr;
        looked = m != nullptr;
    }
    return fn ? fn() : -1;
}

int follow_mode()
{
    int r = store_follow_mode();
    return r >= 0 ? r : std::string(team_moves.get()) == "follow_driver" ? 1 : 0;
}

using TeamFn = int (*)(uint32_t, uint32_t *, int *);

int team_call(const char *name, TeamFn &fn, bool &looked, uint32_t driver_stat, uint32_t *team_stat, int *year)
{
    if (!looked) {
        HMODULE m = GetModuleHandleW(L"progressive.dll");
        fn = m ? reinterpret_cast<TeamFn>(reinterpret_cast<void *>(GetProcAddress(m, name))) : nullptr;
        looked = m != nullptr;
    }
    return fn ? fn(driver_stat, team_stat, year) : -1;
}

int follow_team(uint32_t driver_stat, uint32_t *team_stat, int *year)
{
    static TeamFn fn = nullptr;
    static bool looked = false;
    return team_call("ProgressiveFollowTeam", fn, looked, driver_stat, team_stat, year);
}

int real_team(uint32_t driver_stat, uint32_t *team_stat, int *year)
{
    static TeamFn fn = nullptr;
    static bool looked = false;
    return team_call("ProgressiveRealTeam", fn, looked, driver_stat, team_stat, year);
}

bool sign(uintptr_t contracts)
{
    EntrantRecord human{};
    if (!human_row(contracts, human)) {
        swofty::log("[progressive] follow: no single human entrant; the contract scene stays");
        return false;
    }
    uint32_t driver;
    std::memcpy(&driver, &human.id_lo, sizeof driver);
    uintptr_t rec = contract_record(contracts, driver);
    if (!rec) {
        swofty::log("[progressive] follow: the player has no contract record; the contract scene stays");
        return false;
    }
    bool active = rdv<uint8_t>(rec + offsetof(DriverContract, active)) != 0;
    uint32_t team = human.team_stat_id;
    int year = 0;
    const char *why = "his current team (no contract yet; he did not race this year)";
    if (!active) {
        uint32_t real = 0;
        int r = real_team(driver >> 16, &real, &year);
        if (r < 0)
            why = "his current team (no contract yet; progressive.dll is not loaded)";
        else if (r > 0) {
            team = real << 16;
            why = same_key(team, human.team_stat_id) ? "his real team this year (no contract yet)"
                                                     : "his real team this year (no contract yet; his seat is another team's)";
        }
    } else {
        uint32_t next = 0;
        int r = follow_team(driver >> 16, &next, &year);
        if (r <= 0) {
            swofty::log("[progressive] follow: %s; the stock negotiation stays",
                        r < 0 ? "progressive.dll is not loaded" : "no real seat for the next year (or past 2026), moves are free");
            return false;
        }
        team = next << 16;
        why = same_key(team, human.team_stat_id) ? "his real team next year (stays)" : "his real team next year (moves)";
    }
    if ((team & 0xffff0000u) == kNoKey) {
        swofty::log("[progressive] follow: no team to sign with; the contract scene stays");
        return false;
    }
    if (!has_weekend(contracts)) {
        if (!g_no_weekend_said)
            swofty::log("[progressive] follow: driver %u: no current race weekend yet (a season before its calendar is "
                        "built); nothing signed until a Team HQ with one, since the game's signing reads it",
                        driver >> 16);
        g_no_weekend_said = true;
        return false;
    }
    g_no_weekend_said = false;
    alignas(16) uint8_t buf[0x80] = {};
    Career_Build_DriverContract(reinterpret_cast<void *>(contracts), buf, driver, team, 0, 0, 1);
    int32_t base = rdv<int32_t>(reinterpret_cast<uintptr_t>(buf) + kContractRating);
    int32_t target = recommended_target(base, contracts);
    std::memset(buf, 0, sizeof buf);
    Career_Build_DriverContract(reinterpret_cast<void *>(contracts), buf, driver, team, 0, target, 1);
    uint32_t cached_before = cached_team(contracts, driver);
    Career_Sign_DriverContract(reinterpret_cast<void *>(contracts), buf, driver, kNoKey, 1);
    uintptr_t now = contract_record(contracts, driver);
    bool now_active = now && rdv<uint8_t>(now + offsetof(DriverContract, active)) != 0;
    uint32_t now_team = now ? rdv<uint32_t>(now + offsetof(DriverContract, team)) : kNoKey;
    uint32_t cached = cached_team(contracts, driver);
    bool ok = active ? same_key(cached, team) || (same_key(now_team, team) && now_active)
                     : !Career_Needs_DriverContract(reinterpret_cast<void *>(contracts)) || now_active;
    swofty::log("[progressive] follow: after the signing: contract team %u (%s), next season cached team %u (before: "
                "%u)",
                now_team >> 16, now_active ? "active" : "not active", cached >> 16, cached_before >> 16);
    swofty::log("[progressive] follow: signed driver %u with team %u (%s%s%s), rating %d, target %d, %d season(s), ends "
                "%d: %s",
                driver >> 16, team >> 16, why, year ? ", " : "", year ? std::to_string(year).c_str() : "", base, target,
                rdv<int32_t>(reinterpret_cast<uintptr_t>(buf) + kContractSeasons),
                rdv<int32_t>(reinterpret_cast<uintptr_t>(buf) + kContractEnd),
                ok ? "signed or cached for the season change" : "NOT signed");
    if (ok)
        g_scene.arm(active, team, year, target);
    return ok;
}

namespace mt = progressive::meetings;

mt::Tally g_meetings;

struct GameMem {
    template <class T> bool read(uintptr_t a, T &v) const
    {
        return swofty::mem::read(a, v);
    }
};

mt::Layout meeting_layout()
{
    return {offsetof(DriverCareerContractsManagerComponent, secret_meetings_begin),
            offsetof(DriverCareerContractsManagerComponent, secret_meetings_end),
            offsetof(DriverSecretMeetings, current),
            offsetof(DriverSecretMeetings, driver),
            offsetof(DriverSecretMeetings, previous_begin),
            offsetof(DriverSecretMeetings, previous_end),
            offsetof(SecretMeeting, id),
            offsetof(SecretMeeting, reason),
            offsetof(SecretMeeting, team)};
}

uint32_t humans()
{
    swofty::career::State cs{};
    return swofty::career::state(&cs) == swofty::career::Result::ok ? cs.humans : 1;
}

bool no_meetings()
{
    return mt::suppress(swofty::career::is(kGroup), follow_mode(), humans());
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

struct Cleared {
    int declined = 0;
    int past = 0;
};

int clear_previous(uintptr_t record, uint32_t driver, const mt::Layout &l)
{
    uintptr_t begin = 0;
    std::vector<mt::Past> past;
    if (!mt::previous(GameMem{}, record, l, begin, past)) {
        swofty::log("[progressive] follow: driver %u's past secret meetings could not be read; they stay until the "
                    "season change",
                    driver >> 16);
        return 0;
    }
    if (past.empty())
        return 0;
    if (!writable(record + l.record_previous_end, sizeof(uintptr_t))) {
        swofty::log("[progressive] follow: driver %u's past secret meetings are not writable; they stay until the "
                    "season change",
                    driver >> 16);
        return 0;
    }
    int freed = 0;
    for (const mt::Past &p : past) {
        if (p.meeting && freed < mt::kLogFirst)
            swofty::log("[progressive] follow: past secret meeting %s removed",
                        (mt::describe(p.id, p.team, driver) + " (" + mt::reason_name(p.reason) + ")").c_str());
        auto *alloc = reinterpret_cast<NeAllocator *>(p.allocator);
        if (p.meeting && alloc && alloc->vtable && alloc->vtable->free)
            reinterpret_cast<void (*)(NeAllocator *, void *)>(alloc->vtable->free)(alloc,
                                                                                    reinterpret_cast<void *>(p.meeting));
        ++freed;
    }
    *reinterpret_cast<uintptr_t *>(record + l.record_previous_end) = begin;
    return freed;
}

Cleared clear_saved(uintptr_t contracts)
{
    mt::Layout l = meeting_layout();
    Cleared n;
    for (const mt::Current &c : mt::current(GameMem{}, contracts, l)) {
        std::string what = mt::describe(c);
        if (c.reason == mt::kOffered) {
            if (!writable(c.meeting + l.meeting_reason, 1)) {
                swofty::log("[progressive] follow: secret meeting %s from the save is not writable; it stays", what.c_str());
                continue;
            }
            *reinterpret_cast<uint8_t *>(c.meeting + l.meeting_reason) = mt::kDeclined;
        }
        Game_DriverContracts_AdvanceSecretMeeting(reinterpret_cast<void *>(contracts), c.driver, 0);
        uintptr_t now = rdv<uintptr_t>(c.record + l.record_current);
        if (now == c.meeting)
            swofty::log("[progressive] follow: secret meeting %s from the save declined; the game keeps it until the "
                        "season change (his contract runs past this season)",
                        what.c_str());
        else
            swofty::log("[progressive] follow: secret meeting %s from the save declined%s", what.c_str(),
                        now ? "; the game made another one" : "");
        ++n.declined;
    }
    for (uintptr_t r : mt::records(GameMem{}, contracts, l))
        n.past += clear_previous(r, rdv<uint32_t>(r + l.record_driver), l);
    return n;
}

void meetings_hub()
{
    if (!swofty::career::is(kGroup))
        return;
    int mode = follow_mode();
    uint32_t h = humans();
    if (!mt::suppress(true, mode, h)) {
        g_meetings.take();
        return;
    }
    auto contracts = reinterpret_cast<uintptr_t>(
        swofty::career::component_by_desc(k_Career_DriverCareerContractsManagerComponentDesc.get()));
    Cleared c = contracts ? clear_saved(contracts) : Cleared{};
    int n = g_meetings.take();
    swofty::log("[progressive] follow: Team HQ: %d secret meeting(s) suppressed since the last Team HQ (%d this "
                "session); from the save %d pending declined, %d past removed",
                n, g_meetings.total.load(), c.declined, c.past);
}

bool arm_scene(const char *where)
{
    g_scene.tried = true;
    int mode = follow_mode();
    if (mode != 1) {
        swofty::log("[progressive] follow: contract scene (%s): this career chooses its team; the stock scene stays", where);
        return false;
    }
    uint32_t h = humans();
    if (h > 1) {
        swofty::log("[progressive] follow: contract scene (%s): %u human players; the stock scene stays", where, h);
        return false;
    }
    auto contracts = reinterpret_cast<uintptr_t>(
        swofty::career::component_by_desc(k_Career_DriverCareerContractsManagerComponentDesc.get()));
    if (!contracts) {
        swofty::log("[progressive] follow: contract scene (%s): no contracts component; the stock scene stays", where);
        return false;
    }
    if (!sign(contracts)) {
        swofty::log("[progressive] follow: contract scene (%s): nothing signed; the stock scene stays", where);
        return false;
    }
    swofty::log("[progressive] follow: skipping the contract scene (%s; %s; continue link 0x%08x)", where,
                g_scene.where().c_str(), continue_link());
    return true;
}

bool scene_step(sc::Step step, const char *name)
{
    if (!g_scene.on && !g_scene.tried && swofty::career::is(kGroup)) {
        std::string where = std::string(name) + " entered without the negotiation decision";
        arm_scene(where.c_str());
    }
    if (!g_scene.on)
        return false;
    g_scene.mark(step);
    return true;
}

sc::CalendarLayout calendar_layout()
{
    sc::CalendarLayout l{offsetof(ScheduleComponent, seasons_begin),
                         offsetof(ScheduleComponent, seasons_end),
                         sizeof(ScheduleSeason),
                         offsetof(ScheduleSeason, driver_calendars_begin),
                         offsetof(ScheduleSeason, driver_calendars_end),
                         sizeof(DriverCalendar)};
    l.calendar_driver = offsetof(DriverCalendar, driver);
    l.activities_begin = offsetof(DriverCalendar, activities_begin);
    l.activities_end = offsetof(DriverCalendar, activities_end);
    l.activity_day = offsetof(CalendarActivity, day);
    l.activity_def = offsetof(CalendarActivity, activity);
    l.def_type = offsetof(ActivityDef, type);
    return l;
}

bool own_calendar(uintptr_t contracts, uintptr_t calendar, uint32_t &driver)
{
    EntrantRecord human{};
    if (!human_row(contracts, human))
        return false;
    std::memcpy(&driver, &human.id_lo, sizeof driver);
    if (!same_key(rdv<uint32_t>(calendar + offsetof(DriverCalendar, driver)), driver))
        return false;
    uintptr_t schedule = rdv<uintptr_t>(contracts + offsetof(DriverCareerContractsManagerComponent, schedule));
    return sc::driver_calendar(GameMem{}, schedule, calendar, calendar_layout());
}

int clear_pending_events(const char *when)
{
    auto contracts = reinterpret_cast<uintptr_t>(
        swofty::career::component_by_desc(k_Career_DriverCareerContractsManagerComponentDesc.get()));
    EntrantRecord human{};
    if (!contracts || !human_row(contracts, human))
        return 0;
    uint32_t driver = 0;
    std::memcpy(&driver, &human.id_lo, sizeof driver);
    uintptr_t time = rdv<uintptr_t>(contracts + offsetof(DriverCareerContractsManagerComponent, career_time));
    uint32_t today = time ? rdv<uint32_t>(time + offsetof(CareerTimeComponent, current_minutes)) / 1440 : 0;
    uintptr_t schedule = rdv<uintptr_t>(contracts + offsetof(DriverCareerContractsManagerComponent, schedule));
    sc::CalendarLayout l = calendar_layout();
    std::vector<sc::Pending> events = sc::pending_events(GameMem{}, schedule, driver, today, l);
    if (events.empty())
        return 0;
    if (!sign(contracts)) {
        g_scene.clear();
        swofty::log("[progressive] follow: %s: %zu contract event(s) on driver %u's calendar (first on day %u), nothing "
                    "signed; they stay (the contract scene is skipped when he opens one, if a later signing works)",
                    when, events.size(), driver >> 16, events.front().day);
        return 0;
    }
    sc::Skip signed_now = g_scene;
    g_scene.clear();
    int removed = 0;
    for (const sc::Pending &p : events) {
        Game_CareerActivities_RemoveActivity(reinterpret_cast<void *>(p.calendar), reinterpret_cast<const void *>(p.def),
                                             p.day);
        bool gone = true;
        for (const sc::Pending &q : sc::pending_events(GameMem{}, schedule, driver, today, l))
            if (q.activity == p.activity)
                gone = false;
        removed += gone ? 1 : 0;
        swofty::log("[progressive] follow: %s: contract event on day %u (today %u) %s driver %u's calendar", when, p.day,
                    today, gone ? "removed from" : "NOT removed from", driver >> 16);
    }
    swofty::log("[progressive] follow: %s: %d of %zu contract event(s) removed (%s): signed team %u%s%s (one season, "
                "target %d); no Team HQ card, no contract scene",
                when, removed, events.size(), signed_now.where().c_str(), signed_now.team >> 16,
                signed_now.year > 0 ? " for " : "", signed_now.year > 0 ? std::to_string(signed_now.year).c_str() : "",
                signed_now.target);
    return removed;
}

void contract_events_check(const char *when, bool store_only)
{
    if (!swofty::career::is(kGroup))
        return;
    int mode = store_only ? store_follow_mode() : follow_mode();
    if (mode != 1 || humans() > 1)
        return;
    clear_pending_events(when);
}

void on_career(const SwoftyEvent *ev, void *)
{
    if (!std::strcmp(ev->name, SWOFTY_EVENT_CAREER_UNLOADED)) {
        g_meetings.reset();
        g_scene.clear();
        return;
    }
    if (!std::strcmp(ev->name, SWOFTY_EVENT_CAREER_LOADED)) {
        if (static_cast<swofty::career::Mode>(ev->a) == swofty::career::Mode::driver_career)
            contract_events_check("career load", true);
        return;
    }
    if (ev->a == 1 && static_cast<swofty::career::Mode>(ev->b) == swofty::career::Mode::driver_career) {
        g_scene.clear();
        contract_events_check("Team HQ", false);
        meetings_hub();
    }
}

} // namespace

SWOFTY_WRAP(Career_Decide_ContractNegotiation, uint32_t *, void *state, uint32_t *link)
{
    uint32_t *r = orig(state, link);
    g_scene.clear();
    if (!link || !swofty::career::is(kGroup))
        return r;
    sc::Route route = sc::route(*link);
    if (route == sc::Route::none) {
        swofty::log("[progressive] follow: contract decision link 0x%08x: %s", *link, sc::route_name(route));
        return r;
    }
    std::string where = std::string("the negotiation decision, ") + sc::route_name(route);
    arm_scene(where.c_str());
    return r;
}

SWOFTY_WRAP(Game_ContractTeamSelect_OnEnter, uint64_t, void *state)
{
    return scene_step(sc::kTeamSelect, "team select") ? 1 : orig(state);
}

SWOFTY_WRAP(Game_ContractTeamSelect_OnUpdate, uint32_t *, void *state, uint32_t *link)
{
    if (!g_scene.on)
        return orig(state, link);
    *link = continue_link();
    return link;
}

SWOFTY_WRAP(Game_ContractTeamSelect_OnExit, uint64_t, void *state)
{
    return g_scene.on ? 1 : orig(state);
}

SWOFTY_WRAP(Career_ContractIntro_OnEnter, uint64_t, void *state)
{
    return scene_step(sc::kIntro, "agent intro") ? 1 : orig(state);
}

SWOFTY_WRAP(Career_ContractIntro_OnUpdate, uint32_t *, void *state, uint32_t *link)
{
    if (!g_scene.on)
        return orig(state, link);
    *link = continue_link();
    return link;
}

SWOFTY_WRAP(Career_ContractUpdate_OnEnter, uint64_t, void *state)
{
    if (!scene_step(sc::kTerms, "contract terms"))
        return orig(state);
    auto *s = static_cast<uint8_t *>(state);
    s[0xd58] = 1;
    s[0xd5a] = 0;
    s[0xd5b] = 0;
    std::memset(s + 0xd60, 0, 0x10);
    g_scene.update_skipped = true;
    return 1;
}

SWOFTY_WRAP(Career_ContractUpdate_OnUpdate, uint32_t *, void *state, uint32_t *link)
{
    if (!g_scene.update_skipped)
        return orig(state, link);
    *link = continue_link();
    return link;
}

SWOFTY_WRAP(Career_ContractUpdate_OnExit, uint64_t, void *state)
{
    if (!g_scene.update_skipped)
        return orig(state);
    g_scene.update_skipped = false;
    return 1;
}

SWOFTY_WRAP(Career_ContractOutro_OnEnter, uint64_t, void *state)
{
    return scene_step(sc::kOutro, "outro") ? 1 : orig(state);
}

SWOFTY_WRAP(Career_ContractOutro_OnUpdate, uint32_t *, void *state, uint32_t *link)
{
    if (!g_scene.on) {
        uint32_t *r = orig(state, link);
        if (r && *r == continue_link())
            g_scene.clear();
        return r;
    }
    *link = continue_link();
    swofty::log("[progressive] follow: %s", g_scene.summary().c_str());
    g_scene.clear();
    return link;
}

SWOFTY_WRAP(Game_CareerActivities_AddActivity, void, void *calendar, const void *activity, uint32_t day)
{
    auto def = reinterpret_cast<uintptr_t>(activity);
    if (!def || rdv<uint8_t>(def + offsetof(ActivityDef, type)) != sc::kContractEvent || !swofty::career::is(kGroup))
        return orig(calendar, activity, day);
    auto cal = reinterpret_cast<uintptr_t>(calendar);
    auto contracts = reinterpret_cast<uintptr_t>(
        swofty::career::component_by_desc(k_Career_DriverCareerContractsManagerComponentDesc.get()));
    uint32_t driver = 0;
    if (!contracts || !own_calendar(contracts, cal, driver))
        return orig(calendar, activity, day);
    if (follow_mode() != 1) {
        swofty::log("[progressive] follow: contract event on day %u: this career chooses its team; the event stays", day);
        return orig(calendar, activity, day);
    }
    if (uint32_t h = humans(); h > 1) {
        swofty::log("[progressive] follow: contract event on day %u: %u human players; the event stays", day, h);
        return orig(calendar, activity, day);
    }
    uintptr_t rec = contract_record(contracts, driver);
    if (!rec || !rdv<uint8_t>(rec + offsetof(DriverContract, active))) {
        swofty::log("[progressive] follow: contract event on day %u for driver %u: no contract yet (a new career, no "
                    "weekend yet); the event stays until the first Team HQ signs and removes it",
                    day, driver >> 16);
        return orig(calendar, activity, day);
    }
    if (!sign(contracts)) {
        g_scene.clear();
        swofty::log("[progressive] follow: contract event on day %u for driver %u: nothing signed; the event stays (its "
                    "contract scene is skipped when he opens it, if a later signing works)",
                    day, driver >> 16);
        return orig(calendar, activity, day);
    }
    sc::Skip signed_now = g_scene;
    g_scene.clear();
    swofty::log("[progressive] follow: contract event on day %u not added to driver %u's calendar (%s): signed team %u%s%s "
                "(one season, target %d); no Team HQ card, no contract scene",
                day, driver >> 16, signed_now.where().c_str(), signed_now.team >> 16, signed_now.year > 0 ? " for " : "",
                signed_now.year > 0 ? std::to_string(signed_now.year).c_str() : "", signed_now.target);
}

SWOFTY_WRAP(Game_DriverContracts_OfferSecretMeeting, void, void *contracts, const void *meeting, uint32_t team,
            uint32_t driver, uint8_t reason, uint8_t notify)
{
    if (!no_meetings())
        return orig(contracts, meeting, team, driver, reason, notify);
    uint32_t id = meeting ? rdv<uint32_t>(reinterpret_cast<uintptr_t>(meeting) + offsetof(SecretMeeting, id)) : 0;
    if (g_meetings.note())
        swofty::log("[progressive] follow: secret meeting %s suppressed (%s)", mt::describe(id, team, driver).c_str(),
                    notify ? "the player's secret meetings or contracts screen" : "the career's daily tick");
}

SWOFTY_ON_LOAD
{
    uint32_t token = 0;
    for (const char *ev : {SWOFTY_EVENT_CAREER_HUB, SWOFTY_EVENT_CAREER_LOADED, SWOFTY_EVENT_CAREER_UNLOADED}) {
        swofty::events::Result r = swofty::events::subscribe(ev, on_career, nullptr, &token);
        if (r != swofty::events::Result::ok)
            swofty::log("[progressive] follow: subscribe %s: %s; saved secret meetings and contract events are not "
                        "cleared on it",
                        ev, swofty::events::name(r));
    }
}

SWOFTY_ADDON()
