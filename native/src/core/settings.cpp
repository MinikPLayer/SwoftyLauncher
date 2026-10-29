#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include "json.hpp"
#include "registry/older.hpp"
#include "runtime.hpp"
#include "swofty_api.h"

bool ui_vm_set_path(const std::string &path, int type, const void *value);
bool settings_command(uint32_t hash);

namespace {

enum { SET = 1, ENABLE, RESTART };

struct State {
    json::Value value;
    std::string text, command;
};

struct Watcher {
    SwoftySettingFn fn;
    void *user;
};

struct Option {
    std::string id;
    uint32_t type;
    std::vector<State> states;
    size_t current;
    bool requested = false;
    std::vector<Watcher> watchers;
};

struct AddonSettings {
    std::string id;
    std::vector<std::wstring> dlls;
    std::vector<std::string> enable_commands;
    std::vector<Option> options;
    int enabled = -1;
};

struct Command {
    int kind;
    size_t addon, option, state;
};

struct MfdRow {
    size_t addon, option;
};

struct Owner {
    int addon;
    std::string dll;
};

const char *const type_names[] = {"?", "toggle", "slider", "choice"};

SwoftyApi *api;
SRWLOCK lock = SRWLOCK_INIT, io = SRWLOCK_INIT;
bool loaded, relaunch;
unsigned long long seq;
std::string profile;
std::wstring requests;
std::vector<AddonSettings> addons;
std::vector<MfdRow> mfd_rows;
std::unordered_map<uint32_t, Command> commands;
std::unordered_map<HMODULE, Owner> owners;
std::set<std::string> warned;

uint32_t djb2(const std::string &s)
{
    uint32_t h = 5381;
    for (unsigned char c : s)
        h = h * 33 + c;
    return h;
}

std::wstring wide(const std::string &s)
{
    std::wstring w(s.size(), L'\0');
    w.resize(MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), (int)w.size()));
    return w;
}

std::string utf8(const std::wstring &w)
{
    std::string s(w.size() * 3, '\0');
    s.resize(WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), (int)s.size(), nullptr, nullptr));
    return s;
}

std::wstring full(const std::wstring &p)
{
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetFullPathNameW(p.c_str(), MAX_PATH * 2, buf, nullptr);
    return n && n < MAX_PATH * 2 ? std::wstring(buf, n) : p;
}

std::wstring base_name(const std::wstring &p)
{
    size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? p : p.substr(slash + 1);
}

std::string read_file(const std::wstring &path)
{
    std::string out;
    if (FILE *f = _wfopen(path.c_str(), L"rb")) {
        char buf[65536];
        for (size_t n; (n = fread(buf, 1, sizeof buf, f));)
            out.append(buf, n);
        fclose(f);
    }
    return out;
}

std::string quote(const std::string &s)
{
    std::string out = "\"";
    for (unsigned char c : s) {
        char esc[8];
        if (c == '"' || c == '\\')
            out += '\\', out += (char)c;
        else if (c < 0x20)
            snprintf(esc, sizeof esc, "\\u%04x", c), out += esc;
        else
            out += (char)c;
    }
    return out + '"';
}

std::string json_of(const json::Value &v)
{
    char b[40] = "null";
    if (v.type == json::Value::Bool)
        return v.b ? "true" : "false";
    if (v.type == json::Value::Str)
        return quote(v.s);
    if (v.type == json::Value::Num && v.n == std::floor(v.n) && std::fabs(v.n) < 1e15)
        snprintf(b, sizeof b, "%lld", (long long)v.n);
    else if (v.type == json::Value::Num)
        for (int p = 1; p <= 17; p++)
            if (snprintf(b, sizeof b, "%.*g", p, v.n), strtod(b, nullptr) == v.n)
                break;
    return b;
}

std::string object(const std::vector<std::string> &items)
{
    std::string out = "{";
    for (size_t i = 0; i < items.size(); i++)
        out += (i ? ", " : " ") + items[i];
    return out + (items.empty() ? "}" : " }");
}

std::string requests_text()
{
    std::vector<std::string> options, enabled;
    for (auto &m : addons) {
        std::vector<std::string> mine;
        for (auto &o : m.options)
            if (o.requested)
                mine.push_back(quote(o.id) + ": " + json_of(o.states[o.current].value));
        if (!mine.empty())
            options.push_back(quote(m.id) + ": " + object(mine));
        if (m.enabled >= 0)
            enabled.push_back(quote(m.id) + ": " + (m.enabled ? "true" : "false"));
    }
    return "{\n  \"schema\": 1,\n  \"profile\": " + quote(profile) + ",\n  \"seq\": " + std::to_string(seq) +
           ",\n  \"options\": " + object(options) + ",\n  \"enabled\": " + object(enabled) +
           ",\n  \"relaunch\": " + (relaunch ? "true" : "false") + "\n}\n";
}

DWORD write_requests(const std::string &text)
{
    for (size_t i = requests.find_first_of(L"\\/", 3); i != std::wstring::npos; i = requests.find_first_of(L"\\/", i + 1))
        CreateDirectoryW(requests.substr(0, i).c_str(), nullptr);
    std::wstring tmp = requests + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return GetLastError();
    DWORD n = 0, err = 0;
    if (!WriteFile(f, text.data(), (DWORD)text.size(), &n, nullptr) || n != text.size())
        err = GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
    CloseHandle(f);
    if (!err && !MoveFileExW(tmp.c_str(), requests.c_str(), MOVEFILE_REPLACE_EXISTING))
        err = GetLastError();
    return err;
}

BOOL CALLBACK close_main(HWND h, LPARAM count)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(h) && !GetWindow(h, GW_OWNER) && PostMessageW(h, WM_CLOSE, 0, 0))
        ++*reinterpret_cast<int *>(count);
    return TRUE;
}

SwoftySettingValue value_of(const Option &o, size_t at)
{
    const State &s = o.states[at];
    SwoftySettingValue v{o.type, 0, 0, nullptr};
    if (o.type == SWOFTY_SETTING_TOGGLE)
        v.toggle = s.value.b;
    else if (o.type == SWOFTY_SETTING_SLIDER)
        v.slider = s.value.n;
    else
        v.choice = s.value.s.c_str();
    return v;
}

namespace older = swofty_core::registry::older;

swofty::json::Value to_record_value(const json::Value &v)
{
    if (v.type == json::Value::Bool)
        return swofty::json::Value(v.b);
    if (v.type == json::Value::Num)
        return v.n == std::floor(v.n) && std::fabs(v.n) < 9e15 ? swofty::json::Value(int64_t(v.n)) : swofty::json::Value(v.n);
    if (v.type == json::Value::Str)
        return swofty::json::Value(v.s);
    return swofty::json::Value();
}

std::vector<older::OptionState> option_states(const Option &o)
{
    std::vector<older::OptionState> out;
    for (auto &st : o.states)
        out.push_back({st.text, to_record_value(st.value)});
    return out;
}

void put_option(const std::string &addon, const Option &o)
{
    if (addon.empty())
        return;
    older::put(runtime::older_store(), addon, older::OPTION, o.id, older::option_record(type_names[o.type], option_states(o), o.current),
               "settings.json options");
}

const char *invalid(const Option &o)
{
    if (!o.type)
        return "unknown type";
    if (o.current >= o.states.size())
        return "current is not a state index";
    auto want = o.type == SWOFTY_SETTING_TOGGLE ? json::Value::Bool
                : o.type == SWOFTY_SETTING_SLIDER ? json::Value::Num
                                                  : json::Value::Str;
    for (auto &s : o.states)
        if (s.value.type != want || (want == json::Value::Num && !std::isfinite(s.value.n)))
            return "a state value does not fit the type";
    return nullptr;
}

void add_command(const std::string &name, Command c)
{
    if (!name.empty() && !commands.try_emplace(djb2(name), c).second)
        api->log("[settings] command %s has the same djb2 hash as another command; ignored", name.c_str());
}

const Owner *owner_of(const void *addr)
{
    HMODULE m = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(addr), &m);
    AcquireSRWLockShared(&lock);
    auto it = owners.find(m);
    const Owner *known = it == owners.end() ? nullptr : &it->second;
    ReleaseSRWLockShared(&lock);
    if (known)
        return known;
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(m, path, MAX_PATH);
    std::wstring dll = base_name(path);
    Owner o{-1, utf8(dll)};
    for (size_t i = 0; i < addons.size() && o.addon < 0; i++)
        for (auto &d : addons[i].dlls)
            if (!_wcsicmp(d.c_str(), dll.c_str()))
                o.addon = (int)i;
    AcquireSRWLockExclusive(&lock);
    const Owner *out = &owners.try_emplace(m, o).first->second;
    ReleaseSRWLockExclusive(&lock);
    return out;
}

Option *find_option(const Owner *w, const char *id)
{
    if (w->addon < 0)
        return nullptr;
    for (auto &o : addons[w->addon].options)
        if (o.id == id)
            return &o;
    return nullptr;
}

}

void settings_attach(SwoftyApi *a, const std::wstring &dir)
{
    api = a;
    std::wstring path = full(dir + L"..\\..\\settings.json");
    std::string text = read_file(path);
    json::Value doc;
    if (text.empty())
        return api->log("[settings] no settings.json; every addon setting keeps its code default");
    if (!json::parse(text, doc) || doc["schema"].num() != 1)
        return api->log("[settings] %ls is unreadable or not schema 1; every addon setting keeps its code default",
                        path.c_str());
    profile = doc["profile"].str();
    std::string req = doc["requests"].str();
    requests = full(req.empty() ? dir + L"..\\..\\requests.json" : wide(req));
    size_t count = 0;
    for (auto &jm : doc.field("addons", "mods").a) {
        AddonSettings m{jm["id"].str(), {}, {}, {}};
        for (auto &d : jm["dlls"].a)
            m.dlls.push_back(base_name(wide(d.str())));
        for (auto &c : jm["enable_commands"].a)
            m.enable_commands.push_back(c.str());
        for (auto &jo : jm["options"].a) {
            Option o{jo["id"].str(), 0, {}, (size_t)jo["current"].num(-1), false, {}};
            for (uint32_t t = 1; t < 4; t++)
                if (jo["type"].str() == type_names[t])
                    o.type = t;
            for (auto &s : jo["states"].a)
                o.states.push_back({s["value"], s["text"].str(), s["command"].str()});
            if (const char *why = invalid(o))
                api->log("[settings] %s.%s: %s; option ignored", m.id.c_str(), o.id.c_str(), why);
            else
                m.options.push_back(std::move(o)), count++;
        }
        bool on = jm["enabled"].type == json::Value::Bool ? jm["enabled"].b : true;
        for (auto &r : jm["mfd"]["rows"].a)
            for (size_t j = 0; on && j < m.options.size(); j++)
                if (m.options[j].id == r.str() && mfd_rows.size() < 8)
                    mfd_rows.push_back({addons.size(), j});
        addons.push_back(std::move(m));
    }
    for (size_t i = 0; i < addons.size(); i++) {
        for (size_t k = 0; k < addons[i].enable_commands.size() && k < 2; k++)
            add_command(addons[i].enable_commands[k], {ENABLE, i, 0, k});
        for (size_t j = 0; j < addons[i].options.size(); j++)
            for (size_t k = 0; k < addons[i].options[j].states.size(); k++)
                add_command(addons[i].options[j].states[k].command, {SET, i, j, k});
    }
    add_command(doc["restart_command"].str(), {RESTART, 0, 0, 0});
    loaded = true;
    for (auto &m : addons)
        for (auto &o : m.options)
            put_option(m.id, o);
    api->log("[settings] %ls: profile '%s', %zu addon(s), %zu option(s), %zu command(s); requests file %ls", path.c_str(),
             profile.c_str(), addons.size(), count, commands.size(), requests.c_str());
}

size_t settings_mfd_rows()
{
    return mfd_rows.size();
}

bool settings_mfd_step(size_t row, int dir)
{
    if (row >= mfd_rows.size())
        return false;
    AcquireSRWLockExclusive(&lock);
    const Option &o = addons[mfd_rows[row].addon].options[mfd_rows[row].option];
    size_t n = o.states.size(), at = o.current < n ? o.current : 0;
    std::string command = n ? o.states[(at + n + (dir < 0 ? n - 1 : 1)) % n].command : std::string();
    ReleaseSRWLockExclusive(&lock);
    return !command.empty() && settings_command(djb2(command));
}

std::string settings_addon(const void *from, std::string &dll)
{
    const Owner *w = owner_of(from);
    dll = w->dll;
    return w->addon < 0 ? std::string() : addons[w->addon].id;
}

int settings_get(const char *id, uint32_t type, SwoftySettingValue *out)
{
    const void *from = __builtin_return_address(0);
    if (!loaded || !id)
        return 0;
    const Owner *w = owner_of(from);
    swofty::json::Value rec;
    if (w->addon >= 0)
        rec = older::effective(runtime::older_store(), older::OPTION, addons[w->addon].id + "/" + id);
    AcquireSRWLockExclusive(&lock);
    const Option *o = find_option(w, id);
    bool ok = o && o->type == type;
    bool warn = !ok && warned.insert((w->addon < 0 ? w->dll : addons[w->addon].id) + '\n' + id).second;
    if (ok && out) {
        *out = value_of(*o, older::option_current(rec, option_states(*o), o->current));
    }
    ReleaseSRWLockExclusive(&lock);
    const char *want = type < 4 ? type_names[type] : "?";
    if (warn && w->addon < 0)
        api->log("[settings] %s asked for %s, but no addon in settings.json lists that dll; the code default is used",
                 w->dll.c_str(), id);
    else if (warn && !o)
        api->log("[settings] %s (%s): no option %s in settings.json; the code default is used", w->dll.c_str(),
                 addons[w->addon].id.c_str(), id);
    else if (warn)
        api->log("[settings] %s (%s): %s is a %s option, the code reads a %s; the code default is used",
                 w->dll.c_str(), addons[w->addon].id.c_str(), id, type_names[o->type], want);
    return ok;
}

int settings_watch(const char *id, SwoftySettingFn fn, void *user)
{
    const void *from = __builtin_return_address(0);
    if (!loaded || !id || !fn)
        return 0;
    const Owner *w = owner_of(from);
    AcquireSRWLockExclusive(&lock);
    Option *o = find_option(w, id);
    if (o)
        o->watchers.push_back({fn, user});
    ReleaseSRWLockExclusive(&lock);
    return o != nullptr;
}

// Core-internal access for the hub tab (ui_vm.cpp): an option's current state index and its states'
// texts and commands, and a pick of a state by its value; the pick runs the state's command, the
// path ADDON SETTINGS takes, so the profile, the watchers and the requests file follow.
bool settings_state(const std::string &addon, const std::string &option, size_t &current,
                    std::vector<std::string> *texts, std::vector<std::string> *commands, std::vector<std::string> *values)
{
    AcquireSRWLockShared(&lock);
    bool found = false;
    for (auto &m : addons) {
        if (m.id != addon)
            continue;
        for (auto &o : m.options) {
            if (o.id != option)
                continue;
            current = o.current;
            for (auto &st : o.states) {
                if (texts)
                    texts->push_back(st.text);
                if (commands)
                    commands->push_back(st.command);
                if (values)
                    values->push_back(st.value.type == json::Value::Str    ? st.value.s
                                      : st.value.type == json::Value::Bool ? (st.value.b ? "true" : "false")
                                                                           : std::to_string(st.value.n));
            }
            found = true;
        }
    }
    ReleaseSRWLockShared(&lock);
    return found;
}

void hub_option_changed(const std::string &addon, const std::string &option);

bool settings_command(uint32_t hash)
{
    auto it = commands.find(hash);
    if (it == commands.end())
        return false;
    const Command c = it->second;
    AddonSettings *m = c.kind == RESTART ? nullptr : &addons[c.addon];
    Option *o = c.kind == SET ? &m->options[c.option] : nullptr;
    std::vector<Watcher> call;
    SwoftySettingValue v{};
    Option changed;
    AcquireSRWLockExclusive(&io);
    AcquireSRWLockExclusive(&lock);
    if (o) {
        o->current = c.state;
        o->requested = true;
        call = o->watchers;
        v = value_of(*o, o->current);
        changed = *o;
    } else if (m) {
        m->enabled = (int)c.state;
    } else {
        relaunch = true;
    }
    unsigned long long n = ++seq;
    std::string text = requests_text();
    ReleaseSRWLockExclusive(&lock);
    if (o)
        put_option(m->id, changed);
    if (o)
        api->log("[settings] %s.%s = %s", m->id.c_str(), o->id.c_str(), o->states[c.state].text.c_str());
    else if (m)
        api->log("[settings] %s: %s from the next launch", m->id.c_str(), c.state ? "enabled" : "disabled");
    else
        api->log("[settings] restart requested: relaunch = true");
    for (size_t k = 0; o && k < mfd_rows.size(); k++)
        if (mfd_rows[k].addon == c.addon && mfd_rows[k].option == c.option)
            ui_vm_set_path("ui_menu_data.swofty.m" + std::to_string(k), 0, o->states[c.state].text.c_str());
    DWORD err = write_requests(text);
    ReleaseSRWLockExclusive(&io);
    if (err)
        api->log("[settings] writing requests file %ls (seq %llu) failed, error %lu", requests.c_str(), n, err);
    for (auto &w : call)
        w.fn(o->id.c_str(), &v, w.user);
    if (o)
        hub_option_changed(m->id, o->id);
    if (c.kind == RESTART) {
        int closed = 0;
        EnumWindows(close_main, reinterpret_cast<LPARAM>(&closed));
        api->log("[settings] restart: WM_CLOSE posted to %d main window(s)", closed);
    }
    return true;
}
