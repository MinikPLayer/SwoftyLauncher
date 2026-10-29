// SPDX-License-Identifier: MIT
#include <array>
#include <charconv>
#include <map>
#include <string>
#include <utility>

#include <swofty/json.hpp>
#include <swofty/registry.hpp>
#include <swofty/storage.hpp>
#include <swofty/systems/career.hpp>

using namespace swofty;

using Driver = Ref<drivers::Identity>;

constexpr std::pair<const char *, float career::DriverRatings::*> kRatings[] = {
    {"experience", &career::DriverRatings::experience},
    {"racecraft", &career::DriverRatings::racecraft},
    {"awareness", &career::DriverRatings::awareness},
    {"pace", &career::DriverRatings::pace},
};

static const json::Object &members(const json::Value &v)
{
    static const json::Object none;
    const json::Object *o = v.if_object();
    return o ? *o : none;
}

static int slot_number(std::string_view key)
{
    int n = 0;
    auto [end, err] = std::from_chars(key.data(), key.data() + key.size(), n);
    return err == std::errc() && end == key.data() + key.size() ? n : 0;
}

SWOFTY_REGISTER
{
    // the page's edits for this profile, put here by its build step
    std::string text;
    if (storage::read("profile/whatif.json", text) != storage::Result::ok)
        return;                                             // nothing edited in this profile
    const json::Value edits = json::parse(text);

    // { "seats": { "2017": { "1": ["game/fernando_alonso", "game/valtteri_bottas"] } } }
    for (const auto &[year, slots] : members(edits["seats"])) {
        std::map<int, std::array<Driver, 2>> grid;
        for (const auto &[slot, seats] : members(slots))
            if (int n = slot_number(slot))
                grid[n] = {Driver(seats[0].as_string()), Driver(seats[1].as_string())};

        career::seasons.layer(career::seasons["swofty.progressive/" + year], [grid](career::Season &s) {
            for (const auto &[slot, seats] : grid)
                if (career::SeasonTeam *team = s.teams.find(slot))   // a slot Progressive doesn't have is skipped
                    for (int seat = 0; seat < 2; ++seat)
                        if (!seats[seat].empty())
                            team->seats[seat] = seats[seat];
        }, Scope::profile);
    }

    // { "ratings": { "2017": { "game/fernando_alonso": { "pace": 96 } } } }
    for (const auto &[year, drivers] : members(edits["ratings"]))
        for (const auto &[driver, fields] : members(drivers))
            career::ratings.layer(career::ratings["swofty.progressive/" + year + "/" + driver],
                                  [fields](career::DriverRatings &r) {
                for (const auto &[name, field] : kRatings)
                    if (const json::Value *v = fields.find(name); v && v->is_number())
                        r.*field = static_cast<float>(v->as_double());
            }, Scope::profile);
}

SWOFTY_ADDON()
