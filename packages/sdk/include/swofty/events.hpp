// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <swofty/mixin.hpp>
#if __has_include(<swofty/record.hpp>) && __has_include(<swofty/json.hpp>)
#define SWOFTY_EVENTS_TYPED 1
#include <swofty/record.hpp>
#include <swofty/selector.hpp>
#include <swofty/systems/forward.hpp>
#if defined(_WIN32)
#include <windows.h>
#endif
#include <atomic>
#include <span>
#endif

// Game lifecycle events and a per-frame tick. The core fires lifecycle events from the
// flow-state machine and session hooks, and the frame tick once per rendered frame. Every
// callback runs on the game's flow thread, so handlers must return quickly and may be
// re-entered across frames. Subscribe at any time; a subscription lasts until unsubscribe.

namespace swofty::events {

enum class Result { ok, late, unavailable, invalid };

using Event = SwoftyEvent;

namespace detail {

inline const SwoftyEvents *table()
{
    return api && api->version >= 5 ? api->events : nullptr;
}

inline Result result(int r)
{
    return r == SWOFTY_ASSET_OK ? Result::ok
           : r == SWOFTY_ASSET_LATE ? Result::late
           : r == SWOFTY_ASSET_UNAVAILABLE ? Result::unavailable
                                           : Result::invalid;
}

}

// The events library takes registrations: this core implements it, the feature is on, and the
// core's setup has not found the frame tick missing on this build. Registration is accepted at any
// time from SwoftyAddonInit on: a subscribe or on_frame made before the core's events setup (in
// SwoftyAddonInit or a SWOFTY_ON_LOAD of the first load) returns ok, is queued, and fires once the
// setup completes. If the setup then finds no frame tick, the core log names each queued on_frame.
inline bool available()
{
    auto t = detail::table();
    return t && t->available();
}

// Subscribe to a named lifecycle event (SWOFTY_EVENT_*). The token identifies the
// subscription for unsubscribe; pass nullptr to ignore it.
inline Result subscribe(std::string_view event, SwoftyEventFn fn, void *user = nullptr, uint32_t *token = nullptr)
{
    auto t = detail::table();
    if (!t)
        return Result::unavailable;
    return detail::result(t->subscribe(std::string(event).c_str(), fn, user, token));
}

// Run a callback once per rendered frame with the seconds elapsed since the previous frame.
// unavailable once the core's setup found no frame tick on this build.
inline Result on_frame(SwoftyFrameFn fn, void *user = nullptr, uint32_t *token = nullptr)
{
    auto t = detail::table();
    if (!t)
        return Result::unavailable;
    return detail::result(t->on_frame(fn, user, token));
}

// Drop a subscription or frame callback by its token.
inline Result unsubscribe(uint32_t token)
{
    auto t = detail::table();
    if (!t)
        return Result::unavailable;
    return detail::result(t->unsubscribe(token));
}

inline const char *name(Result r)
{
    return r == Result::ok ? "ok" : r == Result::late ? "late" : r == Result::unavailable ? "unavailable" : "invalid";
}

}

#if defined(SWOFTY_EVENTS_TYPED)

// Standard library 1.6: typed events with a context and scopes. An event is a hook point: on<E>() attaches
// a before, replace or after handler to E::point, and the core calls it only where every selector given
// to in() and when() matches. The handler gets the payload and the moment's Context. On a core without
// the hooks table every call returns unavailable and fire() returns true (nothing replaced it). A payload
// crosses between the core and addons built with other compilers, so its fields are C types only: record
// ids and text are const char * (valid for the handler call), callbacks are C function pointers with a
// user pointer, and the C++ forms (Ref<T>, std::string_view, Result) are member functions.

namespace swofty::detail {

template <class T> Ref<T> payload_ref(const char *id) { return id && *id ? Ref<T>(std::string_view(id)) : Ref<T>(); }
inline std::string_view payload_str(const char *s) { return s ? std::string_view(s) : std::string_view(); }

inline const SwoftyHooks *hooks_table()
{
    if (!api || api->version < 9 || !api->hooks)
        return nullptr;
#if defined(_WIN32)
    static std::atomic<int> exported{-1};
    if (exported.load() < 0) {
        HMODULE core = GetModuleHandleW(L"swofty_core.dll");
        exported = core && GetProcAddress(core, "SwoftyCoreHooks") ? 1 : 0;
    }
    if (!exported.load())
        return nullptr;
#endif
    return api->hooks->available && api->hooks->available() ? api->hooks : nullptr;
}

}

namespace swofty::events {

struct Entrant {
    uint32_t car = 0;
    Ref<drivers::Identity> driver;
    Ref<teams::Identity> team;
    Ref<cars::Vehicle> vehicle;
    bool human = false;
};
SWOFTY_RECORD(Entrant, car, driver, team, vehicle, human)

struct ContentView {
    std::string id;
    std::string registry;
    std::string owner;
    std::vector<std::string> chain;
    std::vector<std::string> owners;
};
SWOFTY_RECORD(ContentView, id, registry, owner, chain, owners)

struct CareerView {
    std::string id;
    std::string data_group;
    int year = 0;
    int season = 0;
    int round = 0;
    int rounds = 0;
    int day = 0;
};
SWOFTY_RECORD(CareerView, id, data_group, year, season, round, rounds, day)

struct WeekendView {
    int round = 0;
    Ref<tracks::Track> track;
};
SWOFTY_RECORD(WeekendView, round, track)

struct SessionView {
    SessionType type = SessionType::practice;
    std::string session;
    int group = 0;
    Ref<tracks::Track> track;
    int weather = 0;
    int laps = 0;
    std::string setup_mode;
};
SWOFTY_RECORD(SessionView, type, session, group, track, weather, laps, setup_mode)

SWOFTY_ENUM(Mode, grand_prix, time_trial, driver_career, my_team, split_screen, story, f1_world, unknown)
SWOFTY_ENUM(SessionType, practice, qualifying, race, sprint, time_trial)

class Context {
public:
    Context() = default;
    explicit Context(const void *handle) : handle_(handle) {}

    Mode mode() const
    {
        Mode m = Mode::unknown;
        std::string s = part("mode");
        from_json(s, m);
        return m;
    }
    const ContentView &content() const { return cached(content_, "content"); }
    const std::optional<CareerView> &career() const { return cached(career_, "career"); }
    const std::optional<WeekendView> &weekend() const { return cached(weekend_, "weekend"); }
    const std::optional<SessionView> &session() const { return cached(session_, "session"); }
    const std::optional<Entrant> &player() const { return cached(player_, "player"); }
    std::span<const Entrant> participants() const
    {
        if (!participants_) {
            participants_.emplace();
            from_json(part("participants"), *participants_);
        }
        return *participants_;
    }
    bool in(const Selector &s) const
    {
        auto t = swofty::detail::hooks_table();
        int out = 0;
        std::string json = detail::array({s});
        return t && t->context_in && t->context_in(handle_, swofty::detail::text(json), &out) == SWOFTY_ASSET_OK && out;
    }
    template <class T> std::optional<T> record() const
    {
        std::string s = part(std::string("record:") + swofty::detail::registry_name<T>());
        T value{};
        if (s.empty() || s == "null" || !from_json(s, value))
            return std::nullopt;
        return value;
    }
    const void *handle() const { return handle_; }

private:
    std::string part(std::string_view name) const
    {
        auto t = swofty::detail::hooks_table();
        std::string out;
        if (!t || !t->context)
            return out;
        std::string n(name);
        if (swofty::detail::fill(out, [&](char *o, size_t cap, size_t *len) { return t->context(handle_, n.c_str(), o, cap, len); }) !=
            SWOFTY_ASSET_OK)
            out.clear();
        return out;
    }
    template <class V> const V &cached(std::optional<V> &slot, const char *name) const
    {
        if (!slot) {
            slot.emplace();
            from_json(part(name), *slot);
        }
        return *slot;
    }
    template <class V> const std::optional<V> &cached(std::optional<std::optional<V>> &slot, const char *name) const
    {
        if (!slot) {
            slot.emplace();
            std::string s = part(name);
            V v{};
            if (!s.empty() && s != "null" && from_json(s, v))
                *slot = std::move(v);
        }
        return *slot;
    }

    const void *handle_ = nullptr;
    mutable std::optional<ContentView> content_;
    mutable std::optional<std::optional<CareerView>> career_;
    mutable std::optional<std::optional<WeekendView>> weekend_;
    mutable std::optional<std::optional<SessionView>> session_;
    mutable std::optional<std::optional<Entrant>> player_;
    mutable std::optional<std::vector<Entrant>> participants_;
};

namespace detail {

inline Result result16(int r) { return events::detail::result(r == SWOFTY_TOO_SMALL ? SWOFTY_ASSET_INVALID : r); }

template <class F> struct Handler {
    F fn;
};

inline Result attach(const char *point, uint32_t phase, int32_t priority, const std::vector<Selector> &scope,
                     SwoftyHookFn fn, void *user, uint32_t *token)
{
    auto t = swofty::detail::hooks_table();
    if (!t || !t->attach_in)
        return Result::unavailable;
    std::string json = array(scope);
    return result16(t->attach_in(point, phase, priority, swofty::detail::text(json), fn, user, token));
}

}

template <class E> class On {
public:
    On &in(Selector s)
    {
        scope_.push_back(std::move(s));
        return *this;
    }
    On &when(Selector s)
    {
        scope_.push_back(std::move(s));
        return *this;
    }
    On &priority(int32_t p)
    {
        priority_ = p;
        return *this;
    }

    Result before(std::function<void(E &, const Context &)> fn, uint32_t *token = nullptr)
    {
        auto *h = new std::function<void(E &, const Context &)>(std::move(fn));
        return detail::attach(E::point, SWOFTY_PHASE_BEFORE, priority_, scope_, &before_entry, h, token);
    }
    Result replace(std::function<bool(E &, const Context &)> fn, uint32_t *token = nullptr)
    {
        auto *h = new std::function<bool(E &, const Context &)>(std::move(fn));
        return detail::attach(E::point, SWOFTY_PHASE_REPLACE, priority_, scope_, &replace_entry, h, token);
    }
    Result after(std::function<void(const E &, const Context &)> fn, uint32_t *token = nullptr)
    {
        auto *h = new std::function<void(const E &, const Context &)>(std::move(fn));
        return detail::attach(E::point, SWOFTY_PHASE_AFTER, priority_, scope_, &after_entry, h, token);
    }

private:
    static int before_entry(void *user, SwoftyHookCall *call)
    {
        return swofty::detail::guarded_entry([&] {
            (*static_cast<std::function<void(E &, const Context &)> *>(user))(*static_cast<E *>(call->payload),
                                                                             Context(call->context));
            return 0;
        });
    }
    static int replace_entry(void *user, SwoftyHookCall *call)
    {
        return swofty::detail::guarded_entry([&] {
            return (*static_cast<std::function<bool(E &, const Context &)> *>(user))(*static_cast<E *>(call->payload),
                                                                                     Context(call->context))
                       ? 1
                       : 0;
        });
    }
    static int after_entry(void *user, SwoftyHookCall *call)
    {
        return swofty::detail::guarded_entry([&] {
            (*static_cast<std::function<void(const E &, const Context &)> *>(user))(
                *static_cast<const E *>(call->payload), Context(call->context));
            return 0;
        });
    }

    std::vector<Selector> scope_;
    int32_t priority_ = 0;
};

template <class E> On<E> on() { return On<E>(); }

class OnName {
public:
    explicit OnName(std::string_view point) : point_(point) {}
    OnName &in(Selector s)
    {
        scope_.push_back(std::move(s));
        return *this;
    }
    OnName &when(Selector s)
    {
        scope_.push_back(std::move(s));
        return *this;
    }
    Result after(std::function<void(std::span<const std::byte>, const Context &)> fn, uint32_t *token = nullptr)
    {
        auto *h = new std::function<void(std::span<const std::byte>, const Context &)>(std::move(fn));
        return detail::attach(point_.c_str(), SWOFTY_PHASE_AFTER, 0, scope_, &entry, h, token);
    }

private:
    static int entry(void *user, SwoftyHookCall *call)
    {
        return swofty::detail::guarded_entry([&] {
            uint32_t size = call->payload ? *static_cast<const uint32_t *>(call->payload) : 0;
            (*static_cast<std::function<void(std::span<const std::byte>, const Context &)> *>(user))(
                std::span<const std::byte>(static_cast<const std::byte *>(call->payload), size), Context(call->context));
            return 0;
        });
    }
    std::string point_;
    std::vector<Selector> scope_;
};

inline OnName on(std::string_view point) { return OnName(point); }

inline Result detach(uint32_t token)
{
    auto t = swofty::detail::hooks_table();
    return t && t->detach ? detail::result16(t->detach(token)) : Result::unavailable;
}

template <class E> Result define(Selector scope = {})
{
    auto t = swofty::detail::hooks_table();
    if (!t || !t->define_in)
        return Result::unavailable;
    std::string json = scope.empty() ? std::string("[]") : detail::array({scope});
    return detail::result16(t->define_in(E::point, E::version, SWOFTY_POINT_CANCEL, swofty::detail::text(json)));
}

template <class E> bool fire(E &payload)
{
    auto t = swofty::detail::hooks_table();
    int replaced = 0;
    if (!t || !t->fire || t->fire(E::point, &payload, &replaced) != SWOFTY_ASSET_OK)
        return true;
    return replaced == 0;
}

template <class E> bool fire(const E &payload)
{
    E copy = payload;
    return fire(copy);
}

}

#endif
