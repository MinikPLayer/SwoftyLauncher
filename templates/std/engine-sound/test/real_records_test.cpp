// SPDX-License-Identifier: MIT
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include <swofty/fake_core.hpp>

#include "../native/src/esp.cpp"
#include "../../../../native/src/core/registry/declare_all.hpp"
#include "../../../../native/src/core/registry/older.hpp"
#include "../../../../native/src/core/registry/table.hpp"
#include "../../../../native/src/core/apply/content/boot.hpp"

static int failures = 0;

static void check(bool ok, const char *what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    failures += !ok;
}

static std::string slurp(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main()
{
    namespace reg = swofty_core::registry;
    const std::string root = "../../..";
    reg::reset_store();
    auto &st = reg::store();
    st.set_order({"swofty.classic-seasons-2017", "mod.esp"});
    st.set_validator(&reg::schema_validator());
    swofty_core::registry::declare_system_registries([&](const std::string &name, const std::string &schema) {
        if (st.declared(name))
            return false;
        return st.declare(name, swofty::json::parse(schema), false, "") == 0;
    });
    swofty_core::registry::older::declare(st);
    check(reg::load_data_file("swofty.classic-seasons", "registry/drivers.json", R"j({ "driver.identity": { "stoffel_vandoorne": {
        "names": { "first": { "literal": "Stoffel" }, "last": { "literal": "Vandoorne" }, "full": { "literal": "Stoffel Vandoorne" }, "code": "VAN" } } } })j") == SWOFTY_ASSET_OK, "stands in for the base addon's driver");
    std::string text = slurp(root + "/addons/classicseasons/classic-seasons-2017/registry/content.json");
    check(!text.empty(), "reads Classic Seasons 2017's real content.json");
    check(reg::load_data_file("swofty.classic-seasons-2017", "registry/content.json", text) == SWOFTY_ASSET_OK, "loads the real records");

    swofty::fake::Core core{"mod.esp"};
    core.run_register();
    st.end_register();

    auto pkg = st.get("content.package", "mod.esp/f1_2017_v10");
    check(pkg.status == reg::Status::ok, "the F1 2017 (V10) variant is valid");
    auto base = st.get("content.package", "swofty.classic-seasons-2017/f1_2017");
    check(base.status == reg::Status::ok, "the real 2017 package is valid");
    auto box = st.get("era.box", "mod.esp/f1_2017_v10");
    check(box.status == reg::Status::ok, "its era box resolves");
    if (pkg.status == reg::Status::ok) {
        const auto &teams = pkg.value["teams"];
        bool merc = false, ferrari = false;
        for (std::size_t i = 0; i < teams.size(); i++) {
            if (teams[i]["key"].as_string() == "mercedes")
                merc = teams[i]["vehicle"].as_string() == "mod.esp/mercedes_v10";
            if (teams[i]["key"].as_string() == "ferrari")
                ferrari = teams[i]["vehicle"].as_string() == "swofty.classic-seasons-2017/ferrari";
        }
        check(merc, "the Mercedes drives the V10 variant");
        check(ferrari, "the Ferrari stays the 2017 Ferrari");
        check(pkg.value["entrant_list"].as_string() == "esp_f1_2017_v10", "its own entrant list");
    }
    auto ferrari = st.get("vehicle", "swofty.classic-seasons-2017/ferrari");
    check(ferrari.status == reg::Status::ok && ferrari.value["engine_sound"].as_string() == "mod.esp/v10_screamer",
          "the stand-in layer puts the V10 on the real 2017 Ferrari");
    namespace content = swofty_core::apply::content;
    std::map<std::string, swofty::json::Value> keep;
    content::Lookup lookup = content::own_lookup(st, keep);
    content::Plan plan;
    content::BaseImage base_image = content::box_base_image(st, lookup);
    for (auto &id : content::in_load_order(st, "era.box"))
        if (const auto *rec = lookup("era.box", id))
            content::lower_box(id, *rec, lookup, plan, base_image);
    const content::Box *v10 = nullptr, *real = nullptr;
    for (auto &b : plan.boxes) {
        if (b.id == "mod.esp/f1_2017_v10")
            v10 = &b;
        if (b.id == "swofty.classic-seasons-2017/f1_2017")
            real = &b;
    }
    check(v10 && real, "both boxes lower");
    if (v10 && real) {
        check(v10->image == real->image && !v10->image.empty(), "the variant box inherits the real box's tile art");
        check(v10->image == "#overrides.home_screen.tiles_solo_6x6[cs_f1_2017]", "which is the 2017 tile");
        check(v10->title == "lng_swofty_era_esp_f1_2017_v10" && v10->literal == "F1 2017 (V10)",
              "its literal title becomes a language tag with the text");
        check(real->title == "lng_cs_f1_2017" && real->literal.empty(), "the real box keeps its language tag");
    }
    for (const char *team : {"mercedes", "redbull"}) {
        const auto *veh = lookup("vehicle", std::string("mod.esp/") + team + "_v10");
        check(veh && (*veh)["team_from"].as_string() == std::string("cs17_") + team,
              "a variant vehicle names the base team row it copies");
        check(veh && (*veh)["label"].as_string() == std::string("esp_cs17_") + team, "under its own label");
    }
    const auto *toro = lookup("vehicle", "swofty.classic-seasons-2017/toro_rosso");
    check(toro && !(*toro)["team_from"].is_string(), "a base vehicle copies nothing");
    {
        std::map<std::string, std::string> db = {
            {"entrants.teams.team[_metadata_=cs17_redbull]._metadata_", "cs17_redbull"},
            {"pit_lane.pit_times._team_pit_times_instancecount_", "0"}};
        content::Read read = [&](const std::string &path) -> std::optional<std::string> {
            auto it = db.find(path);
            return it == db.end() ? std::nullopt : std::optional<std::string>(it->second);
        };
        content::Plan p;
        content::lower_vehicle_team(*lookup("vehicle", "mod.esp/redbull_v10"), read, p);
        check(!p.lines.empty() && p.lines[0].find("esp_cs17_redbull: row copied from cs17_redbull") != std::string::npos,
              "its team row is copied from the real car's");
        check(p.ops.size() >= 1, "as an operation");
        bool logos = !p.logos.empty();
        for (auto &l : p.logos)
            logos = logos && l.key == "esp_cs17_redbull" && l.stock == "cs17_redbull" && l.fallback && l.file.empty();
        check(logos, "and it takes the real car's team logo in every context");
        std::set<std::string> arrays;
        for (auto &l : p.logos)
            arrays.insert(l.array);
        check(arrays.count("teams.driver_select_team_logos") && arrays.count("team_logos.driver_career_2024_team_select"),
              "including driver select and team select");
    }
    {
        content::Plan pp;
        content::StatIds none;
        content::lower_package("mod.esp/f1_2017_v10", *lookup("content.package", "mod.esp/f1_2017_v10"), lookup, pp, none);
        std::string tag = "lng_swofty_era_esp_f1_2017_v10";
        std::string category;
        category = swofty::json::dump(pp.ops);
        check(category.find("\"loc_tag\":\"" + tag + "\"") != std::string::npos,
              "the variant's car type row names its own language tag");
        bool text = false;
        for (auto &[t, v] : pp.texts)
            text = text || (t == tag && v == "F1 2017 (V10)");
        check(text, "whose text is the variant's title");
    }
    check(swofty::content::packages.schema().find("\"type\":[\"number\",\"null\"]") != std::string::npos,
          "an optional field's schema accepts null");
    return failures ? 1 : 0;
}
