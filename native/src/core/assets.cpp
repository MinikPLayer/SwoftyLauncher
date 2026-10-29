#include <windows.h>

#include <safetyhook.hpp>
#include <swofty/mixin.hpp>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "json.hpp"
#include "perf.hpp"
#include "runtime.hpp"

namespace {

struct Target {
    const char *what, *pattern;
    uintptr_t rva;
    enum Kind { addr, rel32, riprel } kind;
    uintptr_t insn = 0, anchor = 0, len = 0;
};

const Target T_PARSE{"Asset_Parse_Manifest",
                     "41 57 41 56 41 55 41 54 56 57 55 53 48 81 EC D8 00 00 00 C5 F8 29 B4 24 C0 00 00 00 4C 89 C7 49 89 D0",
                     0x3a71d20, Target::addr};
const Target T_GLOBAL{"g_Asset_Manifest",
                      "48 C7 41 08 04 01 00 00 48 89 79 10 C6 41 18 00 48 8D 05 ?? ?? ?? ?? 48 89 01 4C 8B 35 ?? ?? ?? ?? 49 8B "
                      "86 08 02 00 00",
                      0x628f690, Target::riprel, 0x1a, 0x1d, 7};
const Target T_REHASH_NAMES{"Asset_Rehash_NameMap", "4C 89 C2 E8 ?? ?? ?? ?? 48 83 BC 24 80 00 00 00 00 0F 84", 0x3a77be0,
                            Target::rel32, 3, 4, 5};
const Target T_REHASH_GROUPS{"Asset_Rehash_GroupMap", "4C 89 C2 E8 ?? ?? ?? ?? 48 BB 7F 7F 7F 7F 7F 7F 7F 7F 48 83 7C 24 40 00",
                             0x3a781b0, Target::rel32, 3, 4, 5};
const Target T_ALIAS{"g_Asset_AliasNames", "56 57 55 53 48 83 EC 28 48 89 C8 48 8B 15 ?? ?? ?? ?? 48 8B 4A 18 48 0F AF C8",
                     0x628f688, Target::riprel, 0xb, 0xe, 7};
const Target T_EXPAND{"Asset_Expand_GroupErps",
                      "41 57 41 56 41 55 41 54 56 57 55 53 48 83 EC 68 C5 F8 29 74 24 50 4D 89 C5 49 89 D4", 0x3a5c8a0,
                      Target::addr};
const Target T_RELEASE{"Asset_Release_GroupErps",
                       "41 57 41 56 41 55 41 54 56 57 55 53 48 83 EC 38 4C 89 C6 48 89 D3 48 89 CF 4C 8B 25", 0x3a5ed30,
                       Target::addr};
const Target T_REHASH_DEPS{"Asset_Rehash_DepMap", "4C 89 C2 E8 ?? ?? ?? ?? 48 83 7C 24 40 00 0F 84 ?? ?? ?? ?? 45 31 F6",
                           0x3a78780, Target::rel32, 3, 4, 5};

constexpr size_t M_GROUPS = 0x10, M_ERPS = 0x58, M_DEPMAP = 0x78, M_DEPS = 0xc0, M_SEG_ALLOC = 0x1b8, M_SEGS = 0x1c0,
                 M_TOKENS = 0x1d0, M_NAMES = 0x1f8, M_SIZE = 0x240;
constexpr uint32_t SEG_LIMIT = 0x4000, TOKEN_LIMIT = 0x1000000;
constexpr DWORD OVERLAY_WAIT_MS = 600000;
constexpr const char *PROBE_GROUP = "asset_groups/ego_package/application.assetgroup";
constexpr const char *STOCK_CODES[] = {"bahrain", "melbourne", "silverstone", "monaco"};

using Rehash_t = void(void *map, uint64_t buckets);
using Alloc_t = void *(void *self, size_t size, size_t align);
using Parse_t = uint64_t(void *manifest, void *path, void *a3);
using Groups_t = void(void *loader, uint64_t *handle, void *out);

struct Slot {
    uint64_t key, value;
};

struct Entry {
    std::string group, source, tmpl;
    std::vector<std::string> erps, deps;
};

struct ErpPlan {
    std::string path;
    uint64_t hash = 0, size = 0;
    bool named = false;
};

struct Plan {
    const Entry *e;
    uint64_t hash = 0, flags = 0, dep_flags = 0;
    std::string tmpl;
    std::vector<ErpPlan> erps;
    std::vector<uint64_t> deps;
    uint32_t group_name_bits = 0x80000000u;
    bool ok = true;
    std::string why;
};

std::vector<Entry> declared;
std::vector<std::string> auto_prefixes;
std::string config_source;
uintptr_t base;
void **g_manifest, **g_alias;
Rehash_t *rehash_names, *rehash_groups, *rehash_deps;
safetyhook::InlineHook h_parse;
std::mutex patch_lock;
std::atomic<void *> patched{nullptr};
std::wstring game_root;

uint64_t fnv(const std::string &s)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 0x100000001b3ull;
    }
    return h;
}

std::string norm(std::string s)
{
    for (auto &c : s)
        if (c == '\\')
            c = '/';
        else if (c >= 'A' && c <= 'Z')
            c = char(c + 32);
    return s;
}

bool starts(const std::string &s, const char *p)
{
    return s.rfind(p, 0) == 0;
}

bool ends(const std::string &s, const char *p)
{
    size_t n = strlen(p);
    return s.size() >= n && s.compare(s.size() - n, n, p) == 0;
}

bool readable(const void *p, size_t n)
{
    MEMORY_BASIC_INFORMATION m{};
    if (!p || !VirtualQuery(p, &m, sizeof m) || m.State != MEM_COMMIT || (m.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return false;
    return uintptr_t(p) + n <= uintptr_t(m.BaseAddress) + m.RegionSize;
}

bool spans(const void *p, size_t n)
{
    return n < 16 ? readable(p, n) : readable(p, 16) && readable(static_cast<const uint8_t *>(p) + n - 16, 16);
}

std::string hex(uint64_t v)
{
    char b[24];
    snprintf(b, sizeof b, "0x%016llx", (unsigned long long)v);
    return b;
}

std::wstring wide(const std::string &s)
{
    std::wstring w(s.size(), L'\0');
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), int(w.size()));
    w.resize(n > 0 ? size_t(n) : 0);
    return w;
}

std::string narrow(const std::wstring &w)
{
    std::string s(w.size() * 3, '\0');
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), int(s.size()), nullptr, nullptr);
    s.resize(n > 0 ? size_t(n) : 0);
    return s;
}

std::wstring module_dir(HMODULE m)
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(m, path, MAX_PATH);
    std::wstring dir(path);
    return dir.substr(0, dir.find_last_of(L"\\/") + 1);
}

bool file_size(const std::string &rel, uint64_t *size)
{
    std::wstring p = game_root + wide(rel);
    std::replace(p.begin(), p.end(), L'/', L'\\');
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d) || (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return false;
    *size = (uint64_t(d.nFileSizeHigh) << 32) | d.nFileSizeLow;
    return true;
}

bool file_size_routed(const std::string &rel, uint64_t *size)
{
    if (file_size(rel, size))
        return true;
    std::string target;
    return runtime::file_route_target(rel, target) && file_size(target, size);
}

uintptr_t find(const Target &t)
{
    uintptr_t at = 0;
    int hits = swofty::api->scan(".xtls", t.pattern, &at);
    if (hits != 1 || !at) {
        swofty::log("[assets] %s: signature matched %d time(s)", t.what, hits);
        return 0;
    }
    uintptr_t out = at + t.anchor;
    if (t.kind == Target::rel32 || t.kind == Target::riprel)
        out = at + t.insn + t.len + *reinterpret_cast<const int32_t *>(at + t.anchor);
    if (out - base != t.rva)
        swofty::log("[assets] %s: resolved to base+0x%llx, KB build has base+0x%llx", t.what,
                    (unsigned long long)(out - base), (unsigned long long)t.rva);
    return out;
}

struct Map {
    uint8_t *s;
    Rehash_t *rehash;

    template <class T> T &at(size_t off) const { return *reinterpret_cast<T *>(s + off); }
    uint64_t mult() const { return at<uint64_t>(0x10); }
    Slot *slots() const { return at<Slot *>(0x18); }
    uint8_t *info() const { return at<uint8_t *>(0x20); }
    uint64_t &count() const { return at<uint64_t>(0x28); }
    uint64_t mask() const { return at<uint64_t>(0x30); }
    uint64_t &max_count() const { return at<uint64_t>(0x38); }
    uint32_t inc() const { return at<uint32_t>(0x40); }
    uint32_t shift() const { return at<uint32_t>(0x44); }

    static uint64_t allowed(uint64_t n) { return n < 0x28f5c28f5c28f5dull ? n * 80 / 100 : (n / 100) * 80; }

    size_t buffer() const { return size_t(mask() + 1 + std::min<uint64_t>(allowed(mask() + 1), 0xff)); }

    bool sane() const
    {
        uint64_t n = mask() + 1;
        return readable(s, 0x48) && n >= 8 && !(n & (n - 1)) && inc() >= 1 && inc() <= 0x20 && slots() && info() &&
               spans(info(), buffer() + 1) && spans(slots(), buffer() * sizeof(Slot));
    }

    void index(uint64_t key, size_t &idx, uint32_t &inf) const
    {
        uint64_t h = key * mult();
        h ^= h >> 33;
        inf = inc() + ((uint32_t(h) & 0x1f) >> (shift() & 0x3f));
        idx = size_t((h >> 5) & mask());
    }

    Slot *find(uint64_t key) const
    {
        size_t idx;
        uint32_t inf;
        index(key, idx, inf);
        const uint8_t *in = info();
        Slot *sl = slots();
        do {
            if (inf == in[idx] && sl[idx].key == key)
                return &sl[idx];
            ++idx;
            inf += inc();
            if (inf == in[idx] && sl[idx].key == key)
                return &sl[idx];
            ++idx;
            inf += inc();
        } while (inf <= in[idx]);
        return nullptr;
    }

    bool insert(uint64_t key, uint64_t value, std::string &why)
    {
        for (int round = 0; round < 4; round++) {
            size_t idx;
            uint32_t inf;
            index(key, idx, inf);
            uint8_t *in = info();
            Slot *sl = slots();
            uint32_t step = inc();
            while (inf < in[idx]) {
                ++idx;
                inf += step;
            }
            for (; inf == in[idx]; ++idx, inf += step)
                if (sl[idx].key == key) {
                    why = "the key is already in the map";
                    return false;
                }
            if (count() >= max_count()) {
                if (!rehash) {
                    why = "the map is full and its rehash helper did not resolve";
                    return false;
                }
                rehash(s, (mask() + 1) * 2);
                continue;
            }
            size_t ins = idx, limit = buffer();
            uint32_t ins_inf = inf;
            if (ins_inf + step > 0xff)
                max_count() = 0;
            while (in[idx]) {
                if (++idx >= limit) {
                    why = "no free slot inside the map's overflow area";
                    return false;
                }
            }
            for (size_t i = idx; i != ins; --i) {
                sl[i] = sl[i - 1];
                in[i] = uint8_t(in[i - 1] + step);
                if (in[i] + step > 0xff)
                    max_count() = 0;
            }
            sl[ins] = {key, value};
            in[ins] = uint8_t(ins_inf);
            ++count();
            return true;
        }
        why = "the map still had no room after a rehash";
        return false;
    }
};

struct Manifest {
    uint8_t *m;
    Map groups, deps_map, names;

    explicit Manifest(uint8_t *p)
        : m(p), groups{p + M_GROUPS, rehash_groups}, deps_map{p + M_DEPMAP, rehash_deps}, names{p + M_NAMES, rehash_names}
    {
    }

    template <class T> T *array(size_t off) const { return *reinterpret_cast<T **>(m + off + 8); }
    uint32_t array_count(size_t off) const { return *reinterpret_cast<uint32_t *>(m + off + 0x18); }
    const char *segs() const { return *reinterpret_cast<const char **>(m + M_SEGS); }

    bool sane(std::string &why) const
    {
        if (!readable(m, M_SIZE))
            return why = "manifest object is not readable", false;
        if (!groups.sane())
            return why = "group map looks wrong", false;
        if (!names.sane())
            return why = "name map looks wrong", false;
        if (!readable(m + M_DEPMAP, 0x48) || (deps_map.mask() && !deps_map.sane()))
            return why = "dependency map looks wrong", false;
        if (!array<uint64_t>(M_ERPS) || !array_count(M_ERPS) || !array<uint16_t>(M_TOKENS) || !array_count(M_TOKENS) ||
            !segs())
            return why = "ERP list, token list or word table is empty", false;
        return true;
    }

    std::string decode(uint64_t value) const
    {
        static const char seps[] = {'/', '.', '_', '\0'};
        const uint16_t *tok = array<uint16_t>(M_TOKENS);
        uint32_t n = array_count(M_TOKENS), i = uint32_t(value) & 0xffffff;
        std::string out;
        for (; i < n && out.size() < 1024; i++) {
            const char *w = segs() + size_t(tok[i] & 0x3fff) * 8;
            out += w;
            unsigned sep = tok[i] >> 14;
            if (sep == 3)
                break;
            out += seps[sep];
        }
        return out;
    }

    std::string name_of(uint64_t hash) const
    {
        Slot *s = names.find(hash);
        return s ? decode(s->value) : std::string();
    }
};

bool alias_has(uint64_t hash)
{
    void *a = g_alias && readable(g_alias, 8) ? *g_alias : nullptr;
    if (!a)
        return false;
    Map m{static_cast<uint8_t *>(a) + 8, nullptr};
    return m.sane() && m.find(hash);
}

void *engine_alloc(void *allocator, size_t size, size_t align)
{
    if (!allocator || !readable(allocator, 8))
        return nullptr;
    auto fn = (*static_cast<Alloc_t ***>(allocator))[3];
    return fn(allocator, size, align);
}

template <class T> bool append(uint8_t *a, const std::vector<T> &items, uint32_t &start, std::string &why)
{
    T *old = *reinterpret_cast<T **>(a + 8);
    uint32_t n = *reinterpret_cast<uint32_t *>(a + 0x18);
    start = n;
    if (items.empty())
        return true;
    size_t bytes = (size_t(n) + items.size()) * sizeof(T);
    auto *fresh = static_cast<T *>(engine_alloc(*reinterpret_cast<void **>(a), bytes, 16));
    if (!fresh)
        return why = "the engine allocator returned nothing", false;
    if (n)
        memcpy(fresh, old, size_t(n) * sizeof(T));
    memcpy(fresh + n, items.data(), items.size() * sizeof(T));
    *reinterpret_cast<T **>(a + 8) = fresh;
    *reinterpret_cast<uint64_t *>(a + 0x10) = bytes;
    *reinterpret_cast<uint32_t *>(a + 0x18) = uint32_t(n + items.size());
    return true;
}

struct Words {
    Manifest &mf;
    std::unordered_map<std::string, uint16_t> dict;
    std::map<std::string, uint16_t> fresh;
    size_t extent = 0;

    explicit Words(Manifest &m) : mf(m)
    {
        std::vector<uint8_t> used(SEG_LIMIT, 0);
        const uint16_t *tok = mf.array<uint16_t>(M_TOKENS);
        for (uint32_t i = 0, n = mf.array_count(M_TOKENS); i < n; i++)
            used[tok[i] & 0x3fff] = 1;
        for (uint32_t i = 0; i < SEG_LIMIT; i++) {
            if (!used[i])
                continue;
            const char *w = mf.segs() + size_t(i) * 8;
            size_t len = strnlen(w, 4096);
            extent = std::max(extent, size_t(i) * 8 + len + 1);
            dict.emplace(std::string(w, len), uint16_t(i));
        }
    }

    size_t next_free() const
    {
        size_t at = (extent + 7) & ~size_t(7);
        for (auto &[w, i] : fresh)
            at = std::max(at, size_t(i) * 8 + ((w.size() + 8) & ~size_t(7)));
        return at;
    }

    bool word(const std::string &w, uint16_t &out, std::string &why)
    {
        if (auto it = dict.find(w); it != dict.end())
            return out = it->second, true;
        if (auto it = fresh.find(w); it != fresh.end())
            return out = it->second, true;
        size_t at = next_free();
        if (at / 8 >= SEG_LIMIT || at + w.size() + 1 > size_t(SEG_LIMIT) * 8) {
            why = "the manifest word table is full (no room for '" + w + "')";
            return false;
        }
        fresh.emplace(w, uint16_t(at / 8));
        out = uint16_t(at / 8);
        return true;
    }

    bool encode(const std::string &path, std::vector<uint16_t> &out, std::string &why)
    {
        std::string cur;
        for (size_t i = 0; i <= path.size(); i++) {
            char c = i < path.size() ? path[i] : '\0';
            int sep = c == '/' ? 0 : c == '.' ? 1 : c == '_' ? 2 : c == '\0' ? 3 : -1;
            if (sep < 0) {
                cur += c;
                continue;
            }
            uint16_t idx;
            if (!word(cur, idx, why))
                return false;
            out.push_back(uint16_t(idx | (sep << 14)));
            cur.clear();
        }
        return true;
    }

    bool commit(std::string &why)
    {
        if (fresh.empty())
            return true;
        size_t old = (extent + 7) & ~size_t(7), size = next_free();
        void *alloc = *reinterpret_cast<void **>(mf.m + M_SEG_ALLOC);
        auto *blob = static_cast<char *>(engine_alloc(alloc, size, 16));
        if (!blob)
            return why = "the engine allocator returned nothing for the word table", false;
        memset(blob, 0, size);
        memcpy(blob, mf.segs(), extent);
        for (auto &[w, i] : fresh)
            memcpy(blob + size_t(i) * 8, w.c_str(), w.size() + 1);
        *reinterpret_cast<char **>(mf.m + M_SEGS) = blob;
        swofty::log("[assets] word table: %zu new word(s) appended at 0x%zx..0x%zx (limit 0x%x)", fresh.size(), old, size,
                    SEG_LIMIT * 8);
        return true;
    }
};

struct Scheme {
    const char *name;
    std::string (*fn)(const std::string &decoded);
};

const Scheme ERP_SCHEMES[] = {
    {"fnv1a64(<name>)", [](const std::string &d) { return d; }},
    {"fnv1a64(<name> without 2025_)", [](const std::string &d) { return starts(d, "2025_") ? d.substr(5) : d; }},
    {"fnv1a64(/data_win/<name>)", [](const std::string &d) { return "/data_win/" + d; }},
    {"fnv1a64(data_win/<name>)", [](const std::string &d) { return "data_win/" + d; }},
    {"fnv1a64(/<name>)", [](const std::string &d) { return "/" + d; }},
};

struct Probe {
    const Scheme *erp_scheme = &ERP_SCHEMES[0];
    std::string group_prefix = "2025_";
    bool size_in_high = false;
    uint32_t erp_name_bits = 0x80000000u;
};

void probe_sizes(const Manifest &mf, Probe &p)
{
    const uint64_t *erps = mf.array<uint64_t>(M_ERPS);
    uint32_t n = mf.array_count(M_ERPS);
    size_t checked = 0, sized = 0, zero = 0, tried = 0;
    std::string example;
    bool bits = false;
    for (uint32_t i = 0; i < n && checked < 8 && tried < 512; i++) {
        Slot *ns = mf.names.find(erps[i]);
        if (!ns)
            continue;
        tried++;
        std::string name = norm(mf.decode(ns->value));
        uint64_t size = 0;
        if (name.empty() || !file_size(name, &size))
            continue;
        checked++;
        uint64_t hi = ns->value >> 32;
        if (hi == size)
            sized++;
        else if (!hi)
            zero++;
        if (!bits) {
            p.erp_name_bits = uint32_t(ns->value) & 0xff000000u;
            bits = true;
            example = name + ": name value " + hex(ns->value) + ", " + std::to_string(size) + " bytes on disk";
        }
    }
    p.size_in_high = checked && sized == checked;
    swofty::log("[assets] probe: %zu on-disk ERP(s) checked (%s): the name map's high 32 bits %s; new ERP names get bits "
                "24-31 = 0x%02x (only bits 0-23 are the token index)",
                checked, checked ? example.c_str() : "none found",
                !checked              ? "are unknown, left 0"
                : p.size_in_high      ? "are the file size, new names carry their size"
                : zero == checked     ? "are 0 for loose ERPs, new names leave them 0"
                                      : "match neither the size nor 0, new names leave them 0",
                p.erp_name_bits >> 24);
}

Probe probe(const Manifest &mf)
{
    Probe p;
    uint64_t gh = fnv(PROBE_GROUP);
    Slot *g = mf.groups.find(gh);
    if (!g) {
        swofty::log("[assets] probe group %s is not in the manifest; using default naming", PROBE_GROUP);
        probe_sizes(mf, p);
        return p;
    }
    std::string gname = mf.name_of(gh);
    if (ends(gname, PROBE_GROUP)) {
        p.group_prefix = gname.substr(0, gname.size() - strlen(PROBE_GROUP));
        swofty::log("[assets] probe: group %s (0x%016llx) is named '%s' in the manifest (prefix '%s')", PROBE_GROUP,
                    (unsigned long long)gh, gname.c_str(), p.group_prefix.c_str());
    } else
        swofty::log("[assets] probe: group %s (0x%016llx) is named '%s'; keeping prefix '2025_'", PROBE_GROUP,
                    (unsigned long long)gh, gname.c_str());
    uint32_t start = uint32_t(g->value), count = uint32_t(g->value >> 32) & 0xffffff;
    if (!count || start >= mf.array_count(M_ERPS)) {
        probe_sizes(mf, p);
        return p;
    }
    uint64_t eh = mf.array<uint64_t>(M_ERPS)[start];
    Slot *ns = mf.names.find(eh);
    std::string ename = ns ? mf.decode(ns->value) : std::string();
    const Scheme *hit = nullptr;
    for (auto &s : ERP_SCHEMES)
        if (!ename.empty() && fnv(s.fn(ename)) == eh) {
            hit = &s;
            break;
        }
    uint64_t size = 0;
    bool on_disk = !ename.empty() && file_size(ename, &size);
    swofty::log("[assets] probe: its first ERP 0x%016llx is '%s' (name value 0x%016llx, %u ERP(s) in the group, flags "
                "0x%08x, on disk %s %llu bytes); ERP hash scheme: %s",
                (unsigned long long)eh, ename.c_str(), (unsigned long long)(ns ? ns->value : 0), count,
                uint32_t(g->value >> 32) & 0xff000000u, on_disk ? "yes," : "no,", (unsigned long long)size,
                hit ? hit->name : "none matched, using fnv1a64(<name>)");
    if (hit)
        p.erp_scheme = hit;
    probe_sizes(mf, p);
    return p;
}

std::string group_to_erp(const std::string &group)
{
    std::string g = group.substr(strlen("asset_groups/"));
    return "2025_asset_groups/" + g.substr(0, g.size() - strlen(".assetgroup")) + ".erp";
}

std::vector<std::string> template_candidates(const Entry &e)
{
    std::vector<std::string> out;
    if (!e.tmpl.empty())
        out.push_back(norm(e.tmpl));
    const char *tracks = "asset_groups/environment_package/tracks/";
    if (starts(e.group, tracks)) {
        std::string rest = e.group.substr(strlen(tracks));
        std::string code = rest.substr(0, rest.find('/'));
        if (!code.empty() && code.size() < rest.size())
            for (const char *stock : STOCK_CODES) {
                std::string r = rest;
                for (size_t at = 0; (at = r.find(code, at)) != std::string::npos; at += strlen(stock))
                    r.replace(at, code.size(), stock);
                out.push_back(tracks + r);
            }
    }
    return out;
}

void scan_dir(const std::wstring &dir, const std::string &rel, std::vector<std::string> &out, unsigned depth)
{
    if (depth > 8)
        return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..")
            continue;
        std::string r = rel + norm(narrow(n));
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            scan_dir(dir + n + L"\\", r + "/", out, depth + 1);
        else if (ends(r, ".erp"))
            out.push_back(r);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

void add_auto(const Manifest &mf, std::vector<Entry> &entries)
{
    std::unordered_set<std::string> have;
    for (auto &e : entries)
        have.insert(e.group);
    for (auto &prefix : auto_prefixes) {
        DWORD t0 = GetTickCount();
        std::string rel = "2025_asset_groups/" + prefix + (ends(prefix, "/") ? "" : "/");
        std::wstring dir = game_root + wide(rel);
        std::replace(dir.begin(), dir.end(), L'/', L'\\');
        std::vector<std::string> files;
        scan_dir(dir, rel, files, 0);
        size_t added = 0;
        for (auto &f : files) {
            std::string body = f.substr(strlen("2025_"));
            std::string group = body.substr(0, body.size() - 4) + ".assetgroup";
            if (have.count(group) || mf.groups.find(fnv(group)))
                continue;
            Entry e;
            e.group = group;
            e.erps.push_back(f);
            e.source = "auto " + prefix;
            have.insert(group);
            entries.push_back(std::move(e));
            added++;
        }
        swofty::log("[assets] auto rule '%s': %zu loose ERP(s), %zu not in the manifest, %lu ms", rel.c_str(), files.size(),
                    added, (unsigned long)(GetTickCount() - t0));
    }
}

using TargetMap = std::unordered_map<uint64_t, std::vector<uint64_t>>;

std::atomic<const TargetMap *> dyn_targets{nullptr};
std::mutex dyn_lock;
std::map<std::vector<uint64_t>, std::unique_ptr<std::vector<uint64_t>>> dyn_lists;
safetyhook::InlineHook h_expand, h_release;

const std::vector<uint64_t> *augmented(const uint64_t *h)
{
    const TargetMap *m = dyn_targets.load(std::memory_order_acquire);
    if (!m || !(reinterpret_cast<const uint8_t *>(h)[0xc] & 4))
        return nullptr;
    auto b = reinterpret_cast<const uint64_t *>(h[3]), e = reinterpret_cast<const uint64_t *>(h[4]);
    if (!b || e <= b || size_t(e - b) > 0x10000 || !spans(b, size_t(e - b) * 8))
        return nullptr;
    std::vector<uint64_t> need;
    for (const uint64_t *p = b; p != e; p++) {
        auto it = m->find(*p);
        if (it == m->end())
            continue;
        for (uint64_t oh : it->second)
            if (std::find(b, e, oh) == e && std::find(need.begin(), need.end(), oh) == need.end())
                need.push_back(oh);
    }
    if (need.empty())
        return nullptr;
    std::vector<uint64_t> key(b, e);
    std::lock_guard<std::mutex> g(dyn_lock);
    auto &slot = dyn_lists[key];
    if (!slot) {
        slot = std::make_unique<std::vector<uint64_t>>(need);
        slot->insert(slot->end(), b, e);
        if (dyn_lists.size() <= 32)
            swofty::log("[assets] resources: dynamic group %s (handle flag 4, %zu ERP(s) in its own list at +0x18) lists a "
                        "target ERP; Asset_Expand_GroupErps and Asset_Release_GroupErps now see %zu overlay ERP(s) first",
                        hex(h[0]).c_str(), key.size(), need.size());
    }
    return slot.get();
}

void groups_call(safetyhook::InlineHook &hook, void *loader, uint64_t *h, void *out)
{
    if (h)
        if (const std::vector<uint64_t> *aug = augmented(h)) {
            uint64_t copy[5];
            memcpy(copy, h, sizeof copy);
            copy[3] = reinterpret_cast<uint64_t>(aug->data());
            copy[4] = reinterpret_cast<uint64_t>(aug->data() + aug->size());
            perf::Pause game;
            hook.call<void>(loader, copy, out);
            return;
        }
    perf::Pause game;
    hook.call<void>(loader, h, out);
}

void expand_hook(void *loader, uint64_t *h, void *out)
{
    static const uint32_t probe = perf::probe("core", "assets group expand");
    perf::Scope p(probe);
    groups_call(h_expand, loader, h, out);
}

void release_hook(void *loader, uint64_t *h, void *out)
{
    static const uint32_t probe = perf::probe("core", "assets group release");
    perf::Scope p(probe);
    groups_call(h_release, loader, h, out);
}

void apply_overlay(Manifest &mf, const Probe &pr, const runtime::Overlay &ov)
{
    struct Ov {
        const runtime::OverlayErp *e;
        uint64_t hash;
        bool named, ok;
        uint32_t tok;
    };
    std::vector<Ov> ovs;
    for (auto &e : ov.erps) {
        Ov o{&e, fnv(pr.erp_scheme->fn(e.path)), false, true, 0};
        if (Slot *ns = mf.names.find(o.hash)) {
            std::string existing = mf.decode(ns->value);
            o.named = existing == e.path;
            o.ok = o.named;
            if (!o.ok)
                swofty::log("[assets] resources: erp hash %s of %s already names '%s'; that overlay is not registered",
                            hex(o.hash).c_str(), e.path.c_str(), existing.c_str());
        } else if (alias_has(o.hash)) {
            o.ok = false;
            swofty::log("[assets] resources: erp hash %s of %s already has a name in g_Asset_AliasNames (the engine's "
                        "fallback when the name map misses); that overlay is not registered",
                        hex(o.hash).c_str(), e.path.c_str());
        }
        ovs.push_back(o);
    }
    TargetMap ov_of;
    for (size_t i = 0; i < ovs.size(); i++) {
        if (!ovs[i].ok)
            continue;
        for (auto &t : ovs[i].e->targets) {
            uint64_t th = fnv(pr.erp_scheme->fn(t));
            Slot *ns = mf.names.find(th);
            std::string n = ns ? norm(mf.decode(ns->value)) : std::string();
            if (ns && n != t) {
                swofty::log("[assets] resources: target %s (erp %s): the name map gives '%s' for that hash; the hash scheme "
                            "does not fit this ERP, skipped",
                            t.c_str(), hex(th).c_str(), n.c_str());
                continue;
            }
            if (!ns)
                swofty::log("[assets] resources: target %s (erp %s) is not in the manifest name map; the engine opens it "
                            "through %s, and groups listing the hash are still patched",
                            t.c_str(), hex(th).c_str(),
                            alias_has(th) ? "its g_Asset_AliasNames entry" : "a formatted-hash path (it may not load at all)");
            auto &v = ov_of[th];
            if (std::find(v.begin(), v.end(), ovs[i].hash) == v.end())
                v.push_back(ovs[i].hash);
        }
    }
    struct Hit {
        uint64_t key;
        uint32_t start, count, flags;
        std::vector<uint64_t> need;
        std::string target;
    };
    std::vector<Hit> hits;
    const uint64_t *erps = mf.array<uint64_t>(M_ERPS);
    uint32_t nerps = mf.array_count(M_ERPS);
    Slot *sl = mf.groups.slots();
    const uint8_t *in = mf.groups.info();
    for (size_t i = 0, n = mf.groups.buffer(); i < n; i++) {
        if (!in[i])
            continue;
        uint64_t v = sl[i].value;
        uint32_t start = uint32_t(v), count = uint32_t(v >> 32) & 0xffffff;
        if (!count || uint64_t(start) + count > nerps)
            continue;
        Hit h{sl[i].key, start, count, uint32_t(v >> 32) & 0xff000000u, {}, {}};
        for (uint32_t j = 0; j < count; j++) {
            auto it = ov_of.find(erps[start + j]);
            if (it == ov_of.end())
                continue;
            if (h.target.empty())
                h.target = runtime::erp_name(erps[start + j]);
            for (uint64_t oh : it->second)
                if (std::find(erps + start, erps + start + count, oh) == erps + start + count &&
                    std::find(h.need.begin(), h.need.end(), oh) == h.need.end())
                    h.need.push_back(oh);
        }
        if (!h.need.empty())
            hits.push_back(std::move(h));
    }
    std::string why;
    Words words(mf);
    std::vector<uint16_t> tokens;
    uint32_t token_base = mf.array_count(M_TOKENS);
    for (auto &o : ovs) {
        if (!o.ok || o.named)
            continue;
        o.tok = token_base + uint32_t(tokens.size());
        if (!words.encode(o.e->path, tokens, why) || uint64_t(token_base) + tokens.size() > TOKEN_LIMIT) {
            runtime::overlay_dropped("overlay names do not fit the manifest (" +
                                     (why.empty() ? std::string("token index past 0xffffff") : why) + ")");
            return;
        }
    }
    std::vector<uint64_t> list;
    for (auto &h : hits) {
        list.insert(list.end(), h.need.begin(), h.need.end());
        list.insert(list.end(), erps + h.start, erps + h.start + h.count);
    }
    uint32_t tok_start = 0, erp_start = 0;
    if (!words.commit(why) || !append(mf.m + M_TOKENS, tokens, tok_start, why) || !append(mf.m + M_ERPS, list, erp_start, why)) {
        runtime::overlay_dropped("manifest arrays: " + why);
        return;
    }
    std::vector<uint64_t> registered;
    for (auto &o : ovs) {
        if (!o.ok)
            continue;
        if (!o.named) {
            uint64_t hi = pr.size_in_high ? o.e->size : 0;
            if (!mf.names.insert(o.hash, (hi << 32) | pr.erp_name_bits | o.tok, why)) {
                swofty::log("[assets] resources: overlay %s not registered: name map: %s", o.e->path.c_str(), why.c_str());
                o.ok = false;
                continue;
            }
        }
        registered.push_back(o.hash);
        swofty::log("[assets] resources: overlay %s registered as erp %s (name read back '%s', %llu bytes, %zu resource(s))",
                    o.e->path.c_str(), hex(o.hash).c_str(), mf.name_of(o.hash).c_str(), (unsigned long long)o.e->size,
                    o.e->resources);
    }
    size_t patched_groups = 0;
    for (auto &h : hits) {
        uint32_t mine = erp_start;
        uint32_t total = uint32_t(h.need.size()) + h.count;
        erp_start += total;
        bool all = true;
        for (uint64_t oh : h.need)
            all &= std::find(registered.begin(), registered.end(), oh) != registered.end();
        Slot *s = mf.groups.find(h.key);
        std::string gname = mf.name_of(h.key);
        if (gname.empty())
            gname = hex(h.key);
        if (!s || !all) {
            swofty::log("[assets] resources: group %s left unchanged (%s)", gname.c_str(),
                        !s ? "it vanished from map A" : "one of its overlays is not registered");
            continue;
        }
        __atomic_store_n(&s->value, (uint64_t(h.flags | total) << 32) | mine, __ATOMIC_RELEASE);
        std::string first;
        for (uint64_t oh : h.need)
            first += (first.empty() ? "" : ", ") + mf.name_of(oh);
        swofty::log("[assets] resources: group %s (0x%016llx) now reads %s first, then its %u stock ERP(s); it lists %s",
                    gname.c_str(), (unsigned long long)h.key, first.c_str(), h.count, h.target.c_str());
        patched_groups++;
    }
    auto dyn = std::make_unique<TargetMap>();
    for (auto &[th, list_of] : ov_of)
        for (uint64_t oh : list_of)
            if (std::find(registered.begin(), registered.end(), oh) != registered.end())
                (*dyn)[th].push_back(oh);
    size_t dyn_count = dyn->size();
    if (h_expand && h_release && dyn_count)
        dyn_targets.store(dyn.release(), std::memory_order_release);
    swofty::log("[assets] resources: %zu of %zu overlay ERP(s) registered, %zu group(s) patched, %zu target ERP hash(es)%s",
                registered.size(), ov.erps.size(), patched_groups, dyn_count,
                h_expand && h_release ? "; flag-4 groups with their own ERP list are covered at expand and release"
                                      : "; flag-4 groups with their own ERP list are NOT covered (expand/release hooks missing)");
    if (registered.empty())
        runtime::overlay_dropped("no overlay ERP could be registered in the manifest");
    else
        runtime::overlay_registered(registered, patched_groups);
}

void apply(uint8_t *m, const char *when, const runtime::Overlay *ov)
{
    std::lock_guard<std::mutex> g(patch_lock);
    if (patched == m)
        return;
    Manifest mf(m);
    std::string why;
    if (!mf.sane(why)) {
        swofty::log("[assets] manifest at %p (%s): %s; registering nothing", static_cast<void *>(m), when, why.c_str());
        if (ov)
            runtime::overlay_dropped("the manifest looks wrong (" + why + ")");
        return;
    }
    patched = m;
    swofty::log("[assets] manifest at %p (%s): %llu group(s), %u ERP reference(s), %llu dependency entr(ies), %llu "
                "name(s), %u name token(s)",
                static_cast<void *>(m), when, (unsigned long long)mf.groups.count(), mf.array_count(M_ERPS),
                (unsigned long long)mf.deps_map.count(), (unsigned long long)mf.names.count(), mf.array_count(M_TOKENS));
    Probe pr = probe(mf);
    std::vector<Entry> entries = declared;
    add_auto(mf, entries);
    struct Late {
        Manifest &mf;
        const Probe &pr;
        const runtime::Overlay *ov;
        ~Late()
        {
            if (ov)
                apply_overlay(mf, pr, *ov);
        }
    } late{mf, pr, ov};
    if (entries.empty() && ov) {
        swofty::log("[assets] registered 0 group(s): none declared; resources only");
        return;
    }
    if (entries.empty()) {
        swofty::log("[assets] registered 0 group(s): nothing declared");
        return;
    }
    Words words(mf);
    swofty::log("[assets] word table: %zu word(s), used up to 0x%zx of 0x%x", words.dict.size(), words.extent,
                SEG_LIMIT * 8);

    std::vector<Plan> plans;
    std::unordered_set<uint64_t> seen, planned_erps;
    std::vector<uint16_t> tokens;
    std::vector<std::pair<uint64_t, uint32_t>> token_starts;
    uint32_t token_base = mf.array_count(M_TOKENS);
    for (auto &e : entries) {
        Plan p;
        p.e = &e;
        p.hash = fnv(e.group);
        auto fail = [&](const std::string &w) {
            p.ok = false;
            p.why = w;
        };
        if (!starts(e.group, "asset_groups/") || !ends(e.group, ".assetgroup"))
            fail("the group path must look like asset_groups/<path>.assetgroup");
        else if (seen.count(p.hash))
            fail("declared twice");
        else if (mf.groups.find(p.hash))
            fail("already in the manifest (a stock group); not overriding it");
        else if (e.erps.empty())
            fail("no ERP given");
        if (p.ok) {
            for (auto &cand : template_candidates(e))
                if (Slot *t = mf.groups.find(fnv(cand))) {
                    p.tmpl = cand;
                    p.flags = (t->value >> 32) & 0xff000000ull;
                    if (Slot *ts = mf.names.find(fnv(cand)))
                        p.group_name_bits = uint32_t(ts->value) & 0xff000000u;
                    if (Slot *td = mf.deps_map.mask() ? mf.deps_map.find(fnv(cand)) : nullptr)
                        p.dep_flags = (td->value >> 32) & 0xff000000ull;
                    break;
                }
            if (!e.tmpl.empty() && p.tmpl != norm(e.tmpl))
                swofty::log("[assets] %s: template %s is not in the manifest; %s", e.group.c_str(), e.tmpl.c_str(),
                            p.tmpl.empty() ? "flags left 0" : ("using " + p.tmpl).c_str());
        }
        for (size_t i = 0; p.ok && i < e.erps.size(); i++) {
            ErpPlan ep;
            ep.path = e.erps[i];
            if (!starts(ep.path, "2025_asset_groups/") || !ends(ep.path, ".erp")) {
                fail("ERP path '" + ep.path + "' must look like 2025_asset_groups/<path>.erp");
                break;
            }
            if (!file_size_routed(ep.path, &ep.size)) {
                fail("ERP not found on disk: " + narrow(game_root) + ep.path);
                break;
            }
            ep.hash = fnv(pr.erp_scheme->fn(ep.path));
            if (planned_erps.count(ep.hash))
                ep.named = true;
            else if (Slot *ns = mf.names.find(ep.hash)) {
                std::string existing = mf.decode(ns->value);
                if (existing != ep.path) {
                    fail("ERP hash " + hex(ep.hash) + " already names '" + existing + "'");
                    break;
                }
                ep.named = true;
            } else if (alias_has(ep.hash)) {
                fail("ERP hash " + hex(ep.hash) + " already has a name in g_Asset_AliasNames (the engine's fallback when "
                     "the name map misses); not overriding it");
                break;
            }
            p.erps.push_back(ep);
        }
        for (auto &d : e.deps) {
            uint64_t dh = fnv(d);
            if (!mf.groups.find(dh)) {
                bool ours = false;
                for (auto &o : entries)
                    ours |= o.group == d;
                if (!ours)
                    swofty::log("[assets] %s: dependency %s is neither in the manifest nor declared; kept (the engine "
                                "skips it)",
                                e.group.c_str(), d.c_str());
            }
            p.deps.push_back(dh);
        }
        if (p.ok) {
            std::vector<uint16_t> local;
            std::string w;
            std::vector<std::pair<uint64_t, uint32_t>> local_starts;
            bool named = true;
            for (auto &ep : p.erps) {
                if (ep.named)
                    continue;
                local_starts.push_back({ep.hash, token_base + uint32_t(tokens.size() + local.size())});
                if (!(named = words.encode(ep.path, local, w)))
                    break;
            }
            if (!named)
                fail(w);
            else {
                size_t keep = local.size();
                local_starts.push_back({p.hash, token_base + uint32_t(tokens.size() + local.size())});
                if (!words.encode(pr.group_prefix + e.group, local, w)) {
                    local.resize(keep);
                    local_starts.pop_back();
                    swofty::log("[assets] %s: no display name (%s); the log will show its hash", e.group.c_str(),
                                w.c_str());
                }
                tokens.insert(tokens.end(), local.begin(), local.end());
                token_starts.insert(token_starts.end(), local_starts.begin(), local_starts.end());
                for (auto &ep : p.erps)
                    planned_erps.insert(ep.hash);
            }
        }
        if (p.ok)
            seen.insert(p.hash);
        plans.push_back(std::move(p));
    }

    bool any = false;
    for (auto &p : plans)
        any |= p.ok;
    size_t done = 0, skipped = 0;
    if (any && uint64_t(token_base) + tokens.size() > TOKEN_LIMIT) {
        swofty::log("[assets] registered 0 group(s): the name token index would pass 0xffffff (only bits 0-23 of a "
                    "name-map value address tokens)");
        return;
    }
    if (any) {
        uint32_t tok_start = 0;
        if (!words.commit(why) || !append(m + M_TOKENS, tokens, tok_start, why)) {
            swofty::log("[assets] registered 0 group(s): %s", why.c_str());
            return;
        }
        std::unordered_map<uint64_t, uint32_t> starts_of(token_starts.begin(), token_starts.end());
        std::vector<uint64_t> erp_list, dep_list;
        for (auto &p : plans)
            if (p.ok) {
                for (auto &ep : p.erps)
                    erp_list.push_back(ep.hash);
                for (auto d : p.deps)
                    dep_list.push_back(d);
            }
        uint32_t erp_start = 0, dep_start = 0;
        if (!append(m + M_ERPS, erp_list, erp_start, why) ||
            (!dep_list.empty() && (!mf.deps_map.mask() || !append(m + M_DEPS, dep_list, dep_start, why)))) {
            swofty::log("[assets] registered 0 group(s): %s", why.empty() ? "the manifest has no dependency map" : why.c_str());
            return;
        }
        for (auto &p : plans) {
            if (!p.ok)
                continue;
            uint32_t my_erps = erp_start, my_deps = dep_start;
            erp_start += uint32_t(p.erps.size());
            dep_start += uint32_t(p.deps.size());
            std::string w;
            bool ok = true;
            for (auto &ep : p.erps) {
                if (ep.named || !ok || mf.names.find(ep.hash))
                    continue;
                uint64_t hi = pr.size_in_high ? ep.size : 0;
                ok = mf.names.insert(ep.hash, (hi << 32) | pr.erp_name_bits | starts_of[ep.hash], w);
                if (!ok)
                    w = "ERP name: " + w;
            }
            if (ok && starts_of.count(p.hash) && !mf.names.find(p.hash)) {
                std::string gw;
                if (!mf.names.insert(p.hash, uint64_t(p.group_name_bits | starts_of[p.hash]), gw))
                    swofty::log("[assets] %s: group name not added (%s); the log will show its hash instead",
                                p.e->group.c_str(), gw.c_str());
            }
            if (ok && !p.deps.empty()) {
                ok = mf.deps_map.insert(p.hash, (uint64_t(p.dep_flags | p.deps.size()) << 32) | my_deps, w);
                if (!ok)
                    w = "dependency map: " + w;
            }
            if (ok) {
                ok = mf.groups.insert(p.hash, (uint64_t(p.flags | p.erps.size()) << 32) | my_erps, w);
                if (!ok)
                    w = "group map: " + w;
            }
            if (!ok) {
                p.ok = false;
                p.why = w;
                continue;
            }
            done++;
            std::string list;
            for (auto &ep : p.erps)
                list += (list.empty() ? "" : ", ") + ep.path + " (" + std::to_string(ep.size) + " bytes" +
                        (ep.named ? ", already named" : "") + ")";
            swofty::log("[assets] registered %s -> %s; group 0x%016llx, erp 0x%016llx, flags 0x%08llx from %s, %zu "
                        "dependenc(ies), from %s",
                        p.e->group.c_str(), list.c_str(), (unsigned long long)p.hash,
                        (unsigned long long)p.erps.front().hash, (unsigned long long)p.flags,
                        p.tmpl.empty() ? "no template" : p.tmpl.c_str(), p.deps.size(), p.e->source.c_str());
            Slot *chk = mf.groups.find(p.hash);
            std::string back = mf.name_of(p.erps.front().hash);
            if (!chk || back != p.erps.front().path)
                swofty::log("[assets] %s: read-back mismatch (group %s, erp name '%s')", p.e->group.c_str(),
                            chk ? "found" : "missing", back.c_str());
        }
    }
    for (auto &p : plans)
        if (!p.ok) {
            skipped++;
            swofty::log("[assets] skipped %s (from %s): %s", p.e->group.c_str(), p.e->source.c_str(), p.why.c_str());
        }
    swofty::log("[assets] registered %zu group(s), skipped %zu; manifest now %llu group(s), %u ERP reference(s), %llu "
                "name(s)",
                done, skipped, (unsigned long long)mf.groups.count(), mf.array_count(M_ERPS),
                (unsigned long long)mf.names.count());
}

uint64_t parse_hook(void *manifest, void *path, void *a3)
{
    static const uint32_t probe = perf::probe("core", "assets manifest parse");
    uint64_t r = h_parse.call<uint64_t>(manifest, path, a3);
    perf::Scope p(probe);
    if (uint32_t(r) == 0)
        apply(static_cast<uint8_t *>(manifest), "parsed after addons loaded",
              patched == manifest ? nullptr : runtime::resource_overlay(OVERLAY_WAIT_MS));
    else
        swofty::log("[assets] manifest parse returned %u; waiting for the next attempt", uint32_t(r));
    return r;
}

void read_entries(const json::Value &list, const std::string &source)
{
    for (auto &j : list.a) {
        Entry e;
        e.source = source.empty() ? "addon " + j["origin"].str() : source;
        if (j.type == json::Value::Str)
            e.group = norm(j.s);
        else {
            e.group = norm(j["group"].str());
            e.tmpl = j["template"].str();
            if (j["erp"].type == json::Value::Str)
                e.erps.push_back(norm(j["erp"].s));
            for (auto &x : j["erps"].a)
                if (x.type == json::Value::Str)
                    e.erps.push_back(norm(x.s));
            for (auto &x : j["deps"].a)
                if (x.type == json::Value::Str)
                    e.deps.push_back(norm(x.s));
        }
        if (e.group.empty())
            continue;
        if (e.erps.empty() && starts(e.group, "asset_groups/") && ends(e.group, ".assetgroup"))
            e.erps.push_back(group_to_erp(e.group));
        declared.push_back(std::move(e));
    }
}

void read_auto(const json::Value &v)
{
    if (v.type == json::Value::Bool && v.b)
        auto_prefixes.push_back("environment_package/tracks");
    else if (v.type == json::Value::Str && !v.s.empty())
        auto_prefixes.push_back(norm(v.s));
    else
        for (auto &x : v.a)
            if (x.type == json::Value::Str && !x.s.empty())
                auto_prefixes.push_back(norm(x.s));
}

bool load_config()
{
    const json::Value &doc = runtime::config();
    const json::Value &list = doc["asset_groups"];
    const json::Value &autos = doc["asset_groups_auto"];
    const json::Value &sdk = runtime::sdk()["asset_groups"];
    if (list.type != json::Value::Arr && autos.type == json::Value::Null && sdk.a.empty())
        return false;
    if (!runtime::enabled("group", "asset_groups"))
        return false;
    config_source = runtime::config_source();
    read_entries(list, config_source);
    read_entries(sdk, {});
    read_auto(autos);
    return true;
}

}

std::string runtime::erp_name(uint64_t hash)
{
    void *m = patched.load();
    if (!m)
        return hex(hash);
    Manifest mf(static_cast<uint8_t *>(m));
    std::string n = mf.name_of(hash);
    return n.empty() ? hex(hash) : n;
}

int runtime::group_add(const json::Value &entry)
{
    std::lock_guard<std::mutex> g(patch_lock);
    if (!h_parse || patched)
        return SWOFTY_ASSET_LATE;
    json::Value list;
    list.a.push_back(entry);
    read_entries(list, {});
    return SWOFTY_ASSET_OK;
}

namespace {

void setup()
{
    base = swofty::api->module_base();
    game_root = module_dir(nullptr);
    bool groups = load_config();
    bool resources = (!runtime::config()["resources"].a.empty() || !runtime::config()["resource_erps"].a.empty() ||
                      !runtime::sdk()["resources"].a.empty()) &&
                     runtime::enabled("erp", nullptr);
    if (!groups && !resources)
        return;
    swofty::log("[assets] %s: %zu declared group(s), %zu auto prefix(es); game root %s",
                groups ? config_source.c_str() : "no asset_groups (resources only)",
                declared.size(), auto_prefixes.size(), narrow(game_root).c_str());
    for (auto &e : declared)
        swofty::log("[assets]   %s -> %s%s", e.group.c_str(), e.erps.empty() ? "?" : e.erps.front().c_str(),
                    e.erps.size() > 1 ? " (+more)" : "");
    if (uintptr_t r = find(T_REHASH_NAMES))
        rehash_names = reinterpret_cast<Rehash_t *>(r);
    if (uintptr_t r = find(T_REHASH_GROUPS))
        rehash_groups = reinterpret_cast<Rehash_t *>(r);
    if (uintptr_t r = find(T_REHASH_DEPS))
        rehash_deps = reinterpret_cast<Rehash_t *>(r);
    if (uintptr_t r = find(T_GLOBAL))
        g_manifest = reinterpret_cast<void **>(r);
    if (uintptr_t r = find(T_ALIAS))
        g_alias = reinterpret_cast<void **>(r);
    if (resources) {
        uintptr_t ex = find(T_EXPAND), rl = find(T_RELEASE);
        if (ex && rl)
            if (auto ih = safetyhook::InlineHook::create(reinterpret_cast<void *>(ex), reinterpret_cast<void *>(&expand_hook)))
                if (auto rh = safetyhook::InlineHook::create(reinterpret_cast<void *>(rl), reinterpret_cast<void *>(&release_hook))) {
                    h_expand = std::move(*ih);
                    h_release = std::move(*rh);
                }
        if (h_expand && h_release) {
            swofty::log("[assets] resources: Asset_Expand_GroupErps and Asset_Release_GroupErps hooked at base+0x%llx and "
                        "base+0x%llx (flag-4 groups that list a target ERP get the overlay first; others pass through)",
                        (unsigned long long)(ex - base), (unsigned long long)(rl - base));
        } else
            swofty::log("[assets] resources: expand/release hooks not installed; flag-4 groups with their own ERP list "
                        "are not covered");
    }
    uintptr_t parse = find(T_PARSE);
    if (parse) {
        auto h = safetyhook::InlineHook::create(reinterpret_cast<void *>(parse), reinterpret_cast<void *>(&parse_hook));
        if (h) {
            h_parse = std::move(*h);
            swofty::log("[assets] manifest parser hooked at base+0x%llx", (unsigned long long)(parse - base));
        } else
            swofty::log("[assets] manifest parser hook at base+0x%llx failed", (unsigned long long)(parse - base));
    }
    void *now = g_manifest && readable(g_manifest, 8) ? *g_manifest : nullptr;
    swofty::log("[assets] manifest global %p = %p, rehash helpers %s", static_cast<void *>(g_manifest), now,
                rehash_names && rehash_groups && rehash_deps ? "resolved" : "MISSING (no room to grow full maps)");
    if (now)
        apply(static_cast<uint8_t *>(now), "already parsed when addons loaded", runtime::resource_overlay(0));
    else if (!h_parse)
        swofty::log("[assets] neither the manifest nor its parser is reachable; registering nothing");
}

}

void runtime::assets_setup()
{
    setup();
}
