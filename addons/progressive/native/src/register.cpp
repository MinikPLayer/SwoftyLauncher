// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career on the registry (standard library 1.6). SWOFTY_REGISTER runs at the early stage,
// reads data/years.json through the addon's storage and adds:
//   career.season          one record per year, key "2017" (swofty.progressive/2017): the data group, the
//                          year's box (Classic Seasons' box of that year with game/f1_2025 as the
//                          fallback, the stock box otherwise), the line-up by career slot and the real
//                          mid-season changes with their kind (swap, stand_in, move, replace);
//   career.driver_ratings  one record per driver of each year, key "2017/game/lewis_hamilton": the four
//                          ratings the schema holds, as years.json has them.
// What the typed fields can't hold of a season's own data rides in the record's extra under
// "progressive". A season keeps the rest of each team ("teams", keyed "slot=1") and of each change
// ("changes", keyed "round=15,slot=7,seat=1"), the season's own lists as they are, its slice of the
// driver pool ("pool": slot, look and number that year) and its ratings source. A rating keeps focus,
// overall, official_overall and source. years.json's addon-wide fields (slots, stat ids, logo keys, the
// pool's per-driver constants, the ratings model) stay private config and go in no record.
// Drivers the stock game has are game/<label>, the others person/<label>; teams are constructor ids made
// from their real name, and a slot with no real team that year is empty. The other DLLs still read
// years.json; this file only registers.

#include <charconv>
#include <initializer_list>
#include <set>
#include <string>
#include <string_view>

#include <swofty/registry.hpp>
#include <swofty/storage.hpp>
#include <swofty/systems/career.hpp>

namespace {

namespace json = swofty::json;
namespace career = swofty::career;

using DriverRef = swofty::Ref<swofty::drivers::Identity>;
using TeamRef = swofty::Ref<swofty::teams::Identity>;
using BoxRef = swofty::Ref<swofty::content::Package>;

constexpr const char *kYearsPath = "data/years.json";
constexpr const char *kAddon = "swofty.progressive";
constexpr const char *kFallbackBox = "game/f1_2025";

struct Counts {
    int seasons = 0, ratings = 0, refused = 0;
    std::string first_refusal;
};

json::Value without(json::Value v, std::initializer_list<std::string_view> keys)
{
    for (std::string_view k : keys)
        v.erase(k);
    return v;
}

std::string slug(std::string_view name)
{
    std::string out;
    for (char c : name) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
        bool keep = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (keep)
            out += c;
        else if (!out.empty() && out.back() != '_')
            out += '_';
    }
    while (!out.empty() && out.back() == '_')
        out.pop_back();
    return out;
}

DriverRef driver_ref(const json::Value &label, const std::set<std::string, std::less<>> &stock)
{
    const std::string *s = label.if_string();
    if (!s || s->empty())
        return {};
    if (stock.contains(*s))
        return DriverRef("game/" + *s);
    return DriverRef("person/" + *s + "|game/" + *s);
}

std::string constructor_key(std::string_view name)
{
    std::string key = slug(name);
    if (key == "red_bull_racing")
        return "red_bull";
    if (key == "rb")
        return "racing_bulls";
    return key;
}

TeamRef team_ref(const json::Value &team, const json::Value &slot_rows)
{
    if (team["kind"].as_string() != "real")
        return {};
    std::string key = constructor_key(team["name"].as_string());
    if (key.empty())
        return {};
    std::string chain;
    for (const json::Value *row : {&team["cs_team"], &team["stock_team"]})
        if (row->is_string() && !row->as_string().empty())
            chain += "game/" + std::string(row->as_string()) + "|";
    chain += "constructor/" + key;
    if (const json::Value *row = slot_rows.find(std::to_string(team["slot"].as_int())); row && row->is_string())
        chain += "|game/" + std::string(row->as_string());
    return TeamRef(chain + "?");
}

json::Value stock_rows(const json::Value &doc)
{
    json::Value rows = json::Value::object();
    if (const json::Array *teams = doc["seasons"]["2025"]["teams"].if_array())
        for (const json::Value &t : *teams)
            if (t["stock_team"].is_string())
                rows[std::to_string(t["slot"].as_int())] = t["stock_team"];
    return rows;
}

BoxRef box_ref(int year, const json::Value &season)
{
    const std::string own = "classic-seasons-" + std::to_string(year);
    bool classic = false;
    if (const json::Array *teams = season["teams"].if_array())
        for (const json::Value &t : *teams)
            classic = classic || t["vehicle"]["source"].as_string() == own;
    if (!classic)
        return BoxRef("game/f1_" + std::to_string(year));
    return BoxRef("swofty.classic-seasons-" + std::to_string(year) + "/f1_" + std::to_string(year))
        .or_else(BoxRef(kFallbackBox));
}

float rating(const json::Value &v)
{
    return static_cast<float>(v.as_double());
}

career::ChangeKind change_kind(std::string_view kind)
{
    if (kind == "stand_in")
        return career::ChangeKind::stand_in;
    if (kind == "move")
        return career::ChangeKind::move;
    if (kind == "replace")
        return career::ChangeKind::replace;
    return career::ChangeKind::swap;
}

bool known_kind(std::string_view kind)
{
    return kind == "swap" || kind == "stand_in" || kind == "move" || kind == "replace";
}

void refused(Counts &n, const std::string &what)
{
    ++n.refused;
    if (n.first_refusal.empty()) {
        std::string why = swofty::last_error();
        n.first_refusal = what + (why.empty() ? "" : ": " + why);
    }
}

json::Value pool_slice(const json::Value &doc, std::string_view year)
{
    json::Value slice = json::Value::object();
    const json::Array *pool = doc["pool"].if_array();
    if (!pool)
        return slice;
    for (const json::Value &p : *pool) {
        json::Value one = json::Value::object();
        if (const json::Value *v = p["years"].find(year))
            one["slot"] = *v;
        if (const json::Value *v = p["looks"].find(year))
            one["look"] = *v;
        if (const json::Value *v = p["numbers"].find(year))
            one["number"] = *v;
        if (one.size() && p["driver"].is_string())
            slice[p["driver"].as_string()] = std::move(one);
    }
    return slice;
}

career::SeasonTeam season_team(const json::Value &t, const std::set<std::string, std::less<>> &stock,
                               const json::Value &slot_rows, json::Value &rest)
{
    career::SeasonTeam team{.slot = static_cast<int>(t["slot"].as_int()), .team = team_ref(t, slot_rows)};
    rest = without(t, {"slot"});
    json::Value drivers = json::Value::array();
    bool taken[2] = {false, false};
    if (const json::Array *list = t["drivers"].if_array())
        for (const json::Value &d : *list) {
            json::Value entry = d;
            const json::Value &pos = d["position"];
            int seat = pos.is_int() ? static_cast<int>(pos.as_int()) : -1;
            if (seat >= 0 && seat < 2 && !taken[seat] && d["driver"].is_string()) {
                taken[seat] = true;
                team.seats[static_cast<size_t>(seat)] = driver_ref(d["driver"], stock);
                entry.erase("driver");
            }
            drivers.push_back(std::move(entry));
        }
    if (t.contains("drivers"))
        rest["drivers"] = std::move(drivers);
    return team;
}

career::SeasonChange season_change(const json::Value &c, const std::set<std::string, std::less<>> &stock)
{
    career::SeasonChange change{
        .round = static_cast<int>(c["round"].as_int()),
        .slot = static_cast<int>(c["slot"].as_int()),
        .seat = static_cast<int>(c["seat"].as_int()),
        .out = driver_ref(c["out"], stock),
        .in = driver_ref(c["in"], stock),
        .kind = change_kind(c["kind"].as_string()),
    };
    if (c["until_round"].is_int())
        change.until_round = static_cast<int>(c["until_round"].as_int());
    return change;
}

std::string change_key(const career::SeasonChange &c)
{
    return "round=" + std::to_string(c.round) + ",slot=" + std::to_string(c.slot) + ",seat=" + std::to_string(c.seat);
}

career::Season season_record(const json::Value &doc, std::string_view key, int year, const json::Value &s,
                             const std::set<std::string, std::less<>> &stock)
{
    career::Season season{
        .data_group = std::string(doc["data_group"].as_string()),
        .year = year,
        .box = box_ref(year, s),
    };
    json::Value extra = without(s, {"teams", "changes"});
    const json::Value rows = stock_rows(doc);
    json::Value teams = json::Value::object();
    if (const json::Array *list = s["teams"].if_array())
        for (const json::Value &t : *list) {
            json::Value rest;
            career::SeasonTeam team = season_team(t, stock, rows, rest);
            teams["slot=" + std::to_string(team.slot)] = std::move(rest);
            season.teams.push_back(std::move(team));
        }
    extra["teams"] = std::move(teams);
    if (s.contains("changes")) {
        json::Value changes = json::Value::object();
        if (const json::Array *list = s["changes"].if_array())
            for (const json::Value &c : *list) {
                career::SeasonChange change = season_change(c, stock);
                json::Value rest = without(c, {"round", "slot", "seat", "out", "in"});
                if (change.until_round)
                    rest.erase("until_round");
                if (known_kind(c["kind"].as_string()))
                    rest.erase("kind");
                changes[change_key(change)] = std::move(rest);
                season.changes.push_back(std::move(change));
            }
        extra["changes"] = std::move(changes);
    }
    extra["pool"] = pool_slice(doc, key);
    if (const json::Value *source = doc["ratings"]["sources"].find(key))
        extra["ratings_source"] = *source;
    season.extra = json::Value::object({{"progressive", std::move(extra)}});
    return season;
}

void add_ratings(const json::Value &doc, std::string_view key, swofty::Ref<career::Season> season,
                 const std::set<std::string, std::less<>> &stock, Counts &n)
{
    const json::Object *drivers = doc["ratings"]["years"][key].if_object();
    if (!drivers)
        return;
    for (const auto &[label, v] : *drivers) {
        DriverRef driver = driver_ref(json::Value(label), stock);
        career::DriverRatings r{
            .season = season,
            .driver = driver,
            .experience = rating(v["experience"]),
            .racecraft = rating(v["racecraft"]),
            .awareness = rating(v["awareness"]),
            .pace = rating(v["pace"]),
        };
        json::Value rest = v;
        for (std::string_view k : {"experience", "racecraft", "awareness", "pace"})
            if (v[k].is_number())
                rest.erase(k);
        r.extra = json::Value::object({{"progressive", std::move(rest)}});
        std::string person(driver.id());
        std::string rkey = std::string(key) + "/" + person.substr(0, person.find('|'));
        if (career::ratings.add(rkey, std::move(r)).empty())
            refused(n, "career.driver_ratings " + rkey);
        else
            ++n.ratings;
    }
}

}

SWOFTY_REGISTER
{
    std::string text;
    if (swofty::storage::Result r = swofty::storage::read(kYearsPath, text); r != swofty::storage::Result::ok) {
        swofty::log("[progressive] register: %s not read (storage %d); no career.season records", kYearsPath,
                    static_cast<int>(r));
        return;
    }
    json::Error err;
    const json::Value doc = json::parse(text, &err);
    if (!err.ok() || !doc["seasons"].is_object()) {
        swofty::log("[progressive] register: %s is not years.json (%s); no career.season records", kYearsPath,
                    err.ok() ? "no seasons" : err.message.c_str());
        return;
    }
    std::set<std::string, std::less<>> stock;
    if (const json::Array *pool = doc["pool"].if_array())
        for (const json::Value &p : *pool)
            if (p["source"].as_string() == "stock" && p["driver"].is_string())
                stock.emplace(p["driver"].as_string());
    Counts n;
    for (const auto &[key, s] : *doc["seasons"].if_object()) {
        int year = 0;
        auto [end, ec] = std::from_chars(key.data(), key.data() + key.size(), year);
        if (ec != std::errc() || end != key.data() + key.size()) {
            refused(n, "career.season " + key + " (not a year)");
            continue;
        }
        swofty::Ref<career::Season> season = career::seasons.add(key, season_record(doc, key, year, s, stock));
        if (season.empty()) {
            refused(n, "career.season " + key);
            season = career::seasons[std::string(kAddon) + "/" + key];
        } else {
            ++n.seasons;
        }
        add_ratings(doc, key, season, stock, n);
    }
    swofty::log("[progressive] register: %s -> %d career.season, %d career.driver_ratings, %d refused%s%s",
                kYearsPath, n.seasons, n.ratings, n.refused, n.first_refusal.empty() ? "" : "; first: ",
                n.first_refusal.c_str());
}

extern "C" SWOFTY_EXPORT int SwoftyAddonRegister(const SwoftyApi *a)
{
    return swofty::detail::run_register(a);
}
