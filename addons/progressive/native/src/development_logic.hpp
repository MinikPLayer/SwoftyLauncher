// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career: in-season car development in a Real life R&D career (pure logic, host tested).
//
// data/years.json gives each real team of 2016-2025 a curve: checkpoints {at 0..1 of the season,
// delta_pct}, its real qualifying gap relative to its season mean (> 0 slower than the year's car;
// tools/development.py). At career round k of n (Season.current_weekend, 0-based) the season point
// is k / (n - 1), and a team's delta is the curve there (linear between checkpoints, flat before the
// first and after the last).
//
// The lever is the game's own vehicle upgrade fill (Game_Rnd_FillVehicleUpgrades 0x141238cb0, per car
// at session entry): Real life already holds every upgrade type's progress at 0 (ui/rnd.cpp); the
// Front and Rear Downforce types (9, 10; the handling file's radUpgradeFrontDownforce /
// radUpgradeRearDownforce blocks, +20-30 % lift at full progress on every season car) instead get
//   progress = (base - delta) / kGainPct, clamped to 0..1,
// with base the year's largest delta (>= 0) so the slowest point of the year sits at progress 0 and
// every team's season mean of progress is the same base / kGainPct: the year's order of cars holds on
// average, and only the in-season swings move. A slot without a curve (2026, the 10-team years'
// filler) gets delta 0, the year's mean. kGainPct is the lap time in percent that full progress on
// both types is worth, an estimate (about 0.1 % per 1 % of downforce) to calibrate in game. The
// values depend only on the year, the round and the data, so every apply and every reload gives the
// same progress; the store keeps the last applied round and deltas for the log.

#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "season_logic.hpp"

namespace progressive::development {

constexpr double kGainPct = 2.0;
constexpr uint8_t kFrontDownforce = 9;
constexpr uint8_t kRearDownforce = 10;

inline bool lever_type(uint8_t type)
{
    return type == kFrontDownforce || type == kRearDownforce;
}

inline double season_point(uint32_t round0, uint32_t rounds)
{
    if (rounds < 2)
        return 0.0;
    return std::clamp(static_cast<double>(round0) / static_cast<double>(rounds - 1), 0.0, 1.0);
}

inline double delta_at(const std::vector<season::DevPoint> &curve, double t)
{
    if (curve.empty())
        return 0.0;
    if (t <= curve.front().at)
        return curve.front().delta_pct;
    for (size_t i = 1; i < curve.size(); ++i) {
        const season::DevPoint &a = curve[i - 1], &b = curve[i];
        if (t <= b.at)
            return b.at > a.at ? a.delta_pct + (b.delta_pct - a.delta_pct) * (t - a.at) / (b.at - a.at) : b.delta_pct;
    }
    return curve.back().delta_pct;
}

inline double year_base(const season::Year &y)
{
    double base = 0.0;
    for (auto &t : y.teams)
        for (auto &c : t.development)
            base = std::max(base, c.delta_pct);
    return base;
}

inline double progress(double base, double delta, double gain = kGainPct)
{
    if (!(gain > 0))
        return 0.0;
    return std::clamp((base - delta) / gain, 0.0, 1.0);
}

struct Entry {
    int slot = 0;
    bool curve = false;
    double delta = 0;
    double progress = 0;
    bool clamped = false;
};

struct Plan {
    int year = 0;
    uint32_t round0 = 0, rounds = 0;
    double point = 0, base = 0;
    std::array<Entry, season::kSlots> teams{};
};

inline Plan plan(const season::Data &d, int year, uint32_t round0, uint32_t rounds, double gain = kGainPct)
{
    Plan p;
    p.year = year;
    p.round0 = round0;
    p.rounds = rounds;
    p.point = season_point(round0, rounds);
    const season::Year *y = d.year(year);
    if (y)
        p.base = year_base(*y);
    for (int i = 0; i < season::kSlots; ++i) {
        Entry &e = p.teams[i];
        e.slot = i + 1;
        const std::vector<season::DevPoint> *curve = y ? &y->teams[i].development : nullptr;
        e.curve = curve && !curve->empty();
        e.delta = e.curve ? delta_at(*curve, p.point) : 0.0;
        double raw = gain > 0 ? (p.base - e.delta) / gain : 0.0;
        e.progress = progress(p.base, e.delta, gain);
        e.clamped = raw < 0.0 || raw > 1.0;
    }
    return p;
}

inline bool any_curve(const Plan &p)
{
    for (auto &e : p.teams)
        if (e.curve)
            return true;
    return false;
}

inline std::string applied_text(const Plan &p)
{
    std::string out;
    for (auto &e : p.teams) {
        if (!e.curve)
            continue;
        char b[32];
        std::snprintf(b, sizeof b, "%s%d:%+.3f", out.empty() ? "" : ",", e.slot, e.delta);
        out += b;
    }
    return out;
}

} // namespace progressive::development
