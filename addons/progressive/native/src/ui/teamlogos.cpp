// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career - every team logo and badge follows the career's year
// (progressive_teamlogos.dll).
//
// The UI resolves each team logo and badge image by a registry path such as
// "#overrides.teams.badges_broadcast.forceindia_26" or
// "overrides.osd.timing_tower.team_badges_hud.<label>" through Vm_Resolve_RegistryNode (0x1442b1b60):
// the formatted keys of Team HQ, standings, results, Team Select and Choose Driver (0x1416864d0), the
// timing tower and broadcast graphics (0x1421a9060) and the UI's own bindings. The wrap gives a path
// of a team logo array (cards_logic.hpp kLogoArrays, the arrays Classic Seasons extends) whose label
// the year maps the year's Classic Seasons key (cards_logic.hpp logo_path(), data/years.json), so every
// screen shows the year's logo. The year is the start season of an active Progressive wizard
// (progressive_cards.dll ProgressiveCardsStartYear), else the year whose team names progressive.dll
// shows (ProgressiveNamesYear: a Progressive career below 2026). 2026 and later, and every other
// career and mode, resolve the stock paths. The resolver copies the path (up to 511 characters)
// during the call, so the rewritten path lives in a thread-local buffer.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <swofty/game.hpp>
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>
#include <swofty/storage.hpp>

#include "../cards_logic.hpp"

namespace swofty::game {
using pg_vm_resolve_t = void *(void *registry, const char *path, const void *base, void *create);
// Declared here under its own name: the KB row Vm_Resolve_RegistryNode is exported by bindgen.
inline constexpr fn<"Vm_Resolve_RegistryNode", pg_vm_resolve_t> pg_vm_resolve{};
} // namespace swofty::game

namespace {

namespace pc = progressive::cards;

constexpr const char *kYearsPath = "data/years.json";
constexpr uint64_t kRetryMs = 2000;

std::mutex g_lock;
bool g_tried = false;
std::atomic<bool> g_ready{false};
std::vector<pc::LogoMap> g_logos;
std::atomic<int> g_logged{0};

using YearFn = int (*)(void);

struct Export {
    const wchar_t *module;
    const char *name;
    std::atomic<YearFn> fn{nullptr};
    std::atomic<uint64_t> next{0};
};

Export g_cards{L"progressive_cards.dll", "ProgressiveCardsStartYear"};
Export g_career{L"progressive.dll", "ProgressiveNamesYear"};

int call(Export &e)
{
    YearFn f = e.fn.load();
    if (!f) {
        uint64_t now = GetTickCount64();
        if (now < e.next.load())
            return 0;
        e.next = now + kRetryMs;
        HMODULE m = GetModuleHandleW(e.module);
        f = m ? reinterpret_cast<YearFn>(GetProcAddress(m, e.name)) : nullptr;
        if (!f)
            return 0;
        e.fn = f;
    }
    return f();
}

int year()
{
    int y = call(g_cards);
    return y ? y : call(g_career);
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
        swofty::log("[progressive] team logos: %s is missing or not JSON; every team logo stays the career team's",
                    kYearsPath);
        return;
    }
    std::vector<std::string> missing;
    g_logos = pc::logo_map(doc, missing);
    swofty::log("[progressive] team logos: %zu season label(s) mapped from %s (%zu logo key(s), %zu Classic Seasons "
                "key(s) without a slice keep the career team's logo)",
                g_logos.size(), kYearsPath, doc["logo_keys"].a.size(), missing.size());
    g_ready = true;
}

// Cheap test before anything else: the wrap sees every registry lookup.
bool maybe_logo(const char *path)
{
    return path && (path[0] == '#' ? path[1] == 'o' : path[0] == 'o');
}

thread_local std::string t_path;

} // namespace

SWOFTY_WRAP(pg_vm_resolve, void *, void *registry, const char *path, const void *base, void *create)
{
    if (!maybe_logo(path))
        return orig(registry, path, base, create);
    int y = year();
    if (!y)
        return orig(registry, path, base, create);
    if (!g_ready.load())
        load();
    if (g_ready.load() && pc::logo_path(g_logos, y, path, t_path)) {
        if (g_logged.fetch_add(1) < 12)
            swofty::log("[progressive] team logos %d: %s -> %s", y, path, t_path.c_str());
        return orig(registry, t_path.c_str(), base, create);
    }
    return orig(registry, path, base, create);
}

SWOFTY_ON_LOAD
{
    swofty::log("[progressive] team logos: every team logo and badge follows the Progressive career's year");
}

SWOFTY_ADDON()
