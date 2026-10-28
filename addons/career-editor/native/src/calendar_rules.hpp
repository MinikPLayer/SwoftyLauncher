// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Career Mode Editor: the round-count rules of the calendar ops (protocol/season.md, "Round count").
// The game stops the career clock at its race-weekend activities, which come from the season length's
// event list in the gamedb, and runs Season.weekends[current_weekend] at each one. The season ends
// only when its last weekend is complete. So the weekend count has to match the race-weekend count.
// No game access, so native/test builds it on the host.

#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ce::cal {

constexpr uint8_t kRaceWeekend = 0x0a; // ActivityDef::type of a race weekend (names at 0x145731eb0)

// Empty when an edit that takes the season from `rounds` to `after` rounds leaves it able to finish:
// the count stays equal to `race_weekends`, or moves closer to it. Otherwise "<code>: <detail>".
// `race_weekends` < 1 means the game's race-weekend count could not be read.
std::string count_change(int rounds, int after, int race_weekends);

struct RoundState {
    std::string name;
    uint8_t track = 0xff;
    bool not_started = false;
};

struct Candidate {
    std::string name;
    uint8_t track = 0xff;
};

struct Repair {
    std::string error;               // "<code>: <detail>", empty when the plan can run
    std::vector<size_t> remove;      // round indices to remove, highest first
    std::vector<Candidate> add;      // weekends to append after the last round, in order
    bool unchanged = false;          // the count already matches
};

// The plan of season.calendar.repair. Too many rounds: the last future, not-started rounds go. Too few:
// `given` (the launcher's picks) or else `known` (weekends of this championship's other seasons, in
// order) supply new last rounds, skipping a name or track already on this calendar.
Repair repair(const std::vector<RoundState> &rounds, uint32_t current, int race_weekends,
              const std::vector<Candidate> &given, const std::vector<Candidate> &known);

} // namespace ce::cal
