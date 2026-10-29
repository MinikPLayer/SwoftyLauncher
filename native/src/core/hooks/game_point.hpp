#pragma once
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

#include <safetyhook.hpp>

#include "dispatch.hpp"

// A game hook point: the core's detour on one game function, installed while the point has
// subscribers. It yields to a raw overwrite of the same function from any addon (the 1.5 rule: the
// overwrite keeps working, the point reports unavailable and its subscribers don't run), and a point
// installed late (its first subscriber arrived after the window stage) refuses itself when anything
// already sits on the function's first bytes. The detour fills the point's payload and calls run(),
// which fires the chain and calls the game's code through the trampoline unless a replace handler
// handled the call.

namespace swofty_core::hooks {

struct GameOps {
    std::function<void(const std::string &)> log;
    std::function<uintptr_t(const std::string &binding)> resolve;
    std::function<std::string(const std::string &binding, uintptr_t at)> overwritten_by;
    std::function<std::string(uintptr_t at)> occupied_by;
    std::function<void(const std::string &point, const std::string &by)> conflict;
};

class GamePoint final : public GameLink {
public:
    enum class State { idle, live, yielded, failed };

    GamePoint(std::string point, std::string binding, void *detour, GameOps ops)
        : point_(std::move(point)), binding_(std::move(binding)), detour_(detour), ops_(std::move(ops))
    {
    }

    bool install(bool late, const std::string &subscribers) override
    {
        std::lock_guard<std::recursive_mutex> g(lock_);
        if (hook_)
            return true;
        uintptr_t at = ops_.resolve ? ops_.resolve(binding_) : 0;
        if (!at)
            return fail(State::failed, "[hooks] " + point_ + " unavailable: " + binding_ +
                                           " does not resolve on this build; subscribers " + subscribers + " not run");
        std::string by = ops_.overwritten_by ? ops_.overwritten_by(binding_, at) : std::string();
        if (!by.empty()) {
            if (ops_.conflict)
                ops_.conflict(point_, by);
            return fail(State::yielded, "[hooks] " + point_ + " unavailable: " + by + " overwrites " + binding_ +
                                            "; subscribers " + subscribers +
                                            " not run (the launcher lists this as a conflict)");
        }
        if (late) {
            std::string there = ops_.occupied_by ? ops_.occupied_by(at) : std::string();
            if (!there.empty())
                return fail(State::yielded, "[hooks] " + point_ + " unavailable: installed while the game runs, it overlaps " +
                                                there + " on " + binding_ + "; refused; subscribers " + subscribers +
                                                " not run");
        }
        auto h = safetyhook::InlineHook::create(reinterpret_cast<void *>(at), detour_);
        if (!h)
            return fail(State::failed, "[hooks] " + point_ + " unavailable: the detour on " + binding_ +
                                           " failed to install; subscribers " + subscribers + " not run");
        hook_ = std::move(*h);
        state_ = State::live;
        installs_++;
        log("[hooks] " + point_ + " live on " + binding_ + (late ? " (installed late)" : " (window stage)") +
            ": subscribers " + subscribers);
        return true;
    }

    void uninstall() override
    {
        std::lock_guard<std::recursive_mutex> g(lock_);
        if (!hook_ || keep_)
            return;
        hook_.reset();
        state_ = State::idle;
        log("[hooks] " + point_ + " has no subscriber left; the detour on " + binding_ + " is removed");
    }

    bool installed() const override
    {
        std::lock_guard<std::recursive_mutex> g(lock_);
        return bool(hook_);
    }

    State state() const
    {
        std::lock_guard<std::recursive_mutex> g(lock_);
        return state_;
    }

    unsigned installs() const { return installs_; }
    const std::string &point() const { return point_; }

    // A point the core needs even with no subscriber (it observes the game's state): installed at the window
    // stage by the core itself, and kept when the last subscriber leaves.
    void keep(bool on) { keep_ = on; }

    template <class R, class... A> R original(A... args) { return hook_.call<R>(args...); }

    template <class P> bool run(Table &t, P &payload, const std::function<void()> &game)
    {
        int replaced = 0;
        t.fire("", point_, &payload, &replaced, game);
        return replaced != 0;
    }

private:
    bool fail(State s, const std::string &line)
    {
        state_ = s;
        log(line);
        return false;
    }

    void log(const std::string &line)
    {
        if (ops_.log)
            ops_.log(line);
    }

    std::string point_, binding_;
    void *detour_;
    GameOps ops_;
    mutable std::recursive_mutex lock_;
    safetyhook::InlineHook hook_;
    State state_ = State::idle;
    unsigned installs_ = 0;
    bool keep_ = false;
};

}
