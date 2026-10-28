// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career - Back in the wizard (progressive_back.dll): the season length step returns to
// the career setup page, Choose Driver returns to the settings step.
//
// The launcher's career setup page (swofty.json career_setup) opens the wizard's season length
// step on CONFIRM and builds career_wizard_season_length_select.back_career_wizard_driver_career_progressive,
// which closes the step and opens the page again. The game's own Back there fires "back" (to the
// career content select) from the wizard step view models' shared command handler
// (site_Game_CareerWizard_BackRoute 0x14bc7ab4d: rdx the route name, rsi the view model, +0x270 the
// wizard state), after it popped the wizard. For a wizard of f1_career_progressive whose step and
// option stacks (+0xe0..+0xe8, +0x100..+0x108) are empty after that pop, which is the first wizard
// step the page opens, the route becomes back_career_wizard_driver_career_progressive. The settings
// step keeps its own back (its view model is told apart by its vtable).

#include <cstring>
#include <string>

#include <swofty/game.hpp>
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

namespace {

constexpr const char *kGroup = "f1_career_progressive";
constexpr char kBack[] = "back";
constexpr char kBackToPage[] = "back_career_wizard_driver_career_progressive";
constexpr uintptr_t kVmWizard = 0x270;
constexpr uintptr_t kWizardGroup = 0x128;
constexpr uintptr_t kSteps = 0xe0;
constexpr uintptr_t kOptions = 0x100;

int g_logged = 0;

bool empty_stack(uintptr_t wizard, uintptr_t at)
{
    uintptr_t b = 0, e = 0;
    return swofty::mem::read(wizard + at, b) && swofty::mem::read(wizard + at + 8, e) && b == e;
}

} // namespace

SWOFTY_SITE(site_Game_CareerWizard_BackRoute, swofty::Context &c)
{
    auto route = reinterpret_cast<const char *>(c.rdx);
    uintptr_t vm = c.rsi, vt = 0, wizard = 0, group = 0;
    std::string g;
    if (!route || !swofty::mem::readable(route, sizeof kBack) || std::strcmp(route, kBack) || !vm ||
        !swofty::mem::read(vm, vt) || vt == swofty::game::vt_Game_CareerWizardSettingsSelectViewModel.address() ||
        !swofty::mem::read(vm + kVmWizard, wizard) || !wizard || !swofty::mem::read(wizard + kWizardGroup, group) ||
        !group || !swofty::mem::c_string(group, g, 64) || g != kGroup)
        return;
    bool first = empty_stack(wizard, kSteps) && empty_stack(wizard, kOptions);
    if (!first) {
        if (g_logged++ < 8)
            swofty::log("[progressive] wizard back: not the first step (stacks not empty); the game's back stays");
        return;
    }
    c.rdx = reinterpret_cast<uintptr_t>(kBackToPage);
    swofty::log("[progressive] wizard back: season length step returns to the setup page (%s)", kBackToPage);
}

// Back on Choose Driver. The stock route (screen_group_back) returns to the Official/Custom Driver
// step, which a Progressive wizard never opens (skip_driver_type), so Back did nothing. As the F1
// 1990 wizard does (site_Game_DriverSelect_BackRoute: rdx the route, rsi the view model, +0x438 the
// wizard state), a Progressive wizard fires screen_group_back_progressive instead (erp/wizard.json:
// it closes the driver select and opens the settings step, career_wizard_settings_select) and drops
// the confirm step and the settings choice it pushed, so the settings step is the top of the wizard
// again.
namespace {

constexpr char kDriverSelectBack[] = "screen_group_back";
constexpr char kDriverSelectBackProgressive[] = "screen_group_back_progressive";
constexpr uintptr_t kVmDriverSelectWizard = 0x438;
constexpr uint8_t kStepConfirm = 6;
constexpr uint8_t kOptionRecommended = 0xb;
constexpr uint8_t kOptionCustomSettings = 0xc;

bool drop_last_if(uintptr_t wizard, uintptr_t at, bool (*want)(uint8_t))
{
    uintptr_t b = 0, e = 0;
    uint8_t last = 0;
    if (!swofty::mem::read(wizard + at, b) || !swofty::mem::read(wizard + at + 8, e) || !b || e <= b ||
        !swofty::mem::read(e - 1, last) || !want(last))
        return false;
    uintptr_t to = e - 1;
    std::memcpy(reinterpret_cast<void *>(wizard + at + 8), &to, sizeof to);
    return true;
}

} // namespace

SWOFTY_SITE(site_Game_DriverSelect_BackRoute, swofty::Context &c)
{
    auto route = reinterpret_cast<const char *>(c.rdx);
    uintptr_t vm = c.rsi, wizard = 0, group = 0;
    std::string g;
    if (!route || !swofty::mem::readable(route, sizeof kDriverSelectBack) || std::strcmp(route, kDriverSelectBack) || !vm ||
        !swofty::mem::read(vm + kVmDriverSelectWizard, wizard) || !wizard ||
        !swofty::mem::read(wizard + kWizardGroup, group) || !group || !swofty::mem::c_string(group, g, 64) || g != kGroup)
        return;
    bool step = drop_last_if(wizard, kSteps, [](uint8_t s) { return s == kStepConfirm; });
    bool option = drop_last_if(wizard, kOptions,
                               [](uint8_t o) { return o == kOptionRecommended || o == kOptionCustomSettings; });
    c.rdx = reinterpret_cast<uintptr_t>(kDriverSelectBackProgressive);
    swofty::log("[progressive] wizard back: Choose Driver returns to the settings step (%s; confirm step %s, settings "
                "choice %s)",
                kDriverSelectBackProgressive, step ? "dropped" : "not on top", option ? "dropped" : "not last");
}

SWOFTY_ADDON()
