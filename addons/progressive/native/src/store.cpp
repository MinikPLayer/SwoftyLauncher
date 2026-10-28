// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career: careers.json (store.hpp).

#include <algorithm>
#include <cstdio>
#include <deque>

#include <swofty/mixin.hpp>
#include <swofty/storage.hpp>

#include "../../../../native/src/core/json.hpp"
#include "store.hpp"
#include "store_logic.hpp"

namespace pg::store {

namespace {

constexpr const char *kPath = "careers.json";

std::deque<Career> g_careers;
bool g_loaded = false;

std::string quoted(const std::string &s)
{
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\')
            out += '\\';
        if (static_cast<unsigned char>(c) >= 0x20)
            out += c;
    }
    return out + "\"";
}

} // namespace

bool load()
{
    if (g_loaded)
        return true;
    std::string text;
    swofty::storage::Result r = swofty::storage::read(kPath, text);
    if (r != swofty::storage::Result::ok) {
        g_loaded = swofty::storage::available();
        return g_loaded;
    }
    json::Value j;
    if (!json::parse(text, j) || j.type != json::Value::Obj) {
        swofty::log("[progressive] store: %s is not JSON; starting a new one (the old file is kept as careers.bad.json)",
                    kPath);
        swofty::storage::write("careers.bad.json", text);
        g_loaded = true;
        return true;
    }
    int cleaned = 0;
    for (auto &c : j["careers"].a) {
        Career x;
        x.key = c["key"].str();
        x.driver = static_cast<uint32_t>(c["driver"].num());
        x.seed = static_cast<uint32_t>(c["seed"].num());
        x.start = static_cast<int>(c["start"].num(2026));
        x.rnd = c["rnd"].str("real_life");
        if (x.rnd != "real_life")
            x.rnd = "hybrid";
        x.moves = c["moves"].str("follow_driver");
        x.created_season = static_cast<int>(c["created_season"].num());
        x.applied_season = static_cast<int>(c["applied_season"].num(-1));
        x.applied_year = static_cast<int>(c["applied_year"].num());
        x.test_seasons = static_cast<int>(c["test_seasons"].num());
        x.used = static_cast<uint64_t>(c["used"].num());
        x.team = static_cast<int>(c["team"].num(-1));
        x.season = static_cast<int>(c["season"].num(-1));
        x.mid_year = static_cast<int>(c["mid_year"].num());
        x.mid_round = static_cast<int>(c["mid_round"].num());
        x.mid_applied = c["mid_applied"].str();
        x.mid_tracks = c["mid_tracks"].str();
        x.dev_year = static_cast<int>(c["dev_year"].num());
        x.dev_round = static_cast<int>(c["dev_round"].num(-1));
        x.dev_rounds = static_cast<int>(c["dev_rounds"].num());
        x.dev_applied = c["dev_applied"].str();
        x.trees_season = static_cast<int>(c["trees_season"].num(-1));
        x.ratings_season = static_cast<int>(c["ratings_season"].num(-1));
        x.player_ratings = static_cast<int>(c["player_ratings"].num(0));
        if (x.key.empty())
            continue;
        if (!x.driver && x.applied_season < 0) {
            swofty::log("[progressive] store: dropping %s: driver 0 and never applied (made at a load without a "
                        "player row)",
                        x.key.c_str());
            ++cleaned;
            continue;
        }
        Career *twin = nullptr;
        for (auto &k : g_careers)
            if (k.key == x.key || (k.driver == x.driver && k.start == x.start && k.rnd == x.rnd && k.moves == x.moves &&
                                   k.applied_season == x.applied_season && k.applied_year == x.applied_year &&
                                   k.test_seasons == x.test_seasons))
                twin = &k;
        if (twin) {
            swofty::log("[progressive] store: merging %s into %s (%s: driver %u, start %d, R&D %s, team moves %s, "
                        "applied season %d)",
                        x.key.c_str(), twin->key.c_str(), twin->key == x.key ? "the same key" : "the same career state",
                        x.driver, x.start, x.rnd.c_str(), x.moves.c_str(), x.applied_season);
            if (twin->key == x.key && x.applied_season > twin->applied_season)
                *twin = x;
            else if (twin->key != x.key && x.seed && x.used >= twin->used) {
                twin->seed = x.seed;
                twin->used = x.used;
            }
            ++cleaned;
            continue;
        }
        g_careers.push_back(x);
    }
    for (size_t i = 0; i < g_careers.size(); ++i)
        for (size_t j = 0; j < g_careers.size(); ++j) {
            if (i == j || !progressive::match::split_of(g_careers[i], g_careers[j]))
                continue;
            swofty::log("[progressive] store: folding %s into %s (made at season %d of the same career: driver %u, "
                        "start %d, the split inferred %d; applied season %d -> %d)",
                        g_careers[j].key.c_str(), g_careers[i].key.c_str(), g_careers[j].created_season,
                        g_careers[i].driver, g_careers[i].start, g_careers[j].start, g_careers[i].applied_season,
                        std::max(g_careers[i].applied_season, g_careers[j].applied_season));
            progressive::match::fold(g_careers[i], g_careers[j]);
            g_careers.erase(g_careers.begin() + static_cast<std::ptrdiff_t>(j));
            if (j < i)
                --i;
            j = static_cast<size_t>(-1);
            ++cleaned;
        }
    g_loaded = true;
    swofty::log("[progressive] store: %zu career(s) in %s", g_careers.size(), kPath);
    if (cleaned)
        save();
    return true;
}

bool save()
{
    std::string out = "{\n  \"careers\": [";
    for (size_t i = 0; i < g_careers.size(); ++i) {
        const Career &c = g_careers[i];
        char b[1024];
        std::snprintf(b, sizeof b,
                      "%s\n    {\"key\": %s, \"driver\": %u, \"seed\": %u, \"start\": %d, \"rnd\": %s, \"moves\": %s, "
                      "\"created_season\": %d, \"applied_season\": %d, \"applied_year\": %d, \"test_seasons\": %d, "
                      "\"used\": %llu, \"team\": %d, \"season\": %d, \"dev_year\": %d, \"dev_round\": %d, "
                      "\"dev_rounds\": %d, \"mid_year\": %d, \"mid_round\": %d, \"dev_applied\": ",
                      i ? "," : "", quoted(c.key).c_str(), c.driver, c.seed, c.start, quoted(c.rnd).c_str(),
                      quoted(c.moves).c_str(), c.created_season, c.applied_season, c.applied_year, c.test_seasons,
                      static_cast<unsigned long long>(c.used), c.team, c.season, c.dev_year, c.dev_round, c.dev_rounds,
                      c.mid_year, c.mid_round);
        out += b;
        out += quoted(c.dev_applied) + ", \"mid_applied\": " + quoted(c.mid_applied) +
               ", \"mid_tracks\": " + quoted(c.mid_tracks) + ", \"trees_season\": " + std::to_string(c.trees_season) +
               ", \"ratings_season\": " + std::to_string(c.ratings_season) +
               ", \"player_ratings\": " + std::to_string(c.player_ratings) + "}";
    }
    out += "\n  ]\n}\n";
    swofty::storage::Result r = swofty::storage::write(kPath, out);
    if (r != swofty::storage::Result::ok)
        swofty::log("[progressive] store: writing %s failed (%s)", kPath, swofty::storage::name(r));
    return r == swofty::storage::Result::ok;
}

Career *find(const std::string &key)
{
    for (auto &c : g_careers)
        if (c.key == key)
            return &c;
    return nullptr;
}

std::deque<Career> &all()
{
    return g_careers;
}

std::vector<Career *> by_driver(uint32_t driver)
{
    std::vector<Career *> out;
    for (auto &c : g_careers)
        if (c.driver == driver)
            out.push_back(&c);
    return out;
}

Career *by_seed(uint32_t seed, uint32_t driver)
{
    for (auto &c : g_careers)
        if (seed && c.seed == seed && c.driver == driver)
            return &c;
    return nullptr;
}

Career &add(const Career &c)
{
    g_careers.push_back(c);
    return g_careers.back();
}

std::string free_key(uint32_t seed, uint32_t driver)
{
    char key[64];
    std::snprintf(key, sizeof key, "pg-%08x-%u", seed, driver);
    std::string k = key;
    for (int n = 2; find(k); ++n)
        k = std::string(key) + "-" + std::to_string(n);
    return k;
}

uint64_t next_use()
{
    uint64_t m = 0;
    for (auto &c : g_careers)
        m = std::max(m, c.used);
    return m + 1;
}

} // namespace pg::store
