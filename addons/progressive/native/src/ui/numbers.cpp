// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career - the start season's driver numbers in Choose Driver and Team Select
// (progressive_numbers.dll).
//
// Choose Driver's details panel (0x1416a3630) and Team Select's carousel items (0x141879310) take a
// driver's number from Ent_Get_EntrantCarNumber_Thunk, which prefers the game mode's number lists and
// the DriverNameOverride component (the drivers' seasonal_infos rows of the 2026 content, last
// season's champion's #1) to the row's car_number. So a 2017 Verstappen showed 2026's #3. Right
// after each call (site_Game_DriverSelect_NumberResult, site_Game_TeamSelect_NumberResult) the
// returned number (al) becomes the driver's number in progressive_cards.dll's start season view
// (ProgressiveCardsNumber) while a Progressive wizard below 2026 is dressed. The row is rbx at the
// first site and r15 at the second; only its driver identity (+0x50, +0x52) is read. Wizard only:
// outside it the view is not active and every number is the game's.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <cstdint>

#include <swofty/game.hpp>
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

namespace {

constexpr uintptr_t kRowIdLo = 0x50;
constexpr uintptr_t kRowIdHi = 0x52;
constexpr uint64_t kRetryMs = 2000;

using NumberFn = int (*)(uint32_t);
std::atomic<NumberFn> g_number{nullptr};
std::atomic<uint64_t> g_next{0};
std::atomic<int> g_logged[2];

int view_number(uint32_t driver)
{
    NumberFn f = g_number.load();
    if (!f) {
        uint64_t now = GetTickCount64();
        if (now < g_next.load())
            return -1;
        g_next = now + kRetryMs;
        HMODULE cards = GetModuleHandleW(L"progressive_cards.dll");
        f = cards ? reinterpret_cast<NumberFn>(GetProcAddress(cards, "ProgressiveCardsNumber")) : nullptr;
        if (!f)
            return -1;
        g_number = f;
    }
    return f(driver);
}

void dress(int site, swofty::Context &c, uintptr_t row)
{
    static const char *const kSites[] = {"Choose Driver", "Team Select"};
    uint8_t lo = 0;
    uint16_t hi = 0;
    if (!row || !swofty::mem::read(row + kRowIdLo, lo) || !swofty::mem::read(row + kRowIdHi, hi))
        return;
    uint32_t driver = static_cast<uint32_t>(lo) | static_cast<uint32_t>(hi) << 16;
    int n = view_number(driver);
    uint8_t was = static_cast<uint8_t>(c.rax & 0xff);
    if (n < 0 || n == was)
        return;
    c.rax = (c.rax & ~uintptr_t(0xff)) | static_cast<uint8_t>(n);
    if (g_logged[site].fetch_add(1) < 6)
        swofty::log("[progressive] numbers: %s driver 0x%08x: #%u -> #%d (the start season's)", kSites[site], driver,
                    was, n);
}

} // namespace

SWOFTY_SITE(site_Game_DriverSelect_NumberResult, swofty::Context &c)
{
    dress(0, c, c.rbx);
}

SWOFTY_SITE(site_Game_TeamSelect_NumberResult, swofty::Context &c)
{
    dress(1, c, c.r15);
}

SWOFTY_ON_LOAD
{
    swofty::log("[progressive] numbers: Choose Driver and Team Select show the start season's driver numbers");
}

SWOFTY_ADDON()
