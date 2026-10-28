// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career: the career state on every Team HQ, for the in-game tests. One line for the
// season (Championship.season_index +0x560, the seasons created so far, completion), one per row
// that has a team (race seats first, by team and seat, then reserves), and the free agents in
// chunks. The core log takes 2047 bytes per line.

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include <swofty/career_content.hpp>
#include <swofty/mixin.hpp>

#include "names.hpp"
#include "progressive.hpp"
#include "seats.hpp"

namespace pg {

namespace {

using names::Kind;

std::string hex(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof b, "0x%08x", v);
    return b;
}

std::string raw(uint32_t v)
{
    return none(v) ? std::string("none") : hex(v);
}

std::string number(uint8_t n)
{
    return n == kNoNumber ? std::string("none") : "#" + std::to_string(n);
}

} // namespace

std::string row_text(const EntrantRecord &r)
{
    char pos[16];
    int p = position_of(r);
    std::snprintf(pos, sizeof pos, "%d", p);
    return names::key_text(Kind::driver, driver_of(r)) + " team " + names::key_text(Kind::team, r.team_stat_id) +
           " pos " + (p < 0 ? std::string("none") : std::string(pos)) + " car " +
           names::key_text(Kind::vehicle, r.vehicle_ref) + " [" + raw(r.vehicle_ref) + "] look " +
           names::key_text(Kind::look, r.driver_customisation) + " [" + raw(r.driver_customisation) + "] " +
           number(r.car_number) + " reg " + raw(r.driver_reg_value) + " livery " + raw(r.vehicle_customisation) +
           " badge " + raw(r.badge_stat_id) + " seat " + hex(r.const_ffff0012) + " ord " + hex(r.ordinal_flags) +
           (human(r) ? " HUMAN" : "");
}

void report(const char *why, const swofty::career::State &state)
{
    uintptr_t e = entity();
    if (!e) {
        swofty::log("[progressive] %s: no career entity", why);
        return;
    }
    names::load();
    SeasonInfo s;
    bool have_season = season(e, s);
    std::vector<Row> rows;
    if (!entrant_rows(e, rows)) {
        swofty::log("[progressive] %s: the entrant rows are unreadable", why);
        return;
    }
    std::vector<const Row *> race, reserve, free;
    for (auto &r : rows) {
        if (teamless(r.rec))
            free.push_back(&r);
        else if (has_car(r.rec))
            race.push_back(&r);
        else
            reserve.push_back(&r);
    }
    auto by_seat = [](const Row *a, const Row *b) {
        uint32_t ta = stat_of(a->rec.team_stat_id), tb = stat_of(b->rec.team_stat_id);
        if (ta != tb)
            return ta < tb;
        return position_of(a->rec) < position_of(b->rec);
    };
    std::stable_sort(race.begin(), race.end(), by_seat);
    std::stable_sort(reserve.begin(), reserve.end(), by_seat);
    std::string group = swofty::career::current_data_group();
    char season_text[96] = "season unreadable";
    if (have_season)
        std::snprintf(season_text, sizeof season_text, "season index %u (%zu season(s) created, completion %u)", s.index,
                      s.seasons, s.completion);
    swofty::log("[progressive] %s: career %s, mode %s, data group %s, %s, %zu entrant rows: %zu race seats, %zu reserves, "
                "%zu free agents",
                why, state.key[0] ? state.key : "(no key)", swofty::career::name(swofty::career::mode(state)),
                group.empty() ? "(unknown)" : group.c_str(), season_text, rows.size(), race.size(), reserve.size(),
                free.size());
    for (const Row *r : race)
        swofty::log("[progressive]   seat %s", row_text(r->rec).c_str());
    for (const Row *r : reserve)
        swofty::log("[progressive]   reserve %s", row_text(r->rec).c_str());
    std::string line;
    size_t chunk = 0;
    for (size_t i = 0; i < free.size(); ++i) {
        const EntrantRecord &r = free[i]->rec;
        line += (line.empty() ? "" : ", ") + names::key_text(Kind::driver, driver_of(r)) + " look " +
                names::key_text(Kind::look, r.driver_customisation) + " " + number(r.car_number);
        if (line.size() > 1500 || i + 1 == free.size()) {
            swofty::log("[progressive]   free agents %zu: %s", ++chunk, line.c_str());
            line.clear();
        }
    }
}

} // namespace pg
