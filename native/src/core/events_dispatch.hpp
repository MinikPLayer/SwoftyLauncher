#pragma once
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#include "swofty_api.h"

namespace runtime::ev {

inline const char *const EVENT_NAMES[] = {
    SWOFTY_EVENT_SESSION_START, SWOFTY_EVENT_SESSION_END,     SWOFTY_EVENT_RACE_START,        SWOFTY_EVENT_LAP,
    SWOFTY_EVENT_FLAG,          SWOFTY_EVENT_STATE,           SWOFTY_EVENT_SCREEN,            SWOFTY_EVENT_PAUSE,
    SWOFTY_EVENT_RESUME,        SWOFTY_EVENT_RESULTS,         SWOFTY_EVENT_FLOW_STATE,        SWOFTY_EVENT_CAREER_LOADED,
    SWOFTY_EVENT_CAREER_UNLOADED, SWOFTY_EVENT_CAREER_HUB,    SWOFTY_EVENT_CAREER_STARTING,   SWOFTY_EVENT_CAREER_CONTENT_PRESSED};

inline const char *canonical(const char *name)
{
    if (!name)
        return nullptr;
    for (auto n : EVENT_NAMES)
        if (!std::strcmp(n, name))
            return n;
    return nullptr;
}

// pending until the core's events setup runs, then live when the flow tick is hooked or failed
// when it is not. Registrations are accepted in every phase except on_frame once failed.
enum class Phase { pending, live, failed };

struct Queued {
    uint32_t token;
    uint64_t owner;
    const char *name;
};

struct Sub {
    uint32_t token;
    uint64_t owner;
    const char *name;
    SwoftyEventFn event_fn;
    SwoftyFrameFn frame_fn;
    void *user;
    uint32_t probe;
};

struct NoProfile {
    explicit NoProfile(uint32_t) {}
};

// Holds the addon subscriptions and turns flow-thread hook calls into callbacks. The clock is a
// monotonic seconds value supplied by the caller (the engine hooks read it; the tests inject it),
// so the dt and the "seconds since attach" time are reproducible without the game. Every entry
// point is thread-safe; callbacks run outside the lock so a handler may subscribe or unsubscribe
// from inside another callback without deadlocking. A registration made while pending is held like
// any other and delivered as soon as the hooks call tick() or fire(); queued reports that case.
// Each callback runs inside a Profile scope built from the probe id set with set_probe.
template <class Profile = NoProfile> class Dispatcher {
public:
    int subscribe(uint64_t owner, const char *name, SwoftyEventFn fn, void *user, uint32_t *token,
                  bool *queued = nullptr)
    {
        const char *canon = canonical(name);
        if (!canon || !fn)
            return SWOFTY_ASSET_INVALID;
        std::lock_guard<std::mutex> g(lock_);
        uint32_t t = ++next_;
        subs_.push_back({t, owner, canon, fn, nullptr, user, 0});
        if (token)
            *token = t;
        if (queued)
            *queued = phase_ == Phase::pending;
        return SWOFTY_ASSET_OK;
    }

    int on_frame(uint64_t owner, SwoftyFrameFn fn, void *user, uint32_t *token, bool *queued = nullptr)
    {
        if (!fn)
            return SWOFTY_ASSET_INVALID;
        std::lock_guard<std::mutex> g(lock_);
        if (phase_ == Phase::failed)
            return SWOFTY_ASSET_UNAVAILABLE;
        uint32_t t = ++next_;
        subs_.push_back({t, owner, nullptr, nullptr, fn, user, 0});
        if (token)
            *token = t;
        if (queued)
            *queued = phase_ == Phase::pending;
        return SWOFTY_ASSET_OK;
    }

    // Ends the pending phase: live when the flow tick is hooked, failed when it is not. Returns the
    // registrations made while pending that are still held, in registration order, so the caller
    // can report them. A second call changes nothing and returns nothing.
    std::vector<Queued> arm(bool frame_tick)
    {
        std::lock_guard<std::mutex> g(lock_);
        std::vector<Queued> out;
        if (phase_ != Phase::pending)
            return out;
        phase_ = frame_tick ? Phase::live : Phase::failed;
        for (auto &s : subs_)
            out.push_back({s.token, s.owner, s.name});
        return out;
    }

    Phase phase()
    {
        std::lock_guard<std::mutex> g(lock_);
        return phase_;
    }

    void set_probe(uint32_t token, uint32_t probe)
    {
        std::lock_guard<std::mutex> g(lock_);
        for (auto &s : subs_)
            if (s.token == token)
                s.probe = probe;
    }

    int unsubscribe(uint32_t token)
    {
        std::lock_guard<std::mutex> g(lock_);
        for (size_t i = 0; i < subs_.size(); i++)
            if (subs_[i].token == token) {
                subs_.erase(subs_.begin() + i);
                return SWOFTY_ASSET_OK;
            }
        return SWOFTY_ASSET_INVALID;
    }

    size_t drop_owner(uint64_t owner)
    {
        std::lock_guard<std::mutex> g(lock_);
        size_t before = subs_.size();
        for (size_t i = subs_.size(); i-- > 0;)
            if (subs_[i].owner == owner)
                subs_.erase(subs_.begin() + i);
        return before - subs_.size();
    }

    // Fire a named lifecycle event. now is the current monotonic clock reading in seconds.
    size_t fire(const char *name, int64_t a, int64_t b, const void *data, double now)
    {
        const char *canon = canonical(name);
        if (!canon)
            return 0;
        struct Call {
            SwoftyEventFn fn;
            void *user;
            uint32_t probe;
        };
        std::vector<Call> calls;
        SwoftyEvent ev{};
        {
            std::lock_guard<std::mutex> g(lock_);
            if (origin_ < 0)
                origin_ = now;
            ev.name = canon;
            ev.frame = frame_;
            ev.time = now - origin_;
            ev.a = a;
            ev.b = b;
            ev.data = data;
            for (auto &s : subs_)
                if (s.name == canon)
                    calls.push_back({s.event_fn, s.user, s.probe});
        }
        for (auto &c : calls) {
            Profile p(c.probe);
            c.fn(&ev, c.user);
        }
        return calls.size();
    }

    // Advance one flow-state-machine frame. Returns the dt handed to the frame callbacks.
    double tick(double now)
    {
        struct Call {
            SwoftyFrameFn fn;
            void *user;
            uint32_t probe;
        };
        std::vector<Call> calls;
        double dt;
        {
            std::lock_guard<std::mutex> g(lock_);
            if (origin_ < 0)
                origin_ = now;
            dt = last_ < 0 ? 0.0 : now - last_;
            if (dt < 0)
                dt = 0.0;
            last_ = now;
            ++frame_;
            for (auto &s : subs_)
                if (s.frame_fn)
                    calls.push_back({s.frame_fn, s.user, s.probe});
        }
        for (auto &c : calls) {
            Profile p(c.probe);
            c.fn(dt, c.user);
        }
        return dt;
    }

    size_t count()
    {
        std::lock_guard<std::mutex> g(lock_);
        return subs_.size();
    }

    uint64_t frame()
    {
        std::lock_guard<std::mutex> g(lock_);
        return frame_;
    }

private:
    std::mutex lock_;
    std::vector<Sub> subs_;
    Phase phase_ = Phase::pending;
    uint32_t next_ = 0;
    uint64_t frame_ = 0;
    double last_ = -1;
    double origin_ = -1;
};

}
