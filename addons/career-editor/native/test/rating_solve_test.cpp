// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host unit test for rating_solve.cpp: the search drivers.ratings.set runs for the Driver Career
// player, whose getter maps the curve value through the progress table and adds the player's points.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "rating_solve.hpp"

using namespace ce::rating;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

// The player's getter as the game builds it: the rating's points from a table indexed by rating
// (linear between entries), plus the player's points for that statistic, clamped to the table, then
// back to a rating. A sum at or past the last entry reads as the entry count.
struct Player {
    std::vector<float> table;
    float bonus = 0.0f;

    float points(float v) const {
        size_t last = table.size() - 1;
        size_t lo = std::min(last, static_cast<size_t>(v)), hi = std::min(last, static_cast<size_t>(v + 1.0f));
        return (table[hi] - table[lo]) * (v - std::floor(v)) + table[lo];
    }
    float rating(float base) const {
        float w = std::clamp(points(base) + bonus, table.front(), table.back());
        for (size_t i = 0; i + 1 < table.size(); ++i)
            if (w < table[i + 1] && table[i] <= w)
                return (w - table[i]) / (table[i + 1] - table[i]) + static_cast<float>(i);
        return static_cast<float>(table.size());
    }
};

static Player player(float bonus) {
    Player p;
    for (int k = 0; k <= 100; ++k)
        p.table.push_back(0.5f * k * k + 10.0f * k);
    p.bonus = bonus;
    return p;
}

int main() {
    Player me = player(828.0f);
    CHECK(std::lround(me.rating(51.8f)) == 64);

    int probes = 0;
    auto flat = [&](float p) { ++probes; return me.rating(p * 100.0f); };

    // the glass_m case: experience 64 on a 51.8 base, set to 87; the editor's old one-step write landed short
    float others = me.rating(51.8f) - 51.8f;
    CHECK(std::lround(me.rating(87.0f - others)) < 86);
    Solve s = solve(0.0f, 1.0f, 87.0f, flat);
    CHECK(s.reached);
    CHECK(std::lround(me.rating(s.p * 100.0f)) == 87);
    CHECK(probes <= 42);

    // every rating from the lowest reachable one up to 100 can be set
    float floor_value = me.rating(0.0f);
    for (int t = static_cast<int>(std::lround(floor_value)); t <= 100; ++t) {
        Solve r = solve(0.0f, 1.0f, static_cast<float>(t), flat);
        CHECK(r.reached);
        CHECK(std::lround(std::min(100.0f, me.rating(r.p * 100.0f))) == t);
    }

    // below the player's points no base reaches the target; the floor is reported and can be set
    Solve low = solve(0.0f, 1.0f, 25.0f, flat);
    CHECK(!low.reached);
    CHECK(25.0f < low.low);
    CHECK(std::lround(low.low) == std::lround(floor_value));
    CHECK(solve(0.0f, 1.0f, static_cast<float>(std::lround(low.low)), flat).reached);

    // shift mode moves every point by p: a 0.4 curve point shifted to the target
    auto shifted = [&](float p) { return me.rating(std::clamp(0.4f + p, 0.0f, 1.0f) * 100.0f); };
    Solve sh = solve(-1.0f, 1.0f, 90.0f, shifted);
    CHECK(sh.reached);
    CHECK(std::lround(me.rating(std::clamp(0.4f + sh.p, 0.0f, 1.0f) * 100.0f)) == 90);

    // a dynamic statistic: the level is searched from 0 to 100
    auto level = [&](float d) { return me.rating(80.0f + d); };
    Solve dy = solve(-80.0f, 20.0f, 99.0f, level);
    CHECK(dy.reached);
    CHECK(std::lround(me.rating(80.0f + dy.p)) == 99);

    // an AI driver: the curve plus fixed terms, which the one-step write already hits
    auto ai = [](float p) { return p * 100.0f + 2.5f; };
    Solve a = solve(0.0f, 1.0f, 75.0f, ai);
    CHECK(a.reached);
    CHECK(std::fabs(a.p - 0.725f) < 0.001f);
    Solve above = solve(0.0f, 1.0f, 100.0f, [](float p) { return p * 90.0f; });
    CHECK(!above.reached);
    CHECK(100.0f > above.high);

    // the game shows a rating rounded down, so the search lands on the target or just above it
    for (float t : {33.3f, 66.7f, 90.0f, 12.34f}) {
        Solve r = solve(0.0f, 1.0f, t, [](float p) { return p * 99.7f + 0.1f; });
        CHECK(r.reached);
        CHECK(r.value >= t);
        CHECK(r.value - t <= 0.01f);
    }

    // a getter that can't be read reaches nothing
    CHECK(!solve(0.0f, 1.0f, 50.0f, [](float) { return NAN; }).reached);

    if (fails)
        return 1;
    std::printf("rating_solve_test: ok\n");
    return 0;
}
