// SPDX-License-Identifier: MIT
#include <map>
#include <string>

#include <swofty/registry.hpp>
#include <swofty/systems/cars.hpp>
#include <swofty/systems/content.hpp>
#include <swofty/systems/sounds.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    // mod.esp/v10_screamer: the bundle is this addon's file; mixing and tuning come from the stock Ferrari
    auto v10 = sounds::engine_sounds.add("v10_screamer", {
        .bundle = file("sounds/v10_screamer.bdl"),
        .settings_from = sounds::engine_sounds["game/ferrari_2025"],
        .tuning_from = sounds::engine_sounds["game/ferrari_2025"],
    });

    // Classic Seasons' 2017 Ferrari gets the V10, everywhere that car races
    cars::vehicles.layer(cars::vehicles["swofty.classic-seasons-2017/ferrari"], [=](cars::Vehicle &v) {
        v.engine_sound = v10;
    });

    // "F1 2017 (V10)": the 2017 season as its own box, with the Mercedes and the Red Bull on the V10 too.
    // Their V10 cars are variants, so the real 2017 box keeps its sounds.
    std::map<std::string, Ref<cars::Vehicle>> v10_cars;
    for (std::string team : {"mercedes", "redbull"})
        v10_cars[team] = cars::vehicles.variant(team + "_v10", cars::vehicles["swofty.classic-seasons-2017/" + team],
                                                [=](cars::Vehicle &v) {
            v.label = "esp_cs17_" + team;
            v.engine_sound = v10;
        });

    auto season = content::packages.variant("f1_2017_v10", content::packages["swofty.classic-seasons-2017/f1_2017"],
                                            [=](content::Package &p) {
        p.title = Text::literal("F1 2017 (V10)");
        p.entrant_list = "esp_f1_2017_v10";
        p.stat_id = StatId::automatic;                       // its own stat id, not the 2017 package's
        for (const auto &[team, car] : v10_cars)
            p.teams.at(team).vehicle = car;
    });

    content::era_boxes.add("f1_2017_v10", {.package = season, .title = Text::literal("F1 2017 (V10)"), .order = 2017});
}

SWOFTY_ADDON()
