// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Any Team Transfers - the pure parts of any_team.cpp, built on the host by test/run.sh.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace anyteam {

constexpr uint32_t kNoTeam = 0x3fff0000u;

// DriverCareerContractsManagerComponent: the per-driver offers Game_DriverContracts_BuildOffers
// (0x140d80ef0) fills, 0x48-byte records {driver key, NeAllocator *, u32 team keys begin/end, capacity}.
constexpr uintptr_t kOffersBegin = 0x3c8;
constexpr uintptr_t kOffersEnd = 0x3d0;
constexpr size_t kOfferStride = 0x48;
constexpr uintptr_t kOfferAlloc = 0x08;
constexpr uintptr_t kOfferTeamsBegin = 0x10;
constexpr uintptr_t kOfferTeamsEnd = 0x18;
constexpr uintptr_t kOfferTeamsCap = 0x20;

// The teams the Team Select lists (FUN_141873a30): in a career, the unique team keys of the game
// mode's entrants, which Game_CareerSetup_ResetEntrants copies into the line-up at +0x2e0..+0x2e8
// (CareerEntity entrants, 0x88-byte rows, team at +0x58); without a CareerSetupComponent, those of
// the entrant list at +0xe18 (func_0x142457610 over its rows at +0x08..+0x10), which
// ResetEntrants stores there. A loaded career leaves +0xe18 unset.
constexpr uintptr_t kEntrantList = 0xe18;
constexpr uintptr_t kEntrantRowsBegin = 0x08;
constexpr uintptr_t kEntrantRowsEnd = 0x10;

constexpr uint8_t kRemovedTalks = 10;

inline bool same_key(uint32_t a, uint32_t b)
{
    return ((a ^ b) & 0xffff00ffu) == 0;
}

inline bool no_team(uint32_t team)
{
    return (team & 0xffff0000u) == kNoTeam;
}

inline bool has_team(const uint32_t *b, const uint32_t *e, uint32_t team)
{
    for (const uint32_t *p = b; p < e; ++p)
        if (same_key(*p, team))
            return true;
    return false;
}

// The listed teams to add to an offer list, in list order: each team once, none already offered,
// no empty team, and his own team left out when skip_own.
inline std::vector<uint32_t> teams_to_add(const uint32_t *ob, const uint32_t *oe, std::span<const uint32_t> listed,
                                          uint32_t own, bool skip_own)
{
    std::vector<uint32_t> add;
    for (uint32_t t : listed) {
        if (no_team(t) || has_team(ob, oe, t) || has_team(add.data(), add.data() + add.size(), t))
            continue;
        if (skip_own && same_key(t, own))
            continue;
        add.push_back(t);
    }
    return add;
}

// What the wrap relies on, as the Steam and EA builds both encode it.
struct Pattern {
    const char *what;
    uint8_t bytes[20];
    size_t n;
};

// Game_DriverContracts_BuildOffers: the offers list load after the entry (the one at +0x13 sits in
// bytes a hook may relocate), the 0x48 record stride, and its own append to a record's team vector
// at 0x140d810d2 (size against capacity, the allocator's alloc with alignment 2 and its free).
inline constexpr Pattern kBuildOffersPatterns[] = {
    {"offers list", {0x48, 0x8b, 0x96, 0xc8, 0x03, 0x00, 0x00}, 7},
    {"offer record stride", {0x49, 0x83, 0xc5, 0x48}, 4},
    {"offer team vector",
     {0x49, 0x8b, 0x55, 0x18, 0x48, 0x89, 0xd1, 0x49, 0x2b, 0x4d, 0x10, 0x48, 0xc1, 0xf9, 0x02, 0x49, 0x3b, 0x4d, 0x20},
     19},
    {"offer allocator alloc", {0x49, 0x8b, 0x4d, 0x08, 0x48, 0x8b, 0x01, 0x41, 0xb8, 0x02, 0x00, 0x00, 0x00, 0xff, 0x50, 0x18},
     16},
    {"offer allocator free", {0x49, 0x8b, 0x4d, 0x08, 0x48, 0x8b, 0x01, 0xff, 0x50, 0x38}, 10},
};

// Game_CareerSetup_ResetEntrants: the line-up clear (mov rax,[rbx+0x2e0]; mov [rbx+0x2e8],rax) and
// mov [rax+0xe18],rdx, the entrant list store.
inline constexpr Pattern kResetEntrantsPatterns[] = {
    {"line-up", {0x48, 0x8b, 0x83, 0xe0, 0x02, 0x00, 0x00, 0x48, 0x89, 0x83, 0xe8, 0x02, 0x00, 0x00}, 14},
    {"entrant list", {0x48, 0x89, 0x90, 0x18, 0x0e, 0x00, 0x00}, 7},
};

inline bool contains(const uint8_t *code, size_t n, const Pattern &p)
{
    if (!code || n < p.n)
        return false;
    for (size_t i = 0; i + p.n <= n; ++i)
        if (!std::memcmp(code + i, p.bytes, p.n))
            return true;
    return false;
}

// Empty when every pattern is in the code, else the first one missing.
inline std::string missing_pattern(const uint8_t *code, size_t n, std::span<const Pattern> patterns)
{
    for (const Pattern &p : patterns)
        if (!contains(code, n, p))
            return p.what;
    return {};
}

inline std::string team_list(const uint32_t *b, const uint32_t *e)
{
    std::string s;
    for (const uint32_t *p = b; p < e; ++p) {
        if (!s.empty())
            s += ' ';
        s += std::to_string(*p >> 16);
    }
    return s.empty() ? "none" : s;
}

} // namespace anyteam
