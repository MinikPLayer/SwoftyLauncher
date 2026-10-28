// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The R&D model performance targets from the committed data/years.json and the metadata rewrite
// (rnd_model_logic.hpp).

#include "rnd_model_logic.hpp"
#include "development_logic.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

using namespace progressive;
using rnd_model::Meta;

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

bool near(double a, double b, double eps = 1e-3)
{
    return std::fabs(a - b) <= eps;
}

std::vector<Meta> stock()
{
    std::vector<Meta> m;
    for (int i = 0; i < 19; ++i)
        m.push_back({static_cast<uint8_t>(i), static_cast<uint8_t>(i % 4), 20.0f + i, 60.0f + 2 * i});
    return m;
}

void rewrite()
{
    EXPECT(near(rnd_model::target(0, 0), rnd_model::kTop));
    EXPECT(near(rnd_model::target(1.0, 0), rnd_model::kTop - rnd_model::kPerPct));
    EXPECT(near(rnd_model::target(1.0, -0.5), rnd_model::kTop - 0.5 * rnd_model::kPerPct));
    EXPECT(near(rnd_model::target(50, 0), rnd_model::kMin));
    EXPECT(near(rnd_model::target(-50, 0), rnd_model::kMax));

    std::vector<Meta> m = stock();
    EXPECT(rnd_model::apply(m, 500, rnd_model::kGainRoom));
    EXPECT(near(rnd_model::base_sum(m), 500));
    EXPECT(near(rnd_model::gain_room(m), rnd_model::kGainRoom));
    for (size_t i = 0; i < m.size(); ++i)
        EXPECT(m[i].type == i && m[i].department == i % 4);
    EXPECT(!rnd_model::apply(m, 500, rnd_model::kGainRoom));
    EXPECT(rnd_model::apply(m, 420, 0));
    EXPECT(near(rnd_model::base_sum(m), 420) && near(rnd_model::gain_room(m), 0));
    EXPECT(!rnd_model::apply(m, 420, 0));

    std::vector<Meta> a = stock(), b = stock();
    b[0].base = 90;
    b[1].base = 5;
    rnd_model::apply(a, 500, 0);
    rnd_model::apply(b, 480, 0);
    for (uint8_t d = 0; d < 4; ++d)
        EXPECT(rnd_model::department_sum(a, d) > rnd_model::department_sum(b, d));

    std::vector<Meta> none;
    EXPECT(!rnd_model::apply(none, 500, 0));
}

void real(const season::Data &d)
{
    for (int year = 2016; year <= 2026; ++year) {
        const season::Year *y = d.year(year);
        EXPECT(y != nullptr);
        if (!y)
            continue;
        double best = -1;
        int best_slot = -1;
        for (int i = 0; i < season::kSlots; ++i) {
            double t = rnd_model::target(rnd_model::year_gap(d, year, i), 0);
            EXPECT(t >= rnd_model::kMin && t <= rnd_model::kMax);
            if (t > best) {
                best = t;
                best_slot = i;
            }
        }
        EXPECT(near(best, rnd_model::kTop) || year >= 2019);
        for (int i = 0; i < season::kSlots; ++i)
            for (int j = 0; j < season::kSlots; ++j)
                if (y->teams[i].gap_pct >= 0 && y->teams[j].gap_pct >= 0 && y->teams[i].gap_pct < y->teams[j].gap_pct)
                    EXPECT(rnd_model::target(rnd_model::year_gap(d, year, i), 0) >
                           rnd_model::target(rnd_model::year_gap(d, year, j), 0));
        (void)best_slot;
    }
    auto t2016 = [&](int slot) { return rnd_model::target(rnd_model::year_gap(d, 2016, slot - 1), 0); };
    EXPECT(t2016(1) > t2016(3) && t2016(3) > t2016(2) && t2016(2) > t2016(5) && t2016(5) > t2016(11));
    EXPECT(near(t2016(1), 560) && t2016(11) < 420);
    std::printf("2016 targets: Mercedes %.0f, Red Bull %.0f, Ferrari %.0f, Force India %.0f, Williams %.0f, McLaren "
                "%.0f, Manor %.0f\n",
                t2016(1), t2016(3), t2016(2), t2016(5), t2016(7), t2016(4), t2016(11));
    for (int slot = 1; slot <= season::kSlots; ++slot)
        EXPECT(near(rnd_model::year_gap(d, 2019, slot - 1) >= 0 ? 1 : 0, 1));
    EXPECT(near(rnd_model::year_gap(d, 2019, 10), 4.431));

    development::Plan p = development::plan(d, 2023, 0, 22), q = development::plan(d, 2023, 11, 22);
    double gap = rnd_model::year_gap(d, 2023, 3);
    EXPECT(rnd_model::target(gap, q.teams[3].delta) > rnd_model::target(gap, p.teams[3].delta) + 40);
}

} // namespace

int main(int argc, char **argv)
{
    rewrite();
    const char *path = argc > 1 ? argv[1] : "../../data/years.json";
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    season::Data d;
    std::string err;
    EXPECT(season::parse(ss.str(), d, err));
    real(d);
    if (failures) {
        std::printf("rnd_model_logic_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("rnd_model_logic_test: ok\n");
    return 0;
}
