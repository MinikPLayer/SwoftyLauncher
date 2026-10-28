// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career: where the player's team lives and whether its copies agree. Pure logic, so
// native/test builds it on the host.
//
// The game keeps the player's team in his entrant row (CareerEntity +0x2e0, the row with the human
// identity bit, team +0x58). The save slot header (info_team, slot info +0xbe) is copied from that
// row at every save by the slot info update 0x1429cec50, and the Team HQ props
// (PlayerTeamModelComponent::initialise 0x142a9f390, material context "2026-<team>") read the same
// row when Team HQ loads; neither keeps its own copy. The other copies the career keeps are his
// DriverContract (DriverCareerContractsManagerComponent +0x350 owner, team +0x00, active +0x2c) and
// a contract the game cached for next season (manager +0x408..+0x410, 0x38 entries, team +0x00,
// driver kind +0x2c, stat +0x2e), which the season processor 0x140d766b0 applies when it names
// another team than the active contract. judge() compares them with the row:
//   - an active contract for another team than the seat: the season apply signs the seat's team, but
//     only after it has written the plan and only when the seat is the one the plan gives him (the
//     plan decides the player's team; a refused or skipped apply leaves the contract alone);
//   - no active contract: the game's negotiation signs one (follow mode: progressive_follow.dll);
//   - a cached contract for another team is a planned move at the season change, reported only;
//   - the store's recorded team (the Resume Career card's year) is refreshed from the row.

#pragma once
#include <string>

namespace progressive::player {

constexpr int kNone = -1;

struct Teams {
    int row = kNone;
    int contract = kNone;
    bool contract_active = false;
    bool contract_found = false;
    int cached = kNone;
    int store = kNone;
};

enum class Fix { none, sign_row_team, negotiation, no_row };

struct Verdict {
    Fix fix = Fix::none;
    bool store_stale = false;
    bool move_planned = false;
    std::string text;
};

inline std::string team_text(int t)
{
    return t == kNone ? "none" : std::to_string(t);
}

inline Verdict judge(const Teams &t)
{
    Verdict v;
    if (t.row == kNone) {
        v.fix = Fix::no_row;
        v.text = "the player has no team row; nothing to compare";
        return v;
    }
    std::string contract = !t.contract_found     ? "no contract record"
                           : !t.contract_active ? "contract " + team_text(t.contract) + " not active"
                                                : "contract " + team_text(t.contract) + " active";
    if (!t.contract_found || !t.contract_active)
        v.fix = Fix::negotiation;
    else if (t.contract != t.row)
        v.fix = Fix::sign_row_team;
    v.store_stale = t.store != t.row;
    v.move_planned = t.cached != kNone && t.cached != t.row;
    v.text = "row " + team_text(t.row) + ", " + contract + ", next season cached " + team_text(t.cached) +
             ", store " + team_text(t.store) + ": ";
    switch (v.fix) {
    case Fix::none:
        v.text += "contract agrees";
        break;
    case Fix::sign_row_team:
        v.text += "contract for another team than his seat";
        break;
    case Fix::negotiation:
        v.text += "no active contract, the game's negotiation signs one";
        break;
    case Fix::no_row:
        break;
    }
    if (v.move_planned)
        v.text += "; the season change moves him to " + team_text(t.cached);
    if (v.store_stale)
        v.text += "; store team updated";
    return v;
}

} // namespace progressive::player
