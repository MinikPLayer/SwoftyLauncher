#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// career::apply_teams' planner (docs/std/systems/career.md, work/std16/RE-CAREER-TEAMS.md section 2): pure logic
// over the career.season records of one data group, so native/test builds it on the host.
//
//   slot       1..12; one team row for the whole career (the slot row), the team identity the slot holds in the
//              first season that fills it. Team keys never change mid-career: the core dresses the slot row
//              as the season's team instead of swapping rows.
//   open       the season's team is the slot row
//   dressed    the season's team differs from the slot row: names, colours and logos of the slot row are
//              replaced for this career by the season team's
//   closed     the season names no team for the slot (or leaves it out): its race seats are released and the
//              slot is left out of the Championship; a later season may open it again
//
// Refused: more than 12 slots, 12 slots with My Team (13 teams), slot numbers below 1, a slot listed twice in
// a season, a slot never filled by any season, a team identity on two slots of one season, a slot row shared
// by two slots, a slot above the table, seats named for a slot with no team.

namespace career_ops::teams {

constexpr int kMaxSlots = 12;
constexpr int kSeats = 2;

struct SlotIn {
    int slot = 0;
    std::string team;
    std::array<std::string, kSeats> seats{};
    int car = -1;
};

struct SeasonIn {
    int year = 0;
    std::vector<SlotIn> slots;
};

struct Table {
    std::vector<std::string> rows;
    std::vector<int> first_year;
    std::vector<std::string> refusals;

    bool ok() const
    {
        return refusals.empty() && !rows.empty();
    }
    int count() const
    {
        return static_cast<int>(rows.size());
    }
};

enum class Kind { open, dressed, closed };

struct Action {
    int slot = 0;
    Kind kind = Kind::closed;
    std::string row, team;
    int car = -1;
};

struct Plan {
    int year = 0;
    std::vector<Action> actions;
    std::vector<std::string> refusals;

    size_t count(Kind k) const
    {
        return static_cast<size_t>(std::count_if(actions.begin(), actions.end(), [&](const Action &a) { return a.kind == k; }));
    }
    bool ok() const
    {
        return refusals.empty();
    }
};

using DressFn = bool (*)(const char *data_group, int slot, const char *row, const char *team, int64_t row_stat);

inline DressFn &dresser()
{
    static DressFn fn = nullptr;
    return fn;
}

inline std::string bare(const std::string &id)
{
    return id.rfind("game/", 0) == 0 ? id.substr(5) : id;
}

inline std::string identity_id(const std::string &team)
{
    return team.find('/') == std::string::npos ? "game/" + team : team;
}

inline void anchor_rows(Table &t, const std::vector<std::string> &physical)
{
    for (size_t i = 0; i < t.rows.size() && i < physical.size(); ++i)
        if (!physical[i].empty())
            t.rows[i] = physical[i];
    t.refusals.erase(std::remove_if(t.refusals.begin(), t.refusals.end(),
                                    [](const std::string &r) { return r.find("is also the row of slot") != std::string::npos; }),
                     t.refusals.end());
    for (size_t i = 0; i < t.rows.size(); ++i)
        for (size_t j = i + 1; j < t.rows.size(); ++j)
            if (!t.rows[i].empty() && t.rows[i] == t.rows[j])
                t.refusals.push_back("slot " + std::to_string(j + 1) + ": its row " + t.rows[j] + " is also the row of slot " +
                                     std::to_string(i + 1));
}

inline bool filled(const SlotIn &s)
{
    return !s.team.empty();
}

inline std::string years_of(const SeasonIn &s)
{
    return std::to_string(s.year);
}

inline void check_season(const SeasonIn &s, std::vector<std::string> &out)
{
    std::vector<int> seen;
    std::vector<std::string> teams;
    for (auto &t : s.slots) {
        std::string where = years_of(s) + " slot " + std::to_string(t.slot);
        if (t.slot < 1) {
            out.push_back(where + ": slots start at 1");
            continue;
        }
        if (t.slot > kMaxSlots) {
            out.push_back(where + ": a career has at most " + std::to_string(kMaxSlots) + " team slots");
            continue;
        }
        if (std::find(seen.begin(), seen.end(), t.slot) != seen.end())
            out.push_back(where + ": listed twice");
        seen.push_back(t.slot);
        if (!filled(t)) {
            for (auto &d : t.seats)
                if (!d.empty())
                    out.push_back(where + ": " + d + " has no team to race for");
            continue;
        }
        if (std::find(teams.begin(), teams.end(), t.team) != teams.end())
            out.push_back(where + ": " + t.team + " is already on another slot of " + years_of(s));
        teams.push_back(t.team);
    }
}

inline Table build_table(std::vector<SeasonIn> seasons, bool my_team)
{
    Table t;
    std::stable_sort(seasons.begin(), seasons.end(), [](const SeasonIn &a, const SeasonIn &b) { return a.year < b.year; });
    int top = 0;
    for (auto &s : seasons) {
        check_season(s, t.refusals);
        for (auto &x : s.slots)
            if (x.slot >= 1 && x.slot <= kMaxSlots)
                top = std::max(top, x.slot);
            else if (x.slot > kMaxSlots)
                top = std::max(top, x.slot);
    }
    if (top > kMaxSlots) {
        t.refusals.push_back(std::to_string(top) + " team slots: a career has at most " + std::to_string(kMaxSlots));
        return t;
    }
    if (top == kMaxSlots && my_team)
        t.refusals.push_back(std::to_string(kMaxSlots) + " team slots with My Team is 13 teams: refused");
    t.rows.assign(static_cast<size_t>(top), std::string());
    t.first_year.assign(static_cast<size_t>(top), 0);
    for (auto &s : seasons)
        for (auto &x : s.slots) {
            if (x.slot < 1 || x.slot > top || !filled(x))
                continue;
            auto i = static_cast<size_t>(x.slot - 1);
            if (t.rows[i].empty()) {
                t.rows[i] = x.team;
                t.first_year[i] = s.year;
            }
        }
    for (size_t i = 0; i < t.rows.size(); ++i) {
        if (t.rows[i].empty()) {
            t.refusals.push_back("slot " + std::to_string(i + 1) + ": no season fills it");
            continue;
        }
        for (size_t j = i + 1; j < t.rows.size(); ++j)
            if (t.rows[j] == t.rows[i])
                t.refusals.push_back("slot " + std::to_string(j + 1) + ": its row " + t.rows[j] + " is also the row of slot " +
                                     std::to_string(i + 1));
    }
    return t;
}

inline Plan plan_season(const Table &table, const SeasonIn &season)
{
    Plan p;
    p.year = season.year;
    if (!table.ok()) {
        p.refusals = table.refusals;
        if (p.refusals.empty())
            p.refusals.push_back("the data group has no team slots");
        return p;
    }
    check_season(season, p.refusals);
    for (auto &x : season.slots)
        if (x.slot > table.count())
            p.refusals.push_back(years_of(season) + " slot " + std::to_string(x.slot) + ": the data group has " +
                                 std::to_string(table.count()) + " slot row(s), made at creation; a slot added later has no row");
    if (!p.refusals.empty())
        return p;
    for (int slot = 1; slot <= table.count(); ++slot) {
        Action a;
        a.slot = slot;
        a.row = table.rows[static_cast<size_t>(slot - 1)];
        auto it = std::find_if(season.slots.begin(), season.slots.end(), [&](const SlotIn &s) { return s.slot == slot; });
        if (it == season.slots.end() || !filled(*it)) {
            a.kind = Kind::closed;
        } else {
            a.team = it->team;
            a.car = it->car;
            a.kind = bare(it->team) == bare(a.row) ? Kind::open : Kind::dressed;
        }
        p.actions.push_back(a);
    }
    return p;
}

inline std::vector<int> closed_slots(const Plan &p)
{
    std::vector<int> out;
    for (auto &a : p.actions)
        if (a.kind == Kind::closed)
            out.push_back(a.slot);
    return out;
}

struct SeatRow {
    uint32_t driver_stat = 0;
    int team = -1;
    int position = -1;
    bool car = false;
    bool human = false;
};

struct Closing {
    std::vector<uint32_t> releases;
    std::vector<std::string> refusals;
};

inline Closing close_slots(const Plan &p, const std::vector<uint32_t> &slot_teams, const std::vector<SeatRow> &rows)
{
    Closing c;
    for (int slot : closed_slots(p)) {
        if (static_cast<size_t>(slot) > slot_teams.size())
            continue;
        int team = static_cast<int>(slot_teams[static_cast<size_t>(slot - 1)]);
        for (auto &r : rows) {
            if (!r.car || r.team != team)
                continue;
            if (r.human) {
                c.refusals.push_back("slot " + std::to_string(slot) + ": closing it would release the player's seat");
                continue;
            }
            if (std::find(c.releases.begin(), c.releases.end(), r.driver_stat) == c.releases.end())
                c.releases.push_back(r.driver_stat);
        }
    }
    return c;
}

inline std::string summary(const Plan &p)
{
    return std::to_string(p.actions.size()) + " slot(s), " + std::to_string(p.count(Kind::open) + p.count(Kind::dressed)) +
           " open, " + std::to_string(p.count(Kind::closed)) + " closed, " + std::to_string(p.count(Kind::dressed)) + " dressed";
}

} // namespace career_ops::teams
