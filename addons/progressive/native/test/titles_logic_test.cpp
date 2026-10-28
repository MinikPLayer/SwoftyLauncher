// SPDX-License-Identifier: AGPL-3.0-or-later

#include "titles_logic.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

using namespace progressive::titles;

namespace {

int failures = 0;

void expect(bool ok, const char *what, int line)
{
    if (!ok) {
        std::printf("FAIL line %d: %s\n", line, what);
        ++failures;
    }
}

#define EXPECT(x) expect((x), #x, __LINE__)

const Text *find(const std::vector<Text> &v, const char *language, const char *key)
{
    for (auto &t : v)
        if (t.language == language && t.key == key)
            return &t;
    return nullptr;
}

void replaces_the_year()
{
    EXPECT(with_year("FORMULA 1 QATAR AIRWAYS AUSTRALIAN GRAND PRIX 2026", "2026", 2017) ==
           "FORMULA 1 QATAR AIRWAYS AUSTRALIAN GRAND PRIX 2017");
    EXPECT(with_year("2026 FIA Formula One World Championship{s:60_bold_drp_shw_TM}™{/s}", "2026", 2019) ==
           "2019 FIA Formula One World Championship{s:60_bold_drp_shw_TM}™{/s}");
    EXPECT(with_year("2026赛季一级方程式赛车", "2026", 2016) == "2016赛季一级方程式赛车");
    EXPECT(with_year("no year here", "2026", 2017) == "no year here");
    EXPECT(with_year("2026 and 2026", "2026", 2020) == "2020 and 2020");
    EXPECT(with_year("text 2026", "", 2017) == "text 2026");
}

void overrides_from_doc()
{
    const char *text = R"({"year": "2026",
      "all": {"lng_track_aus_gp_title_26": "FORMULA 1 AUSTRALIAN GRAND PRIX 2026",
              "lng_f1_2026_championship_season": "2026 FIA Formula One World Championship"},
      "per": {"zhs": {"lng_track_aus_gp_title_26": "2026赛季澳大利亚大奖赛"},
              "fre": {"lng_f1_2026_championship_season": "Championnat du monde 2026"}}})";
    json::Value doc;
    EXPECT(json::parse(text, doc));
    std::vector<Text> v = for_year(doc, 2017);
    EXPECT(v.size() == 4);
    EXPECT(v.size() >= 2 && v[0].language == "*" && v[1].language == "*");
    const Text *aus = find(v, "*", "lng_track_aus_gp_title_26");
    EXPECT(aus && aus->text == "FORMULA 1 AUSTRALIAN GRAND PRIX 2017");
    const Text *zhs = find(v, "zhs", "lng_track_aus_gp_title_26");
    EXPECT(zhs && zhs->text == "2017赛季澳大利亚大奖赛");
    const Text *fre = find(v, "fre", "lng_f1_2026_championship_season");
    EXPECT(fre && fre->text == "Championnat du monde 2017");
    std::vector<std::string> k = keys(doc);
    EXPECT(k.size() == 2);
}

void shipped_data(const char *path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    json::Value doc;
    EXPECT(f.good() || !ss.str().empty());
    EXPECT(json::parse(ss.str(), doc));
    std::vector<Text> v = for_year(doc, 2017);
    EXPECT(v.size() >= 50);
    const Text *aus = find(v, "*", "lng_track_aus_gp_title_26");
    EXPECT(aus && aus->text.find("2017") != std::string::npos && aus->text.find("2026") == std::string::npos);
    size_t left = 0;
    for (auto &t : v)
        left += t.text.find("2026") != std::string::npos;
    EXPECT(left == 0);
    EXPECT(keys(doc).size() == doc["all"].o.size());
}

} // namespace

int main(int argc, char **argv)
{
    replaces_the_year();
    overrides_from_doc();
    if (argc > 1)
        shipped_data(argv[1]);
    if (failures) {
        std::printf("titles_logic_test: %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("titles_logic_test: ok\n");
    return EXIT_SUCCESS;
}
