// SPDX-License-Identifier: MIT
//
// progressive.history, the service Progressive Career publishes when progressive.dll loads (standard library
// 1.6, swofty/services.hpp). Look it up with swofty::services::when<ProgressiveHistory>(...). Every function
// runs on the flow thread.
//   real_team    the career team (team stat id) the driver raced for in that year, by Progressive's career
//                slots; 0 when he did not race that year or the year is outside 2016-2026
//   career_year  the year the loaded Progressive career is in (2016-2026); 0 outside one
//   follow_mode  1 when the loaded career follows the player's real career, 0 when he chooses his teams, -1
//                outside a Progressive career

#pragma once
#include <cstdint>

struct ProgressiveHistory {
    static constexpr const char *name = "progressive.history";
    static constexpr uint32_t version = 1;

    uint32_t size;
    uint32_t (*real_team)(uint32_t driver_stat_id, int year);
    int (*career_year)(void);
    int (*follow_mode)(void);
};
