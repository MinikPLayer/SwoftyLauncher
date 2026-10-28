// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career: which year the career's seats show (season.cpp career_entry, the store
// match). Pure logic, built on the host by native/test.
//
//   cars       the season apply writes each slot's car of the year (season_visuals), so when more
//              than half of the seated cars are one year's own cars (data/years.json, a car named by
//              two years left out) and that year is before 2026, the seats show that year. Stock
//              2026 cars say nothing: a career with season_visuals off keeps them every year.
//   line-ups   otherwise the year whose line-up holds the most seated drivers on their own slots,
//              where a year's line-up is its line-up at any real round (the season's drivers with
//              the mid-season changes in force from their real round until the regular is back),
//              so a career that ran its year's changes still shows that year. At least 12 drivers
//              must match; ties give every best year.

#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "midseason_logic.hpp"

namespace progressive::seats {

using season::Data;
using season::Resolve;
using season::Year;

struct Seated {
    int slot = -1;
    uint32_t driver = 0;
    int vehicle = -1;
};

struct Years {
    std::vector<int> years;
    int count = 0;
    std::string how;
};

constexpr int kMinMatch = 12;

inline std::map<int, int> car_years(const Data &d, const Resolve &r)
{
    std::map<int, std::set<int>> seen;
    for (auto &[year, y] : d.years)
        for (auto &t : y.teams) {
            if (t.vehicle.empty() || t.kind == "filler")
                continue;
            int s = t.vehicle_stat >= 0 ? t.vehicle_stat : r.vehicle_stat ? r.vehicle_stat(t.vehicle) : -1;
            if (s >= 0)
                seen[s].insert(year);
        }
    std::map<int, int> out;
    for (auto &[stat, years] : seen)
        if (years.size() == 1)
            out[stat] = *years.begin();
    return out;
}

inline int year_from_cars(const std::vector<Seated> &seated, const std::map<int, int> &cars, int &count)
{
    std::map<int, int> votes;
    for (auto &s : seated) {
        auto it = cars.find(s.vehicle);
        if (it != cars.end())
            ++votes[it->second];
    }
    int best = 0;
    count = 0;
    for (auto &[year, n] : votes)
        if (n > count) {
            count = n;
            best = year;
        }
    return count * 2 > static_cast<int>(seated.size()) ? best : 0;
}

inline std::vector<std::vector<int>> due_sets(const Year &y)
{
    std::vector<std::vector<int>> out = {{}};
    int rounds = static_cast<int>(y.real_tracks.size());
    for (auto &c : y.changes)
        rounds = std::max({rounds, c.round, c.until});
    for (int k = 1; k <= rounds; ++k) {
        std::vector<int> due;
        for (size_t i = 0; i < y.changes.size(); ++i) {
            const season::Change &c = y.changes[i];
            if (c.slot >= 0 && c.slot < season::kSlots && c.round <= k && (c.until <= 0 || k < c.until))
                due.push_back(static_cast<int>(i));
        }
        if (std::find(out.begin(), out.end(), due) == out.end())
            out.push_back(due);
    }
    return out;
}

inline int line_up_match(const Data &d, const Resolve &r, const Year &y, const std::vector<Seated> &seated)
{
    int best = 0;
    for (auto &due : due_sets(y)) {
        std::vector<int> active;
        std::vector<std::string> notes;
        Year at = due.empty() ? y : midseason::line_up_with(y, due, active, notes);
        lineup::Grid g = season::grid_of(at, r, nullptr, nullptr);
        int n = 0;
        for (auto &s : seated)
            if (s.slot >= 0 && s.slot < season::kSlots)
                for (auto drv : g[s.slot])
                    n += drv == s.driver;
        best = std::max(best, n);
    }
    (void)d;
    return best;
}

inline Years seats_years(const Data &d, const Resolve &r, const std::vector<Seated> &seated, const std::map<int, int> &cars)
{
    Years out;
    int cars_count = 0;
    int by_cars = year_from_cars(seated, cars, cars_count);
    if (by_cars && by_cars < season::kLastYear) {
        out.years = {by_cars};
        out.count = cars_count;
        out.how = "cars";
        return out;
    }
    for (auto &[year, y] : d.years) {
        int n = line_up_match(d, r, y, seated);
        if (n > out.count) {
            out.count = n;
            out.years.clear();
        }
        if (n == out.count && n)
            out.years.push_back(year);
    }
    out.how = "line-ups";
    if (out.count < kMinMatch)
        out.years.clear();
    return out;
}

} // namespace progressive::seats
