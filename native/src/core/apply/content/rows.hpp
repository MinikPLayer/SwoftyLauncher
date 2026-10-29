#pragma once

#include "gvdb.hpp"

#include <swofty/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// The row engine: database operations (add-row, set, list-append, list-remove, put-record) applied to a view of
// the game database, with the launcher's semantics (packages/addon-api/src/ops.rs), turned into single records. A
// copied row takes every key the source row holds (its row shape's keys in row_keys.inc, nested tables included)
// plus the plan's fields list for the table, with the set values over the source's text and tag, at the table's
// next index, and the table's instance count goes up by one. The view is the database the game loaded (a Base) under
// the records the engine has written; every key keeps its last value and the addon that wrote it. A key one record
// created is never written by another: that operation fails.

namespace swofty_core::apply::content::rows {

using Value = swofty::json::Value;

struct Rec {
    int tag = -1;
    std::string text;
};

using Base = std::function<std::optional<Rec>(uint64_t key)>;

struct Write {
    std::string path;
    uint64_t key = 0;
    int tag = -1;
    std::string text, owner, registry, record;
    bool fresh = false;
};

#include "row_keys.inc"

class Keys {
public:
    static const Keys &builtin()
    {
        static const Keys k = [] {
            Keys out;
            std::string all;
            for (const char *chunk : ROW_KEYS)
                all += chunk;
            out.parse(all);
            return out;
        }();
        return k;
    }

    void parse(std::string_view text)
    {
        std::vector<std::string> *current = nullptr;
        std::size_t at = 0;
        while (at <= text.size()) {
            std::size_t end = text.find('\n', at);
            if (end == std::string_view::npos)
                end = text.size();
            std::string_view line = text.substr(at, end - at);
            at = end + 1;
            if (line.empty() || line[0] == '#')
                continue;
            if (line[0] == '@')
                current = &shapes_[std::string(line.substr(1))];
            else if (current)
                current->emplace_back(line);
        }
    }

    const std::vector<std::string> *get(const std::string &shape) const
    {
        auto it = shapes_.find(shape);
        return it == shapes_.end() ? nullptr : &it->second;
    }

private:
    std::unordered_map<std::string, std::vector<std::string>> shapes_;
};

inline std::string shape_of(std::string_view path)
{
    std::string out;
    std::size_t at = 0;
    while (at <= path.size()) {
        std::size_t end = path.find('.', at);
        if (end == std::string_view::npos)
            end = path.size();
        std::string_view seg = path.substr(at, end - at);
        auto us = seg.rfind('_');
        bool indexed = us != std::string_view::npos && us + 1 < seg.size();
        for (std::size_t i = us + 1; indexed && i < seg.size(); i++)
            indexed = seg[i] >= '0' && seg[i] <= '9';
        if (!out.empty())
            out += '.';
        if (indexed) {
            out += seg.substr(0, us);
            out += "_N";
        } else {
            out += seg;
        }
        at = end + 1;
    }
    return out;
}

inline std::string count_path(const std::string &table, const std::string &row)
{
    return table + "._" + row + "_instancecount_";
}

inline std::optional<int64_t> parse_int(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
        s.remove_suffix(1);
    if (s.empty())
        return std::nullopt;
    std::size_t i = s[0] == '-' || s[0] == '+' ? 1 : 0;
    if (i == s.size())
        return std::nullopt;
    for (std::size_t k = i; k < s.size(); k++)
        if (s[k] < '0' || s[k] > '9')
            return std::nullopt;
    return std::strtoll(std::string(s).c_str(), nullptr, 10);
}

class Engine {
public:
    explicit Engine(Base base, const Keys &keys = Keys::builtin()) : base_(std::move(base)), keys_(&keys) {}

    void add_fields(const Value &fields)
    {
        if (auto o = fields.if_object())
            for (auto &[table, list] : *o) {
                auto &into = fields_[std::string(table)];
                for (std::size_t i = 0; i < list.size(); i++)
                    if (list[i].is_string() && std::find(into.begin(), into.end(), list[i].as_string()) == into.end())
                        into.emplace_back(list[i].as_string());
            }
    }

    void attribute(std::string owner, std::string registry, std::string record = {})
    {
        owner_ = std::move(owner);
        registry_ = std::move(registry);
        record_ = std::move(record);
    }

    std::optional<Rec> get(uint64_t key) const
    {
        auto it = index_.find(key);
        if (it != index_.end())
            return Rec{out_[it->second].tag, out_[it->second].text};
        return base_ ? base_(key) : std::nullopt;
    }

    std::optional<std::string> text(const std::string &path) const
    {
        auto r = get(gvdb::fnv64(path));
        if (!r)
            return std::nullopt;
        return r->text;
    }

    bool has(const std::string &path) const { return get(gvdb::fnv64(path)).has_value(); }

    std::optional<int64_t> instance_count(const std::string &table, const std::string &row) const
    {
        auto t = text(count_path(table, row));
        return t ? parse_int(*t) : std::nullopt;
    }

    // Resolves a path with [field=value] selectors to the row path it names.
    std::optional<std::string> resolve(const std::string &path, std::string *why = nullptr) const
    {
        if (path.find('[') == std::string::npos)
            return path;
        std::string base;
        bool have = false;
        std::string_view rest = path;
        while (true) {
            std::size_t end = rest.find_first_of(".[");
            if (end == std::string_view::npos)
                end = rest.size();
            std::string_view name = rest.substr(0, end);
            std::string_view after = rest.substr(end);
            if (name.empty())
                return fail(why, path + ": empty segment");
            if (after.empty() || after[0] == '.') {
                base = have ? base + "." + std::string(name) : std::string(name);
                have = true;
                if (after.empty())
                    break;
                rest = after.substr(1);
                continue;
            }
            std::string_view inner = after.substr(1);
            std::size_t close = std::string_view::npos;
            for (std::size_t i = 0; i < inner.size(); i++)
                if (inner[i] == ']' && (i + 1 == inner.size() || inner[i + 1] == '.')) {
                    close = i;
                    break;
                }
            if (close == std::string_view::npos)
                return fail(why, path + ": [ needs a ] at the end of its segment");
            std::string_view sel = inner.substr(0, close);
            auto eq = sel.find('=');
            if (eq == std::string_view::npos)
                return fail(why, path + ": bad selector [" + std::string(sel) + "]");
            std::string field = trim(sel.substr(0, eq)), want(sel.substr(eq + 1));
            std::string table = have ? base : std::string(name), row = have ? std::string(name) : std::string();
            std::size_t hits = 0;
            std::string hit;
            auto count = row.empty() ? std::nullopt : instance_count(table, row);
            for (int64_t idx = 0; idx < (count ? *count : 4096); idx++) {
                std::string cand = row.empty() ? table + std::to_string(idx) : table + "." + row + "_" + std::to_string(idx);
                if (!has(cand + "._metadata_")) {
                    if (!count)
                        break;
                    continue;
                }
                auto v = text(cand + "." + field);
                if (v && *v == want) {
                    hits++;
                    hit = cand;
                }
            }
            if (hits != 1)
                return fail(why, std::string(path.substr(0, path.size() - inner.size() + close + 1)) + ": " +
                                     (hits ? std::to_string(hits) + " rows match" : std::string("no row matches")));
            base = hit;
            have = true;
            std::string_view tail = inner.substr(close + 1);
            if (tail.empty())
                break;
            rest = tail.substr(1);
        }
        return base;
    }

    // Applies one operation. On failure it writes nothing and returns why; on success an empty string.
    std::string apply(const Value &op)
    {
        std::string kind(op["op"].as_string());
        std::vector<Write> w;
        std::string why;
        if (kind == "add-row")
            why = add_row(op, w);
        else if (kind == "set")
            why = set(op, w);
        else if (kind == "list-append" || kind == "list-remove")
            why = list(op, kind == "list-append", w);
        else if (kind == "put-record")
            why = put(op, w);
        else
            why = "operation " + kind + " is not one the row engine applies";
        if (!why.empty())
            return why;
        for (auto &x : w)
            if (auto it = index_.find(x.key); it != index_.end()) {
                const Write &had = out_[it->second];
                if (had.fresh && !had.record.empty() && !record_.empty() && had.record != record_)
                    return x.path + ": " + had.record + " created it, and a record never writes another's rows";
            }
        for (auto &x : w)
            write(std::move(x));
        return {};
    }

    // Everything written since mark() can be taken back with rollback(mark).
    std::size_t mark() const { return undo_.size(); }

    void rollback(std::size_t to)
    {
        while (undo_.size() > to) {
            Undo u = std::move(undo_.back());
            undo_.pop_back();
            if (u.before) {
                out_[index_[u.key]] = std::move(*u.before);
            } else {
                index_.erase(u.key);
                out_.pop_back();
            }
        }
    }

    void commit() { undo_.clear(); }

    const std::vector<Write> &writes() const { return out_; }
    std::vector<std::string> lines;

private:
    struct Undo {
        uint64_t key;
        std::optional<Write> before;
    };

    static std::nullopt_t fail(std::string *why, std::string text)
    {
        if (why)
            *why = std::move(text);
        return std::nullopt;
    }

    static std::string trim(std::string_view s)
    {
        while (!s.empty() && s.front() == ' ')
            s.remove_prefix(1);
        while (!s.empty() && s.back() == ' ')
            s.remove_suffix(1);
        return std::string(s);
    }

    void write(Write w)
    {
        auto it = index_.find(w.key);
        if (it != index_.end()) {
            Write &old = out_[it->second];
            undo_.push_back({w.key, old});
            w.fresh = old.fresh;
            if (old.fresh)
                w.record = old.record;
            old = std::move(w);
            return;
        }
        undo_.push_back({w.key, std::nullopt});
        w.fresh = !(base_ && base_(w.key));
        index_[w.key] = out_.size();
        out_.push_back(std::move(w));
    }

    Write make(const std::string &path, int tag, std::string text) const
    {
        Write w;
        w.path = path;
        w.key = gvdb::fnv64(path);
        w.tag = tag;
        w.text = std::move(text);
        w.owner = owner_;
        w.registry = registry_;
        w.record = record_;
        return w;
    }

    std::string field_path(const std::string &select, const std::string &field, std::string &why) const
    {
        if (field.find('[') != std::string::npos) {
            auto p = resolve(select + "." + field, &why);
            return p ? *p : std::string();
        }
        auto row = resolve(select, &why);
        return row ? *row + "." + field : std::string();
    }

    std::string set(const Value &op, std::vector<Write> &w) const
    {
        std::string select(op["select"].as_string()), field(op["field"].as_string());
        if (!op["value"].is_string())
            return select + "." + field + ": set needs value";
        std::string why;
        std::string path = field_path(select, field, why);
        if (path.empty())
            return why;
        auto old = get(gvdb::fnv64(path));
        if (!old) {
            if (!op["create"].as_bool() || !op["type"].is_int())
                return select + "." + field + ": no such key";
            w.push_back(make(path, int(op["type"].as_int()), std::string(op["value"].as_string())));
            return {};
        }
        w.push_back(make(path, old->tag, std::string(op["value"].as_string())));
        return {};
    }

    static std::vector<std::string> items(std::string_view text)
    {
        std::vector<std::string> out;
        std::size_t at = 0;
        while (at <= text.size()) {
            std::size_t end = text.find(',', at);
            if (end == std::string_view::npos)
                end = text.size();
            std::string item = trim(text.substr(at, end - at));
            if (!item.empty())
                out.push_back(std::move(item));
            at = end + 1;
        }
        return out;
    }

    std::string list(const Value &op, bool append, std::vector<Write> &w) const
    {
        std::string select(op["select"].as_string()), field(op["field"].as_string());
        std::string why;
        std::string path = field_path(select, field, why);
        if (path.empty())
            return why;
        auto old = get(gvdb::fnv64(path));
        if (!old)
            return select + "." + field + ": no such key";
        std::vector<std::string> have = items(old->text);
        for (auto &v : items(op["value"].as_string())) {
            auto at = std::find(have.begin(), have.end(), v);
            if (append && at == have.end())
                have.push_back(v);
            else if (!append)
                have.erase(std::remove(have.begin(), have.end(), v), have.end());
        }
        std::string text;
        for (auto &v : have)
            text += (text.empty() ? "" : ",") + v;
        w.push_back(make(path, old->tag, text));
        return {};
    }

    std::string put(const Value &op, std::vector<Write> &w) const
    {
        std::string key(op["key"].as_string()), hex(op["value"].as_string());
        if (key.empty() || key.rfind("0x", 0) == 0)
            return key + ": the row engine writes keys by path";
        if (hex.size() % 2)
            return key + ": value must be hex bytes";
        std::string text;
        for (std::size_t i = 0; i < hex.size(); i += 2) {
            auto nib = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
            int a = nib(hex[i]), b = nib(hex[i + 1]);
            if (a < 0 || b < 0)
                return key + ": value must be hex bytes";
            text += char(a * 16 + b);
        }
        std::string why;
        auto path = key.find('[') == std::string::npos ? std::optional<std::string>(key) : resolve(key, &why);
        if (!path)
            return why;
        w.push_back(make(*path, int(op["type"].as_int()), text));
        return {};
    }

    static std::string row_word(const std::string &table, const std::string &from)
    {
        if (from.size() <= table.size() + 1 || from.compare(0, table.size(), table) || from[table.size()] != '.')
            return {};
        std::string r = from.substr(table.size() + 1);
        r = r.substr(0, r.find_first_of(".["));
        auto us = r.rfind('_');
        if (us != std::string::npos && us + 1 < r.size() &&
            r.find_first_not_of("0123456789", us + 1) == std::string::npos)
            r = r.substr(0, us);
        return r;
    }

    void walk(const std::string &path, const std::string &rel, std::set<std::string> &keys,
              std::set<std::string> &uncatalogued) const
    {
        std::string shape = shape_of(path);
        const std::vector<std::string> *entries = keys_->get(shape);
        if (!entries) {
            uncatalogued.insert(shape);
            return;
        }
        for (auto &entry : *entries) {
            if (entry.empty() || entry[0] != '+') {
                if (has(path + "." + entry))
                    keys.insert(rel + entry);
                continue;
            }
            std::string sub = entry.substr(1), head, word = sub;
            auto dot = sub.rfind('.');
            if (dot != std::string::npos) {
                head = sub.substr(0, dot + 1);
                word = sub.substr(dot + 1);
            }
            std::string container = path + "." + head;
            if (!container.empty() && container.back() == '.')
                container.pop_back();
            int64_t rows = 0;
            if (auto n = instance_count(container, word)) {
                keys.insert(rel + head + "_" + word + "_instancecount_");
                rows = *n;
            } else {
                while (rows < 4096 && has(container + "." + word + "_" + std::to_string(rows) + "._metadata_"))
                    rows++;
            }
            for (int64_t i = 0; i < rows; i++)
                walk(container + "." + word + "_" + std::to_string(i), rel + head + word + "_" + std::to_string(i) + ".",
                     keys, uncatalogued);
        }
    }

    std::vector<int64_t> stat_ids(const std::string &table, const std::string &row, int64_t rows) const
    {
        std::vector<int64_t> out;
        for (int64_t i = 0; i < rows; i++)
            if (auto t = text(table + "." + row + "_" + std::to_string(i) + ".stat_id"))
                if (auto v = parse_int(*t))
                    out.push_back(*v);
        return out;
    }

    std::string add_row(const Value &op, std::vector<Write> &w)
    {
        std::string table_sel(op["table"].as_string()), from(op["from"].as_string());
        std::string why;
        auto src = resolve(from, &why);
        if (!src)
            return why;
        auto table = resolve(table_sel, &why);
        if (!table)
            return why;
        std::string row = row_word(*table, *src);
        if (row.empty())
            return from + " is not a row of " + table_sel;
        auto count = instance_count(*table, row);
        if (!count)
            return count_path(*table, row) + ": missing; add-row needs the table's real path";
        const std::vector<std::string> *listed = nullptr;
        if (auto it = fields_.find(table_sel + "." + row); it != fields_.end())
            listed = &it->second;
        else if (auto it2 = fields_.find(*table + "." + row); it2 != fields_.end())
            listed = &it2->second;
        std::set<std::string> found, uncatalogued;
        walk(*src, "", found, uncatalogued);
        if (!uncatalogued.empty() && !listed)
            return "add-row " + *table + "." + row + ": no row keys for " + *uncatalogued.begin() +
                   " in this core; the row is not copied";
        std::set<std::string> fields = found;
        if (listed)
            fields.insert(listed->begin(), listed->end());
        const Value &set = op["set"];
        if (auto o = set.if_object())
            for (auto &[f, v] : *o)
                if (!fields.count(std::string(f)))
                    return *table + "." + row + ": unknown field " + std::string(f);
        std::string dst = *table + "." + row + "_" + std::to_string(*count);
        std::vector<Write> rows;
        for (auto &f : fields) {
            uint64_t k = gvdb::fnv64(dst + "." + f);
            if (index_.count(k))
                return dst + "." + f + " already exists";
            auto s = get(gvdb::fnv64(*src + "." + f));
            if (!s)
                return from + ": has no " + f;
            std::string value = set[f].is_string() ? std::string(set[f].as_string()) : s->text;
            rows.push_back(make(dst + "." + f, s->tag, value));
        }
        bool top = shape_of(dst).find("_N") == shape_of(dst).rfind("_N");
        if (!set["stat_id"].is_string() && top && fields.count("stat_id")) {
            auto ids = stat_ids(*table, row, *count);
            std::set<int64_t> unique(ids.begin(), ids.end());
            if (!ids.empty() && unique.size() == ids.size())
                for (auto &r : rows)
                    if (r.path == dst + ".stat_id") {
                        std::string old = r.text;
                        r.text = std::to_string(*unique.rbegin() + 1);
                        lines.push_back("add-row " + dst + " (from " + from + "): stat_id " + r.text +
                                        " (the source's is " + old + ")");
                    }
        }
        auto cnt = get(gvdb::fnv64(count_path(*table, row)));
        rows.push_back(make(count_path(*table, row), cnt ? cnt->tag : 0, std::to_string(*count + 1)));
        for (auto &r : rows)
            w.push_back(std::move(r));
        return {};
    }

    Base base_;
    const Keys *keys_;
    std::map<std::string, std::vector<std::string>> fields_;
    std::string owner_, registry_, record_;
    std::unordered_map<uint64_t, std::size_t> index_;
    std::vector<Write> out_;
    std::vector<Undo> undo_;
};

}
