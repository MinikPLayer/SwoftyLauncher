#pragma once
// Shared by the Progressive registry tests: the stock team rows the core reads at boot, seeded into the store, and
// Progressive's captured registrations (register/run.sh) added as its records.

#include "../../src/core/apply/content/teams.hpp"
#include "../../src/core/registry/table.hpp"

#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace progressive_seed {

using namespace swofty_core::registry;


inline std::string slurp(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

inline const char *STOCK_LABELS[] = {"mercedes",   "ferrari",    "redbull",    "mclaren",    "forceindia", "renault",
                              "williams",   "tororosso",  "sauber",     "haas",       "mercedes_24", "ferrari_24",
                              "redbull_24", "mclaren_24", "forceindia_24", "renault_24", "williams_24", "tororosso_24",
                              "sauber_24",  "haas_24",    "mercedes_26", "ferrari_26", "redbull_26", "mclaren_26",
                              "forceindia_26", "renault_26", "williams_26", "tororosso_26", "sauber_26", "haas_26",
                              "gm_cadillac_26"};

inline std::vector<std::string> stock_labels()
{
    std::vector<std::string> labels(std::begin(STOCK_LABELS), std::end(STOCK_LABELS));
    for (int yy = 16; yy <= 24; yy++)
        for (const char *team : {"mercedes", "ferrari", "redbull", "mclaren", "force_india", "lotus", "williams", "toro_rosso",
                                 "sauber", "haas"})
            labels.push_back("cs" + std::to_string(yy) + "_" + team);
    labels.push_back("cs16_manor");
    return labels;
}

inline void seed_stock(Store &st)
{
    namespace content = swofty_core::apply::content;
    std::map<std::string, std::string> db;
    int n = 0;
    for (const std::string &label : stock_labels()) {
        std::string at = std::string(content::TEAMS) + ".team_" + std::to_string(n++);
        db[at + "._metadata_"] = label;
        db[at + ".display_name"] = label;
    }
    db[std::string(content::TEAMS) + "._team_instancecount_"] = std::to_string(n);
    content::Read read = [&db](const std::string &path) -> std::optional<std::string> {
        auto it = db.find(path);
        if (it == db.end())
            return std::nullopt;
        return it->second;
    };
    for (auto &c : content::CONSTRUCTOR_SEEDS)
        st.add_base("team.constructor", c.key,
                    Value::object({{"name", Value(std::string(c.name))}, {"country", Value(std::string(c.country))},
                                   {"team", Value()}}));
    for (auto &t : content::stock_teams(read))
        st.add_base("team.identity", t.label, t.record);
    st.stock_loaded("team.identity", true);
}

inline int add_all(Store &st, const std::string &addon, const Value &calls)
{
    int added = 0;
    for (auto &c : *calls["add"].if_array()) {
        std::string id;
        int rc = st.add(addon, std::string(c["registry"].as_string()), std::string(c["key"].as_string()), c["value"], &id);
        if (rc != 0)
            std::printf("refused: %s %s\n", std::string(c["registry"].as_string()).c_str(),
                        std::string(c["key"].as_string()).c_str());
        added += rc == 0;
    }
    return added;
}

}
