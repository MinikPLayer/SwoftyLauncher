#include "runtime.hpp"

#include <safetyhook.hpp>
#include <swofty/mixin.hpp>

#include "apply/moments.hpp"
#include "events_dispatch.hpp"
#include "perf.hpp"

#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace runtime {
std::string hook_error(const safetyhook::MidHook::Error &e);
}

std::string settings_addon(const void *from, std::string &dll);

namespace swofty_core::hooks {
void bridge_legacy(const char *event, int64_t a, int64_t b, const void *data);
}

namespace {

const runtime::Sig S_FLOW{"Vm_Update_FlowStateMachine",
                          "41 57 41 56 41 55 41 54 56 57 55 53 48 81 EC 58 02 00 00 C5 F8 29 B4 24 40 02 00 00 C5 F9 28 F2",
                          0x4423ce0};
const runtime::Sig S_SESSION_ENTER{"Game_GameModule_OnSessionEnter",
                                   "41 57 41 56 41 54 56 57 53 48 81 EC 38 02 00 00 48 89 CE 80 3D ?? ?? ?? ?? 01",
                                   0x80c300};
const runtime::Sig S_RACE_STARTED{"Game_ChampionshipFormationLapComponent_OnRaceStarted",
                                  "41 57 41 56 41 55 41 54 56 57 55 53 48 83 EC 28 48 89 CE 80 3D ?? ?? ?? ?? 01 75 53",
                                  0xb808a0};

runtime::ev::Dispatcher<perf::Scope> disp;
safetyhook::MidHook h_flow, h_session_enter, h_race_started;
std::mutex queued_lock;
std::vector<std::pair<uint32_t, std::string>> queued_by;

double now_seconds()
{
    static LARGE_INTEGER freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return freq.QuadPart ? double(t.QuadPart) / double(freq.QuadPart) : 0.0;
}

uint64_t caller_module(const void *from)
{
    HMODULE m = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(from), &m);
    return reinterpret_cast<uint64_t>(m);
}

std::string who(const void *from)
{
    std::string dll;
    std::string id = settings_addon(from, dll);
    return id.empty() ? dll : id;
}

void flow_mid(safetyhook::Context &)
{
    static const uint32_t probe = perf::probe("core", "flow tick (events, career poll)");
    perf::flow_tick();
    perf::Scope p(probe);
    runtime::career_tick();
    runtime::popup_tick();
    runtime::mfd_tick();
    runtime::netplay_tick();
    runtime::career_content_tick();
    runtime::hub_tick();
    runtime::testagent_tick();
    if (!perf::frame_claim())
        return;
    double dt = disp.tick(now_seconds());
    perf::frame_release();
    static int logged;
    if (logged < 2)
        swofty::log("[events] flow tick %d observed, dt %f; onFrame is live", ++logged, dt);
}

void session_enter_mid(safetyhook::Context &)
{
    static const uint32_t probe = perf::probe("core", "session enter");
    perf::mark("session", "enter");
    perf::Scope p(probe);
    swofty_core::apply::moments::session_load("Game_GameModule_OnSessionEnter");
    disp.fire(SWOFTY_EVENT_SESSION_START, 0, 0, nullptr, now_seconds());
    swofty_core::hooks::bridge_legacy(SWOFTY_EVENT_SESSION_START, 0, 0, nullptr);
}

void race_started_mid(safetyhook::Context &)
{
    static const uint32_t probe = perf::probe("core", "race start");
    perf::mark("race", "start");
    perf::Scope p(probe);
    disp.fire(SWOFTY_EVENT_RACE_START, 0, 0, nullptr, now_seconds());
    swofty_core::hooks::bridge_legacy(SWOFTY_EVENT_RACE_START, 0, 0, nullptr);
}

int ev_available()
{
    return runtime::enabled("events", nullptr) && disp.phase() != runtime::ev::Phase::failed;
}

std::string outcome(int r, uint32_t token, bool queued)
{
    if (r == SWOFTY_ASSET_UNAVAILABLE)
        return "unavailable: the flow tick did not resolve on this build, so onFrame never fires";
    if (r != SWOFTY_ASSET_OK)
        return "invalid";
    return "ok token " + std::to_string(token) + (queued ? ", queued until the events setup" : "");
}

void remember(uint32_t token, const std::string &by)
{
    std::lock_guard<std::mutex> g(queued_lock);
    queued_by.push_back({token, by});
}

int ev_subscribe(const char *event, SwoftyEventFn fn, void *user, uint32_t *token)
{
    const void *from = __builtin_return_address(0);
    if (!runtime::enabled("events", nullptr))
        return SWOFTY_ASSET_UNAVAILABLE;
    uint32_t t = 0;
    bool queued = false;
    int r = disp.subscribe(caller_module(from), event, fn, user, &t, &queued);
    if (r == SWOFTY_ASSET_OK && token)
        *token = t;
    std::string by = who(from);
    if (r == SWOFTY_ASSET_OK)
        disp.set_probe(t, perf::probe(perf::owner_of(reinterpret_cast<const void *>(fn)), std::string("event ") + event));
    if (r == SWOFTY_ASSET_OK && queued)
        remember(t, by);
    swofty::log("[events] subscribe %s by %s -> %s", event ? event : "(null)", by.c_str(), outcome(r, t, queued).c_str());
    return r;
}

int ev_on_frame(SwoftyFrameFn fn, void *user, uint32_t *token)
{
    const void *from = __builtin_return_address(0);
    if (!runtime::enabled("events", nullptr))
        return SWOFTY_ASSET_UNAVAILABLE;
    uint32_t t = 0;
    bool queued = false;
    int r = disp.on_frame(caller_module(from), fn, user, &t, &queued);
    if (r == SWOFTY_ASSET_OK && token)
        *token = t;
    std::string by = who(from);
    if (r == SWOFTY_ASSET_OK)
        disp.set_probe(t, perf::probe(perf::owner_of(reinterpret_cast<const void *>(fn)), "on_frame"));
    if (r == SWOFTY_ASSET_OK && queued)
        remember(t, by);
    swofty::log("[events] on_frame by %s -> %s", by.c_str(), outcome(r, t, queued).c_str());
    return r;
}

int ev_unsubscribe(uint32_t token)
{
    if (!runtime::enabled("events", nullptr))
        return SWOFTY_ASSET_UNAVAILABLE;
    int r = disp.unsubscribe(token);
    swofty::log("[events] unsubscribe token %u -> %s", token, r == SWOFTY_ASSET_OK ? "ok" : "invalid");
    return r;
}

const SwoftyEvents table = {ev_available, ev_subscribe, ev_on_frame, ev_unsubscribe};

uintptr_t resolve(const runtime::Sig &s)
{
    uintptr_t at = 0, base = swofty::api->module_base();
    int hits = swofty::api->scan(".xtls", s.pattern, &at);
    if (hits != 1 || !at) {
        swofty::log("[events] %s signature matched %d time(s); hook not installed", s.what, hits);
        return 0;
    }
    if (at - base != s.rva)
        swofty::log("[events] %s resolved to base+0x%llx, KB build has base+0x%llx", s.what,
                    (unsigned long long)(at - base), (unsigned long long)s.rva);
    return at;
}

safetyhook::MidHook mid(const runtime::Sig &s, void (*fn)(safetyhook::Context &))
{
    uintptr_t at = resolve(s);
    if (!at)
        return {};
    auto h = safetyhook::MidHook::create(reinterpret_cast<void *>(at), fn);
    if (!h) {
        swofty::log("[events] %s at base+0x%llx: the hook failed: %s", s.what,
                    (unsigned long long)(at - swofty::api->module_base()),
                    runtime::hook_error(h.error()).c_str());
        return {};
    }
    swofty::log("[events] %s hooked at base+0x%llx", s.what, (unsigned long long)(at - swofty::api->module_base()));
    return std::move(*h);
}

void report(const std::vector<runtime::ev::Queued> &held, bool live)
{
    std::vector<std::pair<uint32_t, std::string>> by;
    {
        std::lock_guard<std::mutex> g(queued_lock);
        by.swap(queued_by);
    }
    auto owner = [&](uint32_t token) {
        for (auto &[t, name] : by)
            if (t == token)
                return name;
        return std::string("an addon");
    };
    if (held.empty())
        return;
    if (live) {
        std::string list;
        for (auto &q : held)
            list += (list.empty() ? "" : ", ") + std::string(q.name ? q.name : "on_frame") + " token " +
                    std::to_string(q.token) + " (" + owner(q.token) + ")";
        swofty::log("[events] %zu registration(s) queued before setup are armed: %s", held.size(), list.c_str());
        return;
    }
    size_t subscriptions = 0;
    for (auto &q : held) {
        if (q.name) {
            subscriptions++;
            continue;
        }
        swofty::log("[events] on_frame token %u by %s was queued before setup and will not fire: the flow tick did "
                    "not resolve on this build",
                    q.token, owner(q.token).c_str());
    }
    if (subscriptions)
        swofty::log("[events] %zu event subscription(s) queued before setup stay registered; each fires only when its "
                    "source hook is armed (session.start %s, race.start %s; the career and flow events follow the "
                    "[career] setup line)",
                    subscriptions, h_session_enter ? "armed" : "off", h_race_started ? "armed" : "off");
}

void setup()
{
    if (!runtime::enabled("events", "events")) {
        swofty::log("[events] feature off; subscribe/on_frame will report unavailable");
        return;
    }
    h_flow = mid(S_FLOW, flow_mid);
    h_session_enter = mid(S_SESSION_ENTER, session_enter_mid);
    h_race_started = mid(S_RACE_STARTED, race_started_mid);
    swofty::log("[events] setup: %s; onFrame %s, session.start %s, race.start %s",
                h_flow ? "live" : "flow tick did not resolve (onFrame will not fire on this build)",
                h_flow ? "armed" : "off", h_session_enter ? "armed" : "off", h_race_started ? "armed" : "off");
    report(disp.arm(bool(h_flow)), bool(h_flow));
}

}

namespace runtime {

const SwoftyEvents *events_table()
{
    return &table;
}

void events_setup()
{
    setup();
}

size_t events_fire(const char *event, int64_t a, int64_t b, const void *data)
{
    if (!runtime::enabled("events", nullptr))
        return 0;
    size_t n = disp.fire(event, a, b, data, now_seconds());
    swofty_core::hooks::bridge_legacy(event, a, b, data);
    return n;
}

void events_drop_module(void *module)
{
    size_t n = disp.drop_owner(reinterpret_cast<uint64_t>(module));
    if (n)
        swofty::log("[events] dropped %zu subscription(s) from a module that unloaded or failed to init", n);
}

}
