// SPDX-License-Identifier: AGPL-3.0-or-later
#include "calendar_rules.hpp"

#include <cstdlib>
#include <set>

namespace ce::cal {

std::string count_change(int rounds, int after, int race_weekends)
{
    if (after == rounds)
        return {};
    if (race_weekends < 1)
        return "round_count: the game's race dates for this season aren't readable yet";
    if (after == race_weekends || std::abs(after - race_weekends) < std::abs(rounds - race_weekends))
        return {};
    return "round_count: this season has " + std::to_string(race_weekends) +
           " race weekends on the game's calendar, so it keeps " + std::to_string(race_weekends) +
           " rounds; replace or move a round instead";
}

Repair repair(const std::vector<RoundState> &rounds, uint32_t current, int race_weekends,
              const std::vector<Candidate> &given, const std::vector<Candidate> &known)
{
    Repair r;
    if (race_weekends < 1) {
        r.error = "round_count: the game's race dates for this season aren't readable yet";
        return r;
    }
    const size_t want = static_cast<size_t>(race_weekends);
    if (rounds.size() == want) {
        r.unchanged = true;
        return r;
    }
    if (rounds.size() > want) {
        size_t extra = rounds.size() - want;
        for (size_t i = rounds.size(); i-- > 0 && r.remove.size() < extra;) {
            if (i <= current || !rounds[i].not_started)
                break;
            r.remove.push_back(i);
        }
        if (r.remove.size() < extra) {
            r.remove.clear();
            r.error = "not_future: " + std::to_string(extra) + " round(s) have to go, but only the rounds after this " +
                      "weekend that haven't started can";
        }
        return r;
    }
    size_t need = want - rounds.size();
    std::set<std::string> names;
    std::set<uint8_t> tracks;
    for (auto &x : rounds) {
        names.insert(x.name);
        tracks.insert(x.track);
    }
    if (!given.empty()) {
        if (given.size() != need) {
            r.error = "bad_args: " + std::to_string(need) + " weekend(s) needed, " + std::to_string(given.size()) +
                      " given";
            return r;
        }
        for (auto &c : given) {
            if (c.track == 0xff) {
                r.error = "unknown_track: " + c.name;
                return r;
            }
            if (!names.insert(c.name).second) {
                r.error = "duplicate_weekend: " + c.name;
                return r;
            }
            r.add.push_back(c);
        }
        return r;
    }
    for (auto &c : known) {
        if (r.add.size() == need)
            break;
        if (c.track == 0xff || c.name.empty() || names.count(c.name) || tracks.count(c.track))
            continue;
        names.insert(c.name);
        tracks.insert(c.track);
        r.add.push_back(c);
    }
    if (r.add.size() < need) {
        r.error = "unknown_track: " + std::to_string(need) + " round(s) needed, " + std::to_string(r.add.size()) +
                  " track(s) of this championship are not on this calendar; send the weekends to add";
        r.add.clear();
    }
    return r;
}

} // namespace ce::cal
