#include "appliers.hpp"

#include "../applier.hpp"
#include "../database.hpp"
#include "../files_api.hpp"
#include "../ids.hpp"
#include "boot.hpp"
#include "gvdb.hpp"
#include "logos.hpp"
#include "teams.hpp"
#include "../../career_ops/teams_plan.hpp"
#include "../../registry/older.hpp"
#include "../../registry/table.hpp"

#include "../../runtime.hpp"

#include <swofty/mixin.hpp>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <unordered_map>

// The core's appliers for the content registries. At boot, inside the game database load, the engine sound,
// vehicle package, vehicle and package records become database rows: each is lowered to operations and run through
// the row engine (rows.hpp) over the database the game loaded (the database file in the game folder under the older
// addons' edits), and every record it writes is staged for gamedb.cpp, which applies them per addon in load order.
// Before the load, the same expansion without stat ids counts the new keys for the value map's size. Stat ids:
// explicit ones are pinned in the install's store, "auto" ones are allocated from the pools scan_pools finds. An
// addon whose new keys don't fit the room left is left out whole before anything is staged, so the rows of the
// addons after it stay contiguous. Era boxes go into the core's era picker at the window stage.

namespace swofty_core::apply::content {

namespace {

std::recursive_mutex mu;

struct Database {
    gvdb::File file;
    std::unordered_map<uint64_t, rows::Rec> edits;
    bool loaded = false, ok = false;
};

struct State {
    std::unique_ptr<Database> db;
    std::unique_ptr<Expansion> plan, boot;
    std::vector<std::string> boot_lines;
    std::map<std::string, std::set<std::string>> refused;
    bool stock_seeded = false;
};

State &state()
{
    static State s;
    return s;
}

std::string today()
{
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    char b[16];
    std::strftime(b, sizeof b, "%Y-%m-%d", &tm);
    return b;
}

void read_edits(Database &d)
{
    d.edits.clear();
    runtime::db_older_edits([&d](uint64_t key, int tag, const std::string &text) {
        rows::Rec r{tag, text};
        if (r.tag < 0)
            if (auto f = d.file.get(key))
                r.tag = f->tag;
        d.edits[key] = std::move(r);
    });
}

Database &database()
{
    State &s = state();
    if (!s.db)
        s.db = std::make_unique<Database>();
    Database &d = *s.db;
    if (d.loaded)
        return d;
    d.loaded = true;
    std::wstring path = runtime::game_root() + L"\\game_package\\gamedatabases\\gamedatabases.2025.bin";
    d.ok = d.file.load(runtime::read_file(path));
    if (!d.ok) {
        swofty::log("[apply] content: the game database file is unreadable; no content rows are applied");
        return d;
    }
    read_edits(d);
    swofty::log("[apply] content: read %zu record(s) from the game database file and %zu older addon edit(s)",
                d.file.size(), d.edits.size());
    return d;
}

rows::Base base_of(Database &d)
{
    return [&d](uint64_t key) -> std::optional<rows::Rec> {
        if (auto it = d.edits.find(key); it != d.edits.end())
            return it->second;
        if (auto r = d.file.get(key))
            return rows::Rec{r->tag, std::move(r->text)};
        return std::nullopt;
    };
}

void ensure_stock(Store &st)
{
    State &s = state();
    if (s.stock_seeded || (in_load_order(st, "team.identity").empty() && in_load_order(st, "team.pit_times").empty()))
        return;
    Database &d = database();
    if (!d.ok)
        return;
    s.stock_seeded = true;
    rows::Base base = base_of(d);
    Read read = [&base](const std::string &path) -> std::optional<std::string> {
        auto r = base(gvdb::fnv64(path));
        if (!r)
            return std::nullopt;
        return r->text;
    };
    for (auto &c : CONSTRUCTOR_SEEDS)
        st.add_base("team.constructor", c.key,
                    Value::object({{"name", Value(std::string(c.name))}, {"country", Value(std::string(c.country))},
                                   {"team", Value()}}));
    std::size_t teams = 0;
    for (auto &t : stock_teams(read)) {
        st.add_base("team.identity", t.label, t.record);
        teams++;
    }
    st.stock_loaded("team.identity", true);
    swofty::log("[apply] content: %zu stock team record(s) and %zu constructor id(s) declared from the game database", teams,
                std::size(CONSTRUCTOR_SEEDS));
}

bool has_records(Store &st)
{
    ensure_stock(st);
    for (const char *reg : ROW_REGISTRIES)
        if (!in_load_order(st, reg).empty())
            return true;
    return false;
}

Expansion &plan_run(Store &st)
{
    State &s = state();
    if (!s.plan) {
        Database &d = database();
        s.plan = std::make_unique<Expansion>(base_of(d));
        if (d.ok)
            expand(st, *s.plan, {});
    }
    return *s.plan;
}

void define_pools(Database &d, std::vector<std::string> &lines, std::map<std::string, PoolScan> &scans)
{
    rows::Base base = base_of(d);
    auto text = [&base](const std::string &path) -> std::optional<std::string> {
        auto r = base(gvdb::fnv64(path));
        if (!r)
            return std::nullopt;
        return r->text;
    };
    for (auto &scan : scan_all_pools(text)) {
        ids::Pool pool;
        if (!scan.holes.empty())
            pool.ranges = scan.holes;
        else if (scan.lo <= scan.hi)
            pool.ranges.push_back({scan.lo, scan.hi});
        ids::store().define_pool(scan.registry, pool);
        lines.push_back("stat-id pool for " + scan.registry + ": " +
                        (scan.lo <= scan.hi ? std::to_string(scan.lo) + "-" + std::to_string(scan.hi) : std::string("none")) +
                        " (" + scan.table + " holds ids to " + std::to_string(scan.lo - 1) + ")");
        scans[scan.registry] = std::move(scan);
    }
}

void pin_all(Store &st, const std::map<std::string, PoolScan> &scans, std::vector<std::string> &lines)
{
    std::string day = today();
    for (auto &p : pins(st)) {
        ids::Result had = ids::store().get(p.registry, p.id);
        ids::Result r = ids::store().pin(p.registry, p.id, p.stat_id, day);
        if (had.id != p.stat_id || r.line.find("also holds") != std::string::npos)
            swofty::log("%s", r.line.c_str());
        auto scan = scans.find(p.registry);
        if (scan == scans.end())
            continue;
        auto held = scan->second.used.find(p.stat_id);
        if (held != scan->second.used.end())
            lines.push_back(p.registry + " " + p.id + ": stat id " + std::to_string(p.stat_id) + " is also " +
                            scan->second.table + " row " + held->second + "'s, which the game or an older addon added");
    }
}

int64_t stat_id(const std::string &registry, const std::string &id, int64_t pin)
{
    if (pin >= 0)
        return ids::store().pin(registry, id, pin, today()).id;
    ids::Result r = ids::store().allocate(registry, id, today());
    if (r.how != "kept" && !r.line.empty())
        swofty::log("%s", r.line.c_str());
    return r.id;
}

void run_boot(Store &st)
{
    State &s = state();
    Database &d = database();
    Expansion &plan = plan_run(st);
    s.boot = std::make_unique<Expansion>(base_of(d));
    if (!d.ok)
        return;
    read_edits(d);
    std::map<std::string, PoolScan> scans;
    define_pools(d, s.boot_lines, scans);
    pin_all(st, scans, s.boot_lines);
    Admission adm = admit(st.order(), plan.addon_fresh, db::staging().room());
    for (auto &[addon, keys] : adm.refused) {
        db::Refusal r{addon, {}, keys};
        for (const char *reg : ROW_REGISTRIES)
            for (auto &id : st.list(reg, addon + "/"))
                if (addon_of(id) == addon) {
                    r.registries.push_back(reg);
                    s.refused[reg].insert(addon);
                    break;
                }
        db::staging().refuse(std::move(r));
    }
    expand(st, *s.boot, stat_id, adm.skip);
}

void redirect_bundles(Store &st, Expansion &x, Report &report)
{
    std::map<std::string, int> routed, kept;
    std::set<std::string> paths;
    std::vector<std::string> order;
    auto note = [&order](const std::string &addon) {
        if (std::find(order.begin(), order.end(), addon) == order.end())
            order.push_back(addon);
    };
    for (auto &rd : x.redirects) {
        registry::Record rec = st.get("engine_sound", rd.id);
        std::string addon = rec.status == registry::Status::ok
                                ? files::owner_of_field(rec.origins, rec.owner.empty() ? rd.addon : rec.owner, "/bundle")
                                : rd.addon;
        files::Outcome o = files::redirect(rd.path, addon, rd.file, "engine_sound", rd.id, "boot");
        note(addon);
        if (o.kind == files::Kind::routed || o.kind == files::Kind::shadowed)
            paths.insert(o.path);
        else if (o.kind == files::Kind::kept)
            kept[addon]++;
        else {
            report.refused++;
            report.lines.push_back(o.line);
        }
        if (!o.conflict.empty())
            report.lines.push_back(o.conflict);
    }
    for (auto &p : paths) {
        std::string w = files::winner(p);
        if (!w.empty())
            routed[w]++;
    }
    std::string summary;
    for (auto &addon : order) {
        if (routed[addon])
            summary += (summary.empty() ? "" : ", ") + std::to_string(routed[addon]) + " bundle path" +
                       (routed[addon] == 1 ? "" : "s") + " redirected to " + addon;
        if (kept[addon])
            report.lines.push_back(addon + ": " + std::to_string(kept[addon]) + " bundle(s) not in swofty/addons/" + addon +
                                   "/; the game folder's copies are used (its file operations put them there)");
    }
    if (!summary.empty())
        report.summary = summary;
}

uint32_t djb2(const std::string &s)
{
    uint32_t h = 5381;
    for (unsigned char c : s)
        h = h * 33 + c;
    return h;
}

bool in_game_folder(const std::string &rel)
{
    std::string p = rel;
    std::replace(p.begin(), p.end(), '/', '\\');
    uint64_t size = 0;
    return runtime::file_size(runtime::game_root() + L"\\" + runtime::wide(p), &size);
}

void count_outcome(const files::Outcome &o, Report &report, std::set<std::string> &paths)
{
    if (o.kind == files::Kind::routed || o.kind == files::Kind::shadowed)
        paths.insert(o.path);
    else if (o.refused()) {
        report.refused++;
        report.lines.push_back(o.line);
    }
    if (!o.conflict.empty())
        report.lines.push_back(o.conflict);
}

void apply_liveries(Expansion &x, Report &report, const std::string &registry)
{
    std::set<std::string> paths;
    bool livery = registry == "livery";
    for (auto &g : x.groups) {
        if (g.registry != registry)
            continue;
        std::string addon = g.addon;
        count_outcome(files::redirect(g.erp, addon, g.file, registry, g.id, "boot"), report, paths);
        if (g.erp.find("_lx") == std::string::npos && in_game_folder(g.erp))
            continue;
        ::json::Value e;
        e.type = ::json::Value::Obj;
        auto str = [](const std::string &t) {
            ::json::Value v;
            v.type = ::json::Value::Str;
            v.s = t;
            return v;
        };
        ::json::Value list;
        list.type = ::json::Value::Arr;
        for (auto &c : g.common)
            list.a.push_back(str(c));
        list.a.push_back(str(g.erp));
        ::json::Value deps;
        deps.type = ::json::Value::Arr;
        e.o.push_back({"group", str(g.group)});
        e.o.push_back({"erps", std::move(list)});
        e.o.push_back({"deps", std::move(deps)});
        if (!g.tmpl.empty())
            e.o.push_back({"template", str(g.tmpl)});
        int rc = runtime::group_add(e);
        report.lines.push_back(registry + " " + g.id + ": asset group " + g.group + (rc == SWOFTY_ASSET_OK ? " registered" : " not registered"));
        if (rc != SWOFTY_ASSET_OK)
            report.refused++;
    }
    for (auto &f : x.folders) {
        if (!livery)
            break;
        std::filesystem::path dir(runtime::under_swofty("addons/" + f.addon + "/" + f.file));
        std::error_code ec;
        std::size_t n = 0;
        for (std::filesystem::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec))
                continue;
            std::string rel = std::filesystem::relative(it->path(), dir).generic_string();
            count_outcome(files::redirect(f.game_dir + rel, f.addon, f.file + rel, "livery", f.id, "boot"), report, paths);
            n++;
        }
        if (!n)
            report.lines.push_back("livery " + f.id + ": " + f.file + " holds no files; no texture redirected");
    }
    for (auto &o : x.owned)
        if (livery && o.owned)
            runtime::own_item(djb2("eSELivery" + o.label), addon_of(o.id), o.label);
    std::map<std::string, int> routed;
    for (auto &p : paths) {
        std::string w = files::winner(p);
        if (!w.empty())
            routed[w]++;
    }
    std::string summary;
    for (auto &[addon, n] : routed)
        summary += (summary.empty() ? "" : ", ") + std::to_string(n) + (livery ? " livery file" : " archive") + (n == 1 ? "" : "s") + " redirected to " + addon;
    if (!summary.empty())
        report.summary = report.summary.empty() ? summary : report.summary + "; " + summary;
}

void apply_texts(Expansion &x, const char *registry, Report &report)
{
    auto it = x.texts.find(registry);
    if (it == x.texts.end() || it->second.empty())
        return;
    const auto &texts = it->second;
    const char *what = std::string_view(registry) == "content.package" ? " title text(s)" : " name text(s)";
    const SwoftyLang *lang = runtime::lang_table();
    if (!lang) {
        report.lines.push_back(std::string(registry) + ": " + std::to_string(texts.size()) + what +
                               " not set: the language table isn't up yet");
        return;
    }
    std::size_t set = 0;
    std::vector<std::string> layered, failed;
    for (auto &[tag, text] : texts) {
        if (lang->override_set("*", tag.c_str(), text.c_str()) == SWOFTY_ASSET_OK)
            set++;
        else if (registry::older::string_layer(runtime::older_store(), "swofty.core", tag,
                                               Value::object({{"*", Value(text)}})) == SWOFTY_ASSET_OK)
            layered.push_back(tag);
        else
            failed.push_back(tag);
    }
    auto list = [](const std::vector<std::string> &tags) {
        std::string out;
        for (std::size_t i = 0; i < tags.size(); i++)
            out += (i ? ", " : "") + tags[i];
        return out;
    };
    if (set && std::string_view(registry) != "content.package")
        report.lines.push_back(std::string(registry) + ": " + std::to_string(set) + what +
                               " set through the core's lang override (first " + texts.front().first + " = \"" +
                               texts.front().second + "\")");
    if (!layered.empty())
        report.lines.push_back(std::string(registry) + ": " + std::to_string(layered.size()) + what +
                               " registered as lang.string layers from swofty.core; the lang override isn't up yet and "
                               "reads them when it starts (" + list(layered) + ")");
    if (!failed.empty())
        report.lines.push_back(std::string(registry) + ": " + std::to_string(failed.size()) + what + " not set (" +
                               list(failed) + ")");
}

void apply_logos(Expansion &x, Report &report)
{
    Live &live_teams = live();
    live_teams.manifest = x.logos;
    if (x.logos.empty())
        return;
    LogoReport made;
    deliver_logos(x.logos, made);
    for (auto &l : made.lines)
        report.lines.push_back(l);
    report.refused += made.refused;
    report.lines.push_back("team.logo: " + std::to_string(x.logos.size()) + " logo slot(s) planned, " + std::to_string(made.built) +
                           " slice(s) built, " + std::to_string(made.refused) + " refused");
}

void set_up_dresser()
{
    Live &l = live();
    l.log = [](const std::string &line) { swofty::log("[teams] %s", line.c_str()); };
    l.identity = [](const std::string &id) -> const Value * {
        static std::map<std::string, Value> keep;
        auto rec = registry::store().get("team.identity", id);
        if (rec.status != registry::Status::ok)
            return nullptr;
        return &(keep[id] = rec.value);
    };
    l.stat_of = [](const std::string &id) -> int64_t {
        int64_t got = ids::store().get("team.identity", id).id;
        if (got >= 0)
            return got;
        auto rec = registry::store().get("team.identity", id);
        return rec.status == registry::Status::ok && rec.value["stat_id"].is_int() ? rec.value["stat_id"].as_int() : -1;
    };
    l.record = &runtime::team_record_of;
    runtime::team_record_inserted([](uint16_t stat, uint8_t *record) { redress(stat, record); });
    career_ops::teams::dresser() = &dress_slot;
}

void release_database()
{
    State &s = state();
    s.boot.reset();
    s.plan.reset();
    s.db.reset();
}

class Rows : public Applier {
public:
    explicit Rows(const char *registry) : registry_(registry) {}
    const char *registry() const override { return registry_; }
    Applies applies() const override { return Applies::boot; }

    std::size_t database_keys(Store &st) override
    {
        std::lock_guard g(mu);
        if (!has_records(st))
            return 0;
        Expansion &x = plan_run(st);
        auto it = x.fresh.find(registry_);
        return it == x.fresh.end() ? 0 : it->second;
    }

    void boot(Store &st, Report &report) override
    {
        std::lock_guard g(mu);
        State &s = state();
        if (!has_records(st))
            return;
        if (!db::staging().available()) {
            for (auto &id : st.list(registry_, ""))
                if (id.rfind("game/", 0) != 0)
                    report.refused++;
            report.lines.push_back("the game database is not reachable at this boot; no rows applied");
            return;
        }
        bool first = !s.boot;
        if (first)
            run_boot(st);
        if (first)
            for (auto &l : s.boot_lines)
                report.lines.push_back(l);
        Expansion &x = *s.boot;
        std::size_t staged = 0;
        for (auto &w : x.engine.writes()) {
            if (w.registry != registry_)
                continue;
            const char *type = tag_name(w.tag);
            if (!*type) {
                report.lines.push_back(w.path + ": tag " + std::to_string(w.tag) + " stores nothing; not staged");
                continue;
            }
            staged += db::staging().set(w.owner, w.registry, w.path, type, w.text);
        }
        report.applied = int(x.records[registry_]);
        report.refused = int(x.refused[registry_]);
        for (auto &addon : s.refused[registry_]) {
            report.refused++;
            report.lines.push_back(addon + ": left out for room (see the [registry] room line)");
        }
        report.lines.push_back(std::to_string(staged) + " database record(s) staged");
        for (auto &l : x.lines[registry_])
            report.lines.push_back(l);
        if (std::string_view(registry_) == "engine_sound")
            redirect_bundles(st, x, report);
        if (std::string_view(registry_) == "livery" || std::string_view(registry_) == "helmet" ||
            std::string_view(registry_) == "suit" || std::string_view(registry_) == "gloves" ||
            std::string_view(registry_) == "driver.head")
            apply_liveries(x, report, registry_);
        apply_texts(x, registry_, report);
        if (std::string_view(registry_) == "team.identity")
            apply_logos(x, report);
        if (staged) {
            std::string records = std::to_string(staged) + " database record" + (staged == 1 ? "" : "s");
            report.summary = report.summary.empty() ? records : report.summary + "; " + records;
        }
        if (std::string_view(registry_) == ROW_REGISTRIES[std::size(ROW_REGISTRIES) - 1])
            release_database();
    }

private:
    const char *registry_;
};

class Constructors : public Applier {
public:
    const char *registry() const override { return "team.constructor"; }
    Applies applies() const override { return Applies::boot; }
    void boot(Store &st, Report &report) override
    {
        std::lock_guard g(mu);
        ensure_stock(st);
        std::map<std::string, Value> keep;
        Lookup lookup = own_lookup(st, keep);
        TeamRefs refs = refs_of(st, lookup);
        report.applied = refs.constructors;
        report.refused = refs.constructors_refused;
        for (auto &l : refs.lines)
            report.lines.push_back(l);
    }
};

class Logos : public Applier {
public:
    const char *registry() const override { return "team.logo"; }
    Applies applies() const override { return Applies::read; }
    void install_read(Store &st, Report &report) override
    {
        std::lock_guard g(mu);
        std::map<std::string, Value> keep;
        Lookup lookup = own_lookup(st, keep);
        for (auto &id : in_load_order(st, "team.logo")) {
            const Value *rec = lookup("team.logo", id);
            if (!rec)
                continue;
            int images = 0, bad = 0;
            for (const char *context : LOGO_CONTEXTS) {
                const Value &v = (*rec)["images"][context];
                if (v.is_string()) {
                    uint64_t size = 0;
                    bool found = runtime::file_size(runtime::under_swofty("addons/" + addon_of(id) + "/" + std::string(v.as_string())), &size) && size;
                    images++;
                    if (!found) {
                        bad++;
                        report.lines.push_back("team.logo " + id + ": " + context + " image " + std::string(v.as_string()) +
                                               " is not in swofty/addons/" + addon_of(id) + "/");
                    }
                } else if (v.is_object()) {
                    images++;
                }
            }
            if (bad)
                report.refused++;
            else
                report.applied++;
            report.lines.push_back("team.logo " + id + ": " + std::to_string(images) + " image(s)");
        }
    }
};

class Boxes : public Applier {
public:
    const char *registry() const override { return "era.box"; }
    Applies applies() const override { return Applies::read; }
    void install_read(Store &st, Report &report) override
    {
        std::map<std::string, Value> keep;
        Lookup lookup = own_lookup(st, keep);
        Plan plan;
        BaseImage base_image = box_base_image(st, lookup);
        for (auto &id : in_load_order(st, "era.box"))
            if (const Value *rec = lookup("era.box", id))
                lower_box(id, *rec, lookup, plan, base_image);
        const SwoftyEra *era = runtime::era_table();
        const SwoftyLang *lang = runtime::lang_table();
        for (auto &b : plan.boxes) {
            if (!b.literal.empty() && lang)
                lang->override_set("*", b.title.c_str(), b.literal.c_str());
            SwoftyEraBox d{sizeof d, b.entrant_list.c_str(), b.title.c_str(), b.image.c_str(), b.order};
            int rc = era ? era->add_box(&d) : SWOFTY_ASSET_UNAVAILABLE;
            if (rc == SWOFTY_ASSET_OK)
                report.applied++;
            else
                report.refused++;
            report.lines.push_back("era.box " + b.id + ": " + b.entrant_list +
                                   (rc == SWOFTY_ASSET_OK ? " registered"
                                                          : " not registered (a box for it exists or the picker is off)"));
        }
        for (auto &l : plan.lines)
            report.lines.push_back(l);
    }
};

}

void register_appliers()
{
    static std::once_flag once;
    std::call_once(once, [] {
        for (const char *reg : ROW_REGISTRIES)
            make<Rows>(reg);
        make<Boxes>();
        make<Constructors>();
        make<Logos>();
        set_up_dresser();
    });
}

}
