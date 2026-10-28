// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Which careers.json entry a loaded career is, and folding a split entry back (store_logic.hpp).

#include "store_logic.hpp"

#include <cstdio>

using namespace progressive::match;

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

Career entry(const std::string &key, uint32_t driver, uint32_t seed, int start, int applied, uint64_t used,
             int created = 0)
{
    Career c;
    c.key = key;
    c.driver = driver;
    c.seed = seed;
    c.start = start;
    c.applied_season = applied;
    c.applied_year = applied >= 0 ? year_for(start, static_cast<uint32_t>(applied)) : 0;
    c.used = used;
    c.created_season = created;
    return c;
}

} // namespace

int main()
{
    std::vector<Career> one = {entry("pg-051bc27c-15", 15, 0x051bc27c, 2016, 0, 5)};

    Pick rollover = pick(one, 0x051bc27c, 15, 1, {2016}, false, 2026);
    EXPECT(rollover.index == 0);
    EXPECT(rollover.how == "seed");

    Pick redrawn = pick(one, 0x053ea9cd, 15, 1, {2016}, false, 2026);
    EXPECT(redrawn.index == 0);
    EXPECT(redrawn.how == "the seats show its year");

    Pick applied = pick(one, 0x053ea9cd, 15, 1, {2017}, false, 2026);
    EXPECT(applied.index == 0);

    std::vector<Career> ahead = {entry("pg-051bc27c-15", 15, 0x051bc27c, 2016, 1, 5)};
    Pick older_save = pick(ahead, 0x01234567, 15, 0, {2016}, false, 2026);
    EXPECT(older_save.index == 0);

    Pick too_far = pick(one, 0x01234567, 15, 3, {2016}, false, 2026);
    EXPECT(too_far.index == -1);
    EXPECT(too_far.how == "no entry of this driver fits");
    EXPECT(too_far.new_start == 2016);

    Pick stock = pick(one, 0x01234567, 15, 0, {2026}, false, 2020);
    EXPECT(stock.index == 0);

    Pick wizard = pick(one, 0x051bc27c, 15, 0, {2026}, true, 2018);
    EXPECT(wizard.index == -1);
    EXPECT(wizard.new_start == 2018);

    std::vector<Career> two = {entry("pg-0000000a-15", 15, 0xa, 2016, 0, 1), entry("pg-0000000b-15", 15, 0xb, 2019, 0, 2)};
    Pick by_year = pick(two, 0xc, 15, 1, {2016}, false, 2026);
    EXPECT(by_year.index == 0);
    Pick other = pick(two, 0xc, 15, 0, {2019}, false, 2026);
    EXPECT(other.index == 1);

    EXPECT(key_base("pg-051bc27c-15-2") == "pg-051bc27c-15");
    EXPECT(key_base("pg-051bc27c-15") == "pg-051bc27c-15");

    Career original = entry("pg-051bc27c-15", 15, 0x051bc27c, 2016, 0, 5);
    original.rnd = "real_life";
    original.moves = "follow_driver";
    original.dev_year = 2016;
    original.dev_round = 2;
    original.mid_year = 2016;
    original.mid_round = 1;
    original.team = 479;
    original.season = 0;
    Career split = entry("pg-051bc27c-15-2", 15, 0x053ea9cd, 2016, 1, 9, 1);
    split.rnd = "hybrid";
    split.moves = "choose";
    split.team = 476;
    split.season = 1;
    split.mid_tracks = "0,13,3";
    EXPECT(split_of(original, split));
    EXPECT(!split_of(split, original));
    Career wizard_twin = entry("pg-051bc27c-15-2", 15, 0x051bc27c, 2016, 0, 9, 0);
    EXPECT(!split_of(original, wizard_twin));
    Career other_start = split;
    other_start.start = 2018;
    EXPECT(split_of(original, other_start));
    Career other_driver = split;
    other_driver.driver = 7;
    EXPECT(!split_of(original, other_driver));
    Career reseeded = entry("pg-053ea9cd-15", 15, 0x053ea9cd, 2016, 1, 9, 1);
    Career seeded_original = original;
    seeded_original.seed = 0x053ea9cd;
    EXPECT(split_of(seeded_original, reseeded));
    EXPECT(!split_of(original, reseeded));

    fold(original, split);
    EXPECT(original.key == "pg-051bc27c-15");
    EXPECT(original.applied_season == 1 && original.applied_year == 2017);
    EXPECT(original.rnd == "real_life" && original.moves == "follow_driver");
    EXPECT(original.dev_year == 2016 && original.dev_round == 2);
    EXPECT(original.mid_year == 2016 && original.mid_round == 1);
    EXPECT(original.mid_tracks == "0,13,3");
    EXPECT(original.team == 476 && original.season == 1 && original.seed == 0x053ea9cd && original.used == 9);

    std::vector<Career> midseason = {entry("pg-0598d453-15", 15, 0x05ad54f9, 2016, 1, 56)};
    Pick shuffle = pick(midseason, 0x05ad54f9, 15, 1, {2018}, false, 2026);
    EXPECT(shuffle.index == 0);
    EXPECT(shuffle.how == "seed");
    Pick far = pick(midseason, 0x05ad54f9, 15, 1, {2020}, false, 2026);
    EXPECT(far.index == -1);

    Career older = entry("pg-0598d453-15", 15, 0x05ad54f9, 2016, 1, 56);
    Career from_seats = entry("pg-05ad54f9-15", 15, 0x05ad54f9, 2017, 1, 59, 1);
    from_seats.applied_year = 2018;
    EXPECT(split_of(older, from_seats));
    fold(older, from_seats);
    EXPECT(older.key == "pg-0598d453-15" && older.start == 2016);
    EXPECT(older.applied_season == 1 && older.applied_year == 2017);
    EXPECT(older.used == 59);

    if (failures) {
        std::printf("store_logic_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("store_logic_test: ok\n");
    return 0;
}
