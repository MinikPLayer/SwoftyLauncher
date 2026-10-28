// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Career Mode Editor: the search drivers.ratings.set runs when the game's getter isn't the curve plus
// fixed terms, as for the Driver Career player (protocol/drivers.md). No game access, so native/test
// builds it on the host.

#pragma once
#include <functional>

namespace ce::rating {

struct Solve {
    float p = 0.0f;     // the probed p whose value is nearest the target
    float value = 0.0f; // value(p)
    float low = 0.0f;   // value(lo)
    float high = 0.0f;  // value(hi)
    bool reached = false;
};

// Bisects [lo, hi] for a value that doesn't fall as p grows. value(p) writes p and returns the getter's
// raw value, so the caller writes the returned p again. reached when that value rounds to the target.
Solve solve(float lo, float hi, float target, const std::function<float(float)> &value);

} // namespace ce::rating
