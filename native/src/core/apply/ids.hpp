#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace swofty_core::apply::ids {

struct Pool {
    std::vector<std::pair<int64_t, int64_t>> ranges;
};

using CareerMap = std::map<std::string, std::map<std::string, int64_t>>;

struct Result {
    int64_t id = -1;
    std::string how, line;
};

struct Clash {
    std::string registry, record, holder;
    int64_t id = -1, local = -1;
    std::string line;
};

class Store {
public:
    bool load(const std::string &text, std::string *why = nullptr);
    std::string dump() const;
    void clear();

    void define_pool(const std::string &registry, Pool pool);
    bool has_pool(const std::string &registry) const;
    void set_live(const std::string &registry, const std::vector<std::string> &records);

    Result get(const std::string &registry, const std::string &record) const;
    Result allocate(const std::string &registry, const std::string &record, const std::string &today);
    Result pin(const std::string &registry, const std::string &record, int64_t id, const std::string &today);

    CareerMap map_for_career(const std::string &career);
    std::vector<Clash> adopt(const std::string &career, const CareerMap &map, const std::string &today);
    void forget_career(const std::string &career);

    bool dirty() const;
    void saved();
    std::size_t size() const;

private:
    struct Entry {
        int64_t id = -1;
        std::string since;
    };

    bool held_locked(const std::string &registry, int64_t id, std::string *holder) const;
    bool career_uses_locked(const std::string &registry, int64_t id, const std::string &except = {}) const;
    bool live_locked(const std::string &registry, const std::string &record) const;
    std::string pool_text_locked(const std::string &registry) const;
    std::size_t free_count_locked(const std::string &registry) const;

    mutable std::mutex mu_;
    std::map<std::string, std::map<std::string, Entry>> ids_;
    std::map<std::string, CareerMap> careers_;
    std::map<std::string, Pool> pools_;
    std::map<std::string, std::set<std::string>> live_;
    bool dirty_ = false;
};

Store &store();

}
