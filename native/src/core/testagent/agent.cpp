#include "../runtime.hpp"

#include <safetyhook.hpp>
#include <swofty/game.hpp>
#include <swofty/mixin.hpp>
#include <swofty/test.hpp>

#include <atomic>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "agent_logic.hpp"

// The test agent (docs/design/std-2/test-suite.md, "The test run" and "The agent's commands"). It arms
// only in a test run: SWOFTY_TEST_RUN names the run folder, SWOFTY_TEST_TOKEN equals the token in its
// run.json (save_sandbox.cpp reads and checks them once and ends the game when they disagree). Only
// `swofty test` writes those, so normal Play never arms it, never loads a test DLL and never installs its
// hooks. Once armed, a watcher thread does the file IO in the run's agent/ folder (commands.jsonl in,
// results.jsonl out, loader.log tailed for log waits) and the flow tick runs the commands, one step per
// frame, on the thread the menus run on. Retiring the run (its token gone from run.json) stops the agent
// and lets go of the pad.

namespace runtime {
std::string hook_error(const safetyhook::MidHook::Error &e);
bool frame_capture_request(const std::wstring &path);
int frame_capture_poll(std::string *why);
}

namespace {

namespace ta = testagent;

// The game addresses the agent uses, each resolved by its signature in re/db/signatures.json when the
// agent arms; a build where one does not match only loses the commands that need it. Struct layouts
// below are the KB build's, so the commands that read them also check runtime::same_build().
const runtime::Sig SIG_INTRO_UPDATE{"fe_flow.state_intro_on_update", "41 57 41 56 56 57 53 48 83 EC 70 48 89 D6 48 89 CF 48 8B 89 F8 0B 00 00", 0x36da4d0};
const runtime::Sig SIG_NODE_SET_BOOL{"vm.node_set_bool", "41 56 56 57 55 53 48 81 EC 20 03 00 00 F6 41 26 01 0F 85 ?? ?? ?? ?? 44 89 CB 44 89 C7 49 89 D6 48 89 CE 48 8B 49 58 48 8B 01 FF 50 38 84 DB 75 0E 0F B6 08", 0x973e20};
const runtime::Sig SIG_SYNC_COMPLETE{"vm.sync_point_is_complete", "E9 ?? ?? ?? ?? 33 47 0F 84 F2 95 46 01 98 73 7B A4 A3", 0x42626c0};
const runtime::Sig SIG_INTRO_VT{"session.intro", "48 8D 05 ?? ?? ?? ?? 48 89 01 48 81 C1 E0 0D 00 00", 0x585bff0, runtime::Sig::riprel, 0, 3, 7};
const runtime::Sig SIG_BOOL_TYPE{"vm.bool_type_desc", "48 8D 0D ?? ?? ?? ?? FF 50 10 39 C3 75 17 48 8B 07", 0x6129dc0, runtime::Sig::riprel, 0, 3, 7};
const runtime::Sig SIG_EMPTY_NODE{"vm.empty_node_handle", "0F 85 ?? ?? ?? ?? 48 8B 1D ?? ?? ?? ?? C5 F8 57 C0 C5 F8 29 44 24 30 48 C7 44 24 40 00 00 00 00 48 8D 15 ?? ?? ?? ?? 4C 8D 05", 0x6ca0e90, runtime::Sig::riprel, 39, 42, 7};
const runtime::Sig SIG_FE_CAREER_VT{"game.fe_career_view_model", "48 8D 05 ?? ?? ?? ?? 49 89 07 48 8D 05 ?? ?? ?? ?? 49 89 87 60 02 00 00 48 8D 05 ?? ?? ?? ?? 49 89 87 68 02 00 00 48 8D 05 ?? ?? ?? ?? 49 89 87 98 02 00 00 66 41 C7 87 A8 03 00 00 00 01", 0x57385b0, runtime::Sig::riprel, 0, 3, 7};
const runtime::Sig SIG_CAREER_ITEM_VT{"fe_tiles.career_kind_item", "48 8D 05 ?? ?? ?? ?? 48 89 06 48 8D 8E 78 01 00 00", 0x5703eb0, runtime::Sig::riprel, 0, 3, 7};
const runtime::Sig SIG_CONTINUE_HANDLE{"game.career_continue_handle_command", "56 48 83 EC 20 48 8B 02 48 85 C0 74 38", 0x1a1db90};
const runtime::Sig SIG_CONTINUE_DRIVER_VT{"game.career_continue_driver_view_model", "48 8D 05 ?? ?? ?? ?? 49 89 04 24 48 8D 05 ?? ?? ?? ?? 49 89 84 24 60 02 00 00 48 8D 05 ?? ?? ?? ?? 49 89 84 24 68 02 00 00 49 C7 84 24 B8 13 00 00 00 00 00 00", 0x57264a0, runtime::Sig::riprel, 0, 3, 7};
const runtime::Sig SIG_CONTINUE_MY_TEAM_VT{"game.career_continue_my_team_view_model", "48 8D 05 ?? ?? ?? ?? 49 89 04 24 48 8D 05 ?? ?? ?? ?? 49 89 84 24 60 02 00 00 48 8D 05 ?? ?? ?? ?? 49 89 84 24 68 02 00 00 48 8B 4B E8", 0x5726670, runtime::Sig::riprel, 0, 3, 7};
const runtime::Sig SIG_HQ_UPDATE{"game.team_hq_advance_view_model_update", "56 57 53 48 83 EC 40 48 89 CE 48 8B 89 E8 06 00 00", 0x22ca380};
const runtime::Sig SIG_HQ_COMMAND{"game.team_hq_advance_view_model_on_command_raised", "41 57 41 56 41 55 41 54 56 57 55 53 48 81 EC B8 03 00 00 4C 8B B1 20 06 00 00", 0x22c3540};
const runtime::Sig SIG_HQ_VT{"game.team_hq_advance_view_model", "C4 C1 78 11 84 24 90 06 00 00 48 8D 05 ?? ?? ?? ?? 49 89 04 24 48 8D 05", 0x577a9c0, runtime::Sig::riprel, 10, 13, 7};
const runtime::Sig SIG_GAME_MANAGER{"game.manager", "48 8B B8 98 01 00 00 48 85 FF 0F 84 ?? ?? ?? ?? 48 8D 4C 24 70 E8 ?? ?? ?? ?? 48 8B 05", 0x604c2c8, runtime::Sig::riprel, 26, 29, 7};
const runtime::Sig SIG_SAVE_KEY{"game.save_mgr_system_key", "49 89 84 24 80 14 00 00 49 8B B4 24 70 14 00 00 48 8D 05", 0x6100ca8, runtime::Sig::riprel, 16, 19, 7};
const runtime::Sig SIG_SAVE_REQUEST{"game.save_mgr_request_profile_save_thunk", "E9 ?? ?? ?? ?? FF AB 62 24 E9 79 90", 0x36595b0};
const runtime::Sig SIG_VEHICLE_TABLE{"session.vehicle_table", "76 0E 48 8D 05 ?? ?? ?? ?? 4C 8B 3C D8", 0x6ce86e0, runtime::Sig::riprel, 2, 5, 7};
const runtime::Sig SIG_VEHICLE_COUNT{"session.vehicle_count", "74 E5 38 1D ?? ?? ?? ?? 76 0E 48 8D 05", 0x6ce86dc, runtime::Sig::riprel, 2, 4, 6};
const runtime::Sig SIG_ADVANCE_VT{"game.message_set_advance_destination", "48 8D 0D ?? ?? ?? ?? 48 89 4C 24 50 48 C7 44 24 60 02 00 00 00", 0x57548b8, runtime::Sig::riprel, 0, 3, 7};

struct Resolved {
    uintptr_t intro_update = 0, node_set_bool = 0, sync_complete = 0, intro_vt = 0, bool_type = 0, empty_node = 0, fe_career_vt = 0, career_item_vt = 0, continue_handle = 0, continue_driver_vt = 0, continue_my_team_vt = 0, hq_update = 0, hq_command = 0, hq_vt = 0, game_manager = 0, save_key = 0, save_request = 0, vehicle_table = 0, vehicle_count = 0, advance_vt = 0;
} ga;

void resolve_game()
{
    ga.intro_update = runtime::find(SIG_INTRO_UPDATE, "test", "test");
    ga.node_set_bool = runtime::find(SIG_NODE_SET_BOOL, "test", "test");
    ga.sync_complete = runtime::find(SIG_SYNC_COMPLETE, "test", "test");
    ga.intro_vt = runtime::find(SIG_INTRO_VT, "test", "test");
    ga.bool_type = runtime::find(SIG_BOOL_TYPE, "test", "test");
    ga.empty_node = runtime::find(SIG_EMPTY_NODE, "test", "test");
    ga.fe_career_vt = runtime::find(SIG_FE_CAREER_VT, "test", "test");
    ga.career_item_vt = runtime::find(SIG_CAREER_ITEM_VT, "test", "test");
    ga.continue_handle = runtime::find(SIG_CONTINUE_HANDLE, "test", "test");
    ga.continue_driver_vt = runtime::find(SIG_CONTINUE_DRIVER_VT, "test", "test");
    ga.continue_my_team_vt = runtime::find(SIG_CONTINUE_MY_TEAM_VT, "test", "test");
    ga.hq_update = runtime::find(SIG_HQ_UPDATE, "test", "test");
    ga.hq_command = runtime::find(SIG_HQ_COMMAND, "test", "test");
    ga.hq_vt = runtime::find(SIG_HQ_VT, "test", "test");
    ga.game_manager = runtime::find(SIG_GAME_MANAGER, "test", "test");
    ga.save_key = runtime::find(SIG_SAVE_KEY, "test", "test");
    ga.save_request = runtime::find(SIG_SAVE_REQUEST, "test", "test");
    ga.vehicle_table = runtime::find(SIG_VEHICLE_TABLE, "test", "test");
    ga.vehicle_count = runtime::find(SIG_VEHICLE_COUNT, "test", "test");
    ga.advance_vt = runtime::find(SIG_ADVANCE_VT, "test", "test");
}

constexpr DWORD POLL_MS = 20;
constexpr size_t LOG_RING = 4096;

std::atomic<bool> armed, stopped, started;
ta::Session session;
std::wstring mbox;
std::mutex q_lock;
std::deque<ta::Command> incoming;
std::vector<std::string> outgoing;
std::mutex fire_lock;
std::vector<std::string> fired_ids;
std::atomic<uint64_t> ticks;
std::mutex phase_lock;
std::string screen_name, flow_name;
std::mutex log_lock;
std::deque<std::string> log_ring;
uint64_t log_total;

void emit(const std::string &line)
{
    std::lock_guard<std::mutex> g(q_lock);
    outgoing.push_back(line);
}

bool append(const std::wstring &path, const std::string &text)
{
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD w = 0;
    bool ok = WriteFile(h, text.data(), DWORD(text.size()), &w, nullptr) && w == text.size();
    CloseHandle(h);
    return ok;
}

// New bytes of a growing file, from *offset on.
std::string tail(const std::wstring &path, uint64_t *offset)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return {};
    LARGE_INTEGER size{};
    std::string out;
    if (GetFileSizeEx(h, &size) && uint64_t(size.QuadPart) > *offset) {
        uint64_t n = std::min<uint64_t>(uint64_t(size.QuadPart) - *offset, 1 << 20);
        LARGE_INTEGER at;
        at.QuadPart = LONGLONG(*offset);
        out.resize(size_t(n));
        DWORD r = 0;
        if (SetFilePointerEx(h, at, nullptr, FILE_BEGIN) && ReadFile(h, out.data(), DWORD(n), &r, nullptr)) {
            out.resize(r);
            *offset += r;
        } else
            out.clear();
    }
    CloseHandle(h);
    return out;
}

// The home view model, from the tile list populate loop: the view model that owns the list in r14
// (its TileList at +0x2c8) is on the stack, and the home one has FeHome_Handle_Command in vtable slot
// 10. Routes fire from it as the home tiles fire them: Vm_Fire_Route(vm->router (+0x28), vm, &name, 0).
const runtime::Sig S_POPULATE_LOOP{"site_FeTile_PopulateLoop", "49 8B 76 78 4D 8B B6 80 00 00 00 4C 39 F6 8B AC 24 00 03 00 00",
                                   0x1745016};
const runtime::Sig S_FIRE_CALL{"Vm_Fire_Route", "48 8B 4E 28 48 89 F2 49 89 F8 45 31 C9 E8 ?? ?? ?? ?? E9 E4 00 00 00",
                               0x42c0e20, runtime::Sig::rel32, 13, 14, 5};
constexpr size_t VM_TILE_LIST = 0x2c8, VM_ROUTER = 0x28, STACK_SCAN = 0x1000;

using Fire_t = void(void *router, void *vm, swofty::game::NeStr **name, uint64_t a4);
using MakeStr_t = uint32_t(swofty::game::NeStr **out, const char *s, uint32_t flags, uintptr_t len);
using Controller_t = void(void *system, uint64_t vehicle, uint32_t ai, int32_t reason);

// The agent's bindings resolve through the core when it arms, never as the core's own wanted bindings:
// normal Play never needs them, and a build without one only makes the commands that need it answer
// unavailable.
uintptr_t a_home_cmd, a_make_str, a_controller;

void make_str(swofty::game::NeStr **s, const char *text)
{
    reinterpret_cast<MakeStr_t *>(a_make_str)(s, text, 0, 0);
}

std::atomic<void *> home_vm{nullptr}, home_vt{nullptr};
// fe_career's view model (vt_Game_FECareerViewModel 0x1457385b0, L2), seen at the same populate loop.
std::atomic<void *> career_vm{nullptr};
uintptr_t a_fire;
safetyhook::MidHook h_populate;
std::string route_why = "not set up";

void set_home(void *vm)
{
    if (home_vm.load() != vm)
        swofty::log("[test] home view model %p", vm);
    home_vt = *static_cast<void **>(vm);
    home_vm = vm;
}

void *live_home()
{
    void *v = home_vm.load();
    return v && runtime::readable(v, VM_TILE_LIST + 8) && *static_cast<void **>(v) == home_vt.load() ? v : nullptr;
}

void populate_mid(safetyhook::Context &c)
{
    auto *list = reinterpret_cast<void *>(c.r14);
    auto *sp = reinterpret_cast<const uint8_t *>(c.rsp);
    if (!list || !sp || !runtime::readable(list, 0x90))
        return;
    for (size_t off = 0; off < STACK_SCAN; off += 8) {
        if ((off == 0 || (reinterpret_cast<uintptr_t>(sp + off) & 0xfff) == 0) && !runtime::readable(sp + off, 8))
            return;
        uint8_t *v;
        std::memcpy(&v, sp + off, sizeof v);
        if (!v || !runtime::readable(v, VM_TILE_LIST + 8) || *reinterpret_cast<void **>(v + VM_TILE_LIST) != list)
            continue;
        auto **vt = *reinterpret_cast<void ***>(v);
        if (runtime::readable(vt, 11 * sizeof(void *)) &&
            reinterpret_cast<uintptr_t>(vt[10]) == a_home_cmd)
            set_home(v);
        else if (ga.fe_career_vt && reinterpret_cast<uintptr_t>(vt) == ga.fe_career_vt) {
            if (career_vm.exchange(v) != v)
                swofty::log("[test] fe_career view model %p", v);
        }
        return;
    }
}

void routes_setup()
{
    uintptr_t loop = runtime::find(S_POPULATE_LOOP, "test", "test");
    a_fire = runtime::find(S_FIRE_CALL, "test", "test");
    a_home_cmd = swofty::api->resolve("FeHome_Handle_Command");
    a_make_str = swofty::api->resolve("Ne_Make_StrFromCStr");
    if (!a_fire || !a_make_str || !a_home_cmd) {
        route_why = "Vm_Fire_Route, Ne_Make_StrFromCStr or FeHome_Handle_Command did not resolve on this build";
        return;
    }
    if (!loop) {
        route_why = "the tile list populate loop (site_FeTile_PopulateLoop) did not resolve, so the home view model is "
                    "seen only after a home command";
        return;
    }
    auto h = safetyhook::MidHook::create(reinterpret_cast<void *>(loop), populate_mid);
    if (!h) {
        route_why = "the populate loop hook failed: " + runtime::hook_error(h.error());
        return;
    }
    h_populate = std::move(*h);
    route_why.clear();
}

// The title screen without input. StateIntro's onStateUpdate (FeFlow_StateIntro_OnUpdate 0x1436da4d0,
// slot 9 of vt_Sess_Intro 0x14585bff0, L1, no signature: the KB build only) waits until the title's
// sync point (state+0xcd0, sync_points.boot; Vm_SyncPoint_IsComplete 0x1442626c0) has both bool nodes
// true: the request node +0x80 and the UI's node +0xa0, which the title's UI sets on a button. Then it
// sends its own request (kind 0x2e) whose callback (FeFlow_StateIntro_OnEngaged) sets +0xef4, and the
// next update returns the front-end link. The agent sets the two nodes with Vm_NodeSet_Bool
// (0x140973e20, (node, &value, 0, 0) as the sync point's own writes do) once the title's view exists
// ([[state+0xc88]+0x140]+0x198), so the game's own path runs from there.
constexpr size_t INTRO_SYNC = 0xcd0, SYNC_REQUEST = 0x80, SYNC_UI = 0xa0, INTRO_SENT = 0xef3, INTRO_ENGAGED = 0xef4;

using IntroUpdate_t = uint32_t *(void *state, uint32_t *link);
using NodeSetBool_t = void(void *node, const uint8_t *value, int a3, int a4);
using SyncComplete_t = bool(void *sync);

safetyhook::InlineHook h_intro;
std::atomic<void *> intro_state{nullptr};
std::atomic<bool> title_wanted{false}, title_engaged{false}, title_set{false};
std::atomic<uint32_t> title_view{0};
std::string title_why = "not set up", continue_why = "not set up", hq_why = "not set up";

uint8_t *addr_of(uintptr_t a)
{
    return reinterpret_cast<uint8_t *>(a);
}

// A live bool node: not null, not the empty handle, not read-only (+0x26 bit 0), valid (vt+0x20) and
// holding the bool type (as the sync point's own checks).
bool bool_node(void *node)
{
    void *empty = *reinterpret_cast<void **>(ga.empty_node);
    if (!node || node == empty || !runtime::readable(node, 0x60) || (static_cast<uint8_t *>(node)[0x26] & 1))
        return false;
    auto **vt = *static_cast<void ***>(node);
    if (!runtime::readable(vt, 0x28) || !reinterpret_cast<bool (*)(void *)>(vt[4])(node))
        return false;
    void *holder = static_cast<void **>(node)[0xb];
    if (!holder || !runtime::readable(holder, 8))
        return false;
    auto **hvt = *static_cast<void ***>(holder);
    void *type = reinterpret_cast<void *(*)(void *)>(hvt[1])(holder);
    auto *bool_type = addr_of(ga.bool_type);
    auto **tvt = *reinterpret_cast<void ***>(type);
    auto **bvt = *reinterpret_cast<void ***>(bool_type);
    return reinterpret_cast<int (*)(void *)>(tvt[2])(type) == reinterpret_cast<int (*)(void *)>(bvt[2])(bool_type);
}

bool node_value(void *node)
{
    void *holder = static_cast<void **>(node)[0xb];
    auto **hvt = *static_cast<void ***>(holder);
    return *reinterpret_cast<const char *(*)(void *)>(hvt[7])(holder) != 0;
}

void title_nodes(uint8_t *state)
{
    void *view = nullptr;
    void *a = *reinterpret_cast<void **>(state + 0xc88);
    if (a && runtime::readable(a, 0x148)) {
        void *b = *reinterpret_cast<void **>(static_cast<uint8_t *>(a) + 0x140);
        if (b && runtime::readable(b, 0x1a0))
            view = *reinterpret_cast<void **>(static_cast<uint8_t *>(b) + 0x198);
    }
    title_view = view ? 1 : 2;
    if (!view)
        return;
    uint8_t *sync = state + INTRO_SYNC;
    const uint8_t one = 1;
    for (size_t off : {SYNC_REQUEST, SYNC_UI}) {
        void *node = *reinterpret_cast<void **>(sync + off);
        if (!bool_node(node) || node_value(node))
            continue;
        reinterpret_cast<NodeSetBool_t *>(ga.node_set_bool)(node, &one, 0, 0);
        swofty::log("[test] title: set the sync point's %s node (sync_points.boot) to true",
                    off == SYNC_REQUEST ? "request (+0x80)" : "ui (+0xa0)");
    }
    if (!title_set.exchange(true))
        swofty::log("[test] title: sync point complete -> %d; the title sends its own request next",
                    int(reinterpret_cast<SyncComplete_t *>(ga.sync_complete)(sync)));
}

uint32_t *intro_detour(void *state, uint32_t *link)
{
    auto *st = static_cast<uint8_t *>(state);
    if (intro_state.exchange(state) != state)
        swofty::log("[test] title: StateIntro %p", state);
    if (title_wanted.load() && !st[INTRO_ENGAGED])
        title_nodes(st);
    uint32_t *r = h_intro.call<uint32_t *>(state, link);
    if (st[INTRO_ENGAGED] && !title_engaged.exchange(true))
        swofty::log("[test] title: engaged (StateIntro+0xef4 set by the title's request callback); the front end "
                    "comes next");
    return r;
}

void title_setup()
{
    if (!runtime::same_build()) {
        title_why = "StateIntro's state layout (+0xcd0, +0xef3, +0xef4) is the KB build's and this is not that build";
        return;
    }
    if (!ga.intro_update || !ga.intro_vt || !ga.node_set_bool || !ga.sync_complete || !ga.bool_type || !ga.empty_node) {
        title_why = "FeFlow_StateIntro_OnUpdate, Vm_NodeSet_Bool, Vm_SyncPoint_IsComplete or the bool node type did not "
                    "resolve on this build";
        return;
    }
    auto **vt = reinterpret_cast<void **>(ga.intro_vt);
    if (!runtime::readable(vt, 10 * 8) || vt[9] != addr_of(ga.intro_update)) {
        title_why = "vt_Sess_Intro slot 9 is not FeFlow_StateIntro_OnUpdate";
        return;
    }
    auto h = safetyhook::InlineHook::create(addr_of(ga.intro_update), reinterpret_cast<void *>(intro_detour));
    if (!h) {
        title_why = "the hook on FeFlow_StateIntro_OnUpdate failed";
        return;
    }
    h_intro = std::move(*h);
    title_why.clear();
}

// The vehicle controller system, from the game's own calls of Game_Set_VehicleController_Thunk
// (session start hands cars to their controllers).
safetyhook::InlineHook h_controller;
std::atomic<void *> controller_system{nullptr};
std::string controller_why = "not set up";

void controller_detour(void *system, uint64_t vehicle, uint32_t ai, int32_t reason)
{
    if (controller_system.exchange(system) != system)
        swofty::log("[test] vehicle controller system %p (vehicle %u, ai %u, reason %d)", system, unsigned(vehicle & 0xff),
                    ai, reason);
    h_controller.call<void>(system, vehicle, ai, reason);
}

void controller_setup()
{
    uintptr_t a = a_controller = swofty::api->resolve("Game_Set_VehicleController_Thunk");
    if (!a) {
        controller_why = "Game_Set_VehicleController_Thunk did not resolve on this build";
        return;
    }
    auto h = safetyhook::InlineHook::create(reinterpret_cast<void *>(a), reinterpret_cast<void *>(controller_detour));
    if (!h) {
        controller_why = "the hook on Game_Set_VehicleController_Thunk failed";
        return;
    }
    h_controller = std::move(*h);
    controller_why.clear();
}

// The virtual pad: XInputGetState and XInputGetCapabilities in xinput1_4.dll (the game imports them
// by ordinal 2 and 4) report a connected pad in slot 0 while the agent is armed. A script's buttons
// replace a real pad's state while they are held; otherwise a real pad passes through.
struct XPad {
    uint16_t buttons;
    uint8_t lt, rt;
    int16_t lx, ly, rx, ry;
};
struct XState {
    DWORD packet;
    XPad pad;
};
struct XCaps {
    uint8_t type, subtype;
    uint16_t flags;
    XPad pad;
    uint16_t vib_l, vib_r;
};

std::atomic<uint32_t> pad_word{0};
std::atomic<bool> pad_held{false};
std::atomic<DWORD> pad_packet{1};
safetyhook::InlineHook h_xget, h_xcaps;
std::string pad_why = "not set up";

DWORD WINAPI x_get_state(DWORD user, XState *s)
{
    DWORD r = h_xget.call<DWORD>(user, s);
    if (user != 0 || !s || !armed.load() || stopped.load())
        return r;
    if (pad_held.load() || r != ERROR_SUCCESS) {
        uint32_t w = pad_word.load();
        std::memset(s, 0, sizeof *s);
        s->packet = pad_packet.load();
        s->pad.buttons = uint16_t(w);
        s->pad.lt = uint8_t(w >> 16);
        s->pad.rt = uint8_t(w >> 24);
    }
    return ERROR_SUCCESS;
}

DWORD WINAPI x_get_caps(DWORD user, DWORD flags, XCaps *c)
{
    DWORD r = h_xcaps.call<DWORD>(user, flags, c);
    if (user != 0 || !c || !armed.load() || stopped.load() || r == ERROR_SUCCESS)
        return r;
    std::memset(c, 0, sizeof *c);
    c->type = 1;
    c->subtype = 1;
    c->pad.buttons = 0xf3ff;
    c->pad.lt = c->pad.rt = 0xff;
    return ERROR_SUCCESS;
}

void pad_setup()
{
    HMODULE x = LoadLibraryW(L"xinput1_4.dll");
    void *get = x ? reinterpret_cast<void *>(GetProcAddress(x, MAKEINTRESOURCEA(2))) : nullptr;
    void *caps = x ? reinterpret_cast<void *>(GetProcAddress(x, MAKEINTRESOURCEA(4))) : nullptr;
    if (!get || !caps) {
        pad_why = "xinput1_4.dll or its XInputGetState/XInputGetCapabilities exports were not found";
        return;
    }
    auto g = safetyhook::InlineHook::create(get, reinterpret_cast<void *>(x_get_state));
    auto c = safetyhook::InlineHook::create(caps, reinterpret_cast<void *>(x_get_caps));
    if (!g || !c) {
        pad_why = "hooking xinput1_4.dll's exports failed";
        return;
    }
    h_xget = std::move(*g);
    h_xcaps = std::move(*c);
    pad_why.clear();
}

// Cars: g_Sess_VehicleTable (0x146ce86e0, L2, no signature) holds a CarLive per vehicle id, so it is
// read only on the build the KB describes. The field table is the KB's L3 rows (re/db/structs/Car*.h);
// "obf" fields are CarObfFloat pairs: the float's bits XOR the key 8 bytes on.
constexpr size_t CAR_VEHICLE_ID = 0xe8, CAR_IS_AI = 0x124;
enum class Ty { f32, obf, u32, i32, u8 };
struct Field {
    const char *system, *field;
    size_t sys_off, off;
    Ty ty;
};
constexpr Field FIELDS[] = {
    {"chassis", "mass", 0x558, 0x150, Ty::f32},
    {"fuel_tank", "fuel_mass", 0x540, 0x100, Ty::f32},
    {"fuel_tank", "fuel_flow", 0x540, 0x108, Ty::f32},
    {"fuel_tank", "fuel_capacity", 0x540, 0x98, Ty::obf},
    {"engine", "max_torque", 0x530, 0x2518, Ty::obf},
    {"engine", "limiter_rotation_rate", 0x530, 0x2528, Ty::f32},
    {"engine", "rotation_rate", 0x530, 0x2538, Ty::f32},
    {"engine", "throttle", 0x530, 0x2574, Ty::f32},
    {"gearbox", "gear_index", 0x538, 0x680, Ty::u32},
    {"brake_ecu", "brake_bias", 0x550, 0x21d8, Ty::f32},
    {"ers", "energy", 0x5a8, 0x19f0, Ty::f32},
    {"ers", "ers_mode", 0x5a8, 0x1a68, Ty::i32},
    {"fuel_ecu", "fuel_mode", 0x548, 0x128, Ty::i32},
    {"f1_aero", "total_drag", 0x570, 0x19e8, Ty::f32},
};

const char *ty_name(Ty t)
{
    return t == Ty::f32 ? "f32" : t == Ty::obf ? "obf_f32" : t == Ty::u32 ? "u32" : t == Ty::i32 ? "i32" : "u8";
}

std::string car_unbound()
{
    if (!runtime::same_build())
        return "the car field offsets are the KB build's and this is not that build";
    if (!ga.vehicle_table || !ga.vehicle_count)
        return "g_Sess_VehicleTable or g_Sess_VehicleCount did not resolve on this build";
    return {};
}

uint8_t *car_at(int index, std::string &why)
{
    auto *count = reinterpret_cast<uint32_t *>(ga.vehicle_count);
    auto **table = reinterpret_cast<uint8_t **>(ga.vehicle_table);
    if (!runtime::readable(count, 4) || !runtime::readable(table, 8 * 26)) {
        why = "the vehicle table is not readable";
        return nullptr;
    }
    if (index < 0 || index >= 26 || uint32_t(index) >= *count) {
        why = "no car " + std::to_string(index) + " (the session has " + std::to_string(*count) + ")";
        return nullptr;
    }
    uint8_t *car = table[index];
    if (!car || !runtime::readable(car, 0x5b0)) {
        why = "car " + std::to_string(index) + " is not loaded";
        return nullptr;
    }
    return car;
}

int player_index(std::string &why)
{
    auto *count = reinterpret_cast<uint32_t *>(ga.vehicle_count);
    if (!runtime::readable(count, 4) || !*count) {
        why = "no cars in the session";
        return -1;
    }
    for (int i = 0; i < int(*count) && i < 26; i++) {
        std::string w;
        uint8_t *car = car_at(i, w);
        if (car && car[CAR_IS_AI] == 0)
            return i;
    }
    why = "no car is driven by a human (every CarLive.is_ai is set)";
    return -1;
}

uint8_t *field_at(const std::string &path, const Field **out, std::string &why)
{
    ta::CarPath p;
    if (!ta::parse_car_path(path, p, why))
        return nullptr;
    const Field *f = nullptr;
    for (auto &x : FIELDS)
        if (p.system == x.system && p.field == x.field)
            f = &x;
    if (!f) {
        why = "no car field " + p.system + "." + p.field + " in the agent's table";
        return nullptr;
    }
    why = car_unbound();
    if (!why.empty())
        return nullptr;
    int idx = p.player ? player_index(why) : p.index;
    if (idx < 0)
        return nullptr;
    uint8_t *car = car_at(idx, why);
    if (!car)
        return nullptr;
    uint8_t *sys = *reinterpret_cast<uint8_t **>(car + f->sys_off);
    if (!sys || !runtime::readable(sys + f->off, 16)) {
        why = "car " + std::to_string(idx) + " has no " + p.system;
        return nullptr;
    }
    *out = f;
    return sys + f->off;
}

std::string value_json(const uint8_t *at, Ty t)
{
    double v = 0;
    uint32_t bits;
    switch (t) {
    case Ty::f32: {
        float f;
        std::memcpy(&f, at, 4);
        v = f;
        break;
    }
    case Ty::obf: {
        uint32_t key;
        std::memcpy(&bits, at, 4);
        std::memcpy(&key, at + 8, 4);
        bits ^= key;
        float f;
        std::memcpy(&f, &bits, 4);
        v = f;
        break;
    }
    case Ty::u32: std::memcpy(&bits, at, 4); v = bits; break;
    case Ty::i32: {
        int32_t i;
        std::memcpy(&i, at, 4);
        v = i;
        break;
    }
    case Ty::u8: v = at[0]; break;
    }
    return "{\"value\":" + ta::num(v) + ",\"type\":\"" + ty_name(t) + "\"}";
}

// Test DLLs, loaded from the mailbox once armed.
using Names_t = const char *();
using Start_t = void *(const char *, const SwoftyTestHost *);
using Step_t = int(void *);
using End_t = void(void *);
struct Dll {
    std::string name;
    HMODULE m;
    Names_t *names;
    Start_t *start;
    Step_t *step;
    End_t *end;
};
std::vector<Dll> dlls;

void load_dlls()
{
    for (auto &rel : session.dlls) {
        std::wstring path = mbox + L"\\" + runtime::wide(rel);
        for (auto &c : path)
            if (c == L'/')
                c = L'\\';
        HMODULE m = LoadLibraryW(path.c_str());
        Dll d{rel, m, nullptr, nullptr, nullptr, nullptr};
        if (m) {
            d.names = reinterpret_cast<Names_t *>(GetProcAddress(m, "SwoftyTestNames"));
            d.start = reinterpret_cast<Start_t *>(GetProcAddress(m, "SwoftyTestStart"));
            d.step = reinterpret_cast<Step_t *>(GetProcAddress(m, "SwoftyTestStep"));
            d.end = reinterpret_cast<End_t *>(GetProcAddress(m, "SwoftyTestEnd"));
        }
        bool ok = m && d.names && d.start && d.step && d.end;
        swofty::log("[test] test DLL %s: %s", rel.c_str(),
                    !m ? "did not load" : ok ? "loaded" : "has no SWOFTY_TESTS() exports");
        if (ok)
            dlls.push_back(d);
    }
}

struct InGame : ta::Game {
    ta::Runner sub{*this};
    uint64_t sub_seq = 0;
    std::vector<std::pair<uint64_t, std::string>> sub_done;
    Dll *test_dll = nullptr;
    void *test_run = nullptr;
    std::string expects;
    size_t failed = 0;
    std::string capture_file;
    SwoftyTestHost host{sizeof(SwoftyTestHost), SWOFTY_TEST_HOST_VERSION, this, host_start, host_poll, host_expect,
                        host_log, host_read};

    static uint64_t host_start(void *self, const char *line)
    {
        auto *g = static_cast<InGame *>(self);
        std::string l = line ? line : "";
        size_t brace = l.find('{');
        if (brace == std::string::npos)
            return 0;
        uint64_t seq = ++g->sub_seq;
        l.insert(brace + 1, "\"seq\":" + std::to_string(seq) + ",");
        ta::Command c = ta::parse_command(l);
        if (c.cmd == "run_test" || c.cmd == "quit")
            c.error = c.cmd + " is not available inside a test";
        g->sub.submit(std::move(c));
        return seq;
    }

    static int host_poll(void *self, uint64_t op, char *out, size_t cap)
    {
        auto *g = static_cast<InGame *>(self);
        for (auto &[seq, line] : g->sub_done)
            if (seq == op) {
                if (out && cap)
                    snprintf(out, cap, "%s", line.c_str());
                return line.find("\"status\":\"ok\"") != std::string::npos ? 1 : 2;
            }
        return 0;
    }

    static void host_expect(void *self, int ok, const char *what)
    {
        auto *g = static_cast<InGame *>(self);
        std::string w = what ? what : "";
        g->expects += std::string(g->expects.size() > 1 ? "," : "") + "{\"ok\":" + (ok ? "true" : "false") +
                      ",\"what\":" + ta::esc(w) + "}";
        g->failed += !ok;
        emit("{\"event\":\"expect\",\"ok\":" + std::string(ok ? "true" : "false") + ",\"what\":" + ta::esc(w) + "}");
    }

    static void host_log(void *, const char *text)
    {
        swofty::log("[test] %s", text ? text : "");
        emit("{\"event\":\"log\",\"text\":" + ta::esc(text ? text : "") + "}");
    }

    static int host_read(void *self, const char *path, char *out, size_t cap)
    {
        ta::Outcome o = static_cast<InGame *>(self)->read(path ? path : "");
        bool ok = o.status == ta::Status::ok;
        if (out && cap)
            snprintf(out, cap, "%s", ok ? o.value.c_str() : o.error.c_str());
        return ok;
    }

    ta::Outcome hello() override
    {
        std::string c = "{\"route\":" + ta::esc(route_why.empty() ? "ok" : route_why) +
                        ",\"pad\":" + ta::esc(pad_why.empty() ? "ok" : pad_why) +
                        ",\"ai_drives_player\":" + ta::esc(controller_why.empty() ? "ok" : controller_why) +
                        ",\"car\":" + ta::esc(car_unbound().empty() ? "ok" : car_unbound()) +
                        ",\"title\":" + ta::esc(title_why.empty() ? "ok" : title_why) +
                        ",\"resume\":" + ta::esc(continue_why.empty() ? "ok" : continue_why) +
                        ",\"advance\":" + ta::esc(hq_why.empty() ? "ok" : hq_why) + "}";
        std::string names = "[";
        for (auto &d : dlls)
            for (const char *p = d.names(); p && *p;) {
                const char *nl = std::strchr(p, '\n');
                std::string n = nl ? std::string(p, nl - p) : std::string(p);
                names += (names.size() > 1 ? "," : "") + ta::esc(n);
                p = nl ? nl + 1 : nullptr;
            }
        return ta::Outcome::ok("{\"v\":" + std::to_string(ta::PROTOCOL) + ",\"pid\":" + std::to_string(GetCurrentProcessId()) +
                               ",\"run\":" + ta::esc(session.run) + ",\"ticks\":" + std::to_string(ticks.load()) +
                               ",\"phase\":" + ta::esc(phase()) + ",\"sandbox\":" +
                               (runtime::save_sandbox_test_mode() ? (sandboxed() ? "\"armed\"" : "\"required\"") : "\"off\"") +
                               ",\"bindings\":" + c + ",\"tests\":" + names + "]}");
    }

    ta::Outcome route(const std::string &name) override
    {
        if (!route_why.empty())
            return ta::Outcome::unavailable(route_why);
        void *vm = live_home();
        if (!vm)
            return ta::Outcome::unavailable("no home view model: the home screen has not been built since the agent armed");
        void *router = *reinterpret_cast<void **>(static_cast<uint8_t *>(vm) + VM_ROUTER);
        if (!router)
            return ta::Outcome::unavailable("the home view model has no router");
        swofty::game::NeStr *s = nullptr;
        make_str(&s, name.c_str());
        reinterpret_cast<Fire_t *>(a_fire)(router, vm, &s, 0);
        make_str(&s, nullptr);
        swofty::log("[test] route %s fired from the home view model", name.c_str());
        return ta::Outcome::ok();
    }

    ta::Outcome skip(int destination, int session_type) override
    {
        if (!ga.advance_vt)
            return ta::Outcome::unavailable("MessageSetAdvanceDestination's vtable did not resolve on this build");
        const SwoftyCareer *career = runtime::career_table();
        if (!career || !career->post)
            return ta::Outcome::unavailable("the career API's message post is not available");
        struct alignas(8) Message {
            void *vt;
            uint64_t zero;
            uint32_t destination, session;
        } m{reinterpret_cast<void *>(ga.advance_vt), 0, uint32_t(destination),
            uint32_t(session_type)};
        int r = career->post(&m);
        swofty::log("[test] skip: MessageSetAdvanceDestination destination %d session %d posted -> %d", destination,
                    session_type, r);
        if (r == SWOFTY_ASSET_LATE)
            return ta::Outcome::unavailable("the message bus is not active yet");
        if (r != SWOFTY_ASSET_OK)
            return ta::Outcome::fail("the message bus refused the message (" + std::to_string(r) + ")");
        return ta::Outcome::ok();
    }

    ta::Outcome advance(int days) override;
    ta::Outcome advance_poll() override;

    ta::Outcome ai_drives_player(bool on) override
    {
        if (!controller_why.empty())
            return ta::Outcome::unavailable(controller_why);
        void *sys = controller_system.load();
        if (!sys)
            return ta::Outcome::unavailable("the game has not handed a car to a controller since the agent armed, so the "
                                            "vehicle controller system is not known yet (start a session first)");
        std::string why = car_unbound();
        int idx = why.empty() ? player_index(why) : -1;
        if (idx < 0)
            return ta::Outcome::unavailable("the player's car: " + why);
        uint8_t *car = car_at(idx, why);
        if (!car)
            return ta::Outcome::unavailable("the player's car: " + why);
        uint8_t vid = car[CAR_VEHICLE_ID];
        reinterpret_cast<Controller_t *>(a_controller)(sys, vid, on ? 1 : 0, 6);
        swofty::log("[test] ai_drives_player %s: vehicle %u, controller system %p, reason 6 (dev)", on ? "on" : "off",
                    unsigned(vid), sys);
        return ta::Outcome::ok("{\"vehicle\":" + std::to_string(vid) + "}");
    }

    ta::Outcome read(const std::string &path) override
    {
        if (path == "agent.ticks")
            return ta::Outcome::ok("{\"value\":" + std::to_string(ticks.load()) + ",\"type\":\"u64\"}");
        if (path == "agent.run")
            return ta::Outcome::ok("{\"value\":" + ta::esc(session.run) + ",\"type\":\"str\"}");
        if (path == "ui.screen" || path == "flow.state") {
            std::lock_guard<std::mutex> g(phase_lock);
            return ta::Outcome::ok("{\"value\":" + ta::esc(path == "ui.screen" ? screen_name : flow_name) +
                                   ",\"type\":\"str\"}");
        }
        if (path.rfind("car[", 0) == 0) {
            const Field *f = nullptr;
            std::string why;
            uint8_t *at = field_at(path, &f, why);
            if (!at)
                return ta::Outcome::unavailable(why);
            return ta::Outcome::ok(value_json(at, f->ty));
        }
        return ta::Outcome::unavailable("no reader for " + path +
                                        " (this agent reads agent.ticks, agent.run, ui.screen, flow.state and "
                                        "car[<index>|player].<system>.<field>)");
    }

    ta::Outcome set(const std::string &path, const json::Value &value) override
    {
        if (path.rfind("car[", 0) != 0)
            return ta::Outcome::unavailable("only car fields can be set (car[<index>|player].<system>.<field>)");
        if (value.type != json::Value::Num)
            return ta::Outcome::fail("a car field takes a number");
        const Field *f = nullptr;
        std::string why;
        uint8_t *at = field_at(path, &f, why);
        if (!at)
            return ta::Outcome::unavailable(why);
        std::string before = value_json(at, f->ty);
        float fl = float(value.n);
        uint32_t bits;
        switch (f->ty) {
        case Ty::f32: std::memcpy(at, &fl, 4); break;
        case Ty::obf: {
            uint32_t key;
            std::memcpy(&key, at + 8, 4);
            std::memcpy(&bits, &fl, 4);
            bits ^= key;
            std::memcpy(at, &bits, 4);
            break;
        }
        case Ty::u32:
        case Ty::i32: {
            int32_t i = int32_t(value.n);
            std::memcpy(at, &i, 4);
            break;
        }
        case Ty::u8: at[0] = uint8_t(value.n); break;
        }
        std::string after = value_json(at, f->ty);
        swofty::log("[test] set %s: %s -> %s", path.c_str(), before.c_str(), after.c_str());
        return ta::Outcome::ok("{\"before\":" + before + ",\"after\":" + after + "}");
    }

    ta::Outcome capture(const std::string &name) override
    {
        std::wstring dir = mbox + L"\\frames";
        CreateDirectoryW(dir.c_str(), nullptr);
        capture_file = "frames/" + name + ".png";
        if (!runtime::frame_capture_request(dir + L"\\" + runtime::wide(name) + L".png"))
            return ta::Outcome::unavailable("a frame capture is already waiting for a frame");
        return ta::Outcome::ok();
    }

    ta::Outcome capture_poll() override
    {
        std::string why;
        int r = runtime::frame_capture_poll(&why);
        if (r == 0)
            return ta::Outcome::pending();
        if (r < 0)
            return ta::Outcome::unavailable(why);
        return ta::Outcome::ok("{\"file\":" + ta::esc(capture_file) + "}");
    }

    ta::Outcome save_now() override;
    ta::Outcome save_poll() override;

    static BOOL CALLBACK close_main(HWND h, LPARAM count)
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid == GetCurrentProcessId() && IsWindowVisible(h) && !GetWindow(h, GW_OWNER) && PostMessageW(h, WM_CLOSE, 0, 0))
            ++*reinterpret_cast<int *>(count);
        return TRUE;
    }

    ta::Outcome quit() override
    {
        int closed = 0;
        EnumWindows(close_main, reinterpret_cast<LPARAM>(&closed));
        swofty::log("[test] quit: WM_CLOSE posted to %d main window(s)", closed);
        if (!closed)
            return ta::Outcome::unavailable("no visible main window to close; the CLI closes the game by its pid");
        return ta::Outcome::ok("{\"windows\":" + std::to_string(closed) + "}");
    }

    ta::Outcome pad(const ta::PadState &p) override
    {
        if (!pad_why.empty())
            return ta::Outcome::unavailable(pad_why);
        pad_word = uint32_t(p.buttons) | uint32_t(p.lt) << 16 | uint32_t(p.rt) << 24;
        pad_held = p.buttons || p.lt || p.rt;
        pad_packet.fetch_add(1);
        return ta::Outcome::ok();
    }

    std::string phase() override
    {
        std::lock_guard<std::mutex> g(phase_lock);
        return "|" + screen_name + "|" + flow_name + "|";
    }

    ta::Outcome test_start(const std::string &fn) override
    {
        for (auto &d : dlls)
            for (const char *p = d.names(); p && *p;) {
                const char *nl = std::strchr(p, '\n');
                std::string n = nl ? std::string(p, nl - p) : std::string(p);
                p = nl ? nl + 1 : nullptr;
                if (n != fn)
                    continue;
                expects.clear();
                failed = 0;
                sub_done.clear();
                test_run = d.start(fn.c_str(), &host);
                if (!test_run)
                    return ta::Outcome::fail("the test DLL did not start " + fn);
                test_dll = &d;
                swofty::log("[test] run_test %s (%s)", fn.c_str(), d.name.c_str());
                return ta::Outcome::ok();
            }
        return ta::Outcome::unavailable(dlls.empty() ? "no test DLL is loaded" : "no test DLL registers " + fn);
    }

    ta::Outcome test_poll() override
    {
        if (!test_run)
            return ta::Outcome::fail("no test is running");
        std::vector<std::string> out;
        sub.tick(GetTickCount64(), out);
        for (auto &l : out) {
            json::Value v;
            if (json::parse(l, v))
                sub_done.emplace_back(uint64_t(v["seq"].num()), l);
        }
        int r = test_dll->step(test_run);
        if (r == 0)
            return ta::Outcome::pending();
        test_dll->end(test_run);
        test_run = nullptr;
        std::string value = "{\"expects\":[" + expects + "]}";
        swofty::log("[test] run_test finished: %s (%zu failed expectation(s))",
                    r == 1 ? "passed" : r == 2 ? "failed" : "the test threw", failed);
        if (r == 1)
            return ta::Outcome::ok(value);
        ta::Outcome o = ta::Outcome::fail(r == 3 ? "the test threw an exception"
                                                 : std::to_string(failed) + " expectation(s) failed");
        o.value = value;
        return o;
    }

    ta::Outcome title_press() override
    {
        if (!title_why.empty())
            return ta::Outcome::unavailable(title_why);
        title_wanted = true;
        return ta::Outcome::ok();
    }

    std::string title_state() override
    {
        if (title_engaged.load())
            return "engaged";
        auto *st = static_cast<uint8_t *>(intro_state.load());
        if (!st)
            return "waiting: StateIntro has not updated since the agent armed";
        if (title_view.load() != 1)
            return "waiting: the title's view ([[StateIntro+0xc88]+0x140]+0x198) is not up";
        if (st[INTRO_SENT])
            return "waiting: the title's request (kind 0x2e) has not called back (StateIntro+0xef3 set, +0xef4 not)";
        return title_set.load() ? "waiting: the sync point is set and StateIntro has not sent its request"
                                : "waiting: the sync point's nodes are not set yet";
    }

    ta::Outcome career_tile(const std::string &mode) override;
    ta::Outcome resume_press(const std::string &slot) override;
    ta::Outcome session_start() override;

    bool sandboxed() override { return runtime::save_sandbox_armed(); }

    void note(const std::string &line) override { swofty::log("%s", line.c_str()); }

    uint64_t log_lines(uint64_t from, std::vector<std::string> &out) override
    {
        std::lock_guard<std::mutex> g(log_lock);
        uint64_t first = log_total - log_ring.size();
        if (from == UINT64_MAX)
            return log_total;
        for (uint64_t i = std::max(from, first); i < log_total; i++)
            out.push_back(log_ring[size_t(i - first)]);
        return log_total;
    }
};

// Career tiles on fe_career: TileList vm+0x2c8, entries {node, item} 0x10 bytes from +0x78 to +0x80;
// the career item's vtable is 0x145703eb0 with get_action (FeTile_Route_CareerItem 0x141725620, L2) at
// slot 5; its kind is the NeStr at item+0x178 (djb2 0x73b00842 driver_career, 0x353c4a02 my_team_career).
// get_action(item, &action) writes {u32 type, NeStr *route}: type 1 and solo_career_continue_* while the
// mode has a visible slot, else a route into the new-career wizard. The tile screen fires that route
// as Vm_Fire_Route(vm->router, vm, &route, 0) on select_pressed, and so does the agent. get_action is
// not read-only: with no save to continue it also resets the wizard state and the current game mode
// (the same reset a press on that tile makes), so the agent calls it only on the tile it is asked for.
constexpr uint32_t KIND_DRIVER = 0x73b00842, KIND_MY_TEAM = 0x353c4a02;

std::string nestr_text(const swofty::game::NeStr *s)
{
    auto *b = reinterpret_cast<const uint8_t *>(s);
    if (!b || !runtime::readable(b, 0x10))
        return {};
    uint32_t lf;
    std::memcpy(&lf, b + 6, 4);
    size_t n = std::min<size_t>(lf & 0xfffff, 96);
    if (!runtime::readable(b + 9, n))
        return {};
    return std::string(reinterpret_cast<const char *>(b + 9), n);
}

ta::Outcome InGame::career_tile(const std::string &mode)
{
    if (!runtime::same_build())
        return ta::Outcome::unavailable("the career tile's item layout is the KB build's and this is not that build");
    if (!route_why.empty())
        return ta::Outcome::unavailable(route_why);
    if (!ga.fe_career_vt || !ga.career_item_vt)
        return ta::Outcome::unavailable("the fe_career or career tile vtable did not resolve on this build");
    auto *vm = static_cast<uint8_t *>(career_vm.load());
    if (!vm || !runtime::readable(vm, VM_TILE_LIST + 8) || *reinterpret_cast<uintptr_t *>(vm) != ga.fe_career_vt)
        return ta::Outcome::unavailable("no fe_career view model: the career screen has not built its tiles yet");
    auto *list = *reinterpret_cast<uint8_t **>(vm + VM_TILE_LIST);
    if (!list || !runtime::readable(list, 0x90))
        return ta::Outcome::unavailable("fe_career has no tile list yet");
    auto *it = *reinterpret_cast<uint8_t **>(list + 0x78), *end = *reinterpret_cast<uint8_t **>(list + 0x80);
    if (!it || end < it || size_t(end - it) > 0x10 * 64 || !runtime::readable(it, size_t(end - it)))
        return ta::Outcome::unavailable("fe_career's tile list is not readable");
    std::vector<std::pair<uint32_t, uint8_t *>> careers;
    for (; it < end; it += 0x10) {
        auto *item = *reinterpret_cast<uint8_t **>(it + 8);
        if (!item || !runtime::readable(item, 0x180) || *reinterpret_cast<uintptr_t *>(item) != ga.career_item_vt)
            continue;
        auto *kind = *reinterpret_cast<uint8_t **>(item + 0x178);
        if (!kind || !runtime::readable(kind, 4))
            continue;
        careers.emplace_back(*reinterpret_cast<uint32_t *>(kind), item);
    }
    if (careers.empty())
        return ta::Outcome::unavailable("fe_career shows no Driver Career or My Team tile yet");
    std::vector<uint32_t> order;
    if (mode == "driver_career")
        order = {KIND_DRIVER};
    else if (mode == "my_team_career")
        order = {KIND_MY_TEAM};
    else
        order = {KIND_MY_TEAM, KIND_DRIVER};
    std::string tried;
    for (uint32_t want : order)
        for (auto &[kind, item] : careers) {
            if (kind != want)
                continue;
            struct alignas(8) Action {
                uint32_t type, pad;
                swofty::game::NeStr *route;
                uint64_t rest[2];
            } a{};
            auto **vt = *reinterpret_cast<void ***>(item);
            reinterpret_cast<void *(*)(void *, Action *)>(vt[5])(item, &a);
            std::string route = nestr_text(a.route);
            const char *name = want == KIND_DRIVER ? "driver_career" : "my_team_career";
            if (a.type != 1 || route.rfind("solo_career_continue_", 0) != 0) {
                tried += std::string(tried.empty() ? "" : "; ") + "the " + name + " tile's action is type " +
                         std::to_string(a.type) + " route '" + route + "' (no saved career to continue)";
                if (a.route)
                    make_str(&a.route, nullptr);
                continue;
            }
            void *router = *reinterpret_cast<void **>(vm + VM_ROUTER);
            reinterpret_cast<Fire_t *>(a_fire)(router, vm, &a.route, 0);
            make_str(&a.route, nullptr);
            swofty::log("[test] career: the %s tile's action fired route %s from fe_career", name, route.c_str());
            return ta::Outcome::ok("{\"mode\":" + ta::esc(name) + ",\"route\":" + ta::esc(route) + "}");
        }
    if (tried.empty())
        return ta::Outcome::fail("fe_career has no " + (mode.empty() ? std::string("career") : mode) + " tile");
    return ta::Outcome::fail(tried);
}

// The resume screens (CareerContinueDriverViewModel vt 0x1457264a0, CareerContinueMyTeamViewModel vt
// 0x145726670): the agent sees them in Game_CareerContinue_HandleCommand (0x141a1db90, slot 10, L2 with
// agrees in V1485 and V1496), which the UI raises transition_in_finished into when the screen opens. Cards are
// the 0x540-byte entries at vm+0x328 (three), has_save at +0, the slot id at +8. A press is
// HandleCommand(vm, &"slot_focused", &{type, data = card}) then the same with "slot_pressed"; the
// handler reads only the data byte at +8. Presses are ignored while vm+0x1380 is set (PressSlot) and,
// for slot_pressed, while the bool node vm+0x12f0 is true (the handler).
constexpr size_t CARDS = 0x328, CARD_SIZE = 0x540, CARD_COUNT = 3, CONTINUE_BUSY = 0x1380;

using ContinueHandle_t = uint64_t(void *vm, swofty::game::NeStr **cmd, void *value);
safetyhook::InlineHook h_continue;
std::atomic<void *> continue_vm{nullptr};

uint64_t continue_detour(void *vm, swofty::game::NeStr **cmd, void *value)
{
    uintptr_t vt = vm && runtime::readable(vm, 8) ? *static_cast<uintptr_t *>(vm) : 0;
    if ((vt == ga.continue_driver_vt || vt == ga.continue_my_team_vt) && continue_vm.exchange(vm) != vm)
        swofty::log("[test] career: resume screen view model %p (%s)", vm, vt == ga.continue_driver_vt ? "driver" : "my team");
    return h_continue.call<uint64_t>(vm, cmd, value);
}


void continue_setup()
{
    if (!runtime::same_build()) {
        continue_why = "the resume screens' card layout is the KB build's and this is not that build";
        return;
    }
    if (!ga.continue_handle || !ga.continue_driver_vt || !ga.continue_my_team_vt || !ga.node_set_bool) {
        continue_why = "Game_CareerContinue_HandleCommand or the resume screens' vtables did not resolve on this build";
        return;
    }
    auto **vt = reinterpret_cast<void **>(ga.continue_driver_vt);
    if (!runtime::readable(vt, 11 * 8) || vt[10] != addr_of(ga.continue_handle)) {
        continue_why = "CareerContinueDriverViewModel's slot 10 is not Game_CareerContinue_HandleCommand";
        return;
    }
    auto h = safetyhook::InlineHook::create(addr_of(ga.continue_handle), reinterpret_cast<void *>(continue_detour));
    if (!h) {
        continue_why = "the hook on Game_CareerContinue_HandleCommand failed";
        return;
    }
    h_continue = std::move(*h);
    continue_why.clear();
}

ta::Outcome InGame::resume_press(const std::string &slot)
{
    if (!continue_why.empty())
        return ta::Outcome::unavailable(continue_why);
    auto *vm = static_cast<uint8_t *>(continue_vm.load());
    uintptr_t vt = vm && runtime::readable(vm, CONTINUE_BUSY + 8) ? *reinterpret_cast<uintptr_t *>(vm) : 0;
    if (vt != ga.continue_driver_vt && vt != ga.continue_my_team_vt)
        return ta::Outcome::unavailable("no resume screen view model yet (it is seen when the screen opens)");
    if (vm[CONTINUE_BUSY])
        return ta::Outcome::unavailable("the resume screen ignores presses for now (vm+0x1380 is set)");
    int card = -1;
    std::string slots;
    for (size_t i = 0; i < CARD_COUNT; i++) {
        uint8_t *e = vm + CARDS + i * CARD_SIZE;
        if (!e[0])
            continue;
        uint32_t id;
        std::memcpy(&id, e + 8, 4);
        slots += (slots.empty() ? "" : ", ") + std::to_string(id);
        if (card < 0 && (slot == "newest" || std::to_string(id) == slot))
            card = int(i);
    }
    if (slots.empty())
        return ta::Outcome::unavailable("the resume screen's cards hold no save yet");
    if (card < 0)
        return ta::Outcome::fail("no resume card holds slot " + slot + " (the cards hold " + slots + ")");
    uint32_t id;
    std::memcpy(&id, vm + CARDS + size_t(card) * CARD_SIZE + 8, 4);
    struct alignas(8) Value {
        uint32_t type, pad;
        uint64_t data, zero;
    } v{4, 0, uint64_t(card), 0};
    for (const char *c : {"slot_focused", "slot_pressed"}) {
        swofty::game::NeStr *s = nullptr;
        make_str(&s, c);
        reinterpret_cast<ContinueHandle_t *>(ga.continue_handle)(vm, &s, &v);
        make_str(&s, nullptr);
    }
    swofty::log("[test] career: slot_focused and slot_pressed raised on card %d (slot %u)", card, id);
    return ta::Outcome::ok("{\"index\":" + std::to_string(card) + ",\"slot\":" + std::to_string(id) + "}");
}

// Team HQ's advance (TeamHQAdvanceViewModel, vt 0x14577a9c0, L2): Game_TeamHQAdvanceViewModel_Update
// (0x1422ca380, slot 12, L2) runs every frame with rcx = the view model, so a hook on it captures it. The agent raises timeline_advance_pressed through its onCommandRaised (0x1422c3540,
// slot 10, L2) as the Advance button does: PressAdvance checks vm+0x620, canAdvance (+0x758) and
// advancing (+0x759) and shows today's cards (0x1422c7080) instead when there are any, else runs
// Career_TeamHqAdvance_Start and the screen's start_advancing. The game advances a day per tick until
// the next event; for N days the agent raises timeline_stop_pressed once the day (the game's own
// (u32 [[vm+0x6e0]+0x2b0] >> 5) / 45, the clock in minutes over 1440) reaches the start + N.
constexpr size_t HQ_READY = 0x620, HQ_CAN_ADVANCE = 0x758, HQ_ADVANCING = 0x759, HQ_CLOCK = 0x6e0, HQ_CLOCK_MIN = 0x2b0;

using HqCommand_t = uint64_t(void *vm, swofty::game::NeStr **cmd, void *data);
using HqUpdate_t = uint64_t(void *vm);
safetyhook::InlineHook h_hq;
std::atomic<void *> hq_vm{nullptr};
struct {
    bool on = false, stop_sent = false;
    int from = 0, last = 0, target = 0;
} adv;

uint64_t hq_detour(void *vm)
{
    if (vm && hq_vm.exchange(vm) != vm)
        swofty::log("[test] Team HQ advance view model %p", vm);
    return h_hq.call<uint64_t>(vm);
}

void hq_setup()
{
    if (!runtime::same_build()) {
        hq_why = "Team HQ's advance view model layout is the KB build's and this is not that build";
        return;
    }
    if (!ga.hq_update || !ga.hq_command || !ga.hq_vt) {
        hq_why = "Game_TeamHQAdvanceViewModel_Update, its onCommandRaised or its vtable did not resolve on this build";
        return;
    }
    auto **vt = reinterpret_cast<void **>(ga.hq_vt);
    if (!runtime::readable(vt, 13 * 8) || vt[12] != addr_of(ga.hq_update) || vt[10] != addr_of(ga.hq_command)) {
        hq_why = "vt_Game_TeamHQAdvanceViewModel's slots 10 and 12 are not onCommandRaised and update";
        return;
    }
    auto h = safetyhook::InlineHook::create(addr_of(ga.hq_update), reinterpret_cast<void *>(hq_detour));
    if (!h) {
        hq_why = "the hook on Game_TeamHQAdvanceViewModel_Update failed";
        return;
    }
    h_hq = std::move(*h);
    hq_why.clear();
}

uint8_t *live_hq()
{
    auto *vm = static_cast<uint8_t *>(hq_vm.load());
    return vm && runtime::readable(vm, 0x790) && *reinterpret_cast<uintptr_t *>(vm) == ga.hq_vt
               ? vm
               : nullptr;
}

int hq_day(uint8_t *vm)
{
    auto *clock = *reinterpret_cast<uint8_t **>(vm + HQ_CLOCK);
    if (!clock || !runtime::readable(clock + HQ_CLOCK_MIN, 4))
        return -1;
    return int((*reinterpret_cast<uint32_t *>(clock + HQ_CLOCK_MIN) >> 5) / 45);
}

void hq_command(uint8_t *vm, const char *name)
{
    struct alignas(8) {
        uint32_t type, pad;
        uint64_t a, b;
    } data{4, 0, 0, 0};
    swofty::game::NeStr *s = nullptr;
    make_str(&s, name);
    reinterpret_cast<HqCommand_t *>(ga.hq_command)(vm, &s, &data);
    make_str(&s, nullptr);
}

ta::Outcome InGame::advance(int days)
{
    if (!hq_why.empty())
        return ta::Outcome::unavailable(hq_why);
    uint8_t *vm = live_hq();
    if (!vm)
        return ta::Outcome::unavailable("no Team HQ advance view model: Team HQ's advance panel has not updated since "
                                        "the agent armed");
    if (!*reinterpret_cast<uint64_t *>(vm + HQ_READY))
        return ta::Outcome::unavailable("Team HQ's advance panel is not ready (vm+0x620 is 0)");
    if (vm[HQ_ADVANCING])
        return ta::Outcome::unavailable("Team HQ is already advancing");
    if (!vm[HQ_CAN_ADVANCE])
        return ta::Outcome::unavailable("Team HQ can't advance now (canAdvance, vm+0x758, is off)");
    adv = {};
    adv.from = adv.last = hq_day(vm);
    adv.target = days > 0 ? adv.from + days : 0;
    hq_command(vm, "timeline_advance_pressed");
    if (!vm[HQ_ADVANCING])
        return ta::Outcome::unavailable("the Advance press showed today's cards instead of advancing (press again)");
    adv.on = true;
    swofty::log("[test] advance: timeline_advance_pressed raised on Team HQ at day %d (%s)", adv.from,
                days > 0 ? ("for " + std::to_string(days) + " day(s) at most").c_str() : "to the next event");
    return ta::Outcome::ok("{\"from_day\":" + std::to_string(adv.from) + "}");
}

ta::Outcome InGame::advance_poll()
{
    uint8_t *vm = live_hq();
    std::string done = "{\"from_day\":" + std::to_string(adv.from) + ",\"to_day\":";
    if (!vm) {
        adv.on = false;
        swofty::log("[test] advance: Team HQ's advance view model went away at day %d (the flow left Team HQ)", adv.last);
        return ta::Outcome::ok(done + std::to_string(adv.last) + ",\"left_team_hq\":true}");
    }
    adv.last = hq_day(vm);
    if (adv.target && adv.last >= adv.target && vm[HQ_ADVANCING] && !adv.stop_sent) {
        adv.stop_sent = true;
        hq_command(vm, "timeline_stop_pressed");
        swofty::log("[test] advance: day %d reached, timeline_stop_pressed raised", adv.last);
    }
    if (vm[HQ_ADVANCING])
        return ta::Outcome::pending();
    adv.on = false;
    swofty::log("[test] advance: stopped at day %d (from %d)", adv.last, adv.from);
    return ta::Outcome::ok(done + std::to_string(adv.last) + "}");
}

// Save now: Game_SaveMgr_RequestProfileSave_Thunk (0x1436595b0, L2) with the save manager system, as
// Team HQ's own autosave after an advance does: (mgr, &{fn 0, ctx 0}, 1, 0). The manager is the system
// keyed k_Game_SaveMgrSystemKey (0x146100ca8) in g_Game_Manager's system map ([0x14604c2c8]+0x30):
// a flat hash map (find 0x14030ffd0): entries {key = that address, system} are 0x10 bytes from [map+0x18]
// with one control byte each from [map+0x20] (zero when free); the agent scans them instead of hashing.
// The save is done when the sandbox has served a FileWrite.
uint32_t save_writes_before;

ta::Outcome InGame::save_now()
{
    if (!runtime::same_build())
        return ta::Outcome::unavailable("the game manager's layout is the KB build's and this is not that build");
    if (!ga.save_request || !ga.game_manager || !ga.save_key)
        return ta::Outcome::unavailable("Game_SaveMgr_RequestProfileSave_Thunk or the game manager did not resolve on "
                                        "this build");
    auto *mgr = *reinterpret_cast<uint8_t **>(ga.game_manager);
    if (!mgr || !runtime::readable(mgr, 0x38))
        return ta::Outcome::unavailable("g_Game_Manager is not set yet");
    auto *map = *reinterpret_cast<uint8_t **>(mgr + 0x30);
    if (!map || !runtime::readable(map, 0x28))
        return ta::Outcome::unavailable("the game manager has no system map yet");
    auto *first = *reinterpret_cast<uint8_t **>(map + 0x18), *ctrl = *reinterpret_cast<uint8_t **>(map + 0x20);
    uint64_t count = first && ctrl >= first ? uint64_t(ctrl - first) >> 4 : 0;
    if (!count || count > 0x10000 || !runtime::readable(first, count * 0x10 + count))
        return ta::Outcome::unavailable("the game manager's system map is not readable");
    void *save_mgr = nullptr;
    for (uint64_t i = 0; i < count; i++)
        if (ctrl[i] && *reinterpret_cast<uintptr_t *>(first + i * 0x10) == ga.save_key) {
            save_mgr = *reinterpret_cast<void **>(first + i * 0x10 + 8);
            break;
        }
    if (!save_mgr)
        return ta::Outcome::unavailable("the save manager system is not registered yet");
    struct {
        void *fn, *ctx;
    } cb{nullptr, nullptr};
    save_writes_before = runtime::save_sandbox_writes();
    bool r = reinterpret_cast<bool (*)(void *, void *, uint8_t, uint8_t)>(ga.save_request)(save_mgr, &cb, 1, 0);
    swofty::log("[test] save now: the profile save requested from the save manager %p -> %d", save_mgr, int(r));
    return ta::Outcome::ok();
}

ta::Outcome InGame::save_poll()
{
    uint32_t n = runtime::save_sandbox_writes();
    if (n <= save_writes_before)
        return ta::Outcome::pending();
    swofty::log("[test] save now: the sandbox served %u FileWrite(s)", n - save_writes_before);
    return ta::Outcome::ok("{\"writes\":" + std::to_string(n - save_writes_before) + "}");
}

ta::Outcome InGame::session_start()
{
    return ta::Outcome::unavailable(
        "the garage's goto_track command (GarageCockpitViewModel 0x145739830, djb2 0xac456bd2, L1) is found but not "
        "what releases the car after it, nor its confirm popups (time warning, parc ferme, tyre choice); it needs one "
        "trace of goto_track in a practice session");
}

InGame *game;
ta::Runner *runner;

void refuse(const std::string &why)
{
    swofty::log("[test] agent refused: %s", why.c_str());
    DWORD a = GetFileAttributesW(mbox.c_str());
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY))
        append(mbox + L"\\results.jsonl", "{\"event\":\"refused\",\"why\":" + ta::esc(why) + "}\n");
}

void watch()
{
    resolve_game();
    routes_setup();
    pad_setup();
    controller_setup();
    title_setup();
    continue_setup();
    hq_setup();
    load_dlls();
    game = new InGame;
    runner = new ta::Runner(*game);
    armed = true;
    swofty::log("[test] agent armed for run %s: %zu test DLL(s); route %s, pad %s, ai_drives_player %s, title %s; save "
                "sandbox %s",
                session.run.c_str(), dlls.size(), route_why.empty() ? "ok" : route_why.c_str(),
                pad_why.empty() ? "ok" : pad_why.c_str(), controller_why.empty() ? "ok" : controller_why.c_str(),
                title_why.empty() ? "ok" : title_why.c_str(),
                runtime::save_sandbox_test_mode() ? "required" : "off (the player's own saves)");
    ta::Outcome h = game->hello();
    std::string hello = h.value;
    hello.insert(1, "\"event\":\"hello\",");
    std::wstring results = mbox + L"\\results.jsonl", commands = mbox + L"\\commands.jsonl";
    std::wstring log_path = runtime::under_swofty("loader.log");
    append(results, hello + "\n");
    uint64_t cmd_off = 0, log_off = 0;
    std::string cmd_part, log_part;
    {
        std::string skip = tail(log_path, &log_off);
        (void)skip;
    }
    uint64_t checked = GetTickCount64();
    for (;;) {
        if (GetTickCount64() - checked >= 500) {
            checked = GetTickCount64();
            if (!runtime::test_run_live()) {
                stopped = true;
                pad_word = 0;
                pad_held = false;
                title_wanted = false;
                swofty::log("[test] the run's token is gone from run.json: the agent stops and lets go of the pad");
                return;
            }
        }
        cmd_part += tail(commands, &cmd_off);
        for (size_t nl; (nl = cmd_part.find('\n')) != std::string::npos;) {
            std::string line = cmd_part.substr(0, nl);
            cmd_part.erase(0, nl + 1);
            if (line.find_first_not_of(" \r\t") == std::string::npos)
                continue;
            ta::Command c = ta::parse_command(line);
            if (!c.seq) {
                append(results, "{\"event\":\"bad_command\",\"why\":" + ta::esc(c.error) + "}\n");
                continue;
            }
            std::lock_guard<std::mutex> g(q_lock);
            incoming.push_back(std::move(c));
        }
        log_part += tail(log_path, &log_off);
        {
            std::lock_guard<std::mutex> g(log_lock);
            for (size_t nl; (nl = log_part.find('\n')) != std::string::npos;) {
                log_ring.push_back(log_part.substr(0, nl));
                log_part.erase(0, nl + 1);
                ++log_total;
                if (log_ring.size() > LOG_RING)
                    log_ring.pop_front();
            }
        }
        std::vector<std::string> out;
        {
            std::lock_guard<std::mutex> g(q_lock);
            out.swap(outgoing);
        }
        std::string body;
        for (auto &l : out)
            body += l + "\n";
        if (!body.empty() && !append(results, body))
            swofty::log("[test] could not append to results.jsonl");
        Sleep(POLL_MS);
    }
}

}

namespace runtime {

void testagent_setup()
{
    const runtime::TestRun *run = runtime::test_run();
    if (!run || started.exchange(true))
        return;
    session.run = run->id;
    session.token = run->token;
    session.dlls = run->dlls;
    mbox = run->dir + L"\\agent";
    if (run->agent_v != testagent::PROTOCOL) {
        refuse("run.json asks for test agent protocol " + std::to_string(run->agent_v) + ", this agent speaks " +
               std::to_string(testagent::PROTOCOL));
        return;
    }
    CreateDirectoryW(mbox.c_str(), nullptr);
    swofty::log("[test] test run %s: arming the test agent (mailbox %s)", run->id.c_str(), runtime::narrow(mbox).c_str());
    std::thread(watch).detach();
}

void testagent_tick()
{
    if (!armed.load() || stopped.load())
        return;
    ++ticks;
    {
        std::lock_guard<std::mutex> g(fire_lock);
        for (auto &id : fired_ids)
            runner->fires().fired(id);
        fired_ids.clear();
    }
    {
        std::lock_guard<std::mutex> g(q_lock);
        while (!incoming.empty()) {
            runner->submit(std::move(incoming.front()));
            incoming.pop_front();
        }
    }
    std::vector<std::string> out;
    runner->tick(GetTickCount64(), out);
    for (auto &l : out)
        emit(l);
}

void testagent_fired(const std::string &id, bool hot)
{
    if (!armed.load())
        return;
    {
        std::lock_guard<std::mutex> g(fire_lock);
        fired_ids.push_back(id);
    }
    if (!hot)
        emit("{\"event\":\"fired\",\"id\":" + testagent::esc(id) + "}");
}

void testagent_screen(const char *name)
{
    if (!armed.load() || !name)
        return;
    std::lock_guard<std::mutex> g(phase_lock);
    screen_name = name;
}

void testagent_flow(const std::string &name, uint32_t to)
{
    if (!armed.load() || (to != 1 && to != 2))
        return;
    std::string id = name;
    for (const char *known : {"StateIntro", "StateFrontend", "StateTeamHQ", "StateSession", "StateLoadSession",
                              "StateTeamHQToSession", "StateTeamHQTimeAdvance"})
        if (name == testagent::flow_id(known))
            id = known;
    {
        std::lock_guard<std::mutex> g(phase_lock);
        flow_name = id;
    }
    if (to == 1) {
        std::lock_guard<std::mutex> g(fire_lock);
        fired_ids.push_back("flow:" + id);
    }
}

void testagent_home_vm(void *vm)
{
    if (armed.load() && vm && runtime::readable(vm, VM_TILE_LIST + 8))
        set_home(vm);
}

}
