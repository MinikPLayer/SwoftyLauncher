#include "../runtime.hpp"

#include <swofty/mixin.hpp>

#include "../blackbox.hpp"
#include "../perf.hpp"
#include "guard.hpp"
#include "hooks.hpp"

#include <mutex>
#include <string>
#include <vector>

std::string settings_addon(const void *from, std::string &dll);

namespace runtime {
void crash_blame(const char *text);
size_t addon_load_index(uint64_t module);
std::string mixin_overwrite_on(const std::string &binding, uintptr_t at);
std::string mixin_occupying(uintptr_t at);
}

namespace swofty_core::hooks {
namespace {


std::mutex faults_lock;
std::vector<FaultRecord> fault_list;
std::mutex records_lock;
RecordFn records_fn;

struct Caller {
    std::string addon;
    uint64_t module = 0;
};

Caller caller(const void *from)
{
    Caller c;
    HMODULE m = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(from), &m);
    c.module = reinterpret_cast<uint64_t>(m);
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&caller), &self);
    if (m == self)
        return c;
    std::string dll;
    c.addon = settings_addon(from, dll);
    if (c.addon.empty())
        c.addon = dll.empty() ? std::string("an unlisted dll") : dll;
    return c;
}

std::string module_at(uintptr_t a)
{
    HMODULE m = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(a), &m) ||
        !m) {
        char b[32];
        snprintf(b, sizeof b, "0x%llx", (unsigned long long)a);
        return b;
    }
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(m, path, MAX_PATH);
    std::wstring w(path);
    w = w.substr(w.find_last_of(L"\\/") + 1);
    char b[32];
    snprintf(b, sizeof b, "+0x%llx", (unsigned long long)(a - reinterpret_cast<uintptr_t>(m)));
    return runtime::narrow(w) + b;
}

std::string describe(DWORD code, uintptr_t at, const char *thrown = "")
{
    if (code == guard::HANDLER_THREW)
        return std::string("C++ exception") + (thrown && *thrown ? std::string(": ") + thrown : std::string()) +
               " from " + module_at(at);
    const char *what = code == EXCEPTION_ACCESS_VIOLATION                  ? "access violation"
                       : code == guard::MSVC_CXX || code == guard::GCC_CXX ? "C++ exception"
                       : code == EXCEPTION_ILLEGAL_INSTRUCTION             ? "illegal instruction"
                       : code == EXCEPTION_INT_DIVIDE_BY_ZERO              ? "integer divide by zero"
                       : code == EXCEPTION_PRIV_INSTRUCTION                ? "privileged instruction"
                       : code == EXCEPTION_IN_PAGE_ERROR                   ? "in-page error"
                       : code == EXCEPTION_ARRAY_BOUNDS_EXCEEDED           ? "array bounds exceeded"
                                                                           : "exception";
    char b[48];
    snprintf(b, sizeof b, " 0x%08lx", (unsigned long)code);
    return std::string(what) + (what[0] == 'e' ? b : "") + " at " + module_at(at);
}

void blame(const void *ctx, unsigned long code, uintptr_t at, const char *thrown)
{
    auto *b = static_cast<const std::pair<const Handler *, const SwoftyHookCall *> *>(ctx);
    std::string what = describe(DWORD(code), at, thrown);
    std::string addon = b->first->addon.empty() ? std::string("the core") : b->first->addon;
    std::string line = "Crash in " + addon + "'s " + catalogue::phase_name(b->second->phase) + " handler on " +
                       b->second->point;
    runtime::crash_blame((line + " (" + what + ")").c_str());
    blackbox::note("hooks", line.c_str(), (" (" + what + ")").c_str());
    swofty::log("[hooks] %s: %s; a before or replace handler that faults crashes the game", line.c_str(), what.c_str());
}

int guarded(const Handler &h, SwoftyHookCall *c, bool isolate, Fault *f)
{
    perf::Scope p(h.probe);
    if (!isolate) {
        std::pair<const Handler *, const SwoftyHookCall *> b{&h, c};
        return guard::blamed_call(h.fn, h.user, c, blame, &b);
    }
    guard::Seen seen{0, 0, {}};
    int r = guard::isolated_call(h.fn, h.user, c, &seen);
    if (seen.code) {
        f->faulted = true;
        f->what = describe(DWORD(seen.code), seen.at, seen.what);
        return 0;
    }
    return r;
}

Table &the_table()
{
    static Table *t = [] {
        Platform p;
        p.log = [](const std::string &s) { swofty::log("%s", s.c_str()); };
        p.call = guarded;
        p.faulted = [](const std::string &point, const char *phase, const std::string &addon, const std::string &what) {
            blackbox::note("hooks", (addon + " " + phase + " handler faulted on " + point).c_str(), what.c_str());
            std::lock_guard<std::mutex> g(faults_lock);
            fault_list.push_back({point, phase, addon, what});
        };
        p.attached = [](Handler &h, const std::string &point) {
            h.probe = perf::probe(h.addon.empty() ? std::string("core") : h.addon,
                                  std::string(catalogue::phase_name(h.phase)) + " handler on " + point);
        };
        p.load_order = [](const std::string &, uint64_t module) { return uint64_t(runtime::addon_load_index(module)); };
        p.fired = [](const std::string &event, bool hot) { runtime::testagent_fired(event, hot); };
        p.records = [](const Facts &f, std::string_view registry) {
            RecordFn fn;
            {
                std::lock_guard<std::mutex> g(records_lock);
                fn = records_fn;
            }
            return fn ? fn(f, registry) : std::string();
        };
        auto *t = new Table(std::move(p));
        t->declare_core();
        declare_game_points(*t);
        declare_session_points(*t);
        return t;
    }();
    return *t;
}

int put(const std::string &value, char *out, size_t cap, size_t *len)
{
    if (len)
        *len = value.size();
    if (!out || value.size() > cap)
        return SWOFTY_TOO_SMALL;
    memcpy(out, value.data(), value.size());
    if (value.size() < cap)
        out[value.size()] = 0;
    return SWOFTY_ASSET_OK;
}

std::string text(SwoftyText t)
{
    return t.data && t.size ? std::string(t.data, t.size) : std::string();
}

int h_available()
{
    return runtime::enabled("events", nullptr) ? 1 : 0;
}

int h_live(const char *point, uint32_t *version)
{
    if (!point)
        return SWOFTY_ASSET_INVALID;
    return the_table().live(point, version);
}

int attach_from(const void *from, const char *point, uint32_t phase, int32_t priority, SwoftyText selectors,
                SwoftyHookFn fn, void *user, uint32_t *token)
{
    if (!runtime::enabled("events", nullptr))
        return SWOFTY_ASSET_UNAVAILABLE;
    Caller c = caller(from);
    uint32_t t = 0;
    int r = the_table().attach(c.addon, point ? point : "", phase, priority, text(selectors), fn, user, &t, c.module);
    if (r == SWOFTY_ASSET_OK && token)
        *token = t;
    return r;
}

int h_attach(const char *point, uint32_t phase, int32_t priority, SwoftyHookFn fn, void *user, uint32_t *token)
{
    return attach_from(__builtin_return_address(0), point, phase, priority, SwoftyText{nullptr, 0}, fn, user, token);
}

int h_attach_in(const char *point, uint32_t phase, int32_t priority, SwoftyText selectors, SwoftyHookFn fn, void *user,
                uint32_t *token)
{
    return attach_from(__builtin_return_address(0), point, phase, priority, selectors, fn, user, token);
}

int h_detach(uint32_t token)
{
    if (!runtime::enabled("events", nullptr))
        return SWOFTY_ASSET_UNAVAILABLE;
    int r = the_table().detach(token);
    swofty::log("[hooks] detach token %u -> %s", token, r == SWOFTY_ASSET_OK ? "ok" : "invalid");
    return r;
}

int h_define(const char *point, uint32_t version)
{
    if (!runtime::enabled("events", nullptr))
        return SWOFTY_ASSET_UNAVAILABLE;
    Caller c = caller(__builtin_return_address(0));
    return the_table().define(c.addon, point ? point : "", version, SWOFTY_POINT_CANCEL, false, "");
}

int h_define_in(const char *point, uint32_t version, uint32_t kind, SwoftyText scope)
{
    if (!runtime::enabled("events", nullptr))
        return SWOFTY_ASSET_UNAVAILABLE;
    Caller c = caller(__builtin_return_address(0));
    return the_table().define(c.addon, point ? point : "", version, kind, true, text(scope));
}

int h_fire(const char *point, void *payload, int *replaced)
{
    if (!runtime::enabled("events", nullptr))
        return SWOFTY_ASSET_UNAVAILABLE;
    Caller c = caller(__builtin_return_address(0));
    return the_table().fire(c.addon, point ? point : "", payload, replaced);
}

int h_context(const void *context, const char *part, char *out, size_t cap, size_t *len)
{
    if (!part)
        return SWOFTY_ASSET_INVALID;
    std::string value;
    int r = the_table().context_part(context, part, value);
    return r == SWOFTY_ASSET_OK ? put(value, out, cap, len) : r;
}

int h_context_in(const void *context, SwoftyText selectors, int *out)
{
    return the_table().context_in(context, text(selectors), out);
}

const SwoftyHooks hooks_table = {SWOFTY_HOOKS_VERSION, h_available, h_live,   h_attach,     h_detach,  h_define,
                                 h_fire,               h_attach_in, h_define_in, h_context, h_context_in};

const char *mode_of(int64_t m)
{
    return m == SWOFTY_CAREER_MY_TEAM ? "my_team" : m == SWOFTY_CAREER_DRIVER ? "driver_career" : "";
}

}

const SwoftyHooks *table()
{
    return &hooks_table;
}

std::string describe_fault(unsigned long code, uintptr_t at, const char *what)
{
    return describe(DWORD(code), at, what);
}

Table &core()
{
    return the_table();
}

GameOps game_ops()
{
    GameOps ops;
    ops.log = [](const std::string &s) { swofty::log("%s", s.c_str()); };
    ops.resolve = [](const std::string &binding) { return swofty::api->resolve(binding.c_str()); };
    ops.overwritten_by = runtime::mixin_overwrite_on;
    ops.occupied_by = runtime::mixin_occupying;
    ops.conflict = [](const std::string &point, const std::string &by) {
        blackbox::note("hooks", (point + " unavailable: overwritten by ").c_str(), by.c_str());
        std::lock_guard<std::mutex> g(faults_lock);
        fault_list.push_back({point, "install", by, "a raw overwrite of the same function"});
    };
    return ops;
}

void set_records(RecordFn fn)
{
    std::lock_guard<std::mutex> g(records_lock);
    records_fn = std::move(fn);
}

std::vector<FaultRecord> faults()
{
    std::lock_guard<std::mutex> g(faults_lock);
    return fault_list;
}

void window_stage()
{
    Table &t = the_table();
    if (t.armed())
        return;
    t.arm();
    arm_session_points();
    for (auto &[point, subs] : t.undefined())
        swofty::log("[hooks] %s: handlers from %s are attached, and no addon has defined it", point.c_str(), subs.c_str());
    if (runtime::enabled("events", nullptr))
        fire_shell("game.boot");
}

void drop_module(void *module)
{
    size_t n = the_table().drop_module(reinterpret_cast<uint64_t>(module));
    if (n)
        swofty::log("[hooks] dropped %zu handler(s) from a module that unloaded or failed to init", n);
}

void bridge_legacy(const char *event, int64_t a, int64_t b, const void *data)
{
    const catalogue::Event *e = catalogue::by_legacy(event);
    if (!e || e->fired == catalogue::Fired::no || !runtime::enabled("events", nullptr))
        return;
    (void)b;
    std::string id = e->id;
    if (id == "career.loaded") {
        auto *s = static_cast<const SwoftyCareerState *>(data);
        update_facts([&](Facts &f) {
            f.mode = mode_of(a);
            if (!f.career)
                f.career.emplace();
            if (s)
                f.career->id = runtime::snapshots_career_id(std::string(s->key, strnlen(s->key, sizeof s->key)).c_str());
        });
    }
    if (id == "career.hub" && a != 1)
        return;
    fire_shell(e->id);
    if (id == "career.unloaded")
        update_facts([](Facts &f) {
            f.mode.clear();
            f.career.reset();
            f.weekend.reset();
            f.session.reset();
            f.player.reset();
            f.participants.clear();
        });
}

}

extern "C" __declspec(dllexport) const SwoftyHooks *SwoftyCoreHooks(void)
{
    return swofty_core::hooks::table();
}
