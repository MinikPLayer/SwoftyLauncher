// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host unit test for calendar_rules.cpp: the round-count rule of season.calendar.add_round /
// remove_round and the plan of season.calendar.repair. The season model below is what the game reads:
// Season.weekends (one per round), current_weekend (0-based), each weekend's state (0 = not started,
// 2 = complete), and the race-weekend activities of the season length's event list, at which the career
// clock stops and runs weekends[current_weekend].
#include <cstdio>
#include <string>

#include "calendar_rules.hpp"

using namespace ce::cal;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static std::string code(const std::string &e) { return e.substr(0, e.find(':')); }

struct Season {
    std::vector<int> state;  // per round: 0 not started, 2 complete
    uint32_t current = 0;
    int stops = 0;           // race-weekend activities on the game's calendar
};

enum class End { Finished, NullWeekend, Stuck };

// What the game does from the current weekend on: at each remaining race-weekend stop it races the
// current weekend (moving to the next one first when the current one is complete). SetWhereToNext reads
// the current weekend unchecked, so a stop past the last weekend crashes; completeSeason only finishes
// the season when the last weekend is complete.
static End play(Season s)
{
    int raced = 0;
    for (auto st : s.state)
        raced += st == 2;
    for (int stop = raced; stop < s.stops; ++stop) {
        if (s.state[s.current] == 2)
            ++s.current;
        if (s.current >= s.state.size())
            return End::NullWeekend;
        s.state[s.current] = 2;
    }
    bool last = s.current + 1 >= s.state.size() && s.state[s.current] == 2;
    return last ? End::Finished : End::Stuck;
}

static std::vector<RoundState> states(const Season &s)
{
    std::vector<RoundState> out;
    for (size_t i = 0; i < s.state.size(); ++i)
        out.push_back({std::to_string(i), static_cast<uint8_t>(i), s.state[i] == 0});
    return out;
}

static Season apply(Season s, const Repair &r)
{
    for (size_t i : r.remove)
        s.state.erase(s.state.begin() + static_cast<long>(i));
    for (size_t i = 0; i < r.add.size(); ++i)
        s.state.push_back(0);
    return s;
}

int main()
{
    Season stock{std::vector<int>(24, 0), 0, 24};
    CHECK(play(stock) == End::Finished);

    // hammaad: 11 rounds, six removed in the season, the stops stay at 11: a stop past the last weekend
    Season cut{{2, 2, 2, 0, 0}, 2, 11};
    CHECK(play(cut) == End::NullWeekend);
    // a round added: the stops run out before the last weekend, the season never finishes
    Season grown{std::vector<int>(25, 0), 0, 24};
    CHECK(play(grown) == End::Stuck);
    // reorder and replace keep the count: the season still finishes
    CHECK(count_change(24, 24, 24).empty());

    // add/remove are refused when they move the count away from the stops
    CHECK(code(count_change(24, 23, 24)) == "round_count");
    CHECK(code(count_change(24, 25, 24)) == "round_count");
    CHECK(code(count_change(20, 19, 24)) == "round_count");
    CHECK(code(count_change(26, 27, 24)) == "round_count");
    // and allowed when they move it back (a career edited before the rule)
    CHECK(count_change(20, 21, 24).empty());
    CHECK(count_change(26, 25, 24).empty());
    CHECK(count_change(23, 24, 24).empty());
    // unreadable stops refuse every count change
    CHECK(code(count_change(24, 23, -1)) == "round_count");
    CHECK(code(count_change(24, 23, 0)) == "round_count");

    // repair, too few rounds (kfysiu2: 24 stops, 20 rounds at round 1): four tracks of the other seasons,
    // skipping names and tracks already on the calendar
    Season k{std::vector<int>(20, 0), 0, 24};
    std::vector<Candidate> known;
    for (int i = 0; i < 24; ++i)
        known.push_back({std::to_string(i), static_cast<uint8_t>(i)});
    known.push_back({"rw_sochi", 18});
    Repair r = repair(states(k), k.current, k.stops, {}, known);
    CHECK(r.error.empty() && r.remove.empty() && r.add.size() == 4);
    CHECK(r.add.size() == 4 && r.add[0].name == "20" && r.add[3].name == "23");
    CHECK(play(apply(k, r)) == End::Finished);
    // the launcher's picks win, and must be exactly the missing count
    std::vector<Candidate> picks = {{"rw_a", 40}, {"rw_b", 41}, {"rw_c", 42}, {"rw_d", 43}};
    r = repair(states(k), k.current, k.stops, picks, known);
    CHECK(r.error.empty() && r.add.size() == 4 && r.add[0].name == "rw_a");
    CHECK(code(repair(states(k), k.current, k.stops, {{"rw_a", 40}}, known).error) == "bad_args");
    CHECK(code(repair(states(k), k.current, k.stops, {{"rw_a", 40}, {"rw_a", 41}, {"rw_c", 42}, {"rw_d", 43}}, known)
                   .error) == "duplicate_weekend");
    CHECK(code(repair(states(k), k.current, k.stops, {{"3", 40}, {"rw_b", 41}, {"rw_c", 42}, {"rw_d", 43}}, known)
                   .error) == "duplicate_weekend");
    CHECK(code(repair(states(k), k.current, k.stops, {{"rw_a", 0xff}, {"rw_b", 41}, {"rw_c", 42}, {"rw_d", 43}}, known)
                   .error) == "unknown_track");
    // not enough other tracks known
    CHECK(code(repair(states(k), k.current, k.stops, {}, {{"0", 0}, {"rw_x", 30}}).error) == "unknown_track");

    // hammaad after the last remaining race: 5 rounds raced, 11 stops: six new rounds after the current
    Season h{{2, 2, 2, 2, 2}, 4, 11};
    CHECK(play(h) == End::NullWeekend);
    r = repair(states(h), h.current, h.stops, {}, known);
    CHECK(r.error.empty() && r.add.size() == 6);
    CHECK(play(apply(h, r)) == End::Finished);

    // too many rounds: the last future rounds go, highest first
    Season g{std::vector<int>(26, 0), 3, 24};
    g.state[0] = g.state[1] = g.state[2] = 2;
    r = repair(states(g), g.current, g.stops, {}, known);
    CHECK(r.error.empty() && r.remove == (std::vector<size_t>{25, 24}) && r.add.empty());
    CHECK(play(apply(g, r)) == End::Finished);
    // ...but never the current or a started round
    Season late{{2, 2, 2, 0}, 3, 2};
    CHECK(code(repair(states(late), late.current, late.stops, {}, known).error) == "not_future");
    CHECK(repair(states(late), late.current, late.stops, {}, known).remove.empty());

    // already consistent
    r = repair(states(stock), stock.current, stock.stops, {}, known);
    CHECK(r.unchanged && r.error.empty() && r.add.empty() && r.remove.empty());
    // unreadable stops
    CHECK(code(repair(states(k), k.current, -1, {}, known).error) == "round_count");

    if (fails) {
        std::printf("%d failure(s)\n", fails);
        return 1;
    }
    std::printf("calendar_rules: ok\n");
    return 0;
}
