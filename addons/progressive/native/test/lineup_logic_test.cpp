// SPDX-License-Identifier: AGPL-3.0-or-later

#include "lineup_logic.hpp"

#include <cstdio>
#include <cstdlib>

using namespace progressive::lineup;

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

Grid ten_team_grid()
{
    Grid g = empty_grid();
    for (int s = 0; s < 10; ++s) {
        g[s][0] = 100 + s * 2;
        g[s][1] = 101 + s * 2;
    }
    return g;
}

Grid eleven_team_grid()
{
    Grid g = ten_team_grid();
    g[10][0] = 120;
    g[10][1] = 121;
    return g;
}

std::vector<Rating> ratings_by_id()
{
    std::vector<Rating> r;
    for (Driver d = 100; d < 200; ++d)
        r.push_back({d, static_cast<float>(d % 2 ? 70 : 80)});
    return r;
}

void own_team_keeps_real_lineup()
{
    Input in;
    in.real = eleven_team_grid();
    in.player = 104;
    in.player_slot = 2;
    in.ratings = ratings_by_id();
    Output o = fit(in);
    for (int s = 0; s < kSlots; ++s)
        for (int p = 0; p < kSeats; ++p)
            EXPECT(o.at(s, p) == in.real[s][p]);
    EXPECT(o.displaced == kNone);
    EXPECT(o.reserve.empty());
    EXPECT(check(o).empty());
}

void player_swaps_with_lower_rated_driver()
{
    Input in;
    in.real = eleven_team_grid();
    in.player = 104;
    in.player_slot = 0;
    in.ratings = ratings_by_id();
    Output o = fit(in);
    EXPECT(o.at(0, 0) == 100);
    EXPECT(o.at(0, 1) == 104);
    EXPECT(o.seats[0][1].why == Why::player);
    EXPECT(o.displaced == 101);
    EXPECT(o.at(2, 0) == 101);
    EXPECT(o.seats[2][0].why == Why::displaced);
    EXPECT(o.at(2, 1) == 105);
    EXPECT(o.reserve.empty());
    EXPECT(check(o).empty());
}

void lower_rating_decides_not_seat_order()
{
    Input in;
    in.real = eleven_team_grid();
    in.player = 104;
    in.player_slot = 0;
    in.ratings = {{100, 60}, {101, 90}};
    Output o = fit(in);
    EXPECT(o.at(0, 0) == 104);
    EXPECT(o.at(0, 1) == 101);
    EXPECT(o.displaced == 100);
    EXPECT(o.at(2, 0) == 100);
}

void tie_displaces_seat_two()
{
    Input in;
    in.real = eleven_team_grid();
    in.player = 104;
    in.player_slot = 0;
    in.ratings = {{100, 75}, {101, 75}};
    Output o = fit(in);
    EXPECT(o.at(0, 0) == 100);
    EXPECT(o.at(0, 1) == 104);
    EXPECT(o.displaced == 101);
}

void player_who_did_not_race_sends_driver_to_reserve()
{
    Input in;
    in.real = eleven_team_grid();
    in.player = 150;
    in.player_slot = 3;
    in.ratings = ratings_by_id();
    in.pool = {150, 160, 161};
    Output o = fit(in);
    EXPECT(o.at(3, 0) == 106);
    EXPECT(o.at(3, 1) == 150);
    EXPECT(o.displaced == 107);
    EXPECT(o.reserve.size() == 1 && o.reserve[0] == 107);
    EXPECT(check(o).empty());
}

void ten_team_year_fills_slot_eleven_by_tiers()
{
    Input in;
    in.real = ten_team_grid();
    Grid prev = ten_team_grid();
    prev[4][1] = 170;
    Grid next = ten_team_grid();
    next[6][0] = 171;
    in.prev = &prev;
    in.next = &next;
    in.player = 104;
    in.player_slot = 2;
    in.ratings = {{170, 50}, {171, 60}, {172, 99}, {173, 98}};
    in.pool = {172, 173, 170, 171, 104};
    Output o = fit(in);
    EXPECT(o.at(10, 0) == 171);
    EXPECT(o.at(10, 1) == 170);
    EXPECT(o.seats[10][0].why == Why::filler);
    EXPECT(check(o).empty());
}

void ten_team_year_refits_displaced_driver_first()
{
    Input in;
    in.real = ten_team_grid();
    in.player = 150;
    in.player_slot = 0;
    in.ratings = ratings_by_id();
    in.ratings.push_back({172, 99});
    Grid prev = ten_team_grid();
    prev[1][0] = 172;
    in.prev = &prev;
    in.pool = {172, 150};
    Output o = fit(in);
    EXPECT(o.at(0, 1) == 150);
    EXPECT(o.displaced == 101);
    EXPECT(o.at(10, 0) == 101);
    EXPECT(o.at(10, 1) == 172);
    EXPECT(o.reserve.empty());
    EXPECT(check(o).empty());
}

void player_in_slot_eleven_of_ten_team_year()
{
    Input in;
    in.real = ten_team_grid();
    in.player = 108;
    in.player_slot = 10;
    in.ratings = ratings_by_id();
    in.ratings.push_back({180, 90});
    in.ratings.push_back({181, 80});
    in.pool = {180, 181, 108};
    Output o = fit(in);
    EXPECT(o.at(10, 0) == 108);
    EXPECT(o.seats[10][0].why == Why::player);
    EXPECT(o.displaced == kNone);
    EXPECT(o.at(4, 0) == 180);
    EXPECT(o.at(10, 1) == 181);
    EXPECT(check(o).empty());
}

void repeated_driver_sits_once()
{
    Input in;
    in.real = eleven_team_grid();
    in.real[5][1] = 100;
    in.player = 104;
    in.player_slot = 2;
    in.ratings = ratings_by_id();
    in.pool = {190};
    Output o = fit(in);
    EXPECT(o.at(0, 0) == 100);
    EXPECT(o.at(5, 1) == 190);
    EXPECT(!o.notes.empty());
    EXPECT(check(o).empty());
}

void empty_pool_leaves_seat_empty()
{
    Input in;
    in.real = ten_team_grid();
    in.player = 104;
    in.player_slot = 2;
    Output o = fit(in);
    EXPECT(o.at(10, 0) == kNone);
    EXPECT(o.at(10, 1) == kNone);
    EXPECT(o.notes.size() == 2);
}

void player_without_career_seat_is_removed()
{
    Input in;
    in.real = eleven_team_grid();
    in.player = 104;
    in.player_slot = -1;
    in.pool = {195};
    Output o = fit(in);
    EXPECT(o.at(2, 0) == 195);
    EXPECT(check(o).empty());
}

void closed_slot_stays_empty()
{
    Input in;
    in.real = ten_team_grid();
    in.player = 104;
    in.player_slot = 2;
    in.ratings = {{172, 99}, {173, 98}};
    in.pool = {172, 173, 104};
    in.closed[10] = true;
    Output o = fit(in);
    EXPECT(o.at(10, 0) == kNone && o.at(10, 1) == kNone);
    EXPECT(o.at(2, 0) == 104 || o.at(2, 1) == 104);
    EXPECT(check(o).empty());
}

} // namespace

int main()
{
    own_team_keeps_real_lineup();
    player_swaps_with_lower_rated_driver();
    lower_rating_decides_not_seat_order();
    tie_displaces_seat_two();
    player_who_did_not_race_sends_driver_to_reserve();
    ten_team_year_fills_slot_eleven_by_tiers();
    ten_team_year_refits_displaced_driver_first();
    player_in_slot_eleven_of_ten_team_year();
    repeated_driver_sits_once();
    empty_pool_leaves_seat_empty();
    player_without_career_seat_is_removed();
    closed_slot_stays_empty();
    if (failures) {
        std::printf("lineup_logic_test: %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("lineup_logic_test: ok\n");
    return EXIT_SUCCESS;
}
