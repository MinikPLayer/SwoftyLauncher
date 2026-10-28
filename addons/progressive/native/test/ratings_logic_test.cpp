// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ratings_logic.hpp"
#include "season_logic.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

using namespace progressive::ratings;

namespace {

int failures = 0;

void expect(bool ok, const char *what, int line)
{
    if (!ok) {
        std::printf("FAIL line %d: %s\n", line, what);
        ++failures;
    }
}

#define EXPECT(x) expect((x), #x, __LINE__)

bool near(double a, double b, double tol = 0.05)
{
    return std::fabs(a - b) <= tol;
}

std::map<std::string, int> g_ids;

int stat_of(const std::string &label)
{
    auto it = g_ids.find(label);
    return it == g_ids.end() ? -1 : it->second;
}

const Target *target(const Plan &p, const std::string &driver)
{
    for (auto &t : p.targets)
        if (t.driver == driver)
            return &t;
    return nullptr;
}

void data_mapping(const Data &d, const progressive::season::Data &s)
{
    EXPECT(d.stats[0].name == "experience" && d.stats[0].stat_id == 0 && near(d.stats[0].weight, 0.25));
    EXPECT(d.stats[1].name == "racecraft" && d.stats[1].stat_id == 1 && near(d.stats[1].weight, 1));
    EXPECT(d.stats[2].name == "awareness" && d.stats[2].stat_id == 3 && near(d.stats[2].weight, 0.25));
    EXPECT(d.stats[3].name == "pace" && d.stats[3].stat_id == 4 && near(d.stats[3].weight, 2));
    EXPECT(d.stats[4].name == "focus" && d.stats[4].stat_id == 5 && near(d.stats[4].weight, 0));
    for (int y = 2016; y <= 2026; ++y)
        EXPECT(d.years.count(y) == 1);

    const Values *max26 = d.find(2026, "max_verstappen");
    EXPECT(max26 && near(max26->v[3], 97) && near(max26->v[0], 88) && std::lround(max26->overall) == 95);
    const Values *max25 = d.find(2025, "max_verstappen");
    EXPECT(max25 && near(max25->v[3], 96));
    const Values *seb25 = d.find(2025, "sebastian_vettel");
    EXPECT(!seb25);

    const Values *max17 = d.find(2017, "max_verstappen");
    const Values *seb17 = d.find(2017, "sebastian_vettel");
    const Values *ham17 = d.find(2017, "lewis_hamilton");
    EXPECT(max17 && seb17 && ham17);
    if (max17 && seb17 && ham17) {
        EXPECT(max17->overall < max26->overall - 3);
        EXPECT(max17->v[0] < max26->v[0] - 10);
        EXPECT(ham17->overall >= 92 && seb17->overall >= 92);
        EXPECT(ham17->overall > max17->overall + 3 && seb17->overall > max17->overall + 3);
        std::printf("2017: Verstappen %s overall %.1f, Vettel overall %.1f, Hamilton overall %.1f\n",
                    max17->source.c_str(), max17->overall, seb17->overall, ham17->overall);
    }

    const Values *ham21 = d.find(2021, "lewis_hamilton");
    EXPECT(ham21 && near(ham21->v[0], 92) && near(ham21->v[1], 94) && near(ham21->v[2], 89) && near(ham21->v[3], 98));
    EXPECT(ham21 && ham21->source.find("F1 2021 launch") != std::string::npos);
    const Values *nor20 = d.find(2020, "lando_norris");
    EXPECT(nor20 && nor20->source.find("model") != std::string::npos);
    const Values *ham16 = d.find(2016, "lewis_hamilton");
    const Values *ros16 = d.find(2016, "nico_rosberg");
    EXPECT(ham16 && ros16 && ham16->overall > 91 && ros16->overall > 91);

    for (auto &[year, drivers] : d.years)
        for (auto &[label, v] : drivers) {
            EXPECT(near(v.overall, overall(d, v.v), 0.06));
            for (double x : v.v)
                EXPECT(x >= 0 && x <= 99);
        }

    for (auto &[year, y] : s.years) {
        std::set<std::string> seated;
        for (auto &t : y.teams)
            for (auto &sd : t.drivers)
                if (!d.find(year, sd.driver)) {
                    std::printf("FAIL: %d %s is in the line-up without ratings\n", year, sd.driver.c_str());
                    ++failures;
                }
        for (auto &c : y.changes)
            if (!d.find(year, c.in)) {
                std::printf("FAIL: %d change %s brings in %s without ratings\n", year, c.id.c_str(), c.in.c_str());
                ++failures;
            }
    }
}

void plans(const Data &d)
{
    g_ids.clear();
    int next = 1;
    for (auto &[year, drivers] : d.years)
        for (auto &[label, v] : drivers)
            if (!g_ids.count(label))
                g_ids[label] = next++;
    uint32_t max = static_cast<uint32_t>(g_ids["max_verstappen"]);
    uint32_t seb = static_cast<uint32_t>(g_ids["sebastian_vettel"]);

    Input in;
    in.start = 2017;
    in.index = 0;
    in.player = max;
    Plan p = plan(d, in, stat_of);
    EXPECT(p.year == 2017 && p.ai && p.first);
    const Target *pl = target(p, "max_verstappen");
    EXPECT(pl && pl->player && near(pl->tolerance, kFresh));
    EXPECT(pl && near(pl->values.v[3], d.find(2017, "max_verstappen")->v[3]));
    const Target *sv = target(p, "sebastian_vettel");
    EXPECT(sv && !sv->player && near(sv->tolerance, kFresh));
    EXPECT(p.targets.size() == d.years.at(2017).size());

    in.player_ratings = 2017;
    p = plan(d, in, stat_of);
    EXPECT(!target(p, "max_verstappen"));
    EXPECT(p.player_note.find("career creation") != std::string::npos);

    in.player_ratings = 0;
    in.created_season = 2;
    in.index = 2;
    p = plan(d, in, stat_of);
    EXPECT(p.year == 2019 && !target(p, "max_verstappen") && target(p, "lewis_hamilton"));

    in.created_season = 0;
    in.index = 1;
    in.player_ratings = 2017;
    in.ratings_season = 0;
    p = plan(d, in, stat_of);
    EXPECT(p.year == 2018 && p.first && !target(p, "max_verstappen"));
    EXPECT(target(p, "sebastian_vettel") && near(target(p, "sebastian_vettel")->tolerance, kFresh));

    in.ratings_season = 1;
    in.movers = {seb};
    p = plan(d, in, stat_of);
    EXPECT(!p.first);
    EXPECT(target(p, "sebastian_vettel") && target(p, "sebastian_vettel")->mover &&
           near(target(p, "sebastian_vettel")->tolerance, kFresh));
    EXPECT(target(p, "lewis_hamilton") && near(target(p, "lewis_hamilton")->tolerance, kKept));
    in.movers.clear();

    in.index = 9;
    in.ratings_season = 8;
    p = plan(d, in, stat_of);
    EXPECT(p.year == 2026 && p.ai && !p.targets.empty() && !target(p, "max_verstappen"));
    const Target *ham = target(p, "lewis_hamilton");
    EXPECT(ham && near(ham->values.v[3], d.find(2026, "lewis_hamilton")->v[3]));

    in.index = 10;
    in.ratings_season = 9;
    p = plan(d, in, stat_of);
    EXPECT(p.year == 2026 && !p.ai && p.targets.empty());

    in.start = 2026;
    in.index = 0;
    in.player_ratings = 0;
    in.ratings_season = -1;
    p = plan(d, in, stat_of);
    EXPECT(p.year == 2026 && !p.ai && p.targets.empty());

    in.start = 2025;
    in.index = 1;
    in.ratings_season = 0;
    p = plan(d, in, stat_of);
    EXPECT(p.year == 2026 && p.ai && !p.targets.empty());

    in.start = 2016;
    in.index = 0;
    in.enabled = false;
    p = plan(d, in, stat_of);
    EXPECT(p.targets.empty() && !p.notes.empty());

    Target t;
    t.values.v = {70, 90, 80, 91, 75};
    t.tolerance = kFresh;
    std::array<bool, kStats> all{true, true, true, true, true};
    EXPECT(!needs(t, {70.1, 90, 80, 91, 75}, all));
    EXPECT(needs(t, {70.5, 90, 80, 91, 75}, all));
    std::array<bool, kStats> no_focus{true, true, true, true, false};
    EXPECT(!needs(t, {70, 90, 80, 91, 10}, no_focus));
    t.tolerance = kKept;
    EXPECT(!needs(t, {72, 88, 80, 91, 75}, all));
    EXPECT(needs(t, {73, 90, 80, 91, 75}, all));
}

} // namespace

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "../../data/years.json";
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    Data d;
    std::string err;
    if (!parse(ss.str(), d, err)) {
        std::printf("FAIL: parse %s: %s\n", path, err.c_str());
        return 1;
    }
    progressive::season::Data s;
    if (!progressive::season::parse(ss.str(), s, err)) {
        std::printf("FAIL: season parse %s: %s\n", path, err.c_str());
        return 1;
    }
    data_mapping(d, s);
    plans(d);
    Data bad;
    EXPECT(!parse("{\"ratings\": {\"stats\": []}}", bad, err));
    EXPECT(!parse("{}", bad, err));
    if (failures) {
        std::printf("ratings_logic_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("ratings_logic_test: ok\n");
    return 0;
}
