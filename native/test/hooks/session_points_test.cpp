// Host unit test for the core's session hook points and operations (session_points.hpp, vtable_point.hpp,
// Table::fire_direct): the detours run on stand-ins with the game functions' prototypes and on fake
// controller-system memory laid out as the KB's ControllerSet, AiController, InputRecord and
// SpectatorDirector structs. It checks that nothing is patched without a subscriber, the AI controller's
// vtable slot (phases, payload, the record write-back, putting the slot back, yielding to a raw overwrite),
// the apply-site fallback handler, session.player.control (payload, a before handler's changes, replace),
// the destroy/build bracket (operations answer late between them, car.controllers.rebuilt fires per car),
// set_control (flag handling, the pit-lane refusal, queueing during a rebuild and inside a switch), and the
// spectator calls and hud.spectator.visible. run_session_points_test.sh builds and runs it.
#include "../../src/core/hooks/session_points.hpp"
#include "../sdk_payloads.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace swofty_core::hooks;
namespace sp = swofty_core::hooks::session_points;
namespace aie = swofty::ai::events;
namespace sse = swofty::sessions::events;
namespace cae = swofty::cars::events;
namespace spe = swofty::spectator::events;
using swofty::sessions::Control;

static int fails;
static void expect(bool ok, const std::string &what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
    fails += !ok;
}

static std::vector<std::string> lines, trail;
static bool logged(const std::string &s)
{
    for (auto &l : lines)
        if (l.find(s) != std::string::npos)
            return true;
    return false;
}

static std::string joined()
{
    std::string s;
    for (auto &t : trail)
        s += (s.empty() ? "" : " > ") + t;
    return s;
}

static constexpr int CARS = 3;
static uint8_t sys_mem[0x400], vm_mem[0x100], set_mem[CARS][0x60], ai_ctrl[CARS][0x28], rec_mem[CARS][0x20];
static uintptr_t set_list[CARS];
static uintptr_t vehicle_list[CARS];
static float driver_out[CARS][3];
static uint8_t director_mem[0x100];

template <class T> static T &at(uint8_t *base, size_t off) { return *reinterpret_cast<T *>(base + off); }

static void set_mode(int car, uint32_t mode)
{
    at<uint32_t>(set_mem[car], 0x18) = 0x5a5a1234u;
    at<uint32_t>(set_mem[car], 0x10) = mode ^ 0x5a5a1234u;
}

static uint32_t mode_of_car(int car) { return at<uint32_t>(set_mem[car], 0x10) ^ at<uint32_t>(set_mem[car], 0x18); }

static void build_world(bool built = true)
{
    memset(sys_mem, 0, sizeof sys_mem);
    memset(vm_mem, 0, sizeof vm_mem);
    memset(set_mem, 0, sizeof set_mem);
    memset(ai_ctrl, 0, sizeof ai_ctrl);
    memset(rec_mem, 0, sizeof rec_mem);
    for (int i = 0; i < CARS; i++) {
        set_list[i] = reinterpret_cast<uintptr_t>(set_mem[i]);
        vehicle_list[i] = i + 1;
        set_mode(i, i == 0 ? 0 : 1);
        at<void *>(ai_ctrl[i], 0x08) = rec_mem[i];
        at<void *>(ai_ctrl[i], 0x10) = sys_mem;
        ai_ctrl[i][0x18] = uint8_t(i);
        driver_out[i][0] = 0.5f + i;
        driver_out[i][1] = 0.1f * i;
        driver_out[i][2] = -0.25f * i;
    }
    at<uintptr_t>(sys_mem, 0x30) = reinterpret_cast<uintptr_t>(set_list);
    at<uintptr_t>(sys_mem, 0x38) = reinterpret_cast<uintptr_t>(set_list + CARS);
    at<uintptr_t>(sys_mem, 0x90) = reinterpret_cast<uintptr_t>(vm_mem);
    at<uintptr_t>(vm_mem, 0x28) = reinterpret_cast<uintptr_t>(vehicle_list);
    at<uintptr_t>(vm_mem, 0x30) = reinterpret_cast<uintptr_t>(vehicle_list + CARS);
    sys_mem[0x365] = built ? 1 : 0;
}

static void *call_ai_slot(void **vt, int car)
{
    void (*volatile fn)(void *) = reinterpret_cast<void (*)(void *)>(vt[1]);
    fn(ai_ctrl[car]);
    return nullptr;
}

// The stand-ins.
extern "C" __attribute__((noinline)) void sa_ai_update(void *ctrl)
{
    auto *c = static_cast<uint8_t *>(ctrl);
    uint8_t *rec = at<uint8_t *>(c, 0x08);
    int v = c[0x18];
    memcpy(rec + 0x0c, &driver_out[v][0], 4);
    memcpy(rec + 0x18, &driver_out[v][1], 4);
    memcpy(rec + 0x14, &driver_out[v][2], 4);
    trail.push_back("game ai " + std::to_string(v));
}

static void *ai_vtable[4] = {nullptr, reinterpret_cast<void *>(&sa_ai_update), nullptr, nullptr};

extern "C" __attribute__((noinline)) void sa_control(void *system, uint64_t vehicle, uint32_t ai, int32_t flag)
{
    volatile uint32_t pad = ai;
    for (volatile int i = 0; i < 1; i = i + 1)
        pad = pad + 0;
    trail.push_back("game control car " + std::to_string(vehicle) + " ai " + std::to_string(ai) + " flag " + std::to_string(flag));
    if (ai > 1 || vehicle >= CARS || !system)
        return;
    uint32_t &flags = at<uint32_t>(set_mem[vehicle], 0x30);
    if (flag >= 0 && flag < 18)
        flags = ai == 1 ? flags | (1u << flag) : flags & ~(1u << flag);
    set_mode(int(vehicle), ai == 1 || flags ? 1 : 0);
}

extern "C" __attribute__((noinline)) void sa_destroy(void *system)
{
    volatile uintptr_t pad = reinterpret_cast<uintptr_t>(system);
    for (volatile int i = 0; i < 1; i = i + 1)
        pad = pad + 0;
    trail.push_back("game destroy");
    static_cast<uint8_t *>(system)[0x365] = 0;
}

extern "C" __attribute__((noinline)) void sa_build(void *system)
{
    volatile uintptr_t pad = reinterpret_cast<uintptr_t>(system);
    for (volatile int i = 0; i < 1; i = i + 1)
        pad = pad + 0;
    trail.push_back("game build");
    static_cast<uint8_t *>(system)[0x365] = 1;
}

extern "C" __attribute__((noinline)) void sa_hide()
{
    volatile int pad = 0;
    for (volatile int i = 0; i < 1; i = i + 1)
        pad = pad + 0;
    trail.push_back("game hide");
}

extern "C" __attribute__((noinline)) void sa_dir_update(void *dir)
{
    volatile uintptr_t pad = reinterpret_cast<uintptr_t>(dir);
    for (volatile int i = 0; i < 1; i = i + 1)
        pad = pad + 0;
    trail.push_back("game director update");
}

extern "C" __attribute__((noinline)) void sa_dir_set_active(void *dir, bool on)
{
    volatile uintptr_t pad = reinterpret_cast<uintptr_t>(dir);
    for (volatile int i = 0; i < 1; i = i + 1)
        pad = pad + 0;
    trail.push_back(std::string("game director set_active ") + (on ? "1" : "0"));
    auto *d = static_cast<uint8_t *>(dir);
    d[0xd1] = on;
    if (on) {
        d[0xdc] = 0;
        at<int32_t>(d, 0xe0) = 2;
        d[0xd6] = 0;
        at<int32_t>(d, 0xd8) = 2;
    } else {
        d[0xd6] = 0xff;
        at<int32_t>(d, 0xd8) = -1;
    }
}

static std::map<std::string, std::string> overwrites;
static std::vector<std::string> conflicts;
static bool image_ok = true;
static std::string site = "vtable";

static uintptr_t resolve(const std::string &b)
{
    if (b == sp::AI_VTABLE.binding)
        return reinterpret_cast<uintptr_t>(ai_vtable);
    if (b == sp::CONTROL.binding)
        return reinterpret_cast<uintptr_t>(&sa_control);
    if (b == sp::DESTROY.binding)
        return reinterpret_cast<uintptr_t>(&sa_destroy);
    if (b == sp::BUILD.binding)
        return reinterpret_cast<uintptr_t>(&sa_build);
    if (b == sp::HIDE.binding)
        return reinterpret_cast<uintptr_t>(&sa_hide);
    if (b == sp::DIRECTOR_UPDATE.binding)
        return reinterpret_cast<uintptr_t>(&sa_dir_update);
    if (b == sp::DIRECTOR_SET_ACTIVE.binding)
        return reinterpret_cast<uintptr_t>(&sa_dir_set_active);
    return 0;
}

static VtableOps vops()
{
    VtableOps v;
    v.game.log = [](const std::string &s) { lines.push_back(s); };
    v.game.resolve = resolve;
    v.game.overwritten_by = [](const std::string &b, uintptr_t) {
        auto it = overwrites.find(b);
        return it == overwrites.end() ? std::string() : it->second;
    };
    v.game.occupied_by = [](uintptr_t) { return std::string(); };
    v.game.conflict = [](const std::string &point, const std::string &by) { conflicts.push_back(point + " " + by); };
    v.in_image = [](const void *p) { return image_ok && p == reinterpret_cast<void *>(&sa_ai_update); };
    v.write_slot = [](void **slot, void *value) {
        *slot = value;
        return true;
    };
    return v;
}

static bool flow = true;

static sp::Env env()
{
    sp::Env e;
    e.read = [](uintptr_t a, void *out, size_t n) {
        if (!a)
            return false;
        memcpy(out, reinterpret_cast<void *>(a), n);
        return true;
    };
    e.write = [](uintptr_t a, const void *in, size_t n) {
        if (!a)
            return false;
        memcpy(reinterpret_cast<void *>(a), in, n);
        return true;
    };
    e.flow_thread = [] { return flow; };
    e.ai_site = [] { return site; };
    e.log = [](const std::string &s) { lines.push_back(s); };
    e.core_flag = 6;
    return e;
}

struct Platform0 {
    static Platform make()
    {
        Platform p;
        p.log = [](const std::string &s) { lines.push_back(s); };
        return p;
    }
};

template <class P, class F> static uint32_t on(Table &t, const char *addon, uint32_t phase, int priority, F fn)
{
    struct Box {
        F fn;
    };
    auto *box = new Box{fn};
    uint32_t token = 0;
    t.attach(
        addon, P::point, phase, priority, "",
        [](void *user, SwoftyHookCall *c) -> int {
            P &p = *static_cast<P *>(c->payload);
            return static_cast<Box *>(user)->fn(p, *c) ? 1 : 0;
        },
        box, &token);
    return token;
}

struct T1 {};
struct T2 {};
struct T3 {};

static void test_untouched()
{
    lines.clear();
    build_world();
    ai_vtable[1] = reinterpret_cast<void *>(&sa_ai_update);
    unsigned char before[8], after[8];
    memcpy(before, reinterpret_cast<void *>(&sa_control), 8);
    Table t(Platform0::make());
    t.declare_core();
    sp::Set<T1>::declare(t, vops(), env());
    t.arm();
    sp::Set<T1>::arm();
    memcpy(after, reinterpret_cast<void *>(&sa_control), 8);
    expect(!memcmp(before, after, 8), "no subscriber: session.player.control's function is not patched");
    expect(ai_vtable[1] == reinterpret_cast<void *>(&sa_ai_update), "no subscriber: the AI controller's slot is the game's");
    expect(t.live("ai.controller.update", nullptr) == SWOFTY_ASSET_UNAVAILABLE &&
               t.live("session.player.control", nullptr) == SWOFTY_ASSET_UNAVAILABLE &&
               t.live("hud.spectator.visible", nullptr) == SWOFTY_ASSET_UNAVAILABLE,
           "live() says unavailable with no subscriber");
    expect(t.live("car.controllers.rebuilt", nullptr) == SWOFTY_ASSET_OK,
           "the build-all is watched from the window stage, so car.controllers.rebuilt is live");
    trail.clear();
    sa_destroy(sys_mem);
    sa_build(sys_mem);
    expect(sp::Set<T1>::system.load() == reinterpret_cast<uintptr_t>(sys_mem) && !sp::Set<T1>::rebuilding.load(),
           "build-all tells the core where the controller sets are");
}

static void test_ai()
{
    lines.clear();
    trail.clear();
    build_world();
    ai_vtable[1] = reinterpret_cast<void *>(&sa_ai_update);
    Table t(Platform0::make());
    t.declare_core();
    sp::Set<T2>::declare(t, vops(), env());

    aie::ControllerUpdate seen{};
    bool seen_replaced = false;
    float before_throttle = -1;
    on<aie::ControllerUpdate>(t, "kizi.ai-tweaks", SWOFTY_PHASE_BEFORE, 0, [&](aie::ControllerUpdate &u, auto &) {
        before_throttle = u.throttle;
        trail.push_back("before " + std::to_string(u.vehicle));
        return false;
    });
    on<aie::ControllerUpdate>(t, "mod.ai-drive", SWOFTY_PHASE_REPLACE, 0, [&](aie::ControllerUpdate &u, auto &) {
        bool mine = u.vehicle == 2;
        trail.push_back(std::string("replace ") + (mine ? "handled" : "passed"));
        if (mine) {
            u.throttle = 1.0f;
            u.brake = 0.0f;
            u.steering = 0.75f;
        }
        return mine;
    });
    uint32_t after_token = on<aie::ControllerUpdate>(t, "kizi.ai-tweaks", SWOFTY_PHASE_AFTER, 0,
                                                     [&](aie::ControllerUpdate &u, SwoftyHookCall &c) {
                                                         seen = u;
                                                         seen_replaced = c.replaced != 0;
                                                         u.brake = std::min(u.brake, 0.05f);
                                                         trail.push_back("after");
                                                         return false;
                                                     });
    expect(ai_vtable[1] == reinterpret_cast<void *>(&sa_ai_update), "before the window stage nothing is patched");
    sp::Set<T2>::arm();
    t.arm();
    expect(ai_vtable[1] != reinterpret_cast<void *>(&sa_ai_update), "a subscriber puts the dispatcher in the vtable slot");
    expect(logged("[hooks] ai.controller.update live on slot 1 of vt_Sess_AiController"), "the install line names the table and slot");
    expect(t.live("ai.controller.update", nullptr) == SWOFTY_ASSET_OK, "live() says ok");

    trail.clear();
    call_ai_slot(ai_vtable, 1);
    expect(joined() == "before 1 > replace passed > game ai 1 > after", "an AI car: before, replace passes, the game, after: " + joined());
    expect(seen.size == sizeof(aie::ControllerUpdate) && seen.payload_version == 1 && seen.vehicle == 1 &&
               std::fabs(seen.throttle - 1.5f) < 1e-6 && std::fabs(seen.brake - 0.1f) < 1e-6 && std::fabs(seen.steering + 0.25f) < 1e-6 && !seen_replaced,
           "after sees the game's output for the car");
    float brake;
    memcpy(&brake, rec_mem[1] + 0x18, 4);
    expect(std::fabs(brake - 0.05f) < 1e-6, "an after handler's brake is written to the record");
    float throttle;
    memcpy(&throttle, rec_mem[1] + 0x0c, 4);
    expect(std::fabs(throttle - 1.5f) < 1e-6, "the throttle the handler left alone is the game's");

    trail.clear();
    call_ai_slot(ai_vtable, 2);
    float steering;
    memcpy(&steering, rec_mem[2] + 0x14, 4);
    memcpy(&throttle, rec_mem[2] + 0x0c, 4);
    expect(joined() == "before 2 > replace handled > after" && throttle == 1.0f && steering == 0.75f && seen_replaced,
           "a replace handler decides: the game's code is skipped and its values are the record: " + joined());
    expect(before_throttle == 0.0f || before_throttle == 1.0f, "before sees the record's previous values");

    trail.clear();
    call_ai_slot(ai_vtable, 0);
    expect(joined() == "game ai 0", "a car in human mode: the game's code only, no event: " + joined());

    on<aie::ControllerUpdate>(t, "bad.mod", SWOFTY_PHASE_AFTER, 100, [&](aie::ControllerUpdate &u, auto &) {
        u.steering = NAN;
        u.throttle = INFINITY;
        return false;
    });
    trail.clear();
    call_ai_slot(ai_vtable, 1);
    memcpy(&steering, rec_mem[1] + 0x14, 4);
    memcpy(&throttle, rec_mem[1] + 0x0c, 4);
    expect(std::isfinite(steering) && std::isfinite(throttle), "a non-finite value from a handler never reaches the record");

    t.detach(after_token);
    expect(ai_vtable[1] != reinterpret_cast<void *>(&sa_ai_update), "other subscribers keep the slot");
    for (uint32_t tok = 0x80000001u; tok < 0x80000020u; tok++)
        t.detach(tok);
    expect(ai_vtable[1] == reinterpret_cast<void *>(&sa_ai_update), "the last unsubscribe puts the game's function back");
    expect(logged("has no subscriber left; slot 1 of vt_Sess_AiController is put back"), "and logs it");
}

static void test_ai_yield()
{
    lines.clear();
    conflicts.clear();
    build_world();
    ai_vtable[1] = reinterpret_cast<void *>(&sa_ai_update);
    Table t(Platform0::make());
    t.declare_core();
    sp::Set<T3>::declare(t, vops(), env());
    on<aie::ControllerUpdate>(t, "ai.drive", SWOFTY_PHASE_AFTER, 0, [](aie::ControllerUpdate &, auto &) { return false; });
    overwrites[sp::AI_VTABLE.binding] = "ai.drive";
    t.arm();
    expect(ai_vtable[1] == reinterpret_cast<void *>(&sa_ai_update) && conflicts.size() == 1 &&
               logged("unavailable: ai.drive overwrites vt_Sess_AiController") &&
               t.live("ai.controller.update", nullptr) == SWOFTY_ASSET_UNAVAILABLE,
           "a mixin overwrite of the table wins: the point yields, reports the conflict and leaves the slot");
    overwrites.clear();

    lines.clear();
    Table t2(Platform0::make());
    t2.declare_core();
    sp::Set<T3>::declare(t2, vops(), env());
    image_ok = false;
    on<aie::ControllerUpdate>(t2, "raw.writer", SWOFTY_PHASE_AFTER, 0, [](aie::ControllerUpdate &, auto &) { return false; });
    t2.arm();
    image_ok = true;
    expect(logged("no longer points into the game's code") && ai_vtable[1] == reinterpret_cast<void *>(&sa_ai_update),
           "a slot that no longer points into the game's code is left alone");

    lines.clear();
    site = "apply";
    Table t3(Platform0::make());
    t3.declare_core();
    sp::Set<T3>::declare(t3, vops(), env());
    on<aie::ControllerUpdate>(t3, "ai.drive", SWOFTY_PHASE_AFTER, 0, [](aie::ControllerUpdate &, auto &) { return false; });
    t3.arm();
    site = "vtable";
    expect(ai_vtable[1] == reinterpret_cast<void *>(&sa_ai_update) &&
               logged("ai.controller.update unavailable: site_Sess_ControllerSystem_ApplyRecordCall does not resolve"),
           "the apply site is chosen by the switch and leaves the vtable alone");
}

static void test_apply_site()
{
    lines.clear();
    trail.clear();
    build_world();
    Table t(Platform0::make());
    t.declare_core();
    sp::Set<T3>::declare(t, vops(), env());
    int calls = 0;
    on<aie::ControllerUpdate>(t, "ai.drive", SWOFTY_PHASE_REPLACE, 0, [&](aie::ControllerUpdate &u, auto &) {
        calls++;
        u.throttle = 0.9f;
        return true;
    });
    float v = 0.2f;
    memcpy(rec_mem[1] + 0x0c, &v, 4);
    safetyhook::Context ctx{};
    ctx.rax = reinterpret_cast<uintptr_t>(rec_mem[1]);
    ctx.rcx = reinterpret_cast<uintptr_t>(sys_mem);
    ctx.rdx = 1;
    sp::Set<T3>::m_apply(ctx);
    float got;
    memcpy(&got, rec_mem[1] + 0x0c, 4);
    expect(calls == 1 && std::fabs(got - 0.9f) < 1e-6, "the apply-site handler runs the point on an AI car's record");
    ctx.rdx = 0;
    ctx.rax = reinterpret_cast<uintptr_t>(rec_mem[0]);
    sp::Set<T3>::m_apply(ctx);
    expect(calls == 1, "and skips a car in human mode");
}

static void test_control_and_rebuild()
{
    lines.clear();
    trail.clear();
    build_world(false);
    Table t(Platform0::make());
    t.declare_core();
    using S = sp::Set<T1>;
    S::system = 0;
    S::rebuilding = true;
    S::pending.clear();
    S::declare(t, vops(), env());
    t.arm();
    S::arm();
    expect(S::op_control(1, nullptr, nullptr) == SWOFTY_ASSET_LATE && S::op_set_control(1, 0) == SWOFTY_ASSET_LATE,
           "before the first build the operations answer late");

    std::vector<uint32_t> rebuilt;
    on<cae::ControllersRebuilt>(t, "kizi.tracker", SWOFTY_PHASE_AFTER, 0, [&](cae::ControllersRebuilt &r, auto &) {
        rebuilt.push_back(r.car.index() * 10 + r.reason);
        trail.push_back("rebuilt " + std::to_string(r.car.index()) + " readable " + (r.car.control() ? "yes" : "no"));
        return false;
    });
    trail.clear();
    sa_build(sys_mem);
    expect(rebuilt == std::vector<uint32_t>({0, 10, 20}), "build-all fires car.controllers.rebuilt once per car, reason rebuild");
    uint32_t mode = 9, flags = 9;
    expect(S::op_control(0, &mode, &flags) == SWOFTY_ASSET_OK && mode == 0 && flags == 0, "the human car reads mode 0, no flags");
    expect(S::op_control(1, &mode, &flags) == SWOFTY_ASSET_OK && mode == 1, "an AI car reads mode 1");
    expect(S::op_control(7, &mode, &flags) == SWOFTY_ASSET_INVALID, "a car with no set is invalid");

    sa_destroy(sys_mem);
    expect(S::op_control(1, &mode, &flags) == SWOFTY_ASSET_LATE, "between destroy-all and build-all the read answers late");
    expect(S::op_set_control(0, 1) == SWOFTY_ASSET_OK && mode_of_car(0) == 0, "set_control then queues and does not touch the game");
    trail.clear();
    sa_build(sys_mem);
    expect(mode_of_car(0) == 0, "build-all itself applies nothing");
    S::drain();
    expect(mode_of_car(0) == 1 && joined().find("game control car 0 ai 1 flag 6") != std::string::npos,
           "the next frame applies the queued change, with the core's own flag: " + joined());

    expect(S::op_set_control(0, 0) == SWOFTY_ASSET_OK && mode_of_car(0) == 0, "handing the car back clears the core's flag and returns it to its human");
    at<uint32_t>(set_mem[0], 0x30) = 1;
    expect(S::op_set_control(0, 1) == SWOFTY_ASSET_OK && mode_of_car(0) == 1, "to the AI while in the pit lane");
    trail.clear();
    expect(S::op_set_control(0, 0) == SWOFTY_REFUSED && mode_of_car(0) == 1 && trail.empty(),
           "back to human while the pit-lane flag is set: refused, and the game is not called");
    at<uint32_t>(set_mem[0], 0x30) = 1u << 6;
    expect(S::op_set_control(0, 0) == SWOFTY_ASSET_OK && mode_of_car(0) == 0, "with only the core's flag left it goes back");
    flow = false;
    expect(S::op_set_control(0, 1) == SWOFTY_ASSET_LATE, "off the flow thread: late");
    flow = true;
    expect(S::op_set_control(0, 5) == SWOFTY_ASSET_INVALID, "a mode other than 0 or 1 is invalid");

    std::vector<std::string> seen;
    on<sse::PlayerControl>(t, "mod.pit", SWOFTY_PHASE_REPLACE, 0, [&](sse::PlayerControl &c, auto &) {
        seen.push_back("replace car " + std::to_string(c.car.index()) + " from " + std::to_string(int(c.from)) + " to " +
                       std::to_string(int(c.to)) + " reason " + std::to_string(c.reason));
        return c.reason == 3;
    });
    on<sse::PlayerControl>(t, "mod.nested", SWOFTY_PHASE_AFTER, 0, [&](sse::PlayerControl &c, auto &) {
        if (c.car.index() == 2)
            seen.push_back("nested set_control -> " + std::to_string(S::op_set_control(2, 0)));
        return false;
    });
    on<sse::PlayerControl>(t, "mod.edit", SWOFTY_PHASE_BEFORE, 0, [&](sse::PlayerControl &c, auto &) {
        if (c.car.index() == 2 && c.to == Control::ai) {
            c.to = Control::human;
            c.reason = 9;
        }
        return false;
    });
    trail.clear();
    void (*volatile thunk)(void *, uint64_t, uint32_t, int32_t) = &sa_control;
    thunk(sys_mem, 1, 0, 3);
    expect(seen.size() == 1 && seen[0] == "replace car 1 from 1 to 0 reason 3" && joined() == "" && mode_of_car(1) == 1,
           "session.player.control: payload from the arguments, a replace handler skips the whole thunk: " + joined());
    seen.clear();
    trail.clear();
    thunk(sys_mem, 2, 1, 5);
    expect(joined() == "game control car 2 ai 0 flag 9", "a before handler's to and reason reach the game: " + joined());
    expect(seen.size() == 2 && seen[1].find("nested set_control -> 0") == 0 && S::pending.size() == 1,
           "set_control from inside a switch is queued: " + (seen.size() > 1 ? seen[1] : std::string()));
    S::drain();
    expect(S::pending.empty(), "and applied by the frame drain");
    trail.clear();
    thunk(sys_mem, 1, 7, 0);
    expect(joined() == "game control car 1 ai 7 flag 0" && seen.size() == 2, "an ai argument the game ignores passes through with no event");
}

static void test_spectator()
{
    lines.clear();
    trail.clear();
    build_world();
    memset(director_mem, 0, sizeof director_mem);
    director_mem[0xd6] = 0xff;
    at<int32_t>(director_mem, 0xd8) = -1;
    Table t(Platform0::make());
    t.declare_core();
    using S = sp::Set<T2>;
    S::director = 0;
    S::watching = false;
    S::declare(t, vops(), env());
    t.arm();
    S::arm();
    int32_t car = 0, camera = 0;
    expect(S::op_spectator_set_active(1) == SWOFTY_ASSET_LATE, "the director is not known until the game calls it");
    trail.clear();
    void (*volatile update)(void *) = &sa_dir_update;
    update(director_mem);
    expect(S::director.load() == reinterpret_cast<uintptr_t>(director_mem) && joined() == "game director update",
           "a call to the director's update tells the core where it is, and the game's code still runs");
    expect(S::op_spectator_current(&car, &camera) == SWOFTY_ASSET_LATE, "current() is late while the director is off");
    expect(S::op_spectator_follow(3) == SWOFTY_ASSET_LATE && S::op_spectator_set_camera(1) == SWOFTY_ASSET_LATE,
           "follow and set_camera are late while it is off");
    trail.clear();
    expect(S::op_spectator_set_active(1) == SWOFTY_ASSET_OK && joined() == "game director set_active 1" && director_mem[0xd1] == 1,
           "set_active(true) calls the game's SetActive on the director");
    expect(S::op_spectator_follow(2) == SWOFTY_ASSET_OK && director_mem[0xdc] == 2, "follow stores the requested driver");
    expect(S::op_spectator_follow(0xff) == SWOFTY_ASSET_INVALID, "driver 0xff is not a car");
    expect(S::op_spectator_set_camera(1) == SWOFTY_ASSET_OK && at<int32_t>(director_mem, 0xe0) == 1 &&
               S::op_spectator_set_camera(7) == SWOFTY_ASSET_INVALID,
           "set_camera writes the requested camera, 1 or 2 only");
    director_mem[0xd6] = director_mem[0xdc];
    at<int32_t>(director_mem, 0xd8) = 1;
    expect(S::op_spectator_current(&car, &camera) == SWOFTY_ASSET_OK && car == 2 && camera == 1, "current() reads the current driver and camera");
    director_mem[0xd2] = 1;
    expect(S::op_spectator_follow(1) == SWOFTY_ASSET_LATE, "a blocked director refuses follow");
    director_mem[0xd2] = 0;

    int visible_seen = -1;
    uint32_t following = 0;
    on<spe::HudVisible>(t, "mod.director", SWOFTY_PHASE_BEFORE, 0, [&](spe::HudVisible &h, auto &) {
        visible_seen = h.visible;
        following = h.following.index();
        h.visible = true;
        return false;
    });
    trail.clear();
    void (*volatile hide)() = &sa_hide;
    hide();
    expect(visible_seen == 0 && following == 2 && joined().empty(),
           "hud.spectator.visible: the handler sees the car being followed and keeping the HUD up skips the game's hide: " + joined());
    for (uint32_t tok = 0x80000001u; tok < 0x80000040u; tok++)
        t.detach(tok);
    trail.clear();
    hide();
    expect(joined() == "game hide", "with no subscriber the game hides the HUD as before");
}

static void test_layout()
{
    expect(sizeof(aie::ControllerUpdate) == sizeof(SwoftyEvControllerUpdate) && sizeof(sse::PlayerControl) == sizeof(SwoftyEvPlayerControl) &&
               sizeof(cae::ControllersRebuilt) == sizeof(SwoftyEvControllersRebuilt) && sizeof(spe::HudVisible) == sizeof(SwoftyEvHudVisible),
           "the payloads match their C mirrors");
    for (const char *id : {"ai.controller.update", "session.player.control", "car.controllers.rebuilt", "hud.spectator.visible"}) {
        const catalogue::Event *e = catalogue::find(id);
        expect(e && e->fired == catalogue::Fired::game, std::string("the catalogue names ") + id + " a game point");
    }
    const catalogue::Event *e = catalogue::find("ai.controller.update");
    expect(e && e->hot && e->kind == SWOFTY_POINT_CANCEL, "ai.controller.update is a hot cancel point");
}

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    test_untouched();
    test_ai();
    test_ai_yield();
    test_apply_site();
    test_control_and_rebuild();
    test_spectator();
    test_layout();
    printf("%s: %d failure(s)\n", fails ? "FAIL" : "ok", fails);
    return fails ? 1 : 0;
}
