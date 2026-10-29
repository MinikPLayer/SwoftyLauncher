#pragma once
#include <cstdint>
#include <cstring>
#include <string_view>

#include "swofty_api.h"

// The core's own events for standard library 1.6.0: every lifecycle moment of docs/std/events.md and
// every event the 1.6.0 system headers name (career, content, cars, sounds), with its kind, its payload
// type and version, and whether the core fires it in 1.6.0 (game: a game hook point, which fires while its
// detour is installed, from its first subscriber on; game_points.hpp). A hot event is one that can fire every
// frame or per car read: the core logs only its first fire of a session. legacy is the 1.x name the
// same moment already fires under (events.cpp's dispatcher), when there is one.

namespace swofty_core::hooks::catalogue {

enum class Fired : uint8_t { no, yes, after_only, game };

struct Event {
    const char *id;
    uint32_t kind;
    const char *payload;
    uint32_t version;
    bool hot;
    Fired fired;
    const char *legacy;
};

constexpr uint32_t N = SWOFTY_POINT_NOTIFY, M = SWOFTY_POINT_MODIFY, C = SWOFTY_POINT_CANCEL;
constexpr const char *SHELL = "swofty_core::hooks::Shell";

inline constexpr Event EVENTS[] = {
    {"game.boot", N, SHELL, 1, false, Fired::yes, nullptr},
    {"game.ready", N, SHELL, 1, false, Fired::no, nullptr},
    {"flow.state", N, SHELL, 1, true, Fired::yes, SWOFTY_EVENT_FLOW_STATE},
    {"menu.route", C, SHELL, 1, false, Fired::no, nullptr},
    {"menu.screen", N, SHELL, 1, false, Fired::no, SWOFTY_EVENT_SCREEN},
    {"game.pause", N, SHELL, 1, false, Fired::no, SWOFTY_EVENT_PAUSE},
    {"game.resume", N, SHELL, 1, false, Fired::no, SWOFTY_EVENT_RESUME},
    {"game.saving", M, SHELL, 1, false, Fired::no, nullptr},
    {"mode.select", C, SHELL, 1, false, Fired::no, nullptr},
    {"content.select", N, SHELL, 1, false, Fired::yes, SWOFTY_EVENT_CAREER_CONTENT_PRESSED},

    {"career.starting", C, SHELL, 1, false, Fired::after_only, SWOFTY_EVENT_CAREER_STARTING},
    {"career.created", N, "swofty::career::events::Created", 1, false, Fired::no, nullptr},
    {"career.loaded", N, "swofty::career::events::Loaded", 1, false, Fired::yes, SWOFTY_EVENT_CAREER_LOADED},
    {"career.hub", N, SHELL, 1, false, Fired::yes, SWOFTY_EVENT_CAREER_HUB},
    {"career.season.start", N, "swofty::career::events::SeasonStart", 1, false, Fired::yes, nullptr},
    {"career.time.advance", N, SHELL, 1, true, Fired::no, nullptr},
    {"career.day", N, SHELL, 1, true, Fired::no, nullptr},
    {"career.calendar.add_activity", C, "swofty::career::events::AddActivity", 1, false, Fired::game, nullptr},
    {"career.calendar.remove_activity", C, SHELL, 1, false, Fired::no, nullptr},
    {"career.rnd.start", C, SHELL, 1, false, Fired::no, nullptr},
    {"career.rnd.cancel", C, SHELL, 1, false, Fired::no, nullptr},
    {"career.facility.complete", N, SHELL, 1, false, Fired::no, nullptr},
    {"career.weekend.start", N, SHELL, 1, false, Fired::no, nullptr},
    {"career.weekend.end", N, SHELL, 1, false, Fired::no, nullptr},
    {"career.contracts.open", N, SHELL, 1, false, Fired::no, nullptr},
    {"career.contract.decide", C, "swofty::career::events::ContractDecide", 1, false, Fired::game, nullptr},
    {"career.contract.sign", C, "swofty::career::events::ContractSign", 1, false, Fired::game, nullptr},
    {"career.contract.changed", N, SHELL, 1, false, Fired::no, nullptr},
    {"career.driver.moved", N, SHELL, 1, false, Fired::no, nullptr},
    {"career.season.process", C, SHELL, 1, false, Fired::no, nullptr},
    {"career.moves.finalised", N, SHELL, 1, false, Fired::no, nullptr},
    {"career.season.end", N, "swofty::career::events::SeasonEnd", 1, false, Fired::no, nullptr},
    {"career.season.break", N, "swofty::career::events::SeasonBreak", 1, false, Fired::no, nullptr},
    {"career.saving", M, "swofty::career::events::Saving", 1, false, Fired::no, nullptr},
    {"career.unloaded", N, "swofty::career::events::Unloaded", 1, false, Fired::yes, SWOFTY_EVENT_CAREER_UNLOADED},
    {"career.standings.rebuild", M, "swofty::career::events::StandingsRebuild", 1, false, Fired::no, nullptr},

    {"weekend.start", N, SHELL, 1, false, Fired::no, nullptr},
    {"weekend.end", N, SHELL, 1, false, Fired::no, nullptr},

    {"session.load", M, SHELL, 1, false, Fired::no, nullptr},
    {"session.weather.setup", M, SHELL, 1, false, Fired::no, nullptr},
    {"session.fuel.load", C, "swofty::tracks::events::FuelLoad", 1, false, Fired::game, nullptr},
    {"session.start", N, SHELL, 1, false, Fired::yes, SWOFTY_EVENT_SESSION_START},
    {"session.state.enter", N, SHELL, 1, false, Fired::no, SWOFTY_EVENT_STATE},
    {"session.grid", N, SHELL, 1, false, Fired::no, nullptr},
    {"session.formation_lap", N, SHELL, 1, false, Fired::no, nullptr},
    {"race.start", N, SHELL, 1, false, Fired::yes, SWOFTY_EVENT_RACE_START},
    {"session.sector", N, SHELL, 1, true, Fired::no, nullptr},
    {"session.lap", N, SHELL, 1, false, Fired::no, SWOFTY_EVENT_LAP},
    {"session.pit", N, SHELL, 1, false, Fired::no, nullptr},
    {"session.pit_stop", N, SHELL, 1, false, Fired::no, nullptr},
    {"session.flag", N, SHELL, 1, false, Fired::no, SWOFTY_EVENT_FLAG},
    {"session.safety_car.deploy", N, SHELL, 1, false, Fired::no, nullptr},
    {"session.red_flag", N, SHELL, 1, false, Fired::no, nullptr},
    {"session.penalty.issue", C, SHELL, 1, false, Fired::no, nullptr},
    {"session.car.status", N, SHELL, 1, false, Fired::no, nullptr},
    {"session.player.control", C, "swofty::sessions::events::PlayerControl", 1, false, Fired::game, nullptr},
    {"session.finish", N, SHELL, 1, false, Fired::no, nullptr},
    {"session.grid.group_result", C, SHELL, 1, false, Fired::no, nullptr},
    {"session.results.finalise", N, SHELL, 1, false, Fired::no, SWOFTY_EVENT_RESULTS},
    {"session.podium", N, SHELL, 1, false, Fired::no, nullptr},
    {"session.end", N, SHELL, 1, false, Fired::no, SWOFTY_EVENT_SESSION_END},
    {"session.unload", N, SHELL, 1, false, Fired::no, nullptr},

    {"car.upgrades.fit", M, "swofty::cars::events::UpgradesFit", 1, false, Fired::no, nullptr},
    {"car.vtf.read", M, "swofty::cars::events::VtfRead", 1, true, Fired::no, nullptr},
    {"car.built", N, "swofty::cars::events::Built", 1, false, Fired::no, nullptr},
    {"car.reset", N, "swofty::cars::events::Reset", 1, false, Fired::no, nullptr},
    {"car.parts.register", N, "swofty::cars::events::PartsRegister", 1, false, Fired::no, nullptr},
    {"car.controllers.rebuilt", N, "swofty::cars::events::ControllersRebuilt", 1, false, Fired::game, nullptr},

    {"ai.controller.update", C, "swofty::ai::events::ControllerUpdate", 1, true, Fired::game, nullptr},
    {"hud.spectator.visible", M, "swofty::spectator::events::HudVisible", 1, false, Fired::game, nullptr},

    {"audio.bundle.resolve", M, "swofty::sounds::events::BundleResolve", 1, false, Fired::no, nullptr},
};

inline const Event *find(std::string_view id)
{
    for (auto &e : EVENTS)
        if (id == e.id)
            return &e;
    return nullptr;
}

inline const Event *by_legacy(const char *name)
{
    if (!name)
        return nullptr;
    for (auto &e : EVENTS)
        if (e.legacy && !std::strcmp(e.legacy, name))
            return &e;
    return nullptr;
}

inline const char *kind_name(uint32_t kind)
{
    return kind == SWOFTY_POINT_NOTIFY ? "notify" : kind == SWOFTY_POINT_MODIFY ? "modify" : "cancel";
}

inline const char *phase_name(uint32_t phase)
{
    return phase == SWOFTY_PHASE_BEFORE ? "before" : phase == SWOFTY_PHASE_REPLACE ? "replace" : "after";
}

inline bool has_phase(uint32_t kind, uint32_t phase)
{
    return phase == SWOFTY_PHASE_AFTER || (phase == SWOFTY_PHASE_BEFORE && kind != SWOFTY_POINT_NOTIFY) ||
           (phase == SWOFTY_PHASE_REPLACE && kind == SWOFTY_POINT_CANCEL);
}

}
