// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The season plan over the committed data/years.json and a career shaped like a new Progressive
// career: the stock 2026 grid on the race seats, every other pool driver a free agent, two
// fictional reserves.

#include "season_logic.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

using namespace progressive;
using season::CareerRow;

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

season::Data g_data;

int stat(const std::string &label)
{
    const season::PoolDriver *p = g_data.driver(label);
    return p ? p->stat : -1;
}

season::Resolve resolver(bool cs_installed = true)
{
    season::Resolve r;
    r.driver_stat = stat;
    r.vehicle_stat = [cs_installed](const std::string &label) {
        if (label.rfind("cs", 0) == 0)
            return cs_installed ? 1000 + static_cast<int>(std::hash<std::string>{}(label) % 1000) : -1;
        for (auto &s : g_data.slots)
            if (s.vehicle_2026 == label)
                return s.vehicle_2026_stat;
        return -1;
    };
    r.look_known = [cs_installed](int s) { return cs_installed || s < 420 || s >= 700; };
    r.rating = [](uint32_t) { return 75.0; };
    return r;
}

std::vector<CareerRow> career(const std::string &player)
{
    std::vector<CareerRow> rows;
    const season::Year *y = g_data.year(2026);
    std::vector<int> seated;
    for (int i = 0; i < season::kSlots; ++i)
        for (auto &d : y->teams[i].drivers) {
            CareerRow r;
            r.driver = static_cast<uint32_t>(stat(d.driver));
            r.team = g_data.slots[i].team_stat;
            r.position = d.position;
            r.car = true;
            r.human = d.driver == player;
            r.number = static_cast<uint8_t>(d.number);
            rows.push_back(r);
            seated.push_back(stat(d.driver));
        }
    for (auto &p : g_data.pool)
        if (std::find(seated.begin(), seated.end(), p.stat) == seated.end()) {
            CareerRow r;
            r.driver = static_cast<uint32_t>(p.stat);
            r.number = p.number > 0 ? static_cast<uint8_t>(p.number) : season::kNoNumber;
            rows.push_back(r);
        }
    rows.push_back({900, g_data.slots[0].team_stat, 2, false, false, season::kNoNumber});
    rows.push_back({901, g_data.slots[1].team_stat, 2, false, false, season::kNoNumber});
    return rows;
}

uint32_t at(const season::Plan &p, int slot1, int seat)
{
    return p.target[slot1 - 1][seat];
}

bool seated_once(const season::Plan &p)
{
    std::vector<uint32_t> all;
    for (auto &s : p.target)
        for (uint32_t d : s) {
            if (d == lineup::kNone || std::find(all.begin(), all.end(), d) != all.end())
                return false;
            all.push_back(d);
        }
    return all.size() == 22;
}

void years()
{
    EXPECT(season::year_for(2016, 0) == 2016);
    EXPECT(season::year_for(2016, 4) == 2020);
    EXPECT(season::year_for(2016, 10) == 2026);
    EXPECT(season::year_for(2016, 15) == 2026);
    EXPECT(season::year_for(2026, 3) == 2026);
    EXPECT(season::year_for(2010, 0) == 2016);
    EXPECT(season::year_for(2030, 0) == 2026);
}

void parsed()
{
    EXPECT(g_data.years.size() == 11);
    EXPECT(g_data.slots[0].team == "mercedes_26");
    EXPECT(g_data.slots[10].team == "gm_cadillac_26");
    EXPECT(g_data.year(2016)->teams[0].drivers.size() == 2);
    EXPECT(g_data.year(2017)->teams[10].kind == "filler");
    EXPECT(g_data.year(2017)->teams[10].drivers.empty());
    EXPECT(stat("lewis_hamilton") == 7);
}

void stock_2026_unchanged_for_its_own_year()
{
    auto rows = career("lando_norris");
    season::Plan p = season::plan(g_data, 2026, rows, resolver());
    EXPECT(p.error.empty());
    EXPECT(p.moves.empty());
    EXPECT(p.releases.empty());
    EXPECT(seated_once(p));
    for (auto &v : p.vehicles)
        EXPECT(v.from_year == 2026);
}

void hamilton_at_ferrari_in_2016()
{
    auto rows = career("lewis_hamilton");
    season::Plan p = season::plan(g_data, 2016, rows, resolver());
    EXPECT(p.error.empty());
    EXPECT(seated_once(p));
    EXPECT(p.player == 7u);
    EXPECT(p.player_slot == 1);
    EXPECT(at(p, 2, 0) == static_cast<uint32_t>(stat("sebastian_vettel")) ||
           at(p, 2, 1) == static_cast<uint32_t>(stat("sebastian_vettel")));
    EXPECT(at(p, 2, 0) == 7u || at(p, 2, 1) == 7u);
    uint32_t kimi = static_cast<uint32_t>(stat("kimi_raikkonen"));
    EXPECT(p.fit.displaced == kimi);
    EXPECT(at(p, 1, 0) == kimi || at(p, 1, 1) == kimi);
    EXPECT(at(p, 1, 0) == static_cast<uint32_t>(stat("nico_rosberg")) ||
           at(p, 1, 1) == static_cast<uint32_t>(stat("nico_rosberg")));
    for (auto &m : p.moves)
        EXPECT(m.driver != 7u);
    bool norris_out = false;
    for (uint32_t d : p.releases)
        norris_out = norris_out || d == static_cast<uint32_t>(stat("lando_norris"));
    EXPECT(norris_out);
    uint32_t rosberg = static_cast<uint32_t>(stat("nico_rosberg"));
    bool rosberg_in = false;
    for (auto &m : p.moves)
        rosberg_in = rosberg_in || (m.driver == rosberg && m.from.slot < 0 && m.to.slot == 0);
    EXPECT(rosberg_in);
    bool hadjar_cleared = false;
    for (auto &n : p.numbers)
        hadjar_cleared = hadjar_cleared || (n.driver == static_cast<uint32_t>(stat("isack_hadjar")) && n.number == season::kNoNumber);
    EXPECT(hadjar_cleared);
    EXPECT(p.vehicles[0].label == "cs16_mercedes");
    EXPECT(p.vehicles[10].label == "cs16_manor");
    bool kimi_look = false;
    for (auto &l : p.looks)
        kimi_look = kimi_look || (l.driver == kimi && l.label.find("2016-cs_ferrari_kimi") == 0);
    EXPECT(kimi_look);
}

void follow_mode_moves_the_player_in_the_first_season()
{
    auto rows = career("lewis_hamilton");
    season::Plan p = season::plan(g_data, 2016, rows, resolver(), true);
    EXPECT(p.error.empty());
    EXPECT(p.player_moves);
    EXPECT(seated_once(p));
    EXPECT(at(p, 1, 0) == 7u || at(p, 1, 1) == 7u);
    uint32_t vettel = static_cast<uint32_t>(stat("sebastian_vettel")), kimi = static_cast<uint32_t>(stat("kimi_raikkonen"));
    EXPECT((at(p, 2, 0) == vettel && at(p, 2, 1) == kimi) || (at(p, 2, 0) == kimi && at(p, 2, 1) == vettel));
    EXPECT(p.fit.displaced == lineup::kNone);
    bool moved = false;
    for (auto &m : p.moves)
        moved = moved || (m.driver == 7u && m.from.slot == 1 && m.to.slot == 0);
    EXPECT(moved);
    season::Plan same = season::plan(g_data, 2016, career("max_verstappen"), resolver(), true);
    EXPECT(!same.player_moves);
    for (auto &m : same.moves)
        EXPECT(m.driver != static_cast<uint32_t>(stat("max_verstappen")));
}

void ten_team_year_fills_manor()
{
    auto rows = career("lando_norris");
    season::Plan p = season::plan(g_data, 2017, rows, resolver());
    EXPECT(p.error.empty());
    EXPECT(seated_once(p));
    EXPECT(at(p, 11, 0) != lineup::kNone && at(p, 11, 1) != lineup::kNone);
    EXPECT(p.vehicles[10].label == "cs16_manor");
}

void missing_year_addon_falls_back()
{
    auto rows = career("lando_norris");
    season::Plan p = season::plan(g_data, 2018, rows, resolver(false));
    EXPECT(p.error.empty());
    EXPECT(p.vehicles[0].label == "mercedes_2026");
    for (auto &l : p.looks)
        EXPECT(l.stat >= 0 && (l.stat < 420 || l.stat >= 700));
}

void hybrid_budgets_follow_pace()
{
    EXPECT(season::hybrid_budget(g_data, 2016, 0, 1000) == 1000);
    int ferrari = season::hybrid_budget(g_data, 2016, 1, 1000);
    EXPECT(ferrari == 1466);
    int manor16 = season::hybrid_budget(g_data, 2016, 10, 1000);
    int manor17 = season::hybrid_budget(g_data, 2017, 10, 1000);
    EXPECT(manor16 > ferrari);
    int slowest17 = 1000;
    for (int i = 0; i < 10; ++i)
        slowest17 = std::max(slowest17, season::hybrid_budget(g_data, 2017, i, 1000));
    EXPECT(manor17 == slowest17);
}

void numbers_never_collide()
{
    auto rows = career("lando_norris");
    season::Plan p = season::plan(g_data, 2019, rows, resolver());
    std::map<uint32_t, uint8_t> num;
    for (auto &r : rows)
        num[r.driver] = r.number;
    for (auto &n : p.numbers)
        num[n.driver] = n.number;
    std::map<uint8_t, int> seen;
    for (auto &[d, n] : num)
        if (n != season::kNoNumber)
            ++seen[n];
    for (auto &s : p.target)
        for (uint32_t d : s)
            if (num[d] != season::kNoNumber)
                EXPECT(seen[num[d]] == 1);
}

int slot_of(const std::string &team)
{
    for (int i = 0; i < season::kSlots; ++i)
        if (g_data.slots[i].team == team)
            return i;
    return -1;
}

CareerRow &row(std::vector<CareerRow> &rows, const std::string &driver)
{
    for (auto &r : rows)
        if (r.driver == static_cast<uint32_t>(stat(driver)))
            return r;
    std::printf("no row for %s\n", driver.c_str());
    std::abort();
}

void free_agent(CareerRow &r)
{
    r.team = -1;
    r.position = -1;
    r.car = false;
}

uint32_t id(const std::string &driver)
{
    return static_cast<uint32_t>(stat(driver));
}

bool moved_to(const season::Plan &p, const std::string &driver, int slot, int seat = -1)
{
    for (auto &m : p.moves)
        if (m.driver == id(driver) && m.to.slot == slot && (seat < 0 || m.to.index == seat))
            return true;
    return false;
}

void chosen_team_leaves_his_2026_seat_empty()
{
    auto rows = career("sergio_perez");
    int williams = slot_of("williams_26"), cadillac = slot_of("gm_cadillac_26"), force_india = slot_of("forceindia_26");
    EXPECT(williams >= 0 && cadillac >= 0 && force_india >= 0);
    CareerRow &perez = row(rows, "sergio_perez");
    perez.team = g_data.slots[williams].team_stat;
    perez.position = row(rows, "carlos_sainz-jr").position;
    EXPECT(perez.position == 1);
    free_agent(row(rows, "carlos_sainz-jr"));
    season::Plan p = season::plan(g_data, 2016, rows, resolver());
    EXPECT(p.error.empty());
    if (!p.error.empty())
        std::printf("  plan error: %s\n", p.error.c_str());
    EXPECT(seated_once(p));
    EXPECT(p.player_slot == williams);
    EXPECT(p.holder[cadillac][0] < 0 && p.holder[cadillac][1] >= 0);
    EXPECT(at(p, williams + 1, 0) == id("felipe_massa"));
    EXPECT(at(p, williams + 1, 1) == id("sergio_perez"));
    EXPECT(p.fit.displaced == id("valtteri_bottas"));
    EXPECT(at(p, force_india + 1, 0) == id("valtteri_bottas"));
    EXPECT(moved_to(p, "pascal_wehrlein", cadillac, 0));
    EXPECT(moved_to(p, "rio_haryanto", cadillac, 1));
    EXPECT(moved_to(p, "valtteri_bottas", force_india, 0));
    for (auto &m : p.moves)
        EXPECT(m.driver != id("sergio_perez"));
}

void chosen_teammate_stays_on_either_seat()
{
    auto rows = career("sergio_perez");
    int williams = slot_of("williams_26");
    CareerRow &perez = row(rows, "sergio_perez");
    perez.team = g_data.slots[williams].team_stat;
    perez.position = row(rows, "alex_albon").position;
    EXPECT(perez.position == 0);
    free_agent(row(rows, "alex_albon"));
    season::Plan p = season::plan(g_data, 2016, rows, resolver());
    EXPECT(p.error.empty());
    EXPECT(seated_once(p));
    EXPECT(at(p, williams + 1, 0) == id("sergio_perez"));
    EXPECT(at(p, williams + 1, 1) == id("valtteri_bottas"));
    EXPECT(p.fit.displaced == id("felipe_massa"));
}

void player_added_as_a_third_race_seat()
{
    auto rows = career("sergio_perez");
    int williams = slot_of("williams_26");
    CareerRow &perez = row(rows, "sergio_perez");
    perez.team = g_data.slots[williams].team_stat;
    perez.position = 1;
    season::Plan p = season::plan(g_data, 2016, rows, resolver());
    EXPECT(p.error.empty());
    EXPECT(seated_once(p));
    EXPECT(p.extras.size() == 1);
    EXPECT(!p.extras.empty() && rows[p.extras[0]].driver == id("carlos_sainz-jr"));
    EXPECT(p.holder[williams][1] >= 0 && rows[p.holder[williams][1]].human);
    EXPECT(moved_to(p, "carlos_sainz-jr", slot_of("tororosso_26")));
    EXPECT(at(p, williams + 1, 1) == id("sergio_perez"));
    EXPECT(at(p, williams + 1, 0) == id("felipe_massa"));

    auto off = career("sergio_perez");
    CareerRow &h = row(off, "sergio_perez");
    h.team = g_data.slots[williams].team_stat;
    h.position = 2;
    season::Plan q = season::plan(g_data, 2016, off, resolver());
    EXPECT(q.error.empty());
    EXPECT(seated_once(q));
    EXPECT(q.holder[williams][1] >= 0 && off[q.holder[williams][1]].human);
    bool repositioned = false;
    for (auto &m : q.repositions)
        repositioned = repositioned || (m.driver == id("sergio_perez") && m.to.index == 1);
    EXPECT(repositioned);
    EXPECT(q.extras.size() == 1 && off[q.extras[0]].driver == id("carlos_sainz-jr"));
}

void empty_team_filled_from_free_agents()
{
    auto rows = career("lando_norris");
    int cadillac = slot_of("gm_cadillac_26");
    free_agent(row(rows, "sergio_perez"));
    free_agent(row(rows, "valtteri_bottas"));
    season::Plan p = season::plan(g_data, 2016, rows, resolver());
    EXPECT(p.error.empty());
    EXPECT(seated_once(p));
    EXPECT(p.holder[cadillac][0] < 0 && p.holder[cadillac][1] < 0);
    EXPECT(moved_to(p, "pascal_wehrlein", cadillac, 0));
    EXPECT(moved_to(p, "rio_haryanto", cadillac, 1));

    season::Plan same = season::plan(g_data, 2026, rows, resolver());
    EXPECT(same.error.empty());
    EXPECT(seated_once(same));
    EXPECT(moved_to(same, "sergio_perez", cadillac) && moved_to(same, "valtteri_bottas", cadillac));
}

void follow_player_left_on_his_2026_team_with_another_car()
{
    auto rows = career("valtteri_bottas");
    int williams = slot_of("williams_26"), cadillac = slot_of("gm_cadillac_26"), force_india = slot_of("forceindia_26");
    CareerRow &bottas = row(rows, "valtteri_bottas");
    EXPECT(bottas.team == g_data.slots[cadillac].team_stat && bottas.position == 1 && bottas.human);
    EXPECT(row(rows, "sergio_perez").team == g_data.slots[cadillac].team_stat);
    free_agent(row(rows, "carlos_sainz-jr"));
    season::Plan p = season::plan(g_data, 2016, rows, resolver(), true);
    EXPECT(p.error.empty());
    if (!p.error.empty())
        std::printf("  plan error: %s\n", p.error.c_str());
    EXPECT(seated_once(p));
    EXPECT(p.player_moves);
    EXPECT(p.holder[williams][1] < 0);
    EXPECT(at(p, williams + 1, 0) == id("felipe_massa"));
    EXPECT(at(p, williams + 1, 1) == id("valtteri_bottas"));
    EXPECT(moved_to(p, "valtteri_bottas", williams, 1));
    EXPECT(at(p, cadillac + 1, 0) == id("pascal_wehrlein"));
    EXPECT(at(p, cadillac + 1, 1) == id("rio_haryanto"));
    EXPECT(at(p, force_india + 1, 0) == id("sergio_perez"));
    EXPECT(moved_to(p, "sergio_perez", force_india, 0));
    EXPECT(moved_to(p, "carlos_sainz-jr", slot_of("tororosso_26")));
    bool albon_out = false;
    for (uint32_t d : p.releases)
        albon_out = albon_out || d == id("alex_albon");
    EXPECT(albon_out);
}

size_t seated_unique(const season::Plan &p)
{
    std::vector<uint32_t> all;
    for (auto &s : p.target)
        for (uint32_t d : s)
            if (d != lineup::kNone) {
                if (std::find(all.begin(), all.end(), d) != all.end())
                    return 0;
                all.push_back(d);
            }
    return all.size();
}

bool released(const season::Plan &p, const std::string &driver)
{
    return std::find(p.releases.begin(), p.releases.end(), id(driver)) != p.releases.end();
}

void ten_team_years_close_slot_11()
{
    auto rows = career("lando_norris");
    int cadillac = slot_of("gm_cadillac_26");
    rows.push_back({902, g_data.slots[cadillac].team_stat, 2, true, false, season::kNoNumber});
    season::Plan p = season::plan(g_data, 2017, rows, resolver(), false, nullptr, nullptr, false, true);
    EXPECT(p.error.empty());
    EXPECT(p.closed[cadillac]);
    EXPECT(season::closed_slots(p) == 1);
    EXPECT(at(p, cadillac + 1, 0) == lineup::kNone && at(p, cadillac + 1, 1) == lineup::kNone);
    EXPECT(seated_unique(p) == 20);
    for (auto &m : p.moves)
        EXPECT(m.to.slot != cadillac && m.driver != lineup::kNone);
    EXPECT(moved_to(p, "valtteri_bottas", slot_of("mercedes_26")));
    EXPECT(moved_to(p, "sergio_perez", slot_of("forceindia_26")));
    EXPECT(std::find(p.releases.begin(), p.releases.end(), 902u) != p.releases.end());
    for (auto &l : p.looks)
        EXPECT(l.driver != lineup::kNone);
    for (int y = 2018; y <= 2025; ++y) {
        season::Plan q = season::plan(g_data, y, rows, resolver(), false, nullptr, nullptr, false, true);
        EXPECT(q.error.empty() && q.closed[cadillac] && seated_unique(q) == 20);
    }
    for (int y : {2016, 2026}) {
        season::Plan q = season::plan(g_data, y, rows, resolver(), false, nullptr, nullptr, false, true);
        EXPECT(q.error.empty() && !q.closed[cadillac] && seated_unique(q) == 22);
    }
}

void ten_team_years_keep_the_players_slot()
{
    int cadillac = slot_of("gm_cadillac_26");
    auto rows = career("valtteri_bottas");
    season::Plan stay = season::plan(g_data, 2017, rows, resolver(), false, nullptr, nullptr, false, true);
    EXPECT(stay.error.empty());
    EXPECT(!stay.closed[cadillac]);
    EXPECT(seated_unique(stay) == 22);
    EXPECT(at(stay, cadillac + 1, 0) == id("valtteri_bottas") || at(stay, cadillac + 1, 1) == id("valtteri_bottas"));
    season::Plan follow = season::plan(g_data, 2017, rows, resolver(), true, nullptr, nullptr, false, true);
    EXPECT(follow.error.empty());
    EXPECT(follow.player_moves);
    EXPECT(follow.closed[cadillac]);
    EXPECT(seated_unique(follow) == 20);
    EXPECT(moved_to(follow, "valtteri_bottas", slot_of("mercedes_26")));
}

void ten_team_years_bring_cadillac_back_in_2026()
{
    int cadillac = slot_of("gm_cadillac_26");
    auto rows = career("lando_norris");
    for (auto &r : rows)
        if (r.team == g_data.slots[cadillac].team_stat)
            free_agent(r);
    season::Plan p = season::plan(g_data, 2026, rows, resolver(), false, nullptr, nullptr, false, true);
    EXPECT(p.error.empty());
    EXPECT(!p.closed[cadillac]);
    EXPECT(seated_unique(p) == 22);
    EXPECT(p.holder[cadillac][0] < 0 && p.holder[cadillac][1] < 0);
    EXPECT(moved_to(p, "sergio_perez", cadillac));
    EXPECT(moved_to(p, "valtteri_bottas", cadillac));
    for (auto &m : p.moves)
        if (m.to.slot == cadillac)
            EXPECT(m.from.slot < 0);
    EXPECT(!released(p, "sergio_perez") && !released(p, "valtteri_bottas"));
}

void season_start_standings_skip_a_closed_slot()
{
    int cadillac = slot_of("gm_cadillac_26");
    std::array<bool, season::kSlots> closed{};
    closed[cadillac] = true;
    for (int y = 2017; y <= 2025; ++y) {
        std::vector<int> order = season::start_slot_order(*g_data.year(y), closed);
        EXPECT(order.size() == static_cast<size_t>(season::kSlots - 1));
        EXPECT(std::find(order.begin(), order.end(), cadillac) == order.end());
        std::vector<int> open = season::start_slot_order(*g_data.year(y));
        open.erase(std::remove(open.begin(), open.end(), cadillac), open.end());
        EXPECT(order == open);
    }
}

void season_start_standings_follow_the_previous_year()
{
    const season::Year *y17 = g_data.year(2017);
    EXPECT(y17 && y17->previous_year == 2016);
    std::vector<int> order = season::start_slot_order(*y17);
    std::vector<std::string> teams;
    for (int s : order)
        teams.push_back(g_data.slots[s].team);
    std::vector<std::string> want = {"mercedes_26", "redbull_26", "ferrari_26", "forceindia_26", "williams_26",
                                     "mclaren_26", "tororosso_26", "haas_26", "renault_26", "sauber_26",
                                     "gm_cadillac_26"};
    EXPECT(teams == want);

    const season::Year *y16 = g_data.year(2016);
    std::vector<int> o16 = season::start_slot_order(*y16);
    EXPECT(o16.size() == static_cast<size_t>(season::kSlots));
    EXPECT(g_data.slots[o16[0]].team == "mercedes_26" && g_data.slots[o16[2]].team == "williams_26");
    EXPECT(g_data.slots[o16.back()].team == "haas_26");

    auto rows = career("valtteri_bottas");
    season::Plan p = season::plan(g_data, 2017, rows, resolver(), true);
    std::vector<uint32_t> seated;
    for (auto &s : p.target)
        for (uint32_t d : s)
            seated.push_back(d);
    std::vector<uint32_t> drivers = season::start_driver_order(*y17, resolver(), seated);
    EXPECT(drivers.size() == seated.size());
    std::vector<uint32_t> want_first;
    for (auto &label : y17->previous_drivers)
        if (std::find(seated.begin(), seated.end(), id(label)) != seated.end())
            want_first.push_back(id(label));
    EXPECT(want_first.size() >= 18);
    EXPECT(std::equal(want_first.begin(), want_first.end(), drivers.begin()));
    for (size_t i = want_first.size(), k = 0; i < drivers.size(); ++i) {
        while (k < seated.size() && std::find(want_first.begin(), want_first.end(), seated[k]) != want_first.end())
            ++k;
        EXPECT(k < seated.size() && drivers[i] == seated[k]);
        ++k;
    }
    auto at_ = [&](uint32_t d) { return std::find(drivers.begin(), drivers.end(), d) - drivers.begin(); };
    EXPECT(at_(id("lewis_hamilton")) < at_(id("daniel_ricciardo")));
    EXPECT(at_(id("daniel_ricciardo")) < at_(id("sebastian_vettel")));
    uint32_t stroll = id("lance_stroll");
    EXPECT(at_(stroll) > at_(id("sergio_perez")));
    EXPECT(at_(stroll) >= static_cast<long>(want_first.size()));
}

} // namespace

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "../../data/years.json";
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string err;
    if (!season::parse(ss.str(), g_data, err)) {
        std::printf("season_logic_test: cannot parse %s: %s\n", path, err.c_str());
        return EXIT_FAILURE;
    }
    years();
    parsed();
    stock_2026_unchanged_for_its_own_year();
    hamilton_at_ferrari_in_2016();
    follow_mode_moves_the_player_in_the_first_season();
    ten_team_year_fills_manor();
    missing_year_addon_falls_back();
    numbers_never_collide();
    hybrid_budgets_follow_pace();
    chosen_team_leaves_his_2026_seat_empty();
    chosen_teammate_stays_on_either_seat();
    player_added_as_a_third_race_seat();
    empty_team_filled_from_free_agents();
    follow_player_left_on_his_2026_team_with_another_car();
    ten_team_years_close_slot_11();
    ten_team_years_keep_the_players_slot();
    ten_team_years_bring_cadillac_back_in_2026();
    season_start_standings_follow_the_previous_year();
    season_start_standings_skip_a_closed_slot();
    if (failures) {
        std::printf("season_logic_test: %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("season_logic_test: ok\n");
    return EXIT_SUCCESS;
}
