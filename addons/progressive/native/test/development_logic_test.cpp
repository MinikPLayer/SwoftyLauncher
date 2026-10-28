// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The in-season development curves of the committed data/years.json and the progress they give the
// downforce upgrade types (development_logic.hpp).

#include "development_logic.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

using namespace progressive;
using season::DevPoint;

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

bool near(double a, double b, double eps = 1e-9)
{
    return std::fabs(a - b) <= eps;
}

void synthetic()
{
    std::vector<DevPoint> c = {{0.1, 0.6}, {0.3, 0.2}, {0.5, 0.0}, {0.7, -0.3}, {0.9, -0.5}};
    EXPECT(near(development::delta_at(c, 0.0), 0.6));
    EXPECT(near(development::delta_at(c, 0.1), 0.6));
    EXPECT(near(development::delta_at(c, 0.2), 0.4));
    EXPECT(near(development::delta_at(c, 0.6), -0.15));
    EXPECT(near(development::delta_at(c, 0.95), -0.5));
    EXPECT(near(development::delta_at(c, 1.0), -0.5));
    EXPECT(near(development::delta_at({}, 0.5), 0.0));

    EXPECT(near(development::season_point(0, 24), 0.0));
    EXPECT(near(development::season_point(23, 24), 1.0));
    EXPECT(near(development::season_point(30, 24), 1.0));
    EXPECT(near(development::season_point(5, 11), 0.5));
    EXPECT(near(development::season_point(0, 1), 0.0));
    EXPECT(near(development::season_point(3, 0), 0.0));

    EXPECT(near(development::progress(0.6, 0.6, 2.0), 0.0));
    EXPECT(near(development::progress(0.6, -0.4, 2.0), 0.5));
    EXPECT(near(development::progress(0.6, -2.0, 2.0), 1.0));
    EXPECT(near(development::progress(0.6, 0.9, 2.0), 0.0));
    EXPECT(near(development::progress(0.6, 0.0, 0.0), 0.0));
    EXPECT(development::lever_type(9) && development::lever_type(10));
    EXPECT(!development::lever_type(5) && !development::lever_type(0) && !development::lever_type(18));

    season::Data d;
    season::Year &y = d.years[2030];
    y.year = 2030;
    y.teams[0].development = c;
    y.teams[1].development = {{0.1, -0.2}, {0.9, 0.2}};
    development::Plan p = development::plan(d, 2030, 0, 11, 2.0);
    EXPECT(near(p.base, 0.6));
    EXPECT(p.teams[0].curve && near(p.teams[0].delta, 0.6) && near(p.teams[0].progress, 0.0));
    EXPECT(p.teams[1].curve && near(p.teams[1].delta, -0.2) && near(p.teams[1].progress, 0.4));
    EXPECT(!p.teams[2].curve && near(p.teams[2].delta, 0.0) && near(p.teams[2].progress, 0.3));
    EXPECT(development::applied_text(p) == "1:+0.600,2:-0.200");
    development::Plan again = development::plan(d, 2030, 0, 11, 2.0);
    for (int i = 0; i < season::kSlots; ++i)
        EXPECT(again.teams[i].progress == p.teams[i].progress && again.teams[i].delta == p.teams[i].delta);
    development::Plan none = development::plan(d, 2031, 4, 11, 2.0);
    EXPECT(!development::any_curve(none) && near(none.base, 0.0));
    for (auto &e : none.teams)
        EXPECT(near(e.progress, 0.0));
}

void real(const season::Data &d)
{
    for (int year = 2016; year <= 2025; ++year) {
        const season::Year *y = d.year(year);
        EXPECT(y != nullptr);
        if (!y)
            continue;
        int curves = 0;
        for (auto &t : y->teams) {
            if (t.kind != "real") {
                EXPECT(t.development.empty());
                continue;
            }
            EXPECT(t.development.size() == 5);
            if (t.development.size() != 5)
                continue;
            ++curves;
            double sum = 0;
            for (size_t i = 0; i < t.development.size(); ++i) {
                sum += t.development[i].delta_pct;
                EXPECT(std::fabs(t.development[i].delta_pct) < 1.5);
                if (i)
                    EXPECT(t.development[i].at > t.development[i - 1].at);
            }
            EXPECT(std::fabs(sum / 5) < 0.005);
        }
        EXPECT(curves == (year == 2016 ? 11 : 10));
        double base = development::year_base(*y);
        for (uint32_t r = 0; r < 24; ++r) {
            development::Plan p = development::plan(d, year, r, 24);
            int clamped = 0;
            for (auto &e : p.teams) {
                EXPECT(e.progress >= 0.0 && e.progress <= 1.0);
                clamped += e.clamped;
                if (!e.curve)
                    EXPECT(near(e.progress, base / development::kGainPct));
            }
            EXPECT(clamped <= 1);
        }
        for (int i = 0; i < season::kSlots; ++i) {
            if (y->teams[i].development.size() != 5)
                continue;
            double mean = 0;
            for (auto &c : y->teams[i].development)
                mean += development::progress(base, c.delta_pct);
            mean /= 5;
            EXPECT(std::fabs(mean - base / development::kGainPct) < 0.02);
        }
    }
    const season::Year *y2026 = d.year(2026);
    EXPECT(y2026 && !development::any_curve(development::plan(d, 2026, 3, 24)));

    auto at = [&](int year, int slot, double t) { return development::delta_at(d.year(year)->teams[slot - 1].development, t); };
    EXPECT(at(2016, 3, 0.0) > 0.5 && at(2016, 3, 1.0) < -0.2);
    EXPECT(at(2017, 4, 0.0) > 0.8 && at(2017, 4, 0.7) < -0.5);
    EXPECT(at(2019, 2, 0.3) > 0.4 && at(2019, 2, 0.7) < -0.2);
    EXPECT(at(2023, 4, 0.0) > 0.8 && at(2023, 4, 0.5) < -0.4);
    EXPECT(at(2024, 3, 0.0) < at(2024, 3, 1.0));

    development::Plan p = development::plan(d, 2023, 0, 22);
    development::Plan q = development::plan(d, 2023, 11, 22);
    EXPECT(q.teams[3].progress > p.teams[3].progress + 0.3);
    std::printf("2023 McLaren: round 1 delta %+.2f%% progress %.3f, round 12 delta %+.2f%% progress %.3f\n",
                p.teams[3].delta, p.teams[3].progress, q.teams[3].delta, q.teams[3].progress);
}

} // namespace

int main(int argc, char **argv)
{
    synthetic();
    const char *path = argc > 1 ? argv[1] : "../../data/years.json";
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    season::Data d;
    std::string err;
    EXPECT(season::parse(ss.str(), d, err));
    if (err.size())
        std::printf("parse: %s\n", err.c_str());
    real(d);
    if (failures) {
        std::printf("development_logic_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("development_logic_test: ok\n");
    return 0;
}
