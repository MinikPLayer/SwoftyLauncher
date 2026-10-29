#include "../../src/core/apply/content/boot.hpp"
#include "../../src/core/apply/ids.hpp"
#include "../../src/core/registry/table.hpp"
#include "../../src/core/registry/validate.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

using namespace swofty_core;
using swofty::json::Value;
namespace content = apply::content;
namespace rows = apply::content::rows;

static int failures = 0;

static void check(bool ok, const std::string &what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    failures += !ok;
}

static std::string slurp(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

struct Db {
    std::map<uint64_t, rows::Rec> recs;
    void put(const std::string &path, int tag, const std::string &text) { recs[content::gvdb::fnv64(path)] = {tag, text}; }
    void row(const std::string &prefix, std::initializer_list<std::pair<const char *, std::string>> fields)
    {
        for (auto &[f, v] : fields)
            put(prefix + "." + f, std::string_view(f) == "_metadata_" ? 2 : std::string_view(f) == "stat_id" || std::string_view(f) == "id" ? 0 : 2, v);
    }
    rows::Base base()
    {
        return [this](uint64_t key) -> std::optional<rows::Rec> {
            auto it = recs.find(key);
            if (it == recs.end())
                return std::nullopt;
            return it->second;
        };
    }
};

static void stock_rows(Db &db)
{
    const std::string cp = "asset_groups/[CHARACTER_PACKAGE]/";
    db.put("customisation.custom_avatar.suits._Suit_instancecount_", 0, "2");
    db.row("customisation.custom_avatar.suits.Suit_0",
           {{"_metadata_", "2025-sauber"}, {"asset_group", cp + "team_livery/2025-sauber/2025-sauber.assetgroup"},
            {"feet_model", "eaid://x"}, {"logos_model_override_pattern", "eaid://y"}, {"material_override", "2025-sauber"},
            {"id", "500"}, {"colour_1_default", ""}, {"colour_2_default", ""}, {"colour_3_default", ""}, {"colour_4_default", ""},
            {"name_loc_tag", "lng_placeholder_suits"}, {"is_official_team_item", "true"}});
    db.row("customisation.custom_avatar.suits.Suit_1", {{"_metadata_", "generic_suit"}, {"id", "510"}});
    db.put("customisation.custom_avatar.gloves._Gloves_instancecount_", 0, "1");
    db.row("customisation.custom_avatar.gloves.Gloves_0",
           {{"_metadata_", "2025-sauber"}, {"asset_group", cp + "team_livery/2025-sauber/2025-sauber.assetgroup"},
            {"logos_model", "eaid://z"}, {"material_override", "2025-sauber"}, {"id", "500"}, {"colour_1_default", ""},
            {"colour_2_default", ""}, {"colour_3_default", ""}, {"colour_4_default", ""}});
    db.put("customisation.custom_avatar.helmets._Helmet_instancecount_", 0, "1");
    db.row("customisation.custom_avatar.helmets.Helmet_0",
           {{"_metadata_", "2025-sauber_driver"},
            {"asset_group", cp + "drivers/male/2025-sauber_driver/driver_helmet.assetgroup"},
            {"attachments_model", "eaid://a"}, {"model", "eaid://shell"}, {"material_override", "2025-sauber_driver"},
            {"id", "600"}, {"name_loc_tag", "lng_placeholder_helmet"}, {"is_official_team_item", "true"}, {"colour_1_default", "-1"}, {"colour_2_default", "-2"}, {"colour_3_default", "-3"},
            {"colour_4_default", ""}});
    db.put("entrants.animation.driver_heads._driver_head_instancecount_", 0, "2");
    db.row("entrants.animation.driver_heads.driver_head_0",
           {{"_metadata_", "jarno_opmeer"}, {"body_type", "generic_male"}, {"available_for_customisation", "True"}, {"stat_id", "100"}});
    db.row("entrants.animation.driver_heads.driver_head_1",
           {{"_metadata_", "female_one"}, {"body_type", "generic_female"}, {"available_for_customisation", "True"}, {"stat_id", "101"}});
    db.put("entrants.driver_customisations._folder_instancecount_", 0, "2");
    db.row("entrants.driver_customisations.folder_0", {{"_metadata_", "ai_driver_customisations"}});
    db.put("entrants.driver_customisations.folder_0._preloaded_driver_customisation_properties_instancecount_", 0, "0");
    db.row("entrants.driver_customisations.folder_1", {{"_metadata_", "official_driver_customisations"}});
    db.put("entrants.driver_customisations.folder_1._preloaded_driver_customisation_properties_instancecount_", 0, "1");
    db.row("entrants.driver_customisations.folder_1.preloaded_driver_customisation_properties_0",
           {{"_metadata_", "2025-sauber_driver"}, {"available", "False"}, {"badge", ""}, {"car_livery", ""},
            {"gloves", "2025-sauber"}, {"helmet", "2025-sauber_driver"}, {"linked_driver_stat_id", "jarno_opmeer"},
            {"podium_emote", ""}, {"pose", ""}, {"stat_id", "365"}, {"suit", "2025-sauber"}, {"victory_vo", ""}});
    db.put("entrants.drivers._driver_instancecount_", 0, "2");
    db.row("entrants.drivers.driver_0",
           {{"_metadata_", "jarno_opmeer"}, {"first_name", "lng_a"}, {"last_name", "lng_b"}, {"full_name", "lng_c"},
            {"initials", "OPM"}, {"nationality", "netherlands"}, {"car_number", "9"}, {"head", "jarno_opmeer"},
            {"audio_name", "DefaultName"}, {"available", "True"}, {"f1_seasons_completed", "0"},
            {"garage_slot_preference", "0"}, {"stat_id", "250"}, {"expression_label", "jarno_opmeer"},
            {"date_of_birth.year", "2000"}});
    db.row("entrants.drivers.driver_1", {{"_metadata_", "other"}, {"stat_id", "251"}});
    db.put("driver_statistics.driver_statistics._DriverStatisticsHolder_instancecount_", 0, "1");
    db.row("driver_statistics.driver_statistics.DriverStatisticsHolder_0", {{"_metadata_", "jarno_opmeer"}, {"driver", "jarno_opmeer"}});
    std::string ds = "driver_statistics.driver_statistics.DriverStatisticsHolder_0";
    db.put(ds + "._StatisticsDataSet_instancecount_", 0, "1");
    db.row(ds + ".StatisticsDataSet_0", {{"_metadata_", "default"}});
    db.put(ds + ".StatisticsDataSet_0._Statistic_instancecount_", 0, "2");
    db.row(ds + ".StatisticsDataSet_0.Statistic_0", {{"_metadata_", "pace"}, {"info", "pace"}});
    db.put(ds + ".StatisticsDataSet_0.Statistic_0.evolution", 9, "@v2@minx:0,maxx:10,miny:0,maxy:100,0:0.9,0:0.9,1:0.89");
    db.row(ds + ".StatisticsDataSet_0.Statistic_1", {{"_metadata_", "focus"}, {"info", "focus"}});
    db.put(ds + ".StatisticsDataSet_0.Statistic_1.evolution", 9, "@v2@minx:0,maxx:10,miny:0,maxy:100,0:0.5,0:0.5,1:0.5");
    db.put("engine_management.driver_wear_rates._driver_wear_rate_instancecount_", 0, "1");
    db.row("engine_management.driver_wear_rates.driver_wear_rate_0",
           {{"_metadata_", "jarno_opmeer"}, {"driver", "jarno_opmeer"}, {"component_percentage", "0.9"}, {"gearbox_percentage", "0.9"}});
    db.put("entrants.team_customisation._team_customisation_for_year_instancecount_", 0, "1");
    db.row("entrants.team_customisation.team_customisation_for_year_0", {{"_metadata_", "2026"}, {"f1_season", "season_2026"}});
    db.put("entrants.team_customisation.team_customisation_for_year_0._team_customisation_instancecount_", 0, "1");
    db.row("entrants.team_customisation.team_customisation_for_year_0.team_customisation_0",
           {{"_metadata_", "2026-sauber"}, {"gloves", "2025-sauber"}, {"isDefaultForYear", "False"},
            {"is_default_for_team", "False"}, {"linked_team", "sauber_26"}, {"official_driver_customisations", "2025-sauber_driver"},
            {"stat_id", "130"}, {"suit", "2025-sauber"}});
    db.put("entrants.entrant_lists.entrants._entrant_instancecount_", 0, "1");
    db.row("entrants.entrant_lists.entrants.entrant_0",
           {{"_metadata_", "max_verstappen"}, {"driver", "max_verstappen"}, {"driver_customisation", "x"}, {"car_number", "1"}});
    db.put("entrants.standings._standings_instancecount_", 0, "1");
    db.row("entrants.standings.standings_0", {{"_metadata_", "f1_teams_2024"}});
    db.put("entrants.standings.standings_0._team_reference_instancecount_", 0, "1");
    db.row("entrants.standings.standings_0.team_reference_0", {{"_metadata_", "t0"}, {"team", "stock"}});
    db.put("entrants.entrant_lists._entrant_list_instancecount_", 0, "1");
    db.row("entrants.entrant_lists.entrant_list_0", {{"_metadata_", "f1_2025"}, {"display_name", "x"},
                                                      {"previous_season_team_standings", "x"},
                                                      {"previous_season_driver_standings", "x"}, {"leaderboard", "f1"}});
    db.put("entrants.entrant_lists.entrant_list_0.stat_id", 0, "104");
    db.put("entrants.entrant_lists.entrant_list_0._entrant_instancecount_", 0, "1");
    db.row("entrants.entrant_lists.entrant_list_0.entrant_0",
           {{"_metadata_", "e"}, {"team", "t"}, {"vehicle", "v"}, {"entrant_reference", "r"}, {"team_position", "0"},
            {"car_number", "1"}, {"driver", "d"}, {"driver_customisation", "c"}});
    db.put("game_setup.scenarios._scenario_instancecount_", 0, "1");
    db.row("game_setup.scenarios.scenario_0", {{"_metadata_", "f1"}, {"display_name", "x"}, {"entrant_list", "f1_2025"},
                                                {"game_setup_mode", "f1"}, {"race_class", "f1"}, {"use_custom_vehicle_spec", "False"}});
    db.put("game_setup.scenarios.scenario_0.statId", 0, "14");
    std::string opt = "options_system.options.multiplayer.options_folder_2";
    db.put(opt + "._option_instancecount_", 0, "1");
    db.row(opt + ".option_0", {{"_metadata_", "category"}});
    db.put(opt + ".option_0._value_instancecount_", 0, "1");
    db.row(opt + ".option_0.value_0", {{"_metadata_", "f1_2025"}, {"confirmation_dialogue", ""}, {"description_override", ""},
                                        {"game_value", "f1_2025"}, {"is_default", "False"}, {"loc_tag", "x"},
                                        {"loc_tag_alternate", ""}, {"vr_is_default", "False"}});
    db.put("entrants.entrant_setup_modes._entrant_setup_mode_instancecount_", 0, "1");
    db.row("entrants.entrant_setup_modes.entrant_setup_mode_0", {{"_metadata_", "swofty_f1"}});
    db.put("entrants.entrant_setup_modes.entrant_setup_mode_0.entrant_lists", 13, "f1_2025");
    db.put("entrants.entrant_setup_modes.entrant_setup_mode_0.selectable_entrant_lists", 13, "f1_2025");
    db.put("content_sets._content_set_instancecount_", 0, "1");
    db.row("content_sets.content_set_0", {{"_metadata_", "2025"}});
    db.put("content_sets.content_set_0.available_entrant_lists_0.entrant_lists", 13, "f1_2025");
    db.put("entrants.vehicles._vehicle_instancecount_", 0, "0");
}

static std::optional<std::string> written(const rows::Engine &e, const std::string &path)
{
    for (auto &w : e.writes())
        if (w.path == path)
            return w.text;
    return std::nullopt;
}

static std::string find_row(const rows::Engine &e, const std::string &table, const std::string &row, const std::string &label)
{
    for (int i = 0; i < 12; i++)
        if (written(e, table + "." + row + "_" + std::to_string(i) + "._metadata_") == label)
            return table + "." + row + "_" + std::to_string(i);
    return {};
}

static void register_records(const std::string &regs, const char *records)
{
    registry::reset_store();
    auto &st = registry::store();
    st.set_order({"mod.f2012"});
    st.set_validator(&registry::schema_validator());
    const SwoftyRegistry *t = registry::table();
    for (const char *name : {"helmet", "suit", "gloves", "driver.head", "driver.look", "driver.person", "driver.identity",
                             "driver.stats", "wearable.season_set", "content.package", "vehicle", "vehicle.package",
                             "engine_sound", "livery"}) {
        std::string schema = slurp(regs + "/" + name + "/1.0.json");
        t->declare(name, SwoftyText{schema.data(), schema.size()});
    }
    int r = registry::load_data_file("mod.f2012", "registry/wear.json", records);
    st.end_register();
    for (auto &x : st.refusals())
        std::printf("      refused: %s %s %s\n", x.registry.c_str(), x.id.c_str(), x.message.c_str());
    check(r == SWOFTY_ASSET_OK, "the wearables and drivers records register");
}

static const char *RECORDS = R"j({
  "helmet": {
    "salo_helmet": { "archive": "helmets/salo.erp", "from": "game/2025-sauber_driver", "name": "lng_salo_helmet",
                     "colours": ["#FF0000", "#00FF00", "#800000FF", "#000000"], "official_team_item": true },
    "plain_helmet": { "archive": null, "from": "game/2025-sauber_driver", "colours": ["#112233", "#445566", "#778899", "#AABBCC"] }
  },
  "suit": {
    "sauber_suit": { "archive": "kit/sauber.erp", "from": "game/2025-sauber", "team": "mod.f2012/sauber" },
    "salo_suit": { "archive": "kit/salo_suit.erp", "from": "game/2025-sauber" }
  },
  "gloves": {
    "sauber_suit": { "archive": "kit/sauber.erp", "from": "game/2025-sauber", "team": "mod.f2012/sauber" },
    "salo_gloves": { "archive": null, "from": "game/2025-sauber" }
  },
  "driver.head": {
    "salo": { "archive": "heads/salo.erp", "morph": "heads/morph/", "rig_from": "game/jarno_opmeer", "body": "male", "customisable": true },
    "lady": { "archive": "heads/lady.erp", "body": "female" }
  },
  "driver.look": {
    "salo_look": { "helmet": "mod.f2012/salo_helmet", "suit": "mod.f2012/salo_suit", "gloves": "mod.f2012/salo_gloves",
                   "pose": "Pose03", "podium_emote": "Emote01" }
  },
  "driver.person": { "mika_salo": { "name": "Mika Salo", "nationality": "finland", "driver": null } },
  "driver.identity": {
    "mika_salo": { "donor": "game/jarno_opmeer", "person": "person/mika_salo",
                   "names": { "first": "lng_mika", "last": { "literal": "Salo" }, "full": "lng_mika_salo", "code": "SAL" },
                   "nationality": "finland", "number": 17, "head": "mod.f2012/salo", "look": "mod.f2012/salo_look",
                   "audio_name": "salo", "seasons_completed": 7, "garage_slot": 1, "stat_id": "auto",
                   "pools": ["f1_2026_driver_pool"], "date_of_birth.year": 1966 },
    "nolook": { "donor": "game/jarno_opmeer", "names": { "first": "a", "last": "b", "full": "c", "code": "NOL" }, "look": null },
    "adopted": { "names": { "first": "a", "last": "b", "full": "c", "code": "ADO" }, "look": null },
    "salo_twin": { "extends": "mod.f2012/mika_salo", "number": 18 }
  },
  "content.package": { "f2012": {
      "title": "lng_f2012", "entrant_list": "f2012", "season": 2012, "stat_id": 106,
      "teams": [ { "key": "sauber", "vehicle": null,
                   "drivers": [ { "driver": "person/mika_salo", "number": 17, "look": null } ] } ],
      "database": { "entrant_list": "f2012", "scenario_stat_id": 16, "entrant_prefix": "f12-", "standings": "f2012_teams",
                    "content_sets": ["2025"] } } },
  "driver.stats": { "mika_salo": { "driver": "mod.f2012/mika_salo", "pace": { "start": 78, "peak": 84 }, "racecraft": { "start": 75, "peak": 82 } } },
  "wearable.season_set": {
    "sauber_2012": { "team": "mod.f2012/sauber", "season": 2012, "suit": "mod.f2012/sauber_suit", "gloves": "mod.f2012/sauber_suit",
                     "drivers": ["mod.f2012/mika_salo"] },
    "empty_2012": { "team": "mod.f2012/sauber", "season": 2012 }
  }
})j";

static void apply_tests(const std::string &regs)
{
    std::string override_records = std::getenv("W23_RECORDS") ? slurp(std::getenv("W23_RECORDS")) : std::string();
    register_records(regs, override_records.empty() ? RECORDS : override_records.c_str());
    auto &st = registry::store();
    Db db;
    stock_rows(db);
    apply::ids::Store ids;
    auto text = [&](const std::string &path) -> std::optional<std::string> {
        auto r = db.base()(content::gvdb::fnv64(path));
        return r ? std::optional<std::string>(r->text) : std::nullopt;
    };
    for (auto &scan : content::scan_all_pools(text)) {
        apply::ids::Pool pool;
        if (!scan.holes.empty())
            pool.ranges = scan.holes;
        else if (scan.lo <= scan.hi)
            pool.ranges.push_back({scan.lo, scan.hi});
        ids.define_pool(scan.registry, pool);
    }
    content::StatIds stat = [&](const std::string &reg, const std::string &id, int64_t pin) -> int64_t {
        return pin >= 0 ? ids.pin(reg, id, pin, "test").id : ids.allocate(reg, id, "test").id;
    };
    content::Expansion x(db.base());
    content::expand(st, x, stat);
    for (const char *reg : {"helmet", "suit", "gloves", "driver.head", "driver.look", "driver.person", "driver.identity",
                            "driver.stats", "wearable.season_set", "content.package"})
        for (auto &l : x.lines[reg])
            std::printf("      %s\n", l.c_str());
    check(x.records["helmet"] == 2 && x.records["suit"] == 2 && x.records["gloves"] == 2, "every item record applies");
    check(x.records["driver.head"] == 2 && x.records["driver.look"] == 1 && x.records["driver.person"] == 1,
          "heads, the look and the person apply");
    check(x.refused["driver.identity"] == 1 && x.records["driver.identity"] == 3,
          "the identity without a look is refused, the others apply, and one without a donor names an existing row without a refusal");
    check(x.records["driver.stats"] == 1 && x.records["wearable.season_set"] == 2, "the stats and both season sets apply");

    auto &e = x.engine;
    const std::string S = "customisation.custom_avatar.suits", G = "customisation.custom_avatar.gloves",
                      H = "customisation.custom_avatar.helmets";
    std::string suit = find_row(e, S, "Suit", "sauber_suit"), gloves = find_row(e, G, "Gloves", "sauber_suit");
    check(!suit.empty() && !gloves.empty(), "a team's suit and gloves are rows of their tables");
    check(written(e, suit + ".asset_group") == "asset_groups/[CHARACTER_PACKAGE]/team_livery/sauber_suit/sauber_suit.assetgroup" &&
              written(e, gloves + ".asset_group") == written(e, suit + ".asset_group"),
          "the suit and gloves of one team archive share the team livery asset group");
    check(written(e, suit + ".feet_model") == "eaid://[CHARACTER_PACKAGE]/team_livery/sauber_suit/idf/sauber_suit_male" &&
              written(e, suit + ".logos_model_override_pattern") ==
                  "eaid://[CHARACTER_PACKAGE]/team_livery/sauber_suit/idf/sauber_suit_body_logos_[GENDER]" &&
              written(e, gloves + ".logos_model") == "eaid://[CHARACTER_PACKAGE]/team_livery/sauber_suit/idf/sauber_suit_glove_logos_[GENDER]" &&
              written(e, suit + ".material_override") == "sauber_suit",
          "the models and material come from the item's key by the stock name pattern");
    check(written(e, suit + ".colour_1_default") == "" && written(e, suit + ".colour_4_default") == "" &&
              written(e, suit + ".colour_3_default") == "" && written(e, suit + ".name_loc_tag") == "lng_placeholder_suits",
          "colours left empty keep the from row's four keys");
    std::string ss = find_row(e, S, "Suit", "salo_suit");
    check(written(e, ss + ".feet_model") == "eaid://[CHARACTER_PACKAGE]/drivers/male/salo_suit/idf/salo_suit" &&
              written(e, ss + ".asset_group") ==
                  "asset_groups/[CHARACTER_PACKAGE]/drivers/male/salo_suit/salo_suit.assetgroup",
          "a driver's suit uses the driver group and model names");
    check(written(e, suit + ".id") && written(e, ss + ".id") && written(e, suit + ".id") != written(e, ss + ".id") &&
              std::stoi(*written(e, suit + ".id")) >= 511,
          "item ids come from the pool above the table's ids and are unique");
    std::string helmet = find_row(e, H, "Helmet", "salo_helmet"), plain = find_row(e, H, "Helmet", "plain_helmet");
    check(written(e, helmet + ".colour_1_default") == "-65536" && written(e, helmet + ".colour_2_default") == "-16711936" &&
              written(e, helmet + ".colour_3_default") == "-2147483393" && written(e, helmet + ".colour_4_default") == "-16777216",
          "colours are signed ARGB, #RRGGBB opaque");
    check(written(e, helmet + ".asset_group") ==
                  "asset_groups/[CHARACTER_PACKAGE]/drivers/male/salo_helmet/salo_helmet_helmet.assetgroup" &&
              written(e, helmet + ".attachments_model") ==
                  "eaid://[CHARACTER_PACKAGE]/drivers/male/salo_helmet/idf/salo_helmet_helmet_attachments" &&
              written(e, helmet + ".model") == "eaid://shell" && written(e, helmet + ".is_official_team_item") == "True" &&
              written(e, helmet + ".name_loc_tag") == "lng_salo_helmet",
          "a helmet keeps the generic shell and gets its group, attachments, name and flag");
    check(written(e, plain + ".asset_group") == "asset_groups/[CHARACTER_PACKAGE]/drivers/male/2025-sauber_driver/driver_helmet.assetgroup" &&
              written(e, plain + ".colour_4_default") == "-5588020",
          "a helmet with no archive keeps the stock group and takes its colours");
    int groups = 0;
    bool team_group = false, shares = true;
    for (auto &g : x.groups) {
        groups++;
        if (g.group == "asset_groups/character_package/team_livery/sauber_suit/sauber_suit.assetgroup")
            team_group = g.tmpl == "asset_groups/character_package/team_livery/2025-sauber/2025-sauber.assetgroup" &&
                         g.common.size() == 1 && g.common[0] == "2025_asset_groups/character_package/team_livery/2025-sauber/2025-sauber.erp" &&
                         g.erp == "2025_asset_groups/character_package/team_livery/sauber_suit/sauber_suit.erp" &&
                         g.file == "kit/sauber.erp" && g.registry == "suit";
        if (g.group.find("team_livery/sauber_suit") != std::string::npos && g.registry != "suit")
            shares = false;
    }
    check(team_group && shares, "the team group copies the stock group, lists its archive first and is registered once");
    check(groups == 6, "groups: team kit, driver suit, helmet, the head archives and the morph (" + std::to_string(groups) + ")");

    std::string heads = "entrants.animation.driver_heads";
    std::string salo = find_row(e, heads, "driver_head", "salo"), lady = find_row(e, heads, "driver_head", "lady");
    check(!salo.empty() && written(e, salo + ".body_type") == "generic_male" && written(e, salo + ".available_for_customisation") == "True",
          "a head copies its rig row");
    check(!lady.empty() && written(e, lady + ".body_type") == "generic_female" && written(e, lady + ".esports_name") == std::nullopt,
          "a female head copies a female stock head by default");
    check(written(e, salo + ".stat_id") && std::stoi(*written(e, salo + ".stat_id")) >= 102, "head stat ids come from the pool");
    bool morph = false;
    for (auto &g : x.groups)
        if (g.registry == "driver.head" && g.erp.find("morph_poses/salo.erp") != std::string::npos && g.file == "heads/morph/salo.erp")
            morph = true;
    check(morph, "a morph folder resolves to <folder><key>.erp");

    std::string looks = "entrants.driver_customisations.folder_1";
    std::string look = find_row(e, looks, "preloaded_driver_customisation_properties", "salo_look");
    check(!look.empty() && written(e, look + ".helmet") == "salo_helmet" && written(e, look + ".suit") == "salo_suit" &&
              written(e, look + ".gloves") == "salo_gloves" && written(e, look + ".pose") == "Pose03" &&
              written(e, look + ".linked_driver_stat_id") == "mika_salo" && written(e, look + ".available") == "False",
          "a look names its items and is linked to the driver that names it");
    check(written(e, look + ".stat_id") == "366" && written(e, looks + "._preloaded_driver_customisation_properties_instancecount_") == "2",
          "its stat id is the first above the stock looks");

    std::string drv = find_row(e, "entrants.drivers", "driver", "mika_salo");
    check(!drv.empty() && written(e, drv + ".full_name") == "lng_mika_salo" &&
              written(e, drv + ".first_name") == "lng_mika" &&
              written(e, drv + ".last_name") == "lng_swofty_driver_mika_salo_last" &&
              written(e, drv + ".initials") == "SAL" && written(e, drv + ".nationality") == "finland" &&
              written(e, drv + ".car_number") == "17" && written(e, drv + ".head") == "salo" &&
              written(e, drv + ".audio_name") == "salo" && written(e, drv + ".f1_seasons_completed") == "7" &&
              written(e, drv + ".garage_slot_preference") == "1" && written(e, drv + ".date_of_birth.year") == "1966" &&
              written(e, drv + ".expression_label") == "jarno_opmeer",
          "a driver row has his names, number, nationality, head and extra keys, and keeps the donor's other keys");
    check(written(e, drv + ".stat_id") == "208", "his stat id is the lowest free one of Driver Manager's 208..253 range");
    auto texts = x.texts["driver.identity"];
    auto has_text = [&](const std::string &tag, const std::string &value) {
        return std::find(texts.begin(), texts.end(), std::pair<std::string, std::string>{tag, value}) != texts.end();
    };
    check(has_text("lng_swofty_driver_mika_salo_last", "Salo") && texts.size() == 2,
          "his literal last name is a core text; his lang-key names are not");
    check(written(e, drv + ".last_name_standard") == std::nullopt,
          "a donor with no standard last name gets none");
    check(find_row(e, "entrants.drivers", "driver", "nolook").empty(), "an identity without a look writes no row");
    std::string twin = find_row(e, "entrants.drivers", "driver", "salo_twin");
    check(!twin.empty() && written(e, twin + ".full_name") == "lng_mika_salo" && written(e, twin + ".car_number") == "18" &&
              written(e, twin + ".last_name") == "lng_swofty_driver_salo_twin_last" &&
              has_text("lng_swofty_driver_salo_twin_last", "Salo") &&
              written(e, twin + ".stat_id") == "209",
          "a variant copies its base's row with its own number and stat id");
    std::string wear = find_row(e, "engine_management.driver_wear_rates", "driver_wear_rate", "mika_salo");
    check(!wear.empty() && written(e, wear + ".driver") == "mika_salo" && written(e, wear + ".gearbox_percentage") == "0.9",
          "his wear rates are the donor's under his label");
    std::string holder = find_row(e, "driver_statistics.driver_statistics", "DriverStatisticsHolder", "mika_salo");
    check(!holder.empty() && written(e, holder + ".driver") == "mika_salo" &&
              written(e, holder + ".StatisticsDataSet_0.Statistic_0.evolution") ==
                  "@v2@minx:0,maxx:10,miny:0,maxy:100,0:0.78,0:0.78,0.9:0.84,1:0.84" &&
              written(e, holder + ".StatisticsDataSet_0.Statistic_1.evolution") == "@v2@minx:0,maxx:10,miny:0,maxy:100,0:0.5,0:0.5,1:0.5",
          "driver.stats sets the curves it names on his holder and keeps the donor's others");
    std::string twin_holder = find_row(e, "driver_statistics.driver_statistics", "DriverStatisticsHolder", "salo_twin");
    check(!twin_holder.empty() && written(e, twin_holder + ".StatisticsDataSet_0.Statistic_0.evolution") == "@v2@minx:0,maxx:10,miny:0,maxy:100,0:0.9,0:0.9,1:0.89",
          "a driver with no stats record gets a plain copy of the donor's holder");

    std::string year = "entrants.team_customisation.team_customisation_for_year_0";
    check(written(e, year + "._team_customisation_instancecount_") == "2", "the nearest year row takes the kit (no season_2012 row)");
    std::string kit = year + ".team_customisation_1";
    check(written(e, kit + "._metadata_") == "2012-sauber" && written(e, kit + ".linked_team") == "sauber" &&
              written(e, kit + ".suit") == "sauber_suit" && written(e, kit + ".gloves") == "sauber_suit" &&
              written(e, kit + ".official_driver_customisations") == "salo_look" &&
              written(e, kit + ".is_default_for_team") == "False" && written(e, kit + ".stat_id") == "131",
          "a kit row names the team's suit and gloves and the declared drivers' looks");
    check(written(e, year + ".team_customisation_2._metadata_") == std::nullopt,
          "a season set with no suit or gloves writes no kit row");

    std::map<std::string, Value> keep;
    auto lookup = content::own_lookup(st, keep);
    content::WearCtx ctx = content::wear_context(st, lookup);
    check(content::package_driver(lookup, "person/mika_salo", &ctx.person_driver) == "salo_twin" &&
              content::package_look(lookup, "person/mika_salo", &ctx.person_driver) == "salo_look" &&
              content::package_look(lookup, "game/unknown", &ctx.person_driver).empty(),
          "a person/ id seats the driver that is that person, and a seat with no look takes the driver's");
    std::string entrant = find_row(e, "entrants.entrant_lists.entrants", "entrant", "f12-salo_twin");
    check(!entrant.empty() && written(e, entrant + ".driver") == "salo_twin" &&
              written(e, entrant + ".driver_customisation") == "salo_look",
          "a package seat naming person/ seats his driver, wearing the driver's look when the seat names none");
    auto pins = content::pins(st);
    check(pins.size() == 2 && pins[0].registry == "content.package", "auto stat ids pin nothing; the package pins its list and scenario");
}

static const char *NAME_RECORDS = R"j({
  "helmet": { "ida_helmet": { "archive": "kit/ida_helmet.erp", "from": "game/2025-sauber_driver" } },
  "suit": { "ida_suit": { "archive": "kit/ida_suit.erp", "from": "game/2025-sauber" } },
  "gloves": { "ida_gloves": { "archive": "kit/ida_gloves.erp", "from": "game/2025-sauber" } },
  "driver.look": { "ida_look": { "helmet": "mod.f2012/ida_helmet", "suit": "mod.f2012/ida_suit", "gloves": "mod.f2012/ida_gloves" } },
  "driver.identity": {
    "ida_gamer": { "donor": "game/jarno_opmeer",
                   "names": { "first": { "literal": "Ida" }, "last": { "literal": "Gamer" }, "full": { "literal": "Ida Gamer" }, "code": "IGA" },
                   "nationality": "great_britain", "number": 77, "look": "mod.f2012/ida_look", "seasons_completed": 0 },
    "keyed": { "donor": "game/lance_stroll",
               "names": { "first": "lng_k_first", "last": "lng_k_last", "full": "lng_k_full", "code": "KEY" },
               "look": "mod.f2012/ida_look" },
    "standard": { "donor": "game/lance_stroll",
                  "names": { "first": { "literal": "Sam" }, "last": { "literal": "Öhl" }, "full": { "literal": "Sam Öhl" }, "code": "OHL" },
                  "look": "mod.f2012/ida_look" }
  }
})j";

static void literal_name_tests(const std::string &regs)
{
    register_records(regs, NAME_RECORDS);
    auto &st = registry::store();
    Db db;
    stock_rows(db);
    db.put("entrants.drivers._driver_instancecount_", 0, "3");
    db.row("entrants.drivers.driver_2",
           {{"_metadata_", "lance_stroll"}, {"first_name", "lng_lance"}, {"last_name", "lng_stroll"},
            {"last_name_standard", "lng_stroll_standard"}, {"full_name", "lng_lance_stroll"}, {"initials", "STR"},
            {"nationality", "canada"}, {"car_number", "18"}, {"head", "jarno_opmeer"}, {"audio_name", "DefaultName"},
            {"available", "True"}, {"f1_seasons_completed", "0"}, {"garage_slot_preference", "0"}, {"stat_id", "40"},
            {"expression_label", "lance_stroll"}, {"date_of_birth.year", "1998"}});
    int64_t next = 230;
    content::StatIds stat = [&](const std::string &, const std::string &, int64_t pin) -> int64_t { return pin >= 0 ? pin : next++; };
    content::Expansion x(db.base());
    content::expand(st, x, stat);
    for (auto &l : x.lines["driver.identity"])
        std::printf("      %s\n", l.c_str());
    check(x.records["driver.identity"] == 3 && x.refused["driver.identity"] == 0, "the three name records apply");
    auto &e = x.engine;
    auto texts = x.texts["driver.identity"];
    auto has_text = [&](const std::string &tag, const std::string &value) {
        return std::find(texts.begin(), texts.end(), std::pair<std::string, std::string>{tag, value}) != texts.end();
    };
    std::string ida = find_row(e, "entrants.drivers", "driver", "ida_gamer");
    check(!ida.empty() && written(e, ida + ".first_name") == "lng_swofty_driver_ida_gamer_first" &&
              written(e, ida + ".last_name") == "lng_swofty_driver_ida_gamer_last" &&
              written(e, ida + ".full_name") == "lng_swofty_driver_ida_gamer_full" && written(e, ida + ".initials") == "IGA" &&
              written(e, ida + ".car_number") == "77" && written(e, ida + ".nationality") == "great_britain",
          "a literal-name driver's row holds core tags for his names, his code as initials and his number");
    check(has_text("lng_swofty_driver_ida_gamer_first", "Ida") && has_text("lng_swofty_driver_ida_gamer_last", "Gamer") &&
              has_text("lng_swofty_driver_ida_gamer_full", "Ida Gamer"),
          "the core registers the text of each literal name under its tag");
    check(written(e, ida + ".last_name_standard") == std::nullopt, "a donor with no standard last name leaves the field alone");
    std::string keyed = find_row(e, "entrants.drivers", "driver", "keyed");
    check(!keyed.empty() && written(e, keyed + ".first_name") == "lng_k_first" && written(e, keyed + ".last_name") == "lng_k_last" &&
              written(e, keyed + ".full_name") == "lng_k_full" && written(e, keyed + ".initials") == "KEY" &&
              written(e, keyed + ".last_name_standard") == "lng_stroll_standard",
          "lang-key names are written as they are and keep the donor's standard last name");
    std::string standard = find_row(e, "entrants.drivers", "driver", "standard");
    check(!standard.empty() && written(e, standard + ".last_name") == "lng_swofty_driver_standard_last" &&
              written(e, standard + ".last_name_standard") == "lng_swofty_driver_standard_last" &&
              has_text("lng_swofty_driver_standard_last", "Öhl"),
          "a literal last name also replaces the donor's standard last name");
    check(texts.size() == 6, "only the literal names become texts");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::printf("usage: wear_test <registries dir>\n");
        return 2;
    }
    apply_tests(argv[1]);
    literal_name_tests(argv[1]);
    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
