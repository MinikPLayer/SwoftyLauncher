#include "runtime.hpp"

#include <safetyhook.hpp>
#include <swofty/game.hpp>
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <atomic>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

// Team HQ hub tabs (swofty/hub.hpp, SwoftyHub). An addon's settings.json entry carries `hub_tab`
// {name, title, options, data_group} (from its manifest); careers filed under data_group get one more
// Team HQ laptop tab after STANDINGS with one page whose content template is swofty_hub_<name>.
//
// The laptop's tabs are built natively by Career_Build_HubLaptop (0x141c3f130) when the hub loads, one
// block per primary tab. A primary tab is allocated through the laptop's allocator (laptop +0x2a8,
// vt+0x18, 0xb68 bytes), constructed by 0x1414c0860 (tab, laptop+0x28, laptop+0x268, laptop, content
// "tabbing_secondary_controller", header "tabbing_secondary_tab", lng name, icon, 1) and pushed with its
// key onto the laptop's tab vector object (laptop +0x2b0 {allocator, begin +0x2b8, end +0x2c0,
// capacity}, 0x18-byte entries {u32 key, allocator, tab}) by 0x141e5dfb0, which takes the {allocator,
// tab} pair over. A page (sub-tab) is allocated through the tab's allocator (tab +0x270, 0x660 bytes),
// constructed by the tab view model constructor 0x1414bdac0 (page, laptop+0x28, laptop+0x268, laptop,
// content template, lng name, 0, "", page type, flag) and added by 0x1414c13e0 (tab, page). The STANDINGS
// block (0x141c420dc..0x141c4261e) is the pattern. The new tab is added at 0x141c42623, where every
// branch of the STANDINGS block joins and the builder starts registering each tab's view model node,
// so the new tab gets the same registration as the stock ones.
//
// The page is a plain tab view model whose vtable is copied with slot 10 (onCommandRaised) replaced:
// swofty_next:<addon>:<option> / swofty_prev:<addon>:<option> raised by its rows step the option the
// way ADDON SETTINGS does (the state's command through settings_command), anything else goes to the
// game's handler; the launcher's page raises them on its template root, so they reach this view model.
// The page's data is published at view_model_data.hud_swofty_hub_<name>: title, count and o<k>.label /
// value (the current state's text) / description, again after every change of one of its options. The tab's title is the lng key lng_swofty_hub_<name>, set to the title in capitals.
//
// The R&D tab block is skipped by the builder when the laptop's r_and_d_visible property (laptop
// +0x920, read through [rsp+0x168]) does not hold a true bool: `cmp byte [rax], 0; je 0x141c41cf2`
// at 0x141c41bac. The laptop's other R&D lookups by the tab key 0xb88a669 (0x141c43aa5 availability,
// 0x141c44d79 / 0x141c44dd9 / 0x141c44df0 the tab jump) already handle the tab being absent, which is
// the stock state when r_and_d_visible is false.
//
// hide_rnd hides the R&D tab through the tab model's own visibility flag, so it follows live. The builders
// register every tab with a tab model (laptop +0x2d0, Team HQ container +0x2e8) through 0x14431d720, which
// gives each tab a VM entry whose header_states byte holds bit 0x1 (disabled) and bit 0x2 (hidden).
// Vm_Set_TabEnabled (0x14431f470, body .data1 0x153c60e40; despite the KB name it writes the hidden bit)
// rewrites bit 0x2 of one tab's entry; the UI tab controller watches header_states (Vm_UiTabs_OnMessage
// 0x144264d50 subscribes, 0x144267740 applies: hidden -> header invisible, disabled -> header disabled)
// and, when a header's visibility changes, recounts the visible headers and lays the bar out again
// (0x144266490). Tab selection goes by the entry node (current_tab), not by index, and refuses a hidden or
// disabled tab (0x14431f6b0), so hiding the R&D tab, which sits before the hub tab, keeps the selected tab
// and its content, and the stock R&D jumps (0x141871390 -> 0x14431f6b0) refuse a hidden tab. Stock Team HQ
// hides and shows all its tabs the same way (0x1422d7bb0 / 0x1422d7a20, onboarding), and the laptop hides
// single tabs (0x141c45340). So each builder now builds the R&D tab as stock does and, right after the
// registration loop (site_Game_HubLaptopTabsRegistered 0x141c42689, site_Game_TeamHqTabsRegistered
// 0x1422d5d3b), hides it while hide_rnd is on; the container and its R&D tab are recorded there, and a
// later hide_rnd change is applied on the next flow tick to the recorded container, when its vtable and its
// R&D tab entry are still the ones recorded. Without the visibility signatures the builders fall back to
// leaving the R&D tab out (laptop: rax points at a zero byte at the gate; Team HQ: resume at the VEHICLE
// block), which only changes at the next Team HQ entry.
//
// The game also shows the tabs again on its own: Team HQ's command handler (0x1422d6d40) answers one
// message (0xcc3dd6bb) by showing every tab and raising enable_navigation (0x1422d7a20) and another
// (0x3cc065a8) by hiding every tab (0x1422d7bb0), most likely what undid the hide on the Overview tab. So a
// hook at the show-all tail (0x1422d7b2b, before enable_navigation, rsi = the container) hides the R&D tab
// again while hide_rnd is on, and the flow tick re-hides it about twice a second while Team HQ stays open,
// which covers any other path that rewrites header_states. A hide_rnd(false) is never undone: both only act
// while it is on.

bool settings_command(uint32_t hash);
bool settings_state(const std::string &addon, const std::string &option, size_t &current,
                    std::vector<std::string> *texts, std::vector<std::string> *commands, std::vector<std::string> *values);
bool ui_vm_set_path(const std::string &path, int type, const void *value);

namespace {

struct HubOption {
    std::string id, label, description;
};

struct HubTab {
    std::string addon, name, title, group, lng, content, data;
    std::vector<HubOption> options;
    uint32_t key = 0;
};

constexpr uintptr_t LAPTOP_ARG2 = 0x28, LAPTOP_ALLOC = 0x2a8, LAPTOP_ARG3 = 0x268, LAPTOP_TABS = 0x2b0,
                    LAPTOP_TABS_END = 0x2c0, TAB_ALLOC = 0x270;
constexpr size_t TAB_SIZE = 0xb68, TAB_ALIGN = 0x10, PAGE_SIZE = 0x660, PAGE_ALIGN = 8, VT_COPY = 64, VT_COMMAND = 10;
constexpr uint32_t PAGE_TYPE = 5;
constexpr char ICON[] = "overrides.career.laptop.page_icons[4]";

const runtime::Sig S_RND_GATE{"laptop R&D gate", "80 38 00 0F 84 ?? ?? ?? ?? 4C 8B BE 68 02 00 00", 0x1c41bac};
const runtime::Sig S_TABS_END{"laptop tabs end", "48 89 DA E8 ?? ?? ?? ?? 4C 8B AE B8 02 00 00", 0x1c42623,
                              runtime::Sig::addr, 0, 8, 0};
constexpr const char *P_STANDINGS = "E8 ?? ?? ?? ?? 48 89 5C 24 60 4C 89 6C 24 68 48 8D 94 24 B0 00 00 00 4C 8D 44 24 60 "
                                    "48 8B 8C 24 00 01 00 00 E8 ?? ?? ?? ?? 48 8B 86 C0 02 00 00 48 8B 68 F8";
const runtime::Sig S_TAB_CTOR{"laptop tab constructor", P_STANDINGS, 0x14c0860, runtime::Sig::rel32, 0, 1, 5};
const runtime::Sig S_TAB_PUSH{"laptop tab push", P_STANDINGS, 0x1e5dfb0, runtime::Sig::rel32, 0x24, 0x25, 5};
const runtime::Sig S_PAGE_CTOR{"tab view model constructor",
                               "C6 44 24 48 00 C7 44 24 40 05 00 00 00 48 C7 44 24 30 00 00 00 00 48 89 D9 4C 89 EA 4D "
                               "89 F8 49 89 F1 E8 ?? ?? ?? ?? 48 8D 05",
                               0x14bdac0, runtime::Sig::rel32, 0x22, 0x23, 5};
const runtime::Sig S_PAGE_ADD{"laptop tab add page", "48 89 DA E8 ?? ?? ?? ?? 4C 8B BE 68 02 00 00", 0x14c13e0,
                              runtime::Sig::rel32, 3, 4, 5};

// Driver Career Team HQ (screen team_hq_container): its tabs are built by the Team HQ container's own
// builder (0x1422cf7a0..0x1422d5e43), not the laptop's. Its primary tabs are 0xc08-byte Team HQ tabs:
// allocated through container +0x2c0, constructed by 0x1414bf8c0 (tab, container+0x28,
// container+0x268, container, "team_hq_tab_secondary_controller", "team_hq_secondary_tab", lng name,
// "", 1), given the class vtables (0x145787460, +0x260 0x145787540, +0x268 0x145787570) and the
// 0x80-byte field block from 0x14577c048 at +0xb78 (+0xbf8 and +0xc00 zero), then pushed onto
// container +0x2c8 (begin +0x2d0, end +0x2d8) by 0x1423ac4d0. A page is allocated through the tab's
// allocator +0x668 (0x660 bytes, align 0x10), constructed by the same tab view model constructor, and
// appended to the tab's page vector object +0x670 {allocator, begin +0x678, end +0x680, capacity}
// ({0, allocator, page} entries, the shape Career_Push_HubLaptopTab pushes); then bit 0x8 of page
// +0x628 is cleared and, when bit 0x4 is set, it is cleared and the page's vt+0xc8 runs. STANDINGS
// (0x1422d4d3e..) is the pattern, and the new tab is added at 0x1422d5cce, where the builder joins and
// starts registering every tab (container at [rsp+0x50]). The R&D tab block (0x1422d23e5..0x1422d2ce0,
// key 0xf4482186) has no gate in this builder: while hide_rnd is on, execution resumes at the VEHICLE
// block (0x1422d2ce1), which reloads everything it reads from the container. The other users of the
// R&D key (0x1422d6132 search, 0x1422d65f1 availability, 0x1422d6822 / 0x1422d6fce tab jumps) are the
// same null-tolerant lookups as the laptop's.
const runtime::Sig S_TQ_END{"team HQ tabs end", "4C 8B A6 D0 02 00 00 4C 8B AE D8 02 00 00", 0x22d5cce};
const runtime::Sig S_TQ_RND{"team HQ R&D block", "E8 ?? ?? ?? ?? 4C 8B BE 68 02 00 00 4C 8B 66 28", 0x22d23e5,
                            runtime::Sig::addr, 0, 5, 0};
const runtime::Sig S_TQ_VEHICLE{"team HQ VEHICLE block",
                                "4C 8B BE 68 02 00 00 4C 8B 66 28 C7 84 24 E0 00 00 00 01 A7 73 B0", 0x22d2ce1};
constexpr const char *P_TQ_STANDINGS = "E8 ?? ?? ?? ?? 48 8D 05 ?? ?? ?? ?? 49 89 07 48 8D 05 ?? ?? ?? ?? 49 89 87 60 02 00 "
                                       "00 48 8D 05 ?? ?? ?? ?? 49 89 87 68 02 00 00 C5 F8 10 05 ?? ?? ?? ?? C4 C1 78 11 87 "
                                       "88 0B 00 00";
const runtime::Sig S_TQ_CTOR{"team HQ tab constructor", P_TQ_STANDINGS, 0x14bf8c0, runtime::Sig::rel32, 0, 1, 5};
const runtime::Sig S_TQ_VT0{"team HQ tab vtable", P_TQ_STANDINGS, 0x5787460, runtime::Sig::riprel, 5, 8, 7};
const runtime::Sig S_TQ_VT260{"team HQ tab vtable +0x260", P_TQ_STANDINGS, 0x5787540, runtime::Sig::riprel, 0xf, 0x12, 7};
const runtime::Sig S_TQ_VT268{"team HQ tab vtable +0x268", P_TQ_STANDINGS, 0x5787570, runtime::Sig::riprel, 0x1d, 0x20, 7};
const runtime::Sig S_TQ_INIT{"team HQ tab field block", P_TQ_STANDINGS, 0x577c058, runtime::Sig::riprel, 0x2b, 0x2f, 8};
const runtime::Sig S_TQ_REGISTERED{"team HQ tabs registered", "48 8B 74 24 50 48 8D BE E8 02 00 00 31 DB EB", 0x22d5d3b};
const runtime::Sig S_LAPTOP_REGISTERED{"laptop tabs registered", "49 83 C5 18 49 39 ED 75 C7 80 BE 98 09 00 00 00",
                                       0x1c42689, runtime::Sig::addr, 0, 9, 0};
const runtime::Sig S_TAB_VISIBLE{"Vm_Set_TabEnabled (tab visibility)",
                                 "48 8D 9E E8 02 00 00 EB 27 0F 1F 00 31 C0 48 8D 90 68 02 00 00 48 85 C0 48 0F 44 D0 48 "
                                 "89 D9 45 31 C0 E8 ?? ?? ?? ??",
                                 0x431f470, runtime::Sig::rel32, 0x22, 0x23, 5};
const runtime::Sig S_TQ_SHOW_ALL{"team HQ show all tabs tail",
                                "41 B0 01 E8 ?? ?? ?? ?? 49 83 C6 18 4D 39 FE 74 37 48 8B 47 10 48 8B 4F 18", 0x22d7b2b,
                                runtime::Sig::addr, 0, 0x48, 0};
const runtime::Sig S_TQ_PUSH{"team HQ tab push",
                             "4C 89 7C 24 60 48 8D 94 24 E0 00 00 00 4C 8D 44 24 58 48 8B 8C 24 D8 00 00 00 E8 ?? ?? ?? "
                             "?? 48 8B 86 D8 02 00 00 4C 8B 68 F8",
                             0x23ac4d0, runtime::Sig::rel32, 0x1a, 0x1b, 5};

using Alloc_t = void *(void *allocator, size_t size, size_t align);
using TabCtor_t = void(void *tab, void *a2, void *a3, void *laptop, const char *content, const char *header,
                       const char *name, const char *icon, uint8_t flag);
using Push_t = void(void *tabs, const uint32_t *key, void *owned);
using PageCtor_t = void(void *page, void *a2, void *a3, void *owner, const char *content, const char *name, void *a7,
                        const char *a8, uint32_t page_type, uint8_t flag);
using AddPage_t = void(void *tab, void *page);
using Command_t = uint64_t(void *vm, swofty::game::NeStr **name, const void *data);
using SetVisible_t = void(void *model, void *tab, uint8_t visible);

struct Bar {
    const char *name;
    uintptr_t begin, end, model;
    uint32_t rnd_key;
    uintptr_t owner = 0, owner_vt = 0, rnd = 0, rnd_vt = 0;
    bool told = false;
};

struct Target {
    uintptr_t model = 0, tab = 0;
};

std::vector<HubTab> tabs;
std::atomic<bool> hide{false}, live{false}, pending{false}, laptop_flag{false}, team_hq_flag{false};
const uint8_t zero_byte = 0;
uintptr_t a_tab_ctor, a_push, a_page_ctor, a_add_page, a_set_visible;
uintptr_t a_tq_ctor, a_tq_vt0, a_tq_vt260, a_tq_vt268, a_tq_init, a_tq_push, a_tq_vehicle;
safetyhook::MidHook h_end, h_rnd, h_tq_end, h_tq_rnd, h_tq_registered, h_laptop_registered, h_tq_show_all;
SRWLOCK bars_lock = SRWLOCK_INIT;
Bar laptop_bar{"laptop", 0x2b8, 0x2c0, 0x2d0, 0xb88a669}, team_hq_bar{"Team HQ", 0x2d0, 0x2d8, 0x2e8, 0xf4482186};
void **page_vt, **page_vt_src;
Command_t *page_cmd;
std::atomic<uint32_t> told{0};

void note(const char *fmt, const char *a = "", const char *b = "", const char *c = "")
{
    if (told.fetch_add(1) < 48)
        swofty::log(fmt, a, b, c);
}

template <class T> T at(uintptr_t p)
{
    T v{};
    swofty::mem::read(p, v);
    return v;
}

void publish(const HubTab &t)
{
    std::string base = t.data;
    if (!ui_vm_set_path(base + ".title", 0, t.title.c_str())) {
        note("[hub] %s: %s did not resolve; the page shows no data", t.name.c_str(), base.c_str());
        return;
    }
    int32_t count = int32_t(t.options.size());
    ui_vm_set_path(base + ".count", 2, &count);
    for (size_t k = 0; k < t.options.size(); k++) {
        const HubOption &o = t.options[k];
        size_t current = 0;
        std::vector<std::string> texts;
        settings_state(t.addon, o.id, current, &texts, nullptr, nullptr);
        std::string row = base + ".o" + std::to_string(k);
        ui_vm_set_path(row + ".label", 0, o.label.c_str());
        ui_vm_set_path(row + ".value", 0, current < texts.size() ? texts[current].c_str() : "");
        ui_vm_set_path(row + ".description", 0, o.description.c_str());
    }
}

bool step(const HubTab &t, const std::string &option, bool next)
{
    size_t current = 0;
    std::vector<std::string> commands;
    if (!settings_state(t.addon, option, current, nullptr, &commands, nullptr) || commands.empty())
        return false;
    size_t n = commands.size(), want = next ? (current + 1) % n : (current + n - 1) % n;
    uint32_t h = 5381;
    for (const char *s = commands[want].c_str(); *s; ++s)
        h = h * 33 + uint8_t(*s);
    return settings_command(h);
}

uint64_t page_command(void *vm, swofty::game::NeStr **name, const void *data)
{
    std::string text;
    if (name && *name)
        swofty::mem::c_string(reinterpret_cast<uintptr_t>(*name) + 9, text, 160);
    bool next = text.rfind("swofty_next:", 0) == 0, prev = text.rfind("swofty_prev:", 0) == 0;
    if (next || prev) {
        std::string rest = text.substr(12);
        size_t colon = rest.find(':');
        std::string addon = rest.substr(0, colon), option = colon == std::string::npos ? "" : rest.substr(colon + 1);
        for (auto &t : tabs) {
            if (t.addon != addon)
                continue;
            for (auto &o : t.options)
                if (o.id == option) {
                    bool ok = step(t, option, next);
                    swofty::log("[hub] %s: %s %s.%s%s", t.name.c_str(), next ? "next" : "prev", addon.c_str(),
                                option.c_str(), ok ? "" : " (no such option state)");
                    return 0;
                }
        }
    }
    return page_cmd ? page_cmd(vm, name, data) : 0;
}

bool own_vtable(void *page)
{
    void **src = *static_cast<void ***>(page);
    if (!page_vt) {
        page_vt_src = src;
        auto **copy = new void *[VT_COPY + 1];
        std::memcpy(copy, src - 1, (VT_COPY + 1) * sizeof(void *));
        page_cmd = reinterpret_cast<Command_t *>(copy[1 + VT_COMMAND]);
        copy[1 + VT_COMMAND] = reinterpret_cast<void *>(&page_command);
        page_vt = copy + 1;
    }
    if (src != page_vt_src)
        return false;
    *static_cast<void ***>(page) = page_vt;
    return true;
}

void *alloc_through(uintptr_t holder)
{
    void *allocator = at<void *>(holder);
    if (!allocator || !swofty::mem::object_like(allocator))
        return nullptr;
    auto fn = at<Alloc_t *>(at<uintptr_t>(reinterpret_cast<uintptr_t>(allocator)) + 0x18);
    return swofty::mem::in_image(reinterpret_cast<uintptr_t>(fn)) ? allocator : nullptr;
}

void *allocate(void *allocator, size_t size, size_t align)
{
    auto fn = at<Alloc_t *>(at<uintptr_t>(reinterpret_cast<uintptr_t>(allocator)) + 0x18);
    return fn(allocator, size, align);
}

void add_tab(uintptr_t laptop, HubTab &t)
{
    void *allocator = alloc_through(laptop + LAPTOP_ALLOC);
    if (!allocator) {
        note("[hub] %s: the laptop has no allocator; tab not added", t.name.c_str());
        return;
    }
    void *tab = allocate(allocator, TAB_SIZE, TAB_ALIGN);
    if (!tab)
        return;
    void *a2 = at<void *>(laptop + LAPTOP_ARG2), *a3 = at<void *>(laptop + LAPTOP_ARG3);
    reinterpret_cast<TabCtor_t *>(a_tab_ctor)(tab, a2, a3, reinterpret_cast<void *>(laptop),
                                              "tabbing_secondary_controller", "tabbing_secondary_tab", t.lng.c_str(),
                                              ICON, 1);
    struct {
        void *allocator, *obj;
    } owned{allocator, tab};
    uint32_t key = t.key;
    reinterpret_cast<Push_t *>(a_push)(reinterpret_cast<void *>(laptop + LAPTOP_TABS), &key, &owned);
    uintptr_t end = at<uintptr_t>(laptop + LAPTOP_TABS_END);
    if (!end || at<void *>(end - 8) != tab) {
        note("[hub] %s: the tab is not the laptop's last tab after the push; no page added", t.name.c_str());
        return;
    }
    void *page_alloc = alloc_through(reinterpret_cast<uintptr_t>(tab) + TAB_ALLOC);
    void *page = page_alloc ? allocate(page_alloc, PAGE_SIZE, PAGE_ALIGN) : nullptr;
    if (!page) {
        note("[hub] %s: no page allocated", t.name.c_str());
        return;
    }
    reinterpret_cast<PageCtor_t *>(a_page_ctor)(page, a2, a3, reinterpret_cast<void *>(laptop), t.content.c_str(),
                                                t.lng.c_str(), nullptr, "", PAGE_TYPE, 0);
    bool mine = own_vtable(page);
    reinterpret_cast<AddPage_t *>(a_add_page)(tab, page);
    publish(t);
    swofty::log("[hub] %s: tab '%s' added after STANDINGS (key 0x%08x, page %s%s, data %s)", t.name.c_str(),
                t.title.c_str(), t.key, t.content.c_str(), mine ? "" : ", its commands stay the game's",
                t.data.c_str());
}

void add_team_hq_tab(uintptr_t hq, HubTab &t)
{
    constexpr uintptr_t HQ_ARG2 = 0x28, HQ_ALLOC = 0x2c0, HQ_ARG3 = 0x268, HQ_TABS = 0x2c8, HQ_TABS_END = 0x2d8,
                        TQ_PAGE_ALLOC = 0x668, TQ_PAGES = 0x670, TQ_FIELDS = 0xb78, TQ_FIELDS_END = 0xbf8, TQ_FLAG = 0xc00,
                        PAGE_FLAGS = 0x628, PAGE_VT_REFRESH = 0xc8;
    constexpr size_t TQ_SIZE = 0xc08;
    void *allocator = alloc_through(hq + HQ_ALLOC);
    if (!allocator) {
        note("[hub] %s: the Team HQ container has no allocator; tab not added", t.name.c_str());
        return;
    }
    auto *tab = static_cast<uint8_t *>(allocate(allocator, TQ_SIZE, TAB_ALIGN));
    if (!tab)
        return;
    void *a2 = at<void *>(hq + HQ_ARG2), *a3 = at<void *>(hq + HQ_ARG3);
    reinterpret_cast<TabCtor_t *>(a_tq_ctor)(tab, a2, a3, reinterpret_cast<void *>(hq), "team_hq_tab_secondary_controller",
                                             "team_hq_secondary_tab", t.lng.c_str(), "", 1);
    *reinterpret_cast<uintptr_t *>(tab) = a_tq_vt0;
    *reinterpret_cast<uintptr_t *>(tab + 0x260) = a_tq_vt260;
    *reinterpret_cast<uintptr_t *>(tab + 0x268) = a_tq_vt268;
    std::memcpy(tab + TQ_FIELDS, reinterpret_cast<const void *>(a_tq_init - 0x10), TQ_FIELDS_END - TQ_FIELDS);
    *reinterpret_cast<uint64_t *>(tab + TQ_FIELDS_END) = 0;
    tab[TQ_FLAG] = 0;
    struct {
        void *allocator, *obj;
    } owned{allocator, tab};
    uint32_t key = t.key;
    reinterpret_cast<Push_t *>(a_tq_push)(reinterpret_cast<void *>(hq + HQ_TABS), &key, &owned);
    uintptr_t end = at<uintptr_t>(hq + HQ_TABS_END);
    if (!end || at<void *>(end - 8) != tab) {
        note("[hub] %s: the tab is not Team HQ's last tab after the push; no page added", t.name.c_str());
        return;
    }
    void *page_alloc = alloc_through(reinterpret_cast<uintptr_t>(tab) + TQ_PAGE_ALLOC);
    auto *page = static_cast<uint8_t *>(page_alloc ? allocate(page_alloc, PAGE_SIZE, TAB_ALIGN) : nullptr);
    if (!page) {
        note("[hub] %s: no page allocated", t.name.c_str());
        return;
    }
    reinterpret_cast<PageCtor_t *>(a_page_ctor)(page, a2, a3, reinterpret_cast<void *>(hq), t.content.c_str(),
                                                t.lng.c_str(), nullptr, "", PAGE_TYPE, 0);
    bool mine = own_vtable(page);
    struct {
        void *allocator, *obj;
    } page_owned{page_alloc, page};
    uint32_t zero_key = 0;
    reinterpret_cast<Push_t *>(a_push)(tab + TQ_PAGES, &zero_key, &page_owned);
    uint8_t flags = page[PAGE_FLAGS];
    page[PAGE_FLAGS] = flags & ~0x8;
    if (flags & 0x4) {
        page[PAGE_FLAGS] &= ~0x4;
        using Refresh_t = void(void *);
        reinterpret_cast<Refresh_t *>(at<uintptr_t>(at<uintptr_t>(reinterpret_cast<uintptr_t>(page)) + PAGE_VT_REFRESH))(page);
    }
    publish(t);
    swofty::log("[hub] %s: Team HQ tab '%s' added after STANDINGS (key 0x%08x, page %s%s, data %s)", t.name.c_str(),
                t.title.c_str(), t.key, t.content.c_str(), mine ? "" : ", its commands stay the game's", t.data.c_str());
}

void team_hq_end(safetyhook::Context &c)
{
    uintptr_t hq = at<uintptr_t>(c.rsp + 0x50);
    if (!hq || hq != c.rsi || !swofty::mem::object_like(hq)) {
        note("[hub] Team HQ: the container is not where the builder keeps it; no tab");
        return;
    }
    char group[64] = {};
    if (runtime::career_content_table()->data_group(nullptr, group, sizeof group) != SWOFTY_ASSET_OK)
        return;
    for (auto &t : tabs)
        if (t.group == group)
            add_team_hq_tab(hq, t);
}

void team_hq_rnd(safetyhook::Context &c)
{
    if (!hide.load() || team_hq_flag.load())
        return;
    c.rip = a_tq_vehicle;
    note("[hub] Team HQ: the R&D tab is left out (hide_rnd)");
}

uintptr_t rnd_tab(const Bar &b, uintptr_t owner)
{
    uintptr_t p = at<uintptr_t>(owner + b.begin), e = at<uintptr_t>(owner + b.end);
    if (!p || e < p || (e - p) % 0x18 || (e - p) / 0x18 > 64)
        return 0;
    for (; p != e; p += 0x18)
        if (at<uint32_t>(p) == b.rnd_key)
            return at<uintptr_t>(p + 0x10);
    return 0;
}

void set_visible(const Target &t, bool visible)
{
    reinterpret_cast<SetVisible_t *>(a_set_visible)(reinterpret_cast<void *>(t.model), reinterpret_cast<void *>(t.tab),
                                                    visible ? 1 : 0);
}

Target target(const Bar &b)
{
    return {b.owner + b.model, b.rnd + 0x268};
}

bool still_open(const Bar &b)
{
    return b.owner && swofty::mem::object_like(b.owner) && at<uintptr_t>(b.owner) == b.owner_vt &&
           rnd_tab(b, b.owner) == b.rnd && swofty::mem::object_like(b.rnd) && at<uintptr_t>(b.rnd) == b.rnd_vt;
}

void registered(Bar &b, uintptr_t owner)
{
    if (!owner || !swofty::mem::object_like(owner))
        return;
    uintptr_t rnd = rnd_tab(b, owner);
    Target t;
    bool hidden = false;
    AcquireSRWLockExclusive(&bars_lock);
    b.owner = 0;
    b.told = false;
    if (rnd && swofty::mem::object_like(rnd)) {
        b.owner = owner;
        b.owner_vt = at<uintptr_t>(owner);
        b.rnd = rnd;
        b.rnd_vt = at<uintptr_t>(rnd);
        hidden = hide.load();
        t = target(b);
    }
    ReleaseSRWLockExclusive(&bars_lock);
    if (hidden) {
        set_visible(t, false);
        swofty::log("[hub] %s: the R&D tab is built hidden (hide_rnd); it follows live from here", b.name);
    }
}

void team_hq_registered(safetyhook::Context &c)
{
    registered(team_hq_bar, at<uintptr_t>(c.rsp + 0x50));
}

void laptop_registered(safetyhook::Context &c)
{
    registered(laptop_bar, c.rsi);
}

void reassert(Bar &b, uintptr_t owner, const char *who)
{
    if (!hide.load())
        return;
    Target t;
    bool open = false, first = false;
    AcquireSRWLockExclusive(&bars_lock);
    if (b.owner && b.owner == owner && still_open(b)) {
        open = true;
        first = !b.told;
        b.told = true;
        t = target(b);
    }
    ReleaseSRWLockExclusive(&bars_lock);
    if (!open)
        return;
    set_visible(t, false);
    if (first)
        swofty::log("[hub] %s: the game showed the R&D tab again (%s); hidden again", b.name, who);
}

void team_hq_show_all(safetyhook::Context &c)
{
    reassert(team_hq_bar, c.rsi, "Team HQ showed all its tabs, enable_navigation");
}

void keep(Bar &b, bool change, bool on)
{
    Target t;
    bool open = false, closed = false;
    AcquireSRWLockExclusive(&bars_lock);
    if (b.owner) {
        open = still_open(b);
        closed = !open;
        if (open)
            t = target(b);
        else
            b.owner = 0;
    }
    ReleaseSRWLockExclusive(&bars_lock);
    if (open) {
        set_visible(t, !on);
        if (change)
            swofty::log("[hub] %s: R&D tab %s live", b.name, on ? "hidden" : "shown");
    } else if (closed && change)
        swofty::log("[hub] %s: no longer open; its R&D tab follows at the next build", b.name);
}

void tabs_end(safetyhook::Context &c)
{
    uintptr_t laptop = c.rsi;
    if (!laptop || !swofty::mem::object_like(laptop))
        return;
    char group[64] = {};
    if (runtime::career_content_table()->data_group(nullptr, group, sizeof group) != SWOFTY_ASSET_OK)
        return;
    for (auto &t : tabs)
        if (t.group == group)
            add_tab(laptop, t);
}

void rnd_gate(safetyhook::Context &c)
{
    if (!hide.load() || laptop_flag.load())
        return;
    c.rax = reinterpret_cast<uintptr_t>(&zero_byte);
    note("[hub] laptop: the R&D tab is left out (hide_rnd)");
}

std::string upper(std::string s)
{
    for (auto &ch : s)
        ch = char(std::toupper(uint8_t(ch)));
    return s;
}

void load()
{
    std::string text = runtime::read_file(runtime::under_swofty("settings.json"), size_t(16) << 20);
    json::Value doc;
    if (text.empty() || !json::parse(text, doc))
        return;
    for (auto &m : doc.field("addons", "mods").a) {
        const json::Value &h = m["hub_tab"];
        if (h.type != json::Value::Obj || (m["enabled"].type == json::Value::Bool && !m["enabled"].b))
            continue;
        HubTab t;
        t.addon = m["id"].str();
        t.name = h["name"].str();
        t.title = h["title"].str(m["name"].str(t.addon.c_str()).c_str());
        t.group = h["data_group"].str();
        if (t.name.empty() || t.group.empty())
            continue;
        t.lng = "lng_swofty_hub_" + t.name;
        t.content = "swofty_hub_" + t.name;
        t.data = "view_model_data.hud_swofty_hub_" + t.name;
        t.key = runtime::fnv32(t.content.c_str());
        for (auto &id : h["options"].a)
            for (auto &o : m["options"].a)
                if (o["id"].str() == id.str())
                    t.options.push_back({id.str(), o["label"].str(id.str().c_str()), o["description"].str()});
        tabs.push_back(std::move(t));
    }
}

void setup()
{
    load();
    if (tabs.empty())
        return;
    a_tab_ctor = runtime::find(S_TAB_CTOR, "hub tabs", "hub");
    a_push = runtime::find(S_TAB_PUSH, "hub tabs", "hub");
    a_page_ctor = runtime::find(S_PAGE_CTOR, "hub tabs", "hub");
    a_add_page = runtime::find(S_PAGE_ADD, "hub tabs", "hub");
    uintptr_t end = runtime::find(S_TABS_END, "hub tabs", "hub"), gate = runtime::find(S_RND_GATE, "hub tabs", "hub");
    a_tq_ctor = runtime::find(S_TQ_CTOR, "hub tabs", "hub");
    a_tq_vt0 = runtime::find(S_TQ_VT0, "hub tabs", "hub");
    a_tq_vt260 = runtime::find(S_TQ_VT260, "hub tabs", "hub");
    a_tq_vt268 = runtime::find(S_TQ_VT268, "hub tabs", "hub");
    a_tq_init = runtime::find(S_TQ_INIT, "hub tabs", "hub");
    a_tq_push = runtime::find(S_TQ_PUSH, "hub tabs", "hub");
    a_tq_vehicle = runtime::find(S_TQ_VEHICLE, "hub tabs", "hub");
    uintptr_t tq_end = runtime::find(S_TQ_END, "hub tabs", "hub"), tq_rnd = runtime::find(S_TQ_RND, "hub tabs", "hub");
    a_set_visible = runtime::find(S_TAB_VISIBLE, "hub tabs", "hub");
    uintptr_t tq_registered = runtime::find(S_TQ_REGISTERED, "hub tabs", "hub"),
              laptop_reg = runtime::find(S_LAPTOP_REGISTERED, "hub tabs", "hub"),
              tq_show_all = runtime::find(S_TQ_SHOW_ALL, "hub tabs", "hub");
    bool laptop = a_tab_ctor && a_push && a_page_ctor && a_add_page && end && gate;
    bool team_hq = a_push && a_page_ctor && a_tq_ctor && a_tq_vt0 && a_tq_vt260 && a_tq_vt268 && a_tq_init && a_tq_push &&
                   a_tq_vehicle && tq_end && tq_rnd;
    if (laptop) {
        auto e = safetyhook::MidHook::create(reinterpret_cast<void *>(end), tabs_end);
        auto g = safetyhook::MidHook::create(reinterpret_cast<void *>(gate), rnd_gate);
        if (e && g) {
            h_end = std::move(*e);
            h_rnd = std::move(*g);
        } else
            laptop = false;
    }
    if (team_hq) {
        auto e = safetyhook::MidHook::create(reinterpret_cast<void *>(tq_end), team_hq_end);
        auto g = safetyhook::MidHook::create(reinterpret_cast<void *>(tq_rnd), team_hq_rnd);
        if (e && g) {
            h_tq_end = std::move(*e);
            h_tq_rnd = std::move(*g);
        } else
            team_hq = false;
    }
    if (laptop && a_set_visible && laptop_reg) {
        auto r = safetyhook::MidHook::create(reinterpret_cast<void *>(laptop_reg), laptop_registered);
        if (r) {
            h_laptop_registered = std::move(*r);
            laptop_flag = true;
        }
    }
    if (team_hq && a_set_visible && tq_registered) {
        auto r = safetyhook::MidHook::create(reinterpret_cast<void *>(tq_registered), team_hq_registered);
        if (r) {
            h_tq_registered = std::move(*r);
            team_hq_flag = true;
        }
    }
    if (team_hq_flag && tq_show_all) {
        auto r = safetyhook::MidHook::create(reinterpret_cast<void *>(tq_show_all), team_hq_show_all);
        if (r)
            h_tq_show_all = std::move(*r);
    }
    swofty::log("[hub] builders: Driver Career Team HQ (team_hq_container) %s, laptop (laptop_container) %s",
                team_hq ? "hooked" : "NOT hooked", laptop ? "hooked" : "NOT hooked");
    swofty::log("[hub] R&D tab: Team HQ %s, laptop %s", team_hq_flag ? "follows hide_rnd live" : "left out at the next entry",
                laptop_flag ? "follows hide_rnd live" : "left out at the next entry");
    if (!laptop && !team_hq)
        return;
    if (const SwoftyLang *l = runtime::lang_table())
        for (auto &t : tabs)
            l->override_set("*", t.lng.c_str(), upper(t.title).c_str());
    live = true;
    for (auto &t : tabs)
        swofty::log("[hub] %s: tab '%s' for careers of %s, %zu option row(s) from %s", t.name.c_str(), t.title.c_str(),
                    t.group.c_str(), t.options.size(), t.addon.c_str());
}

int api_available()
{
    return live.load() ? 1 : 0;
}

int api_hide_rnd(int on)
{
    if (hide.exchange(on != 0) != (on != 0)) {
        pending = true;
        swofty::log("[hub] R&D tab %s (hide_rnd %s)%s", on ? "hidden" : "shown", on ? "on" : "off",
                    team_hq_flag || laptop_flag ? "; an open Team HQ follows on the next frame"
                                                : " from the next Team HQ entry");
    }
    return SWOFTY_ASSET_OK;
}

int api_set_option(const char *addon, const char *option, const char *value)
{
    if (!addon || !option || !value)
        return SWOFTY_ASSET_INVALID;
    size_t current = 0;
    std::vector<std::string> commands, values;
    if (!settings_state(addon, option, current, nullptr, &commands, &values))
        return SWOFTY_ASSET_INVALID;
    for (size_t i = 0; i < values.size(); i++)
        if (values[i] == value) {
            if (i == current)
                return SWOFTY_ASSET_OK;
            uint32_t h = 5381;
            for (const char *s = commands[i].c_str(); *s; ++s)
                h = h * 33 + uint8_t(*s);
            return settings_command(h) ? SWOFTY_ASSET_OK : SWOFTY_ASSET_INVALID;
        }
    return SWOFTY_ASSET_INVALID;
}

const SwoftyHub hub_table = {SWOFTY_HUB_VERSION, api_available, api_hide_rnd, api_set_option};

}

void hub_option_changed(const std::string &addon, const std::string &option)
{
    if (!live.load())
        return;
    for (auto &t : tabs) {
        if (t.addon != addon)
            continue;
        for (auto &o : t.options)
            if (o.id == option) {
                publish(t);
                break;
            }
    }
}

namespace runtime {

void hub_tick()
{
    static uint32_t ticks;
    bool change = pending.load() && pending.exchange(false), on = hide.load();
    if (!change && !(on && ++ticks % 30 == 0))
        return;
    if (team_hq_flag.load())
        keep(team_hq_bar, change, on);
    if (laptop_flag.load())
        keep(laptop_bar, change, on);
}

}

extern "C" __declspec(dllexport) const SwoftyHub *SwoftyCoreHub(void)
{
    return &hub_table;
}

SWOFTY_ON_LOAD
{
    runtime::when_code_ready("hub tabs: laptop builder hooks", setup);
}
