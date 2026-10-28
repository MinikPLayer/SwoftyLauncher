#include "runtime.hpp"

#include "dbread_logic.hpp"
#include "perf.hpp"

#include <safetyhook.hpp>
#include <swofty/mixin.hpp>

#include <atomic>
#include <cerrno>
#include <climits>
#include <cmath>
#include <map>
#include <mutex>

namespace {

const runtime::Sig S_LOAD{"Gdb_Load_GameDatabase", "41 57 41 56 41 55 41 54 56 57 55 53 48 81 EC 28 02 00 00 48 89 D7 49 89 CC",
                          0x45a0350};
const runtime::Sig S_INSERT{"Gdb_HashMap_Insert",
                            "41 57 41 56 41 55 41 54 56 57 55 53 48 81 EC D8 00 00 00 C5 F8 29 B4 24 C0 00 00 00 4C 89 CE 44 88 44 24 37",
                            0x45a12d0};
const runtime::Sig S_LOOKUP{"Gdb_Lookup_Key", "56 57 55 53 49 B8 25 23 22 84 E4 9C F2 CB", 0x45a22f0};
const runtime::Sig S_GLOBAL{"g_Gdb_Database", "56 57 55 53 49 B8 25 23 22 84 E4 9C F2 CB 48 8B 15 ?? ?? ?? ?? 44 0F B6 11",
                            0x6cc4278, runtime::Sig::riprel, 0xe, 0x11, 7};

constexpr size_t DB_MAP = 0xa8, DB_LOADED = 0xf0, DB_ALLOC = 0x8;
constexpr const char *PROBE_PREFIX = "swofty.class_probe.t";
constexpr size_t PROBE_RECORDS = 32;

using Load_t = void *(void *db, void *cfg);
using Insert_t = void *(void *db, uint64_t key, uint8_t tag, char *text);
using Lookup_t = void **(const char *path);
using Dtor_t = void *(void *self, int flags);
using Free_t = void(void *alloc, void *p);

struct Edit {
    std::string path, value;
    uint64_t key = 0;
    int tag = -1;
    json::Value::Type kind = json::Value::Null;
    bool retype = false;
};

struct TagName {
    const char *name;
    int tag;
};

const TagName TAGS[] = {{"int", 0},  {"float", 1}, {"string", 2}, {"enum", 3},   {"id", 4},      {"bool", 5},
                        {"time", 6}, {"t7", 7},    {"t8", 8},     {"curve", 9},  {"colour", 11}, {"color", 11},
                        {"t12", 12}, {"list", 13}, {"vector", 14}, {"t17", 17}, {"t18", 18}};
const int INFER[] = {0, 1, 2, 5, 11, 12};
const uintptr_t KB_VT[19] = {0x5bee788, 0x5bee798, 0x5bee6b8, 0x5bee6b8, 0x5bee6b8, 0x56e6518, 0x5bee7a8,
                             0x5bee7b8, 0x5bee7c8, 0x5bee7d8, 0,         0x57a6730, 0x5bee7e8, 0x5bee6c8,
                             0x5bee7f8, 0,         0,         0x5bee6b8, 0x5bee6c8};

std::vector<Edit> edits;
Insert_t *insert_fn;
Lookup_t *lookup_fn;
void **g_db;
safetyhook::InlineHook h_load;
std::atomic<bool> done{false};
std::mutex edits_lock;
std::map<int, void *> class_vt;
uintptr_t base;

int tag_of(const json::Value &v)
{
    if (v.type == json::Value::Num)
        return int(v.num());
    std::string t = runtime::lower(v.str());
    for (auto &x : TAGS)
        if (t == x.name)
            return x.tag;
    return -2;
}

const char *tag_name(int tag)
{
    for (auto &x : TAGS)
        if (x.tag == tag)
            return x.name;
    return "?";
}

bool all_digits(const std::string &s, size_t from)
{
    if (from >= s.size())
        return false;
    for (size_t i = from; i < s.size(); i++)
        if (s[i] < '0' || s[i] > '9')
            return false;
    return true;
}

std::string check_text(const Edit &e, int tag)
{
    const std::string &t = e.value;
    // Gdb_HashMap_Insert parses empty text as 0 (atoi for tags 0 and 11, strtoull for 12), 0.0 (atof for 1) or
    // false (_stricmp against "true" for 5), the same record the stock load makes from it. 7,007 stock colours
    // hold ''.
    if (t.empty() && e.kind == json::Value::Str && (tag == 0 || tag == 1 || tag == 5 || tag == 11 || tag == 12))
        return {};
    switch (tag) {
    case 0:
    case 11: {
        size_t from = !t.empty() && (t[0] == '-' || t[0] == '+') ? 1 : 0;
        if (e.kind == json::Value::Bool || !all_digits(t, from))
            return "an int needs integer text (the game parses it with atoi), got '" + t + "'";
        errno = 0;
        long long v = strtoll(t.c_str(), nullptr, 10);
        if (errno == ERANGE || v < INT_MIN || v > INT_MAX)
            return "'" + t + "' is outside the 32-bit int range the game stores";
        return {};
    }
    case 12: {
        // The game parses t12 text with strtoull, which takes a sign: "-1" is 2^64 - 1.
        size_t from = !t.empty() && (t[0] == '-' || t[0] == '+') ? 1 : 0;
        if (e.kind == json::Value::Bool || !all_digits(t, from))
            return "a t12 value needs integer text (the game parses it with strtoull), got '" + t + "'";
        errno = 0;
        strtoull(t.c_str(), nullptr, 10);
        return errno == ERANGE ? "'" + t + "' is outside the unsigned 64-bit range" : std::string();
    }
    case 1: {
        if (e.kind == json::Value::Bool || t.empty())
            return "a float needs number text, got '" + t + "'";
        char *end = nullptr;
        double v = strtod(t.c_str(), &end);
        if (*end || !std::isfinite(v) || std::fabs(v) > 3.4028234663852886e38)
            return "'" + t + "' is not a finite 32-bit float";
        return {};
    }
    case 5:
        if (_stricmp(t.c_str(), "true") && _stricmp(t.c_str(), "false"))
            return "a bool needs true or false, got '" + t + "'";
        return {};
    case 2:
    case 3:
    case 4:
    case 17:
        if (e.kind != json::Value::Str)
            return "a string record needs a JSON string value";
        return {};
    default:
        if (e.kind != json::Value::Str)
            return std::string("a ") + tag_name(tag) + " record needs its game text as a JSON string";
        return {};
    }
}

bool read_edit(const json::Value &j)
{
    Edit e;
    e.path = j["key"].str();
    std::string h = j["hash"].str();
    e.key = !e.path.empty() ? runtime::fnv64(e.path) : h.empty() ? 0 : strtoull(h.c_str(), nullptr, 16);
    const json::Value &v = j["value"];
    e.kind = v.type;
    if (v.type == json::Value::Str || v.type == json::Value::Num)
        e.value = v.s;
    else if (v.type == json::Value::Bool)
        e.value = v.b ? "True" : "False";
    e.retype = j["retype"].type == json::Value::Bool && j["retype"].b;
    if (j["type"].type != json::Value::Null)
        e.tag = tag_of(j["type"]);
    if (!e.key || (v.type != json::Value::Str && v.type != json::Value::Num && v.type != json::Value::Bool) ||
        e.tag == -2 || e.tag > 18 || e.tag == 10 || e.tag == 15 || e.tag == 16) {
        swofty::log("[assets] gamedb: edit %s ignored: it needs \"key\" (or \"hash\"), a string, number or bool "
                    "\"value\" and a known \"type\" (tags 10, 15 and 16 store nothing)",
                    e.path.empty() ? h.c_str() : e.path.c_str());
        return false;
    }
    if (e.tag >= 0) {
        std::string bad = check_text(e, e.tag);
        if (!bad.empty()) {
            swofty::log("[assets] gamedb: edit %s ignored: %s", e.path.empty() ? h.c_str() : e.path.c_str(), bad.c_str());
            return false;
        }
    }
    edits.push_back(std::move(e));
    return true;
}

void read_config()
{
    for (auto &j : runtime::config()["db_edits"].a)
        read_edit(j);
    for (auto &j : runtime::sdk()["db_edits"].a)
        read_edit(j);
}

std::string label(const Edit &e)
{
    return e.path.empty() ? runtime::hex16(e.key) : e.path;
}

uint64_t *slot_of(uint8_t *db, uint64_t key)
{
    runtime::Robin map{db + DB_MAP};
    return map.find(key);
}

void *value_of(uint8_t *db, uint64_t key)
{
    uint64_t *s = slot_of(db, key);
    return s ? reinterpret_cast<void *>(s[1]) : nullptr;
}

void *vt_of(void *v)
{
    return v ? *static_cast<void **>(v) : nullptr;
}

std::string probe_text(int tag, const std::string &value)
{
    switch (tag) {
    case 0:
    case 1:
    case 11:
    case 12:
        return "0";
    case 5:
        return "false";
    case 2:
    case 3:
    case 4:
    case 17:
        return "";
    default:
        return value;
    }
}

void *probe_class(uint8_t *db, int tag, const std::string &text)
{
    if (auto it = class_vt.find(tag); it != class_vt.end())
        return it->second;
    std::string path = PROBE_PREFIX + std::to_string(tag);
    uint64_t key = runtime::fnv64(path);
    std::string buf = text;
    insert_fn(db, key, uint8_t(tag), buf.data());
    void *vt = vt_of(value_of(db, key));
    class_vt[tag] = vt;
    uintptr_t kb = tag < 19 ? KB_VT[tag] : 0;
    swofty::log("[assets] gamedb: class of tag %d (%s) is vtable base+0x%llx via probe key %s%s", tag, tag_name(tag),
                vt ? (unsigned long long)(reinterpret_cast<uintptr_t>(vt) - base) : 0ull, path.c_str(),
                !runtime::same_build() || !kb ? "" : reinterpret_cast<uintptr_t>(vt) - base == kb ? " (matches the KB vtable)" : " (DIFFERS from the KB vtable)");
    return vt;
}

std::string describe(void *v, int tag)
{
    if (!v)
        return "no value";
    auto p = static_cast<uint8_t *>(v);
    char b[160];
    switch (tag) {
    case 0:
    case 11:
        snprintf(b, sizeof b, "%d", *reinterpret_cast<int32_t *>(p + 8));
        break;
    case 1:
        snprintf(b, sizeof b, "%g", double(*reinterpret_cast<float *>(p + 8)));
        break;
    case 5:
        snprintf(b, sizeof b, "%s", p[8] ? "true" : "false");
        break;
    case 12:
        snprintf(b, sizeof b, "%llu", (unsigned long long)*reinterpret_cast<uint64_t *>(p + 8));
        break;
    case 2:
    case 3:
    case 4:
    case 17:
        snprintf(b, sizeof b, "string, label fnv32 0x%08x", *reinterpret_cast<uint32_t *>(p + 0x30));
        break;
    default:
        snprintf(b, sizeof b, "record at %p", v);
    }
    return b;
}

void apply(uint8_t *db, const char *when)
{
    if (done.exchange(true))
        return;
    runtime::Robin map{db + DB_MAP};
    if (!map.sane()) {
        swofty::log("[assets] gamedb: database at %p (%s): its map looks wrong; no edit applied", static_cast<void *>(db), when);
        return;
    }
    uint64_t before = map.count();
    swofty::log("[assets] gamedb: database at %p (%s) on thread %lu, loaded=%u, %llu record(s); applying %zu edit(s)",
                static_cast<void *>(db), when, GetCurrentThreadId(), unsigned(db[DB_LOADED]), (unsigned long long)before,
                edits.size());
    size_t fresh = 0;
    for (auto &e : edits)
        if (!value_of(db, e.key))
            fresh++;
    uint64_t slots = map.mask() + 1;
    if (!dbread::fits_without_growing(before, fresh + PROBE_RECORDS, slots, 0)) {
        swofty::log("[assets] gamedb: the edits add %zu record(s) to %llu, and the game's database map grows at %llu "
                    "(80%% of %llu slots); growing it after the game has loaded crashes the game at boot, so no edit "
                    "applied and the database is the game's own",
                    fresh, (unsigned long long)before, (unsigned long long)dbread::grows_at(slots),
                    (unsigned long long)slots);
        return;
    }
    size_t overwrote = 0, created = 0, retyped = 0, skipped = 0;
    for (auto &e : edits) {
        void *old = value_of(db, e.key);
        int tag = e.tag;
        void *want = nullptr;
        if (tag < 0) {
            if (!old) {
                swofty::log("[assets] gamedb: %s skipped: a new key needs \"type\"", label(e).c_str());
                skipped++;
                continue;
            }
            for (int t : INFER)
                if (probe_class(db, t, "0") == vt_of(old)) {
                    tag = t;
                    break;
                }
            if (tag < 0) {
                swofty::log("[assets] gamedb: %s skipped: its record class (vtable base+0x%llx) is not a scalar or "
                            "string; give \"type\"",
                            label(e).c_str(), (unsigned long long)(reinterpret_cast<uintptr_t>(vt_of(old)) - base));
                skipped++;
                continue;
            }
            std::string bad = check_text(e, tag);
            if (!bad.empty()) {
                swofty::log("[assets] gamedb: %s skipped: the existing record is %s and %s", label(e).c_str(), tag_name(tag),
                            bad.c_str());
                skipped++;
                continue;
            }
        }
        want = probe_class(db, tag, probe_text(tag, e.value));
        if (!want) {
            swofty::log("[assets] gamedb: %s skipped: tag %d makes no record", label(e).c_str(), tag);
            skipped++;
            continue;
        }
        bool retype = old && vt_of(old) != want;
        int old_tag = tag;
        if (retype) {
            old_tag = -1;
            for (auto &[t, vt] : class_vt)
                if (vt && vt == vt_of(old) && old_tag < 0)
                    old_tag = t;
            if (old_tag < 0)
                for (int t : INFER)
                    if (probe_class(db, t, "0") == vt_of(old)) {
                        old_tag = t;
                        break;
                    }
            if (!e.retype) {
                swofty::log("[assets] gamedb: %s skipped: the existing record is %s (vtable base+0x%llx), the edit says %s; "
                            "set \"retype\": true to replace the record",
                            label(e).c_str(), old_tag < 0 ? "of another class" : tag_name(old_tag),
                            (unsigned long long)(reinterpret_cast<uintptr_t>(vt_of(old)) - base), tag_name(tag));
                skipped++;
                continue;
            }
        }
        std::string was = old ? describe(old, old_tag) : std::string();
        if (retype) {
            auto vt = static_cast<void **>(vt_of(old));
            reinterpret_cast<Dtor_t *>(vt[1])(old, 0);
            void *alloc = db + DB_ALLOC;
            reinterpret_cast<Free_t *>((*static_cast<void ***>(alloc))[7])(alloc, old);
            if (uint64_t *s = slot_of(db, e.key))
                s[1] = 0;
        }
        std::string buf = e.value;
        insert_fn(db, e.key, uint8_t(tag), buf.data());
        void *now = value_of(db, e.key);
        bool ok = now && vt_of(now) == want;
        std::string got = describe(now, tag);
        if (ok && (tag == 2 || tag == 3 || tag == 4 || tag == 17) &&
            *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(now) + 0x30) != runtime::fnv32(e.value.c_str()))
            got += " (fnv32 of the text is " + runtime::hex16(runtime::fnv32(e.value.c_str())).substr(10) + ")";
        if (!ok) {
            swofty::log("[assets] gamedb: %s: insert left %s; counted as skipped", label(e).c_str(),
                        now ? "a record of another class" : "no record");
            skipped++;
            continue;
        }
        (retype ? retyped : old ? overwrote : created)++;
        swofty::log("[assets] gamedb: %s %s (key %s, tag %d %s): %s%s%s", retype ? "retyped" : old ? "overwrote" : "created",
                    label(e).c_str(), runtime::hex16(e.key).c_str(), tag, tag_name(tag), old ? was.c_str() : "",
                    old ? " -> " : "", got.c_str());
    }
    swofty::log("[assets] gamedb: %zu overwritten, %zu created, %zu retyped, %zu skipped; %llu -> %llu record(s) (class "
                "probes included)",
                overwrote, created, retyped, skipped, (unsigned long long)before, (unsigned long long)map.count());
}

void late_inplace(uint8_t *db)
{
    if (done.exchange(true))
        return;
    runtime::Robin map{db + DB_MAP};
    if (!map.sane() || !runtime::same_build()) {
        swofty::log("[assets] gamedb: the database was built before addons loaded and %s; no edit applied",
                    map.sane() ? "this exe is not the analysed build (record classes unknown)" : "its map looks wrong");
        return;
    }
    swofty::log("[assets] gamedb: the database was built before addons loaded (no early stage): only in-place int, float "
                "and bool overwrites of existing keys run now; new keys, strings and retypes need \"early_stage\": true");
    size_t ok = 0, skipped = 0;
    for (auto &e : edits) {
        void *v = value_of(db, e.key);
        int tag = e.tag;
        uintptr_t vt = v ? reinterpret_cast<uintptr_t>(vt_of(v)) - base : 0;
        if (tag < 0)
            tag = vt == KB_VT[0] ? 0 : vt == KB_VT[1] ? 1 : vt == KB_VT[5] ? 5 : -1;
        if (!v || (tag != 0 && tag != 1 && tag != 5) || vt != KB_VT[tag]) {
            swofty::log("[assets] gamedb: %s skipped late: %s", label(e).c_str(),
                        !v ? "the key does not exist" : "not an existing int, float or bool record of that type");
            skipped++;
            continue;
        }
        std::string bad = check_text(e, tag);
        if (!bad.empty()) {
            swofty::log("[assets] gamedb: %s skipped late: %s", label(e).c_str(), bad.c_str());
            skipped++;
            continue;
        }
        auto p = static_cast<uint8_t *>(v);
        std::string was = describe(v, tag);
        if (tag == 0)
            *reinterpret_cast<int32_t *>(p + 8) = int32_t(strtol(e.value.c_str(), nullptr, 10));
        else if (tag == 1)
            *reinterpret_cast<float *>(p + 8) = float(strtod(e.value.c_str(), nullptr));
        else
            p[8] = _stricmp(e.value.c_str(), "true") == 0;
        swofty::log("[assets] gamedb: %s overwrote in place: %s -> %s", label(e).c_str(), was.c_str(), describe(v, tag).c_str());
        ok++;
    }
    swofty::log("[assets] gamedb: late fallback: %zu in-place overwrite(s), %zu skipped", ok, skipped);
}

void *load_hook(void *db, void *cfg)
{
    static const uint32_t probe = perf::probe("core", "gamedb load edits");
    swofty::log("[assets] gamedb: Gdb_Load_GameDatabase(%p) entered on thread %lu", db, GetCurrentThreadId());
    void *r = h_load.call<void *>(db, cfg);
    perf::Scope p(probe);
    std::lock_guard<std::mutex> g(edits_lock);
    apply(static_cast<uint8_t *>(db), "right after Gdb_Load_GameDatabase returned");
    return r;
}

void window_stage(int stage)
{
    if (stage != runtime::STAGE_WINDOW || !g_db)
        return;
    auto db = static_cast<uint8_t *>(*g_db);
    swofty::log("[assets] gamedb: window stage: g_Gdb_Database = %p, loaded=%u, edits %s", static_cast<void *>(db),
                db && runtime::readable(db + DB_LOADED, 1) ? unsigned(db[DB_LOADED]) : 0u,
                done ? "applied" : "NOT applied (the load wrap never ran)");
    if (!db || !lookup_fn)
        return;
    for (auto &e : edits) {
        if (e.path.empty())
            continue;
        void **slot = lookup_fn(e.path.c_str());
        void *v = slot ? *slot : nullptr;
        int tag = e.tag;
        if (tag < 0)
            for (auto &[t, vt] : class_vt)
                if (vt == vt_of(v) && (tag < 0 || t < tag))
                    tag = t;
        swofty::log("[assets] gamedb: window stage: Gdb_Lookup_Key(\"%s\") -> %s", e.path.c_str(),
                    v ? describe(v, tag).c_str() : "not found");
    }
}

}

int runtime::db_add(const json::Value &entry)
{
    std::lock_guard<std::mutex> g(edits_lock);
    if (!h_load || done)
        return SWOFTY_ASSET_LATE;
    return read_edit(entry) ? SWOFTY_ASSET_OK : SWOFTY_ASSET_INVALID;
}

namespace {

void setup()
{
    read_config();
    if (edits.empty())
        return;
    base = swofty::api->module_base();
    swofty::log("[assets] gamedb: %zu edit(s) in %s; addons loaded at the %s stage", edits.size(),
                runtime::config_source().c_str(), runtime::load_stage().c_str());
    uintptr_t load = runtime::find(S_LOAD, "gamedb"), ins = runtime::find(S_INSERT, "gamedb");
    uintptr_t look = runtime::find(S_LOOKUP, "gamedb"), glob = runtime::find(S_GLOBAL, "gamedb");
    insert_fn = reinterpret_cast<Insert_t *>(ins);
    lookup_fn = reinterpret_cast<Lookup_t *>(look);
    g_db = reinterpret_cast<void **>(glob);
    runtime::on_stage(window_stage);
    if (!load || !ins || !glob) {
        swofty::log("[assets] gamedb: a signature is missing; no edit applied");
        return;
    }
    auto db = static_cast<uint8_t *>(*g_db);
    bool built = db && runtime::readable(db + DB_LOADED, 1) && db[DB_LOADED];
    if (runtime::load_stage() != "early") {
        swofty::log("[assets] gamedb: early_stage is off, so addons loaded at the window trigger. The game database is built "
                    "before that (Boot_Init_NeApplication 0x1443c0560 step 10 -> 0x1443c45e0 -> app_desc vt+0x28 0x1400f7950 "
                    "-> .data1 0x147096270 -> Gdb_Load_GameDatabase 0x1445a0350), so the load wrap is not installed. New "
                    "keys, strings and retypes need \"early_stage\": true in swofty/assets.json");
        if (built)
            late_inplace(db);
        else
            swofty::log("[assets] gamedb: the database is not built at the window stage either (g_Gdb_Database = %p); "
                        "no edit applied",
                        static_cast<void *>(db));
        return;
    }
    if (built) {
        swofty::log("[assets] gamedb: addons loaded at the early stage but the database is already built; the early "
                    "anchor fired too late on this build");
        late_inplace(db);
        return;
    }
    auto h = safetyhook::InlineHook::create(reinterpret_cast<void *>(load), reinterpret_cast<void *>(&load_hook));
    if (!h) {
        swofty::log("[assets] gamedb: hooking Gdb_Load_GameDatabase at base+0x%llx failed", (unsigned long long)(load - base));
        return;
    }
    h_load = std::move(*h);
    swofty::log("[assets] gamedb: Gdb_Load_GameDatabase hooked at base+0x%llx (g_Gdb_Database = %p, not built yet); edits "
                "apply when it returns",
                (unsigned long long)(load - base), db);
}

}

void runtime::gamedb_setup()
{
    setup();
}
