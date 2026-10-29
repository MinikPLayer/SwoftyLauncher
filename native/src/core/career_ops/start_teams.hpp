#pragma once
#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "teams_plan.hpp"

// The start-season team plan of a data group (work/std16/RE-CAREER-TEAMS.md section 2.2 for a career whose start
// season varies, docs/std/systems/career.md): pure logic over the boot-time effective career.season records and the
// data group's slot rows, so native/test builds it on the host.
//
// A new career's Team Select lists the teams of the career's own entrant rows, which the game copies by stat id from
// the career setup's list when Choose Driver's pick makes the career (cards.cpp in Progressive, 0x141873a30 /
// 0x141879310). That list serves every start season of the data group, so an added team can't be a row of it: the
// wizard shows the added team on its slot row instead, dressed as it (names, colours, logo), and the season start
// (career::apply_teams) keeps the same dress on the same row. No second team row enters the career.
//
//   added      a season's slot names a team identity an addon made (its own row from the team applier at boot), not a
//              row the game database has: the effective records resolve Progressive's own chains to game/ rows
//              (game/cs16_manor, game/mercedes) and constructor/ ids, which its own names and logos dress, so those are
//              never added; a fallback chain ("a|b", "a?") that did not resolve is not either
//   dress      the slot row, as the data group's slot table names it, wears the added team in the new-career wizard
//              of that start season; a slot with no row (a twelfth team) has nothing to dress until its season starts

namespace career_ops::teams {

struct StartDress {
    int slot = 0;
    std::string row, team;
    std::array<std::string, kSeats> seats{};
};

struct StartYear {
    int year = 0;
    std::vector<StartDress> dresses;
    std::vector<std::string> notes;
};

struct StartPlan {
    std::string data_group;
    std::vector<StartYear> years;

    const StartYear *find(int year) const
    {
        for (auto &y : years)
            if (y.year == year)
                return &y;
        return nullptr;
    }
    size_t dresses() const
    {
        size_t n = 0;
        for (auto &y : years)
            n += y.dresses.size();
        return n;
    }
};

inline bool added_team_id(const std::string &team)
{
    if (team.empty() || team.find('|') != std::string::npos || team.find('?') != std::string::npos)
        return false;
    return team.rfind("constructor/", 0) != 0 && team.rfind("game/", 0) != 0 && team.find('/') != std::string::npos;
}

inline StartPlan start_plan(const std::string &data_group, std::vector<SeasonIn> seasons, const std::vector<std::string> &slot_rows)
{
    StartPlan p;
    p.data_group = data_group;
    std::stable_sort(seasons.begin(), seasons.end(), [](const SeasonIn &a, const SeasonIn &b) { return a.year < b.year; });
    for (auto &s : seasons) {
        StartYear y;
        y.year = s.year;
        std::vector<SlotIn> slots = s.slots;
        std::stable_sort(slots.begin(), slots.end(), [](const SlotIn &a, const SlotIn &b) { return a.slot < b.slot; });
        for (auto &x : slots) {
            if (!added_team_id(x.team) || x.slot < 1 || x.slot > kMaxSlots)
                continue;
            auto i = static_cast<size_t>(x.slot - 1);
            std::string row = i < slot_rows.size() ? slot_rows[i] : std::string();
            if (!row.empty() && bare(row) == bare(x.team))
                continue;
            if (row.empty()) {
                y.notes.push_back(std::to_string(s.year) + " slot " + std::to_string(x.slot) + ": " + x.team +
                                  " has no slot row to wear it in the new-career wizard; it races from its season start");
                continue;
            }
            y.dresses.push_back({x.slot, row, x.team, x.seats});
        }
        if (!y.dresses.empty() || !y.notes.empty())
            p.years.push_back(std::move(y));
    }
    return p;
}

inline std::string describe(const StartDress &d)
{
    std::string seats;
    for (auto &s : d.seats)
        seats += (seats.empty() ? "" : ", ") + (s.empty() ? std::string("(open)") : s);
    return "slot " + std::to_string(d.slot) + ": " + d.row + " as " + d.team + " (seats " + seats + ")";
}

inline std::vector<std::string> describe(const StartPlan &p)
{
    std::vector<std::string> out;
    for (auto &y : p.years) {
        for (auto &d : y.dresses)
            out.push_back(std::to_string(y.year) + " " + describe(d));
        for (auto &n : y.notes)
            out.push_back(n);
    }
    return out;
}

inline std::string json_text(const std::string &s)
{
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    return out + "\"";
}

inline std::string plan_json(const StartPlan &p)
{
    std::string out = "{\"data_group\":" + json_text(p.data_group) + ",\"years\":[";
    bool first_year = true;
    for (auto &y : p.years) {
        if (y.dresses.empty())
            continue;
        out += (first_year ? "" : ",") + std::string("{\"year\":") + std::to_string(y.year) + ",\"slots\":[";
        first_year = false;
        bool first = true;
        for (auto &d : y.dresses) {
            out += (first ? "" : ",") + std::string("{\"slot\":") + std::to_string(d.slot) + ",\"row\":" + json_text(d.row) +
                   ",\"team\":" + json_text(d.team) + ",\"seats\":[" + json_text(d.seats[0]) + "," + json_text(d.seats[1]) + "]}";
            first = false;
        }
        out += "]}";
    }
    return out + "]}";
}

} // namespace career_ops::teams
