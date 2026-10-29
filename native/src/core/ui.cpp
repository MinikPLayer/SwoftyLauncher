#include <windows.h>

#include <safetyhook.hpp>
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <string>
#include <vector>

#include "json.hpp"
#include "perf.hpp"

namespace runtime {
std::string hook_error(const safetyhook::MidHook::Error &e);
void netplay_screen(const char *name);
void testagent_screen(const char *name);
}

bool ui_vm_init(uintptr_t module_base, const std::wstring &swofty_dir);
bool ui_vm_attach(const std::string &screen);
bool ui_vm_ready();

namespace {

struct NeStrHdr {
    uint32_t hash;
    uint16_t refs;
};

struct Callback {
    void (*fn)(void *ctx, int status);
    void *ctx;
};

enum class State { idle, armed, pending, in_table, dropped };

struct Screen {
    std::string name, file, flow, anchor;
    uint32_t hash = 0;
    std::string data;
    void *resid = nullptr, *failed_resid = nullptr, *noted_resid = nullptr;
    State state = State::idle;
    bool engine = false, started = false, result_seen = false, view_model = false, vm_attached = false,
         vm_noted = false;
    int result = 0;
    unsigned attempts = 0;
};

constexpr unsigned MAX_ATTEMPTS = 3;

struct Target {
    const char *what, *pattern;
    uintptr_t rva;
    uintptr_t anchor = 0;
};

const Target T_HANDLER{"Ui_Uidf_Handler", "41 57 41 56 41 55 41 54 56 57 55 53 48 83 EC 48 4C 89 CB 49 89 D6", 0x449e140};
const Target T_PARSER{"Ui_Uidf_LoadScreens",
                      "41 57 41 56 41 55 41 54 56 57 55 53 48 81 EC 48 01 00 00 C5 F8 29 B4 24 30 01 00 00 4C 89 CF 49 89 CD",
                      0x41d7a90};
const Target T_PKGOPEN{"Ui_Pkg_Open", "56 57 53 48 83 EC 40 48 85 C9 0F 84 ?? ?? ?? ?? 48 89 CF 8B 01", 0x3927510};
const Target T_RESOLVE{"Vm_RouteCmd_ResolveTargetScreen",
                       "41 57 41 56 41 55 41 54 56 57 55 53 48 81 EC 48 01 00 00 C5 F8 29 B4 24 30 01 00 00 49 89 D4 48 89 4C 24 68",
                       0x4187620};
const Target T_OPENEXEC{"Vm_RouteCmd_OpenScreen", "41 57 41 56 41 55 41 54 56 57 55 53 48 83 EC 58 4D 89 C7 48 89 D6",
                        0x3889310};
const Target T_SITE_READ{"site_Ui_Job_NodeRead", "E8 ?? ?? ?? ?? 89 C5 41 89 85 8C 00 00 00", 0x41dda07, 5};
const Target T_SITE_INIT{"site_Ui_Job_NodeInit", "4C 89 EA FF 50 48 89 C5 85 C0 0F 85", 0x41dda71, 6};
const Target T_SITE_FACTORY{"site_Ui_Job_NodeNoObject", "BD 05 00 00 00 41 89 AD 8C 00 00 00", 0x41ddc1c, 5};
const Target T_SITE_SCREEN{"site_Ui_Job_ScreenDone", "8B 43 50 FF C0 89 43 50 83 FD 55 0F 84", 0x41de0d7, 8};
const Target T_OPEN{"Vm_Open_Screen", "41 57 41 56 41 55 41 54 56 57 55 53 48 83 EC 48 48 89 D6 48 89 CF 80 3D ?? ?? ?? ?? 01",
                    0x4182660};
constexpr uintptr_t SCREEN_SYS_LOAD = 0x10c;

using Handler_t = void(void *self, int64_t *resid, void *a3, uint8_t *frags, int64_t count, uint8_t *res);
using Parser_t = int(void *mgr, void *pkg, void *resid, Callback *cb, int flag);
using PkgOpen_t = int(const void *data, void **out);
using RouteExec_t = NeStrHdr **(void *self, NeStrHdr **err, void *cmd);
using Open_t = uint64_t(void *scene, void *screen, void *a3, void *a4);
using Table_t = uint8_t *(void *self);


std::vector<Screen> screens;
bool strict = false, do_register = true, recover = true, diagnostics = false, perf = false;
SRWLOCK lock = SRWLOCK_INIT;
uintptr_t base, a_handler, a_parser, a_pkgopen, a_resolve, a_openexec, a_open;
void **screen_sys;
void *mgr;
void *last_scene, *last_screen, *prev_scene, *prev_screen;
safetyhook::InlineHook h_handler, h_resolve, h_openexec, h_open;
safetyhook::MidHook m_read, m_init, m_factory, m_screen;
thread_local char cur_screen[96];
unsigned engine_failures;
constexpr unsigned MAX_ENGINE_FAILURES = 50;

uint32_t djb2(const char *s)
{
    uint32_t h = 5381;
    while (*s)
        h = h * 33 + uint8_t(*s++);
    return h;
}

const char *chars(const void *s)
{
    return s ? static_cast<const char *>(s) + 9 : "";
}

std::wstring self_dir()
{
    HMODULE m = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&self_dir), &m);
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(m, path, MAX_PATH);
    std::wstring dir(path);
    return dir.substr(0, dir.find_last_of(L"\\/") + 1);
}

std::string read_file(const std::wstring &path)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return {};
    std::string out;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart < (64 << 20)) {
        out.resize(size_t(size.QuadPart));
        DWORD got = 0;
        if (!ReadFile(f, out.data(), DWORD(out.size()), &got, nullptr) || got != out.size())
            out.clear();
    }
    CloseHandle(f);
    return out;
}

std::wstring wide(const std::string &s)
{
    std::wstring w(s.size(), L'\0');
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), int(w.size()));
    w.resize(n > 0 ? size_t(n) : 0);
    return w;
}

uintptr_t find(const Target &t)
{
    uintptr_t at = 0;
    int hits = swofty::api->scan(".xtls", t.pattern, &at);
    if (hits != 1 || !at) {
        swofty::log("[ui] %s: signature matched %d time(s); the ui prototype stays off", t.what, hits);
        return 0;
    }
    if (at + t.anchor - base != t.rva)
        swofty::log("[ui] %s: matched at base+0x%llx, KB build has base+0x%llx", t.what, (unsigned long long)(at + t.anchor - base),
                    (unsigned long long)t.rva);
    return at;
}

bool readable(const void *p, size_t n)
{
    return swofty::mem::readable(p, n);
}

void *screen_lookup(uint32_t h, bool *ok)
{
    *ok = false;
    if (!screen_sys || !readable(screen_sys, 8) || !*screen_sys || !readable(*screen_sys, 8))
        return nullptr;
    void *sys = *screen_sys;
    auto fn = reinterpret_cast<Table_t *>((*static_cast<void ***>(sys))[0x48 / 8]);
    uint8_t *t = fn(sys);
    if (!t || !readable(t + 0x98, 0x38))
        return nullptr;
    uint64_t mul = *reinterpret_cast<uint64_t *>(t + 0x98), mask = *reinterpret_cast<uint64_t *>(t + 0xb8);
    auto *entries = *reinterpret_cast<uint8_t **>(t + 0xa0), *meta = *reinterpret_cast<uint8_t **>(t + 0xa8);
    uint32_t step = *reinterpret_cast<uint32_t *>(t + 0xc8);
    uint8_t shift = *(t + 0xcc);
    *ok = true;
    if (!mask || !entries || !meta)
        return nullptr;
    uint64_t u = uint64_t(h) * mul;
    u = (u >> 0x21) ^ u;
    uint32_t dist = (uint32_t(u) & 0x1f) >> (shift & 0x3f);
    uint32_t d0 = step + dist, d1 = dist + step * 2;
    uint64_t i = (u >> 5) & mask;
    for (unsigned guard = 0; guard < 4096; guard++) {
        uint8_t b0 = meta[i], b1 = meta[i + 1];
        if (d0 == b0 && *reinterpret_cast<uint32_t *>(entries + i * 16) == h)
            return *reinterpret_cast<void **>(entries + i * 16 + 8);
        if (d1 == b1 && *reinterpret_cast<uint32_t *>(entries + (i + 1) * 16) == h)
            return *reinterpret_cast<void **>(entries + (i + 1) * 16 + 8);
        d0 += step * 2;
        d1 += step * 2;
        uint8_t b2 = meta[i + 2];
        i += 2;
        if (d0 > b2)
            break;
    }
    return nullptr;
}

bool contains(const uint8_t *data, size_t size, const std::string &name)
{
    if (!data || !size || name.empty() || name.size() > 250 || !readable(data, size))
        return false;
    std::string needle;
    needle.push_back(char(name.size()));
    needle += name;
    needle.push_back('\0');
    return std::string_view(reinterpret_cast<const char *>(data), size).find(needle) != std::string_view::npos;
}

std::string fourcc_text(uint32_t v)
{
    std::string out;
    for (int i = 0; i < 4; i++) {
        char c = char((v >> (8 * i)) & 0xff);
        out.push_back(c >= 0x20 && c < 0x7f ? c : '?');
    }
    return out;
}

void on_parsed(void *ctx, int)
{
    auto *s = static_cast<Screen *>(ctx);
    bool ok = false;
    void *scr = screen_lookup(s->hash, &ok);
    s->state = scr ? State::in_table : State::dropped;
    if (!scr)
        s->failed_resid = s->resid;
    if (scr)
        swofty::log("[ui] screen %s (%s): build job finished, in screen table: yes", s->name.c_str(), s->flow.c_str());
    else if (!s->started)
        swofty::log("[ui] screen %s (%s): build job finished without reading its root node (the job never reached "
                    "its screen); in screen table: %s",
                    s->name.c_str(), s->flow.c_str(), ok ? "no" : "table unreadable");
    else
        swofty::log("[ui] screen %s (%s): build job finished, in screen table: %s, screen result %s0x%x",
                    s->name.c_str(), s->flow.c_str(), ok ? "no" : "table unreadable",
                    s->result_seen ? "" : "not reached, last node code ", s->result);
}

Screen *job_owner(uint8_t *job)
{
    if (!job || !readable(job + 0xa0, 16) ||
        *reinterpret_cast<void **>(job + 0xa0) != reinterpret_cast<void *>(&on_parsed))
        return nullptr;
    auto *ctx = *reinterpret_cast<Screen **>(job + 0xa8);
    for (auto &s : screens)
        if (&s == ctx)
            return ctx;
    return nullptr;
}

void node_text(uint8_t *rec, char *name, size_t n, uint32_t *fourcc, uint32_t *index)
{
    *fourcc = readable(rec + 0x70, 4) ? *reinterpret_cast<uint32_t *>(rec + 0x70) : 0;
    *index = readable(rec + 0x88, 4) ? *reinterpret_cast<uint32_t *>(rec + 0x88) : ~0u;
    const char *p = readable(rec + 0x68, 8) ? *reinterpret_cast<const char **>(rec + 0x68) : nullptr;
    snprintf(name, n, "%s", p && readable(p, 1) ? p : "?");
}

bool list_base(uint32_t cls)
{
    switch (cls) {
    case 0x47544955:
    case 0x42544955:
    case 0x544c4955:
    case 0x324c4955:
    case 0x4c454955:
        return true;
    }
    return false;
}

const char *init_reason(uint32_t cls, int code)
{
    if (list_base(cls) && code == 3)
        return "its 'data' property has no dbentry binding: this class needs data=@<array path> (every shipping "
               "UITG also binds selection=@<value path>); a literal or empty 'data' value fails. Code 3 can also come "
               "from the element base: the node's layout attach (slot 4) lacks componentId or nodeId";
    if (list_base(cls) && code == 5)
        return "its 'data' property is missing, is not a {value, dbentry} object, or has an empty dbentry";
    if (code == 3)
        return "the element base rejected it: the node's layout attach (slot 4) lacks componentId or nodeId, or a "
               "required property object lacks its value/dbentry key";
    if (cls == 0x4443564d)
        return "MVCD init rejected its data (its init body is Denuvo-relocated and not mapped; compare its properties "
               "with a shipping MVCD)";
    return "was rejected by its class (init from data returned non-zero)";
}

void node_failure(uint8_t *job, uint8_t *rec, int code, const char *what)
{
    Screen *s = job_owner(job);
    if (!s && !strict && ++engine_failures > MAX_ENGINE_FAILURES)
        return;
    char name[96];
    uint32_t cls, idx;
    node_text(rec, name, sizeof name, &cls, &idx);
    if (s)
        s->result = code;
    swofty::log("[ui] %s screen '%s'%s%s: node '%s' (index %u, class %s) %s, code %d (0x%x)", s ? "addon" : "engine",
                cur_screen, s ? " from " : "", s ? s->file.c_str() : "", name, idx, fourcc_text(cls).c_str(), what,
                code, code);
}

void read_site(safetyhook::Context &c)
{
    auto *job = reinterpret_cast<uint8_t *>(c.rbx), *rec = reinterpret_cast<uint8_t *>(c.r13);
    int rc = int(c.rax);
    uint32_t cls = *reinterpret_cast<const volatile uint32_t *>(rec + 0x70);
    if (!rc && cls != 0x43534955)
        return;
    Screen *s = job_owner(job);
    if (cls == 0x43534955) {
        const char *p = readable(rec + 0x68, 8) ? *reinterpret_cast<const char **>(rec + 0x68) : nullptr;
        snprintf(cur_screen, sizeof cur_screen, "%s", p && readable(p, 1) ? p : "?");
        if (s && !s->started) {
            s->started = true;
            swofty::log("[ui] screen %s (%s): build job started, root node '%s'", s->name.c_str(), s->flow.c_str(),
                        cur_screen);
        }
    }
    if (rc == 0x55 && s)
        swofty::log("[ui] addon screen %s: a screen named '%s' is already registered with an equal or higher priority; "
                    "the engine skips this one",
                    s->name.c_str(), cur_screen);
    else if (rc && rc != 0x55)
        node_failure(job, rec, rc, "could not be read from the .ui data");
}

void init_site(safetyhook::Context &c)
{
    int rc = int(c.rax);
    if (!rc || rc == 0x23 || rc == 0x57)
        return;
    auto *rec = reinterpret_cast<uint8_t *>(c.r13);
    uint32_t cls = readable(rec + 0x70, 4) ? *reinterpret_cast<uint32_t *>(rec + 0x70) : 0;
    node_failure(reinterpret_cast<uint8_t *>(c.rbx), rec, rc, init_reason(cls, rc));
}

void factory_site(safetyhook::Context &c)
{
    node_failure(reinterpret_cast<uint8_t *>(c.rbx), reinterpret_cast<uint8_t *>(c.r13), 5,
                 "has no registered class factory, or its factory returned no object");
}

void screen_site(safetyhook::Context &c)
{
    static const uint32_t probe = perf::probe("core", "ui screen built");
    perf::Scope p(probe);
    auto *job = reinterpret_cast<uint8_t *>(c.rbx);
    int rc = int(c.rbp);
    Screen *s = job_owner(job);
    if (!m_read)
        snprintf(cur_screen, sizeof cur_screen, "%s", s ? s->name.c_str() : "?");
    if (s) {
        s->result = rc;
        s->result_seen = true;
        swofty::log("[ui] screen %s (%s): root '%s' built with result 0x%x (%s)", s->name.c_str(), s->flow.c_str(),
                    cur_screen, rc,
                    rc == 0      ? "ok, the engine inserts it into the screen table next"
                    : rc == 0x55 ? "skipped: a same-name screen is already registered"
                                 : "failed: the engine drops it and ends this build job");
    } else if (rc && rc != 0x55 && (strict || ++engine_failures <= MAX_ENGINE_FAILURES)) {
        swofty::log("[ui] engine screen '%s' failed to build, result 0x%x; the engine drops it", cur_screen, rc);
    }
}

void register_pending(const char *why)
{
    if (!mgr || !do_register)
        return;
    for (auto &s : screens) {
        if (s.state != State::armed)
            continue;
        if (s.view_model && !s.vm_attached)
            s.vm_attached = ui_vm_attach(s.name);
        if (s.view_model && !s.vm_attached) {
            s.state = State::idle;
            if (!s.vm_noted)
                swofty::log("[ui] screen %s (%s) needs a view model and %s; not registered, so routes to it are "
                            "cancelled and the front end stays where it is",
                            s.name.c_str(), s.flow.c_str(),
                            ui_vm_ready() ? "its view model is not attached yet" : "view models are off on this build");
            s.vm_noted = true;
            continue;
        }
        bool ok = false;
        if (screen_lookup(s.hash, &ok)) {
            s.state = State::in_table;
            s.engine = !s.attempts;
            swofty::log("[ui] screen %s (%s) is already in the screen table%s; nothing to do", s.name.c_str(),
                        s.flow.c_str(), s.engine ? " (registered by the engine)" : "");
            continue;
        }
        s.state = State::idle;
        if (s.data.empty()) {
            swofty::log("[ui] screen %s (%s): no data (%s missing or empty); not registered", s.name.c_str(),
                        s.flow.c_str(), s.file.c_str());
            continue;
        }
        void *pkg = nullptr;
        int rc = reinterpret_cast<PkgOpen_t *>(a_pkgopen)(s.data.data(), &pkg);
        if (rc || !pkg) {
            swofty::log("[ui] screen %s (%s): %s is not a valid !pkg (code %d); not registered", s.name.c_str(),
                        s.flow.c_str(), s.file.c_str(), rc);
            continue;
        }
        Callback cb{on_parsed, &s};
        s.attempts++;
        s.state = State::pending;
        rc = reinterpret_cast<Parser_t *>(a_parser)(mgr, pkg, s.resid, &cb, 0);
        if (rc && rc != 0x23) {
            s.state = State::dropped;
            s.failed_resid = s.resid;
            swofty::log("[ui] screen %s (%s): engine screen loader refused it (code %d)", s.name.c_str(), s.flow.c_str(),
                        rc);
            continue;
        }
        s.started = s.result_seen = false;
        s.result = 0;
        auto *jb = *reinterpret_cast<uint8_t **>(static_cast<uint8_t *>(mgr) + 0xb900);
        auto *je = *reinterpret_cast<uint8_t **>(static_cast<uint8_t *>(mgr) + 0xb908);
        size_t jobs = jb && je > jb ? size_t(je - jb) / 0xd0 : 0;
        swofty::log("[ui] registered screen %s (%s) via the engine screen loader (parse returned %d = build job queued, "
                    "position %zu of %zu), attempt %u, %zu bytes, resource %p, trigger %s",
                    s.name.c_str(), s.flow.c_str(), rc, jobs, jobs, s.attempts, s.data.size(), s.resid, why);
    }
}

void handler_hook(void *self, int64_t *resid, void *a3, uint8_t *frags, int64_t count, uint8_t *res)
{
    static const uint32_t probe = perf::probe("core", "ui screen loader handler");
    h_handler.call<void>(self, resid, a3, frags, count, res);
    if (!self)
        return;
    perf::Scope p(probe);
    AcquireSRWLockExclusive(&lock);
    if (!mgr) {
        mgr = *reinterpret_cast<void **>(static_cast<uint8_t *>(self) + 0x10);
        swofty::log("[ui] screen loader manager %p (handler %p)", mgr, self);
    }
    void *copy = res && readable(res + 0x38, 8) ? *reinterpret_cast<uint8_t **>(res + 0x38) : nullptr;
    for (int64_t k = 0; k < count && k < 16; k++) {
        uint8_t *e = frags + k * 32;
        if (!readable(e, 32) || *reinterpret_cast<uint32_t *>(e + 0x10) != 0x706d6574)
            continue;
        auto *data = *reinterpret_cast<uint8_t **>(e);
        size_t size = *reinterpret_cast<uint64_t *>(e + 8);
        std::vector<std::pair<const std::string *, bool>> seen;
        auto has_anchor = [&](const std::string &anchor) {
            for (auto &[a, hit] : seen)
                if (*a == anchor)
                    return hit;
            bool hit = contains(data, size, anchor);
            seen.push_back({&anchor, hit});
            return hit;
        };
        for (auto &s : screens) {
            if (!copy || !has_anchor(s.anchor))
                continue;
            void *rid = static_cast<uint8_t *>(copy) + 8;
            bool ok = false;
            switch (s.state) {
            case State::armed:
            case State::pending:
                break;
            case State::in_table:
                if (screen_lookup(s.hash, &ok) || !ok) {
                    if (s.noted_resid != rid)
                        swofty::log("[ui] screen %s: anchor '%s' resource %p loaded; already in the screen table, "
                                    "not registering again",
                                    s.name.c_str(), s.anchor.c_str(), rid);
                    s.noted_resid = rid;
                    break;
                }
                [[fallthrough]];
            case State::idle:
                s.resid = rid;
                s.state = State::armed;
                swofty::log("[ui] screen %s: anchor '%s' (%s) loaded, resource %p; registering at the next screen open",
                            s.name.c_str(), s.anchor.c_str(), s.flow.c_str(), rid);
                break;
            case State::dropped:
                if (rid == s.failed_resid || s.attempts >= MAX_ATTEMPTS) {
                    if (s.noted_resid != rid)
                        swofty::log("[ui] screen %s: anchor resource %p loaded; not retrying (dropped %u time(s); a "
                                    "retry needs a fresh flow load and fewer than %u attempts)",
                                    s.name.c_str(), rid, s.attempts, MAX_ATTEMPTS);
                    s.noted_resid = rid;
                    break;
                }
                s.resid = rid;
                s.state = State::armed;
                swofty::log("[ui] screen %s: flow reloaded (resource %p); retrying after %u failed attempt(s)",
                            s.name.c_str(), rid, s.attempts);
                break;
            }
        }
    }
    ReleaseSRWLockExclusive(&lock);
}

double perf_ms(const LARGE_INTEGER &a, const LARGE_INTEGER &b)
{
    static LARGE_INTEGER f = [] {
        LARGE_INTEGER q;
        QueryPerformanceFrequency(&q);
        return q;
    }();
    return double(b.QuadPart - a.QuadPart) * 1000.0 / double(f.QuadPart);
}

void perf_probe(const char *what, const void *p)
{
    MEMORY_BASIC_INFORMATION m{};
    if (!p || !VirtualQuery(p, &m, sizeof m))
        return;
    LARGE_INTEGER a, b;
    QueryPerformanceCounter(&a);
    for (int i = 0; i < 100; i++)
        VirtualQuery(p, &m, sizeof m);
    QueryPerformanceCounter(&b);
    swofty::log("[perf] VirtualQuery on %s %p: allocation base %p, region %p + %llu MB, %.2f us per call", what, p,
                m.AllocationBase, m.BaseAddress, (unsigned long long)(m.RegionSize >> 20), perf_ms(a, b) * 10.0);
}

uint64_t open_hook(void *scene, void *screen, void *a3, void *a4)
{
    static const uint32_t probe = perf::probe("core", "ui screen open");
    perf::Scope p(probe);
    if (perf::live() && readable(screen, 0x70) &&
        readable(*reinterpret_cast<void **>(static_cast<uint8_t *>(screen) + 0x68), 10))
        perf::mark("screen", chars(*reinterpret_cast<void **>(static_cast<uint8_t *>(screen) + 0x68)));
    LARGE_INTEGER t0, t1, t2;
    QueryPerformanceCounter(&t0);
    AcquireSRWLockExclusive(&lock);
    register_pending("screen open");
    ReleaseSRWLockExclusive(&lock);
    QueryPerformanceCounter(&t1);
    uint64_t r;
    {
        perf::Pause game;
        r = h_open.call<uint64_t>(scene, screen, a3, a4);
    }
    QueryPerformanceCounter(&t2);
    double total = perf_ms(t0, t2);
    if (perf || total >= 50.0) {
        const char *name = readable(screen, 0x70) && readable(*reinterpret_cast<void **>(static_cast<uint8_t *>(screen) + 0x68), 10)
                               ? chars(*reinterpret_cast<void **>(static_cast<uint8_t *>(screen) + 0x68))
                               : "?";
        swofty::log("[perf] screen open '%.64s': %.2f ms (addon screen registration %.2f ms, engine open %.2f ms)", name,
                    total, perf_ms(t0, t1), perf_ms(t1, t2));
        static bool probed;
        if (perf && !probed) {
            probed = true;
            perf_probe("the screen object", screen);
            perf_probe("the scene object", scene);
        }
    }
    if (readable(screen, 0x70) && readable(*reinterpret_cast<void **>(static_cast<uint8_t *>(screen) + 0x68), 10)) {
        runtime::netplay_screen(chars(*reinterpret_cast<void **>(static_cast<uint8_t *>(screen) + 0x68)));
        runtime::testagent_screen(chars(*reinterpret_cast<void **>(static_cast<uint8_t *>(screen) + 0x68)));
    }
    if (screen != last_screen) {
        prev_scene = last_scene;
        prev_screen = last_screen;
        last_scene = scene;
        last_screen = screen;
    }
    return r;
}

bool is_open(void *scene, void *screen)
{
    auto *sc = static_cast<uint8_t *>(scene);
    if (!readable(sc + 0x130, 0x10))
        return true;
    uint8_t *n = *reinterpret_cast<uint8_t **>(sc + 0x138);
    for (unsigned guard = 0; n && n != sc + 0x130 && guard < 256; guard++) {
        if (!readable(n, 0x18))
            return true;
        if (*reinterpret_cast<void **>(n + 0x10) == screen)
            return true;
        n = *reinterpret_cast<uint8_t **>(n + 8);
    }
    return false;
}

bool guarded(const std::string &name)
{
    if (strict)
        return true;
    if (name.find("swofty") != std::string::npos)
        return true;
    for (auto &s : screens)
        if (s.name == name)
            return true;
    return false;
}

NeStrHdr **cancel_missing(NeStrHdr **err, const char *step)
{
    if (!err || !*err || !readable(*err, 16))
        return err;
    std::string text = chars(*err);
    const char *p = "No screen called ";
    if (text.rfind(p, 0) != 0)
        return err;
    std::string name = text.substr(strlen(p));
    name = name.substr(0, name.find(' '));
    if (!guarded(name)) {
        swofty::log("[ui] route step %s -> screen '%s' is not registered; not an addon screen, the engine handles it "
                    "(strict mode off)",
                    step, name.c_str());
        return err;
    }
    *err = nullptr;
    swofty::log("[ui] route -> screen '%s' is not registered; route cancelled (%s), front end kept", name.c_str(), step);
    AcquireSRWLockExclusive(&lock);
    for (auto &s : screens)
        if (s.name == name && s.state == State::idle && s.resid)
            s.state = State::armed;
    ReleaseSRWLockExclusive(&lock);
    if (recover && h_open && last_screen && last_scene) {
        bool ok = false;
        const char *cur = readable(last_screen, 0x70) ? chars(*reinterpret_cast<void **>(
                                                            static_cast<uint8_t *>(last_screen) + 0x68))
                                                      : "?";
        void *scr = screen_lookup(djb2(cur), &ok);
        if (scr == last_screen && is_open(last_scene, last_screen))
            swofty::log("[ui] last screen '%s' is still open; staying on it", cur);
        else if (scr == last_screen) {
            swofty::log("[ui] re-opening the last screen '%s' after the cancelled route", cur);
            h_open.call<uint64_t>(last_scene, last_screen, nullptr, nullptr);
        }
    }
    return err;
}

NeStrHdr **resolve_hook(void *self, NeStrHdr **err, void *cmd)
{
    static const uint32_t probe = perf::probe("core", "ui route guard");
    NeStrHdr **r = h_resolve.call<NeStrHdr **>(self, err, cmd);
    perf::Scope p(probe);
    return cancel_missing(r, "resolve-target");
}

NeStrHdr **openexec_hook(void *self, NeStrHdr **err, void *cmd)
{
    static const uint32_t probe = perf::probe("core", "ui route guard");
    NeStrHdr **r = h_openexec.call<NeStrHdr **>(self, err, cmd);
    perf::Scope p(probe);
    return cancel_missing(r, "open-screen");
}

bool env_on(const char *name)
{
    char v[16] = {};
    DWORD n = GetEnvironmentVariableA(name, v, sizeof v);
    return n && n < sizeof v && v[0] && v[0] != '0';
}

void load_config()
{
    diagnostics = env_on("SWOFTY_UI_DIAG");
    perf = env_on("SWOFTY_PERF");
    std::wstring root = self_dir() + L"..\\..\\";
    json::Value doc;
    std::wstring src = L"settings.json";
    std::string text = read_file(root + src);
    if (text.empty() || !json::parse(text, doc) || doc["ui"].type != json::Value::Obj) {
        doc = json::Value{};
        src = L"ui.json";
        text = read_file(root + src);
        if (text.empty() || !json::parse(text, doc) || doc["ui"].type != json::Value::Obj) {
            swofty::log("[ui] no \"ui\" block in settings.json or ui.json; no addon screens declared (route guard and "
                        "build diagnostics stay on)");
            return;
        }
    }
    const json::Value &ui = doc["ui"];
    diagnostics = diagnostics || (ui["diagnostics"].type == json::Value::Bool && ui["diagnostics"].b);
    strict = ui["strict"].type == json::Value::Bool && ui["strict"].b;
    do_register = !(ui["register"].type == json::Value::Bool && !ui["register"].b);
    recover = !(ui["recover"].type == json::Value::Bool && !ui["recover"].b);
    for (auto &j : ui["screens"].a) {
        Screen s;
        s.name = j["name"].str();
        if (s.name.empty())
            continue;
        s.flow = j["flow"].str("flow_frontend");
        s.anchor = j["anchor"].str(s.flow == "flow_options" ? "settings_container" : "fe_home");
        s.file = j["file"].str(("ui/" + s.name + ".ui").c_str());
        s.view_model = j["view_model"].type == json::Value::Bool && j["view_model"].b;
        s.hash = djb2(s.name.c_str());
        s.data = read_file(root + wide(s.file));
        if (s.data.size() < 16 || s.data.compare(0, 4, "!pkg"))
            swofty::log("[ui] screen %s: %s is missing or not a !pkg file (%zu bytes)", s.name.c_str(), s.file.c_str(),
                        s.data.size());
        screens.push_back(std::move(s));
    }
    swofty::log("[ui] %ls: %zu addon screen(s), register %s, strict %s, recover %s", src.c_str(), screens.size(),
                do_register ? "on" : "off", strict ? "on" : "off", recover ? "on" : "off");
    for (auto &s : screens)
        swofty::log("[ui]   %s (%s, anchor %s, djb2 0x%08x, %zu bytes from %s%s)", s.name.c_str(), s.flow.c_str(),
                    s.anchor.c_str(), s.hash, s.data.size(), s.file.c_str(), s.view_model ? ", view model" : "");
}

template <class F> bool install(safetyhook::InlineHook &h, uintptr_t at, F *fn, const char *what)
{
    auto r = safetyhook::InlineHook::create(reinterpret_cast<void *>(at), reinterpret_cast<void *>(fn));
    if (!r) {
        swofty::log("[ui] %s: hook at 0x%llx failed", what, (unsigned long long)at);
        return false;
    }
    h = std::move(*r);
    swofty::log("[ui] %s: hooked at base+0x%llx", what, (unsigned long long)(at - base));
    return true;
}

void mid(safetyhook::MidHook &h, const Target &t, void (*fn)(safetyhook::Context &), const char *what)
{
    uintptr_t at = find(t);
    if (!at)
        return;
    auto r = safetyhook::MidHook::create(reinterpret_cast<void *>(at + t.anchor), fn);
    if (!r) {
        swofty::log("[ui] %s: mid hook at base+0x%llx failed: %s", what, (unsigned long long)(at + t.anchor - base),
                    runtime::hook_error(r.error()).c_str());
        return;
    }
    h = std::move(*r);
    swofty::log("[ui] %s: mid hook at base+0x%llx", what, (unsigned long long)(at + t.anchor - base));
}

}

namespace runtime {
void when_code_ready(const char *what, void (*fn)());
}

namespace {

void ui_load()
{
    base = swofty::api->module_base();
    load_config();
    bool vms = false;
    for (auto &s : screens)
        vms |= s.view_model;
    if (vms)
        ui_vm_init(base, self_dir() + L"..\\..\\");
    a_handler = find(T_HANDLER);
    a_parser = find(T_PARSER);
    a_pkgopen = find(T_PKGOPEN);
    a_resolve = find(T_RESOLVE);
    a_openexec = find(T_OPENEXEC);
    a_open = find(T_OPEN);
    if (a_resolve) {
        auto *insn = reinterpret_cast<const uint8_t *>(a_resolve + SCREEN_SYS_LOAD);
        if (insn[0] == 0x48 && insn[1] == 0x8b && insn[2] == 0x0d)
            screen_sys = reinterpret_cast<void **>(a_resolve + SCREEN_SYS_LOAD + 7 + *reinterpret_cast<const int32_t *>(insn + 3));
        swofty::log("[ui] screen system global %p", static_cast<void *>(screen_sys));
    }
    if (a_resolve && screen_sys)
        install(h_resolve, a_resolve, resolve_hook, "route guard (resolve-target step)");
    if (a_openexec && screen_sys)
        install(h_openexec, a_openexec, openexec_hook, "route guard (open-screen step)");
    if (a_open)
        install(h_open, a_open, open_hook, "screen open");
    if (diagnostics) {
        mid(m_read, T_SITE_READ, read_site, "screen build diagnostics (node read)");
        mid(m_init, T_SITE_INIT, init_site, "screen build diagnostics (node init)");
        mid(m_factory, T_SITE_FACTORY, factory_site, "screen build diagnostics (node class)");
    } else {
        swofty::log("[ui] per-node screen build diagnostics off (set SWOFTY_UI_DIAG=1 or \"diagnostics\": true in the "
                    "\"ui\" block to turn them on); the screen result hook stays on");
    }
    mid(m_screen, T_SITE_SCREEN, screen_site, "screen build diagnostics (screen result)");
    if (screens.empty() || !do_register)
        return;
    if (a_handler && a_parser && a_pkgopen && screen_sys && h_open)
        install(h_handler, a_handler, handler_hook, "screen loader (UIDF handler)");
    else
        swofty::log("[ui] addon screen registration off: a loader target did not resolve");
}

}

SWOFTY_ON_LOAD
{
    runtime::when_code_ready("ui: screen hooks", ui_load);
}
