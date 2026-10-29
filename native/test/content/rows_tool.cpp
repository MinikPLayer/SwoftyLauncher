#include "../../src/core/apply/content/boot.hpp"
#include "../../src/core/apply/ids.hpp"
#include "../../src/core/registry/table.hpp"
#include "../../src/core/registry/validate.hpp"

#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

// The core's boot apply of the content registries, outside the game: the same expansion appliers.cpp runs, over a
// game database file and a build's swofty/assets.json db_edits (the older addons' edits), with a fresh stat-id store.
// It writes the database the game would hold after the boot apply, as db_edits against the file (the records that
// differ from it), and a summary.

using namespace swofty_core;
using swofty::json::Value;
namespace content = apply::content;

static std::string slurp(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

static std::string hex16(uint64_t k)
{
    char b[24];
    std::snprintf(b, sizeof b, "%016llx", (unsigned long long)k);
    return b;
}

int main(int argc, char **argv)
{
    std::string regs, gvdb_path, out_path, edits_path, summary_path, room_text;
    std::vector<std::pair<std::string, std::string>> files;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--registries")
            regs = next();
        else if (a == "--db")
            gvdb_path = next();
        else if (a == "--edits")
            edits_path = next();
        else if (a == "--out")
            out_path = next();
        else if (a == "--summary")
            summary_path = next();
        else if (a == "--room")
            room_text = next();
        else if (i + 1 < argc) {
            files.push_back({a, argv[i + 1]});
            i++;
        }
    }
    if (regs.empty() || gvdb_path.empty() || out_path.empty() || files.empty()) {
        std::cerr << "usage: rows_tool --registries DIR --db GVDB --out OUT.json [--edits assets.json] [--summary S.json] "
                     "[--room N] <addon> <data file>...\n";
        return 2;
    }
    content::gvdb::File file;
    if (!file.load(slurp(gvdb_path))) {
        std::cerr << "cannot read " << gvdb_path << "\n";
        return 2;
    }
    std::unordered_map<uint64_t, content::rows::Rec> edits;
    if (!edits_path.empty()) {
        Value doc = swofty::json::parse(slurp(edits_path));
        const Value &list = doc["db_edits"];
        for (std::size_t i = 0; i < list.size(); i++) {
            const Value &e = list[i];
            uint64_t key = e["key"].is_string() ? content::gvdb::fnv64(e["key"].as_string())
                                                : std::strtoull(std::string(e["hash"].as_string()).c_str(), nullptr, 16);
            int tag = e["type"].is_int() ? int(e["type"].as_int()) : -1;
            if (tag < 0)
                if (auto f = file.get(key))
                    tag = f->tag;
            edits[key] = {tag, std::string(e["value"].as_string())};
        }
    }
    content::rows::Base base = [&](uint64_t key) -> std::optional<content::rows::Rec> {
        if (auto it = edits.find(key); it != edits.end())
            return it->second;
        if (auto r = file.get(key))
            return content::rows::Rec{r->tag, r->text};
        return std::nullopt;
    };

    registry::reset_store();
    auto &st = registry::store();
    std::vector<std::string> order;
    for (auto &[addon, path] : files)
        if (std::find(order.begin(), order.end(), addon) == order.end())
            order.push_back(addon);
    st.set_order(order);
    st.set_validator(&registry::schema_validator());
    st.set_log([](const std::string &l) { std::cerr << l << "\n"; });
    const SwoftyRegistry *t = registry::table();
    for (const char *name : {"engine_sound", "vehicle.package", "vehicle", "livery", "content.package", "era.box", "team.identity", "team.constructor", "team.pit_times", "team.logo", "helmet", "suit",
                             "gloves", "driver.head", "driver.look", "driver.person", "driver.identity", "driver.stats",
                             "wearable.season_set"}) {
        std::string schema = slurp(regs + "/" + name + "/1.0.json");
        t->declare(name, SwoftyText{schema.data(), schema.size()});
    }
    int bad = 0;
    for (auto &[addon, path] : files)
        if (registry::load_data_file(addon, path, slurp(path)) != SWOFTY_ASSET_OK) {
            std::cerr << "data file " << path << ": some records refused\n";
            bad++;
        }
    st.end_register();
    for (auto &r : st.refusals()) {
        std::cerr << "refused: " << r.addon << " " << r.registry << " " << r.id << " " << r.reason << ": " << r.message << "\n";
        bad++;
    }

    content::Expansion plan(base);
    content::expand(st, plan, {});

    auto text = [&](const std::string &path) -> std::optional<std::string> {
        auto r = base(content::gvdb::fnv64(path));
        if (!r)
            return std::nullopt;
        return r->text;
    };
    auto &ids = apply::ids::store();
    ids.clear();
    std::map<std::string, content::PoolScan> scans;
    Value pools = Value::object();
    for (auto &scan : content::scan_all_pools(text)) {
        apply::ids::Pool pool;
        if (!scan.holes.empty())
            pool.ranges = scan.holes;
        else if (scan.lo <= scan.hi)
            pool.ranges.push_back({scan.lo, scan.hi});
        ids.define_pool(scan.registry, pool);
        pools[scan.registry] = Value::array({Value(scan.lo), Value(scan.hi)});
        scans[scan.registry] = scan;
    }
    Value clashes = Value::array();
    for (auto &p : content::pins(st)) {
        ids.pin(p.registry, p.id, p.stat_id, "test");
        auto s = scans.find(p.registry);
        if (s != scans.end() && s->second.used.count(p.stat_id))
            clashes.push_back(Value(p.registry + " " + p.id + " " + std::to_string(p.stat_id) + " " +
                                    s->second.used[p.stat_id]));
    }
    uint64_t room = room_text.empty() ? UINT64_MAX : std::strtoull(room_text.c_str(), nullptr, 10);
    content::Admission adm = content::admit(st.order(), plan.addon_fresh, room);
    Value allocated = Value::object();
    content::StatIds stat = [&](const std::string &reg, const std::string &id, int64_t pin) -> int64_t {
        if (pin >= 0)
            return ids.pin(reg, id, pin, "test").id;
        auto r = ids.allocate(reg, id, "test");
        allocated[reg + " " + id] = Value(r.id);
        return r.id;
    };
    content::Expansion boot(base);
    content::expand(st, boot, stat, adm.skip);

    std::map<uint64_t, std::pair<int, std::string>> db;
    for (auto &[k, r] : edits)
        db[k] = {r.tag, r.text};
    std::size_t untyped = 0;
    Value staged = Value::array();
    for (auto &w : boot.engine.writes()) {
        if (!*content::tag_name(w.tag))
            untyped++;
        db[w.key] = {w.tag, w.text};
        staged.push_back(Value::object({{"addon", Value(w.owner)},
                                        {"registry", Value(w.registry)},
                                        {"path", Value(w.path)},
                                        {"type", Value(content::tag_name(w.tag))},
                                        {"value", Value(w.text)}}));
    }
    Value out = Value::array();
    for (auto &[k, v] : db) {
        auto f = file.get(k);
        if (f && f->tag == v.first && f->text == v.second)
            continue;
        out.push_back(Value::object({{"hash", Value(hex16(k))}, {"type", Value(v.first)}, {"value", Value(v.second)}}));
    }
    std::ofstream(out_path) << swofty::json::dump(Value::object({{"db_edits", out}}), 0) << "\n";

    Value summary = Value::object();
    auto counts = [](const std::map<std::string, std::size_t> &m) {
        Value o = Value::object();
        for (auto &[k, v] : m)
            o[k] = Value(int64_t(v));
        return o;
    };
    summary["plan_fresh"] = counts(plan.fresh);
    summary["boot_fresh"] = counts(boot.fresh);
    summary["addon_fresh"] = counts(plan.addon_fresh);
    Value recs = Value::object(), refused = Value::object(), lines = Value::object();
    for (auto &[k, v] : boot.records)
        recs[k] = Value(v);
    for (auto &[k, v] : boot.refused)
        refused[k] = Value(v);
    for (auto &[k, v] : boot.lines) {
        Value a = Value::array();
        for (auto &l : v)
            a.push_back(Value(l));
        lines[k] = a;
    }
    summary["records"] = recs;
    summary["refused"] = refused;
    summary["room_refused"] = Value::array();
    for (auto &[a, k] : adm.refused)
        summary["room_refused"].push_back(Value::object({{"addon", Value(a)}, {"keys", Value(int64_t(k))}}));
    summary["pools"] = pools;
    summary["pin_clashes"] = clashes;
    summary["allocated"] = allocated;
    summary["staged"] = Value(int64_t(staged.size()));
    summary["untyped"] = Value(int64_t(untyped));
    summary["lines"] = lines;
    if (!summary_path.empty()) {
        Value full = summary;
        full["staged_records"] = staged;
        std::ofstream(summary_path) << swofty::json::dump(full, 1) << "\n";
    }
    int refused_n = 0;
    for (auto &[k, v] : boot.refused)
        refused_n += v;
    std::cout << staged.size() << " records staged, " << out.size() << " db_edits after the boot apply; plan "
              << swofty::json::dump(summary["plan_fresh"]) << ", boot " << swofty::json::dump(summary["boot_fresh"])
              << "; " << refused_n << " record(s) refused, " << adm.refused.size() << " addon(s) left out for room\n";
    return bad || refused_n || untyped ? 1 : 0;
}
