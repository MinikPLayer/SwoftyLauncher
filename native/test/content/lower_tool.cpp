#include "../../src/core/apply/content/gvdb.hpp"
#include "../../src/core/apply/content/lower.hpp"
#include "../../src/core/registry/table.hpp"
#include "../../src/core/registry/validate.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

using namespace swofty_core;
using swofty::json::Value;

static std::string slurp(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

int main(int argc, char **argv)
{
    if (argc < 6) {
        std::cerr << "usage: lower_tool <registries dir> <stock gvdb> <out ops.json> <addon id> <data file>...\n";
        return 2;
    }
    std::string regs = argv[1];
    std::unordered_map<uint64_t, apply::content::gvdb::Record> db;
    std::string bin = slurp(argv[2]);
    if (!apply::content::gvdb::read(reinterpret_cast<const unsigned char *>(bin.data()), bin.size(), db)) {
        std::cerr << "cannot read " << argv[2] << "\n";
        return 2;
    }
    registry::reset_store();
    auto &st = registry::store();
    std::vector<std::string> order;
    for (int i = 4; i + 1 < argc; i += 2)
        order.emplace_back(argv[i]);
    st.set_order(order);
    st.set_validator(&registry::schema_validator());
    st.set_log([](const std::string &l) { std::cerr << l << "\n"; });
    const SwoftyRegistry *t = registry::table();
    const char *names[] = {"engine_sound", "vehicle.package", "vehicle", "livery", "content.package", "era.box"};
    for (const char *name : names) {
        std::string schema = slurp(regs + "/" + name + "/1.0.json");
        t->declare(name, SwoftyText{schema.data(), schema.size()});
    }
    for (int i = 4; i + 1 < argc; i += 2)
        if (registry::load_data_file(argv[i], argv[i + 1], slurp(argv[i + 1])) != SWOFTY_ASSET_OK)
            std::cerr << "data file " << argv[i + 1] << ": some records refused\n";
    st.end_register();
    for (auto &r : st.refusals())
        std::cerr << "refused: " << r.addon << " " << r.registry << " " << r.id << " " << r.reason << ": " << r.message << "\n";
    std::map<std::string, Value> keep;
    apply::content::Lookup lookup = [&](const std::string &reg, const std::string &id) -> const Value * {
        auto k = reg + "|" + id;
        auto it = keep.find(k);
        if (it != keep.end())
            return &it->second;
        auto rec = st.get(reg, id);
        if (rec.status != registry::Status::ok)
            return nullptr;
        return &(keep[k] = rec.value);
    };
    apply::content::Source source = [&](const std::string &key) -> std::optional<apply::content::Stock> {
        auto it = db.find(apply::content::gvdb::fnv64(key));
        if (it == db.end())
            return std::nullopt;
        return apply::content::Stock{it->second.tag, it->second.text};
    };
    apply::content::Plan plan;
    for (const char *name : names)
        for (auto &id : st.list(name, "")) {
            const Value *rec = lookup(name, id);
            if (!rec)
                continue;
            std::string n = name;
            if (n == "engine_sound")
                apply::content::lower_engine_sound(id, *rec, plan);
            else if (n == "vehicle.package")
                apply::content::lower_vehicle_package(id, *rec, plan);
            else if (n == "vehicle")
                apply::content::lower_vehicle(id, *rec, lookup, source, plan);
            else if (n == "livery")
                ;
            else if (n == "content.package")
                apply::content::lower_package(id, *rec, lookup, plan);
            else
                apply::content::lower_box(id, *rec, lookup, plan);
        }
    for (auto &l : plan.lines)
        std::cout << l << "\n";
    for (auto &b : plan.boxes)
        std::cout << "box " << b.id << ": " << b.entrant_list << " title " << b.title << " order " << b.order << "\n";
    for (auto &r : plan.redirects)
        std::cout << "redirect " << r.path << " -> " << r.addon << "/" << r.file << "\n";
    std::cout << plan.ops.size() << " ops, about " << plan.keys << " new database keys, " << plan.refused
              << " refused\n";
    Value out = Value::object({{"schema_version", 1}, {"fields", plan.fields}, {"ops", plan.ops}});
    std::ofstream(argv[3]) << swofty::json::dump(out, 1) << "\n";
    return plan.refused ? 1 : 0;
}
