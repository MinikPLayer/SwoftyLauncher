#include "moments.hpp"

#include "../hooks/hooks.hpp"
#include "../registry/table.hpp"
#include "../runtime.hpp"
#include "content/appliers.hpp"
#include "database.hpp"
#include "facts.hpp"
#include "files_api.hpp"
#include "scheduler.hpp"

#include <swofty/game.hpp>
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <windows.h>

#include <atomic>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <cstddef>
#include <map>
#include <mutex>
#include <set>

namespace swofty_core::apply::moments {

namespace {

namespace reg = swofty_core::registry;
using swofty::json::Value;

std::once_flag attached;
std::mutex status_mu;
std::map<std::string, std::vector<std::string>> status_lines;
std::atomic<bool> loaded_from_flow{false};
std::mutex ids_mu;

std::wstring registry_root()
{
    const std::wstring &state = runtime::state_root();
    return (state.empty() ? runtime::swofty_root() : state) + L"registry";
}

std::string registry_dir()
{
    return runtime::narrow(registry_root());
}

void make_registry_dir()
{
    std::wstring w = registry_root();
    for (size_t i = w.find_first_of(L"\\/", 3); i != std::wstring::npos; i = w.find_first_of(L"\\/", i + 1))
        CreateDirectoryW(w.substr(0, i).c_str(), nullptr);
    CreateDirectoryW(w.c_str(), nullptr);
}

std::string today()
{
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    char b[16];
    std::strftime(b, sizeof b, "%Y-%m-%d", &tm);
    return b;
}

bool write_file(const std::string &path, const std::string &text)
{
    make_registry_dir();
    return runtime::write_file(runtime::wide(path), text);
}

std::string read_file(const std::string &path)
{
    return runtime::read_file(runtime::wide(path), size_t(256) << 20);
}

void migrate_ids()
{
    if (runtime::state_root().empty())
        return;
    std::wstring old = runtime::swofty_root() + L"registry\\ids.json", now = registry_root() + L"\\ids.json";
    uint64_t size = 0;
    if (runtime::file_size(now, &size) || !runtime::file_size(old, &size))
        return;
    std::string text = runtime::read_file(old, size_t(256) << 20);
    if (text.empty()) {
        swofty::log("[registry] stat ids: ids.json in the game folder could not be read; it is not moved");
        return;
    }
    make_registry_dir();
    if (!runtime::write_file(now, text)) {
        swofty::log("[registry] stat ids: ids.json could not be moved to %s", runtime::narrow(now).c_str());
        return;
    }
    DeleteFileW(old.c_str());
    swofty::log("[registry] stat ids: moved ids.json from the game folder to %s", runtime::narrow(now).c_str());
}

void save_ids()
{
    std::lock_guard g(ids_mu);
    if (!ids::store().dirty())
        return;
    if (write_file(registry_dir() + "\\ids.json", ids::store().dump()))
        ids::store().saved();
    else
        swofty::log("[registry] stat ids: ids.json could not be written in %s", registry_dir().c_str());
}

std::string owner_of(const std::string &id)
{
    auto s = id.find('/');
    return s == std::string::npos ? std::string() : id.substr(0, s);
}

void facts(const reg::Context &c)
{
    std::string session = scheduler().session_name();
    hooks::update_facts([&](hooks::Facts &f) {
        f.mode = c.mode.empty() ? std::string("unknown") : c.mode;
        f.content = c.content.empty() ? std::string() : c.content.front();
        f.content_registry = c.content.empty() ? std::string() : std::string("content.package");
        f.content_owner = owner_of(f.content);
        f.chain = c.content;
        f.owners.clear();
        for (auto &id : c.content)
            f.owners.push_back(owner_of(id));
        if (!c.career.empty()) {
            if (!f.career)
                f.career.emplace();
            f.career->id = c.career;
            f.career->data_group = c.data_group;
            f.career->year = c.has_year ? c.year : 0;
        } else {
            f.career.reset();
        }
        if (!c.session_type.empty() || !c.track.empty()) {
            if (!f.session)
                f.session.emplace();
            f.session->type = c.session_type;
            f.session->session = session;
            f.session->track = c.track;
            f.session->setup_mode = c.setup_mode;
        } else {
            f.session.reset();
        }
        f.vehicle_packages.clear();
        if (!c.vehicle_package.empty())
            f.vehicle_packages.push_back(c.vehicle_package);
    });
}

uint64_t pe_timestamp()
{
    uintptr_t base = swofty::api ? swofty::api->module_base() : 0;
    if (!base)
        return 0;
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    return nt->FileHeader.TimeDateStamp;
}

const char *mode_name(unsigned m)
{
    return m == SWOFTY_CAREER_MY_TEAM ? "my_team" : m == SWOFTY_CAREER_DRIVER ? "driver_career" : "";
}

void extend(Value &, Value &conflicts)
{
    files::report(conflicts);
    Value &list = conflicts["registry"];
    if (!list.is_array())
        list = Value::array();
    for (auto &f : hooks::faults())
        list.push_back(Value::object({{"kind", Value("handler_faulted")},
                                      {"point", Value(f.point)},
                                      {"phase", Value(f.phase)},
                                      {"addon", Value(f.addon)},
                                      {"error", Value(f.what)}}));
    std::lock_guard g(status_mu);
    if (status_lines.empty())
        return;
    Value st = Value::object();
    for (auto &[addon, lines] : status_lines) {
        Value a = Value::array();
        for (auto &l : lines)
            a.push_back(Value(l));
        st[addon] = a;
    }
    conflicts["status"] = st;
}


namespace game {

namespace g = swofty::game;
namespace mem = swofty::mem;

constexpr size_t MODE_NAME = 0x278;
constexpr size_t SESSION_STRIDE = 0xa0;
constexpr uint64_t MAX_SEASONS = 64, MAX_ROUNDS = 64, MAX_SESSIONS = 32;

std::mutex once_mu;
std::set<std::string> said;

void once(const std::string &why)
{
    {
        std::lock_guard g(once_mu);
        if (!said.insert(why).second)
            return;
    }
    swofty::log("[apply] facts: %s (logged once)", why.c_str());
}

uintptr_t resolve(const char *id)
{
    return swofty::api ? swofty::api->resolve(id) : 0;
}

template <class T> bool rd(uintptr_t p, T &out)
{
    return p && mem::read(p, out);
}

uintptr_t current_context()
{
    uintptr_t slot = resolve("g_Game_Manager"), mgr = 0, ctx = 0;
    if (!slot) {
        once("g_Game_Manager does not resolve on this build; mode, session and track stay empty at a session load");
        return 0;
    }
    if (!rd(slot, mgr) || !mgr || !rd(mgr + offsetof(g::GameManager, mode_context), ctx) || !ctx)
        return 0;
    return ctx;
}

uintptr_t holder_of(uintptr_t ctx)
{
    uintptr_t holder = 0;
    if (!ctx || !rd(ctx + offsetof(g::GameModeContext, championship_holder), holder) || !mem::object_like(holder))
        return 0;
    return holder;
}

std::string mode_name(uintptr_t holder)
{
    uintptr_t name = 0;
    std::string out;
    if (!holder || !rd(holder + MODE_NAME, name) || !mem::c_string(name, out, 64))
        return {};
    for (char ch : out)
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_'))
            return {};
    return out;
}

std::string mode_of(uintptr_t ctx, const char *where)
{
    uintptr_t holder = holder_of(ctx);
    std::string name = mode_name(holder);
    if (name.empty()) {
        once(std::string("the game mode name could not be read at ") + where + "; mode unknown");
        return "unknown";
    }
    std::string mode = facts::mode_of(name);
    once("game mode " + name + " is " + mode);
    return mode;
}

bool vtable_is(uintptr_t obj, const char *id)
{
    uintptr_t want = resolve(id), vt = 0;
    return want && rd(obj, vt) && vt == want;
}

struct Weekend {
    uint32_t session = 0;
    bool sprint = false;
    int track = -1;
};

bool weekend(uintptr_t holder, Weekend &out)
{
    uintptr_t entity = 0, champ = 0, sb = 0, se = 0, season = 0, wb = 0, we = 0, wk = 0, xb = 0, xe = 0;
    uint32_t si = 0, wi = 0, xi = 0;
    if (!resolve("vt_Career_Championship") || !resolve("vt_Career_Season") || !resolve("vt_Career_Weekend")) {
        once("vt_Career_Championship, vt_Career_Season or vt_Career_Weekend does not resolve on this build; session "
             "and track stay empty");
        return false;
    }
    if (!rd(holder + offsetof(g::ChampionshipHolder, entity), entity) || !mem::object_like(entity) ||
        !rd(entity + offsetof(g::CareerEntity, current_championship), champ) ||
        !vtable_is(champ, "vt_Career_Championship"))
        return false;
    if (!rd(champ + offsetof(g::Championship, seasons_begin), sb) ||
        !rd(champ + offsetof(g::Championship, seasons_end), se) ||
        !rd(champ + offsetof(g::Championship, season_index), si) || se < sb ||
        (se - sb) / sizeof(g::ChampionshipSeasonEntry) > MAX_SEASONS ||
        si >= (se - sb) / sizeof(g::ChampionshipSeasonEntry) ||
        !rd(sb + si * sizeof(g::ChampionshipSeasonEntry) + offsetof(g::ChampionshipSeasonEntry, season), season) ||
        !vtable_is(season, "vt_Career_Season"))
        return false;
    if (!rd(season + offsetof(g::Season, weekends_begin), wb) || !rd(season + offsetof(g::Season, weekends_end), we) ||
        !rd(season + offsetof(g::Season, current_weekend), wi) || we < wb || (we - wb) / 8 > MAX_ROUNDS ||
        wi >= (we - wb) / 8 || !rd(wb + wi * 8, wk) || !vtable_is(wk, "vt_Career_Weekend"))
        return false;
    uint8_t track = 0xff;
    if (rd(wk + offsetof(g::Weekend, track_stat_id), track) && track != 0xff)
        out.track = track;
    if (!rd(wk + offsetof(g::Weekend, sessions_begin), xb) || !rd(wk + offsetof(g::Weekend, sessions_end), xe) ||
        !rd(wk + offsetof(g::Weekend, current_session), xi) || xe < xb || (xe - xb) / SESSION_STRIDE > MAX_SESSIONS)
        return true;
    std::vector<uint32_t> ids;
    for (uintptr_t at = xb; at < xe; at += SESSION_STRIDE) {
        uint32_t id = 0;
        if (rd(at + offsetof(g::Session, session_id), id))
            ids.push_back(id);
    }
    out.sprint = facts::sprint_weekend(ids);
    if (xi < ids.size())
        out.session = ids[xi];
    return true;
}

std::mutex tracks_mu;
std::map<int, std::string> track_labels;
bool tracks_read = false;

std::string track_label(int stat_id)
{
    std::lock_guard g(tracks_mu);
    if (!tracks_read) {
        const SwoftyDb *db = runtime::db_table();
        SwoftyDbCursor cur{};
        if (!db || !db->available || !db->available() || db->open("tracks", &cur) != SWOFTY_ASSET_OK) {
            once("the tracks table is not readable yet; the track is left empty");
            return {};
        }
        tracks_read = true;
        do {
            SwoftyDbValue v{};
            const char *label = db->row_name(&cur);
            if (label && db->cell(&cur, "statId", &v) == SWOFTY_ASSET_OK)
                track_labels[int(v.i)] = label;
        } while (db->step(&cur) == SWOFTY_ASSET_OK);
    }
    auto it = track_labels.find(stat_id);
    return it == track_labels.end() ? std::string() : it->second;
}

std::string track_id(int stat_id)
{
    if (stat_id < 0)
        return {};
    std::vector<facts::TrackRecord> records;
    for (auto &id : reg::store().list("track", "")) {
        reg::Record r = reg::store().get("track", id);
        if (r.status == reg::Status::ok && r.value["stat_id"].is_int())
            records.push_back({id, r.value["stat_id"].as_int()});
    }
    std::string id = facts::track_of(stat_id, track_label(stat_id), records);
    if (id.empty())
        once("track stat id " + std::to_string(stat_id) + " has no tracks row or track record");
    return id;
}

SessionFacts at_session_load()
{
    SessionFacts f;
    uintptr_t ctx = current_context();
    if (!ctx)
        return f;
    f.mode = mode_of(ctx, "the session load");
    if (f.mode == "time_trial") {
        f.type = f.session = "time_trial";
        return f;
    }
    if (f.mode == "unknown")
        return f;
    Weekend w;
    if (!weekend(holder_of(ctx), w)) {
        once("the " + f.mode + " game mode has no current championship weekend; session and track stay empty");
        return f;
    }
    facts::Session s = facts::session_of(w.session, w.sprint);
    if (s.type.empty())
        once("session id " + std::to_string(w.session) + " is not a known session");
    f.type = s.type;
    f.session = s.session;
    f.track = track_id(w.track);
    return f;
}

}

void on_stage(int stage)
{
    if (stage == runtime::STAGE_WINDOW)
        window();
}

}

void attach()
{
    std::call_once(attached, [] {
        content::register_appliers();
        Scheduler &s = scheduler();
        s.set_log([](const std::string &line) { swofty::log("%s", line.c_str()); });
        s.set_facts(facts);
        migrate_ids();
        std::string text = read_file(registry_dir() + "\\ids.json");
        std::string why;
        if (!text.empty() && !ids::store().load(text, &why))
            swofty::log("[registry] stat ids: ids.json unreadable (%s); it is rewritten from the ids this game start "
                        "allocates",
                        why.c_str());
        else if (!text.empty())
            swofty::log("[registry] stat ids: ids.json holds %zu id(s)", ids::store().size());
        reg::set_stat_id_source([](const std::string &registry, const std::string &id, int64_t *out) {
            std::string line;
            int r = stat_id(reg::store(), ids::store(), registry, id, out, today(), &line);
            if (!line.empty())
                swofty::log("%s", line.c_str());
            save_ids();
            return r;
        });
        runtime::on_stage(on_stage);
    });
}

std::size_t boot_plan()
{
    attach();
    return scheduler().plan_boot();
}

bool boot_work()
{
    return scheduler().boot_work();
}

void status(const std::string &addon, const std::string &line)
{
    std::lock_guard g(status_mu);
    status_lines[addon.empty() ? std::string("swofty.core") : addon].push_back(line);
}

void write_room(const room::Report &report)
{
    room::Report r = report;
    r.pe_timestamp = pe_timestamp();
    if (r.profile.empty())
        r.profile = reg::store().context().profile;
    std::string path = registry_dir() + "\\room.json";
    bool ok = write_file(path, swofty::json::dump(room::to_json(r), 1));
    swofty::log("%s", room::line(r).c_str());
    swofty::log("[registry] room report %s%s", ok ? "written to " : "NOT written to ", path.c_str());
}

void boot_load(room::Report &report)
{
    (void)report;
    attach();
    scheduler().run_boot();
    save_ids();
}

void boot_without_database(room::Report report)
{
    attach();
    scheduler().run_boot();
    auto rows = db::staging().close();
    std::map<std::string, std::size_t> refused;
    for (auto &row : rows)
        refused[row.addon]++;
    for (auto &[addon, n] : refused) {
        status(addon, std::to_string(n) + " database record(s) not applied: " +
                          (report.reason.empty() ? std::string("the database was not reachable") : report.reason));
        swofty::log("[registry] register: %s %zu database record(s) refused (late): %s", addon.c_str(), n,
                    report.reason.c_str());
    }
    report.registry_asked = scheduler().planned_keys();
    save_ids();
    write_room(report);
    if (runtime::load_stage() != "early")
        window();
}

void career_loaded(const char *key, unsigned mode)
{
    attach();
    char group[128] = {};
    if (auto t = runtime::career_content_table())
        if (t->data_group && t->data_group(nullptr, group, sizeof group) != SWOFTY_ASSET_OK)
            group[0] = 0;
    scheduler().career_loaded(key ? key : "", mode_name(mode), group);
}

void career_renamed(const char *from, const char *to)
{
    attach();
    ids::store().forget_career(from ? from : "");
    map_for_career(to ? to : "");
}

void career_unloaded()
{
    attach();
    scheduler().career_unloaded();
}

void career_season(int year)
{
    attach();
    scheduler().career_season(year);
}

void box_picked(const char *entrant_list)
{
    box_picked(entrant_list, nullptr);
}

void box_picked(const char *entrant_list, const void *mode_context)
{
    attach();
    uintptr_t ctx = mode_context ? reinterpret_cast<uintptr_t>(mode_context) : game::current_context();
    scheduler().content_picked(entrant_list ? entrant_list : "", game::mode_of(ctx, "the box press"));
}

void session_load(const char *source)
{
    attach();
    std::string from = source ? source : "";
    bool flow = from == "StateLoadSession";
    if (!flow && scheduler().in_session() && loaded_from_flow.load())
        return;
    loaded_from_flow = flow;
    scheduler().session_load(game::at_session_load());
    write_reports(from.c_str());
}

void session_end(const char *source)
{
    attach();
    loaded_from_flow = false;
    scheduler().session_end(source ? source : "the session ended");
}

void window()
{
    attach();
    scheduler().install_read();
}

int write_reports(const char *when)
{
    save_ids();
    make_registry_dir();
    int r = reg::write_reports(registry_dir(), extend);
    swofty::log("[registry] effective.json and conflicts.json %s (%s)", r == SWOFTY_ASSET_OK ? "written" : "NOT written",
                when ? when : "");
    return r;
}

namespace {

DWORD WINAPI crash_writer(void *)
{
    write_reports("crash");
    return 0;
}

}

void crash_reports()
{
    HANDLE t = CreateThread(nullptr, 0, crash_writer, nullptr, 0, nullptr);
    if (!t)
        return;
    if (WaitForSingleObject(t, 3000) != WAIT_OBJECT_0)
        swofty::log("[registry] effective.json and conflicts.json not written on the crash: the registry stayed busy");
    CloseHandle(t);
}

ids::CareerMap map_for_career(const std::string &career)
{
    attach();
    for (auto *a : appliers())
        ids::store().set_live(a->registry(), reg::store().list(a->registry(), ""));
    ids::CareerMap m = ids::store().map_for_career(career);
    save_ids();
    return m;
}

std::vector<ids::Clash> adopt(const std::string &career, const ids::CareerMap &map)
{
    attach();
    auto clashes = ids::store().adopt(career, map, today());
    for (auto &c : clashes)
        swofty::log("[guards] career.load: %s", c.line.c_str());
    save_ids();
    return clashes;
}

}
