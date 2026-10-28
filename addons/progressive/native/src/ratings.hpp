// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career: each season's real driver ratings written into the career (ratings_logic.hpp
// plans who and what; this writes it). The write is the Career Mode Editor's drivers.ratings.set
// (addons/career-editor/native/src/drivers.cpp, protocol/drivers.md), copied so this addon stands
// alone: the career's DriverCareerPlayerDriverStatisticsComponent (DriverStatisticsComponent in My
// Team), the driver's entry by driver key, and for each of the five statistics in every statistic
// set of the driver (the default one and each context-aware one) the evolution curve (and the
// patched one when the career has it) moved by one amount so that it reads the target at the
// driver's curve position, point count and x kept, then lut[] and the end value rebuilt with the
// game's own evaluator (Career_Eval_Curve), as the game's curve loader does. The save keeps the
// curves, so the ratings persist; the overall rating (Career_Get_DriverOverallRating) follows from
// them. The player's getter (vtable +0x280 0x14113e260) maps the base value through the player's
// attribute levels and adds his earned bonus, so writing his base at career creation keeps any
// progression on top. Every other driver's values are read before and after the batch and the
// batch is rolled back if one moved. Flow thread only (career.hub).

#pragma once
#include <cstdint>
#include <vector>

#include "store.hpp"

namespace pg::ratings {

void season(uintptr_t entity, store::Career *c, uint32_t index, uint32_t player, const std::vector<uint32_t> &movers);

}
