#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The context facts the apply moments read from the game, as pure mappings the host tests cover.
//
// Mode: the current game mode's row label (game_modes.<ModeChampionship|ModeTimeTrial|GameMode>_N
// _metadata_, held as a char * at +0x278 of the mode object GameModeContext +0x88 points at) maps to
// an events::Mode name. A label not listed is unknown, never grand_prix.
//
// Session: the game's session id (championship::Session +0x08; the enum of docs/std/systems/
// sessions.md, 1..18) maps to a SessionType and the session's own name. A weekend that holds a sprint
// shootout runs its sprint as race id 15 and the grand prix as race_2.
//
// Track: a weekend's track stat id maps to the track record carrying that stat_id, else
// game/<tracks.Track_N label>.

namespace swofty_core::apply::facts {

inline std::string mode_of(std::string_view game_mode)
{
    static const std::pair<std::string_view, std::string_view> exact[] = {
        {"time_trial", "time_trial"},
        {"solo_testing", "time_trial"},
        {"grand_prix_23", "grand_prix"},
        {"grand_prix_splitscreen", "split_screen"},
        {"story_mode", "story"},
        {"story_mode_zenith", "story"},
        {"event_mode", "f1_world"},
        {"championships", "f1_world"},
        {"rewind_event_mode", "f1_world"},
        {"F1CG_solo_battles", "f1_world"},
    };
    for (auto &[label, mode] : exact)
        if (game_mode == label)
            return std::string(mode);
    auto starts = [&](std::string_view p) { return game_mode.substr(0, p.size()) == p; };
    auto ends = [&](std::string_view s) {
        return game_mode.size() >= s.size() && game_mode.substr(game_mode.size() - s.size()) == s;
    };
    if (ends("_online"))
        return "unknown";
    if (starts("my_team_career_"))
        return "my_team";
    if (starts("driver_career_") || (starts("career_") && game_mode.size() == 9))
        return "driver_career";
    return "unknown";
}

struct Session {
    std::string type, session;
};

inline bool sprint_weekend(const std::vector<uint32_t> &ids)
{
    for (uint32_t id : ids)
        if (id >= 10 && id <= 14)
            return true;
    return false;
}

inline Session session_of(uint32_t id, bool sprint)
{
    static const char *const names[] = {"",
                                        "practice_1",
                                        "practice_2",
                                        "practice_3",
                                        "practice_short",
                                        "qualifying_1",
                                        "qualifying_2",
                                        "qualifying_3",
                                        "qualifying_short",
                                        "qualifying_one_shot",
                                        "sprint_shootout_1",
                                        "sprint_shootout_2",
                                        "sprint_shootout_3",
                                        "sprint_shootout_short",
                                        "sprint_shootout_one_shot",
                                        "race",
                                        "race_2",
                                        "race_3",
                                        "time_trial"};
    if (id == 0 || id > 18)
        return {};
    Session s{"", names[id]};
    if (id <= 4)
        s.type = "practice";
    else if (id <= 14)
        s.type = "qualifying";
    else if (id == 15 && sprint)
        s = {"sprint", "sprint"};
    else if (id <= 17)
        s.type = "race";
    else
        s.type = "time_trial";
    return s;
}

struct TrackRecord {
    std::string id;
    int64_t stat_id = -1;
};

inline std::string track_of(int stat_id, const std::string &label, const std::vector<TrackRecord> &records)
{
    if (stat_id < 0 || stat_id >= 0xff)
        return {};
    std::string found;
    for (auto &r : records)
        if (r.stat_id == stat_id && (found.empty() || r.id.rfind("game/", 0) != 0))
            found = r.id;
    if (!found.empty())
        return found;
    return label.empty() ? std::string() : "game/" + label;
}

inline std::string line(const std::string &mode, const std::string &type, const std::string &session,
                        const std::string &track)
{
    std::string s = "[apply] facts: mode " + (mode.empty() ? std::string("unknown") : mode) + ", session " +
                    (type.empty() ? std::string("none") : type);
    if (!session.empty() && session != type)
        s += " (" + session + ")";
    return s + ", track " + (track.empty() ? std::string("none") : track);
}

}
