#pragma once
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

#include "dispatch.hpp"
#include "game_point.hpp"

// A game hook point on one slot of a class's vtable: the core writes its dispatcher into the slot while the
// point has subscribers and puts the game's function back when the last one leaves. Only the original table
// is patched: an addon that cloned the table for its own objects (the 1.5 way) keeps calling the game's code
// directly. Like GamePoint it yields to a raw overwrite: a slot that no longer points into the game's image,
// or that a mixin overwrites, leaves the point unavailable and its subscribers unrun.

namespace swofty_core::hooks {

struct VtableOps {
    GameOps game;
    std::function<bool(const void *code)> in_image;
    std::function<bool(void **slot, void *value)> write_slot;
};

class VtablePoint final : public GameLink {
public:
    enum class State { idle, live, yielded, failed };

    VtablePoint(std::string point, std::string table_binding, size_t slot, void *detour, VtableOps ops)
        : point_(std::move(point)), binding_(std::move(table_binding)), slot_(slot), detour_(detour), ops_(std::move(ops))
    {
    }

    bool install(bool late, const std::string &subscribers) override
    {
        std::lock_guard<std::recursive_mutex> g(lock_);
        if (live_)
            return true;
        uintptr_t table = ops_.game.resolve ? ops_.game.resolve(binding_) : 0;
        if (!table)
            return fail(State::failed, "[hooks] " + point_ + " unavailable: " + binding_ +
                                           " does not resolve on this build; subscribers " + subscribers + " not run");
        void **slot = reinterpret_cast<void **>(table) + slot_;
        std::string by = ops_.game.overwritten_by ? ops_.game.overwritten_by(binding_, reinterpret_cast<uintptr_t>(slot)) : "";
        if (!by.empty()) {
            if (ops_.game.conflict)
                ops_.game.conflict(point_, by);
            return fail(State::yielded, "[hooks] " + point_ + " unavailable: " + by + " overwrites " + binding_ +
                                            "; subscribers " + subscribers +
                                            " not run (the launcher lists this as a conflict)");
        }
        void *current = *slot;
        if (ops_.in_image && !ops_.in_image(current))
            return fail(State::yielded, "[hooks] " + point_ + " unavailable: slot " + std::to_string(slot_) + " of " +
                                            binding_ + " no longer points into the game's code (something else wrote it); "
                                            "subscribers " + subscribers + " not run");
        if (!ops_.write_slot || !ops_.write_slot(slot, detour_))
            return fail(State::failed, "[hooks] " + point_ + " unavailable: slot " + std::to_string(slot_) + " of " +
                                           binding_ + " could not be written; subscribers " + subscribers + " not run");
        slot_addr_ = slot;
        original_ = current;
        live_ = true;
        state_ = State::live;
        installs_++;
        log("[hooks] " + point_ + " live on slot " + std::to_string(slot_) + " of " + binding_ + (late ? " (installed late)" : " (window stage)") +
            ": subscribers " + subscribers);
        return true;
    }

    void uninstall() override
    {
        std::lock_guard<std::recursive_mutex> g(lock_);
        if (!live_)
            return;
        if (*slot_addr_ == detour_ && ops_.write_slot)
            ops_.write_slot(slot_addr_, original_);
        live_ = false;
        state_ = State::idle;
        log("[hooks] " + point_ + " has no subscriber left; slot " + std::to_string(slot_) + " of " + binding_ +
            " is put back");
    }

    bool installed() const override
    {
        std::lock_guard<std::recursive_mutex> g(lock_);
        return live_;
    }

    State state() const
    {
        std::lock_guard<std::recursive_mutex> g(lock_);
        return state_;
    }

    void *original() const { return original_; }
    unsigned installs() const { return installs_; }

private:
    bool fail(State s, const std::string &line)
    {
        state_ = s;
        log(line);
        return false;
    }

    void log(const std::string &line)
    {
        if (ops_.game.log)
            ops_.game.log(line);
    }

    std::string point_, binding_;
    size_t slot_;
    void *detour_;
    VtableOps ops_;
    mutable std::recursive_mutex lock_;
    void **slot_addr_ = nullptr;
    void *original_ = nullptr;
    bool live_ = false;
    State state_ = State::idle;
    unsigned installs_ = 0;
};

}
