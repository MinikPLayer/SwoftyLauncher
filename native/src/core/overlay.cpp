#include "runtime.hpp"

#include "perf.hpp"

#include <safetyhook.hpp>
#include <swofty/mixin.hpp>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_set>

namespace {

const runtime::Sig S_RESOLVE{"Fs_Resolve_Mount", "41 56 56 57 55 53 48 81 EC 30 01 00 00 4C 89 CF", 0x3f41bc0};
const runtime::Sig S_LOAD_LNG{"Loc_Load_LanguageFile", "41 56 56 57 53 48 81 EC A8 00 00 00 83 FA 12 0F 87", 0x4436480};
const runtime::Sig S_ADD_TABLE{"Loc_Add_LangTable", "41 57 41 56 41 54 56 57 55 53 48 83 EC 30 44 89 CB 44 89 C5", 0x3ad0270};
const runtime::Sig S_MEMSTREAM{"vt_Ne_MemReadStream",
                               "48 8D 05 ?? ?? ?? ?? 48 89 44 24 68 C5 F8 28 84 24 90 00 00 00 C5 F8 11 44 24 70", 0x5899750,
                               runtime::Sig::riprel, 0, 3, 7};

constexpr size_t FS_CWD = 0x370, MOUNT_PROBE = 0x131, PATH_MAX_FS = 0x104, LOC_SETS = 0x28, LOC_LANGS = 0x13;
constexpr uintptr_t LNG_FOLDER_BYTE = 0x44361da;

struct Redirect {
    std::string rel, target, source;
    std::atomic<unsigned> uses{0};
};

struct Table {
    std::string code, source, data, first_key, first_value;
    size_t strings = 0;
    uint32_t priority = 0;
};

struct MemStream {
    void *vt;
    const void *ptr;
    uint64_t size, pos;
};

using Resolve_t = void *(void *fs, void *after, const char *path, void *out, uint8_t wild, uint8_t want);
using LoadLng_t = int32_t(void *module, uint32_t lang, void *path);
using AddTable_t = int32_t(void *set, void *stream, uint32_t priority, uint8_t flag);
using Find_t = const char *(void *set, const char *key, int mode);

std::vector<std::unique_ptr<Redirect>> redirects, route_store;
std::vector<std::pair<uint64_t, Redirect *>> by_hash, routes;
std::map<std::string, Redirect *> lang_replace;
std::vector<Table> tables;
safetyhook::InlineHook h_resolve, h_lang;
SRWLOCK redirect_lock = SRWLOCK_INIT;
std::unordered_set<uint64_t> seen;
std::mutex hook_lock;
bool hook_tried, legacy_hooked;
std::mutex lang_lock;
bool lang_loaded;
AddTable_t *add_fn;
void *memstream_vt;
std::string swofty_rel;
uintptr_t base;

bool fs_safe(const std::string &p)
{
    for (char c : p)
        if (strchr("<>\\|:?*()!", c))
            return false;
    return !p.empty();
}

Redirect *add_redirect(const std::string &rel, const std::string &target, const std::string &source)
{
    auto r = std::make_unique<Redirect>();
    r->rel = runtime::lower(rel);
    r->target = target;
    r->source = source;
    Redirect *p = r.get();
    redirects.push_back(std::move(r));
    return p;
}

void walk(const std::wstring &dir, const std::string &rel, const std::string &disk, const std::string &root, unsigned depth,
          size_t &count, size_t &bad)
{
    if (depth > 12)
        return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..")
            continue;
        std::string name = runtime::narrow(n);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            walk(dir + n + L"\\", rel + name + "/", disk + name + "/", root, depth + 1, count, bad);
            continue;
        }
        std::string r = rel + name, target = root + disk + name;
        if (!fs_safe(r) || 6 + target.size() >= PATH_MAX_FS) {
            swofty::log("[assets] overlay: %s skipped: the game's paths reject (<>\\|:?*)! and paths over 0x103 characters",
                        r.c_str());
            bad++;
            continue;
        }
        add_redirect(r, target, "overlay folder");
        count++;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

void put_be(std::string &s, uint32_t v)
{
    char b[4] = {char(v >> 24), char(v >> 16), char(v >> 8), char(v)};
    s.append(b, 4);
}

uint32_t lng_hash(const std::string &key)
{
    uint32_t h = 5381;
    for (char c : key)
        h = h * 33 + uint32_t(int32_t(static_cast<signed char>(c)));
    return h;
}

std::string build_lngt(const std::vector<std::pair<std::string, std::string>> &kv)
{
    uint32_t n = uint32_t(kv.size()), nb = std::max<uint32_t>(1, n);
    std::vector<std::pair<uint32_t, size_t>> order;
    for (size_t i = 0; i < kv.size(); i++)
        order.push_back({lng_hash(kv[i].first) % nb, i});
    std::stable_sort(order.begin(), order.end(), [](auto &a, auto &b) { return a.first < b.first; });
    std::vector<std::pair<uint32_t, uint32_t>> buckets(nb, {0, 0});
    std::string sida, sidb, lngb;
    for (uint32_t i = 0; i < n; i++) {
        auto &[b, k] = order[i];
        if (!buckets[b].second)
            buckets[b].first = i;
        buckets[b].second++;
        put_be(sida, uint32_t(sidb.size()));
        put_be(sida, uint32_t(lngb.size()));
        sidb += kv[k].first;
        sidb.push_back('\0');
        lngb += kv[k].second;
        lngb.push_back('\0');
    }
    std::string body = "HSHS";
    put_be(body, 12);
    put_be(body, nb);
    put_be(body, 5381);
    put_be(body, 33);
    body += "HSHT";
    put_be(body, nb * 8);
    for (auto &[s, c] : buckets)
        put_be(body, s), put_be(body, c);
    body += "SIDA";
    put_be(body, n * 8);
    put_be(body, n);
    body += sida;
    body += "SIDB";
    put_be(body, uint32_t(sidb.size()));
    body += sidb;
    body += "LNGB";
    put_be(body, uint32_t(lngb.size()));
    body += lngb;
    std::string out = "LNGT";
    put_be(out, uint32_t(8 + body.size()));
    return out + body;
}

uint32_t get_be(const std::string &d, size_t o)
{
    auto u = reinterpret_cast<const uint8_t *>(d.data()) + o;
    return uint32_t(u[0]) << 24 | uint32_t(u[1]) << 16 | uint32_t(u[2]) << 8 | u[3];
}

bool chunk(const std::string &d, size_t &p, const char *tag, uint32_t &size, std::string &why)
{
    if (p + 8 > d.size() || d.compare(p, 4, tag)) {
        why = std::string("expected chunk ") + tag + " at offset " + std::to_string(p);
        return false;
    }
    size = get_be(d, p + 4);
    p += 8;
    if (size > d.size() - p) {
        why = std::string(tag) + " size " + std::to_string(size) + " runs past the end";
        return false;
    }
    return true;
}

bool strings_ok(const std::string &d, size_t at, uint32_t size, const std::vector<uint32_t> &offs, const char *what,
                std::string &why)
{
    if (size && d[at + size - 1] != '\0') {
        why = std::string(what) + " does not end with a NUL";
        return false;
    }
    for (uint32_t o : offs)
        if (o >= size) {
            why = std::string(what) + " offset " + std::to_string(o) + " is past its " + std::to_string(size) + " bytes";
            return false;
        }
    return true;
}

bool lngt_ok(const std::string &d, size_t &count, std::string &why)
{
    size_t p = 8;
    uint32_t size;
    if (d.size() < 8 || d.compare(0, 4, "LNGT"))
        return why = "no LNGT header", false;
    if (get_be(d, 4) != d.size())
        return why = "LNGT size " + std::to_string(get_be(d, 4)) + " is not the file size " + std::to_string(d.size()), false;
    if (!chunk(d, p, "HSHS", size, why))
        return false;
    if (size != 12)
        return why = "HSHS is " + std::to_string(size) + " bytes, not 12", false;
    uint32_t nb = get_be(d, p), seed = get_be(d, p + 4), mult = get_be(d, p + 8);
    p += 12;
    if (!nb || nb > (1u << 24))
        return why = "HSHS bucket count " + std::to_string(nb) + " is out of range", false;
    if (!chunk(d, p, "HSHT", size, why))
        return false;
    if (size != uint64_t(nb) * 8)
        return why = "HSHT is " + std::to_string(size) + " bytes for " + std::to_string(nb) + " buckets", false;
    size_t hsht = p;
    p += size;
    if (!chunk(d, p, "SIDA", size, why))
        return false;
    if (p + 4 > d.size())
        return why = "SIDA has no count", false;
    uint32_t n = get_be(d, p);
    p += 4;
    if (size != uint64_t(n) * 8 || size > d.size() - p)
        return why = "SIDA is " + std::to_string(size) + " bytes for " + std::to_string(n) + " strings", false;
    std::vector<uint32_t> keys(n), vals(n);
    for (uint32_t i = 0; i < n; i++)
        keys[i] = get_be(d, p + i * 8), vals[i] = get_be(d, p + i * 8 + 4);
    p += size;
    if (!chunk(d, p, "SIDB", size, why) || !strings_ok(d, p, size, keys, "SIDB", why))
        return false;
    size_t sidb = p;
    p += size;
    if (!chunk(d, p, "LNGB", size, why) || !strings_ok(d, p, size, vals, "LNGB", why))
        return false;
    for (uint32_t b = 0; b < nb; b++) {
        uint32_t start = get_be(d, hsht + b * 8), cnt = get_be(d, hsht + b * 8 + 4);
        if (uint64_t(start) + cnt > n)
            return why = "HSHT bucket " + std::to_string(b) + " covers strings " + std::to_string(start) + ".." +
                         std::to_string(uint64_t(start) + cnt) + " of " + std::to_string(n),
                   false;
        for (uint32_t i = start; i < start + cnt; i++) {
            uint32_t h = seed;
            for (const char *c = d.data() + sidb + keys[i]; *c; c++)
                h = h * mult + uint32_t(int32_t(static_cast<signed char>(*c)));
            if (h % nb != b)
                return why = "key '" + std::string(d.data() + sidb + keys[i]) + "' sits in bucket " + std::to_string(b) +
                             " but hashes to " + std::to_string(h % nb),
                       false;
        }
    }
    count = n;
    return true;
}

std::string code_of(const char *path)
{
    if (!path)
        return {};
    const char *f = strrchr(path, '/');
    f = f ? f + 1 : path;
    if (_strnicmp(f, "language_", 9))
        return {};
    const char *dot = strchr(f + 9, '.');
    return runtime::lower(std::string(f + 9, dot ? size_t(dot - f - 9) : strlen(f + 9)));
}

Redirect *match(const char *rel)
{
    char low[PATH_MAX_FS];
    size_t n = 0;
    for (; rel[n] && n < PATH_MAX_FS - 1; n++) {
        char c = rel[n];
        low[n] = c >= 'A' && c <= 'Z' ? char(c + 32) : c;
    }
    if (rel[n] || (n >= 7 && !memcmp(low, "swofty/", 7)))
        return nullptr;
    low[n] = 0;
    uint64_t h = runtime::fnv64(low, n);
    seen.insert(h);
    for (auto r = std::lower_bound(routes.begin(), routes.end(), std::make_pair(h, static_cast<Redirect *>(nullptr)));
         r != routes.end() && r->first == h; ++r)
        if (r->second->rel.size() == n && !memcmp(r->second->rel.data(), low, n))
            return r->second;
    auto it = std::lower_bound(by_hash.begin(), by_hash.end(), std::make_pair(h, static_cast<Redirect *>(nullptr)));
    for (; it != by_hash.end() && it->first == h; ++it)
        if (it->second->rel.size() == n && !memcmp(it->second->rel.data(), low, n))
            return it->second;
    if (!lang_replace.empty() && n > 13 && (!memcmp(low, "localisation/", 13) || !memcmp(low, "localisatioa/", 13)) &&
        n > 4 && !memcmp(low + n - 4, ".lng", 4)) {
        auto l = lang_replace.find(code_of(low));
        if (l != lang_replace.end())
            return l->second;
    }
    return nullptr;
}

void *resolve_hook(void *fs, void *after, const char *path, void *out, uint8_t wild, uint8_t want)
{
    static const uint32_t probe = perf::probe("core", "file resolve (overlay)");
    perf::Scope p(probe);
    if (!wild && path && *path) {
        const char *rel = nullptr;
        if (path[0] == '/')
            rel = !strncmp(path, "/game/", 6) ? path + 6 : nullptr;
        else if (fs && !memcmp(static_cast<const char *>(fs) + FS_CWD, "/game/", 7))
            rel = path;
        AcquireSRWLockExclusive(&redirect_lock);
        Redirect *r = rel ? match(rel) : nullptr;
        ReleaseSRWLockExclusive(&redirect_lock);
        if (r) {
            char buf[PATH_MAX_FS];
            int len = snprintf(buf, sizeof buf, "/game/%s", r->target.c_str());
            if (len > 0 && len < int(PATH_MAX_FS)) {
                void *m;
                {
                    perf::Pause game;
                    m = h_resolve.call<void *>(fs, after, static_cast<const char *>(buf), out, wild, want);
                }
                unsigned use = r->uses.fetch_add(1);
                if (use < 4) {
                    int probe = m && runtime::readable(static_cast<uint8_t *>(m) + MOUNT_PROBE, 1)
                                    ? static_cast<uint8_t *>(m)[MOUNT_PROBE]
                                    : -1;
                    swofty::log("[assets] overlay: %s -> %s (%s; resolve #%u on thread %lu: mount %p probe=%d, %s)", path, buf,
                                r->source.c_str(), use + 1, GetCurrentThreadId(), m, probe,
                                probe == 1   ? "archive: its exists-check misses, the caller resolves again"
                                : probe == 0 ? "loose files: opened from disk"
                                             : "no mount");
                }
                return m;
            }
        }
    }
    perf::Pause game;
    return h_resolve.call<void *>(fs, after, path, out, wild, want);
}

int32_t load_lang_hook(void *module, uint32_t lang, void *path)
{
    static const uint32_t probe = perf::probe("core", "language file load");
    int32_t r = h_lang.call<int32_t>(module, lang, path);
    perf::Scope prof(probe);
    const char *p = path && runtime::readable(static_cast<uint8_t *>(path) + 0x10, 8)
                        ? *reinterpret_cast<const char *const *>(static_cast<uint8_t *>(path) + 0x10)
                        : nullptr;
    std::string code = code_of(p);
    swofty::log("[assets] lang: Loc_Load_LanguageFile(lang %u, \"%s\") returned %d on thread %lu", lang, p ? p : "?", r,
                GetCurrentThreadId());
    std::lock_guard<std::mutex> g(lang_lock);
    lang_loaded = true;
    if (r != 0 || lang >= LOC_LANGS || !add_fn || !memstream_vt)
        return r;
    void *set = *reinterpret_cast<void **>(static_cast<uint8_t *>(module) + LOC_SETS + size_t(lang) * 0x10);
    if (!set)
        return r;
    auto find = reinterpret_cast<Find_t *>((*static_cast<void ***>(set))[1]);
    for (auto &t : tables) {
        if (t.code != code && t.code != "*")
            continue;
        MemStream ms{memstream_vt, t.data.data(), t.data.size(), 0};
        int32_t ar = add_fn(set, &ms, t.priority, 0);
        const char *back = find ? find(set, t.first_key.c_str(), 0) : nullptr;
        bool first = back && t.first_value == back;
        swofty::log("[assets] lang: added %zu string(s) from %s for '%s' at priority %u (Loc_Add_LangTable returned %d); "
                    "lookup \"%s\" -> \"%s\" (%s)",
                    t.strings, t.source.c_str(), code.c_str(), t.priority, ar, t.first_key.c_str(), back ? back : "(none)",
                    first ? "the mod table is searched first" : "NOT the mod's string");
    }
    return r;
}

void read_lang_entry(const std::string &code_raw, const json::Value &v)
{
    std::string code = runtime::lower(code_raw);
    uint32_t priority = v["priority"].type == json::Value::Num ? uint32_t(v["priority"].num()) : 0;
    const json::Value &strings = v["strings"];
    if (strings.type == json::Value::Obj && !strings.o.empty()) {
        std::vector<std::pair<std::string, std::string>> kv;
        std::string first_bad;
        size_t bad = 0;
        for (auto &[k, s] : strings.o)
            if (s.type == json::Value::Str && !k.empty())
                kv.push_back({k, s.s});
            else if (!bad++)
                first_bad = k;
        if (bad)
            swofty::log("[assets] lang: '%s': %zu entr(ies) in \"strings\" skipped: values must be JSON strings and keys "
                        "non-empty (first: \"%s\")",
                        code.c_str(), bad, first_bad.c_str());
        if (kv.empty())
            swofty::log("[assets] lang: '%s': no usable string in \"strings\"; no table added", code.c_str());
        else {
            Table t;
            t.code = code;
            t.source = "assets.json strings";
            t.data = build_lngt(kv);
            t.strings = kv.size();
            t.first_key = kv.front().first;
            t.first_value = kv.front().second;
            t.priority = priority;
            tables.push_back(std::move(t));
        }
    }
    std::string file = v["table"].str();
    if (!file.empty()) {
        Table t;
        t.code = code;
        t.source = file;
        t.priority = priority;
        t.data = runtime::read_file(runtime::under_swofty(file), size_t(64) << 20);
        std::string why;
        size_t n = 0;
        if (t.data.empty())
            swofty::log("[assets] lang: table %s skipped: missing or empty", file.c_str());
        else if (!lngt_ok(t.data, n, why))
            swofty::log("[assets] lang: table %s skipped: not a valid LNGT table (%s); the engine's parser checks only "
                        "chunk tags, so a bad table would make every lookup in that language read out of bounds",
                        file.c_str(), why.c_str());
        else if (!n)
            swofty::log("[assets] lang: table %s skipped: it holds no string", file.c_str());
        else {
            t.strings = n;
            size_t p = 8 + 20;
            uint32_t nb = get_be(t.data, 8 + 8);
            p += 8 + size_t(nb) * 8 + 8 + 4;
            uint32_t k0 = get_be(t.data, p), v0 = get_be(t.data, p + 4);
            size_t sidb = p + size_t(n) * 8 + 8;
            uint32_t sidb_size = get_be(t.data, sidb - 4);
            size_t lngb = sidb + sidb_size + 8;
            t.first_key = t.data.c_str() + sidb + k0;
            t.first_value = t.data.c_str() + lngb + v0;
            tables.push_back(std::move(t));
        }
    }
    std::string repl = v["replace"].str();
    if (!repl.empty() && runtime::enabled("file", ("lang replace " + repl).c_str())) {
        std::string target = swofty_rel + runtime::lower(repl);
        std::replace(target.begin(), target.end(), '\\', '/');
        uint64_t size = 0;
        if (code == "*" || !fs_safe(target) || !runtime::file_size(runtime::under_swofty(repl), &size)) {
            swofty::log("[assets] lang: replace %s for '%s' skipped: %s", repl.c_str(), code.c_str(),
                        code == "*" ? "a whole-file replacement needs one language code"
                                    : "the file is missing or its path has a character the game rejects");
            return;
        }
        lang_replace[code] = add_redirect("localisation/*/language_" + code + ".lng", target, "lang replace " + repl);
        swofty::log("[assets] lang: '%s' language file -> /game/%s (%llu bytes, whole file through the fs overlay)",
                    code.c_str(), target.c_str(), (unsigned long long)size);
    }
}

void log_tables(size_t from)
{
    for (size_t i = from; i < tables.size(); i++) {
        auto &t = tables[i];
        swofty::log("[assets] lang: table for '%s': %zu string(s) from %s, %zu bytes LNGT, priority %u (first \"%s\" = "
                    "\"%s\"); tables are searched highest priority first, newest first at equal priority",
                    t.code.c_str(), t.strings, t.source.c_str(), t.data.size(), t.priority, t.first_key.c_str(),
                    t.first_value.c_str());
    }
}

void read_lang(const json::Value &lang)
{
    for (auto &[code, v] : lang.o)
        read_lang_entry(code, v);
    for (auto &v : runtime::sdk()["lang"].a)
        read_lang_entry(v["language"].str(), v);
    log_tables(0);
}

void add_file(const json::Value &v)
{
    Redirect *r = add_redirect(v["path"].str(), swofty_rel + v["file"].str(), "addon " + v["origin"].str());
    by_hash.insert(std::upper_bound(by_hash.begin(), by_hash.end(), std::make_pair(runtime::fnv64(r->rel), r)),
                   {runtime::fnv64(r->rel), r});
    swofty::log("[assets] overlay:   %s -> /game/%s (%s)", r->rel.c_str(), r->target.c_str(), r->source.c_str());
}

template <class F> bool hook(safetyhook::InlineHook &h, uintptr_t at, F *fn, const char *what)
{
    auto r = safetyhook::InlineHook::create(reinterpret_cast<void *>(at), reinterpret_cast<void *>(fn));
    if (!r) {
        swofty::log("[assets] %s: hook at base+0x%llx failed", what, (unsigned long long)(at - base));
        return false;
    }
    h = std::move(*r);
    swofty::log("[assets] %s: hooked at base+0x%llx", what, (unsigned long long)(at - base));
    return true;
}

bool hook_resolve()
{
    std::lock_guard<std::mutex> g(hook_lock);
    if (h_resolve)
        return true;
    if (hook_tried)
        return false;
    hook_tried = true;
    if (!base)
        base = swofty::api->module_base();
    if (uintptr_t at = runtime::find(S_RESOLVE, "overlay"))
        return hook(h_resolve, at, &resolve_hook, "overlay: Fs_Resolve_Mount");
    return false;
}

bool swofty_prefix(std::string &out, std::string &why)
{
    std::string root = runtime::lower(runtime::narrow(runtime::game_root())), sw = runtime::lower(runtime::narrow(runtime::swofty_root()));
    if (sw.rfind(root, 0) != 0) {
        why = "the swofty folder " + sw + " is not under the game root " + root + ", so the game's file system cannot reach it";
        return false;
    }
    out = sw.substr(root.size());
    std::replace(out.begin(), out.end(), '\\', '/');
    return true;
}

}

int runtime::lang_add(const json::Value &entry)
{
    std::lock_guard<std::mutex> g(lang_lock);
    if (!h_lang || lang_loaded)
        return SWOFTY_ASSET_LATE;
    size_t from = tables.size();
    read_lang_entry(entry["language"].str(), entry);
    log_tables(from);
    return tables.size() > from ? SWOFTY_ASSET_OK : SWOFTY_ASSET_INVALID;
}

int runtime::file_add(const json::Value &entry)
{
    AcquireSRWLockExclusive(&redirect_lock);
    bool late = !legacy_hooked || seen.count(runtime::fnv64(runtime::lower(entry["path"].str())));
    if (!late)
        add_file(entry);
    ReleaseSRWLockExclusive(&redirect_lock);
    return late ? SWOFTY_ASSET_LATE : SWOFTY_ASSET_OK;
}

std::string runtime::file_route_prefix()
{
    std::string p, why;
    return swofty_prefix(p, why) ? p : std::string();
}

bool runtime::file_route_target(const std::string &game_path, std::string &target)
{
    std::string rel = runtime::lower(game_path);
    uint64_t h = runtime::fnv64(rel);
    bool found = false;
    AcquireSRWLockShared(&redirect_lock);
    for (auto &e : routes)
        if (e.first == h && e.second->rel == rel) {
            target = e.second->target;
            found = true;
        }
    ReleaseSRWLockShared(&redirect_lock);
    return found;
}

bool runtime::file_route(const std::string &game_path, const std::string *target, const std::string &source, std::string &why)
{
    std::string rel = runtime::lower(game_path);
    uint64_t h = runtime::fnv64(rel);
    Redirect *r = nullptr;
    if (target) {
        std::string prefix;
        if (!swofty_prefix(prefix, why))
            return false;
        if (!hook_resolve()) {
            why = "the core's file route is not available on this build (the Fs_Resolve_Mount signature did not resolve "
                  "or its hook failed)";
            return false;
        }
        auto made = std::make_unique<Redirect>();
        made->rel = rel;
        made->target = prefix + *target;
        made->source = source;
        r = made.get();
        AcquireSRWLockExclusive(&redirect_lock);
        route_store.push_back(std::move(made));
    } else
        AcquireSRWLockExclusive(&redirect_lock);
    bool opened = seen.count(h) != 0;
    routes.erase(std::remove_if(routes.begin(), routes.end(), [&](auto &e) { return e.first == h && e.second->rel == rel; }),
                 routes.end());
    if (r)
        routes.insert(std::upper_bound(routes.begin(), routes.end(), std::make_pair(h, r)), {h, r});
    ReleaseSRWLockExclusive(&redirect_lock);
    swofty::log("[assets] overlay: route %s %s%s%s", rel.c_str(), r ? "-> /game/" : "removed", r ? r->target.c_str() : "",
                opened ? " (the game opened this path before; its next open takes the change)" : "");
    return true;
}

namespace {

void setup()
{
    const json::Value &cfg = runtime::config();
    const json::Value &ov = cfg["overlay"], &lang_cfg = cfg["lang"];
    const json::Value &sdk_lang = runtime::sdk()["lang"], &sdk_files = runtime::sdk()["overlay_files"];
    bool want_overlay = ((ov.type == json::Value::Bool && ov.b) || (ov.type == json::Value::Str && !ov.s.empty())) &&
                        runtime::enabled("file", "overlay");
    bool want_lang = lang_cfg.type == json::Value::Obj || !sdk_lang.a.empty();
    if (!want_overlay && !want_lang && sdk_files.a.empty())
        return;
    base = swofty::api->module_base();
    std::string root = runtime::lower(runtime::narrow(runtime::game_root())), sw = runtime::lower(runtime::narrow(runtime::swofty_root()));
    if (sw.rfind(root, 0) != 0) {
        swofty::log("[assets] overlay: the swofty folder %s is not under the game root %s; the game's file system cannot "
                    "reach it; overlay and language files off",
                    sw.c_str(), root.c_str());
        return;
    }
    swofty_rel = sw.substr(root.size());
    std::replace(swofty_rel.begin(), swofty_rel.end(), '\\', '/');
    if (want_overlay) {
        std::string dir = ov.type == json::Value::Str ? ov.s : "overlay";
        std::string rel = swofty_rel + runtime::lower(dir) + "/";
        std::replace(rel.begin(), rel.end(), '\\', '/');
        size_t count = 0, bad = 0;
        DWORD t0 = GetTickCount();
        walk(runtime::under_swofty(dir) + L"\\", "", "", rel, 0, count, bad);
        swofty::log("[assets] overlay: %zu file(s) under /game/%s (%zu rejected) in %lu ms", count, rel.c_str(), bad,
                    (unsigned long)(GetTickCount() - t0));
        for (size_t i = 0; i < redirects.size() && i < 32; i++)
            swofty::log("[assets] overlay:   %s -> /game/%s", redirects[i]->rel.c_str(), redirects[i]->target.c_str());
    }
    if (want_lang)
        read_lang(lang_cfg);
    for (auto &r : redirects)
        if (r->rel.find('*') == std::string::npos)
            by_hash.push_back({runtime::fnv64(r->rel), r.get()});
    std::sort(by_hash.begin(), by_hash.end());
    for (auto &v : sdk_files.a)
        add_file(v);
    if (runtime::same_build()) {
        auto b = reinterpret_cast<const char *>(base + LNG_FOLDER_BYTE);
        if (*b == 'a')
            swofty::log("[assets] lang: this exe carries Team Simplified's localisatioa patch; language paths match either "
                        "folder");
    }
    if (!by_hash.empty() || !lang_replace.empty())
        legacy_hooked = hook_resolve();
    if (want_lang) {
        add_fn = reinterpret_cast<AddTable_t *>(runtime::find(S_ADD_TABLE, "lang"));
        memstream_vt = reinterpret_cast<void *>(runtime::find(S_MEMSTREAM, "lang"));
        if (uintptr_t at = runtime::find(S_LOAD_LNG, "lang"))
            hook(h_lang, at, &load_lang_hook, "lang: Loc_Load_LanguageFile");
        if (!tables.empty() && (!add_fn || !memstream_vt))
            swofty::log("[assets] lang: Loc_Add_LangTable or the memory stream vtable did not resolve; string tables off");
    }
}

}

void runtime::overlay_setup()
{
    setup();
}
