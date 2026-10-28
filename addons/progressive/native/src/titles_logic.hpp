// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career: the race weekend and season titles of the career's year. Pure logic, no game
// access, so native/test builds it on the host.
//
// The 2026 content's event titles (lng_track_<code>_gp_title_26, _gp_title_multiline_26) and season
// titles carry the year 2026 in their text. data/titles.json (tools/titles.py) holds the stock text
// of every language: "all" the English text, "per" the languages whose text differs. A career below
// 2026 shows each of them with its own year in place of 2026.

#pragma once
#include <string>
#include <vector>

#include "../../../../native/src/core/json.hpp"

namespace progressive::titles {

struct Text {
    std::string language; // "*" for every language
    std::string key;
    std::string text;
};

inline std::string with_year(const std::string &text, const std::string &from, int year)
{
    if (from.empty())
        return text;
    std::string to = std::to_string(year), out;
    size_t pos = 0;
    for (size_t hit; (hit = text.find(from, pos)) != std::string::npos; pos = hit + from.size())
        out.append(text, pos, hit - pos).append(to);
    return out.append(text, pos, std::string::npos);
}

// Every override for the year, the "*" ones first so a language's own text wins over them.
inline std::vector<Text> for_year(const json::Value &doc, int year)
{
    std::vector<Text> out;
    std::string from = doc["year"].str();
    for (auto &[key, v] : doc["all"].o)
        if (v.type == json::Value::Str)
            out.push_back({"*", key, with_year(v.s, from, year)});
    for (auto &[language, table] : doc["per"].o)
        for (auto &[key, v] : table.o)
            if (v.type == json::Value::Str)
                out.push_back({language, key, with_year(v.s, from, year)});
    return out;
}

// The keys to clear when the career's year is 2026 or later (every language at once).
inline std::vector<std::string> keys(const json::Value &doc)
{
    std::vector<std::string> out;
    for (auto &[key, v] : doc["all"].o)
        out.push_back(key);
    for (auto &[language, table] : doc["per"].o)
        for (auto &[key, v] : table.o) {
            bool seen = false;
            for (auto &k : out)
                seen = seen || k == key;
            if (!seen)
                out.push_back(key);
        }
    return out;
}

} // namespace progressive::titles
