// SPDX-License-Identifier: MIT
#pragma once
#include <swofty/events.hpp>
#include <swofty/registry.hpp>
#include <swofty/systems/forward.hpp>
#include <swofty/systems/sessions.hpp>

// Grand Prix and Time Trial content: a package is one season build (an entrant list, its teams, cars
// and drivers, and the box's rules); an era box is the tile in the era picker that opens it.

namespace swofty::content {

using Result = swofty::Result;

struct Seat {
    Ref<drivers::Identity> driver;
    int number = 0;
    Ref<drivers::Look> look;
};
SWOFTY_RECORD(Seat, driver, number, look)

struct Team {
    std::string key;
    Ref<teams::Identity> team;
    Ref<cars::Vehicle> vehicle;
    Ref<sounds::EngineSound> engine_sound;
    Ref<liveries::Livery> livery;
    std::vector<Seat> drivers;
};
SWOFTY_RECORD(Team, key, team, vehicle, engine_sound, livery, drivers)

struct Package {
    static constexpr Version version{1, 0};
    Text title;
    std::string entrant_list;
    int season = 0;
    Ref<career::Calendar> calendar;
    Keyed<Team, &Team::key> teams;
    sessions::Rules rules;
    std::string setup_mode = "swofty_f1";
    StatId stat_id = StatId::automatic;
    json::Value extra;
};
SWOFTY_RECORD(Package, title, entrant_list, season, calendar, teams, rules, setup_mode, stat_id)

struct EraBox {
    static constexpr Version version{1, 0};
    Ref<Package> package;
    Text title;
    std::string image;
    int order = 0;
    json::Value extra;
};
SWOFTY_RECORD(EraBox, package, title, image, order)

inline Registry<Package> packages{"content.package", {.applies = Applies::content}};
inline Registry<EraBox> era_boxes{"era.box", {.applies = Applies::read}};

}
