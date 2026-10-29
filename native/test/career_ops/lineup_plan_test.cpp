// career::apply_lineup's season planner (native/src/core/career_ops/lineup_plan.hpp) on the host.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../../src/core/career_ops/lineup_plan.hpp"

using namespace career_ops::plan;

static int failures = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                                    \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

static uint32_t key(uint32_t stat) { return stat << 16; }

static Row race(uint32_t driver, int team, int pos, bool player = false)
{
    return {key(driver), team, pos, true, player};
}

static Row free_agent(uint32_t driver)
{
    return {key(driver), -1, -1, false, false};
}

// Two teams: 476 (slot 1) holds 10 + 11, 477 (slot 2) holds 20 (the player) + 21; 30 and 31 are free agents.
static std::vector<Row> grid()
{
    return {race(10, 476, 0), race(11, 476, 1), race(20, 477, 0, true), race(21, 477, 1), free_agent(30), free_agent(31)};
}

static Input input()
{
    Input in;
    in.data_group = "f1_career_test";
    in.have_slots = true;
    in.slot_teams = {476, 477};
    return in;
}

static Team team(int slot, int a, int b, const char *ra = "game/a", const char *rb = "game/b")
{
    Team t;
    t.slot = slot;
    t.refs = {a == -2 ? "" : ra, b == -2 ? "" : rb};
    t.drivers = {a, b};
    return t;
}

static bool refused_with(const Plan &p, const std::string &what)
{
    for (auto &r : p.refusals)
        if (r.find(what) != std::string::npos)
            return true;
    return false;
}

int main()
{
    {
        Input in = input();
        in.teams = {team(1, 10, 11), team(2, 20, 21)};
        Plan p = plan(in, grid());
        CHECK(p.moves.empty() && p.releases.empty() && p.refusals.empty());
        CHECK(p.unchanged == 4);
        CHECK(p.player_team == 477);
    }
    {
        Input in = input();
        in.teams = {team(1, 30, 11)};
        Plan p = plan(in, grid());
        CHECK(p.moves.size() == 1 && p.moves[0].driver == 30 && p.moves[0].team == 476 && p.moves[0].seat == 0);
        CHECK(p.releases.size() == 1 && p.releases[0] == 10);
        CHECK(p.unchanged == 1 && p.refusals.empty());
    }
    {
        Input in = input();
        in.teams = {team(1, 11, 10)};
        Plan p = plan(in, grid());
        CHECK(p.moves.size() == 2 && p.releases.empty());
        CHECK(p.moves[0].driver == 11 && p.moves[0].seat == 0 && p.moves[1].driver == 10 && p.moves[1].seat == 1);
    }
    {
        Input in = input();
        in.teams = {team(2, 30, 21)};
        Plan p = plan(in, grid());
        CHECK(p.moves.empty() && p.releases.empty());
        CHECK(p.refusals.size() == 1 && refused_with(p, "the player's seat"));
        CHECK(p.unchanged == 1);
    }
    {
        Input in = input();
        in.teams = {team(1, 20, 11)};
        Plan p = plan(in, grid());
        CHECK(p.moves.empty() && p.releases.empty());
        CHECK(refused_with(p, "the player, who is never moved"));
    }
    {
        Input in = input();
        in.teams = {team(1, -1, 11, "game/nobody")};
        Plan p = plan(in, grid());
        CHECK(p.moves.empty() && p.releases.empty());
        CHECK(refused_with(p, "game/nobody is not in the drivers table"));
    }
    {
        Input in = input();
        in.teams = {team(1, 99, 11, "person/ghost")};
        Plan p = plan(in, grid());
        CHECK(refused_with(p, "person/ghost has no career row"));
        CHECK(p.releases.empty());
    }
    {
        Input in = input();
        in.teams = {team(1, 30, 30, "game/x", "game/x")};
        Plan p = plan(in, grid());
        CHECK(p.moves.size() == 1 && p.moves[0].seat == 0);
        CHECK(refused_with(p, "already seated at slot 1 seat 1"));
        CHECK(p.releases.size() == 1 && p.releases[0] == 10);
    }
    {
        Input in = input();
        in.have_slots = false;
        in.slot_teams.clear();
        in.teams = {team(1, 30, 11), team(2, 20, 21)};
        Plan p = plan(in, grid());
        CHECK(p.moves.empty() && p.releases.empty() && p.unchanged == 0);
        CHECK(p.refusals.size() == 4 && refused_with(p, "no slot table for data group f1_career_test"));
    }
    {
        Input in = input();
        in.teams = {team(3, 30, 31)};
        Plan p = plan(in, grid());
        CHECK(p.refusals.size() == 2 && refused_with(p, "has 2 slot(s)"));
    }
    {
        Input in = input();
        in.teams = {team(1, 31, -2)};
        Plan p = plan(in, grid());
        CHECK(p.moves.size() == 1 && p.releases.size() == 1 && p.releases[0] == 10 && p.unchanged == 0);
    }
    {
        std::vector<Row> rows = grid();
        rows.push_back(race(10, 476, 0));
        Input in = input();
        in.teams = {team(1, 10, 11)};
        Plan p = plan(in, rows);
        CHECK(refused_with(p, "has more than one career row"));
    }
    {
        Input in = input();
        in.teams = {team(1, 40, 30, "ig.teampack/ig_driver", "swofty.classic-seasons/rio_haryanto")};
        Plan p = plan(in, grid());
        CHECK(refused_with(p, "ig.teampack/ig_driver has no career row"));
        CHECK(p.moves.size() == 1 && p.moves[0].driver == 30 && p.moves[0].team == 476 && p.moves[0].seat == 1);
        CHECK(p.releases.size() == 1 && p.releases[0] == 11);
        defer_to(p, {{31, 476, 0}, {30, 476, 1}, {11, 478, 0}});
        CHECK(p.moves.empty() && p.releases.empty() && p.refusals.size() == 1);
    }
    {
        Input in = input();
        in.teams = {team(1, 30, 31)};
        Plan p = plan(in, grid());
        CHECK(p.moves.size() == 2 && p.releases.size() == 2);
        defer_to(p, {{30, 476, 1}, {21, 476, 0}});
        CHECK(p.moves.empty() && p.releases.size() == 2);
        CHECK(refused_with(p, "driver 30: the request's line-up seats him at team 476 seat 2, not the season record's team 476 seat 1"));
        CHECK(refused_with(p, "team 476 seat 2: the request's line-up gives it to driver 30, not the season record's driver 31"));
    }
    {
        Input in = input();
        in.teams = {team(1, 30, 11)};
        Plan p = plan(in, grid());
        defer_to(p, {{21, 477, 0}});
        CHECK(p.moves.size() == 1 && p.moves[0].driver == 30 && p.releases.size() == 1 && p.releases[0] == 10 &&
              p.refusals.empty());
    }
    if (failures) {
        std::printf("lineup_plan_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("lineup_plan_test: unchanged seats, moves, releases, the player's row and seat, unknown and duplicate "
                "drivers, no slot table, a record's seats next to the request's line-up: ok\n");
    return 0;
}
