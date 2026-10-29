#pragma once
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <swofty/systems/ai.hpp>
#include <swofty/systems/cars.hpp>
#include <swofty/systems/sessions.hpp>
#include <swofty/systems/spectator.hpp>

#include "dispatch.hpp"
#include "game_point.hpp"
#include "payloads.hpp"
#include "vtable_point.hpp"

// The core's session hook points and session operations (standard library 1.6.0):
//   ai.controller.update      slot 1 of vt_Sess_AiController (Sess_AiController_Update), direct dispatch; or, when
//                             the config picks the "apply" site, a mid-hook on the record-apply call in
//                             Sess_ControllerSystem_ApplyInputs
//   session.player.control    Game_Set_VehicleController_Thunk(system, vehicle, ai, flag index)
//   car.controllers.rebuilt   the return of the controller sets' build-all (Sess_ControllerSystem_BuildSets)
//   hud.spectator.visible     Sess_SpectatorHud_Hide (hide reason 6)
// The destroy-all (Sess_ControllerSystem_DestroySets) and build-all hooks stay installed from the window stage:
// they tell the core when the sets exist, which the operations need whether or not an addon subscribes.
// Set<Tag> keeps the state per tag so the host tests run the same detours on stand-ins. Every offset below is a
// KB field (re/db/structs/{ControllerSet,AiController,InputRecord,SpectatorDirector}.h).

namespace swofty_core::hooks::session_points {

namespace aie = swofty::ai::events;
namespace sse = swofty::sessions::events;
namespace cae = swofty::cars::events;
namespace spe = swofty::spectator::events;
using swofty::sessions::Control;

constexpr uintptr_t SYS_SETS_BEGIN = 0x30, SYS_SETS_END = 0x38, SYS_VEHICLES = 0x90, SYS_BUILT = 0x365;
constexpr uintptr_t VM_BEGIN = 0x28, VM_END = 0x30;
constexpr uintptr_t SET_MODE_MASKED = 0x10, SET_MODE_KEY = 0x18, SET_FLAGS = 0x30;
constexpr uintptr_t CTRL_INPUT = 0x08, CTRL_VEHICLE = 0x18;
constexpr uintptr_t REC_THROTTLE = 0x0c, REC_STEERING = 0x14, REC_BRAKE = 0x18;
constexpr uintptr_t DIR_ACTIVE = 0xd1, DIR_BLOCKED = 0xd2, DIR_CURRENT_DRIVER = 0xd6, DIR_CURRENT_CAMERA = 0xd8,
                    DIR_REQUESTED_DRIVER = 0xdc, DIR_REQUESTED_CAMERA = 0xe0;
constexpr size_t AI_UPDATE_SLOT = 1;
constexpr uint32_t NO_CAR = 0xffffffffu;

struct Spec {
    const char *point;
    const char *binding;
};

inline constexpr Spec AI_VTABLE{"ai.controller.update", "vt_Sess_AiController"};
inline constexpr Spec AI_APPLY{"ai.controller.update", "site_Sess_ControllerSystem_ApplyRecordCall"};
inline constexpr Spec CONTROL{"session.player.control", "Game_Set_VehicleController_Thunk"};
inline constexpr Spec BUILD{"car.controllers.rebuilt", "Sess_ControllerSystem_BuildSets"};
inline constexpr Spec DESTROY{"car.controllers.destroyed", "Sess_ControllerSystem_DestroySets"};
inline constexpr Spec HIDE{"hud.spectator.visible", "Sess_SpectatorHud_Hide"};
inline constexpr Spec DIRECTOR_UPDATE{"spectator.director", "Sess_SpectatorDirector_Update"};
inline constexpr Spec DIRECTOR_SET_ACTIVE{"spectator.director", "Sess_SpectatorDirector_SetActive"};

struct Env {
    std::function<bool(uintptr_t addr, void *out, size_t n)> read;
    std::function<bool(uintptr_t addr, const void *in, size_t n)> write;
    std::function<bool()> flow_thread;
    std::function<std::string()> ai_site;
    std::function<void(const std::string &)> log;
    int core_flag = 6;
};

class MidPoint final : public GameLink {
public:
    MidPoint(std::string point, std::string binding, void (*fn)(safetyhook::Context &), GameOps ops)
        : point_(std::move(point)), binding_(std::move(binding)), fn_(fn), ops_(std::move(ops))
    {
    }

    bool install(bool late, const std::string &subscribers) override
    {
        std::lock_guard<std::recursive_mutex> g(lock_);
        if (hook_)
            return true;
        uintptr_t at = ops_.resolve ? ops_.resolve(binding_) : 0;
        if (!at)
            return fail("[hooks] " + point_ + " unavailable: " + binding_ + " does not resolve on this build; subscribers " +
                        subscribers + " not run");
        std::string by = ops_.overwritten_by ? ops_.overwritten_by(binding_, at) : std::string();
        if (!by.empty()) {
            if (ops_.conflict)
                ops_.conflict(point_, by);
            return fail("[hooks] " + point_ + " unavailable: " + by + " overwrites " + binding_ + "; subscribers " +
                        subscribers + " not run (the launcher lists this as a conflict)");
        }
        if (late) {
            std::string there = ops_.occupied_by ? ops_.occupied_by(at) : std::string();
            if (!there.empty())
                return fail("[hooks] " + point_ + " unavailable: installed while the game runs, it overlaps " + there +
                            " on " + binding_ + "; refused; subscribers " + subscribers + " not run");
        }
        auto h = safetyhook::MidHook::create(reinterpret_cast<void *>(at), fn_);
        if (!h)
            return fail("[hooks] " + point_ + " unavailable: the mid-hook on " + binding_ + " failed to install; subscribers " +
                        subscribers + " not run");
        hook_ = std::move(*h);
        log("[hooks] " + point_ + " live on " + binding_ + (late ? " (installed late)" : " (window stage)") +
            ": subscribers " + subscribers);
        return true;
    }

    void uninstall() override
    {
        std::lock_guard<std::recursive_mutex> g(lock_);
        if (!hook_)
            return;
        hook_.reset();
        log("[hooks] " + point_ + " has no subscriber left; the mid-hook on " + binding_ + " is removed");
    }

    bool installed() const override
    {
        std::lock_guard<std::recursive_mutex> g(lock_);
        return bool(hook_);
    }

private:
    bool fail(const std::string &line)
    {
        log(line);
        return false;
    }
    void log(const std::string &line)
    {
        if (ops_.log)
            ops_.log(line);
    }

    std::string point_, binding_;
    void (*fn_)(safetyhook::Context &);
    GameOps ops_;
    mutable std::recursive_mutex lock_;
    safetyhook::MidHook hook_;
};

class AiLink final : public GameLink {
public:
    AiLink(VtablePoint *vtable, MidPoint *apply, std::function<std::string()> site)
        : vtable_(vtable), apply_(apply), site_(std::move(site))
    {
    }

    bool install(bool late, const std::string &subscribers) override
    {
        if (!active_) {
            std::string site = site_ ? site_() : std::string();
            active_ = site == "apply" ? static_cast<GameLink *>(apply_) : static_cast<GameLink *>(vtable_);
        }
        return active_->install(late, subscribers);
    }
    void uninstall() override
    {
        if (active_)
            active_->uninstall();
    }
    bool installed() const override { return active_ && active_->installed(); }

private:
    VtablePoint *vtable_;
    MidPoint *apply_;
    std::function<std::string()> site_;
    GameLink *active_ = nullptr;
};

inline int mode_of(uint32_t masked, uint32_t key, uint32_t &mode)
{
    uint32_t m = masked ^ key;
    if (m > 1)
        return SWOFTY_ASSET_INVALID;
    mode = m;
    return SWOFTY_ASSET_OK;
}

template <class Tag> struct Set {
    static inline Table *table = nullptr;
    static inline Env env;
    static inline GameOps ops;
    static inline Point *ai_point = nullptr, *control_p = nullptr, *rebuilt_p = nullptr, *hide_p = nullptr;

    static inline std::unique_ptr<VtablePoint> ai_vtable;
    static inline std::unique_ptr<MidPoint> ai_apply;
    static inline std::unique_ptr<AiLink> ai_link;
    static inline std::unique_ptr<GamePoint> control, build, destroy, hide;
    static inline std::unique_ptr<GamePoint> director_update, director_set_active;

    static inline std::atomic<uintptr_t> system{0};
    static inline std::atomic<bool> rebuilding{true};
    static inline std::atomic<int> depth{0};
    static inline std::atomic<uintptr_t> director{0};
    static inline std::atomic<bool> watching{false};

    struct Pending {
        uint32_t car;
        uint32_t mode;
    };
    static inline std::mutex pending_lock;
    static inline std::vector<Pending> pending;

    template <class T> static T rd(uintptr_t a, T def = T{})
    {
        T v{};
        return a && env.read && env.read(a, &v, sizeof v) ? v : def;
    }

    template <class T> static bool wr(uintptr_t a, T v) { return a && env.write && env.write(a, &v, sizeof v); }

    static void note(const std::string &s)
    {
        if (env.log)
            env.log(s);
    }

    static float rf(const uint8_t *rec, uintptr_t off)
    {
        float v;
        std::memcpy(&v, rec + off, sizeof v);
        return v;
    }

    static void sf(uint8_t *rec, uintptr_t off, float v, float keep)
    {
        if (!std::isfinite(v))
            v = keep;
        std::memcpy(rec + off, &v, sizeof v);
    }

    // The ai.controller.update payload goes into the record's three floats; a non-finite value from a handler
    // keeps what the record held.
    static void store_record(uint8_t *rec, const aie::ControllerUpdate &u, const float held[3])
    {
        sf(rec, REC_THROTTLE, u.throttle, held[0]);
        sf(rec, REC_BRAKE, u.brake, held[1]);
        sf(rec, REC_STEERING, u.steering, held[2]);
    }

    static void ai_fire(uint8_t *rec, uint32_t vehicle, void (*original)(void *), void *user)
    {
        aie::ControllerUpdate u;
        payloads::controller_update(u, int32_t(vehicle), rf(rec, REC_THROTTLE), rf(rec, REC_BRAKE), rf(rec, REC_STEERING));
        struct Frame {
            uint8_t *rec;
            aie::ControllerUpdate *u;
            void (*original)(void *);
            void *user;
        } frame{rec, &u, original, user};
        int replaced = 0;
        table->fire_direct(
            *ai_point, &u, &replaced,
            [](void *p) {
                auto &f = *static_cast<Frame *>(p);
                if (f.original)
                    f.original(f.user);
                f.u->throttle = rf(f.rec, REC_THROTTLE);
                f.u->brake = rf(f.rec, REC_BRAKE);
                f.u->steering = rf(f.rec, REC_STEERING);
            },
            &frame);
        const float held[3] = {rf(rec, REC_THROTTLE), rf(rec, REC_BRAKE), rf(rec, REC_STEERING)};
        store_record(rec, u, held);
    }

    static void d_ai_update(void *ctrl)
    {
        using Fn = void (*)(void *);
        auto original = reinterpret_cast<Fn>(ai_vtable->original());
        auto *c = static_cast<uint8_t *>(ctrl);
        uint8_t *rec = c ? *reinterpret_cast<uint8_t **>(c + CTRL_INPUT) : nullptr;
        if (!rec || !car_is_ai(*reinterpret_cast<uintptr_t *>(c + 0x10), c[CTRL_VEHICLE])) {
            original(ctrl);
            return;
        }
        ai_fire(rec, c[CTRL_VEHICLE], [](void *p) { reinterpret_cast<Fn>(ai_vtable->original())(p); }, ctrl);
    }

    // Raw reads for the per-frame paths: the controller system and its sets are live objects the game itself is
    // using at these calls.
    static bool car_is_ai(uintptr_t sys, uint32_t vehicle)
    {
        if (!sys)
            return false;
        auto begin = *reinterpret_cast<const uintptr_t *>(sys + SYS_SETS_BEGIN);
        auto end = *reinterpret_cast<const uintptr_t *>(sys + SYS_SETS_END);
        if (!begin || end <= begin || vehicle >= (end - begin) / 8)
            return false;
        auto set = *reinterpret_cast<const uintptr_t *>(begin + uintptr_t(vehicle) * 8);
        uint32_t mode = 0;
        return set && mode_of(*reinterpret_cast<const uint32_t *>(set + SET_MODE_MASKED),
                              *reinterpret_cast<const uint32_t *>(set + SET_MODE_KEY), mode) == SWOFTY_ASSET_OK &&
               mode == 1;
    }

    // The apply-site fallback: at the call in the per-car loop of ApplyInputs rcx is the controller system, edx
    // the vehicle, rax the input record. The record is already the game's decision for the frame, so replace
    // handlers decide on top of it and there is no game step between replace and after; only AI cars run it.
    static void m_apply(safetyhook::Context &ctx)
    {
        auto *rec = reinterpret_cast<uint8_t *>(ctx.rax);
        auto vehicle = uint32_t(ctx.rdx);
        if (!rec || !car_is_ai(ctx.rcx, vehicle))
            return;
        ai_fire(rec, vehicle, nullptr, nullptr);
    }

    static bool read_set(uint32_t car, uint32_t &mode, uint32_t &flags, int &why)
    {
        uintptr_t sys = system.load();
        if (!sys || rebuilding.load() || !rd<uint8_t>(sys + SYS_BUILT)) {
            why = SWOFTY_ASSET_LATE;
            return false;
        }
        uintptr_t begin = rd<uintptr_t>(sys + SYS_SETS_BEGIN), end = rd<uintptr_t>(sys + SYS_SETS_END);
        if (!begin || end <= begin || car >= (end - begin) / 8) {
            why = SWOFTY_ASSET_INVALID;
            return false;
        }
        uintptr_t set = rd<uintptr_t>(begin + uintptr_t(car) * 8);
        if (!set || mode_of(rd<uint32_t>(set + SET_MODE_MASKED), rd<uint32_t>(set + SET_MODE_KEY), mode) != SWOFTY_ASSET_OK) {
            why = SWOFTY_ASSET_INVALID;
            return false;
        }
        flags = rd<uint32_t>(set + SET_FLAGS);
        why = SWOFTY_ASSET_OK;
        return true;
    }

    static void d_control(void *sys, uint64_t vehicle, uint32_t ai, int32_t flag)
    {
        if (ai > 1 || !table->has_handlers(*control_p)) {
            control->original<void>(sys, vehicle, ai, flag);
            return;
        }
        uint32_t from = 0, flags = 0;
        int why = 0;
        if (!read_set(uint32_t(vehicle), from, flags, why))
            from = 0;
        sse::PlayerControl c;
        payloads::player_control(c, uint32_t(vehicle), from == 1 ? Control::ai : Control::human,
                                 ai == 1 ? Control::ai : Control::human, flag);
        const Control asked = c.to;
        depth++;
        control->run(*table, c, [&] {
            uint32_t a = ai;
            if (c.to != asked)
                a = c.to == Control::ai ? 1u : 0u;
            control->original<void>(sys, vehicle, a, c.reason);
        });
        depth--;
    }


    static void d_destroy(void *sys)
    {
        rebuilding = true;
        destroy->original<void>(sys);
    }

    static void d_build(void *sys)
    {
        build->original<void>(sys);
        system = reinterpret_cast<uintptr_t>(sys);
        rebuilding = false;
        fire_rebuilt(reinterpret_cast<uintptr_t>(sys), cae::ControllersRebuilt::rebuild);
    }

    static void fire_rebuilt(uintptr_t sys, uint32_t reason)
    {
        if (!table->has_handlers(*rebuilt_p))
            return;
        uintptr_t vm = rd<uintptr_t>(sys + SYS_VEHICLES);
        uintptr_t vb = rd<uintptr_t>(vm + VM_BEGIN), ve = rd<uintptr_t>(vm + VM_END);
        uintptr_t sb = rd<uintptr_t>(sys + SYS_SETS_BEGIN), se = rd<uintptr_t>(sys + SYS_SETS_END);
        size_t cars = vm && ve > vb ? ((ve - vb) / 8) & 0xff : 0;
        size_t sets = sb && se > sb ? (se - sb) / 8 : 0;
        if (cars > sets)
            cars = sets;
        for (size_t i = 0; i < cars; i++) {
            if (!rd<uintptr_t>(sb + i * 8))
                continue;
            cae::ControllersRebuilt r;
            payloads::controllers_rebuilt(r, uint32_t(i), reason);
            build->run(*table, r, [] {});
        }
    }


    static uint32_t director_following()
    {
        uintptr_t d = director.load();
        if (!d)
            return NO_CAR;
        uint8_t driver = rd<uint8_t>(d + DIR_CURRENT_DRIVER, 0xff);
        return driver == 0xff ? NO_CAR : driver;
    }

    static void d_hide()
    {
        watch_director();
        if (!table->has_handlers(*hide_p)) {
            hide->original<void>();
            return;
        }
        spe::HudVisible h;
        payloads::hud_visible(h, director_following(), false);
        hide->run(*table, h, [&] {
            if (!h.visible)
                hide->original<void>();
        });
    }


    static void d_director_update(void *dir)
    {
        director = reinterpret_cast<uintptr_t>(dir);
        director_update->original<void>(dir);
    }

    static void d_director_set_active(void *dir, bool on)
    {
        director = reinterpret_cast<uintptr_t>(dir);
        director_set_active->original<void>(dir, on);
    }

    static uintptr_t resolve(const char *binding) { return ops.resolve ? ops.resolve(binding) : 0; }

    static int apply_control(uint32_t car, uint32_t mode)
    {
        uint32_t cur = 0, flags = 0;
        int why = 0;
        if (!read_set(car, cur, flags, why))
            return why;
        const uint32_t bit = 1u << env.core_flag;
        uintptr_t fn = resolve(CONTROL.binding);
        if (!fn) {
            note(std::string("[sessions] set_control unavailable: ") + CONTROL.binding + " does not resolve on this build");
            return SWOFTY_ASSET_UNAVAILABLE;
        }
        auto thunk = reinterpret_cast<void (*)(void *, uint64_t, uint32_t, int32_t)>(fn);
        auto sys = reinterpret_cast<void *>(system.load());
        if (mode == 1) {
            if (cur == 1 && (flags & bit))
                return SWOFTY_ASSET_OK;
            thunk(sys, car, 1, env.core_flag);
            note("[sessions] set_control: car " + std::to_string(car) + " handed to the AI (flag " +
                 std::to_string(env.core_flag) + ")");
            return SWOFTY_ASSET_OK;
        }
        uint32_t others = flags & ~bit;
        if (others) {
            note("[sessions] set_control: car " + std::to_string(car) + " stays with the AI: controller flags 0x" +
                 hex(others) + " are set (bit 0 is the pit lane)");
            return SWOFTY_REFUSED;
        }
        if (cur == 0 && !(flags & bit))
            return SWOFTY_ASSET_OK;
        thunk(sys, car, 0, env.core_flag);
        note("[sessions] set_control: car " + std::to_string(car) + " handed back to its human");
        return SWOFTY_ASSET_OK;
    }

    static std::string hex(uint32_t v)
    {
        char b[16];
        snprintf(b, sizeof b, "%x", v);
        return b;
    }

    static bool unsafe() { return rebuilding.load() || depth.load() > 0; }

    static int op_control(uint32_t car, uint32_t *mode, uint32_t *flags)
    {
        uint32_t m = 0, f = 0;
        int why = 0;
        if (!read_set(car, m, f, why))
            return why;
        if (mode)
            *mode = m;
        if (flags)
            *flags = f;
        return SWOFTY_ASSET_OK;
    }

    static int op_set_control(uint32_t car, uint32_t mode)
    {
        if (env.flow_thread && !env.flow_thread())
            return SWOFTY_ASSET_LATE;
        if (mode > 1)
            return SWOFTY_ASSET_INVALID;
        if (!system.load())
            return SWOFTY_ASSET_LATE;
        if (unsafe()) {
            std::lock_guard<std::mutex> g(pending_lock);
            for (auto &p : pending)
                if (p.car == car) {
                    p.mode = mode;
                    return SWOFTY_ASSET_OK;
                }
            pending.push_back({car, mode});
            note("[sessions] set_control for car " + std::to_string(car) + " queued: the game is " +
                 (rebuilding.load() ? "rebuilding the controller sets" : "inside a controller switch"));
            return SWOFTY_ASSET_OK;
        }
        return apply_control(car, mode);
    }

    // Called once a frame on the flow thread: queued changes apply when no rebuild or switch is running.
    static void drain()
    {
        if (unsafe())
            return;
        std::vector<Pending> todo;
        {
            std::lock_guard<std::mutex> g(pending_lock);
            todo.swap(pending);
        }
        for (auto &p : todo) {
            int r = apply_control(p.car, p.mode);
            if (r != SWOFTY_ASSET_OK)
                note("[sessions] queued set_control for car " + std::to_string(p.car) + " not applied (result " +
                     std::to_string(r) + ")");
        }
    }

    static int op_spectator_set_active(int on)
    {
        if (env.flow_thread && !env.flow_thread())
            return SWOFTY_ASSET_LATE;
        watch_director();
        uintptr_t d = director.load(), fn = resolve(DIRECTOR_SET_ACTIVE.binding);
        if (!fn)
            return SWOFTY_ASSET_UNAVAILABLE;
        if (!d)
            return SWOFTY_ASSET_LATE;
        reinterpret_cast<void (*)(void *, bool)>(fn)(reinterpret_cast<void *>(d), on != 0);
        note(std::string("[spectator] director ") + (on ? "on" : "off"));
        return SWOFTY_ASSET_OK;
    }

    static bool director_active(uintptr_t d) { return d && rd<uint8_t>(d + DIR_ACTIVE) != 0; }

    static int op_spectator_follow(uint32_t car)
    {
        if (env.flow_thread && !env.flow_thread())
            return SWOFTY_ASSET_LATE;
        watch_director();
        uintptr_t d = director.load();
        if (car > 0xfe)
            return SWOFTY_ASSET_INVALID;
        if (!director_active(d) || rd<uint8_t>(d + DIR_BLOCKED))
            return SWOFTY_ASSET_LATE;
        return wr<uint8_t>(d + DIR_REQUESTED_DRIVER, uint8_t(car)) ? SWOFTY_ASSET_OK : SWOFTY_ASSET_INVALID;
    }

    static int op_spectator_set_camera(int32_t camera)
    {
        watch_director();
        if (env.flow_thread && !env.flow_thread())
            return SWOFTY_ASSET_LATE;
        if (camera != 1 && camera != 2)
            return SWOFTY_ASSET_INVALID;
        uintptr_t d = director.load();
        if (!director_active(d))
            return SWOFTY_ASSET_LATE;
        return wr<int32_t>(d + DIR_REQUESTED_CAMERA, camera) ? SWOFTY_ASSET_OK : SWOFTY_ASSET_INVALID;
    }

    static int op_spectator_current(int32_t *car, int32_t *camera)
    {
        watch_director();
        uintptr_t d = director.load();
        if (!director_active(d))
            return SWOFTY_ASSET_LATE;
        uint8_t driver = rd<uint8_t>(d + DIR_CURRENT_DRIVER, 0xff);
        if (car)
            *car = driver == 0xff ? -1 : driver;
        if (camera)
            *camera = rd<int32_t>(d + DIR_CURRENT_CAMERA, -1);
        return SWOFTY_ASSET_OK;
    }

    // Installs what the core observes from the window stage on (the sets' destroy-all and build-all).
    static void arm()
    {
        for (GamePoint *p : {destroy.get(), build.get()}) {
            p->keep(true);
            p->install(false, "the core");
        }
    }

    // The spectator director is found by watching its own calls; installed when an addon first uses a
    // spectator call or subscribes to hud.spectator.visible.
    static void watch_director()
    {
        if (watching.exchange(true))
            return;
        for (GamePoint *p : {director_update.get(), director_set_active.get()}) {
            p->keep(true);
            p->install(true, "the core");
        }
    }

    static void declare(Table &t, const VtableOps &vops, Env e)
    {
        table = &t;
        env = std::move(e);
        ops = vops.game;
        ai_vtable = std::make_unique<VtablePoint>(AI_VTABLE.point, AI_VTABLE.binding, AI_UPDATE_SLOT,
                                                  reinterpret_cast<void *>(&d_ai_update), vops);
        ai_apply = std::make_unique<MidPoint>(AI_APPLY.point, AI_APPLY.binding, &m_apply, vops.game);
        ai_link = std::make_unique<AiLink>(ai_vtable.get(), ai_apply.get(), env.ai_site);
        t.declare_game(AI_VTABLE.point, SWOFTY_POINT_CANCEL, aie::ControllerUpdate::version, ai_link.get(), true);
        ai_point = t.direct_point(AI_VTABLE.point);
        control_p = t.direct_point(CONTROL.point);
        rebuilt_p = t.direct_point(BUILD.point);
        hide_p = t.direct_point(HIDE.point);

        auto make = [&](std::unique_ptr<GamePoint> &slot, const Spec &s, void *detour) {
            slot = std::make_unique<GamePoint>(s.point, s.binding, detour, vops.game);
        };
        make(control, CONTROL, reinterpret_cast<void *>(&d_control));
        t.declare_game(CONTROL.point, SWOFTY_POINT_CANCEL, sse::PlayerControl::version, control.get());
        make(build, BUILD, reinterpret_cast<void *>(&d_build));
        t.declare_game(BUILD.point, SWOFTY_POINT_NOTIFY, cae::ControllersRebuilt::version, build.get());
        make(destroy, DESTROY, reinterpret_cast<void *>(&d_destroy));
        make(hide, HIDE, reinterpret_cast<void *>(&d_hide));
        t.declare_game(HIDE.point, SWOFTY_POINT_MODIFY, spe::HudVisible::version, hide.get());
        make(director_update, DIRECTOR_UPDATE, reinterpret_cast<void *>(&d_director_update));
        make(director_set_active, DIRECTOR_SET_ACTIVE, reinterpret_cast<void *>(&d_director_set_active));
    }
};

}
