// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career - the year on the Resume Career cards (progressive_resume.dll).
//
// Career > Driver Career > Progressive Career opens career_continue_driver when Progressive saves
// exist: up to three cards, each "DRIVER CAREER / driver / team / LAST PLAYED". The view model
// fills a card from its save slot info with Game_CareerContinue_FillSlot (0x141a0f760), whose
// first call writes the card's display_title: Vm_Holder_SetCStr(holder, "lng_homescreen_driver_career")
// at site_Game_CareerContinue_DriverSlotTitle (0x141a0f7ac; rcx the holder vm + 0x3b8 + slot *
// 0x540, rdx the key, rbx the slot info, rdi the view model, sil the slot). The holder copies the
// key into an NeStr, so any key that lives through the call will do.
//
// The cards are a Progressive list when the view model's career wizard state (vm + 0x1370, the
// state the view model's DriverCareerSlotPredicate lookup reads) holds the data group
// f1_career_progressive at +0x128, and the career content tile last pressed (the core's
// career.content.pressed) was Progressive. Then the title key becomes lng_pg_resume_<year>
// ("Progressive - <year>", lang/career.json) with the year progressive.dll's ProgressiveCardYear
// gives for the slot's player driver (slot info +0xc0, low 14 bits), team (+0xbe, low 14 bits)
// and season index (+0xac); with no year it becomes lng_pg_career ("Progressive Career"). Stock
// Driver Career lists, My Team lists (their title is the other call) and every other card line
// stay as the game writes them.
//
// The card's team. The game copies the slot's team (info_team, slot info +0xbe, (kind << 14) |
// stat) from the player's seat at every save (0x1429cec50), and the card reads it after the title
// (team node 0x141a0f8c7, the list's card team after the fill, the standings rows). A follow-mode
// career whose season apply moved the player and was not saved since still holds the seat of its
// last save (the creation save: Ferrari for a 2016 Hamilton), and loading it applies the move again.
// So when progressive.dll's ProgressiveCardTeam names the team the careers.json entry recorded at
// the card's season index, the title site writes it into the slot info in memory for the card; the
// save is never written. The original goes back on career.starting and career.loaded (the slot
// identity, driver and the written team checked first), and any save of a loaded career recomputes
// the field from the seat anyway. The Driver Career card shows no car (info_vehicle +0xc2 is read
// only by the My Team custom-team delete path), so only the team changes.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <swofty/events.hpp>
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

// bindgen exports the site as a call binding (its expect bytes are the E8), and SWOFTY_SITE takes
// only site bindings; the core installs a site mixin on either kind, so it is declared here as one.
namespace swofty::game {
inline constexpr site<"site_Game_CareerContinue_DriverSlotTitle"> pg_resume_title_site{};
}

namespace {

constexpr const char *kGroup = "f1_career_progressive";
constexpr char kStockTitle[] = "lng_homescreen_driver_career";
constexpr const char *kNoYearTitle = "lng_pg_career";
constexpr uintptr_t kVmWizard = 0x1370;
constexpr uintptr_t kWizardGroup = 0x128;
constexpr uintptr_t kVmCards = 0x328;
constexpr uintptr_t kCardStride = 0x540;
constexpr uintptr_t kCardTitleHolder = 0x90;
constexpr uintptr_t kSlotSeason = 0xac;
constexpr uintptr_t kSlotTeam = 0xbe;
constexpr uintptr_t kSlotDriver = 0xc0;
constexpr uint16_t kStatMask = 0x3fff;
constexpr int kFirstYear = 2016;
constexpr int kLastYear = 2026;

constexpr uint16_t kKindMask = 0xc000;

struct Patched {
    uintptr_t info = 0;
    uint64_t id = 0;
    uint16_t driver = 0;
    uint16_t original = 0;
    uint16_t shown = 0;
};

char g_keys[kLastYear - kFirstYear + 1][24];
std::mutex g_patch_lock;
std::vector<Patched> g_patched;
std::mutex g_pressed_lock;
std::string g_pressed;
bool g_pressed_seen = false;
int g_logged = 0;

const char *year_key(int year)
{
    if (year < kFirstYear || year > kLastYear)
        return kNoYearTitle;
    char *k = g_keys[year - kFirstYear];
    if (!k[0])
        std::snprintf(k, sizeof g_keys[0], "lng_pg_resume_%d", year);
    return k;
}

int card_year(uint32_t driver, int team, int season)
{
    using Fn = int (*)(uint32_t, int, int);
    static Fn fn = nullptr;
    static bool looked = false;
    if (!looked) {
        HMODULE m = GetModuleHandleW(L"progressive.dll");
        fn = m ? reinterpret_cast<Fn>(reinterpret_cast<void *>(GetProcAddress(m, "ProgressiveCardYear"))) : nullptr;
        looked = m != nullptr;
    }
    return fn ? fn(driver, team, season) : 0;
}

int card_team(uint32_t driver, int team, int season)
{
    using Fn = int (*)(uint32_t, int, int);
    static Fn fn = nullptr;
    static bool looked = false;
    if (!looked) {
        HMODULE m = GetModuleHandleW(L"progressive.dll");
        fn = m ? reinterpret_cast<Fn>(reinterpret_cast<void *>(GetProcAddress(m, "ProgressiveCardTeam"))) : nullptr;
        looked = m != nullptr;
    }
    return fn ? fn(driver, team, season) : -1;
}

bool writable(uintptr_t p, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<const void *>(p), &mbi, sizeof mbi) != sizeof mbi)
        return false;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD))
        return false;
    DWORD w = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (mbi.Protect & w) && p + n <= reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
}

void restore(const char *why)
{
    std::lock_guard<std::mutex> l(g_patch_lock);
    size_t back = 0, left = 0;
    for (const Patched &p : g_patched) {
        uint64_t id = 0;
        uint16_t driver = 0, team = 0;
        if (swofty::mem::read(p.info, id) && id == p.id && swofty::mem::read(p.info + kSlotDriver, driver) &&
            driver == p.driver && swofty::mem::read(p.info + kSlotTeam, team) && team == p.shown &&
            writable(p.info + kSlotTeam, sizeof team)) {
            *reinterpret_cast<uint16_t *>(p.info + kSlotTeam) = p.original;
            ++back;
        } else {
            ++left;
        }
    }
    if (!g_patched.empty())
        swofty::log("[progressive] resume: %s: %zu card team(s) back to the save's own%s", why, back,
                    left ? ", the others' slot info was already rebuilt by the game" : "");
    g_patched.clear();
}

void on_starting(const SwoftyEvent *, void *)
{
    restore("career.starting");
}

void on_loaded(const SwoftyEvent *, void *)
{
    restore("career.loaded");
}

bool pressed_progressive()
{
    std::lock_guard<std::mutex> l(g_pressed_lock);
    return !g_pressed_seen || g_pressed == kGroup;
}

bool progressive_list(uintptr_t vm)
{
    uintptr_t wizard = 0, group = 0;
    std::string g;
    return vm && swofty::mem::read(vm + kVmWizard, wizard) && wizard && swofty::mem::read(wizard + kWizardGroup, group) &&
           group && swofty::mem::c_string(group, g, 64) && g == kGroup && pressed_progressive();
}

void on_content(const SwoftyEvent *ev, void *)
{
    auto *group = static_cast<const char *>(ev->data);
    std::lock_guard<std::mutex> l(g_pressed_lock);
    g_pressed = group ? group : "";
    g_pressed_seen = true;
}

bool check()
{
    if (swofty::api->resolve(swofty::game::pg_resume_title_site.id))
        return true;
    swofty::log("[progressive] resume: binding %s does not resolve on this build or core; the Resume Career cards "
                "keep the stock title",
                swofty::game::pg_resume_title_site.id);
    return false;
}

} // namespace

SWOFTY_SITE(pg_resume_title_site, swofty::Context &c)
{
    auto key = reinterpret_cast<const char *>(c.rdx);
    if (!key || !swofty::mem::readable(key, sizeof kStockTitle) || std::strcmp(key, kStockTitle))
        return;
    uintptr_t vm = c.rdi, info = c.rbx;
    unsigned slot = static_cast<unsigned>(c.rsi & 0xff);
    if (slot > 2 || c.rcx != vm + kVmCards + slot * kCardStride + kCardTitleHolder || !info)
        return;
    if (!progressive_list(vm))
        return;
    uint16_t team = 0, driver = 0;
    int32_t season = -1;
    if (!swofty::mem::read(info + kSlotTeam, team) || !swofty::mem::read(info + kSlotDriver, driver) ||
        !swofty::mem::read(info + kSlotSeason, season))
        return;
    if (!(team & kKindMask)) {
        int shown = card_team(driver & kStatMask, team & kStatMask, season);
        uint64_t id = 0;
        if (shown >= 0 && shown <= kStatMask && shown != (team & kStatMask) && swofty::mem::read(info, id) &&
            writable(info + kSlotTeam, sizeof team)) {
            auto v = static_cast<uint16_t>(shown);
            {
                std::lock_guard<std::mutex> l(g_patch_lock);
                bool known = false;
                for (Patched &p : g_patched)
                    if (p.info == info) {
                        p.shown = v;
                        known = true;
                    }
                if (!known)
                    g_patched.push_back({info, id, driver, team, v});
                *reinterpret_cast<uint16_t *>(info + kSlotTeam) = v;
            }
            swofty::log("[progressive] resume: card %u (driver %u, season index %d) shows team %u, the career's seat "
                        "since its season apply; its save, from before the apply, has team %u (slot info changed in "
                        "memory only)",
                        slot, driver & kStatMask, season, v, team & kStatMask);
            team = v;
        }
    }
    int year = card_year(driver & kStatMask, team & kStatMask, season);
    const char *title = year ? year_key(year) : kNoYearTitle;
    c.rdx = reinterpret_cast<uintptr_t>(title);
    if (g_logged++ < 24)
        swofty::log("[progressive] resume: card %u (driver %u, team %u, season index %d): %s", slot, driver & kStatMask,
                    team & kStatMask, season, year ? title : "no careers.json match, Progressive Career");
}

SWOFTY_ON_LOAD
{
    uint32_t token = 0;
    swofty::events::Result r = swofty::events::subscribe(SWOFTY_EVENT_CAREER_CONTENT_PRESSED, on_content, nullptr, &token);
    swofty::events::Result s = swofty::events::subscribe(SWOFTY_EVENT_CAREER_STARTING, on_starting, nullptr, &token);
    swofty::events::Result l = swofty::events::subscribe(SWOFTY_EVENT_CAREER_LOADED, on_loaded, nullptr, &token);
    swofty::log("[progressive] resume: Resume Career cards of Progressive careers show their year and the career's "
                "seat (content tile events: %s, career.starting: %s, career.loaded: %s)",
                swofty::events::name(r), swofty::events::name(s), swofty::events::name(l));
}

SWOFTY_ADDON(check)
