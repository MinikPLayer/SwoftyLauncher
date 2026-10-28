// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Which year the career's seats show (seats_logic.hpp), over the committed data/years.json.

#include "seats_logic.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

using namespace progressive;

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

season::Data g_data;
std::map<std::string, int> g_vehicles;

int stat(const std::string &label)
{
    const season::PoolDriver *p = g_data.driver(label);
    return p ? p->stat : -1;
}

season::Resolve resolver()
{
    season::Resolve r;
    r.driver_stat = stat;
    r.vehicle_stat = [](const std::string &label) {
        for (auto &s : g_data.slots)
            if (s.vehicle_2026 == label)
                return s.vehicle_2026_stat;
        auto it = g_vehicles.find(label);
        if (it != g_vehicles.end())
            return it->second;
        int id = 2000 + static_cast<int>(g_vehicles.size());
        g_vehicles[label] = id;
        return id;
    };
    r.look_known = [](int) { return true; };
    r.rating = [](uint32_t) { return 75.0; };
    return r;
}

std::vector<int> due_at(const season::Year &y, int k)
{
    std::vector<int> due;
    for (size_t i = 0; i < y.changes.size(); ++i) {
        const season::Change &c = y.changes[i];
        if (c.round <= k && (c.until <= 0 || k < c.until))
            due.push_back(static_cast<int>(i));
    }
    return due;
}

std::vector<seats::Seated> seated_at(int year, int real_round, bool year_cars)
{
    season::Resolve r = resolver();
    const season::Year *y = g_data.year(year);
    std::vector<int> active;
    std::vector<std::string> notes;
    season::Year at = midseason::line_up_with(*y, due_at(*y, real_round), active, notes);
    lineup::Grid g = season::grid_of(at, r, nullptr, nullptr);
    std::vector<seats::Seated> out;
    for (int s = 0; s < season::kSlots; ++s)
        for (auto d : g[s])
            if (d != lineup::kNone)
                out.push_back({s, d, year_cars ? r.vehicle_stat(y->teams[s].vehicle) : g_data.slots[s].vehicle_2026_stat});
    return out;
}

bool sits(const std::vector<seats::Seated> &v, int slot, const std::string &driver)
{
    for (auto &s : v)
        if (s.slot == slot && s.driver == static_cast<uint32_t>(stat(driver)))
            return true;
    return false;
}

int slot_of(const std::string &team)
{
    for (int i = 0; i < season::kSlots; ++i)
        if (g_data.slots[i].team == team)
            return i;
    return -1;
}

} // namespace

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "../../data/years.json";
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string err;
    if (!season::parse(ss.str(), g_data, err)) {
        std::printf("seats_logic_test: cannot parse %s: %s\n", path, err.c_str());
        return EXIT_FAILURE;
    }
    season::Resolve r = resolver();
    std::map<int, int> cars = seats::car_years(g_data, r);
    int tr = slot_of("tororosso_26"), re = slot_of("renault_26");

    std::vector<seats::Seated> late17 = seated_at(2017, 20, true);
    EXPECT(sits(late17, tr, "brendon_hartley") && sits(late17, tr, "pierre_gasly"));
    EXPECT(sits(late17, re, "nico_hulkenberg") && sits(late17, re, "carlos_sainz-jr"));
    seats::Years by_cars = seats::seats_years(g_data, r, late17, cars);
    EXPECT(by_cars.how == "cars");
    EXPECT(by_cars.years == std::vector<int>{2017});

    std::vector<seats::Seated> stock = seated_at(2017, 20, false);
    seats::Years by_lineup = seats::seats_years(g_data, r, stock, cars);
    EXPECT(by_lineup.how == "line-ups");
    EXPECT(by_lineup.years == std::vector<int>{2017});
    EXPECT(seats::line_up_match(g_data, r, *g_data.year(2017), stock) >
           seats::line_up_match(g_data, r, *g_data.year(2018), stock));
    int main_only = 0;
    lineup::Grid g17 = season::grid_of(*g_data.year(2017), r, nullptr, nullptr);
    for (auto &s : stock)
        for (auto d : g17[s.slot])
            main_only += d == s.driver;
    EXPECT(main_only < seats::line_up_match(g_data, r, *g_data.year(2017), stock));

    std::vector<seats::Seated> start18 = seated_at(2018, 0, true);
    EXPECT(seats::seats_years(g_data, r, start18, cars).years == std::vector<int>{2018});
    std::vector<seats::Seated> start16 = seated_at(2016, 0, false);
    EXPECT(seats::seats_years(g_data, r, start16, cars).years == std::vector<int>{2016});

    std::vector<seats::Seated> mixed = seated_at(2017, 20, true);
    for (size_t i = 0; i < mixed.size() / 2 + 1 && i < mixed.size(); ++i)
        mixed[i].vehicle = g_data.slots[mixed[i].slot].vehicle_2026_stat;
    EXPECT(seats::seats_years(g_data, r, mixed, cars).how == "line-ups");

    if (failures) {
        std::printf("seats_logic_test: %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("seats_logic_test: ok\n");
    return EXIT_SUCCESS;
}
