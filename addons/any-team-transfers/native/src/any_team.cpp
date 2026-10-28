// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Any Team Transfers (any_team.dll): in a Driver Career contract negotiation every team makes the
// player an offer, so the negotiation's Team Select lets him sign for any of them.
//
// The Team Select team list builder (0x141873a30) lists the unique teams of the game mode's
// entrant list (+0xe18, 0x88-byte rows, team at +0x58), calls Game_DriverContracts_BuildOffers for
// the driver, then sets each team tile's is_enabled (tile +0x110, made in 0x1418734b0) true when
// the team is in his offers (DriverCareerContractsManagerComponent +0x3c8..+0x3d0, 0x48-byte records
// keyed by driver, team keys at +0x10..+0x18) and false when it is not. Stock BuildOffers ranks his
// team and picks two or three teams at random from a window ranked near it, plus his own team when
// it renews him. After the stock call the wrap appends every other team of that same entrant list
// to his offers, through the record's own allocator as the stock append does (0x140d810d2), so
// every tile the Team Select shows is enabled. His own team stays out after he started talks with
// another team (contract removedReason 10), as in the stock offers. With no offer record (he is not
// negotiating) nothing changes. The wrap runs at priority -10, inside every other BuildOffers wrap:
// Progressive Career's follow_driver lock (progressive_cards.dll, priority 0) cuts the offers to
// the driver's real team after this has run, so a follow career still signs only there. Teams the
// builder drops from its list before making tiles (those the +0xcb0 component holds for another
// human driver) stay dropped. Before the first call the wrap checks that BuildOffers and
// Game_CareerSetup_ResetEntrants still read and write those fields where this was written against
// (offers_logic.hpp, the same on the Steam and EA builds); if not, every call stays stock and says
// why once.

#include <cstring>
#include <string>
#include <vector>

#include <swofty/game.hpp>
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include "offers_logic.hpp"

namespace {

using namespace swofty::game;
using namespace anyteam;

constexpr size_t kBuildOffersScan = 0x900;
constexpr size_t kResetEntrantsScan = 0x200;
constexpr uintptr_t kAllocVtAlloc = 0x18;
constexpr uintptr_t kAllocVtFree = 0x38;
constexpr size_t kMaxRows = 256;

enum class Code { unchecked, ok, changed };
Code g_code = Code::unchecked;
int g_logged = 0;

bool note()
{
    return g_logged < 20 && ++g_logged;
}

template <class T> T rdv(uintptr_t a, T def = T{})
{
    T v{};
    return swofty::mem::read(a, v) ? v : def;
}

template <class T> void put(uintptr_t a, const T &v)
{
    std::memcpy(reinterpret_cast<void *>(a), &v, sizeof v);
}

std::string check(const char *name, uintptr_t fn, size_t n, std::span<const Pattern> patterns)
{
    if (!fn || !swofty::mem::readable(fn, n))
        return std::string(name) + " is not readable";
    std::string miss = missing_pattern(reinterpret_cast<const uint8_t *>(fn), n, patterns);
    return miss.empty() ? miss : std::string(name) + " no longer has the " + miss + " this addon expects";
}

bool code_ok()
{
    if (g_code != Code::unchecked)
        return g_code == Code::ok;
    std::string why = check("Game_DriverContracts_BuildOffers", Game_DriverContracts_BuildOffers.address(),
                            kBuildOffersScan, kBuildOffersPatterns);
    if (why.empty())
        why = check("Game_CareerSetup_ResetEntrants", Game_CareerSetup_ResetEntrants.address(), kResetEntrantsScan,
                    kResetEntrantsPatterns);
    g_code = why.empty() ? Code::ok : Code::changed;
    if (g_code == Code::changed)
        swofty::log("[any-team] %s; contract offers stay stock", why.c_str());
    return g_code == Code::ok;
}

uintptr_t offer_record(uintptr_t c, uint32_t driver)
{
    uintptr_t b = rdv<uintptr_t>(c + kOffersBegin), e = rdv<uintptr_t>(c + kOffersEnd);
    if (!b || e <= b || (e - b) % kOfferStride || (e - b) / kOfferStride > 64 || !swofty::mem::readable(b, e - b))
        return 0;
    for (uintptr_t p = b; p < e; p += kOfferStride)
        if (same_key(rdv<uint32_t>(p), driver))
            return p;
    return 0;
}

uintptr_t contract_record(uintptr_t c, uint32_t driver)
{
    uintptr_t owner = rdv<uintptr_t>(c + offsetof(DriverCareerContractsManagerComponent, contract_owner));
    uintptr_t b = owner ? rdv<uintptr_t>(owner + offsetof(DriverCareerContractOwner, contracts_begin)) : 0;
    uintptr_t e = owner ? rdv<uintptr_t>(owner + offsetof(DriverCareerContractOwner, contracts_end)) : 0;
    if (!b || e <= b || (e - b) % 16 || (e - b) / 16 > kMaxRows)
        return 0;
    for (uintptr_t p = b; p < e; p += 16)
        if (uintptr_t rec = rdv<uintptr_t>(p + 8); rec && same_key(rdv<uint32_t>(rec + offsetof(DriverContract, driver)), driver))
            return rec;
    return 0;
}

bool listed_teams(uintptr_t c, std::vector<uint32_t> &out)
{
    uintptr_t entity = rdv<uintptr_t>(c + offsetof(DriverCareerContractsManagerComponent, entity));
    uintptr_t list = entity ? rdv<uintptr_t>(entity + kEntrantList) : 0;
    uintptr_t b = list ? rdv<uintptr_t>(list + kEntrantRowsBegin) : 0;
    uintptr_t e = list ? rdv<uintptr_t>(list + kEntrantRowsEnd) : 0;
    if (!b || e <= b || (e - b) % sizeof(EntrantRecord) || (e - b) / sizeof(EntrantRecord) > kMaxRows ||
        !swofty::mem::readable(b, e - b))
        return false;
    for (uintptr_t p = b; p < e; p += sizeof(EntrantRecord))
        out.push_back(rdv<uint32_t>(p + offsetof(EntrantRecord, team_stat_id)));
    return true;
}

bool push_team(uintptr_t rec, uint32_t team)
{
    uintptr_t b = rdv<uintptr_t>(rec + kOfferTeamsBegin), e = rdv<uintptr_t>(rec + kOfferTeamsEnd);
    size_t cap = rdv<size_t>(rec + kOfferTeamsCap);
    if (e < b || (e - b) % 4 || (e - b) / 4 > cap || cap > 1024)
        return false;
    size_t n = (e - b) / 4;
    if (n < cap && b) {
        put(e, team);
        put(rec + kOfferTeamsEnd, e + 4);
        return true;
    }
    uintptr_t alloc = rdv<uintptr_t>(rec + kOfferAlloc);
    if (!swofty::mem::object_like(alloc))
        return false;
    uintptr_t vt = rdv<uintptr_t>(alloc);
    uintptr_t grow_fn = rdv<uintptr_t>(vt + kAllocVtAlloc), free_fn = rdv<uintptr_t>(vt + kAllocVtFree);
    if (!swofty::mem::in_image(grow_fn) || !swofty::mem::in_image(free_fn))
        return false;
    auto grow = reinterpret_cast<uintptr_t (*)(uintptr_t, size_t, uint32_t)>(grow_fn);
    auto release = reinterpret_cast<void (*)(uintptr_t, uintptr_t)>(free_fn);
    size_t want = n + n / 2 > n + 1 ? n + n / 2 : n + 1;
    uintptr_t fresh = grow(alloc, want * 4, 2);
    if (!fresh)
        return false;
    if (n)
        std::memcpy(reinterpret_cast<void *>(fresh), reinterpret_cast<const void *>(b), n * 4);
    put(fresh + n * 4, team);
    if (b)
        release(alloc, b);
    put(rec + kOfferTeamsBegin, fresh);
    put(rec + kOfferTeamsEnd, fresh + (n + 1) * 4);
    put(rec + kOfferTeamsCap, want);
    return true;
}

std::string offered(uintptr_t rec)
{
    auto b = reinterpret_cast<const uint32_t *>(rdv<uintptr_t>(rec + kOfferTeamsBegin));
    auto e = reinterpret_cast<const uint32_t *>(rdv<uintptr_t>(rec + kOfferTeamsEnd));
    return std::to_string(e - b) + " (" + team_list(b, e) + ")";
}

} // namespace

SWOFTY_WRAP_PRIORITY(-10, Game_DriverContracts_BuildOffers, void, void *contracts, uint32_t driver)
{
    orig(contracts, driver);
    auto c = reinterpret_cast<uintptr_t>(contracts);
    if (!c || !code_ok())
        return;
    uintptr_t rec = offer_record(c, driver);
    if (!rec) {
        if (note())
            swofty::log("[any-team] driver %u: no offer record (not negotiating); nothing added", driver >> 16);
        return;
    }
    std::vector<uint32_t> listed;
    if (!listed_teams(c, listed)) {
        if (note())
            swofty::log("[any-team] driver %u: no entrant list on the game mode; offers stay stock", driver >> 16);
        return;
    }
    uintptr_t contract = contract_record(c, driver);
    uint32_t own = contract ? rdv<uint32_t>(contract + offsetof(DriverContract, team)) : kNoTeam;
    bool talks = contract && rdv<uint8_t>(contract + offsetof(DriverContract, state)) == kRemovedTalks;
    std::string stock = offered(rec);
    auto b = reinterpret_cast<const uint32_t *>(rdv<uintptr_t>(rec + kOfferTeamsBegin));
    auto e = reinterpret_cast<const uint32_t *>(rdv<uintptr_t>(rec + kOfferTeamsEnd));
    std::vector<uint32_t> add = teams_to_add(b, e, listed, own, talks);
    size_t added = 0;
    for (uint32_t t : add)
        if (push_team(rec, t))
            ++added;
    if (note())
        swofty::log("[any-team] driver %u: stock offers %s; added %zu of %zu team(s); offers now %s (own team %u%s)",
                    driver >> 16, stock.c_str(), added, add.size(), offered(rec).c_str(), own >> 16,
                    talks ? ", left out: talks with another team" : "");
}

SWOFTY_ADDON()
