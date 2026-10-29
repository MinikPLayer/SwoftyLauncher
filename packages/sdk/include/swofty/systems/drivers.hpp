// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <swofty/events.hpp>
#include <swofty/registry.hpp>
#include <swofty/systems/career_ops.hpp>
#include <swofty/systems/forward.hpp>
#include <swofty/systems/wearables.hpp>

// Drivers: real people (core-owned person/ ids), driver identities (one entrants.drivers row each), heads,
// looks (the helmet, suit, gloves, pose and podium emote a driver wears) and stock ratings. A new driver
// names a donor, the stock row whose every key it copies; a variant of a stock driver copies its base's
// row the same way. A person/ id works as a driver reference and resolves to the last enabled driver in
// load order that is that person. The core refuses an identity without a look. What a row holds that this
// header doesn't name is kept in the record's `extra`. set_look calls the core's career operations (table
// version 6) and returns unavailable on a core without it; it refuses an empty look.

namespace swofty::drivers {

using Result = swofty::Result;

enum class BodyType { male, female };
SWOFTY_ENUM(BodyType, male, female)
enum class Stat : int32_t { experience, racecraft, awareness, pace, focus };
SWOFTY_ENUM(Stat, experience, racecraft, awareness, pace, focus)

struct Person {
    static constexpr Version version{1, 0};
    std::string name;
    std::string nationality;
    Ref<Identity> driver;
    json::Value extra;
};
SWOFTY_RECORD(Person, name, nationality, driver)
SWOFTY_RECORD_REGISTRY(Person, "driver.person")

struct Names {
    Text first;
    Text last;
    Text full;
    std::string code;
};
SWOFTY_RECORD(Names, first, last, full, code)

struct Head {
    static constexpr Version version{1, 0};
    Path archive;
    Path morph;
    Ref<Head> rig_from;
    BodyType body = BodyType::male;
    bool customisable = false;
    StatId stat_id = StatId::automatic;
    json::Value extra;
};
SWOFTY_RECORD(Head, archive, morph, rig_from, body, customisable, stat_id)
SWOFTY_RECORD_REGISTRY(Head, "driver.head")

struct Look {
    static constexpr Version version{1, 0};
    Ref<wearables::Helmet> helmet;
    Ref<wearables::Suit> suit;
    Ref<wearables::Gloves> gloves;
    std::string pose;
    std::string podium_emote;
    StatId stat_id = StatId::automatic;
    json::Value extra;
};
SWOFTY_RECORD(Look, helmet, suit, gloves, pose, podium_emote, stat_id)

struct Identity {
    static constexpr Version version{1, 0};
    Ref<Identity> donor;
    Ref<Person> person;
    Names names;
    std::string nationality;
    int number = 0;
    Ref<Head> head;
    Ref<Look> look;
    std::string audio_name;
    int seasons_completed = 0;
    int garage_slot = 0;
    std::vector<std::string> pools;
    StatId stat_id = StatId::automatic;
    json::Value extra;
};
SWOFTY_RECORD(Identity, donor, person, names, nationality, number, head, look, audio_name, seasons_completed,
              garage_slot, pools, stat_id)

struct Curve {
    float start = 0;
    float peak = 0;
};
SWOFTY_RECORD(Curve, start, peak)

struct Stats {
    static constexpr Version version{1, 0};
    Ref<Identity> driver;
    Curve experience, racecraft, awareness, pace, focus;
    json::Value extra;
};
SWOFTY_RECORD(Stats, driver, experience, racecraft, awareness, pace, focus)
SWOFTY_RECORD_REGISTRY(Stats, "driver.stats")

inline Registry<Person> people{"driver.person", {.applies = Applies::boot}};
inline Registry<Identity> identities{"driver.identity", {.applies = Applies::boot}};
inline Registry<Head> heads{"driver.head", {.applies = Applies::boot}};
inline Registry<Look> looks{"driver.look", {.applies = Applies::boot}};
inline Registry<Stats> stats{"driver.stats", {.applies = Applies::boot}};

inline Result set_look(Ref<Identity> driver, Ref<Look> look)
{
    auto t = career::detail::live_ops();
    if (!t || t->version < 6 || !t->set_look)
        return Result::unavailable;
    std::string d(driver.id()), l(look.id());
    if (d.empty() || l.empty())
        return Result::invalid;
    switch (career::detail::ops_result(t->set_look(d.c_str(), l.c_str()))) {
    case career::Result::ok:
        return Result::ok;
    case career::Result::late:
        return Result::late;
    case career::Result::unavailable:
        return Result::unavailable;
    default:
        return Result::invalid;
    }
}

}

namespace swofty::drivers::events {

struct RatingsRead {
    static constexpr const char *point = "driver.ratings.read";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    const char *driver_id;
    Stat stat;
    float value;

    Ref<Identity> driver() const { return swofty::detail::payload_ref<Identity>(driver_id); }
};

struct NumberAssign {
    static constexpr const char *point = "driver.number.assign";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    const char *driver_id;
    int32_t number;

    Ref<Identity> driver() const { return swofty::detail::payload_ref<Identity>(driver_id); }
};

struct HeadResolve {
    static constexpr const char *point = "driver.head.resolve";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    const char *driver_id;
    const char *head_id;
    int32_t (*set_head_fn)(void *user, const char *id);
    void *user;

    Ref<Identity> driver() const { return swofty::detail::payload_ref<Identity>(driver_id); }
    Ref<Head> head() const { return swofty::detail::payload_ref<Head>(head_id); }
    Result set_head(const Ref<Head> &head)
    {
        if (!set_head_fn)
            return Result::unavailable;
        std::string id(head.id());
        return swofty::detail::result(set_head_fn(user, id.c_str()));
    }
};

}
