#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "swofty_api.h"

#include "dispatch.hpp"
#include "game_point.hpp"

// What the rest of the core uses of the 1.6 hook points: the dispatch table, the current facts, the
// firing helper for the core's own events, the 1.x bridge, the game point ops and the window-stage
// arm. The C table (SwoftyHooks) and its export live in hooks.cpp.

namespace swofty_core::hooks {

struct Shell {
    uint32_t size, payload_version;
};

struct FaultRecord {
    std::string point, phase, addon, what;
};

const SwoftyHooks *table();
Table &core();
GameOps game_ops();
void declare_game_points(Table &t);
void declare_session_points(Table &t);
void arm_session_points();
void set_records(RecordFn fn);
void window_stage();
void drop_module(void *module);
void bridge_legacy(const char *event, int64_t a, int64_t b, const void *data);
std::vector<FaultRecord> faults();
std::string describe_fault(unsigned long code, uintptr_t at, const char *what);

template <class E> bool fire(E &payload, std::shared_ptr<const Facts> facts = nullptr)
{
    payload.size = uint32_t(sizeof(E));
    payload.payload_version = E::version;
    int replaced = 0;
    core().fire("", E::point, &payload, &replaced, {}, std::move(facts));
    return replaced == 0;
}

inline bool fire_shell(const char *point)
{
    Shell s{sizeof(Shell), 1};
    int replaced = 0;
    core().fire("", point, &s, &replaced);
    return replaced == 0;
}

template <class F> void update_facts(F &&fn) { core().update(std::forward<F>(fn)); }

}
