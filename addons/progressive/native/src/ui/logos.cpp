// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career - the start season's team logos in Choose Driver and Team Select
// (progressive_logos.dll).
//
// The three Ne_Format_String calls that build Team Select's logo key
// (site_Game_TeamSelect_TeamLogoFormat), Choose Driver's badge key (site_Game_DriverSelect_BadgeFormat)
// and its details logo key (site_Game_DriverSelect_TeamLogoFormat) get the start season's Classic
// Seasons team key for the career team's label (cards_logic.hpp logo_map(), from data/years.json)
// while progressive_cards.dll has the start season view of a Progressive wizard active
// (ProgressiveCardsStartYear). Wizard only: the view is never active in a running career, so Team HQ
// and the other screens keep the career teams' logos. A build where one of the sites does not
// resolve drops this DLL alone; progressive_cards.dll keeps the cards, cars and team choice.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <swofty/game.hpp>
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>
#include <swofty/storage.hpp>

#include "../cards_logic.hpp"

namespace {

namespace pc = progressive::cards;
using swofty::game::NeStr;

constexpr const char *kYearsPath = "data/years.json";

std::mutex g_lock;
bool g_tried = false;
std::atomic<bool> g_ready{false};
std::vector<pc::LogoMap> g_logos;
std::atomic<int> g_logged[3];

using StartYear = int (*)(void);
std::atomic<StartYear> g_start_year{nullptr};
std::atomic<bool> g_cards_missing_logged{false};

int start_year()
{
    StartYear f = g_start_year.load();
    if (!f) {
        HMODULE cards = GetModuleHandleW(L"progressive_cards.dll");
        f = cards ? reinterpret_cast<StartYear>(GetProcAddress(cards, "ProgressiveCardsStartYear")) : nullptr;
        if (!f) {
            if (!g_cards_missing_logged.exchange(true))
                swofty::log("[progressive] logos: progressive_cards.dll (ProgressiveCardsStartYear) is not loaded; "
                            "every logo stays the career team's");
            return 0;
        }
        g_start_year = f;
    }
    return f();
}

void load()
{
    std::lock_guard<std::mutex> g(g_lock);
    if (g_tried)
        return;
    g_tried = true;
    std::string text;
    json::Value doc;
    if (swofty::storage::read(kYearsPath, text) != swofty::storage::Result::ok || !json::parse(text, doc)) {
        swofty::log("[progressive] logos: %s is missing or not JSON; every logo stays the career team's", kYearsPath);
        return;
    }
    std::vector<std::string> missing;
    g_logos = pc::logo_map(doc, missing);
    std::string list;
    for (const std::string &m : missing)
        list += (list.empty() ? "" : ", ") + m;
    swofty::log("[progressive] logos: %zu start season label(s) mapped from %s (%zu logo key(s)); %zu Classic Seasons "
                "team key(s) have no logo slice and keep the career team's logo%s%s",
                g_logos.size(), kYearsPath, doc["logo_keys"].a.size(), missing.size(), list.empty() ? "" : ": ",
                list.c_str());
    g_ready = true;
}

const char *logo(int site, const char *label)
{
    static const char *const kSites[] = {"Team Select logo", "Choose Driver badge", "Choose Driver details logo"};
    int year = start_year();
    if (!year || !label || !swofty::mem::readable(reinterpret_cast<uintptr_t>(label), 1))
        return label;
    load();
    if (!g_ready.load())
        return label;
    const char *to = pc::logo_for(g_logos, year, label);
    if (to != label && g_logged[site].fetch_add(1) < 3)
        swofty::log("[progressive] logos: %s %d: %s -> %s", kSites[site], year, label, to);
    return to;
}

} // namespace

SWOFTY_REDIRECT(site_Game_TeamSelect_TeamLogoFormat, int64_t, NeStr **out, const char *fmt, const char *label)
{
    return orig(out, fmt, logo(0, label));
}

SWOFTY_REDIRECT(site_Game_DriverSelect_BadgeFormat, int64_t, NeStr **out, const char *fmt, const char *label)
{
    return orig(out, fmt, logo(1, label));
}

SWOFTY_REDIRECT(site_Game_DriverSelect_TeamLogoFormat, int64_t, NeStr **out, const char *fmt, const char *label)
{
    return orig(out, fmt, logo(2, label));
}

SWOFTY_ON_LOAD
{
    swofty::log("[progressive] logos: Choose Driver and Team Select show the start season's team logos");
}

SWOFTY_ADDON()
