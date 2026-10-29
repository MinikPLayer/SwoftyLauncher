// The table version 4 operations' logic on the host: set_seat's planner (seat_plan.hpp), set_car_number's
// rules (number_plan.hpp) and set_track_fuel's named data sets, burn writes and track filter (fuel_logic.hpp).

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../src/core/career_ops/fuel_logic.hpp"
#include "../../src/core/career_ops/number_plan.hpp"
#include "../../src/core/career_ops/seat_plan.hpp"

using namespace career_ops;

static int failures = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                                    \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

static uint32_t key(uint32_t stat) { return stat << 16; }
static plan::Row race(uint32_t d, int team, int pos, bool player = false) { return {key(d), team, pos, true, player}; }
static plan::Row free_agent(uint32_t d) { return {key(d), -1, -1, false, false}; }
static plan::Row reserve(uint32_t d, int team) { return {key(d), team, -1, false, false}; }

static std::vector<plan::Row> grid()
{
    return {race(5, 476, 0), race(6, 476, 1), race(7, 484, 0), race(8, 484, 1), race(1, 477, 0, true), race(2, 477, 1),
            free_agent(30), reserve(31, 476), free_agent(32), free_agent(32)};
}

static void test_seats()
{
    using seat::Holder;
    auto g = grid();
    seat::Plan p = seat::plan(g, 30, 476, 0);
    CHECK(p.refusal.empty() && !p.unchanged && p.what == Holder::released && p.holder == 5 && p.from_team == -1);
    CHECK(p.player_team == 477);

    p = seat::plan(g, 7, 476, 1);
    CHECK(p.refusal.empty() && p.what == Holder::swapped && p.holder == 6 && p.from_team == 484 && p.from_seat == 0);

    p = seat::plan(g, 6, 476, 0);
    CHECK(p.refusal.empty() && p.what == Holder::swapped && p.holder == 5 && p.from_team == 476 && p.from_seat == 1);

    p = seat::plan(g, 5, 476, 0);
    CHECK(p.refusal.empty() && p.unchanged);

    CHECK(seat::plan(g, 1, 476, 0).refusal == "the player is never moved");
    CHECK(seat::plan(g, 30, 477, 0).refusal == "the player's seat is never given away");
    CHECK(seat::plan(g, 31, 484, 0).refusal.find("reserve") != std::string::npos);
    CHECK(seat::plan(g, 99, 476, 0).refusal == "the driver has no career row");
    CHECK(seat::plan(g, 32, 476, 0).refusal == "the driver has more than one career row");
    CHECK(seat::plan(g, 30, 476, 2).refusal == "seat must be 0 or 1");
    CHECK(seat::plan(g, 30, -1, 0).refusal == "no team for the seat");

    p = seat::plan(g, 30, 486, 0);
    CHECK(p.refusal.empty() && p.what == Holder::none && p.holder == -1);
    CHECK(seat::plan(g, 7, 486, 0).refusal.find("left empty") != std::string::npos);

    CHECK(std::strcmp(seat::name(Holder::released), "released") == 0 && std::strcmp(seat::name(Holder::none), "none") == 0);
}

static void test_numbers()
{
    using numbers::Error;
    std::vector<numbers::Row> rows = {{key(5), 44, true}, {key(6), 63, true}, {key(30), numbers::kNone, false}, {key(31), 14, true}};
    numbers::Plan p = numbers::plan(rows, key(5), 7, false);
    CHECK(p.error == Error::none && p.row == 0 && p.swap_with == -1 && !p.unchanged);
    CHECK(numbers::plan(rows, key(5), 44, false).unchanged);
    CHECK(numbers::plan(rows, key(5), 63, false).error == Error::taken);
    p = numbers::plan(rows, key(5), 63, true);
    CHECK(p.error == Error::none && p.swap_with == 1);
    CHECK(numbers::plan(rows, key(30), 63, true).error == Error::nothing_to_give);
    CHECK(numbers::plan(rows, key(30), 9, false).error == Error::none);
    CHECK(numbers::plan(rows, key(5), 0, false).error == Error::bad_value);
    CHECK(numbers::plan(rows, key(5), 100, false).error == Error::bad_value);
    CHECK(numbers::plan(rows, key(5), 7.5, false).error == Error::bad_value);
    CHECK(numbers::plan(rows, key(99), 7, false).error == Error::no_entrant);
}

static fuel::Track track(uint32_t index, uintptr_t object, uintptr_t record, const char *name, float burn, uint64_t rows)
{
    fuel::Track t;
    t.index = index;
    t.object = object;
    t.record = record;
    t.name = name;
    t.fuel.burn = burn;
    t.fuel.count = rows;
    t.fuel.one_shot = burn * 3.5f;
    return t;
}

static void test_fuel_sets()
{
    CHECK(fuel::table_named("f1_2025") == &fuel::kSource && fuel::table_named("f1_2026") == &fuel::kTarget);
    CHECK(fuel::table_named("sprint_f1_2025") == &fuel::kSprintSource && fuel::table_named("sprint_f1_2026") == &fuel::kSprintTarget);
    CHECK(!fuel::table_named("classic") && !fuel::table_named(""));

    fuel::Fuel f;
    f.burn = 1.30f;
    f.count = 1;
    f.full_quali = 4.5f;
    fuel::Fuel b = fuel::with_burn(f, 1.74f);
    CHECK(b.burn == 1.74f && b.count == 1 && b.full_quali == 4.5f && !fuel::same(f, b));

    fuel::Set dst, src;
    dst.tracks = {track(0, 0x100, 0x10, "melbourne", 1.30f, 1), track(1, 0x200, 0x20, "silverstone", 1.31f, 1),
                  track(2, 0x300, 0x30, "imola", 1.61f, 1)};
    src.tracks = {track(0, 0x400, 0x10, "melbourne", 1.74f, 1), track(1, 0x500, 0x20, "silverstone", 1.88f, 1),
                  track(2, 0x600, 0x99, "imola", 1.61f, 1)};
    size_t skipped = 0;
    auto all = fuel::plan_track(dst, src, "*", skipped);
    CHECK(all.size() == 2 && skipped == 1);
    auto mel = fuel::plan_track(dst, src, "melbourne", skipped);
    CHECK(mel.size() == 1 && mel[0].dst->name == "melbourne" && mel[0].src->fuel.burn == 1.74f);
    CHECK(fuel::merged(mel[0].dst->fuel, mel[0].src->fuel).burn == 1.74f);
    CHECK(fuel::plan_track(dst, src, "monaco", skipped).empty());
}

int main()
{
    test_seats();
    test_numbers();
    test_fuel_sets();
    if (failures) {
        std::printf("ops_logic_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("ops_logic_test: set_seat plans (swap, release, empty seat, refusals), set_car_number rules, track fuel "
                "data sets, burn and track filter: ok\n");
    return 0;
}
