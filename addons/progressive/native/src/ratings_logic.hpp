// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career: each season's real driver ratings (option season_ratings, default on). Pure
// logic over data/years.json `ratings` (tools/ratings.py), so native/test builds it on the host.
//
//   parse   per year and driver label the base value of the five driver statistics (experience,
//           racecraft, awareness, pace, focus: the game's stat ids and overall weights in `stats`)
//           and the overall rating they make.
//   plan    who gets which ratings at this Team HQ:
//           AI drivers: every driver of the year (line-up, stand-ins, mid-season changes) except the
//             player, in every season up to 2025 and in the first 2026 season of a career that
//             started earlier (back to the game's 2026 ratings); a career's later 2026 seasons, and
//             a career that starts in 2026, keep the game's own ratings and their evolution.
//             `fresh` (written unless already within kFresh of the target) at the season's first
//             apply (store ratings_season) and for a driver this apply moved into a seat (a
//             mid-season stand-in gets his year's ratings when he comes in); otherwise only a
//             driver more than kKept away is written again (a reload of an older save).
//           The player: only at the career's creation (season index 0 of an entry made at season
//             0, store player_ratings unset) to his driver's ratings of the start year; never
//             again, so his progression stays his.
//   needs   whether a driver's live base values are away from the targets.

#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "../../../../native/src/core/json.hpp"

namespace progressive::ratings {

constexpr int kStats = 5;
constexpr int kLastYear = 2026;
constexpr double kFresh = 0.25;
constexpr double kKept = 2.5;
inline constexpr std::array<const char *, kStats> kNames = {"experience", "racecraft", "awareness", "pace", "focus"};
inline constexpr std::array<const char *, kStats> kShort = {"EXP", "RAC", "AWA", "PAC", "FOC"};

struct StatDef {
    std::string name;
    int stat_id = -1;
    double weight = 0;
};

struct Values {
    std::array<double, kStats> v{};
    double overall = -1;
    std::string source;
};

struct Data {
    std::array<StatDef, kStats> stats{};
    std::map<int, std::map<std::string, Values>> years;

    const Values *find(int year, const std::string &driver) const
    {
        auto y = years.find(year);
        if (y == years.end())
            return nullptr;
        auto d = y->second.find(driver);
        return d == y->second.end() ? nullptr : &d->second;
    }
};

inline int stat_index(const std::string &name)
{
    for (int i = 0; i < kStats; ++i)
        if (name == kNames[i])
            return i;
    return -1;
}

inline double overall(const Data &d, const std::array<double, kStats> &v)
{
    double sum = 0, w = 0;
    for (int i = 0; i < kStats; ++i) {
        sum += v[i] * d.stats[i].weight;
        w += d.stats[i].weight;
    }
    return w > 0 ? sum / w : -1;
}

inline bool parse(const std::string &text, Data &out, std::string &err)
{
    out = Data{};
    json::Value j;
    if (!json::parse(text, j) || j.type != json::Value::Obj) {
        err = "years.json is not a JSON object";
        return false;
    }
    const json::Value &r = j["ratings"];
    if (r.type != json::Value::Obj) {
        err = "years.json has no ratings (tools/gen.py older than season_ratings)";
        return false;
    }
    int seen = 0;
    for (auto &s : r["stats"].a) {
        int i = stat_index(s["name"].str());
        if (i < 0 || s["stat_id"].type != json::Value::Num || s["weight"].type != json::Value::Num)
            continue;
        out.stats[i] = {s["name"].str(), static_cast<int>(s["stat_id"].n), s["weight"].n};
        seen |= 1 << i;
    }
    if (seen != (1 << kStats) - 1) {
        err = "years.json ratings.stats must list experience, racecraft, awareness, pace and focus";
        return false;
    }
    for (auto &[key, drivers] : r["years"].o) {
        int year = std::atoi(key.c_str());
        if (year < 2016 || year > kLastYear || drivers.type != json::Value::Obj)
            continue;
        for (auto &[label, v] : drivers.o) {
            Values x;
            bool ok = true;
            for (int i = 0; i < kStats; ++i) {
                const json::Value &n = v[kNames[i]];
                if (n.type != json::Value::Num || !(n.n >= 0 && n.n <= 100))
                    ok = false;
                else
                    x.v[i] = n.n;
            }
            if (!ok)
                continue;
            x.overall = v["overall"].type == json::Value::Num ? v["overall"].n : overall(out, x.v);
            x.source = v["source"].str();
            out.years[year][label] = x;
        }
    }
    if (out.years.empty()) {
        err = "years.json ratings has no years";
        return false;
    }
    return true;
}

inline int year_for(int start, uint32_t index)
{
    long long y = static_cast<long long>(start < 2016 ? 2016 : start > kLastYear ? kLastYear : start) + index;
    return y > kLastYear ? kLastYear : static_cast<int>(y);
}

struct Input {
    int start = kLastYear;
    uint32_t index = 0;
    uint32_t player = 0;
    int created_season = 0;
    int player_ratings = 0;
    int ratings_season = -1;
    bool enabled = true;
    std::vector<uint32_t> movers;
};

struct Target {
    std::string driver;
    uint32_t stat = 0;
    Values values;
    bool player = false;
    bool mover = false;
    double tolerance = kKept;
};

struct Plan {
    int year = 0;
    bool ai = false;
    bool first = false;
    std::vector<Target> targets;
    std::vector<std::string> notes;
    std::string player_note;
};

inline Plan plan(const Data &d, const Input &in, const std::function<int(const std::string &)> &driver_stat)
{
    Plan p;
    p.year = year_for(in.start, in.index);
    p.first = in.ratings_season != static_cast<int>(in.index);
    if (!in.enabled) {
        p.notes.push_back("option season_ratings is off: every driver keeps the game's ratings");
        return p;
    }
    auto y = d.years.find(p.year);
    if (y == d.years.end()) {
        p.notes.push_back("no ratings for " + std::to_string(p.year) + " in years.json");
        return p;
    }
    bool first_2026 = p.year == kLastYear && in.index > 0 && year_for(in.start, in.index - 1) < kLastYear;
    p.ai = p.year < kLastYear || first_2026;
    if (!p.ai)
        p.notes.push_back(in.index == 0 ? "a career that starts in 2026 races with the game's own ratings"
                                        : "season " + std::to_string(in.index) +
                                              " is a 2026 season after the first: the game's own ratings carry on");
    std::string player_label;
    for (auto &[label, v] : y->second) {
        int s = driver_stat(label);
        if (s < 0) {
            p.notes.push_back(label + ": not in the gamedb, no ratings written");
            continue;
        }
        uint32_t stat = static_cast<uint32_t>(s);
        if (stat == in.player) {
            player_label = label;
            continue;
        }
        if (!p.ai)
            continue;
        Target t{label, stat, v, false, false, p.first ? kFresh : kKept};
        for (uint32_t m : in.movers)
            if (m == stat) {
                t.mover = true;
                t.tolerance = kFresh;
            }
        p.targets.push_back(t);
    }
    bool creation = in.index == 0 && in.created_season == 0 && in.player_ratings == 0;
    if (player_label.empty())
        p.player_note = "the player's driver has no " + std::to_string(p.year) + " ratings; his ratings stay the game's";
    else if (p.year == kLastYear)
        p.player_note = "the player (" + player_label + ") keeps the game's 2026 ratings";
    else if (!creation)
        p.player_note = in.player_ratings ? "the player (" + player_label + ") got his " +
                                                std::to_string(in.player_ratings) +
                                                " ratings at career creation; his ratings are his own from there"
                                          : "the player (" + player_label + ") keeps his own ratings (season index " +
                                                std::to_string(in.index) + ", not the career's creation)";
    else {
        p.player_note = "career creation: the player (" + player_label + ") starts with his " +
                        std::to_string(p.year) + " ratings; never written again";
        p.targets.push_back({player_label, in.player, y->second.at(player_label), true, false, kFresh});
    }
    return p;
}

inline bool needs(const Target &t, const std::array<double, kStats> &now, const std::array<bool, kStats> &have)
{
    for (int i = 0; i < kStats; ++i)
        if (have[i] && !(std::fabs(now[i] - t.values.v[i]) <= t.tolerance))
            return true;
    return false;
}

} // namespace progressive::ratings
