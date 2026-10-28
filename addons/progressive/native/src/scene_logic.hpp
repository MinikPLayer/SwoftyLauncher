// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career: the contract scene a follow_driver career skips (progressive_follow.dll). Pure
// logic, built on the host by native/test.
//
//   Skip      armed by the negotiation decision once follow.cpp has signed the contract: where it
//             happened (the career's start, with no contract yet, or the end of his contract), the
//             team, year and target signed, and which steps of the scene passed through (team
//             select, the agent intro, the contract terms, the outro).
//   summary   the text of the log line the outro writes.
//   route     what ContractNegotiationsDecision's link leads to: 0x5e3c3c2d when he has no contract
//             yet (or it runs past this season), the scene from the agent intro; 0xd2985448 when his
//             contract ends this season, the scene from the team select; 0x7c9b1ec4 (no contracts
//             component) no scene.
//   tried     a scene is signed for at most once: the decision, or the first scene step entered
//             without it (team select, agent intro), whichever comes first.
//   event     the contract event that leads to the scene is a career calendar activity of type 0x0f
//             (NCSContractRenewal), the Team HQ card 'CONTRACT EVENT'; a follow career signs when it
//             is about to be added to the player's own driver calendar (one of a ScheduleSeason's
//             driver calendars whose driver is his) and does not add it.
//   pending   the contract events already on his driver calendars (a save from before, or one added
//             another way): every activity of type 0x0f on a day not before today, which a follow
//             career removes through the game's own remover after signing.

#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace progressive::scene {

constexpr uint32_t kLinkNoContract = 0x5e3c3c2d;
constexpr uint32_t kLinkContractEnds = 0xd2985448;

constexpr uint8_t kContractEvent = 0x0f;
constexpr size_t kMaxSeasons = 64;
constexpr size_t kMaxCalendars = 256;
constexpr size_t kMaxActivities = 1024;
constexpr size_t kSlot = 16;

enum class Route { none, no_contract, contract_ends };

inline Route route(uint32_t link)
{
    return link == kLinkNoContract ? Route::no_contract : link == kLinkContractEnds ? Route::contract_ends : Route::none;
}

inline const char *route_name(Route r)
{
    switch (r) {
    case Route::no_contract:
        return "no contract yet: the scene starts at the agent intro";
    case Route::contract_ends:
        return "his contract ends: the scene starts at the team select";
    default:
        return "no contract scene follows";
    }
}

enum Step : unsigned {
    kTeamSelect = 1u << 0,
    kIntro = 1u << 1,
    kTerms = 1u << 2,
    kOutro = 1u << 3,
};

struct Skip {
    bool on = false;
    bool tried = false;
    bool update_skipped = false;
    bool contract_end = false;
    uint32_t team = 0;
    int year = 0;
    int target = 0;
    unsigned steps = 0;

    void arm(bool end, uint32_t team_key, int season_year, int target_rating)
    {
        *this = Skip{};
        on = true;
        tried = true;
        contract_end = end;
        team = team_key;
        year = season_year;
        target = target_rating;
    }

    void mark(Step s)
    {
        steps |= s;
    }

    void clear()
    {
        *this = Skip{};
    }

    std::string where() const
    {
        return contract_end ? "end of his contract" : "career start, no contract yet";
    }

    std::string passed() const
    {
        static constexpr struct {
            Step step;
            const char *name;
        } kNames[] = {{kTeamSelect, "team select"}, {kIntro, "agent intro"}, {kTerms, "contract terms"}, {kOutro, "outro"}};
        std::string out;
        for (auto &n : kNames)
            if (steps & n.step) {
                if (!out.empty())
                    out += ", ";
                out += n.name;
            }
        return out.empty() ? "no step reached" : out;
    }

    std::string summary() const
    {
        char b[160];
        if (year > 0)
            std::snprintf(b, sizeof b, "), signed team %u for %d (one season, target %d)", team >> 16, year, target);
        else
            std::snprintf(b, sizeof b, "), signed team %u (one season, target %d)", team >> 16, target);
        return "contract scene skipped (" + where() + ": " + passed() + b;
    }
};

struct CalendarLayout {
    size_t seasons_begin, seasons_end, season_size;
    size_t drivers_begin, drivers_end, calendar_size;
    size_t calendar_driver = 0, activities_begin = 0, activities_end = 0;
    size_t activity_day = 0, activity_def = 0, def_type = 0;
};

struct Pending {
    uintptr_t calendar = 0;
    uintptr_t activity = 0;
    uintptr_t def = 0;
    uint16_t day = 0;
};

inline bool same_driver(uint32_t a, uint32_t b)
{
    return ((a ^ b) & 0xffff00ffu) == 0;
}

template <class Mem>
bool driver_calendar(const Mem &m, uintptr_t schedule, uintptr_t calendar, const CalendarLayout &l)
{
    uintptr_t b = 0, e = 0;
    if (!schedule || !calendar || !l.season_size || !l.calendar_size || !m.read(schedule + l.seasons_begin, b) ||
        !m.read(schedule + l.seasons_end, e))
        return false;
    if (!b || e < b || (e - b) % l.season_size || (e - b) / l.season_size > kMaxSeasons)
        return false;
    for (uintptr_t season = b; season < e; season += l.season_size) {
        uintptr_t db = 0, de = 0;
        if (!m.read(season + l.drivers_begin, db) || !m.read(season + l.drivers_end, de) || !db || de < db)
            continue;
        if (calendar >= db && calendar < de && (calendar - db) % l.calendar_size == 0)
            return true;
    }
    return false;
}

template <class Mem>
std::vector<Pending> pending_events(const Mem &m, uintptr_t schedule, uint32_t driver, uint32_t today, const CalendarLayout &l)
{
    std::vector<Pending> out;
    uintptr_t b = 0, e = 0;
    if (!schedule || !l.season_size || !l.calendar_size || !m.read(schedule + l.seasons_begin, b) ||
        !m.read(schedule + l.seasons_end, e))
        return out;
    if (!b || e < b || (e - b) % l.season_size || (e - b) / l.season_size > kMaxSeasons)
        return out;
    for (uintptr_t season = b; season < e; season += l.season_size) {
        uintptr_t db = 0, de = 0;
        if (!m.read(season + l.drivers_begin, db) || !m.read(season + l.drivers_end, de) || !db || de < db ||
            (de - db) % l.calendar_size || (de - db) / l.calendar_size > kMaxCalendars)
            continue;
        for (uintptr_t cal = db; cal < de; cal += l.calendar_size) {
            uint32_t owner = 0;
            if (!m.read(cal + l.calendar_driver, owner) || !same_driver(owner, driver))
                continue;
            uintptr_t ab = 0, ae = 0;
            if (!m.read(cal + l.activities_begin, ab) || !m.read(cal + l.activities_end, ae) || !ab || ae < ab ||
                (ae - ab) % kSlot || (ae - ab) / kSlot > kMaxActivities)
                continue;
            for (uintptr_t slot = ab; slot < ae; slot += kSlot) {
                Pending p;
                uint8_t type = 0;
                if (!m.read(slot + 8, p.activity) || !p.activity || !m.read(p.activity + l.activity_def, p.def) ||
                    !p.def || !m.read(p.def + l.def_type, type) || type != kContractEvent ||
                    !m.read(p.activity + l.activity_day, p.day) || p.day < today)
                    continue;
                p.calendar = cal;
                out.push_back(p);
            }
        }
    }
    return out;
}

}
