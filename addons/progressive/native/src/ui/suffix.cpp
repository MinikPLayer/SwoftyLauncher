// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career - Choose Driver lists the start year's drivers (progressive_suffix.dll).
//
// The career tile router (FeTile_Route_CareerItem 0x141725620, decomp lines 135-150) copies the
// tile item's suffix (item +0x188, an NeStr, text at +0x09) into the wizard state (item +0x150):
// a char buffer {+0x148 capacity, +0x150 chars} that grows when the text does not fit. The
// driver select formats F1CAREER_%s, F2CAREER_%s and ICONS_%s with the suffix. The tile registers
// PG26; after the router, and after every wizard step (the setup page sets the start season after
// the press), a wizard of f1_career_progressive whose suffix reads PG + two digits gets the start
// season's two digits written over them, in place: same length, inside the capacity, so nothing is
// reallocated. Lists F1CAREER_PG16 .. F1CAREER_PG26 come with the addon's data. At the same points
// the start season's team names are set (progressive.dll ProgressiveWizardNames, the season apply's
// overrides), so Team Select and the rest of the wizard show them; pressing another career tile
// puts the 2026 names back.
//
// The three cars behind the season length, settings and confirm steps are the data group's
// career.career_setup.CareerSetupDataGroup SpawnedF1 car_1..car_3. The wizard state is the game's
// CareerUISetupManager: at database load it reads every CareerSetupDataGroup row into an entry
// (0x1438705b0), and the router points +0xd0 at the pressed tile's entry (0x14387d100). The
// season length step spawns the entry's cars (0x143879900, from its view model 0x141a29400) and
// shows the F1 set (0x143875010). An entry's SpawnedF1 cars are a map at +0x20 (node +0x1a slot,
// +0x1c vehicle ref, stat id in the high word) and their scene handles a map at +0x50 (+0x1c, 0xffff
// until spawned). At the same points as the suffix, and when the start season changes on the setup
// page, the f1_career_progressive entry's three refs are set to the start season's Haas, Ferrari
// and Williams (2016-2025) or the stock 2026 cars. A car already spawned keeps its vehicle until
// the next tile press despawns it.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <swofty/db.hpp>
#include <swofty/game.hpp>
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

SWOFTY_SETTING(std::string_view, start_season, "2026");

namespace {

constexpr const char *kGroup = "f1_career_progressive";
constexpr uintptr_t kItemDataGroup = 0x180;  // career content item: data group NeStr
constexpr uintptr_t kItemWizard = 0x150;     // career content item [0x2a]: the wizard state
constexpr uintptr_t kWizardGroup = 0x128;    // wizard state: data group chars
constexpr uintptr_t kWizardSuffixCap = 0x148; // wizard state: suffix buffer capacity
constexpr uintptr_t kWizardSuffix = 0x150;   // wizard state: suffix buffer chars
constexpr uintptr_t kNeStrChars = 0x09;
constexpr uintptr_t kWizardSetup = 0xd0;     // wizard state: the data group's CareerSetupDataGroup entry
constexpr uintptr_t kSetupCars = 0x20;       // entry: SpawnedF1 car map head
constexpr uintptr_t kSetupHandles = 0x50;    // entry: the F1 cars' scene handle map head
constexpr uintptr_t kNodeLeft = 0x00;
constexpr uintptr_t kNodeRoot = 0x08;        // head node: the root
constexpr uintptr_t kNodeRight = 0x10;
constexpr uintptr_t kNodeNil = 0x19;
constexpr uintptr_t kNodeSlot = 0x1a;
constexpr uintptr_t kNodeValue = 0x1c;
constexpr uint16_t kNoHandle = 0xffff;
constexpr int kBackdropCars = 3;
constexpr const char *kBackdropTeams[kBackdropCars] = {"haas", "ferrari", "williams"};
constexpr const char *kBackdrop2026[kBackdropCars] = {"gm_cadillac_2026", "ferrari_2026", "sauber_2026"};

int g_logged = 0;
int g_backdrop_logged = 0;
uintptr_t g_wizard = 0;

bool writable(uintptr_t p, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<const void *>(p), &mbi, sizeof mbi) != sizeof mbi || mbi.State != MEM_COMMIT ||
        (mbi.Protect & PAGE_GUARD) || !(mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)))
        return false;
    return p + n <= reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
}

std::string text_at(uintptr_t p)
{
    std::string s;
    if (p)
        swofty::mem::c_string(p, s, 64);
    return s;
}

int season_year(std::string_view text)
{
    std::string v(text);
    int y = std::atoi(v.c_str());
    return y < 2016 ? 2016 : y > 2026 ? 2026 : y;
}

int start_year()
{
    return season_year(start_season.get());
}

bool progressive_wizard(uintptr_t wizard)
{
    uintptr_t group = 0;
    return wizard && swofty::mem::read(wizard + kWizardGroup, group) && text_at(group) == kGroup;
}

uintptr_t find_node(uintptr_t head, uint8_t slot)
{
    uintptr_t node = 0, best = 0;
    if (!head || !swofty::mem::read(head + kNodeRoot, node))
        return 0;
    for (int depth = 0; depth < 32; ++depth) {
        uint8_t nil = 1, key = 0;
        if (!node || !swofty::mem::read(node + kNodeNil, nil))
            return 0;
        if (nil)
            break;
        if (!swofty::mem::read(node + kNodeSlot, key))
            return 0;
        if (key >= slot)
            best = node;
        if (!swofty::mem::read(node + (key < slot ? kNodeRight : kNodeLeft), node))
            return 0;
    }
    uint8_t key = 0xff;
    return best && swofty::mem::read(best + kNodeSlot, key) && key == slot ? best : 0;
}

int vehicle_stat(const std::string &label)
{
    swofty::db::Cursor c{};
    swofty::db::Value v{};
    if (swofty::db::row("entrants.vehicles", label, &c) != swofty::db::Result::ok ||
        swofty::db::cell(&c, "stat_id", &v) != swofty::db::Result::ok || v.i < 0 || v.i >= 0x3fff)
        return -1;
    return static_cast<int>(v.i);
}

std::string backdrop_label(int year, int car)
{
    char label[48];
    if (year >= 2026)
        return kBackdrop2026[car];
    if (year <= 2024)
        std::snprintf(label, sizeof label, "cs%02d_%s", year % 100, kBackdropTeams[car]);
    else
        std::snprintf(label, sizeof label, "%s_%d", kBackdropTeams[car], year);
    return label;
}

void backdrop(uintptr_t wizard, int year, const char *when)
{
    uintptr_t entry = 0, cars = 0, handles = 0;
    if (!swofty::mem::read(wizard + kWizardSetup, entry) || !entry || !swofty::mem::read(entry + kSetupCars, cars) ||
        !cars || !swofty::mem::read(entry + kSetupHandles, handles)) {
        if (g_backdrop_logged++ < 8)
            swofty::log("[progressive] backdrop: %s: the wizard has no career setup entry; the cars stay", when);
        return;
    }
    std::string changed, kept;
    for (int car = 0; car < kBackdropCars; ++car) {
        std::string label = backdrop_label(year, car);
        int stat = vehicle_stat(label);
        if (stat < 0) {
            label = kBackdrop2026[car];
            stat = vehicle_stat(label);
        }
        uintptr_t node = find_node(cars, static_cast<uint8_t>(car));
        uint32_t ref = 0;
        if (stat < 0 || !node || !swofty::mem::read(node + kNodeValue, ref) || !writable(node + kNodeValue, sizeof ref)) {
            if (g_backdrop_logged++ < 8)
                swofty::log("[progressive] backdrop: %s: car_%d (%s) left alone (vehicle stat id %d, map node %s)", when,
                            car + 1, label.c_str(), stat, node ? "found" : "missing");
            continue;
        }
        uint32_t want = (static_cast<uint32_t>(stat) << 16) | (ref & 0xffffu);
        if (want == ref)
            continue;
        uint16_t handle = kNoHandle;
        uintptr_t shown = find_node(handles, static_cast<uint8_t>(car));
        if (shown && swofty::mem::read(shown + kNodeValue, handle) && handle != kNoHandle)
            kept += (kept.empty() ? "" : ", ") + std::string("car_") + std::to_string(car + 1);
        std::memcpy(reinterpret_cast<void *>(node + kNodeValue), &want, sizeof want);
        changed += (changed.empty() ? "" : ", ") + label;
    }
    if (!changed.empty())
        swofty::log("[progressive] backdrop: %s: start season %d cars %s%s%s", when, year, changed.c_str(),
                    kept.empty() ? "" : "; already spawned until the next tile press: ", kept.c_str());
}

void wizard_names(int year)
{
    static void (*fn)(int) = nullptr;
    static bool looked = false;
    if (!looked) {
        HMODULE m = GetModuleHandleW(L"progressive.dll");
        fn = m ? reinterpret_cast<void (*)(int)>(reinterpret_cast<void *>(GetProcAddress(m, "ProgressiveWizardNames")))
               : nullptr;
        looked = m != nullptr;
    }
    if (fn)
        fn(year);
}

void new_career()
{
    static void (*fn)() = nullptr;
    static bool looked = false;
    if (!looked) {
        HMODULE m = GetModuleHandleW(L"progressive.dll");
        fn = m ? reinterpret_cast<void (*)()>(reinterpret_cast<void *>(GetProcAddress(m, "ProgressiveNewCareer")))
               : nullptr;
        looked = m != nullptr;
    }
    if (fn)
        fn();
}

void rewrite(uintptr_t wizard, const char *when, bool step)
{
    uintptr_t chars = 0;
    uint64_t cap = 0;
    if (!progressive_wizard(wizard))
        return;
    g_wizard = wizard;
    if (step)
        new_career();
    int year = start_year();
    wizard_names(year);
    backdrop(wizard, year, when);
    if (!swofty::mem::read(wizard + kWizardSuffixCap, cap) || !swofty::mem::read(wizard + kWizardSuffix, chars))
        return;
    std::string now = text_at(chars);
    char want[8];
    std::snprintf(want, sizeof want, "PG%02d", year % 100);
    bool shape = now.size() == 4 && now[0] == 'P' && now[1] == 'G' && now[2] >= '0' && now[2] <= '9' && now[3] >= '0' &&
                 now[3] <= '9';
    if (now == want)
        return;
    if (!shape || cap < 5 || !writable(chars + 2, 2)) {
        if (g_logged++ < 8)
            swofty::log("[progressive] suffix: %s: wizard suffix '%s' (capacity %llu) left alone; Choose Driver keeps it",
                        when, now.c_str(), static_cast<unsigned long long>(cap));
        return;
    }
    std::memcpy(reinterpret_cast<void *>(chars + 2), want + 2, 2);
    swofty::log("[progressive] suffix: %s: %s -> %s (start season %d): Choose Driver lists F1CAREER_%s", when, now.c_str(),
                want, year, want);
}

} // namespace

SWOFTY_WRAP(FeTile_Route_CareerItem, swofty::game::Action *, swofty::game::HomeItem *item, swofty::game::Action *out)
{
    swofty::game::Action *r = orig(item, out);
    auto at = reinterpret_cast<uintptr_t>(item);
    uintptr_t group = 0, wizard = 0;
    if (at && swofty::mem::read(at + kItemDataGroup, group) && group && text_at(group + kNeStrChars) == kGroup &&
        swofty::mem::read(at + kItemWizard, wizard))
        rewrite(wizard, "tile press", false);
    return r;
}

SWOFTY_WRAP(Game_CareerWizard_PushNextStep, void, void *wizard)
{
    orig(wizard);
    rewrite(reinterpret_cast<uintptr_t>(wizard), "wizard step", true);
}

SWOFTY_ON_CHANGE(start_season, std::string_view value)
{
    if (progressive_wizard(g_wizard))
        backdrop(g_wizard, season_year(value), "start season changed");
}

SWOFTY_ON_LOAD
{
    swofty::log("[progressive] suffix: Choose Driver follows the start season (now %d)", start_year());
}

SWOFTY_ADDON()
