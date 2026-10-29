// SPDX-License-Identifier: MIT
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include <swofty/fake_core.hpp>

#include "../native/src/esp.cpp"

namespace json = swofty::json;

static int failures = 0;

static void check(bool ok, const char *what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    failures += !ok;
}

// What Classic Seasons 2017 registers, as far as this addon uses it.
static void classic_seasons_2017()
{
    using namespace swofty;
    for (const char *team : {"mercedes", "ferrari", "redbull"})
        sounds::engine_sounds.add(team, {.bundle = file(std::string("sounds/cs17_") + team + ".bdl")});
    for (const char *team : {"mercedes", "ferrari", "redbull"})
        cars::vehicles.add(team, {.label = std::string("cs17_") + team,
                                  .model = file(std::string("cars/cs17_") + team + ".erp"),
                                  .engine_sound = sounds::engine_sounds[std::string("swofty.classic-seasons-2017/") + team]});
    content::Package season{.title = "lng_cs_f1_2017", .entrant_list = "cs_f1_2017", .season = 2017};
    for (const char *team : {"mercedes", "ferrari", "redbull"})
        season.teams.push_back({.key = team, .vehicle = cars::vehicles[std::string("swofty.classic-seasons-2017/") + team]});
    content::packages.add("f1_2017", season);
}

static const swofty::fake::Record *recorded(const swofty::fake::Core &core, std::string_view op, std::string_view id)
{
    for (const auto &r : core.records())
        if (r.op == op && r.id == id)
            return &r;
    return nullptr;
}

int main()
{
    swofty::fake::Core core{"mod.esp"};
    if (core.rules()) {
        swofty::fake::Core::default_engine()->begin_addon("swofty.classic-seasons-2017");
        classic_seasons_2017();
    }
    core.run_register();

    auto sounds = core.ids("engine_sound");
    check(std::find(sounds.begin(), sounds.end(), "mod.esp/v10_screamer") != sounds.end(), "adds mod.esp/v10_screamer");
    auto sound = core.base("engine_sound", "mod.esp/v10_screamer");
    check(sound && (*sound)["bundle"].as_string() == "sounds/v10_screamer.bdl", "its bundle is the addon's file");
    check(sound && (*sound)["settings_from"].as_string() == "game/ferrari_2025", "mixing from the stock Ferrari");

    auto ferrari = core.layers("vehicle", "swofty.classic-seasons-2017/ferrari");
    check(ferrari.size() == 1 && ferrari[0]->scope == SWOFTY_SCOPE_GLOBAL, "one layer on the 2017 Ferrari");
    if (ferrari.size() == 1) {
        auto v = core.apply(*ferrari[0], json::parse(R"({ "label": "cs17_ferrari", "model": "cars/cs17_ferrari.erp",
            "engine_sound": "swofty.classic-seasons-2017/ferrari" })"));
        check(v && (*v)["engine_sound"].as_string() == "mod.esp/v10_screamer", "the layer puts the V10 on it");
        check(v && (*v)["label"].as_string() == "cs17_ferrari", "and leaves the rest of the car alone");
    }

    for (const char *team : {"mercedes", "redbull"}) {
        std::string id = std::string("mod.esp/") + team + "_v10";
        const auto *r = recorded(core, "variant", id);
        check(r && r->base == std::string("swofty.classic-seasons-2017/") + team, ("a V10 variant of the " + std::string(team)).c_str());
    }
    const auto *box = recorded(core, "variant", "mod.esp/f1_2017_v10");
    check(box && box->base == "swofty.classic-seasons-2017/f1_2017", "F1 2017 (V10) is a variant of the 2017 package");
    auto era = core.base("era.box", "mod.esp/f1_2017_v10");
    check(era && (*era)["package"].as_string() == "mod.esp/f1_2017_v10", "with its own era box");
    check(era && (*era)["order"].as_int() == 2017, "sorted with 2017");

    if (!core.rules()) {
        std::printf("skip  effective values and origins: no registry engine linked\n");
        return failures ? 1 : 0;
    }
    auto car = core.effective("vehicle", "swofty.classic-seasons-2017/ferrari");
    check(car && (*car)["engine_sound"].as_string() == "mod.esp/v10_screamer", "the effective 2017 Ferrari has the V10");
    check(core.origin("vehicle", "swofty.classic-seasons-2017/ferrari", "/engine_sound") == "mod.esp", "from mod.esp");
    auto mercedes = core.effective("vehicle", "swofty.classic-seasons-2017/mercedes");
    check(mercedes && (*mercedes)["engine_sound"].as_string() == "swofty.classic-seasons-2017/mercedes",
          "the real 2017 Mercedes keeps its sound");
    auto v10 = core.effective("vehicle", "mod.esp/mercedes_v10");
    check(v10 && (*v10)["engine_sound"].as_string() == "mod.esp/v10_screamer", "its V10 variant has the V10");
    check(v10 && (*v10)["model"].as_string() == "cars/cs17_mercedes.erp", "and the base car's model");
    check(v10 && (*v10)["label"].as_string() == "esp_cs17_mercedes", "under its own label");

    auto pkg = core.effective(swofty::content::packages, swofty::content::packages["mod.esp/f1_2017_v10"]);
    check(pkg && pkg->title.is_literal() && pkg->title.str() == "F1 2017 (V10)", "the box is called F1 2017 (V10)");
    check(pkg && pkg->season == 2017 && pkg->teams.size() == 3, "with the 2017 season's teams");
    check(pkg && pkg->teams.find(std::string("mercedes")) &&
              pkg->teams.find(std::string("mercedes"))->vehicle.id() == "mod.esp/mercedes_v10",
          "the Mercedes drives the V10 variant");
    check(pkg && pkg->teams.find(std::string("ferrari")) &&
              pkg->teams.find(std::string("ferrari"))->vehicle.id() == "swofty.classic-seasons-2017/ferrari",
          "the Ferrari is the 2017 Ferrari, with its layer");
    auto real = core.effective(swofty::content::packages, swofty::content::packages["swofty.classic-seasons-2017/f1_2017"]);
    check(real && real->teams.find(std::string("mercedes")) &&
              real->teams.find(std::string("mercedes"))->vehicle.id() == "swofty.classic-seasons-2017/mercedes",
          "the real 2017 box keeps its Mercedes");
    auto era_box = core.effective("era.box", "mod.esp/f1_2017_v10");
    check(era_box && (*era_box)["package"].as_string() == "mod.esp/f1_2017_v10", "the era box opens the variant");
    return failures ? 1 : 0;
}
