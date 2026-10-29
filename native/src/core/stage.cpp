#include "runtime.hpp"

#include <swofty/mixin.hpp>

#include "state_logic.hpp"

#include <mutex>

namespace runtime {

namespace {

struct Feature {
    const char *name;
    bool stable;
};

const Feature FEATURES[] = {{"group", true},    {"erp", true},        {"db", true},
                            {"lang", true},     {"file", false},      {"events", true},
                            {"storage", true},  {"overlay", false},   {"telemetry", false},
                            {"shaders", false}, {"career", true},     {"popup", true},
                            {"postfx", false},  {"registry_rows", true}};

std::once_flag config_once;
json::Value config_doc;
std::string config_from = "none";
std::wstring game_dir, swofty_dir;
std::vector<void (*)(int)> stage_fns, late_fns;
std::mutex stage_lock;
bool window_seen;

std::wstring module_dir(HMODULE m)
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(m, path, MAX_PATH);
    std::wstring dir(path);
    return dir.substr(0, dir.find_last_of(L"\\/") + 1);
}

std::wstring self_dir()
{
    HMODULE m = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&self_dir), &m);
    return module_dir(m);
}

void load_config()
{
    game_dir = module_dir(nullptr);
    swofty_dir = self_dir() + L"..\\..\\";
    wchar_t full[MAX_PATH * 2] = {};
    if (GetFullPathNameW(swofty_dir.c_str(), MAX_PATH * 2, full, nullptr))
        swofty_dir = full;
    std::string text = read_file(swofty_dir + L"assets.json", size_t(16) << 20);
    if (text.empty()) {
        config_from = "no swofty/assets.json";
        return;
    }
    json::Value doc;
    if (!json::parse(text, doc)) {
        config_from = "swofty/assets.json does not parse";
        return;
    }
    if (doc.type == json::Value::Obj)
        config_doc = std::move(doc);
    config_from = "swofty/assets.json";
}

const uint32_t K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
const uint8_t R[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9,  14, 20, 5, 9,
                       14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                       4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

void md5_block(uint32_t h[4], const uint8_t *p)
{
    uint32_t w[16];
    for (int i = 0; i < 16; i++)
        w[i] = uint32_t(p[i * 4]) | uint32_t(p[i * 4 + 1]) << 8 | uint32_t(p[i * 4 + 2]) << 16 | uint32_t(p[i * 4 + 3]) << 24;
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    for (int i = 0; i < 64; i++) {
        uint32_t f;
        int g;
        if (i < 16)
            f = (b & c) | (~b & d), g = i;
        else if (i < 32)
            f = (d & b) | (~d & c), g = (5 * i + 1) & 15;
        else if (i < 48)
            f = b ^ c ^ d, g = (3 * i + 5) & 15;
        else
            f = c ^ (b | ~d), g = (7 * i) & 15;
        uint32_t t = d;
        d = c;
        c = b;
        uint32_t x = a + f + K[i] + w[g];
        b = b + ((x << R[i]) | (x >> (32 - R[i])));
        a = t;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d;
}

}

const json::Value &config()
{
    std::call_once(config_once, load_config);
    return config_doc;
}

const std::string &config_source()
{
    config();
    return config_from;
}

const std::wstring &game_root()
{
    config();
    return game_dir;
}

const std::wstring &swofty_root()
{
    config();
    return swofty_dir;
}

const std::wstring &state_root()
{
    static std::wstring dir;
    static std::once_flag once;
    std::call_once(once, [] {
        std::wstring want;
        std::string from;
        wchar_t env[MAX_PATH * 2] = {};
        DWORD n = GetEnvironmentVariableW(wide(state_logic::ENV).c_str(), env, MAX_PATH * 2);
        if (n && n < MAX_PATH * 2) {
            want = state_logic::root(std::wstring(env, n));
            from = std::string("the ") + state_logic::ENV + " variable";
        } else {
            json::Value doc;
            std::string text = read_file(swofty_root() + wide(state_logic::FILE), size_t(1) << 16);
            if (!text.empty() && json::parse(text, doc) && doc.type == json::Value::Obj) {
                want = state_logic::root(wide(doc[state_logic::KEY].str()));
                from = std::string("swofty/") + state_logic::FILE;
            }
        }
        if (want.empty())
            return;
        for (size_t i = want.find(L'\\', 3); i != std::wstring::npos; i = want.find(L'\\', i + 1))
            CreateDirectoryW(want.substr(0, i).c_str(), nullptr);
        std::wstring probe = want + L".swofty-write-test";
        HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            swofty::log("[state] %s names %s, which the game can't write to (error %lu); the core's state files stay in "
                        "swofty/registry/ in the game folder",
                        from.c_str(), narrow(want).c_str(), GetLastError());
            return;
        }
        CloseHandle(h);
        dir = want;
        swofty::log("[state] the core's state files are in %s (from %s)", narrow(want).c_str(), from.c_str());
    });
    return dir;
}

bool enabled(const char *feature, const char *what)
{
    for (auto &f : FEATURES) {
        if (strcmp(f.name, feature))
            continue;
        if (f.stable)
            return true;
        for (auto &x : config()["features"].a)
            if (x.str() == feature)
                return true;
        if (what)
            swofty::log("[assets] %s skipped: the runtime %s feature is experimental and \"features\" in %s does not "
                        "turn it on",
                        what, feature, config_source().c_str());
        return false;
    }
    return false;
}

std::string load_stage()
{
    char buf[32] = {};
    DWORD n = GetEnvironmentVariableA("SWOFTY_LOAD_STAGE", buf, sizeof buf);
    return n && n < sizeof buf ? std::string(buf, n) : std::string("window");
}

bool same_build()
{
    auto b = reinterpret_cast<const uint8_t *>(swofty::api->module_base());
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(b + reinterpret_cast<const IMAGE_DOS_HEADER *>(b)->e_lfanew);
    return nt->FileHeader.TimeDateStamp == 1789118616u && nt->OptionalHeader.SizeOfImage > 0x6000000;
}

uintptr_t find(const Sig &s, const char *tag, const char *channel)
{
    uintptr_t at = 0, base = swofty::api->module_base();
    int hits = swofty::api->scan(".xtls", s.pattern, &at);
    if (hits != 1 || !at) {
        swofty::log("[%s] %s: %s signature matched %d time(s)", channel, tag, s.what, hits);
        return 0;
    }
    uintptr_t out = at + s.anchor;
    if (s.kind == Sig::rel32 || s.kind == Sig::riprel)
        out = at + s.insn + s.len + *reinterpret_cast<const int32_t *>(at + s.anchor);
    if (out - base != s.rva)
        swofty::log("[%s] %s: %s resolved to base+0x%llx, KB build has base+0x%llx", channel, tag, s.what,
                    (unsigned long long)(out - base), (unsigned long long)s.rva);
    return out;
}

bool readable(const void *p, size_t n)
{
    MEMORY_BASIC_INFORMATION m{};
    if (!p || !VirtualQuery(p, &m, sizeof m) || m.State != MEM_COMMIT || (m.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return false;
    return uintptr_t(p) + n <= uintptr_t(m.BaseAddress) + m.RegionSize;
}

std::string read_file(const std::wstring &path, size_t limit)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return {};
    std::string out;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(f, &size) && size.QuadPart > 0 && uint64_t(size.QuadPart) <= limit) {
        out.resize(size_t(size.QuadPart));
        size_t done = 0;
        while (done < out.size()) {
            DWORD got = 0, want = DWORD(std::min<size_t>(out.size() - done, 1u << 30));
            if (!ReadFile(f, out.data() + done, want, &got, nullptr) || !got)
                break;
            done += got;
        }
        if (done != out.size())
            out.clear();
    }
    CloseHandle(f);
    return out;
}

bool write_file(const std::wstring &path, const std::string &data)
{
    std::wstring tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    size_t done = 0;
    bool ok = true;
    while (ok && done < data.size()) {
        DWORD put = 0, want = DWORD(std::min<size_t>(data.size() - done, 1u << 30));
        ok = WriteFile(f, data.data() + done, want, &put, nullptr) && put;
        done += put;
    }
    CloseHandle(f);
    if (!ok || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

bool file_size(const std::wstring &path, uint64_t *size)
{
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &d) || (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return false;
    *size = (uint64_t(d.nFileSizeHigh) << 32) | d.nFileSizeLow;
    return true;
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

std::wstring under_swofty(const std::string &rel)
{
    std::wstring p = wide(rel);
    std::replace(p.begin(), p.end(), L'/', L'\\');
    if (p.size() > 1 && (p[1] == L':' || (p[0] == L'\\' && p[1] == L'\\')))
        return p;
    return swofty_root() + p;
}

std::string lower(std::string s)
{
    for (auto &c : s)
        if (c == '\\')
            c = '/';
        else if (c >= 'A' && c <= 'Z')
            c = char(c + 32);
    return s;
}

uint64_t fnv64(const char *s, size_t n)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; i++) {
        h ^= uint8_t(s[i]);
        h *= 0x100000001b3ull;
    }
    return h;
}

uint32_t fnv32(const char *s)
{
    uint32_t h = 0x811c9dc5u;
    for (; *s; s++) {
        h ^= uint8_t(*s);
        h *= 0x01000193u;
    }
    return h;
}

void md5(const void *data, size_t n, uint8_t out[16])
{
    uint32_t h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    auto p = static_cast<const uint8_t *>(data);
    size_t full = n & ~size_t(63);
    for (size_t i = 0; i < full; i += 64)
        md5_block(h, p + i);
    uint8_t tail[128] = {};
    size_t rest = n - full;
    memcpy(tail, p + full, rest);
    tail[rest] = 0x80;
    size_t len = rest + 1 + 8 <= 64 ? 64 : 128;
    uint64_t bits = uint64_t(n) * 8;
    for (int i = 0; i < 8; i++)
        tail[len - 8 + i] = uint8_t(bits >> (8 * i));
    for (size_t i = 0; i < len; i += 64)
        md5_block(h, tail + i);
    for (int i = 0; i < 16; i++)
        out[i] = uint8_t(h[i / 4] >> (8 * (i % 4)));
}

std::string hex16(uint64_t v)
{
    char b[24];
    snprintf(b, sizeof b, "0x%016llx", (unsigned long long)v);
    return b;
}

void on_stage(void (*fn)(int))
{
    std::lock_guard<std::mutex> g(stage_lock);
    stage_fns.push_back(fn);
    if (window_seen)
        late_fns.push_back(fn);
}

/* A setup deferred on a packed build until its code decrypts registers its handlers after the window stage;
   they get that stage once the setup is done. */
void stage_catch_up()
{
    std::vector<void (*)(int)> fns;
    {
        std::lock_guard<std::mutex> g(stage_lock);
        fns.swap(late_fns);
    }
    for (auto fn : fns)
        fn(STAGE_WINDOW);
}

}

extern "C" __declspec(dllexport) int SwoftyCoreStage(int stage)
{
    std::vector<void (*)(int)> fns;
    {
        std::lock_guard<std::mutex> g(runtime::stage_lock);
        fns = runtime::stage_fns;
        runtime::window_seen |= stage == runtime::STAGE_WINDOW;
    }
    swofty::log("[assets] stage %s: %zu handler(s); addons were loaded at the %s stage",
                stage == runtime::STAGE_WINDOW ? "window" : "?", fns.size(), runtime::load_stage().c_str());
    for (auto fn : fns)
        fn(stage);
    return int(fns.size());
}
