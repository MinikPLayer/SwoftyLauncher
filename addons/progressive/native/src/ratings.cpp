// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career: each season's real driver ratings (ratings.hpp, ratings_logic.hpp).

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <swofty/career.hpp>
#include <swofty/game.hpp>
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>
#include <swofty/storage.hpp>

#include "names.hpp"
#include "stats_mem.hpp"
#include "ratings.hpp"
#include "ratings_logic.hpp"

SWOFTY_SETTING(bool, season_ratings, true);

namespace pg::ratings {

namespace {

using namespace swofty::game;
using namespace progressive::ratings;

using namespace pg::stats;

Data g_ratings;
int g_state = 0;
std::string g_said;

bool data()
{
    if (g_state)
        return g_state > 0;
    std::string text, err;
    if (swofty::storage::read("data/years.json", text) != swofty::storage::Result::ok) {
        swofty::log("[progressive] ratings: data/years.json is missing; drivers keep the game's ratings");
        g_state = -1;
        return false;
    }
    bool ok = parse(text, g_ratings, err);
    g_state = ok ? 1 : -1;
    size_t n = 0;
    for (auto &[y, ds] : g_ratings.years)
        n += ds.size();
    swofty::log("[progressive] ratings: years.json ratings %s (%zu years, %zu driver seasons)%s%s",
                ok ? "loaded" : "REFUSED", g_ratings.years.size(), n, err.empty() ? "" : ": ", err.c_str());
    return ok;
}

std::string values_text(const std::array<double, kStats> &v)
{
    std::string out;
    for (int i = 0; i < kStats; ++i) {
        char b[24];
        std::snprintf(b, sizeof b, "%s%s %.0f", i ? " " : "", kShort[i], v[i]);
        out += b;
    }
    return out;
}

void say_once(const std::string &key, const char *fmt, const std::string &a, const std::string &b)
{
    if (g_said == key)
        return;
    g_said = key;
    swofty::log(fmt, a.c_str(), b.c_str());
}

} // namespace

void season(uintptr_t, store::Career *c, uint32_t index, uint32_t player, const std::vector<uint32_t> &movers)
{
    if (!c || !data())
        return;
    Input in;
    in.start = c->start;
    in.index = index;
    in.player = player;
    in.created_season = c->created_season;
    in.player_ratings = c->player_ratings;
    in.ratings_season = c->ratings_season;
    in.enabled = season_ratings;
    in.movers = movers;
    auto stat_of = [](const std::string &label) {
        uint32_t s = 0;
        return names::stat(names::Kind::driver, label, s) ? static_cast<int>(s) : -1;
    };
    Plan p = plan(g_ratings, in, stat_of);
    std::string notes;
    for (auto &n : p.notes)
        notes += "; " + n;
    std::string key = c->key + "|" + std::to_string(index) + "|" + std::to_string(c->player_ratings) + "|" +
                      std::to_string(c->ratings_season) + "|" + notes + p.player_note;
    if (p.targets.empty()) {
        say_once(key, "[progressive] ratings: nothing to write%s; %s", notes, p.player_note);
        if (p.first && in.enabled) {
            c->ratings_season = static_cast<int>(index);
            store::save();
        }
        return;
    }
    Comp comp;
    const char *why = "";
    if (!find_comp(comp, &why)) {
        say_once(key, "[progressive] ratings: NOT written: %s%s", why, notes);
        return;
    }
    struct Pending {
        const Target *t;
        uintptr_t driver;
        uint32_t key;
        std::array<double, kStats> before{};
        std::array<bool, kStats> have{};
        float overall_before = NAN;
    };
    std::vector<Pending> todo;
    int kept = 0, missing = 0;
    bool player_kept = false;
    std::string missing_text;
    for (auto &t : p.targets) {
        uint32_t k = t.stat << 16;
        uintptr_t d = find_driver(comp, k);
        if (!d) {
            ++missing;
            missing_text += (missing_text.empty() ? "" : ", ") + t.driver;
            continue;
        }
        Pending q{&t, d, k};
        float x = progress(comp, k);
        for (int i = 0; i < kStats; ++i)
            q.have[i] = base_now(comp, k, d, static_cast<uint16_t>(g_ratings.stats[i].stat_id), x, q.before[i]);
        if (!needs(t, q.before, q.have)) {
            ++kept;
            if (t.player) {
                player_kept = true;
                swofty::log("[progressive] ratings %d: the player's driver %s(%u) already has his %d ratings (%s); "
                            "never written again",
                            p.year, t.driver.c_str(), t.stat, p.year, values_text(q.before).c_str());
            }
            continue;
        }
        q.overall_before = overall_now(comp, k);
        todo.push_back(q);
    }
    if (todo.empty()) {
        if (p.first || !missing_text.empty())
            say_once(key, "[progressive] ratings %s: every driver already has his ratings%s", std::to_string(p.year),
                     missing ? " (not in the career's statistics table: " + missing_text + ")" : std::string());
        if (player_kept)
            c->player_ratings = p.year;
        if (p.first)
            c->ratings_season = static_cast<int>(index);
        if (p.first || player_kept)
            store::save();
        return;
    }
    std::vector<uint32_t> others;
    for (uint32_t k : all_keys(comp)) {
        bool target = false;
        for (auto &q : todo)
            target |= q.key == k;
        if (!target)
            others.push_back(k);
    }
    std::vector<float> before_others;
    for (uint32_t k : others)
        for (int i = 0; i < kStats; ++i)
            before_others.push_back(shown(comp, k, static_cast<uint16_t>(g_ratings.stats[i].stat_id), false));
    std::vector<Undo> log;
    std::vector<int> sets(todo.size(), 0);
    for (size_t j = 0; j < todo.size(); ++j) {
        auto &q = todo[j];
        float x = progress(comp, q.key);
        for (int i = 0; i < kStats; ++i) {
            if (!q.have[i])
                continue;
            int n = write_stat(comp, q.key, q.driver, static_cast<uint16_t>(g_ratings.stats[i].stat_id),
                               q.t->values.v[i], x, log);
            if (n < 0) {
                roll_back(log);
                swofty::log("[progressive] ratings %d: WRITE FAILED for %s (%s): curve empty or memory not writable; "
                            "every write of this batch rolled back",
                            p.year, q.t->driver.c_str(), kNames[i]);
                return;
            }
            sets[j] = std::max(sets[j], n);
        }
    }
    size_t at = 0;
    for (uint32_t k : others)
        for (int i = 0; i < kStats; ++i) {
            float now = shown(comp, k, static_cast<uint16_t>(g_ratings.stats[i].stat_id), false);
            float was = before_others[at++];
            if (!(std::fabs(now - was) < 1e-3f || (std::isnan(now) && std::isnan(was)))) {
                roll_back(log);
                swofty::log("[progressive] ratings %d: WARNING driver key 0x%x %s moved %.2f -> %.2f although he was "
                            "not written; the batch is rolled back",
                            p.year, k, kNames[i], was, now);
                return;
            }
        }
    int wrote = 0, ai = 0;
    bool player_done = false;
    for (size_t j = 0; j < todo.size(); ++j) {
        auto &q = todo[j];
        float x = progress(comp, q.key);
        std::array<double, kStats> after{};
        std::array<double, kStats> show{};
        for (int i = 0; i < kStats; ++i) {
            uint16_t info = static_cast<uint16_t>(g_ratings.stats[i].stat_id);
            after[i] = q.before[i];
            base_now(comp, q.key, q.driver, info, x, after[i]);
            show[i] = shown(comp, q.key, info, true);
        }
        ++wrote;
        if (q.t->player)
            player_done = true;
        else
            ++ai;
        std::string who = q.t->player ? "the player's driver " : q.t->mover ? "mid-season, coming in: " : "";
        swofty::log("[progressive] ratings %d: %s%s(%u) base %s -> %s (shown %s), overall %.0f -> %.0f (data %.1f; %s; "
                    "%d set(s), curve position %.3f)",
                    p.year, who.c_str(), q.t->driver.c_str(), q.t->stat, values_text(q.before).c_str(),
                    values_text(after).c_str(), values_text(show).c_str(), q.overall_before, overall_now(comp, q.key),
                    q.t->values.overall, q.t->values.source.c_str(), sets[j], x);
    }
    swofty::log("[progressive] ratings %d: %d driver(s) written (%d AI%s), %d already at their %d ratings, %zu other "
                "driver(s) unchanged (%s)%s%s; %s",
                p.year, wrote, ai, player_done ? " + the player" : "", kept, p.year, others.size(),
                comp.player_class ? "DriverCareerPlayerDriverStatistics" : "DriverStatistics",
                missing ? ", not in the career's statistics table: " : "", missing_text.c_str(), p.player_note.c_str());
    g_said = key;
    if (player_done || player_kept)
        c->player_ratings = p.year;
    if (p.first)
        c->ratings_season = static_cast<int>(index);
    store::save();
}

} // namespace pg::ratings
