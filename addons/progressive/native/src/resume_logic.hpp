// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career: which store entry a Resume Career card shows. Pure logic, so native/test
// builds it on the host.
//
// A card's save slot info carries the player's driver, team and season index of the save, not the
// seed or the other seats, so a card is matched to the careers.json entries of its driver
// (store.hpp). Each entry's year for the card is worked out by the caller the way season.cpp does
// for a loaded career: year_for(start, the card's season index + test seasons). When the entries
// disagree on that year, the narrowing follows career_entry's order as far as a card allows:
//   1. the team and season index the entry recorded at its last load or Team HQ;
//   2. the entry whose year has the driver racing for the card's team in real life (the card's
//      share of "the seats show its year");
//   3. the driver's last used entry.
// The result is 0 only when no entry has the card's driver.
//
// card_team: the team a card shows. The game copies the slot's team (info_team) from the player's
// seat at each save, and a follow-mode career whose season apply moved him has not been saved since
// shows the seat of its last save; loading it applies the move again. So when no entry of the
// driver recorded the card's team at the card's season index, and the follow-mode entries recorded
// at that season index all name one other team, the card shows that team; otherwise (or when
// nothing differs) -1, the card's own team.

#pragma once
#include <cstdint>
#include <vector>

namespace progressive::resume {

struct Entry {
    uint32_t driver = 0;
    int team = -1;
    int season = -1;
    int year = 0;
    bool real_team = false;
    uint64_t used = 0;
    bool follow = false;
};

inline int agreed_year(const std::vector<const Entry *> &es)
{
    if (es.empty())
        return 0;
    int y = es.front()->year;
    for (const Entry *e : es)
        if (e->year != y)
            return 0;
    return y;
}

template <class Keep> std::vector<const Entry *> narrow(const std::vector<const Entry *> &es, Keep keep)
{
    std::vector<const Entry *> out;
    for (const Entry *e : es)
        if (keep(*e))
            out.push_back(e);
    return out.empty() ? es : out;
}

inline int card_year(const std::vector<Entry> &entries, uint32_t driver, int team, int season)
{
    std::vector<const Entry *> c;
    for (const Entry &e : entries)
        if (e.driver == driver && e.year)
            c.push_back(&e);
    if (c.empty())
        return 0;
    if (int y = agreed_year(c))
        return y;
    c = narrow(c, [&](const Entry &e) { return team >= 0 && e.team == team; });
    c = narrow(c, [&](const Entry &e) { return season >= 0 && e.season == season; });
    if (int y = agreed_year(c))
        return y;
    c = narrow(c, [](const Entry &e) { return e.real_team; });
    if (int y = agreed_year(c))
        return y;
    const Entry *last = c.front();
    for (const Entry *e : c)
        if (e->used >= last->used)
            last = e;
    return last->year;
}

inline int card_team(const std::vector<Entry> &entries, uint32_t driver, int team, int season)
{
    if (season < 0)
        return -1;
    int other = -1;
    for (const Entry &e : entries) {
        if (e.driver != driver || e.season != season || e.team < 0)
            continue;
        if (e.team == team)
            return -1;
        if (!e.follow)
            continue;
        if (other >= 0 && other != e.team)
            return -1;
        other = e.team;
    }
    return other;
}

} // namespace progressive::resume
