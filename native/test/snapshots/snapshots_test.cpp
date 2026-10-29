// Host test for career snapshots (native/src/core/snapshots/): the swofty_registry entry's format and
// bag envelope, freezes and reads through the registry store, budgets, unknown majors and items kept
// byte for byte, migration through an adapter, a career from before 1.6, Freeze::at_creation, corrupt
// input, and the entry spliced into a profile save's containers block with the save splice's own
// logic; the season clock (clock.hpp): the year of a season index from the career.season records, the
// owner-only career::report_season, career.created after a new-career route, career.season.start once per
// career and year, and the dry save default. --generate DIR writes the fixture entries of packages/sdk/std/fixtures; --fixtures DIR checks
// every fixture there loads and reads as its .json says. run_snapshots_test.sh builds and runs it.
#include "../../src/core/save_splice_logic.hpp"
#include "../../src/core/snapshots/clock.hpp"
#include "../../src/core/snapshots/snapshots.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

namespace snap = swofty_core::snapshots;
namespace fmt = swofty_core::snapshots::format;
namespace reg = swofty_core::registry;
namespace sp = runtime::save_splice;
namespace sn = swofty_core::snapshots::seasons;
using swofty::json::Value;

static int fails;
static std::vector<std::string> logs;

static void expect(bool ok, const std::string &what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
    fails += !ok;
}

static bool logged(const std::string &needle)
{
    for (auto &l : logs)
        if (l.find(needle) != std::string::npos)
            return true;
    return false;
}

static Value parse(const std::string &s)
{
    swofty::json::Error e;
    Value v = swofty::json::parse(s, &e);
    if (!e.message.empty())
        fprintf(stderr, "bad json in test: %s\n", s.c_str());
    return v;
}

static void declare(reg::Store &st, const std::string &name, bool snapshot, int budget = 65536,
                    const char *version = "1.0")
{
    Value schema = parse(R"({"type":"object"})");
    schema["x-version"] = Value(std::string(version));
    if (snapshot) {
        schema["x-snapshot"] = Value(true);
        schema["x-snapshot-budget"] = Value(int64_t(budget));
    }
    st.declare(name, schema, true);
}

static void world(reg::Store &st, const char *season_version = "1.0")
{
    st.set_log([](const std::string &s) { logs.push_back(s); });
    declare(st, "career.season", true, 65536, season_version);
    declare(st, "career.driver_ratings", true);
    declare(st, "tails.whatif.presets", true, 16384);
    declare(st, "vehicle", false);
    for (int y : {2016, 2017, 2018}) {
        st.add("swofty.progressive", "career.season", std::to_string(y),
               parse(R"({"data_group":"f1_career_progressive","year":)" + std::to_string(y) +
                     R"(,"teams":[{"slot":1,"seats":["game/a","game/b"]}]})"),
               nullptr);
        st.add("swofty.progressive", "career.driver_ratings", std::to_string(y) + "/game/a",
               parse(R"({"season":"swofty.progressive/)" + std::to_string(y) + R"(","driver":"game/a","pace":)" +
                     std::to_string(y - 1930) + "}"),
               nullptr);
    }
    st.add("other.mode", "career.season", "2016",
           parse(R"({"data_group":"f1_career_other","year":2016,"teams":[]})"), nullptr);
    st.add("tails.whatif", "tails.whatif.presets", "classic", parse(R"({"name":"classic","start_year":2016})"), nullptr);
    st.add("game", "vehicle", "redbull", parse(R"({"livery":"stock"})"), nullptr);
}

static uint32_t layer(reg::Store &st, const std::string &addon, const std::string &registry, const std::string &id,
                      const std::string &patch, reg::Scope scope = reg::Scope::global)
{
    reg::LayerSpec L;
    L.addon = addon, L.registry = registry, L.id = id, L.scope = scope;
    L.change.is_patch = true;
    L.change.patch = parse(patch);
    uint32_t h = 0;
    st.layer(L, &h);
    return h;
}

static Value get(reg::Store &st, const std::string &registry, const std::string &id)
{
    reg::Record r = st.get(registry, id);
    return r.status == reg::Status::ok ? r.value : Value();
}

static int64_t pace(reg::Store &st, int year)
{
    return get(st, "career.driver_ratings", "swofty.progressive/" + std::to_string(year) + "/game/a")["pace"].as_int(-1);
}

static std::string seat(reg::Store &st, int year)
{
    Value v = get(st, "career.season", "swofty.progressive/" + std::to_string(year));
    return std::string(v["teams"][0]["seats"][0].as_string());
}

static reg::Context career_ctx()
{
    reg::Context c;
    c.data_group = "f1_career_progressive";
    return c;
}

static fmt::Entry sample_entry()
{
    fmt::Entry e;
    fmt::Career c;
    c.id = "dc-0000002a";
    c.majors = R"({"career.season":"1.0"})";
    c.idmap = R"({"career.mode/swofty.progressive/progressive":82})";
    fmt::Season s;
    s.year = 2016;
    fmt::Frozen f;
    f.registry = "career.season";
    fmt::compress(R"({"records":{"swofty.progressive/2016":{"year":2016}},"loose":[],"ranged":[]})", f.records);
    s.frozen.push_back(f);
    c.seasons.push_back(s);
    e.careers.push_back(c);
    return e;
}

static void format_tests()
{
    fmt::Entry e = sample_entry();
    fmt::Bytes a = e.encode();
    fmt::Entry back;
    std::string why;
    expect(fmt::decode(a, back, why) == fmt::Decoded::ok, "an entry decodes");
    expect(back.encode() == a, "an entry round-trips byte for byte");
    expect(back.careers.size() == 1 && back.careers[0].id == "dc-0000002a" &&
               back.careers[0].idmap == e.careers[0].idmap && back.careers[0].seasons[0].frozen[0].registry ==
                                                                  "career.season",
           "the round trip keeps the career id, its stat-id map and its season snapshot");

    fmt::Entry u = sample_entry();
    u.extra.push_back({77, {1, 2, 3}});
    u.careers[0].extra.push_back({66, {4, 5}});
    u.careers[0].seasons[0].extra.push_back({55, {6}});
    u.careers[0].seasons[0].frozen[0].extra.push_back({44, {7, 8, 9}});
    fmt::Frozen newer;
    newer.registry = "career.season.next";
    newer.major = 9, newer.minor = 3;
    newer.records = {0xde, 0xad, 0xbe, 0xef};
    u.careers[0].seasons[0].frozen.push_back(newer);
    fmt::Bytes ub = u.encode();
    fmt::Entry ur;
    expect(fmt::decode(ub, ur, why) == fmt::Decoded::ok && ur.encode() == ub,
           "unknown items at every level and an unknown registry of major 9 are written back byte for byte");

    fmt::Bytes body, payload;
    expect(fmt::bag_body(a, body) && fmt::parse_bag(body, payload, why) && payload == a,
           "the bag envelope carries the payload");
    expect(body.size() >= 16 && body[0] == 1 && body[8] == 2, "the envelope has the codec header and its entry count");

    std::string big(200000, 'x');
    std::mt19937 rng(7);
    for (auto &ch : big)
        ch = char('a' + rng() % 26);
    fmt::Entry large = sample_entry();
    large.careers[0].seasons[0].frozen[0].records.assign(big.begin(), big.end());
    fmt::Bytes lb = large.encode(), lbody, lpay;
    expect(fmt::bag_body(lb, lbody) && fmt::parse_bag(lbody, lpay, why) && lpay == lb,
           "a payload over several chunks reads back");

    fmt::Bytes b64;
    expect(fmt::unbase64(fmt::base64({1, 2, 3, 4, 5}), b64) && b64 == fmt::Bytes{1, 2, 3, 4, 5}, "base64 round trip");
    expect(!fmt::unbase64("ab=c", b64) && !fmt::unbase64("abc", b64), "malformed base64 is refused");

    fmt::Bytes future = a;
    future[4] = 2;
    fmt::Entry fe;
    expect(fmt::decode(future, fe, why) == fmt::Decoded::newer, "a newer entry format is reported as newer");
    fmt::Bytes flipped = a;
    flipped.back() ^= 0x40;
    expect(fmt::decode(flipped, fe, why) == fmt::Decoded::corrupt, "a flipped byte fails the checksum");
}

static void provisional_save_tests()
{
    logs.clear();
    reg::Store st;
    world(st);
    snap::Book book(st, [](const std::string &s) { logs.push_back(s); });
    st.set_snapshot_source(book.source());
    book.loaded(nullptr);
    book.career_loaded("driver_career:new1", career_ctx());
    book.season_start(2016, career_ctx());
    fmt::Bytes body;
    book.saving(body);
    expect(logged("1 career(s) still on a provisional id are left out of this save"),
           "a career still on a provisional id is not written with the save");
}

static void freeze_tests()
{
    logs.clear();
    reg::Store st;
    world(st);
    snap::Book book(st, [](const std::string &s) { logs.push_back(s); });
    st.set_snapshot_source(book.source());
    book.loaded(nullptr);
    expect(logged("this profile save has none"), "a save without swofty_registry is a pre-1.6 profile");
    book.career_loaded("dc-1", career_ctx());
    expect(logged("no snapshot in swofty_registry"), "a career with no entry reads live and is logged");
    expect(seat(st, 2016) == "game/a", "before any freeze the career reads live");

    book.season_start(2016, career_ctx());
    expect(logged("career season 2016 starts in dc-1"), "the season start logs its snapshot");
    layer(st, "tails.whatif", "career.season", "swofty.progressive/2016", R"({"teams":[{"slot":1,"seats":["game/z","game/b"]}]})");
    layer(st, "tails.whatif", "career.season", "swofty.progressive/2017", R"({"teams":[{"slot":1,"seats":["game/y","game/b"]}]})");
    layer(st, "tails.whatif", "career.driver_ratings", "swofty.progressive/2016/game/a", R"({"pace":1})");
    layer(st, "tails.whatif", "tails.whatif.presets", "tails.whatif/classic", R"({"start_year":1990})");
    expect(seat(st, 2016) == "game/a", "a change to a frozen season does not reach the career");
    expect(pace(st, 2016) == 86, "frozen ratings keep their value");
    expect(seat(st, 2017) == "game/y", "a season the career has not reached stays live");
    expect(get(st, "tails.whatif.presets", "tails.whatif/classic")["start_year"].as_int() == 2016,
           "a record with no year is frozen at the season start");
    expect(get(st, "vehicle", "game/redbull")["livery"].as_string() == "stock", "a registry without a snapshot reads live");
    expect(!get(st, "career.season", "other.mode/2016").is_null(), "another mode's season still reads (live)");
    fmt::Entry e = book.entry();
    const fmt::Season *s16 = e.careers.empty() ? nullptr : e.careers[0].season(2016);
    bool other = false;
    if (s16)
        if (auto *f = s16->find("career.season")) {
            std::string raw;
            fmt::decompress(f->records, raw);
            other = raw.find("other.mode") != std::string::npos;
        }
    expect(s16 && s16->frozen.size() == 3 && !other,
           "season 2016 froze the three snapshot registries and left out another mode's season");

    layer(st, "tails.whatif", "career.season", "swofty.progressive/2016", R"({"teams":[{"slot":1,"seats":["game/c","game/b"]}]})",
          reg::Scope::career);
    expect(seat(st, 2016) == "game/c", "a career layer applies on top of the snapshot");

    fmt::Bytes body;
    expect(book.saving(body), "a freeze makes the entry due at the next profile save");
    book.written(false);
    fmt::Bytes again;
    expect(book.saving(again) && again == body, "a failed write keeps the entry due");
    book.written(true);
    fmt::Bytes none;
    expect(!book.saving(none), "after a write nothing is due until the next change");

    book.career_unloaded();
    expect(seat(st, 2016) == "game/z", "with no career loaded, reads are live");

    logs.clear();
    snap::Book reload(st, [](const std::string &s) { logs.push_back(s); });
    st.set_snapshot_source(reload.source());
    reload.loaded(&body);
    expect(logged("read 1 career(s), 1 season snapshot(s)"), "the reloaded profile reads the entry");
    reload.career_loaded("dc-1", career_ctx());
    expect(logged("1 season snapshot(s) for 2016-2016"), "the career's load logs its snapshot years");
    expect(seat(st, 2016) == "game/c", "after a reload the career keeps its 2016 snapshot (and its career layer)");
    expect(pace(st, 2016) == 86, "and its 2016 ratings");
    reload.season_start(2017, career_ctx());
    expect(seat(st, 2017) == "game/y", "a career advanced to 2017 gets the change made before 2017 started");
    layer(st, "tails.whatif", "career.season", "swofty.progressive/2017", R"({"teams":[{"slot":1,"seats":["game/q","game/b"]}]})");
    expect(seat(st, 2017) == "game/y", "and keeps it once 2017 is frozen");
    reload.season_start(2017, career_ctx());
    expect(logged("already frozen"), "a second season start for a frozen season keeps it");

    logs.clear();
    reload.career_unloaded();
    reload.career_loaded("dc-2", career_ctx());
    expect(seat(st, 2016) == "game/z", "another career reads live: snapshots belong to one career");
}

static int up_season(void *, SwoftyText in, char *out, size_t cap, size_t *len)
{
    Value v = parse(std::string(in.data, in.size));
    Value seats = v["teams"][0]["seats"];
    v["teams"][0]["seats"] = Value::array({Value::object({{"driver", seats[0]}}), Value::object({{"driver", seats[1]}})});
    std::string s = swofty::json::dump(v);
    *len = s.size();
    if (s.size() > cap)
        return SWOFTY_TOO_SMALL;
    memcpy(out, s.data(), s.size());
    return SWOFTY_ASSET_OK;
}

static void migration_tests()
{
    logs.clear();
    fmt::Bytes body;
    {
        reg::Store st;
        world(st);
        snap::Book book(st);
        st.set_snapshot_source(book.source());
        book.loaded(nullptr);
        book.career_loaded("dc-1", career_ctx());
        book.season_start(2016, career_ctx());
        book.saving(body);
    }
    {
        reg::Store st;
        world(st, "2.0");
        st.adapter("swofty.core", "career.season", 1, 2, up_season, nullptr, nullptr);
        snap::Book book(st, [](const std::string &s) { logs.push_back(s); });
        st.set_snapshot_source(book.source());
        book.loaded(&body);
        book.career_loaded("dc-1", career_ctx());
        Value v = get(st, "career.season", "swofty.progressive/2016");
        expect(v["teams"][0]["seats"][0]["driver"].as_string() == "game/a",
               "a snapshot of schema 1 is upgraded to schema 2 through the adapter on read");
    }
    {
        reg::Store st;
        world(st, "2.0");
        st.set_log([](const std::string &s) { logs.push_back(s); });
        snap::Book book(st);
        st.set_snapshot_source(book.source());
        book.loaded(&body);
        book.career_loaded("dc-1", career_ctx());
        layer(st, "x", "career.season", "swofty.progressive/2016", R"({"teams":[{"slot":1,"seats":["game/m","game/b"]}]})");
        expect(seat(st, 2016) == "game/m" && logged("has no migration to 2"),
               "with no adapter an older major reads live, with a log line");
    }
    {
        reg::Store st;
        world(st, "1.0");
        snap::Book writer(st);
        st.set_snapshot_source(writer.source());
        writer.loaded(nullptr);
        writer.career_loaded("dc-1", career_ctx());
        writer.season_start(2016, career_ctx());
        fmt::Entry e = writer.entry();
        for (auto &f : e.careers[0].seasons[0].frozen)
            if (f.registry == "career.season")
                f.major = 3;
        fmt::Bytes newer;
        fmt::bag_body(e.encode(), newer);
        logs.clear();
        snap::Book book(st, [](const std::string &s) { logs.push_back(s); });
        st.set_snapshot_source(book.source());
        book.loaded(&newer);
        book.career_loaded("dc-1", career_ctx());
        layer(st, "x", "career.season", "swofty.progressive/2016", R"({"teams":[{"slot":1,"seats":["game/n","game/b"]}]})");
        expect(seat(st, 2016) == "game/n" && logged("newer than this core's 1; kept in the save, read live"),
               "a snapshot of an unknown (newer) major reads live with a log line");
        book.season_start(2017, career_ctx());
        fmt::Bytes out, payload;
        std::string why;
        book.saving(out);
        fmt::parse_bag(out, payload, why);
        fmt::Entry back;
        fmt::decode(payload, back, why);
        bool kept = false;
        for (auto &s : back.careers[0].seasons)
            if (auto *f = s.find("career.season"); s.year == 2016 && f)
                kept = f->major == 3 && f->records == e.careers[0].seasons[0].find("career.season")->records;
        expect(kept, "and its bytes are written back unchanged with the next save");
    }
}

static void budget_tests()
{
    logs.clear();
    reg::Store st;
    world(st);
    declare(st, "tails.tiny", true, 32);
    std::mt19937 rng(11);
    auto noise = [&](size_t n) {
        std::string s(n, 'a');
        for (auto &c : s)
            c = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"[rng() % 62];
        return s;
    };
    st.add("tails", "tails.tiny", "a", parse(R"({"year":2016,"blob":")" + noise(400) + R"("})"), nullptr);
    declare(st, "tails.big", true, 1 << 20);
    for (int y = 2016; y <= 2018; y++)
        st.add("tails", "tails.big", std::to_string(y), parse(R"({"year":)" + std::to_string(y) + R"(,"blob":")" + noise(130000) + R"("})"),
               nullptr);
    snap::Book book(st, [](const std::string &s) { logs.push_back(s); });
    st.set_snapshot_source(book.source());
    book.loaded(nullptr);
    book.career_loaded("dc-1", career_ctx());
    book.season_start(2016, career_ctx());
    expect(logged("tails.tiny 2016: snapshot") && logged("budget 32 B; not saved, this career reads it live for 2016"),
           "a registry over its budget is not saved for that season, with a log line");
    bool status = false;
    for (auto &r : st.refusals())
        status |= r.addon == "tails" && r.reason == "snapshot_budget";
    expect(status, "and a status line for the records' owner");
    layer(st, "x", "tails.tiny", "tails/a", R"({"blob":"changed"})");
    expect(get(st, "tails.tiny", "tails/a")["blob"].as_string() == "changed", "the refused registry reads live");
    book.season_start(2017, career_ctx());
    book.season_start(2018, career_ctx());
    expect(logged("tails.big 2018: snapshot") && logged("would take the career's entry past 256.0 KB"),
           "the season that would take a career past 256 KB is refused");
    fmt::Entry e = book.entry();
    expect(e.careers[0].size() <= fmt::CAREER_CAP, "the career's entry stays within 256 KB");
    expect(e.careers[0].season(2018) && !e.careers[0].season(2018)->find("tails.big") &&
               e.careers[0].season(2018)->find("career.season"),
           "the newest season's big registry is left out and the others are kept");
}

static void creation_tests()
{
    logs.clear();
    reg::Store st;
    world(st);
    snap::Book book(st, [](const std::string &s) { logs.push_back(s); });
    st.set_snapshot_source(book.source());
    book.loaded(nullptr);
    book.career_created("dc-9", fmt::Freeze::at_creation, career_ctx());
    expect(logged("froze 3 season(s) 2016-2018"), "Freeze::at_creation freezes every season at creation");
    layer(st, "x", "career.season", "swofty.progressive/2018", R"({"teams":[{"slot":1,"seats":["game/w","game/b"]}]})");
    layer(st, "x", "tails.whatif.presets", "tails.whatif/classic", R"({"start_year":2001})");
    expect(seat(st, 2018) == "game/a", "a later change does not reach any season of that career");
    expect(get(st, "tails.whatif.presets", "tails.whatif/classic")["start_year"].as_int() == 2016,
           "nor its records with no year");
    book.season_start(2017, career_ctx());
    expect(logged("frozen at creation; nothing new to freeze"), "its season starts freeze nothing new");
    fmt::Bytes body;
    expect(book.saving(body), "the created career is due at the next save");
}

static void add_season(reg::Store &st, const std::string &addon, const std::string &key, const std::string &group,
                       int year)
{
    st.add(addon, "career.season", key,
           parse(R"({"data_group":")" + group + R"(","year":)" + std::to_string(year) + R"(,"teams":[]})"), nullptr);
}

static bool said(const sn::Moment &m, const std::string &needle)
{
    for (auto &l : m.lines)
        if (l.find(needle) != std::string::npos)
            return true;
    return false;
}

static void year_map_tests()
{
    reg::Store st;
    st.set_log([](const std::string &) {});
    declare(st, "career.season", true);
    for (int y = 2016; y <= 2026; y++)
        add_season(st, "swofty.progressive", std::to_string(y), "f1_career_progressive", y);
    add_season(st, "game", "f1_2025", "f1_career_2025", 2025);
    add_season(st, "gap.addon", "2016", "f1_career_gap", 2016);
    add_season(st, "gap.addon", "2017", "f1_career_gap", 2017);
    add_season(st, "gap.addon", "2019", "f1_career_gap", 2019);
    add_season(st, "dup.addon", "a", "f1_career_dup", 2020);
    add_season(st, "dup.addon", "b", "f1_career_dup", 2020);
    sn::YearMap pg = sn::year_map(st, "f1_career_progressive");
    std::string why;
    expect(pg.why.empty() && pg.years.size() == 11, "Progressive's 11 seasons map without a gap");
    expect(sn::year_at(pg, 0, why) == 2016 && sn::year_at(pg, 10, why) == 2026, "index 0 is 2016 and index 10 is 2026");
    expect(sn::year_at(pg, 11, why) == 0 && why.find("past the 11 career.season record(s)") != std::string::npos,
           "an index past the last season has no year: " + why);
    layer(st, "tails.whatif", "career.season", "swofty.progressive/2026", R"({"year":2027})");
    sn::YearMap moved = sn::year_map(st, "f1_career_progressive");
    expect(moved.why.find("skip from 2025 to 2027") != std::string::npos,
           "the effective records count, with every addon's layers: " + moved.why);
    st.clear_layer("tails.whatif", "career.season", "swofty.progressive/2026", reg::Scope::global);

    sn::YearMap stock = sn::year_map(st, "f1_career_2025");
    expect(sn::year_at(stock, 0, why) == 2025, "the stock 2025 data group maps its season 0 to 2025");
    expect(sn::year_at(stock, 1, why) == 0, "and has no year for its second season");

    sn::YearMap gap = sn::year_map(st, "f1_career_gap");
    expect(sn::year_at(gap, 0, why) == 0 && why.find("skip from 2017 to 2019") != std::string::npos,
           "a gap between years leaves every index without a year: " + why);
    sn::YearMap dup = sn::year_map(st, "f1_career_dup");
    expect(!dup.why.empty() && dup.why.find("two career.season records") != std::string::npos,
           "two records for one year leave it ambiguous");
    sn::YearMap none = sn::year_map(st, "f1_career_2026");
    expect(none.why.find("no career.season record has data group f1_career_2026") != std::string::npos,
           "a data group without records has no years");
}

static void clock_tests()
{
    reg::Store st;
    st.set_log([](const std::string &) {});
    declare(st, "career.season", true);
    for (int y = 2016; y <= 2026; y++)
        add_season(st, "swofty.progressive", std::to_string(y), "f1_career_progressive", y);
    add_season(st, "game", "f1_2025", "f1_career_2025", 2025);
    sn::YearMap pg = sn::year_map(st, "f1_career_progressive");
    sn::YearMap stock = sn::year_map(st, "f1_career_2025");

    sn::Clock clock;
    clock.starting(true);
    expect(clock.loaded("dc-1"), "the first career loaded after a new-career route is created");
    expect(!clock.loaded("dc-1"), "loading it again creates nothing");
    clock.starting(false);
    expect(!clock.loaded("dc-2"), "a continued career is not created");
    clock.starting(true);
    clock.unloaded();
    expect(!clock.loaded("dc-3"), "a new-career route that ends without a load creates nothing later");

    sn::Season s{"stock-1", "f1_career_2025", "", 0, false, "the season has no race weekends yet"};
    sn::Moment m = clock.hub(s, stock);
    expect(!m.fire && said(m, "not ready (the season has no race weekends yet)"), "no season start before the weekends");
    s.ready = true;
    m = clock.hub(s, stock);
    expect(m.fire && m.year == 2025 && m.index == 0, "a stock career no addon owns starts 2025 from its records");
    m = clock.hub(s, stock);
    expect(!m.fire, "the next Team HQ of the same season fires nothing");
    s.index = 1;
    m = clock.hub(s, stock);
    expect(!m.fire && said(m, "past the 1 career.season record(s)"), "a season past the records fires nothing");
    m = clock.hub(s, stock);
    expect(!m.fire && m.lines.empty(), "and says so once");

    sn::Season p{"pg-1", "f1_career_progressive", "swofty.progressive", 0, true, ""};
    m = clock.hub(p, pg);
    expect(!m.fire && said(m, "takes its year from swofty.progressive's career::report_season (the records put 2016 here)"),
           "an owned career waits for its owner's report");
    sn::Report r = clock.report("tails.whatif", p, 2016, pg, m);
    expect(r == sn::Report::refused && !m.fire && said(m, "only swofty.progressive"), "another addon's report is refused");
    r = clock.report("swofty.progressive", sn::Season{"pg-1", "f1_career_2025", "", 0, true, ""}, 2025, stock, m);
    expect(r == sn::Report::refused && said(m, "no addon owns data group f1_career_2025"),
           "nobody reports a career no addon owns");
    r = clock.report("swofty.progressive", sn::Season{}, 2016, pg, m);
    expect(r == sn::Report::late, "a report with no career loaded is late");
    {
        sn::Clock fresh;
        sn::Season none{"", "f1_career_progressive", "swofty.progressive", 0, false, ""};
        r = fresh.report("swofty.progressive", none, 2016, pg, m);
        expect(r == sn::Report::late && !m.fire && fresh.queued() && said(m, "queued until the career has an id"),
               "a new career's report with no id yet is late and queued");
        sn::Season idle{"", "f1_career_progressive", "swofty.progressive", 0, true, ""};
        m = fresh.hub(idle, pg);
        expect(!m.fire && fresh.queued(), "a Team HQ with no id replays nothing");
        sn::Season committed{"f1_career_progressive_save:new1", "f1_career_progressive", "swofty.progressive", 0, true, ""};
        m = fresh.hub(committed, pg);
        expect(m.fire && m.year == 2016 && m.index == 0 && !fresh.queued() && said(m, "queued career::report_season(2016, 0)"),
               "the queued report fires career.season.start once the id exists");
        m = fresh.hub(committed, pg);
        expect(!m.fire, "and only once");
        r = fresh.report("swofty.progressive", committed, 2016, pg, m);
        expect(r == sn::Report::ok && !m.fire && said(m, "already started"), "the owner's retry afterwards starts nothing again");
        fresh.rename("f1_career_progressive_save:new1", "f1_career_progressive_save:7");
        sn::Season slot = committed;
        slot.career = "f1_career_progressive_save:7";
        r = fresh.report("swofty.progressive", slot, 2016, pg, m);
        expect(r == sn::Report::ok && !m.fire && said(m, "already started"), "the slot id learnt at the first save keeps the season started");
        sn::Clock other;
        r = other.report("tails.whatif", none, 2016, pg, m);
        expect(!other.queued(), "only the owner's report is queued");
        r = other.report("swofty.progressive", none, 1900, pg, m);
        expect(!other.queued(), "nor an invalid year");
        r = other.report("swofty.progressive", none, 2016, pg, m);
        other.unloaded();
        expect(!other.queued(), "an unloaded career drops the queue");
        expect(sn::is_provisional(sn::provisional_id("x.driver_career", 3)) && !sn::is_provisional(sn::career_id("x", 3, 0xaa)),
               "provisional ids are told apart from slot ids");
    }
    r = clock.report("swofty.progressive", p, 1900, pg, m);
    expect(r == sn::Report::invalid, "a year out of range is invalid");
    r = clock.report("swofty.progressive", p, 2016, pg, m);
    expect(r == sn::Report::ok && m.fire && m.year == 2016 && m.index == 0, "the owner's report starts its season");
    m = clock.hub(p, pg);
    expect(!m.fire && m.lines.empty(), "the Team HQ after the report adds nothing");
    r = clock.report("swofty.progressive", p, 2016, pg, m);
    expect(r == sn::Report::ok && !m.fire && said(m, "already started"), "once per career and year");
    p.index = 1;
    r = clock.report("swofty.progressive", p, 2018, pg, m);
    expect(r == sn::Report::ok && m.fire && m.year == 2018 && said(m, "the records put 2017 at this index"),
           "a test season: the reported year wins over the records' order");
    p.career = "pg-2";
    p.index = 0;
    r = clock.report("swofty.progressive", p, 2020, pg, m);
    expect(r == sn::Report::ok && m.fire && m.year == 2020, "a career that starts later starts its first year");
    sn::Season other{"pg-3", "f1_career_progressive", "swofty.progressive", 0, true, ""};
    r = clock.report("swofty.progressive", other, 2016, pg, m);
    expect(r == sn::Report::ok && m.fire, "the same year starts again in another career");

    logs.clear();
    reg::Store bs;
    world(bs);
    snap::Book book(bs, [](const std::string &l) { logs.push_back(l); });
    bs.set_snapshot_source(book.source());
    book.loaded(nullptr);
    book.career_loaded("dc-7", career_ctx());
    book.season_start(2016, career_ctx());
    expect(book.started("dc-7", 2016) && !book.started("dc-7", 2017), "the book knows which seasons a career froze");
    sn::Clock next;
    next.set_started([&](const std::string &c, int y) { return book.started(c, y); });
    sn::YearMap three = sn::year_map(bs, "f1_career_progressive");
    m = next.hub(sn::Season{"dc-7", "f1_career_progressive", "", 0, true, ""}, three);
    expect(!m.fire && said(m, "already started"), "a season the saved entry froze does not start again next session");
    m = next.hub(sn::Season{"dc-7", "f1_career_progressive", "", 1, true, ""}, three);
    expect(m.fire && m.year == 2017, "the next season does");
    book.career_created("dc-8", fmt::Freeze::at_creation, career_ctx());
    expect(!book.started("dc-8", 2016), "a career frozen at creation still gets each season's start");

    expect(sn::save_blocked("", true, "").find("defaults to dry") != std::string::npos, "saving defaults to dry");
    expect(sn::save_blocked("off", true, "").find("SWOFTY_REGISTRY_SAVE=off") != std::string::npos, "off is dry");
    expect(sn::save_blocked("dry", true, "") != "", "dry is dry");
    expect(sn::save_blocked("on", true, "").empty() && sn::save_blocked("1", true, "").empty(), "on and 1 write");
    expect(sn::save_blocked("on", false, "") == "the save splice is off", "on without the splice writes nothing");
    expect(sn::save_blocked("on", true, "the bag type differs") == "the bag type differs", "nor when it is blocked");
    fmt::Bytes body;
    expect(book.saving(body), "a frozen season is due at the next save");
    expect(sn::entry_growth(15, body.size(), false, 0) == (long long)(20 + body.size()),
           "a new entry adds its body and its 20-byte root child head");
    expect(sn::entry_growth(15, 900, true, 1000) == -100, "a smaller entry over a loaded one adds less than nothing");
    expect(!sn::SAVE_BY_DEFAULT, "the entry is not written by default");
}

static void career_id_tests()
{
    expect(sn::career_id(sn::mode_part("", "f1_career_2026", "driver_career"), 3, 0x00ab12cd34ef5678ull) ==
               "f1_career_2026.driver_career:00ab12cd34ef5678",
           "a stock career's id is its data group and kind with the slot guid in 16 hex digits");
    expect(sn::career_id(sn::mode_part("f1_career_progressive_save", "f1_career_progressive", "driver_career"), 7, 0) ==
               "f1_career_progressive_save:7",
           "an addon career's id uses its content's save name, and the slot id without a guid");
    logs.clear();
    reg::Store st;
    world(st);
    snap::Book book(st, [](const std::string &l) { logs.push_back(l); });
    st.set_snapshot_source(book.source());
    book.loaded(nullptr);
    book.career_loaded("dc-44", career_ctx());
    book.season_start(2016, career_ctx());
    fmt::Bytes body;
    expect(book.saving(body), "an entry keyed by the old career key is due");
    book.written(true);
    snap::Book next(st, [](const std::string &l) { logs.push_back(l); });
    next.loaded(&body);
    std::string id = "f1_career_progressive.driver_career:00000000000000aa";
    expect(next.rename("dc-44", id), "an old dc-<driver> entry moves to the save slot id at its first load");
    expect(next.has_career(id) && !next.has_career("dc-44") && next.started(id, 2016),
           "and keeps its frozen seasons under the new id");
    expect(!next.rename("dc-44", id), "a second load moves nothing");
    next.career_loaded("dc-45", career_ctx());
    next.season_start(2016, career_ctx());
    expect(!next.rename("dc-45", id), "an id that already has an entry is never overwritten");
    fmt::Bytes again;
    expect(next.saving(again), "the moved entry is due at the next save");
}

static void fuzz_tests()
{
    reg::Store st;
    world(st);
    snap::Book seed(st);
    st.set_snapshot_source(seed.source());
    seed.loaded(nullptr);
    seed.career_loaded("dc-1", career_ctx());
    seed.season_start(2016, career_ctx());
    seed.season_start(2017, career_ctx());
    fmt::Bytes body;
    seed.saving(body);
    fmt::Entry e = seed.entry();
    fmt::Bytes payload = e.encode();
    std::mt19937 rng(1606);
    size_t ran = 0, read_ok = 0;
    for (int i = 0; i < 3000; i++) {
        fmt::Bytes b;
        int kind = i % 4;
        if (kind == 0) {
            b = body;
            for (int k = 0, n = 1 + rng() % 8; k < n; k++)
                b[rng() % b.size()] ^= uint8_t(1 + rng() % 255);
        } else if (kind == 1) {
            b.assign(body.begin(), body.begin() + rng() % body.size());
        } else if (kind == 2) {
            fmt::Bytes p = payload;
            for (int k = 0, n = 1 + rng() % 6; k < n; k++)
                p[12 + rng() % (p.size() - 12)] = uint8_t(rng());
            uint32_t crc = fmt::crc32(p.data() + 12, p.size() - 12);
            for (int k = 0; k < 4; k++)
                p[8 + k] = uint8_t(crc >> (8 * k));
            fmt::bag_body(p, b);
        } else {
            b.resize(rng() % 300);
            for (auto &x : b)
                x = uint8_t(rng());
            if (b.size() > 16)
                b[0] = 1, memset(&b[1], 0, 7);
        }
        snap::Book book(st);
        st.set_snapshot_source(book.source());
        book.loaded(&b);
        book.career_loaded("dc-1", career_ctx());
        read_ok += !get(st, "career.season", "swofty.progressive/2016").is_null();
        book.season_start(2018, career_ctx());
        fmt::Bytes out;
        book.saving(out);
        book.career_unloaded();
        ran++;
    }
    expect(ran == 3000 && read_ok == 3000, "3000 corrupt entries load without a crash and every read answers");
    st.set_snapshot_source(nullptr);
}

struct BagIn {
    const fmt::Bytes &b;
    size_t at = 0;
    bool ok = true;
    bool need(size_t n) { return ok = ok && at + n <= b.size(); }
    uint64_t u64()
    {
        uint64_t v = 0;
        if (need(8))
            memcpy(&v, &b[at], 8), at += 8;
        return v;
    }
    uint32_t u32()
    {
        uint32_t v = 0;
        if (need(4))
            memcpy(&v, &b[at], 4), at += 4;
        return v;
    }
    std::string key()
    {
        if (!need(1))
            return {};
        size_t n = b[at++];
        if (!need(n))
            return {};
        std::string s(reinterpret_cast<const char *>(&b[at]), n);
        at += n;
        return s;
    }
};

struct GameBag {
    std::vector<std::pair<std::string, GameBag>> bags;
    std::vector<std::pair<std::string, std::string>> strings;
};

static bool read_game_bag(BagIn &in, GameBag &out)
{
    if (in.u64() != 1)
        return false;
    uint64_t n = in.u64();
    for (uint64_t i = 0; i < n && in.ok; i++) {
        std::string k = in.key();
        uint32_t type = in.u32();
        if (type == fmt::TYPE_BAG) {
            GameBag child;
            if (!read_game_bag(in, child))
                return false;
            out.bags.emplace_back(k, std::move(child));
        } else if (type == fmt::TYPE_DSTR) {
            uint32_t len = in.u32();
            if (!in.need(len))
                return false;
            out.strings.emplace_back(k, std::string(reinterpret_cast<const char *>(&in.b[in.at]), len));
            in.at += len;
        } else {
            return false;
        }
    }
    return in.ok;
}

static void write_game_bag(fmt::Writer &w, const GameBag &b, bool reverse)
{
    w.u64(1);
    w.u64(b.bags.size() + b.strings.size());
    auto strings = b.strings;
    if (reverse)
        std::reverse(strings.begin(), strings.end());
    for (auto &[k, v] : strings)
        fmt::dstr(w, k, v);
    for (auto &[k, v] : b.bags) {
        fmt::key(w, k);
        w.u32(fmt::TYPE_BAG);
        write_game_bag(w, v, reverse);
    }
}

static void splice_tests()
{
    reg::Store st;
    world(st);
    snap::Book book(st);
    st.set_snapshot_source(book.source());
    book.loaded(nullptr);
    book.career_loaded("dc-1", career_ctx());
    book.season_start(2016, career_ctx());
    fmt::Bytes body;
    book.saving(body);

    GameBag progressive;
    progressive.strings.emplace_back("uid", "abcd");
    fmt::Writer pw;
    write_game_bag(pw, progressive, false);
    sp::Store kept;
    kept.begin_load();
    kept.add("driver_career_progressive", pw.out);
    expect(kept.add(sp::ENTRY_NAME, body) == sp::Add::kept, "the splice store keeps the swofty_registry body");
    sp::Owed owed;
    int result = 0;
    fmt::Writer block;
    fmt::key(block, "containers");
    block.u32(fmt::TYPE_BAG);
    block.u64(1);
    block.u64(owed.bump(&result, 1, sp::pick(kept.kept(), {"driver_career_26"})));
    for (size_t i : owed.take(&result)) {
        auto e = sp::entry_bytes(kept.kept()[i].name, fmt::TYPE_BAG, kept.kept()[i].body);
        block.raw(e.data(), e.size());
    }
    GameBag stock;
    stock.strings.emplace_back("uid", "x");
    fmt::key(block, "driver_career_26");
    block.u32(fmt::TYPE_BAG);
    write_game_bag(block, stock, false);

    BagIn in{block.out};
    GameBag root;
    bool read = in.key() == "containers" && in.u32() == fmt::TYPE_BAG && read_game_bag(in, root) && in.at == block.out.size();
    expect(read && root.bags.size() == 3, "the spliced containers block reads as a bag with the entry as a root child");
    const GameBag *entry = nullptr;
    for (auto &[k, v] : root.bags)
        if (k == sp::ENTRY_NAME)
            entry = &v;
    expect(entry && entry->strings.size() >= 2, "swofty_registry reads back as a bag of string properties");
    if (entry) {
        fmt::Writer rewritten;
        write_game_bag(rewritten, *entry, true);
        snap::Book next(st);
        st.set_snapshot_source(next.source());
        next.loaded(&rewritten.out);
        next.career_loaded("dc-1", career_ctx());
        layer(st, "x", "career.season", "swofty.progressive/2016", R"({"teams":[{"slot":1,"seats":["game/k","game/b"]}]})");
        expect(seat(st, 2016) == "game/a",
               "the bag the game writes back at the next load (entries in another order) gives the same snapshot");
    }
}

static void write_file(const std::filesystem::path &p, const std::string &s)
{
    std::ofstream f(p, std::ios::binary);
    f << s;
}

static std::string read_file(const std::filesystem::path &p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static int generate(const std::filesystem::path &dir)
{
    reg::Store st;
    world(st);
    snap::Book book(st);
    st.set_snapshot_source(book.source());
    book.loaded(nullptr);
    book.career_loaded("dc-0000002a", career_ctx());
    book.season_start(2016, career_ctx());
    book.season_start(2017, career_ctx());
    fmt::Bytes body;
    book.saving(body);
    std::filesystem::create_directories(dir);
    write_file(dir / "1.6.0-per-season.bin", std::string(body.begin(), body.end()));
    Value expect_doc = Value::object(
        {{"career", Value("dc-0000002a")},
         {"data_group", Value("f1_career_progressive")},
         {"layer", Value::object({{"registry", Value("career.season")},
                                  {"id", Value("swofty.progressive/2016")},
                                  {"patch", parse(R"({"teams":[{"slot":1,"seats":["game/z","game/b"]}]})")}})},
         {"reads", Value::array({Value::object({{"registry", Value("career.season")},
                                                {"id", Value("swofty.progressive/2016")},
                                                {"pointer", Value("/teams/0/seats/0")},
                                                {"value", Value("game/a")}}),
                                 Value::object({{"registry", Value("career.driver_ratings")},
                                                {"id", Value("swofty.progressive/2017/game/a")},
                                                {"pointer", Value("/pace")},
                                                {"value", Value(int64_t(87))}})})}});
    write_file(dir / "1.6.0-per-season.json", swofty::json::dump(expect_doc, 1) + "\n");
    printf("wrote %s (%zu bytes)\n", (dir / "1.6.0-per-season.bin").string().c_str(), body.size());
    return 0;
}

static void fixture_tests(const std::filesystem::path &dir)
{
    size_t n = 0;
    for (auto &f : std::filesystem::directory_iterator(dir)) {
        if (f.path().extension() != ".bin")
            continue;
        std::string bin = read_file(f.path());
        Value doc = parse(read_file(std::filesystem::path(f.path()).replace_extension(".json")));
        fmt::Bytes body(bin.begin(), bin.end());
        reg::Store st;
        world(st);
        snap::Book book(st);
        st.set_snapshot_source(book.source());
        book.loaded(&body);
        reg::Context ctx;
        ctx.data_group = std::string(doc["data_group"].as_string());
        book.career_loaded(std::string(doc["career"].as_string()), ctx);
        const Value &L = doc["layer"];
        if (L.is_object())
            layer(st, "fixture", std::string(L["registry"].as_string()), std::string(L["id"].as_string()),
                  swofty::json::dump(L["patch"]));
        bool all = true;
        for (auto &r : *doc["reads"].if_array()) {
            Value v = get(st, std::string(r["registry"].as_string()), std::string(r["id"].as_string()));
            const Value *at = v.get(r["pointer"].as_string());
            all &= at && swofty::json::dump(*at) == swofty::json::dump(r["value"]);
        }
        expect(all, "fixture " + f.path().filename().string() + " loads and reads its snapshot");
        n++;
    }
    expect(n > 0, "the fixtures folder has at least one fixture entry");
}

void test_pad_tests()
{
    using snap::test_pad_bytes;
    expect(test_pad_bytes("65536", false) == 0, "test pad: ignored when the test agent is not armed");
    expect(test_pad_bytes(nullptr, true) == 0 && test_pad_bytes("", true) == 0, "test pad: off by default");
    expect(test_pad_bytes("65536", true) == 65536 && test_pad_bytes("262144", true) == 262144,
           "test pad: 64 KB and 256 KB accepted");
    expect(test_pad_bytes("0", true) == 0 && test_pad_bytes("12x", true) == 0 && test_pad_bytes("-5", true) == 0 &&
               test_pad_bytes("99999999", true) == 0,
           "test pad: zero, junk and past-the-cap values are refused");

    reg::Store st;
    snap::Book book(st);
    fmt::Bytes none;
    expect(!book.saving(none), "test pad: an untouched book has nothing due");
    book.set_test_pad(65536);
    fmt::Bytes body;
    expect(book.saving(body), "test pad: the pad makes the next save due with no snapshot");
    expect(body.size() > 65536 && body.size() < 2 * 65536, "test pad: the entry is the pad plus its encoding");
    book.written(true);
    fmt::Bytes again;
    expect(!book.saving(again), "test pad: one write only");
    book.set_test_pad(262144);
    fmt::Bytes big;
    expect(book.saving(big) && big.size() > 262144, "test pad: 256 KB entry");
    fmt::Bytes payload;
    std::string why;
    fmt::Entry e;
    expect(fmt::parse_bag(big, payload, why) && fmt::decode(payload, e, why) == fmt::Decoded::ok && e.extra.size() == 1 &&
               e.extra[0].tag == fmt::T_TEST_PAD && e.extra[0].body.size() == 262144,
           "test pad: the entry decodes with the filler kept as an unknown item");
}

int main(int argc, char **argv)
{
    if (argc == 3 && std::string(argv[1]) == "--generate")
        return generate(argv[2]);
    format_tests();
    freeze_tests();
    provisional_save_tests();
    migration_tests();
    budget_tests();
    creation_tests();
    year_map_tests();
    clock_tests();
    career_id_tests();
    fuzz_tests();
    splice_tests();
    test_pad_tests();
    if (argc == 3 && std::string(argv[1]) == "--fixtures")
        fixture_tests(argv[2]);
    printf("%s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
