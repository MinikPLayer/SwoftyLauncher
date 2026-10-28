// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career - the start season's driver ratings in the new-career wizard (wizard_ratings.hpp).

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <swofty/storage.hpp>

#include "../names.hpp"
#include "../ratings_logic.hpp"
#include "../stats_mem.hpp"
#include "wizard_ratings.hpp"

namespace pg::wizard_ratings {

namespace {

using namespace pg::stats;
namespace pr = progressive::ratings;

struct Kept {
    uintptr_t pts = 0;
    std::vector<float> original, written;
    uintptr_t stat = 0;
    float level_original = 0, level_written = 0;
    bool curve = true;
};

pr::Data g_data;
int g_state = 0;
uintptr_t g_comp = 0;
int g_year = 0;
std::map<uintptr_t, Kept> g_kept;
int g_logged = 0;

bool data()
{
    if (g_state)
        return g_state > 0;
    std::string text, err;
    if (swofty::storage::read("data/years.json", text) != swofty::storage::Result::ok) {
        swofty::log("[progressive] wizard ratings: data/years.json is missing; Choose Driver shows the game's ratings");
        g_state = -1;
        return false;
    }
    g_state = pr::parse(text, g_data, err) ? 1 : -1;
    if (g_state < 0)
        swofty::log("[progressive] wizard ratings: years.json ratings REFUSED: %s", err.c_str());
    return g_state > 0;
}

bool read_ys(uintptr_t curve, std::vector<float> &ys, uintptr_t &pts)
{
    CurveView v;
    if (!curve_view(curve, v) || !curve_read_y(v, ys))
        return false;
    pts = v.pts;
    return true;
}

} // namespace

void show(int year, const char *when)
{
    if (!year || !data() || !names::load())
        return;
    Comp comp;
    const char *why = "";
    if (!find_comp(comp, &why)) {
        if (g_logged++ < 8)
            swofty::log("[progressive] wizard ratings %d (%s): not yet: %s", year, when, why);
        return;
    }
    if (comp.at == g_comp && year == g_year) {
        size_t rewritten = 0;
        for (auto it = g_kept.begin(); it != g_kept.end();) {
            std::vector<float> now;
            uintptr_t pts = 0;
            bool intact = it->second.curve ? read_ys(it->first, now, pts) && pts == it->second.pts &&
                                                 now == it->second.written
                                           : rdv<float>(it->second.stat + kStatLevel) == it->second.level_written;
            if (intact) {
                ++it;
                continue;
            }
            ++rewritten;
            it = g_kept.erase(it);
        }
        if (!rewritten)
            return;
        swofty::log("[progressive] wizard ratings %d (%s): the game rewrote %zu curve(s); showing the %d ratings again",
                    year, when, rewritten, year);
    }
    if (g_comp && comp.at != g_comp) {
        swofty::log("[progressive] wizard ratings: the statistics component changed (0x%llx -> 0x%llx); %zu kept "
                    "curve(s) dropped",
                    static_cast<unsigned long long>(g_comp), static_cast<unsigned long long>(comp.at), g_kept.size());
        g_kept.clear();
    }
    auto y = g_data.years.find(year);
    if (y == g_data.years.end())
        return;
    int drivers = 0, missing = 0, failed = 0;
    std::string sample;
    for (auto &[label, values] : y->second) {
        uint32_t stat = 0;
        if (!names::stat(names::Kind::driver, label, stat)) {
            ++missing;
            continue;
        }
        uint32_t key = stat << 16;
        uintptr_t d = find_driver(comp, key);
        if (!d) {
            ++missing;
            continue;
        }
        float before = overall_now(comp, key);
        float x = progress(comp, key);
        std::vector<Undo> log;
        bool ok = true;
        for (int i = 0; i < pr::kStats && ok; ++i)
            ok = write_stat(comp, key, d, static_cast<uint16_t>(g_data.stats[i].stat_id), values.v[i], x, log) >= 0;
        if (!ok) {
            roll_back(log);
            ++failed;
            continue;
        }
        for (auto &u : log) {
            uintptr_t at = u.curve_changed ? u.curve : u.stat;
            Kept &k = g_kept[at];
            if (u.curve_changed) {
                if (k.original.empty()) {
                    k.curve = true;
                    k.original = u.ys;
                }
                read_ys(u.curve, k.written, k.pts);
            } else {
                if (!k.stat) {
                    k.curve = false;
                    k.stat = u.stat;
                    k.level_original = u.level;
                }
                k.level_written = rdv<float>(u.stat + kStatLevel);
            }
        }
        ++drivers;
        if (sample.size() < 160) {
            char b[96];
            std::snprintf(b, sizeof b, "%s%s %.0f -> %.0f", sample.empty() ? "" : ", ", label.c_str(), before,
                          overall_now(comp, key));
            sample += b;
        }
    }
    g_comp = comp.at;
    g_year = year;
    swofty::log("[progressive] wizard ratings %d (%s): Choose Driver and Team Select show %d driver(s) with their %d "
                "ratings (%s), %d not in the statistics table, %d failed; %zu curve(s) kept to put back (%s 0x%llx)",
                year, when, drivers, year, sample.c_str(), missing, failed, g_kept.size(),
                comp.player_class ? "DriverCareerPlayerDriverStatistics" : "DriverStatistics",
                static_cast<unsigned long long>(comp.at));
}

void restore(const char *why, bool apply)
{
    if (g_kept.empty()) {
        g_comp = 0;
        g_year = 0;
        return;
    }
    Comp comp;
    const char *no = "";
    bool live = find_comp(comp, &no) && comp.at == g_comp;
    size_t back = 0, changed = 0;
    if (apply && live) {
        for (auto &[at, k] : g_kept) {
            if (k.curve) {
                std::vector<float> now;
                uintptr_t pts = 0;
                CurveView v;
                if (!read_ys(at, now, pts) || pts != k.pts || now != k.written || !curve_view(at, v) ||
                    !curve_write_y(at, v, k.original)) {
                    ++changed;
                    continue;
                }
            } else {
                float now = rdv<float>(k.stat + kStatLevel);
                if (now != k.level_written || !wr(k.stat + kStatLevel, k.level_original)) {
                    ++changed;
                    continue;
                }
            }
            ++back;
        }
    }
    swofty::log("[progressive] wizard ratings %d: %s: %s", g_year, why,
                !apply  ? "the career owns its ratings now (its Team HQ writes the start year's); nothing put back"
                : !live ? "the wizard's statistics component is gone; nothing to put back"
                        : (std::to_string(back) + " curve(s) back on the game's ratings, " + std::to_string(changed) +
                           " changed since and left alone")
                              .c_str());
    g_kept.clear();
    g_comp = 0;
    g_year = 0;
}

} // namespace pg::wizard_ratings
