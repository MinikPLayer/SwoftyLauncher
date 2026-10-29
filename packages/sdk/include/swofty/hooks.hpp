// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <swofty/record.hpp>
#include <swofty/events.hpp>

// Hook points (standard library 1.6): a named place in the game, or in an addon, where the core runs every
// subscriber in one chain. P is the point's payload struct: static constexpr point and version, then
// uint32_t size and uint32_t payload_version. Phases: before (highest priority first, then load order),
// replace (until one returns true), after (every handler, with the outcome). Every call returns
// unavailable on a core without the hooks table, and fire() then returns true.

namespace swofty::hooks {

using Result = swofty::events::Result;

struct Priority {
    int value = 0;
};

struct Outcome {
    bool replaced = false;
};

namespace detail {

inline Result attach(const char *point, uint32_t phase, int32_t priority, SwoftyHookFn fn, void *user, uint32_t *token)
{
    auto t = swofty::detail::hooks_table();
    if (!t || !t->attach)
        return Result::unavailable;
    return swofty::events::detail::result16(t->attach(point, phase, priority, fn, user, token));
}

template <class P> int before_entry(void *user, SwoftyHookCall *call)
{
    return swofty::detail::guarded_entry([&] {
        (*static_cast<std::function<void(P &)> *>(user))(*static_cast<P *>(call->payload));
        return 0;
    });
}

template <class P> int replace_entry(void *user, SwoftyHookCall *call)
{
    return swofty::detail::guarded_entry(
        [&] { return (*static_cast<std::function<bool(P &)> *>(user))(*static_cast<P *>(call->payload)) ? 1 : 0; });
}

template <class P> int after_entry(void *user, SwoftyHookCall *call)
{
    return swofty::detail::guarded_entry([&] {
        Outcome o{call->replaced != 0};
        (*static_cast<std::function<void(const P &, const Outcome &)> *>(user))(*static_cast<const P *>(call->payload), o);
        return 0;
    });
}

}

template <class P> Result before(Priority priority, std::function<void(P &)> fn, uint32_t *token = nullptr)
{
    return detail::attach(P::point, SWOFTY_PHASE_BEFORE, priority.value, &detail::before_entry<P>,
                          new std::function<void(P &)>(std::move(fn)), token);
}

template <class P> Result before(std::function<void(P &)> fn, uint32_t *token = nullptr)
{
    return before<P>(Priority{}, std::move(fn), token);
}

template <class P> Result replace(Priority priority, std::function<bool(P &)> fn, uint32_t *token = nullptr)
{
    return detail::attach(P::point, SWOFTY_PHASE_REPLACE, priority.value, &detail::replace_entry<P>,
                          new std::function<bool(P &)>(std::move(fn)), token);
}

template <class P> Result replace(std::function<bool(P &)> fn, uint32_t *token = nullptr)
{
    return replace<P>(Priority{}, std::move(fn), token);
}

template <class P>
Result after(Priority priority, std::function<void(const P &, const Outcome &)> fn, uint32_t *token = nullptr)
{
    return detail::attach(P::point, SWOFTY_PHASE_AFTER, priority.value, &detail::after_entry<P>,
                          new std::function<void(const P &, const Outcome &)>(std::move(fn)), token);
}

template <class P> Result after(std::function<void(const P &, const Outcome &)> fn, uint32_t *token = nullptr)
{
    return after<P>(Priority{}, std::move(fn), token);
}

inline Result detach(uint32_t token) { return swofty::events::detach(token); }

template <class P> Result define()
{
    auto t = swofty::detail::hooks_table();
    if (!t || !t->define)
        return Result::unavailable;
    return swofty::events::detail::result16(t->define(P::point, P::version));
}

template <class P> bool fire(P &payload) { return swofty::events::fire(payload); }

template <class P> bool live()
{
    auto t = swofty::detail::hooks_table();
    uint32_t version = 0;
    return t && t->live && t->live(P::point, &version) == SWOFTY_ASSET_OK && version >= P::version;
}

}
