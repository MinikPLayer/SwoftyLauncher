// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career - Choose Driver and Team Select show the start season (progressive_cards.dll).
//
// Choose Driver (vt_Game_DriverCareerDriverSelectViewModel slot 13, 0x141a5adc0) takes only driver
// keys from the official lists F1CAREER_/F2CAREER_/ICONS_<suffix> (0x141a5f2d0 pushes every list's
// keys, with no check for a key it already has). Everything a card shows comes from the career's
// setup list, the game mode's +0xe18 (the registry's f1_career_progressive list): the team badge
// (0x1416a5440, overrides.teams.badges_broadcast.<team>), and the populate's copy of its rows (vm
// +0x1470) gives the focused driver's look, number and team (0x141a5d180, 0x1416a3630) and the pick
// (0x141a5df70 stores the copy's look and team). Team Select (0x141878ea0, 0x141877ec0) takes each
// team's car from its first seat in +0xe18 and the drivers it shows from the team's first two rows.
//
// So the populate of a Progressive wizard below 2026 gets a view of the setup list dressed as the
// start season (cards_logic.hpp build()). +0xe18 points at the view, a private copy of the list
// header whose row vector is ours. The view stays through Team Select: the line-up is never copied
// from it, because Game_CareerSetup_ResetEntrants and the line-up builder 0x140c2a870 both copy the
// registry's list by stat id and point +0xe18 back at it when the career is built. The view is
// also put back when the wizard is left (a career tile press or career.starting) and at Team HQ.
// During the populate the driver pool (ICONS) and the F2 list read empty, so the cards are the
// season list's drivers only.
//
// Team Select's 3D cars do not come from +0xe18. Its team list (0x141873a30) builds one carousel
// item per team (0x141879310), which spawns the car of the team's first line-up row (the game
// mode's entrant rows, team +0x58, position +0x80, Sess_Find_Vehicle on +0x5c). The first Team
// Select is built in the same frames as the Choose Driver pick, before the core's poll publishes
// career.loaded, so the rows are dressed right after every Choose Driver command once the career
// component exists (the pick makes it), and again at career.loaded: each line-up row wears its
// team's start season car from the view. The R&D team models were made from the career's cars
// before Choose Driver. The rows get the career's cars back at the next Choose Driver populate and
// when the wizard is left (a tile press, career.starting). Once the career exists (Team HQ, a
// saved career's career.loaded) the rows are the career's own and keep what the game built from
// them; only the bookkeeping is dropped.
//
// Driver ratings: while the view is active every start season driver shows his ratings of that
// year in Choose Driver's Insights and in Team Select (wizard_ratings.hpp); they are put back
// wherever the view is taken back.
//
// Team logos are progressive_logos.dll's (logos.cpp) and the drivers' numbers progressive_numbers.dll's
// (numbers.cpp): they ask ProgressiveCardsStartYear() and ProgressiveCardsNumber() below whether the
// view is active and what it holds, so a build where their sites do not resolve loses only those.
//
// Team choice: in the new-career wizard Team Select (+0xc68 set) lists only the picked driver's
// team when his line-up row has one, and every team when it has none; its offer build
// (Game_DriverContracts_BuildOffers) only runs outside the wizard. So right after the pick his row's
// team is his start season team from the view with team_moves follow_driver (every team when the
// view does not seat him) and none with choose, until Team Select is left. The BuildOffers wrap
// keeps the same lock for a Team Select that does build offers.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <cstddef>
#include <iterator>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <swofty/career.hpp>
#include <swofty/events.hpp>
#include <swofty/game.hpp>
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include "../cards_logic.hpp"
#include "wizard_ratings.hpp"

namespace {

using swofty::game::EntrantRecord;
namespace pc = progressive::cards;

static_assert(pc::kEventHub == SWOFTY_EVENT_CAREER_HUB);
static_assert(pc::kEventContentPressed == SWOFTY_EVENT_CAREER_CONTENT_PRESSED);
static_assert(pc::kEventStarting == SWOFTY_EVENT_CAREER_STARTING);
static_assert(pc::kEventLoaded == SWOFTY_EVENT_CAREER_LOADED);

constexpr const char *kGroup = "f1_career_progressive";
constexpr uintptr_t kVmWizard = 0x438;
constexpr uintptr_t kVmDriver = 0x480;
constexpr uintptr_t kVmEntrants = 0x440;
constexpr uintptr_t kVmFlow = 0x448;
constexpr uintptr_t kVmCardsBegin = 0x2c0;
constexpr uintptr_t kVmCardsEnd = 0x2c8;
constexpr size_t kCardSize = 0x170;
constexpr uintptr_t kFlowHolder = 0x88;
constexpr uintptr_t kHolderGameMode = 0x260;
constexpr uintptr_t kModeSetupList = 0xe18;
constexpr uintptr_t kWizardGroup = 0x128;
constexpr uintptr_t kWizardSuffix = 0x150;
constexpr uintptr_t kEntrantsLists = 0x560;
constexpr size_t kListSlot = 0x10;
constexpr uintptr_t kListBegin = 0x10;
constexpr uintptr_t kListEnd = 0x18;
constexpr uintptr_t kListCapacity = 0x20;
constexpr size_t kListHeader = 0x100;
constexpr size_t kMaxRows = pc::kMaxRows;
constexpr uintptr_t kModeEntrants = 0x2e0;
constexpr uintptr_t kContractsGameMode = 0x278;
constexpr uintptr_t kContractsOffers = 0x3c8;
constexpr size_t kOfferSize = 0x48;
constexpr uintptr_t kOfferTeamsBegin = 0x10;
constexpr uintptr_t kOfferTeamsEnd = 0x18;
constexpr uintptr_t kOfferTeamsCapacity = 0x20;
constexpr uintptr_t kRowVehicle = offsetof(EntrantRecord, vehicle_ref);

static_assert(sizeof(EntrantRecord) == 0x88);
static_assert(kRowVehicle == 0x5c);
constexpr uintptr_t kRowTeam = offsetof(EntrantRecord, team_stat_id);
static_assert(kRowTeam == 0x58);

SWOFTY_SETTING(std::string_view, team_moves, "follow_driver");

alignas(16) uint8_t g_header[kListHeader];
EntrantRecord g_rows[kMaxRows];
uintptr_t g_mode = 0;
uintptr_t g_list = 0;
size_t g_count = 0;
int g_year = 0;
int g_logged = 0;

struct Swap {
    uintptr_t row;
    uint8_t id_lo;
    uint16_t id_hi;
    uint32_t was, now;
};
std::mutex g_swap_lock;
std::vector<Swap> g_swaps;
std::vector<Swap> g_team_swaps;
uint32_t g_gated_driver = pc::kNoKey;
bool g_live = false;
int g_offer_logged = 0;


uintptr_t view()
{
    return reinterpret_cast<uintptr_t>(g_header);
}

template <class T> T rd(uintptr_t p)
{
    T v{};
    swofty::mem::read(p, v);
    return v;
}

bool writable(uintptr_t p, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<const void *>(p), &mbi, sizeof mbi) != sizeof mbi || mbi.State != MEM_COMMIT ||
        (mbi.Protect & PAGE_GUARD) || !(mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)))
        return false;
    return p + n <= reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
}

template <class T> bool put(uintptr_t p, T v)
{
    if (!writable(p, sizeof v))
        return false;
    std::memcpy(reinterpret_cast<void *>(p), &v, sizeof v);
    return true;
}

std::string text_at(uintptr_t p)
{
    std::string s;
    if (p)
        swofty::mem::c_string(p, s, 64);
    return s;
}

using Rows = pc::Rows<EntrantRecord>;

bool rows_of(uintptr_t list, Rows &out)
{
    out = {};
    uintptr_t b = rd<uintptr_t>(list + kListBegin), e = rd<uintptr_t>(list + kListEnd);
    if (!list || !b || e < b || (e - b) % sizeof(EntrantRecord) || (e - b) / sizeof(EntrantRecord) > kMaxRows ||
        !swofty::mem::readable(b, e - b))
        return false;
    out.begin = reinterpret_cast<const EntrantRecord *>(b);
    out.count = (e - b) / sizeof(EntrantRecord);
    return true;
}

uintptr_t list_at(uintptr_t entrants, unsigned stat)
{
    uintptr_t b = rd<uintptr_t>(entrants + kEntrantsLists), e = rd<uintptr_t>(entrants + kEntrantsLists + 8);
    if (!b || e < b || stat >= (e - b) / kListSlot)
        return 0;
    uintptr_t list = rd<uintptr_t>(b + stat * kListSlot + 8);
    return swofty::mem::readable(list, kListHeader) ? list : 0;
}

// apply false: the career now owns the line-up rows (it was built from them), so the changes stay
// and only the bookkeeping is dropped.
void restore_cars(const char *why, bool apply = true)
{
    std::lock_guard<std::mutex> g(g_swap_lock);
    g_gated_driver = pc::kNoKey;
    if (g_swaps.empty() && g_team_swaps.empty())
        return;
    if (!apply) {
        swofty::log("[progressive] cards: Team Select: %zu car change(s) and %zu team change(s) left on the line-up: the "
                    "career owns those rows now (%s)",
                    g_swaps.size(), g_team_swaps.size(), why);
        g_swaps.clear();
        g_team_swaps.clear();
        return;
    }
    size_t back = 0;
    for (const Swap &w : g_swaps) {
        EntrantRecord r{};
        if (swofty::mem::read(w.row, r) && r.id_lo == w.id_lo && r.id_hi == w.id_hi && r.vehicle_ref == w.now &&
            put<uint32_t>(w.row + kRowVehicle, w.was))
            ++back;
    }
    if (!g_swaps.empty())
        swofty::log("[progressive] cards: Team Select: %zu of %zu line-up row(s) back on their career cars (%s)", back,
                    g_swaps.size(), why);
    g_swaps.clear();
    for (const Swap &w : g_team_swaps) {
        EntrantRecord r{};
        bool ok = swofty::mem::read(w.row, r) && r.id_lo == w.id_lo && r.id_hi == w.id_hi && r.team_stat_id == w.now &&
                  put<uint32_t>(w.row + kRowTeam, w.was);
        swofty::log("[progressive] cards: Team Select: the picked driver's line-up row is %s (%s)",
                    ok ? "back on his career team" : "no longer the one the pick changed", why);
    }
    g_team_swaps.clear();
}

void take_back(const char *why, bool restore_rows)
{
    restore_cars(why, restore_rows);
    pg::wizard_ratings::restore(why, std::string_view(why) != pc::kEventHub);
    g_live = false;
    if (!g_mode)
        return;
    uintptr_t setup = rd<uintptr_t>(g_mode + kModeSetupList);
    if (setup == view()) {
        bool ok = put<uintptr_t>(g_mode + kModeSetupList, g_list);
        swofty::log("[progressive] cards: %s: the setup list is the career's own again%s", why, ok ? "" : " (write failed)");
    } else if (g_logged++ < 16) {
        swofty::log("[progressive] cards: %s: the game already pointed the setup list back at %s 0x%llx", why,
                    setup == g_list ? "the career's own list" : "another list", static_cast<unsigned long long>(setup));
    }
    g_mode = 0;
}

bool dress(uintptr_t vm, uintptr_t mode, uintptr_t entrants, uintptr_t career_list, int year)
{
    unsigned stat = pc::kFirstSeasonList + static_cast<unsigned>(year - pc::kFirstYear);
    Rows career{}, season{}, first{};
    if (!rows_of(career_list, career) || !rows_of(list_at(entrants, stat), season) || !season.count) {
        if (g_logged++ < 8)
            swofty::log("[progressive] cards: %d: the career list (%u) or the season list (%u) has no rows; Choose Driver "
                        "keeps the 2026 looks",
                        year, pc::kCareerList, stat);
        return false;
    }
    rows_of(list_at(entrants, pc::kFirstSeasonList), first);
    pc::Stats st{};
    size_t n = pc::build(career, season, first, g_rows, kMaxRows, st);
    if (!n || st.season == 0)
        return false;
    std::memcpy(g_header, reinterpret_cast<const void *>(career_list), kListHeader);
    uintptr_t b = reinterpret_cast<uintptr_t>(&g_rows[0]);
    std::memcpy(g_header + kListBegin, &b, sizeof b);
    uintptr_t e = b + n * sizeof(EntrantRecord);
    std::memcpy(g_header + kListEnd, &e, sizeof e);
    uint64_t cap = n;
    std::memcpy(g_header + kListCapacity, &cap, sizeof cap);
    if (!put<uintptr_t>(mode + kModeSetupList, view()))
        return false;
    g_mode = mode;
    g_list = career_list;
    g_count = n;
    g_year = year;
    if (g_logged++ < 16)
        swofty::log("[progressive] cards: Choose Driver %d (vm 0x%llx): %zu season driver(s) on their %d seats (looks %zu, "
                    "cars %zu, numbers %zu), %zu not in the career list, %zu filler seat(s), %zu 2026 seat(s) left free, "
                    "%zu team(s), %zu row(s); kept through Team Select",
                    year, static_cast<unsigned long long>(vm), st.season, year, st.looks, st.cars, st.numbers, st.missing,
                    st.filled, st.benched, st.teams, n);
    return true;
}

int suffix_year(uintptr_t wizard)
{
    std::string s = text_at(rd<uintptr_t>(wizard + kWizardSuffix));
    if (s.size() != 4 || s[0] != 'P' || s[1] != 'G' || s[2] < '0' || s[2] > '9' || s[3] < '0' || s[3] > '9')
        return 0;
    return 2000 + (s[2] - '0') * 10 + (s[3] - '0');
}

const EntrantRecord *view_row(uint32_t driver)
{
    for (size_t i = 0; i < g_count; ++i)
        if (g_rows[i].id_lo == (driver & 0xff) && g_rows[i].id_hi == (driver >> 16))
            return &g_rows[i];
    return nullptr;
}

uint32_t season_car(uint32_t team)
{
    uint32_t any = pc::kNoKey;
    for (size_t i = 0; i < g_count; ++i) {
        const EntrantRecord &r = g_rows[i];
        if (!pc::seated(r) || !pc::same_team(r.team_stat_id, team) || pc::none(r.vehicle_ref))
            continue;
        if ((r.team_position & 0xff) == 0)
            return r.vehicle_ref;
        if (pc::none(any))
            any = r.vehicle_ref;
    }
    return any;
}

size_t swap_cars(const char *why, bool quiet)
{
    uintptr_t mode = g_mode;
    uintptr_t b = mode ? rd<uintptr_t>(mode + kModeEntrants) : 0, e = mode ? rd<uintptr_t>(mode + kModeEntrants + 8) : 0;
    if (!b || e < b || (e - b) % sizeof(EntrantRecord) || (e - b) / sizeof(EntrantRecord) > kMaxRows ||
        !swofty::mem::readable(b, e - b)) {
        if (g_logged++ < 16)
            swofty::log("[progressive] cards: Team Select %d (%s): the game mode 0x%llx has no line-up to dress (0x%llx..0x%llx)",
                        g_year, why, static_cast<unsigned long long>(mode), static_cast<unsigned long long>(b),
                        static_cast<unsigned long long>(e));
        return 0;
    }
    size_t added = 0, total = 0;
    {
        std::lock_guard<std::mutex> g(g_swap_lock);
        for (uintptr_t p = b; p < e; p += sizeof(EntrantRecord)) {
            const auto &r = *reinterpret_cast<const EntrantRecord *>(p);
            if (pc::none(r.team_stat_id) || pc::none(r.vehicle_ref))
                continue;
            uint32_t car = season_car(r.team_stat_id);
            if (pc::none(car) || car == r.vehicle_ref)
                continue;
            Swap w{p, r.id_lo, r.id_hi, r.vehicle_ref, car};
            if (put<uint32_t>(p + kRowVehicle, car)) {
                g_swaps.push_back(w);
                ++added;
            }
        }
        total = g_swaps.size();
    }
    if (!quiet || added)
        swofty::log("[progressive] cards: Team Select %d (%s): %zu more line-up row(s) wear their team's %d car, %zu in "
                    "all (line-up 0x%llx, %zu row(s); setup list %s)",
                    g_year, why, added, g_year, total, static_cast<unsigned long long>(b),
                    (e - b) / sizeof(EntrantRecord),
                    rd<uintptr_t>(mode + kModeSetupList) == view() ? "the start season view" : "the career's own");
    return added;
}

const char *lock_offer(uintptr_t contracts, uint32_t driver, uint32_t &team, size_t &before)
{
    const EntrantRecord *v = view_row(driver);
    if (!v || !pc::seated(*v))
        return "the driver has no seat in the start season view";
    team = v->team_stat_id;
    uintptr_t b = rd<uintptr_t>(contracts + kContractsOffers), e = rd<uintptr_t>(contracts + kContractsOffers + 8);
    if (!b || e < b || (e - b) % kOfferSize)
        return "the offer list does not look like one";
    for (uintptr_t o = b; o < e; o += kOfferSize) {
        if (rd<uint8_t>(o) != (driver & 0xff) || rd<uint16_t>(o + 2) != (driver >> 16))
            continue;
        uintptr_t tb = rd<uintptr_t>(o + kOfferTeamsBegin), te = rd<uintptr_t>(o + kOfferTeamsEnd);
        uint64_t cap = rd<uint64_t>(o + kOfferTeamsCapacity);
        before = tb && te >= tb ? (te - tb) / 4 : 0;
        if (!tb || cap < 1 || te < tb)
            return "the driver's offer list has no room, so every offered team stays open";
        if (!put<uint32_t>(tb, team) || !put<uintptr_t>(o + kOfferTeamsEnd, tb + 4))
            return "the offer list write failed";
        return nullptr;
    }
    return "the driver has no offer entry, so every team stays open";
}

void team_select(uintptr_t contracts, uint32_t driver)
{
    auto skip = [&](const char *why) {
        if (g_offer_logged++ < 16)
            swofty::log("[progressive] cards: offers for driver 0x%08x left as they are: %s", driver, why);
    };
    if (!g_mode)
        return skip("no start season view is active (not a Progressive wizard below 2026)");
    if (!contracts)
        return skip("no contracts component");
    if (uintptr_t mode = rd<uintptr_t>(contracts + kContractsGameMode); mode != g_mode) {
        if (g_offer_logged++ < 16)
            swofty::log("[progressive] cards: offers for driver 0x%08x left as they are: their game mode 0x%llx is not "
                        "the dressed one 0x%llx",
                        driver, static_cast<unsigned long long>(mode), static_cast<unsigned long long>(g_mode));
        return;
    }
    swap_cars("offers built", true);
    if (std::string_view(team_moves.get()) != "follow_driver") {
        swofty::log("[progressive] cards: Team Select %d: team moves %.*s, the game's offers stand", g_year,
                    static_cast<int>(team_moves.get().size()), team_moves.get().data());
        return;
    }
    uint32_t team = pc::kNoKey;
    size_t before = 0;
    if (const char *why = lock_offer(contracts, driver, team, before))
        swofty::log("[progressive] cards: Team Select %d: follow_driver: driver 0x%08x not locked: %s", g_year, driver,
                    why);
    else
        swofty::log("[progressive] cards: Team Select %d: follow_driver: driver 0x%08x can sign only for his %d team "
                    "0x%08x (%zu offer(s) before)",
                    g_year, driver, g_year, team, before);
}

// Team Select in the new-career wizard (0x141873a30 with +0xc68 set, the driver from the Choose
// Driver pick) lists only the picked driver's team when his line-up row (the game mode's +0x2e0,
// matched by driver key) has one, and every team when it has none; Game_DriverContracts_BuildOffers
// is not called there. So right after the pick his row's team (+0x58) becomes his start season
// team from the view in follow_driver (open when the view does not seat him), and none in choose.
void gate_team(uintptr_t vm)
{
    auto skip = [](const char *why) {
        if (g_offer_logged++ < 24)
            swofty::log("[progressive] cards: Team Select %d: team choice left to the game: %s", g_year, why);
    };
    uint32_t driver = rd<uint32_t>(vm + kVmDriver);
    if (pc::none(driver))
        return skip("no picked driver at the view model");
    uintptr_t b = rd<uintptr_t>(g_mode + kModeEntrants), e = rd<uintptr_t>(g_mode + kModeEntrants + 8);
    if (!b || e < b || (e - b) % sizeof(EntrantRecord) || (e - b) / sizeof(EntrantRecord) > kMaxRows ||
        !swofty::mem::readable(b, e - b))
        return skip("the game mode has no line-up");
    uintptr_t row = 0;
    for (uintptr_t p = b; p < e && !row; p += sizeof(EntrantRecord)) {
        const auto &r = *reinterpret_cast<const EntrantRecord *>(p);
        if (r.id_lo == (driver & 0xff) && r.id_hi == (driver >> 16))
            row = p;
    }
    if (!row)
        return skip("the picked driver has no line-up row");
    const auto &r = *reinterpret_cast<const EntrantRecord *>(row);
    bool follow = std::string_view(team_moves.get()) == "follow_driver";
    const EntrantRecord *v = view_row(driver);
    uint32_t want = follow && v && pc::seated(*v) ? v->team_stat_id : pc::kNoKey;
    const char *what = !follow                   ? "choose: every team open"
                       : pc::none(want)          ? "follow_driver: no start season seat, every team open"
                                                 : "follow_driver: only his start season team";
    std::lock_guard<std::mutex> g(g_swap_lock);
    if (g_gated_driver == driver && r.team_stat_id == want)
        return;
    g_gated_driver = driver;
    if (r.team_stat_id == want) {
        swofty::log("[progressive] cards: Team Select %d: driver 0x%08x, %s (his line-up team 0x%08x already is)", g_year,
                    driver, what, want);
        return;
    }
    Swap w{row, r.id_lo, r.id_hi, r.team_stat_id, want};
    if (!put<uint32_t>(row + kRowTeam, want))
        return skip("the line-up row write failed");
    g_team_swaps.push_back(w);
    swofty::log("[progressive] cards: Team Select %d: driver 0x%08x, %s (line-up team 0x%08x -> 0x%08x until Team Select "
                "is left)",
                g_year, driver, what, w.was, want);
}

void on_event(const SwoftyEvent *ev, void *)
{
    if (pc::takes_back(ev->name, ev->a)) {
        take_back(ev->name, pc::restores_rows(ev->name));
    } else if (ev->name == pc::kEventLoaded && g_mode) {
        auto *st = static_cast<const SwoftyCareerState *>(ev->data);
        if (st && st->key[0]) {
            take_back("career.loaded of a saved career", false);
            return;
        }
        g_live = true;
        swap_cars("career.loaded", false);
    }
}

bool progressive_wizard(uintptr_t vm)
{
    uintptr_t wizard = vm ? rd<uintptr_t>(vm + kVmWizard) : 0;
    return wizard && text_at(rd<uintptr_t>(wizard + kWizardGroup)) == kGroup;
}

struct Hidden {
    uintptr_t list = 0, end = 0;
};

} // namespace

SWOFTY_VTABLE(vt_Game_DriverCareerDriverSelectViewModel, 13, void *, void *vm)
{
    auto at = reinterpret_cast<uintptr_t>(vm);
    if (!progressive_wizard(at)) {
        take_back("another wizard's Choose Driver", true);
        return orig(vm);
    }
    uintptr_t wizard = rd<uintptr_t>(at + kVmWizard);
    uintptr_t entrants = rd<uintptr_t>(at + kVmEntrants);
    uintptr_t holder = rd<uintptr_t>(rd<uintptr_t>(at + kVmFlow) + kFlowHolder);
    uintptr_t mode = holder ? rd<uintptr_t>(holder + kHolderGameMode) : 0;
    uintptr_t career_list = entrants ? list_at(entrants, pc::kCareerList) : 0;
    uintptr_t setup = mode ? rd<uintptr_t>(mode + kModeSetupList) : 0;
    int year = suffix_year(wizard);
    restore_cars("Choose Driver");
    if (g_mode && g_mode != mode)
        take_back("Choose Driver of another game mode", true);
    bool live = g_live && g_mode == mode;
    bool ours = career_list && (setup == career_list || setup == view());
    if (setup == view() && career_list)
        put<uintptr_t>(mode + kModeSetupList, career_list);
    g_mode = 0;
    if (!ours && g_logged++ < 8)
        swofty::log("[progressive] cards: Choose Driver: the game mode's setup list 0x%llx is not f1_career_progressive "
                    "0x%llx; cards left as they are",
                    static_cast<unsigned long long>(setup), static_cast<unsigned long long>(career_list));
    if (ours && year >= pc::kFirstYear && year <= pc::kLastListYear)
        dress(at, mode, entrants, career_list, year);
    if (g_mode)
        pg::wizard_ratings::show(g_year, "Choose Driver populate");
    else
        pg::wizard_ratings::restore("Choose Driver without a start season view", true);
    g_live = live && g_mode == mode;

    Hidden hidden[std::size(pc::kHiddenLists)];
    size_t hid = 0;
    for (size_t i = 0; ours && i < std::size(pc::kHiddenLists); ++i) {
        uintptr_t list = list_at(entrants, pc::kHiddenLists[i]);
        uintptr_t end = list ? rd<uintptr_t>(list + kListEnd) : 0;
        if (list && end && list != career_list && put<uintptr_t>(list + kListEnd, rd<uintptr_t>(list + kListBegin)))
            hidden[hid++] = {list, end};
    }
    void *r = orig(vm);
    for (size_t i = 0; i < hid; ++i)
        put<uintptr_t>(hidden[i].list + kListEnd, hidden[i].end);
    if (ours && g_logged++ < 16) {
        uintptr_t cb = rd<uintptr_t>(at + kVmCardsBegin), ce = rd<uintptr_t>(at + kVmCardsEnd);
        swofty::log("[progressive] cards: Choose Driver %d populated %ld card(s), %zu of %zu list(s) left out (the driver "
                    "pool and F2)",
                    year, cb && ce >= cb ? static_cast<long>((ce - cb) / kCardSize) : -1L, hid,
                    std::size(pc::kHiddenLists));
    }
    return r;
}

SWOFTY_VTABLE(vt_Game_DriverCareerDriverSelectViewModel, 10, void *, void *vm, void *cmd, void *data)
{
    if (g_mode && progressive_wizard(reinterpret_cast<uintptr_t>(vm)))
        pg::wizard_ratings::show(g_year, "Choose Driver command");
    void *r = orig(vm, cmd, data);
    if (!g_mode || !progressive_wizard(reinterpret_cast<uintptr_t>(vm)))
        return r;
    if (!g_live && swofty::career::component_by_desc(swofty::game::k_Career_CareerComponentDesc.get())) {
        g_live = true;
        swofty::log("[progressive] cards: Team Select %d: the Choose Driver pick made the career component", g_year);
    }
    if (g_live) {
        gate_team(reinterpret_cast<uintptr_t>(vm));
        swap_cars("Choose Driver command", true);
    }
    return r;
}

SWOFTY_WRAP(Game_DriverContracts_BuildOffers, void, void *contracts, uint32_t driver)
{
    orig(contracts, driver);
    team_select(reinterpret_cast<uintptr_t>(contracts), driver);
}

// The start season of the active view (2016-2025), 0 when no Progressive wizard is dressed.
extern "C" __declspec(dllexport) int ProgressiveCardsStartYear(void)
{
    int year = g_year;
    return g_mode && year >= pc::kFirstYear && year <= pc::kLastListYear ? year : 0;
}

// progressive_numbers.dll: the start season number of a driver (key: id byte | id word << 16) in the
// active view, -1 when no Progressive wizard is dressed, the view does not hold him or has no number.
extern "C" __declspec(dllexport) int ProgressiveCardsNumber(uint32_t driver)
{
    if (!ProgressiveCardsStartYear())
        return -1;
    const EntrantRecord *v = view_row(driver);
    return v && v->car_number != pc::kNoNumber ? v->car_number : -1;
}

SWOFTY_ON_LOAD
{
    uint32_t token = 0;
    for (std::string_view ev : pc::kEvents) {
        swofty::events::Result r = swofty::events::subscribe(ev, on_event, nullptr, &token);
        if (r != swofty::events::Result::ok)
            swofty::log("[progressive] cards: subscribe %.*s: %s; the view is not taken back on it",
                        static_cast<int>(ev.size()), ev.data(), swofty::events::name(r));
    }
    swofty::events::Result l = swofty::events::subscribe(pc::kEventLoaded, on_event, nullptr, &token);
    if (l != swofty::events::Result::ok)
        swofty::log("[progressive] cards: subscribe career.loaded: %s; Team Select's cars are dressed only at a Choose "
                    "Driver command", swofty::events::name(l));
    swofty::log("[progressive] cards: Choose Driver and Team Select show the start season's drivers, looks and cars");
}

SWOFTY_ADDON()
