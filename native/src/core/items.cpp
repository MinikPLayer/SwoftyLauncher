#include <windows.h>

#include <safetyhook.hpp>
#include <swofty/mixin.hpp>

#include <cstdlib>
#include <string>
#include <vector>

#include "json.hpp"
#include "perf.hpp"

namespace {

struct Target {
    const char *what, *pattern;
    uintptr_t rva;
};

const Target T_OWNED{"Game_Get_UnlockableOwnedState",
                     "41 56 56 57 55 53 48 83 EC 20 48 8B 01 48 8B 71 08 FF 50 08 8B 90 A0 00 00 00", 0x2ec37d0};
constexpr size_t KEY_HASH = 0xa0;
constexpr uint32_t OWNED = 1;

using KeyOf_t = uint8_t *(void *item);

struct Item {
    uint32_t key;
    std::string addon, what;
    LONG logged = 0;
};

std::vector<Item> items;
uintptr_t base;
safetyhook::InlineHook h_owned;

std::wstring self_dir()
{
    HMODULE m = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&self_dir), &m);
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(m, path, MAX_PATH);
    std::wstring dir(path);
    return dir.substr(0, dir.find_last_of(L"\\/") + 1);
}

std::string read_file(const std::wstring &path)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return {};
    std::string out;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart < (64 << 20)) {
        out.resize(size_t(size.QuadPart));
        DWORD got = 0;
        if (!ReadFile(f, out.data(), DWORD(out.size()), &got, nullptr) || got != out.size())
            out.clear();
    }
    CloseHandle(f);
    return out;
}

bool parse_key(const std::string &s, uint32_t *out)
{
    if (s.size() < 3 || s.size() > 10 || s[0] != '0' || (s[1] != 'x' && s[1] != 'X'))
        return false;
    char *end = nullptr;
    unsigned long long v = strtoull(s.c_str() + 2, &end, 16);
    if (!end || *end || v > 0xffffffffull)
        return false;
    *out = uint32_t(v);
    return true;
}

bool load_config()
{
    json::Value doc;
    std::string text = read_file(self_dir() + L"..\\..\\settings.json");
    if (text.empty() || !json::parse(text, doc) || doc["owned_items"].type != json::Value::Arr)
        return false;
    for (auto &j : doc["owned_items"].a) {
        std::string k = j["key"].str(), addon = j.field("addon", "mod").str(), what = j["what"].str();
        uint32_t key = 0;
        if (!parse_key(k, &key) || addon.empty()) {
            swofty::log("[items] skipping owned_items entry key '%s' addon '%s': needs a 0x hex key and an addon id",
                        k.c_str(), addon.c_str());
            continue;
        }
        bool dup = false;
        for (auto &it : items)
            dup |= it.key == key;
        if (dup) {
            swofty::log("[items] skipping duplicate owned_items key 0x%08x (%s)", key, addon.c_str());
            continue;
        }
        items.push_back({key, addon, what.empty() ? k : what});
    }
    return true;
}

uint32_t owned_hook(void *item)
{
    static const uint32_t probe = perf::probe("core", "item ownership check");
    perf::Scope p(probe);
    auto key_of = reinterpret_cast<KeyOf_t *>((*static_cast<void ***>(item))[1]);
    uint32_t key = *reinterpret_cast<const uint32_t *>(key_of(item) + KEY_HASH);
    for (auto &it : items)
        if (it.key == key) {
            if (!InterlockedExchange(&it.logged, 1))
                swofty::log("[items] reporting mod item 0x%08x (%s, %s) as owned", key, it.what.c_str(), it.addon.c_str());
            return OWNED;
        }
    perf::Pause game;
    return h_owned.call<uint32_t>(item);
}

uintptr_t find(const Target &t)
{
    uintptr_t at = 0;
    int hits = swofty::api->scan(".xtls", t.pattern, &at);
    if (hits != 1 || !at) {
        swofty::log("[items] %s: signature matched %d time(s); mod items stay locked", t.what, hits);
        return 0;
    }
    if (at - base != t.rva)
        swofty::log("[items] %s: matched at base+0x%llx, KB build has base+0x%llx", t.what, (unsigned long long)(at - base),
                    (unsigned long long)t.rva);
    return at;
}

}

namespace runtime {
void when_code_ready(const char *what, void (*fn)());
}

namespace {

bool hooks_ready = false;

void hook_owned()
{
    if (h_owned || !hooks_ready || items.empty())
        return;
    uintptr_t at = find(T_OWNED);
    if (!at)
        return;
    auto r = safetyhook::InlineHook::create(reinterpret_cast<void *>(at), reinterpret_cast<void *>(&owned_hook));
    if (!r) {
        swofty::log("[items] ownership check hook at base+0x%llx failed; mod items stay locked",
                    (unsigned long long)(at - base));
        return;
    }
    h_owned = std::move(*r);
    swofty::log("[items] ownership check hooked at base+0x%llx; every other key goes to the game",
                (unsigned long long)(at - base));
}

void items_load()
{
    base = swofty::api->module_base();
    hooks_ready = true;
    if (!load_config() && items.empty()) {
        swofty::log("[items] no \"owned_items\" in settings.json; ownership check left alone");
        return;
    }
    swofty::log("[items] %zu mod item(s) to report as owned", items.size());
    for (auto &it : items)
        swofty::log("[items]   0x%08x %s (%s)", it.key, it.what.c_str(), it.addon.c_str());
    hook_owned();
}

}

namespace runtime {
void own_item(uint32_t key, const std::string &addon, const std::string &what)
{
    for (auto &it : items)
        if (it.key == key)
            return;
    items.push_back({key, addon, what});
    swofty::log("[items] 0x%08x %s (%s) reported as owned", key, what.c_str(), addon.c_str());
    hook_owned();
}
}

SWOFTY_ON_LOAD
{
    runtime::when_code_ready("items: ownership check hook", items_load);
}
