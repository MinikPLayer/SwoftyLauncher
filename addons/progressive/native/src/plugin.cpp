// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career - engine plugin for F1 25.
//
// On every Team HQ (the core's career.hub entered) of a Driver Career it logs the season and the
// seat table (report.cpp); in a Progressive career it applies the season once (season.cpp); then it
// applies data/probe.json once per career (probe.cpp, experiment E1). career.loaded and
// career.unloaded set and clear the per-career state and the team-name overrides. Every game address is a KB binding resolved at load; one
// unresolved binding means the addon registers nothing.

#include <cstring>

#include <swofty/career.hpp>
#include <swofty/events.hpp>
#include <swofty/mixin.hpp>

#include "progressive.hpp"

namespace {

constexpr const char *kVersion = "1.2.2";

void on_career(const SwoftyEvent *ev, void *)
{
    auto *s = static_cast<const SwoftyCareerState *>(ev->data);
    swofty::career::State state{};
    if (s)
        state = *s;
    else
        swofty::career::state(&state);
    if (!std::strcmp(ev->name, SWOFTY_EVENT_CAREER_UNLOADED)) {
        swofty::log("[progressive] career unloaded (it was %s)",
                    swofty::career::name(static_cast<swofty::career::Mode>(ev->a)));
        pg::forget_career();
        pg::season_unloaded();
        return;
    }
    if (!std::strcmp(ev->name, SWOFTY_EVENT_CAREER_LOADED)) {
        swofty::log("[progressive] career loaded: mode %s, career %s",
                    swofty::career::name(static_cast<swofty::career::Mode>(ev->a)), state.key[0] ? state.key : "(no key yet)");
        pg::season_loaded(state);
        if (static_cast<swofty::career::Mode>(ev->a) == swofty::career::Mode::driver_career)
            pg::probe(state);
        return;
    }
    if (ev->a != 1)
        return;
    if (static_cast<swofty::career::Mode>(ev->b) != swofty::career::Mode::driver_career) {
        swofty::log("[progressive] Team HQ in %s: nothing to do outside Driver Career",
                    swofty::career::name(static_cast<swofty::career::Mode>(ev->b)));
        return;
    }
    pg::report("Team HQ", state);
    pg::season_hub(state);
    pg::probe(state);
}

void on_content(const SwoftyEvent *ev, void *)
{
    auto *group = static_cast<const char *>(ev->data);
    if (!group || std::strcmp(group, "f1_career_progressive"))
        pg::wizard_left(group);
}

// A continue route loads an existing save, so a new-career wizard left with Back stays abandoned.
void on_starting(const SwoftyEvent *ev, void *)
{
    if (ev->b == SWOFTY_CAREER_START_CONTINUE)
        pg::wizard_abandoned("a saved career is continued");
}

void subscribe(const char *event)
{
    uint32_t token = 0;
    swofty::events::Result r = swofty::events::subscribe(event, on_career, nullptr, &token);
    swofty::log("[progressive] subscribe %s: %s", event, swofty::events::name(r));
}

} // namespace

SWOFTY_ON_LOAD
{
    swofty::log("[progressive] loaded v%s (season apply, E1 seat probe); core API v%u", kVersion,
                swofty::api ? swofty::api->version : 0u);
    subscribe(SWOFTY_EVENT_CAREER_LOADED);
    subscribe(SWOFTY_EVENT_CAREER_UNLOADED);
    subscribe(SWOFTY_EVENT_CAREER_HUB);
    uint32_t token = 0;
    swofty::events::Result r = swofty::events::subscribe(SWOFTY_EVENT_CAREER_CONTENT_PRESSED, on_content, nullptr, &token);
    swofty::log("[progressive] subscribe %s: %s", SWOFTY_EVENT_CAREER_CONTENT_PRESSED, swofty::events::name(r));
    r = swofty::events::subscribe(SWOFTY_EVENT_CAREER_STARTING, on_starting, nullptr, &token);
    swofty::log("[progressive] subscribe %s: %s", SWOFTY_EVENT_CAREER_STARTING, swofty::events::name(r));
}

SWOFTY_ADDON()
