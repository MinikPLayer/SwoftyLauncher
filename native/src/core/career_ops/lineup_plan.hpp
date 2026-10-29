#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// career::apply_lineup's planner for a career.season record (request form "season"): pure logic over
// an abstract view of the career's entrant rows, so native/test builds it on the host. The executor
// (career_ops.cpp) writes the plan through the game's Driver Career sequence.
//
//   slot N       the data group's slot table (set_slots): slot N races as team stat slot_teams[N-1]
//   seat k       a race seat of that team: the row with a car, the team and team position k
//   target       a seat's driver from the record, resolved to a stat id (-1: not in the drivers table)
//
// Rules, seat by seat in record order: a slot outside the table, a driver not in the drivers table, a
// driver without a single career row, a driver already targeted at an earlier seat, the player's row
// (never moved) and the player's seat (never given away) are refused; a driver already on his seat is
// unchanged; any other driver is moved. A race-seat holder of a seat that got a target, who is not the
// player and is targeted nowhere, is released. Mid-season changes are not planned here.

namespace career_ops::plan {

constexpr int kSeats = 2;

struct Row {
    uint32_t driver = 0;
    int team = -1;
    int position = -1;
    bool car = false;
    bool human = false;
};

struct Team {
    int slot = 0;
    std::array<std::string, kSeats> refs{};
    std::array<int, kSeats> drivers{-1, -1};
};

struct Input {
    std::string data_group;
    bool have_slots = false;
    std::vector<uint32_t> slot_teams;
    std::vector<Team> teams;
};

struct Move {
    uint32_t driver = 0; // stat id
    int team = -1;
    int seat = 0;
};

struct Plan {
    std::vector<Move> moves;
    std::vector<uint32_t> releases; // stat ids
    size_t unchanged = 0;
    std::vector<std::string> refusals;
    int player_team = -1;
};

inline uint32_t stat_of(uint32_t key)
{
    return key >> 16;
}

inline bool driver_key(uint32_t key, int stat)
{
    return stat >= 0 && stat_of(key) == static_cast<uint32_t>(stat) && (key & 0xffu) == 0;
}

inline int single_row(const std::vector<Row> &rows, int stat)
{
    int hit = -1;
    for (size_t i = 0; i < rows.size(); ++i)
        if (driver_key(rows[i].driver, stat)) {
            if (hit >= 0)
                return -2;
            hit = static_cast<int>(i);
        }
    return hit;
}

inline int holder_of(const std::vector<Row> &rows, int team, int seat)
{
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].car && rows[i].team == team && rows[i].position == seat)
            return static_cast<int>(i);
    return -1;
}

inline std::string seat_name(int slot, int seat)
{
    return "slot " + std::to_string(slot) + " seat " + std::to_string(seat + 1);
}

inline Plan plan(const Input &in, const std::vector<Row> &rows)
{
    Plan p;
    for (auto &r : rows)
        if (r.human && r.team >= 0)
            p.player_team = r.team;
    struct Taken {
        uint32_t stat;
        int slot, seat;
    };
    std::vector<Taken> taken;
    struct Seat {
        int team, seat;
    };
    std::vector<Seat> targeted;
    for (auto &t : in.teams)
        for (int k = 0; k < kSeats; ++k) {
            if (t.refs[k].empty())
                continue;
            std::string where = seat_name(t.slot, k);
            if (!in.have_slots) {
                p.refusals.push_back(where + ": " + t.refs[k] + ": no slot table for data group " +
                                     (in.data_group.empty() ? "(none)" : in.data_group));
                continue;
            }
            if (t.slot < 1 || static_cast<size_t>(t.slot) > in.slot_teams.size()) {
                p.refusals.push_back(where + ": " + t.refs[k] + ": the slot table of " + in.data_group + " has " +
                                     std::to_string(in.slot_teams.size()) + " slot(s)");
                continue;
            }
            int team = static_cast<int>(in.slot_teams[static_cast<size_t>(t.slot - 1)]);
            int stat = t.drivers[k];
            if (stat < 0) {
                p.refusals.push_back(where + ": " + t.refs[k] + " is not in the drivers table");
                continue;
            }
            int idx = single_row(rows, stat);
            if (idx < 0) {
                p.refusals.push_back(where + ": " + t.refs[k] + (idx == -2 ? " has more than one career row" : " has no career row"));
                continue;
            }
            auto dup = std::find_if(taken.begin(), taken.end(), [&](const Taken &x) { return x.stat == static_cast<uint32_t>(stat); });
            if (dup != taken.end()) {
                p.refusals.push_back(where + ": " + t.refs[k] + " is already seated at " + seat_name(dup->slot, dup->seat));
                continue;
            }
            int h = holder_of(rows, team, k);
            if (h >= 0 && rows[h].human && h != idx) {
                p.refusals.push_back(where + ": " + t.refs[k] + ": the player's seat");
                continue;
            }
            const Row &r = rows[static_cast<size_t>(idx)];
            bool there = r.car && r.team == team && r.position == k;
            if (r.human && !there) {
                p.refusals.push_back(where + ": " + t.refs[k] + " is the player, who is never moved");
                continue;
            }
            taken.push_back({static_cast<uint32_t>(stat), t.slot, k});
            targeted.push_back({team, k});
            if (there)
                ++p.unchanged;
            else
                p.moves.push_back({stat_of(r.driver), team, k});
        }
    for (auto &s : targeted) {
        int h = holder_of(rows, s.team, s.seat);
        if (h < 0 || rows[h].human)
            continue;
        uint32_t stat = stat_of(rows[h].driver);
        bool kept = std::any_of(taken.begin(), taken.end(), [&](const Taken &x) { return x.stat == stat; });
        bool listed = std::find(p.releases.begin(), p.releases.end(), stat) != p.releases.end();
        if (!kept && !listed)
            p.releases.push_back(stat);
    }
    return p;
}

struct Given {
    uint32_t driver = 0; // stat id
    int team = -1;
    int seat = 0;
};

// apply_teams with a line-up in the request: the line-up's seats stand and the record's seats join them. A record
// move the line-up makes too is left to it; a record move whose driver the line-up seats elsewhere, or into a seat
// the line-up gives another driver, is refused; a release of a driver the line-up seats is dropped.
inline void defer_to(Plan &p, const std::vector<Given> &lineup)
{
    auto seat_text = [](int team, int seat) { return "team " + std::to_string(team) + " seat " + std::to_string(seat + 1); };
    std::vector<Move> kept;
    for (auto &m : p.moves) {
        auto same = std::find_if(lineup.begin(), lineup.end(), [&](const Given &g) { return g.driver == m.driver; });
        if (same != lineup.end()) {
            if (same->team != m.team || same->seat != m.seat)
                p.refusals.push_back("driver " + std::to_string(m.driver) + ": the request's line-up seats him at " +
                                     seat_text(same->team, same->seat) + ", not the season record's " +
                                     seat_text(m.team, m.seat));
            continue;
        }
        auto other = std::find_if(lineup.begin(), lineup.end(), [&](const Given &g) { return g.team == m.team && g.seat == m.seat; });
        if (other != lineup.end()) {
            p.refusals.push_back(seat_text(m.team, m.seat) + ": the request's line-up gives it to driver " +
                                 std::to_string(other->driver) + ", not the season record's driver " + std::to_string(m.driver));
            continue;
        }
        kept.push_back(m);
    }
    p.moves = std::move(kept);
    p.releases.erase(std::remove_if(p.releases.begin(), p.releases.end(),
                                    [&](uint32_t d) {
                                        return std::any_of(lineup.begin(), lineup.end(), [&](const Given &g) { return g.driver == d; });
                                    }),
                     p.releases.end());
}

} // namespace career_ops::plan
