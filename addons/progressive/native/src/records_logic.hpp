// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career: years.json with the effective registry records in it (standard library 1.6).
// register.cpp registers each year's career.season and each driver's career.driver_ratings from
// data/years.json; other addons layer them. Every reader of years.json (season.cpp, ratings.cpp,
// ui/wizard_ratings.cpp) parses the document overlay() returns, so a layer reaches the seats, the
// mid-season changes and the ratings the career applies and Choose Driver shows. What the records
// hold is taken from them; the rest of years.json (the career slots, the pool, the looks, numbers,
// vehicles, the ratings model, the logo keys) stays as it is.
//
//   seats     a record seat whose driver differs from years.json gets an entry of its own (driver and
//             position only): his number and look come from the pool for that year. A driver a record
//             seats in another seat of the same year leaves the seat years.json gives him, which the
//             planner fills from the free agents. An empty record seat empties the seat.
//   teams     a record team that names a team of its own (not the constructor chain register.cpp
//             registers) is written as the slot's `team` with the record's seat ids as its `seats`; a slot 12
//             (a twelfth team) is added to the year with that team and its seats. season.cpp hands them to the
//             core's apply_teams, and the planner keeps each of that slot's drivers in the record's seat.
//   changes   matched by (round, slot, seat): out, in, kind and until_round from the record, the rest
//             (circuit, track, number, look) kept; a change only years.json has is dropped; a change
//             only the record has is added with no track, so it is placed by its round.
//   ratings   experience, racecraft, awareness and pace from the record; when one differs the
//             overall is the ratings model's weighted mean of the five values.
// A record that is absent (no registry, or a season or rating register.cpp did not add) changes nothing.
// Pure logic over swofty::json and the SDK record types, so native/test builds it on the host.

#pragma once
#include <cmath>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <swofty/json.hpp>
#include <swofty/systems/career.hpp>

namespace progressive::records {

namespace json = swofty::json;
namespace career = swofty::career;

using Stock = std::set<std::string, std::less<>>;

inline constexpr const char *kAddon = "swofty.progressive";
inline constexpr std::string_view kRatingFields[] = {"experience", "racecraft", "awareness", "pace"};

struct Summary {
    int seasons = 0, ratings = 0;
    int seats = 0, emptied = 0, changes = 0, ratings_changed = 0, teams = 0;
    std::vector<std::string> notes;
    bool changed() const { return seats || emptied || changes || ratings_changed || teams; }
};

inline Stock stock_drivers(const json::Value &doc)
{
    Stock stock;
    if (const json::Array *pool = doc["pool"].if_array())
        for (const json::Value &p : *pool)
            if (p["source"].as_string() == "stock" && p["driver"].is_string())
                stock.emplace(p["driver"].as_string());
    return stock;
}

inline std::string driver_id(std::string_view label, const Stock &stock)
{
    return (stock.contains(label) ? "game/" : "person/") + std::string(label);
}

inline std::string label_of(std::string_view id)
{
    size_t slash = id.find('/');
    return std::string(slash == std::string_view::npos ? id : id.substr(slash + 1));
}

inline std::string season_id(std::string_view year)
{
    return std::string(kAddon) + "/" + std::string(year);
}

inline std::string rating_id(std::string_view year, std::string_view label, const Stock &stock)
{
    return std::string(kAddon) + "/" + std::string(year) + "/" + driver_id(label, stock);
}

inline int seat_of(const json::Value &d)
{
    const json::Value &pos = d["position"];
    return pos.is_int() ? static_cast<int>(pos.as_int()) : -1;
}

inline json::Value *team_of(json::Value &season, int slot)
{
    json::Value *teams = season.find("teams");
    if (json::Array *list = teams ? teams->if_array() : nullptr)
        for (json::Value &t : *list)
            if (std::as_const(t)["slot"].is_int() && std::as_const(t)["slot"].as_int() == slot)
                return &t;
    return nullptr;
}

inline void unseat_elsewhere(json::Value &season, const std::string &label, int slot, int seat, Summary &n,
                             const std::string &year)
{
    json::Value *list = season.find("teams");
    json::Array *teams = list ? list->if_array() : nullptr;
    if (!teams)
        return;
    for (json::Value &t : *teams) {
        json::Value *field = t.find("drivers");
        json::Array *drivers = field ? field->if_array() : nullptr;
        if (!drivers)
            continue;
        int s = static_cast<int>(std::as_const(t)["slot"].as_int());
        for (size_t i = 0; i < drivers->size();) {
            const json::Value &d = (*drivers)[i];
            int k = seat_of(d);
            if (d["driver"].as_string() == label && !(s == slot && k == seat) && k >= 0 && k < 2) {
                n.notes.push_back(year + " " + label + " leaves slot " + std::to_string(s) + " seat " +
                                  std::to_string(k + 1) + " (a record seats him in slot " + std::to_string(slot) +
                                  " seat " + std::to_string(seat + 1) + ")");
                drivers->erase(drivers->begin() + static_cast<std::ptrdiff_t>(i));
                ++n.emptied;
                continue;
            }
            ++i;
        }
    }
}

inline constexpr int kCareerSlots = 11;
inline constexpr int kMaxSlots = 12;

inline bool is_registered_team(const json::Value &doc, const json::Value &t, std::string_view name, int slot)
{
    if (name.rfind("constructor/", 0) == 0)
        return true;
    for (const json::Value *row : {&t["cs_team"], &t["stock_team"], &t["team"], &doc["slots"][static_cast<size_t>(slot - 1)]["team"]})
        if (row->is_string() && row->as_string() == name)
            return true;
    return false;
}

inline std::string team_name(std::string_view id)
{
    return std::string(id.rfind("game/", 0) == 0 ? id.substr(5) : id);
}

inline json::Value seat_ids(const career::SeasonTeam &rt)
{
    size_t last = 0;
    for (size_t k = 0; k < rt.seats.size(); ++k)
        if (!rt.seats[k].empty())
            last = k + 1;
    json::Value ids = json::Value::array();
    for (size_t k = 0; k < last; ++k)
        ids.push_back(rt.seats[k].empty() ? std::string() : std::string(rt.seats[k].id()));
    return ids;
}

inline void overlay_teams(const json::Value &doc, json::Value &season, const career::Season &s, Summary &n, const std::string &year)
{
    for (const career::SeasonTeam &rt : s.teams) {
        if (rt.slot < 1 || rt.slot > kMaxSlots || rt.team.empty())
            continue;
        std::string team = team_name(rt.team.id());
        json::Value *t = team_of(season, rt.slot);
        if (t ? is_registered_team(doc, *t, team, rt.slot) : (rt.slot <= kCareerSlots || team.find('|') != std::string::npos))
            continue;
        if (!t && rt.slot > kCareerSlots) {
            json::Value drivers = json::Value::array(), ids = json::Value::array();
            for (int k = 0; k < 2; ++k) {
                const auto &seat = rt.seats[static_cast<size_t>(k)];
                if (seat.empty())
                    continue;
                ids.push_back(std::string(seat.id()));
                drivers.push_back(json::Value::object({{"driver", label_of(seat.id())}, {"position", k}}));
            }
            json::Value entry = json::Value::object({{"slot", rt.slot}, {"kind", "added"}, {"drivers", std::move(drivers)},
                                                     {"seats", std::move(ids)}});
            json::Value *list = season.find("teams");
            if (!list || !list->is_array())
                continue;
            list->push_back(std::move(entry));
            t = team_of(season, rt.slot);
        }
        if (!t)
            continue;
        json::Value ids = seat_ids(rt);
        bool same_team = std::as_const(*t)["team"].is_string() && std::as_const(*t)["team"].as_string() == team;
        if (same_team && (ids.size() == 0 || std::as_const(*t)["seats"] == ids))
            continue;
        (*t)["team"] = team;
        if (ids.size())
            (*t)["seats"] = std::move(ids);
        if (same_team)
            continue;
        ++n.teams;
        n.notes.push_back(year + " slot " + std::to_string(rt.slot) + " is " + team + " (a record names the team)");
    }
}

inline void overlay_seats(json::Value &season, const career::Season &s, Summary &n, const std::string &year)
{
    std::vector<std::pair<std::string, std::pair<int, int>>> seated;
    for (const career::SeasonTeam &rt : s.teams) {
        json::Value *t = team_of(season, rt.slot);
        if (!t) {
            n.notes.push_back(year + " slot " + std::to_string(rt.slot) + ": not in years.json; its seats are skipped");
            continue;
        }
        if (!std::as_const(*t)["drivers"].is_array())
            (*t)["drivers"] = json::Value::array();
        json::Array &drivers = *(*t)["drivers"].if_array();
        for (int k = 0; k < 2; ++k) {
            std::string want = rt.seats[static_cast<size_t>(k)].empty() ? std::string()
                                                                         : label_of(rt.seats[static_cast<size_t>(k)].id());
            json::Value *have = nullptr;
            size_t at = 0;
            for (size_t i = 0; i < drivers.size() && !have; ++i)
                if (seat_of(drivers[i]) == k && std::as_const(drivers[i])["driver"].is_string()) {
                    have = &drivers[i];
                    at = i;
                }
            std::string now = have ? std::string(std::as_const(*have)["driver"].as_string()) : std::string();
            if (want == now)
                continue;
            if (want.empty()) {
                drivers.erase(drivers.begin() + static_cast<std::ptrdiff_t>(at));
                ++n.emptied;
                continue;
            }
            json::Value entry = json::Value::object({{"driver", want}, {"position", k}});
            if (have)
                *have = std::move(entry);
            else
                drivers.push_back(std::move(entry));
            seated.push_back({want, {rt.slot, k}});
            ++n.seats;
        }
    }
    for (const auto &[label, at] : seated)
        unseat_elsewhere(season, label, at.first, at.second, n, year);
}

inline std::string change_key(int round, int slot, int seat)
{
    return std::to_string(round) + "/" + std::to_string(slot) + "/" + std::to_string(seat);
}

inline void overlay_changes(json::Value &season, const career::Season &s, Summary &n, const std::string &year)
{
    json::Value out = json::Value::array();
    std::set<std::string> used;
    const json::Array *had = std::as_const(season)["changes"].if_array();
    for (const career::SeasonChange &rc : s.changes) {
        std::string key = change_key(rc.round, rc.slot, rc.seat);
        if (!used.insert(key).second)
            continue;
        const json::Value *old = nullptr;
        if (had)
            for (const json::Value &c : *had)
                if (c["round"].is_int() && change_key(static_cast<int>(c["round"].as_int()), static_cast<int>(c["slot"].as_int()),
                                                      static_cast<int>(c["seat"].as_int())) == key)
                    old = &c;
        json::Value c = old ? *old
                            : json::Value::object({{"id", year + "-r" + std::to_string(rc.round) + "-s" + std::to_string(rc.slot) +
                                                              (rc.seat ? "b-" : "a-") + label_of(rc.in.id())},
                                                   {"round", rc.round},
                                                   {"slot", rc.slot},
                                                   {"seat", rc.seat}});
        json::Value before = c;
        c["out"] = label_of(rc.out.id());
        c["in"] = label_of(rc.in.id());
        if (!old || old->contains("kind") || rc.kind != career::ChangeKind::swap)
            c["kind"] = std::string(career::swofty_enum_name(rc.kind));
        if (rc.until_round)
            c["until_round"] = *rc.until_round;
        else
            c.erase("until_round");
        if (!old) {
            n.notes.push_back(year + " change " + key + " (" + label_of(rc.out.id()) + " -> " + label_of(rc.in.id()) +
                              ") added by a record");
            ++n.changes;
        } else if (!(c == before)) {
            ++n.changes;
        }
        out.push_back(std::move(c));
    }
    if (had)
        for (const json::Value &c : *had)
            if (!used.contains(change_key(static_cast<int>(c["round"].as_int()), static_cast<int>(c["slot"].as_int()),
                                          static_cast<int>(c["seat"].as_int())))) {
                n.notes.push_back(year + " change " + std::string(c["id"].as_string()) + " dropped by a record");
                ++n.changes;
            }
    if (had || out.size())
        season["changes"] = std::move(out);
}

inline double model_overall(const json::Value &doc, const json::Value &v)
{
    double sum = 0, w = 0;
    if (const json::Array *stats = doc["ratings"]["stats"].if_array())
        for (const json::Value &s : *stats) {
            const json::Value &x = v[s["name"].as_string()];
            if (!x.is_number())
                return -1;
            sum += x.as_double() * s["weight"].as_double();
            w += s["weight"].as_double();
        }
    return w > 0 ? sum / w : -1;
}

inline void overlay_rating(const json::Value &doc, json::Value &v, const career::DriverRatings &r, Summary &n)
{
    const float values[] = {r.experience, r.racecraft, r.awareness, r.pace};
    bool changed = false;
    for (size_t i = 0; i < 4; ++i) {
        const json::Value &now = std::as_const(v)[kRatingFields[i]];
        double want = std::round(static_cast<double>(values[i]) * 1000.0) / 1000.0;
        if (now.is_number() && std::fabs(now.as_double() - want) < 1e-3)
            continue;
        v[kRatingFields[i]] = want;
        changed = true;
    }
    if (!changed)
        return;
    double o = model_overall(doc, v);
    if (o >= 0)
        v["overall"] = std::round(o * 10.0) / 10.0;
    ++n.ratings_changed;
}

using SeasonOf = std::function<std::optional<career::Season>(const std::string &id)>;
using RatingOf = std::function<std::optional<career::DriverRatings>(const std::string &id)>;

inline Summary overlay(json::Value &doc, const SeasonOf &season_of, const RatingOf &rating_of)
{
    Summary n;
    const Stock stock = stock_drivers(doc);
    json::Value *list = doc.find("seasons");
    if (json::Object *seasons = list ? list->if_object() : nullptr)
        for (auto &[year, season] : *seasons) {
            std::optional<career::Season> s = season_of(season_id(year));
            if (!s)
                continue;
            ++n.seasons;
            overlay_teams(doc, season, *s, n, year);
            overlay_seats(season, *s, n, year);
            overlay_changes(season, *s, n, year);
        }
    json::Value *ratings = doc.find("ratings");
    json::Value *years = ratings ? ratings->find("years") : nullptr;
    if (json::Object *ys = years ? years->if_object() : nullptr)
        for (auto &[year, drivers] : *ys)
            if (json::Object *ds = drivers.if_object())
                for (auto &[label, v] : *ds) {
                    std::optional<career::DriverRatings> r = rating_of(rating_id(year, label, stock));
                    if (!r)
                        continue;
                    ++n.ratings;
                    overlay_rating(doc, v, *r, n);
                }
    return n;
}

}
