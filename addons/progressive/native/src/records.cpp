// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career: years.json with the effective records in it (records.hpp).

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <swofty/hooks.hpp>
#include <swofty/registry.hpp>
#include <swofty/services.hpp>
#include <swofty/storage.hpp>
#include <swofty/systems/career.hpp>

#include "../../api/progressive_api/history.hpp"
#include "../../api/progressive_api/hooks.hpp"
#include "records.hpp"
#include "records_logic.hpp"

namespace pg::records {

namespace {

namespace json = swofty::json;
namespace career = swofty::career;
namespace pr = progressive::records;

constexpr const char *kYearsPath = "data/years.json";

}

bool read_years(std::string &text, const char *who, const career::Season *season)
{
    if (swofty::storage::read(kYearsPath, text) != swofty::storage::Result::ok)
        return false;
    if (!swofty::detail::registry_table()) {
        swofty::log("[progressive] %s: %s as it is (the core has no registry)", who, kYearsPath);
        return true;
    }
    json::Error err;
    json::Value doc = json::parse(text, &err);
    if (!err.ok() || !doc.is_object())
        return true;
    pr::Summary n = pr::overlay(
        doc,
        [season](const std::string &id) {
            if (season && id == pr::season_id(std::to_string(season->year)))
                return std::optional<career::Season>(*season);
            return career::seasons.get(career::seasons[id]);
        },
        [](const std::string &id) { return career::ratings.get(career::ratings[id]); });
    if (!n.seasons && !n.ratings) {
        swofty::log("[progressive] %s: %s as it is (no swofty.progressive career.season or career.driver_ratings "
                    "record in the registry)",
                    who, kYearsPath);
        return true;
    }
    if (n.changed())
        text = json::dump(doc);
    swofty::log("[progressive] %s: %s with the effective records: %d career.season, %d career.driver_ratings; %d "
                "seat(s), %d emptied, %d change(s), %d rating(s) differ from the file",
                who, kYearsPath, n.seasons, n.ratings, n.seats, n.emptied, n.changes, n.ratings_changed);
    for (const std::string &note : n.notes)
        swofty::log("[progressive]   %s", note.c_str());
    return true;
}

int season_apply(int year, std::string &text)
{
    if (!swofty::detail::registry_table() || !swofty::detail::hooks_table())
        return 0;
    std::optional<career::Season> s = career::seasons.get(career::seasons[pr::season_id(std::to_string(year))]);
    if (!s)
        return 0;
    career::Season season = *s;
    const std::string before = swofty::to_json(season);
    progressive_api::SeasonApply p{sizeof p, progressive_api::SeasonApply::version, year, &season};
    if (!swofty::hooks::fire(p)) {
        swofty::log("[progressive] season %d: a %s replace handler applied the season; Progressive leaves its seats "
                    "and cars this session",
                    year, progressive_api::SeasonApply::point);
        return 2;
    }
    if (swofty::to_json(season) == before)
        return 0;
    season.year = year;
    if (!read_years(text, "season apply", &season))
        return 0;
    swofty::log("[progressive] season %d: a %s before handler changed the season; Progressive applies the changed "
                "season this session",
                year, progressive_api::SeasonApply::point);
    return 1;
}

void publish(uint32_t (*real_team)(uint32_t, int), int (*career_year)(void), int (*follow_mode)(void))
{
    static ProgressiveHistory history{};
    history = {sizeof(ProgressiveHistory), real_team, career_year, follow_mode};
    swofty::services::Result s = swofty::services::publish(history);
    swofty::hooks::Result h = swofty::hooks::define<progressive_api::SeasonApply>();
    swofty::log("[progressive] %s v%u: %s; hook point %s: %s", ProgressiveHistory::name, ProgressiveHistory::version,
                swofty::services::name(s), progressive_api::SeasonApply::point, swofty::events::name(h));
}

const char *report_season(int year, int season_index)
{
    return career::name(career::report_season(year, season_index));
}

const SwoftyCareerOps *core_ops()
{
    static const SwoftyCareerOps *ops = [] {
        HMODULE core = GetModuleHandleW(L"swofty_core.dll");
        auto get = core ? reinterpret_cast<SwoftyCoreCareerOpsFn>(GetProcAddress(core, "SwoftyCoreCareerOps")) : nullptr;
        const SwoftyCareerOps *t = get ? get() : nullptr;
        if (t && (t->version < 1 || !t->available || !t->apply_lineup))
            t = nullptr;
        swofty::log("[progressive] season: %s", t ? "the core's career operations (SwoftyCoreCareerOps) apply the seats "
                                                    "and ratings"
                                                  : "the core has no career operations; Progressive applies the seats "
                                                    "and ratings itself");
        return t;
    }();
    return ops && ops->available() ? ops : nullptr;
}

}
