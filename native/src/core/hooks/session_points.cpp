#include "../runtime.hpp"

#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <atomic>
#include <cstring>
#include <string>

#include "hooks.hpp"
#include "session_points.hpp"

// The session hook points and operations on the live game (session_points.hpp): memory reads are checked
// (swofty::mem), the flow thread is the one that ticks on_frame, and the config picks the ai.controller.update
// site ("ai_controller_site": "vtable" is the default, "apply" the mid-hook on the record-apply call) and the
// controller flag set_control uses ("session_control_flag", default 6, DEV).

namespace swofty_core::hooks {
namespace {

namespace mem = swofty::mem;

struct CoreTag {};
using Points = session_points::Set<CoreTag>;

std::atomic<uint32_t> g_flow_tid{0};

bool on_flow_thread()
{
    uint32_t t = g_flow_tid.load();
    return !t || t == GetCurrentThreadId();
}

void frame(double, void *)
{
    g_flow_tid = GetCurrentThreadId();
    Points::drain();
}

int ops_available()
{
    return runtime::enabled("events", nullptr) ? 1 : 0;
}

const SwoftySessionOps g_ops = {
    SWOFTY_SESSION_OPS_VERSION,
    ops_available,
    Points::op_control,
    Points::op_set_control,
    Points::op_spectator_set_active,
    Points::op_spectator_follow,
    Points::op_spectator_set_camera,
    Points::op_spectator_current,
};

session_points::Env make_env()
{
    session_points::Env e;
    e.read = [](uintptr_t a, void *out, size_t n) {
        if (!mem::readable(a, n))
            return false;
        std::memcpy(out, reinterpret_cast<const void *>(a), n);
        return true;
    };
    e.write = [](uintptr_t a, const void *in, size_t n) {
        if (!mem::readable(a, n))
            return false;
        std::memcpy(reinterpret_cast<void *>(a), in, n);
        return true;
    };
    e.flow_thread = on_flow_thread;
    e.ai_site = [] {
        std::string site = runtime::config()["ai_controller_site"].str("vtable");
        return site == "apply" ? site : std::string("vtable");
    };
    e.log = [](const std::string &s) { swofty::log("%s", s.c_str()); };
    long long flag = runtime::config()["session_control_flag"].num(6);
    e.core_flag = flag >= 0 && flag <= 17 ? int(flag) : 6;
    return e;
}

VtableOps make_vops()
{
    VtableOps v;
    v.game = game_ops();
    v.in_image = [](const void *p) { return mem::in_image(p); };
    v.write_slot = [](void **slot, void *value) {
        DWORD old = 0;
        if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old))
            return false;
        *slot = value;
        VirtualProtect(slot, sizeof(void *), old, &old);
        return true;
    };
    return v;
}

void setup()
{
    const SwoftyEvents *ev = runtime::events_table();
    uint32_t token = 0;
    if (ev && ev->on_frame)
        ev->on_frame(frame, nullptr, &token);
}

}

void declare_session_points(Table &t)
{
    Points::declare(t, make_vops(), make_env());
}

void arm_session_points()
{
    Points::arm();
}

}

extern "C" __declspec(dllexport) const SwoftySessionOps *SwoftyCoreSessionOps(void)
{
    return &swofty_core::hooks::g_ops;
}

SWOFTY_ON_LOAD
{
    runtime::when_code_ready("session operations: flow thread", swofty_core::hooks::setup);
}
