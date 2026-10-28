// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career: what plugin.cpp runs on the core's career events.

#pragma once
#include <cstdint>
#include <string>

#include <swofty/career.hpp>

#include "seats.hpp"

namespace pg {

std::string row_text(const EntrantRecord &r);
void report(const char *why, const swofty::career::State &state);
void probe(const swofty::career::State &state);
void forget_career();

bool progressive_career();
void season_loaded(const swofty::career::State &state);
void season_unloaded();
void wizard_left(const char *group);
void wizard_abandoned(const char *why);
void rnd_option_changed();
void rnd_frame(double dt, void *user);
void season_hub(const swofty::career::State &state);

} // namespace pg
