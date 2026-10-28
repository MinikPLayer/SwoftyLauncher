// Host unit test for the career content logic (career_content_logic.hpp): the descriptor check, the
// save-slot data group walk over a fake career (championship list, CareerSetupComponent, entrant
// list names), the car group pick and the database requirements against a fake database shaped
// like the 1990 content's. run_events_test.sh builds and runs it.
#include "../src/core/career_content_logic.hpp"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace content = runtime::content;

static int fails;

static void expect(bool ok, const std::string &what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
    fails += !ok;
}

template <class T> static void put(std::vector<uint8_t> &mem, size_t off, T v)
{
    if (mem.size() < off + sizeof v)
        mem.resize(off + sizeof v);
    std::memcpy(mem.data() + off, &v, sizeof v);
}

struct Fake {
    std::vector<uint8_t> career, champs, component, source, groups;
    std::vector<std::vector<uint8_t>> lists, named;
    std::vector<std::string> names;
    uint8_t index = 0;
    bool has_component = true;
};

static void *fake_setup(void *, void *user)
{
    auto *f = static_cast<Fake *>(user);
    return f->has_component ? f->component.data() : nullptr;
}

static int fake_index(void *, void *user)
{
    return static_cast<Fake *>(user)->index;
}

static bool fake_readable(const void *p, size_t, void *)
{
    return p != nullptr;
}

static void build(Fake &f, const std::vector<std::string> &names, uint8_t index)
{
    f.names = names;
    f.index = index;
    f.lists.assign(names.size(), {});
    f.named.assign(names.size(), {});
    f.groups.assign(names.size() * content::GROUP_STRIDE, 0);
    for (size_t i = 0; i < names.size(); i++) {
        put(f.named[i], content::NAMED_CHARS, f.names[i].c_str());
        put(f.lists[i], content::GROUP_NAMED, f.named[i].data());
        put(f.groups, i * content::GROUP_STRIDE + content::GROUP_AT, f.lists[i].data());
    }
    f.source.assign(content::GROUPS_END + 8, 0);
    put(f.source, content::GROUPS_BEGIN, f.groups.data());
    put(f.source, content::GROUPS_END, f.groups.data() + f.groups.size());
    f.component.assign(content::SETUP_SOURCE + 8, 0);
    put(f.component, content::SETUP_SOURCE, f.source.data());
    f.champs.assign(content::CHAMP_STRIDE, 0);
    put(f.champs, content::CHAMP_AT, (void *)f.champs.data());
    f.career.assign(content::CAREER_CHAMPS_END + 8, 0);
    put(f.career, content::CAREER_CHAMPS_BEGIN, f.champs.data());
    put(f.career, content::CAREER_CHAMPS_END, f.champs.data() + f.champs.size());
}

static std::map<std::string, std::string> texts;
static std::map<std::string, int64_t> ints;
static std::map<std::string, int64_t> lists;

static bool db_text(const std::string &k, std::string &out, void *)
{
    auto it = texts.find(k);
    return it != texts.end() && (out = it->second, true);
}

static bool db_int(const std::string &k, int64_t &out, void *)
{
    auto it = ints.find(k);
    return it != ints.end() && (out = it->second, true);
}

static bool db_list(const std::string &k, int64_t &out, void *)
{
    auto it = lists.find(k);
    return it != lists.end() && (out = it->second, true);
}

static void stock_db()
{
    texts.clear(), ints.clear(), lists.clear();
    ints["game_modes._ModeChampionship_instancecount_"] = 3;
    texts["game_modes.ModeChampionship_0.save_data.name"] = "driver_career_24";
    ints["game_modes.ModeChampionship_0.statId"] = 28;
    texts["game_modes.ModeChampionship_1.save_data.name"] = "driver_career_26";
    ints["game_modes.ModeChampionship_1.statId"] = 78;
    texts["game_modes.ModeChampionship_2.save_data_0.name"] = "driver_career_1990";
    ints["game_modes.ModeChampionship_2.statId"] = 81;
    lists["f1_career_2026"] = 97;
    lists["f1_career_1990"] = 107;
    lists["f1_career_huge"] = 300;
    ints["content_sets._content_set_instancecount_"] = 2;
    ints["content_sets.content_set_1._official_entrant_list_instancecount_"] = 4;
    texts["content_sets.content_set_1.official_entrant_list_0.name"] = "F1CAREER_26";
    texts["content_sets.content_set_1.official_entrant_list_1.name"] = "F1CAREER_90";
    texts["content_sets.content_set_1.official_entrant_list_2.name"] = "F2CAREER_90";
    texts["content_sets.content_set_1.official_entrant_list_3.name"] = "ICONS_90";
}

static content::Desc desc1990()
{
    content::Desc d;
    d.id = "f1-1990";
    d.title = "lng_f1990_career_content";
    d.description = "lng_f1990_career_content_description";
    d.data_group = "f1_career_1990";
    d.suffix = "90";
    d.mode = "driver_career_1990";
    return d;
}

int main()
{
    auto none = [](const char *, const std::string &) { return false; };
    {
        content::Desc d = desc1990();
        expect(content::check(d, none).empty(), "check: the 1990 descriptor is accepted");
        auto taken = [](const char *f, const std::string &v) { return std::string(f) == "data_group" && v == "f1_career_1990"; };
        expect(content::check(d, taken).find("data group") != std::string::npos, "check: a duplicate data group is refused");
        d.data_group = "f1_career_2026";
        expect(content::check(d, none).find("stock") != std::string::npos, "check: a stock data group is refused");
        d = desc1990();
        d.suffix = "26";
        expect(!content::check(d, none).empty(), "check: a stock suffix is refused");
        d = desc1990();
        d.id = "Bad Id";
        expect(!content::check(d, none).empty(), "check: an id with spaces or capitals is refused");
        d = desc1990();
        d.title.clear();
        expect(!content::check(d, none).empty(), "check: an empty title is refused");
        d = desc1990();
        d.model = 7;
        expect(!content::check(d, none).empty(), "check: an unknown model is refused");
        d = desc1990();
        d.car_group = -2;
        expect(!content::check(d, none).empty(), "check: a car group below -1 is refused");
        d = desc1990();
        d.careers = content::CAREER_MY_TEAM;
        expect(content::check(d, none).empty(), "check: a My Team content is accepted");
        d.careers = content::CAREER_ALL;
        expect(content::check(d, none).empty(), "check: a content for both careers is accepted");
        d.careers = 0;
        expect(!content::check(d, none).empty(), "check: a content for no career is refused");
        d.careers = 4;
        expect(!content::check(d, none).empty(), "check: an unknown career bit is refused");
    }
    {
        expect(content::career_of(SWOFTY_CAREER_DRIVER) == content::CAREER_DRIVER, "career: a Driver Career pick is Driver Career");
        expect(content::career_of(SWOFTY_CAREER_MY_TEAM) == content::CAREER_MY_TEAM, "career: a My Team pick is My Team");
        expect(content::career_of(0) == content::CAREER_DRIVER, "career: no pick yet is Driver Career");
        expect(content::stock_command(content::ADDON_SELECT_PRESSED) == content::SELECT_PRESSED,
               "command: addon_select_pressed runs as select_pressed");
        expect(content::stock_command(content::ADDON_FOCUS_CHANGED) == content::FOCUS_CHANGED,
               "command: addon_focus_changed runs as focus_changed");
        expect(content::stock_command(content::SELECT_PRESSED) == 0, "command: a stock command is left alone");
    }
    {
        const uintptr_t select = 0x145738430, showroom = 0x1457390a0;
        const char *career[] = {"career_default_content", "career_dlc_content"};
        const char *cars[] = {"showroom_f1_DLC", "showroom_f1", "showroom_f2_next", "showroom_f2",
                              "showroom_player_cars_dlc", "showroom_player_cars", nullptr};
        expect(content::content_select(select, select, career, 2), "select: the content select view model's list");
        expect(content::content_select(select, select, nullptr, 0),
               "select: the content select view model's list before its tiles are read");
        expect(!content::content_select(showroom, select, cars, 7), "select: the showroom's car tiles are left alone");
        expect(!content::content_select(0, select, cars, 7),
               "select: car tiles with no view model found are left alone");
        expect(content::content_select(0, select, career, 2),
               "select: the stock career tiles with no view model found are the content select");
        expect(!content::content_select(0, 0, nullptr, 0), "select: no vtable bound and no tiles is left alone");
        expect(!content::career_tile_id(nullptr) && !content::career_tile_id("career") &&
                   content::career_tile_id("career_dlc_content"),
               "select: only the two stock career content ids count");
    }
    {
        Fake f;
        build(f, {"f1_2024", "f1_career_2025", "f1_career_2026", "f1_career_1990"}, 3);
        content::Access a{fake_setup, fake_index, fake_readable, &f};
        content::GroupSeen seen = content::data_group(f.career.data(), a);
        expect(seen.component && seen.index == 3 && seen.count == 4 && seen.name && std::string(seen.name) == "f1_career_1990",
               "walk: index 3 of 4 names f1_career_1990");
        expect(content::data_group_is(f.career.data(), "f1_career_1990", a), "walk: data_group_is matches the 1990 group");
        expect(!content::data_group_is(f.career.data(), "f1_career_2026", a), "walk: data_group_is refuses another group");
        f.index = 9;
        seen = content::data_group(f.career.data(), a);
        expect(seen.index == 9 && !seen.name, "walk: an index past the list (addon off, unknown group) names nothing");
        expect(!content::data_group_is(f.career.data(), "f1_career_1990", a), "walk: and matches nothing");
        f.has_component = false;
        seen = content::data_group(f.career.data(), a);
        expect(!seen.component && !seen.name, "walk: a career without a CareerSetupComponent has no data group");
        expect(!content::data_group(nullptr, a).name, "walk: a null career has no data group");
    }
    {
        std::vector<uint8_t> e(3 * content::ENTRY_SIZE, 0);
        put<uint32_t>(e, 0 * content::ENTRY_SIZE + content::ENTRY_ID, 0x00000011);
        put<uint32_t>(e, 1 * content::ENTRY_SIZE + content::ENTRY_ID, 0x00010022);
        e[1 * content::ENTRY_SIZE + content::ENTRY_DLC] = 1;
        put<uint32_t>(e, 2 * content::ENTRY_SIZE + content::ENTRY_ID, 0x00020033);
        e[2 * content::ENTRY_SIZE + content::ENTRY_DLC] = 1;
        const uint8_t *b = e.data(), *en = e.data() + e.size();
        content::Pick p = content::pick(b, en, 2, content::MODEL_2026);
        expect(p.kind == content::Pick::exact && p.id == 0x00020033, "pick: car group 2 is taken by its stat_id");
        p = content::pick(b, en, 9, content::MODEL_2026);
        expect(p.kind == content::Pick::fallback && p.id == 0x00010022, "pick: no group 9, the 2026 model's first dlc group");
        p = content::pick(b, en, -1, content::MODEL_2025);
        expect(p.kind == content::Pick::fallback && p.id == 0x00000011, "pick: the 2025 model takes the first non-dlc group");
        p = content::pick(nullptr, nullptr, 2, content::MODEL_2026);
        expect(p.kind == content::Pick::none && p.id == content::NO_CONTENT, "pick: no entries selects no content");
    }
    {
        stock_db();
        content::Db db{db_text, db_int, db_list, nullptr};
        content::Requirements r = content::requirements(desc1990(), db);
        expect(r.ok && r.mode == 81 && r.group == 107 && r.f1 && r.f2 && r.icons && r.warn.empty(),
               "requirements: the 1990 data resolves mode 81 and group 107 with all three official lists");
        content::Desc d = desc1990();
        d.mode = "driver_career_1991";
        r = content::requirements(d, db);
        expect(!r.ok && r.why.find("ModeChampionship") != std::string::npos, "requirements: a missing mode row fails the requirements");
        d = desc1990();
        d.data_group = "f1_career_1991";
        r = content::requirements(d, db);
        expect(!r.ok && r.why.find("entrant_lists") != std::string::npos, "requirements: a missing entrant list fails the requirements");
        d.data_group = "f1_career_huge";
        r = content::requirements(d, db);
        expect(!r.ok && r.why.find("below 256") != std::string::npos, "requirements: a stat_id past a byte fails the requirements");
        texts.erase("content_sets.content_set_1.official_entrant_list_1.name");
        r = content::requirements(desc1990(), db);
        expect(!r.ok && r.why.find("F1CAREER_90") != std::string::npos, "requirements: no F1CAREER_<suffix> list fails the requirements");
        stock_db();
        texts.erase("content_sets.content_set_1.official_entrant_list_3.name");
        r = content::requirements(desc1990(), db);
        expect(r.ok && r.warn.find("ICONS_90") != std::string::npos, "requirements: a missing ICONS_<suffix> list only warns");
    }
    {
        content::Retry r;
        expect(r.due(0), "retry: the first check is due at once");
        expect(r.failed(0, "no entrants.entrant_lists row is named f1_career_1990"), "retry: a first failure is logged");
        expect(!r.due(content::RETRY_MS - 1) && r.due(content::RETRY_MS), "retry: the next check waits RETRY_MS");
        expect(!r.failed(content::RETRY_MS, "no entrants.entrant_lists row is named f1_career_1990"),
               "retry: the same reason is not logged again");
        expect(r.failed(2 * content::RETRY_MS, "no content set lists the official entrant list F1CAREER_90"),
               "retry: a new reason is logged");
        expect(r.tries == 3 && r.due(3 * content::RETRY_MS), "retry: it keeps checking, never latching off");
    }
    printf("%s\n", fails ? "career_content_test: FAILED" : "career_content_test: all passed");
    return fails ? 1 : 0;
}
