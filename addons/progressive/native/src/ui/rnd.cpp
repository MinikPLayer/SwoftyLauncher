// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career - Real-life R&D (progressive_rnd.dll, DESIGN.md D9). In a Progressive career
// whose R&D mode is real_life, the car is the season's car and nothing on top: the vehicle upgrade
// fill (Game_Rnd_FillVehicleUpgrades 0x141238cb0, per car at session entry) gets progress 0 for
// every upgrade type from Game_Rnd_CalcUpgradeTypeProgress (0x143526260). Only calls made inside
// the fill are changed: the same function also feeds power-unit wear (0x1411cd6da) and the R&D
// screens, which keep the game's answer. The mode comes from progressive.dll
// (ProgressiveRndMode: 0 not a Progressive career, 1 real_life, 3 hybrid), set at Team HQ from the
// career's store entry and at once when the R&D option changes while the career is loaded.
// In-season development (development_logic.hpp): inside the fill, the Front and Rear Downforce types
// instead get the progress progressive.dll worked out for the car's team at the last load or Team
// HQ (ProgressiveDevelopmentProgress), 0 when it has none. The fill passes the team's R&D model
// (Upgrades, team key at +0x0c) + 0x1a30 as the type metadata (decomp 0x141238cb0), which names the
// team here. A probe logs Team HQ's vehicle performance comparison with its parts whenever the game
// refreshes it in a Progressive career.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <string>

#include <swofty/game.hpp>
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include "../development_logic.hpp"

namespace {

thread_local int g_fill_depth = 0;
thread_local float g_fill_dev = -1.0f;
thread_local uint32_t g_fill_team = 0;
std::atomic<uint32_t> g_frozen{0};
std::atomic<uint32_t> g_dev_lines{0};
constexpr uintptr_t kModelMeta = 0x1a30;

void *progressive_export(const char *name)
{
    HMODULE m = GetModuleHandleW(L"progressive.dll");
    return m ? reinterpret_cast<void *>(GetProcAddress(m, name)) : nullptr;
}

int rnd_mode()
{
    static int (*fn)() = nullptr;
    static bool looked = false;
    if (!looked) {
        fn = reinterpret_cast<int (*)()>(progressive_export("ProgressiveRndMode"));
        looked = GetModuleHandleW(L"progressive.dll") != nullptr;
    }
    return fn ? fn() : 0;
}

bool development(uint32_t team_key, float &progress)
{
    using Fn = int (*)(uint32_t, float *);
    static Fn fn = nullptr;
    static bool looked = false;
    if (!looked) {
        fn = reinterpret_cast<Fn>(progressive_export("ProgressiveDevelopmentProgress"));
        looked = GetModuleHandleW(L"progressive.dll") != nullptr;
    }
    return fn && fn(team_key, &progress) != 0;
}

} // namespace

SWOFTY_WRAP(Game_Rnd_FillVehicleUpgrades, void, void *component, uint8_t car, uint8_t a3, uint8_t a4)
{
    bool freeze = rnd_mode() == 1;
    if (freeze) {
        ++g_fill_depth;
        g_fill_dev = -1.0f;
        g_fill_team = 0;
    }
    orig(component, car, a3, a4);
    if (freeze) {
        --g_fill_depth;
        if (g_fill_dev >= 0.0f) {
            if (g_dev_lines++ < 200)
                swofty::log("[progressive] R&D real life: car %u (team %u) upgrade progress held at 0, downforce at "
                            "%.3f (in-season development)",
                            car, g_fill_team >> 16, g_fill_dev);
        } else if (g_frozen++ < 4) {
            swofty::log("[progressive] R&D real life: car %u's upgrade progress held at 0", car);
        }
    }
}

SWOFTY_WRAP(Game_Rnd_CalcUpgradeTypeProgress, float, void *const *instances, uint8_t type, uint8_t state, const void *meta)
{
    if (g_fill_depth > 0) {
        uint32_t key = 0;
        float p = 0.0f;
        if (!progressive::development::lever_type(type) || !meta ||
            !swofty::mem::read(reinterpret_cast<uintptr_t>(meta) - kModelMeta + offsetof(swofty::game::Upgrades, team_key),
                               key) ||
            !development(key, p))
            return 0.0f;
        g_fill_dev = p;
        g_fill_team = key;
        return p;
    }
    return orig(instances, type, state, meta);
}

// Probe of Team HQ's VEHICLE PERFORMANCE COMPARISON (Game_Refresh_PerformanceComparison 0x141890990,
// the hub widget and the R&D page): after the game fills and sorts it, each entry {team key +0,
// driver key +4, value +8} (count +0x38, 20 at +0x40, 0x14 each; mode +0x1d0, department +0x1d8,
// 5 = all) is logged with its parts read from the team's R&D model (UpgradesComponent at +0x10,
// models +0x298..+0x2a0): the type metadata's rating base sum for that department and the rest of
// the value, the completed upgrades' gain, with the room the metadata leaves for it.
constexpr uintptr_t kCmpComponent = 0x10, kCmpCount = 0x38, kCmpEntries = 0x40, kCmpStride = 0x14;
constexpr uintptr_t kCmpMode = 0x1d0, kCmpDept = 0x1d8, kCmpPlayer = 0x1f7;
constexpr uintptr_t kModels = 0x298, kTypeMeta = 0x1a38, kMetaStride = 0x20;
std::atomic<uint32_t> g_chart_lines{0};
std::string g_chart_last;

bool model_parts(uintptr_t comp, uint32_t team, uint8_t dept, double &base, double &room)
{
    uintptr_t b = 0, e = 0;
    base = room = 0;
    if (!swofty::mem::read(comp + kModels, b) || !swofty::mem::read(comp + kModels + 8, e) || e <= b || e - b > 64 * 8)
        return false;
    for (uintptr_t at = b; at < e; at += 8) {
        uintptr_t model = 0;
        uint32_t key = 0;
        if (!swofty::mem::read(at, model) || !model ||
            !swofty::mem::read(model + offsetof(swofty::game::Upgrades, team_key), key) || (key >> 16) != (team >> 16))
            continue;
        uintptr_t mb = 0, me = 0;
        if (!swofty::mem::read(model + kTypeMeta, mb) || !swofty::mem::read(model + kTypeMeta + 8, me) || me <= mb ||
            (me - mb) / kMetaStride > 64)
            return false;
        for (uintptr_t m = mb; m < me; m += kMetaStride) {
            uint8_t d = 0;
            float lo = 0, hi = 0;
            swofty::mem::read(m + 1, d);
            swofty::mem::read(m + 0xc, lo);
            swofty::mem::read(m + 0x10, hi);
            if (dept == 5 || d == dept) {
                base += lo;
                room += hi - lo;
            }
        }
        return true;
    }
    return false;
}

SWOFTY_WRAP(Game_Refresh_PerformanceComparison, void, void *comparison)
{
    orig(comparison);
    if (rnd_mode() == 0 || !comparison)
        return;
    auto c = reinterpret_cast<uintptr_t>(comparison);
    uint64_t count = 0;
    uint32_t mode = 0;
    uint8_t dept = 0, player = 0;
    uintptr_t comp = 0;
    if (!swofty::mem::read(c + kCmpCount, count) || count > 20 || !swofty::mem::read(c + kCmpMode, mode) ||
        !swofty::mem::read(c + kCmpDept, dept) || !swofty::mem::read(c + kCmpComponent, comp))
        return;
    swofty::mem::read(c + kCmpPlayer, player);
    std::string line;
    for (uint64_t i = 0; i < count; ++i) {
        uint32_t team = 0, driver = 0;
        float value = 0;
        uintptr_t at = c + kCmpEntries + i * kCmpStride;
        if (!swofty::mem::read(at, team) || !swofty::mem::read(at + 4, driver) || !swofty::mem::read(at + 8, value))
            break;
        double base = 0, room = 0;
        bool parts = comp && model_parts(comp, team, dept, base, room);
        char b[160];
        if (parts)
            std::snprintf(b, sizeof b, "%s%llu. team %u%s %.1f = base %.1f + upgrades %.1f (room %.1f)",
                          line.empty() ? "" : ", ", static_cast<unsigned long long>(i + 1), team >> 16,
                          (driver & 0xffff0000u) != 0x3fff0000u ? " (driver row)" : "", value, base, value - base, room);
        else
            std::snprintf(b, sizeof b, "%s%llu. team %u %.1f (no model)", line.empty() ? "" : ", ",
                          static_cast<unsigned long long>(i + 1), team >> 16, value);
        line += b;
    }
    char head[96];
    std::snprintf(head, sizeof head, "mode %u, department %u%s, %llu team(s)", mode, dept, dept == 5 ? " (all)" : "",
                  static_cast<unsigned long long>(count));
    std::string all = std::string(head) + ": " + line;
    if (all == g_chart_last || g_chart_lines++ >= 60)
        return;
    g_chart_last = all;
    swofty::log("[progressive] R&D chart %s", all.c_str());
}

SWOFTY_ON_LOAD
{
    swofty::log("[progressive] R&D: real-life freeze armed (only in Progressive careers set to real_life)");
}

SWOFTY_ADDON()
