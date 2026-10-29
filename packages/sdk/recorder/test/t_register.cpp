// The recorder test's addon: adds, a variant, layers, an event subscription, a hook point and services in
// SWOFTY_REGISTER, and an init the recorder must never call.
#include <cstdlib>

#include <swofty/events.hpp>
#include <swofty/hooks.hpp>
#include <swofty/registry.hpp>
#include <swofty/services.hpp>
#include <swofty/systems/career.hpp>
#include <swofty/systems/cars.hpp>
#include <swofty/systems/sounds.hpp>

using namespace swofty;

struct HistoryApi {
    static constexpr const char *name = "t.history";
    static constexpr uint32_t version = 2;
    uint32_t size;
    int (*seasons)(void);
};

struct SeasonApply {
    static constexpr const char *point = "t.recorder.season.apply";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    int year;
};

struct OtherApi {
    static constexpr const char *name = "other.api";
    static constexpr uint32_t version = 1;
    uint32_t size;
};

static int seasons() { return 10; }
static const HistoryApi history{sizeof(HistoryApi), &seasons};

SWOFTY_REGISTER
{
    auto v10 = sounds::engine_sounds.add("v10", {
                                                    .bundle = file("sounds/v10.bdl"),
                                                    .settings_from = sounds::engine_sounds["game/ferrari_2025"],
                                                });
    cars::vehicles.add("ferrari_v10", {
                                          .label = "ferrari_v10",
                                          .donor = cars::vehicles["swofty.classic-seasons-2017/ferrari"],
                                          .engine_sound = v10,
                                          .livery = cars::vehicles.name().empty() ? Ref<liveries::Livery>() : Ref<liveries::Livery>("mod.liveries/red").optional(),
                                      });
    sounds::engine_sounds.add("Not A Key/x", {.bundle = file("sounds/refused.bdl")});
    career::modes.variant("v10_career", career::modes["game/f1_career_2026"], [](career::ModeRecord &m) {
        m.data_group = "f1_career_v10";
        m.title = Text("lng_v10_title");
    });
    cars::vehicles
        .layer(cars::vehicles["swofty.classic-seasons-2017/ferrari"], [=](cars::Vehicle &v) { v.engine_sound = v10; },
               Scope::profile)
        .in(events::mode(events::Mode::time_trial));
    cars::vehicles.layer(under<cars::Vehicle>("game/").except({cars::vehicles["game/safety_car"]}),
                         [](cars::Vehicle &v) {
                             if (option<std::string>("wear") == "high")
                                 v.tyre_wear_scale = 1.2f;
                         });
    events::on<career::events::SeasonStart>()
        .in(events::content("swofty.progressive/f1_career_progressive"))
        .after([](const career::events::SeasonStart &, const events::Context &) {});
    hooks::define<SeasonApply>();
    services::publish(history);
    services::when<OtherApi>([](const OtherApi &) {});
}

SWOFTY_ON_LOAD
{
    std::abort();
}

SWOFTY_ADDON()
