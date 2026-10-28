#include "runtime.hpp"

#include <swofty/mixin.hpp>

#include "../loader/data_folder.h"
#include "perf.hpp"
#include "storage_logic.hpp"

#include <cstdio>
#include <unordered_map>

std::string settings_addon(const void *from, std::string &dll);

namespace {

namespace sl = runtime::storage_logic;

const char *const RESULTS[] = {"ok", "late", "unavailable", "invalid"};

struct Caller {
    std::string id, dll;
};

Caller caller(const void *from)
{
    Caller c;
    c.id = settings_addon(from, c.dll);
    return c;
}

constexpr uint32_t QUIET_AFTER = 3;
SRWLOCK seen_lock = SRWLOCK_INIT;
std::unordered_map<std::string, uint32_t> seen;

bool log_every_call()
{
    static const bool all = [] {
        char v[8] = {};
        DWORD n = GetEnvironmentVariableA("SWOFTY_STORAGE_LOG", v, sizeof v);
        return n && n < sizeof v && !_stricmp(v, "all");
    }();
    return all;
}

int done(const Caller &c, const std::string &call, int r, const std::string &why = {})
{
    const char *id = c.id.empty() ? "no addon" : c.id.c_str();
    if (r == SWOFTY_ASSET_OK && !log_every_call()) {
        std::string key = std::string(id) + '\n' + call.substr(0, call.find(" ("));
        AcquireSRWLockExclusive(&seen_lock);
        uint32_t n = ++seen[key];
        ReleaseSRWLockExclusive(&seen_lock);
        if (n > QUIET_AFTER) {
            if (n == QUIET_AFTER + 1)
                swofty::log("[storage] api: %s (%s) %s -> ok; further successful calls like this are not logged "
                            "(failures still are; SWOFTY_STORAGE_LOG=all logs every call)",
                            id, c.dll.c_str(), call.c_str());
            return r;
        }
    }
    swofty::log("[storage] api: %s (%s) %s -> %s%s%s", id, c.dll.c_str(), call.c_str(), RESULTS[r],
                why.empty() ? "" : ": ", why.c_str());
    return r;
}

uint32_t probe(const Caller &c, const char *op)
{
    return perf::live() ? perf::probe(c.id.empty() ? c.dll : c.id, std::string("api storage ") + op) : 0;
}

std::string bytes(size_t n)
{
    char b[48];
    snprintf(b, sizeof b, " (%zu byte(s))", n);
    return b;
}

struct Root {
    std::wstring game, data;
};

SRWLOCK roots_lock = SRWLOCK_INIT;
std::unordered_map<std::string, Root> roots;

void make_dirs(const std::wstring &full, size_t from)
{
    for (size_t i = full.find_first_of(L"\\/", from); i != std::wstring::npos; i = full.find_first_of(L"\\/", i + 1))
        CreateDirectoryW(full.substr(0, i).c_str(), nullptr);
}

DWORD game_refuses(const std::wstring &dir)
{
    make_dirs(dir, runtime::swofty_root().size());
    HANDLE h = CreateFileW((dir + runtime::wide(sl::PROBE)).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        return e ? e : ERROR_ACCESS_DENIED;
    }
    CloseHandle(h);
    return 0;
}

std::wstring data_folder()
{
    wchar_t buf[MAX_PATH * 2];
    DWORD n = swofty_data_folder(buf, MAX_PATH * 2);
    return std::wstring(buf, n);
}

std::string where(const std::string &id, const std::string &rel)
{
    return "swofty/addons/" + id + (rel.empty() ? "" : "/" + rel);
}

const Root &root_of(const std::string &id)
{
    AcquireSRWLockExclusive(&roots_lock);
    auto it = roots.find(id);
    if (it == roots.end()) {
        Root r;
        r.game = runtime::swofty_root() + L"addons\\" + runtime::wide(id) + L"\\";
        std::string shown = where(id, "") + "/";
        DWORD e = game_refuses(r.game);
        if (!e) {
            swofty::log("[storage] %s: files in %s", id.c_str(), shown.c_str());
        } else if (std::wstring folder = data_folder(); folder.empty()) {
            swofty::log("[storage] %s: the game can't create files in %s (error %lu) and LOCALAPPDATA is not set, so "
                        "writes fail",
                        id.c_str(), shown.c_str(), e);
        } else {
            r.data = sl::data_root(folder, runtime::wide(id));
            make_dirs(r.data, folder.size());
            char why[256];
            snprintf(why, sizeof why,
                     "The game can't create files in %s (error %lu), so the core keeps %s's files here this session.\r\n",
                     shown.c_str(), e, id.c_str());
            runtime::write_file(r.data + runtime::wide(sl::MARK), why);
            swofty::log("[storage] %s: the game can't create files in %s (error %lu), so this session its files are in "
                        "%s, and a file not there is read from %s",
                        id.c_str(), shown.c_str(), e, runtime::narrow(r.data).c_str(), shown.c_str());
        }
        it = roots.emplace(id, std::move(r)).first;
    }
    const Root &r = it->second;
    ReleaseSRWLockExclusive(&roots_lock);
    return r;
}

bool resolve(const Caller &c, const char *path, bool allow_root, const Root *&r, std::string &rel, std::string &why)
{
    if (c.id.empty())
        return why = "no addon in settings.json lists " + c.dll + ", so it has no folder", false;
    if (!sl::inside(path, allow_root, rel))
        return why = "'" + rel + "' is not a relative path inside swofty/addons/" + c.id + "/", false;
    r = &root_of(c.id);
    return true;
}

std::wstring game_at(const Root &r, const std::string &rel)
{
    return sl::join(r.game, runtime::wide(rel));
}

std::wstring data_at(const Root &r, const std::string &rel)
{
    return sl::join(r.data, runtime::wide(rel));
}

std::wstring write_at(const Root &r, const std::string &rel)
{
    std::wstring full = r.data.empty() ? game_at(r, rel) : data_at(r, rel);
    make_dirs(full, (r.data.empty() ? r.game : r.data).size());
    return full;
}

std::string missing(const Caller &c, const Root &r, const std::string &rel)
{
    if (r.data.empty())
        return where(c.id, rel) + " is missing";
    return rel + " is in neither " + runtime::narrow(r.data) + " nor " + where(c.id, "") + "/";
}

bool locate(const Root &r, const std::string &rel, std::wstring &full, uint64_t &n)
{
    if (!r.data.empty() && runtime::file_size(full = data_at(r, rel), &n))
        return true;
    return runtime::file_size(full = game_at(r, rel), &n);
}

bool listing(const std::wstring &dir, std::string &out)
{
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((dir + (!dir.empty() && dir.back() == L'\\' ? L"*" : L"\\*")).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    do {
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..")
            continue;
        out += runtime::narrow(n);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            out += '/';
        out += '\n';
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return true;
}

bool gone(DWORD e)
{
    return e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND;
}

int st_available()
{
    return runtime::enabled("storage", nullptr) ? 1 : 0;
}

int st_read(const char *path, void *buf, size_t cap, size_t *len)
{
    Caller c = caller(__builtin_return_address(0));
    perf::Scope p(probe(c, "read"));
    std::string call = std::string("read ") + (path ? path : "(null)");
    const Root *r = nullptr;
    std::string rel, why;
    if (!resolve(c, path, false, r, rel, why))
        return done(c, call, SWOFTY_ASSET_INVALID, why);
    std::wstring full;
    uint64_t n = 0;
    if (!locate(*r, rel, full, n))
        return done(c, call, SWOFTY_ASSET_INVALID, missing(c, *r, rel));
    if (len)
        *len = size_t(n);
    if (n > cap)
        return done(c, call, SWOFTY_ASSET_INVALID, "the file is larger than the buffer");
    if (n && !buf)
        return done(c, call, SWOFTY_ASSET_INVALID, "no buffer");
    std::string data = runtime::read_file(full, size_t(n));
    if (data.size() != n)
        return done(c, call, SWOFTY_ASSET_INVALID, "the file could not be read");
    if (n)
        memcpy(buf, data.data(), size_t(n));
    return done(c, call + bytes(size_t(n)), SWOFTY_ASSET_OK);
}

int st_write(const char *path, const void *data, size_t len)
{
    Caller c = caller(__builtin_return_address(0));
    perf::Scope p(probe(c, "write"));
    std::string call = std::string("write ") + (path ? path : "(null)") + bytes(len);
    if (len && !data)
        return done(c, call, SWOFTY_ASSET_INVALID, "no data");
    const Root *r = nullptr;
    std::string rel, why;
    if (!resolve(c, path, false, r, rel, why))
        return done(c, call, SWOFTY_ASSET_INVALID, why);
    if (!runtime::write_file(write_at(*r, rel), len ? std::string(static_cast<const char *>(data), len) : std::string()))
        return done(c, call, SWOFTY_ASSET_INVALID, "the file could not be written");
    return done(c, call, SWOFTY_ASSET_OK);
}

int st_append(const char *path, const void *data, size_t len)
{
    Caller c = caller(__builtin_return_address(0));
    perf::Scope p(probe(c, "append"));
    std::string call = std::string("append ") + (path ? path : "(null)") + bytes(len);
    if (len && !data)
        return done(c, call, SWOFTY_ASSET_INVALID, "no data");
    const Root *r = nullptr;
    std::string rel, why;
    if (!resolve(c, path, false, r, rel, why))
        return done(c, call, SWOFTY_ASSET_INVALID, why);
    std::wstring full = write_at(*r, rel);
    uint64_t n = 0;
    if (!r->data.empty() && !runtime::file_size(full, &n) && runtime::file_size(game_at(*r, rel), &n)) {
        std::string body = runtime::read_file(game_at(*r, rel), size_t(n));
        if (body.size() != n || !runtime::write_file(full, body))
            return done(c, call, SWOFTY_ASSET_INVALID,
                        where(c.id, rel) + " could not be copied to " + runtime::narrow(r->data) + " to append to it");
    }
    HANDLE f = CreateFileW(full.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return done(c, call, SWOFTY_ASSET_INVALID, "the file could not be opened");
    bool ok = true;
    for (size_t off = 0; ok && off < len;) {
        DWORD put = 0, want = DWORD(std::min<size_t>(len - off, 1u << 30));
        ok = WriteFile(f, static_cast<const char *>(data) + off, want, &put, nullptr) && put;
        off += put;
    }
    CloseHandle(f);
    return done(c, call, ok ? SWOFTY_ASSET_OK : SWOFTY_ASSET_INVALID, ok ? "" : "the write failed");
}

int st_remove(const char *path)
{
    Caller c = caller(__builtin_return_address(0));
    perf::Scope p(probe(c, "remove"));
    std::string call = std::string("remove ") + (path ? path : "(null)");
    const Root *r = nullptr;
    std::string rel, why;
    if (!resolve(c, path, false, r, rel, why))
        return done(c, call, SWOFTY_ASSET_INVALID, why);
    bool removed = false;
    uint64_t n = 0;
    std::wstring game = game_at(*r, rel);
    if (r->data.empty() || runtime::file_size(game, &n)) {
        if (DeleteFileW(game.c_str())) {
            removed = true;
        } else if (DWORD e = GetLastError(); !gone(e)) {
            char code[32];
            snprintf(code, sizeof code, " (error %lu)", e);
            return done(c, call, SWOFTY_ASSET_INVALID,
                        r->data.empty() ? std::string("the file could not be removed")
                                        : where(c.id, rel) + " stays: the game can't remove files there" + code);
        }
    }
    if (!r->data.empty()) {
        if (DeleteFileW(data_at(*r, rel).c_str()))
            removed = true;
        else if (!gone(GetLastError()))
            return done(c, call, SWOFTY_ASSET_INVALID, "the file could not be removed");
    }
    return removed ? done(c, call, SWOFTY_ASSET_OK) : done(c, call, SWOFTY_ASSET_INVALID, missing(c, *r, rel));
}

int st_size(const char *path, uint64_t *out)
{
    Caller c = caller(__builtin_return_address(0));
    perf::Scope p(probe(c, "size"));
    std::string call = std::string("size ") + (path ? path : "(null)");
    const Root *r = nullptr;
    std::string rel, why;
    if (!resolve(c, path, false, r, rel, why))
        return done(c, call, SWOFTY_ASSET_INVALID, why);
    std::wstring full;
    uint64_t n = 0;
    if (!locate(*r, rel, full, n))
        return done(c, call, SWOFTY_ASSET_INVALID, missing(c, *r, rel));
    if (out)
        *out = n;
    return done(c, call, SWOFTY_ASSET_OK);
}

int st_list(const char *dir, char *buf, size_t cap, size_t *len)
{
    Caller c = caller(__builtin_return_address(0));
    perf::Scope p(probe(c, "list"));
    std::string call = std::string("list ") + (dir && *dir ? dir : ".");
    const Root *r = nullptr;
    std::string rel, why;
    if (!resolve(c, dir, true, r, rel, why))
        return done(c, call, SWOFTY_ASSET_INVALID, why);
    std::string out, theirs;
    if (r->data.empty()) {
        if (!listing(game_at(*r, rel), theirs))
            return done(c, call, SWOFTY_ASSET_INVALID, where(c.id, rel) + " is not a folder");
        out = sl::without_reserved(theirs);
    } else {
        bool mine = listing(data_at(*r, rel), out), game = listing(game_at(*r, rel), theirs);
        if (!mine && !game)
            return done(c, call, SWOFTY_ASSET_INVALID,
                        (rel.empty() ? "." : rel) + " is a folder in neither " + runtime::narrow(r->data) + " nor " +
                            where(c.id, "") + "/");
        out = sl::merge_listing(out, theirs);
    }
    if (len)
        *len = out.size();
    if (out.size() > cap)
        return done(c, call, SWOFTY_ASSET_INVALID, "the listing is larger than the buffer");
    if (!out.empty() && buf)
        memcpy(buf, out.data(), out.size());
    return done(c, call, SWOFTY_ASSET_OK);
}

const SwoftyStorage table = {st_available, st_read, st_write, st_append, st_remove, st_size, st_list};

}

namespace runtime {

const SwoftyStorage *storage_table()
{
    return &table;
}

}
