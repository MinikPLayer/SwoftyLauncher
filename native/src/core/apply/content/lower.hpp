#pragma once

#include <swofty/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

// Turns the effective records of the content registries (content.package, era.box, vehicle,
// vehicle.package, engine_sound) into the database changes the game needs, in the operation format of
// a database operation file (add-row, set, list-append, put-record): the same rows Classic Seasons'
// build operations make. Pure: the stock records a copy reads come from a Source, so the host test
// runs it against the stock database file and the core against the database the game loads. Stat ids
// come from a StatIds callback: a record's explicit stat_id is pinned, "auto" is allocated from the
// table's pool (scan_pools). Without a callback an explicit stat_id is written as it is, and a copy without
// one keeps the build operations' rule (the table's highest id plus one).

namespace swofty_core::apply::content {

using Value = swofty::json::Value;

struct Stock {
    int tag = -1;
    std::string text;
};

using Source = std::function<std::optional<Stock>(const std::string &key)>;
using Lookup = std::function<const Value *(const std::string &registry, const std::string &id)>;
using StatIds = std::function<int64_t(const std::string &registry, const std::string &id, int64_t pin)>;

inline constexpr const char *SCENARIO_IDS = "content.package.scenario";

struct Redirect {
    std::string path, file, addon, id;
};

struct Group {
    std::string group, tmpl, erp, file, addon, id;
    std::vector<std::string> common;
    std::string registry = "livery";
};

struct Folder {
    std::string game_dir, file, addon, id;
};

struct Owned {
    std::string label, id;
    bool owned = true;
};

struct Box {
    std::string id, entrant_list, title, image, literal;
    int order = 0;
};

struct LogoImage {
    std::string array, key, file, stock, context, addon, id, team;
    bool fallback = false;
};

struct Plan {
    std::string owner;
    std::vector<std::string> owners;
    Value ops = Value::array();
    Value fields = Value::object();
    std::vector<Redirect> redirects;
    std::vector<Box> boxes;
    std::vector<Group> groups;
    std::vector<Folder> folders;
    std::vector<Owned> owned;
    std::vector<LogoImage> logos;
    std::vector<std::pair<std::string, std::string>> texts;
    std::vector<std::string> lines;
    std::size_t keys = 0;
    std::size_t refused = 0;
};

inline constexpr const char *AUDIO_BUNDLES = "audio.audio_bundle_settings.audio_bundle_settings";
inline constexpr const char *AUDIO_CARS = "audio.audio_car_data";
inline constexpr const char *VEHICLES = "entrants.vehicles.vehicle";
inline constexpr const char *PACKAGES = "entrants.vehicles.vehicle_packages.vehicle_package";
inline constexpr const char *SETUP_PRESETS = "car_setup.presets.vehicle_package_setups";
inline constexpr const char *ENTRANTS = "entrants.entrant_lists.entrants.entrant";
inline constexpr const char *STANDINGS = "entrants.standings.standings";
inline constexpr const char *LISTS = "entrants.entrant_lists.entrant_list";
inline constexpr const char *SCENARIOS = "game_setup.scenarios.scenario";
inline constexpr const char *SETUP_ROW = "entrants.entrant_setup_modes.entrant_setup_mode";
inline constexpr const char *CATEGORY =
    "options_system.options.multiplayer.options_folder_2.option[_metadata_=category]";

inline constexpr const char *AUDIO_CAR_FIELDS[] = {
    "_metadata_", "bundle_name", "detonations_category", "safety_car", "cabin_radio", "left_right_panning",
    "frontwing_offset_x", "frontwing_offset_y", "frontwing_offset_z", "rearwing_offset_x", "rearwing_offset_y",
    "rearwing_offset_z", "windscreen_offset_x", "windscreen_offset_y", "windscreen_offset_z", "engine_offset_x",
    "engine_offset_y", "engine_offset_z", "engine_rotation_y", "exhaust_offset_x", "exhaust_offset_y",
    "exhaust_offset_z", "exhaust_rotation_y", "cabin_offset_x", "cabin_offset_y", "cabin_offset_z",
    "cabin_rotation_y", "gearbox_offset_x", "gearbox_offset_y", "gearbox_offset_z", "gearbox_rotation_y",
    "braking_slowdown", "engine_braking_scale", "exhaust_braking_scale", "engine_downshift_load_lag",
    "exhaust_downshift_load_lag", "engine_torque_throttle_mixer", "exhaust_torque_throttle_mixer",
    "judder_wobble_scale", "surface_load_wobble_scale", "off_load_decell_attenuation_idle",
    "off_load_decell_attenuation_redline", "throttle_blend_power_base"};

inline constexpr const char *AUDIO_BUNDLE_FIELDS[] = {"_metadata_", "bundle_name", "era", "engine", "exhaust", "cabin"};
inline constexpr const char *SCENARIO_FIELDS[] = {"_metadata_", "display_name", "entrant_list", "game_setup_mode",
                                                  "race_class", "statId", "use_custom_vehicle_spec"};
inline constexpr const char *CATEGORY_FIELDS[] = {"_metadata_", "confirmation_dialogue", "description_override",
                                                  "game_value", "is_default", "loc_tag", "loc_tag_alternate",
                                                  "vr_is_default"};

// Records a copied row adds, by the table it copies in (the fields of the stock rows the copies come from,
// from tools/gamedb/catalog/keys.tsv.gz and the operation files' field lists).
inline std::size_t row_keys(const std::string &table)
{
    static const std::map<std::string, std::size_t> sizes = {
        {"audio.audio_bundle_settings", 6},        {"entrants.vehicles", 40},
        {"entrants.vehicles.vehicle_packages", 30}, {"car_setup.presets", 421},
        {"entrants.entrant_lists.entrants", 12},    {"entrants.standings", 23},
        {"entrants.entrant_lists", 190},            {"game_setup.scenarios", 7},
        {"options_system.options.multiplayer.options_folder_2.option[_metadata_=category]", 8}};
    auto it = sizes.find(table);
    return it == sizes.end() ? 16 : it->second;
}

inline std::string key_of(std::string_view id)
{
    auto slash = id.rfind('/');
    return std::string(slash == std::string_view::npos ? id : id.substr(slash + 1));
}

inline std::string addon_of(std::string_view id)
{
    auto slash = id.rfind('/');
    return slash == std::string_view::npos ? std::string() : std::string(id.substr(0, slash));
}

inline std::string title_text(const Value &t)
{
    if (t.is_string())
        return std::string(t.as_string());
    if (t.is_object() && t["literal"].is_string())
        return std::string(t["literal"].as_string());
    return {};
}

inline int64_t pin_of(const Value &rec)
{
    return rec["stat_id"].is_int() && rec["stat_id"].as_int() >= 0 ? rec["stat_id"].as_int() : -1;
}

inline std::optional<int64_t> stat_of(const StatIds &stat, const char *registry, const std::string &id, int64_t pin)
{
    if (!stat)
        return pin >= 0 ? std::optional<int64_t>(pin) : std::nullopt;
    int64_t got = stat(registry, id, pin);
    return got >= 0 ? std::optional<int64_t>(got) : std::nullopt;
}

inline std::string text(const Value &v)
{
    if (v.is_string())
        return std::string(v.as_string());
    if (v.is_bool())
        return v.as_bool() ? "True" : "False";
    if (v.is_int())
        return std::to_string(v.as_int());
    if (v.is_double()) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.17g", v.as_double());
        for (int p = 1; p <= 17; p++) {
            char shorter[64];
            std::snprintf(shorter, sizeof shorter, "%.*g", p, v.as_double());
            if (std::strtod(shorter, nullptr) == v.as_double())
                return shorter;
        }
        return buf;
    }
    return {};
}

inline std::string stem(std::string_view path)
{
    auto slash = path.find_last_of("/\\");
    std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
    auto dot = name.rfind('.');
    return std::string(dot == std::string_view::npos ? name : name.substr(0, dot));
}

inline std::string hex(std::string_view s)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(s.size() * 2);
    for (unsigned char c : s) {
        out += digits[c >> 4];
        out += digits[c & 15];
    }
    return out;
}

inline void add_fields(Plan &plan, const char *table, const char *const *names, std::size_t n)
{
    Value list = Value::array();
    for (std::size_t i = 0; i < n; i++)
        list.push_back(Value(names[i]));
    plan.fields[table] = std::move(list);
}

inline Value add_row(const std::string &table, const std::string &from, Value set)
{
    return Value::object({{"op", "add-row"}, {"table", table}, {"from", from}, {"set", std::move(set)}});
}

inline Value set_op(const std::string &select, const std::string &field, const std::string &value)
{
    return Value::object({{"op", "set"}, {"select", select}, {"field", field}, {"value", value}});
}

inline Value append_op(const std::string &select, const std::string &field, const std::string &value)
{
    return Value::object({{"op", "list-append"}, {"select", select}, {"field", field}, {"value", value}});
}

inline void push_op(Plan &plan, Value op)
{
    plan.owners.push_back(plan.owner);
    plan.ops.push_back(std::move(op));
}

inline void push_row(Plan &plan, const std::string &table, const std::string &from, Value set)
{
    plan.keys += row_keys(table);
    push_op(plan, add_row(table, from, std::move(set)));
}

inline std::string bundle_name(const Value &sound)
{
    const Value &b = sound["bundle"];
    if (b.is_string())
        return stem(b.as_string());
    if (b.is_object() && b["stock"].is_string())
        return stem(b["stock"].as_string());
    return {};
}

inline void lower_engine_sound(const std::string &id, const Value &rec, Plan &plan)
{
    plan.owner = addon_of(id);
    const Value &b = rec["bundle"];
    std::string name = bundle_name(rec);
    if (name.empty()) {
        plan.lines.push_back("engine_sound " + id + ": no bundle; nothing applied");
        plan.refused++;
        return;
    }
    if (b.is_object()) {
        plan.lines.push_back("engine_sound " + id + ": stock bundle " + name + "; the game's row is used as is");
        return;
    }
    std::string from = rec["settings_from"].is_string() ? key_of(rec["settings_from"].as_string()) : name;
    Value set = Value::object({{"_metadata_", name}, {"bundle_name", name}});
    push_row(plan, "audio.audio_bundle_settings", std::string(AUDIO_BUNDLES) + "[_metadata_=" + from + "]",
             std::move(set));
    add_fields(plan, AUDIO_BUNDLES, AUDIO_BUNDLE_FIELDS, std::size(AUDIO_BUNDLE_FIELDS));
    plan.redirects.push_back({"audio/2025/bundles/" + name + ".bdl", std::string(b.as_string()), addon_of(id), id});
    plan.lines.push_back("engine_sound " + id + ": bundle " + name + ", settings from " + from);
}

inline void lower_vehicle_package(const std::string &id, const Value &rec, Plan &plan, const StatIds &stat = {})
{
    plan.owner = addon_of(id);
    std::string label = key_of(id);
    std::string donor = rec["donor"].is_string() ? key_of(rec["donor"].as_string()) : "f1_2025";
    std::string setups = rec["setups_from"].is_string() ? key_of(rec["setups_from"].as_string()) : donor;
    Value set = Value::object({{"_metadata_", label}});
    for (const char *field : {"resource_path", "blob_shadow_decal", "driver_animation_set", "ai_package_folder"})
        if (rec[field].is_string())
            set[field] = rec[field];
    set["has_cockpit_halo"] = rec["halo"].is_bool() ? text(rec["halo"]) : std::string("True");
    if (rec["aero_2026"].is_bool())
        set["is_26_aero_regulations"] = text(rec["aero_2026"]);
    auto sid = stat_of(stat, "vehicle.package", id, pin_of(rec));
    if (sid)
        set["stat_id"] = std::to_string(*sid);
    push_row(plan, "entrants.vehicles.vehicle_packages", std::string(PACKAGES) + "[_metadata_=" + donor + "]",
             std::move(set));
    push_row(plan, "car_setup.presets", std::string(SETUP_PRESETS) + "[_metadata_=" + setups + "_vehicle_package]",
             Value::object({{"_metadata_", label + "_vehicle_package"}}));
    plan.lines.push_back("vehicle.package " + id + ": row " + label + " from " + donor + ", setups from " + setups +
                         (sid ? ", stat id " + std::to_string(*sid) : std::string()));
}

inline void lower_vehicle(const std::string &id, const Value &rec, const Lookup &lookup, const Source &source,
                          Plan &plan, const StatIds &stat = {})
{
    plan.owner = addon_of(id);
    std::string label = rec["label"].is_string() ? std::string(rec["label"].as_string()) : key_of(id);
    if (!rec["donor"].is_string()) {
        plan.lines.push_back("vehicle " + id + ": no donor; nothing applied");
        plan.refused++;
        return;
    }
    std::string donor = key_of(rec["donor"].as_string());
    Value set = Value::object({{"_metadata_", label}});
    if (rec["performance_tier"].is_number())
        set["performance_tier"] = text(rec["performance_tier"]);
    if (rec["tyre_wear_scale"].is_number())
        set["tyre_wear_scale"] = text(rec["tyre_wear_scale"]);
    if (rec["package"].is_string())
        set["vehicle_package"] = key_of(rec["package"].as_string());
    const Value &ai = rec["ai"]["asset_filename"];
    if (ai.is_string() && ai.as_string() != "auto")
        set["asset_filename"] = ai;
    auto sid = stat_of(stat, "vehicle", id, pin_of(rec));
    if (sid)
        set["stat_id"] = std::to_string(*sid);
    push_row(plan, "entrants.vehicles", std::string(VEHICLES) + "[_metadata_=" + donor + "]", std::move(set));
    std::string line = "vehicle " + id + ": row " + label + " from " + donor +
                       (sid ? ", stat id " + std::to_string(*sid) : std::string());
    const Value &sound_id = rec["engine_sound"];
    if (sound_id.is_string() && rec["audio_switch"].is_string() && rec["audio_from"].is_string()) {
        const Value *sound = lookup("engine_sound", std::string(sound_id.as_string()));
        std::string bundle = sound ? bundle_name(*sound) : std::string();
        std::string to = std::string(AUDIO_CARS) + "." + std::string(rec["audio_switch"].as_string()) + ".";
        std::string from = std::string(AUDIO_CARS) + "." + std::string(rec["audio_from"].as_string()) + ".";
        std::size_t copied = 0;
        for (const char *field : AUDIO_CAR_FIELDS) {
            auto stock = source(from + field);
            if (!stock)
                continue;
            std::string value = stock->text;
            if (std::string_view(field) == "_metadata_")
                value = std::string(rec["audio_switch"].as_string());
            else if (std::string_view(field) == "bundle_name" && !bundle.empty())
                value = bundle + ".bdl";
            push_op(plan, Value::object({{"op", "put-record"}, {"key", to + field}, {"type", stock->tag},
                                         {"value", hex(value)}}));
            copied++;
        }
        plan.keys += copied;
        line += ", sound " + (bundle.empty() ? std::string("(missing)") : bundle) + " on " +
                std::string(rec["audio_switch"].as_string()) + " (" + std::to_string(copied) + " audio records)";
        if (!sound || bundle.empty())
            plan.refused++;
    }
    plan.lines.push_back(line);
}

inline std::string vehicle_label(const Value &team, const Lookup &lookup)
{
    if (!team["vehicle"].is_string())
        return {};
    const Value *v = lookup("vehicle", std::string(team["vehicle"].as_string()));
    if (v && (*v)["label"].is_string())
        return std::string((*v)["label"].as_string());
    return key_of(team["vehicle"].as_string());
}

// A package's private "database" block names the rows its own entrant list gets (entrant_prefix, standings,
// scenario_stat_id). A variant inherits its base's block with a new entrant list: when the block's entrant_list is
// another list, those names are made from the variant's entrant list and its scenario gets an id from the pool.
using PersonMap = std::map<std::string, std::string>;

inline std::string package_driver_id(std::string_view ref, const PersonMap *persons)
{
    if (persons && ref.rfind("person/", 0) == 0) {
        auto it = persons->find(std::string(ref));
        if (it != persons->end())
            return it->second;
    }
    return std::string(ref);
}

inline std::string record_label(const Lookup &lookup, const char *reg, std::string_view id)
{
    const Value *v = lookup(reg, std::string(id));
    if (v && (*v)["label"].is_string() && !(*v)["label"].as_string().empty())
        return std::string((*v)["label"].as_string());
    return key_of(id);
}

inline std::string package_driver(const Lookup &lookup, std::string_view ref, const PersonMap *persons)
{
    return record_label(lookup, "driver.identity", package_driver_id(ref, persons));
}

inline std::string package_look(const Lookup &lookup, std::string_view ref, const PersonMap *persons)
{
    const Value *d = lookup("driver.identity", package_driver_id(ref, persons));
    if (!d || !(*d)["look"].is_string() || (*d)["look"].as_string().empty())
        return {};
    return record_label(lookup, "driver.look", (*d)["look"].as_string());
}

inline std::string title_tag(const std::string &entrant_list)
{
    return "lng_swofty_era_" + entrant_list;
}

inline void lower_package(const std::string &id, const Value &rec, const Lookup &lookup, Plan &plan,
                          const StatIds &stat = {}, const PersonMap *wear = nullptr)
{
    plan.owner = addon_of(id);
    const Value &db = rec["database"];
    std::string list = std::string(rec["entrant_list"].as_string());
    std::string title = title_text(rec["title"]);
    if (title.empty())
        title = list;
    std::string mode = rec["setup_mode"].is_string() ? std::string(rec["setup_mode"].as_string()) : "swofty_f1";
    bool own = !db["entrant_list"].is_string() || db["entrant_list"].as_string() == list;
    std::string prefix = own && db["entrant_prefix"].is_string() ? std::string(db["entrant_prefix"].as_string())
                                                                  : list + "-";
    std::string standings = own && db["standings"].is_string() ? std::string(db["standings"].as_string())
                                                                : list + "_teams";
    int64_t scenario_pin = own && db["scenario_stat_id"].is_int() ? db["scenario_stat_id"].as_int() : -1;
    if (!own)
        plan.lines.push_back("content.package " + id + ": its database block is " +
                             std::string(db["entrant_list"].as_string()) + "'s; its own rows are " + prefix +
                             "<driver> and " + standings);
    std::size_t seats = 0;
    const Value &teams = rec["teams"];
    for (std::size_t t = 0; t < teams.size(); t++)
        seats += teams[t]["drivers"].size();
    bool wide = seats > 20;
    std::string list_from = db["entrant_list_from"].is_string() ? std::string(db["entrant_list_from"].as_string())
                                                                 : wide ? "f1_2026" : "f1_2025";
    std::string standings_from = db["standings_from"].is_string() ? std::string(db["standings_from"].as_string())
                                                                   : wide ? "f1_teams_2026" : "f1_teams_2024";
    for (std::size_t t = 0; t < teams.size(); t++) {
        const Value &team = teams[t];
        for (std::size_t s = 0; s < team["drivers"].size(); s++) {
            const Value &seat = team["drivers"][s];
            std::string driver = package_driver(lookup, seat["driver"].as_string(), wear);
            std::string look;
            if (seat["look"].is_string())
                look = key_of(seat["look"].as_string());
            else if (db["looks"].is_object())
                look = std::string(db["looks"][std::string(team["key"].as_string()) + "/" + driver].as_string());
            if (look.empty())
                look = package_look(lookup, seat["driver"].as_string(), wear);
            push_row(plan, "entrants.entrant_lists.entrants", std::string(ENTRANTS) + "[_metadata_=max_verstappen]",
                     Value::object({{"_metadata_", prefix + driver},
                                    {"driver", driver},
                                    {"driver_customisation", look},
                                    {"car_number", text(seat["number"])}}));
        }
    }
    push_row(plan, "entrants.standings", std::string(STANDINGS) + "[_metadata_=" + standings_from + "]",
             Value::object({{"_metadata_", standings}}));
    for (std::size_t t = 0; t < teams.size(); t++)
        push_op(plan, set_op(std::string(STANDINGS) + "[_metadata_=" + standings + "].team_reference_" +
                                 std::to_string(t),
                             "team", vehicle_label(teams[t], lookup)));
    Value row = Value::object({{"_metadata_", list},
                               {"display_name", title},
                               {"previous_season_team_standings", standings},
                               {"previous_season_driver_standings", ""}});
    if (db["entrant_list_set"].is_object())
        for (auto &[field, value] : *db["entrant_list_set"].if_object())
            row[field] = value;
    auto list_id = stat_of(stat, "content.package", id, pin_of(rec));
    if (list_id)
        row["stat_id"] = std::to_string(*list_id);
    if (wide)
        push_op(plan, set_op(std::string(SETUP_ROW) + "[_metadata_=" + mode + "]", "max_entrants", std::to_string(seats)));
    push_row(plan, "entrants.entrant_lists", std::string(LISTS) + "[_metadata_=" + list_from + "]", std::move(row));
    std::size_t slot = 0;
    for (std::size_t t = 0; t < teams.size(); t++) {
        const Value &team = teams[t];
        std::string label = vehicle_label(team, lookup);
        for (std::size_t s = 0; s < team["drivers"].size(); s++, slot++) {
            std::string driver = package_driver(lookup, team["drivers"][s]["driver"].as_string(), wear);
            std::string at = std::string(LISTS) + "[_metadata_=" + list + "].entrant_" + std::to_string(slot);
            char pos[16];
            std::snprintf(pos, sizeof pos, "_driver_0%zu", s + 1);
            for (auto &[field, value] : std::initializer_list<std::pair<const char *, std::string>>{
                     {"_metadata_", label + pos},
                     {"team", label},
                     {"vehicle", label},
                     {"entrant_reference", prefix + driver},
                     {"team_position", std::to_string(s)},
                     {"car_number", "255"},
                     {"driver", ""},
                     {"driver_customisation", ""}})
                push_op(plan, set_op(at, field, value));
        }
    }
    Value scenario = Value::object({{"_metadata_", list},
                                    {"display_name", title},
                                    {"entrant_list", list},
                                    {"game_setup_mode", mode}});
    auto scenario_id = stat_of(stat, SCENARIO_IDS, id, scenario_pin);
    if (scenario_id)
        scenario["statId"] = std::to_string(*scenario_id);
    push_row(plan, "game_setup.scenarios", std::string(SCENARIOS) + "[_metadata_=f1]", std::move(scenario));
    std::string category_tag = title;
    if (title.rfind("lng_", 0) != 0) {
        category_tag = title_tag(list);
        plan.texts.emplace_back(category_tag, title);
    }
    push_row(plan, CATEGORY, std::string(CATEGORY) + ".value[_metadata_=f1_2025]",
             Value::object({{"_metadata_", list}, {"game_value", list}, {"loc_tag", category_tag}}));
    for (const char *field : {"entrant_lists", "selectable_entrant_lists"})
        push_op(plan, append_op(std::string(SETUP_ROW) + "[_metadata_=" + mode + "]", field, list));
    std::vector<std::string> sets;
    if (db["content_sets"].is_array())
        for (std::size_t i = 0; i < db["content_sets"].size(); i++)
            sets.emplace_back(db["content_sets"][i].as_string());
    else
        sets = {"2025", "2026"};
    for (auto &cs : sets)
        push_op(plan, append_op("content_sets.content_set[_metadata_=" + cs + "].available_entrant_lists_0",
                                "entrant_lists", list));
    add_fields(plan, SCENARIOS, SCENARIO_FIELDS, std::size(SCENARIO_FIELDS));
    add_fields(plan, (std::string(CATEGORY) + ".value").c_str(), CATEGORY_FIELDS, std::size(CATEGORY_FIELDS));
    plan.lines.push_back("content.package " + id + ": entrant list " + list + ", " + std::to_string(seats) +
                         " entrants in " + mode +
                         (list_id ? ", stat id " + std::to_string(*list_id) : std::string()) +
                         (scenario_id ? ", scenario stat id " + std::to_string(*scenario_id) : std::string()));
}

using BaseImage = std::function<std::string(const std::string &package)>;

inline void lower_box(const std::string &id, const Value &rec, const Lookup &lookup, Plan &plan,
                      const BaseImage &base_image = {})
{
    std::string package = rec["package"].is_string() ? std::string(rec["package"].as_string()) : std::string();
    const Value *pkg = package.empty() ? nullptr : lookup("content.package", package);
    if (!pkg || !(*pkg)["entrant_list"].is_string()) {
        plan.lines.push_back("era.box " + id + ": its package is missing; no box");
        plan.refused++;
        return;
    }
    Box box;
    box.id = id;
    box.entrant_list = std::string((*pkg)["entrant_list"].as_string());
    std::string title = title_text(rec["title"]);
    if (title.empty())
        title = title_text((*pkg)["title"]);
    if (title.empty())
        title = box.entrant_list;
    if (title.rfind("lng_", 0) == 0) {
        box.title = title;
    } else {
        box.title = title_tag(box.entrant_list);
        box.literal = title;
    }
    box.image = std::string(rec["image"].as_string());
    if (box.image.empty() && base_image)
        box.image = base_image(package);
    box.order = int(rec["order"].as_int());
    plan.boxes.push_back(box);
}

// The stat-id pools of the content registries: the ids above every row the table holds when the game loads, up to
// the table's ceiling, and the rows that hold each id below it.
struct PoolSpec {
    const char *registry, *table, *row, *field;
    int64_t cap;
};

inline constexpr PoolSpec POOLS[] = {
    {"vehicle", "entrants.vehicles", "vehicle", "stat_id", 1023},
    {"vehicle.package", "entrants.vehicles.vehicle_packages", "vehicle_package", "stat_id", 255},
    {"content.package", "entrants.entrant_lists", "entrant_list", "stat_id", 255},
    {SCENARIO_IDS, "game_setup.scenarios", "scenario", "statId", 255},
    {"team.identity", "entrants.teams", "team", "stat_id", 1023},
};

struct PoolScan {
    std::string registry, table;
    int64_t lo = 0, hi = -1;
    std::map<int64_t, std::string> used;
    std::vector<std::pair<int64_t, int64_t>> holes;
};

inline std::vector<PoolScan> scan_pools(const std::function<std::optional<std::string>(const std::string &)> &text)
{
    std::vector<PoolScan> out;
    for (auto &spec : POOLS) {
        PoolScan scan{spec.registry, spec.table, 0, -1, {}};
        std::string base = std::string(spec.table) + "." + spec.row + "_";
        auto count = text(std::string(spec.table) + "._" + spec.row + "_instancecount_");
        int64_t rows = count ? std::strtoll(count->c_str(), nullptr, 10) : 0;
        int64_t top = -1;
        for (int64_t i = 0; i < rows; i++) {
            auto v = text(base + std::to_string(i) + "." + spec.field);
            if (!v || v->empty())
                continue;
            int64_t id = std::strtoll(v->c_str(), nullptr, 10);
            auto label = text(base + std::to_string(i) + "._metadata_");
            scan.used[id] = label ? *label : base + std::to_string(i);
            top = std::max(top, id);
        }
        scan.lo = top + 1;
        scan.hi = spec.cap;
        out.push_back(std::move(scan));
    }
    return out;
}

}

