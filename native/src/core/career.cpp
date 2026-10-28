#include "runtime.hpp"

#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>

#include "career_state.hpp"
#include "dbread_logic.hpp"
#include "layout.hpp"
#include "perf.hpp"

// Flow states (site_FeFlow_SetStateStatus, FlowState). Every flow state's status change goes
// through FeFlow_Set_StateStatus(state, status); the site sits after its prologue with rcx = the
// state and edx = the new status, while FlowState.status still holds the old one. A state is keyed
// by FlowState.name_hash (djb2 of its name); the name buffer is k_Gdb_StringXorKey-encoded until
// the game decodes it in place, so the core decodes a copy itself. Read only.
//
// Team HQ answer (site_Career_TeamHqIsMyTeamResult). StateTeamHQ's onStateEnter asks
// Career_Is_MyTeamGameMode which Team HQ to open; the site reads that answer (al) right after the
// call. It answers in both career modes. Read only.
//
// Career (g_Game_Manager, GameManager, GameModeContext, ChampionshipHolder, CareerEntity,
// EntrantRecord, k_Career_<Class>Desc). The championship entity is
// manager->mode_context->championship_holder->entity; its component registry is embedded at
// CareerEntity.component_registry and answers get(registry, &desc, search_parents) at vtable slot
// +0x10, the call the game's own components make. A CareerComponent means a loaded career;
// CareerBank or MyTeamFacilities means My Team, DriverCareerPlayerDriverStatistics Driver Career.
// The human entrant (EntrantRecord.ordinal_flags bit 0x10000000) gives the career key.
//
// Bus (g_Game_MessageBus, GameMessageBusHolder, NeMessageBus, Ne_Post_BusMessage). post() hands a
// caller-built message to the game's synchronous publish while the bus is active.
//
// The two sites are core mixins of their own (manager.cpp), so a build without them loses only
// this library; every other binding resolves lazily and each lookup that fails reports
// unavailable or null for that call alone.

namespace {

namespace g = swofty::game;
namespace layout = runtime::layout;
namespace mem = swofty::mem;
using runtime::career::PollSchedule;
using runtime::career::Tracker;

constexpr const char *FLOW_MIXIN = "swofty_core.dll (flow states)";
constexpr const char *TEAM_HQ_MIXIN = "swofty_core.dll (Team HQ answer)";
constexpr uintptr_t REGISTRY_GET_SLOT = 0x10;
constexpr uint32_t HUMAN_FLAG = 0x10000000u;
constexpr size_t MAX_ENTRANTS = 128;
// How long the poll trusts a big heap region's VirtualQuery answer: the poll
// runs every POLL_MS after a transition, and each fresh answer for the game's
// heap costs milliseconds on the game thread.
constexpr uint32_t POLL_TRUST_MS = 4000;
constexpr const char *STATUS[4] = {"None", "Entering", "Updating", "Exiting"};

using RegistryGet = void *(void *registry, const void *desc, uint32_t search_parents);
using Post = uint64_t(void *bus, void *msg);

std::mutex lock;
Tracker tracker;
std::atomic<bool> armed, set_up;
bool flow_live, team_hq_live;
std::atomic<uint64_t> last_poll;
PollSchedule schedule;
std::atomic<bool> in_session;
std::mutex names_lock;
std::unordered_map<uint32_t, std::string> names;

std::mutex bind_lock;
std::unordered_map<std::string, uintptr_t> bound;

std::mutex perf_lock;
std::unordered_map<uint32_t, LARGE_INTEGER> entering_at;

bool perf_on()
{
    static const bool on = [] {
        char v[8] = {};
        DWORD n = GetEnvironmentVariableA("SWOFTY_PERF", v, sizeof v);
        return n && n < sizeof v && v[0] && v[0] != '0';
    }();
    return on;
}

void perf_flow(uint32_t hash, uint32_t from, uint32_t to, const std::string &name)
{
    LARGE_INTEGER now, f;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&f);
    std::lock_guard<std::mutex> g(perf_lock);
    if (to == 1) {
        entering_at[hash] = now;
        return;
    }
    auto it = entering_at.find(hash);
    if (from != 1 || it == entering_at.end())
        return;
    double ms = double(now.QuadPart - it->second.QuadPart) * 1000.0 / double(f.QuadPart);
    entering_at.erase(it);
    swofty::log("[perf] flow \"%s\" Entering -> %s took %.1f ms", name.c_str(), STATUS[to & 3], ms);
}

uint64_t now_ms()
{
    return GetTickCount64();
}

uintptr_t binding(const std::string &id)
{
    std::lock_guard<std::mutex> g(bind_lock);
    auto it = bound.find(id);
    if (it != bound.end())
        return it->second;
    uintptr_t a = swofty::api->resolve(id.c_str());
    if (a || runtime::code_ready())
        bound[id] = a;
    return a;
}

void fire(const char *event, int64_t a, int64_t b, const void *data)
{
    runtime::events_fire(event, a, b, data);
}

const char *mode_name(uint32_t m)
{
    return m == SWOFTY_CAREER_MY_TEAM ? "my_team" : m == SWOFTY_CAREER_DRIVER ? "driver_career" : "none";
}

void publish(const runtime::career::Edges &e, const SwoftyCareerState &s)
{
    if (e.hub == 0) {
        perf::mark("career", "hub left");
        swofty::log("[career] career.hub left (mode %s)", mode_name(e.unloaded ? e.old_mode : s.mode));
        fire(SWOFTY_EVENT_CAREER_HUB, 0, e.unloaded ? e.old_mode : s.mode, &s);
    }
    if (e.unloaded) {
        perf::mark("career", "unloaded");
        swofty::log("[career] career.unloaded (mode %s)", mode_name(e.old_mode));
        fire(SWOFTY_EVENT_CAREER_UNLOADED, e.old_mode, 0, &s);
    }
    if (e.loaded) {
        perf::mark("career", "loaded");
        swofty::log("[career] career.loaded: mode %s, key '%s' (components %s, component mode %s, Team HQ answer %s)",
                    mode_name(s.mode), s.key,
                    s.components == SWOFTY_CAREER_COMPONENTS_LIVE       ? "live"
                    : s.components == SWOFTY_CAREER_COMPONENTS_FALLBACK ? "fallback to flow"
                                                                        : "unseen",
                    mode_name(s.component_mode), mode_name(s.team_hq_mode));
        fire(SWOFTY_EVENT_CAREER_LOADED, s.mode, 0, &s);
    }
    if (e.hub == 1) {
        perf::mark("career", "hub entered");
        swofty::log("[career] career.hub entered (mode %s)", mode_name(s.mode));
        fire(SWOFTY_EVENT_CAREER_HUB, 1, s.mode, &s);
    }
}

void settle_and_publish()
{
    runtime::career::Edges e;
    SwoftyCareerState s;
    {
        std::lock_guard<std::mutex> g(lock);
        e = tracker.settle();
        s = tracker.state();
    }
    if (e.loaded || e.unloaded || e.hub >= 0)
        publish(e, s);
}

template <class S = g::FlowState>
bool read_state(uintptr_t at, uint32_t &hash, int32_t &status, uint64_t &size, uintptr_t &name)
{
    if constexpr (layout::flow_state<S>) {
        return mem::read(at + offsetof(S, name_hash), hash) && mem::read(at + offsetof(S, status), status) &&
               mem::read(at + offsetof(S, name_size), size) && mem::read(at + offsetof(S, name), name);
    } else {
        (void)at, (void)hash, (void)status, (void)size, (void)name;
        return false;
    }
}

bool state_name(uint32_t hash, uint64_t size, uintptr_t chars, std::string &out)
{
    if (size < 2 || size > 128 || !mem::readable(chars, size_t(size)))
        return false;
    out.assign(reinterpret_cast<const char *>(chars), size_t(size - 1));
    if (out.find('\0') == std::string::npos && mem::printable_utf8(out) &&
        runtime::career::djb2(out.c_str()) == hash)
        return true;
    uintptr_t key = binding("k_Gdb_StringXorKey");
    if (key && mem::readable(key, 4) &&
        dbread::decode_text(reinterpret_cast<const uint8_t *>(chars), size, reinterpret_cast<const uint8_t *>(key), out) &&
        runtime::career::djb2(out.c_str()) == hash)
        return true;
    out.clear();
    return false;
}

void on_flow(SwoftyContext *c)
{
    uint32_t hash = 0, to = uint32_t(c->rdx);
    int32_t from = 0;
    uint64_t size = 0;
    uintptr_t chars = 0;
    if (to > 3 || !read_state(c->rcx, hash, from, size, chars) || from < 0 || from > 3)
        return;
    const char *known = tracker.known_name(hash);
    std::string name;
    bool first = false;
    if (known) {
        name = known;
    } else {
        std::lock_guard<std::mutex> g(names_lock);
        auto it = names.find(hash);
        if (it != names.end()) {
            name = it->second;
        } else {
            first = true;
            if (!state_name(hash, size, chars, name)) {
                char b[16];
                snprintf(b, sizeof b, "#%08x", hash);
                name = b;
            }
            if (names.size() < runtime::career::MAX_STATES)
                names[hash] = name;
        }
    }
    bool named = name[0] != '#';
    SwoftyFlowState f;
    {
        std::lock_guard<std::mutex> g(lock);
        uint64_t now = now_ms();
        tracker.flow(hash, uint32_t(from), to, name.c_str(), named, now);
        f = *tracker.last();
        if (tracker.settles(hash))
            schedule.transition(now);
        in_session = tracker.session();
    }
    if (known || first)
        swofty::log("[career] flow \"%s\" %s -> %s%s", name.c_str(), STATUS[from & 3], STATUS[to & 3],
                    known ? "" : " (first sighting)");
    if (to == 1 || to == 3)
        perf::mark("flow", name + " " + STATUS[to]);
    if (perf_on())
        perf_flow(hash, uint32_t(from), to, name);
    runtime::netplay_flow(hash, to);
    fire(SWOFTY_EVENT_FLOW_STATE, to, from, &f);
    settle_and_publish();
}

void on_team_hq(SwoftyContext *c)
{
    bool my_team = (c->rax & 0xff) != 0;
    uint32_t was;
    {
        std::lock_guard<std::mutex> g(lock);
        was = tracker.state().team_hq_mode;
        tracker.team_hq_answer(my_team);
    }
    uint32_t now = my_team ? SWOFTY_CAREER_MY_TEAM : SWOFTY_CAREER_DRIVER;
    if (was != now)
        swofty::log("[career] Team HQ entered: the game reports %s (Career_Is_MyTeamGameMode)",
                    my_team ? "My Team" : "not My Team");
    settle_and_publish();
}

template <class M = g::GameManager, class C = g::GameModeContext, class H = g::ChampionshipHolder,
          class E = g::CareerEntity>
uintptr_t championship_entity()
{
    if constexpr (layout::career_chain<M, C, H, E>) {
        uintptr_t slot = binding("g_Game_Manager"), mgr = 0, ctx = 0, holder = 0, e = 0;
        if (!slot || !mem::read(slot, mgr) || !mgr || !mem::read(mgr + offsetof(M, mode_context), ctx) || !ctx ||
            !mem::read(ctx + offsetof(C, championship_holder), holder) || !holder ||
            !mem::read(holder + offsetof(H, entity), e) || !e)
            return 0;
        return mem::object_like(e) && mem::object_like(e + offsetof(E, component_registry)) ? e : 0;
    } else {
        return 0;
    }
}

template <class E = g::CareerEntity> uintptr_t registry_get(uintptr_t e, uintptr_t desc)
{
    if constexpr (layout::CAREER) {
        uintptr_t reg = e + offsetof(E, component_registry), vt = 0, fn = 0;
        if (!e || !desc || !mem::read(reg, vt) || !mem::in_image(vt) || !mem::read(vt + REGISTRY_GET_SLOT, fn) ||
            !mem::in_image(fn))
            return 0;
        static const uint32_t probe = perf::probe("core", "career RegistryGet (game call)");
        perf::Scope p(probe);
        auto c = reinterpret_cast<uintptr_t>(
            reinterpret_cast<RegistryGet *>(fn)(reinterpret_cast<void *>(reg), reinterpret_cast<const void *>(desc), 1));
        return mem::object_like(c) ? c : 0;
    } else {
        (void)e, (void)desc;
        return 0;
    }
}

std::string desc_id(const char *type)
{
    std::string t = type ? type : "";
    if (t.empty() || t.rfind("k_", 0) == 0)
        return t;
    return "k_Career_" + t + "Desc";
}

uintptr_t component_of(uintptr_t e, const char *type)
{
    std::string id = desc_id(type);
    return id.empty() ? 0 : registry_get(e, binding(id));
}

template <class E = g::CareerEntity, class R = g::EntrantRecord>
void entrants(uintptr_t e, runtime::career::Components &out)
{
    if constexpr (layout::CAREER && layout::ENTRANT) {
        uintptr_t b = 0, en = 0;
        if (!mem::read(e + offsetof(E, entrants_begin), b) || !mem::read(e + offsetof(E, entrants_end), en) || !b ||
            en < b || (en - b) % sizeof(R) || (en - b) / sizeof(R) > MAX_ENTRANTS || (en != b && !mem::readable(b, en - b)))
            return;
        uint32_t humans = 0;
        for (uintptr_t p = b; p < en; p += sizeof(R)) {
            uint32_t flags = 0, driver = 0, team = 0;
            mem::read(p + offsetof(R, ordinal_flags), flags);
            if (!(flags & HUMAN_FLAG))
                continue;
            mem::read(p + offsetof(R, id_lo), driver);
            mem::read(p + offsetof(R, team_stat_id), team);
            out.driver = driver, out.team = team;
            ++humans;
        }
        out.humans = humans;
        out.player = humans == 1;
        if (!out.player)
            out.driver = out.team = 0;
    } else {
        (void)e, (void)out;
    }
}

runtime::career::Components poll()
{
    static const uint32_t find = perf::probe("core", "career poll: entity");
    static const uint32_t lookup = perf::probe("core", "career poll: components");
    static const uint32_t walk = perf::probe("core", "career poll: entrants");
    mem::Trust trust(POLL_TRUST_MS);
    runtime::career::Components c;
    uintptr_t e = 0;
    {
        perf::Scope p(find);
        e = championship_entity();
    }
    c.entity = e != 0;
    if (!e)
        return c;
    {
        perf::Scope p(lookup);
        c.career = component_of(e, "CareerComponent");
        if (c.career) {
            if (component_of(e, "CareerBankComponent") || component_of(e, "MyTeamFacilitiesComponent"))
                c.mode = SWOFTY_CAREER_MY_TEAM;
            else if (component_of(e, "DriverCareerPlayerDriverStatisticsComponent"))
                c.mode = SWOFTY_CAREER_DRIVER;
        }
    }
    if (c.career) {
        perf::Scope p(walk);
        entrants(e, c);
    }
    return c;
}

int api_available()
{
    return (set_up ? armed.load() : bool(layout::FLOW)) && runtime::enabled("career", nullptr);
}

int api_state(SwoftyCareerState *out)
{
    if (!out)
        return SWOFTY_ASSET_INVALID;
    if (!api_available())
        return SWOFTY_ASSET_UNAVAILABLE;
    std::lock_guard<std::mutex> g(lock);
    *out = tracker.state();
    return SWOFTY_ASSET_OK;
}

int api_flow(SwoftyFlowState *out)
{
    if (!out)
        return SWOFTY_ASSET_INVALID;
    if (!api_available())
        return SWOFTY_ASSET_UNAVAILABLE;
    std::lock_guard<std::mutex> g(lock);
    if (!tracker.last())
        return SWOFTY_ASSET_LATE;
    *out = *tracker.last();
    return SWOFTY_ASSET_OK;
}

int api_flow_status(uint32_t hash, uint32_t *status, uint64_t *age_ms)
{
    if (!api_available())
        return SWOFTY_ASSET_UNAVAILABLE;
    uint32_t s = 0;
    uint64_t age = 0;
    bool seen;
    {
        std::lock_guard<std::mutex> g(lock);
        seen = tracker.status_of(hash, s, age, now_ms());
    }
    if (status)
        *status = s;
    if (age_ms)
        *age_ms = age;
    return seen ? SWOFTY_ASSET_OK : SWOFTY_ASSET_INVALID;
}

void *api_entity()
{
    if (!runtime::enabled("career", nullptr))
        return nullptr;
    return reinterpret_cast<void *>(championship_entity());
}

void *api_component(const char *type)
{
    if (!runtime::enabled("career", nullptr) || !type || !*type)
        return nullptr;
    uintptr_t e = championship_entity();
    return e ? reinterpret_cast<void *>(component_of(e, type)) : nullptr;
}

void *api_component_by_desc(const void *desc)
{
    if (!runtime::enabled("career", nullptr) || !desc)
        return nullptr;
    uintptr_t e = championship_entity();
    return e ? reinterpret_cast<void *>(registry_get(e, reinterpret_cast<uintptr_t>(desc))) : nullptr;
}

template <class H = g::GameMessageBusHolder, class B = g::NeMessageBus> int api_post(void *msg)
{
    if (!runtime::enabled("career", nullptr))
        return SWOFTY_ASSET_UNAVAILABLE;
    if (!msg || !mem::object_like(msg))
        return SWOFTY_ASSET_INVALID;
    if constexpr (layout::message_bus<H, B>) {
        uintptr_t slot = binding("g_Game_MessageBus"), fn = binding("Ne_Post_BusMessage"), holder = 0, bus = 0;
        uint8_t active = 0;
        if (!slot || !fn)
            return SWOFTY_ASSET_UNAVAILABLE;
        if (!mem::read(slot, holder) || !holder || !mem::read(holder + offsetof(H, bus), bus) || !mem::object_like(bus) ||
            !mem::read(bus + offsetof(B, active), active) || !active)
            return SWOFTY_ASSET_LATE;
        reinterpret_cast<Post *>(fn)(reinterpret_cast<void *>(bus), msg);
        return SWOFTY_ASSET_OK;
    } else {
        return SWOFTY_ASSET_UNAVAILABLE;
    }
}

int api_post_msg(void *msg)
{
    return api_post(msg);
}

int api_block(uint32_t mode, const char *title, const char *body)
{
    return runtime::career_block(__builtin_return_address(0), mode, title, body);
}

int api_unblock(uint32_t mode)
{
    return runtime::career_unblock(__builtin_return_address(0), mode);
}

const SwoftyCareer table = {api_available, api_state,     api_flow,              api_flow_status, api_entity,
                            api_component, api_component_by_desc, api_post_msg, api_block,      api_unblock};

void flow_entry(SwoftyContext *c)
{
    on_flow(c);
}

void team_hq_entry(SwoftyContext *c)
{
    on_team_hq(c);
}

}

namespace runtime {

const SwoftyCareer *career_table()
{
    return &table;
}

std::vector<CoreMixins> career_mixins()
{
    std::vector<CoreMixins> out;
    if (!enabled("career", "career")) {
        swofty::log("[career] feature off; the career library reports unavailable");
        return out;
    }
    if (!layout::FLOW) {
        swofty::log("[career] this core was built without the FlowState layout; the career library reports unavailable");
        return out;
    }
    out.push_back({FLOW_MIXIN, {{"site_FeFlow_SetStateStatus", SWOFTY_MIXIN_SITE, 100, (void *)&flow_entry, nullptr}}});
    out.push_back({TEAM_HQ_MIXIN,
                   {{"site_Career_TeamHqIsMyTeamResult", SWOFTY_MIXIN_SITE, 100, (void *)&team_hq_entry, nullptr}}});
    return out;
}

void career_setup()
{
    if (set_up || !enabled("career", nullptr))
        return;
    flow_live = core_mixins_active(FLOW_MIXIN);
    team_hq_live = core_mixins_active(TEAM_HQ_MIXIN);
    armed = flow_live;
    set_up = true;
    career_start_setup();
    swofty::log("[career] setup: %s; flow states %s, Team HQ answer %s, career layout %s, entrants %s, bus %s, "
                "string key %s",
                armed ? "live" : "the flow-state site is not installed, so the career library reports unavailable",
                flow_live ? "armed" : "off", team_hq_live ? "armed" : "off", layout::CAREER ? "yes" : "no",
                layout::ENTRANT ? "yes" : "no", layout::BUS ? "yes" : "no",
                binding("k_Gdb_StringXorKey") ? "bound" : "missing (unknown state names stay hashes)");
}

void career_tick()
{
    if (!armed || !layout::CAREER)
        return;
    uint64_t now = now_ms(), was = last_poll.load();
    if (!schedule.due(now, was, in_session.load(std::memory_order_relaxed)) ||
        !last_poll.compare_exchange_strong(was, now))
        return;
    runtime::career::Components c = poll();
    {
        std::lock_guard<std::mutex> g(lock);
        tracker.components(c, now);
    }
    settle_and_publish();
}

}
