// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career: the season line-up fit (DESIGN.md D7). Pure logic, no game access, so
// native/test builds it on the host.
//
// Input: the real line-up of year Y per career team slot (11 slots, two seats each; slot 10 is
// Manor in 2016 and Cadillac from 2026, empty in the 10-team years), the player and the slot he
// is seated at, driver ratings, the career's driver pool and, when known, the real line-ups of
// Y-1 and Y+1.
//
// Rules:
//   1. The player's real team in Y is his slot: the real line-ups stand as they are.
//   2. Otherwise the player takes the seat of his slot's real driver D on the seat he holds in the
//      career (player_seat: the teammate he kept at Team Select stays), or with no such seat the
//      lower-rated real driver (on a tie the seat-2 driver). D takes the player's real seat in Y
//      when the player raced in Y; otherwise D has no seat.
//   3. A driver never sits twice: a repeat in the input keeps its first seat.
//   4. Every seat still empty (slot 10 in the 10-team years, a seat the player's move left
//      empty, a repeat) gets a driver without a seat, best first: drivers who raced in Y but lost
//      their seat (D), then drivers who raced in Y-1 or Y+1, then everyone else in the pool, each
//      tier by rating (higher first), then by id. With `current` (a season already applied) a seat
//      keeps its present driver when he is still in the pool, so the fillers stay all season. A
//      closed slot (Input.closed: slot 10 in the 10-team years with ten_team_years on) gets no
//      driver and stays empty.
// A displaced driver who is not refitted ends up in `reserve`.
// Mid-season (Input.take >= 0): the player keeps the seat index of his slot he took at the season's
// start (take_seat on the season's opening line-up), so a change of that seat's real driver moves
// the newcomer to the player's real seat (or out), and a change elsewhere leaves the player's slot.

#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace progressive::lineup {

using Driver = uint32_t;
constexpr Driver kNone = 0xffffffffu;
constexpr int kSlots = 11;
constexpr int kSeats = 2;

using Grid = std::array<std::array<Driver, kSeats>, kSlots>;

inline Grid empty_grid()
{
    Grid g;
    for (auto &s : g)
        s.fill(kNone);
    return g;
}

struct Rating {
    Driver driver = kNone;
    float value = 0;
};

struct Input {
    Grid real = empty_grid();
    Driver player = kNone;
    int player_slot = -1;
    int player_seat = -1;
    const Grid *current = nullptr;
    std::vector<Rating> ratings;
    std::vector<Driver> pool;
    const Grid *prev = nullptr;
    const Grid *next = nullptr;
    int take = -1;
    std::array<bool, kSlots> closed{};
};

enum class Why { real, player, displaced, filler };

struct Seat {
    Driver driver = kNone;
    Why why = Why::real;
};

struct Output {
    std::array<std::array<Seat, kSeats>, kSlots> seats{};
    Driver displaced = kNone;
    std::vector<Driver> reserve;
    std::vector<std::string> notes;

    Driver at(int slot, int seat) const { return seats[slot][seat].driver; }
};

inline bool in_grid(const Grid &g, Driver d, int *slot = nullptr, int *seat = nullptr)
{
    if (d == kNone)
        return false;
    for (int s = 0; s < kSlots; ++s)
        for (int p = 0; p < kSeats; ++p)
            if (g[s][p] == d) {
                if (slot)
                    *slot = s;
                if (seat)
                    *seat = p;
                return true;
            }
    return false;
}

inline float rating_of(const std::vector<Rating> &r, Driver d)
{
    for (auto &x : r)
        if (x.driver == d)
            return x.value;
    return 0;
}

inline Grid dedupe(const Grid &in, std::vector<std::string> &notes)
{
    Grid out = empty_grid();
    std::vector<Driver> seen;
    for (int s = 0; s < kSlots; ++s)
        for (int p = 0; p < kSeats; ++p) {
            Driver d = in[s][p];
            if (d == kNone)
                continue;
            if (std::find(seen.begin(), seen.end(), d) != seen.end()) {
                notes.push_back("driver " + std::to_string(d) + " listed twice; slot " + std::to_string(s) + " seat " +
                                std::to_string(p) + " left for a filler");
                continue;
            }
            seen.push_back(d);
            out[s][p] = d;
        }
    return out;
}

inline int take_seat(const Grid &real, Driver player, int slot, const std::vector<Rating> &ratings, int held = -1)
{
    if (player == kNone || slot < 0 || slot >= kSlots)
        return -1;
    for (int p = 0; p < kSeats; ++p)
        if (real[slot][p] == player)
            return p;
    for (int p = 0; p < kSeats; ++p)
        if (real[slot][p] == kNone)
            return p;
    if (held >= 0 && held < kSeats)
        return held;
    return rating_of(ratings, real[slot][0]) < rating_of(ratings, real[slot][1]) ? 0 : 1;
}

inline Output fit(const Input &in)
{
    Output out;
    Grid real = dedupe(in.real, out.notes);
    Grid g = real;
    std::vector<Driver> lost;

    int rs = -1, rp = -1;
    bool raced = in_grid(real, in.player, &rs, &rp);
    bool seated = in.player_slot >= 0 && in.player_slot < kSlots;

    auto mark = [&](int s, int p, Why w) { out.seats[s][p].why = w; };

    if (in.player != kNone && !seated) {
        if (raced) {
            g[rs][rp] = kNone;
            out.notes.push_back("player has no career seat; his real seat goes to a filler");
        }
    } else if (in.player != kNone && !(raced && rs == in.player_slot)) {
        int S = in.player_slot;
        int take = in.take >= 0 && in.take < kSeats ? in.take : -1;
        if (take < 0)
            for (int p = 0; p < kSeats; ++p)
                if (g[S][p] == kNone) {
                    take = p;
                    break;
                }
        if (take < 0) {
            if (in.player_seat >= 0 && in.player_seat < kSeats) {
                take = in.player_seat;
            } else {
                float r0 = rating_of(in.ratings, g[S][0]), r1 = rating_of(in.ratings, g[S][1]);
                take = r0 < r1 ? 0 : 1;
            }
        }
        Driver d = g[S][take];
        if (raced)
            g[rs][rp] = kNone;
        g[S][take] = in.player;
        mark(S, take, Why::player);
        if (d != kNone) {
            out.displaced = d;
            if (raced) {
                g[rs][rp] = d;
                mark(rs, rp, Why::displaced);
                out.notes.push_back("driver " + std::to_string(d) + " takes the player's real seat (slot " +
                                    std::to_string(rs) + " seat " + std::to_string(rp) + ")");
            } else {
                lost.push_back(d);
                out.notes.push_back("driver " + std::to_string(d) + " loses his seat to the player, who did not race "
                                    "this year");
            }
        }
    }

    std::vector<Driver> pool;
    for (Driver d : in.pool)
        if (d != kNone && d != in.player && !in_grid(g, d) && std::find(pool.begin(), pool.end(), d) == pool.end())
            pool.push_back(d);
    for (Driver d : lost)
        if (std::find(pool.begin(), pool.end(), d) == pool.end())
            pool.push_back(d);
    auto tier = [&](Driver d) {
        if (std::find(lost.begin(), lost.end(), d) != lost.end() || in_grid(real, d))
            return 0;
        if ((in.prev && in_grid(*in.prev, d)) || (in.next && in_grid(*in.next, d)))
            return 1;
        return 2;
    };
    std::stable_sort(pool.begin(), pool.end(), [&](Driver a, Driver b) {
        int ta = tier(a), tb = tier(b);
        if (ta != tb)
            return ta < tb;
        float ra = rating_of(in.ratings, a), rb = rating_of(in.ratings, b);
        if (ra != rb)
            return ra > rb;
        return a < b;
    });

    std::vector<bool> taken(pool.size(), false);
    for (int s = 0; s < kSlots; ++s)
        for (int p = 0; p < kSeats; ++p) {
            if (g[s][p] != kNone)
                continue;
            if (in.closed[s])
                continue;
            size_t pick = pool.size();
            if (in.current) {
                Driver c = (*in.current)[s][p];
                for (size_t i = 0; i < pool.size() && pick == pool.size(); ++i)
                    if (!taken[i] && pool[i] == c)
                        pick = i;
            }
            for (size_t i = 0; i < pool.size() && pick == pool.size(); ++i)
                if (!taken[i])
                    pick = i;
            if (pick == pool.size()) {
                out.notes.push_back("slot " + std::to_string(s) + " seat " + std::to_string(p) +
                                    " stays empty: the pool has no driver left");
                continue;
            }
            taken[pick] = true;
            g[s][p] = pool[pick];
            mark(s, p, Why::filler);
        }

    for (int s = 0; s < kSlots; ++s)
        for (int p = 0; p < kSeats; ++p)
            out.seats[s][p].driver = g[s][p];
    for (Driver d : lost)
        if (!in_grid(g, d))
            out.reserve.push_back(d);
    return out;
}

inline std::string check(const Output &o)
{
    std::vector<Driver> seen;
    for (int s = 0; s < kSlots; ++s)
        for (int p = 0; p < kSeats; ++p) {
            Driver d = o.at(s, p);
            if (d == kNone)
                continue;
            if (std::find(seen.begin(), seen.end(), d) != seen.end())
                return "driver " + std::to_string(d) + " sits twice";
            seen.push_back(d);
        }
    for (Driver d : o.reserve)
        if (std::find(seen.begin(), seen.end(), d) != seen.end())
            return "reserve driver " + std::to_string(d) + " also has a seat";
    return {};
}

} // namespace progressive::lineup
