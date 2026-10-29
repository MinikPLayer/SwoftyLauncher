#pragma once

#include "store.hpp"

#include <functional>
#include <string>

namespace swofty_core::registry {

Store &store();
void reset_store();
const SwoftyRegistry *table();

void set_caller_resolver(std::function<std::string(const void *from)> fn);

class AsAddon {
public:
    explicit AsAddon(std::string addon);
    ~AsAddon();
    AsAddon(const AsAddon &) = delete;
    AsAddon &operator=(const AsAddon &) = delete;

private:
    std::string prev_;
    bool had_;
};

struct IndexResult {
    bool found = false;
    std::string profile;
    std::vector<std::string> order;
    std::size_t files = 0, records = 0, layers = 0, tags = 0, failed = 0;
};

IndexResult load_index(const std::string &swofty_dir);
int load_data_file(const std::string &addon, const std::string &path, const std::string &text);
int write_reports(const std::string &registry_dir,
                  const std::function<void(Value &effective, Value &conflicts)> &extend = {});

using StatIdSource = std::function<int(const std::string &registry, const std::string &id, int64_t *out)>;
void set_stat_id_source(StatIdSource fn);

}
