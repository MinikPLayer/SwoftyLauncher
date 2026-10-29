#include "../runtime.hpp"

#include <swofty/game.hpp>
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>
#include <swofty/systems/career.hpp>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>

#include "../apply/moments.hpp"
#include "../hooks/core_events.hpp"
#include "../hooks/hooks.hpp"
#include "../registry/table.hpp"
#include "../save_splice_logic.hpp"
#include "clock.hpp"
#include "snapshots.hpp"

// Career snapshots in game: the book (snapshots.cpp) on the core's registry store, fed by the career
// moments on the hooks table and by the save splice's root loads and profile saves.
//
// Moments: career.loaded and career.unloaded (fired by the core's career tracker). career.created: the
// first career loaded after a new-career route (career.starting, kind new). career.season.start: when
// the clock (clock.hpp) knows the season's year, either from the owner's career::report_season
// (SwoftyCareerOps.report_season, on the owner's own Team HQ moment) or, for a career no addon owns, from
// the career.season records at an idle Team HQ (career.hub, after every other handler) whose current
// Season has its weekends and a current weekend. At a season start the apply scheduler activates the
// season (apply::moments::career_season) and then the freeze runs, so both see the same season; this runs
// first among each moment's after handlers, so addons that read the season there read the snapshot. The
// career's stat-id map comes from apply::moments.
//
// Writing: only through the save splice, only when it is armed and the game's bag type id is the one the
// entry's layout assumes (save_splice::entry_blocked), and only with SWOFTY_REGISTRY_SAVE=on (or 1). By
// default (dry) the snapshots stay in memory for the session and each profile save logs the entry it
// would have written and how many bytes it would add to the save.

namespace runtime {
namespace {

namespace reg = swofty_core::registry;
namespace snap = swofty_core::snapshots;
namespace sp = runtime::save_splice;
namespace hk = swofty_core::hooks;

namespace g = swofty::game;
namespace sn = swofty_core::snapshots::seasons;

std::once_flag started;
std::unique_ptr<snap::Book> the_book;
bool writes = false;
std::string blocked;
std::mutex clock_mu;
sn::Clock the_clock;
size_t loaded_entry = 0;
bool loaded_has_entry = false;

constexpr uintptr_t SLOT_MANAGER_CURRENT = 0x280;
std::mutex slot_mu;
uintptr_t slot_manager = 0;
uint64_t seen_id = 0, seen_guid = 0;
bool seen = false;
std::string committed_key, committed_id;
unsigned committed_mode = 0;
bool committed_provisional = false;
unsigned provisional_count = 0;

snap::Book &book() { return *the_book; }

reg::Context facts_context()
{
    reg::Context ctx;
    if (auto f = hk::core().current(); f && f->career)
        ctx.data_group = f->career->data_group;
    char group[128] = {};
    if (auto t = runtime::career_content_table(); ctx.data_group.empty() && t && t->data_group &&
                                                  t->data_group(nullptr, group, sizeof group) == SWOFTY_ASSET_OK)
        ctx.data_group = group;
    return ctx;
}

std::string map_json(const std::string &career)
{
    swofty::json::Value out = swofty::json::Value::object();
    for (auto &[registry, records] : swofty_core::apply::moments::map_for_career(career))
        for (auto &[record, id] : records)
            out[registry][record] = swofty::json::Value(int64_t(id));
    return swofty::json::dump(out);
}

std::vector<std::string> adopt_json(const std::string &career, const std::string &text)
{
    swofty::json::Error e;
    swofty::json::Value v = swofty::json::parse(text, &e);
    swofty_core::apply::ids::CareerMap map;
    if (const auto *regs = v.if_object())
        for (auto &[registry, records] : *regs)
            if (const auto *o = records.if_object())
                for (auto &[record, id] : *o)
                    if (id.is_int())
                        map[std::string(registry)][std::string(record)] = id.as_int();
    std::vector<std::string> lines;
    for (auto &c : swofty_core::apply::moments::adopt(career, map))
        lines.push_back(c.line);
    return lines;
}

std::string facts_career()
{
    auto f = hk::core().current();
    return f && f->career ? f->career->id : std::string();
}

std::string loaded_data_group()
{
    char group[128] = {};
    if (auto t = runtime::career_content_table(); t && t->data_group &&
                                                  t->data_group(nullptr, group, sizeof group) == SWOFTY_ASSET_OK)
        return group;
    return {};
}

std::string slot_career_id(const std::string &data_group, unsigned mode, uint64_t id, uint64_t guid)
{
    std::string save;
    runtime::career_content_owner(data_group, &save);
    return sn::career_id(sn::mode_part(save, data_group, mode == SWOFTY_CAREER_MY_TEAM ? "my_team" : "driver_career"),
                         id, guid);
}

bool current_slot(uintptr_t manager, uint64_t &id, uint64_t &guid)
{
    uintptr_t cur = 0;
    return manager && swofty::mem::read(manager + SLOT_MANAGER_CURRENT, cur) && cur && swofty::mem::read(cur, id) &&
           swofty::mem::read(cur + 8, guid);
}

void refresh_slot()
{
    std::string from, to;
    {
        std::lock_guard g(slot_mu);
        uint64_t id = 0, guid = 0;
        if (committed_id.empty() || (committed_id == committed_key && !committed_provisional) ||
            !current_slot(slot_manager, id, guid))
            return;
        to = slot_career_id(loaded_data_group(), committed_mode, id, guid);
        if (to == committed_id)
            return;
        from = committed_id;
        committed_id = to;
        committed_provisional = false;
    }
    {
        std::lock_guard g(clock_mu);
        the_clock.rename(from, to);
    }
    swofty_core::apply::moments::career_renamed(from.c_str(), to.c_str());
    bool moved = book().active() == from && book().rename(from, to);
    hk::update_facts([&](hk::Facts &f) {
        if (f.career && f.career->id == from)
            f.career->id = to;
    });
    swofty::log("[registry] career %s: its save slot now reads %s at this save%s", from.c_str(), to.c_str(),
                moved ? "; its entry moved with it" : "");
}

std::string owner_of_data_group(const std::string &data_group)
{
    if (data_group.empty())
        return {};
    std::string owner = runtime::career_content_owner(data_group);
    reg::Store &st = reg::store();
    if (owner.empty())
        for (auto &id : st.list("career.mode", "")) {
            reg::Record r = st.get("career.mode", id);
            if (r.status == reg::Status::ok && r.value["data_group"].as_string() == data_group) {
                owner = id.substr(0, id.find('/'));
                break;
            }
        }
    return owner == "game" || owner == "swofty.core" ? std::string() : owner;
}

template <class T> T rdv(uintptr_t a, T def = T{})
{
    T v{};
    return swofty::mem::read(a, v) ? v : def;
}

uintptr_t resolved(const char *id) { return swofty::api && swofty::api->resolve ? swofty::api->resolve(id) : 0; }

bool championship_season(int &index, std::string &not_ready)
{
    index = -1;
    const SwoftyCareer *c = runtime::career_table();
    auto e = c && c->entity ? reinterpret_cast<uintptr_t>(c->entity()) : 0;
    g::CareerEntity ent{};
    if (!e || !swofty::mem::object_like(e) || !swofty::mem::read(e, ent)) {
        not_ready = "no career entity";
        return false;
    }
    uintptr_t want = resolved("vt_Career_Championship");
    uintptr_t cand[2] = {rdv<uintptr_t>(e + offsetof(g::CareerEntity, current_championship)), 0};
    if (uintptr_t desc = resolved("k_Career_ChampionshipDesc"); desc && c->component_by_desc)
        cand[1] = reinterpret_cast<uintptr_t>(c->component_by_desc(reinterpret_cast<void *>(desc)));
    uintptr_t ch = 0;
    for (uintptr_t x : cand)
        if (want && x && rdv<uintptr_t>(x) == want)
            ch = x;
    g::Championship champ{};
    if (!ch || !swofty::mem::read(ch, champ)) {
        not_ready = want ? "the championship is not readable" : "vt_Career_Championship does not resolve";
        return false;
    }
    auto b = reinterpret_cast<uintptr_t>(champ.seasons_begin), en = reinterpret_cast<uintptr_t>(champ.seasons_end);
    size_t n = en > b && !((en - b) % sizeof(g::ChampionshipSeasonEntry)) ? (en - b) / sizeof(g::ChampionshipSeasonEntry) : 0;
    if (!n || n > 64 || champ.season_index >= n) {
        not_ready = "the championship's seasons are not built";
        return false;
    }
    index = int(champ.season_index);
    if (ent.entrant_update_busy) {
        not_ready = "the career is busy";
        return false;
    }
    g::ChampionshipSeasonEntry entry{};
    uintptr_t season_vt = resolved("vt_Career_Season"), weekend_vt = resolved("vt_Career_Weekend");
    if (!swofty::mem::read(b + champ.season_index * sizeof entry, entry) || !entry.season || !season_vt ||
        rdv<uintptr_t>(reinterpret_cast<uintptr_t>(entry.season)) != season_vt) {
        not_ready = "the current season is not readable";
        return false;
    }
    g::Season season{};
    if (!swofty::mem::read(reinterpret_cast<uintptr_t>(entry.season), season)) {
        not_ready = "the current season is not readable";
        return false;
    }
    auto wb = reinterpret_cast<uintptr_t>(season.weekends_begin), we = reinterpret_cast<uintptr_t>(season.weekends_end);
    size_t weekends = wb && we > wb && !((we - wb) % sizeof(void *)) ? (we - wb) / sizeof(void *) : 0;
    if (!weekends || weekends > 64) {
        not_ready = "the season has no race weekends yet";
        return false;
    }
    for (size_t i = 0; i < weekends; i++) {
        uintptr_t w = rdv<uintptr_t>(wb + i * sizeof(void *));
        if (!w || !weekend_vt || rdv<uintptr_t>(w) != weekend_vt) {
            not_ready = "the season's race weekends are not built";
            return false;
        }
    }
    if (season.current_weekend >= weekends) {
        not_ready = "the season has no current weekend";
        return false;
    }
    return true;
}

sn::Season season_now(bool read_game)
{
    sn::Season s;
    s.career = facts_career();
    s.data_group = facts_context().data_group;
    s.owner = owner_of_data_group(s.data_group);
    if (read_game && !s.career.empty())
        s.ready = championship_season(s.index, s.not_ready);
    return s;
}

void say(const std::vector<std::string> &lines)
{
    for (auto &l : lines)
        swofty::log("%s", l.c_str());
}

void season_start(const sn::Moment &m)
{
    if (m.fire)
        hk::events::career_season_start(m.year, m.index);
}

snap::format::Freeze freeze_of(const std::string &data_group)
{
    reg::Store &st = reg::store();
    for (auto &id : st.list("career.mode", "")) {
        reg::Record r = st.get("career.mode", id);
        if (r.status == reg::Status::ok && !data_group.empty() && r.value["data_group"].as_string() == data_group)
            return r.value["freeze"].as_string() == "at_creation" ? snap::format::Freeze::at_creation
                                                                   : snap::format::Freeze::per_season;
    }
    return snap::format::Freeze::per_season;
}

int on_loaded(void *, SwoftyHookCall *)
{
    std::string career = facts_career(), legacy;
    {
        std::lock_guard g(slot_mu);
        if (career == committed_id && committed_key != committed_id)
            legacy = committed_key;
    }
    if (!legacy.empty() && book().rename(legacy, career))
        swofty::log("[registry] career %s: its swofty_registry entry was keyed %s; migrated to the save slot id",
                    career.c_str(), legacy.c_str());
    book().career_loaded(career, facts_context());
    bool created;
    {
        std::lock_guard g(clock_mu);
        created = the_clock.loaded(career);
    }
    if (created) {
        swofty::log("[registry] career %s is new (a new-career route started it); career.created", career.c_str());
        hk::events::career_created();
    }
    return 0;
}

int on_unloaded(void *, SwoftyHookCall *)
{
    {
        std::lock_guard g(clock_mu);
        the_clock.unloaded();
    }
    book().career_unloaded();
    return 0;
}

int on_hub(void *, SwoftyHookCall *)
{
    sn::Season s = season_now(true);
    if (s.career.empty())
        return 0;
    sn::YearMap map = sn::year_map(reg::store(), s.data_group);
    sn::Moment m;
    {
        std::lock_guard g(clock_mu);
        m = the_clock.hub(s, map);
    }
    say(m.lines);
    season_start(m);
    return 0;
}

void on_starting(const SwoftyEvent *ev, void *)
{
    if (!ev || ev->b == SWOFTY_CAREER_START_PICK)
        return;
    std::lock_guard g(clock_mu);
    the_clock.starting(ev->b == SWOFTY_CAREER_START_NEW);
}

int on_created(void *, SwoftyHookCall *)
{
    reg::Context ctx = facts_context();
    book().career_created(facts_career(), freeze_of(ctx.data_group), ctx);
    return 0;
}

int on_season_start(void *, SwoftyHookCall *call)
{
    int year = 0;
    if (call && call->payload) {
        auto *p = static_cast<const swofty::career::events::SeasonStart *>(call->payload);
        if (p->size >= offsetof(swofty::career::events::SeasonStart, season_index) + sizeof(int))
            year = p->year;
    }
    if (!year)
        if (auto f = hk::core().current(); f && f->career)
            year = f->career->year;
    if (!year) {
        swofty::log("[registry] career.season.start without a year; nothing frozen");
        return 0;
    }
    swofty_core::apply::moments::career_season(year);
    book().season_start(year, facts_context());
    return 0;
}

void entry_loaded(const std::vector<uint8_t> *body)
{
    loaded_has_entry = body != nullptr;
    loaded_entry = body ? body->size() : 0;
    book().loaded(body);
}

bool entry_saving(std::vector<uint8_t> &body)
{
    refresh_slot();
    if (!book().saving(body)) {
        swofty::log("[registry] swofty_registry: this profile save %s", loaded_has_entry
                        ? ("writes back the entry it loaded (" + std::to_string(loaded_entry + 20) + " bytes)").c_str()
                        : "has no entry to write");
        return false;
    }
    if (writes) {
        swofty::log("[registry] swofty_registry: this profile save writes a %zu-byte entry, adding %lld bytes to the save",
                    size_t(sn::entry_growth(std::strlen(sp::ENTRY_NAME), body.size(), false, 0)),
                    sn::entry_growth(std::strlen(sp::ENTRY_NAME), body.size(), loaded_has_entry, loaded_entry));
        return true;
    }
    book().written(false);
    snap::format::Entry e = book().entry();
    size_t seasons = 0;
    for (auto &c : e.careers)
        seasons += c.seasons.size();
    size_t name = std::strlen(sp::ENTRY_NAME);
    long long added = sn::entry_growth(name, body.size(), loaded_has_entry, loaded_entry);
    swofty::log("[registry] swofty_registry dry: this profile save would write a %zu-byte entry (%zu career(s), %zu "
                "season snapshot(s)), adding %lld bytes to the save%s; nothing written (%s)",
                size_t(sn::entry_growth(name, body.size(), false, 0)), e.careers.size(), seasons, added,
                loaded_has_entry ? " over the entry it loaded" : "", blocked.c_str());
    return false;
}

void entry_written(bool ok)
{
    book().written(ok);
    if (ok)
        swofty::log("[registry] swofty_registry: written with this profile save");
}

void attach(const char *point, SwoftyHookFn fn)
{
    uint32_t token = 0;
    hk::core().attach("swofty.core", point, SWOFTY_PHASE_AFTER, 1000000, "", fn, nullptr, &token);
}

}

void snapshots_start(bool splice_live)
{
    std::call_once(started, [&] {
        the_book = std::make_unique<snap::Book>(reg::store(), [](const std::string &s) { swofty::log("%s", s.c_str()); });
        the_book->set_ids({map_json, adopt_json});
        reg::store().set_snapshot_source(the_book->source());
        attach("career.loaded", on_loaded);
        attach("career.unloaded", on_unloaded);
        attach("career.created", on_created);
        attach("career.season.start", on_season_start);
        uint32_t token = 0;
        hk::core().attach("swofty.core", "career.hub", SWOFTY_PHASE_AFTER, -1000000, "", on_hub, nullptr, &token);
        if (const SwoftyEvents *ev = runtime::events_table(); ev && ev->subscribe)
            ev->subscribe(SWOFTY_EVENT_CAREER_STARTING, on_starting, nullptr, &token);
        {
            std::lock_guard g(clock_mu);
            the_clock.set_started([](const std::string &career, int year) { return book().started(career, year); });
        }
        char v[16] = {};
        DWORD n = GetEnvironmentVariableA("SWOFTY_REGISTRY_SAVE", v, sizeof v);
        std::string mode = n && n < sizeof v ? v : "";
        blocked = sn::save_blocked(mode, splice_live, splice_live ? sp::entry_blocked() : std::string());
        writes = blocked.empty();
        {
            char pv[16] = {};
            DWORD pn = GetEnvironmentVariableA("SWOFTY_TEST_SNAPSHOT_PAD", pv, sizeof pv);
            bool armed = runtime::test_run() && runtime::test_run_live();
            size_t pad = snap::test_pad_bytes(pn && pn < sizeof pv ? pv : "", armed);
            if (pad) {
                the_book->set_test_pad(pad);
                swofty::log("[registry] test: SWOFTY_TEST_SNAPSHOT_PAD set; the next profile save writes a %zu-byte "
                            "filler in swofty_registry (needs SWOFTY_REGISTRY_SAVE=on)",
                            pad);
            } else if (pn)
                swofty::log("[registry] test: SWOFTY_TEST_SNAPSHOT_PAD ignored (%s)",
                            armed ? "not a size in 1..4128768" : "the test agent is not armed");
        }
        if (splice_live)
            sp::set_entry_hooks({entry_loaded, entry_saving, entry_written});
        swofty::log("[registry] career snapshots on: %s", writes ? "saved in swofty_registry through the save splice"
                                                                 : ("not saved (" + blocked + ")").c_str());
    });
}


void snapshots_slot_selected(uintptr_t manager, uint64_t slot_id, uint64_t slot_guid)
{
    std::lock_guard g(slot_mu);
    slot_manager = manager;
    seen_id = slot_id, seen_guid = slot_guid;
    seen = true;
}

std::string snapshots_career_loaded(const char *key, unsigned mode)
{
    std::string k = key ? key : "";
    std::string group = loaded_data_group();
    std::lock_guard g(slot_mu);
    uint64_t id = seen_id, guid = seen_guid, cur_id = 0, cur_guid = 0;
    bool have = seen;
    if (have && current_slot(slot_manager, cur_id, cur_guid))
        id = cur_id, guid = cur_guid;
    if (!have)
        slot_manager = 0;
    committed_key = k;
    committed_mode = mode;
    committed_provisional = !have && k.empty();
    committed_id = have ? slot_career_id(group, mode, id, guid)
                   : committed_provisional
                       ? sn::provisional_id(sn::mode_part(std::string(), group, mode == SWOFTY_CAREER_MY_TEAM ? "my_team" : "driver_career"),
                                            ++provisional_count)
                       : k;
    seen = false;
    if (have)
        swofty::log("[registry] career id %s (save slot %llu, guid %016llx; the career tracker's key '%s')",
                    committed_id.c_str(), (unsigned long long)id, (unsigned long long)guid, k.c_str());
    else if (committed_provisional)
        swofty::log("[registry] career id %s: no save slot was selected before this load (a new career's slot is read "
                    "at its first save); the career runs under this provisional id until then, so career.season.start "
                    "and career::report_season work in its first session",
                    committed_id.c_str());
    else
        swofty::log("[registry] career id '%s': no save slot was selected before this load (a new career's slot is "
                    "read at its first save)",
                    k.c_str());
    return committed_id;
}

std::string snapshots_career_id(const char *key)
{
    std::lock_guard g(slot_mu);
    return key && committed_key == key && !committed_id.empty() ? committed_id : std::string(key ? key : "");
}

int snapshots_report_season(const std::string &caller, int year, int season_index)
{
    if (!the_book) {
        swofty::log("[registry] career::report_season(%d, %d) from %s: unavailable (the career snapshots are not "
                    "started)",
                    year, season_index, caller.c_str());
        return SWOFTY_ASSET_UNAVAILABLE;
    }
    sn::Season s = season_now(false);
    s.index = season_index;
    sn::YearMap map = sn::year_map(reg::store(), s.data_group);
    sn::Moment m;
    sn::Report r;
    {
        std::lock_guard g(clock_mu);
        r = the_clock.report(caller, s, year, map, m);
    }
    say(m.lines);
    season_start(m);
    return r == sn::Report::ok        ? SWOFTY_ASSET_OK
           : r == sn::Report::late    ? SWOFTY_ASSET_LATE
           : r == sn::Report::refused ? SWOFTY_REFUSED
                                      : SWOFTY_ASSET_INVALID;
}

}
