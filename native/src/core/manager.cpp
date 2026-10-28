#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bcrypt.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "../loader/data_folder.h"
#include "Zydis.h"
#include "json.hpp"
#include "perf.hpp"
#include "safetyhook.hpp"
#include "scan_cache.hpp"
#include "swofty_api.h"

void crash_attach(SwoftyApi *a, const std::wstring &dir);
void crash_game_hooks();
void settings_attach(SwoftyApi *a, const std::wstring &dir);
int settings_get(const char *id, uint32_t type, SwoftySettingValue *out);
int settings_watch(const char *id, SwoftySettingFn fn, void *user);

namespace runtime {
const SwoftyAssets *assets_table();
const SwoftyEvents *events_table();
const SwoftyOverlay *overlay_table();
const SwoftyTelemetry *telemetry_table();
const SwoftyDb *db_table();
const SwoftyStorage *storage_table();
const SwoftyCareer *career_table();
const SwoftyPopup *popup_table();
const SwoftyLang *lang_table();
const SwoftyCharacters *characters_table();
const SwoftyCareerWizard *wizard_table();
struct CoreMixins {
    const char *name;
    std::vector<SwoftyMixin> mixins;
};
std::vector<CoreMixins> career_mixins();
const SwoftyCareerContent *career_content_table();
std::vector<CoreMixins> career_content_mixins();
void career_content_commit();
const SwoftyEra *era_table();
std::vector<CoreMixins> era_mixins();
void era_commit();
std::vector<CoreMixins> car_category_mixins();
void car_category_commit();
std::vector<CoreMixins> save_splice_mixins();
void save_splice_commit();
std::vector<CoreMixins> ers_newlap_mixins();
void ers_newlap_commit();
std::vector<CoreMixins> track_acclim_mixins();
void track_acclim_commit();
std::vector<CoreMixins> characters_mixins();
std::vector<CoreMixins> wizard_mixins();
std::vector<CoreMixins> tile_route_mixins();
void characters_commit();
void wizard_commit();
void tile_route_commit();
std::string load_stage();
void career_setup();
void popup_setup();
void mfd_setup();
void ready();
void events_drop_module(void *module);
void career_start_drop_module(void *module);
const std::wstring &swofty_root();
void on_stage(void (*fn)(int));
void when_code_ready(const char *what, void (*fn)());
void stage_catch_up();
}

namespace {

static_assert(sizeof(SwoftyContext) == sizeof(safetyhook::Context64));
static_assert(offsetof(SwoftyContext, r12) == offsetof(safetyhook::Context64, r12));
static_assert(offsetof(SwoftyContext, rip) == offsetof(safetyhook::Context64, rip));

struct Trace {
    const char *method = "-";
    int matches = -1, verified = -1;
    long long match = -1, site = -1;
    std::string reason;
};

struct Binding {
    std::string id, kind, status, section, pattern, rkind;
    std::vector<uint8_t> expect;
    long long anchor = 0, insn = 0, len = 0, rva = -1, slots = 0, match = -1;
    bool resolved = false;
    uintptr_t addr = 0;
    std::string sa_string = {};
    long long sa_func_rva = -1, sa_func_off = 0;
    bool sa_durable = false;
    std::string unresolvable = {};
    bool deferred = false;
    Trace tr = {};
};

struct Mixin {
    std::string target;
    uint32_t kind;
    int32_t priority;
    void *handler;
    void **orig;
    uintptr_t addr = 0;
};

struct Addon {
    std::string name;
    std::vector<Mixin> mixins;
    bool active = true;
    bool core = false;
    bool waiting = false, late = false, hit_deferred = false;
    SwoftyAddonInitFn reinit = nullptr;
    HMODULE module = nullptr;
};

struct Handler {
    size_t addon;
    Mixin *m;
};

struct Group {
    uint32_t kind;
    uintptr_t addr;
    size_t span;
    std::vector<Handler> handlers;
};

struct Held {
    uint32_t kind;
    uintptr_t addr;
    size_t span;
};

struct Mark {
    size_t written, stubs, mids, inlines, sites;
};

constexpr size_t MAX_SITES = 256;

SwoftyApi *api;
uintptr_t base;
bool same_build;
std::vector<Binding> bindings;
std::unordered_map<std::string, size_t> binding_index;
std::string bindings_file = "bindings.json";
struct ScanHit {
    uintptr_t at;
    int hits;
};
std::unordered_map<std::string, ScanHit> scans;
std::unordered_map<std::string, uintptr_t> anchor_funcs;
bool kb_first = true;
int (*loader_scan)(const char *, const char *, uintptr_t *);
scancache::Cache scan_cache;
std::wstring scan_cache_path, cache_dir;
const char *cache_name = "swofty\\cache";
bool scan_cache_on;
std::unordered_map<std::string, std::string> section_alias;
SRWLOCK scan_lock = SRWLOCK_INIT;
json::Value session;
bool have_session;
std::vector<Addon> addons;
Addon *current;
size_t settled;
std::vector<Held> held;
Mark mark;
bool closed;

constexpr double ENCRYPTED = 7.5, PLAIN = 6.5;
constexpr int RETRIES = 8;
constexpr DWORD RETRY_MS = 1000;
bool packed;
std::string build_tag = "unknown";
long long exe_stamp, exe_bytes = -1;
std::string exe_sha = "-";
struct SectionRead {
    double entropy;
    DWORD at;
};
std::unordered_map<std::string, SectionRead> section_reads;
std::recursive_mutex commit_lock, resolve_lock;
volatile LONG retries_started;
bool unreported;
std::atomic<bool> code_plain;
struct CodeTask {
    std::string what;
    void (*fn)();
};
std::vector<CodeTask> code_tasks;

std::vector<safetyhook::InlineHook> inline_hooks;
std::vector<std::string> inline_names;
std::vector<safetyhook::MidHook> mid_hooks;
std::vector<safetyhook::Allocation> stubs;
std::vector<std::pair<uintptr_t, std::vector<uint8_t>>> written;
struct SiteHandler {
    void (*fn)(SwoftyContext *);
    uint32_t probe;
};

std::vector<SiteHandler> site_chains[MAX_SITES];
size_t sites_used;

template <size_t I> void site_thunk(safetyhook::Context &c)
{
    for (auto &h : site_chains[I]) {
        perf::Scope p(h.probe);
        h.fn(reinterpret_cast<SwoftyContext *>(&c));
    }
}

template <size_t... I> constexpr auto make_thunks(std::index_sequence<I...>)
{
    return std::array<safetyhook::MidHookFn, sizeof...(I)>{site_thunk<I>...};
}
constexpr auto site_thunks = make_thunks(std::make_index_sequence<MAX_SITES>{});

const char *kind_name(uint32_t k)
{
    static const char *names[] = {"?", "wrap", "site", "vtable", "redirect", "patch", "on_load"};
    k = SWOFTY_MIXIN_KIND(k);
    return k < 7 ? names[k] : "?";
}

unsigned long long rva(uintptr_t a)
{
    return a - base;
}

std::string utf8(const wchar_t *w)
{
    char buf[MAX_PATH * 3] = {};
    WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, sizeof buf - 1, nullptr, nullptr);
    return buf;
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

std::string select_bindings(const std::wstring &dir, long long stamp, std::string &chosen)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"bindings*.json").c_str(), &fd);
    std::string match_text, default_text, first_text;
    std::wstring match_name, first_name;
    int candidates = 0;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            std::string text = read_file(dir + fd.cFileName);
            json::Value d;
            if (!json::parse(text, d) || d["build"].type == json::Value::Null)
                continue;
            candidates++;
            if (first_text.empty())
                first_text = text, first_name = fd.cFileName;
            if (!_wcsicmp(fd.cFileName, L"bindings.json"))
                default_text = text;
            if (match_text.empty() && (long long)d["build"]["pe_timestamp"].num() == stamp)
                match_text = text, match_name = fd.cFileName;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (!match_text.empty()) {
        chosen = utf8(match_name.c_str());
        api->log("[core] bindings: %d set(s) present, selected %s by PE timestamp %lld", candidates, chosen.c_str(), stamp);
        return match_text;
    }
    if (!default_text.empty()) {
        chosen = "bindings.json";
        api->log("[core] bindings: %d set(s) present, none match PE timestamp %lld; using bindings.json", candidates,
                 stamp);
        return default_text;
    }
    chosen = first_name.empty() ? "bindings.json" : utf8(first_name.c_str());
    if (candidates)
        api->log("[core] bindings: %d set(s) present, none match PE timestamp %lld and no bindings.json; using %s",
                 candidates, stamp, chosen.c_str());
    return first_text;
}

std::vector<uint8_t> hex_bytes(const std::string &s)
{
    std::vector<uint8_t> out;
    for (const char *p = s.c_str(); *p;) {
        char *end;
        long v = strtol(p, &end, 16);
        if (end == p)
            break;
        out.push_back((uint8_t)v);
        p = end;
    }
    return out;
}

const IMAGE_SECTION_HEADER *section_of(uintptr_t a)
{
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + reinterpret_cast<const IMAGE_DOS_HEADER *>(base)->e_lfanew);
    auto s = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, s++)
        if (a - (base + s->VirtualAddress) < (s->Misc.VirtualSize ? s->Misc.VirtualSize : s->SizeOfRawData))
            return s;
    return nullptr;
}

bool section_named(uintptr_t a, const std::string &name)
{
    auto s = section_of(a);
    return s && name.size() <= 8 && !strncmp(reinterpret_cast<const char *>(s->Name), name.c_str(), 8);
}

bool is_code(uintptr_t a)
{
    auto s = section_of(a);
    return s && (s->Characteristics & IMAGE_SCN_MEM_EXECUTE) && !(s->Characteristics & IMAGE_SCN_MEM_WRITE);
}

uint8_t byte_at(uintptr_t a)
{
    return *reinterpret_cast<const uint8_t *>(a);
}

size_t insn_span(uintptr_t a, size_t min)
{
    ZydisDecoder d;
    ZydisDecoderInit(&d, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    size_t n = 0;
    while (n < min) {
        ZydisDecodedInstruction i;
        if (!ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(&d, nullptr, reinterpret_cast<void *>(a + n), 15, &i)))
            return std::max(n, min);
        n += i.length;
    }
    return n;
}

uintptr_t unresolved(const Binding &b, const char *why)
{
    api->log("[core] binding %s: %s", b.id.c_str(), why);
    return 0;
}

const IMAGE_NT_HEADERS *nt_headers()
{
    return reinterpret_cast<const IMAGE_NT_HEADERS *>(base + reinterpret_cast<const IMAGE_DOS_HEADER *>(base)->e_lfanew);
}

bool page_readable(uintptr_t p, size_t n)
{
    MEMORY_BASIC_INFORMATION m{};
    if (!VirtualQuery(reinterpret_cast<void *>(p), &m, sizeof m) || m.State != MEM_COMMIT ||
        !(m.Protect & 0xEE) || (m.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
        return false;
    return p + n <= reinterpret_cast<uintptr_t>(m.BaseAddress) + m.RegionSize;
}

template <class F> void each_committed(uintptr_t lo, uintptr_t hi, F fn)
{
    for (uintptr_t a = lo; a < hi;) {
        MEMORY_BASIC_INFORMATION m{};
        if (!VirtualQuery(reinterpret_cast<void *>(a), &m, sizeof m))
            break;
        uintptr_t next = reinterpret_cast<uintptr_t>(m.BaseAddress) + m.RegionSize;
        if (next > hi)
            next = hi;
        if (next <= a)
            break;
        if (m.State == MEM_COMMIT && (m.Protect & 0xEE) && !(m.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
            fn(a, next - a);
        a = next;
    }
}

uintptr_t section_end(const IMAGE_SECTION_HEADER *s)
{
    return base + s->VirtualAddress + (s->Misc.VirtualSize ? s->Misc.VirtualSize : s->SizeOfRawData);
}

uintptr_t locate_string(const std::string &str)
{
    std::string want = str;
    want.push_back('\0');
    auto nt = nt_headers();
    auto s = IMAGE_FIRST_SECTION(nt);
    uintptr_t hit = 0;
    int hits = 0;
    for (int i = 0; i < nt->FileHeader.NumberOfSections && hits < 2; i++, s++) {
        DWORD ch = s->Characteristics;
        if (!(ch & IMAGE_SCN_MEM_READ) || (ch & IMAGE_SCN_MEM_EXECUTE))
            continue;
        each_committed(base + s->VirtualAddress, section_end(s), [&](uintptr_t p, size_t n) {
            if (n < want.size())
                return;
            const uint8_t *lo = reinterpret_cast<const uint8_t *>(p), *last = lo + n - want.size();
            for (const uint8_t *q = lo; q <= last;) {
                const uint8_t *f = reinterpret_cast<const uint8_t *>(memchr(q, want[0], last - q + 1));
                if (!f)
                    break;
                if (!memcmp(f, want.data(), want.size()) && hits++ == 0)
                    hit = reinterpret_cast<uintptr_t>(f);
                q = f + 1;
            }
        });
    }
    return hits == 1 ? hit : 0;
}

uintptr_t func_base_of(uintptr_t site)
{
    struct RtFunc {
        uint32_t begin, end, unwind;
    };
    auto nt = nt_headers();
    auto &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (!dir.VirtualAddress || dir.Size < sizeof(RtFunc))
        return 0;
    auto *fn = reinterpret_cast<const RtFunc *>(base + dir.VirtualAddress);
    size_t n = dir.Size / sizeof(RtFunc), lo = 0, hi = n;
    uint32_t r = static_cast<uint32_t>(site - base);
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (fn[mid].begin <= r)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo && fn[lo - 1].begin <= r && r < fn[lo - 1].end)
        return base + fn[lo - 1].begin;
    return 0;
}

uintptr_t anchor_func(const std::string &str)
{
    auto cached = anchor_funcs.find(str);
    if (cached != anchor_funcs.end())
        return cached->second;
    uintptr_t sVA = locate_string(str), fbase = 0;
    bool conflict = false;
    long long sites = 0;
    if (sVA) {
        auto nt = nt_headers();
        auto s = IMAGE_FIRST_SECTION(nt);
        for (int i = 0; i < nt->FileHeader.NumberOfSections && !conflict; i++, s++) {
            DWORD ch = s->Characteristics;
            if (!(ch & IMAGE_SCN_MEM_EXECUTE) || !(ch & IMAGE_SCN_MEM_READ) || (ch & IMAGE_SCN_MEM_WRITE))
                continue;
            each_committed(base + s->VirtualAddress, section_end(s), [&](uintptr_t p, size_t n) {
                if (n < 4)
                    return;
                for (uintptr_t q = p, last = p + n - 4; q <= last; q++) {
                    if (q + 4 + *reinterpret_cast<const int32_t *>(q) != sVA)
                        continue;
                    uintptr_t f = func_base_of(q);
                    if (!f)
                        continue;
                    sites++;
                    if (!fbase)
                        fbase = f;
                    else if (fbase != f)
                        conflict = true;
                }
            });
        }
    }
    uintptr_t out = (conflict || !sites) ? 0 : fbase;
    anchor_funcs[str] = out;
    return out;
}

uintptr_t apply_match(const Binding &b, uintptr_t match, const char **why)
{
    uintptr_t at = match + b.anchor, target = at, i = match + b.insn;
    if (b.rkind == "rel32" || b.rkind == "riprel") {
        if (!page_readable(std::min(at, i), (at > i ? at - i : i - at) + 16)) {
            *why = "anchor offset is not mapped on this build";
            return 0;
        }
    }
    if (b.rkind == "rel32") {
        bool branch = (byte_at(i) == 0xE8 || byte_at(i) == 0xE9) && at == i + 1;
        bool jcc = byte_at(i) == 0x0F && (byte_at(i + 1) & 0xF0) == 0x80 && at == i + 2;
        if (!branch && !jcc) {
            *why = "anchor is not the rel32 of a call or jump";
            return 0;
        }
        target = at + 4 + *reinterpret_cast<const int32_t *>(at);
    } else if (b.rkind == "riprel") {
        if (b.anchor <= b.insn || b.anchor + 4 > b.insn + b.len) {
            *why = "anchor is not inside the RIP-relative instruction";
            return 0;
        }
        target = i + b.len + *reinterpret_cast<const int32_t *>(at);
    } else if (b.rkind != "addr") {
        *why = "unknown resolve kind";
        return 0;
    }
    if (!b.expect.empty() &&
        (!page_readable(at, b.expect.size()) || memcmp(reinterpret_cast<const void *>(at), b.expect.data(), b.expect.size()))) {
        *why = "expect bytes differ";
        return 0;
    }
    bool code = b.kind == "func" || b.kind == "site" || b.kind == "call";
    auto own = section_of(target);
    bool own_exec_site = (b.kind == "site" || b.kind == "call") && own && (own->Characteristics & IMAGE_SCN_MEM_EXECUTE) &&
                         section_named(target, b.section);
    if (code ? !is_code(target) && !own_exec_site : !own) {
        *why = code ? "target is not in read-only code" : "target is outside the exe";
        return 0;
    }
    if (same_build && b.rva >= 0 && rva(target) != (unsigned long long)b.rva) {
        *why = "target differs from the KB build";
        return 0;
    }
    return target;
}

const std::vector<uint8_t> *hooked_range(uintptr_t a, uintptr_t &start)
{
    for (auto &h : inline_hooks)
        if (a - h.target_address() < h.original_bytes().size())
            return start = h.target_address(), &h.original_bytes();
    for (auto &h : mid_hooks)
        if (a - h.target_address() < h.original_bytes().size())
            return start = h.target_address(), &h.original_bytes();
    for (auto &w : written)
        if (a - w.first < w.second.size())
            return start = w.first, &w.second;
    return nullptr;
}

bool bytes_at(const Binding &b, uintptr_t at, bool &under)
{
    auto pat = scancache::tokens(b.pattern);
    under = false;
    if (pat.empty() || !page_readable(at, pat.size()))
        return false;
    for (size_t k = 0; k < pat.size(); k++) {
        uintptr_t start = 0;
        auto orig = hooked_range(at + k, start);
        under |= orig != nullptr;
        uint8_t v = orig ? (*orig)[at + k - start] : byte_at(at + k);
        if (pat[k] >= 0 && v != pat[k])
            return false;
    }
    return true;
}

uintptr_t match_under_hooks(const Binding &b)
{
    if (!same_build || b.rva < 0 || b.rkind != "addr")
        return 0;
    uintptr_t at = base + b.rva - b.anchor;
    bool under = false;
    return bytes_at(b, at, under) && under ? at : 0;
}

uintptr_t kb_match(const Binding &b)
{
    if (!kb_first || !same_build || b.match < 0 || b.pattern.empty())
        return 0;
    uintptr_t at = base + b.match;
    bool under = false;
    if (!section_named(at, b.section) || !bytes_at(b, at, under)) {
        api->log("[core] binding %s: the pattern does not match at the KB address 0x%llx; scanning %s", b.id.c_str(),
                 (unsigned long long)at, b.section.c_str());
        return 0;
    }
    if (under && b.rkind != "addr")
        return 0;
    if (under)
        api->log("[core] binding %s: its bytes carry an installed hook; the pattern matches the bytes under it at the KB "
                 "address 0x%llx", b.id.c_str(), (unsigned long long)at);
    return at;
}

int core_scan(const char *section, const char *pattern, uintptr_t *out);

const IMAGE_SECTION_HEADER *section_by_name(const std::string &name)
{
    auto nt = nt_headers();
    auto s = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, s++)
        if (name.size() <= 8 && !strncmp(reinterpret_cast<const char *>(s->Name), name.c_str(), 8))
            return s;
    return nullptr;
}

const IMAGE_SECTION_HEADER *code_section()
{
    auto nt = nt_headers();
    auto s = IMAGE_FIRST_SECTION(nt);
    const IMAGE_SECTION_HEADER *exec = nullptr;
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, s++) {
        DWORD c = s->Characteristics;
        if ((c & IMAGE_SCN_MEM_EXECUTE) && (c & IMAGE_SCN_MEM_READ) && !(c & IMAGE_SCN_MEM_WRITE))
            return s;
        if ((c & IMAGE_SCN_MEM_EXECUTE) && !exec)
            exec = s;
    }
    return exec;
}

double page_entropy(uintptr_t p)
{
    if (!page_readable(p, 0x1000))
        return -1;
    unsigned long cnt[256] = {0};
    auto b = reinterpret_cast<const uint8_t *>(p);
    for (int i = 0; i < 0x1000; i++)
        cnt[b[i]]++;
    double h = 0;
    for (int i = 0; i < 256; i++)
        if (cnt[i]) {
            double pr = cnt[i] / 4096.0;
            h -= pr * std::log2(pr);
        }
    return h;
}

double section_entropy(const IMAGE_SECTION_HEADER *s)
{
    uintptr_t start = base + s->VirtualAddress;
    size_t size = s->Misc.VirtualSize ? s->Misc.VirtualSize : s->SizeOfRawData;
    double sum = 0;
    int ok = 0;
    for (int k = 0; k < 16; k++) {
        size_t off = size > 16 * 0x1000 ? (size_t)((double)k / 16 * (double)(size - 0x1000)) & ~(size_t)0xfff
                                        : (size_t)k * 0x1000;
        if (k && off + 0x1000 > size)
            break;
        double e = page_entropy(start + off);
        if (e >= 0)
            ok++, sum += e;
    }
    return ok ? sum / ok : -1;
}

const char *reads(double e)
{
    return e < 0 ? "unreadable" : e > ENCRYPTED ? "encrypted" : e > PLAIN ? "partly encrypted" : "decrypted";
}

/* Bulk decryption only goes one way, so a section seen decrypted is not sampled again; an encrypted one is
   resampled at most every 50 ms because addon inits ask for many bindings in a row. */
double section_now(const std::string &name)
{
    auto s = section_by_name(name);
    if (!s)
        return -1;
    DWORD now = GetTickCount();
    auto it = section_reads.find(name);
    if (it != section_reads.end() && ((it->second.entropy >= 0 && it->second.entropy <= PLAIN) || now - it->second.at < 50))
        return it->second.entropy;
    double e = section_entropy(s);
    section_reads[name] = {e, now};
    return e;
}

bool page_encrypted(uintptr_t a)
{
    double e = page_entropy(a & ~uintptr_t(0xfff));
    return e < 0 || e > ENCRYPTED;
}

bool in_exec(uintptr_t a)
{
    auto s = section_of(a);
    return s && (s->Characteristics & IMAGE_SCN_MEM_EXECUTE);
}

bool guarded(const Binding &b)
{
    auto s = packed ? section_by_name(b.section) : nullptr;
    return s && (s->Characteristics & IMAGE_SCN_MEM_EXECUTE);
}

uintptr_t resolve_by_pattern(Binding &b, const char **why, bool *at_kb)
{
    Trace &t = b.tr;
    uintptr_t match = kb_match(b);
    *at_kb = match != 0;
    if (kb_first && same_build && b.match >= 0)
        t.match = t.site = b.match, t.verified = match ? 1 : 0;
    if (match) {
        t.method = "hint", t.matches = 1;
        uintptr_t target = apply_match(b, match, why);
        t.verified = target != 0;
        return target;
    }
    auto key = b.section + '|' + b.pattern;
    auto hit = scans.find(key);
    if (hit == scans.end()) {
        int hits = core_scan(b.section.c_str(), b.pattern.c_str(), &match);
        hit = scans.emplace(key, ScanHit{match, hits}).first;
    }
    match = hit->second.at;
    t.matches = hit->second.hits;
    bool under = false;
    if (!match && (match = match_under_hooks(b))) {
        under = true;
        api->log("[core] binding %s: its bytes carry an installed hook; the pattern matches the bytes under it at the KB "
                 "address 0x%llx", b.id.c_str(), (unsigned long long)match);
    }
    if (!match) {
        *why = "signature did not match exactly once";
        return 0;
    }
    t.method = under ? "hint" : "scan";
    t.match = t.site = rva(match);
    uintptr_t target = apply_match(b, match, why);
    t.verified = target != 0;
    return target;
}

uintptr_t resolve_by_anchor(Binding &b, const char **why)
{
    if (b.sa_string.empty()) {
        *why = "no string anchor";
        return 0;
    }
    uintptr_t f = anchor_func(b.sa_string);
    if (!f) {
        *why = "string anchor did not locate a unique function";
        return 0;
    }
    if (b.tr.site < 0)
        b.tr.site = rva(f + b.sa_func_off);
    if (same_build && b.sa_func_rva >= 0 && rva(f) != (unsigned long long)b.sa_func_rva) {
        *why = "string anchor function differs from the KB build";
        return 0;
    }
    return apply_match(b, f + b.sa_func_off, why);
}

/* One order for every build: the set's verified hint, then a section scan, then the string anchor. A set
   entry without a pattern is the old per-build format and can only use its string anchor. */
uintptr_t resolve_binding(Binding &b)
{
    b.tr = {};
    if (!b.unresolvable.empty()) {
        b.tr.reason = "unresolvable: " + b.unresolvable;
        api->log("[core] binding %s: the set marks it unresolvable on this build (%s)", b.id.c_str(), b.unresolvable.c_str());
        return 0;
    }
    std::string missing;
    const char *pw = "signature did not match exactly once", *aw = "no string anchor";
    uintptr_t target = 0;
    const char *method = "";
    bool kb = false, legacy = b.pattern.empty();
    if (legacy) {
        pw = "the set gives no pattern (legacy format)";
    } else if (!section_by_name(b.section)) {
        missing = "section " + b.section + " is not in this exe";
        pw = missing.c_str();
    } else if ((target = resolve_by_pattern(b, &pw, &kb))) {
        method = kb ? "pattern at the KB address" : "pattern";
    }
    if (!target && !b.sa_string.empty() && (target = resolve_by_anchor(b, &aw))) {
        method = legacy ? "string anchor (legacy set entry, no pattern)" : "string anchor (pattern fell back)";
        b.tr.method = legacy ? "anchor" : "anchor-fallback";
        b.tr.match = -1, b.tr.verified = -1;
    }
    if (!target) {
        b.tr.method = "-";
        b.tr.reason = b.sa_string.empty() ? std::string(pw) : std::string("pattern: ") + pw + "; string anchor: " + aw;
        if (b.sa_string.empty())
            return unresolved(b, pw);
        api->log("[core] binding %s: unresolved (pattern: %s; string anchor: %s)", b.id.c_str(), pw, aw);
        return 0;
    }
    if (legacy)
        b.tr.reason = "legacy set entry without a pattern";
    api->log("[core] bind %s -> 0x%llx (base+0x%llx), %s %s via %s", b.id.c_str(), (unsigned long long)target,
             rva(target), b.kind.c_str(), b.status.c_str(), method);
    return target;
}

void settle(Binding &b, uintptr_t addr)
{
    b.addr = addr, b.resolved = true, b.deferred = false;
}

void defer(Binding &b, const std::string &why, bool final)
{
    scans.erase(b.section + '|' + b.pattern);
    for (auto it = anchor_funcs.begin(); it != anchor_funcs.end();)
        it = it->second ? std::next(it) : anchor_funcs.erase(it);
    if (final) {
        b.tr.method = "-";
        b.tr.reason = why + "; still so at the last retry, so it fails closed";
        if (b.tr.site < 0 && b.match >= 0)
            b.tr.site = b.match;
        api->log("[core] binding %s: %s", b.id.c_str(), b.tr.reason.c_str());
        return settle(b, 0);
    }
    b.tr = {};
    b.tr.reason = why;
    b.addr = 0, b.resolved = false, b.deferred = true;
}

/* A packed build's code reads encrypted until the game decrypts it, so its bindings wait for that instead of
   resolving to garbage: the section must read decrypted, and a resolved site's own page too. A pattern that
   fails while some of the section still reads encrypted is retried later, not given up. */
void attempt(Binding &b, bool final)
{
    if (!guarded(b) || !b.unresolvable.empty())
        return settle(b, resolve_binding(b));
    char why[200];
    double e = section_now(b.section);
    if (e < 0 || e > ENCRYPTED) {
        snprintf(why, sizeof why, "code section %s reads %s (entropy %.2f)", b.section.c_str(), reads(e), e < 0 ? 0 : e);
        b.tr = {};
        return defer(b, why, final);
    }
    uintptr_t target = resolve_binding(b);
    if (target) {
        uintptr_t m = b.tr.match >= 0 ? base + b.tr.match : 0;
        uintptr_t bad = m && page_encrypted(m) ? m : in_exec(target) && page_encrypted(target) ? target : 0;
        if (!bad)
            return settle(b, target);
        snprintf(why, sizeof why, "the page at base+0x%llx still reads encrypted", rva(bad));
        return defer(b, why, final);
    }
    bool partly = e > PLAIN || (b.tr.site >= 0 && page_encrypted(base + b.tr.site));
    if (!partly)
        return settle(b, 0);
    snprintf(why, sizeof why, "code section %s reads %s (entropy %.2f): ", b.section.c_str(), reads(e), e);
    defer(b, why + b.tr.reason, final);
}

std::string quoted(const std::string &s)
{
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c == '\r' || c == '\n' ? ' ' : c;
    }
    return out;
}

std::string hex_rva(long long v)
{
    char buf[24];
    snprintf(buf, sizeof buf, "0x%llx", v);
    return v < 0 ? "-" : buf;
}

std::string site_bytes(long long site)
{
    std::string out;
    for (int i = 0; i < 32; i++) {
        uintptr_t a = base + site + i, start = 0;
        if (!page_readable(a, 1))
            break;
        auto orig = hooked_range(a, start);
        char h[4];
        snprintf(h, sizeof h, "%s%02X", i ? " " : "", orig ? (*orig)[a - start] : byte_at(a));
        out += h;
    }
    return out;
}

void report(const char *pass)
{
    unreported = false;
    size_t done = 0, waiting = 0, failed = 0;
    for (auto &b : bindings) {
        const Trace &t = b.tr;
        bool ok = b.resolved && b.addr;
        const char *result = b.deferred ? "deferred" : ok ? "resolved" : "unresolved";
        (b.deferred ? waiting : ok ? done : failed)++;
        std::string line = "[bind] id=" + b.id + " build=" + build_tag + " pass=" + pass + " result=" + result +
                           " method=" + (ok ? t.method : "-") + " matches=" +
                           (b.deferred || t.matches < 0 ? "-" : std::to_string(t.matches)) +
                           " rva=" + (ok ? hex_rva((long long)rva(b.addr)) : "-") +
                           " match=" + (b.deferred ? "-" : hex_rva(t.match)) +
                           " verified=" + (b.deferred || t.verified < 0 ? "-" : t.verified ? "yes" : "no") +
                           " reason=\"" + quoted(t.reason) + "\"";
        if (!ok && !b.deferred)
            line += " site=" + hex_rva(t.site) + " site_bytes=\"" + (t.site >= 0 ? site_bytes(t.site) : "") + "\"";
        else
            line += " site_bytes=\"\"";
        api->log("%s", line.c_str());
    }
    api->log("[bind] summary build=%s pass=%s resolved=%zu deferred=%zu unresolved=%zu total=%zu pe_timestamp=%lld "
             "size=%lld sha256=%s",
             build_tag.c_str(), pass, done, waiting, failed, bindings.size(), exe_stamp, exe_bytes, exe_sha.c_str());
}

std::string exe_sha256(const std::wstring &exe, const std::wstring &cache)
{
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (!GetFileAttributesExW(exe.c_str(), GetFileExInfoStandard, &fa))
        return "-";
    char key[96];
    snprintf(key, sizeof key, "%lld\t%llu\t%lld\t", exe_bytes,
             ((unsigned long long)fa.ftLastWriteTime.dwHighDateTime << 32) | fa.ftLastWriteTime.dwLowDateTime, exe_stamp);
    std::string kept = read_file(cache);
    if (!kept.compare(0, strlen(key), key) && kept.size() >= strlen(key) + 64)
        return kept.substr(strlen(key), 64);
    HMODULE bc = LoadLibraryW(L"bcrypt.dll");
    auto open = reinterpret_cast<decltype(&BCryptOpenAlgorithmProvider)>(GetProcAddress(bc, "BCryptOpenAlgorithmProvider"));
    auto create = reinterpret_cast<decltype(&BCryptCreateHash)>(GetProcAddress(bc, "BCryptCreateHash"));
    auto data = reinterpret_cast<decltype(&BCryptHashData)>(GetProcAddress(bc, "BCryptHashData"));
    auto finish = reinterpret_cast<decltype(&BCryptFinishHash)>(GetProcAddress(bc, "BCryptFinishHash"));
    auto destroy = reinterpret_cast<decltype(&BCryptDestroyHash)>(GetProcAddress(bc, "BCryptDestroyHash"));
    auto close = reinterpret_cast<decltype(&BCryptCloseAlgorithmProvider)>(GetProcAddress(bc, "BCryptCloseAlgorithmProvider"));
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    if (!open || !create || !data || !finish || !destroy || !close || open(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))
        return "-";
    HANDLE f = CreateFileW(exe.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    bool ok = f != INVALID_HANDLE_VALUE && !create(alg, &h, nullptr, 0, nullptr, 0, 0);
    std::vector<uint8_t> buf(size_t(4) << 20);
    for (DWORD got = 0; ok && ReadFile(f, buf.data(), DWORD(buf.size()), &got, nullptr) && got;)
        ok = !data(h, buf.data(), got, 0);
    uint8_t digest[32];
    ok = ok && !finish(h, digest, sizeof digest, 0);
    if (h)
        destroy(h);
    close(alg, 0);
    if (f != INVALID_HANDLE_VALUE)
        CloseHandle(f);
    if (!ok)
        return "-";
    std::string hex;
    for (uint8_t c : digest) {
        char x[3];
        snprintf(x, sizeof x, "%02x", c);
        hex += x;
    }
    if (FILE *o = _wfopen(cache.c_str(), L"wb")) {
        fprintf(o, "%s%s\n", key, hex.c_str());
        fclose(o);
    }
    return hex;
}

int core_scan(const char *section, const char *pattern, uintptr_t *out)
{
    if (out)
        *out = 0;
    if (!scan_cache_on || !section || !pattern)
        return loader_scan(section, pattern, out);
    uint64_t known = 0;
    AcquireSRWLockShared(&scan_lock);
    bool have = scan_cache.find(section, pattern, known);
    ReleaseSRWLockShared(&scan_lock);
    if (have) {
        auto pat = scancache::tokens(pattern);
        uintptr_t at = base + known;
        if (section_named(at, section) && page_readable(at, pat.size()) &&
            scancache::matches(reinterpret_cast<const uint8_t *>(at), pat)) {
            api->log("scan %s [%s] -> 1 hit at 0x%llx (base+0x%llx), the address cached for this exe", section, pattern,
                     (unsigned long long)at, (unsigned long long)known);
            if (out)
                *out = at;
            return 1;
        }
    }
    uintptr_t at = 0;
    int hits = loader_scan(section, pattern, &at);
    AcquireSRWLockExclusive(&scan_lock);
    scan_cache.found(section, pattern, hits, hits == 1 ? rva(at) : 0);
    ReleaseSRWLockExclusive(&scan_lock);
    if (out && hits == 1)
        *out = at;
    return hits;
}

/* The key names the exe by fields that stay put while it runs: a packer may rewrite section flags, sizes or
   the image base in memory, and the decrypted-stage scans must still be found by the next launch. */
void read_section_map(const json::Value &doc, const std::string &file)
{
    const auto &given = doc["sections"].type == json::Value::Obj ? doc["sections"] : doc["build"]["sections"];
    for (auto &[from, to] : given.o)
        if (to.type == json::Value::Str && from != to.s && section_by_name(to.s))
            section_alias[from] = to.s;
    const char *source = "the set's section map";
    std::unordered_map<std::string, size_t> uses;
    for (auto &b : bindings)
        uses[b.section]++;
    auto top = std::max_element(uses.begin(), uses.end(), [](auto &x, auto &y) { return x.second < y.second; });
    if (section_alias.empty() && !bindings.empty() && !uses.count(".xtls") && top != uses.end()) {
        auto s = section_by_name(top->first);
        if (s && (s->Characteristics & IMAGE_SCN_MEM_EXECUTE))
            section_alias[".xtls"] = top->first, source = "inferred: no binding of the set scans .xtls";
    }
    for (auto &[from, to] : section_alias)
        api->log("[core] %s: scans of %s by core code and addons go to %s on this build (%s)", file.c_str(),
                 from.c_str(), to.c_str(), source);
}

uint64_t exe_key()
{
    auto nt = nt_headers();
    auto &fh = nt->FileHeader;
    auto &oh = nt->OptionalHeader;
    uint64_t k = scancache::fnv64(&fh.Machine, sizeof fh.Machine);
    k = scancache::fnv64(&fh.NumberOfSections, sizeof fh.NumberOfSections, k);
    k = scancache::fnv64(&fh.TimeDateStamp, sizeof fh.TimeDateStamp, k);
    k = scancache::fnv64(&oh.AddressOfEntryPoint, sizeof oh.AddressOfEntryPoint, k);
    k = scancache::fnv64(&oh.SizeOfImage, sizeof oh.SizeOfImage, k);
    k = scancache::fnv64(&oh.CheckSum, sizeof oh.CheckSum, k);
    k = scancache::fnv64(&exe_bytes, sizeof exe_bytes, k);
    auto s = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < fh.NumberOfSections; i++, s++) {
        k = scancache::fnv64(s->Name, sizeof s->Name, k);
        k = scancache::fnv64(&s->VirtualAddress, sizeof s->VirtualAddress, k);
        k = scancache::fnv64(&s->SizeOfRawData, sizeof s->SizeOfRawData, k);
        k = scancache::fnv64(&s->PointerToRawData, sizeof s->PointerToRawData, k);
    }
    return k;
}

std::string section_name(const IMAGE_SECTION_HEADER *s)
{
    char n[9] = {};
    if (s)
        memcpy(n, s->Name, 8);
    return n;
}

/* A scan of encrypted bytes matches garbage or nothing, so on a packed build it is refused while its section
   reads encrypted, and a hit on a page that still does is dropped. An addon init that hit one is called again
   once the code decrypts, as for a deferred binding. */
bool scan_refused(const char *section, const char *pattern, uintptr_t hit)
{
    char why[160];
    if (!hit) {
        auto s = section ? section_by_name(section) : code_section();
        if (!s || !(s->Characteristics & IMAGE_SCN_MEM_EXECUTE))
            return false;
        double e = section_now(section_name(s));
        if (e >= 0 && e <= ENCRYPTED)
            return false;
        snprintf(why, sizeof why, "%s reads %s (entropy %.2f)", section_name(s).c_str(), reads(e), e < 0 ? 0 : e);
    } else {
        if (!in_exec(hit) || !page_encrypted(hit))
            return false;
        snprintf(why, sizeof why, "its hit at base+0x%llx is on a page that still reads encrypted", rva(hit));
    }
    if (current)
        current->hit_deferred = true;
    api->log("[core] scan %s [%s]%s%s refused: %s on this packed build; nothing resolves in encrypted code",
             section ? section : "<exec>", pattern, current ? " for " : "", current ? current->name.c_str() : "", why);
    return true;
}

/* Core code and addons written for Steam scan Steam's section names; the set's section map sends them to this
   build's section. Bindings already name their own build's section and scan without the map. */
int api_scan(const char *section, const char *pattern, uintptr_t *out)
{
    auto it = section ? section_alias.find(section) : section_alias.end();
    const char *real = it == section_alias.end() ? section : it->second.c_str();
    if (!packed)
        return core_scan(real, pattern, out);
    std::lock_guard<std::recursive_mutex> g(resolve_lock);
    if (out)
        *out = 0;
    uintptr_t at = 0;
    if (scan_refused(real, pattern, 0))
        return 0;
    int hits = core_scan(real, pattern, &at);
    if (hits == 1 && scan_refused(real, pattern, at))
        return 0;
    if (out)
        *out = at;
    return hits;
}

const std::wstring &caches(const std::wstring &dir)
{
    if (!cache_dir.empty())
        return cache_dir;
    cache_dir = dir + L"..\\..\\cache\\";
    CreateDirectoryW(cache_dir.c_str(), nullptr);
    HANDLE probe = CreateFileW((cache_dir + L"write-test.tmp").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (probe != INVALID_HANDLE_VALUE) {
        CloseHandle(probe);
        return cache_dir;
    }
    DWORD error = GetLastError();
    wchar_t folder[MAX_PATH * 2];
    if (!swofty_data_folder(folder, MAX_PATH * 2)) {
        api->log("[core] caches: the game can't create files in swofty\\cache (error %lu) and LOCALAPPDATA is not set; "
                 "the scan and exe hash caches are not kept",
                 error);
        return cache_dir;
    }
    std::wstring data = folder;
    CreateDirectoryW(data.c_str(), nullptr);
    CreateDirectoryW((data += L"\\cache").c_str(), nullptr);
    CreateDirectoryW((data += L"\\game").c_str(), nullptr);
    cache_dir = data + L"\\";
    cache_name = "%LOCALAPPDATA%\\Swofty\\cache\\game";
    api->log("[core] caches: the game can't create files in swofty\\cache (error %lu); the scan and exe hash caches "
             "are in %s",
             error, cache_name);
    return cache_dir;
}

void open_scan_cache(SwoftyApi *a, const std::wstring &dir)
{
    loader_scan = a->scan;
    a->scan = api_scan;
    char v[16] = {0};
    if (GetEnvironmentVariableA("SWOFTY_RESOLVE", v, sizeof v) && !_stricmp(v, "scan")) {
        kb_first = false;
        api->log("[core] binding resolution: SWOFTY_RESOLVE=scan, every pattern is scanned and the scan cache is off");
        return;
    }
    uint64_t key = exe_key();
    scan_cache_path = caches(dir) + L"scan-cache.tsv";
    std::string text = read_file(scan_cache_path);
    if (text.empty()) {
        scan_cache.parse("", key);
        api->log("[core] scan cache: none yet for this exe (key %016llx); patterns found by a scan are kept in "
                 "%s\\scan-cache.tsv", (unsigned long long)key, cache_name);
    } else if (!scan_cache.parse(text, key)) {
        api->log("[core] scan cache: %s\\scan-cache.tsv belongs to another exe; starting over (key %016llx)", cache_name,
                 (unsigned long long)key);
    } else {
        api->log("[core] scan cache: %zu address(es) for this exe (key %016llx), each checked against the pattern before use",
                 scan_cache.rva.size(), (unsigned long long)key);
    }
    scan_cache_on = true;
}

void save_scan_cache()
{
    AcquireSRWLockExclusive(&scan_lock);
    std::string text;
    size_t n = scan_cache.rva.size();
    if (scan_cache_on && scan_cache.dirty)
        text = scan_cache.serialize(), scan_cache.dirty = false;
    ReleaseSRWLockExclusive(&scan_lock);
    if (text.empty())
        return;
    std::wstring tmp = scan_cache_path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD w = 0;
    bool ok = f != INVALID_HANDLE_VALUE && WriteFile(f, text.data(), DWORD(text.size()), &w, nullptr) && w == text.size();
    if (f != INVALID_HANDLE_VALUE)
        CloseHandle(f);
    ok = ok && MoveFileExW(tmp.c_str(), scan_cache_path.c_str(), MOVEFILE_REPLACE_EXISTING);
    if (!ok)
        DeleteFileW(tmp.c_str());
    if (ok)
        api->log("[core] scan cache: %zu address(es) saved to %s\\scan-cache.tsv", n, cache_name);
    else
        api->log("[core] scan cache: writing %s\\scan-cache.tsv failed (%zu address(es) kept for this session)",
                 cache_name, n);
}

Binding *find_binding(const std::string &id)
{
    auto it = binding_index.find(id);
    return it == binding_index.end() ? nullptr : &bindings[it->second];
}

uintptr_t api_resolve(const char *id)
{
    std::lock_guard<std::recursive_mutex> g(resolve_lock);
    const char *name = id ? id : "(null)";
    Binding *b = find_binding(id ? id : "");
    if (!b) {
        if (current)
            api->log("[core] addon %s needs %s, which %s does not list: this loader is older than the addon, or %s is "
                     "not an exported binding",
                     current->name.c_str(), name, bindings_file.c_str(), name);
        else
            api->log("[core] no binding %s in %s", name, bindings_file.c_str());
        return 0;
    }
    if (!b->resolved) {
        bool was = b->deferred;
        attempt(*b, false);
        unreported |= was && !b->deferred;
    }
    if (b->deferred) {
        if (current) {
            current->hit_deferred = true;
            api->log("[core] addon %s needs %s, which waits for its code to decrypt (%s)", current->name.c_str(), name,
                     b->tr.reason.c_str());
        }
        return 0;
    }
    if (!b->addr && current)
        api->log("[core] addon %s needs %s, which does not resolve on this build", current->name.c_str(), name);
    return b->addr;
}

bool same_dll(const std::string &decl, const std::string &dll)
{
    auto slash = decl.find_last_of("/\\");
    std::string a = slash == std::string::npos ? decl : decl.substr(slash + 1);
    return a.size() == dll.size() && !_stricmp(a.c_str(), dll.c_str());
}

bool declared(const Addon &addon, const SwoftyMixin &m)
{
    for (auto &e : session["native"].a) {
        if (!same_dll(e["dll"].str(), addon.name))
            continue;
        for (auto &d : e["mixins"].a) {
            bool vt = SWOFTY_MIXIN_KIND(m.kind) == SWOFTY_MIXIN_VTABLE;
            if (d["target"].str() == m.target && d["kind"].str() == kind_name(m.kind) &&
                (!vt || d["slot"].num(-1) == SWOFTY_MIXIN_ARG(m.kind)) &&
                (d["priority"].type == json::Value::Null || d["priority"].num() == m.priority) &&
                d["overwrite"].b == bool(m.kind & SWOFTY_MIXIN_OVERWRITE))
                return true;
        }
    }
    return false;
}

int api_register(const SwoftyMixin *m, size_t n)
{
    if (!current) {
        api->log("[core] register_mixins outside SwoftyAddonInit; refused");
        return 0;
    }
    std::vector<Mixin> add;
    for (size_t i = 0; i < n; i++) {
        uint32_t k = SWOFTY_MIXIN_KIND(m[i].kind);
        bool on_load = k == SWOFTY_MIXIN_ON_LOAD;
        if (k < SWOFTY_MIXIN_WRAP || k > SWOFTY_MIXIN_ON_LOAD || !m[i].handler || (!on_load && !m[i].target) ||
            (k == SWOFTY_MIXIN_PATCH && !SWOFTY_MIXIN_ARG(m[i].kind))) {
            api->log("[core] addon %s: mixin %zu is malformed (kind 0x%x); registration refused", current->name.c_str(), i,
                     m[i].kind);
            return 0;
        }
        if (!on_load && have_session && !declared(*current, m[i])) {
            api->log("[core] addon %s: %s on %s is not declared in session.json; registration refused",
                     current->name.c_str(), kind_name(k), m[i].target);
            return 0;
        }
        add.push_back({on_load ? "" : m[i].target, m[i].kind, m[i].priority, m[i].handler, m[i].orig});
    }
    current->mixins.insert(current->mixins.end(), add.begin(), add.end());
    api->log("[core] addon %s: registered %zu mixin(s)", current->name.c_str(), n);
    return 1;
}

void *api_fields(const void *obj, size_t size)
{
    struct Key {
        const void *obj;
        HMODULE mod;
        bool operator==(const Key &) const = default;
    };
    struct Hash {
        size_t operator()(const Key &k) const { return std::hash<const void *>()(k.obj) ^ (uintptr_t)k.mod; }
    };
    static std::unordered_map<Key, std::pair<size_t, void *>, Hash> table;
    static SRWLOCK lock = SRWLOCK_INIT;
    HMODULE mod = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(__builtin_return_address(0)), &mod);
    void *out = nullptr;
    size_t held = 0;
    AcquireSRWLockExclusive(&lock);
    auto it = table.find({obj, mod});
    if (!size) {
        if (it != table.end())
            free(it->second.second), table.erase(it);
    } else if (it == table.end()) {
        out = calloc(1, size);
        table[{obj, mod}] = {size, out};
    } else if (it->second.first == size) {
        out = it->second.second;
    } else {
        held = it->second.first;
    }
    ReleaseSRWLockExclusive(&lock);
    if (held)
        api->log("[core] fields(%p, %zu): this addon already keeps %zu bytes on that object", obj, size, held);
    return out;
}

void drop(size_t i, const char *fmt, const char *a = "", const char *b = "", const char *c = "")
{
    if (!addons[i].active)
        return;
    char why[512];
    snprintf(why, sizeof why, fmt, a, b, c);
    addons[i].active = false;
    api->log("[core] addon %s dropped: %s; none of its mixins install", addons[i].name.c_str(), why);
}

void check_targets(size_t i)
{
    addons[i].waiting = false;
    for (auto &m : addons[i].mixins) {
        uint32_t k = SWOFTY_MIXIN_KIND(m.kind), arg = SWOFTY_MIXIN_ARG(m.kind);
        if (k == SWOFTY_MIXIN_ON_LOAD)
            continue;
        Binding *b = find_binding(m.target);
        if (!b)
            return drop(i, "needs %s, which %s does not list", m.target.c_str(), bindings_file.c_str());
        m.addr = api_resolve(m.target.c_str());
        if (b->deferred) {
            addons[i].waiting = true;
            continue;
        }
        if (!m.addr)
            return drop(i, "needs %s, which does not resolve on this build", m.target.c_str());
        const std::string &bk = b->kind;
        bool ok = k == SWOFTY_MIXIN_WRAP     ? bk == "func"
                  : k == SWOFTY_MIXIN_VTABLE ? bk == "vtable"
                  : k == SWOFTY_MIXIN_REDIRECT ? bk == "call"
                                               : bk == "site" || bk == "call";
        if (!ok)
            return drop(i, "%s on %s needs another kind of binding, it is a %s", kind_name(k), m.target.c_str(), bk.c_str());
        if (k == SWOFTY_MIXIN_VTABLE && b->slots && arg >= (uint32_t)b->slots)
            return drop(i, "vtable slot out of range on %s", m.target.c_str());
        if (k == SWOFTY_MIXIN_VTABLE)
            m.addr += 8 * arg;
        if (k == SWOFTY_MIXIN_PATCH && arg > b->expect.size())
            return drop(i, "patch on %s is longer than the binding's expect bytes", m.target.c_str());
        if (k == SWOFTY_MIXIN_REDIRECT && byte_at(m.addr) != 0xE8)
            return drop(i, "redirect target %s is not a call rel32", m.target.c_str());
    }
}

std::vector<Group> plan(size_t from, bool late = false)
{
    std::vector<Group> groups;
    for (size_t i = from; i < addons.size(); i++) {
        auto &a = addons[i];
        if (!a.active || a.waiting || a.reinit || (late && !a.late))
            continue;
        for (auto &m : addons[i].mixins) {
            uint32_t k = SWOFTY_MIXIN_KIND(m.kind);
            if (k == SWOFTY_MIXIN_ON_LOAD)
                continue;
            bool chains = k == SWOFTY_MIXIN_WRAP || k == SWOFTY_MIXIN_SITE || k == SWOFTY_MIXIN_VTABLE;
            auto g = std::find_if(groups.begin(), groups.end(),
                                  [&](const Group &x) { return chains && x.kind == k && x.addr == m.addr; });
            if (g == groups.end()) {
                size_t span = k == SWOFTY_MIXIN_VTABLE     ? 8
                              : k == SWOFTY_MIXIN_REDIRECT ? 5
                              : k == SWOFTY_MIXIN_PATCH    ? SWOFTY_MIXIN_ARG(m.kind)
                                                           : insn_span(m.addr, 5);
                groups.push_back({k, m.addr, span, {}});
                g = groups.end() - 1;
            }
            g->handlers.push_back({i, &m});
        }
    }
    for (auto &g : groups)
        std::stable_sort(g.handlers.begin(), g.handlers.end(),
                         [](const Handler &a, const Handler &b) { return a.m->priority > b.m->priority; });
    std::stable_sort(groups.begin(), groups.end(), [](const Group &a, const Group &b) { return a.kind < b.kind; });
    return groups;
}

std::string owners(const Group &g)
{
    std::string s;
    for (auto &h : g.handlers)
        s += (s.empty() ? "" : " > ") + addons[h.addon].name + "(" + std::to_string(h.m->priority) +
             (h.m->kind & SWOFTY_MIXIN_OVERWRITE ? ", overwrite" : "") + ")";
    return s;
}

bool core_only(const Group &g)
{
    for (auto &h : g.handlers)
        if (!addons[h.addon].core)
            return false;
    return true;
}

bool drop_conflicts(const std::vector<Group> &groups, bool warn)
{
    std::vector<size_t> bad;
    for (size_t x = 0; x < groups.size(); x++) {
        auto &g = groups[x];
        auto &target = g.handlers[0].m->target;
        std::string over;
        for (auto &h : g.handlers)
            if (h.m->kind & SWOFTY_MIXIN_OVERWRITE)
                over += (over.empty() ? "" : ", ") + addons[h.addon].name, bad.push_back(h.addon);
        if (std::count(over.begin(), over.end(), ',')) {
            api->log("[core] conflict: two overwrites on %s: %s", target.c_str(), over.c_str());
        } else if (!over.empty()) {
            bad.pop_back();
            if (warn && !(g.handlers.back().m->kind & SWOFTY_MIXIN_OVERWRITE))
                api->log("[core] warning: an overwrite on %s hides the handlers below it: %s", target.c_str(),
                         owners(g).c_str());
        }
        for (size_t y = x + 1; y < groups.size(); y++) {
            auto &o = groups[y];
            bool data = g.kind == SWOFTY_MIXIN_VTABLE, odata = o.kind == SWOFTY_MIXIN_VTABLE;
            if (data != odata || g.addr >= o.addr + o.span || o.addr >= g.addr + g.span)
                continue;
            api->log("[core] conflict: %s %s at 0x%llx overlaps %s %s at 0x%llx: %s vs %s", kind_name(g.kind),
                     target.c_str(), (unsigned long long)g.addr, kind_name(o.kind), o.handlers[0].m->target.c_str(),
                     (unsigned long long)o.addr, owners(g).c_str(), owners(o).c_str());
            bool gc = core_only(g), oc = core_only(o);
            for (auto *z : {&g, &o})
                if (gc == oc || core_only(*z))
                    for (auto &h : z->handlers)
                        bad.push_back(h.addon);
        }
        for (auto &h : held) {
            bool data = g.kind == SWOFTY_MIXIN_VTABLE, hdata = h.kind == SWOFTY_MIXIN_VTABLE;
            if (data != hdata || g.addr >= h.addr + h.span || h.addr >= g.addr + g.span)
                continue;
            api->log("[core] conflict: %s %s at 0x%llx overlaps a %s installed at the early stage: %s", kind_name(g.kind),
                     target.c_str(), (unsigned long long)g.addr, kind_name(h.kind), owners(g).c_str());
            for (auto &x : g.handlers)
                bad.push_back(x.addon);
        }
    }
    for (size_t i : bad)
        drop(i, "it conflicts with another addon (see the conflict line above)");
    return !bad.empty();
}

bool write_bytes(uintptr_t at, const void *data, size_t n)
{
    DWORD old;
    auto p = reinterpret_cast<void *>(at);
    if (!VirtualProtect(p, n, in_exec(at) ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE, &old))
        return false;
    auto b = reinterpret_cast<const uint8_t *>(p);
    written.push_back({at, std::vector<uint8_t>(b, b + n)});
    memcpy(p, data, n);
    VirtualProtect(p, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, n);
    return true;
}

void uninstall_all()
{
    for (size_t i = written.size(); i-- > mark.written;) {
        auto &w = written[i];
        DWORD old;
        auto p = reinterpret_cast<void *>(w.first);
        VirtualProtect(p, w.second.size(), PAGE_EXECUTE_READWRITE, &old);
        memcpy(p, w.second.data(), w.second.size());
        VirtualProtect(p, w.second.size(), old, &old);
        FlushInstructionCache(GetCurrentProcess(), p, w.second.size());
    }
    written.resize(mark.written);
    stubs.erase(stubs.begin() + mark.stubs, stubs.end());
    mid_hooks.erase(mid_hooks.begin() + mark.mids, mid_hooks.end());
    inline_hooks.erase(inline_hooks.begin() + mark.inlines, inline_hooks.end());
    inline_names.erase(inline_names.begin() + mark.inlines, inline_names.end());
    for (size_t i = mark.sites; i < MAX_SITES; i++)
        site_chains[i].clear();
    sites_used = mark.sites;
}

void chain(const Group &g, void *last)
{
    for (size_t i = 0; i < g.handlers.size(); i++)
        if (g.handlers[i].m->orig)
            *g.handlers[i].m->orig = i + 1 < g.handlers.size() ? g.handlers[i + 1].m->handler : last;
}

/* Hooks reach their trampolines with a 5-byte jump, so the trampolines must sit within 2 GB of the code.
   The loader reserves a pool next to the image at attach (SwoftyLoaderHookPool); the core commits it and
   hands it to safetyhook's global allocator, which serves every later trampoline and stub from it. On a
   build whose core loads at the window stage the game has used the address space around the image by
   then, and every site and short wrap failed with no memory in range. */
std::shared_ptr<safetyhook::Allocator> hook_allocator;
uintptr_t hook_pool;

void adopt_hook_pool(void (*loader_fn)())
{
    hook_allocator = safetyhook::Allocator::global();
    HMODULE loader = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(loader_fn), &loader);
    auto take = loader ? reinterpret_cast<int (*)(uintptr_t *, size_t *)>(
                             reinterpret_cast<void *>(GetProcAddress(loader, "SwoftyLoaderHookPool")))
                       : nullptr;
    uintptr_t at = 0;
    size_t size = 0;
    if (!take || !take(&at, &size)) {
        api->log("[core] hook pool: %s; trampolines come from safetyhook's own search near each hook",
                 take ? "the loader reserved none at attach" : "this loader reserves none");
        return;
    }
    if (!VirtualAlloc(reinterpret_cast<void *>(at), size, MEM_COMMIT, PAGE_EXECUTE_READWRITE)) {
        api->log("[core] hook pool: committing %zu KB at 0x%llx failed, error %lu; trampolines come from "
                 "safetyhook's own search near each hook", size >> 10, (unsigned long long)at, GetLastError());
        VirtualFree(reinterpret_cast<void *>(at), 0, MEM_RELEASE);
        return;
    }
    hook_allocator->adopt(reinterpret_cast<uint8_t *>(at), size);
    hook_pool = at;
    long long off = (long long)(at - base);
    api->log("[core] hook pool: %zu KB at 0x%llx (base%s0x%llx), reserved at loader attach; hook trampolines and "
             "stubs come from it", size >> 10, (unsigned long long)at, off < 0 ? "-" : "+",
             (unsigned long long)(off < 0 ? -off : off));
}

std::string target_bytes(uintptr_t at)
{
    std::string s;
    char b[4];
    for (int i = 0; i < 16; i++) {
        snprintf(b, sizeof b, i ? " %02X" : "%02X", reinterpret_cast<const uint8_t *>(at)[i]);
        s += b;
    }
    return s;
}

std::string where(const uint8_t *ip)
{
    char b[48];
    uintptr_t a = reinterpret_cast<uintptr_t>(ip);
    if (a >= base)
        snprintf(b, sizeof b, "base+0x%llx", rva(a));
    else
        snprintf(b, sizeof b, "0x%llx", (unsigned long long)a);
    return b;
}

const char *alloc_error(safetyhook::Allocator::Error e)
{
    return e == safetyhook::Allocator::Error::NO_MEMORY_IN_RANGE ? "no free memory within 2 GB of the target"
                                                                  : "VirtualAlloc failed";
}

void dump_free_regions_near(uintptr_t target)
{
    static bool dumped;
    if (dumped)
        return;
    dumped = true;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t lo = target > 0x80000000ULL ? target - 0x80000000ULL : (uintptr_t)si.lpMinimumApplicationAddress;
    uintptr_t hi = target + 0x80000000ULL;
    int regions = 0, slots = 0, shown = 0;
    unsigned long long biggest = 0;
    for (uintptr_t p = lo & ~(uintptr_t)0xFFF; p < hi;) {
        MEMORY_BASIC_INFORMATION m;
        if (!VirtualQuery(reinterpret_cast<void *>(p), &m, sizeof m)) {
            api->log("[core]   VirtualQuery failed at 0x%llx, error %lu", (unsigned long long)p, GetLastError());
            break;
        }
        uintptr_t next = (uintptr_t)m.BaseAddress + m.RegionSize;
        if (m.State == MEM_FREE) {
            uintptr_t first = (p + 0xFFFF) & ~(uintptr_t)0xFFFF;
            int n = first < next ? (int)((next - first) >> 16) : 0;
            regions++;
            slots += n;
            biggest = m.RegionSize > biggest ? m.RegionSize : biggest;
            if (shown < 16) {
                api->log("[core]   free 0x%llx..0x%llx (%llu KB, %d whole 64 KB slot(s))", (unsigned long long)p,
                         (unsigned long long)next, (unsigned long long)(m.RegionSize >> 10), n);
                shown++;
            }
        }
        p = next > p ? next : p + si.dwPageSize;
    }
    api->log("[core]   within 2 GB of 0x%llx: %d free region(s), %d whole 64 KB slot(s), largest %llu KB; hook pool %s",
             (unsigned long long)target, regions, slots, biggest >> 10, hook_pool ? "adopted" : "none");
}

} // namespace

namespace runtime {

std::string hook_error(const safetyhook::InlineHook::Error &e)
{
    using E = safetyhook::InlineHook::Error;
    switch (e.type) {
    case E::BAD_ALLOCATION:
        return std::string("the trampoline did not allocate: ") + alloc_error(e.allocator_error);
    case E::FAILED_TO_DECODE_INSTRUCTION:
        return "the instruction at " + where(e.ip) + " does not decode";
    case E::SHORT_JUMP_IN_TRAMPOLINE:
        return "a short jump at " + where(e.ip) + " sits in the relocated bytes";
    case E::IP_RELATIVE_INSTRUCTION_OUT_OF_RANGE:
        return "the ip-relative instruction at " + where(e.ip) + " cannot be relocated that far";
    case E::UNSUPPORTED_INSTRUCTION_IN_TRAMPOLINE:
        return "the instruction at " + where(e.ip) + " cannot be relocated";
    case E::FAILED_TO_UNPROTECT:
        return "the page at " + where(e.ip) + " did not unprotect";
    case E::NOT_ENOUGH_SPACE:
        return "not enough room for the jump at " + where(e.ip);
    }
    return "unknown InlineHook error " + std::to_string((int)e.type);
}

std::string hook_error(const safetyhook::MidHook::Error &e)
{
    if (e.type == safetyhook::MidHook::Error::BAD_ALLOCATION)
        return std::string("the stub did not allocate: ") + alloc_error(e.allocator_error);
    return hook_error(e.inline_hook_error);
}

} // namespace runtime

namespace {

std::string install_error;

const char *failed(std::string why, const Group &g, bool allocation)
{
    install_error = std::move(why) + "; bytes at the target: " + target_bytes(g.addr);
    if (allocation)
        dump_free_regions_near(g.addr);
    return install_error.c_str();
}

const char *install(const Group &g)
{
    void *first = g.handlers[0].m->handler;
    if (packed && g.kind != SWOFTY_MIXIN_VTABLE && page_encrypted(g.addr))
        return "the code there still reads encrypted";
    switch (g.kind) {
    case SWOFTY_MIXIN_WRAP: {
        auto h = safetyhook::InlineHook::create(reinterpret_cast<void *>(g.addr), first, safetyhook::InlineHook::StartDisabled);
        if (!h)
            return failed(runtime::hook_error(h.error()), g, h.error().type == safetyhook::InlineHook::Error::BAD_ALLOCATION);
        chain(g, h->original<void *>());
        if (auto on = h->enable(); !on)
            return failed("enabling it failed: " + runtime::hook_error(on.error()), g, false);
        inline_hooks.push_back(std::move(*h));
        inline_names.push_back(g.handlers[0].m->target);
        return nullptr;
    }
    case SWOFTY_MIXIN_SITE: {
        if (sites_used == MAX_SITES)
            return "too many sites";
        size_t s = sites_used++;
        for (auto &h : g.handlers)
            site_chains[s].push_back({reinterpret_cast<void (*)(SwoftyContext *)>(h.m->handler),
                                      perf::probe(perf::owner_of(h.m->handler), "site " + h.m->target)});
        auto h = safetyhook::MidHook::create(reinterpret_cast<void *>(g.addr), site_thunks[s]);
        if (!h) {
            auto &e = h.error();
            bool alloc = e.type == safetyhook::MidHook::Error::BAD_ALLOCATION ||
                         e.inline_hook_error.type == safetyhook::InlineHook::Error::BAD_ALLOCATION;
            return failed(runtime::hook_error(e), g, alloc);
        }
        mid_hooks.push_back(std::move(*h));
        return nullptr;
    }
    case SWOFTY_MIXIN_VTABLE:
        chain(g, *reinterpret_cast<void **>(g.addr));
        return write_bytes(g.addr, &first, sizeof first) ? nullptr : "VirtualProtect failed";
    case SWOFTY_MIXIN_REDIRECT: {
        auto stub = safetyhook::Allocator::global()->allocate_near({reinterpret_cast<uint8_t *>(g.addr)}, 14);
        if (!stub) {
            install_error = std::string("the jump stub did not allocate: ") + alloc_error(stub.error());
            dump_free_regions_near(g.addr);
            return install_error.c_str();
        }
        uint8_t jmp[14] = {0xFF, 0x25};
        memcpy(jmp + 6, &first, 8);
        memcpy(stub->data(), jmp, sizeof jmp);
        chain(g, reinterpret_cast<void *>(g.addr + 5 + *reinterpret_cast<const int32_t *>(g.addr + 1)));
        int32_t rel = (int32_t)(stub->address() - (g.addr + 5));
        stubs.push_back(std::move(*stub));
        return write_bytes(g.addr + 1, &rel, 4) ? nullptr : "VirtualProtect failed";
    }
    case SWOFTY_MIXIN_PATCH: {
        auto *b = find_binding(g.handlers[0].m->target);
        if (memcmp(reinterpret_cast<const void *>(g.addr), b->expect.data(), b->expect.size()))
            return "bytes changed since resolve";
        return write_bytes(g.addr, first, g.span) ? nullptr : "VirtualProtect failed";
    }
    }
    return "unknown kind";
}

bool install_all(const std::vector<Group> &groups)
{
    for (auto &g : groups) {
        const char *err = install(g);
        auto &target = g.handlers[0].m->target;
        if (err) {
            api->log("[core] %s on %s at 0x%llx: %s", kind_name(g.kind), target.c_str(), (unsigned long long)g.addr, err);
            uninstall_all();
            for (auto &h : g.handlers)
                drop(h.addon, "installing its %s on %s failed", kind_name(g.kind), target.c_str());
            return false;
        }
        api->log("[core] %s %s at 0x%llx (base+0x%llx) installed: %s", kind_name(g.kind), target.c_str(),
                 (unsigned long long)g.addr, rva(g.addr), owners(g).c_str());
    }
    return true;
}

void add_core_sets(std::vector<runtime::CoreMixins> sets)
{
    for (auto &c : sets) {
        Addon own{c.name, {}, true, true};
        for (auto &m : c.mixins)
            own.mixins.push_back({m.target, m.kind, m.priority, m.handler, m.orig});
        api->log("[core] core mixin set '%s': %zu mixin(s), installed or dropped on its own", c.name, own.mixins.size());
        addons.push_back(std::move(own));
    }
}

size_t offer_core_sets()
{
    size_t before = addons.size();
    for (auto *sets : {runtime::career_content_mixins, runtime::characters_mixins, runtime::wizard_mixins,
                       runtime::tile_route_mixins, runtime::era_mixins, runtime::car_category_mixins,
                       runtime::save_splice_mixins, runtime::ers_newlap_mixins, runtime::track_acclim_mixins})
        add_core_sets(sets());
    return addons.size() - before;
}

void settle_core_sets()
{
    runtime::tile_route_commit();
    runtime::career_content_commit();
    runtime::characters_commit();
    runtime::wizard_commit();
    runtime::era_commit();
    runtime::car_category_commit();
    runtime::save_splice_commit();
    runtime::ers_newlap_commit();
    runtime::track_acclim_commit();
}

void install_groups(size_t from, bool late, size_t *count)
{
    std::vector<Group> groups;
    do
        groups = plan(from, late);
    while (drop_conflicts(groups, false) || !install_all(groups));
    drop_conflicts(groups, true);
    for (auto &g : groups) {
        count[SWOFTY_MIXIN_KIND(g.kind)]++;
        held.push_back({g.kind, g.addr, g.span});
    }
    mark = {written.size(), stubs.size(), mid_hooks.size(), inline_hooks.size(), sites_used};
}

void note_waiting(size_t i)
{
    if (addons[i].active && addons[i].waiting)
        api->log("[core] addon %s waits: a mixin target sits in code that still reads encrypted; it installs once that "
                 "code decrypts",
                 addons[i].name.c_str());
}

void install_pass(size_t from, size_t *count)
{
    settled = addons.size();
    for (size_t i = from; i < settled; i++)
        if (addons[i].active && !addons[i].reinit)
            check_targets(i), note_waiting(i);
    install_groups(from, false, count);
}

void run_on_load(size_t i)
{
    if (!addons[i].active || addons[i].waiting || addons[i].reinit)
        return;
    for (auto &x : addons[i].mixins)
        if (SWOFTY_MIXIN_KIND(x.kind) == SWOFTY_MIXIN_ON_LOAD)
            reinterpret_cast<void (*)()>(x.handler)();
}

bool waits(bool core)
{
    for (auto &a : addons)
        if (a.active && (a.waiting || a.reinit) && (!core || a.core))
            return true;
    return false;
}

size_t deferred_count()
{
    size_t n = 0;
    for (auto &b : bindings)
        n += b.deferred;
    return n;
}

void drop_module(HMODULE m)
{
    runtime::events_drop_module(m);
    runtime::career_start_drop_module(m);
}

/* An SDK addon's init fails when a binding it wants is deferred, so it is called again once code decrypts;
   it registered nothing on the failed call. */
void reinit_pass(bool final)
{
    for (size_t i = 0; i < addons.size(); i++) {
        if (!addons[i].reinit || !addons[i].active)
            continue;
        addons[i].hit_deferred = false;
        addons[i].mixins.clear();
        current = &addons[i];
        int r = addons[i].reinit(api);
        current = nullptr;
        auto &a = addons[i];
        if (r) {
            a.reinit = nullptr, a.waiting = true;
            api->log("[core] addon %s: SwoftyAddonInit ran again now that its code decrypted and returned 1", a.name.c_str());
            continue;
        }
        a.mixins.clear();
        drop_module(a.module);
        if (!a.hit_deferred || final) {
            a.reinit = nullptr;
            drop(i, "SwoftyAddonInit returned 0 once its code %s", a.hit_deferred ? "still read encrypted at the last retry" : "decrypted");
        }
    }
}

void late_install(size_t *count)
{
    size_t from = settled;
    settled = addons.size();
    bool any = false;
    for (size_t i = 0; i < settled; i++) {
        auto &a = addons[i];
        if (!a.active || a.reinit || !(a.waiting || i >= from))
            continue;
        check_targets(i);
        if (a.active && !a.waiting)
            a.late = any = true;
    }
    if (any)
        install_groups(0, true, count);
    for (size_t i = 0; i < addons.size(); i++)
        if (addons[i].late)
            run_on_load(i), addons[i].late = false;
}

void settle_core_sets();
size_t offer_core_sets();

/* Core and addon scans name Steam's .xtls; the set's section map sends it to this build's code section. */
std::string game_code()
{
    auto it = section_alias.find(".xtls");
    if (it != section_alias.end())
        return it->second;
    return section_by_name(".xtls") ? ".xtls" : section_name(code_section());
}

bool code_decrypted(double &e)
{
    e = section_now(game_code());
    return e >= 0 && e <= PLAIN;
}

/* Setups that scan or hook game code without bindings wait for the whole code section, not a page, to read
   decrypted: a setup that ran on half-decrypted code would give up for good on what is still encrypted. */
size_t run_code_tasks(const char *pass, bool final)
{
    double e;
    if (code_tasks.empty() || (!code_decrypted(e) && !final))
        return 0;
    bool ok = e >= 0 && e <= PLAIN;
    auto tasks = std::move(code_tasks);
    code_tasks.clear();
    for (auto &t : tasks) {
        if (!ok) {
            api->log("[core] %s: not set up; %s still reads %s (entropy %.2f) at the last retry, so it fails closed",
                     t.what.c_str(), game_code().c_str(), reads(e), e < 0 ? 0 : e);
            continue;
        }
        api->log("[core] %s: %s reads decrypted (entropy %.2f) at the %s pass; setting it up now", t.what.c_str(),
                 game_code().c_str(), e, pass);
        t.fn();
        runtime::stage_catch_up();
    }
    return tasks.size();
}

void late_pass(const char *pass, bool final)
{
    std::lock_guard<std::recursive_mutex> c(commit_lock);
    std::lock_guard<std::recursive_mutex> r(resolve_lock);
    size_t before = deferred_count();
    if (!before && !waits(false) && code_tasks.empty()) {
        if (unreported)
            report(pass);
        return;
    }
    section_reads.clear();
    std::vector<std::string> seen;
    std::string sampled;
    for (auto &b : bindings)
        if (b.deferred && std::find(seen.begin(), seen.end(), b.section) == seen.end()) {
            seen.push_back(b.section);
            double e = section_now(b.section);
            char one[96];
            snprintf(one, sizeof one, "%s%s entropy %.2f (%s)", sampled.empty() ? "" : ", ", b.section.c_str(),
                     e < 0 ? 0 : e, reads(e));
            sampled += one;
        }
    for (auto &b : bindings)
        if (b.deferred)
            attempt(b, final);
    reinit_pass(final);
    offer_core_sets();
    size_t count[7] = {};
    late_install(count);
    if (!waits(true)) {
        runtime::career_setup();
        settle_core_sets();
    }
    size_t setups = run_code_tasks(pass, final);
    if (!waits(false))
        closed = true;
    size_t after = deferred_count(), installed = 0;
    for (size_t k : count)
        installed += k;
    if (after == before && !final && !installed && !setups && strcmp(pass, "window")) {
        api->log("[core] %s pass: %zu binding(s) and %zu setup(s) still wait for their code to decrypt (%s)", pass, after,
                 code_tasks.size(), sampled.c_str());
        return;
    }
    api->log("[core] %s pass: %s; %zu binding(s) came out of deferral, %zu still deferred; installed %zu wrap(s), %zu site(s), "
             "%zu vtable slot(s), %zu redirect(s), %zu patch(es)%s",
             pass, sampled.c_str(), before - after, after, count[SWOFTY_MIXIN_WRAP], count[SWOFTY_MIXIN_SITE],
             count[SWOFTY_MIXIN_VTABLE], count[SWOFTY_MIXIN_REDIRECT], count[SWOFTY_MIXIN_PATCH],
             waits(false) ? "; some addons still wait" : "");
    report(pass);
    save_scan_cache();
}

bool pending()
{
    std::lock_guard<std::recursive_mutex> c(commit_lock);
    return deferred_count() || waits(false) || !code_tasks.empty();
}

DWORD WINAPI retry_worker(LPVOID)
{
    for (int n = 1; n <= RETRIES && pending(); n++) {
        Sleep(RETRY_MS);
        char pass[16];
        snprintf(pass, sizeof pass, "stage:%d", n);
        late_pass(pass, n == RETRIES);
    }
    return 0;
}

void start_retries()
{
    if (InterlockedExchange(&retries_started, 1))
        return;
    api->log("[core] bindings: %zu binding(s) and %zu setup(s) still wait for their code to decrypt; retrying every %lu "
             "ms, %d time(s), then they fail closed",
             deferred_count(), code_tasks.size(), (unsigned long)RETRY_MS, RETRIES);
    if (HANDLE t = CreateThread(nullptr, 0, retry_worker, nullptr, 0, nullptr))
        CloseHandle(t);
    else
        late_pass("stage:1", true);
}

void bind_stage(int stage)
{
    if (stage != SWOFTY_STAGE_WINDOW)
        return;
    late_pass("window", false);
    if (pending())
        start_retries();
}

}

namespace runtime {

/* Core setups that scan or hook game code without a binding run now, or on a packed build whose code still
   reads encrypted, at the first stage pass that reads it decrypted (the retries bound it like bindings). */
void when_code_ready(const char *what, void (*fn)())
{
    std::lock_guard<std::recursive_mutex> c(commit_lock);
    {
        std::lock_guard<std::recursive_mutex> r(resolve_lock);
        double e = 0;
        if (packed && !code_decrypted(e)) {
            code_tasks.push_back({what, fn});
            api->log("[core] %s waits: %s reads %s (entropy %.2f) on this packed build; it is set up once that code "
                     "decrypts",
                     what, game_code().c_str(), reads(e), e < 0 ? 0 : e);
            return;
        }
    }
    fn();
    runtime::stage_catch_up();
}

/* Lazy lookups ask this before caching a miss, so a miss on code that still reads encrypted is tried again. */
bool code_ready()
{
    if (!packed || code_plain.load(std::memory_order_acquire))
        return true;
    std::lock_guard<std::recursive_mutex> r(resolve_lock);
    double e;
    if (code_decrypted(e))
        code_plain.store(true, std::memory_order_release);
    return code_plain.load(std::memory_order_acquire);
}

bool core_mixins_active(const char *name)
{
    for (auto &a : addons)
        if (a.name == name)
            return a.active && !a.waiting && !a.reinit && !a.mixins.empty();
    return false;
}

bool core_mixins_closed()
{
    return closed;
}

}

std::string core_code_owner(uintptr_t addr)
{
    for (auto &m : addons)
        for (auto &x : m.mixins)
            if (reinterpret_cast<uintptr_t>(x.handler) == addr && SWOFTY_MIXIN_KIND(x.kind) != SWOFTY_MIXIN_PATCH)
                return m.name + " " + kind_name(x.kind) + (x.target.empty() ? "" : " " + x.target) + " handler";
    for (size_t i = 0; i < inline_hooks.size(); i++)
        if (addr - inline_hooks[i].trampoline().address() < inline_hooks[i].trampoline().size())
            return "trampoline of the wrap on " + inline_names[i];
    return {};
}

extern "C" __declspec(dllexport) int SwoftyCoreAttach(SwoftyApi *a)
{
    api = a;
    base = a->module_base();
    adopt_hook_pool(reinterpret_cast<void (*)()>(a->module_base));
    wchar_t path[MAX_PATH] = {};
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&SwoftyCoreAttach), &self);
    GetModuleFileNameW(self, path, MAX_PATH);
    std::wstring dir(path);
    dir.resize(dir.find_last_of(L'\\') + 1);
    crash_attach(a, dir);
    settings_attach(a, dir);
    perf::attach();
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + reinterpret_cast<const IMAGE_DOS_HEADER *>(base)->e_lfanew);
    long long running_stamp = (long long)nt->FileHeader.TimeDateStamp;
    std::string chosen = "bindings.json";
    json::Value doc;
    if (!json::parse(select_bindings(dir, running_stamp, chosen), doc))
        api->log("[core] %ls%s missing or unreadable; no binding will resolve", dir.c_str(), chosen.c_str());
    bindings_file = chosen;
    for (auto &j : doc["bindings"].a) {
        Binding b{j["id"].str(), j["kind"].str(), j["status"].str(), j["section"].str(), j["pattern"].str(),
                  j["resolve"]["kind"].str("addr"), hex_bytes(j["expect"].str()), j["anchor"].num(),
                  j["resolve"]["insn"].num(), j["resolve"]["len"].num(), j["rva"].num(-1), j["slots"].num(),
                  j["match"].num(-1)};
        const auto &sa = j["string_anchor"];
        if (sa.type == json::Value::Obj) {
            b.sa_string = sa["string"].str();
            b.sa_func_rva = sa["func_rva"].num(-1);
            b.sa_func_off = sa["func_offset"].num();
            b.sa_durable = sa["durable"].b;
        }
        if (b.status == "unresolvable")
            b.unresolvable = j["reason"].str("the set gives no reason");
        binding_index[b.id] = bindings.size();
        bindings.push_back(std::move(b));
    }
    same_build = doc["build"]["pe_timestamp"].num() == running_stamp;
    size_t anchored = 0, durable = 0, placed = 0, legacy = 0, unresolvable = 0;
    for (auto &b : bindings) {
        if (!b.sa_string.empty())
            anchored++, durable += b.sa_durable;
        placed += b.match >= 0 && !b.pattern.empty();
        legacy += b.pattern.empty();
        unresolvable += !b.unresolvable.empty();
    }
    std::string dist = doc["build"]["distribution"].str();
    for (auto &c : dist)
        c = char(tolower((unsigned char)c));
    build_tag = dist == "steam" ? "steam" : dist == "ea app" || dist == "ea" ? "ea" : "unknown";
    read_section_map(doc, chosen);
    api->log("[core] %s: %zu binding(s) for build %.16s, %s; %zu string-anchored (%zu build-agnostic), pattern first",
             chosen.c_str(), bindings.size(), doc["build"]["sha256"].str("?").c_str(),
             same_build ? "this exe (rva checked)" : "a different exe (patterns are scanned)", anchored, durable);
    if (legacy)
        api->log("[core] %s: %zu binding(s) carry no pattern (legacy set format); they resolve by string anchor only",
                 chosen.c_str(), legacy);
    if (unresolvable)
        api->log("[core] %s: %zu binding(s) are marked unresolvable on this build and are skipped", chosen.c_str(),
                 unresolvable);
    long long kb_size = doc["build"]["size"].num(-1), exe_size = -1;
    wchar_t exe[MAX_PATH] = {};
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) && GetFileAttributesExW(exe, GetFileExInfoStandard, &fa))
        exe_size = (long long)(((unsigned long long)fa.nFileSizeHigh << 32) | fa.nFileSizeLow);
    exe_stamp = running_stamp, exe_bytes = exe_size;
    open_scan_cache(a, dir);
    if (kb_first && same_build && kb_size > 0 && exe_size != kb_size) {
        kb_first = false;
        api->log("[core] binding resolution: the exe is %lld bytes and the KB build %lld; the KB addresses are not used, "
                 "every pattern is scanned", exe_size, kb_size);
    }
    if (kb_first && same_build)
        api->log("[core] binding resolution: %zu of %zu pattern(s) carry their KB address; each is checked there first and "
                 "scanned only when the bytes differ", placed, bindings.size());
    std::wstring session_path = dir + L"..\\..\\session.json";
    std::string text = read_file(session_path);
    have_session = !text.empty();
    if (have_session && !json::parse(text, session)) {
        api->log("[core] session.json unreadable; every mixin registration is refused");
        session = {};
    }
    api->log(have_session ? "[core] session.json: mixins not declared there are refused"
                          : "[core] no session.json; mixins are not checked against a manifest");
    a->resolve = api_resolve;
    a->register_mixins = api_register;
    a->fields = api_fields;
    a->setting = settings_get;
    a->watch_setting = settings_watch;
    a->assets = runtime::assets_table();
    a->events = runtime::events_table();
    a->overlay = runtime::overlay_table();
    a->telemetry = runtime::telemetry_table();
    a->db = runtime::db_table();
    a->storage = runtime::storage_table();
    a->career = runtime::career_table();
    a->popup = runtime::popup_table();
    a->lang = runtime::lang_table();
    a->career_content = runtime::career_content_table();
    a->characters = runtime::characters_table();
    a->career_wizard = runtime::wizard_table();
    a->era = runtime::era_table();
    a->version = SWOFTY_API_VERSION;
    add_core_sets(runtime::career_mixins());
    exe_sha = exe_sha256(exe, caches(dir) + L"exe-sha256.tsv");
    const auto &said = doc["build"]["packed"];
    auto cs = section_by_name(game_code());
    double ce = cs ? section_entropy(cs) : -1;
    packed = said.type == json::Value::Bool ? said.b : ce > ENCRYPTED;
    char cname[9] = {};
    if (cs)
        memcpy(cname, cs->Name, 8);
    api->log("[core] build %s: %s (%s); code section %s entropy %.2f, reads %s", build_tag.c_str(),
             packed ? "packed" : "not packed", said.type == json::Value::Bool ? "the set says so" : "measured at init",
             cname, ce < 0 ? 0 : ce, reads(ce));
    {
        std::lock_guard<std::recursive_mutex> g(resolve_lock);
        for (auto &b : bindings)
            if (!b.resolved && !b.deferred)
                attempt(b, false);
        report("init");
    }
    save_scan_cache();
    runtime::when_code_ready("crash: Ne_ReportMemoryError hook", crash_game_hooks);
    if (packed)
        runtime::on_stage(bind_stage);
    return 1;
}

extern "C" __declspec(dllexport) int SwoftyCoreInit(const wchar_t *name, SwoftyAddonInitFn init)
{
    std::lock_guard<std::recursive_mutex> c(commit_lock);
    addons.push_back({utf8(name), {}, true});
    current = &addons.back();
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(init), &current->module);
    int r = init(api);
    if (!r) {
        current->mixins.clear();
        drop_module(current->module);
        if (packed && current->hit_deferred) {
            current->reinit = init;
            api->log("[core] addon %s: its init needs code that still reads encrypted (a binding or a scan); "
                     "SwoftyAddonInit runs again once that code decrypts",
                     current->name.c_str());
            r = 1;
        } else {
            current->active = false;
        }
    }
    current = nullptr;
    return r;
}

extern "C" __declspec(dllexport) int SwoftyCoreCommit(void)
{
    std::lock_guard<std::recursive_mutex> c(commit_lock);
    offer_core_sets();
    size_t from = settled;
    size_t count[7] = {};
    install_pass(from, count);
    if (!waits(true))
        runtime::career_setup();
    if (!from)
        runtime::when_code_ready("popup: message system hook", runtime::popup_setup);
    if (!from)
        runtime::when_code_ready("mfd: development switch", runtime::mfd_setup);
    if (!waits(true))
        settle_core_sets();
    for (size_t i = from; i < settled; i++)
        run_on_load(i);
    if (offer_core_sets()) {
        api->log("[core] commit: installing the core mixin sets that registrations at load asked for");
        install_pass(settled, count);
    }
    if (!waits(true))
        settle_core_sets();
    closed = (from || runtime::load_stage() != "early") && !waits(false);
    if (!from)
        runtime::when_code_ready("runtime setup (assets, resources, gamedb, dbread, overlay, lang, events, hud, telemetry)",
                                 runtime::ready);
    size_t active = 0;
    for (size_t i = from; i < addons.size(); i++)
        active += addons[i].active;
    api->log("[core] commit: %zu of %zu mixin addon(s) active; %zu wrap(s), %zu site(s), %zu vtable slot(s), %zu "
             "redirect(s), %zu patch(es)",
             active, addons.size() - from, count[SWOFTY_MIXIN_WRAP], count[SWOFTY_MIXIN_SITE], count[SWOFTY_MIXIN_VTABLE],
             count[SWOFTY_MIXIN_REDIRECT], count[SWOFTY_MIXIN_PATCH]);
    save_scan_cache();
    if (pending() && runtime::load_stage() != "early")
        start_retries();
    return (int)active;
}
