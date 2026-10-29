// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career: Choose Driver on the effective career.season records (standard library 1.6).
// The start season list the cards read (cards_logic.hpp build()) is the game's own list for that
// year, made from data/years.json when the career database was built. reseat() brings its rows to
// the seats of the effective years.json (records_logic.hpp overlay), so a layer that seats a driver
// elsewhere shows on Choose Driver as it does on the grid:
//   a driver whose seat differs from years.json takes the team, car and livery of that slot's rows
//   and the record's seat; his own row keeps his look and number, and a driver the list has no row
//   for gets one made from the slot's rows with the pool's number and no look;
//   a driver years.json seats and the effective seats no longer hold has no row; a driver the list gains takes
//   the place of a row the slot loses (same seat first), so the cards keep the list's team order.
// A seat driver the pool has no entry for (an added team's new driver) takes his stat id from driver_stat (the game's
// drivers table row of his label) and gets a row like a pool driver with none; without driver_stat he is left out.
// fill() gives a row with no look or number those of the same driver's row in another list (the career list, the
// driver pool), where the core's entrant for an added team's new driver carries his look and number.
// With the same years.json on both sides nothing changes. R is any record with the EntrantRecord
// field names this file reads. Pure logic, no game access, so native/test builds it on the host.

#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "cards_logic.hpp"
#include "season_logic.hpp"

namespace progressive::choose {

struct Stats {
    size_t moved = 0, added = 0, removed = 0;
    bool changed() const { return moved || added || removed; }
};

using Seat = std::pair<int, int>;

inline std::map<std::string, Seat> seats_of(const season::Year &y)
{
    std::map<std::string, Seat> out;
    for (int slot = 0; slot < season::kSlots; ++slot)
        for (const season::SeatDriver &d : y.teams[static_cast<size_t>(slot)].drivers)
            if (d.position >= 0 && d.position < 2)
                out[d.driver] = {slot, d.position};
    return out;
}

template <class R> size_t row_of(const std::vector<R> &rows, int stat)
{
    size_t any = rows.size();
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].id_hi != static_cast<uint16_t>(stat))
            continue;
        if (rows[i].id_lo == 0)
            return i;
        if (any == rows.size())
            any = i;
    }
    return any;
}

using DriverStat = std::function<int(const std::string &label)>;

template <class R>
Stats reseat(std::vector<R> &rows, const season::Data &base, const season::Data &effective, int year,
             const DriverStat &driver_stat = {})
{
    Stats st;
    const season::Year *by = base.year(year), *ey = effective.year(year);
    if (!by || !ey)
        return st;
    const std::map<std::string, Seat> was = seats_of(*by), now = seats_of(*ey);

    std::array<std::optional<R>, season::kSlots> slot_row;
    for (const auto &[label, at] : was) {
        const season::PoolDriver *p = base.driver(label);
        size_t i = p && p->stat >= 0 ? row_of(rows, p->stat) : rows.size();
        if (i != rows.size() && !slot_row[static_cast<size_t>(at.first)])
            slot_row[static_cast<size_t>(at.first)] = rows[i];
    }

    std::vector<std::pair<Seat, R>> added;
    std::set<size_t> moved_rows;
    for (const auto &[label, at] : now) {
        auto w = was.find(label);
        if (w != was.end() && w->second == at)
            continue;
        const season::PoolDriver *p = effective.driver(label);
        const std::optional<R> &tmpl = slot_row[static_cast<size_t>(at.first)];
        int stat = p ? p->stat : driver_stat ? driver_stat(label) : -1;
        if (stat < 0 || stat > 0xffff || !tmpl)
            continue;
        size_t i = row_of(rows, stat);
        R r;
        if (i != rows.size()) {
            r = rows[i];
            moved_rows.insert(i);
        } else {
            r = *tmpl;
            r.id_lo = 0;
            r.id_hi = static_cast<uint16_t>(stat);
            r.driver_customisation = cards::kNoKey;
            int number = -1;
            if (p) {
                auto n = p->numbers.find(year);
                number = n != p->numbers.end() ? n->second : p->number;
            }
            r.car_number = number >= 0 && number < 0xff ? static_cast<uint8_t>(number) : cards::kNoNumber;
        }
        r.team_stat_id = tmpl->team_stat_id;
        r.vehicle_ref = tmpl->vehicle_ref;
        r.vehicle_customisation = tmpl->vehicle_customisation;
        r.team_position = static_cast<uint16_t>(0x100 | at.second);
        if (i != rows.size()) {
            rows[i] = r;
            ++st.moved;
        } else {
            added.push_back({at, r});
            ++st.added;
        }
    }

    std::map<size_t, Seat> gone;
    for (const auto &[label, at] : was) {
        if (now.contains(label))
            continue;
        const season::PoolDriver *p = base.driver(label);
        size_t i = p && p->stat >= 0 ? row_of(rows, p->stat) : rows.size();
        if (i != rows.size() && !moved_rows.contains(i))
            gone[i] = at;
    }
    std::vector<R> appended;
    for (const auto &[at, r] : added) {
        auto place = gone.end();
        for (auto g = gone.begin(); g != gone.end(); ++g)
            if (g->second.first == at.first && (place == gone.end() || g->second.second == at.second))
                place = g;
        if (place == gone.end()) {
            appended.push_back(r);
            continue;
        }
        rows[place->first] = r;
        gone.erase(place);
        ++st.removed;
    }
    for (auto it = gone.rbegin(); it != gone.rend(); ++it) {
        rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(it->first));
        ++st.removed;
    }
    rows.insert(rows.end(), appended.begin(), appended.end());
    return st;
}

struct Filled {
    size_t looks = 0, numbers = 0;
};

template <class R> Filled fill(std::vector<R> &rows, std::initializer_list<cards::Rows<R>> sources)
{
    Filled f;
    for (R &r : rows) {
        bool look = cards::none(r.driver_customisation), number = r.car_number == cards::kNoNumber;
        for (const cards::Rows<R> &src : sources)
            for (size_t k = 0; k < src.count && (look || number); ++k) {
                const R &s = src.begin[k];
                if (!cards::same_driver(s, r))
                    continue;
                if (look && !cards::none(s.driver_customisation)) {
                    r.driver_customisation = s.driver_customisation;
                    look = false;
                    ++f.looks;
                }
                if (number && s.car_number != cards::kNoNumber) {
                    r.car_number = s.car_number;
                    number = false;
                    ++f.numbers;
                }
            }
    }
    return f;
}

}
