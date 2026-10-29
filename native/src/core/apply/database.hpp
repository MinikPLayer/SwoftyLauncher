#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

namespace swofty_core::apply::db {

struct Row {
    std::string addon, registry, path, type, text;
};

// An addon whose database records an applier left out for room before staging them.
struct Refusal {
    std::string addon;
    std::vector<std::string> registries;
    std::size_t keys = 0;
};

inline constexpr uint64_t UNMEASURED = std::numeric_limits<uint64_t>::max();

class Staging {
public:
    int set(const std::string &addon, const std::string &registry, const std::string &path, const std::string &type,
            const std::string &text)
    {
        std::lock_guard g(mu_);
        if (!open_)
            return 0;
        rows_.push_back({addon, registry, path, type, text});
        return 1;
    }

    bool available() const
    {
        std::lock_guard g(mu_);
        return open_;
    }

    bool exists(const std::string &path) const
    {
        std::function<bool(const std::string &)> fn;
        {
            std::lock_guard g(mu_);
            fn = exists_;
        }
        return fn && fn(path);
    }

    void open(std::function<bool(const std::string &)> exists, uint64_t room = UNMEASURED)
    {
        std::lock_guard g(mu_);
        open_ = true;
        exists_ = std::move(exists);
        room_ = room;
    }

    // The new keys the database takes this game start before it would grow, after the older addons' edits.
    uint64_t room() const
    {
        std::lock_guard g(mu_);
        return room_;
    }

    void refuse(Refusal r)
    {
        std::lock_guard g(mu_);
        refused_.push_back(std::move(r));
    }

    std::vector<Refusal> take_refusals()
    {
        std::lock_guard g(mu_);
        std::vector<Refusal> out;
        out.swap(refused_);
        return out;
    }

    std::vector<Row> close()
    {
        std::lock_guard g(mu_);
        open_ = false;
        exists_ = nullptr;
        std::vector<Row> out;
        out.swap(rows_);
        return out;
    }

    std::size_t size() const
    {
        std::lock_guard g(mu_);
        return rows_.size();
    }

private:
    mutable std::mutex mu_;
    bool open_ = false;
    uint64_t room_ = UNMEASURED;
    std::function<bool(const std::string &)> exists_;
    std::vector<Row> rows_;
    std::vector<Refusal> refused_;
};

inline Staging &staging()
{
    static Staging s;
    return s;
}

}
