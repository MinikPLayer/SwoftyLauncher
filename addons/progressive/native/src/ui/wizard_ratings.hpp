// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career - Choose Driver and Team Select show the start season's driver ratings
// (progressive_cards.dll, called from cards.cpp).
//
// Choose Driver's details and Insights > Driver Ratings panel (0x14167c850, from
// Game_DriverSelect_UpdateFocusedDriver at the end of the populate and on every focus) reads the
// overall (Career_Get_DriverOverallRating) and each statistic (Career_Get_DriverStatBase_Thunk)
// from the DriverStatisticsComponent of the career entity the wizard has already made (its
// registry at +0x1030), at curve position 0. So while the start season view is active, show()
// moves every start season driver's curves in that component to his years.json ratings of that
// year (the season writer's write, stats_mem.hpp), keeping the first values it saw of every curve,
// and restore() puts them back when the view is taken back: a tile press, career.starting, a saved
// career's career.loaded, another wizard's Choose Driver, and Team HQ, where the career's own
// season writer (ratings.cpp) writes the start year's ratings itself. A curve is only put back
// when the component is still the live one and the curve still holds what show() wrote.

#pragma once

namespace pg::wizard_ratings {

void show(int year, const char *when);
void restore(const char *why, bool apply);

}
