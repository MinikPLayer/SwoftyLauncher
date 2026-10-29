#include <swofty/systems/career.hpp>
#include <swofty/systems/ai.hpp>
#include <swofty/systems/cars.hpp>
#include <swofty/systems/content.hpp>
#include <swofty/systems/sessions.hpp>
#include <swofty/systems/sounds.hpp>
#include <swofty/systems/spectator.hpp>
#include <swofty/systems/tracks.hpp>

#include "core_events.hpp"
#include "hooks.hpp"
#include "payloads.hpp"

namespace swofty_core::hooks::events {
namespace {

constexpr bool same(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

template <class E> constexpr bool declared(uint32_t kind)
{
    for (auto &e : catalogue::EVENTS)
        if (same(e.id, E::point))
            return e.kind == kind && e.version == E::version;
    return false;
}

namespace ce = swofty::career::events;
namespace ca = swofty::cars::events;
namespace so = swofty::sounds::events;

static_assert(declared<ce::SeasonStart>(SWOFTY_POINT_NOTIFY));
static_assert(declared<ce::AddActivity>(SWOFTY_POINT_CANCEL));
static_assert(declared<ce::ContractDecide>(SWOFTY_POINT_CANCEL));
static_assert(declared<ce::ContractSign>(SWOFTY_POINT_CANCEL));
static_assert(declared<swofty::tracks::events::FuelLoad>(SWOFTY_POINT_CANCEL));
static_assert(declared<ce::StandingsRebuild>(SWOFTY_POINT_MODIFY));
static_assert(declared<ce::Created>(SWOFTY_POINT_NOTIFY));
static_assert(declared<ce::Loaded>(SWOFTY_POINT_NOTIFY));
static_assert(declared<ce::Unloaded>(SWOFTY_POINT_NOTIFY));
static_assert(declared<ce::SeasonEnd>(SWOFTY_POINT_NOTIFY));
static_assert(declared<ce::SeasonBreak>(SWOFTY_POINT_NOTIFY));
static_assert(declared<ce::Saving>(SWOFTY_POINT_MODIFY));
static_assert(declared<ca::UpgradesFit>(SWOFTY_POINT_MODIFY));
static_assert(declared<ca::VtfRead>(SWOFTY_POINT_MODIFY));
static_assert(declared<ca::Built>(SWOFTY_POINT_NOTIFY));
static_assert(declared<ca::Reset>(SWOFTY_POINT_NOTIFY));
static_assert(declared<ca::PartsRegister>(SWOFTY_POINT_NOTIFY));
static_assert(declared<ca::ControllersRebuilt>(SWOFTY_POINT_NOTIFY));
static_assert(declared<swofty::ai::events::ControllerUpdate>(SWOFTY_POINT_CANCEL));
static_assert(declared<swofty::sessions::events::PlayerControl>(SWOFTY_POINT_CANCEL));
static_assert(declared<swofty::spectator::events::HudVisible>(SWOFTY_POINT_MODIFY));
static_assert(declared<so::BundleResolve>(SWOFTY_POINT_MODIFY));
static_assert(sizeof(ce::Loaded) == sizeof(Shell) && sizeof(ce::Created) == sizeof(Shell));

constexpr auto fire_core = [](auto &e) { return fire(e); };

template <class E> bool shell()
{
    E e{};
    return fire(e);
}

}

bool career_created() { return shell<ce::Created>(); }
bool career_season_end() { return shell<ce::SeasonEnd>(); }
bool career_season_break() { return shell<ce::SeasonBreak>(); }
bool career_saving() { return shell<ce::Saving>(); }

bool career_season_start(int year, int season_index)
{
    ce::SeasonStart e{};
    e.year = year;
    e.season_index = season_index;
    return fire(e);
}

bool career_add_activity(ce::AddActivity &payload) { return fire(payload); }
bool career_standings_rebuild(ce::StandingsRebuild &payload) { return fire(payload); }
bool car_upgrades_fit(ca::UpgradesFit &payload) { return fire(payload); }
bool car_vtf_read(ca::VtfRead &payload) { return fire(payload); }
bool car_built(ca::Built &payload) { return fire(payload); }
bool car_reset(ca::Reset &payload) { return fire(payload); }
bool car_parts_register(ca::PartsRegister &payload) { return fire(payload); }
bool audio_bundle_resolve(so::BundleResolve &payload) { return fire(payload); }

bool car_upgrades_fit(int32_t car, const char *vehicle, swofty::cars::Department department, float driver_stat,
                      float &rating)
{
    return payloads::upgrades_fit(fire_core, car, vehicle, department, driver_stat, rating);
}

bool car_vtf_read(const swofty::cars::live::Car &car, const char *section, const char *field, const char *upgrade,
                  float &value)
{
    return payloads::vtf_read(fire_core, car, section, field, upgrade, value);
}

bool car_built(const swofty::cars::live::Car &car) { return payloads::built(fire_core, car); }
bool car_reset(const swofty::cars::live::Car &car, bool upgrade) { return payloads::reset(fire_core, car, upgrade); }

bool car_parts_register(int32_t car, const char *vehicle, void *front_wing, const payloads::PartSink &add)
{
    return payloads::parts_register(fire_core, car, vehicle, front_wing, add);
}

bool audio_bundle_resolve(int32_t car, const char *vehicle, const char *switch_name, std::string &engine_sound)
{
    return payloads::bundle_resolve(fire_core, car, vehicle, switch_name, engine_sound);
}

}
