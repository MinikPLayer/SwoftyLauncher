// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Whether the copies of the player's team agree (player_logic.hpp).

#include "player_logic.hpp"

#include <cstdio>

using namespace progressive::player;

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

Teams teams(int row, int contract, bool active, int cached = kNone, int store = kNone, bool found = true)
{
    Teams t;
    t.row = row;
    t.contract = contract;
    t.contract_active = active;
    t.contract_found = found;
    t.cached = cached;
    t.store = store;
    return t;
}

} // namespace

int main()
{
    {
        Verdict v = judge(teams(476, 476, true, kNone, 476));
        EXPECT(v.fix == Fix::none);
        EXPECT(!v.store_stale);
        EXPECT(!v.move_planned);
        EXPECT(v.text == "row 476, contract 476 active, next season cached none, store 476: contract agrees");
    }
    {
        Verdict v = judge(teams(476, 477, true, kNone, 477));
        EXPECT(v.fix == Fix::sign_row_team);
        EXPECT(v.store_stale);
        EXPECT(v.text.find("contract for another team than his seat") != std::string::npos);
        EXPECT(v.text.find("store team updated") != std::string::npos);
    }
    {
        Verdict v = judge(teams(476, 477, false, kNone, 476));
        EXPECT(v.fix == Fix::negotiation);
        EXPECT(!v.store_stale);
    }
    {
        Verdict v = judge(teams(476, kNone, false, kNone, 476, false));
        EXPECT(v.fix == Fix::negotiation);
        EXPECT(v.text.find("no contract record") != std::string::npos);
    }
    {
        Verdict v = judge(teams(476, 476, true, 477, 476));
        EXPECT(v.fix == Fix::none);
        EXPECT(v.move_planned);
        EXPECT(v.text.find("the season change moves him to 477") != std::string::npos);
    }
    {
        Verdict v = judge(teams(476, 476, true, 476, 476));
        EXPECT(!v.move_planned);
    }
    {
        Verdict v = judge(teams(kNone, 476, true));
        EXPECT(v.fix == Fix::no_row);
    }
    if (failures) {
        std::printf("player_logic_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("player_logic_test: ok\n");
    return 0;
}
