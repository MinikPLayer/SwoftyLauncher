// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career: the start season view of the career's setup list that Choose Driver and Team
// Select read (progressive_cards.dll). Pure logic, no game access, so native/test builds it on the
// host. R is any record with the EntrantRecord field names this file reads.
//
// build(): every team seated in the career list that the season or the first season list names
// keeps exactly two seated rows, positions 0 and 1 (Team Select's 0x141877ec0 reads a team's first
// two rows with no count check). A team neither list names (the F2 teams of the career list's F2
// drivers) keeps its rows as they are.
//   1. the season list's drivers of that team on their season seats, with the season row's look,
//      car, livery and number (the career row keeps its identity, ordinal and driver key);
//   2. a seat the season leaves empty gets a career row of that team's 2026 seats whose driver is
//      not in the season, else a free row whose driver is not in the season, with the team's car
//      of the season, else of the first season list, else the team's 2026 car;
//   3. every other career row follows unchanged, except that a 2026 seat of a dressed team is taken
//      off the grid.
// So the view has as many rows as the career list (when every season driver is in it), and no
// driver twice.
//
// takes_back(): when the view is put back to the career's own list. Not on career.loaded: in the
// wizard it fires when Choose Driver's pick makes the career component, before Team Select reads
// the setup list. The game's own line-up copies (Game_CareerSetup_ResetEntrants, 0x140c2a870) point
// the setup list back at the registry's list by stat id when the career is built.

#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "../../../../native/src/core/json.hpp"

namespace progressive::cards {

constexpr uint32_t kNoKey = 0x3fff0000u;
constexpr uint8_t kNoNumber = 0xff;
constexpr size_t kMaxRows = 512;
constexpr size_t kMaxTeams = 32;

// Entrant list stat ids: tools/gen.py LIST_STAT_IDS (career, pool, then 2016..2025), and the stock
// career_f2_2025_for_f1_2026 that the content set's F2CAREER_26 row names and every F2CAREER_PG*
// row keeps.
constexpr unsigned kCareerList = 120;
constexpr unsigned kPoolList = 121;
constexpr unsigned kFirstSeasonList = 122;
constexpr unsigned kF2List = 100;
constexpr int kFirstYear = 2016;
constexpr int kLastListYear = 2025;

// Read empty during the populate, so the cards are the F1 list's drivers only: the F2 and Icons
// tabs are hidden in a Progressive wizard, and a pool driver of the season would get a second card.
constexpr unsigned kHiddenLists[] = {kPoolList, kF2List};

constexpr std::string_view kEventHub = "career.hub";
constexpr std::string_view kEventContentPressed = "career.content.pressed";
constexpr std::string_view kEventStarting = "career.starting";
constexpr std::string_view kEvents[] = {kEventHub, kEventContentPressed, kEventStarting};
// career.loaded only dresses Team Select's cars (the career components exist from then on).
constexpr std::string_view kEventLoaded = "career.loaded";

inline bool takes_back(std::string_view event, int64_t a)
{
    if (event == kEventHub)
        return a == 1;
    return event == kEventContentPressed || event == kEventStarting;
}

// Whether a take-back also puts the line-up rows back (cars, the picked driver's team). Only when
// the wizard is left (a tile press, career.starting): at Team HQ the career was built from those
// rows and owns them.
inline bool restores_rows(std::string_view event)
{
    return event == kEventContentPressed || event == kEventStarting;
}

inline bool none(uint32_t key)
{
    return (key & 0xffff0000u) == kNoKey;
}

inline bool same_team(uint32_t a, uint32_t b)
{
    return ((a ^ b) & 0xffff00ffu) == 0;
}

template <class R> bool same_driver(const R &a, const R &b)
{
    return a.id_lo == b.id_lo && a.id_hi == b.id_hi;
}

template <class R> bool seated(const R &r)
{
    return !none(r.team_stat_id) && (r.team_position >> 8) && (r.team_position & 0xff) < 2;
}

template <class R> struct Rows {
    const R *begin = nullptr;
    size_t count = 0;
};

struct Stats {
    size_t season = 0, missing = 0, looks = 0, cars = 0, numbers = 0, filled = 0, benched = 0, teams = 0;
};

template <class R> size_t build(const Rows<R> &career, const Rows<R> &season, const Rows<R> &first_season, R *out,
                                size_t cap, Stats &st)
{
    if (career.count > kMaxRows || cap < career.count)
        return 0;
    bool used[kMaxRows] = {};
    uint32_t teams[kMaxTeams];
    size_t team_count = 0, n = 0;
    uint16_t free_position = 0x00ff;
    auto listed = [&](uint32_t team) {
        for (const Rows<R> *rows : {&season, &first_season})
            for (size_t i = 0; i < rows->count; ++i)
                if (same_team(rows->begin[i].team_stat_id, team))
                    return true;
        return false;
    };
    auto dressed = [&](uint32_t team) {
        for (size_t t = 0; t < team_count; ++t)
            if (same_team(teams[t], team))
                return true;
        return false;
    };
    for (size_t i = 0; i < career.count; ++i) {
        const R &c = career.begin[i];
        if (none(c.team_stat_id))
            free_position = c.team_position;
        if (seated(c) && !dressed(c.team_stat_id) && listed(c.team_stat_id) && team_count < kMaxTeams)
            teams[team_count++] = c.team_stat_id;
    }
    st.teams = team_count;

    auto find = [&](const R &y) -> size_t {
        for (size_t i = 0; i < career.count; ++i)
            if (!used[i] && same_driver(career.begin[i], y))
                return i;
        return career.count;
    };
    auto in_season = [&](const R &c) {
        for (size_t k = 0; k < season.count; ++k)
            if (same_driver(season.begin[k], c))
                return true;
        return false;
    };
    auto car_for = [&](uint32_t team, uint32_t fallback) -> uint32_t {
        for (const Rows<R> *rows : {&season, &first_season})
            for (size_t i = 0; i < rows->count; ++i)
                if (same_team(rows->begin[i].team_stat_id, team) && !none(rows->begin[i].vehicle_ref))
                    return rows->begin[i].vehicle_ref;
        return fallback;
    };

    for (size_t t = 0; t < team_count && n < cap; ++t) {
        uint32_t team = teams[t];
        int placed = 0;
        for (int pos = 0; pos < 2; ++pos)
            for (size_t k = 0; k < season.count && n < cap && placed < 2; ++k) {
                const R &y = season.begin[k];
                if (!same_team(y.team_stat_id, team) || (y.team_position & 0xff) != pos)
                    continue;
                size_t i = find(y);
                if (i == career.count) {
                    ++st.missing;
                    continue;
                }
                R r = career.begin[i];
                if (!none(y.driver_customisation)) {
                    r.driver_customisation = y.driver_customisation;
                    ++st.looks;
                }
                if (!none(y.vehicle_ref)) {
                    r.vehicle_ref = y.vehicle_ref;
                    ++st.cars;
                }
                r.vehicle_customisation = y.vehicle_customisation;
                if (y.car_number != kNoNumber) {
                    r.car_number = y.car_number;
                    ++st.numbers;
                }
                r.team_stat_id = y.team_stat_id;
                r.team_position = static_cast<uint16_t>(0x100 | placed);
                out[n++] = r;
                used[i] = true;
                ++placed;
                ++st.season;
            }
        for (int pass = 0; pass < 2 && placed < 2; ++pass)
            for (size_t i = 0; i < career.count && placed < 2 && n < cap; ++i) {
                const R &c = career.begin[i];
                bool fits = pass == 0 ? seated(c) && same_team(c.team_stat_id, team) : none(c.team_stat_id);
                if (used[i] || !fits || in_season(c))
                    continue;
                R r = c;
                r.team_stat_id = team;
                r.team_position = static_cast<uint16_t>(0x100 | placed);
                r.vehicle_ref = car_for(team, seated(c) ? c.vehicle_ref : kNoKey);
                if (none(r.vehicle_ref))
                    for (size_t j = 0; j < career.count; ++j)
                        if (seated(career.begin[j]) && same_team(career.begin[j].team_stat_id, team))
                            r.vehicle_ref = career.begin[j].vehicle_ref;
                r.vehicle_customisation = kNoKey;
                out[n++] = r;
                used[i] = true;
                ++placed;
                ++st.filled;
            }
    }
    for (size_t i = 0; i < career.count && n < cap; ++i) {
        if (used[i])
            continue;
        R r = career.begin[i];
        if (seated(r) && dressed(r.team_stat_id)) {
            r.team_stat_id = kNoKey;
            r.vehicle_ref = kNoKey;
            r.vehicle_customisation = kNoKey;
            r.badge_stat_id = kNoKey;
            r.team_position = free_position;
            ++st.benched;
        }
        out[n++] = r;
    }
    return n;
}

// Team logos (logo_map()): Choose Driver's badge and details logo and Team Select's logo format the
// career team row's label (forceindia_26) into an overrides key. In a start season below 2026 the
// label maps to that season's Classic Seasons team key from data/years.json: seasons[Y].teams[]
// .cs_team of the label's slot (cs17_force_india; slot 11 is the filler cs16_manor in 2017-2025)
// when logo_keys lists it, else the stock label; a season team with no cs_team (2025) maps to the
// label without _26 (forceindia), the stock 2025 key. A label of no slot, or a slot the season
// leaves empty, keeps its own key.
struct LogoMap {
    int year = 0;
    std::string from, to;
};

inline std::vector<LogoMap> logo_map(const json::Value &doc, std::vector<std::string> &missing)
{
    std::vector<LogoMap> out;
    std::vector<std::string> keys;
    for (const json::Value &k : doc["logo_keys"].a)
        keys.push_back(k.str());
    const json::Value &seasons = doc["seasons"];
    for (int year = kFirstYear; year <= kLastListYear; ++year) {
        const json::Value &season = seasons[std::to_string(year).c_str()];
        for (const json::Value &slot : doc["slots"].a) {
            std::string label = slot["team"].str();
            long long n = slot["slot"].num(-1);
            if (label.empty() || n < 0)
                continue;
            for (const json::Value &t : season["teams"].a) {
                if (t["slot"].num(-2) != n)
                    continue;
                std::string cs = t["cs_team"].str();
                if (cs.empty()) {
                    if (label.size() > 3 && label.compare(label.size() - 3, 3, "_26") == 0)
                        out.push_back({year, label, label.substr(0, label.size() - 3)});
                } else if (std::find(keys.begin(), keys.end(), cs) != keys.end()) {
                    out.push_back({year, label, cs});
                } else if (std::find(missing.begin(), missing.end(), cs) == missing.end()) {
                    missing.push_back(cs);
                }
                break;
            }
        }
    }
    return out;
}

// The override arrays Classic Seasons adds a slice to for every season team (tools/gen.py
// LOGO_ARRAYS, the logo_keys of data/years.json), as the UI's registry paths name them.
constexpr std::string_view kLogoArrays[] = {
    "team_logos.f1_2023",          "team_logos.driver_career_2024_team_select", "teams.mp_team_select_logos",
    "teams.driver_select_team_logos", "teams.badges_broadcast",              "broadcast_ui_generic.grid_team_logos",
    "osd.timing_tower.team_badges_hud", "osd.timing_tower.team_badges_broadcast"};

inline const char *logo_for(const std::vector<LogoMap> &map, int year, const char *label)
{
    if (!label)
        return label;
    for (const LogoMap &m : map)
        if (m.year == year && m.from == label)
            return m.to.c_str();
    return label;
}

// A registry path "[#]overrides.<array>.<label>" of a team logo array whose label the year maps
// gets the year's key: out is the path with the label replaced. Anything else stays (false).
inline bool logo_path(const std::vector<LogoMap> &map, int year, const char *path, std::string &out)
{
    if (!path)
        return false;
    std::string_view p(path);
    constexpr std::string_view kRoot = "overrides.";
    size_t at = !p.empty() && p[0] == '#' ? 1 : 0;
    if (p.size() < at + kRoot.size() || p.substr(at, kRoot.size()) != kRoot)
        return false;
    size_t dot = p.rfind('.');
    if (dot == std::string_view::npos || dot <= at + kRoot.size())
        return false;
    std::string_view array = p.substr(at + kRoot.size(), dot - at - kRoot.size());
    bool logos = false;
    for (std::string_view a : kLogoArrays)
        logos = logos || a == array;
    if (!logos)
        return false;
    std::string label(p.substr(dot + 1));
    const char *to = logo_for(map, year, label.c_str());
    if (to == label.c_str())
        return false;
    out.assign(p.substr(0, dot + 1));
    out += to;
    return true;
}

} // namespace progressive::cards
