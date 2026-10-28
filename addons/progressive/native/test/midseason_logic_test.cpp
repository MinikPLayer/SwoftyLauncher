// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The mid-season changes over the committed data/years.json: the career round of a real round on
// the stock 2026 calendars, the change windows, the line-up at a career round with the player
// rules, and the season plan at a round in follow and choose mode.

#include "midseason_logic.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

using namespace progressive;
using midseason::career_round;
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

const std::vector<int> kLong = {0, 2, 13, 3, 29, 30, 6, 5, 4, 17, 7, 10, 9, 26, 11, 42, 20, 12, 15, 19, 16, 31, 32, 14};
const std::vector<int> kShort = {0, 13, 3, 17, 7, 10, 11, 42, 16, 31};
const std::vector<int> kMedium = {0, 2, 13, 3, 30, 6, 17, 7, 10, 26, 11, 42, 16, 31, 32, 14};
const std::vector<int> kTest = {0, 13, 3};

int stat(const std::string &label)
{
    const season::PoolDriver *p = g_data.driver(label);
    return p ? p->stat : -1;
}

uint32_t st(const std::string &label)
{
    return static_cast<uint32_t>(stat(label));
}

season::Resolve resolver()
{
    season::Resolve r;
    r.driver_stat = stat;
    r.vehicle_stat = [](const std::string &label) {
        if (label.rfind("cs", 0) == 0)
            return 1000 + static_cast<int>(std::hash<std::string>{}(label) % 1000);
        for (auto &s : g_data.slots)
            if (s.vehicle_2026 == label)
                return s.vehicle_2026_stat;
        return -1;
    };
    r.look_known = [](int) { return true; };
    r.rating = [](uint32_t) { return 75.0; };
    return r;
}

std::function<bool(const std::string &)> player_is(const std::string &label)
{
    return [label](const std::string &d) { return d == label; };
}

std::string seat(const midseason::At &a, int slot1, int position)
{
    for (auto &d : a.line_up.teams[slot1 - 1].drivers)
        if (d.position == position)
            return d.driver;
    return {};
}

int change(int year, const std::string &id)
{
    const season::Year *y = g_data.year(year);
    for (size_t i = 0; i < y->changes.size(); ++i)
        if (y->changes[i].id == id)
            return static_cast<int>(i);
    return -1;
}

// The career's rows with the given race seats (slot 1-11, two labels each) and every other pool
// driver a free agent; `player` is the human row.
std::vector<CareerRow> rows_for(const std::vector<std::array<std::string, 2>> &seats, const std::string &player)
{
    std::vector<CareerRow> rows;
    std::vector<int> seated;
    for (int i = 0; i < season::kSlots; ++i)
        for (int k = 0; k < 2; ++k) {
            CareerRow r;
            r.driver = st(seats[i][k]);
            r.team = g_data.slots[i].team_stat;
            r.position = k;
            r.car = true;
            r.human = seats[i][k] == player;
            rows.push_back(r);
            seated.push_back(stat(seats[i][k]));
        }
    for (auto &p : g_data.pool)
        if (std::find(seated.begin(), seated.end(), p.stat) == seated.end()) {
            CareerRow r;
            r.driver = static_cast<uint32_t>(p.stat);
            rows.push_back(r);
        }
    return rows;
}

// The seats a plan leaves: each target driver on his row, the rest free agents.
std::vector<CareerRow> after(const season::Plan &p, const std::vector<CareerRow> &rows)
{
    std::vector<CareerRow> out = rows;
    for (auto &r : out)
        if (r.car) {
            r.car = false;
            r.team = -1;
            r.position = -1;
        }
    for (int i = 0; i < season::kSlots; ++i)
        for (int k = 0; k < 2; ++k)
            for (auto &r : out)
                if (r.driver == p.target[i][k]) {
                    r.car = true;
                    r.team = g_data.slots[i].team_stat;
                    r.position = k;
                }
    return out;
}

bool in_slot(const season::Plan &p, int slot1, const std::string &label)
{
    return p.target[slot1 - 1][0] == st(label) || p.target[slot1 - 1][1] == st(label);
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

std::vector<std::array<std::string, 2>> season_seats(int year)
{
    std::vector<std::array<std::string, 2>> s(season::kSlots);
    const season::Year *y = g_data.year(year);
    std::vector<std::string> spare = {"jenson_button", "felipe_massa"};
    for (int i = 0; i < season::kSlots; ++i) {
        auto ds = y->teams[i].drivers;
        for (int k = 0; k < 2; ++k) {
            if (k < static_cast<int>(ds.size())) {
                s[i][k] = ds[k].driver;
            } else {
                s[i][k] = spare.back();
                spare.pop_back();
            }
        }
    }
    return s;
}

void data_has_the_real_changes()
{
    const season::Year *y16 = g_data.year(2016);
    EXPECT(y16->real_tracks.size() == 21);
    EXPECT(y16->changes.size() == 4);
    int v = change(2016, "2016-r2-s4a-stoffel_vandoorne");
    EXPECT(v >= 0 && y16->changes[v].until == 3 && y16->changes[v].number == 47 && y16->changes[v].kind == "stand_in");
    int o = change(2016, "2016-r13-s11b-esteban_ocon");
    EXPECT(o >= 0 && y16->changes[o].until == 0 && y16->changes[o].out == "rio_haryanto");
    EXPECT(g_data.year(2018)->changes.empty());
    EXPECT(g_data.year(2026)->changes.empty());
    EXPECT(change(2025, "2025-r1-s3b-liam_lawson") >= 0);
    EXPECT(g_data.year(2025)->changes[change(2025, "2025-r1-s3b-liam_lawson")].look.label == "2025-redbull_liam_lawson");
    EXPECT(change(2020, "2020-r16-s1a-george_russell") >= 0);
}

void timing()
{
    const auto &t16 = g_data.year(2016)->real_tracks;
    EXPECT(career_round(t16, 1, kLong) == 1);
    EXPECT(career_round(t16, 2, kLong) == 4);
    EXPECT(career_round(t16, 5, kLong) == 9);
    EXPECT(career_round(t16, 13, kLong) == 12);
    EXPECT(career_round(t16, 8, kLong) == 9);
    EXPECT(career_round(t16, 17, kLong) == 19);
    EXPECT(career_round(t16, 4, kLong) == 5);
    EXPECT(career_round(t16, 21, kLong) == 24);
    EXPECT(career_round(t16, 5, kShort) == 2);
    EXPECT(career_round(t16, 2, kShort) == 3);
    EXPECT(career_round(t16, 13, kShort) == 6);
    std::vector<int> aliased = kLong;
    aliased[3] = 35;
    EXPECT(career_round(t16, 2, aliased) == 4);
    EXPECT(career_round(t16, 2, {}) == 1);
    const auto &t20 = g_data.year(2020)->real_tracks;
    EXPECT(career_round(t20, 16, kLong) == 23);
    EXPECT(career_round(t20, 17, kLong) == 24);
}

void windows()
{
    const season::Year *y = g_data.year(2016);
    std::vector<bool> all(y->changes.size(), true);
    auto w = midseason::windows(*y, kLong, all);
    int v = change(2016, "2016-r2-s4a-stoffel_vandoorne");
    EXPECT(w[v].from == 4 && w[v].to == 5);
    int k = change(2016, "2016-r1-s3b-daniil_kvyat");
    EXPECT(w[k].from == 1 && w[k].to == 9);
    int o = change(2016, "2016-r13-s11b-esteban_ocon");
    EXPECT(w[o].from == 12 && w[o].to == midseason::kOpen);
    const season::Year *y20 = g_data.year(2020);
    auto w20 = midseason::windows(*y20, kLong, std::vector<bool>(y20->changes.size(), true));
    int r = change(2020, "2020-r16-s1a-george_russell");
    EXPECT(w20[r].from == 23 && w20[r].to == 24);
    int h = change(2020, "2020-r4-s5a-nico_hulkenberg");
    EXPECT(w20[h].from == 11 && w20[h].to == 13);
    const season::Year *y22 = g_data.year(2022);
    auto w22 = midseason::windows(*y22, kLong, std::vector<bool>(y22->changes.size(), true));
    int dv = change(2022, "2022-r16-s7a-nyck_de-vries");
    EXPECT(w22[dv].from == 15 && w22[dv].to == 16);
    const season::Year *y17 = g_data.year(2017);
    auto w17 = midseason::windows(*y17, kLong, std::vector<bool>(y17->changes.size(), true));
    int gio = change(2017, "2017-r1-s9b-antonio_giovinazzi");
    EXPECT(w17[gio].from == 1 && w17[gio].to == 3);
    auto ws = midseason::windows(*y17, kShort, std::vector<bool>(y17->changes.size(), true));
    for (size_t i = 0; i + 1 < y17->changes.size(); ++i)
        EXPECT(ws[i].from <= ws[i + 1].from);
    for (auto &x : ws)
        EXPECT(x.to > x.from);
}

void line_up_by_round()
{
    auto nobody = player_is("");
    auto r1 = midseason::at_round(g_data, 2016, kLong, 1, nobody);
    EXPECT(seat(r1, 3, 1) == "daniil_kvyat");
    EXPECT(seat(r1, 8, 1) == "max_verstappen");
    EXPECT(seat(r1, 4, 0) == "fernando_alonso");
    auto r4 = midseason::at_round(g_data, 2016, kLong, 4, nobody);
    EXPECT(seat(r4, 4, 0) == "stoffel_vandoorne");
    for (auto &d : r4.line_up.teams[3].drivers)
        if (d.driver == "stoffel_vandoorne")
            EXPECT(d.number == 47 && d.look.label == "2017-cs_mclaren_stoffel_vandoorne");
    auto r5 = midseason::at_round(g_data, 2016, kLong, 5, nobody);
    EXPECT(seat(r5, 4, 0) == "fernando_alonso");
    EXPECT(seat(r5, 3, 1) == "daniil_kvyat");
    auto r9 = midseason::at_round(g_data, 2016, kLong, 9, nobody);
    EXPECT(seat(r9, 3, 1) == "max_verstappen");
    EXPECT(seat(r9, 8, 1) == "daniil_kvyat");
    EXPECT(seat(r9, 11, 1) == "rio_haryanto");
    auto r12 = midseason::at_round(g_data, 2016, kLong, 12, nobody);
    EXPECT(seat(r12, 11, 1) == "esteban_ocon");
    auto end = midseason::at_round(g_data, 2016, kLong, 25, nobody);
    EXPECT(seat(end, 11, 1) == "esteban_ocon");
    EXPECT(seat(end, 3, 1) == "max_verstappen");

    std::vector<std::string> b;
    for (int n = 1; n <= 24; ++n)
        b.push_back(seat(midseason::at_round(g_data, 2017, kLong, n, nobody), 8, 1));
    int gasly_first = -1, kvyat_back = -1, gasly_last = -1;
    for (int n = 0; n < 24; ++n) {
        if (b[n] == "pierre_gasly" && gasly_first < 0)
            gasly_first = n;
        if (gasly_first >= 0 && b[n] == "daniil_kvyat" && kvyat_back < 0)
            kvyat_back = n;
        if (kvyat_back >= 0 && b[n] == "pierre_gasly" && gasly_last < 0)
            gasly_last = n;
    }
    EXPECT(gasly_first > 0 && kvyat_back > gasly_first && gasly_last > kvyat_back);
    EXPECT(b[23] == "pierre_gasly");

    for (int year = 2016; year <= 2025; ++year)
        for (auto *cal : {&kLong, &kShort})
            for (int n = 1; n <= static_cast<int>(cal->size()) + 1; ++n) {
                auto a = midseason::at_round(g_data, year, *cal, n, nobody);
                std::vector<std::string> seen;
                for (auto &t : a.line_up.teams)
                    for (auto &d : t.drivers) {
                        EXPECT(std::find(seen.begin(), seen.end(), d.driver) == seen.end());
                        seen.push_back(d.driver);
                    }
            }
}

void player_rules()
{
    auto alonso = midseason::at_round(g_data, 2016, kLong, 4, player_is("fernando_alonso"));
    EXPECT(seat(alonso, 4, 0) == "fernando_alonso");
    EXPECT(alonso.skipped.size() == 1);
    auto max1 = midseason::at_round(g_data, 2016, kLong, 1, player_is("max_verstappen"));
    EXPECT(seat(max1, 8, 1) == "max_verstappen" && seat(max1, 3, 1) == "daniil_kvyat");
    EXPECT(max1.skipped.empty());
    auto russell = midseason::at_round(g_data, 2020, kLong, 23, player_is("george_russell"));
    EXPECT(seat(russell, 1, 0) == "lewis_hamilton" && seat(russell, 7, 0) == "george_russell");
    EXPECT(russell.skipped.size() == 2);
    auto aitken = midseason::at_round(g_data, 2020, kLong, 23, player_is(""));
    EXPECT(seat(aitken, 1, 0) == "george_russell" && seat(aitken, 7, 0) == "jack_aitken");
    auto back = midseason::at_round(g_data, 2020, kLong, 24, player_is(""));
    EXPECT(seat(back, 1, 0) == "lewis_hamilton" && seat(back, 7, 0) == "george_russell");
    auto kvyat = midseason::at_round(g_data, 2017, kLong, 24, player_is("daniil_kvyat"));
    EXPECT(seat(kvyat, 8, 1) == "daniil_kvyat");
    auto sainz = midseason::at_round(g_data, 2017, kLong, 24, player_is("carlos_sainz-jr"));
    EXPECT(seat(sainz, 6, 1) == "carlos_sainz-jr" && seat(sainz, 8, 0) == "brendon_hartley");
    auto sargeant = midseason::at_round(g_data, 2024, kLong, 20, player_is("logan_sargeant"));
    EXPECT(seat(sargeant, 7, 1) == "logan_sargeant");
}

void follow_moves_the_player_with_his_driver()
{
    season::Resolve r = resolver();
    auto rows = rows_for(season_seats(2016), "max_verstappen");
    auto is = player_is("max_verstappen");
    auto opening = midseason::at_round(g_data, 2016, kLong, 1, is);
    auto r1 = midseason::at_round(g_data, 2016, kLong, 1, is);
    season::Plan p1 = season::plan(g_data, 2016, rows, r, true, &r1.line_up, &opening.line_up);
    EXPECT(p1.error.empty());
    EXPECT(seated_once(p1));
    EXPECT(p1.player_moves);
    EXPECT(in_slot(p1, 8, "max_verstappen") && in_slot(p1, 3, "daniil_kvyat"));
    auto rows1 = after(p1, rows);
    auto r8 = midseason::at_round(g_data, 2016, kLong, 8, is);
    season::Plan p8 = season::plan(g_data, 2016, rows1, r, true, &r8.line_up, &opening.line_up);
    EXPECT(p8.moves.empty() && p8.releases.empty());
    auto r9 = midseason::at_round(g_data, 2016, kLong, 9, is);
    season::Plan p9 = season::plan(g_data, 2016, rows1, r, true, &r9.line_up, &opening.line_up);
    EXPECT(p9.player_moves);
    EXPECT(in_slot(p9, 3, "max_verstappen") && in_slot(p9, 8, "daniil_kvyat"));
    EXPECT(p9.moves.size() == 2);

    auto lawson_rows = rows_for(season_seats(2025), "liam_lawson");
    auto lis = player_is("liam_lawson");
    auto lo = midseason::at_round(g_data, 2025, kLong, 1, lis);
    season::Plan l1 = season::plan(g_data, 2025, lawson_rows, r, true, &lo.line_up, &lo.line_up);
    EXPECT(in_slot(l1, 3, "liam_lawson") && in_slot(l1, 8, "yuki_tsunoda"));
    auto l3 = midseason::at_round(g_data, 2025, kLong, 3, lis);
    season::Plan p3 = season::plan(g_data, 2025, after(l1, lawson_rows), r, true, &l3.line_up, &lo.line_up);
    EXPECT(p3.player_moves && in_slot(p3, 8, "liam_lawson") && in_slot(p3, 3, "yuki_tsunoda"));

    auto alonso_rows = rows_for(season_seats(2016), "fernando_alonso");
    auto ais = player_is("fernando_alonso");
    auto a4 = midseason::at_round(g_data, 2016, kLong, 4, ais);
    auto a1 = midseason::at_round(g_data, 2016, kLong, 1, ais);
    season::Plan pa = season::plan(g_data, 2016, alonso_rows, r, true, &a4.line_up, &a1.line_up);
    EXPECT(in_slot(pa, 4, "fernando_alonso") && !in_slot(pa, 4, "stoffel_vandoorne"));
    for (auto &m : pa.moves)
        EXPECT(m.driver != st("fernando_alonso"));
}

void choose_fits_changes_around_the_player()
{
    season::Resolve r = resolver();
    auto seats = season_seats(2016);
    seats[2][1] = "lewis_hamilton";
    seats[0][0] = "max_verstappen";
    auto rows = rows_for(seats, "lewis_hamilton");
    auto is = player_is("lewis_hamilton");
    auto opening = midseason::at_round(g_data, 2016, kLong, 1, is);
    season::Plan p1 = season::plan(g_data, 2016, rows, r, false, &opening.line_up, &opening.line_up);
    EXPECT(p1.error.empty() && seated_once(p1));
    EXPECT(in_slot(p1, 3, "lewis_hamilton") && in_slot(p1, 3, "daniel_ricciardo"));
    EXPECT(in_slot(p1, 1, "daniil_kvyat") && in_slot(p1, 1, "nico_rosberg"));
    EXPECT(in_slot(p1, 8, "max_verstappen"));
    auto rows1 = after(p1, rows);
    auto r9 = midseason::at_round(g_data, 2016, kLong, 9, is);
    season::Plan p9 = season::plan(g_data, 2016, rows1, r, false, &r9.line_up, &opening.line_up);
    EXPECT(p9.error.empty() && seated_once(p9));
    EXPECT(!p9.player_moves);
    EXPECT(in_slot(p9, 3, "lewis_hamilton") && in_slot(p9, 3, "daniel_ricciardo"));
    EXPECT(in_slot(p9, 1, "max_verstappen") && in_slot(p9, 8, "daniil_kvyat"));
    for (auto &m : p9.moves)
        EXPECT(m.driver != st("lewis_hamilton") && m.driver != st("daniel_ricciardo"));

    auto vseats = season_seats(2016);
    vseats[1][1] = "max_verstappen";
    vseats[2][1] = "kimi_raikkonen";
    auto vrows = rows_for(vseats, "max_verstappen");
    auto vis = player_is("max_verstappen");
    auto vo = midseason::at_round(g_data, 2016, kLong, 1, vis);
    season::Plan v1 = season::plan(g_data, 2016, vrows, r, false, &vo.line_up, &vo.line_up);
    EXPECT(in_slot(v1, 2, "max_verstappen") && in_slot(v1, 2, "sebastian_vettel"));
    EXPECT(in_slot(v1, 8, "kimi_raikkonen") && in_slot(v1, 3, "daniil_kvyat"));
    auto v9 = midseason::at_round(g_data, 2016, kLong, 9, vis);
    season::Plan p = season::plan(g_data, 2016, after(v1, vrows), r, false, &v9.line_up, &vo.line_up);
    EXPECT(seated_once(p));
    EXPECT(in_slot(p, 2, "max_verstappen"));
    EXPECT(in_slot(p, 3, "kimi_raikkonen") && in_slot(p, 8, "daniil_kvyat"));

    auto rseats = season_seats(2019);
    rseats[2][1] = "nico_rosberg";
    auto rrows = rows_for(rseats, "nico_rosberg");
    auto ris = player_is("nico_rosberg");
    auto ro = midseason::at_round(g_data, 2019, kLong, 1, ris);
    season::Plan r1 = season::plan(g_data, 2019, rrows, r, false, &ro.line_up, &ro.line_up);
    EXPECT(r1.error.empty() && in_slot(r1, 3, "nico_rosberg") && !in_slot(r1, 3, "pierre_gasly"));
    auto r12 = midseason::at_round(g_data, 2019, kLong, 12, ris);
    EXPECT(seat(r12, 3, 1) == "alex_albon");
    season::Plan rp = season::plan(g_data, 2019, after(r1, rrows), r, false, &r12.line_up, &ro.line_up);
    EXPECT(rp.error.empty() && seated_once(rp));
    EXPECT(in_slot(rp, 3, "nico_rosberg") && in_slot(rp, 3, "max_verstappen"));
    EXPECT(in_slot(rp, 8, "alex_albon") && !in_slot(rp, 8, "pierre_gasly"));
    EXPECT(rp.moves.empty() && rp.releases.empty());

    auto own = season_seats(2016);
    auto orows = rows_for(own, "daniil_kvyat");
    auto kis = player_is("daniil_kvyat");
    auto ko = midseason::at_round(g_data, 2016, kLong, 1, kis);
    season::Plan k1 = season::plan(g_data, 2016, orows, r, false, &ko.line_up, &ko.line_up);
    EXPECT(in_slot(k1, 8, "daniil_kvyat") && in_slot(k1, 3, "max_verstappen"));
    auto k9 = midseason::at_round(g_data, 2016, kLong, 9, kis);
    season::Plan k9p = season::plan(g_data, 2016, after(k1, orows), r, false, &k9.line_up, &ko.line_up);
    EXPECT(k9p.moves.empty());
}

void changes_fill_an_empty_seat()
{
    season::Resolve r = resolver();
    auto seats = season_seats(2016);
    seats[2][1] = "daniil_kvyat";
    seats[7][1] = "max_verstappen";
    auto rows = rows_for(seats, "lewis_hamilton");
    for (auto &row : rows)
        if (row.driver == st("max_verstappen")) {
            row.car = false;
            row.team = -1;
            row.position = -1;
        }
    auto is = player_is("lewis_hamilton");
    auto opening = midseason::at_round(g_data, 2016, kLong, 1, is);
    auto r9 = midseason::at_round(g_data, 2016, kLong, 9, is);
    season::Plan p = season::plan(g_data, 2016, rows, r, true, &r9.line_up, &opening.line_up);
    EXPECT(p.error.empty() && seated_once(p));
    EXPECT(p.holder[7][1] < 0);
    EXPECT(p.target[7][1] == st("daniil_kvyat") && p.target[2][1] == st("max_verstappen"));
    bool kvyat = false, max = false;
    for (auto &m : p.moves) {
        kvyat = kvyat || (m.driver == st("daniil_kvyat") && m.to.slot == 7 && m.to.index == 1);
        max = max || (m.driver == st("max_verstappen") && m.from.slot < 0 && m.to.slot == 2);
    }
    EXPECT(kvyat && max);
}

// Every change ends inside the season, whatever its length: on the 3-race test calendar of
// swofty's 2016 career (tracks 0,13,3) Kvyat and Verstappen swapped back at career round 5 and
// Alonso came back at round 4, after the season.
int rounds_with(int year, const std::vector<int> &cal, int slot1, int position, const std::string &driver,
                int *first = nullptr, int *last = nullptr)
{
    int count = 0;
    if (first)
        *first = -1;
    if (last)
        *last = -1;
    for (int n = 1; n <= static_cast<int>(cal.size()); ++n)
        if (seat(midseason::at_round(g_data, year, cal, n, player_is("")), slot1, position) == driver) {
            ++count;
            if (first && *first < 0)
                *first = n;
            if (last)
                *last = n;
        }
    return count;
}

void ends_scale_to_the_season()
{
    const std::vector<int> two = {0, 14};
    for (auto *cal : {&two, &kTest, &kShort, &kMedium, &kLong}) {
        int m = static_cast<int>(cal->size());
        int first = 0, last = 0;
        EXPECT(seat(midseason::at_round(g_data, 2016, *cal, 1, player_is("")), 3, 1) == "daniil_kvyat");
        EXPECT(seat(midseason::at_round(g_data, 2016, *cal, 1, player_is("")), 8, 1) == "max_verstappen");
        EXPECT(rounds_with(2016, *cal, 3, 1, "max_verstappen", &first) >= 1 && first >= 2 && first <= m);
        EXPECT(seat(midseason::at_round(g_data, 2016, *cal, m, player_is("")), 3, 1) == "max_verstappen");
        EXPECT(seat(midseason::at_round(g_data, 2016, *cal, m, player_is("")), 8, 1) == "daniil_kvyat");
        EXPECT(rounds_with(2016, *cal, 4, 0, "stoffel_vandoorne", &first) == 1);
        if (first < m)
            EXPECT(seat(midseason::at_round(g_data, 2016, *cal, first + 1, player_is("")), 4, 0) == "fernando_alonso");
        EXPECT(rounds_with(2016, *cal, 11, 1, "esteban_ocon", &first, &last) >= 1 && last == m);

        if (career_round(g_data.year(2019)->real_tracks, 13, *cal) > 1)
            EXPECT(seat(midseason::at_round(g_data, 2019, *cal, 1, player_is("")), 3, 1) == "pierre_gasly");
        else
            EXPECT(m == 2);
        EXPECT(seat(midseason::at_round(g_data, 2019, *cal, m, player_is("")), 3, 1) == "alex_albon");
        EXPECT(seat(midseason::at_round(g_data, 2019, *cal, m, player_is("")), 8, 1) == "pierre_gasly");

        EXPECT(rounds_with(2020, *cal, 1, 0, "george_russell", &first) == 1);
        int aitken = 0;
        EXPECT(rounds_with(2020, *cal, 7, 0, "jack_aitken", &aitken) == 1 && aitken == first);
        if (first < m) {
            auto back = midseason::at_round(g_data, 2020, *cal, first + 1, player_is(""));
            EXPECT(seat(back, 1, 0) == "lewis_hamilton" && seat(back, 7, 0) == "george_russell");
        }

        for (int year = 2016; year <= 2025; ++year) {
            const season::Year *y = g_data.year(year);
            auto w = midseason::windows(*y, *cal, std::vector<bool>(y->changes.size(), true));
            for (size_t i = 0; i < y->changes.size(); ++i) {
                EXPECT(w[i].from >= 1 && w[i].from <= m && w[i].to > w[i].from);
                if (y->changes[i].until && w[i].from < m)
                    EXPECT(w[i].to <= m);
                if (y->changes[i].kind == "stand_in")
                    EXPECT(w[i].to - w[i].from <= y->changes[i].until - y->changes[i].round);
            }
        }
    }
    const season::Year *y = g_data.year(2016);
    auto w = midseason::windows(*y, kTest, std::vector<bool>(y->changes.size(), true));
    int k = change(2016, "2016-r1-s3b-daniil_kvyat"), v = change(2016, "2016-r2-s4a-stoffel_vandoorne");
    EXPECT(w[k].from == 1 && w[k].to == 2);
    EXPECT(w[v].from == 3 && w[v].to == 4);
}

void ends_follow_the_player()
{
    season::Resolve r = resolver();
    auto rows = rows_for(season_seats(2016), "max_verstappen");
    auto is = player_is("max_verstappen");
    auto opening = midseason::at_round(g_data, 2016, kTest, 1, is);
    season::Plan p1 = season::plan(g_data, 2016, rows, r, true, &opening.line_up, &opening.line_up);
    EXPECT(p1.player_moves && in_slot(p1, 8, "max_verstappen"));
    auto r2 = midseason::at_round(g_data, 2016, kTest, 2, is);
    season::Plan p2 = season::plan(g_data, 2016, after(p1, rows), r, true, &r2.line_up, &opening.line_up);
    EXPECT(p2.player_moves && in_slot(p2, 3, "max_verstappen") && in_slot(p2, 8, "daniil_kvyat"));

    auto seats = season_seats(2016);
    seats[2][1] = "lewis_hamilton";
    seats[0][0] = "max_verstappen";
    auto hrows = rows_for(seats, "lewis_hamilton");
    auto his = player_is("lewis_hamilton");
    auto ho = midseason::at_round(g_data, 2016, kTest, 1, his);
    season::Plan h1 = season::plan(g_data, 2016, hrows, r, false, &ho.line_up, &ho.line_up);
    EXPECT(in_slot(h1, 1, "daniil_kvyat") && in_slot(h1, 3, "lewis_hamilton"));
    auto h2 = midseason::at_round(g_data, 2016, kTest, 2, his);
    season::Plan p = season::plan(g_data, 2016, after(h1, hrows), r, false, &h2.line_up, &ho.line_up);
    EXPECT(in_slot(p, 1, "max_verstappen") && in_slot(p, 8, "daniil_kvyat") && in_slot(p, 3, "lewis_hamilton"));
}

// A season raced round by round on a career calendar, Hamilton (Mercedes every year) the player in
// follow mode: the first apply chooses the fillers, later ones keep them; each round's plan feeds
// the next. In swofty's 2017 rollover Button was a Manor filler, came in for Alonso at Monaco and
// Alonso took his filler seat.
struct Raced {
    std::vector<season::Plan> plans;
    std::vector<midseason::At> at;
};

Raced race_season(int year, const std::vector<int> &cal, bool live_ratings = false)
{
    season::Resolve r = resolver();
    int round = 1;
    if (live_ratings)
        r.rating = [&round](uint32_t d) { return 60.0 + static_cast<double>((d * 7 + round * 13) % 30); };
    auto seats = season_seats(year);
    auto rows = rows_for(seats, "lewis_hamilton");
    auto is = player_is("lewis_hamilton");
    auto opening = midseason::at_round(g_data, year, cal, 1, is);
    Raced out;
    for (int n = 1; n <= static_cast<int>(cal.size()) + 1; ++n) {
        round = n;
        out.at.push_back(midseason::at_round(g_data, year, cal, n, is));
        out.plans.push_back(season::plan(g_data, year, rows, r, true, &out.at.back().line_up, &opening.line_up, n > 1));
        rows = after(out.plans.back(), rows);
    }
    return out;
}

void changes_leave_the_fillers_alone()
{
    for (int year = 2017; year <= 2025; ++year)
        for (auto *cal : {&kTest, &kShort, &kLong}) {
            Raced s = race_season(year, *cal);
            const season::Year *y = g_data.year(year);
            std::vector<uint32_t> changed;
            for (auto &c : y->changes)
                for (const std::string *l : {&c.in, &c.out})
                    changed.push_back(st(*l));
            for (size_t n = 0; n < s.plans.size(); ++n) {
                const season::Plan &p = s.plans[n];
                EXPECT(p.error.empty() && seated_once(p));
                EXPECT(p.target[10] == s.plans[0].target[10]);
                for (uint32_t d : p.target[10])
                    EXPECT(std::find(changed.begin(), changed.end(), d) == changed.end());
                const midseason::At &a = s.at[n];
                for (int i : a.active) {
                    const season::Change &c = y->changes[i];
                    EXPECT(in_slot(p, c.slot + 1, c.in));
                    bool moved_on = false;
                    for (int j : a.active)
                        moved_on = moved_on || y->changes[j].in == c.out;
                    if (!moved_on)
                        for (auto &t : p.target)
                            for (uint32_t d : t)
                                EXPECT(d != st(c.out));
                }
            }
        }
}

void fillers_ignore_rating_drift()
{
    for (int year : {2017, 2021, 2025}) {
        Raced s = race_season(year, kLong, true);
        for (auto &p : s.plans)
            EXPECT(p.target[10] == s.plans[0].target[10]);
    }
}

void button_at_monaco_only()
{
    for (auto *cal : {&kTest, &kLong}) {
        Raced s = race_season(2017, *cal);
        int monaco = midseason::career_round(g_data.year(2017)->real_tracks, 6, *cal);
        for (size_t n = 0; n < s.plans.size(); ++n) {
            const season::Plan &p = s.plans[n];
            bool button = in_slot(p, 4, "jenson_button"), alonso = false;
            for (auto &t : p.target)
                for (uint32_t d : t)
                    alonso = alonso || d == st("fernando_alonso");
            EXPECT(button == (static_cast<int>(n) + 1 == monaco));
            EXPECT(alonso != button);
            EXPECT(!button || in_slot(p, 4, "stoffel_vandoorne"));
            for (auto &m : p.moves)
                EXPECT(!(m.driver == st("fernando_alonso") && m.to.slot != 3));
        }
    }
    for (auto *cal : {&kTest, &kLong}) {
        Raced h = race_season(2020, *cal);
        bool hulk = false;
        for (auto &p : h.plans) {
            hulk = hulk || in_slot(p, 5, "nico_hulkenberg");
            EXPECT(!in_slot(p, 11, "nico_hulkenberg"));
            if (in_slot(p, 5, "nico_hulkenberg"))
                EXPECT(!in_slot(p, 5, "sergio_perez") || !in_slot(p, 5, "lance_stroll"));
        }
        EXPECT(hulk);
        Raced b = race_season(2024, *cal);
        bool ferrari = false, haas = false;
        for (auto &p : b.plans) {
            ferrari = ferrari || in_slot(p, 2, "oliver_bearman");
            haas = haas || in_slot(p, 10, "oliver_bearman");
            EXPECT(!in_slot(p, 11, "oliver_bearman"));
            if (in_slot(p, 2, "oliver_bearman")) {
                for (auto &t : p.target)
                    for (uint32_t d : t)
                        EXPECT(d != st("carlos_sainz-jr"));
            }
        }
        EXPECT(ferrari && haas);
    }
}

// swofty's 2017 rollover: the first Team HQ of the season could not read the calendar, so the apply
// used the plain season line-up and the round-1 changes came a Team HQ later.
void season_break_timing()
{
    using From = midseason::Timing::From;
    const season::Year &y17 = *g_data.year(2017);
    auto t = midseason::timing(y17, true, kTest, true, 2, false, {}, false);
    EXPECT(t.from == From::calendar && t.next == 2 && t.tracks == kTest);
    t = midseason::timing(y17, true, kTest, false, 1, true, {}, false);
    EXPECT(t.from == From::this_season && t.next == 1 && t.tracks == kTest);
    t = midseason::timing(y17, false, {}, false, 1, true, midseason::split_tracks("0,13,3"), false);
    EXPECT(t.from == From::recorded && t.next == 1 && t.tracks == kTest);
    auto a = midseason::at_round(g_data, 2017, t.tracks, t.next, player_is(""));
    EXPECT(seat(a, 9, 1) == "antonio_giovinazzi" && seat(a, 4, 0) == "jenson_button");
    t = midseason::timing(y17, false, {}, false, 1, true, {}, false);
    EXPECT(t.from == From::real && t.next == 1 && t.tracks.size() == 20);
    a = midseason::at_round(g_data, 2017, t.tracks, t.next, player_is(""));
    EXPECT(seat(a, 9, 1) == "antonio_giovinazzi" && seat(a, 4, 0) == "fernando_alonso");
    t = midseason::timing(y17, false, {}, false, 1, false, midseason::split_tracks("0,13,3"), true);
    EXPECT(t.from == From::record);
    t = midseason::timing(y17, false, {}, false, 1, false, midseason::split_tracks("0,13,3"), false);
    EXPECT(t.from == From::none);
    t = midseason::timing(y17, true, kTest, false, 1, false, {}, false);
    EXPECT(t.from == From::none);
    EXPECT(midseason::tracks_text(kTest) == "0,13,3");
    EXPECT(midseason::split_tracks("").empty());
}

void recorded_ids_round_trip()
{
    auto a = midseason::at_round(g_data, 2016, kLong, 4, player_is(""));
    std::string ids = midseason::ids_text(*g_data.year(2016), a.active);
    EXPECT(ids == "2016-r1-s3b-daniil_kvyat,2016-r1-s8b-max_verstappen,2016-r2-s4a-stoffel_vandoorne");
    auto b = midseason::at_ids(g_data, 2016, 4, midseason::split_ids(ids));
    EXPECT(seat(b, 4, 0) == "stoffel_vandoorne" && seat(b, 3, 1) == "daniil_kvyat");
    EXPECT(midseason::split_ids("").empty());
}

void ten_team_years_keep_changes_out_of_slot_11()
{
    for (int year = 2017; year <= 2025; ++year) {
        auto seats = season_seats(year);
        std::string player = seats[0][0];
        auto rows = rows_for(seats, player);
        auto is = player_is(player);
        auto r = resolver();
        auto opening = midseason::at_round(g_data, year, kLong, 1, is);
        season::Plan first = season::plan(g_data, year, rows, r, true, &opening.line_up, &opening.line_up, false, true);
        EXPECT(first.error.empty() && first.closed[10]);
        auto now = after(first, rows);
        for (int n = 1; n <= 25; ++n) {
            auto at = midseason::at_round(g_data, year, kLong, n, is);
            season::Plan p = season::plan(g_data, year, now, r, true, &at.line_up, &opening.line_up, true, true);
            EXPECT(p.error.empty());
            EXPECT(p.closed[10]);
            EXPECT(p.target[10][0] == lineup::kNone && p.target[10][1] == lineup::kNone);
            size_t seated = 0;
            for (auto &sl : p.target)
                for (uint32_t d : sl)
                    seated += d != lineup::kNone;
            EXPECT(seated == 20);
            for (auto &m : p.moves)
                EXPECT(m.to.slot != 10);
            now = after(p, now);
        }
    }
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
        std::printf("midseason_logic_test: cannot parse %s: %s\n", path, err.c_str());
        return EXIT_FAILURE;
    }
    data_has_the_real_changes();
    timing();
    windows();
    line_up_by_round();
    player_rules();
    follow_moves_the_player_with_his_driver();
    choose_fits_changes_around_the_player();
    changes_fill_an_empty_seat();
    ends_scale_to_the_season();
    ends_follow_the_player();
    changes_leave_the_fillers_alone();
    button_at_monaco_only();
    fillers_ignore_rating_drift();
    season_break_timing();
    recorded_ids_round_trip();
    ten_team_years_keep_changes_out_of_slot_11();
    if (failures) {
        std::printf("midseason_logic_test: %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("midseason_logic_test: ok\n");
    return EXIT_SUCCESS;
}
