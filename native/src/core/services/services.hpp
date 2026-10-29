#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "swofty_api.h"

// Services (standard library 1.6): versioned function tables one addon publishes under a name, and
// waiters that run once a table of that name, of at least a version and a size, is published (at once
// when it already is). The first addon to publish a name owns it; it may publish a newer version of
// it later, and each waiter runs once, with the first table that satisfies it. A table of too old a
// version or too small a size never reaches a waiter, and is logged once per waiter. Callbacks run on
// the publishing (or waiting) thread, outside the lock, in the order the waiters asked.

namespace swofty_core::services {

class Registry {
public:
    using Invoke = std::function<bool(SwoftyServiceFn, void *, const void *, size_t, uint32_t, std::string *)>;

    explicit Registry(std::function<void(const std::string &)> log = {}, Invoke invoke = {})
        : log_(std::move(log)), invoke_(std::move(invoke))
    {
    }

    int publish(const std::string &addon, const std::string &name, uint32_t version, const void *table, size_t size)
    {
        if (name.empty() || !table || size < sizeof(uint32_t)) {
            log("[services] publish " + name + " by " + who(addon) + " refused: no name or table");
            return SWOFTY_ASSET_INVALID;
        }
        std::vector<Call> calls;
        {
            std::lock_guard<std::mutex> g(lock_);
            Entry *e = find(name);
            if (e && e->addon != addon) {
                log("[services] publish " + name + " v" + std::to_string(version) + " by " + who(addon) +
                    " refused: " + who(e->addon) + " publishes it");
                return SWOFTY_ASSET_INVALID;
            }
            if (e && version < e->version) {
                log("[services] publish " + name + " v" + std::to_string(version) + " by " + who(addon) +
                    " refused: v" + std::to_string(e->version) + " is already published");
                return SWOFTY_ASSET_INVALID;
            }
            if (!e) {
                entries_.push_back({name, addon, version, table, size});
                e = &entries_.back();
            } else {
                e->version = version, e->table = table, e->size = size;
            }
            log("[services] publish " + name + " v" + std::to_string(version) + " (" + std::to_string(size) +
                " bytes) by " + who(addon) + " -> ok");
            calls = due(*e);
        }
        run(calls);
        return SWOFTY_ASSET_OK;
    }

    int when(const std::string &addon, const std::string &name, uint32_t min_version, size_t min_size, SwoftyServiceFn fn,
             void *user)
    {
        if (name.empty() || !fn) {
            log("[services] when " + name + " by " + who(addon) + " refused: no name or callback");
            return SWOFTY_ASSET_INVALID;
        }
        std::vector<Call> calls;
        {
            std::lock_guard<std::mutex> g(lock_);
            waiters_.push_back({addon, name, min_version, min_size, fn, user, false, false});
            Entry *e = find(name);
            log("[services] when " + name + " v" + std::to_string(min_version) + "+ by " + who(addon) + " -> " +
                (e ? "published by " + who(e->addon) : std::string("waiting for it")));
            if (e)
                calls = due(*e);
        }
        run(calls);
        return SWOFTY_ASSET_OK;
    }

    bool published(const std::string &name, uint32_t *version = nullptr)
    {
        std::lock_guard<std::mutex> g(lock_);
        Entry *e = find(name);
        if (e && version)
            *version = e->version;
        return e != nullptr;
    }

    // Once every addon has loaded: one line per waiter still waiting, naming the consumer and why.
    std::vector<std::string> missing()
    {
        std::lock_guard<std::mutex> g(lock_);
        std::vector<std::string> out;
        for (auto &w : waiters_) {
            if (w.done)
                continue;
            Entry *e = find(w.name);
            std::string line = "[services] " + who(w.addon) + " waits for " + w.name + " v" +
                               std::to_string(w.min_version) + "+: ";
            line += e ? who(e->addon) + " publishes v" + std::to_string(e->version) + " (" + std::to_string(e->size) +
                            " bytes, " + std::to_string(w.min_size) + " needed); its callback never runs"
                      : std::string("nothing publishes it; its callback never runs");
            out.push_back(line);
        }
        return out;
    }

private:
    struct Entry {
        std::string name, addon;
        uint32_t version;
        const void *table;
        size_t size;
    };
    struct Waiter {
        std::string addon, name;
        uint32_t min_version;
        size_t min_size;
        SwoftyServiceFn fn;
        void *user;
        bool done, warned;
    };
    struct Call {
        SwoftyServiceFn fn;
        void *user;
        const void *table;
        size_t size;
        uint32_t version;
        std::string addon, name;
    };

    Entry *find(const std::string &name)
    {
        for (auto &e : entries_)
            if (e.name == name)
                return &e;
        return nullptr;
    }

    std::vector<Call> due(const Entry &e)
    {
        std::vector<Call> calls;
        for (auto &w : waiters_) {
            if (w.done || w.name != e.name)
                continue;
            if (e.version < w.min_version || e.size < w.min_size) {
                if (!w.warned)
                    log("[services] " + e.name + " v" + std::to_string(e.version) + " (" + std::to_string(e.size) +
                        " bytes) from " + who(e.addon) + " is older than " + who(w.addon) + " needs (v" +
                        std::to_string(w.min_version) + ", " + std::to_string(w.min_size) + " bytes); not passed");
                w.warned = true;
                continue;
            }
            w.done = true;
            calls.push_back({w.fn, w.user, e.table, e.size, e.version, w.addon, e.name});
        }
        return calls;
    }

    void run(const std::vector<Call> &calls)
    {
        for (auto &c : calls) {
            std::string what;
            if (!invoke_) {
                c.fn(c.user, c.table, c.size, c.version);
                continue;
            }
            if (!invoke_(c.fn, c.user, c.table, c.size, c.version, &what))
                log("[services] when callback from " + who(c.addon) + " for " + c.name + " faulted (" + what +
                    "); the game carries on");
        }
    }

    static std::string who(const std::string &addon) { return addon.empty() ? std::string("the core") : addon; }

    void log(const std::string &line)
    {
        if (log_)
            log_(line);
    }

    std::function<void(const std::string &)> log_;
    Invoke invoke_;
    std::mutex lock_;
    std::vector<Entry> entries_;
    std::vector<Waiter> waiters_;
};

}
