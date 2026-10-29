#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "lineup_plan.hpp"

// career::set_seat's planner: pure logic over the career's entrant rows (lineup_plan.hpp's Row), so
// native/test builds it on the host. The executor is apply_lineup's (career_ops.cpp execute()). The seat
// change is Career Mode Editor's Driver Career drivers.swap (addons/career-editor/native/src/drivers.cpp):
// a race driver and the seat's driver exchange seats; a free agent takes the seat and its driver leaves the
// grid, after the incoming driver, as the game's replace (0x140d766b0) orders it.
//
//   seat k       a race seat of the team: the row with a car, the team and team position k (0 or 1)
//   free agent   a row with no team; a row with a team and no race seat is a reserve
//
// Refused: a seat other than 0 or 1, a driver without a single career row, the player (never moved), a
// reserve (R&D is per race driver), the player's seat (never given away), and an empty seat a race driver
// would leave his own seat empty for. A driver already on the seat is unchanged.

namespace career_ops::seat {

enum class Holder { none, swapped, released };

inline const char *name(Holder h)
{
    return h == Holder::swapped ? "swapped" : h == Holder::released ? "released" : "none";
}

struct Plan {
    std::string refusal;
    bool unchanged = false;
    uint32_t driver = 0;
    int team = -1, seat = 0;
    int holder = -1;
    Holder what = Holder::none;
    int from_team = -1, from_seat = -1;
    int player_team = -1;
};

inline bool seated(const plan::Row &r)
{
    return r.car && r.team >= 0 && r.position >= 0;
}

inline Plan plan(const std::vector<plan::Row> &rows, uint32_t driver, int team, int seat)
{
    Plan p;
    p.driver = driver;
    p.team = team;
    p.seat = seat;
    for (auto &r : rows)
        if (r.human && r.team >= 0)
            p.player_team = r.team;
    if (seat < 0 || seat >= plan::kSeats) {
        p.refusal = "seat must be 0 or 1";
        return p;
    }
    if (team < 0 || team >= 0x3fff) {
        p.refusal = "no team for the seat";
        return p;
    }
    int idx = -1;
    for (size_t i = 0; i < rows.size(); ++i)
        if ((rows[i].driver >> 16) == driver && (rows[i].driver & 0xff) == 0) {
            if (idx >= 0) {
                p.refusal = "the driver has more than one career row";
                return p;
            }
            idx = static_cast<int>(i);
        }
    if (idx < 0) {
        p.refusal = "the driver has no career row";
        return p;
    }
    const plan::Row &d = rows[idx];
    if (d.human) {
        p.refusal = "the player is never moved";
        return p;
    }
    if (seated(d) && d.team == team && d.position == seat) {
        p.unchanged = true;
        return p;
    }
    if (d.team >= 0 && !seated(d)) {
        p.refusal = "the driver is a reserve (a team but no race seat); only race drivers and free agents take a seat";
        return p;
    }
    if (seated(d)) {
        p.from_team = d.team;
        p.from_seat = d.position;
    }
    for (auto &r : rows)
        if (seated(r) && r.team == team && r.position == seat) {
            if (r.human) {
                p.refusal = "the player's seat is never given away";
                return p;
            }
            p.holder = static_cast<int>(r.driver >> 16);
            break;
        }
    if (p.holder < 0) {
        if (seated(d)) {
            p.refusal = "the seat is empty and the driver's own seat would be left empty";
            return p;
        }
        p.what = Holder::none;
        return p;
    }
    p.what = seated(d) ? Holder::swapped : Holder::released;
    return p;
}

}
