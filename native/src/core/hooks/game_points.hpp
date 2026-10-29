#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <swofty/systems/career.hpp>
#include <swofty/systems/tracks.hpp>

#include "dispatch.hpp"
#include "game_point.hpp"
#include "payloads.hpp"

// The core's game hook points (standard library 1.6.0), one detour per game function:
//   career.calendar.add_activity  Game_CareerActivities_AddActivity  void(calendar, activity, day)
//   career.contract.decide        Career_Decide_ContractNegotiation  uint32_t *(state, link)
//   career.contract.sign          Career_Sign_DriverContract         void(contracts, contract, driver, replaced, notify)
//   session.fuel.load             Game_CarSetup_SessionFuelLoad      float(setup)
// All four are cancel points. Each detour fills its payload from the arguments (payloads.hpp) and from
// the Probe (what the core reads from the game), fires the chain through GamePoint::run, and calls the
// game's code with the payload's (possibly changed) arguments unless a replace handler handled the
// call. Set<Tag> keeps the detours' state per tag, so the host tests run the same detours on harmless
// stand-ins.

namespace swofty_core::hooks::game_points {

namespace ce = swofty::career::events;
namespace te = swofty::tracks::events;

struct Probe {
    std::function<void(ce::AddActivity &)> add_activity;
    std::function<void(ce::ContractSign &)> contract_sign;
    std::function<std::string(void *setup, uint32_t &session_type)> fuel_track;
};

struct Spec {
    const char *point;
    const char *binding;
};

inline constexpr Spec ADD_ACTIVITY{"career.calendar.add_activity", "Game_CareerActivities_AddActivity"};
inline constexpr Spec DECIDE{"career.contract.decide", "Career_Decide_ContractNegotiation"};
inline constexpr Spec SIGN{"career.contract.sign", "Career_Sign_DriverContract"};
inline constexpr Spec FUEL{"session.fuel.load", "Game_CarSetup_SessionFuelLoad"};

template <class Tag> struct Set {
    static inline Table *table = nullptr;
    static inline Probe probe;
    static inline std::unique_ptr<GamePoint> add, decide, sign, fuel;

    static void d_add_activity(void *calendar, const void *activity, uint32_t day)
    {
        ce::AddActivity a;
        payloads::add_activity(a, calendar, activity, day);
        if (probe.add_activity)
            probe.add_activity(a);
        add->run(*table, a, [&] { add->original<void>(a.calendar, a.activity, a.day); });
    }

    static uint32_t *d_decide(void *state, uint32_t *link)
    {
        ce::ContractDecide d;
        payloads::contract_decide(d, state, link);
        uint32_t *r = link;
        bool replaced = decide->run(*table, d, [&] {
            r = decide->original<uint32_t *>(d.state, link);
            d.link = link ? *link : 0;
        });
        if (replaced && link) {
            *link = d.link;
            r = link;
        }
        return r;
    }

    static void d_sign(void *contracts, void *contract, uint64_t driver, uint32_t replaced, uint8_t notify)
    {
        ce::ContractSign s;
        payloads::contract_sign(s, contracts, contract, driver, replaced, notify);
        if (probe.contract_sign)
            probe.contract_sign(s);
        sign->run(*table, s, [&] {
            uint64_t key = (driver & 0xffffffff00000000ull) | s.driver;
            sign->original<void>(s.contracts, s.contract, key, s.replaced, uint8_t(s.notify ? 1 : 0));
        });
    }

    static float d_fuel(void *setup)
    {
        uint32_t type = 0;
        std::string track = probe.fuel_track ? probe.fuel_track(setup, type) : std::string();
        te::FuelLoad f;
        payloads::fuel_load(f, track.c_str(), type);
        fuel->run(*table, f, [&] { f.kg = fuel->original<float>(setup); });
        return f.kg;
    }

    static void declare(Table &t, const GameOps &ops, Probe p)
    {
        table = &t;
        probe = std::move(p);
        auto make = [&](std::unique_ptr<GamePoint> &slot, const Spec &s, void *detour, uint32_t version) {
            slot = std::make_unique<GamePoint>(s.point, s.binding, detour, ops);
            t.declare_game(s.point, SWOFTY_POINT_CANCEL, version, slot.get());
        };
        make(add, ADD_ACTIVITY, reinterpret_cast<void *>(&d_add_activity), ce::AddActivity::version);
        make(decide, DECIDE, reinterpret_cast<void *>(&d_decide), ce::ContractDecide::version);
        make(sign, SIGN, reinterpret_cast<void *>(&d_sign), ce::ContractSign::version);
        make(fuel, FUEL, reinterpret_cast<void *>(&d_fuel), te::FuelLoad::version);
    }
};

}
