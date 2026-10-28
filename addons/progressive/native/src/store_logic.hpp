// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career: which careers.json entry a loaded career is (store.hpp), and folding an entry
// that a load split off its career back into it. Pure logic, so native/test builds it on the host.
//
// shows(): the year the career's seats show is the entry's year at the game's season index, or the
// year of the season before: at a season change the seats keep the old season's cars until the
// season apply of the new one runs at the first idle Team HQ.
//
// pick(): a new career from the Progressive wizard (its seats show no past year) gets a new entry;
// otherwise the entry with the career's seed unless the seats show a year two or more seasons off
// its year (a mid-season line-up can look like a neighbouring year; a seed match is kept through
// that), else the last used entry of the driver whose year the seats show, else (seats
// still on the stock grid) the driver's last used entry; no entry: a new one, its start taken from
// the seats when they show a past year.
//
// split_of() / fold(): an entry a load made because no entry fitted (created at a season index
// above 0, its key the original's key base with a "-N" suffix or the original's seed, same driver)
// is the same career, whatever start it inferred from the seats. fold() keeps the original's options and its dev and mid-season
// fields unless the split one has a later year for them, and takes the later applied season, seed,
// use, team and season index.

#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "store.hpp"

namespace progressive::match {

using pg::store::Career;

constexpr int kFirstYear = 2016;
constexpr int kLastYear = 2026;

inline int year_for(int start, uint32_t index)
{
    long long y = static_cast<long long>(std::clamp(start, kFirstYear, kLastYear)) + index;
    return y > kLastYear ? kLastYear : static_cast<int>(y);
}

inline bool shows(const Career &c, uint32_t season_index, const std::vector<int> &seen)
{
    uint32_t i = season_index + static_cast<uint32_t>(std::max(0, c.test_seasons));
    auto has = [&](int y) { return std::find(seen.begin(), seen.end(), y) != seen.end(); };
    return has(year_for(c.start, i)) || (i > 0 && has(year_for(c.start, i - 1)));
}

inline bool within_a_season(const Career &c, uint32_t season_index, const std::vector<int> &seen)
{
    int y = year_for(c.start, season_index + static_cast<uint32_t>(std::max(0, c.test_seasons)));
    for (int s : seen)
        if (s - y <= 1 && y - s <= 1)
            return true;
    return false;
}

inline int past_year(const std::vector<int> &seen)
{
    for (int y : seen)
        if (y < kLastYear)
            return y;
    return 0;
}

struct Pick {
    int index = -1;
    std::string how;
    int new_start = 0;
};

template <class Entries>
Pick pick(const Entries &all, uint32_t seed, uint32_t driver, uint32_t season_index,
                 const std::vector<int> &seen, bool fresh, int option_start)
{
    Pick out;
    int past = past_year(seen);
    auto latest = [&](const std::vector<int> &is) {
        int b = -1;
        for (int i : is)
            if (b < 0 || all[i].used >= all[b].used)
                b = i;
        return b;
    };
    if (!(fresh && !past)) {
        std::vector<int> same, fit, seeded;
        for (size_t i = 0; i < all.size(); ++i) {
            if (all[i].driver != driver)
                continue;
            same.push_back(static_cast<int>(i));
            bool ok = past && shows(all[i], season_index, seen);
            if (ok)
                fit.push_back(static_cast<int>(i));
            if (all[i].seed == seed && (!past || ok || within_a_season(all[i], season_index, seen)))
                seeded.push_back(static_cast<int>(i));
        }
        if (!seeded.empty()) {
            out.index = latest(seeded);
            out.how = "seed";
        } else if (!fit.empty()) {
            out.index = latest(fit);
            out.how = "the seats show its year";
        } else if (!past && !same.empty()) {
            out.index = latest(same);
            out.how = "the driver's last used entry (the seats show no past year yet)";
        }
        if (out.index >= 0)
            return out;
        out.how = "no entry of this driver fits";
    } else {
        out.how = "new career from the Progressive wizard";
    }
    out.new_start = past ? std::max(kFirstYear, past - static_cast<int>(season_index)) : option_start;
    return out;
}

inline std::string key_base(const std::string &key)
{
    size_t a = key.find('-');
    size_t b = a == std::string::npos ? a : key.find('-', a + 1);
    size_t c = b == std::string::npos ? b : key.find('-', b + 1);
    return c == std::string::npos ? key : key.substr(0, c);
}

inline std::string seed_base(uint32_t seed, uint32_t driver)
{
    char k[64];
    std::snprintf(k, sizeof k, "pg-%08x-%u", seed, driver);
    return k;
}

inline bool split_of(const Career &original, const Career &split)
{
    if (original.key == split.key || original.driver != split.driver || split.created_season <= 0 ||
        split.created_season < original.created_season)
        return false;
    std::string base = key_base(split.key);
    bool suffixed = split.key != base;
    return (suffixed && base == key_base(original.key)) || base == seed_base(original.seed, original.driver);
}

inline void fold(Career &original, const Career &split)
{
    if (split.applied_season > original.applied_season) {
        original.applied_season = split.applied_season;
        original.applied_year = split.applied_year;
    }
    if (split.used >= original.used) {
        original.used = split.used;
        original.seed = split.seed;
        original.team = split.team;
        original.season = split.season;
    }
    if (split.dev_year > original.dev_year) {
        original.dev_year = split.dev_year;
        original.dev_round = split.dev_round;
        original.dev_rounds = split.dev_rounds;
        original.dev_applied = split.dev_applied;
    }
    original.ratings_season = std::max(original.ratings_season, split.ratings_season);
    if (!original.player_ratings)
        original.player_ratings = split.player_ratings;
    if (original.mid_tracks.empty())
        original.mid_tracks = split.mid_tracks;
    if (split.mid_year > original.mid_year) {
        original.mid_year = split.mid_year;
        original.mid_round = split.mid_round;
        original.mid_applied = split.mid_applied;
        if (!split.mid_tracks.empty())
            original.mid_tracks = split.mid_tracks;
    }
}

} // namespace progressive::match
