// SPDX-License-Identifier: MIT
//
// swofty.progressive.season.apply, the hook point Progressive Career defines (standard library 1.6,
// swofty/hooks.hpp). progressive.dll fires it once per loaded career and year, at the first Team HQ that
// plans that year's line-up, with the year's effective career.season record:
//   before   handlers may change *season; Progressive seats, swaps and rates that year from the changed
//            record for the rest of the session
//   replace  a handler that returns true has applied the season itself: Progressive leaves that year's
//            seats and cars alone for the rest of the session (its ratings and team names still apply)
//   after    handlers see the season Progressive applies
// season points at Progressive's own copy, valid for the call only.

#pragma once
#include <cstdint>

#include <swofty/systems/career.hpp>

namespace progressive_api {

struct SeasonApply {
    static constexpr const char *point = "swofty.progressive.season.apply";
    static constexpr uint32_t version = 1;
    uint32_t size;
    uint32_t payload_version;
    int year;
    swofty::career::Season *season;
};

}
