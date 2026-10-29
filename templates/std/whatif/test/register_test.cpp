// SPDX-License-Identifier: MIT
#include "../native/src/whatif.cpp"

#include "whatif_test.hpp"

static void the_2017_edit()
{
    Run run("test/storage/");
    auto seasons = run.core.layers("career.season");
    check(seasons.size() == 1, "one layer on career.season");
    check(seasons.size() == 1 && seasons[0]->id == "swofty.progressive/2017", "on Progressive's 2017 season");
    check(seasons.size() == 1 && seasons[0]->scope == SWOFTY_SCOPE_PROFILE, "in the profile scope");

    if (seasons.size() == 1) {
        auto v = run.core.apply(*seasons[0], json::parse(kSeason2017));
        check(v.has_value(), "the season edit runs");
        if (v) {
            const json::Value &mercedes = (*v)["teams"][0];
            check(mercedes["seats"][0].as_string() == "game/fernando_alonso", "Alonso takes the first Mercedes seat");
            check(mercedes["seats"][1].as_string() == "game/valtteri_bottas", "Bottas keeps the second");
            check(mercedes["team"].as_string() == "constructor/mercedes", "the team stays Progressive's");
            check((*v)["teams"][1]["seats"][0].as_string() == "game/sebastian_vettel", "Ferrari is untouched");
            check((*v)["progressive"]["ratings_source"].as_string() == "official",
                  "Progressive's own data rides through");
        }
        auto other = run.core.apply(*seasons[0], json::parse(R"({ "year": 2017, "teams": [ { "slot": 2,
            "team": "constructor/ferrari", "seats": ["game/sebastian_vettel", "game/kimi_raikkonen"] } ] })"));
        check(other && (*other)["teams"].size() == 1 && (*other)["teams"][0]["slot"].as_int() == 2,
              "a slot the season doesn't have is skipped, not added");
    }

    auto ratings = run.core.layers("career.driver_ratings");
    check(ratings.size() == 1 && ratings[0]->id == "swofty.progressive/2017/game/fernando_alonso",
          "one layer on Alonso's 2017 ratings");
    check(ratings.size() == 1 && ratings[0]->scope == SWOFTY_SCOPE_PROFILE, "in the profile scope");
    if (ratings.size() == 1) {
        auto r = run.core.apply(*ratings[0], json::parse(kAlonso2017));
        check(r && (*r)["pace"].as_double() == 96, "his pace becomes 96");
        check(r && (*r)["racecraft"].as_double() == 93, "his other ratings stay");
    }

    if (!run.core.rules()) {
        std::printf("skip  effective values and origins: no registry engine linked\n");
        return;
    }
    auto season = run.core.effective(swofty::career::seasons, swofty::career::seasons["swofty.progressive/2017"]);
    check(season && season->teams.find(1) && season->teams.find(1)->seats[0].id() == "game/fernando_alonso",
          "the effective 2017 season has Alonso in the Mercedes");
    check(season && season->teams.find(2) && season->teams.find(2)->seats[0].id() == "game/sebastian_vettel",
          "and Ferrari as Progressive has it");
    check(run.core.origin("career.season", "swofty.progressive/2017", "/teams/slot=1/seats/0") == "tails.whatif-history",
          "the seat's origin is this addon");
    check(run.core.origin("career.season", "swofty.progressive/2017", "/teams/slot=2/seats/0") == "swofty.progressive",
          "Ferrari's seat is still Progressive's");
    auto alonso = run.core.effective("career.driver_ratings", "swofty.progressive/2017/game/fernando_alonso");
    check(alonso && (*alonso)["pace"].as_double() == 96, "the effective 2017 ratings have his pace at 96");
    check(run.core.origin("career.driver_ratings", "swofty.progressive/2017/game/fernando_alonso", "/pace") ==
              "tails.whatif-history",
          "and the pace's origin is this addon");
}

int main()
{
    the_2017_edit();
    return failures ? 1 : 0;
}
