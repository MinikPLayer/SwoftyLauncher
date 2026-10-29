#pragma once

#include "liveries.hpp"
#include "lower.hpp"

#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <optional>
#include <set>

// Lowers team.identity (a new entrants.teams row and the side rows found by its label), team.pit_times and
// team.logo to the operations of the row engine, and dresses a loaded team record as another identity. A new team
// copies its donor's row (every key, story_info included) and takes its names, colours (signed ARGB), livery
// name, prestige, nationality, audio id and stat id from the record. The side rows (pit times, initial cash,
// facilities, driver market, recognition, renown and form, primary sponsors, showroom history) are copies of the
// donor's rows relabelled to the new team, in every data group that holds one. The wearables stream writes the
// team customisation rows (suit, gloves), the vehicle's animation team data follows the car.

namespace swofty_core::apply::content {

inline constexpr const char *TEAMS = "entrants.teams";
inline constexpr const char *TEAM_ROW = "entrants.teams.team";
inline constexpr const char *PIT_TIMES = "pit_lane.pit_times";
inline constexpr const char *PIT_ROW = "team_pit_times";
inline constexpr const char *TEAM_IDS = "team.identity";

struct SideTable {
    const char *group, *group_row, *sub, *row;
    bool pit;
};

inline constexpr SideTable SIDE_TABLES[] = {
    {PIT_TIMES, nullptr, nullptr, PIT_ROW, true},
    {"bank.initial_team_cash", nullptr, nullptr, "InitialTeamCash", false},
    {"showroom.teams_history", nullptr, nullptr, "team_history", false},
    {"facilities.facilities", "Facilities", "team_developments", "TeamDevelopment", false},
    {"driver_market.driver_market", "DataGroup", "team_driver_rating_range_requirements",
     "TeamDriverRatingRangeRequirement", false},
    {"recognition", "RecognitionDataGroup", "team_modifiers", "TeamModifier", false},
    {"career.my_team_renown_and_form", "MyTeamRenownAndFormDataGroup", "initial_renown_and_form",
     "InitialTeamRenownAndForm", false},
    {"career.my_team_sponsors", "MyTeamSponsorsDataGroup", "team_primary_sponsors", "team_primary_sponsor", false},
};

struct TeamRefs {
    std::map<std::string, std::string> constructor_team;
    std::map<std::string, std::string> pit_record;
    std::vector<std::string> lines;
    int constructors = 0, constructors_refused = 0;
};

inline std::string constructor_token(const Value &ref)
{
    if (!ref.is_string())
        return {};
    std::string s(ref.as_string());
    if (s.rfind("constructor/", 0) == 0)
        return s.substr(12);
    return key_of(s);
}

inline std::string team_label(const Value &ref, const TeamRefs &refs)
{
    if (!ref.is_string())
        return {};
    std::string s(ref.as_string());
    if (s.rfind("constructor/", 0) == 0) {
        auto it = refs.constructor_team.find(s.substr(12));
        return it == refs.constructor_team.end() ? std::string() : key_of(it->second);
    }
    return key_of(s);
}

inline std::optional<int32_t> argb(const Value &v)
{
    if (!v.is_string())
        return std::nullopt;
    std::string s(v.as_string());
    if (s.size() != 7 && s.size() != 9)
        return std::nullopt;
    if (s[0] != '#')
        return std::nullopt;
    uint32_t n = 0;
    for (std::size_t i = 1; i < s.size(); i++) {
        char c = s[i];
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0)
            return std::nullopt;
        n = n * 16 + uint32_t(d);
    }
    if (s.size() == 7)
        n |= 0xff000000u;
    return int32_t(n);
}

inline TeamRefs team_refs(const std::vector<std::pair<std::string, const Value *>> &identities,
                          const std::vector<std::pair<std::string, const Value *>> &constructors,
                          const std::vector<std::pair<std::string, const Value *>> &pit_times)
{
    TeamRefs r;
    for (auto &[id, rec] : identities)
        if (std::string t = constructor_token((*rec)["constructor"]); !t.empty())
            r.constructor_team[t] = id;
    for (auto &[id, rec] : constructors) {
        std::string token = key_of(id);
        if (!(*rec)["team"].is_string())
            continue;
        std::string team(static_cast<const Value &>(*rec)["team"].as_string());
        r.constructor_team[token] = team;
        if (team.rfind("game/", 0) == 0)
            continue;
        r.lines.push_back("team.constructor constructor/" + token + ": provided by " + team);
        r.constructors++;
    }
    for (auto &[id, rec] : pit_times) {
        std::string label = team_label((*rec)["team"], r);
        if (!label.empty())
            r.pit_record[label] = id;
    }
    return r;
}

struct ConstructorSeed {
    const char *key, *name, *country, *label;
};

inline constexpr ConstructorSeed CONSTRUCTOR_SEEDS[] = {
    {"mercedes", "Mercedes", "germany", "mercedes"},
    {"ferrari", "Ferrari", "italy", "ferrari"},
    {"red_bull", "Red Bull Racing", "austria", "redbull"},
    {"mclaren", "McLaren", "great_britain", "mclaren"},
    {"aston_martin", "Aston Martin", "great_britain", "astonmartin"},
    {"alpine", "Alpine", "france", "alpine"},
    {"williams", "Williams", "great_britain", "williams"},
    {"haas", "Haas", "united_states", "haas"},
    {"toro_rosso", "Scuderia Toro Rosso", "italy", "tororosso"},
    {"alpha_tauri", "AlphaTauri", "italy", "alphatauri"},
    {"racing_bulls", "Racing Bulls", "italy", "racingbulls"},
    {"sauber", "Sauber", "switzerland", "sauber"},
    {"alfa_romeo", "Alfa Romeo", "switzerland", "alfaromeo"},
    {"kick_sauber", "Kick Sauber", "switzerland", "kicksauber"},
    {"renault", "Renault", "france", "renault"},
    {"lotus", "Lotus", "great_britain", "lotus"},
    {"force_india", "Force India", "india", "forceindia"},
    {"racing_point", "Racing Point", "great_britain", "racingpoint"},
    {"manor", "Manor", "great_britain", "manor"},
    {"marussia", "Marussia", "russia", "marussia"},
    {"caterham", "Caterham", "malaysia", "caterham"},
    {"hrt", "HRT", "spain", "hrt"},
    {"virgin", "Virgin", "great_britain", "virgin"},
    {"toyota", "Toyota", "japan", "toyota"},
    {"honda", "Honda", "japan", "honda"},
    {"bmw_sauber", "BMW Sauber", "germany", "bmwsauber"},
    {"brawn", "Brawn", "great_britain", "brawn"},
    {"jordan", "Jordan", "ireland", "jordan"},
    {"minardi", "Minardi", "italy", "minardi"},
    {"tyrrell", "Tyrrell", "great_britain", "tyrrell"},
    {"benetton", "Benetton", "italy", "benetton"},
    {"arrows", "Arrows", "great_britain", "arrows"},
};

inline std::string constructor_label(const std::string &label)
{
    std::string out;
    for (char c : label)
        if (c != '_')
            out += char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    std::size_t n = out.size();
    while (n && out[n - 1] >= '0' && out[n - 1] <= '9')
        n--;
    if (n < out.size() && out.size() - n == 4)
        out.resize(n);
    return out;
}

inline int label_year(const std::string &label)
{
    std::size_t n = label.size();
    if (n < 5 || label[n - 5] != '_')
        return 0;
    int y = 0;
    for (std::size_t i = n - 4; i < n; i++) {
        if (label[i] < '0' || label[i] > '9')
            return 0;
        y = y * 10 + (label[i] - '0');
    }
    return y;
}

inline std::string colour_text(const std::string &signed_text)
{
    char *end = nullptr;
    long long n = std::strtoll(signed_text.c_str(), &end, 10);
    if (!end || end == signed_text.c_str() || *end)
        return {};
    uint32_t v = uint32_t(int32_t(n));
    char buf[16];
    if ((v >> 24) == 0xff)
        std::snprintf(buf, sizeof buf, "#%06X", v & 0xffffff);
    else
        std::snprintf(buf, sizeof buf, "#%08X", v);
    return buf;
}

inline std::string team_at(const std::string &label)
{
    return std::string(TEAM_ROW) + "[_metadata_=" + label + "]";
}

inline int64_t rows_of(const Read &read, const std::string &path)
{
    auto n = read(path);
    return n ? std::strtoll(n->c_str(), nullptr, 10) : 0;
}

struct StockTeam {
    std::string label;
    Value record;
};

inline std::vector<StockTeam> stock_teams(const Read &read)
{
    std::vector<StockTeam> out;
    int64_t rows = rows_of(read, std::string(TEAMS) + "._team_instancecount_");
    std::map<std::string, std::pair<int, std::size_t>> latest;
    for (int64_t i = 0; i < rows; i++) {
        std::string at = std::string(TEAMS) + ".team_" + std::to_string(i);
        auto label = read(at + "._metadata_");
        if (!label || label->empty())
            continue;
        auto field = [&](const char *f) { return read(at + "." + f); };
        Value rec = Value::object();
        if (auto sid = field("stat_id"); sid && !sid->empty()) {
            long long n = std::strtoll(sid->c_str(), nullptr, 10);
            if (n >= 0)
                rec["stat_id"] = Value(int64_t(n));
        }
        Value names = Value::object();
        auto literal = [&](const char *key, const char *f) {
            if (auto v = field(f); v && !v->empty())
                names[key] = Value::object({{"literal", Value(*v)}});
        };
        literal("title", "display_name");
        literal("short_name", "display_name_short");
        literal("super_short", "display_name_super_short");
        if (auto c = field("display_name_code"); c && !c->empty() && c->size() <= 4) {
            bool fine = true;
            for (char ch : *c)
                fine = fine && ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'));
            if (fine)
                names["code"] = Value(*c);
        }
        rec["names"] = std::move(names);
        Value colours = Value::object();
        for (auto [key, f] : {std::pair{"menu", "ui_team_colour"}, {"primary", "primary_colour"},
                              {"secondary", "secondary_colour"}, {"tertiary", "tertiary_colour"}})
            if (auto v = field(f))
                if (std::string t = colour_text(*v); !t.empty())
                    colours[key] = Value(t);
        rec["colours"] = std::move(colours);
        auto livery = field("livery_name");
        rec["livery_name"] = Value(livery && !livery->empty() ? *livery : *label);
        if (auto v = field("nationality"); v && !v->empty())
            rec["nationality"] = Value(*v);
        if (auto v = field("audio_id"); v && !v->empty())
            rec["audio_id"] = Value(*v);
        if (auto v = field("prestige"); v && !v->empty())
            rec["prestige"] = Value(int64_t(std::strtoll(v->c_str(), nullptr, 10)));
        std::string norm = constructor_label(*label);
        for (auto &seed : CONSTRUCTOR_SEEDS)
            if (norm == seed.label) {
                auto it = latest.find(norm);
                int year = label_year(*label);
                if (it == latest.end() || year >= it->second.first)
                    latest[norm] = {year, out.size()};
                break;
            }
        out.push_back({*label, std::move(rec)});
    }
    for (auto &[norm, at] : latest)
        for (auto &seed : CONSTRUCTOR_SEEDS)
            if (norm == seed.label)
                out[at.second].record["provides"] = Value::array({Value(std::string("constructor/") + seed.key)});
    return out;
}

struct SideRow {
    std::string table, path;
    bool found = false;
};

inline std::vector<SideRow> side_rows(const SideTable &s, const Read &read, const std::string &team)
{
    std::vector<SideRow> out;
    int64_t groups = s.group_row ? rows_of(read, std::string(s.group) + "._" + s.group_row + "_instancecount_") : 1;
    for (int64_t g = 0; g < groups; g++) {
        std::string table = s.group_row ? std::string(s.group) + "." + s.group_row + "_" + std::to_string(g) + "." + s.sub
                                        : std::string(s.group);
        int64_t rows = rows_of(read, table + "._" + s.row + "_instancecount_");
        SideRow hit{table, {}, false};
        for (int64_t i = 0; i < rows; i++) {
            std::string path = table + "." + s.row + "_" + std::to_string(i);
            if (read(path + ".team") == team) {
                hit.path = path;
                hit.found = true;
                break;
            }
        }
        out.push_back(std::move(hit));
    }
    return out;
}

inline std::size_t copy_side(Plan &plan, const SideTable &s, const Read &read, const std::string &from,
                             const std::string &to)
{
    std::size_t copied = 0;
    auto exists = side_rows(s, read, to);
    std::vector<SideRow> sources = side_rows(s, read, from);
    for (std::size_t g = 0; g < sources.size(); g++) {
        if (!sources[g].found || (g < exists.size() && exists[g].found))
            continue;
        push_row(plan, sources[g].table, sources[g].path,
                 Value::object({{"_metadata_", to}, {"team", to}}));
        copied++;
    }
    return copied;
}

inline constexpr const char *LOGO_CONTEXTS[] = {"driver_select", "team_select", "hud", "broadcast", "hub"};

struct LogoArray {
    const char *array;
    bool tower;
};

inline std::vector<LogoArray> logo_arrays(const std::string &context)
{
    if (context == "driver_select" || context == "hub")
        return {{"teams.driver_select_team_logos", false}};
    if (context == "team_select")
        return {{"team_logos.driver_career_2024_team_select", false}, {"teams.mp_team_select_logos", false}};
    if (context == "hud")
        return {{"osd.timing_tower.team_badges_hud", true}};
    return {{"osd.timing_tower.team_badges_broadcast", true},
            {"teams.badges_broadcast", false},
            {"broadcast_ui_generic.grid_team_logos", false}};
}

inline bool has_file(const Value &logo, const char *context)
{
    return logo["images"][context].is_string();
}

inline bool has_any_file(const Value *logo)
{
    if (!logo)
        return false;
    for (const char *context : LOGO_CONTEXTS)
        if (has_file(*logo, context))
            return true;
    return false;
}

// A context the record gives no image takes the team's own image of another context, scaled to each array by the
// build: the timing tower arrays look at the other tower context first, every other array at the menu logos first.
inline const char *own_image_for(const Value *logo, const std::string &context, bool tower)
{
    static constexpr const char *MENU_FIRST[] = {"driver_select", "hub", "team_select", "broadcast", "hud"};
    static constexpr const char *TOWER_FIRST[] = {"hud", "broadcast", "driver_select", "hub", "team_select"};
    if (!logo)
        return nullptr;
    for (const char *from : tower ? TOWER_FIRST : MENU_FIRST)
        if (context != from && has_file(*logo, from))
            return from;
    return nullptr;
}

inline std::string stock_key(const Value &logo, const char *context)
{
    const Value &v = logo["images"][context];
    return v.is_object() && v["stock"].is_string() ? std::string(v["stock"].as_string()) : std::string();
}

inline void lower_logo_images(const std::string &team, const std::string &donor, const std::string &logo_id,
                              const Value *logo, const std::string &tower_key, Plan &plan)
{
    std::string addon = addon_of(logo_id);
    std::set<std::string> taken;
    std::set<std::string> fell_back;
    std::set<std::string> borrowed;
    for (const char *context : LOGO_CONTEXTS) {
        const Value *v = logo ? &(*logo)["images"][context] : nullptr;
        bool own = v && (v->is_string() || (v->is_object() && (*v)["stock"].is_string()));
        for (auto &a : logo_arrays(context)) {
            bool own_file = own && v->is_string();
            const char *from = nullptr;
            if (a.tower) {
                if (tower_key != team || !(own_file || (from = own_image_for(logo, context, true))))
                    continue;
            } else if (!own && !(from = own_image_for(logo, context, false)) && donor.empty()) {
                continue;
            }
            std::string key = a.tower ? tower_key : team;
            if (!taken.insert(std::string(a.array) + "|" + key).second)
                continue;
            LogoImage img;
            img.array = a.array;
            img.key = key;
            img.context = context;
            img.addon = addon;
            img.id = logo_id;
            img.team = team;
            if (from) {
                img.file = std::string((*logo)["images"][from].as_string());
                if (borrowed.insert(std::string(from) + "|" + context).second)
                    plan.lines.push_back("team.logo: " + team + " uses its " + from + " image for " + context);
            } else if (!own) {
                img.stock = donor;
                img.fallback = true;
                if (fell_back.insert(context).second)
                    plan.lines.push_back("team.logo: " + team + " uses " + donor + "'s " + context + " logo");
            } else if (v->is_string())
                img.file = std::string(v->as_string());
            else
                img.stock = std::string((*v)["stock"].as_string());
            plan.logos.push_back(std::move(img));
        }
    }
}

inline void lower_team(const std::string &id, const Value &rec, const Lookup &lookup, const Read &read,
                       const TeamRefs &refs, Plan &plan, const StatIds &stat = {})
{
    plan.owner = addon_of(id);
    std::string label = key_of(id);
    if (!rec["donor"].is_string()) {
        plan.lines.push_back("team.identity " + id + ": no donor; nothing applied");
        plan.refused++;
        return;
    }
    std::string donor = team_label(rec["donor"], refs);
    if (donor.empty() || !read(team_at(donor) + "._metadata_")) {
        plan.lines.push_back("team.identity " + id + ": the game database has no team row " +
                             (donor.empty() ? std::string(rec["donor"].as_string()) : donor) + " to copy; nothing applied");
        plan.refused++;
        return;
    }
    if (read(team_at(label) + "._metadata_")) {
        plan.lines.push_back("team.identity " + id + ": a team row named " + label + " exists; nothing applied");
        plan.refused++;
        return;
    }
    Value set = Value::object({{"_metadata_", label}});
    std::vector<std::string> skipped;
    auto put = [&](const char *field, std::string value) {
        if (value.empty())
            return;
        if (read(team_at(donor) + "." + field))
            set[field] = Value(std::move(value));
        else
            skipped.push_back(field);
    };
    const Value &names = rec["names"];
    put("display_name", title_text(names["title"]));
    put("display_name_short", title_text(names["short_name"]));
    put("display_name_super_short", title_text(names["super_short"]));
    put("display_name_code", names["code"].is_string() ? std::string(names["code"].as_string()) : std::string());
    put("livery_name", rec["livery_name"].is_string() ? std::string(rec["livery_name"].as_string()) : std::string());
    put("nationality", rec["nationality"].is_string() ? std::string(rec["nationality"].as_string()) : std::string());
    put("audio_id", rec["audio_id"].is_string() ? std::string(rec["audio_id"].as_string()) : std::string());
    put("prestige", rec["prestige"].is_int() ? std::to_string(rec["prestige"].as_int()) : std::string());
    const Value &c = rec["colours"];
    auto put_colour = [&](const char *field, const Value &v) {
        if (auto n = argb(v))
            put(field, std::to_string(*n));
    };
    put_colour("ui_team_colour", c["menu"]);
    put_colour("primary_colour", c["primary"]);
    put_colour("secondary_colour", c["secondary"]);
    put_colour("tertiary_colour", c["tertiary"]);
    const Value &w = rec["wear"].is_object() ? rec["wear"] : c;
    put_colour("team_wear_primary_colour", w["primary"]);
    put_colour("team_wear_secondary_colour", w["secondary"]);
    put_colour("team_wear_tertiary_colour", w["tertiary"]);
    auto sid = stat_of(stat, TEAM_IDS, id, pin_of(rec));
    if (sid)
        set["stat_id"] = Value(std::to_string(*sid));
    const Value *logo = rec["logo"].is_string() ? lookup("team.logo", std::string(rec["logo"].as_string())) : nullptr;
    bool tower_image = logo && (has_file(*logo, "hud") || has_file(*logo, "broadcast"));
    std::string tower_stock;
    if (logo && !tower_image)
        tower_stock = !stock_key(*logo, "hud").empty() ? stock_key(*logo, "hud") : stock_key(*logo, "broadcast");
    tower_image = tower_image || (tower_stock.empty() && has_any_file(logo));
    std::string tower_key = tower_image ? label : tower_stock;
    put("timing_tower_texture_path", tower_key);
    push_row(plan, TEAMS, team_at(donor), std::move(set));
    std::size_t sides = 0;
    bool own_pit = refs.pit_record.count(label) != 0;
    for (auto &s : SIDE_TABLES) {
        if (s.pit && own_pit)
            continue;
        sides += copy_side(plan, s, read, donor, label);
    }
    if (rec["logo"].is_string() && !logo)
        plan.lines.push_back("team.identity " + id + ": its logo " + std::string(rec["logo"].as_string()) +
                             " is not a team.logo record; no logo");
    lower_logo_images(label, donor, rec["logo"].is_string() ? std::string(rec["logo"].as_string()) : id, logo, tower_key, plan);
    for (auto &f : skipped)
        plan.lines.push_back("team.identity " + id + ": " + donor + " has no " + f + "; left out");
    plan.lines.push_back("team.identity " + id + ": row " + label + " from " + donor +
                         (sid ? ", stat id " + std::to_string(*sid) : std::string()) + ", " + std::to_string(sides) +
                         " side row(s)" + (tower_image ? std::string(", own timing tower badge key")
                          : tower_stock.empty() ? std::string() : ", timing tower badge " + tower_stock));
}

inline void lower_vehicle_team(const Value &rec, const Read &read, Plan &plan)
{
    if (!rec["team_from"].is_string() || !rec["label"].is_string())
        return;
    std::string from(rec["team_from"].as_string()), label(rec["label"].as_string());
    if (from == label || !read(team_at(from) + "._metadata_") || read(team_at(label) + "._metadata_"))
        return;
    push_row(plan, TEAMS, team_at(from), Value::object({{"_metadata_", label}}));
    std::size_t sides = 0;
    for (auto &s : SIDE_TABLES)
        sides += copy_side(plan, s, read, from, label);
    std::set<std::string> taken;
    for (const char *context : LOGO_CONTEXTS)
        for (auto &a : logo_arrays(context)) {
            if (a.tower || !taken.insert(a.array).second)
                continue;
            LogoImage img;
            img.array = a.array;
            img.key = label;
            img.context = context;
            img.id = label;
            img.team = label;
            img.stock = from;
            img.fallback = true;
            plan.logos.push_back(std::move(img));
        }
    plan.lines.push_back("vehicle team " + label + ": row copied from " + from + " with " + std::to_string(sides) +
                         " side row(s)");
}

inline void lower_pit_times(const std::string &id, const Value &rec, const Lookup &lookup, const Read &read,
                            const TeamRefs &refs, Plan &plan)
{
    plan.owner = addon_of(id);
    std::string team = team_label(rec["team"], refs);
    if (team.empty()) {
        plan.lines.push_back("team.pit_times " + id + ": no team; nothing applied");
        plan.refused++;
        return;
    }
    std::string from;
    if (rec["from"].is_string()) {
        const Value *src = lookup("team.pit_times", std::string(rec["from"].as_string()));
        from = src && (*src)["team"].is_string() ? team_label((*src)["team"], refs) : key_of(rec["from"].as_string());
    } else if (const Value *ident = rec["team"].is_string() ? lookup(TEAM_IDS, std::string(rec["team"].as_string())) : nullptr) {
        if ((*ident)["donor"].is_string())
            from = team_label((*ident)["donor"], refs);
    }
    static constexpr const char *FIELDS[] = {"min_pit_time", "max_pit_time", "min_pit_time_fully_upgraded",
                                              "max_pit_time_fully_upgraded"};
    SideTable table = SIDE_TABLES[0];
    auto have = side_rows(table, read, team);
    Value set = Value::object({{"_metadata_", team}, {"team", team}});
    std::size_t edits = 0;
    for (const char *f : FIELDS)
        if (rec[f].is_number()) {
            set[f] = Value(text(rec[f]));
            edits++;
        }
    if (have[0].found) {
        for (const char *f : FIELDS)
            if (rec[f].is_number())
                push_op(plan, set_op(have[0].path, f, text(rec[f])));
        plan.lines.push_back("team.pit_times " + id + ": " + std::to_string(edits) + " value(s) set on the row of " + team);
        return;
    }
    if (from.empty() || !side_rows(table, read, from)[0].found) {
        plan.lines.push_back("team.pit_times " + id + ": no pit times row of " + (from.empty() ? std::string("(no source)") : from) +
                             " to copy; nothing applied");
        plan.refused++;
        return;
    }
    auto src = side_rows(table, read, from)[0];
    push_row(plan, src.table, src.path, std::move(set));
    plan.lines.push_back("team.pit_times " + id + ": row for " + team + " from " + from +
                         (edits ? ", " + std::to_string(edits) + " value(s) set" : std::string()));
}

struct Dress {
    std::string title, short_name, super_short, code;
    std::optional<int32_t> primary, secondary, tertiary, wear_primary, wear_secondary, wear_tertiary, menu;
    std::string tower_key;
};

inline Dress dress_of(const Value &rec)
{
    Dress d;
    d.title = title_text(rec["names"]["title"]);
    d.short_name = title_text(rec["names"]["short_name"]);
    d.super_short = title_text(rec["names"]["super_short"]);
    if (rec["names"]["code"].is_string())
        d.code = std::string(rec["names"]["code"].as_string());
    const Value &c = rec["colours"];
    d.menu = argb(c["menu"]);
    d.primary = argb(c["primary"]);
    d.secondary = argb(c["secondary"]);
    d.tertiary = argb(c["tertiary"]);
    const Value &w = rec["wear"].is_object() ? rec["wear"] : c;
    d.wear_primary = argb(w["primary"]);
    d.wear_secondary = argb(w["secondary"]);
    d.wear_tertiary = argb(w["tertiary"]);
    return d;
}

namespace team_record {
inline constexpr std::size_t DISPLAY_NAME = 0x0b0, CODE = 0x148, SHORT = 0x1e0, SUPER_SHORT = 0x278,
                             PRIMARY = 0x564, SECONDARY = 0x568, TERTIARY = 0x56c, WEAR_PRIMARY = 0x570,
                             WEAR_SECONDARY = 0x574, WEAR_TERTIARY = 0x578, MENU = 0x57c, HOLDER_CAPACITY = 0x80;
}

inline void write_holder(uint8_t *record, std::size_t at, const std::string &text)
{
    if (text.empty())
        return;
    char *ptr;
    std::memcpy(&ptr, record + at, sizeof ptr);
    if (!ptr)
        return;
    std::size_t n = std::min(text.size(), team_record::HOLDER_CAPACITY - 1);
    std::memcpy(ptr, text.data(), n);
    ptr[n] = 0;
}

inline void write_colour(uint8_t *record, std::size_t at, const std::optional<int32_t> &v)
{
    if (v)
        std::memcpy(record + at, &*v, sizeof(int32_t));
}

inline void write_dress(uint8_t *record, const Dress &d)
{
    using namespace team_record;
    write_holder(record, DISPLAY_NAME, d.title);
    write_holder(record, SHORT, d.short_name);
    write_holder(record, SUPER_SHORT, d.super_short);
    write_holder(record, CODE, d.code);
    write_colour(record, PRIMARY, d.primary);
    write_colour(record, SECONDARY, d.secondary);
    write_colour(record, TERTIARY, d.tertiary);
    write_colour(record, WEAR_PRIMARY, d.wear_primary);
    write_colour(record, WEAR_SECONDARY, d.wear_secondary);
    write_colour(record, WEAR_TERTIARY, d.wear_tertiary);
    write_colour(record, MENU, d.menu);
}

struct Pending {
    std::string group, row, team;
    int slot = 0;
};

struct Live {
    std::function<const Value *(const std::string &id)> identity;
    std::function<int64_t(const std::string &id)> stat_of;
    std::function<uint8_t *(uint16_t stat)> record;
    std::function<void(const std::string &line)> log;
    std::vector<Pending> pending;
    std::map<uint16_t, std::string> wears;
    std::map<std::string, std::string> logo_override;
    std::vector<LogoImage> manifest;
};

inline Live &live()
{
    static Live l;
    return l;
}

inline std::recursive_mutex &live_mutex()
{
    static std::recursive_mutex m;
    return m;
}

inline bool dress_slot(const char *group, int slot, const char *row, const char *team, int64_t row_stat)
{
    std::lock_guard g(live_mutex());
    Live &l = live();
    auto say = [&l](const std::string &line) {
        if (l.log)
            l.log(line);
    };
    std::string where = std::string("dress ") + (group ? group : "") + " slot " + std::to_string(slot) + ": " +
                        (row ? row : "") + " as " + (team ? team : "");
    if (!row || !team || !l.identity || !l.stat_of) {
        say(where + ": the team applier is not set up");
        return false;
    }
    int64_t stat = row_stat >= 0 ? row_stat : l.stat_of(row);
    bool own = std::string(row) == team;
    if (stat < 0 || stat > 0xffff) {
        if (!own)
            say(where + ": the slot row has no stat id from the install's store");
        return own;
    }
    if (own && !l.wears.count(uint16_t(stat)))
        return true;
    const Value *rec = l.identity(team);
    if (!rec) {
        say(where + ": " + team + " is not a team.identity record");
        return false;
    }
    auto worn = l.wears.find(uint16_t(stat));
    bool again = !own && worn != l.wears.end() && worn->second == team;
    if (own)
        l.wears.erase(uint16_t(stat));
    else
        l.wears[uint16_t(stat)] = team;
    uint8_t *record = l.record ? l.record(uint16_t(stat)) : nullptr;
    if (!record) {
        l.pending.push_back({group ? group : "", row, team, slot});
        say(where + ": queued; the loaded team record (stat id " + std::to_string(stat) + ") is not reachable yet");
        return false;
    }
    write_dress(record, dress_of(*rec));
    say(where + (again ? ": already worn; names and colours written again to the loaded record of stat id "
                       : ": names and colours written to the loaded record of stat id ") +
        std::to_string(stat));
    return true;
}

inline bool redress(uint16_t stat, uint8_t *record)
{
    std::lock_guard g(live_mutex());
    Live &l = live();
    auto it = l.wears.find(stat);
    if (it == l.wears.end() || !l.identity || !record)
        return false;
    const Value *rec = l.identity(it->second);
    if (!rec)
        return false;
    write_dress(record, dress_of(*rec));
    if (l.log)
        l.log("redress stat id " + std::to_string(stat) + ": the loader stored its record again; dressed as " + it->second);
    return true;
}

}
