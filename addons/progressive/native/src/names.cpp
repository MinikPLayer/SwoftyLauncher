// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career: gamedb labels (names.hpp).

#include <cstdio>
#include <cstring>

#include <swofty/db.hpp>
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include "names.hpp"
#include "seats.hpp"

namespace pg::names {

namespace {

constexpr size_t kLabelMax = 128;
constexpr int64_t kMaxRows = 8192;

struct Table {
    std::unordered_map<uint32_t, std::string> by_stat;
    std::unordered_map<std::string, uint32_t> by_label;
};

Table g_tables[4];
bool g_loaded = false;

Table &table(Kind k)
{
    return g_tables[static_cast<int>(k)];
}

bool text(const std::string &path, std::string &out)
{
    out.clear();
    const char *v = nullptr;
    if (swofty::db::string_at(path, &v) != swofty::db::Result::ok || !v)
        return false;
    size_t n = strnlen(v, kLabelMax);
    if (!n || n >= kLabelMax || !swofty::mem::printable_utf8(v, n))
        return false;
    out.assign(v, n);
    return true;
}

bool integer(const std::string &path, int64_t &out)
{
    return swofty::db::int_at(path, &out) == swofty::db::Result::ok;
}

template <class F> void each_row(const std::string &table, const std::string &stem, F f)
{
    int64_t count = 0;
    if (!integer(table + "._" + stem + "_instancecount_", count) || count <= 0 || count > kMaxRows)
        return;
    for (int64_t i = 0; i < count; ++i)
        f(table + "." + stem + "_" + std::to_string(i));
}

void add(Kind k, const std::string &row)
{
    int64_t id = -1;
    std::string name;
    if (!integer(row + ".stat_id", id) || id < 0 || id >= 0x3fff || !text(row + "._metadata_", name))
        return;
    Table &t = table(k);
    auto s = static_cast<uint32_t>(id);
    t.by_stat.emplace(s, name);
    t.by_label.emplace(name, s);
}

} // namespace

const char *kind_name(Kind k)
{
    switch (k) {
    case Kind::driver:
        return "driver";
    case Kind::team:
        return "team";
    case Kind::vehicle:
        return "vehicle";
    case Kind::look:
        return "look";
    }
    return "?";
}

bool loaded()
{
    return g_loaded;
}

bool load()
{
    if (g_loaded)
        return true;
    if (!swofty::db::available())
        return false;
    for (auto &t : g_tables)
        t = {};
    each_row("entrants.drivers", "driver", [](const std::string &r) { add(Kind::driver, r); });
    each_row("entrants.teams", "team", [](const std::string &r) { add(Kind::team, r); });
    each_row("entrants.vehicles", "vehicle", [](const std::string &r) { add(Kind::vehicle, r); });
    each_row("entrants.driver_customisations", "folder", [](const std::string &folder) {
        each_row(folder, "preloaded_driver_customisation_properties", [](const std::string &r) { add(Kind::look, r); });
    });
    g_loaded = !table(Kind::driver).by_stat.empty() && !table(Kind::vehicle).by_stat.empty();
    swofty::log("[progressive] gamedb labels %s: %zu drivers, %zu teams, %zu vehicles, %zu looks",
                g_loaded ? "loaded" : "NOT loaded (retrying at the next Team HQ)", count(Kind::driver),
                count(Kind::team), count(Kind::vehicle), count(Kind::look));
    return g_loaded;
}

std::string label(Kind k, uint32_t s)
{
    auto &m = table(k).by_stat;
    auto it = m.find(s);
    return it == m.end() ? std::string() : it->second;
}

bool stat(Kind k, const std::string &l, uint32_t &out)
{
    auto &m = table(k).by_label;
    auto it = m.find(l);
    if (it == m.end())
        return false;
    out = it->second;
    return true;
}

bool known(Kind k, uint32_t s)
{
    return table(k).by_stat.count(s) != 0;
}

size_t count(Kind k)
{
    return table(k).by_stat.size();
}

std::string key_text(Kind k, uint32_t key)
{
    if (none(key))
        return "none";
    uint32_t s = stat_of(key);
    std::string l = label(k, s);
    char b[48];
    std::snprintf(b, sizeof b, "(%u%s)", s, (key & 0xffffu) ? "+low" : "");
    return (l.empty() ? std::string("?") : l) + b;
}

} // namespace pg::names
