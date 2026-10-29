#pragma once
#include <cstdint>
#include <functional>
#include <string>

#include <swofty/systems/ai.hpp>
#include <swofty/systems/career.hpp>
#include <swofty/systems/cars.hpp>
#include <swofty/systems/sessions.hpp>
#include <swofty/systems/sounds.hpp>
#include <swofty/systems/spectator.hpp>
#include <swofty/systems/tracks.hpp>

// Builds the C-layout payloads of the core's car and sound events and fires them through `fire` (the
// core's hooks::fire, or a test table). Text and record ids point at the caller's strings for the call;
// what handlers change comes back through the reference arguments. The game hook points' payloads
// (game_points.hpp) are filled from the game function's arguments by the builders at the end.

namespace swofty_core::hooks::payloads {

using PartSink = std::function<int32_t(const char *node, float detach_at, bool debris)>;

template <class Fire>
bool upgrades_fit(Fire &&fire, int32_t car, const char *vehicle, swofty::cars::Department department, float driver_stat,
                  float &rating)
{
    swofty::cars::events::UpgradesFit e{};
    e.car = car;
    e.vehicle_id = vehicle;
    e.department = department;
    e.driver_stat = driver_stat;
    e.rating = rating;
    bool ok = fire(e);
    rating = e.rating;
    return ok;
}

template <class Fire>
bool vtf_read(Fire &&fire, const swofty::cars::live::Car &car, const char *section, const char *field,
              const char *upgrade, float &value)
{
    swofty::cars::events::VtfRead e{};
    e.car = car;
    e.section_str = section;
    e.field_str = field;
    e.upgrade_str = upgrade ? upgrade : "";
    e.value = value;
    bool ok = fire(e);
    value = e.value;
    return ok;
}

template <class Fire> bool built(Fire &&fire, const swofty::cars::live::Car &car)
{
    swofty::cars::events::Built e{};
    e.car = car;
    return fire(e);
}

template <class Fire> bool reset(Fire &&fire, const swofty::cars::live::Car &car, bool upgrade)
{
    swofty::cars::events::Reset e{};
    e.car = car;
    e.upgrade = upgrade;
    return fire(e);
}

template <class Fire>
bool parts_register(Fire &&fire, int32_t car, const char *vehicle, void *front_wing, const PartSink &sink)
{
    swofty::cars::events::PartsRegister e{};
    e.car = car;
    e.vehicle_id = vehicle;
    e.front_wing = front_wing;
    e.add_fn = [](void *user, const char *node, float detach_at, bool debris) -> int32_t {
        auto &s = *static_cast<const PartSink *>(user);
        if (!node || !*node)
            return SWOFTY_ASSET_INVALID;
        return s ? s(node, detach_at, debris) : SWOFTY_ASSET_UNAVAILABLE;
    };
    e.user = const_cast<PartSink *>(&sink);
    return fire(e);
}

struct SoundSlot {
    std::string id;
    swofty::sounds::events::BundleResolve *payload;
};

template <class Fire>
bool bundle_resolve(Fire &&fire, int32_t car, const char *vehicle, const char *switch_name, std::string &engine_sound)
{
    swofty::sounds::events::BundleResolve e{};
    SoundSlot slot{engine_sound, &e};
    e.car = car;
    e.vehicle_id = vehicle;
    e.switch_name_str = switch_name;
    e.engine_sound_id = slot.id.c_str();
    e.set_engine_sound_fn = [](void *user, const char *id) -> int32_t {
        if (!id)
            return SWOFTY_ASSET_INVALID;
        auto *s = static_cast<SoundSlot *>(user);
        s->id = id;
        s->payload->engine_sound_id = s->id.c_str();
        return SWOFTY_ASSET_OK;
    };
    e.user = &slot;
    bool ok = fire(e);
    engine_sound = e.engine_sound_id ? e.engine_sound_id : "";
    return ok;
}

// The game hook points: each payload is filled from the game function's arguments before the
// handlers run; game_points.hpp's probe adds what the core reads from the game (the calendar's
// driver, the contract's team, the session's track).

inline void add_activity(swofty::career::events::AddActivity &a, void *calendar, const void *activity, uint32_t day)
{
    a = {};
    a.size = sizeof a;
    a.payload_version = swofty::career::events::AddActivity::version;
    a.calendar = calendar;
    a.activity = activity;
    a.day = day;
}

inline void contract_decide(swofty::career::events::ContractDecide &d, void *state, const uint32_t *link)
{
    d = {};
    d.size = sizeof d;
    d.payload_version = swofty::career::events::ContractDecide::version;
    d.state = state;
    d.link = link ? *link : 0;
}

inline void contract_sign(swofty::career::events::ContractSign &s, void *contracts, void *contract, uint64_t driver,
                          uint32_t replaced, uint8_t notify)
{
    s = {};
    s.size = sizeof s;
    s.payload_version = swofty::career::events::ContractSign::version;
    s.contracts = contracts;
    s.contract = contract;
    s.driver = uint32_t(driver);
    s.replaced = replaced;
    s.notify = notify != 0;
}

// The game's session types (1-18) by the table Game_CarSetup_SessionFuelLoad reads (0x14568e730):
// 1-4 practice, 5-14 qualifying and the sprint shootout, 15-17 the races, 18 Time Trial; any other the
// game treats as a race.
inline uint8_t fuel_kind(uint32_t session_type)
{
    using F = swofty::tracks::events::FuelLoad;
    if (session_type >= 1 && session_type <= 4)
        return F::practice;
    if (session_type >= 5 && session_type <= 14)
        return F::qualifying;
    if (session_type == 18)
        return F::other;
    return F::race;
}

inline void fuel_load(swofty::tracks::events::FuelLoad &f, const char *track, uint32_t session_type)
{
    f = {};
    f.size = sizeof f;
    f.payload_version = swofty::tracks::events::FuelLoad::version;
    f.track_id = track ? track : "";
    f.session_type = session_type;
    f.kind = fuel_kind(session_type);
}

// The session points (session_points.hpp). Vehicle indexes become cars::live::Car handles (the game's car
// object is not carried: addons read a car through its index).

inline void controller_update(swofty::ai::events::ControllerUpdate &u, int32_t vehicle, float throttle, float brake,
                              float steering)
{
    u = {};
    u.size = sizeof u;
    u.payload_version = swofty::ai::events::ControllerUpdate::version;
    u.vehicle = vehicle;
    u.throttle = throttle;
    u.brake = brake;
    u.steering = steering;
}

inline void player_control(swofty::sessions::events::PlayerControl &c, uint32_t vehicle, swofty::sessions::Control from,
                           swofty::sessions::Control to, int32_t reason)
{
    c = {};
    c.size = sizeof c;
    c.payload_version = swofty::sessions::events::PlayerControl::version;
    c.car = swofty::cars::live::Car(vehicle);
    c.from = from;
    c.to = to;
    c.reason = reason;
}

inline void controllers_rebuilt(swofty::cars::events::ControllersRebuilt &r, uint32_t vehicle, uint32_t reason)
{
    r = {};
    r.size = sizeof r;
    r.payload_version = swofty::cars::events::ControllersRebuilt::version;
    r.car = swofty::cars::live::Car(vehicle);
    r.reason = reason;
}

inline void hud_visible(swofty::spectator::events::HudVisible &h, uint32_t following, bool visible)
{
    h = {};
    h.size = sizeof h;
    h.payload_version = swofty::spectator::events::HudVisible::version;
    h.following = swofty::cars::live::Car(following);
    h.visible = visible;
}

}
