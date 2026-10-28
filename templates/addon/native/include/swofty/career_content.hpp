// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <swofty/career.hpp>

// Extra career contents: a tile in the Addons row of the career content select, under the stock 2025
// and 2026 tiles, in Driver Career, My Team or both, with its own save slots. Register one from the
// init check of your addon (the function you pass to SWOFTY_ADDON), before the core's mixin commit;
// the core then builds the tile, presses it, routes it into the career wizard, passes its mode
// through the driver-career gate and answers the save-slot predicate for its data group. The data
// side (mode row, entrant list, official entrant lists, championship components) comes from your
// database operations; the core checks it once the database has loaded and turns a content off with
// a log line when something is missing.
//
// data_group(), is() and current_data_group() tell which career a career entity is, by the data
// group its save slot is filed under, so one DLL can act on one career only.
//
// Feature detection: the table is read only when the running swofty_core.dll exports
// SwoftyCoreCareerContent, so an addon built against this header also loads on an older core and
// sees available() false there.

namespace swofty::career {

namespace content {

using swofty::career::Result;

enum class Model : uint32_t { f1_2026 = SWOFTY_CAREER_CONTENT_MODEL_2026, f1_2025 = SWOFTY_CAREER_CONTENT_MODEL_2025 };

// The careers whose content select shows the tile: Driver Career, My Team or both.
enum class Career : uint32_t {
    driver = SWOFTY_CAREER_CONTENT_DRIVER,
    my_team = SWOFTY_CAREER_CONTENT_MY_TEAM,
    both = SWOFTY_CAREER_CONTENT_DRIVER | SWOFTY_CAREER_CONTENT_MY_TEAM,
};

enum class State : uint32_t {
    queued = SWOFTY_CAREER_CONTENT_QUEUED,
    live = SWOFTY_CAREER_CONTENT_LIVE,
    off = SWOFTY_CAREER_CONTENT_OFF,
};

using Info = SwoftyCareerContentInfo;

inline constexpr const char *pressed_event = SWOFTY_EVENT_CAREER_CONTENT_PRESSED;

// One content. id names it in logs and info(); title and description are lng_* keys for the tile;
// data_group is the save-slot key (the entrant list named by the championship's
// ComCareerSetup.entrant_list); suffix is what the driver select formats F1CAREER_%s, F2CAREER_%s
// and ICONS_%s with; mode is the save name (save_data.name) of its game_modes.ModeChampionship row,
// whose statId the core reads at runtime. model is the stock tile it copies (route, menu systems,
// vtable, press fallback). car_group is the customisation car group stat_id its press selects, -1
// for the model tile's pick. badge is the tile's notification value (0 none). entitlement replaces
// the 2026 model's entitlement string (empty keeps the model's). careers picks the content selects
// that show the tile, in the Addons row under the stock tiles.
struct Content {
    std::string_view id, title, description, data_group, suffix, mode;
    Model model = Model::f1_2026;
    int32_t car_group = -1;
    uint32_t badge = 0;
    std::string_view entitlement = {};
    Career careers = Career::driver;
};

namespace detail {

inline const SwoftyCareerContent *table()
{
    if (!api || api->version < 6)
        return nullptr;
    static std::atomic<const SwoftyCareerContent *> found{nullptr};
    if (auto t = found.load())
        return t;
    HMODULE core = GetModuleHandleW(L"swofty_core.dll");
    if (!core || !GetProcAddress(core, "SwoftyCoreCareerContent") || !api->career_content)
        return nullptr;
    found = api->career_content;
    return api->career_content;
}

}

// The core implements contents and the career feature is on.
inline bool available()
{
    auto t = detail::table();
    return t && t->available();
}

// Register a content. ok: queued for the next mixin commit (or live at once when the hooks are
// already installed); invalid: a bad or duplicate descriptor (the log says which field);
// unavailable: an older core, the career feature off, or the hooks could not be installed.
inline Result add(const Content &c)
{
    auto t = detail::table();
    if (!t)
        return Result::unavailable;
    std::string id(c.id), title(c.title), description(c.description), group(c.data_group), suffix(c.suffix),
        mode(c.mode), entitlement(c.entitlement);
    SwoftyCareerContentDesc d{sizeof d,
                              static_cast<uint32_t>(c.model),
                              id.c_str(),
                              title.c_str(),
                              description.c_str(),
                              group.c_str(),
                              suffix.c_str(),
                              mode.c_str(),
                              entitlement.c_str(),
                              c.car_group,
                              c.badge,
                              static_cast<uint32_t>(c.careers)};
    return swofty::career::detail::result(t->add(&d));
}

// State of a registered content: queued, live or off (with why), its resolved mode statId and
// data group index, and how many tiles and presses it has had.
inline Result info(std::string_view id, Info *out)
{
    auto t = detail::table();
    if (!t)
        return Result::unavailable;
    return swofty::career::detail::result(t->info(std::string(id).c_str(), out));
}

inline State state(const Info &i)
{
    return static_cast<State>(i.state);
}

// Data group of the last career content tile pressed (a stock one too, e.g. f1_career_2026). late
// before the first press; unavailable while no content hooks are installed.
inline Result pressed(std::string *data_group)
{
    auto t = detail::table();
    if (!t)
        return Result::unavailable;
    char buf[64];
    Result r = swofty::career::detail::result(t->pressed(buf, sizeof buf));
    if (data_group)
        *data_group = r == Result::ok ? buf : "";
    return r;
}

inline bool pressed_is(std::string_view data_group)
{
    std::string g;
    return pressed(&g) == Result::ok && g == data_group;
}

}

// Data group of a career: the career entity (entity()) or the career a save-slot predicate is asked
// about; nullptr for the loaded career. late when no career is loaded, invalid when the career has
// no CareerSetupComponent with a named data group. Calls into the game: use it on the game thread.
inline Result data_group(std::string *out, const void *career = nullptr)
{
    auto t = content::detail::table();
    if (out)
        out->clear();
    if (!t)
        return Result::unavailable;
    char buf[64];
    Result r = detail::result(t->data_group(career, buf, sizeof buf));
    if (out && r == Result::ok)
        *out = buf;
    return r;
}

inline std::string current_data_group()
{
    std::string g;
    data_group(&g);
    return g;
}

// The career (the loaded one by default) is filed under data_group.
inline bool is(std::string_view data_group_name, const void *career = nullptr)
{
    std::string g;
    return data_group(&g, career) == Result::ok && g == data_group_name;
}

}
