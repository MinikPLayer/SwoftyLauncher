#include "../../src/core/registry/table.hpp"

#include <swofty/fake_core.hpp>

#include <cstdio>
#include <string>

using namespace swofty;

// Mirrors packages/sdk/include/swofty/systems/career.hpp: the record struct carries `extra` for fields from a
// newer minor, its nested Keyed element struct (SeasonTeam) doesn't. versioning.md's own example adds
// SeasonTeam::reserve in 1.1 and promises a DLL built against 1.0 "never loses it".

namespace v11 {
struct Team {
    int slot = 0;
    std::string name;
    std::string reserve;
};
SWOFTY_RECORD(Team, slot, name, reserve)
struct Season {
    static constexpr Version version{1, 1};
    int year = 0;
    Keyed<Team, &Team::slot> teams;
    json::Value extra;
};
SWOFTY_RECORD(Season, year, teams)
inline Registry<Season> seasons{"test.season"};
}

namespace v10 {
struct Team {
    int slot = 0;
    std::string name;
};
SWOFTY_RECORD(Team, slot, name)
struct Season {
    static constexpr Version version{1, 0};
    int year = 0;
    Keyed<Team, &Team::slot> teams;
    json::Value extra;
};
SWOFTY_RECORD(Season, year, teams)
inline Registry<Season> seasons{"test.season"};
}

int main()
{
    fake::Core core{"a.provider"};
    const SwoftyFakeEngine *engine = swofty_registry_engine();
    engine->begin_addon("a.provider");
    v11::Season s;
    s.year = 2017;
    s.teams = {{1, "mercedes", "game/reserve_one"}, {2, "ferrari", "game/reserve_two"}};
    Ref<v11::Season> id = v11::seasons.add("2017", s);
    if (id.id().empty()) {
        std::printf("FAIL: the 1.1 provider could not add its record: %s\n", last_error().c_str());
        return 1;
    }
    engine->begin_addon("b.editor");
    auto layer = v10::seasons.layer(Ref<v10::Season>(std::string(id.id())), [](v10::Season &v) { v.year = 2018; });
    if (layer.result() != Result::ok) {
        std::printf("FAIL: the 1.0 edit was refused: %s\n", last_error().c_str());
        return 1;
    }
    engine->end_register();
    swofty_core::registry::Record r = swofty_core::registry::store().get("test.season", std::string(id.id()));
    if (r.status != swofty_core::registry::Status::ok) {
        std::printf("FAIL: the record does not read back (status %s)\n", swofty_core::registry::status_name(r.status));
        return 1;
    }
    std::string got = json::dump(r.value);
    std::printf("effective: %s\n", got.c_str());
    int failures = 0;
    if (r.value["year"].as_int() != 2018) {
        std::printf("FAIL: the 1.0 edit did not apply\n");
        failures++;
    }
    for (int i = 0; i < 2; i++)
        if (r.value["teams"][i]["reserve"].as_string().empty()) {
            std::printf("FAIL: teams[%d].reserve, a field from minor 1.1, was dropped by an edit built against 1.0 "
                        "(docs/std/versioning.md: \"never loses it\")\n",
                        i);
            failures++;
        }
    if (!failures)
        std::printf("ok: an edit built against an older minor keeps the newer nested fields\n");
    return failures ? 1 : 0;
}
