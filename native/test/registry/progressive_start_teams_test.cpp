// The start-season team plan (native/src/core/career_ops/start_teams.hpp) on Progressive's real records: the captured
// registrations of data/years.json (register/run.sh), the slot rows Progressive gives set_slots (years.json slots[]),
// and with "layer" the IG6 team pack's layer (2016 slot 11 = ig.teampack/ig_racing with Ida Gamer and Rio Haryanto).
// Prints the plan JSON; with no layer the plan has no year, so the wizard dresses nothing.

#include "../../src/core/career_ops/start_teams.hpp"
#include "../../src/core/career_ops/teams_store.hpp"
#include "../../src/core/registry/declare_all.hpp"
#include "../../src/core/registry/older.hpp"
#include "progressive_seed.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace swofty_core::registry;
using namespace progressive_seed;
namespace ct = career_ops::teams;

static int failures = 0;
#define CHECK(c, ...)                          \
    do {                                       \
        if (!(c)) {                            \
            std::printf("FAIL: " __VA_ARGS__); \
            std::printf("\n");                 \
            failures++;                        \
        }                                      \
    } while (0)

int main(int argc, char **argv)
{
    if (argc < 4) {
        std::printf("usage: progressive_start_teams_test <captured.json> <years.json> none|layer\n");
        return 2;
    }
    bool with_layer = std::string(argv[3]) == "layer";
    Store &st = store();
    declare_system_registries([&](const std::string &name, const std::string &schema) {
        return !st.declared(name) && st.declare(name, swofty::json::parse(schema), false, "") == 0;
    });
    older::declare(st);
    seed_stock(st);
    Value calls = swofty::json::parse(slurp(argv[1]));
    Value years = swofty::json::parse(slurp(argv[2]));
    int added = add_all(st, "swofty.progressive", calls);
    if (with_layer) {
        std::string id;
        int t = st.add("ig.teampack", "team.identity", "ig_racing",
                       swofty::json::parse(R"({"donor":"game/williams","names":{"title":{"literal":"IG Racing"},"code":"IGR"},
                                              "livery_name":"ig3_racing","logo":"ig.teampack/ig_racing"})"),
                       &id);
        std::string did;
        int dr = st.add("ig.teampack", "driver.identity", "ig_driver",
                        swofty::json::parse(R"({"donor":"game/jarno_opmeer","names":{"first":{"literal":"Ida"},"last":{"literal":"Gamer"},
                                               "full":{"literal":"Ida Gamer"},"code":"IGA"},"nationality":"great_britain","number":77})"),
                        &did);
        int rh = st.add("swofty.classic-seasons", "driver.identity", "rio_haryanto",
                        swofty::json::parse(R"({"donor":"game/jarno_opmeer","names":{"full":{"literal":"Rio Haryanto"},"code":"HAR"},
                                               "number":88})"),
                        &did);
        CHECK(dr == 0 && rh == 0, "the layer's two drivers are driver.identity records (%d, %d)", dr, rh);
        LayerSpec sp;
        sp.addon = "ig.teampack";
        sp.registry = "career.season";
        sp.id = "swofty.progressive/2016";
        sp.scope = Scope::profile;
        sp.change.is_patch = true;
        sp.change.patch = swofty::json::parse(
            R"({"teams":[{"slot":11,"team":"ig.teampack/ig_racing","seats":["ig.teampack/ig_driver","swofty.classic-seasons/rio_haryanto"]}]})");
        uint32_t handle = 0;
        int l = st.layer(sp, &handle);
        CHECK(t == 0 && l == 0, "the team pack's identity and its 2016 slot 11 layer are accepted (%d, %d)", t, l);
    }
    st.end_register();

    const std::string group(years["data_group"].as_string());
    std::vector<std::string> rows;
    for (auto &s : *years["slots"].if_array())
        rows.push_back(std::string(s["team"].as_string()));
    std::vector<ct::SeasonIn> seasons = ct::seasons_of(st, group);
    CHECK(added > 0 && seasons.size() == 11, "the effective career.season records of %s are the 11 seasons (%zu)", group.c_str(),
          seasons.size());
    CHECK(rows.size() == 11 && rows[10] == "gm_cadillac_26", "slot 11's row is gm_cadillac_26");

    if (with_layer) {
        Record r16 = st.get("career.season", "swofty.progressive/2016");
        CHECK(r16.status == Status::ok, "the layered 2016 season is valid (%s)", r16.why.c_str());
    }
    for (auto &s : seasons)
        if (s.year == 2016)
            for (auto &x : s.slots)
                if (x.slot == 11)
                    std::printf("2016 slot 11: %s (%s, %s)\n", x.team.c_str(), x.seats[0].c_str(), x.seats[1].c_str());
    ct::StartPlan plan = ct::start_plan(group, seasons, rows);
    std::string json = ct::plan_json(plan);
    std::printf("%s\n", json.c_str());
    for (auto &line : ct::describe(plan))
        std::printf("  %s\n", line.c_str());

    if (!with_layer) {
        CHECK(json == "{\"data_group\":\"f1_career_progressive\",\"years\":[]}",
              "no layer: no year of Progressive's own records names an added team, so the wizard dresses nothing");
        CHECK(plan.years.empty(), "and the plan has no notes either");
    } else {
        const ct::StartYear *y16 = plan.find(2016);
        CHECK(plan.dresses() == 1 && y16 && y16->dresses.size() == 1, "the layer adds exactly one dress, in 2016");
        if (y16 && !y16->dresses.empty()) {
            const ct::StartDress &d = y16->dresses[0];
            CHECK(d.slot == 11 && d.row == "gm_cadillac_26" && d.team == "ig.teampack/ig_racing",
                  "2016 slot 11: gm_cadillac_26 wears ig.teampack/ig_racing in the new-career wizard");
            CHECK(d.seats[0] == "ig.teampack/ig_driver" && d.seats[1] == "swofty.classic-seasons/rio_haryanto",
                  "with the layer's seats (%s, %s)", d.seats[0].c_str(), d.seats[1].c_str());

            ct::Table table = ct::build_table(seasons, false);
            ct::anchor_rows(table, rows);
            const ct::SeasonIn *s16 = nullptr;
            for (auto &s : seasons)
                if (s.year == 2016)
                    s16 = &s;
            ct::Plan season = s16 ? ct::plan_season(table, *s16) : ct::Plan{};
            CHECK(table.ok() && table.count() == 11, "the season start's slot table keeps 11 rows: no second row for the added team");
            CHECK(season.ok() && season.actions.size() == 11 && season.actions[10].kind == ct::Kind::dressed &&
                      season.actions[10].row == d.row && season.actions[10].team == d.team,
                  "the 2016 season start dresses the same row as the same team (%s)", ct::summary(season).c_str());
        }
        for (int year : {2017, 2018, 2025, 2026})
            CHECK(!plan.find(year), "%d dresses nothing", year);
    }
    std::printf("%s: start-season team plan on Progressive's records (%s)\n", failures ? "FAIL" : "ok",
                with_layer ? "the team pack's 2016 slot 11 layer" : "no layer");
    return failures ? 1 : 0;
}
