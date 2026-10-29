// SPDX-License-Identifier: AGPL-3.0-or-later
//
// years.json as Progressive's DLLs read it on a 1.6 core (records_logic.hpp), on the host: register.cpp
// registers its records into the core's registry (native/src/core/registry, through the SDK fake core),
// a second addon layers swofty.progressive/2017, and the overlaid years.json goes through the same
// parsers and season planner the DLL uses.
//   no layer:    every record read back leaves years.json exactly as it is;
//   a seat:      tails.what-if puts Alonso in the 2017 Mercedes' first seat: the plan seats him there,
//                Hamilton leaves the Mercedes and Alonso's McLaren seat goes to another driver;
//   a rating:    the same addon's pace 96 for Alonso in 2017 reaches the ratings parser;
//   a change:    a layer that drops a 2017 mid-season change drops it from what the planner reads;
//   choose:      the same layer puts Alonso at the Mercedes in Choose Driver's season list (choose_logic.hpp)
//                and takes Hamilton off it; no layer leaves the list as it is;
//   a team:      ig.teampack names its ig_racing for 2016 slot 11 with seats ig_driver + rio_haryanto (the IG6d
//                layer): the apply_teams request carries those seat ids, the plan keeps Haryanto in the record's
//                second seat, and ig_driver takes the first once the career has a row for him; the start
//                season view seats ig_driver (from the driver pool list) and Haryanto on the slot-11 row with
//                its 2016 car, and the slot's logo paths name ig_racing's own key; Choose Driver's season list
//                (the cards' driver keys) has ig_driver in Wehrlein's place with his own look and number;
//   no registry: no record means years.json as it is.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include <swofty/fake_core.hpp>
#include <swofty/storage.hpp>

#include "cards_logic.hpp"
#include "choose_logic.hpp"
#include "ratings_logic.hpp"
#include "records_logic.hpp"
#include "season_logic.hpp"

extern "C" int SwoftyAddonRegister(const SwoftyApi *a);

namespace {

namespace sj = swofty::json;
namespace career = swofty::career;
namespace pr = progressive::records;
namespace season = progressive::season;
namespace lineup = progressive::lineup;

int failures = 0;

void expect(bool ok, const char *what, int line)
{
    if (!ok) {
        std::printf("FAIL line %d: %s\n", line, what);
        ++failures;
    }
}

#define EXPECT(x) expect((x), #x, __LINE__)

std::string g_root;

bool slurp(const std::string &path, std::string &out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    std::ostringstream s;
    s << f.rdbuf();
    out = s.str();
    return true;
}

int st_available() { return 1; }
int st_size(const char *path, uint64_t *out)
{
    std::string s;
    if (!slurp(g_root + "/" + path, s))
        return SWOFTY_ASSET_INVALID;
    *out = s.size();
    return SWOFTY_ASSET_OK;
}
int st_read(const char *path, void *buf, size_t cap, size_t *len)
{
    std::string s;
    if (!slurp(g_root + "/" + path, s) || s.size() > cap)
        return SWOFTY_ASSET_INVALID;
    std::memcpy(buf, s.data(), s.size());
    *len = s.size();
    return SWOFTY_ASSET_OK;
}
int st_write(const char *, const void *, size_t) { return SWOFTY_ASSET_UNAVAILABLE; }
int st_remove(const char *) { return SWOFTY_ASSET_UNAVAILABLE; }
int st_list(const char *, char *, size_t, size_t *) { return SWOFTY_ASSET_UNAVAILABLE; }

const SwoftyStorage kStorage = {
    .available = st_available,
    .read = st_read,
    .write = st_write,
    .append = st_write,
    .remove = st_remove,
    .size = st_size,
    .list = st_list,
};

std::optional<career::Season> season_of(const std::string &id) { return career::seasons.get(career::seasons[id]); }
std::optional<career::DriverRatings> rating_of(const std::string &id) { return career::ratings.get(career::ratings[id]); }

season::Data parse_season(const sj::Value &doc)
{
    season::Data d;
    std::string err;
    if (!season::parse(sj::dump(doc), d, err))
        std::printf("season parse: %s\n", err.c_str());
    return d;
}

season::Data g_data;

int stat(const std::string &label)
{
    const season::PoolDriver *p = g_data.driver(label);
    return p ? p->stat : -1;
}

season::Resolve resolver()
{
    season::Resolve r;
    r.driver_stat = stat;
    r.vehicle_stat = [](const std::string &label) { return 1000 + static_cast<int>(std::hash<std::string>{}(label) % 1000); };
    r.look_known = [](int) { return true; };
    r.rating = [](uint32_t) { return 75.0; };
    return r;
}

std::vector<season::CareerRow> career_rows(const std::string &player)
{
    std::vector<season::CareerRow> rows;
    const season::Year *y = g_data.year(2026);
    std::vector<int> seated;
    for (int i = 0; i < season::kSlots; ++i)
        for (auto &d : y->teams[i].drivers) {
            season::CareerRow r;
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
            season::CareerRow r;
            r.driver = static_cast<uint32_t>(p.stat);
            r.number = p.number > 0 ? static_cast<uint8_t>(p.number) : season::kNoNumber;
            rows.push_back(r);
        }
    return rows;
}

bool in_slot(const season::Plan &p, int slot1, const std::string &driver)
{
    uint32_t s = static_cast<uint32_t>(stat(driver));
    return p.target[slot1 - 1][0] == s || p.target[slot1 - 1][1] == s;
}

void no_layer_reads_years_json_as_it_is(const sj::Value &file)
{
    sj::Value doc = file;
    pr::Summary n = pr::overlay(doc, season_of, rating_of);
    EXPECT(n.seasons == 11);
    EXPECT(n.ratings > 200);
    EXPECT(!n.changed());
    EXPECT(doc == file);
    for (auto &note : n.notes)
        std::printf("  note: %s\n", note.c_str());
}

void what_if_layer_changes_the_2017_seats(const sj::Value &file)
{
    sj::Value doc = file;
    pr::Summary n = pr::overlay(doc, season_of, rating_of);
    EXPECT(n.seats == 1);
    EXPECT(n.emptied == 1);
    EXPECT(n.ratings_changed == 1);
    EXPECT(n.changes == 1);
    g_data = parse_season(doc);
    const season::Year *y = g_data.year(2017);
    EXPECT(y && y->teams[0].drivers.size() == 2);
    if (y && !y->teams[0].drivers.empty())
        EXPECT(y->teams[0].drivers[0].driver == "fernando_alonso" && y->teams[0].drivers[0].look.stat < 0);
    EXPECT(y && y->teams[3].drivers.size() == 1);

    season::Plan p = season::plan(g_data, 2017, career_rows("max_verstappen"), resolver());
    EXPECT(p.error.empty());
    EXPECT(p.target[0][0] == static_cast<uint32_t>(stat("fernando_alonso")));
    EXPECT(in_slot(p, 1, "valtteri_bottas"));
    EXPECT(!in_slot(p, 1, "lewis_hamilton"));
    EXPECT(!in_slot(p, 4, "fernando_alonso"));
    EXPECT(in_slot(p, 4, "stoffel_vandoorne"));
    EXPECT(p.target[3][0] != lineup::kNone);
    bool look = false;
    for (auto &l : p.looks)
        look = look || (l.driver == static_cast<uint32_t>(stat("fernando_alonso")) && l.label == "2017-cs_mclaren_fernando_alonso");
    EXPECT(look);

    season::Data untouched = parse_season(file);
    season::Plan q = season::plan(untouched, 2017, career_rows("max_verstappen"), resolver());
    EXPECT(q.target[0][0] == static_cast<uint32_t>(stat("lewis_hamilton")));
    EXPECT(in_slot(q, 4, "fernando_alonso"));

    progressive::ratings::Data r;
    std::string err;
    EXPECT(progressive::ratings::parse(sj::dump(doc), r, err));
    const progressive::ratings::Values *v = r.find(2017, "fernando_alonso");
    EXPECT(v && std::fabs(v->v[3] - 96.0) < 1e-6);
    EXPECT(v && std::fabs(v->v[0] - 91.5) < 1e-6);
    EXPECT(v && v->overall > 87.0);
    const progressive::ratings::Values *h = r.find(2017, "lewis_hamilton");
    EXPECT(h && std::fabs(h->v[3] - 93.9) < 1e-6);

    EXPECT(g_data.year(2017)->changes.size() + 1 == parse_season(file).year(2017)->changes.size());
    EXPECT(parse_season(doc).year(2016)->changes.size() == parse_season(file).year(2016)->changes.size());
}

struct ListRow {
    uint8_t id_lo = 0;
    uint16_t id_hi = 0;
    uint32_t driver_customisation = progressive::cards::kNoKey;
    uint32_t team_stat_id = progressive::cards::kNoKey;
    uint32_t vehicle_ref = progressive::cards::kNoKey;
    uint32_t vehicle_customisation = progressive::cards::kNoKey;
    uint32_t badge_stat_id = progressive::cards::kNoKey;
    uint16_t team_position = 0x00ff;
    uint8_t car_number = progressive::cards::kNoNumber;
};

std::vector<ListRow> season_list(const season::Data &d, int year)
{
    std::vector<ListRow> rows;
    const season::Year *y = d.year(year);
    for (int i = 0; i < season::kSlots; ++i)
        for (const season::SeatDriver &sd : y->teams[static_cast<size_t>(i)].drivers) {
            const season::PoolDriver *p = d.driver(sd.driver);
            ListRow r;
            std::memset(static_cast<void *>(&r), 0, sizeof r);
            r.vehicle_customisation = progressive::cards::kNoKey;
            r.badge_stat_id = progressive::cards::kNoKey;
            r.id_hi = static_cast<uint16_t>(p->stat);
            r.team_stat_id = static_cast<uint32_t>(d.slots[static_cast<size_t>(i)].team_stat) << 16;
            r.vehicle_ref = static_cast<uint32_t>(5000 + i) << 16;
            r.driver_customisation = static_cast<uint32_t>(sd.look.stat) << 16;
            r.team_position = static_cast<uint16_t>(0x100 | sd.position);
            r.car_number = static_cast<uint8_t>(sd.number);
            rows.push_back(r);
        }
    return rows;
}

const ListRow *find_row(const std::vector<ListRow> &rows, const season::Data &d, const char *label)
{
    for (const ListRow &r : rows)
        if (r.id_hi == d.driver(label)->stat)
            return &r;
    return nullptr;
}

void choose_driver_lists_the_effective_seats(const sj::Value &file)
{
    sj::Value doc = file;
    pr::Summary n = pr::overlay(doc, season_of, rating_of);
    EXPECT(n.changed());
    season::Data base = parse_season(file), eff = parse_season(doc);
    std::vector<ListRow> rows = season_list(base, 2017);
    const size_t before = rows.size();
    const ListRow *alonso0 = find_row(rows, base, "fernando_alonso");
    const uint32_t mclaren_team = alonso0 ? alonso0->team_stat_id : 0;
    const uint32_t mclaren_look = alonso0 ? alonso0->driver_customisation : 0;
    const ListRow *ham = find_row(rows, base, "lewis_hamilton");
    const uint32_t mercedes_team = ham ? ham->team_stat_id : 1;
    const uint32_t mercedes_car = ham ? ham->vehicle_ref : 1;

    progressive::choose::Stats st = progressive::choose::reseat(rows, base, eff, 2017);
    EXPECT(st.changed() && st.moved == 1 && st.removed == 1);
    const ListRow *alonso = find_row(rows, eff, "fernando_alonso");
    EXPECT(alonso && alonso->team_stat_id == mercedes_team && alonso->team_stat_id != mclaren_team);
    EXPECT(alonso && alonso->vehicle_ref == mercedes_car);
    EXPECT(alonso && (alonso->team_position & 0xff) == 0 && (alonso->team_position >> 8));
    EXPECT(alonso && alonso->driver_customisation == mclaren_look);
    EXPECT(!find_row(rows, eff, "lewis_hamilton"));
    const ListRow *bottas = find_row(rows, eff, "valtteri_bottas");
    EXPECT(bottas && bottas->team_stat_id == mercedes_team);
    EXPECT(rows.size() + st.removed == before + st.added);

    std::vector<ListRow> career = season_list(base, 2017);
    std::vector<ListRow> out(rows.size() + 64);
    progressive::cards::Stats cs{};
    progressive::cards::Rows<ListRow> c{career.data(), career.size()}, s{rows.data(), rows.size()}, f{};
    size_t built = progressive::cards::build(c, s, f, out.data(), out.size(), cs);
    bool at_mercedes = false;
    for (size_t i = 0; i < built; ++i)
        if (out[i].id_hi == eff.driver("fernando_alonso")->stat && out[i].team_stat_id == mercedes_team)
            at_mercedes = true;
    EXPECT(at_mercedes);

    std::vector<ListRow> same = season_list(base, 2017);
    EXPECT(!progressive::choose::reseat(same, base, base, 2017).changed());
    std::vector<ListRow> untouched = season_list(base, 2017);
    EXPECT(untouched.size() == same.size() && std::memcmp(untouched.data(), same.data(), same.size() * sizeof(ListRow)) == 0);
    EXPECT(!progressive::choose::reseat(same, base, eff, 2016).changed());
}

void a_layer_adds_teams(const sj::Value &file)
{
    sj::Value doc = file;
    pr::Summary n = pr::overlay(doc, season_of, rating_of);
    EXPECT(n.teams == 2);
    season::Data base = parse_season(file), eff = parse_season(doc);
    EXPECT(!season::names_new_teams(base, 2016));
    EXPECT(season::names_new_teams(eff, 2016));
    EXPECT(!season::names_new_teams(eff, 2017));
    const season::Year *y = eff.year(2016);
    EXPECT(y && y->teams[10].team_id == "ig.teampack/ig_racing");
    EXPECT(y && y->teams[10].drivers.size() == 2);
    EXPECT(y && y->teams[0].team_id.empty());
    EXPECT(y && y->extra_teams.size() == 1 && y->extra_teams[0].slot == 12 &&
           y->extra_teams[0].team_id == "ig.teampack/ig_twelve" && y->extra_teams[0].seats[0] == "game/lando_norris" &&
           y->extra_teams[0].seats[1] == "person/ig_rookie");
    EXPECT(base.year(2016)->extra_teams.empty());
    season::Plan p = season::plan(eff, 2016, career_rows("max_verstappen"), resolver());
    EXPECT(p.error.empty());
    std::vector<season::TeamSlot> req = season::team_slots(eff, p);
    EXPECT(req.size() == 12 && req[10].team == "ig.teampack/ig_racing" && req[11].slot == 12 &&
           req[11].team == "ig.teampack/ig_twelve" && req[11].seats.size() == 2 && req[0].team == "mercedes_26");
    std::vector<season::TeamSlot> plain = season::team_slots(base, season::plan(base, 2016, career_rows("max_verstappen"), resolver()));
    EXPECT(plain.size() == 11 && plain[10].team == base.slots[10].team);
    for (const season::TeamSlot &t : plain)
        EXPECT(t.seats.empty());
    for (size_t i = 0; i < 10; ++i)
        EXPECT(req[i].seats.empty());
    EXPECT(y && y->teams[10].seat_ids[0] == "ig.teampack/ig_driver" &&
           y->teams[10].seat_ids[1] == "swofty.classic-seasons/rio_haryanto");
    EXPECT(y && !season::record_seats(y->teams[0]) && !season::record_seats(base.year(2016)->teams[10]));
    EXPECT(req[10].seats.size() == 2 && req[10].seats[0] == "ig.teampack/ig_driver" &&
           req[10].seats[1] == "swofty.classic-seasons/rio_haryanto");
    EXPECT(p.target[10][1] == static_cast<uint32_t>(stat("rio_haryanto")));
    EXPECT(p.target[10][0] != static_cast<uint32_t>(stat("rio_haryanto")) && p.target[10][0] != lineup::kNone);
    season::Resolve known = resolver();
    known.driver_stat = [](const std::string &label) { return label == "ig_driver" ? 208 : stat(label); };
    std::vector<season::CareerRow> rows = career_rows("max_verstappen");
    season::Plan none = season::plan(eff, 2016, rows, known);
    EXPECT(none.target[10][0] != 208 && none.target[10][1] == static_cast<uint32_t>(stat("rio_haryanto")));
    season::CareerRow ida;
    ida.driver = 208;
    rows.push_back(ida);
    season::Plan seated = season::plan(eff, 2016, rows, known);
    EXPECT(seated.error.empty());
    EXPECT(seated.target[10][0] == 208 && seated.target[10][1] == static_cast<uint32_t>(stat("rio_haryanto")));
    bool moved = false;
    for (const season::Move &m : seated.moves)
        moved |= m.driver == 208 && m.to.slot == 10 && m.to.index == 0;
    EXPECT(moved);
    season::Plan stock = season::plan(base, 2016, rows, known);
    for (int i = 0; i < season::kSlots; ++i)
        for (int k = 0; k < lineup::kSeats; ++k)
            EXPECT(stock.target[i][k] == season::plan(base, 2016, career_rows("max_verstappen"), resolver()).target[i][k]);
    sj::Value once = doc;
    pr::Summary again = pr::overlay(once, season_of, rating_of);
    EXPECT(again.teams == 0);
}

void the_start_view_shows_the_added_team(const sj::Value &file)
{
    sj::Value doc = file;
    pr::Summary n = pr::overlay(doc, season_of, rating_of);
    EXPECT(n.teams == 2);
    season::Data base = parse_season(file), eff = parse_season(doc);
    const uint32_t row486 = static_cast<uint32_t>(base.slots[10].team_stat) << 16;
    const uint32_t car11 = static_cast<uint32_t>(5000 + 10) << 16;
    const int haryanto = stat("rio_haryanto"), wehrlein = stat("pascal_wehrlein");
    EXPECT(base.slots[10].team == "gm_cadillac_26" && base.slots[10].team_stat == 486);

    std::vector<ListRow> plain = season_list(base, 2016);
    progressive::choose::Stats old = progressive::choose::reseat(plain, base, eff, 2016);
    EXPECT(old.added == 0 && old.removed == 1);

    std::vector<ListRow> rows = season_list(base, 2016);
    size_t wehrlein_at = rows.size();
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].id_hi == wehrlein)
            wehrlein_at = i;
    const size_t listed = rows.size();
    auto ig_stat = [](const std::string &label) { return label == "ig_driver" ? 208 : -1; };
    progressive::choose::Stats st = progressive::choose::reseat(rows, base, eff, 2016, ig_stat);
    EXPECT(st.added == 1 && st.removed == 1 && st.moved == 0);
    const ListRow *ida = nullptr;
    for (const ListRow &r : rows)
        if (r.id_hi == 208)
            ida = &r;
    EXPECT(ida && ida->team_stat_id == row486 && ida->vehicle_ref == car11 && ida->team_position == 0x100);
    EXPECT(rows.size() == listed && wehrlein_at < rows.size() && rows[wehrlein_at].id_hi == 208);
    for (const ListRow &r : rows)
        EXPECT(r.id_hi != wehrlein);
    EXPECT(ida && progressive::cards::none(ida->driver_customisation) && ida->car_number == progressive::cards::kNoNumber);

    std::vector<ListRow> career = season_list(base, 2016);
    ListRow pool_row;
    pool_row.id_hi = 208;
    pool_row.driver_customisation = 721u << 16;
    pool_row.car_number = 77;
    std::vector<ListRow> keys = rows;
    progressive::choose::Filled filled =
        progressive::choose::fill(keys, {progressive::cards::Rows<ListRow>{career.data(), career.size()},
                                         progressive::cards::Rows<ListRow>{&pool_row, 1}});
    EXPECT(filled.looks == 1 && filled.numbers == 1);
    EXPECT(keys[wehrlein_at].id_hi == 208 && keys[wehrlein_at].driver_customisation == (721u << 16) &&
           keys[wehrlein_at].car_number == 77 && keys[wehrlein_at].team_stat_id == row486);
    for (size_t i = 0; i < keys.size(); ++i)
        if (i != wehrlein_at)
            EXPECT(std::memcmp(&keys[i], &rows[i], sizeof(ListRow)) == 0);
    std::vector<ListRow> out(career.size() + 64);
    progressive::cards::Rows<ListRow> c{career.data(), career.size()}, s{rows.data(), rows.size()}, f{}, pool{&pool_row, 1};
    progressive::cards::Stats cs{};
    size_t built = progressive::cards::build(c, s, f, out.data(), out.size(), cs, pool);
    EXPECT(cs.pooled == 1 && cs.missing == 0 && cs.filled == 0);
    std::vector<const ListRow *> seats;
    for (size_t i = 0; i < built; ++i)
        if (progressive::cards::seated(out[i]) && progressive::cards::same_team(out[i].team_stat_id, row486))
            seats.push_back(&out[i]);
    EXPECT(seats.size() == 2);
    if (seats.size() == 2) {
        EXPECT(seats[0]->id_hi == 208 && (seats[0]->team_position & 0xff) == 0 && seats[0]->vehicle_ref == car11 &&
               seats[0]->driver_customisation == (721u << 16) && seats[0]->car_number == 77);
        EXPECT(seats[1]->id_hi == haryanto && (seats[1]->team_position & 0xff) == 1 && seats[1]->vehicle_ref == car11);
    }
    for (size_t i = 0; i < built; ++i)
        EXPECT(out[i].id_hi != wehrlein || !progressive::cards::seated(out[i]));

    std::vector<ListRow> out2(career.size() + 64);
    progressive::cards::Stats no_pool{};
    progressive::cards::build(c, s, f, out2.data(), out2.size(), no_pool);
    EXPECT(no_pool.pooled == 0 && no_pool.missing == 1);

    std::vector<ListRow> same = season_list(base, 2016);
    EXPECT(!progressive::choose::reseat(same, base, base, 2016, ig_stat).changed());
    std::vector<ListRow> other = season_list(base, 2017), other_plain = season_list(base, 2017);
    progressive::choose::Stats o = progressive::choose::reseat(other, base, eff, 2017, ig_stat);
    progressive::choose::Stats op = progressive::choose::reseat(other_plain, base, eff, 2017);
    EXPECT(o.added == op.added && o.moved == op.moved && o.removed == op.removed && other.size() == other_plain.size());

    std::vector<std::string> missing;
    ::json::Value years;
    EXPECT(::json::parse(sj::dump(doc), years));
    std::vector<progressive::cards::LogoMap> map = progressive::cards::logo_map(years, missing);
    auto resolve = [&](const season::Data &d, int year, const char *path) {
        std::string to;
        auto key_of = [&](const std::string &label) { return season::added_team_logo(d, year, label); };
        if (progressive::cards::added_logo_path(path, key_of, to) || progressive::cards::logo_path(map, year, path, to))
            return to;
        return std::string(path);
    };
    EXPECT(season::added_team_logo(eff, 2016, "gm_cadillac_26") == "ig_racing");
    EXPECT(resolve(eff, 2016, "#overrides.teams.driver_select_team_logos.gm_cadillac_26") ==
           "#overrides.teams.driver_select_team_logos.ig_racing");
    EXPECT(resolve(eff, 2016, "overrides.team_logos.driver_career_2024_team_select.gm_cadillac_26") ==
           "overrides.team_logos.driver_career_2024_team_select.ig_racing");
    EXPECT(resolve(eff, 2016, "#overrides.teams.badges_broadcast.gm_cadillac_26") == "#overrides.teams.badges_broadcast.ig_racing");
    EXPECT(resolve(eff, 2016, "overrides.osd.timing_tower.team_badges_hud.gm_cadillac_26") ==
           "overrides.osd.timing_tower.team_badges_hud.cs16_manor");
    EXPECT(resolve(eff, 2016, "#overrides.teams.driver_select_team_logos.mercedes_26") ==
           "#overrides.teams.driver_select_team_logos.cs16_mercedes");
    EXPECT(resolve(eff, 2017, "#overrides.teams.driver_select_team_logos.gm_cadillac_26") ==
           resolve(base, 2017, "#overrides.teams.driver_select_team_logos.gm_cadillac_26"));
    EXPECT(resolve(base, 2016, "#overrides.teams.driver_select_team_logos.gm_cadillac_26") ==
           "#overrides.teams.driver_select_team_logos.cs16_manor");
    EXPECT(resolve(eff, 2026, "#overrides.teams.driver_select_team_logos.gm_cadillac_26") ==
           "#overrides.teams.driver_select_team_logos.gm_cadillac_26");
}

void no_registry_changes_nothing(const sj::Value &file)
{
    sj::Value doc = file;
    pr::Summary n = pr::overlay(
        doc, [](const std::string &) { return std::optional<career::Season>(); },
        [](const std::string &) { return std::optional<career::DriverRatings>(); });
    EXPECT(n.seasons == 0 && n.ratings == 0 && !n.changed());
    EXPECT(doc == file);
}

}

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::printf("records_test <addon folder>\n");
        return EXIT_FAILURE;
    }
    g_root = argv[1];
    std::string text;
    if (!slurp(g_root + "/data/years.json", text)) {
        std::printf("records_test: no data/years.json in %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    const sj::Value file = sj::parse(text);

    {
        swofty::fake::Core core{"swofty.progressive"};
        const SwoftyFakeEngine *engine = swofty::fake::Core::default_engine();
        SwoftyApi api = core.api();
        api.storage = &kStorage;
        swofty::api = &api;
        engine->begin_addon("swofty.progressive");
        SwoftyAddonRegister(&api);
        no_layer_reads_years_json_as_it_is(file);

        engine->begin_addon("tails.what-if");
        career::seasons.layer(career::seasons["swofty.progressive/2017"], [](career::Season &s) {
            if (auto *team = s.teams.find(1))
                team->seats = {swofty::Ref<swofty::drivers::Identity>("game/fernando_alonso"),
                               swofty::Ref<swofty::drivers::Identity>("game/valtteri_bottas")};
            if (!s.changes.empty())
                s.changes.items().erase(s.changes.items().begin());
        }, swofty::Scope::profile);
        career::ratings.layer(career::ratings["swofty.progressive/2017/game/fernando_alonso"],
                              [](career::DriverRatings &r) { r.pace = 96; }, swofty::Scope::profile);
        engine->end_register();
        what_if_layer_changes_the_2017_seats(file);
        choose_driver_lists_the_effective_seats(file);

        engine->begin_addon("ig.teampack");
        career::seasons.layer(career::seasons["swofty.progressive/2016"], [](career::Season &s) {
            if (auto *t = s.teams.find(11)) {
                t->team = swofty::Ref<swofty::teams::Identity>("ig.teampack/ig_racing");
                t->seats = {swofty::Ref<swofty::drivers::Identity>("ig.teampack/ig_driver"),
                            swofty::Ref<swofty::drivers::Identity>("swofty.classic-seasons/rio_haryanto")};
            }
            career::SeasonTeam extra;
            extra.slot = 12;
            extra.team = swofty::Ref<swofty::teams::Identity>("ig.teampack/ig_twelve");
            extra.seats = {swofty::Ref<swofty::drivers::Identity>("game/lando_norris"),
                           swofty::Ref<swofty::drivers::Identity>("person/ig_rookie")};
            s.teams.push_back(extra);
        }, swofty::Scope::profile);
        engine->end_register();
        a_layer_adds_teams(file);
        the_start_view_shows_the_added_team(file);
        swofty::api = nullptr;
    }
    no_registry_changes_nothing(file);

    if (failures) {
        std::printf("records_test: %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("records_test: ok (no layer: years.json as it is; a 2017 layer moves Alonso into the Mercedes, "
                "his pace to 96 and drops a change; a 2016 layer's own team keeps its drivers in their record seats and shows them and its logo in the start "
                "season view)\n");
    return EXIT_SUCCESS;
}
