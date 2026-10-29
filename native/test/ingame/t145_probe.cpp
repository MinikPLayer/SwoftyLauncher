#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <swofty/events.hpp>
#include <swofty/hooks.hpp>
#include <swofty/mixin.hpp>
#include <swofty/systems/ai.hpp>
#include <swofty/systems/cars.hpp>

namespace {

struct Count {
    unsigned ai = 0, rebuilt = 0, resettled = 0;
    std::set<DWORD> threads;
};

std::mutex mu;
std::map<int, Count> cars;
ULONGLONG window_start;
unsigned long long total_ai, total_rebuilt;
unsigned windows;

void flush_locked(ULONGLONG now)
{
    if (now - window_start < 1000)
        return;
    double secs = double(now - window_start) / 1000.0;
    window_start = now;
    ++windows;
    if (cars.empty()) {
        if (windows % 10 == 0)
            swofty::log("[t145] second %u: no ai.controller.update and no car.controllers.rebuilt yet (totals ai %llu, rebuilt %llu)",
                        windows, total_ai, total_rebuilt);
        return;
    }
    unsigned ai = 0, rebuilt = 0;
    for (auto &[v, c] : cars) {
        ai += c.ai, rebuilt += c.rebuilt + c.resettled;
        std::string t;
        for (DWORD id : c.threads)
            t += (t.empty() ? "" : ",") + std::to_string(id);
        swofty::log("[t145] second %u vehicle %d: ai.controller.update %.1f/s (%u), car.controllers.rebuilt %u (resettle %u), threads %s",
                    windows, v, c.ai / secs, c.ai, c.rebuilt, c.resettled, t.empty() ? "-" : t.c_str());
    }
    swofty::log("[t145] second %u: %zu vehicle(s), ai.controller.update %u, car.controllers.rebuilt %u (totals ai %llu, rebuilt %llu)",
                windows, cars.size(), ai, rebuilt, total_ai, total_rebuilt);
    for (auto &[v, c] : cars)
        c = Count{};
    cars.clear();
}

void on_update(const swofty::ai::events::ControllerUpdate &u, const swofty::hooks::Outcome &)
{
    std::lock_guard g(mu);
    auto &c = cars[u.vehicle];
    ++c.ai, ++total_ai;
    c.threads.insert(GetCurrentThreadId());
    flush_locked(GetTickCount64());
}

void on_rebuilt(const swofty::cars::events::ControllersRebuilt &r, const swofty::hooks::Outcome &)
{
    std::lock_guard g(mu);
    auto &c = cars[int(r.car.index())];
    (r.reason == swofty::cars::events::ControllersRebuilt::resettle ? c.resettled : c.rebuilt)++;
    ++total_rebuilt;
    flush_locked(GetTickCount64());
}

void on_tick(double, void *)
{
    std::lock_guard g(mu);
    flush_locked(GetTickCount64());
}

bool at_init()
{
    window_start = GetTickCount64();
    auto a = swofty::hooks::after<swofty::ai::events::ControllerUpdate>(on_update);
    auto b = swofty::hooks::after<swofty::cars::events::ControllersRebuilt>(on_rebuilt);
    uint32_t token = 0;
    auto f = swofty::events::on_frame(on_tick, nullptr, &token);
    swofty::log("[t145] probe installed: after ai.controller.update %s, after car.controllers.rebuilt %s, on_frame %s",
                swofty::events::name(a), swofty::events::name(b), swofty::events::name(f));
    return true;
}

}

SWOFTY_ADDON(at_init)
