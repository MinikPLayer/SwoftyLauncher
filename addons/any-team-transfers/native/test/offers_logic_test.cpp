// SPDX-License-Identifier: AGPL-3.0-or-later

#include "offers_logic.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>

using namespace anyteam;

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

// The bytes the wrap checks, at their offsets in Game_DriverContracts_BuildOffers on the Steam and
// EA builds, with filler between them.
std::vector<uint8_t> build_offers_like()
{
    std::vector<uint8_t> code(0x400, 0xcc);
    for (auto [at, p] : {std::pair{0x16bu, 0u}, {0x40u, 1u}, {0x1e2u, 2u}, {0x21cu, 3u}, {0x265u, 4u}})
        std::memcpy(code.data() + at, kBuildOffersPatterns[p].bytes, kBuildOffersPatterns[p].n);
    return code;
}

void test_patterns()
{
    auto code = build_offers_like();
    EXPECT(missing_pattern(code.data(), code.size(), kBuildOffersPatterns).empty());

    auto hooked = code;
    std::memset(hooked.data(), 0x90, 0x20);
    EXPECT(missing_pattern(hooked.data(), hooked.size(), kBuildOffersPatterns).empty());

    auto stride = code;
    stride[0x43] = 0x50;
    EXPECT(missing_pattern(stride.data(), stride.size(), kBuildOffersPatterns) == "offer record stride");

    auto align = code;
    align[0x21c + 9] = 0x04;
    EXPECT(missing_pattern(align.data(), align.size(), kBuildOffersPatterns) == "offer allocator alloc");

    EXPECT(missing_pattern(code.data(), 0x100, kBuildOffersPatterns) == "offers list");
    EXPECT(!missing_pattern(nullptr, 0, kBuildOffersPatterns).empty());

    std::vector<uint8_t> reset(0x200, 0xcc);
    std::memcpy(reset.data() + 158, kResetEntrantsPatterns[0].bytes, kResetEntrantsPatterns[0].n);
    EXPECT(missing_pattern(reset.data(), reset.size(), kResetEntrantsPatterns).empty());
    reset[158 + 3] = 0x20;
    EXPECT(missing_pattern(reset.data(), reset.size(), kResetEntrantsPatterns) == "entrant list");
}

void test_keys()
{
    EXPECT(same_key(0x00430005u, 0x00431205u));
    EXPECT(!same_key(0x00430005u, 0x00440005u));
    EXPECT(!same_key(0x00430005u, 0x00430006u));
    EXPECT(no_team(0x3fff0000u) && no_team(0x3fff0005u) && !no_team(0x00070000u));
    uint32_t teams[] = {0x00010000u, 0x00070000u, 0x00030000u};
    EXPECT(has_team(teams, teams + 3, 0x00070000u));
    EXPECT(has_team(teams, teams + 3, 0x00071100u));
    EXPECT(!has_team(teams, teams + 3, 0x00080000u));
    EXPECT(!has_team(teams, teams, 0x00010000u));
    EXPECT(team_list(teams, teams + 3) == "1 7 3");
    EXPECT(team_list(teams, teams) == "none");
}

void test_teams_to_add()
{
    // Two rows per team, a free agent row, and the stock offers 3 and 4 (4 is his own team).
    std::vector<uint32_t> listed = {0x00010000u, 0x00010000u, 0x00020000u, 0x00020000u, 0x3fff0000u,
                                    0x00030000u, 0x00030000u, 0x00040000u, 0x00040000u, 0x00050000u};
    uint32_t stock[] = {0x00030000u, 0x00040000u};
    auto add = teams_to_add(stock, stock + 2, listed, 0x00040000u, false);
    EXPECT((add == std::vector<uint32_t>{0x00010000u, 0x00020000u, 0x00050000u}));

    uint32_t none[] = {0x00030000u};
    add = teams_to_add(none, none + 1, listed, 0x00040000u, false);
    EXPECT((add == std::vector<uint32_t>{0x00010000u, 0x00020000u, 0x00040000u, 0x00050000u}));

    add = teams_to_add(none, none + 1, listed, 0x00040000u, true);
    EXPECT((add == std::vector<uint32_t>{0x00010000u, 0x00020000u, 0x00050000u}));

    add = teams_to_add(none, none, listed, 0x3fff0000u, true);
    EXPECT(add.size() == 5);

    uint32_t all[] = {0x00010000u, 0x00020000u, 0x00030000u, 0x00040000u, 0x00050000u};
    EXPECT(teams_to_add(all, all + 5, listed, 0x00040000u, false).empty());
    EXPECT(teams_to_add(all, all, {}, 0x00040000u, false).empty());
}

// --build-offers <file> / --reset-entrants <file>: a dump of the real function from its entry (the
// exe or a memory dump); every pattern must be in it.
void test_dump(std::string_view which, const char *path)
{
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> code{std::istreambuf_iterator<char>(f), {}};
    std::string miss = which == "--build-offers" ? missing_pattern(code.data(), code.size(), kBuildOffersPatterns)
                                                 : missing_pattern(code.data(), code.size(), kResetEntrantsPatterns);
    std::printf("%s %s: %zu bytes, %s\n", which.data(), path, code.size(), miss.empty() ? "every pattern found" : miss.c_str());
    EXPECT(!code.empty() && miss.empty());
}

} // namespace

int main(int argc, char **argv)
{
    test_patterns();
    test_keys();
    test_teams_to_add();
    for (int i = 1; i + 1 < argc; i += 2) {
        std::string_view which = argv[i];
        EXPECT(which == "--build-offers" || which == "--reset-entrants");
        test_dump(which, argv[i + 1]);
    }
    if (failures) {
        std::printf("offers_logic_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("offers_logic_test: ok\n");
    return 0;
}
