#include "runtime.hpp"

#include <safetyhook.hpp>
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <atomic>
#include <mutex>
#include <string>

#include "career_start.hpp"
#include "perf.hpp"

// Career start gate (Vm_Run_Route, sig vm.run_route). Every native route fire (Vm_Fire_Route,
// which the front-end tile handlers call) runs its steps through Vm_Run_Route(ui_system, node,
// route_name, group, flags, a6, a7); route_name is an NeStr ** whose characters sit at +9. The
// career screens start each mode through fixed route names (career_start.hpp). The wrap publishes
// career.starting for those routes and, when an addon holds a block on the route's mode, returns
// without running the route: nothing has closed or opened yet, so the player stays on the screen
// they pressed on, and the blocking addon's popup is queued. The route runs before the game builds
// the career, which happens as fe_career_dlc_select or the career wizard opens.

std::string settings_addon(const void *from, std::string &dll);

namespace {

namespace cs = runtime::career_start;

const runtime::Sig S_RUN_ROUTE{"Vm_Run_Route",
                               "41 57 41 56 41 55 41 54 56 57 55 53 48 81 EC A8 00 00 00 C5 F8 29 B4 24 90 00 00 00 4C 89 "
                               "CB 4C 89 C7 48 89 CE",
                               0x4188370};
constexpr size_t NESTR_CHARS = 9;
constexpr uint64_t POPUP_GAP_MS = 1500;

using RunRoute_t = void(void *ui, void *node, void *name, void *group, int32_t flags, int64_t a6, int64_t a7);

safetyhook::InlineHook h_run;
std::mutex lock;
cs::Blocks blocks;
uint64_t last_popup[3];
bool live, diag;
std::atomic<uint32_t> picked{0};

const char *mode_name(uint32_t m)
{
    return m == SWOFTY_CAREER_MY_TEAM ? "my_team" : m == SWOFTY_CAREER_DRIVER ? "driver_career" : "none";
}

const char *kind_name(uint32_t k)
{
    return k == SWOFTY_CAREER_START_NEW ? "new" : k == SWOFTY_CAREER_START_CONTINUE ? "continue" : "pick";
}

bool route_name(void *name, std::string &out)
{
    uintptr_t s = 0;
    return name && swofty::mem::read(reinterpret_cast<uintptr_t>(name), s) && s &&
           swofty::mem::c_string(s + NESTR_CHARS, out, 80);
}

bool gate(const cs::Route &r)
{
    if (r.kind == SWOFTY_CAREER_START_PICK)
        picked = r.mode;
    std::string by, title, body;
    bool blocked = false, popup = false;
    {
        std::lock_guard<std::mutex> g(lock);
        if (const cs::Block *b = blocks.find(r.mode)) {
            blocked = true;
            by = b->by, title = b->title, body = b->body;
            popup = (!title.empty() || !body.empty()) && cs::due(GetTickCount64(), last_popup[r.mode], POPUP_GAP_MS);
        }
    }
    SwoftyCareerStart s = cs::make_start(r, blocked);
    perf::mark("career", std::string("starting ") + mode_name(r.mode) + (blocked ? " blocked" : ""));
    swofty::log("[career] career.starting: %s (%s, route '%s')%s%s", mode_name(r.mode), kind_name(r.kind), r.name,
                blocked ? "; blocked by " : "", blocked ? by.c_str() : "");
    runtime::events_fire(SWOFTY_EVENT_CAREER_STARTING, r.mode, r.kind, &s);
    if (!blocked)
        return false;
    if (popup) {
        int rc = runtime::popup_show(title, body, by);
        if (rc != SWOFTY_ASSET_OK)
            swofty::log("[career] route '%s' cancelled; the popup for %s is unavailable (code %d), so the player sees "
                        "no message",
                        r.name, by.c_str(), rc);
        else
            swofty::log("[career] route '%s' cancelled; the player stays on the menu and %s's popup is queued", r.name,
                        by.c_str());
    } else {
        swofty::log("[career] route '%s' cancelled; the player stays on the menu", r.name);
    }
    return true;
}

void run_route_hook(void *ui, void *node, void *name, void *group, int32_t flags, int64_t a6, int64_t a7)
{
    static const uint32_t probe = perf::probe("core", "career start gate");
    std::string text;
    const cs::Route *r = nullptr;
    {
        perf::Scope p(probe);
        if (route_name(name, text)) {
            if (runtime::wizard_route_running(text, name))
                route_name(name, text);
            r = cs::classify(text);
        }
        if (!r && diag)
            swofty::log("[career] route '%s' (not a career start)", text.c_str());
        if (r && gate(*r))
            return;
    }
    perf::Pause game;
    h_run.call<void>(ui, node, name, group, flags, a6, a7);
}

uint64_t caller(const void *from)
{
    HMODULE m = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(from), &m);
    return reinterpret_cast<uint64_t>(m);
}

std::string who(const void *from)
{
    std::string dll, id = settings_addon(from, dll);
    return id.empty() ? dll : id;
}

}

namespace runtime {

int career_block(const void *from, uint32_t mode, const char *title, const char *body)
{
    std::string by = who(from);
    if (mode != SWOFTY_CAREER_MY_TEAM && mode != SWOFTY_CAREER_DRIVER) {
        swofty::log("[career] block by %s refused: mode %u is not my_team (1) or driver_career (2)", by.c_str(), mode);
        return SWOFTY_ASSET_INVALID;
    }
    if (!enabled("career", nullptr)) {
        swofty::log("[career] block by %s: the career feature is off", by.c_str());
        return SWOFTY_ASSET_UNAVAILABLE;
    }
    std::string t = title ? title : "", b = body ? body : "";
    {
        std::lock_guard<std::mutex> g(lock);
        blocks.set(caller(from), by, mode, t, b);
    }
    swofty::log("[career] %s blocks %s careers%s; %s", by.c_str(), mode_name(mode),
                t.empty() && b.empty() ? " (no popup)" : "",
                live ? "the start gate is live" : "the start gate is not installed yet");
    return SWOFTY_ASSET_OK;
}

int career_unblock(const void *from, uint32_t mode)
{
    std::string by = who(from);
    bool had;
    {
        std::lock_guard<std::mutex> g(lock);
        had = blocks.clear(caller(from), mode);
    }
    swofty::log("[career] %s %s its %s block", by.c_str(), had ? "lifts" : "has no", mode_name(mode));
    return had ? SWOFTY_ASSET_OK : SWOFTY_ASSET_INVALID;
}

void career_start_drop_module(void *module)
{
    std::lock_guard<std::mutex> g(lock);
    blocks.drop_owner(reinterpret_cast<uint64_t>(module));
}

bool career_start_live()
{
    return live;
}

uint32_t career_start_picked()
{
    return picked;
}

namespace {

void start_gate_setup()
{
    uintptr_t at = find(S_RUN_ROUTE, "start gate", "career");
    if (!at) {
        swofty::log("[career] start gate: Vm_Run_Route did not resolve; career.starting never fires and blocks cannot "
                    "cancel a route");
        return;
    }
    auto r = safetyhook::InlineHook::create(reinterpret_cast<void *>(at), reinterpret_cast<void *>(&run_route_hook));
    if (!r) {
        swofty::log("[career] start gate: hook on Vm_Run_Route failed; blocks cannot cancel a route");
        return;
    }
    h_run = std::move(*r);
    live = true;
    char v[8] = {};
    DWORD got = GetEnvironmentVariableA("SWOFTY_UI_DIAG", v, sizeof v);
    diag = got && got < sizeof v && v[0] && v[0] != '0';
    size_t n;
    {
        std::lock_guard<std::mutex> g(lock);
        n = blocks.size();
    }
    swofty::log("[career] start gate: live on Vm_Run_Route (base+0x%llx), %zu block(s) held",
                (unsigned long long)(at - swofty::api->module_base()), n);
}

}

void career_start_setup()
{
    if (enabled("career", nullptr))
        when_code_ready("career: start gate (Vm_Run_Route hook)", start_gate_setup);
}

}
