#include "snapshots.hpp"
#include "clock.hpp"

#include <algorithm>
#include <cstdio>


namespace swofty_core::snapshots {

using swofty::json::Value;
namespace fmt = format;

namespace {

std::string kb(size_t n)
{
    char b[32];
    if (n < 1024)
        snprintf(b, sizeof b, "%zu B", n);
    else
        snprintf(b, sizeof b, "%.1f KB", double(n) / 1024.0);
    return b;
}

std::string owner_of(const std::string &id)
{
    auto at = id.find('/');
    return at == std::string::npos ? std::string() : id.substr(0, at);
}

std::string season_ref(const Value &v)
{
    const Value *s = v.find("season");
    return s && s->is_string() ? std::string(s->as_string()) : std::string();
}

void within_years(const Value &w, std::pair<int, int> &out)
{
    if (const auto *a = w.if_array()) {
        for (auto &e : *a)
            within_years(e, out);
        return;
    }
    if (!w.is_object())
        return;
    const Value *y = w.find("year");
    if (!y)
        return;
    if (y->is_int())
        out = {int(y->as_int()), int(y->as_int())};
    else if (const auto *r = y->if_array(); r && r->size() == 2 && (*r)[0].is_int() && (*r)[1].is_int())
        out = {int((*r)[0].as_int()), int((*r)[1].as_int())};
}

std::string data_group_of(reg::Store &store, const Value &v, const reg::Context &ctx)
{
    if (const Value *g = v.find("data_group"); g && g->is_string())
        return std::string(g->as_string());
    std::string s = season_ref(v);
    if (s.empty())
        return {};
    reg::Record r = store.get_live("career.season", s, ctx);
    if (r.status != reg::Status::ok)
        return {};
    return std::string(r.value["data_group"].as_string());
}

}

std::pair<int, int> years_of(reg::Store &store, const std::string &registry, const Value &v, const reg::Context &ctx)
{
    (void)registry;
    std::pair<int, int> out{0, 0};
    if (const Value *y = v.find("year"); y && y->is_int())
        return {int(y->as_int()), int(y->as_int())};
    if (const Value *s = v.find("season")) {
        if (s->is_int())
            return {int(s->as_int()), int(s->as_int())};
        if (s->is_string()) {
            reg::Record r = store.get_live("career.season", std::string(s->as_string()), ctx);
            if (r.status == reg::Status::ok && r.value["year"].is_int())
                return {int(r.value["year"].as_int()), int(r.value["year"].as_int())};
        }
    }
    if (const Value *w = v.find("within"))
        within_years(*w, out);
    return out;
}

struct Book::Cache {
    struct Decoded {
        bool ok = false;
        Value records;
        std::set<std::string> loose;
        int major = 1;
    };
    std::map<std::pair<int, std::string>, Decoded> seasons;
    std::map<std::string, int> majors;
    std::set<std::string> logged;
};

Book::Book(reg::Store &store, std::function<void(const std::string &)> log)
    : store_(store), log_(std::move(log)), cache_(std::make_unique<Cache>())
{
}

Book::~Book() = default;

void Book::set_ids(Ids ids) { ids_ = std::move(ids); }

std::string Book::idmap(const std::string &career) const { return ids_.map ? ids_.map(career) : std::string("{}"); }

void Book::note(const std::string &s) const
{
    if (log_)
        log_(s);
}

reg::SnapshotSource Book::source()
{
    return [this](const std::string &registry, const std::string &id, const reg::Context &ctx) {
        return read(registry, id, ctx);
    };
}

void Book::refresh()
{
    std::map<std::string, int> majors;
    for (auto &name : store_.registries())
        if (auto d = store_.def(name); d && d->snapshot)
            majors[name] = d->major;
    {
        std::lock_guard g(mu_);
        cache_->majors = std::move(majors);
    }
    store_.set_snapshot_source(source());
}

void Book::loaded(const Bytes *body)
{
    fmt::Entry e;
    fmt::Decoded st = fmt::Decoded::ok;
    std::string why;
    bool have = body != nullptr;
    if (body) {
        Bytes payload;
        if (!fmt::parse_bag(*body, payload, why))
            st = fmt::Decoded::corrupt;
        else
            st = fmt::decode(payload, e, why);
        if (st != fmt::Decoded::ok)
            e = fmt::Entry{};
    }
    size_t careers = 0, seasons = 0, unreadable = 0;
    {
        std::lock_guard g(mu_);
        for (auto &id : dirty_)
            if (fmt::Career *mine = entry_.career(id)) {
                if (fmt::Career *theirs = e.career(id))
                    *theirs = *mine;
                else
                    e.careers.push_back(*mine);
            }
        entry_ = std::move(e);
        loaded_state_ = st;
        have_entry_ = have;
        pending_.clear();
        cache_->seasons.clear();
        careers = entry_.careers.size();
        for (auto &c : entry_.careers) {
            seasons += c.seasons.size();
            unreadable += c.unreadable;
        }
    }
    if (!have)
        note("[registry] swofty_registry: this profile save has none; each career starts its snapshots at its next "
             "season");
    else if (st == fmt::Decoded::newer)
        note("[registry] swofty_registry: " + why + "; kept as is and written back unchanged, and careers read "
             "their registries live");
    else if (st == fmt::Decoded::corrupt)
        note("[registry] swofty_registry: ignored (" + why + "); careers read their registries live, and the next "
             "snapshot replaces it");
    else
        note("[registry] swofty_registry: read " + std::to_string(careers) + " career(s), " + std::to_string(seasons) +
             " season snapshot(s) (" + kb(body->size()) + ")" +
             (unreadable ? "; " + std::to_string(unreadable) + " career(s) unreadable, kept as is" : ""));
    refresh();
}

bool Book::saving(Bytes &body)
{
    std::lock_guard g(mu_);
    if (loaded_state_ == fmt::Decoded::newer || (dirty_.empty() && !test_pad_))
        return false;
    fmt::Entry out = entry_;
    size_t provisional = std::erase_if(out.careers, [](const fmt::Career &c) { return seasons::is_provisional(c.id); });
    if (provisional)
        note("[registry] swofty_registry: " + std::to_string(provisional) +
             " career(s) still on a provisional id are left out of this save");
    if (test_pad_) {
        std::erase_if(out.extra, [](const fmt::Item &i) { return i.tag == fmt::T_TEST_PAD; });
        fmt::Item pad;
        pad.tag = fmt::T_TEST_PAD;
        uint32_t x = 0x9e3779b9u;
        pad.body.resize(test_pad_);
        for (auto &b : pad.body)
            x = x * 1664525u + 1013904223u, b = uint8_t(x >> 24);
        out.extra.push_back(std::move(pad));
    }
    Bytes payload = out.encode();
    if (payload.size() > fmt::ENTRY_CAP) {
        note("[registry] swofty_registry: the entry's payload of " + kb(payload.size()) + " is past the " +
             kb(fmt::ENTRY_CAP) + " cap; not written with this save");
        return false;
    }
    if (!fmt::bag_body(payload, body))
        return false;
    pending_ = dirty_;
    test_pad_ = 0;
    return true;
}

void Book::set_test_pad(size_t bytes)
{
    std::lock_guard g(mu_);
    test_pad_ = bytes;
}

size_t test_pad_bytes(const char *text, bool agent_armed)
{
    if (!agent_armed || !text || !*text)
        return 0;
    size_t n = 0;
    for (const char *c = text; *c; ++c) {
        if (*c < '0' || *c > '9' || n > fmt::ENTRY_CAP)
            return 0;
        n = n * 10 + size_t(*c - '0');
    }
    return n >= 1 && n <= fmt::ENTRY_CAP - (64u << 10) ? n : 0;
}

void Book::written(bool ok)
{
    std::lock_guard g(mu_);
    if (ok)
        for (auto &id : pending_)
            dirty_.erase(id);
    pending_.clear();
}

std::string Book::active() const
{
    std::lock_guard g(mu_);
    return active_;
}

bool Book::has_career(const std::string &id) const
{
    std::lock_guard g(mu_);
    return const_cast<fmt::Entry &>(entry_).career(id) != nullptr;
}

bool Book::rename(const std::string &from, const std::string &to)
{
    if (from.empty() || to.empty() || from == to)
        return false;
    std::lock_guard g(mu_);
    fmt::Career *c = entry_.career(from);
    if (!c || entry_.career(to))
        return false;
    c->id = to;
    dirty_.erase(from);
    dirty_.insert(to);
    if (active_ == from)
        active_ = to;
    cache_->seasons.clear();
    return true;
}

bool Book::started(const std::string &id, int year) const
{
    std::lock_guard g(mu_);
    const fmt::Career *c = const_cast<fmt::Entry &>(entry_).career(id);
    if (!c || c->unreadable || c->freeze != fmt::Freeze::per_season)
        return false;
    const fmt::Season *s = c->season(year);
    return s && !s->frozen.empty();
}

fmt::Entry Book::entry() const
{
    std::lock_guard g(mu_);
    return entry_;
}

std::string Book::state() const
{
    std::lock_guard g(mu_);
    return std::string(have_entry_ ? fmt::decoded_name(loaded_state_) : "none") + (dirty_.empty() ? "" : ", unsaved");
}

void Book::career_loaded(const std::string &id, reg::Context ctx)
{
    if (id.empty())
        return;
    std::string map, line;
    bool adopt = false;
    {
        std::lock_guard g(mu_);
        active_ = id;
        cache_->seasons.clear();
        cache_->logged.clear();
        fmt::Career *c = entry_.career(id);
        if (!c) {
            line = "[registry] career " + id + ": no snapshot in swofty_registry (a career from before 1.6, or a new "
                   "one); it reads live and freezes from its next season";
        } else if (c->unreadable) {
            line = "[registry] career " + id + ": its snapshot is unreadable; kept as is, and it reads live";
        } else {
            std::vector<int> years;
            for (auto &s : c->seasons)
                if (s.year)
                    years.push_back(s.year);
            std::sort(years.begin(), years.end());
            line = "[registry] career " + id + ": " + std::to_string(c->seasons.size()) + " season snapshot(s)" +
                   (years.empty() ? "" : " for " + std::to_string(years.front()) + "-" + std::to_string(years.back())) +
                   " read from swofty_registry (" + c->majors + ", freeze " +
                   (c->freeze == fmt::Freeze::at_creation ? "at creation" : "per season") + ")";
            map = c->idmap;
            adopt = true;
        }
    }
    note(line);
    if (adopt && ids_.adopt) {
        std::vector<std::string> clashes = ids_.adopt(id, map);
        if (!clashes.empty())
            note("[registry] career " + id + ": " + std::to_string(clashes.size()) +
                 " stat id(s) in its saved map clash with this install (see the [guards] lines)");
    }
    reg::Context now = store_.context();
    now.career = id;
    if (!ctx.data_group.empty())
        now.data_group = ctx.data_group;
    store_.set_context(now);
    refresh();
}

void Book::career_unloaded()
{
    {
        std::lock_guard g(mu_);
        active_.clear();
        cache_->seasons.clear();
    }
    reg::Context now = store_.context();
    now.career.clear();
    store_.set_context(now);
    refresh();
}

std::vector<fmt::Frozen> Book::freeze(int year, const reg::Context &ctx, bool yearless, bool only_year,
                                      const fmt::Career *have, std::vector<std::string> &refused,
                                      std::vector<std::pair<std::string, std::string>> &owners, Stats &st)
{
    std::vector<fmt::Frozen> out;
    size_t used = have ? have->size() : 0;
    for (auto &name : store_.registries()) {
        auto def = store_.def(name);
        if (!def || !def->snapshot)
            continue;
        if (have)
            if (const fmt::Season *s = have->season(year); s && s->find(name))
                continue;
        Value records = Value::object(), loose = Value::array(), ranged = Value::array();
        size_t n = 0;
        std::string first;
        for (auto &id : store_.list(name, "")) {
            reg::Record r = store_.get_live(name, id, ctx);
            if (r.status != reg::Status::ok)
                continue;
            if (!ctx.data_group.empty()) {
                std::string dg = data_group_of(store_, r.value, ctx);
                if (!dg.empty() && dg != ctx.data_group)
                    continue;
            }
            auto [from, to] = years_of(store_, name, r.value, ctx);
            bool none = from == 0 && to == 0;
            if (none ? !yearless : (only_year || year < from || year > to))
                continue;
            if (none)
                loose.push_back(Value(id));
            else if (from != to)
                ranged.push_back(Value(id));
            records[id] = std::move(r.value);
            if (first.empty())
                first = id;
            n++;
        }
        if (!n)
            continue;
        Value doc = Value::object({{"records", std::move(records)}, {"loose", std::move(loose)}, {"ranged", std::move(ranged)}});
        std::string raw = swofty::json::dump(doc);
        fmt::Frozen f;
        f.registry = name;
        f.major = uint16_t(def->major), f.minor = uint16_t(def->minor);
        if (!fmt::compress(raw, f.records))
            continue;
        std::string label = name + (year ? " " + std::to_string(year) : " (every season)");
        size_t item = f.encode().size() + 16;
        if (f.records.size() > def->budget) {
            refused.push_back(name);
            owners.push_back({owner_of(first), label + ": snapshot " + kb(f.records.size()) + " compressed, budget " +
                                                   kb(def->budget) + "; not saved, this career reads it live" +
                                                   (year ? " for " + std::to_string(year) : "")});
            st.refused++;
            continue;
        }
        if (used + item > fmt::CAREER_CAP) {
            refused.push_back(name);
            owners.push_back({owner_of(first), label + ": snapshot " + kb(f.records.size()) +
                                                   " compressed would take the career's entry past " +
                                                   kb(fmt::CAREER_CAP) + "; not saved, this career reads it live" +
                                                   (year ? " for " + std::to_string(year) : "")});
            st.refused++;
            continue;
        }
        used += item;
        st.registries++, st.records += n, st.raw += raw.size(), st.compressed += f.records.size();
        out.push_back(std::move(f));
    }
    return out;
}

namespace {

void merge_season(fmt::Career &c, int year, std::vector<fmt::Frozen> frozen, const std::vector<std::string> &refused)
{
    fmt::Season *s = c.season(year);
    if (!s) {
        c.seasons.push_back(fmt::Season{});
        s = &c.seasons.back();
        s->year = year;
    }
    for (auto &f : frozen)
        if (!s->find(f.registry))
            s->frozen.push_back(std::move(f));
    for (auto &r : refused)
        if (std::find(s->refused.begin(), s->refused.end(), r) == s->refused.end())
            s->refused.push_back(r);
}

void merge_majors(fmt::Career &c, const std::vector<fmt::Frozen> &frozen)
{
    swofty::json::Error e;
    Value m = swofty::json::parse(c.majors, &e);
    if (!e.message.empty() || !m.is_object())
        m = Value::object();
    for (auto &f : frozen)
        m[f.registry] = Value(std::to_string(f.major) + "." + std::to_string(f.minor));
    c.majors = swofty::json::dump(m);
}

}

void Book::season_start(int year, reg::Context ctx)
{
    std::string id;
    fmt::Career copy;
    bool newer = false;
    {
        std::lock_guard g(mu_);
        newer = loaded_state_ == fmt::Decoded::newer;
        id = active_;
        if (id.empty()) {
            note("[registry] career season " + std::to_string(year) + " starts with no career loaded; nothing frozen");
            return;
        }
        if (!entry_.career(id)) {
            fmt::Career c;
            c.id = id;
            entry_.careers.push_back(std::move(c));
        }
        copy = *entry_.career(id);
    }
    if (copy.unreadable || newer) {
        note("[registry] career season " + std::to_string(year) + " starts in " + id +
             ": its saved entry is not one this core can extend; nothing frozen, it reads live");
        return;
    }
    if (copy.freeze == fmt::Freeze::at_creation) {
        note("[registry] career season " + std::to_string(year) + " starts in " + id +
             ": frozen at creation; nothing new to freeze");
        return;
    }
    if (const fmt::Season *s = copy.season(year); s && !s->frozen.empty()) {
        note("[registry] career season " + std::to_string(year) + " starts in " + id + ": already frozen (" +
             std::to_string(s->frozen.size()) + " registr" + (s->frozen.size() == 1 ? "y" : "ies") + "); kept");
        return;
    }
    ctx.career = id;
    ctx.year = year;
    ctx.has_year = true;
    std::vector<std::string> refused;
    std::vector<std::pair<std::string, std::string>> owners;
    Stats st;
    auto frozen = freeze(year, ctx, true, false, &copy, refused, owners, st);
    std::string map = idmap(id);
    std::string parts;
    for (auto &f : frozen)
        parts += (parts.empty() ? "" : ", ") + f.registry;
    {
        std::lock_guard g(mu_);
        fmt::Career *c = entry_.career(id);
        if (!c)
            return;
        merge_majors(*c, frozen);
        merge_season(*c, year, std::move(frozen), refused);
        c->idmap = map;
        dirty_.insert(id);
        cache_->seasons.clear();
    }
    note("[registry] career season " + std::to_string(year) + " starts in " + id + (ctx.data_group.empty() ? "" : "/" + ctx.data_group) +
         ": snapshot of " + (parts.empty() ? "nothing" : parts) + " (" + std::to_string(st.records) + " record(s), " +
         kb(st.raw) + " -> " + kb(st.compressed) + " compressed); saved with the next profile save");
    for (auto &[owner, msg] : owners) {
        note("[registry] " + msg);
        store_.note_refusal(owner, "", "", "snapshot_budget", msg);
    }
    refresh();
}

void Book::career_created(const std::string &id, fmt::Freeze freeze_mode, reg::Context ctx)
{
    if (id.empty())
        return;
    bool replaced = false;
    {
        std::lock_guard g(mu_);
        replaced = std::erase_if(entry_.careers, [&](const fmt::Career &c) { return c.id == id; }) > 0;
        fmt::Career c;
        c.id = id;
        c.freeze = freeze_mode;
        entry_.careers.push_back(std::move(c));
        active_ = id;
        cache_->seasons.clear();
        dirty_.insert(id);
    }
    std::string map = idmap(id);
    std::string line = "[registry] career " + id + " created (freeze " +
                       (freeze_mode == fmt::Freeze::at_creation ? "at creation" : "per season") + ")" +
                       (replaced ? "; it replaces an older career of the same id in swofty_registry" : "");
    if (freeze_mode != fmt::Freeze::at_creation) {
        {
            std::lock_guard g(mu_);
            if (fmt::Career *c = entry_.career(id))
                c->idmap = map;
        }
        note(line);
        reg::Context now = store_.context();
        now.career = id;
        if (!ctx.data_group.empty())
            now.data_group = ctx.data_group;
        store_.set_context(now);
        refresh();
        return;
    }
    ctx.career = id;
    std::set<int> years;
    for (auto &name : store_.registries()) {
        auto def = store_.def(name);
        if (!def || !def->snapshot)
            continue;
        for (auto &rid : store_.list(name, "")) {
            reg::Record r = store_.get_live(name, rid, ctx);
            if (r.status != reg::Status::ok)
                continue;
            if (!ctx.data_group.empty()) {
                std::string dg = data_group_of(store_, r.value, ctx);
                if (!dg.empty() && dg != ctx.data_group)
                    continue;
            }
            auto [from, to] = years_of(store_, name, r.value, ctx);
            if (from && to && to - from < 64)
                for (int y = from; y <= to; y++)
                    years.insert(y);
        }
    }
    Stats all;
    std::vector<std::pair<std::string, std::string>> owners;
    std::vector<std::pair<int, std::vector<fmt::Frozen>>> seasons;
    std::vector<std::pair<int, std::vector<std::string>>> refusals;
    fmt::Career running;
    running.id = id;
    for (int y : years) {
        reg::Context c = ctx;
        c.year = y, c.has_year = true;
        std::vector<std::string> refused;
        auto frozen = freeze(y, c, false, false, &running, refused, owners, all);
        merge_season(running, y, frozen, refused);
        seasons.push_back({y, std::move(frozen)});
        refusals.push_back({y, std::move(refused)});
    }
    {
        std::vector<std::string> refused;
        auto frozen = freeze(0, ctx, true, true, &running, refused, owners, all);
        merge_season(running, 0, frozen, refused);
        seasons.push_back({0, std::move(frozen)});
        refusals.push_back({0, std::move(refused)});
    }
    {
        std::lock_guard g(mu_);
        if (fmt::Career *c = entry_.career(id)) {
            for (size_t i = 0; i < seasons.size(); i++) {
                merge_majors(*c, seasons[i].second);
                if (!seasons[i].second.empty() || !refusals[i].second.empty())
                    merge_season(*c, seasons[i].first, std::move(seasons[i].second), refusals[i].second);
            }
            c->idmap = map;
        }
        cache_->seasons.clear();
    }
    note(line + ": froze " + std::to_string(years.size()) + " season(s)" +
         (years.empty() ? "" : " " + std::to_string(*years.begin()) + "-" + std::to_string(*years.rbegin())) + ", " +
         std::to_string(all.registries) + " registry snapshot(s), " + std::to_string(all.records) + " record(s), " +
         kb(all.raw) + " -> " + kb(all.compressed) + " compressed");
    for (auto &[owner, msg] : owners) {
        note("[registry] " + msg);
        store_.note_refusal(owner, "", "", "snapshot_budget", msg);
    }
    reg::Context now = store_.context();
    now.career = id;
    if (!ctx.data_group.empty())
        now.data_group = ctx.data_group;
    store_.set_context(now);
    refresh();
}

reg::Frozen Book::read(const std::string &registry, const std::string &id, const reg::Context &ctx)
{
    reg::Frozen out;
    std::lock_guard g(mu_);
    if (active_.empty() || ctx.career != active_)
        return out;
    fmt::Career *c = entry_.career(active_);
    if (!c || c->unreadable || c->seasons.empty())
        return out;
    auto known = cache_->majors.find(registry);
    int current = known == cache_->majors.end() ? 0 : known->second;
    int latest = 0;
    for (auto &s : c->seasons)
        latest = std::max(latest, s.year);
    int cur = ctx.has_year ? ctx.year : latest;
    auto decoded = [&](const fmt::Season &s) -> Cache::Decoded * {
        const fmt::Frozen *f = s.find(registry);
        if (!f)
            return nullptr;
        auto key = std::make_pair(s.year, registry);
        auto it = cache_->seasons.find(key);
        if (it == cache_->seasons.end()) {
            Cache::Decoded d;
            d.major = f->major;
            std::string raw;
            if (current && f->major > current) {
                if (cache_->logged.insert(registry + "@" + std::to_string(s.year)).second)
                    note("[registry] career " + active_ + ": " + registry + " " + std::to_string(s.year) +
                         " was frozen with schema " + std::to_string(f->major) + "." + std::to_string(f->minor) +
                         ", newer than this core's " + std::to_string(current) + "; kept in the save, read live");
            } else if (fmt::decompress(f->records, raw)) {
                swofty::json::Error e;
                Value doc = swofty::json::parse(raw, &e);
                if (e.message.empty() && doc.is_object() && doc["records"].is_object()) {
                    d.ok = true;
                    d.records = doc["records"];
                    for (const char *list : {"loose", "ranged"})
                        if (auto *a = doc[list].if_array())
                            for (auto &v : *a)
                                if (v.is_string())
                                    d.loose.insert(std::string(v.as_string()));
                }
            }
            if (!d.ok && !(current && f->major > current) &&
                cache_->logged.insert(registry + "#" + std::to_string(s.year)).second)
                note("[registry] career " + active_ + ": " + registry + " " + std::to_string(s.year) +
                     " snapshot does not decode; this career reads it live");
            it = cache_->seasons.emplace(key, std::move(d)).first;
        }
        return it->second.ok ? &it->second : nullptr;
    };
    auto hit = [&](Cache::Decoded *d) {
        const Value *v = d ? d->records.find(id) : nullptr;
        if (!v)
            return false;
        out.found = true;
        out.value = *v;
        out.major = d->major;
        return true;
    };
    if (const fmt::Season *s = c->season(cur); s && cur && hit(decoded(*s)))
        return out;
    for (auto &s : c->seasons) {
        if (!s.year || s.year == cur)
            continue;
        Cache::Decoded *d = decoded(s);
        if (d && !d->loose.count(id) && hit(d))
            return out;
    }
    if (const fmt::Season *s = c->season(0); s && hit(decoded(*s)))
        return out;
    return out;
}

}
