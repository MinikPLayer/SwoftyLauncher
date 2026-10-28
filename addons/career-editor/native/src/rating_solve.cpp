// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rating_solve.hpp"

#include <cmath>

namespace ce::rating {

namespace {

constexpr int kSteps = 40;
constexpr float kExact = 0.01f;
constexpr float kNear = 0.5f;

} // namespace

Solve solve(float lo, float hi, float target, const std::function<float(float)> &value)
{
    Solve s{lo, NAN, NAN, NAN, false};
    auto miss = [&](float v) { return v >= target ? v - target : target - v + kNear; };
    auto probe = [&](float p) {
        float v = value(p);
        if (std::isfinite(v) && (std::isnan(s.value) || miss(v) < miss(s.value))) {
            s.p = p;
            s.value = v;
        }
        return v;
    };
    s.low = probe(lo);
    s.high = probe(hi);
    if (std::isfinite(s.low) && std::isfinite(s.high) && target > s.low && target < s.high) {
        float a = lo, b = hi;
        for (int i = 0; i < kSteps && !(s.value >= target && s.value - target <= kExact); ++i) {
            float m = 0.5f * (a + b);
            float v = probe(m);
            if (!std::isfinite(v))
                break;
            if (v < target)
                a = m;
            else
                b = m;
        }
    }
    s.reached = std::isfinite(s.value) && std::fabs(s.value - target) <= kNear;
    return s;
}

} // namespace ce::rating
