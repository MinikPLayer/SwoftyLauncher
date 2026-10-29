// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career on the registry (standard library 1.6).
//   read_years     data/years.json as the career reads it. On a core with the registry (API 9) the seats,
//                  mid-season changes and ratings are the effective career.season and career.driver_ratings
//                  records, with every addon's layers (records_logic.hpp), and `season` (when given) in
//                  place of its year's record; on an older core it is the file as it is.
//   season_apply   fires swofty.progressive.season.apply (api/progressive_api/hooks.hpp) for a year with
//                  its effective record: 0 apply as planned, 1 a before handler changed the season (text is
//                  years.json with the changed season), 2 a replace handler applied it.
//   core_ops       the core's career operations (SwoftyCoreCareerOps) on a 1.6 core, else null.
//   report_season  tells the core the year of the career's season at the Championship's season_index
//                  (career::report_season), so it fires career.season.start and freezes the season; the
//                  result's name.
//   publish        the progressive.history service (api/progressive_api/history.hpp) and the hook point.

#pragma once
#include <cstdint>
#include <string>

#include <swofty_api.h>

namespace swofty::career {
struct Season;
}

namespace pg::records {

bool read_years(std::string &text, const char *who, const swofty::career::Season *season = nullptr);
int season_apply(int year, std::string &text);
const SwoftyCareerOps *core_ops();
const char *report_season(int year, int season_index);
void publish(uint32_t (*real_team)(uint32_t driver_stat_id, int year), int (*career_year)(void), int (*follow_mode)(void));

}
