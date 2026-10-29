#pragma once

#include "liveries.hpp"
#include "lower.hpp"
#include "rows.hpp"

#include <set>

// Lowers the wearables and drivers registries (helmet, suit, gloves, driver.head, driver.look, driver.person,
// driver.identity, driver.stats, wearable.season_set) to database rows and asset groups, following
// work/std16/RE-WEARABLES.md. An item copies a stock row (its `from`) and sets its archive's asset group, models,
// colours and id; a team's suit and gloves that use one archive share one asset group. A look is a row in the
// official driver customisations folder naming a driver's helmet, suit and gloves. A driver identity copies its
// donor's driver row and names a head and a look; one without a look is refused. A season set is written as a
// team kit row only when it names a suit or gloves: a team whose drivers all have looks needs none, since each
// driver then wears his own look.

namespace swofty_core::apply::content {

inline constexpr const char *HELMETS = "customisation.custom_avatar.helmets";
inline constexpr const char *SUITS = "customisation.custom_avatar.suits";
inline constexpr const char *GLOVES = "customisation.custom_avatar.gloves";
inline constexpr const char *HEADS = "entrants.animation.driver_heads";
inline constexpr const char *DRIVERS = "entrants.drivers";
inline constexpr const char *LOOK_FOLDERS = "entrants.driver_customisations";
inline constexpr const char *OFFICIAL_LOOKS = "entrants.driver_customisations.folder[_metadata_=official_driver_customisations]";
inline constexpr const char *KIT_YEARS = "entrants.team_customisation";
inline constexpr const char *STATS_TABLE = "driver_statistics.driver_statistics";
inline constexpr const char *WEAR_RATES = "engine_management.driver_wear_rates";

inline constexpr const char *WEAR_REGISTRIES[] = {"helmet", "suit", "gloves", "driver.head", "driver.look",
                                                  "driver.person", "driver.identity", "driver.stats",
                                                  "wearable.season_set"};

inline bool is_wear_registry(std::string_view reg)
{
    for (const char *r : WEAR_REGISTRIES)
        if (reg == r)
            return true;
    return false;
}

struct WearCtx {
    struct Shared {
        std::string folder, stem;
    };
    std::map<std::string, Shared> groups;
    std::set<std::string> stats_of;
    std::map<std::string, std::string> look_driver;
    std::map<std::string, std::string> person_driver;
};

inline std::string label_of(const Lookup &lookup, const char *reg, std::string_view id)
{
    return record_label(lookup, reg, id);
}

inline std::string person_key(std::string_view id)
{
    return "person/" + key_of(id);
}

inline std::string archive_file(const Value &p)
{
    if (p.is_string())
        return std::string(p.as_string());
    if (p.is_object() && p["erp"].is_string())
        return std::string(p["erp"].as_string());
    return {};
}

inline std::string folder_file(const Value &p, const std::string &label)
{
    std::string f = archive_file(p);
    if (!f.empty() && f.back() == '/')
        f += label + ".erp";
    return f;
}

inline std::string argb_text(std::string_view c)
{
    if (c.size() != 7 && c.size() != 9)
        return {};
    uint32_t v = static_cast<uint32_t>(std::strtoul(std::string(c.substr(1)).c_str(), nullptr, 16));
    if (c.size() == 7)
        v |= 0xff000000u;
    return std::to_string(static_cast<int32_t>(v));
}

inline std::string text_of(const Value &t)
{
    return title_text(t);
}

inline std::string driver_name_tag(std::string_view label, std::string_view part)
{
    return "lng_swofty_driver_" + std::string(label) + "_" + std::string(part);
}

inline std::string character_erp(std::string group)
{
    group = replace_all(group, "[CHARACTER_PACKAGE]", "character_package");
    const std::string head = "asset_groups/";
    if (group.rfind(head, 0) == 0)
        group = "2025_asset_groups/" + group.substr(head.size());
    const std::string tail = ".assetgroup";
    if (group.size() > tail.size() && group.compare(group.size() - tail.size(), tail.size(), tail) == 0)
        group = group.substr(0, group.size() - tail.size()) + ".erp";
    return group;
}

inline void collect_extras(Value &set, const Value &v, const std::string &key, const std::vector<std::string> &keys)
{
    if (v.is_object()) {
        for (auto &[k, child] : *v.if_object())
            collect_extras(set, child, key + "." + std::string(k), keys);
        return;
    }
    if ((!v.is_string() && !v.is_bool() && !v.is_number()) || set.contains(key))
        return;
    if (std::find(keys.begin(), keys.end(), key) != keys.end())
        set[key] = Value(text(v));
}

inline void set_extras(Value &set, const Value &rec, const std::string &shape, std::initializer_list<const char *> handled)
{
    const auto *keys = rows::Keys::builtin().get(shape);
    auto o = rec.if_object();
    if (!keys || !o)
        return;
    for (auto &[k, v] : *o) {
        std::string key(k);
        if (key == "extends" || key == "label" || key == "_metadata_")
            continue;
        bool skip = false;
        for (const char *h : handled)
            skip = skip || key == h;
        if (!skip)
            collect_extras(set, v, key, *keys);
    }
}

struct ItemKind {
    const char *reg, *table, *row, *shape;
};

inline constexpr ItemKind ITEM_KINDS[] = {
    {"helmet", HELMETS, "Helmet", "customisation.custom_avatar.helmets.Helmet_N"},
    {"suit", SUITS, "Suit", "customisation.custom_avatar.suits.Suit_N"},
    {"gloves", GLOVES, "Gloves", "customisation.custom_avatar.gloves.Gloves_N"},
};

inline const ItemKind &item_kind(std::string_view reg)
{
    for (auto &k : ITEM_KINDS)
        if (reg == k.reg)
            return k;
    return ITEM_KINDS[0];
}

inline std::string row_at(const char *table, const char *row, const std::string &label)
{
    return std::string(table) + "." + row + "[_metadata_=" + label + "]";
}

inline void lower_item(const ItemKind &k, const std::string &id, const Value &rec, const Lookup &lookup, const Read &read,
                       Plan &plan, const StatIds &stat, WearCtx &ctx)
{
    plan.owner = addon_of(id);
    std::string label = label_of(lookup, k.reg, id);
    std::string from = std::string(k.table) + "." + k.row + "_0";
    if (rec["from"].is_string())
        from = row_at(k.table, k.row, label_of(lookup, k.reg, rec["from"].as_string()));
    if (!read(from + "._metadata_")) {
        plan.lines.push_back(std::string(k.reg) + " " + id + ": no row " + from + " to copy; nothing applied");
        plan.refused++;
        return;
    }
    bool helmet = std::string_view(k.reg) == "helmet";
    bool kit = !helmet && rec["team"].is_string() && !rec["team"].as_string().empty();
    Value set = Value::object({{"_metadata_", label}});
    if (auto sid = stat_of(stat, k.reg, id, pin_of(rec)))
        set["id"] = std::to_string(*sid);
    if (rec["colours"].is_array() && rec["colours"].size() == 4)
        for (std::size_t i = 0; i < 4; i++)
            set["colour_" + std::to_string(i + 1) + "_default"] = Value(argb_text(rec["colours"][i].as_string()));
    std::string name = text_of(rec["name"]);
    if (!name.empty())
        set["name_loc_tag"] = name;
    if (rec["official_team_item"].is_bool())
        set["is_official_team_item"] = text(rec["official_team_item"]);
    std::string file = archive_file(rec["archive"]);
    std::string line = std::string(k.reg) + " " + id + ": row " + label + " from " + from;
    if (!file.empty()) {
        auto stock_group = read(from + ".asset_group");
        if (!stock_group || stock_group->empty()) {
            plan.lines.push_back(std::string(k.reg) + " " + id + ": " + from + " has no asset group to extend; nothing applied");
            plan.refused++;
            return;
        }
        std::string addon = addon_of(id);
        std::string key = (helmet ? "helmet|" : "kit|") + addon + "|" + file;
        auto at = ctx.groups.find(key);
        bool fresh = at == ctx.groups.end();
        if (fresh) {
            WearCtx::Shared s;
            s.folder = helmet || !kit ? "drivers/male/" + label : "team_livery/" + label;
            s.stem = helmet ? label + "_helmet" : label;
            at = ctx.groups.emplace(key, s).first;
        }
        const auto &s = at->second;
        std::string name_of = helmet ? label : s.stem;
        set["asset_group"] = "asset_groups/[CHARACTER_PACKAGE]/" + s.folder + "/" + s.stem + ".assetgroup";
        set["material_override"] = helmet ? label : s.stem;
        std::string idf = "eaid://[CHARACTER_PACKAGE]/" + s.folder + "/idf/";
        if (std::string_view(k.reg) == "suit") {
            set["feet_model"] = idf + name_of + (kit ? "_male" : "");
            set["logos_model_override_pattern"] = idf + name_of + (kit ? "_body_logos_[GENDER]" : "_body_logos");
        } else if (std::string_view(k.reg) == "gloves") {
            set["logos_model"] = idf + name_of + (kit ? "_glove_logos_[GENDER]" : "_glove_logos");
        } else {
            set["attachments_model"] = idf + label + "_helmet_attachments";
        }
        if (fresh) {
            Group g;
            g.registry = k.reg;
            g.group = "asset_groups/character_package/" + s.folder + "/" + s.stem + ".assetgroup";
            g.tmpl = replace_all(*stock_group, "[CHARACTER_PACKAGE]", "character_package");
            g.erp = "2025_asset_groups/character_package/" + s.folder + "/" + s.stem + ".erp";
            g.file = file;
            g.addon = addon;
            g.id = id;
            g.common = {character_erp(*stock_group)};
            plan.groups.push_back(std::move(g));
        }
        line += std::string(", asset group ") + (fresh ? "" : "shared ") + s.folder + "/" + s.stem;
    }
    set_extras(set, rec, k.shape, {"archive", "from", "name", "driver", "colours", "official_team_item", "stat_id", "team"});
    push_row(plan, k.table, from, std::move(set));
    plan.lines.push_back(line);
}

inline void lower_head(const std::string &id, const Value &rec, const Lookup &lookup, const Read &read, Plan &plan,
                       const StatIds &stat)
{
    plan.owner = addon_of(id);
    std::string label = label_of(lookup, "driver.head", id);
    bool female = rec["body"].is_string() && rec["body"].as_string() == "female";
    std::string from;
    if (rec["rig_from"].is_string())
        from = row_at(HEADS, "driver_head", label_of(lookup, "driver.head", rec["rig_from"].as_string()));
    else {
        auto count = read(std::string(HEADS) + "._driver_head_instancecount_");
        int64_t n = count ? std::strtoll(count->c_str(), nullptr, 10) : 0;
        for (int64_t i = 0; i < n && from.empty(); i++) {
            std::string at = std::string(HEADS) + ".driver_head_" + std::to_string(i);
            auto body = read(at + ".body_type");
            if (body && *body == (female ? "generic_female" : "generic_male"))
                from = at;
        }
    }
    if (from.empty() || !read(from + "._metadata_")) {
        plan.lines.push_back("driver.head " + id + ": no head row to copy; nothing applied");
        plan.refused++;
        return;
    }
    std::string from_label = read(from + "._metadata_").value_or("");
    Value set = Value::object({{"_metadata_", label}});
    set["body_type"] = female ? "generic_female" : "generic_male";
    if (rec["customisable"].is_bool())
        set["available_for_customisation"] = text(rec["customisable"]);
    if (auto sid = stat_of(stat, "driver.head", id, pin_of(rec)))
        set["stat_id"] = std::to_string(*sid);
    set_extras(set, rec, "entrants.animation.driver_heads.driver_head_N",
               {"archive", "morph", "rig_from", "body", "customisable", "stat_id"});
    push_row(plan, HEADS, from, std::move(set));
    std::string line = "driver.head " + id + ": row " + label + " from " + from_label;
    std::string addon = addon_of(id);
    std::string heads = "asset_groups/animation_package/scenes/cinematics/drivers/";
    for (auto [kind, value] : {std::pair<const char *, std::string>{"heads", folder_file(rec["archive"], label)},
                                {"morph_poses", folder_file(rec["morph"], label)}}) {
        if (value.empty())
            continue;
        Group g;
        g.registry = "driver.head";
        g.group = heads + kind + "/" + label + ".assetgroup";
        g.tmpl = heads + kind + "/" + from_label + ".assetgroup";
        g.erp = "2025_asset_groups/animation_package/scenes/cinematics/drivers/" + std::string(kind) + "/" + label + ".erp";
        g.file = value;
        g.addon = addon;
        g.id = id;
        plan.groups.push_back(std::move(g));
        line += std::string(", ") + kind + " archive";
    }
    plan.lines.push_back(line);
}

inline void lower_look(const std::string &id, const Value &rec, const Lookup &lookup, const Read &read, Plan &plan,
                       const StatIds &stat, const WearCtx &ctx)
{
    plan.owner = addon_of(id);
    std::string label = label_of(lookup, "driver.look", id);
    std::string folder = OFFICIAL_LOOKS;
    std::string from = folder + ".preloaded_driver_customisation_properties_0";
    if (rec["from"].is_string())
        from = folder + ".preloaded_driver_customisation_properties[_metadata_=" +
               label_of(lookup, "driver.look", rec["from"].as_string()) + "]";
    if (!read(from + "._metadata_")) {
        plan.lines.push_back("driver.look " + id + ": no look row to copy; nothing applied");
        plan.refused++;
        return;
    }
    Value set = Value::object({{"_metadata_", label}, {"available", "False"}});
    for (const char *part : {"helmet", "suit", "gloves"})
        if (rec[part].is_string())
            set[part] = label_of(lookup, part, rec[part].as_string());
    for (const char *part : {"pose", "podium_emote"})
        if (rec[part].is_string())
            set[part] = rec[part];
    auto who = ctx.look_driver.find(id);
    if (who != ctx.look_driver.end())
        set["linked_driver_stat_id"] = who->second;
    auto sid = stat_of(stat, "driver.look", id, pin_of(rec));
    if (sid)
        set["stat_id"] = std::to_string(*sid);
    set_extras(set, rec, "entrants.driver_customisations.folder_N.preloaded_driver_customisation_properties_N",
               {"helmet", "suit", "gloves", "pose", "podium_emote", "stat_id", "from"});
    push_row(plan, folder, from, std::move(set));
    plan.lines.push_back("driver.look " + id + ": row " + label + (sid ? ", stat id " + std::to_string(*sid) : std::string()) +
                         (who != ctx.look_driver.end() ? ", linked to " + who->second : std::string(", no driver names it")));
}

inline void lower_person(const std::string &id, const Value &rec, const Lookup &lookup, const WearCtx &ctx, Plan &plan)
{
    plan.owner = addon_of(id);
    auto it = ctx.person_driver.find(person_key(id));
    plan.lines.push_back("driver.person " + id + " (" + std::string(rec["name"].as_string()) + "): " + person_key(id) +
                         (it == ctx.person_driver.end() ? " has no driver"
                                                        : " resolves to " + label_of(lookup, "driver.identity", it->second)));
}

inline void side_row(Plan &plan, const Read &read, const char *table, const char *row, const std::string &donor,
                     const std::string &label)
{
    std::string from = row_at(table, row, donor);
    if (!read(from + "._metadata_"))
        return;
    push_row(plan, table, from, Value::object({{"_metadata_", label}, {"driver", label}}));
}

inline std::string evolution_text(const Value &curve)
{
    auto pct = [](double v) {
        v = std::max(0.0, std::min(100.0, v)) / 100.0;
        return text(Value(v));
    };
    std::string s = pct(curve["start"].as_double()), p = pct(curve["peak"].as_double());
    return "@v2@minx:0,maxx:10,miny:0,maxy:100,0:" + s + ",0:" + s + ",0.9:" + p + ",1:" + p;
}

inline void holder_row(Plan &plan, const Read &read, const std::string &donor, const std::string &label, const Value *stats)
{
    std::string from = row_at(STATS_TABLE, "DriverStatisticsHolder", donor);
    if (!read(from + "._metadata_"))
        from = std::string(STATS_TABLE) + ".DriverStatisticsHolder_0";
    if (!read(from + "._metadata_"))
        return;
    push_row(plan, STATS_TABLE, from, Value::object({{"_metadata_", label}, {"driver", label}}));
    if (!stats)
        return;
    std::string holder = row_at(STATS_TABLE, "DriverStatisticsHolder", label);
    auto sets = read(from + "._StatisticsDataSet_instancecount_");
    int64_t ns = sets ? std::strtoll(sets->c_str(), nullptr, 10) : 0;
    for (int64_t d = 0; d < ns; d++) {
        std::string data = from + ".StatisticsDataSet_" + std::to_string(d);
        auto count = read(data + "._Statistic_instancecount_");
        int64_t nc = count ? std::strtoll(count->c_str(), nullptr, 10) : 0;
        for (int64_t s = 0; s < nc; s++) {
            auto name = read(data + ".Statistic_" + std::to_string(s) + "._metadata_");
            if (!name || !(*stats)[*name].is_object())
                continue;
            push_op(plan, set_op(holder + ".StatisticsDataSet_" + std::to_string(d) + ".Statistic_" + std::to_string(s),
                                 "evolution", evolution_text((*stats)[*name])));
        }
    }
}

inline void lower_identity(const std::string &id, const Value &rec, const Lookup &lookup, const Read &read, Plan &plan,
                           const StatIds &stat, const WearCtx &ctx)
{
    plan.owner = addon_of(id);
    std::string label = label_of(lookup, "driver.identity", id);
    if (!rec["donor"].is_string()) {
        plan.lines.push_back("driver.identity " + id + ": no donor row to copy; the record names a driver row something else made, nothing applied");
        return;
    }
    if (!rec["look"].is_string() || rec["look"].as_string().empty()) {
        plan.lines.push_back("driver.identity " + id + ": no look; a seated driver without one hangs the next session load; nothing applied");
        plan.refused++;
        return;
    }
    std::string donor = label_of(lookup, "driver.identity", rec["donor"].as_string());
    std::string from = row_at(DRIVERS, "driver", donor);
    if (!read(from + "._metadata_")) {
        plan.lines.push_back("driver.identity " + id + ": the donor row " + donor + " is not in the game database; nothing applied");
        plan.refused++;
        return;
    }
    Value set = Value::object({{"_metadata_", label}, {"available", "True"}});
    const Value &n = rec["names"];
    std::vector<std::string> tags;
    for (auto [field, key] : {std::pair<const char *, const char *>{"first_name", "first"}, {"last_name", "last"},
                               {"full_name", "full"}}) {
        std::string value = text_of(n[key]);
        if (value.empty())
            continue;
        if (n[key].is_object()) {
            std::string tag = driver_name_tag(label, key);
            plan.texts.emplace_back(tag, value);
            tags.push_back(tag);
            value = tag;
            if (std::string_view(key) == "last")
                if (auto standard = read(from + ".last_name_standard"); standard && !standard->empty())
                    set["last_name_standard"] = tag;
        }
        set[field] = std::move(value);
    }
    if (n["code"].is_string() && !n["code"].as_string().empty())
        set["initials"] = n["code"];
    if (rec["nationality"].is_string() && !rec["nationality"].as_string().empty())
        set["nationality"] = rec["nationality"];
    if (rec["number"].is_int() && rec["number"].as_int() > 0)
        set["car_number"] = std::to_string(rec["number"].as_int());
    if (rec["head"].is_string() && !rec["head"].as_string().empty())
        set["head"] = label_of(lookup, "driver.head", rec["head"].as_string());
    if (rec["audio_name"].is_string() && !rec["audio_name"].as_string().empty())
        set["audio_name"] = rec["audio_name"];
    if (rec["seasons_completed"].is_int())
        set["f1_seasons_completed"] = std::to_string(rec["seasons_completed"].as_int());
    if (rec["garage_slot"].is_int())
        set["garage_slot_preference"] = std::to_string(rec["garage_slot"].as_int());
    auto sid = stat_of(stat, "driver.identity", id, pin_of(rec));
    if (sid)
        set["stat_id"] = std::to_string(*sid);
    set_extras(set, rec, "entrants.drivers.driver_N",
               {"donor", "person", "names", "nationality", "number", "head", "look", "audio_name", "seasons_completed",
                "garage_slot", "pools", "stat_id"});
    push_row(plan, DRIVERS, from, std::move(set));
    if (!ctx.stats_of.count(id))
        holder_row(plan, read, donor, label, nullptr);
    side_row(plan, read, WEAR_RATES, "driver_wear_rate", donor, label);
    std::string line = "driver.identity " + id + ": row " + label + " from " + donor +
                       (sid ? ", stat id " + std::to_string(*sid) : std::string()) + ", look " +
                       label_of(lookup, "driver.look", rec["look"].as_string());
    if (!tags.empty()) {
        line += "; literal names as";
        for (auto &t : tags)
            line += " " + t;
    }
    if (rec["pools"].is_array() && rec["pools"].size())
        line += "; pools are not applied (the driver pool tables are not mapped)";
    plan.lines.push_back(line);
}

inline void lower_stats(const std::string &id, const Value &rec, const Lookup &lookup, const Read &read, Plan &plan)
{
    plan.owner = addon_of(id);
    if (!rec["driver"].is_string()) {
        plan.lines.push_back("driver.stats " + id + ": no driver; nothing applied");
        plan.refused++;
        return;
    }
    std::string driver(rec["driver"].as_string());
    std::string label = label_of(lookup, "driver.identity", driver);
    std::string donor = label;
    if (const Value *d = lookup("driver.identity", driver); d && (*d)["donor"].is_string())
        donor = label_of(lookup, "driver.identity", (*d)["donor"].as_string());
    Value curves = Value::object();
    for (const char *s : {"experience", "racecraft", "awareness", "pace", "focus"})
        if (rec[s].is_object())
            curves[s] = rec[s];
    holder_row(plan, read, donor, label, &curves);
    plan.lines.push_back("driver.stats " + id + ": holder for " + label + " from " + donor);
}

inline int season_of(std::string_view label)
{
    auto us = label.rfind('_');
    return us == std::string_view::npos ? 0 : std::atoi(std::string(label.substr(us + 1)).c_str());
}

inline void lower_season_set(const std::string &id, const Value &rec, const Lookup &lookup, const Read &read, Plan &plan,
                             const StatIds &stat)
{
    plan.owner = addon_of(id);
    bool suit = rec["suit"].is_string() && !rec["suit"].as_string().empty();
    bool gloves = rec["gloves"].is_string() && !rec["gloves"].as_string().empty();
    if (!rec["team"].is_string()) {
        plan.lines.push_back("wearable.season_set " + id + ": no team; nothing applied");
        plan.refused++;
        return;
    }
    std::string team = label_of(lookup, "team.identity", rec["team"].as_string());
    if (!suit && !gloves) {
        plan.lines.push_back("wearable.season_set " + id + ": names no suit or gloves, so no kit row; each driver of " + team +
                             " wears his own look");
        return;
    }
    int year = int(rec["season"].as_int());
    auto count = read(std::string(KIT_YEARS) + "._team_customisation_for_year_instancecount_");
    int64_t n = count ? std::strtoll(count->c_str(), nullptr, 10) : 0;
    std::string want = "season_" + std::to_string(year), found, nearest;
    int best = -1;
    for (int64_t i = 0; i < n; i++) {
        std::string at = std::string(KIT_YEARS) + ".team_customisation_for_year_" + std::to_string(i);
        auto s = read(at + ".f1_season");
        if (!s)
            continue;
        if (*s == want)
            found = at;
        int gap = std::abs(season_of(*s) - year);
        if (best < 0 || gap < best) {
            best = gap;
            nearest = at;
        }
    }
    std::string year_row = !found.empty() ? found : nearest;
    if (year_row.empty()) {
        plan.lines.push_back("wearable.season_set " + id + ": the game database has no team kit year row; nothing applied");
        plan.refused++;
        return;
    }
    std::string list;
    if (rec["drivers"].is_array())
        for (std::size_t i = 0; i < rec["drivers"].size(); i++) {
            const Value *d = lookup("driver.identity", std::string(rec["drivers"][i].as_string()));
            if (d && (*d)["look"].is_string())
                list += (list.empty() ? "" : ",") + label_of(lookup, "driver.look", (*d)["look"].as_string());
        }
    std::string label = std::to_string(year) + "-" + team;
    Value set = Value::object({{"_metadata_", label},
                               {"linked_team", team},
                               {"suit", suit ? label_of(lookup, "suit", rec["suit"].as_string()) : std::string()},
                               {"gloves", gloves ? label_of(lookup, "gloves", rec["gloves"].as_string()) : std::string()},
                               {"official_driver_customisations", list},
                               {"is_default_for_team", "False"},
                               {"isDefaultForYear", "False"}});
    auto sid = stat_of(stat, "wearable.season_set", id, pin_of(rec));
    if (sid)
        set["stat_id"] = std::to_string(*sid);
    push_row(plan, year_row, year_row + ".team_customisation_0", std::move(set));
    plan.lines.push_back("wearable.season_set " + id + ": kit " + label + " for " + team + " in " + year_row +
                         (found.empty() ? " (no season_" + std::to_string(year) + " row; the nearest year row, which the game's kit pick falls back to)"
                                        : std::string()) +
                         (list.empty() ? "; no drivers declared, so the kit's suit and gloves replace every driver's"
                                       : "; official looks " + list));
}

inline std::vector<PoolScan> scan_wear_pools(const std::function<std::optional<std::string>(const std::string &)> &text)
{
    std::vector<PoolScan> out;
    auto count = [&](const std::string &table, const std::string &row) {
        auto c = text(table + "._" + row + "_instancecount_");
        return c ? std::strtoll(c->c_str(), nullptr, 10) : int64_t(0);
    };
    auto top_of = [&](PoolScan &scan, const std::string &prefix, int64_t rows, const char *field) {
        int64_t top = -1;
        for (int64_t i = 0; i < rows; i++) {
            auto v = text(prefix + std::to_string(i) + "." + field);
            if (!v || v->empty())
                continue;
            int64_t id = std::strtoll(v->c_str(), nullptr, 10);
            auto label = text(prefix + std::to_string(i) + "._metadata_");
            scan.used[id] = label ? *label : prefix + std::to_string(i);
            top = std::max(top, id);
        }
        return top;
    };
    for (auto &k : ITEM_KINDS) {
        PoolScan s{k.reg, k.table, 0, 4095, {}, {}};
        s.lo = top_of(s, std::string(k.table) + "." + k.row + "_", count(k.table, k.row), "id") + 1;
        out.push_back(std::move(s));
    }
    PoolScan heads{"driver.head", HEADS, 0, 1023, {}, {}};
    heads.lo = top_of(heads, std::string(HEADS) + ".driver_head_", count(HEADS, "driver_head"), "stat_id") + 1;
    out.push_back(std::move(heads));
    PoolScan looks{"driver.look", LOOK_FOLDERS, 0, 1023, {}, {}};
    int64_t top = -1;
    for (int64_t f = 0, nf = count(LOOK_FOLDERS, "folder"); f < nf; f++) {
        std::string folder = std::string(LOOK_FOLDERS) + ".folder_" + std::to_string(f);
        top = std::max(top, top_of(looks, folder + ".preloaded_driver_customisation_properties_",
                                   count(folder, "preloaded_driver_customisation_properties"), "stat_id"));
    }
    looks.lo = top + 1;
    out.push_back(std::move(looks));
    PoolScan drivers{"driver.identity", DRIVERS, 208, 253, {}, {}};
    top_of(drivers, std::string(DRIVERS) + ".driver_", count(DRIVERS, "driver"), "stat_id");
    int64_t run = -1;
    for (int64_t i = 208; i <= 254; i++) {
        bool free = i <= 253 && !drivers.used.count(i);
        if (free && run < 0)
            run = i;
        if (!free && run >= 0) {
            drivers.holes.push_back({run, i - 1});
            run = -1;
        }
    }
    drivers.lo = drivers.holes.empty() ? 254 : drivers.holes.front().first;
    drivers.hi = drivers.holes.empty() ? 253 : drivers.holes.back().second;
    out.push_back(std::move(drivers));
    PoolScan kits{"wearable.season_set", KIT_YEARS, 0, 255, {}, {}};
    int64_t ktop = -1;
    for (int64_t y = 0, ny = count(KIT_YEARS, "team_customisation_for_year"); y < ny; y++) {
        std::string year = std::string(KIT_YEARS) + ".team_customisation_for_year_" + std::to_string(y);
        ktop = std::max(ktop, top_of(kits, year + ".team_customisation_", count(year, "team_customisation"), "stat_id"));
    }
    kits.lo = ktop + 1;
    out.push_back(std::move(kits));
    return out;
}

}
