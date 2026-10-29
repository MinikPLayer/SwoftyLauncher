// SPDX-License-Identifier: MIT
#pragma once
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include <swofty/fake_core.hpp>

namespace json = swofty::json;

inline int failures = 0;

inline void check(bool ok, const char *what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    failures += !ok;
}

// The addon's storage, as the core gives it: files under one folder, here test/storage/ (the test runs in
// the addon's folder).
inline std::string g_storage;

inline bool slurp(const char *path, std::string &out)
{
    std::ifstream f(g_storage + path, std::ios::binary);
    if (!f)
        return false;
    std::ostringstream s;
    s << f.rdbuf();
    out = s.str();
    return true;
}

static int s_available() { return 1; }
static int s_size(const char *path, uint64_t *out)
{
    std::string text;
    if (!slurp(path, text))
        return SWOFTY_ASSET_INVALID;
    *out = text.size();
    return SWOFTY_ASSET_OK;
}
static int s_read(const char *path, void *buf, size_t cap, size_t *len)
{
    std::string text;
    if (!slurp(path, text) || text.size() > cap)
        return SWOFTY_ASSET_INVALID;
    std::memcpy(buf, text.data(), text.size());
    *len = text.size();
    return SWOFTY_ASSET_OK;
}
static int s_refuse(const char *, const void *, size_t) { return SWOFTY_ASSET_INVALID; }
static int s_remove(const char *) { return SWOFTY_ASSET_INVALID; }
static int s_list(const char *, char *, size_t, size_t *) { return SWOFTY_ASSET_INVALID; }
inline const SwoftyStorage kStorage = {s_available, s_read, s_refuse, s_refuse, s_remove, s_size, s_list};

// Progressive's 2017 season and Alonso's 2017 ratings, as swofty.progressive registers them.
static const char *kSeason2017 = R"({
  "data_group": "f1_career_progressive", "year": 2017, "box": "swofty.classic-seasons-2017/f1_2017|game/f1_2025",
  "teams": [
    { "slot": 1, "team": "constructor/mercedes", "seats": ["game/lewis_hamilton", "game/valtteri_bottas"] },
    { "slot": 2, "team": "constructor/ferrari", "seats": ["game/sebastian_vettel", "game/kimi_raikkonen"] }
  ],
  "changes": [],
  "progressive": { "ratings_source": "official" }
})";
static const char *kAlonso2017 = R"({
  "season": "swofty.progressive/2017", "driver": "game/fernando_alonso",
  "experience": 97, "racecraft": 93, "awareness": 91, "pace": 92
})";

struct Run {
    swofty::fake::Core core{"tails.whatif-history"};
    SwoftyApi api{};

    explicit Run(const char *storage, bool progressive = true)
    {
        g_storage = storage;
        api = core.api();
        api.storage = &kStorage;
        swofty::api = &api;
        if (progressive && core.rules()) {
            swofty::fake::Core::default_engine()->begin_addon("swofty.progressive");
            swofty::career::Season season;
            swofty::from_json(kSeason2017, season);
            swofty::career::seasons.add("2017", season);
            swofty::career::DriverRatings alonso;
            swofty::from_json(kAlonso2017, alonso);
            swofty::career::ratings.add("2017/game/fernando_alonso", alonso);
        }
        core.run_register();
    }
};

