// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career: the per-career store, careers.json in the addon's storage folder. An entry's
// key is fixed when it is made (pg-<seed>-<driver>, with a suffix when taken); seed is the
// WeatherAndTimeOfDayComponent random_seed (+0x2c4) seen at the career's last load, a hint only
// since the game redraws it, and the player's driver stat id. season.cpp career_entry picks the
// entry of a loaded career (seed, then the year its seats show, then the last used entry of the
// driver). The profile options are copied in when the entry is made, so later option changes
// leave running careers alone; the last applied season sits next to them (an optimisation, the
// seats decide), and the seasons the developer option test_next_season added on top of the game's
// season index. Each load and Team HQ also records the player's team and the game's season index,
// which the Resume Career cards (save slots carry the driver, team and season index but no seed)
// use to tell careers of one driver apart (resume_logic.hpp). A Real life career also keeps the
// last in-season development it applied (development_logic.hpp): year, round (0-based) of rounds,
// and each curved slot's delta ("<slot>:<delta %>,..."), so a load or Team HQ only logs a change.
// The mid-season changes in force at the last applied Team HQ (year, career race, change ids) are
// kept too: the seats follow the round the save is at, the record names what changed since and
// stands in when the calendar can't be read; the career calendar's tracks (the same every season)
// scale a season break's first apply when the new season's calendar can't be read yet. The season
// index whose driver ratings were written (ratings_season) and the year whose ratings the player's
// driver got at the career's creation (player_ratings, 0 for none) gate ratings.hpp.

#pragma once
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace pg::store {

struct Career {
    std::string key;
    uint32_t driver = 0;
    uint32_t seed = 0;
    int start = 2026;
    std::string rnd = "real_life";
    std::string moves = "follow_driver";
    int created_season = 0;
    int applied_season = -1;
    int applied_year = 0;
    int test_seasons = 0;
    uint64_t used = 0;
    int team = -1;
    int season = -1;
    int dev_year = 0;
    int dev_round = -1;
    int dev_rounds = 0;
    std::string dev_applied;
    int trees_season = -1;
    int ratings_season = -1;
    int player_ratings = 0;
    int mid_year = 0;
    int mid_round = 0;
    std::string mid_applied;
    std::string mid_tracks;
};

bool load();
bool save();
Career *find(const std::string &key);
std::vector<Career *> by_driver(uint32_t driver);
std::deque<Career> &all();
Career *by_seed(uint32_t seed, uint32_t driver);
Career &add(const Career &c);
std::string free_key(uint32_t seed, uint32_t driver);
uint64_t next_use();

} // namespace pg::store
