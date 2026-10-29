#include "runtime.hpp"
#include "registry/older.hpp"

#include <safetyhook.hpp>
#include <swofty/mixin.hpp>

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Runtime string overrides (SwoftyLang, API v8). Every lng_* lookup reaches Loc_LangSet_Find, the
// language set's virtual find (the set's other lookups call it through the vtable). Once an addon
// sets its first override the core wraps that function: while the map is non-empty a key in it
// returns the override text, otherwise the game's own lookup runs; with no override set the
// function is not hooked at all. Overrides apply to every language ("*") or to one language code,
// which wins while it is the game's current language (Loc_LocaleModule_Init's module +0x160 into
// g_Loc_LanguageInfo +0x60). Each distinct text is copied once into storage that is never freed,
// because the game may keep a returned pointer after the override is cleared. refresh() sends the current language
// to the module's listeners (+0x170), the notification the game sends after it loads a language.

namespace {

const runtime::Sig S_SET_FIND{"Loc_LangSet_Find", "41 57 41 56 41 54 56 57 53 48 83 EC 28 44 89 C7 49 89 D6 48 89 CB",
                              0x3acf800};
const runtime::Sig S_MODULE_INIT{"Loc_LocaleModule_Init", "41 57 41 56 41 55 41 54 56 57 53 48 81 EC A0 03 00 00",
                                 0x4435970};
const runtime::Sig S_LANG_INFO{"g_Loc_LanguageInfo", "48 8D 0D ?? ?? ?? ?? 48 8B 5C 08 60", 0x62925e8,
                               runtime::Sig::riprel, 0, 3, 7};

constexpr uint32_t LANGS = 0x13;
constexpr size_t MODULE_LANG = 0x160, MODULE_LISTENERS = 0x170, INFO_STRIDE = 0x100, INFO_CODE = 0x60;

using Find_t = const char *(void *set, const char *key, int32_t mode);
using Init_t = int32_t(void *module);
using Listener_t = void(void *ctx, uint32_t language, uint32_t loaded);

struct Hash {
    using is_transparent = void;
    size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
};

struct Eq {
    using is_transparent = void;
    bool operator()(std::string_view a, std::string_view b) const noexcept { return a == b; }
};

struct Entry {
    const char *all = nullptr;
    std::vector<std::pair<std::string, const char *>> per;
};

bool g_on;
std::once_flag g_setup_once;
uintptr_t g_find_at;
safetyhook::InlineHook h_find, h_init;
std::mutex g_install;
bool g_find_failed;
SRWLOCK g_lock = SRWLOCK_INIT;
std::atomic<size_t> g_count{0};
std::unordered_map<std::string, Entry, Hash, Eq> g_map;
std::unordered_set<std::string, Hash, Eq> g_texts;
std::atomic<uint8_t *> g_module{nullptr};
uint8_t *g_info;

std::string lower(std::string_view s)
{
    std::string out(s);
    for (char &c : out)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    return out;
}

bool all_languages(const char *language)
{
    return !language || !*language || !strcmp(language, "*");
}

const char *current_code()
{
    uint8_t *m = g_module.load(std::memory_order_acquire);
    if (!m || !g_info)
        return nullptr;
    uint32_t lang = *reinterpret_cast<const uint32_t *>(m + MODULE_LANG);
    if (lang >= LANGS)
        return nullptr;
    return *reinterpret_cast<const char *const *>(g_info + size_t(lang) * INFO_STRIDE + INFO_CODE);
}

const char *lookup(const char *key)
{
    const char *out = nullptr;
    AcquireSRWLockShared(&g_lock);
    auto it = g_map.find(std::string_view(key));
    if (it != g_map.end()) {
        out = it->second.all;
        if (!it->second.per.empty())
            if (const char *code = current_code())
                for (auto &[c, text] : it->second.per)
                    if (!_stricmp(c.c_str(), code)) {
                        out = text;
                        break;
                    }
    }
    ReleaseSRWLockShared(&g_lock);
    return out;
}

namespace older = swofty_core::registry::older;

std::mutex g_shim;
std::map<std::string, std::map<std::string, swofty::json::Value>> g_layers;

void fill(const std::string &key, const swofty::json::Value &rec)
{
    auto texts = older::string_texts(rec);
    AcquireSRWLockExclusive(&g_lock);
    if (texts.empty())
        g_map.erase(key);
    else {
        Entry e;
        for (auto &[language, text] : texts) {
            const char *copy = g_texts.emplace(text).first->c_str();
            if (all_languages(language.c_str()))
                e.all = copy;
            else
                e.per.emplace_back(lower(language), copy);
        }
        g_map[key] = std::move(e);
    }
    g_count.store(g_map.size(), std::memory_order_release);
    ReleaseSRWLockExclusive(&g_lock);
}

void refresh_key(const std::string &key)
{
    fill(key, older::effective(runtime::older_store(), older::STRING, "game/" + key));
}

size_t refresh_all()
{
    auto &st = runtime::older_store();
    size_t n = 0;
    for (auto &id : older::string_ids(st)) {
        swofty::json::Value rec = older::effective(st, older::STRING, id);
        if (older::addon_of(id) != "game" && older::string_texts(rec).empty())
            continue;
        fill(older::key_of(id), rec);
        n++;
    }
    return n;
}

void put_layer(const std::string &addon, const std::string &key)
{
    auto &st = runtime::older_store();
    auto a = g_layers.find(addon);
    if (a == g_layers.end())
        return;
    auto k = a->second.find(key);
    if (k == a->second.end() || !k->second.is_object() || !k->second.size()) {
        older::string_layer(st, addon, key, swofty::json::Value());
        if (k != a->second.end())
            a->second.erase(k);
        return;
    }
    older::string_layer(st, addon, key, k->second);
}

const char *find_hook(void *set, const char *key, int32_t mode)
{
    if (g_count.load(std::memory_order_acquire) && key)
        if (const char *text = lookup(key))
            return text;
    return h_find.call<const char *>(set, key, mode);
}

int32_t init_hook(void *module)
{
    int32_t r = h_init.call<int32_t>(module);
    g_module.store(static_cast<uint8_t *>(module), std::memory_order_release);
    swofty::log("[lang] locale module %p initialised (%d); current language %s", module, r,
                current_code() ? current_code() : "?");
    return r;
}

bool ensure_find_hook()
{
    std::lock_guard<std::mutex> g(g_install);
    if (h_find)
        return true;
    if (g_find_failed || !g_find_at)
        return false;
    auto r = safetyhook::InlineHook::create(reinterpret_cast<void *>(g_find_at), reinterpret_cast<void *>(&find_hook));
    if (!r) {
        g_find_failed = true;
        swofty::log("[lang] override: hook on Loc_LangSet_Find at base+0x%llx failed; string overrides off",
                    (unsigned long long)(g_find_at - swofty::api->module_base()));
        return false;
    }
    h_find = std::move(*r);
    swofty::log("[lang] override: Loc_LangSet_Find hooked at base+0x%llx",
                (unsigned long long)(g_find_at - swofty::api->module_base()));
    return true;
}

void setup_once();

int lang_available()
{
    setup_once();
    return g_on && g_find_at && !g_find_failed;
}

int lang_override_set(const char *language, const char *key, const char *text)
{
    setup_once();
    if (!g_on || !g_find_at)
        return SWOFTY_ASSET_UNAVAILABLE;
    if (!key || !*key || !text)
        return SWOFTY_ASSET_INVALID;
    if (!ensure_find_hook())
        return SWOFTY_ASSET_UNAVAILABLE;
    std::string addon = runtime::older_addon(__builtin_return_address(0));
    {
        std::lock_guard<std::mutex> g(g_shim);
        swofty::json::Value &t = g_layers[addon][key];
        if (!t.is_object())
            t = swofty::json::Value::object();
        t[all_languages(language) ? std::string("*") : lower(language)] = text;
        put_layer(addon, key);
        refresh_key(key);
    }
    return SWOFTY_ASSET_OK;
}

int lang_override_clear(const char *language, const char *key)
{
    if (!g_on)
        return SWOFTY_ASSET_UNAVAILABLE;
    if (!key || !*key)
        return SWOFTY_ASSET_INVALID;
    std::lock_guard<std::mutex> g(g_shim);
    for (auto &[addon, keys] : g_layers) {
        auto k = keys.find(key);
        if (k == keys.end())
            continue;
        if (all_languages(language))
            k->second = swofty::json::Value();
        else
            k->second.erase(lower(language));
        put_layer(addon, key);
    }
    refresh_key(key);
    return SWOFTY_ASSET_OK;
}

int lang_override_clear_all()
{
    if (!g_on)
        return SWOFTY_ASSET_UNAVAILABLE;
    std::lock_guard<std::mutex> g(g_shim);
    for (auto &[addon, keys] : g_layers) {
        std::vector<std::string> names;
        for (auto &[k, t] : keys)
            names.push_back(k);
        for (auto &k : names) {
            keys[k] = swofty::json::Value();
            put_layer(addon, k);
        }
    }
    g_layers.clear();
    AcquireSRWLockExclusive(&g_lock);
    g_map.clear();
    g_count.store(0, std::memory_order_release);
    ReleaseSRWLockExclusive(&g_lock);
    refresh_all();
    return SWOFTY_ASSET_OK;
}

int lang_refresh()
{
    uint8_t *m = g_module.load(std::memory_order_acquire);
    if (!g_on || !m)
        return SWOFTY_ASSET_UNAVAILABLE;
    auto *list = *reinterpret_cast<uint8_t **>(m + MODULE_LISTENERS);
    if (!list || !runtime::readable(list, 0x18))
        return SWOFTY_ASSET_UNAVAILABLE;
    auto *b = *reinterpret_cast<uint8_t **>(list + 0x08), *e = *reinterpret_cast<uint8_t **>(list + 0x10);
    uint32_t lang = *reinterpret_cast<const uint32_t *>(m + MODULE_LANG);
    size_t n = 0;
    for (; b && b < e; b += 0x10, n++) {
        auto fn = *reinterpret_cast<Listener_t **>(b);
        if (fn)
            fn(*reinterpret_cast<void **>(b + 0x08), lang, 1);
    }
    swofty::log("[lang] override: refresh sent language %u to %zu listener(s)", lang, n);
    return SWOFTY_ASSET_OK;
}

uintptr_t find(const runtime::Sig &s)
{
    uintptr_t at = 0;
    if (swofty::api->scan(".xtls", s.pattern, &at) != 1 || !at)
        return 0;
    if (s.kind == runtime::Sig::riprel)
        return at + s.insn + s.len + *reinterpret_cast<const int32_t *>(at + s.anchor);
    return at + s.anchor;
}

void setup()
{
    if (!runtime::enabled("lang", nullptr))
        return;
    g_on = true;
    g_find_at = find(S_SET_FIND);
    g_info = reinterpret_cast<uint8_t *>(find(S_LANG_INFO));
    if (uintptr_t at = find(S_MODULE_INIT)) {
        auto r = safetyhook::InlineHook::create(reinterpret_cast<void *>(at), reinterpret_cast<void *>(&init_hook));
        if (r)
            h_init = std::move(*r);
        else
            swofty::log("[lang] override: hook on Loc_LocaleModule_Init failed; per-language overrides and refresh off");
    }
    swofty::log("[lang] override: %s (find %s, language table %s, module hook %s)",
                g_find_at ? "ready, hooked on first use" : "off: Loc_LangSet_Find not found", g_find_at ? "found" : "missing",
                g_info ? "found" : "missing", h_init ? "on" : "off");
    if (!g_find_at)
        return;
    std::lock_guard<std::mutex> g(g_shim);
    if (size_t n = refresh_all()) {
        swofty::log("[lang] override: %zu string(s) from lang.string records", n);
        ensure_find_hook();
    }
}

void setup_once()
{
    if (runtime::code_ready())
        std::call_once(g_setup_once, setup);
}

const SwoftyLang table = {lang_available, lang_override_set, lang_override_clear, lang_override_clear_all,
                          lang_refresh};

}

const SwoftyLang *runtime::lang_table()
{
    return &table;
}

void runtime::lang_override_setup()
{
    setup_once();
}
