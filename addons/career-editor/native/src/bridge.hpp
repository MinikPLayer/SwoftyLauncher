// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Career Mode Editor: the bridge's own interfaces between bridge.cpp, state.cpp and plugin.cpp.
// Feature modules include ce.hpp only.

#pragma once
#include <cstdint>
#include <string>

#include "ce.hpp"

namespace ce {

inline constexpr const char *kVersion = "0.3.5";

uint64_t now_ms();
uint64_t wall_ms();

namespace state {

// Refreshes career / mode / team from the components helper. Flow thread, about 4 Hz.
void poll(uint64_t now);

// The "status" object of snapshot.json.
void write_status(Writer &w, uint64_t now);

// Why a batch cannot run now, or "" when it can. anytime = every op in it is CE_OP_ANYTIME.
std::string blocked(bool anytime);

// Sections may be rendered: a career is loaded and the flow is in a settled state.
bool sections_ok();

// "team_hq", "team_hq_busy", "weekend_hub", "session", "loading", "frontend" or "unknown".
const char *phase();

const char *mode_name(Mode m);

} // namespace state

namespace bridge {

void start();
void frame();

// Blocks until the background snapshot writer has written the newest snapshot (tests).
void flush_writes();

// The last rendered value of a section, or nullptr when it has not rendered (the status block
// copies season fields from the "season" section and the team from "economy").
const Json *section(const char *name);

} // namespace bridge

} // namespace ce
