// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career: the season apply (DESIGN.md D6). At every idle Team HQ of a career filed
// under f1_career_progressive the live seats are planned against the career's year and written
// where they differ, whatever the store's applied season says (that marker only gates the
// once-a-season steps, the hybrid budgets):
//   1. year = min(start + Championship.season_index, 2026) (season_logic.hpp);
//   2. the plan: the year's line-up fitted around the player, its moves, releases and numbers, each
//      slot's car and each seated driver's look (season_logic.hpp, data/years.json); in follow mode
//      the career's first season also moves the player's own row to his real team of that year;
//   3. one batch through the Career Mode Editor's Driver Career sequence (seats.hpp): every move
//      announced (MessageDriverMoveConfirmed) and written with the seat's team and car keys, every
//      released driver's contract released, announced to no team and written teamless; then the
//      number-only and car/look rows; every row read back against the plan (written back on a
//      mismatch); the new seats' contracts signed; numbers for seated rows without one; one
//      finalise against the line-up from before the batch;
//   4. the team names of the year through swofty::lang (also on every career load), and the race
//      weekend and season titles with the year in place of 2026 (titles_logic.hpp, data/titles.json);
//   5. in a hybrid R&D career, each team's resource points for the season;
//   6. each driver's real ratings of the year (ratings.hpp, ratings_logic.hpp; option season_ratings):
//      every AI driver of the year at the season's first apply and a mid-season driver when he
//      comes in; the player only at the career's creation.
// In a Real life R&D career every load and Team HQ also works out the in-season development of the
// year at the season's current round (development_logic.hpp) for progressive_rnd.dll
// (ProgressiveDevelopmentProgress), while the option season_development is on (the default), and
// every team's R&D model performance (Team HQ's vehicle performance comparison, the game's team
// rankings) follows the year's real order, while season_rnd_models is on (rnd_model_logic.hpp).
// Mid-season (midseason_logic.hpp): the year's real line-up changes are placed on the career's own
// calendar (Season.weekends' tracks, the next race), and the plan is made from the line-up at the
// next race, so each change is written at the first idle Team HQ before its career race and a
// stand-in's regular driver comes back at the next one. The player is never swapped out; in follow
// mode a move of his driver moves him (the same move and contract signing as a season's first apply).
// The changes in force are recorded in the career's store entry (mid_year, mid_round, mid_applied).
// Every career load and Team HQ also records the player's team and the season index in the store
// entry, for the year on the career's Resume Career card (ProgressiveCardYear,
// progressive_resume.dll).
// The cars and looks are written while the option season_visuals is on (the default; experiment E1
// showed a Team HQ rewrite of vehicle_ref and look in the next session).
// ten_team_years (hidden, default on): a year with no team for career slot 11 (2017-2025) closes
// the slot (season_logic.hpp): its drivers are released, so no row of its team has a car. A weekend
// takes its entrants from the career rows with a car when it is (re)built (0x140af7680 ->
// Weekend::start 0x140af0460, at a load and on entering a session not yet past session 1), so it
// races 20 cars; the constructors' standings, the Team Select list and the AI market list the
// unique team keys of their rows and skip a teamless one, so they leave the slot out. A weekend
// built before the apply is rebuilt by the game's Weekend::start when its first session's
// entrants differ from the career rows with a car (rebuild_entrants, weekend_logic.hpp).
// A plan that moves or releases drivers waits until the current Season has its weekends and
// current_weekend is one of them: MessageDriverMovesFinalised (and the career load) ends a
// teammate rivalry whose rival left the player's team, and the eTeammate reward (0x1412590b0)
// reads Season.weekends[current_weekend] unchecked. After Finish Season the next season's first
// Team HQ has no weekends yet, so the apply runs at a later Team HQ.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <swofty/career.hpp>
#include <swofty/career_content.hpp>
#include <swofty/hub.hpp>
#include <swofty/hud.hpp>
#include <swofty/lang.hpp>
#include <swofty/events.hpp>
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>
#include <swofty/storage.hpp>

#include "development_logic.hpp"
#include "rnd_model_logic.hpp"
#include "midseason_logic.hpp"
#include "names.hpp"
#include "player_logic.hpp"
#include "titles_logic.hpp"
#include "progressive.hpp"
#include "ratings.hpp"
#include "resume_logic.hpp"
#include "season_logic.hpp"
#include "seats.hpp"
#include "seats_logic.hpp"
#include "store.hpp"
#include "weekend_logic.hpp"
#include "store_logic.hpp"

SWOFTY_SETTING(std::string_view, start_season, "2026");
SWOFTY_SETTING(std::string_view, rnd_mode, "real_life");
SWOFTY_SETTING(std::string_view, team_moves, "follow_driver");
SWOFTY_SETTING(bool, season_visuals, true);
SWOFTY_SETTING(bool, season_development, true);
SWOFTY_SETTING(bool, season_rnd_models, true);
SWOFTY_SETTING(bool, test_next_season, false);
SWOFTY_SETTING(bool, ten_team_years, true);

namespace pg {

namespace {

using namespace progressive;
using names::Kind;
using namespace swofty::game;

constexpr const char *kGroup = "f1_career_progressive";
constexpr const char *kYearsPath = "data/years.json";

season::Data g_data;
bool g_data_ok = false;
int g_failures = 0;
std::atomic<bool> g_rnd_changed{false};
std::string g_names_for;
int g_names_year = 0;

bool data()
{
    if (g_data_ok)
        return true;
    std::string text, err;
    if (swofty::storage::read(kYearsPath, text) != swofty::storage::Result::ok) {
        swofty::log("[progressive] season: %s is missing from swofty/addons/swofty.progressive/", kYearsPath);
        return false;
    }
    g_data_ok = season::parse(text, g_data, err);
    swofty::log("[progressive] season: %s %s (%zu years, %zu pool drivers)%s%s", kYearsPath,
                g_data_ok ? "loaded" : "REFUSED", g_data.years.size(), g_data.pool.size(), err.empty() ? "" : ": ",
                err.c_str());
    return g_data_ok;
}

uint32_t career_seed()
{
    auto *w = static_cast<const WeatherAndTimeOfDayComponent *>(
        swofty::career::component_by_desc(k_Career_WeatherAndTimeOfDayComponentDesc.get()));
    WeatherAndTimeOfDayComponent c{};
    return w && swofty::mem::read(w, c) ? c.random_seed : 0;
}

int setting_year()
{
    std::string v(start_season.get());
    int y = std::atoi(v.c_str());
    return season::clamp_year(y ? y : 2026);
}

std::string label_or(Kind k, uint32_t stat)
{
    std::string l = names::label(k, stat);
    return l.empty() ? std::to_string(stat) : l + "(" + std::to_string(stat) + ")";
}

season::Resolve resolver();

std::atomic<bool> g_new_career{false};

std::string years_text(const std::vector<int> &ys);

// The year the career's seats show (seats_logic.hpp): the year of most of the seated cars, else the
// years whose line-up at some real round holds the most seated drivers (at least 12); empty
// otherwise.

std::vector<int> seats_years(const std::vector<Row> &rows, int &count)
{
    season::Resolve r = resolver();
    static std::map<int, int> cars;
    static bool cars_ok = false;
    if (!cars_ok && g_data_ok) {
        cars = progressive::seats::car_years(g_data, r);
        cars_ok = true;
    }
    std::vector<progressive::seats::Seated> seated;
    for (auto &row : rows)
        if (has_car(row.rec) && !teamless(row.rec))
            seated.push_back({season::slot_of_team(g_data, static_cast<int>(stat_of(row.rec.team_stat_id))),
                              stat_of(driver_of(row.rec)), static_cast<int>(stat_of(row.rec.vehicle_ref))});
    progressive::seats::Years y = progressive::seats::seats_years(g_data, r, seated, cars);
    count = y.count;
    static std::string last;
    std::string now = years_text(y.years) + " by " + y.how + " (" + std::to_string(y.count) + ")";
    if (now != last) {
        last = now;
        swofty::log("[progressive] seats: the career's seats show %s", now.c_str());
    }
    return y.years;
}

std::string years_text(const std::vector<int> &ys)
{
    std::string out;
    for (int y : ys)
        out += (out.empty() ? "" : "/") + std::to_string(y);
    return out.empty() ? "no real year" : out;
}

// The entry of the loaded career. A new career from the Progressive wizard gets a new entry; a
// loaded one keeps its entry: the one whose seed matches, else the one whose year matches the
// year its seats show, else (seats still on the stock grid) the driver's last used entry. Only a
// driver with no entry gets a new one at a load, its start taken from the seats when they show a
// past year (start = that year - season index), else from the start season option.
store::Career *career_entry(const swofty::career::State &state, uint32_t season_index, uint32_t driver,
                            const std::vector<Row> &rows)
{
    if (!store::load() || !data())
        return nullptr;
    uint32_t seed = career_seed();
    bool fresh = g_new_career.exchange(false);
    int count = 0;
    std::vector<int> seen = seats_years(rows, count);
    int past = progressive::match::past_year(seen);
    progressive::match::Pick m =
        progressive::match::pick(store::all(), seed, driver, season_index, seen, fresh, setting_year());
    store::Career *pick = m.index >= 0 ? &store::all()[static_cast<size_t>(m.index)] : nullptr;
    std::string how = m.how;
    if (pick) {
        if (pick->seed != seed || how != "seed")
            swofty::log("[progressive] store: career %s (driver %u, season %u, seats %s, %d seats matched) is %s by "
                        "%s; seed %08x -> %08x",
                        state.key[0] ? state.key : "-", driver, season_index, years_text(seen).c_str(), count,
                        pick->key.c_str(), how.c_str(), pick->seed, seed);
        pick->seed = seed;
        pick->used = store::next_use();
        store::save();
        return pick;
    }
    store::Career c;
    c.key = store::free_key(seed, driver);
    c.driver = driver;
    c.seed = seed;
    c.start = m.new_start;
    c.rnd = std::string(rnd_mode.get()) == "real_life" ? "real_life" : "hybrid";
    c.moves = std::string(team_moves.get());
    c.created_season = static_cast<int>(season_index);
    c.used = store::next_use();
    store::Career &added = store::add(c);
    store::save();
    swofty::log("[progressive] store: new entry %s for career %s (%s) at season %u: start %d (%s), R&D %s, team moves %s; "
                "seats %s",
                c.key.c_str(), state.key[0] ? state.key : "-", how.c_str(),
                season_index, c.start, past ? "from the seats, which show a past year" : "the start season option",
                c.rnd.c_str(), c.moves.c_str(), years_text(seen).c_str());
    return &added;
}

season::Resolve resolver()
{
    season::Resolve r;
    r.driver_stat = [](const std::string &label) {
        uint32_t s = 0;
        if (names::stat(Kind::driver, label, s))
            return static_cast<int>(s);
        const season::PoolDriver *p = g_data.driver(label);
        return p ? p->stat : -1;
    };
    r.vehicle_stat = [](const std::string &label) {
        uint32_t s = 0;
        return names::stat(Kind::vehicle, label, s) ? static_cast<int>(s) : -1;
    };
    r.look_known = [](int s) { return s >= 0 && names::known(Kind::look, static_cast<uint32_t>(s)); };
    r.rating = [](uint32_t stat) -> double {
        void *c = swofty::career::component_by_desc(k_Career_DriverCareerPlayerDriverStatisticsComponentDesc.get());
        if (!c)
            c = swofty::career::component_by_desc(k_Career_DriverStatisticsComponentDesc.get());
        if (!c || !Career_Get_DriverOverallRating.address())
            return -1;
        uint32_t key = stat << 16;
        float v = Career_Get_DriverOverallRating(c, static_cast<uint8_t *>(c) + offsetof(DriverStatisticsComponent, stats_data),
                                                 &key, true);
        return std::isfinite(v) && v > 0 ? v : -1;
    };
    return r;
}

std::vector<season::CareerRow> view(const std::vector<Row> &rows)
{
    std::vector<season::CareerRow> out;
    for (auto &r : rows) {
        season::CareerRow c;
        c.driver = stat_of(driver_of(r.rec));
        c.team = teamless(r.rec) ? -1 : static_cast<int>(stat_of(r.rec.team_stat_id));
        c.position = position_of(r.rec);
        c.car = has_car(r.rec);
        c.human = human(r.rec);
        c.number = r.rec.car_number;
        out.push_back(c);
    }
    return out;
}

int row_index(const std::vector<Row> &rows, uint32_t stat)
{
    int hit = -1;
    for (size_t i = 0; i < rows.size(); ++i)
        if (stat_of(driver_of(rows[i].rec)) == stat && (driver_of(rows[i].rec) & 0xff) == 0) {
            if (hit >= 0)
                return -2;
            hit = static_cast<int>(i);
        }
    return hit;
}

constexpr const char *kTitlesPath = "data/titles.json";
json::Value g_titles;
int g_titles_state = 0;

bool titles()
{
    if (g_titles_state)
        return g_titles_state > 0;
    std::string text;
    bool ok = swofty::storage::read(kTitlesPath, text) == swofty::storage::Result::ok && json::parse(text, g_titles);
    g_titles_state = ok ? 1 : -1;
    swofty::log("[progressive] event titles: %s %s (%zu title(s))", kTitlesPath, ok ? "loaded" : "missing or not JSON",
                ok ? g_titles["all"].o.size() : size_t(0));
    return ok;
}

// The race weekend and season titles show the career's year (titles_logic.hpp); 2026 and later
// keep the stock titles.
void set_titles(int year, const char *why)
{
    if (!titles())
        return;
    size_t set = 0;
    if (year >= season::kLastYear) {
        for (auto &key : progressive::titles::keys(g_titles))
            swofty::lang::clear(key);
    } else {
        for (auto &t : progressive::titles::for_year(g_titles, year))
            if (swofty::lang::set(t.key, t.text, t.language) == swofty::lang::Result::ok)
                ++set;
    }
    swofty::log("[progressive] event titles %d (%s): %zu override(s)%s", year, why, set,
                year >= season::kLastYear ? ", the stock 2026 titles" : "");
}

void clear_titles()
{
    if (g_titles_state <= 0)
        return;
    for (auto &key : progressive::titles::keys(g_titles))
        swofty::lang::clear(key);
}

void set_names(int year, const char *why)
{
    if (!swofty::lang::available() || !data())
        return;
    const season::Year *y = g_data.year(year);
    size_t set = 0;
    for (int i = 0; i < season::kSlots; ++i)
        for (auto &key : g_data.slots[i].name_keys) {
            if (key.empty())
                continue;
            if (year >= season::kLastYear || !y || y->teams[i].name.empty())
                swofty::lang::clear(key);
            else if (swofty::lang::set(key, y->teams[i].name) == swofty::lang::Result::ok)
                ++set;
        }
    set_titles(year, why);
    swofty::lang::refresh();
    g_names_year = year;
    swofty::log("[progressive] team names %d (%s): %zu override(s)%s", year, why, set,
                year >= season::kLastYear ? ", the 2026 names" : "");
}

void clear_names()
{
    if (!g_names_year || !swofty::lang::available())
        return;
    for (auto &s : g_data.slots)
        for (auto &key : s.name_keys)
            if (!key.empty())
                swofty::lang::clear(key);
    clear_titles();
    swofty::lang::refresh();
    g_names_year = 0;
}

struct Write {
    int row = -1;
    EntrantWriteData data{};
    uint16_t position = 0;
    uint8_t number = kNoNumber;
    bool move = false, release = false;
    uint32_t old_team = kNoKey, new_team = kNoKey;
    std::string why;
};

EntrantRecord expected(const Row &r, const Write &w)
{
    EntrantRecord x = r.rec;
    x.driver_customisation = w.data.driver_customisation;
    x.team_stat_id = w.data.team_stat_id;
    x.vehicle_ref = w.data.vehicle_ref;
    x.driver_reg_value = w.data.driver_reg_value;
    x.vehicle_customisation = w.data.vehicle_customisation;
    x.badge_stat_id = w.data.badge_stat_id;
    x.const_ffff0012 = w.data.seat_value;
    x.team_position = w.position;
    x.car_number = w.number;
    return x;
}

// The seat a move writes: the row holding it (its team, car, registry value, livery, badge, seat
// value and position), or for an empty seat its teammate's row, else a row of the slot's team,
// with the slot's car when that row has none and the seat's position.
bool seat_template(const season::Plan &p, const std::vector<Row> &rows, const Row &driver, const season::Seat &to,
                   EntrantWriteData &data, uint16_t &position, std::string &why)
{
    int h = p.holder[to.slot][to.index];
    if (h >= 0) {
        data = seat_data(driver, rows[h]);
        position = rows[h].rec.team_position;
        return true;
    }
    const Row *tpl = nullptr;
    int mate = p.holder[to.slot][1 - to.index];
    if (mate >= 0)
        tpl = &rows[mate];
    uint32_t team = static_cast<uint32_t>(g_data.slots[to.slot].team_stat) << 16;
    for (size_t i = 0; !tpl && i < rows.size(); ++i)
        if (!teamless(rows[i].rec) && stat_of(rows[i].rec.team_stat_id) == stat_of(team))
            tpl = &rows[i];
    data = tpl ? seat_data(driver, *tpl) : released_data(driver);
    if (!tpl)
        data.team_stat_id = team;
    if (!tpl || !has_car(tpl->rec)) {
        if (p.vehicles[to.slot].stat < 0) {
            why = "no row of its team has a car and no vehicle row resolves for the slot";
            return false;
        }
        data.vehicle_ref = static_cast<uint32_t>(p.vehicles[to.slot].stat) << 16;
    }
    position = static_cast<uint16_t>(0x100 | to.index);
    return true;
}

std::string seat_name(const season::Seat &s)
{
    if (s.slot < 0)
        return "free agent";
    return g_data.slots[s.slot].team + " seat " + std::to_string(s.index + 1);
}

// The career's grid as weekends take it: rows with a car (the weekend's entrant filter), the teams
// they race for and slot 11's rows.
void log_grid(uintptr_t e, const season::Plan &p, const char *when)
{
    std::vector<Row> rows;
    if (!entrant_rows(e, rows))
        return;
    size_t cars = 0, slot11 = 0;
    std::vector<uint32_t> teams;
    for (auto &r : rows) {
        if (!has_car(r.rec))
            continue;
        ++cars;
        uint32_t t = stat_of(r.rec.team_stat_id);
        if (std::find(teams.begin(), teams.end(), t) == teams.end())
            teams.push_back(t);
        slot11 += static_cast<int>(t) == g_data.slots[season::kSlots - 1].team_stat;
    }
    swofty::log("[progressive] grid %d (%s): %zu career row(s) with a car (the weekend entrants), %zu team(s), %zu on "
                "%s; ten_team_years %s, %d closed slot(s)",
                p.year, when, cars, teams.size(), slot11, g_data.slots[season::kSlots - 1].team.c_str(),
                ten_team_years ? "on" : "off", season::closed_slots(p));
}

int apply(uintptr_t e, const season::Plan &p, const std::vector<Row> &rows, bool visuals, const std::string &why)
{
    uintptr_t manager = contracts_manager();
    if (!manager) {
        swofty::log("[progressive] season %d REFUSED: no DriverCareerContractsManagerComponent", p.year);
        return -1;
    }
    std::map<int, Write> writes;
    auto write_for = [&](int idx) -> Write & {
        auto it = writes.find(idx);
        if (it != writes.end())
            return it->second;
        Write w;
        w.row = idx;
        w.data = own_data(rows[idx]);
        w.position = rows[idx].rec.team_position;
        w.number = rows[idx].rec.car_number;
        return writes.emplace(idx, w).first->second;
    };
    std::vector<uintptr_t> objects;
    if (!contract_objects(manager, objects)) {
        swofty::log("[progressive] season %d REFUSED: the contract list is unreadable", p.year);
        return -1;
    }
    for (auto &m : p.moves) {
        int idx = row_index(rows, m.driver);
        if (idx < 0) {
            swofty::log("[progressive] season %d REFUSED: driver %s has no single row", p.year,
                        label_or(Kind::driver, m.driver).c_str());
            return -1;
        }
        EntrantWriteData data{};
        uint16_t position = 0;
        std::string bad;
        if (!seat_template(p, rows, rows[idx], m.to, data, position, bad)) {
            swofty::log("[progressive] season %d REFUSED: %s is empty and cannot be written: %s", p.year,
                        seat_name(m.to).c_str(), bad.c_str());
            return -1;
        }
        Write &w = write_for(idx);
        w.data = data;
        w.position = position;
        w.move = true;
        w.old_team = rows[idx].rec.team_stat_id;
        w.new_team = data.team_stat_id;
        w.why = seat_name(m.from) + " -> " + seat_name(m.to) +
                (p.holder[m.to.slot][m.to.index] < 0 ? " (an empty seat)" : "");
    }
    for (auto &m : p.repositions) {
        int idx = row_index(rows, m.driver);
        if (idx < 0)
            continue;
        Write &w = write_for(idx);
        w.position = static_cast<uint16_t>(0x100 | m.to.index);
        w.why = "race seat position " + std::to_string(m.from.index) + " -> " + seat_name(m.to);
    }
    for (uint32_t d : p.releases) {
        int idx = row_index(rows, d);
        if (idx < 0)
            continue;
        Write &w = write_for(idx);
        w.data = released_data(rows[idx]);
        w.position = 0;
        w.release = true;
        w.old_team = rows[idx].rec.team_stat_id;
        w.why = "released";
    }
    for (auto &n : p.numbers) {
        int idx = row_index(rows, n.driver);
        if (idx >= 0 && !human(rows[idx].rec))
            write_for(idx).number = n.number;
    }
    if (visuals) {
        for (int i = 0; i < season::kSlots; ++i)
            for (int k = 0; k < lineup::kSeats; ++k) {
                int idx = row_index(rows, p.target[i][k]);
                const season::VehicleSet &v = p.vehicles[i];
                if (idx < 0 || v.stat < 0)
                    continue;
                Write &w = write_for(idx);
                w.data.vehicle_ref = (static_cast<uint32_t>(v.stat) << 16) | (w.data.vehicle_ref & 0xffffu);
            }
        for (auto &l : p.looks) {
            int idx = row_index(rows, l.driver);
            if (idx >= 0 && l.stat > 0)
                write_for(idx).data.driver_customisation = static_cast<uint32_t>(l.stat) << 16;
        }
    }
    std::vector<int> order;
    for (auto &[idx, w] : writes) {
        EntrantRecord x = expected(rows[idx], w);
        if (std::memcmp(&rows[idx].rec, &x, sizeof x) != 0)
            order.push_back(idx);
    }
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        auto rank = [&](int i) { return writes[i].move ? 0 : writes[i].release ? 1 : 2; };
        return rank(a) < rank(b);
    });
    int plan_team = -1;
    for (int i = 0; i < season::kSlots; ++i)
        for (int k = 0; k < lineup::kSeats; ++k)
            if (p.player != lineup::kNone && p.target[i][k] == p.player)
                plan_team = g_data.slots[i].team_stat;
    auto sign_seats = [&]() {
        std::vector<Row> now;
        std::vector<uintptr_t> live;
        size_t need = 0, done = 0;
        if (!entrant_rows(e, now) || !contract_objects(manager, live)) {
            swofty::log("[progressive] season %d WARNING contracts: the line-up or contract list is unreadable", p.year);
            return;
        }
        for (auto &r : now) {
            if (!has_car(r.rec) || teamless(r.rec))
                continue;
            uint32_t d = driver_of(r.rec);
            uintptr_t own = contract_of(live, d);
            if (contract_fits(own, r.rec.team_stat_id))
                continue;
            if (human(r.rec)) {
                int at = static_cast<int>(&r - now.data());
                bool moved_player = writes.count(at) && writes[at].move;
                DriverContract k{};
                bool stale = own && swofty::mem::read(own, k) && k.active;
                if (!moved_player && !stale)
                    continue;
                if (plan_team < 0 || static_cast<int>(stat_of(r.rec.team_stat_id)) != plan_team) {
                    swofty::log("[progressive] season %d player contract left alone: %s sits at %s, which is not the "
                                "seat the plan gives him (%s)",
                                p.year, label_or(Kind::driver, stat_of(d)).c_str(),
                                label_or(Kind::team, stat_of(r.rec.team_stat_id)).c_str(),
                                plan_team < 0 ? "no seat" : label_or(Kind::team, static_cast<uint32_t>(plan_team)).c_str());
                    continue;
                }
                swofty::log("[progressive] season %d player contract: %s sits at %s, his contract names %s (%s); signing "
                            "his seat's team",
                            p.year, label_or(Kind::driver, stat_of(d)).c_str(),
                            label_or(Kind::team, stat_of(r.rec.team_stat_id)).c_str(),
                            own ? label_or(Kind::team, stat_of(k.team)).c_str() : "no team",
                            moved_player ? "moved in this apply" : "a stale contract from before a move");
            }
            ++need;
            Signing s;
            if (const char *why = plan_signing(manager, live, d, r.rec.team_stat_id, s))
                swofty::log("[progressive] season %d WARNING %s has no contract with %s: %s", p.year,
                            label_or(Kind::driver, stat_of(d)).c_str(), label_or(Kind::team, stat_of(r.rec.team_stat_id)).c_str(),
                            why);
            else if (!sign_contract(e, manager, s))
                swofty::log("[progressive] season %d WARNING %s's contract with %s was not signed", p.year,
                            label_or(Kind::driver, stat_of(d)).c_str(), label_or(Kind::team, stat_of(r.rec.team_stat_id)).c_str());
            else
                ++done;
        }
        if (need)
            swofty::log("[progressive] season %d contracts: %zu/%zu seated driver(s) without a contract for their team "
                        "signed (terms from Career_Build_DriverContract, one season)",
                        p.year, done, need);
    };
    if (order.empty()) {
        sign_seats();
        return 0;
    }
    swofty::log("[progressive] season %d: applying (%s)", p.year, why.c_str());
    for (auto &n : p.notes)
        swofty::log("[progressive]   note: %s", n.c_str());
    for (int i = 0; i < season::kSlots; ++i)
        if (p.closed[i])
            swofty::log("[progressive]   %s: does not race in %d (ten_team_years: no team in this year); its drivers "
                        "are released",
                        g_data.slots[i].team.c_str(), p.year);
        else
            swofty::log("[progressive]   %s: %s + %s, car %s", g_data.slots[i].team.c_str(),
                        label_or(Kind::driver, p.target[i][0]).c_str(), label_or(Kind::driver, p.target[i][1]).c_str(),
                        p.vehicles[i].stat < 0 ? "unchanged" : p.vehicles[i].label.c_str());
    swofty::log("[progressive] season %d plan: %zu move(s), %zu release(s), %zu number change(s), cars and looks %s; "
                "%zu row(s) to write",
                p.year, p.moves.size(), p.releases.size(), p.numbers.size(), visuals ? "on" : "off (season_visuals)",
                order.size());
    for (int idx : order) {
        const Write &w = writes[idx];
        if (human(rows[idx].rec) && w.move)
            swofty::log("[progressive] season %d player move: %s %s -> %s; the save header (info_team) and the Team HQ "
                        "props read his seat, so they show %s from the game's next save and the next Team HQ load",
                        p.year, label_or(Kind::driver, stat_of(driver_of(rows[idx].rec))).c_str(),
                        label_or(Kind::team, stat_of(w.old_team)).c_str(), label_or(Kind::team, stat_of(w.new_team)).c_str(),
                        label_or(Kind::team, stat_of(w.new_team)).c_str());
        swofty::log("[progressive]   %s %s: %s", w.move ? "move" : w.release ? "release" : "update",
                    label_or(Kind::driver, stat_of(driver_of(rows[idx].rec))).c_str(),
                    w.why.empty() ? "number, car or look" : w.why.c_str());
    }

    Baseline baseline(rows);
    std::vector<int> written;
    auto back = [&]() {
        size_t ok = 0;
        for (auto it = written.rbegin(); it != written.rend(); ++it) {
            std::vector<Row> now;
            const Row *r = entrant_rows(e, now) && now.size() > static_cast<size_t>(*it) ? &now[*it] : nullptr;
            if (r && write_row(e, *r, own_data(rows[*it]), rows[*it].rec.team_position, rows[*it].rec.car_number))
                ++ok;
        }
        swofty::log("[progressive] season %d: wrote back %zu/%zu row(s)%s", p.year, ok, written.size(),
                    ok == written.size() ? "" : "; RELOAD THE SAVE without saving");
    };
    for (int idx : order) {
        const Write &w = writes[idx];
        std::vector<Row> now;
        if (!entrant_rows(e, now) || now.size() != rows.size() || now[idx].at != rows[idx].at) {
            swofty::log("[progressive] season %d FAILED: the line-up changed during the batch", p.year);
            back();
            return -1;
        }
        bool found = false;
        if (w.release && !release_contract(e, manager, driver_of(rows[idx].rec), found))
            swofty::log("[progressive] season %d WARNING %s: contract not released", p.year,
                        label_or(Kind::driver, stat_of(driver_of(rows[idx].rec))).c_str());
        if ((w.move || w.release) &&
            !announce(manager, driver_of(rows[idx].rec), w.old_team, w.release ? kNoKey : w.new_team,
                      w.release ? kReasonReleased : kReasonSigned))
            swofty::log("[progressive] season %d WARNING %s: MessageDriverMoveConfirmed not posted", p.year,
                        label_or(Kind::driver, stat_of(driver_of(rows[idx].rec))).c_str());
        if (!write_row(e, now[idx], w.data, w.position, w.number)) {
            swofty::log("[progressive] season %d FAILED writing %s", p.year,
                        label_or(Kind::driver, stat_of(driver_of(rows[idx].rec))).c_str());
            back();
            return -1;
        }
        written.push_back(idx);
    }
    std::vector<Row> after;
    bool ok = entrant_rows(e, after) && after.size() == rows.size();
    for (size_t i = 0; ok && i < rows.size(); ++i) {
        auto it = writes.find(static_cast<int>(i));
        bool mine = it != writes.end() && std::find(written.begin(), written.end(), static_cast<int>(i)) != written.end();
        const EntrantRecord &got = after[i].rec;
        if (!mine) {
            if (std::memcmp(&got, &rows[i].rec, sizeof got) != 0) {
                swofty::log("[progressive] season %d: %s changed though it was not written", p.year,
                            label_or(Kind::driver, stat_of(driver_of(rows[i].rec))).c_str());
                ok = false;
            }
            continue;
        }
        EntrantRecord want = expected(rows[i], it->second);
        if (got.team_stat_id != want.team_stat_id || got.vehicle_ref != want.vehicle_ref ||
            got.driver_customisation != want.driver_customisation || got.car_number != want.car_number ||
            driver_of(got) != driver_of(want)) {
            swofty::log("[progressive] season %d: %s did not read back as planned (%s)", p.year,
                        label_or(Kind::driver, stat_of(driver_of(rows[i].rec))).c_str(), row_text(got).c_str());
            ok = false;
        }
    }
    if (!ok) {
        back();
        return -1;
    }
    sign_seats();
    size_t numbered = 0;
    if (uintptr_t cn = car_numbers(e, manager)) {
        std::vector<Row> now;
        if (entrant_rows(e, now))
            for (auto &r : now)
                if (has_car(r.rec) && r.rec.car_number == kNoNumber && !entity_busy(e) && unique_identity(e, r)) {
                    Career_DriverNumbers_CheckExisting(reinterpret_cast<void *>(cn), driver_of(r.rec));
                    ++numbered;
                }
    }
    uint64_t changes = 0;
    bool fin = true;
    if (!p.moves.empty() || !p.releases.empty())
        fin = finalise(e, baseline, changes);
    swofty::log("[progressive] season %d applied: %zu row(s) written, %zu racing number(s) drawn, finalise %s (%llu "
                "line-up change(s))",
                p.year, written.size(), numbered,
                p.moves.empty() && p.releases.empty() ? "not needed" : fin ? "posted" : "FAILED",
                static_cast<unsigned long long>(changes));
    log_grid(e, p, "after the season apply");
    return static_cast<int>(written.size());
}

std::string g_standings_pending;
std::string g_refreshed_for;

// The driver and constructor standings rows come from the line-up when the game initialises a
// season's standings (Career_Rebuild_SeasonStandings 0x140ae8040, from the season start and
// Career_Rebuild_ChampionshipStandings), so drivers the apply seated have no rows until the next
// init. The championship-wide rebuild clears the four standings of every season and re-inits each
// one only when its first weekend has sessions; before that it would leave them empty. So it runs
// once the current season's first weekend has sessions: right after the apply, or at a later Team
// HQ (the first one after round 1 starts), and the re-init re-applies every session result.
// Mirrors the career rows into every weekend of the current season that has not started (its own
// line-up copy and each session's participants), so the round 1 sessions the career already built
// and the standings initialised from them show the season's seats.
// The game's Weekend::start entrant list: {allocator, begin, end, capacity} of EntrantRecord
// pointers, only read by the callee (0x140af7680 builds it from the career rows with a car).
struct EntrantPointers {
    void *allocator = nullptr;
    const EntrantRecord *const *begin = nullptr;
    const EntrantRecord *const *end = nullptr;
    size_t capacity = 0;
};

std::vector<uint32_t> participant_drivers(uintptr_t session)
{
    std::vector<uint32_t> out;
    Session s{};
    if (!swofty::mem::read(session, s))
        return out;
    auto b = reinterpret_cast<uintptr_t>(s.participants_begin), en = reinterpret_cast<uintptr_t>(s.participants_end);
    if (!b || en < b || (en - b) % sizeof(EntrantRecord) || (en - b) / sizeof(EntrantRecord) > kMaxEntrants)
        return out;
    for (uintptr_t p = b; p < en; p += sizeof(EntrantRecord)) {
        EntrantRecord r{};
        if (swofty::mem::read(p, r))
            out.push_back(driver_of(r));
    }
    return out;
}

// A not-started weekend whose first session's entrants are not the career rows with a car (the
// filter of the game's builder 0x140af7680) gets them again from the game: Weekend::start with
// begin false refills the session through its own pushes and rebuilds the weekend's entrant
// pointer list, the way the game builds a weekend (weekend_logic.hpp). Returns true when rebuilt.
bool rebuild_entrants(uintptr_t e, uintptr_t w, int year, const char *tag)
{
    Weekend wk{};
    if (!swofty::mem::read(w, wk))
        return false;
    auto sb = reinterpret_cast<uintptr_t>(wk.sessions_begin), se = reinterpret_cast<uintptr_t>(wk.sessions_end);
    weekend::State st;
    st.progress = wk.progress_state;
    st.current = wk.current_session;
    st.sessions = sb && se > sb && (se - sb) % sizeof(Session) == 0 ? (se - sb) / sizeof(Session) : 0;
    Session first{};
    if (st.sessions && swofty::mem::read(sb, first))
        st.first_completion = first.completion;
    std::vector<uint32_t> copy = st.sessions ? participant_drivers(sb) : std::vector<uint32_t>{};
    st.first_rows = copy.size();
    if (weekend::not_rebuildable(st))
        return false;
    std::vector<Row> rows;
    if (!entrant_rows(e, rows))
        return false;
    std::vector<uint32_t> career;
    std::vector<const EntrantRecord *> list;
    for (auto &r : rows)
        if (has_car(r.rec)) {
            career.push_back(driver_of(r.rec));
            list.push_back(reinterpret_cast<const EntrantRecord *>(r.at));
        }
    if (weekend::same_entrants(copy, career))
        return false;
    CareerEntity ent{};
    if (!swofty::mem::read(e, ent))
        return false;
    auto cb = reinterpret_cast<uintptr_t>(ent.championships_begin), ce = reinterpret_cast<uintptr_t>(ent.championships_end);
    if (ce > cb && ce - cb >= 0x20) {
        swofty::log("[progressive] season %d %s: weekend entrants differ from the career (%zu -> %zu rows) but the career "
                    "has more than one championship; left to the game",
                    year, tag, copy.size(), career.size());
        return false;
    }
    if (!Sess_Weekend_Start.address() || entity_busy(e)) {
        swofty::log("[progressive] season %d %s: weekend entrants differ from the career (%zu -> %zu rows); %s", year, tag,
                    copy.size(), career.size(),
                    Sess_Weekend_Start.address() ? "the line-up is mid-update, left for the next Team HQ"
                                                 : "Sess_Weekend_Start is not bound, left as they are");
        return false;
    }
    EntrantPointers ptrs;
    ptrs.begin = list.data();
    ptrs.end = list.data() + list.size();
    ptrs.capacity = list.size();
    Sess_Weekend_Start(reinterpret_cast<void *>(w), &ptrs, false);
    std::vector<uint32_t> now = participant_drivers(sb);
    Weekend after{};
    bool still = swofty::mem::read(w, after) && after.progress_state == 0 && after.current_session == 0;
    swofty::log("[progressive] season %d %s: weekend entrants rebuilt by the game: %zu -> %zu rows%s", year, tag,
                copy.size(), now.size(),
                !weekend::same_entrants(now, career) ? " (WARNING: they still differ from the career rows with a car)"
                : !still                             ? " (WARNING: the weekend no longer reads as not started)"
                                                     : "");
    return true;
}

void refresh_weekends(uintptr_t e, int year)
{
    auto ptr = [](uintptr_t a) {
        uintptr_t v = 0;
        return swofty::mem::read(a, v) ? v : uintptr_t{0};
    };
    uintptr_t champ = championship(e);
    Championship ch{};
    std::vector<Row> rows;
    if (!champ || !swofty::mem::read(champ, ch) || !entrant_rows(e, rows))
        return;
    auto b = reinterpret_cast<uintptr_t>(ch.seasons_begin), en = reinterpret_cast<uintptr_t>(ch.seasons_end);
    size_t n = en > b ? (en - b) / sizeof(ChampionshipSeasonEntry) : 0;
    ChampionshipSeasonEntry entry{};
    Season season{};
    if (ch.season_index >= n || !swofty::mem::read(b + ch.season_index * sizeof entry, entry) || !entry.season ||
        !swofty::mem::read(entry.season, season))
        return;
    auto wb = reinterpret_cast<uintptr_t>(season.weekends_begin), we = reinterpret_cast<uintptr_t>(season.weekends_end);
    size_t weekends = 0, copies = 0, refused = 0, rebuilt = 0;
    for (uintptr_t at = wb; at && at < we && weekends < 64; at += sizeof(Weekend *)) {
        uintptr_t w = 0;
        Weekend wk{};
        if (!swofty::mem::read(at, w) || !w || !swofty::mem::read(w, wk) || wk.progress_state != 0)
            continue;
        ++weekends;
        char tag[64];
        std::snprintf(tag, sizeof tag, "round %zu", static_cast<size_t>((at - wb) / sizeof(Weekend *)) + 1);
        Mirror m;
        constexpr uintptr_t kWeekendRows = 0x20;
        if (mirror_rows(w + kWeekendRows, rows, m)) {
            ++copies;
            if (m.changed || m.replaced)
                swofty::log("[progressive] season %d %s line-up copy: %zu rows, %zu matched, %zu rewritten, %zu replaced; "
                            "race seats %zu -> %zu",
                            year, tag, m.rows, m.matched, m.changed, m.replaced, m.race_before, m.race_after);
        } else if (ptr(w + kWeekendRows)) {
            ++refused;
        }
        auto sb = reinterpret_cast<uintptr_t>(wk.sessions_begin), se = reinterpret_cast<uintptr_t>(wk.sessions_end);
        for (uintptr_t s = sb; s && s < se && (s - sb) / sizeof(Session) < 16; s += sizeof(Session)) {
            if (!mirror_rows(s + offsetof(Session, participants_begin), rows, m)) {
                if (ptr(s + offsetof(Session, participants_begin)))
                    ++refused;
                continue;
            }
            ++copies;
            swofty::log("[progressive] season %d %s session %zu participants: %zu rows, %zu matched, %zu rewritten, %zu "
                        "replaced; race seats %zu -> %zu",
                        year, tag, static_cast<size_t>((s - sb) / sizeof(Session)) + 1, m.rows, m.matched, m.changed,
                        m.replaced, m.race_before, m.race_after);
        }
        rebuilt += rebuild_entrants(e, w, year, tag);
    }
    swofty::log("[progressive] season %d: %zu weekend(s) not started, %zu line-up copies refreshed, %zu rebuilt by the "
                "game%s",
                year, weekends, copies, rebuilt,
                refused ? " (some vectors did not look like line-up copies and were left)" : "");
}

bool standings_rebuild(uintptr_t e, const char *when)
{
    uintptr_t champ = championship(e);
    Championship ch{};
    if (!champ || !swofty::mem::read(champ, ch) || !Career_Rebuild_ChampionshipStandings.address())
        return false;
    auto b = reinterpret_cast<uintptr_t>(ch.seasons_begin), en = reinterpret_cast<uintptr_t>(ch.seasons_end);
    size_t n = en > b ? (en - b) / sizeof(ChampionshipSeasonEntry) : 0;
    if (ch.season_index >= n)
        return false;
    ChampionshipSeasonEntry entry{};
    Season season{};
    Weekend first{};
    uintptr_t weekend = 0;
    if (!swofty::mem::read(b + ch.season_index * sizeof entry, entry) || !entry.season ||
        !swofty::mem::read(entry.season, season) || !season.weekends_begin || season.weekends_end <= season.weekends_begin ||
        !swofty::mem::read(reinterpret_cast<uintptr_t>(season.weekends_begin), weekend) || !weekend ||
        !swofty::mem::read(weekend, first) || !first.sessions_begin || first.sessions_end <= first.sessions_begin)
        return false;
    NeOptionalVec none_a{}, none_b{};
    Career_Rebuild_ChampionshipStandings(reinterpret_cast<uint8_t *>(champ) + offsetof(Championship, list_vtable),
                                         &none_a, &none_b);
    swofty::log("[progressive] standings rebuilt from the line-up (%s): Career_Rebuild_ChampionshipStandings", when);
    return true;
}

// Hybrid R&D (DESIGN.md D9): at each season apply a team's spendable resource points become the
// season's budget (season_logic.hpp hybrid_budget). The game's RP generation (0x141229e00) credits
// a team's award to one wallet: the team's human driver, else its last race seat in line-up order;
// Career_Adjust_RpDriverWallet (0x1434fefe0) changes only an existing entry and leaves a locked
// (unlimited) one alone, so the budget goes to that same driver's wallet (per-driver mode) or to
// the team wallet (per-team mode), and a missing entry is logged. How the AI spends RP is not
// mapped.
constexpr int kHybridBase = 1000;

void hybrid_budgets(uintptr_t e, int year)
{
    auto *rp = static_cast<ResourcePointsComponent *>(
        swofty::career::component_by_desc(k_Career_ResourcePointsComponentDesc.get()));
    ResourcePointsComponent view{};
    std::vector<Row> rows;
    if (!rp || !swofty::mem::read(rp, view) || !entrant_rows(e, rows)) {
        swofty::log("[progressive] hybrid R&D %d: no ResourcePointsComponent or line-up; budgets unchanged", year);
        return;
    }
    bool per_team = view.wallet_mode == 1;
    if (view.wallet_mode > 1) {
        swofty::log("[progressive] hybrid R&D %d: wallet mode %u unknown; budgets unchanged", year, view.wallet_mode);
        return;
    }
    size_t set = 0, missing = 0;
    for (int i = 0; i < season::kSlots; ++i) {
        uint32_t team = static_cast<uint32_t>(g_data.slots[i].team_stat) << 16;
        const Row *earner = nullptr;
        for (auto &r : rows)
            if (((r.rec.team_stat_id ^ team) & 0xffff00ffu) == 0) {
                if (human(r.rec)) {
                    earner = &r;
                    break;
                }
                if (has_car(r.rec) && position_of(r.rec) >= 0 && position_of(r.rec) < 2)
                    earner = &r;
            }
        if (!earner)
            continue;
        uint32_t driver = driver_of(earner->rec);
        int budget = season::hybrid_budget(g_data, year, i, kHybridBase);
        uint64_t before = 0, after = 0;
        Career_Get_RpWallet(&rp->wallet_entity, &before, driver);
        int32_t value = static_cast<int32_t>(before & 0xffffffffu);
        bool locked = ((before >> 32) & 0xff) != 0;
        if (locked) {
            swofty::log("[progressive] hybrid R&D %d: %s (%s) wallet is unlimited; left alone", year,
                        g_data.slots[i].team.c_str(), label_or(Kind::driver, stat_of(driver)).c_str());
            continue;
        }
        int32_t delta = budget - value;
        if (per_team)
            Career_Adjust_RpTeamWallet(&rp->team_wallets, earner->rec.team_stat_id, delta);
        else
            Career_Adjust_RpDriverWallet(&rp->driver_wallets, driver, delta);
        Career_Get_RpWallet(&rp->wallet_entity, &after, driver);
        int32_t now = static_cast<int32_t>(after & 0xffffffffu);
        bool ok = now == budget;
        ok ? ++set : ++missing;
        swofty::log("[progressive] hybrid R&D %d: %s %s wallet %d -> %d (budget %d)%s", year,
                    g_data.slots[i].team.c_str(),
                    per_team ? "team" : label_or(Kind::driver, stat_of(driver)).c_str(), value, now, budget,
                    ok ? "" : delta && now == value ? ": NO WALLET ENTRY, nothing changed" : ": did not read back");
    }
    swofty::log("[progressive] hybrid R&D %d: %zu/%zu team budget(s) set (%s wallets)", year, set, set + missing,
                per_team ? "per-team" : "per-driver");
}

std::mutex g_dev_mutex;
std::vector<std::pair<uint32_t, float>> g_dev;
std::string g_dev_logged;

bool season_rounds(uintptr_t e, uint32_t &current, uint32_t &rounds)
{
    uintptr_t champ = championship(e);
    Championship ch{};
    if (!champ || !swofty::mem::read(champ, ch))
        return false;
    auto b = reinterpret_cast<uintptr_t>(ch.seasons_begin), en = reinterpret_cast<uintptr_t>(ch.seasons_end);
    size_t n = en > b ? (en - b) / sizeof(ChampionshipSeasonEntry) : 0;
    ChampionshipSeasonEntry entry{};
    Season season{};
    if (ch.season_index >= n || !swofty::mem::read(b + ch.season_index * sizeof entry, entry) || !entry.season ||
        !swofty::mem::read(entry.season, season))
        return false;
    auto wb = reinterpret_cast<uintptr_t>(season.weekends_begin), we = reinterpret_cast<uintptr_t>(season.weekends_end);
    if (we <= wb || (we - wb) % sizeof(Weekend *) || (we - wb) / sizeof(Weekend *) > 64)
        return false;
    rounds = static_cast<uint32_t>((we - wb) / sizeof(Weekend *));
    current = season.current_weekend;
    return true;
}

void dev_publish(const development::Plan *p)
{
    std::lock_guard<std::mutex> lock(g_dev_mutex);
    g_dev.clear();
    if (!p)
        return;
    for (int i = 0; i < season::kSlots; ++i)
        if (g_data.slots[i].team_stat >= 0)
            g_dev.emplace_back(static_cast<uint32_t>(g_data.slots[i].team_stat), static_cast<float>(p->teams[i].progress));
}

// Real life R&D: the year's in-season development at the current round becomes the downforce
// progress progressive_rnd.dll gives each team's cars at the next session (development_logic.hpp).
// Recomputed at every load and Team HQ from the year and round alone, so applies and reloads agree;
// the store records the last applied round and deltas, and a change is logged.
void development_apply(uintptr_t e, store::Career *c, int year, const char *when)
{
    if (!c || !season_development || c->rnd != "real_life") {
        dev_publish(nullptr);
        return;
    }
    uint32_t current = 0, rounds = 0;
    if (!e || !season_rounds(e, current, rounds)) {
        dev_publish(nullptr);
        swofty::log("[progressive] development %d (%s): no current season or rounds; the cars stay at the year's pace",
                    year, when);
        return;
    }
    development::Plan p = development::plan(g_data, year, current, rounds);
    if (!development::any_curve(p)) {
        dev_publish(nullptr);
        if (g_dev_logged != c->key + ":none:" + std::to_string(year)) {
            g_dev_logged = c->key + ":none:" + std::to_string(year);
            swofty::log("[progressive] development %d (%s): no in-season curve for this year; the cars stay at the "
                        "year's pace",
                        year, when);
        }
        return;
    }
    dev_publish(&p);
    std::string applied = development::applied_text(p);
    bool same = c->dev_year == year && c->dev_round == static_cast<int>(current) &&
                c->dev_rounds == static_cast<int>(rounds) && c->dev_applied == applied;
    if (!same) {
        c->dev_year = year;
        c->dev_round = static_cast<int>(current);
        c->dev_rounds = static_cast<int>(rounds);
        c->dev_applied = applied;
        store::save();
    }
    std::string logged = c->key + ":" + std::to_string(year) + ":" + std::to_string(current) + ":" + applied;
    if (same && g_dev_logged == logged)
        return;
    g_dev_logged = logged;
    const season::Year *y = g_data.year(year);
    std::string teams;
    int clamped = 0;
    for (int i = 0; i < season::kSlots; ++i) {
        const development::Entry &t = p.teams[i];
        char b[128];
        std::snprintf(b, sizeof b, "%s%s %+.2f%% (%.3f)%s", teams.empty() ? "" : ", ",
                      y && !y->teams[i].name.empty() ? y->teams[i].name.c_str() : g_data.slots[i].team.c_str(), t.delta,
                      t.progress, t.curve ? "" : " no curve");
        teams += b;
        clamped += t.clamped;
    }
    swofty::log("[progressive] development %d round %u/%u: %s [%s; season point %.2f, base %.2f%%, downforce progress "
                "= (base - delta) / %.1f%s%s]",
                year, current + 1, rounds, teams.c_str(), when, p.point, p.base, development::kGainPct,
                clamped ? "; some clamped to 0..1" : "", same ? "" : "; recorded in careers.json");
}

// The R&D models' type metadata (rnd_model_logic.hpp): model +0x1a38..+0x1a40, 0x20-byte entries
// with the department at +1, the rating base at +0xc and the fully upgraded rating at +0x10 (decomps
// 0x141890e60 and 0x143518350 read them; the vehicle fill 0x141238cb0 reads the same entries' +0,
// +4, +8).
constexpr uintptr_t kModelTypeMeta = 0x1a38;
constexpr uintptr_t kMetaStride = 0x20, kMetaDept = 0x1, kMetaBase = 0xc, kMetaFull = 0x10;
std::string g_models_logged;

bool read_meta(uintptr_t model, uintptr_t &begin, std::vector<rnd_model::Meta> &out)
{
    uintptr_t end = 0;
    begin = 0;
    out.clear();
    if (!swofty::mem::read(model + kModelTypeMeta, begin) || !swofty::mem::read(model + kModelTypeMeta + 8, end) ||
        !begin || end <= begin || (end - begin) % kMetaStride || (end - begin) / kMetaStride > 64 ||
        !swofty::mem::readable(begin, end - begin))
        return false;
    for (uintptr_t at = begin; at < end; at += kMetaStride) {
        rnd_model::Meta m;
        swofty::mem::read(at, m.type);
        swofty::mem::read(at + kMetaDept, m.department);
        swofty::mem::read(at + kMetaBase, m.base);
        swofty::mem::read(at + kMetaFull, m.full);
        out.push_back(m);
    }
    return true;
}

bool team_models(std::vector<uintptr_t> &out, UpgradesComponent &view)
{
    out.clear();
    auto *comp = static_cast<UpgradesComponent *>(
        swofty::career::component_by_desc(k_Career_UpgradesComponentDesc.get()));
    if (!comp || !swofty::mem::read(comp, view))
        return false;
    auto ub = reinterpret_cast<uintptr_t>(view.upgrades_begin), ue = reinterpret_cast<uintptr_t>(view.upgrades_end);
    if (ue <= ub || (ue - ub) % sizeof(void *) || (ue - ub) / sizeof(void *) > 64)
        return false;
    for (uintptr_t at = ub; at < ue; at += sizeof(void *)) {
        uintptr_t model = 0;
        if (swofty::mem::read(at, model) && model && swofty::mem::readable(model, sizeof(Upgrades)))
            out.push_back(model);
    }
    return true;
}

// Every team's R&D model gets the year's performance (rnd_model_logic.hpp): Real life the season's
// fixed level plus the in-season development at the current round, upgrades worth nothing; Hybrid
// the season's baseline plus a small room for this season's R&D. Runs at every load, Team HQ and
// R&D change from the year alone, so a new season takes the new year's order and reloads agree;
// logged with a read-back.
void rnd_models(uintptr_t e, store::Career *c, int year, const char *when)
{
    if (!c || !season_rnd_models)
        return;
    bool real = c->rnd == "real_life";
    development::Plan dev;
    bool have_dev = false;
    uint32_t current = 0, rounds = 0;
    if (real && season_development && e && season_rounds(e, current, rounds)) {
        dev = development::plan(g_data, year, current, rounds);
        have_dev = development::any_curve(dev);
    }
    UpgradesComponent view{};
    std::vector<uintptr_t> models;
    if (!team_models(models, view)) {
        swofty::log("[progressive] R&D models %d (%s): no UpgradesComponent or team models; left alone", year, when);
        return;
    }
    double room = real ? 0.0 : rnd_model::kGainRoom;
    const season::Year *y = g_data.year(year);
    std::string teams;
    size_t count = 0, changed = 0, failed = 0, skipped = 0;
    for (uintptr_t model : models) {
        uint32_t key = 0;
        swofty::mem::read(model + offsetof(Upgrades, team_key), key);
        int slot = season::slot_of_team(g_data, static_cast<int>(key >> 16));
        uintptr_t begin = 0;
        std::vector<rnd_model::Meta> m;
        if (slot < 0 || !read_meta(model, begin, m) || m.empty()) {
            ++skipped;
            continue;
        }
        ++count;
        double delta = have_dev ? dev.teams[slot].delta : 0.0;
        double goal = rnd_model::target(rnd_model::year_gap(g_data, year, slot), delta);
        double was = rnd_model::base_sum(m), was_room = rnd_model::gain_room(m);
        if (rnd_model::apply(m, goal, room)) {
            ++changed;
            for (size_t i = 0; i < m.size(); ++i) {
                *reinterpret_cast<float *>(begin + i * kMetaStride + kMetaBase) = m[i].base;
                *reinterpret_cast<float *>(begin + i * kMetaStride + kMetaFull) = m[i].full;
            }
        }
        std::vector<rnd_model::Meta> back;
        uintptr_t begin2 = 0;
        bool ok = read_meta(model, begin2, back) && begin2 == begin && back.size() == m.size() &&
                  std::fabs(rnd_model::base_sum(back) - goal) < 0.5 && std::fabs(rnd_model::gain_room(back) - room) < 0.5;
        if (!ok)
            ++failed;
        char b[192];
        std::snprintf(b, sizeof b, "%s%s(%u) %.0f +%.0f (was %.0f +%.0f)%s", teams.empty() ? "" : ", ",
                      y && !y->teams[slot].name.empty() ? y->teams[slot].name.c_str() : g_data.slots[slot].team.c_str(),
                      key >> 16, ok ? rnd_model::base_sum(back) : goal, ok ? rnd_model::gain_room(back) : room, was, was_room,
                      ok ? "" : " READ-BACK FAILED");
        teams += b;
    }
    std::string logged = c->key + ":" + std::to_string(year) + ":" + teams;
    if (!changed && !failed && g_models_logged == logged)
        return;
    g_models_logged = logged;
    swofty::log("[progressive] R&D models %d: %s [%s; %s; each of a model's types = rating / n, room for upgrades "
                "after +; rating = %.0f - %.0f x (gap %%%s); %zu model(s), %zu rewritten, %zu skipped, read-back %s]",
                year, teams.c_str(), when, real ? "Real life: upgrades add nothing" : "Hybrid: this season's R&D on top",
                rnd_model::kTop, rnd_model::kPerPct, have_dev ? " + development delta %" : "", count, changed, skipped,
                failed ? "FAILED" : "ok");
}

// Every team's R&D tree starts each Progressive season fresh, the way a new car starts a real year:
// every upgrade past Available (in progress, completed, failed, in development, waiting to be fitted)
// or fitted goes back through the game's per-instance reset (Car_Reset_LastFittedUpgrade, the one its
// reset-all and the Career Mode Editor's rnd.reset use), then availability is refreshed and, when a
// completed upgrade was reset, the team's fitted ratio is re-posted. Covers every tree of a model
// (the player's per-driver tree too). Once per season index (store trees_season); logged with a
// read-back.
constexpr uint8_t kStateAvailable = 1, kStateCompleted = 3, kStateInvalid = 5;
constexpr uint8_t kDeptTyres = 4;
constexpr size_t kMaxInstances = 0x180;

template <class F> void each_instance(uintptr_t model, F &&f)
{
    uint64_t trees = 0;
    swofty::mem::read(model + offsetof(Upgrades, tree_count), trees);
    for (uint64_t k = 0; k < trees && k < 2; ++k) {
        UpgradeTree tree{};
        if (!swofty::mem::read(model + offsetof(Upgrades, trees) + k * sizeof(UpgradeTree), tree))
            continue;
        auto b = reinterpret_cast<uintptr_t>(tree.instances_begin), en = reinterpret_cast<uintptr_t>(tree.instances_end);
        if (!b || en < b || (en - b) % sizeof(UpgradeInstance) || (en - b) / sizeof(UpgradeInstance) > kMaxInstances ||
            !swofty::mem::readable(b, en - b))
            continue;
        for (uintptr_t u = b; u < en; u += sizeof(UpgradeInstance))
            f(u);
    }
}

bool last_state(uintptr_t inst, UpgradeStateEntry &out, size_t &count)
{
    UpgradeInstance i{};
    count = 0;
    if (!swofty::mem::read(inst, i))
        return false;
    auto b = reinterpret_cast<uintptr_t>(i.states_begin), en = reinterpret_cast<uintptr_t>(i.states_end);
    if (b == en)
        return true;
    if (!b || en < b || (en - b) % sizeof(UpgradeStateEntry) || (en - b) / sizeof(UpgradeStateEntry) > 0x1000)
        return false;
    count = (en - b) / sizeof(UpgradeStateEntry);
    return swofty::mem::read(en - sizeof(UpgradeStateEntry), out);
}

bool developed(uintptr_t inst, bool *completed = nullptr)
{
    UpgradeInstance i{};
    UpgradeStateEntry last{};
    size_t count = 0;
    if (!swofty::mem::read(inst, i) || i.department >= kDeptTyres || !last_state(inst, last, count) || !count)
        return false;
    if (completed)
        *completed = last.state == kStateCompleted;
    return last.state != kStateInvalid && (last.state > kStateAvailable || last.adaptation);
}

void reset_trees(uintptr_t e, store::Career *c, uint32_t index, int year, const char *when)
{
    if (!c || !season_rnd_models || c->trees_season == static_cast<int>(index))
        return;
    UpgradesComponent view{};
    std::vector<uintptr_t> models;
    if (!team_models(models, view) || models.empty()) {
        swofty::log("[progressive] R&D trees %d (%s): no UpgradesComponent or team models; not reset", year, when);
        return;
    }
    uint32_t minutes = 0;
    if (!view.career_time ||
        !swofty::mem::read(reinterpret_cast<uintptr_t>(view.career_time) + offsetof(CareerTimeComponent, current_minutes),
                           minutes)) {
        swofty::log("[progressive] R&D trees %d (%s): no career clock; not reset", year, when);
        return;
    }
    uint16_t day = static_cast<uint16_t>(minutes / 1440);
    std::vector<Row> rows;
    entrant_rows(e, rows);
    std::string teams;
    size_t reset_teams = 0, left_total = 0;
    for (uintptr_t model : models) {
        uint32_t key = 0;
        uint8_t mode = 0;
        swofty::mem::read(model + offsetof(Upgrades, team_key), key);
        swofty::mem::read(model + offsetof(Upgrades, mode), mode);
        int slot = season::slot_of_team(g_data, static_cast<int>(key >> 16));
        size_t done = 0, completed = 0;
        uintptr_t first_completed = 0;
        each_instance(model, [&](uintptr_t u) {
            bool comp = false;
            if (!developed(u, &comp))
                return;
            ++done;
            if (comp) {
                ++completed;
                if (!first_completed)
                    first_completed = u;
            }
        });
        if (!done)
            continue;
        each_instance(model, [&](uintptr_t u) {
            if (developed(u))
                Car_Reset_LastFittedUpgrade(reinterpret_cast<void *>(u));
        });
        if (mode == 1 || mode == 2) {
            Career_Refresh_UpgradeAvailability(reinterpret_cast<void *>(model), day);
        } else {
            uint32_t owner = 0x3fff0000;
            for (auto &r : rows)
                if (((r.rec.team_stat_id ^ key) & 0xffff00ffu) == 0 && (human(r.rec) || owner == 0x3fff0000))
                    owner = driver_of(r.rec);
            if ((owner & 0xffff0000u) != 0x3fff0000u)
                Career_Set_UpgradesOwner(reinterpret_cast<void *>(model), owner);
            else
                reinterpret_cast<Upgrades *>(model)->owner_cache = 0x3fff0000;
        }
        if (first_completed)
            Career_Post_UpgradeFittedRatio(reinterpret_cast<void *>(model), reinterpret_cast<void *>(first_completed), 0);
        size_t left = 0;
        each_instance(model, [&](uintptr_t u) { left += developed(u) ? 1 : 0; });
        left_total += left;
        ++reset_teams;
        std::string name = slot >= 0 ? g_data.slots[slot].team : std::to_string(key >> 16);
        char b[160];
        std::snprintf(b, sizeof b, "%s%s mode %u: %zu developed (%zu completed) -> %zu", teams.empty() ? "" : ", ",
                      name.c_str(), mode, done, completed, left);
        teams += b;
    }
    c->trees_season = static_cast<int>(index);
    store::save();
    swofty::log("[progressive] R&D trees %d: reset for %zu team(s)%s%s [%s; season index %u, day %u; read-back %s]", year,
                reset_teams, teams.empty() ? "" : ": ", teams.c_str(), when, index, day,
                left_total ? "SOME LEFT" : "ok");
}

void season_performance(uintptr_t e, store::Career *c, int year, const char *when)
{
    SeasonInfo s;
    if (e && c && std::strcmp(when, "Team HQ") == 0 && season(e, s))
        reset_trees(e, c, s.index + static_cast<uint32_t>(std::max(0, c->test_seasons)), year, when);
    development_apply(e, c, year, when);
    rnd_models(e, c, year, when);
}

std::map<std::string, std::string> g_player_seen;
std::map<std::string, std::string> g_mid_logged;

std::string driver_text(const std::string &label, const season::Resolve &r)
{
    int st = r.driver_stat(label);
    return st < 0 ? label : label + "(" + std::to_string(st) + ")";
}

std::string seat_text(int slot, int seat)
{
    return (slot >= 0 && slot < season::kSlots ? g_data.slots[slot].team : std::string("?")) + " seat " +
           std::to_string(seat + 1);
}

std::string change_text(const season::Year &y, const season::Change &c, const season::Resolve &r)
{
    char b[96];
    std::snprintf(b, sizeof b, ", #%d, real round %d of %zu (%s)", c.number, c.round, y.real_tracks.size(),
                  c.circuit.c_str());
    std::string t = driver_text(c.in, r) + " in for " + driver_text(c.out, r) + " at " + seat_text(c.slot, c.seat) + b;
    t += ", " + c.kind;
    t += c.until ? ", until real round " + std::to_string(c.until) : ", for the rest of the season";
    t += ", look " + (c.look.label.empty() ? std::string("none") : c.look.label);
    return t;
}

// The career's mid-season schedule, once per session and calendar: which career race each change
// of the year comes before, and the changes the player rules skip.
void log_schedule(const store::Career &c, const midseason::At &at, const season::Resolve &r, bool from_store)
{
    const season::Year *y = g_data.year(at.year);
    if (!y)
        return;
    std::string tracks;
    for (int t : at.career)
        tracks += (tracks.empty() ? "" : ",") + std::to_string(t);
    std::string key = std::to_string(at.year) + "|" + tracks + "|" + (from_store ? "store" : "calendar");
    if (g_mid_logged[c.key] == key)
        return;
    g_mid_logged[c.key] = key;
    if (from_store) {
        swofty::log("[progressive] mid-season %d: the career calendar is unreadable; using the %zu change(s) recorded "
                    "at career round %d",
                    at.year, at.active.size(), at.round);
        return;
    }
    swofty::log("[progressive] mid-season %d schedule: career calendar of %zu races (tracks %s), %zu real rounds, %zu "
                "change(s); next race is career round %d (real round %d's changes are due)",
                at.year, at.career.size(), tracks.c_str(), y->real_tracks.size(), y->changes.size(), at.round,
                at.real_round);
    for (size_t i = 0; i < y->changes.size(); ++i) {
        const season::Change &ch = y->changes[i];
        if (!at.kept[i])
            continue;
        const midseason::Window &w = at.windows[i];
        std::string until = w.to >= midseason::kOpen ? std::string("for the rest of the season")
                            : w.to > static_cast<int>(at.career.size())
                                ? "to the season's end; the regular misses the last race"
                                : "back at career round " + std::to_string(w.to);
        swofty::log("[progressive] mid-season %d round %d: %s (%s)", at.year, w.from, change_text(*y, ch, r).c_str(),
                    until.c_str());
    }
    for (auto &k : at.skipped)
        swofty::log("[progressive] mid-season %d skipped: %s: %s", at.year, change_text(*y, y->changes[k.change], r).c_str(),
                    k.why.c_str());
}

// What changed against the career's record: each change now in force that was not, each one that
// ended, and where the plan put its drivers when the player's seat or driver remapped it.
void log_changes(const store::Career &c, const midseason::At &at, const season::Plan &p, const season::Resolve &r)
{
    const season::Year *y = g_data.year(at.year);
    if (!y)
        return;
    std::vector<std::string> before = c.mid_year == at.year ? midseason::split_ids(c.mid_applied)
                                                             : std::vector<std::string>{};
    auto target_of = [&](const std::string &label, int &slot, int &seat) {
        int st = r.driver_stat(label);
        slot = seat = -1;
        for (int i = 0; st >= 0 && i < season::kSlots; ++i)
            for (int k = 0; k < lineup::kSeats; ++k)
                if (p.target[i][k] == static_cast<uint32_t>(st)) {
                    slot = i;
                    seat = k;
                }
    };
    for (int i : at.active) {
        const season::Change &ch = y->changes[i];
        if (std::find(before.begin(), before.end(), ch.id) != before.end())
            continue;
        swofty::log("[progressive] mid-season %d round %d: %s", at.year, at.round, change_text(*y, ch, r).c_str());
        int slot, seat;
        target_of(ch.in, slot, seat);
        if (static_cast<uint32_t>(r.driver_stat(ch.in)) == p.player)
            swofty::log("[progressive] mid-season %d round %d: the player's driver %s moves to %s%s", at.year, at.round,
                        ch.in.c_str(), g_data.slots[ch.slot].team.c_str(),
                        p.player_moves ? "; follow mode moves the player with a contract for that team"
                                       : "; choose mode keeps the player at his team (the season fit remaps the seat)");
        else if (slot < 0)
            swofty::log("[progressive] mid-season %d round %d: %s has no seat in the plan (the player's seat or the "
                        "season fit took it)",
                        at.year, at.round, ch.in.c_str());
        else if (slot != ch.slot)
            swofty::log("[progressive] mid-season %d round %d: %s races for %s instead (the player holds his real "
                        "seat; choose mode remap)",
                        at.year, at.round, ch.in.c_str(), seat_text(slot, seat).c_str());
    }
    for (auto &id : before) {
        bool still = false;
        for (int i : at.active)
            still = still || y->changes[i].id == id;
        if (still)
            continue;
        for (auto &ch : y->changes)
            if (ch.id == id)
                swofty::log("[progressive] mid-season %d round %d: %s's drive at %s ended; %s",
                            at.year, at.round, ch.in.c_str(), seat_text(ch.slot, ch.seat).c_str(),
                            ch.until && (at.windows.empty() || at.round >= at.windows[&ch - y->changes.data()].to)
                                ? (ch.out + " is back").c_str()
                                                                  : "the seat follows the line-up at this round");
    }
}

// Compares the copies of the player's team (player_logic.hpp) and logs them when they change; the
// store's recorded team (the Resume Career card) follows the seat. The stale-contract repair itself
// is the season apply's contract step (sign_seats), which runs at every idle Team HQ.
player::Verdict player_check(uintptr_t e, store::Career *c, const char *when)
{
    uintptr_t manager = contracts_manager();
    std::vector<Row> rows;
    std::vector<uintptr_t> live;
    if (!c || !manager || !entrant_rows(e, rows) || !contract_objects(manager, live))
        return {};
    const Row *h = nullptr;
    for (auto &r : rows)
        if (human(r.rec))
            h = &r;
    if (!h)
        return {};
    player::Teams t;
    t.row = teamless(h->rec) ? player::kNone : static_cast<int>(stat_of(h->rec.team_stat_id));
    uint32_t d = driver_of(h->rec);
    DriverContract k{};
    uintptr_t own = contract_of(live, d);
    t.contract_found = own && swofty::mem::read(own, k);
    if (t.contract_found) {
        t.contract = none(k.team) ? player::kNone : static_cast<int>(stat_of(k.team));
        t.contract_active = k.active != 0;
    }
    uint32_t cached = cached_contract_team(manager, d);
    t.cached = none(cached) ? player::kNone : static_cast<int>(stat_of(cached));
    t.store = c->team;
    player::Verdict v = player::judge(t);
    std::string &seen = g_player_seen[when];
    std::string now = c->key + "|" + v.text;
    if (seen != now) {
        seen = now;
        swofty::log("[progressive] player team (%s): %s at %s; %s", when, label_or(Kind::driver, stat_of(d)).c_str(),
                    t.row == player::kNone ? "no team" : label_or(Kind::team, static_cast<uint32_t>(t.row)).c_str(),
                    v.text.c_str());
    }
    if (v.store_stale) {
        c->team = t.row;
        store::save();
    }
    return v;
}

void contract_left_alone(const player::Verdict &v, store::Career *c, int year, const char *why)
{
    if (v.fix != player::Fix::sign_row_team)
        return;
    std::string &seen = g_player_seen["left alone"];
    std::string now = c->key + "|" + std::to_string(year) + "|" + why;
    if (seen == now)
        return;
    seen = now;
    swofty::log("[progressive] season %d player contract left alone: %s; the plan decides his team before any contract "
                "is signed",
                year, why);
}

std::atomic<int> g_rnd{0};
std::atomic<bool> g_test_armed{false};
std::string g_current;
std::string g_entry;
std::map<std::string, int> g_applies;

int rnd_code(const std::string &m)
{
    return m == "real_life" ? 1 : m == "hybrid" ? 3 : 0;
}

std::string rnd_option()
{
    std::string v(rnd_mode.get());
    return v == "real_life" ? v : "hybrid";
}

// The Team HQ tab (lane A) reads these view-model bools: the tab shows in a Progressive career, and
// its R&D tab only when the career's R&D mode is hybrid.
constexpr const char *kVmCareer = "view_model_data.hud_progressive_career";
constexpr const char *kVmRnd = "view_model_data.hud_progressive_rnd";

void publish(bool career, const std::string &rnd)
{
    bool show_rnd = career && rnd == "hybrid";
    auto a = swofty::hud::set(kVmCareer, career), b = swofty::hud::set(kVmRnd, show_rnd);
    static int logged = 0;
    if (logged++ < 12)
        swofty::log("[progressive] view model: %s = %s (%s), %s = %s (%s)", kVmCareer, career ? "true" : "false",
                    swofty::hud::name(a), kVmRnd, show_rnd ? "true" : "false", swofty::hud::name(b));
}

} // namespace

bool progressive_career()
{
    return swofty::career::is(kGroup);
}

// The Team HQ tab (manifest hub_tab, built by the core) shows rnd_mode, which is a profile option:
// on load the career's own mode is pushed into it, and a Real-life career hides the R&D tab of Team HQ
// (the core's hide_rnd: applied when Team HQ is built, and to an open Team HQ on the next frame).
store::Career *live_entry()
{
    const std::string &k = g_current.empty() ? g_entry : g_current;
    return k.empty() ? nullptr : store::find(k);
}

int live_year(const store::Career *c)
{
    SeasonInfo s;
    uintptr_t e = entity();
    uint32_t index = e && season(e, s) ? s.index : 0;
    if (c)
        index += static_cast<uint32_t>(std::max(0, c->test_seasons));
    return season::year_for(c ? c->start : setting_year(), index);
}

void note_card(store::Career *c, const std::vector<Row> &rows, uint32_t season_index)
{
    int team = -1;
    for (auto &r : rows)
        if (human(r.rec))
            team = teamless(r.rec) ? -1 : static_cast<int>(stat_of(r.rec.team_stat_id));
    if (c->team == team && c->season == static_cast<int>(season_index))
        return;
    c->team = team;
    c->season = static_cast<int>(season_index);
    store::save();
}

int real_team(uint32_t driver, int year)
{
    for (auto &p : g_data.pool) {
        uint32_t st = 0;
        int stat = names::stat(Kind::driver, p.driver, st) ? static_cast<int>(st) : p.stat;
        if (stat < 0 || static_cast<uint32_t>(stat) != driver)
            continue;
        auto it = p.slot_by_year.find(year);
        if (it == p.slot_by_year.end() || it->second < 1 || it->second > season::kSlots)
            return -1;
        return g_data.slots[it->second - 1].team_stat;
    }
    return -1;
}

void new_career()
{
    if (!g_new_career.exchange(true))
        swofty::log("[progressive] store: a Progressive new-career wizard step; the next career load gets a new entry");
}

void hub_sync(const std::string &rnd, const char *why)
{
    swofty::hub::Result h = swofty::hub::hide_rnd(rnd == "real_life");
    swofty::hub::Result o = rnd_option() == rnd ? swofty::hub::Result::ok
                                                : swofty::hub::set_option("swofty.progressive", "rnd_mode", rnd);
    swofty::log("[progressive] Team HQ tab (%s): R&D %s, R&D tab %s (%s), option %s", why, rnd.c_str(),
                rnd == "real_life" ? "hidden" : "shown", swofty::hub::name(h), swofty::hub::name(o));
}

void season_loaded(const swofty::career::State &state)
{
    if (!progressive_career()) {
        clear_names();
        swofty::hub::hide_rnd(false);
        return;
    }
    names::load();
    uintptr_t e = entity();
    SeasonInfo s;
    std::vector<Row> rows;
    if (!e || !season(e, s) || !entrant_rows(e, rows) || !store::load())
        return;
    uint32_t player = 0;
    int humans = 0;
    for (auto &r : rows)
        if (human(r.rec)) {
            player = stat_of(driver_of(r.rec));
            ++humans;
        }
    if (humans != 1 || !data() || !names::load())
        return;
    store::Career *c = career_entry(state, s.index, player, rows);
    if (!c)
        return;
    note_card(c, rows, s.index);
    g_entry = c->key;
    hub_sync(c->rnd, "career load");
    season_performance(e, c, season::year_for(c->start, s.index + static_cast<uint32_t>(std::max(0, c->test_seasons))),
                       "career load");
}

void rnd_option_changed()
{
    store::Career *c = g_current.empty() ? nullptr : store::find(g_current);
    if (!c)
        return;
    std::string now = rnd_option();
    if (now == c->rnd)
        return;
    swofty::log("[progressive] R&D: career %s switches from %s to %s", c->key.c_str(), c->rnd.c_str(), now.c_str());
    c->rnd = now;
    store::save();
    g_rnd = rnd_code(now);
    publish(true, now);
    hub_sync(now, "R&D changed; an open Team HQ follows on the next frame");
    uintptr_t e = entity();
    SeasonInfo s;
    if (now == "hybrid" && e && season(e, s) && data()) {
        int year = c->applied_season >= 0 && c->applied_year ? c->applied_year : season::year_for(c->start, s.index);
        hybrid_budgets(e, year);
    }
    if (e && season(e, s) && data())
        season_performance(e, c,
                           season::year_for(c->start, s.index + static_cast<uint32_t>(std::max(0, c->test_seasons))),
                           "R&D changed");
}

void rnd_frame(double, void *)
{
    if (g_rnd_changed.exchange(false))
        rnd_option_changed();
}

void wizard_abandoned(const char *why)
{
    if (g_new_career.exchange(false))
        swofty::log("[progressive] store: %s; the pending new Progressive career is dropped", why);
}

void wizard_left(const char *group)
{
    if (g_new_career.exchange(false))
        swofty::log("[progressive] store: the %s tile was pressed; the pending new Progressive career is dropped",
                    group ? group : "?");
    if (g_current.empty() && g_names_year && !progressive_career()) {
        swofty::log("[progressive] team names: the %s tile was pressed; the 2026 names are back", group ? group : "?");
        clear_names();
    }
}

void season_unloaded()
{
    if (!g_current.empty())
        publish(false, "");
    swofty::hub::hide_rnd(false);
    g_rnd = 0;
    dev_publish(nullptr);
    g_dev_logged.clear();
    g_models_logged.clear();
    g_current.clear();
    g_entry.clear();
    g_applies.clear();
    g_standings_pending.clear();
    g_refreshed_for.clear();
    clear_names();
    g_names_for.clear();
    g_player_seen.clear();
    g_mid_logged.clear();
    g_failures = 0;
}

void season_hub(const swofty::career::State &state)
{
    if (!progressive_career())
        return;
    uintptr_t e = entity();
    SeasonInfo s;
    std::vector<Row> rows;
    if (!e || !season(e, s) || !entrant_rows(e, rows) || !data() || !names::load())
        return;
    uint32_t player = 0;
    int humans = 0;
    for (auto &r : rows)
        if (human(r.rec)) {
            player = stat_of(driver_of(r.rec));
            ++humans;
        }
    if (humans != 1)
        return;
    store::Career *c = career_entry(state, s.index, player, rows);
    if (!c)
        return;
    note_card(c, rows, s.index);
    if (g_current != c->key) {
        publish(true, c->rnd);
        hub_sync(c->rnd, "career loaded");
    }
    g_current = c->key;
    if (g_rnd.exchange(rnd_code(c->rnd)) != rnd_code(c->rnd))
        swofty::log("[progressive] R&D mode of this career: %s", c->rnd.c_str());
    if (g_rnd_changed.exchange(false))
        rnd_option_changed();
    if (g_test_armed.exchange(false)) {
        ++c->test_seasons;
        store::save();
        swofty::log("[progressive] TEST test_next_season: career %s moves on one season without racing it: game season "
                    "index %u + %d test season(s) = %u (turn the option off and on again for another)",
                    c->key.c_str(), s.index, c->test_seasons, s.index + c->test_seasons);
    }
    uint32_t index = s.index + static_cast<uint32_t>(std::max(0, c->test_seasons));
    int year = season::year_for(c->start, index);
    int shown = year;
    if (g_names_for != c->key || g_names_year != shown) {
        g_names_for = c->key;
        set_names(shown, "career");
    }
    season_performance(e, c, year, "Team HQ");
    if (c->applied_season >= 0 && g_refreshed_for != c->key) {
        g_refreshed_for = c->key;
        refresh_weekends(e, shown);
        if (standings_rebuild(e, "first Team HQ of this session"))
            g_standings_pending.clear();
    }
    if (g_standings_pending == c->key && standings_rebuild(e, "first Team HQ with round 1 sessions"))
        g_standings_pending.clear();
    if (entity_busy(e) || g_failures >= 3)
        return;
    player::Verdict before = player_check(e, c, "Team HQ, before the season apply");
    bool new_season = c->applied_season != static_cast<int>(index);
    bool move_player = c->moves == "follow_driver";
    season::Resolve res = resolver();
    Calendar cal;
    bool read = calendar(e, cal);
    if (read && !cal.tracks.empty() && midseason::tracks_text(cal.tracks) != c->mid_tracks) {
        c->mid_tracks = midseason::tracks_text(cal.tracks);
        store::save();
        swofty::log("[progressive] mid-season: career calendar recorded (%zu races, tracks %s)", cal.tracks.size(),
                    c->mid_tracks.c_str());
    }
    const season::Year *yr = g_data.year(year);
    bool mid = yr && !yr->changes.empty();
    midseason::At at;
    season::Plan p;
    if (mid) {
        auto is_player = [&](const std::string &label) {
            int st = res.driver_stat(label);
            return st >= 0 && static_cast<uint32_t>(st) == player;
        };
        midseason::Timing t = midseason::timing(*yr, read, cal.tracks, cal.current_ok, cal.next,
                                                 new_season || (read && !cal.started),
                                                 midseason::split_tracks(c->mid_tracks), c->mid_year == year);
        if (t.from != midseason::Timing::From::calendar) {
            static std::string said;
            std::string now = c->key + "|" + std::to_string(year) + "|" + t.how;
            if (said != now) {
                said = now;
                swofty::log("[progressive] mid-season %d: %s", year, t.how.c_str());
            }
        }
        if (t.from == midseason::Timing::From::record) {
            at = midseason::at_ids(g_data, year, c->mid_round, midseason::split_ids(c->mid_applied));
            log_schedule(*c, at, res, true);
            p = season::plan(g_data, year, view(rows), res, move_player, &at.line_up, nullptr, !new_season,
                             ten_team_years);
        } else if (t.from == midseason::Timing::From::none) {
            mid = false;
            p = season::plan(g_data, year, view(rows), res, move_player, nullptr, nullptr, !new_season,
                             ten_team_years);
        } else {
            at = midseason::at_round(g_data, year, t.tracks, t.next, is_player);
            midseason::At opening = midseason::at_round(g_data, year, t.tracks, 1, is_player);
            log_schedule(*c, at, res, false);
            p = season::plan(g_data, year, view(rows), res, move_player, &at.line_up, &opening.line_up, !new_season,
                             ten_team_years);
        }
        for (auto &n : at.notes)
            p.notes.push_back("mid-season: " + n);
    } else {
        p = season::plan(g_data, year, view(rows), res, move_player, nullptr, nullptr, !new_season, ten_team_years);
    }
    std::string mid_ids = mid ? midseason::ids_text(g_data.years[year], at.active) : std::string();
    auto record_mid = [&]() {
        if (!mid || (c->mid_year == year && c->mid_round == at.round && c->mid_applied == mid_ids))
            return;
        c->mid_year = year;
        c->mid_round = at.round;
        c->mid_applied = mid_ids;
        store::save();
    };
    if (!p.error.empty()) {
        static std::string last;
        std::string what = c->key + " " + std::to_string(year) + " " + p.error;
        if (what != last) {
            last = what;
            swofty::log("[progressive] season %d REFUSED: %s", year, p.error.c_str());
            ++g_failures;
        }
        contract_left_alone(before, c, year, "the season plan was refused");
        return;
    }
    if ((!p.moves.empty() || !p.releases.empty()) && !(read && cal.current_ok)) {
        static std::string waiting;
        std::string now = c->key + "|" + std::to_string(year);
        if (waiting != now) {
            waiting = now;
            swofty::log("[progressive] season %d: waiting for the season's race weekends before moving drivers (%s); "
                        "the game ends a rivalry whose rival leaves the player's team against the current weekend",
                        year, read ? "the current round is past the calendar" : "the calendar is not built yet");
        }
        contract_left_alone(before, c, year, "the season's race weekends are not built yet");
        return;
    }
    char why[256];
    if (new_season)
        std::snprintf(why, sizeof why, "season %u of career %s, start %d%s: not applied yet", index, c->key.c_str(),
                      c->start, c->test_seasons ? ", includes test seasons" : "");
    else
        std::snprintf(why, sizeof why, "season %u of career %s: the seats differ from the %d plan although season %d is "
                      "marked applied", index, c->key.c_str(), year, c->applied_season);
    if (mid) {
        size_t n = std::strlen(why);
        std::snprintf(why + n, sizeof why - n, "; mid-season line-up of career round %d (%zu change(s) in force)",
                      at.round, at.active.size());
    }
    std::string tries = c->key + ":" + std::to_string(year) + ":" + mid_ids;
    if (g_applies[tries] >= 2) {
        if (g_applies[tries]++ == 2)
            swofty::log("[progressive] season %d: the seats differ from the plan after two applies this session; "
                        "leaving them until the next load",
                        year);
        contract_left_alone(before, c, year, "the season apply already ran twice this session");
        return;
    }
    if (mid)
        log_changes(*c, at, p, res);
    int wrote = apply(e, p, rows, season_visuals, why);
    player_check(e, c, "after the season apply");
    if (wrote < 0) {
        ++g_failures;
        return;
    }
    std::vector<uint32_t> movers;
    for (auto &m : p.moves)
        movers.push_back(m.driver);
    ratings::season(e, c, index, player, movers);
    if (wrote)
        ++g_applies[tries];
    record_mid();
    if (!wrote && !new_season)
        return;
    c->applied_season = static_cast<int>(index);
    c->applied_year = year;
    store::save();
    if (g_names_year != year)
        set_names(year, "season applied");
    if (new_season && c->rnd == "hybrid")
        hybrid_budgets(e, year);
    if (!wrote)
        return;
    refresh_weekends(e, year);
    if (standings_rebuild(e, "season applied"))
        g_standings_pending.clear();
    else {
        g_standings_pending = c->key;
        swofty::log("[progressive] standings: round 1 has no sessions yet; the rebuild waits for a later Team HQ");
    }
    report("after season apply", state);
}

} // namespace pg

namespace pg {

// The season-start order of the standings. Career_Rebuild_ChampionshipStandings (0x140ae78f0) builds two
// order lists per season with 0x140ae2d80 and hands them to Career_Rebuild_SeasonStandings as
// order_a (u32 driver keys) and order_b (u32 team keys): the previous career season's final drivers'
// (season +0x2a8) and constructors' (season +0x3b8) order, or for the first season the setup
// entrant list (+0xe18). They are the tie order, so a season on 0 points is listed in them. In a
// Progressive career, while no weekend of the season has started, both lists are replaced with the
// real previous year's final order (data/years.json previous_standings, season_logic.hpp
// start_slot_order / start_driver_order), allocated from the game's default allocator because the
// callee frees them; the game's own lists are released as the callee would have. Once a weekend has
// started the game's lists stay.
namespace {

std::atomic<int> g_order_logs{0};

void *game_alloc(size_t bytes)
{
    void *alloc = nullptr;
    uintptr_t vt = 0, fn = 0;
    if (!g_Ne_DefaultAllocator.get() || !swofty::mem::read(reinterpret_cast<uintptr_t>(g_Ne_DefaultAllocator.get()), alloc) ||
        !alloc || !swofty::mem::read(reinterpret_cast<uintptr_t>(alloc) + offsetof(NeAllocator, vtable), vt) ||
        !swofty::mem::read(vt + offsetof(NeAllocatorVtbl, alloc), fn) || !fn)
        return nullptr;
    return reinterpret_cast<void *(*)(void *, uint64_t, uint64_t)>(fn)(alloc, bytes, alignof(uint32_t));
}

void *default_allocator()
{
    void *alloc = nullptr;
    return g_Ne_DefaultAllocator.get() && swofty::mem::read(reinterpret_cast<uintptr_t>(g_Ne_DefaultAllocator.get()), alloc)
               ? alloc
               : nullptr;
}

void release(NeOptionalVec *v)
{
    if (!v || !v->engaged || !v->begin || !v->alloc)
        return;
    uintptr_t vt = 0, fn = 0;
    if (!swofty::mem::read(reinterpret_cast<uintptr_t>(v->alloc) + offsetof(NeAllocator, vtable), vt) ||
        !swofty::mem::read(vt + offsetof(NeAllocatorVtbl, free), fn) || !fn)
        return;
    reinterpret_cast<void (*)(void *, void *)>(fn)(v->alloc, v->begin);
    v->begin = v->end = nullptr;
    v->count = 0;
}

bool fill(NeOptionalVec &v, const std::vector<uint32_t> &keys)
{
    if (keys.empty())
        return false;
    auto *buf = static_cast<uint32_t *>(game_alloc(keys.size() * sizeof(uint32_t)));
    if (!buf)
        return false;
    std::memcpy(buf, keys.data(), keys.size() * sizeof(uint32_t));
    v.alloc = static_cast<NeAllocator *>(default_allocator());
    v.begin = buf;
    v.end = buf + keys.size();
    v.count = keys.size();
    v.engaged = 1;
    return true;
}

int season_slot(uintptr_t season, bool &started)
{
    started = false;
    uintptr_t e = entity();
    uintptr_t champ = e ? championship(e) : 0;
    Championship ch{};
    if (!champ || !swofty::mem::read(champ, ch))
        return -1;
    auto b = reinterpret_cast<uintptr_t>(ch.seasons_begin), en = reinterpret_cast<uintptr_t>(ch.seasons_end);
    int index = -1;
    for (uintptr_t at = b; at && at < en && (at - b) / sizeof(ChampionshipSeasonEntry) < 64;
         at += sizeof(ChampionshipSeasonEntry)) {
        ChampionshipSeasonEntry entry{};
        if (swofty::mem::read(at, entry) && reinterpret_cast<uintptr_t>(entry.season) == season) {
            index = static_cast<int>((at - b) / sizeof(ChampionshipSeasonEntry));
            break;
        }
    }
    Season sn{};
    if (index < 0 || !swofty::mem::read(season, sn))
        return -1;
    auto wb = reinterpret_cast<uintptr_t>(sn.weekends_begin), we = reinterpret_cast<uintptr_t>(sn.weekends_end);
    for (uintptr_t at = wb; at && at < we && (at - wb) / sizeof(Weekend *) < 64; at += sizeof(Weekend *)) {
        uintptr_t w = 0;
        Weekend wk{};
        if (swofty::mem::read(at, w) && w && swofty::mem::read(w, wk) && wk.progress_state != 0)
            started = true;
    }
    return index;
}

} // namespace

bool start_order(uintptr_t season, NeOptionalVec &drivers, NeOptionalVec &teams, std::string &why)
{
    if (!progressive_career() || !data() || !names::load()) {
        why = "not a Progressive career";
        return false;
    }
    store::Career *c = live_entry();
    if (!c) {
        why = "the career's store entry is not known yet";
        return false;
    }
    bool started = false;
    int index = season_slot(season, started);
    if (index < 0) {
        why = "the season is not in the current championship";
        return false;
    }
    if (started) {
        why = "a weekend of this season has started; the game's order stays";
        return false;
    }
    int year = season::year_for(c->start, static_cast<uint32_t>(index + std::max(0, c->test_seasons)));
    const season::Year *y = g_data.year(year);
    if (!y || y->previous_slots.empty()) {
        why = "years.json has no previous standings for " + std::to_string(year);
        return false;
    }
    uintptr_t e = entity();
    std::vector<Row> rows;
    if (!e || !entrant_rows(e, rows)) {
        why = "the line-up is unreadable";
        return false;
    }
    std::vector<uint32_t> seated, team_keys;
    std::map<uint32_t, uint32_t> key_of;
    for (int i = 0; i < season::kSlots; ++i)
        for (int k = 0; k < lineup::kSeats; ++k)
            for (auto &r : rows)
                if (has_car(r.rec) && !teamless(r.rec) && position_of(r.rec) == k &&
                    season::slot_of_team(g_data, static_cast<int>(stat_of(r.rec.team_stat_id))) == i) {
                    seated.push_back(stat_of(driver_of(r.rec)));
                    key_of[stat_of(driver_of(r.rec))] = driver_of(r.rec);
                }
    for (auto &r : rows)
        if (has_car(r.rec) && !teamless(r.rec) && !key_of.count(stat_of(driver_of(r.rec)))) {
            seated.push_back(stat_of(driver_of(r.rec)));
            key_of[stat_of(driver_of(r.rec))] = driver_of(r.rec);
        }
    std::vector<uint32_t> driver_keys;
    for (uint32_t d : season::start_driver_order(*y, resolver(), seated))
        driver_keys.push_back(key_of[d]);
    std::string first;
    std::array<bool, season::kSlots> closed{};
    for (int i = 0; ten_team_years && i < season::kSlots; ++i) {
        bool cars = false;
        for (auto &r : rows)
            cars = cars || (has_car(r.rec) && !teamless(r.rec) &&
                            static_cast<int>(stat_of(r.rec.team_stat_id)) == g_data.slots[i].team_stat);
        closed[i] = season::no_team(g_data, year, i) && !cars;
    }
    for (int slot : season::start_slot_order(*y, closed)) {
        uint32_t key = static_cast<uint32_t>(g_data.slots[slot].team_stat) << 16;
        for (auto &r : rows)
            if (!teamless(r.rec) && stat_of(r.rec.team_stat_id) == stat_of(key)) {
                key = r.rec.team_stat_id;
                break;
            }
        team_keys.push_back(key);
        if (first.size() < 120)
            first += (first.empty() ? "" : ", ") + g_data.slots[slot].team;
    }
    if (!fill(drivers, driver_keys) || !fill(teams, team_keys)) {
        release(&drivers);
        release(&teams);
        why = "no game allocator for the order lists";
        return false;
    }
    if (g_order_logs++ < 24)
        swofty::log("[progressive] standings: season %d starts in the real %d order: %zu driver(s) (%s first), %zu "
                    "team(s): %s",
                    year, y->previous_year, driver_keys.size(),
                    driver_keys.empty() ? "-" : label_or(Kind::driver, stat_of(driver_keys[0])).c_str(),
                    team_keys.size(), first.c_str());
    return true;
}

// After the rebuild the constructors' table lists its initial order (season +0x490..+0x498, 8-byte
// {u32 team key, u32} elements, the list 0x1413cf8a0 fills from order_b; read only) until a scored
// session exists: logged so a test sees the order the screen will show.
void log_start_order(uintptr_t season)
{
    constexpr uintptr_t kConstructorsInitial = 0x490;
    uintptr_t b = 0, e = 0;
    if (!swofty::mem::read(season + kConstructorsInitial, b) || !swofty::mem::read(season + kConstructorsInitial + 8, e) ||
        !b || e <= b || (e - b) % 8 || (e - b) / 8 > 16 || !swofty::mem::readable(b, e - b))
        return;
    std::string out;
    for (uintptr_t p = b; p < e; p += 8) {
        uint32_t key = 0;
        swofty::mem::read(p, key);
        out += (out.empty() ? "" : ", ") + label_or(Kind::team, stat_of(key));
    }
    if (g_order_logs++ < 24)
        swofty::log("[progressive] standings: constructors' start order in the game: %s", out.c_str());
}

} // namespace pg

SWOFTY_WRAP(Career_Rebuild_SeasonStandings, void, void *season, void *order_a, void *order_b, void *extra_points,
            void *extra_e)
{
    swofty::game::NeOptionalVec drivers{}, teams{};
    std::string why;
    if (!season || !order_a || !order_b || !pg::start_order(reinterpret_cast<uintptr_t>(season), drivers, teams, why)) {
        static std::string last;
        if (!why.empty() && why != "not a Progressive career" && why != last && pg::g_order_logs++ < 24) {
            last = why;
            swofty::log("[progressive] standings: the game's order stays (%s)", why.c_str());
        }
        return orig(season, order_a, order_b, extra_points, extra_e);
    }
    orig(season, &drivers, &teams, extra_points, extra_e);
    pg::release(static_cast<swofty::game::NeOptionalVec *>(order_a));
    pg::release(static_cast<swofty::game::NeOptionalVec *>(order_b));
    pg::log_start_order(reinterpret_cast<uintptr_t>(season));
}

extern "C" __declspec(dllexport) int ProgressiveRndMode()
{
    return pg::g_rnd.load();
}

// progressive_teamlogos.dll: the year whose team names a Progressive career shows (2016-2025), 0 when
// the stock 2026 names are shown (2026 careers, every other career and mode).
extern "C" __declspec(dllexport) int ProgressiveNamesYear()
{
    int year = pg::g_names_year;
    return year >= progressive::season::kFirstYear && year < progressive::season::kLastYear ? year : 0;
}

// progressive_rnd.dll: the downforce progress of a team's cars in a Real life career's session
// (development_logic.hpp); team_key is the R&D model's team key (Upgrades +0x0c, stat id << 16).
extern "C" __declspec(dllexport) int ProgressiveDevelopmentProgress(uint32_t team_key, float *progress)
{
    if (!progress)
        return 0;
    std::lock_guard<std::mutex> lock(pg::g_dev_mutex);
    for (auto &[stat, p] : pg::g_dev)
        if (stat == (team_key >> 16)) {
            *progress = p;
            return 1;
        }
    return 0;
}

SWOFTY_ON_CHANGE(test_next_season, bool on)
{
    if (on) {
        pg::g_test_armed = true;
        swofty::log("[progressive] TEST test_next_season on: the next Team HQ of a Progressive career applies the "
                    "following season");
    }
}

SWOFTY_ON_LOAD
{
    if (test_next_season) {
        pg::g_test_armed = true;
        swofty::log("[progressive] TEST test_next_season is on at load: the next Team HQ of a Progressive career "
                    "applies the following season");
    }
}

extern "C" __declspec(dllexport) int ProgressiveFollowMode()
{
    pg::store::Career *c = pg::live_entry();
    return !c ? -1 : c->moves == "follow_driver" ? 1 : 0;
}

extern "C" __declspec(dllexport) int ProgressiveFollowTeam(uint32_t driver_stat, uint32_t *team_stat, int *year)
{
    using namespace progressive;
    pg::store::Career *c = pg::live_entry();
    if (!c || c->moves != "follow_driver" || !pg::data())
        return 0;
    int next = pg::live_year(c) + 1;
    if (year)
        *year = next;
    if (next > season::kLastYear)
        return 0;
    for (auto &p : pg::g_data.pool) {
        uint32_t s = 0;
        int stat = pg::names::stat(pg::names::Kind::driver, p.driver, s) ? static_cast<int>(s) : p.stat;
        if (stat < 0 || static_cast<uint32_t>(stat) != driver_stat)
            continue;
        auto it = p.slot_by_year.find(next);
        if (it == p.slot_by_year.end() || it->second < 1 || it->second > season::kSlots)
            return 0;
        int team = pg::g_data.slots[it->second - 1].team_stat;
        if (team < 0)
            return 0;
        if (team_stat)
            *team_stat = static_cast<uint32_t>(team);
        swofty::log("[progressive] follow: driver %u races for %s in %d", driver_stat,
                    pg::g_data.slots[it->second - 1].team.c_str(), next);
        return 1;
    }
    return 0;
}

// The year a Resume Career card of a Progressive career shows (progressive_resume.dll). The card's
// save slot gives the player's driver and team stat ids and the game's season index; each entry of
// the driver gets the year its career is in at that season index, as live_year works it out for a
// loaded career, and resume_logic.hpp picks among them. 0 when no entry has the driver.
extern "C" __declspec(dllexport) int ProgressiveCardYear(uint32_t driver, int team, int season_index)
{
    using namespace progressive;
    if (!pg::store::load())
        return 0;
    bool data = pg::data();
    if (data)
        pg::names::load();
    bool index_ok = season_index >= 0 && season_index < 64;
    std::vector<resume::Entry> entries;
    for (pg::store::Career *c : pg::store::by_driver(driver)) {
        resume::Entry e;
        e.driver = c->driver;
        e.team = c->team;
        e.season = c->season;
        e.used = c->used;
        e.year = index_ok ? season::year_for(c->start, static_cast<uint32_t>(season_index + std::max(0, c->test_seasons)))
                 : c->applied_season >= 0 && c->applied_year ? c->applied_year
                                                             : season::clamp_year(c->start);
        e.real_team = data && team >= 0 && pg::real_team(driver, e.year) == team;
        entries.push_back(e);
    }
    return resume::card_year(entries, driver, team, index_ok ? season_index : -1);
}

// The team a Resume Career card of a Progressive career shows when its save predates the season
// apply's move of the player (resume_logic.hpp card_team); -1 keeps the slot's own team.
extern "C" __declspec(dllexport) int ProgressiveCardTeam(uint32_t driver, int team, int season_index)
{
    using namespace progressive;
    if (!pg::store::load())
        return -1;
    std::vector<resume::Entry> entries;
    for (pg::store::Career *c : pg::store::by_driver(driver)) {
        resume::Entry e;
        e.driver = c->driver;
        e.team = c->team;
        e.season = c->season;
        e.used = c->used;
        e.follow = c->moves == "follow_driver";
        entries.push_back(e);
    }
    return resume::card_team(entries, driver, team, season_index);
}

extern "C" __declspec(dllexport) void ProgressiveWizardNames(int year)
{
    if (!pg::g_current.empty() || pg::progressive_career())
        return;
    year = progressive::season::clamp_year(year);
    if (pg::g_names_year != year)
        pg::set_names(year, "new-career wizard");
}

SWOFTY_ON_CHANGE(rnd_mode, std::string_view)
{
    pg::g_rnd_changed = true;
}

SWOFTY_ON_LOAD
{
    uint32_t token = 0;
    swofty::events::Result r = swofty::events::on_frame(pg::rnd_frame, nullptr, &token);
    if (r != swofty::events::Result::ok)
        swofty::log("[progressive] R&D: no frame callback (%s); an R&D mode change applies at the next Team HQ",
                    swofty::events::name(r));
}

extern "C" __declspec(dllexport) int ProgressiveRealTeam(uint32_t driver_stat, uint32_t *team_stat, int *year)
{
    using namespace progressive;
    if (!pg::data())
        return 0;
    int now = pg::live_year(pg::live_entry());
    if (year)
        *year = now;
    for (auto &p : pg::g_data.pool) {
        uint32_t st = 0;
        int stat = pg::names::stat(pg::names::Kind::driver, p.driver, st) ? static_cast<int>(st) : p.stat;
        if (stat < 0 || static_cast<uint32_t>(stat) != driver_stat)
            continue;
        auto it = p.slot_by_year.find(now);
        if (it == p.slot_by_year.end() || it->second < 1 || it->second > season::kSlots)
            return 0;
        int team = pg::g_data.slots[it->second - 1].team_stat;
        if (team < 0)
            return 0;
        if (team_stat)
            *team_stat = static_cast<uint32_t>(team);
        swofty::log("[progressive] follow: driver %u raced for %s in %d", driver_stat,
                    pg::g_data.slots[it->second - 1].team.c_str(), now);
        return 1;
    }
    return 0;
}

extern "C" __declspec(dllexport) void ProgressiveNewCareer()
{
    pg::new_career();
}
