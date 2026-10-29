#include "../runtime.hpp"

#include <swofty/game.hpp>
#include <swofty/mem.hpp>

#include <cstddef>
#include <cstring>
#include <string>

#include "game_points.hpp"
#include "hooks.hpp"

// The core's four game hook points on the live game: the Probe reads what each payload carries beyond
// the function's arguments, through KB structs (L2), every pointer checked before it is read. A build
// whose game headers lack a struct leaves that field at its default (0, false, "").

namespace swofty::game {
struct ActivityDef;
struct DriverCalendar;
struct ScheduleComponent;
struct ScheduleSeason;
struct TrackDataRecord;
}

namespace swofty_core::hooks {
namespace {

namespace g = swofty::game;
namespace mem = swofty::mem;

struct CoreTag {};
using Points = game_points::Set<CoreTag>;

constexpr size_t MAX_SEASONS = 64;

template <class T> T rd(uintptr_t a, T def = T{})
{
    T v{};
    return a && mem::read(a, v) ? v : def;
}

template <class D = g::ActivityDef> uint8_t activity_type(const void *activity)
{
    if constexpr (requires(D *d) { d->type; })
        return rd<uint8_t>(reinterpret_cast<uintptr_t>(activity) + offsetof(D, type));
    else
        return 0;
}

template <class S = g::ScheduleComponent, class Z = g::ScheduleSeason, class C = g::DriverCalendar>
bool driver_calendar(uintptr_t calendar, uint32_t &driver)
{
    if constexpr (requires(S *s, Z *z, C *c) {
                      s->seasons_begin;
                      s->seasons_end;
                      z->driver_calendars_begin;
                      z->driver_calendars_end;
                      c->driver;
                  }) {
        const SwoftyCareer *career = runtime::career_table();
        auto schedule = reinterpret_cast<uintptr_t>(career && career->component ? career->component("ScheduleComponent")
                                                                                  : nullptr);
        uintptr_t b = rd<uintptr_t>(schedule + offsetof(S, seasons_begin));
        uintptr_t e = rd<uintptr_t>(schedule + offsetof(S, seasons_end));
        if (!schedule || !b || e <= b || (e - b) % sizeof(Z) || (e - b) / sizeof(Z) > MAX_SEASONS)
            return false;
        for (uintptr_t season = b; season < e; season += sizeof(Z)) {
            uintptr_t db = rd<uintptr_t>(season + offsetof(Z, driver_calendars_begin));
            uintptr_t de = rd<uintptr_t>(season + offsetof(Z, driver_calendars_end));
            if (db && de > db && calendar >= db && calendar < de && (calendar - db) % sizeof(C) == 0)
                return mem::read(calendar + offsetof(C, driver), driver);
        }
        return false;
    } else {
        (void)calendar, (void)driver;
        return false;
    }
}

void probe_add_activity(game_points::ce::AddActivity &a)
{
    a.type = activity_type(a.activity);
    uint32_t driver = 0;
    if (!driver_calendar(reinterpret_cast<uintptr_t>(a.calendar), driver))
        return;
    a.driver = driver;
    SwoftyCareerState s{};
    const SwoftyCareer *career = runtime::career_table();
    if (career && career->state && career->state(&s) == SWOFTY_ASSET_OK && s.has_player && s.humans == 1)
        a.player = ((s.player_driver ^ driver) & 0xffff00ffu) == 0;
}

constexpr uintptr_t CONTRACT_TEAM = 0x00;
constexpr uintptr_t CONTRACT_TYPE = 0x0e;

void probe_contract_sign(game_points::ce::ContractSign &s)
{
    auto c = reinterpret_cast<uintptr_t>(s.contract);
    s.team = rd<uint32_t>(c + CONTRACT_TEAM);
    s.type = rd<uint8_t>(c + CONTRACT_TYPE);
}

// Game_CarSetup_SessionFuelLoad's own first reads: the session source at +0x270 (vtable +0xa8 the
// session, type at +8; +0x98 the weekend, track slot byte at +0x278) and the TRACK_DATA system at
// [+0x278]+0xf58 (records +0x28..+0x30, 16-byte items, the record at +0x08, its name at +0xd0).
constexpr uintptr_t SETUP_SOURCE = 0x270, SETUP_OWNER = 0x278, OWNER_TRACK_DATA = 0xf58;
constexpr uintptr_t VT_SESSION = 0xa8, VT_WEEKEND = 0x98, SESSION_TYPE = 0x08, WEEKEND_TRACK_SLOT = 0x278;
constexpr uintptr_t RECORDS_BEGIN = 0x28, RECORDS_END = 0x30, RECORD_ITEM = 0x10, ITEM_RECORD = 0x08;

using Getter = void *(void *);

uintptr_t call_getter(uintptr_t obj, uintptr_t slot)
{
    uintptr_t vt = rd<uintptr_t>(obj), fn = vt ? rd<uintptr_t>(vt + slot) : 0;
    if (!mem::in_image(vt) || !mem::in_image(fn))
        return 0;
    return reinterpret_cast<uintptr_t>(reinterpret_cast<Getter *>(fn)(reinterpret_cast<void *>(obj)));
}

template <class R = g::TrackDataRecord> std::string fuel_track(void *setup, uint32_t &session_type)
{
    auto s = reinterpret_cast<uintptr_t>(setup);
    uintptr_t source = rd<uintptr_t>(s + SETUP_SOURCE);
    if (!mem::object_like(source))
        return {};
    uintptr_t session = call_getter(source, VT_SESSION);
    session_type = rd<uint32_t>(session ? session + SESSION_TYPE : 0);
    if constexpr (requires(R *r) { r->name; }) {
        uintptr_t weekend = call_getter(source, VT_WEEKEND);
        uintptr_t owner = rd<uintptr_t>(s + SETUP_OWNER);
        uintptr_t system = rd<uintptr_t>(owner ? owner + OWNER_TRACK_DATA : 0);
        uintptr_t b = rd<uintptr_t>(system ? system + RECORDS_BEGIN : 0), e = rd<uintptr_t>(system ? system + RECORDS_END : 0);
        if (!weekend || !b || e <= b || (e - b) % RECORD_ITEM)
            return {};
        uint8_t slot = rd<uint8_t>(weekend + WEEKEND_TRACK_SLOT);
        if (slot >= (e - b) / RECORD_ITEM)
            return {};
        uintptr_t record = rd<uintptr_t>(b + slot * RECORD_ITEM + ITEM_RECORD);
        uintptr_t name = rd<uintptr_t>(record ? record + offsetof(R, name) : 0);
        std::string out;
        if (!name || !mem::c_string(name, out, 64) || out.empty())
            return {};
        return "game/" + out;
    } else {
        return {};
    }
}

}

void declare_game_points(Table &t)
{
    game_points::Probe p;
    p.add_activity = probe_add_activity;
    p.contract_sign = probe_contract_sign;
    p.fuel_track = [](void *setup, uint32_t &type) { return fuel_track(setup, type); };
    Points::declare(t, game_ops(), std::move(p));
}

}
