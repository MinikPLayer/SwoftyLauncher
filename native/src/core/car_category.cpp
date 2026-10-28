#include "runtime.hpp"

#include <swofty/game.hpp>
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "car_category_logic.hpp"

// LAN Car Category addon boxes. The host's Car Category screen after the LAN Game dialog is the F1 World
// container's tab session_create_dlc_select_tab (CreateSessionDLCSelectTabViewModel). Its tile build
// (Game_CreateSessionDlcSelectTab_BuildTiles) fills two fixed tiles in a tile set at vm+0x630: the VM list
// buttons, then F1 2025 (vm+0x650) and F1 2026 (vm+0x6d0), each a child appended with Vm_List_AppendChild
// and filled with Game_CreateSessionDlcSelectTab_FillTile. Its command handler takes button_1 (F1 2025:
// disc content, route open_create_custom_25), button_2 (F1 2026) and back_pressed. The lobby reads the
// car category from the options store's category option in setupFromUnranked
// (Game_Network_CategoryOptionUsing), whose value names are the multiplayer category values addons add
// (Classic Seasons adds cs_f1_2016 and the like, with a scenario of the same name).
//
// Build (wrapped): after the stock build, every era box (swofty/era.hpp) whose entrant list is a category
// value, up to the launcher UI build's slots (buttons.entry_2 to entry_5, shown by the addon_select_N buttons
// it adds to the template, which raise button_3 to button_6), gets a child appended to buttons and filled
// into core-owned tile storage, which holds four VM
// handles for the view model's life. Before the next build those handles are unlinked and released the way
// the game's tile set clear releases its own (a handle the game already cleared is skipped). The log
// records the buttons child count before and after and every added box, which with a look at the screen
// says whether it spawns one tile per child or shows its two fixed slots.
//
// Press (wrapped): button_<3 + i> is added box i. It remembers that box's category value and runs the stock
// F1 2025 path (button_1). Other commands go to the stock handler; one it does not know is logged.
//
// setupFromUnranked (wrapped): the original resets the session (scenario 0), forces the category option to
// the index the content domain gives (3, f1, on the F1 2025 path), reads it and plays the scenario of that
// name, so an index set before it does not last. With a remembered value, on a game that decides the
// scenario (session role offline or host), after the original the category option's index is set to it
// through the option's vtable (+0x10 get, +0x18 set) and Game_Network_SetScenario plays the scenario of the
// value's name (Classic Seasons' cs_f1_2016 is stat id 15, setup mode swofty_f1). That call sets the setup
// mode and entrants, marks the session dirty and, on the host, sends the scenario to the players. The
// previous index comes back at the next build of the screen or a press on a stock box, if the option still
// holds ours.
//
// Every hook falls back to the stock behaviour when a shape is not what it expects.

namespace runtime {
std::vector<era::Box> era_boxes();
}

namespace {

namespace mem = swofty::mem;
namespace logic = runtime::car_category;
using swofty::game::Game_CreateSessionDlcSelectTab_BuildTiles_t;
using swofty::game::Game_CreateSessionDlcSelectTab_FillTile_t;
using swofty::game::Game_CreateSessionDlcSelectTab_HandleCommand_t;
using swofty::game::Game_Network_CategoryOptionUsing_t;
using swofty::game::Game_Network_SetScenario_t;
using swofty::game::Game_OptionsStore_FindOption_t;
using swofty::game::Vm_List_AppendChild_t;
using swofty::game::Vm_Release_Node_t;

constexpr const char *MIXIN = "swofty_core.dll (car category)";
constexpr uint32_t LOG_FIRST = 8;

constexpr size_t VM_TILE_SET = 0x630;
constexpr size_t VM_LIST_NODE = 0x638;
constexpr size_t VM_TILE_A = 0x650;
constexpr size_t VM_TILE_B = 0x6d0;
constexpr size_t VM_SPAN = 0x7a8;
constexpr size_t TILE_BYTES = 0x80;
constexpr size_t TILE_HANDLES = 4;
constexpr size_t HANDLE_WRAPPER = 0x20;

constexpr size_t STORE_MAP = 0x270;
constexpr size_t STORE_ENTRIES = 0x288;
constexpr size_t STORE_END = 0x290;
constexpr size_t STORE_ENTRY = 0x10;
constexpr size_t OPT_VALUES = 0x80;
constexpr size_t OPT_VALUES_END = 0x88;
constexpr size_t VALUE_BYTES = 0x50;
constexpr size_t VALUE_NAME = 0x38;
constexpr size_t NAME_LEN = 0x10;
constexpr size_t NAME_SRC = 0x18;
constexpr size_t NAME_PLAIN = 0x40;
constexpr size_t NAME_BUF = 0x48;
constexpr size_t DISPLAY_TEXT = 0x10;
constexpr size_t MAX_VALUES = 64;
constexpr size_t SELF_NETWORK = 0x10;
constexpr size_t NETWORK_SESSION = 0x418;
constexpr size_t SESSION_ROLE = 8;
constexpr size_t SESSION_SPEC = 0x18;
constexpr size_t SESSION_SPEC_SET = 0x1c;
constexpr size_t SESSION_SCENARIO = 0x410;
constexpr size_t SESSION_SPAN = 0x770;
constexpr size_t SCENARIOS_BEGIN = 0x68;
constexpr size_t SCENARIOS_END = 0x70;
constexpr size_t SCENARIO_BYTES = 0x50;
constexpr size_t SCENARIO_NAME_HOLDER = 8;
constexpr size_t HOLDER_NAME = 0x10;
constexpr size_t MAX_SCENARIOS = 512;
constexpr uint32_t DEFAULT_SPEC = 0x3fff0000;
constexpr size_t MAX_CHILDREN = 4096;

struct Bound {
    uintptr_t build, press, fill, append, release, setup, find, store, key, registry, set_scenario, scenarios;
} b;

struct Added {
    std::string list, value;
    uint8_t *node;
};

struct Forced {
    void *option = nullptr;
    int prior = -1, ours = -1;
};

std::recursive_mutex lock;
std::atomic<bool> offered, settled, live, off;
std::string off_why;
alignas(16) uint8_t tiles[logic::MAX_ADDED][TILE_BYTES];
size_t tiles_used;
std::vector<Added> added;
uint8_t *built_vm;
std::string chosen;
Forced forced;
Game_CreateSessionDlcSelectTab_BuildTiles_t *build_orig;
Game_CreateSessionDlcSelectTab_HandleCommand_t *press_orig;
Game_Network_CategoryOptionUsing_t *setup_orig;
uint32_t logged_builds, logged_values, logged_unknown, logged_refused, logged_setup, logged_scenario;
thread_local bool applying;

template <class T> T at(const void *base, size_t off)
{
    T v{};
    memcpy(&v, static_cast<const uint8_t *>(base) + off, sizeof v);
    return v;
}

void *registry()
{
    return *reinterpret_cast<void **>(b.registry);
}

bool code_ptr(const void *p)
{
    auto a = reinterpret_cast<uintptr_t>(p);
    uintptr_t base = swofty::api->module_base();
    return p && a >= base && a < base + 0x20000000 && mem::readable(p, 1);
}

template <class F> F *vfunc(void *object, size_t slot)
{
    if (!mem::readable(object, 8))
        return nullptr;
    auto **vt = *static_cast<void ***>(object);
    if (!mem::readable(vt, (slot + 1) * 8) || !code_ptr(vt[slot]))
        return nullptr;
    return reinterpret_cast<F *>(vt[slot]);
}

size_t children(uint8_t *list)
{
    if (!mem::readable(list, 0x88))
        return SIZE_MAX;
    if (auto *vector_list = vfunc<char(void *)>(list, 2); vector_list && vector_list(list)) {
        auto *begin = at<uint8_t *>(list, 0x40), *end = at<uint8_t *>(list, 0x48);
        return end >= begin && size_t(end - begin) / 8 <= MAX_CHILDREN ? size_t(end - begin) / 8 : SIZE_MAX;
    }
    if (auto *counted = vfunc<char(void *)>(list, 5); counted && counted(list)) {
        auto n = at<uint64_t>(list, 0x80);
        return n <= MAX_CHILDREN ? size_t(n) : SIZE_MAX;
    }
    return 0;
}

std::string text_at(const char *p)
{
    if (!p || !mem::readable(p, 1))
        return {};
    std::string out;
    for (size_t i = 0; i < logic::VALUE_NAME_MAX && mem::readable(p + i, 1) && p[i]; ++i)
        out.push_back(p[i]);
    return out;
}

void *category_option(std::string &why)
{
    void *store = *reinterpret_cast<void **>(b.store);
    if (!mem::readable(store, STORE_END + 8)) {
        why = "the options store is not readable";
        return nullptr;
    }
    const char *name = "category";
    uint64_t index = reinterpret_cast<Game_OptionsStore_FindOption_t *>(b.find)(static_cast<uint8_t *>(store) + STORE_MAP,
                                                                                 &name);
    auto *base = at<uint8_t *>(store, STORE_ENTRIES), *end = at<uint8_t *>(store, STORE_END);
    if (!base || end < base || index >= size_t(end - base) / STORE_ENTRY) {
        why = "the options store has no category option";
        return nullptr;
    }
    uint8_t *entry = base + index * STORE_ENTRY;
    void *option = mem::readable(entry, STORE_ENTRY) ? at<void *>(entry, 8) : nullptr;
    if (!mem::readable(option, OPT_VALUES_END + 8)) {
        why = "the category option is not readable";
        return nullptr;
    }
    return option;
}

std::vector<logic::Value> values_of(void *option)
{
    std::vector<logic::Value> out;
    auto *v = at<uint8_t *>(option, OPT_VALUES), *e = at<uint8_t *>(option, OPT_VALUES_END);
    if (!v || e < v || size_t(e - v) % VALUE_BYTES || size_t(e - v) / VALUE_BYTES > MAX_VALUES ||
        !mem::readable(v, size_t(e - v)))
        return out;
    const auto *key = reinterpret_cast<const uint8_t *>(b.key);
    for (uint8_t *entry = v; entry < e; entry += VALUE_BYTES) {
        logic::Value value;
        auto *display = at<uint8_t *>(entry, 0);
        if (mem::readable(display, DISPLAY_TEXT + 8))
            value.display = text_at(at<const char *>(display, DISPLAY_TEXT));
        auto *holder = at<uint8_t *>(entry, VALUE_NAME);
        auto *name = mem::readable(holder, 8) ? at<uint8_t *>(holder, 0) : nullptr;
        if (mem::readable(name, NAME_BUF + 8)) {
            if (at<int64_t>(name, NAME_PLAIN) == 1) {
                value.name = text_at(at<const char *>(name, NAME_BUF));
            } else {
                auto len = at<uint64_t>(name, NAME_LEN);
                auto *src = at<const uint8_t *>(name, NAME_SRC);
                if (len >= 2 && len <= logic::VALUE_NAME_MAX && mem::readable(src, size_t(len)) && mem::readable(key, 4))
                    value.name = logic::decode_name(src, size_t(len), key);
            }
        }
        out.push_back(value);
    }
    return out;
}

bool option_index(void *option, int &index)
{
    auto *get = vfunc<int(void *, void *)>(option, 2);
    if (!get)
        return false;
    uint64_t scratch[2] = {};
    index = get(option, scratch);
    return true;
}

bool set_option_index(void *option, int index)
{
    auto *set = vfunc<void(void *, int)>(option, 3);
    if (!set)
        return false;
    set(option, index);
    return true;
}

void restore_forced(const char *why)
{
    if (!forced.option)
        return;
    int now = -1;
    std::string err;
    void *option = category_option(err);
    if (option == forced.option && option_index(option, now) && now == forced.ours && set_option_index(option, forced.prior)) {
        int back = -1;
        option_index(option, back);
        swofty::log("[car-category] category option index %d -> %d (restored, %s)", now, back, why);
    } else {
        swofty::log("[car-category] category option left as it is (%s): %s", why,
                    option != forced.option ? "the option object changed" : "it no longer holds the forced index");
    }
    forced = {};
}

void release_tiles()
{
    size_t released = 0;
    for (size_t t = 0; t < tiles_used; ++t)
        for (size_t h = 0; h < TILE_HANDLES; ++h) {
            auto &handle = *reinterpret_cast<runtime::era::Handle *>(tiles[t] + h * HANDLE_WRAPPER + 8);
            released += logic::unlink(handle, [](uint8_t *node) {
                reinterpret_cast<Vm_Release_Node_t *>(b.release)(registry(), node);
            });
        }
    if (tiles_used)
        swofty::log("[car-category] released %zu handle(s) of %zu added tile(s) before the rebuild", released, tiles_used);
    tiles_used = 0;
    added.clear();
    built_vm = nullptr;
}

std::string hex_hash(uint32_t h)
{
    char s[16];
    snprintf(s, sizeof s, "0x%08x", h);
    return s;
}

void build_entry(void *vm)
{
    auto *v = static_cast<uint8_t *>(vm);
    {
        std::lock_guard<std::recursive_mutex> g(lock);
        release_tiles();
        restore_forced("the Car Category screen was built again");
        chosen.clear();
    }
    build_orig(vm);
    if (!live || off || !mem::readable(v, VM_SPAN))
        return;
    std::lock_guard<std::recursive_mutex> g(lock);
    uint8_t *list = at<uint8_t *>(v, VM_LIST_NODE);
    size_t before = children(list);
    if (before == SIZE_MAX) {
        swofty::log("[car-category] the buttons list at vm+0x638 (%p) has a shape this core does not know; no boxes added",
                    list);
        return;
    }
    std::string why;
    void *option = category_option(why);
    std::vector<logic::Value> values = option ? values_of(option) : std::vector<logic::Value>{};
    if (logged_values++ < LOG_FIRST) {
        std::string listed;
        for (size_t i = 0; i < values.size(); ++i)
            listed += (i ? ", " : "") + std::to_string(i) + "=" + values[i].name + " (" + values[i].display + ")";
        int index = -1;
        bool got = option && option_index(option, index);
        swofty::log("[car-category] category option %p: %zu value(s): %s; current index %s%s", option, values.size(),
                    listed.empty() ? "none" : listed.c_str(), got ? std::to_string(index).c_str() : "unknown",
                    option ? "" : (" (" + why + ")").c_str());
    }
    auto *append = reinterpret_cast<Vm_List_AppendChild_t *>(b.append);
    auto *fill = reinterpret_cast<Game_CreateSessionDlcSelectTab_FillTile_t *>(b.fill);
    for (const auto &box : runtime::era_boxes()) {
        int value = logic::value_of(values, box.entrant_list);
        if (value < 0) {
            swofty::log("[car-category] box %s from %s skipped: no category value has that game name",
                        box.entrant_list.c_str(), box.owner.c_str());
            continue;
        }
        if (tiles_used >= logic::MAX_ADDED) {
            swofty::log("[car-category] box %s from %s skipped: the screen has %zu addon box slots", box.entrant_list.c_str(),
                        box.owner.c_str(), logic::MAX_ADDED);
            continue;
        }
        void **node = append(v + VM_TILE_SET);
        if (!node || !mem::readable(node, 8)) {
            swofty::log("[car-category] appending a child to buttons gave %p; no more boxes", static_cast<void *>(node));
            break;
        }
        std::string command = "goto_" + box.entrant_list, image = runtime::era::image_of(box);
        memset(tiles[tiles_used], 0, TILE_BYTES);
        fill(tiles[tiles_used], node, box.title.c_str(), command.c_str(), image.c_str());
        auto *child = at<uint8_t *>(tiles[tiles_used], 8);
        added.push_back({box.entrant_list, values[size_t(value)].name, child});
        tiles_used++;
        swofty::log("[car-category] added box %zu (press command button_%zu = %s): %s from %s, value %d, title %s, "
                    "image %s, child node %p",
                    added.size() - 1, logic::STOCK_BOXES + added.size(),
                    hex_hash(logic::button_hash(logic::STOCK_BOXES + added.size())).c_str(), box.entrant_list.c_str(),
                    box.owner.c_str(), value, box.title.c_str(), image.c_str(), child);
    }
    built_vm = added.empty() ? nullptr : v;
    size_t after = children(list);
    if (logged_builds++ < LOG_FIRST)
        swofty::log("[car-category] Car Category built: view model %p, buttons list %p, %zu child(ren) before and %zu "
                    "after; stock tiles on nodes %p (F1 2025) and %p (F1 2026); %zu addon box(es) added",
                    vm, list, before, after, at<void *>(v, VM_TILE_A + 8), at<void *>(v, VM_TILE_B + 8), added.size());
}

char *press_entry(void *vm, void *command)
{
    auto *cmd = command ? *static_cast<const uint32_t **>(command) : nullptr;
    if (!cmd || !mem::readable(cmd, 4))
        return press_orig(vm, command);
    uint32_t hash = *cmd;
    std::unique_lock<std::recursive_mutex> g(lock);
    if (hash == logic::BUTTON_1 || hash == logic::BUTTON_2) {
        if (!chosen.empty() || forced.option)
            swofty::log("[car-category] stock box %s pressed; the addon category is dropped", hash == logic::BUTTON_1 ? "F1 2025" : "F1 2026");
        chosen.clear();
        restore_forced("a stock box was pressed");
        g.unlock();
        return press_orig(vm, command);
    }
    int box = logic::added_box_of(hash, added.size());
    if (box < 0) {
        if (hash != logic::BACK && logged_unknown++ < 16)
            swofty::log("[car-category] command %s on the Car Category screen is not a stock or addon box (%zu addon box(es))",
                        hex_hash(hash).c_str(), added.size());
        g.unlock();
        return press_orig(vm, command);
    }
    if (vm != built_vm) {
        if (logged_refused++ < LOG_FIRST)
            swofty::log("[car-category] addon box press refused: the screen that raised it is not the one the boxes were "
                        "built for");
        g.unlock();
        return press_orig(vm, command);
    }
    chosen = added[size_t(box)].value;
    swofty::log("[car-category] addon box %d pressed: category %s; running the F1 2025 path (button_1)", box,
                chosen.c_str());
    g.unlock();
    uint32_t stock = logic::BUTTON_1;
    const void *fake[4] = {&stock, nullptr, nullptr, nullptr};
    return press_orig(vm, fake);
}

std::vector<logic::Scenario> scenarios()
{
    std::vector<logic::Scenario> out;
    void *registry = *reinterpret_cast<void **>(b.scenarios);
    if (!mem::readable(registry, SCENARIOS_END + 8))
        return out;
    auto *e = at<uint8_t *>(registry, SCENARIOS_BEGIN), *end = at<uint8_t *>(registry, SCENARIOS_END);
    if (!e || end < e || size_t(end - e) % SCENARIO_BYTES || size_t(end - e) / SCENARIO_BYTES > MAX_SCENARIOS ||
        !mem::readable(e, size_t(end - e)))
        return out;
    for (; e < end; e += SCENARIO_BYTES) {
        auto *holder = at<uint8_t *>(e, SCENARIO_NAME_HOLDER);
        std::string name = mem::readable(holder, HOLDER_NAME + 8) ? text_at(at<const char *>(holder, HOLDER_NAME)) : "";
        out.push_back({e[0], name});
    }
    return out;
}

// Puts the category option on the remembered value after setupFromUnranked forced its own. True when the
// option holds it.
bool hold_category(const std::string &want)
{
    std::string why;
    void *option = category_option(why);
    std::vector<logic::Value> values = option ? values_of(option) : std::vector<logic::Value>{};
    int target = logic::value_of(values, want), before = -1, after = -1;
    if (!option) {
        swofty::log("[car-category] setupFromUnranked: category %s not set: %s", want.c_str(), why.c_str());
        return false;
    }
    if (target < 0) {
        swofty::log("[car-category] setupFromUnranked: category %s not set: no value has that name (%zu value(s))",
                    want.c_str(), values.size());
        return false;
    }
    if (!option_index(option, before) || before < 0 || size_t(before) >= values.size()) {
        swofty::log("[car-category] setupFromUnranked: category %s not set: the option's current index is %d of %zu",
                    want.c_str(), before, values.size());
        return false;
    }
    if (before == target)
        return true;
    if (!set_option_index(option, target) || !option_index(option, after) || after != target) {
        swofty::log("[car-category] setupFromUnranked: setting the category option to %d (%s) read back %d; putting %d "
                    "back",
                    target, want.c_str(), after, before);
        set_option_index(option, before);
        return false;
    }
    if (!forced.option)
        forced = {option, before, target};
    swofty::log("[car-category] setupFromUnranked: category option index %d (%s) -> %d (%s)", before,
                values[size_t(before)].name.c_str(), after, want.c_str());
    return true;
}

// Plays the scenario of the remembered value's name on the session setupFromUnranked just set up.
void play_scenario(void *self, const std::string &want)
{
    auto *network = mem::readable(self, SELF_NETWORK + 8) ? at<uint8_t *>(self, SELF_NETWORK) : nullptr;
    auto *session = mem::readable(network, NETWORK_SESSION + 8) ? at<uint8_t *>(network, NETWORK_SESSION) : nullptr;
    if (!mem::readable(session, SESSION_SPAN)) {
        swofty::log("[car-category] setupFromUnranked: scenario for %s not played: the session is not readable", want.c_str());
        return;
    }
    int role = at<int>(session, SESSION_ROLE);
    if (!logic::decides_scenario(role)) {
        if (logged_scenario++ < LOG_FIRST)
            swofty::log("[car-category] setupFromUnranked: session role %d takes the host's scenario", role);
        return;
    }
    std::vector<logic::Scenario> all = scenarios();
    int stat = logic::scenario_of(all, want);
    if (stat < 0) {
        swofty::log("[car-category] setupFromUnranked: scenario for %s not played: no scenario has that name (%zu "
                    "scenario(s))",
                    want.c_str(), all.size());
        return;
    }
    uint8_t now = at<uint8_t>(session, SESSION_SCENARIO);
    if (now == uint8_t(stat))
        return;
    uint32_t spec = at<uint8_t>(session, SESSION_SPEC_SET) ? at<uint32_t>(session, SESSION_SPEC) : DEFAULT_SPEC;
    reinterpret_cast<Game_Network_SetScenario_t *>(b.set_scenario)(session, uint8_t(stat), &spec);
    swofty::log("[car-category] setupFromUnranked: scenario %u -> %d (%s), now %u, session role %d%s", now, stat,
                want.c_str(), at<uint8_t>(session, SESSION_SCENARIO), role,
                role == 1 ? "; the host sends it to the players" : "");
}

void setup_entry(void *self)
{
    if (applying) {
        setup_orig(self);
        return;
    }
    setup_orig(self);
    std::unique_lock<std::recursive_mutex> g(lock);
    std::string want = chosen;
    if (want.empty()) {
        if (forced.option && logged_setup++ < LOG_FIRST)
            swofty::log("[car-category] setupFromUnranked with no addon category chosen; the forced index stays until "
                        "the next build or stock press");
        return;
    }
    applying = true;
    if (hold_category(want))
        play_scenario(self, want);
    applying = false;
}

uintptr_t need(const char *id, std::string &missing)
{
    uintptr_t a = swofty::api->resolve(id);
    if (!a)
        missing += std::string(missing.empty() ? "" : ", ") + id;
    return a;
}

bool bind_all(std::string &why)
{
    std::string missing;
    b.build = need("Game_CreateSessionDlcSelectTab_BuildTiles", missing);
    b.press = need("Game_CreateSessionDlcSelectTab_HandleCommand", missing);
    b.fill = need("Game_CreateSessionDlcSelectTab_FillTile", missing);
    b.append = need("Vm_List_AppendChild", missing);
    b.release = need("Vm_Release_Node", missing);
    b.setup = need("Game_Network_CategoryOptionUsing", missing);
    b.find = need("Game_OptionsStore_FindOption", missing);
    b.store = need("g_Game_OptionsStore", missing);
    b.key = need("k_Gdb_StringXorKey", missing);
    b.registry = need("g_Vm_Registry", missing);
    b.set_scenario = need("Game_Network_SetScenario", missing);
    b.scenarios = need("g_Game_Scenarios", missing);
    if (missing.empty())
        return true;
    why = "needs " + missing + ", which this build does not resolve";
    return false;
}

}

namespace runtime {

std::vector<CoreMixins> car_category_mixins()
{
    std::vector<CoreMixins> out;
    if (offered || off || !code_ready() || era_boxes().empty())
        return out;
    offered = true;
    std::string why;
    if (!bind_all(why)) {
        off_why = why;
        off = true;
        swofty::log("[car-category] addon boxes off: %s", why.c_str());
        return out;
    }
    out.push_back({MIXIN,
                   {{"Game_CreateSessionDlcSelectTab_BuildTiles", SWOFTY_MIXIN_WRAP, 100, (void *)&build_entry,
                     (void **)&build_orig},
                    {"Game_CreateSessionDlcSelectTab_HandleCommand", SWOFTY_MIXIN_WRAP, 100, (void *)&press_entry,
                     (void **)&press_orig},
                    {"Game_Network_CategoryOptionUsing", SWOFTY_MIXIN_WRAP, 100, (void *)&setup_entry,
                     (void **)&setup_orig}}});
    swofty::log("[car-category] %zu era box(es) registered; offering the Car Category hooks", era_boxes().size());
    return out;
}

void car_category_commit()
{
    if (!offered || settled || off)
        return;
    settled = true;
    if (core_mixins_active(MIXIN)) {
        live = true;
        swofty::log("[car-category] Car Category hooks installed; addon boxes show from the next build of the screen");
        return;
    }
    off_why = "the Car Category hooks did not install (see the [core] lines for " + std::string(MIXIN) + ")";
    off = true;
    swofty::log("[car-category] addon boxes off: %s", off_why.c_str());
}

}
