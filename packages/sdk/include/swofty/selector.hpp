// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

// Selectors (standard library 1.6): where a layer applies, and where an event or hook handler runs. A
// selector is a value; every selector a layer or handler is given must match. It crosses the C ABI as
// one entry of a JSON selector array (see swofty_api.h).

namespace swofty::events {

enum class Mode { grand_prix, time_trial, driver_career, my_team, split_screen, story, f1_world, unknown };
enum class SessionType { practice, qualifying, race, sprint, time_trial };

class Selector {
public:
    Selector() = default;
    static Selector raw(std::string json) { return Selector(std::move(json)); }
    const std::string &json() const { return json_; }
    bool empty() const { return json_.empty(); }

private:
    explicit Selector(std::string json) : json_(std::move(json)) {}
    std::string json_;
};

namespace detail {

inline std::string quote(std::string_view s)
{
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\')
            out += '\\', out += c;
        else if (static_cast<unsigned char>(c) < 0x20) {
            static const char hex[] = "0123456789abcdef";
            out += "\\u00";
            out += hex[(c >> 4) & 0xf];
            out += hex[c & 0xf];
        } else
            out += c;
    }
    return out + "\"";
}

inline Selector one(std::string_view key, const std::string &value)
{
    return Selector::raw("{" + quote(key) + ":" + value + "}");
}

inline const char *mode_name(Mode m)
{
    static const char *const names[] = {"grand_prix", "time_trial", "driver_career", "my_team",
                                        "split_screen", "story", "f1_world", "unknown"};
    return names[static_cast<int>(m)];
}

inline const char *session_type_name(SessionType t)
{
    static const char *const names[] = {"practice", "qualifying", "race", "sprint", "time_trial"};
    return names[static_cast<int>(t)];
}

inline std::string array(const std::vector<Selector> &all)
{
    std::string out = "[";
    for (const auto &s : all) {
        if (s.empty())
            continue;
        if (out.size() > 1)
            out += ",";
        out += s.json();
    }
    return out + "]";
}

}

inline Selector content(std::string_view id) { return detail::one("content", detail::quote(id)); }
inline Selector content_exact(std::string_view id) { return detail::one("content_exact", detail::quote(id)); }
inline Selector owner(std::string_view addon_id) { return detail::one("owner", detail::quote(addon_id)); }
inline Selector data_group(std::string_view name) { return detail::one("data_group", detail::quote(name)); }
inline Selector mode(Mode m) { return detail::one("mode", detail::quote(detail::mode_name(m))); }
inline Selector track(std::string_view id) { return detail::one("track", detail::quote(id)); }
inline Selector setup_mode(std::string_view id) { return detail::one("setup_mode", detail::quote(id)); }
inline Selector vehicle_package(std::string_view id) { return detail::one("vehicle_package", detail::quote(id)); }

inline Selector year(int from, int to)
{
    return detail::one("year", "[" + std::to_string(from) + "," + std::to_string(to) + "]");
}

inline Selector session_type(std::initializer_list<SessionType> types)
{
    std::string list = "[";
    for (SessionType t : types)
        list += (list.size() > 1 ? "," : "") + detail::quote(detail::session_type_name(t));
    return detail::one("session_type", list + "]");
}

inline Selector any(std::initializer_list<Selector> selectors)
{
    return detail::one("any", detail::array(std::vector<Selector>(selectors)));
}

inline Selector not_(const Selector &s) { return detail::one("not", s.empty() ? "{}" : s.json()); }

}
