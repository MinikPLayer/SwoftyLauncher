// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <string>
#include <string_view>

#include <swofty/events.hpp>
#include <swofty/systems/career.hpp>
#include <swofty/systems/forward.hpp>

// Tracks: the operations on the game's track data and the session.fuel.load hook point that ship in 1.6.0.
// The tracks registries come in 1.6.x.
// set_track_fuel and copy_track_fuel call the core's career operations (SwoftyCoreCareerOps version 4) and
// return unavailable on a core without them. The core keeps the bytes it found and puts them back when the
// career unloads, or on restore_track_fuel. Data sets: f1_2025, sprint_f1_2025, f1_2026, sprint_f1_2026.

namespace swofty::tracks {

using Result = swofty::career::Result;

struct Fuel {
    float burn_per_lap = 0;
};

namespace detail {

inline std::string code_of(const Ref<Track> &track)
{
    std::string id(track.id());
    size_t slash = id.find('/');
    return slash == std::string::npos ? id : id.substr(slash + 1);
}

inline std::string text(std::string_view s)
{
    std::string out = "\"";
    for (char c : s)
        if (c != '"' && c != '\\' && static_cast<unsigned char>(c) >= 0x20)
            out += c;
    return out + "\"";
}

inline Result fuel_request(const std::string &request)
{
    auto t = swofty::career::detail::live_ops();
    if (!t || t->version < 4 || !t->set_track_fuel)
        return Result::unavailable;
    char out[2048];
    size_t len = 0;
    int r = t->set_track_fuel({request.data(), request.size()}, out, sizeof out, &len);
    return r == SWOFTY_TOO_SMALL ? Result::ok : swofty::career::detail::ops_result(r);
}

}

// The track's burn per lap in one data set (game/melbourne: the track code after the slash).
inline Result set_track_fuel(Ref<Track> track, const Fuel &fuel, std::string_view data_set = "f1_2026")
{
    return detail::fuel_request("{\"track\":" + detail::text(detail::code_of(track)) + ",\"set\":" + detail::text(data_set) +
                                ",\"burn_per_lap\":" + std::to_string(fuel.burn_per_lap) + "}");
}

// The track's fuel in data_set (burn per lap, race fuel rows, qualifying fuel) from the same track in from;
// track "*" copies every track.
inline Result copy_track_fuel(std::string_view track, std::string_view data_set, std::string_view from, std::string_view why = {})
{
    return detail::fuel_request("{\"track\":" + detail::text(track) + ",\"set\":" + detail::text(data_set) +
                                ",\"from\":" + detail::text(from) + ",\"why\":" + detail::text(why) + "}");
}

inline Result restore_track_fuel(std::string_view why = {})
{
    return detail::fuel_request("{\"restore\":true,\"why\":" + detail::text(why) + "}");
}

}

namespace swofty::tracks::events {

struct FuelLoad {
    static constexpr const char *point = "session.fuel.load";
    static constexpr uint32_t version = 1;
    enum Kind : uint8_t { practice = 0, qualifying = 1, race = 2, other = 3 };
    uint32_t size, payload_version;
    const char *track_id;
    uint32_t session_type;
    float kg;
    uint8_t kind;

    Ref<Track> track() const { return swofty::detail::payload_ref<Track>(track_id); }
};

}
