// SPDX-License-Identifier: MIT
#pragma once
#include <swofty_api.h>
#include <atomic>
#include <string>
#include <swofty/career.hpp>
#include <swofty/events.hpp>
#include <swofty/registry.hpp>
#include <swofty/systems/ops.hpp>
#include <swofty/systems/forward.hpp>

// The core's career operations table (swofty_api.h SwoftyCareerOps, the core export SwoftyCoreCareerOps) as the
// system headers reach it: the table, the result mapping and a driver reference's stat id. swofty/systems/career.hpp
// and swofty/systems/drivers.hpp both include it.

namespace swofty::career {

namespace detail {

inline Result ops_result(int r)
{
    return r == SWOFTY_ASSET_OK ? Result::ok
           : r == SWOFTY_ASSET_LATE ? Result::late
           : r == SWOFTY_ASSET_UNAVAILABLE ? Result::unavailable
           : r == SWOFTY_REFUSED ? Result::refused
                                           : Result::invalid;
}

inline bool stat_of(const SwoftyCareerOps *t, const Ref<drivers::Identity> &d, uint32_t &out)
{
    std::string id(d.id());
    return !id.empty() && t->driver_stat(id.c_str(), &out) == SWOFTY_ASSET_OK;
}

}

}
