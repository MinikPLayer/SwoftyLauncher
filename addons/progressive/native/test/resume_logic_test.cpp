// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Which careers.json entry a Resume Career card shows (resume_logic.hpp).

#include "resume_logic.hpp"

#include <cstdio>

using progressive::resume::card_team;
using progressive::resume::card_year;
using progressive::resume::Entry;

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

Entry entry(uint32_t driver, int team, int season, int year, bool real_team = false, uint64_t used = 0)
{
    Entry e;
    e.driver = driver;
    e.team = team;
    e.season = season;
    e.year = year;
    e.real_team = real_team;
    e.used = used;
    return e;
}

} // namespace

int main()
{
    EXPECT(card_year({}, 7, 3, 0) == 0);

    std::vector<Entry> one = {entry(7, 3, 2, 2018)};
    EXPECT(card_year(one, 7, 3, 2) == 2018);
    EXPECT(card_year(one, 7, 9, 5) == 2018);
    EXPECT(card_year(one, 8, 3, 2) == 0);

    std::vector<Entry> unrecorded = {entry(7, -1, -1, 2020)};
    EXPECT(card_year(unrecorded, 7, 3, 0) == 2020);

    std::vector<Entry> two_drivers = {entry(7, 3, 0, 2016), entry(9, 3, 0, 2024)};
    EXPECT(card_year(two_drivers, 7, 3, 0) == 2016);
    EXPECT(card_year(two_drivers, 9, 3, 0) == 2024);

    std::vector<Entry> same_year = {entry(7, 3, 0, 2019, false, 1), entry(7, 4, 0, 2019, false, 2)};
    EXPECT(card_year(same_year, 7, 5, 0) == 2019);

    std::vector<Entry> by_team = {entry(7, 3, 2, 2018, false, 2), entry(7, 4, 2, 2022, false, 1)};
    EXPECT(card_year(by_team, 7, 3, 2) == 2018);
    EXPECT(card_year(by_team, 7, 4, 2) == 2022);

    std::vector<Entry> by_season = {entry(7, 3, 2, 2018, false, 1), entry(7, 3, 0, 2022, false, 2)};
    EXPECT(card_year(by_season, 7, 3, 2) == 2018);
    EXPECT(card_year(by_season, 7, 3, 0) == 2022);

    std::vector<Entry> both = {entry(7, 3, 2, 2018, false, 3), entry(7, 3, 0, 2022, false, 2), entry(7, 4, 2, 2025, false, 1)};
    EXPECT(card_year(both, 7, 3, 0) == 2022);
    EXPECT(card_year(both, 7, 4, 2) == 2025);
    EXPECT(card_year(both, 7, 3, 2) == 2018);

    std::vector<Entry> unrecorded_two = {entry(7, -1, -1, 2017, true, 1), entry(7, -1, -1, 2021, false, 2)};
    EXPECT(card_year(unrecorded_two, 7, 3, 1) == 2017);

    std::vector<Entry> last_used = {entry(7, -1, -1, 2017, false, 5), entry(7, -1, -1, 2021, false, 2)};
    EXPECT(card_year(last_used, 7, 3, 1) == 2017);

    std::vector<Entry> identical = {entry(7, 3, 2, 2018, true, 1), entry(7, 3, 2, 2021, true, 4)};
    EXPECT(card_year(identical, 7, 3, 2) == 2021);

    std::vector<Entry> recorded_beats_real = {entry(7, 3, 2, 2018, false, 1), entry(7, 4, 2, 2021, true, 4)};
    EXPECT(card_year(recorded_beats_real, 7, 3, 2) == 2018);

    std::vector<Entry> no_year = {entry(7, 3, 2, 0), entry(7, 4, 2, 2021)};
    EXPECT(card_year(no_year, 7, 3, 2) == 2021);

    auto follow = [](Entry e) {
        e.follow = true;
        return e;
    };
    std::vector<Entry> moved = {follow(entry(7, 476, 0, 2016))};
    EXPECT(card_team(moved, 7, 477, 0) == 476);
    EXPECT(card_team(moved, 7, 476, 0) == -1);
    EXPECT(card_team(moved, 7, 477, 1) == -1);
    EXPECT(card_team(moved, 7, 477, -1) == -1);
    EXPECT(card_team(moved, 9, 477, 0) == -1);

    std::vector<Entry> chooses = {entry(7, 476, 0, 2016)};
    EXPECT(card_team(chooses, 7, 477, 0) == -1);

    std::vector<Entry> one_agrees = {follow(entry(7, 476, 0, 2016)), entry(7, 477, 0, 2020)};
    EXPECT(card_team(one_agrees, 7, 477, 0) == -1);

    std::vector<Entry> two_others = {follow(entry(7, 476, 0, 2016)), follow(entry(7, 478, 0, 2019))};
    EXPECT(card_team(two_others, 7, 477, 0) == -1);

    std::vector<Entry> same_other = {follow(entry(7, 476, 0, 2016)), follow(entry(7, 476, 0, 2017))};
    EXPECT(card_team(same_other, 7, 477, 0) == 476);

    std::vector<Entry> follow_unrecorded = {follow(entry(7, -1, -1, 2016))};
    EXPECT(card_team(follow_unrecorded, 7, 477, 0) == -1);

    if (failures) {
        std::printf("resume_logic_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("resume_logic_test: ok\n");
    return 0;
}
