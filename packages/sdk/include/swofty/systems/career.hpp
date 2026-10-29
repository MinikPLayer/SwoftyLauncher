// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <map>
#include <optional>
#include <swofty/career.hpp>
#include <swofty/events.hpp>
#include <swofty/registry.hpp>
#include <swofty/systems/career_ops.hpp>
#include <swofty/systems/content.hpp>
#include <swofty/systems/drivers.hpp>
#include <swofty/systems/forward.hpp>
#include <swofty/systems/ops.hpp>
#include <swofty/systems/teams.hpp>

// Career: modes, contents, seasons and their boxes, calendars, ratings, characters and wizards. The 1.x career library (swofty/career.hpp) keeps
// its names in this namespace: its Result is the operations' result here, and its Mode enum stays the 1.x
// mode, so the career.mode record struct is ModeRecord. apply_teams, apply_lineup, set_driver_ratings,
// swap_drivers, report_season, set_seat and set_car_number call the core's career operations
// (SwoftyCoreCareerOps) and return unavailable on a core without them; apply_dropped_scores returns unavailable.
// A season's teams name team.identity records (new teams included) in up to 12 career team slots.

namespace swofty::career {

enum class Kind { driver_career, my_team };
SWOFTY_ENUM(Kind, driver_career, my_team)
enum class Freeze { per_season, at_creation };
SWOFTY_ENUM(Freeze, per_season, at_creation)
enum class AfterLast { market, hold, end };
SWOFTY_ENUM(AfterLast, market, hold, end)
enum class ChangeKind { swap, stand_in, move, replace };
SWOFTY_ENUM(ChangeKind, swap, stand_in, move, replace)
enum class Careers { driver, my_team, both };
SWOFTY_ENUM(Careers, driver, my_team, both)

struct ModeRecord {
    static constexpr Version version{1, 0};
    std::string data_group;
    std::string save_container;
    Text title;
    Kind kind = Kind::driver_career;
    Freeze freeze = Freeze::per_season;
    StatId stat_id = StatId::automatic;
    json::Value extra;
};
SWOFTY_RECORD(ModeRecord, data_group, save_container, title, kind, freeze, stat_id)
inline Registry<ModeRecord> modes{"career.mode", {.applies = Applies::boot}};

struct Content {
    static constexpr Version version{1, 0};
    Ref<ModeRecord> mode;
    Path tile;
    Ref<Content> model;
    Ref<Calendar> calendar;
    AfterLast after_last = AfterLast::market;
    Text title;
    Text description;
    std::string data_group;
    std::string suffix;
    std::string save_container;
    int car_group = -1;
    int badge = 0;
    std::string entitlement;
    Careers careers = Careers::driver;
    json::Value extra;
};
SWOFTY_RECORD(Content, mode, tile, model, calendar, after_last, title, description, data_group, suffix, save_container,
              car_group, badge, entitlement, careers)
inline Registry<Content> contents{"career.content", {.applies = Applies::boot}};

struct SeasonTeam {
    int slot = 0;
    Ref<teams::Identity> team;
    std::array<Ref<drivers::Identity>, 2> seats;
};
SWOFTY_RECORD(SeasonTeam, slot, team, seats)

struct SeasonChange {
    int round = 0, slot = 0, seat = 0;
    Ref<drivers::Identity> out, in;
    ChangeKind kind = ChangeKind::swap;
    std::optional<int> until_round;
};
SWOFTY_RECORD(SeasonChange, round, slot, seat, out, in, kind, until_round)

struct Season {
    static constexpr Version version{1, 0};
    std::string data_group;
    int year = 0;
    Ref<::swofty::content::Package> box;
    Ref<Calendar> calendar;
    Keyed<SeasonTeam, &SeasonTeam::slot> teams;
    Keyed<SeasonChange, &SeasonChange::round, &SeasonChange::slot, &SeasonChange::seat> changes;
    json::Value extra;
};
SWOFTY_RECORD(Season, data_group, year, box, calendar, teams, changes)
inline Registry<Season> seasons{"career.season",
                                {.applies = Applies::read, .snapshot = true, .snapshot_budget = 65536}};

struct Round {
    int round = 0;
    Text race;
    Ref<tracks::Venue> venue;
    std::chrono::year_month_day date;
};
SWOFTY_RECORD(Round, round, race, venue, date)

struct Calendar {
    static constexpr Version version{1, 0};
    Keyed<Round, &Round::round> rounds;
    int seasons = 10;
    std::vector<int> transfer_windows;
    json::Value extra;
};
SWOFTY_RECORD(Calendar, rounds, seasons, transfer_windows)
inline Registry<Calendar> calendars{"career.calendar", {.applies = Applies::boot}};

struct DriverRatings {
    static constexpr Version version{1, 0};
    Ref<Season> season;
    Ref<drivers::Identity> driver;
    float experience = 0, racecraft = 0, awareness = 0, pace = 0;
    json::Value extra;
};
SWOFTY_RECORD(DriverRatings, season, driver, experience, racecraft, awareness, pace)
inline Registry<DriverRatings> ratings{"career.driver_ratings",
                                       {.applies = Applies::read, .snapshot = true, .snapshot_budget = 65536}};

struct CharacterSwap {
    std::string character;
    std::string with;
};
SWOFTY_RECORD(CharacterSwap, character, with)

struct Character {
    static constexpr Version version{1, 0};
    Ref<Character> replaces;
    Text first_name, last_name;
    Path portrait;
    std::vector<std::string> mute;
    std::string data_group;
    Keyed<CharacterSwap, &CharacterSwap::character> swaps;
    std::map<std::string, std::map<std::string, std::string>> strings;
    json::Value extra;
};
SWOFTY_RECORD(Character, replaces, first_name, last_name, portrait, mute, data_group, swaps, strings)
SWOFTY_RECORD_REGISTRY(Character, "career.character")
inline Registry<Character> characters{"career.character", {.applies = Applies::content}};

struct WizardStep {
    std::string route;
    Text title;
    std::vector<std::string> options;
};
SWOFTY_RECORD(WizardStep, route, title, options)

struct WizardRoute {
    std::string from;
    std::string to;
};
SWOFTY_RECORD(WizardRoute, from, to)

struct WizardSwap {
    int at = 0;
    int with = 0;
};
SWOFTY_RECORD(WizardSwap, at, with)

struct Wizard {
    static constexpr Version version{1, 0};
    Ref<Content> content;
    std::vector<WizardStep> steps;
    std::string data_group;
    bool starts_in_f1 = false;
    Keyed<WizardRoute, &WizardRoute::from> tile_routes;
    Keyed<WizardSwap, &WizardSwap::at> replace_steps;
    Keyed<WizardSwap, &WizardSwap::at> force_options;
    Keyed<WizardRoute, &WizardRoute::from> confirm_routes;
    std::vector<std::string> hide_driver_tabs;
    json::Value extra;
};
SWOFTY_RECORD(Wizard, content, steps, data_group, starts_in_f1, tile_routes, replace_steps, force_options, confirm_routes,
              hide_driver_tabs)
inline Registry<Wizard> wizards{"career.wizard", {.applies = Applies::content}};

struct HubTab {
    static constexpr Version version{1, 0};
    std::string name;
    std::string title;
    std::string data_group;
    std::vector<std::string> options;
    json::Value extra;
};
SWOFTY_RECORD(HubTab, name, title, data_group, options)
SWOFTY_RECORD_REGISTRY(HubTab, "hub.tab")
inline Registry<HubTab> hub_tabs{"hub.tab", {.applies = Applies::read}};

namespace events {

struct SeasonStart {
    static constexpr const char *point = "career.season.start";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    int32_t year;
    int32_t season_index;
};

struct AddActivity {
    static constexpr const char *point = "career.calendar.add_activity";
    static constexpr uint32_t version = 1;
    static constexpr uint8_t contract_event = 0x0f;
    uint32_t size, payload_version;
    void *calendar;
    const void *activity;
    uint32_t driver;
    uint32_t day;
    uint8_t type;
    bool player;
};

struct ContractDecide {
    static constexpr const char *point = "career.contract.decide";
    static constexpr uint32_t version = 1;
    static constexpr uint32_t negotiate = 0x5e3c3c2d;
    static constexpr uint32_t ends_this_season = 0xd2985448;
    static constexpr uint32_t next = 0x7c9b1ec4;
    uint32_t size, payload_version;
    void *state;
    uint32_t link;
};

struct ContractSign {
    static constexpr const char *point = "career.contract.sign";
    static constexpr uint32_t version = 1;
    static constexpr uint32_t work_out_replaced = 0x3fff0000;
    uint32_t size, payload_version;
    void *contracts;
    void *contract;
    uint32_t driver;
    uint32_t replaced;
    uint32_t team;
    uint8_t type;
    bool notify;
};

struct StandingsRebuild {
    static constexpr const char *point = "career.standings.rebuild";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    void *championship;
};

#define SWOFTY_CAREER_EVENT_(T, name)                                                                                  \
    struct T {                                                                                                         \
        static constexpr const char *point = name;                                                                     \
        static constexpr uint32_t version = 1;                                                                         \
        uint32_t size, payload_version;                                                                                \
    };
SWOFTY_CAREER_EVENT_(Created, "career.created")
SWOFTY_CAREER_EVENT_(Loaded, "career.loaded")
SWOFTY_CAREER_EVENT_(Unloaded, "career.unloaded")
SWOFTY_CAREER_EVENT_(SeasonEnd, "career.season.end")
SWOFTY_CAREER_EVENT_(SeasonBreak, "career.season.break")
SWOFTY_CAREER_EVENT_(Saving, "career.saving")
#undef SWOFTY_CAREER_EVENT_

}

struct ApplyReport {
    size_t changed = 0, unchanged = 0, refused = 0;
    std::vector<std::string> refusals;
};

// The core's career operations (swofty_api.h SwoftyCareerOps, the core export SwoftyCoreCareerOps). On a core
// without them every call returns unavailable.
inline const SwoftyCareerOps *ops() { return detail::live_ops(); }

// A data group's career team slots: slot N (1-based) races as team stat team_stats[N-1]. apply_lineup reads
// the table of the season's data group.
inline Result set_slots(std::string_view data_group, const std::vector<uint32_t> &team_stats)
{
    auto t = detail::live_ops();
    if (!t)
        return Result::unavailable;
    return detail::ops_result(t->set_slots(std::string(data_group).c_str(), team_stats.data(), team_stats.size()));
}

namespace detail {

using SeasonOpFn = int (*)(SwoftyText, char *, size_t, size_t *);

inline Result run_season_op(SeasonOpFn fn, const Season &season, ApplyReport *report)
{
    std::string request = "{\"season\":" + to_json(season) + "}";
    std::string out(1024, '\0');
    size_t len = 0;
    int r = fn({request.data(), request.size()}, out.data(), out.size(), &len);
    if (r == SWOFTY_TOO_SMALL) {
        out.assign(len + 1, '\0');
        r = fn({request.data(), request.size()}, out.data(), out.size(), &len);
    }
    if (report && len < out.size()) {
        json::Value v = json::parse(std::string_view(out.data(), len));
        report->changed = static_cast<size_t>(v["changed"].as_int());
        report->unchanged = static_cast<size_t>(v["unchanged"].as_int());
        report->refused = static_cast<size_t>(v["refused"].as_int());
        if (const json::Array *list = v["refusals"].if_array())
            for (const json::Value &why : *list)
                report->refusals.emplace_back(why.as_string());
    }
    return ops_result(r);
}

}

// The season's teams race in its data group's career: each listed slot (1-12) races as its team (the slot
// table and the Championship's participants), and a team with no career rows gets them, built from its
// donor's. An empty team leaves the slot out of the Championship; a slot the season doesn't list keeps its
// team. Call it before apply_lineup. unavailable on a core without the call (table version 3).
inline Result apply_teams(const Season &season, ApplyReport *report = nullptr)
{
    if (report)
        *report = {};
    auto t = detail::live_ops();
    if (!t || t->version < 3 || !t->apply_teams)
        return Result::unavailable;
    return detail::run_season_op(t->apply_teams, season, report);
}

inline Result apply_lineup(const Season &season, ApplyReport *report = nullptr)
{
    if (report)
        *report = {};
    auto t = detail::live_ops();
    if (!t)
        return Result::unavailable;
    return detail::run_season_op(t->apply_lineup, season, report);
}

namespace detail {

inline Result seat_request(const SwoftyCareerOps *t, const std::string &request)
{
    char out[2048];
    size_t len = 0;
    int r = t->set_seat({request.data(), request.size()}, out, sizeof out, &len);
    return r == SWOFTY_TOO_SMALL ? Result::ok : ops_result(r);
}

}

// The driver takes race seat `seat` (0 or 1) of career team slot `slot` (1-based, the loaded career's data
// group's slot table): a race driver exchanges seats with the seat's driver; a free agent takes the seat and its
// driver leaves the grid. Driver Career only; unavailable on a core without the call (table version 4).
inline Result set_seat(int slot, int seat, Ref<drivers::Identity> driver)
{
    auto t = detail::live_ops();
    if (!t || t->version < 4 || !t->set_seat)
        return Result::unavailable;
    uint32_t stat = 0;
    if (!detail::stat_of(t, driver, stat))
        return Result::invalid;
    return detail::seat_request(t, "{\"driver\":" + std::to_string(stat) + ",\"slot\":" + std::to_string(slot) +
                                       ",\"seat\":" + std::to_string(seat) + "}");
}

// The same by team stat id, for a career without a slot table (a stock career).
inline Result set_seat_of_team(uint32_t team_stat, int seat, Ref<drivers::Identity> driver)
{
    auto t = detail::live_ops();
    if (!t || t->version < 4 || !t->set_seat)
        return Result::unavailable;
    uint32_t stat = 0;
    if (!detail::stat_of(t, driver, stat))
        return Result::invalid;
    return detail::seat_request(t, "{\"driver\":" + std::to_string(stat) + ",\"team\":" + std::to_string(team_stat) +
                                       ",\"seat\":" + std::to_string(seat) + "}");
}

inline Result set_driver_ratings(Ref<drivers::Identity> driver, const DriverRatings &ratings)
{
    auto t = detail::live_ops();
    if (!t)
        return Result::unavailable;
    uint32_t stat = 0;
    if (!detail::stat_of(t, driver, stat))
        return Result::invalid;
    const uint16_t ids[] = {0, 1, 3, 4};
    const float values[] = {ratings.experience, ratings.racecraft, ratings.awareness, ratings.pace};
    return detail::ops_result(t->set_driver_ratings(stat, ids, values, 4));
}

// The driver's racing number (1-99) on his career row and the current weekend's copy. refused when another
// driver holds it, unless swap, which gives that driver this driver's old number. Unavailable on a core
// without the call (table version 4).
inline Result set_car_number(Ref<drivers::Identity> driver, int number, bool swap = false)
{
    auto t = detail::live_ops();
    if (!t || t->version < 4 || !t->set_car_number)
        return Result::unavailable;
    uint32_t stat = 0;
    if (!detail::stat_of(t, driver, stat))
        return Result::invalid;
    return detail::ops_result(t->set_car_number(stat, number, swap ? SWOFTY_NUMBER_SWAP : 0));
}

inline Result swap_drivers(Ref<drivers::Identity> a, Ref<drivers::Identity> b)
{
    auto t = detail::live_ops();
    if (!t)
        return Result::unavailable;
    uint32_t sa = 0, sb = 0;
    if (!detail::stat_of(t, a, sa) || !detail::stat_of(t, b, sb))
        return Result::invalid;
    return detail::ops_result(t->swap_drivers(sa, sb));
}

inline Result apply_dropped_scores(const events::StandingsRebuild &, int) { return Result::unavailable; }

// The season at the Championship's season_index is year, from the addon that owns the active career's data
// group: the core fires career.season.start once per career and year, activates the season's records and
// freezes the season. refused from any other addon; unavailable on a core without the call (table version 2).
inline Result report_season(int year, int season_index)
{
    auto t = detail::live_ops();
    if (!t || t->version < 2 || !t->report_season)
        return Result::unavailable;
    return detail::ops_result(t->report_season(year, season_index));
}

}
