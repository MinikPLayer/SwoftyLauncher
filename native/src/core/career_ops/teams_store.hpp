#pragma once
#include <string>
#include <vector>

#include <swofty/json.hpp>

#include "../registry/store.hpp"
#include "teams_plan.hpp"

// The career.season records of one data group as the planner's input (docs/std/systems/career.md): the
// effective records for the data group's context, the teams list read slot by slot, a team identity as its id.

namespace career_ops::teams {

inline SlotIn slot_of(const swofty::json::Value &t)
{
    SlotIn s;
    s.slot = static_cast<int>(t["slot"].as_int());
    s.team = std::string(t["team"].as_string());
    for (size_t k = 0; k < static_cast<size_t>(kSeats); ++k)
        s.seats[k] = std::string(t["seats"][k].as_string());
    s.car = t["car"].is_int() ? static_cast<int>(t["car"].as_int()) : -1;
    return s;
}

inline SeasonIn season_of(const swofty::json::Value &v)
{
    SeasonIn s;
    s.year = static_cast<int>(v["year"].as_int());
    if (const auto *a = v["teams"].if_array())
        for (auto &t : *a)
            s.slots.push_back(slot_of(t));
    return s;
}

inline std::vector<SeasonIn> seasons_of(swofty_core::registry::Store &store, const std::string &data_group)
{
    namespace reg = swofty_core::registry;
    std::vector<SeasonIn> out;
    if (data_group.empty())
        return out;
    reg::Context ctx = store.context();
    ctx.data_group = data_group;
    for (auto &id : store.list("career.season", "")) {
        reg::Record r = store.get_live("career.season", id, ctx);
        if (r.status != reg::Status::ok || r.value["data_group"].as_string() != data_group)
            continue;
        out.push_back(season_of(r.value));
    }
    return out;
}

} // namespace career_ops::teams
