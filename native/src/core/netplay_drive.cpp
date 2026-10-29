#include "runtime.hpp"

#include <safetyhook.hpp>
#include <swofty/game.hpp>
#include <swofty/mixin.hpp>

#include <atomic>
#include <cstring>
#include <deque>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include "netplay_drive_logic.hpp"
#include "netplay_session.hpp"

// Launcher commands for LAN netplay, over the same kind of file mailbox the Career Editor uses
// (protocol/bridge.md there): <game>/swofty/netplay/inbox/<seq>.json from the launcher (written as
// .tmp and renamed), one reply per command in outbox/<seq>.json, and status.json rewritten when it
// changes and every 5 s. A watcher thread does all file IO; commands run on the flow thread from the
// flow tick (runtime::netplay_tick), the thread the menus' own handlers run on.
//
// State comes from what core already sees: flow states (StateFrontend for the front end,
// StateOnlineLobby for a LAN lobby) and the screens the UI opens (fe_home for the main menu).
// status and clear always answer. configure (the room: member number, bridge base port and the
// signed-in name for the driver, shared with the hooks through netplay_session.hpp), host, join and
// leave need SWOFTY_NETPLAY in the game's environment, host and join also a configured room, and each
// a game state that allows it. host, join and leave report unavailable until their game entry points
// are bound (research/multiplayer/lan-netplay-design.md, "Driving host/join").

namespace runtime {
std::string hook_error(const safetyhook::MidHook::Error &e);
}

bool ui_vm_set_path(const std::string &path, int type, const void *value);

namespace {

namespace drive = runtime::drive;

// The systems the LAN tile's getAction switches with (base+0x172c840: game mode "online_custom",
// then NetworkSubsystem::changeMPMode to eLan) come from its init (base+0x172c5a0), which looks
// them up in the tile builder's context: the game mode manager into item+0x148, the network
// subsystem into item+0x150. With the LAN tile no longer built, a probe at the builder's populate
// loop (site_FeTile_PopulateLoop, where rbx and rdi are the init arguments of every tile) runs that
// init on a private LAN item while armed, so the mode switch can later run on it without the tile.
// The probe only reads; it logs what it reached.
const runtime::Sig S_LAN_VT{"vt_FeTile_LanItem",
                            "48 8D 05 ?? ?? ?? ?? 49 89 04 24 C4 C1 78 11 B4 24 48 01 00 00 41 C6 84 24 58 01 00 00 00",
                            0x5704740, runtime::Sig::riprel, 0, 3, 7};
const runtime::Sig S_POPULATE_LOOP{"site_FeTile_PopulateLoop",
                                   "49 8B 76 78 4D 8B B6 80 00 00 00 4C 39 F6 8B AC 24 00 03 00 00", 0x1745016};
constexpr size_t LAN_ITEM_SIZE = 0x160, LAN_GAME_MODES = 0x148, LAN_NETWORK = 0x150, LAN_ACTIVE = 0x158;
constexpr unsigned MAX_PROBES = 2;

using LanInit_t = uint64_t(void *item, void *registry, void *ctx);

alignas(16) uint8_t lan_item[LAN_ITEM_SIZE];
void **lan_vt;
safetyhook::MidHook h_populate;
unsigned probes;
std::atomic<bool> lan_systems;

template <class T> T lan_field(size_t off)
{
    T v;
    std::memcpy(&v, lan_item + off, sizeof v);
    return v;
}

void capture_vm(safetyhook::Context &c);

void populate_mid(safetyhook::Context &c)
{
    capture_vm(c);
    if (lan_systems.load() || probes >= MAX_PROBES)
        return;
    auto *registry = reinterpret_cast<void *>(c.rbx), *ctx = reinterpret_cast<void *>(c.rdi);
    if (!runtime::readable(registry, 8) || !runtime::readable(ctx, 0x28))
        return;
    ++probes;
    std::memset(lan_item, 0, sizeof lan_item);
    std::memcpy(lan_item, &lan_vt, sizeof lan_vt);
    uint64_t rc = reinterpret_cast<LanInit_t *>(lan_vt[3])(lan_item, registry, ctx);
    auto *modes = lan_field<void *>(LAN_GAME_MODES), *network = lan_field<void *>(LAN_NETWORK);
    bool ok = rc == 0 && modes && network;
    lan_systems = ok;
    swofty::log("[netplay] LAN systems probe %u: init returned 0x%llx, game modes %p, network %p, Network.LAN.active %u%s",
                probes, (unsigned long long)rc, modes, network, unsigned(lan_item[LAN_ACTIVE]),
                ok ? "; the mode switch can run without the LAN tile" : "");
}

// View models the menus fire routes from. Every tile-list view model keeps its TileList at +0x2c8
// (career_content.cpp finds its owner the same way), so at the populate loop the owner of the list in
// r14 is on the stack: the home view model (vtable slot 10 is FeHome_Handle_Command) and the Local
// Multiplayer one (its list is the lone splitscreen tile once the LAN tile is gone). A route fires as
// the view model's own handlers fire it: Vm_Fire_Route(vm->router (+0x28), vm, &name, 0), which runs
// the route in the group of the view model's screen.
const runtime::Sig S_FIRE_CALL{"Vm_Fire_Route", "48 8B 4E 28 48 89 F2 49 89 F8 45 31 C9 E8 ?? ?? ?? ?? E9 E4 00 00 00",
                               0x42c0e20, runtime::Sig::rel32, 13, 14, 5};
constexpr size_t VM_TILE_LIST = 0x2c8, VM_ROUTER = 0x28, STACK_SCAN = 0x1000, NESTR_CHARS = 9;
constexpr uintptr_t NET_SYSTEM = 0xd8, NET_MODE = 0x750;

using Fire_t = void(void *router, void *vm, swofty::game::NeStr **name, uint64_t a4);
using GetAction_t = swofty::game::Action *(void *item, swofty::game::Action *out);
using PopupCb_t = uint64_t(void *ctx, swofty::game::NeStr **button);

struct Vm {
    std::atomic<void *> self{nullptr}, vt{nullptr};

    void set(void *v)
    {
        vt = *static_cast<void **>(v);
        self = v;
    }

    void *live() const
    {
        void *v = self.load();
        return v && runtime::readable(v, VM_TILE_LIST + 8) && *static_cast<void **>(v) == vt.load() ? v : nullptr;
    }
};

Vm home_vm, local_mp_vm;
uintptr_t a_fire;
std::atomic<bool> local_mp_opened;

bool lone_splitscreen(swofty::game::TileList *list)
{
    auto *b = list->items_begin, *e = list->items_end;
    if (!b || e != b + 1 || !runtime::readable(b, sizeof *b) || !b->tile || !runtime::readable(b->tile, 0xd8))
        return false;
    auto *item = b->tile->item;
    if (!item || !runtime::readable(item, 0x18) || !item->id || !runtime::readable(item->id, NESTR_CHARS + 12))
        return false;
    return !std::strcmp(reinterpret_cast<const char *>(item->id) + NESTR_CHARS, "splitscreen");
}

void capture_vm(safetyhook::Context &c)
{
    auto *list = reinterpret_cast<swofty::game::TileList *>(c.r14);
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
        if (!runtime::readable(vt, 11 * sizeof(void *)))
            return;
        bool home = reinterpret_cast<uintptr_t>(vt[10]) == swofty::game::FeHome_Handle_Command.address();
        Vm &slot = home ? home_vm : local_mp_vm;
        if (!home && !lone_splitscreen(list))
            continue;
        if (slot.self.load() != v)
            swofty::log("[netplay] %s view model %p", home ? "home" : "Local Multiplayer", v);
        slot.set(v);
        return;
    }
}

bool fire_route(void *vm, const char *name)
{
    if (!a_fire || !vm)
        return false;
    void *router = *reinterpret_cast<void **>(static_cast<uint8_t *>(vm) + VM_ROUTER);
    if (!router)
        return false;
    swofty::game::NeStr *s = nullptr;
    swofty::game::Ne_Make_StrFromCStr(&s, name, 0, 0);
    reinterpret_cast<Fire_t *>(a_fire)(router, vm, &s, 0);
    swofty::game::Ne_Make_StrFromCStr(&s, nullptr, 0, 0);
    return true;
}

bool lan_mode()
{
    auto *network = lan_field<uint8_t *>(LAN_NETWORK);
    if (!lan_systems.load() || !network || !runtime::readable(network + NET_SYSTEM, 8))
        return false;
    auto *sys = *reinterpret_cast<uint8_t **>(network + NET_SYSTEM);
    return sys && runtime::readable(sys + NET_MODE, 1) && sys[NET_MODE] == 1;
}

std::string mode_switch()
{
    if (!lan_systems.load())
        return "the LAN systems were not reached (no LAN systems probe line in loader.log)";
    swofty::game::Action a{};
    reinterpret_cast<GetAction_t *>(lan_vt[5])(lan_item, &a);
    int32_t type = a.type;
    if (a.str)
        swofty::game::Ne_Make_StrFromCStr(&a.str, nullptr, 0, 0);
    if (type == 3)
        return "the game has LAN custom games switched off (beta_mp_custom_disabled_msg)";
    return {};
}

bool answer(void *fn, void *ctx, const char *button)
{
    if (!fn || !ctx)
        return false;
    swofty::game::NeStr *s = nullptr;
    swofty::game::Ne_Make_StrFromCStr(&s, button, 0, 0);
    reinterpret_cast<PopupCb_t *>(fn)(ctx, &s);
    swofty::game::Ne_Make_StrFromCStr(&s, nullptr, 0, 0);
    return true;
}

void probe_setup()
{
    uintptr_t vt = runtime::find(S_LAN_VT, "netplay", "netplay"), loop = runtime::find(S_POPULATE_LOOP, "netplay", "netplay");
    if (!vt || !loop || !runtime::readable(reinterpret_cast<void *>(vt), 6 * sizeof(void *))) {
        swofty::log("[netplay] LAN systems probe not installed (LAN item vtable %s, populate loop %s)",
                    vt ? "found" : "missing", loop ? "found" : "missing");
        return;
    }
    lan_vt = reinterpret_cast<void **>(vt);
    a_fire = runtime::find(S_FIRE_CALL, "netplay", "netplay");
    auto h = safetyhook::MidHook::create(reinterpret_cast<void *>(loop), populate_mid);
    if (!h) {
        swofty::log("[netplay] LAN systems probe: the hook failed: %s", runtime::hook_error(h.error()).c_str());
        return;
    }
    h_populate = std::move(*h);
    swofty::log("[netplay] LAN systems probe at base+0x%llx (LAN item init base+0x%llx, getAction base+0x%llx)",
                (unsigned long long)(loop - swofty::api->module_base()),
                (unsigned long long)(reinterpret_cast<uintptr_t>(lan_vt[3]) - swofty::api->module_base()),
                (unsigned long long)(reinterpret_cast<uintptr_t>(lan_vt[5]) - swofty::api->module_base()));
}

constexpr DWORD POLL_MS = 250;
constexpr uint64_t HEARTBEAT_MS = 5000;

std::atomic<bool> armed, frontend, lobby, home_screen;
std::atomic<int> role{int(drive::Role::none)};

std::mutex queue_lock;
std::deque<drive::Command> incoming;
std::deque<std::pair<uint64_t, std::string>> outgoing;
std::atomic<bool> started;

drive::State state()
{
    drive::State s;
    s.armed = armed.load();
    s.configured = runtime::netplay_session::snapshot(s.self, s.base);
    s.frontend = frontend.load();
    s.lobby = lobby.load();
    s.home_screen = home_screen.load();
    s.role = drive::Role(role.load());
    return s;
}

uint64_t wall_ms()
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t t = (uint64_t(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return t / 10000 - 11644473600000ull;
}

std::wstring dir(const char *sub)
{
    return runtime::under_swofty(std::string("netplay\\") + sub);
}

std::string run_leave(const drive::Command &)
{
    return "unavailable: leaving is not bound in this build";
}

std::mutex job_lock;
drive::Job job;
void *answer_fn, *answer_ctx;
std::string job_ip;

void watch()
{
    std::wstring inbox = dir("inbox"), outbox = dir("outbox");
    CreateDirectoryW(runtime::under_swofty("netplay").c_str(), nullptr);
    CreateDirectoryW(inbox.c_str(), nullptr);
    CreateDirectoryW(outbox.c_str(), nullptr);
    std::set<uint64_t> seen;
    std::string last_status;
    uint64_t last_written = 0;
    for (;;) {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((inbox + L"\\*.json").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                uint64_t seq = 0;
                std::string name = runtime::narrow(fd.cFileName);
                if (!drive::inbox_name(name, &seq) || seen.count(seq))
                    continue;
                seen.insert(seq);
                drive::Command c = drive::parse(runtime::read_file(inbox + L"\\" + fd.cFileName, 64 << 10));
                if (!c.seq || c.seq != seq) {
                    if (c.error.empty())
                        c.error = "seq missing or different from the file name";
                    c.seq = seq;
                }
                swofty::log("[netplay] command %llu: %s%s%s", (unsigned long long)seq, drive::op_name(c.op),
                            c.error.empty() ? "" : " (", c.error.empty() ? "" : (c.error + ")").c_str());
                std::lock_guard<std::mutex> g(queue_lock);
                incoming.push_back(std::move(c));
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        std::deque<std::pair<uint64_t, std::string>> done;
        {
            std::lock_guard<std::mutex> g(queue_lock);
            done.swap(outgoing);
        }
        for (auto &[seq, body] : done) {
            std::wstring n = std::to_wstring(seq) + L".json";
            if (!runtime::write_file(outbox + L"\\" + n, body))
                swofty::log("[netplay] could not write the reply to command %llu", (unsigned long long)seq);
            DeleteFileW((inbox + L"\\" + n).c_str());
        }
        std::string status = drive::status_json(state());
        uint64_t now = GetTickCount64();
        if (status != last_status || now - last_written >= HEARTBEAT_MS) {
            std::string body = "{\"v\":1,\"pid\":" + std::to_string(GetCurrentProcessId()) +
                               ",\"written\":" + std::to_string(wall_ms()) + ",\"status\":" + status + "}";
            if (runtime::write_file(runtime::under_swofty("netplay\\status.json"), body)) {
                if (status != last_status)
                    swofty::log("[netplay] status %s", status.c_str());
                last_status = status;
                last_written = now;
            }
        }
        Sleep(POLL_MS);
    }
}

}

namespace runtime {

// FeHome_Handle_Command receives the home view model on every home command, so it backs up the
// populate-loop capture for a home screen whose list was built before the core could see it.
void netplay_home_vm(void *vm)
{
    if (!armed || !vm || !runtime::readable(vm, VM_TILE_LIST + 8))
        return;
    if (home_vm.self.load() != vm)
        swofty::log("[netplay] home view model %p (home command)", vm);
    home_vm.set(vm);
}

void netplay_drive_setup()
{
    char v[1024] = {};
    DWORD n = GetEnvironmentVariableA("SWOFTY_NETPLAY", v, sizeof v);
    armed = n > 0 && n < sizeof v;
    if (!armed)
        return;
    swofty::log("[netplay] launcher commands on (<game>/swofty/netplay); configure, host, join and leave accepted");
    probe_setup();
    if (!started.exchange(true))
        std::thread(watch).detach();
}

void netplay_flow(uint32_t hash, uint32_t to)
{
    bool in = to == 1 || to == 2;
    if (hash == drive::STATE_FRONTEND) {
        frontend = in;
        if (!in) {
            home_screen = false;
            home_vm.self = nullptr;
            local_mp_vm.self = nullptr;
        }
    } else if (hash == drive::STATE_ONLINE_LOBBY) {
        lobby = in;
        if (!in)
            role = int(drive::Role::none);
    }
}

void netplay_screen(const char *name)
{
    static const char *const overlays[] = {"fe_planes", "persistent", "playercard", "render_badge",
                                           "theming_overlay_frontend", "standard_wipe", "frontend_transition",
                                           "slice_bg_content_underlay", "f1_life_driver_tags", "audio_themes"};
    if (!name || std::strncmp(name, "fe_", 3) != 0)
        return;
    for (const char *o : overlays)
        if (!std::strcmp(name, o))
            return;
    home_screen = !std::strcmp(name, "fe_home");
    if (!std::strcmp(name, "fe_local_multiplayer"))
        local_mp_opened = true;
}

bool netplay_popup(const char *id, void *fn, void *ctx)
{
    if (!id)
        return false;
    std::lock_guard<std::mutex> g(job_lock);
    if (!job.popup(id, GetTickCount64()))
        return false;
    answer_fn = fn;
    answer_ctx = ctx;
    swofty::log("[netplay] %s: answering '%s' for the launcher (callback %p, view model %p)", drive::op_name(job.cmd.op),
                id, fn, ctx);
    return true;
}

void reply(const drive::Command &c, const std::string &error, const std::string &detail = {})
{
    if (c.op != drive::Op::status || !error.empty())
        swofty::log("[netplay] command %llu %s: %s%s%s%s", (unsigned long long)c.seq, drive::op_name(c.op),
                    error.empty() ? "ok" : error.c_str(), detail.empty() ? "" : " (", detail.c_str(),
                    detail.empty() ? "" : ")");
    std::string body = drive::reply_json(c, state(), error, wall_ms(), detail);
    std::lock_guard<std::mutex> g(queue_lock);
    outgoing.emplace_back(c.seq, std::move(body));
}

void run_job()
{
    drive::Facts f;
    void *local_mp = local_mp_vm.live();
    f.local_mp = local_mp && local_mp_opened.load();
    f.lan_mode = lan_mode();
    f.lobby = lobby.load();
    f.home = frontend.load() && home_screen.load();
    drive::Do d;
    void *fn, *ctx;
    {
        std::lock_guard<std::mutex> g(job_lock);
        d = job.tick(f, GetTickCount64());
        fn = answer_fn;
        ctx = answer_ctx;
    }
    std::string err;
    switch (d) {
    case drive::Do::fire_local_mp:
        // The screen builds a new view model on each visit, often at the old one's address. Until the
        // new tile list is populated the old pointer still reads as live, and a route fired from it
        // lands outside the new screen, which drops the game back to online mode.
        local_mp_opened = false;
        local_mp_vm.self = nullptr;
        if (!fire_route(home_vm.live(), "local_multiplayer"))
            err = "no home view model to fire local_multiplayer from (Vm_Fire_Route or the home tile list not seen)";
        break;
    case drive::Do::mode_switch:
        err = mode_switch();
        break;
    case drive::Do::fire_lan:
        if (!fire_route(local_mp, "lan"))
            err = "no Local Multiplayer view model to fire lan from";
        break;
    case drive::Do::answer_dialog:
        if (!answer(fn, ctx, job.button()))
            err = "the LAN Game dialog had no callback to answer";
        break;
    case drive::Do::answer_ip:
        if (!ui_vm_set_path("ui_menu_data.lan_ip_address", 0, job_ip.c_str()))
            err = "could not set ui_menu_data.lan_ip_address";
        else if (!answer(fn, ctx, "ok"))
            err = "the Join via IP prompt had no callback to answer";
        break;
    default:
        break;
    }
    if (d != drive::Do::nothing)
        swofty::log("[netplay] %s: step %d (%s)%s%s", drive::op_name(job.cmd.op), int(d), drive::do_name(d),
                    err.empty() ? "" : " failed: ", err.c_str());
    drive::Command c;
    std::string error, detail;
    {
        std::lock_guard<std::mutex> g(job_lock);
        if (!err.empty())
            job.fail(err);
        if (!job.finished())
            return;
        c = job.cmd;
        error = job.error;
        detail = job.detail;
        job.reset();
        if (error.empty())
            role = int(c.op == drive::Op::host ? drive::Role::host : drive::Role::client);
    }
    reply(c, error, detail);
}

void netplay_tick()
{
    if (!started.load())
        return;
    bool running;
    {
        std::lock_guard<std::mutex> g(job_lock);
        running = job.active();
    }
    if (running)
        run_job();
    drive::Command c;
    {
        std::lock_guard<std::mutex> g(queue_lock);
        if (incoming.empty())
            return;
        c = std::move(incoming.front());
        incoming.pop_front();
    }
    drive::State s = state();
    s.busy = running;
    std::string error = drive::refuse(c, s);
    if (error.empty()) {
        switch (c.op) {
        case drive::Op::configure:
            if (runtime::netplay_session::configure(c.self, c.base)) {
                runtime::netplay_session::set_name(c.name);
                runtime::netplay_bridge_status_changed();
            } else
                error = "the room was refused (self " + std::to_string(c.self) + ", base " + std::to_string(c.base) + ")";
            break;
        case drive::Op::clear:
            runtime::netplay_session::clear();
            runtime::netplay_bridge_status_changed();
            break;
        case drive::Op::host:
        case drive::Op::join: {
            std::lock_guard<std::mutex> g(job_lock);
            job.start(c, GetTickCount64());
            job_ip = c.ip;
            answer_fn = answer_ctx = nullptr;
            swofty::log("[netplay] %s: started", drive::op_name(c.op));
            return;
        }
        case drive::Op::leave: error = run_leave(c); break;
        case drive::Op::cancel: {
            bool cancelled;
            {
                std::lock_guard<std::mutex> g(job_lock);
                cancelled = job.cancel();
            }
            reply(c, error, cancelled ? "the running host or join ends at its next step" : "nothing was running");
            return;
        }
        default: break;
        }
    }
    reply(c, error);
}

}
