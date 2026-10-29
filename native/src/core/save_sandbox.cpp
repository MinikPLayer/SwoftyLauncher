#include "runtime.hpp"

#include <windows.h>

#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "save_sandbox_logic.hpp"

// Test save sandbox (re/db/subsystems/core.md#save-sandbox, docs/design/std-2/test-suite.md "The save
// sandbox"). Only when `swofty test` starts the game with a run token: SWOFTY_TEST_RUN names the run
// folder and SWOFTY_TEST_TOKEN must equal the token in its run.json.
//
// The game keeps every career in one profile save that it reads and writes through Steam's
// ISteamRemoteStorage. Its save device (NeSaveSystem2Win, vtable 0x145a5c640) caches that interface
// in its Steam helper (device+0x30, interface at helper+0x108, written once before the device's Start,
// Ne_SaveSystem2Win_Start, vt+0x08). The core wraps Start: it checks the device and swaps
// helper+0x108 for a proxy that serves every slot from <saves>\remote and never calls Steam. Loads
// and saves mount into %TEMP% and go through the interface, so Steam never sees a test save and
// nothing reaches the player's userdata folder or Steam Cloud. helper+0x00 (the user data folder) is
// only a prefix the device strips from its title folder to build remote names, never a path it opens,
// so it stays as Steam filled it.
//
// Two more wraps: the device's corrupt-save upload (Ne_SaveSystem2Win_WriteInfoXml, vt+0x58) zips a
// save and uploads it through the global Steam interface, so in a test run it does nothing; and
// Ne_SaveSystem2Win_Mount, which every save load and save passes before any file is read, quits the
// game if the sandbox has not armed by then.
//
// Fail closed: in test mode, any failure (a binding that does not resolve, a hook that does not
// install, a device or interface that is not the expected one, an unusable run folder) logs why and
// ends the process before the title screen can load a save. Outside test mode nothing here runs.

namespace {

namespace mem = swofty::mem;
namespace ss = runtime::save_sandbox;

constexpr const char *MIXIN = "swofty_core.dll (test save sandbox)";
constexpr const char *START = "Ne_SaveSystem2Win_Start";
constexpr const char *MOUNT = "Ne_SaveSystem2Win_Mount";
constexpr const char *UPLOAD = "Ne_SaveSystem2Win_WriteInfoXml";
constexpr size_t DEVICE_HELPER = 0x30;
constexpr size_t DEVICE_STORE_IN_CLOUD = 0x3e8;
constexpr size_t HELPER_USER_DATA = 0x00;
constexpr size_t HELPER_REMOTE = 0x108;
constexpr UINT EXIT_SANDBOX = 91;

enum class Mode { off, on };

Mode mode = Mode::off;
bool offered = false, settled = false, read_once = false, test_on = false;
ss::Run run_info;
std::wstring run_dir, saves_dir;
std::string run_id, remote_a;
std::mutex lock;
std::unique_ptr<ss::RemoteDir> remote;
std::atomic<bool> armed{false};
std::atomic<uint32_t> calls[ss::SLOTS];
uintptr_t start_addr = 0;

using Start_t = uint64_t (*)(void *device);
using Mount_t = uint32_t (*)(void *device, void *op, void *save, uint8_t for_saving, uint8_t a5, uint8_t *created);
using Upload_t = int (*)(void *device, void *op, void *a3);
Start_t start_orig = nullptr;
Mount_t mount_orig = nullptr;
Upload_t upload_orig = nullptr;
std::atomic<uint32_t> uploads{0};

struct Proxy {
    void **vtable;
};
Proxy proxy;
void *proxy_vt[ss::SLOTS];

std::string env(const char *name)
{
    wchar_t buf[2048] = {};
    DWORD n = GetEnvironmentVariableW(runtime::wide(name).c_str(), buf, 2048);
    return n && n < 2048 ? runtime::narrow(buf) : std::string();
}

std::wstring dos_path(const std::string &p)
{
    if (!p.empty() && p[0] == '/')
        if (HMODULE k = GetModuleHandleW(L"kernel32.dll"))
            if (auto f = reinterpret_cast<WCHAR *(CDECL *)(LPCSTR)>(GetProcAddress(k, "wine_get_dos_file_name")))
                if (WCHAR *w = f(p.c_str())) {
                    std::wstring out(w);
                    HeapFree(GetProcessHeap(), 0, w);
                    while (out.size() > 3 && (out.back() == L'\\' || out.back() == L'/'))
                        out.pop_back();
                    return out;
                }
    return runtime::wide(ss::windows_path(p));
}

void status(const char *state, const std::string &why)
{
    if (run_dir.empty())
        return;
    std::string text = std::string("{\"schema\":1,\"run\":\"") + run_id + "\",\"state\":\"" + state + "\",\"pid\":" +
                       std::to_string(GetCurrentProcessId()) + ",\"remote\":\"";
    auto esc = [](const std::string &s) {
        std::string o;
        for (char c : s)
            o += c == '\\' || c == '"' ? std::string("\\") + c : std::string(1, c);
        return o;
    };
    text += esc(remote_a) + "\",\"why\":\"" + esc(why) + "\"}\n";
    runtime::write_file(run_dir + L"\\sandbox.json", text);
}

[[noreturn]] void fail_closed(const std::string &why)
{
    for (int i = 0; i < 3; i++)
        swofty::log("[saves] TEST SANDBOX FAILED: %s. The game quits now, before the title screen can load a save "
                    "(run %s)",
                    why.c_str(), run_id.empty() ? "?" : run_id.c_str());
    status("failed", why);
    TerminateProcess(GetCurrentProcess(), EXIT_SANDBOX);
    ExitProcess(EXIT_SANDBOX);
}

void note(int slot, const char *what)
{
    uint32_t n = ++calls[slot];
    if (n <= 3 || !ss::served(slot))
        swofty::log("[saves] sandbox: %s%s%s (call %u)", ss::slot_name(slot), what && *what ? " " : "", what ? what : "",
                    n);
}

std::string named(const char *name)
{
    return name ? std::string(name) : std::string("(null)");
}

bool p_file_write(Proxy *, const char *name, const void *data, int32_t size)
{
    std::lock_guard<std::mutex> g(lock);
    bool ok = remote->write(named(name), data, size);
    ++calls[ss::FileWrite];
    swofty::log("[saves] sandbox: FileWrite %s (%d bytes) -> %s", named(name).c_str(), size, ok ? "written" : "FAILED");
    return ok;
}

int32_t p_file_read(Proxy *, const char *name, void *data, int32_t size)
{
    std::lock_guard<std::mutex> g(lock);
    int32_t n = remote->read(named(name), data, size);
    note(ss::FileRead, (named(name) + " -> " + std::to_string(n) + " bytes").c_str());
    return n;
}

bool p_file_forget(Proxy *, const char *name)
{
    std::lock_guard<std::mutex> g(lock);
    note(ss::FileForget, named(name).c_str());
    return remote->exists(named(name));
}

bool p_file_delete(Proxy *, const char *name)
{
    std::lock_guard<std::mutex> g(lock);
    bool ok = remote->remove(named(name));
    ++calls[ss::FileDelete];
    swofty::log("[saves] sandbox: FileDelete %s -> %s", named(name).c_str(), ok ? "deleted" : "no such file");
    return ok;
}

bool p_set_sync_platforms(Proxy *, const char *name, int32_t)
{
    std::lock_guard<std::mutex> g(lock);
    note(ss::SetSyncPlatforms, named(name).c_str());
    return remote->exists(named(name));
}

bool p_file_exists(Proxy *, const char *name)
{
    std::lock_guard<std::mutex> g(lock);
    bool yes = remote->exists(named(name));
    note(ss::FileExists, (named(name) + (yes ? " -> yes" : " -> no")).c_str());
    return yes;
}

bool p_file_persisted(Proxy *, const char *name)
{
    std::lock_guard<std::mutex> g(lock);
    note(ss::FilePersisted, named(name).c_str());
    return remote->exists(named(name));
}

int32_t p_get_file_size(Proxy *, const char *name)
{
    std::lock_guard<std::mutex> g(lock);
    int32_t n = remote->size(named(name));
    note(ss::GetFileSize, (named(name) + " -> " + std::to_string(n)).c_str());
    return n;
}

int64_t p_get_file_timestamp(Proxy *, const char *name)
{
    std::lock_guard<std::mutex> g(lock);
    note(ss::GetFileTimestamp, named(name).c_str());
    return remote->timestamp(named(name));
}

int32_t p_get_sync_platforms(Proxy *, const char *name)
{
    note(ss::GetSyncPlatforms, named(name).c_str());
    return -1;
}

int32_t p_get_file_count(Proxy *)
{
    std::lock_guard<std::mutex> g(lock);
    int32_t n = remote->count();
    note(ss::GetFileCount, ("-> " + std::to_string(n)).c_str());
    return n;
}

const char *p_get_file_name_and_size(Proxy *, int32_t i, int32_t *size)
{
    std::lock_guard<std::mutex> g(lock);
    const char *n = remote->name_at(i, size);
    note(ss::GetFileNameAndSize, (std::to_string(i) + " -> " + n).c_str());
    return n;
}

bool p_get_quota(Proxy *, uint64_t *total, uint64_t *available)
{
    std::lock_guard<std::mutex> g(lock);
    uint64_t used = remote->used();
    if (total)
        *total = uint64_t(ss::QUOTA);
    if (available)
        *available = used < uint64_t(ss::QUOTA) ? uint64_t(ss::QUOTA) - used : 0;
    note(ss::GetQuota, nullptr);
    return true;
}

bool p_cloud_account(Proxy *)
{
    note(ss::IsCloudEnabledForAccount, nullptr);
    return true;
}

bool p_cloud_app(Proxy *)
{
    note(ss::IsCloudEnabledForApp, nullptr);
    return true;
}

void p_set_cloud_app(Proxy *, bool on)
{
    note(ss::SetCloudEnabledForApp, on ? "true" : "false");
}

int32_t p_local_changes(Proxy *)
{
    note(ss::GetLocalFileChangeCount, nullptr);
    return 0;
}

bool p_batch(Proxy *)
{
    note(ss::BeginFileWriteBatch, nullptr);
    return true;
}

bool p_batch_end(Proxy *)
{
    note(ss::EndFileWriteBatch, nullptr);
    return true;
}

template <int N> uint64_t p_unserved(Proxy *)
{
    note(N, "is not served by the sandbox: it answers 0 and nothing reaches Steam");
    return 0;
}

template <int... N> void fill_unserved(std::integer_sequence<int, N...>)
{
    ((proxy_vt[N] = reinterpret_cast<void *>(&p_unserved<N>)), ...);
}

void build_proxy()
{
    fill_unserved(std::make_integer_sequence<int, ss::SLOTS>{});
    auto put = [](int slot, auto fn) { proxy_vt[slot] = reinterpret_cast<void *>(fn); };
    put(ss::FileWrite, &p_file_write);
    put(ss::FileRead, &p_file_read);
    put(ss::FileForget, &p_file_forget);
    put(ss::FileDelete, &p_file_delete);
    put(ss::SetSyncPlatforms, &p_set_sync_platforms);
    put(ss::FileExists, &p_file_exists);
    put(ss::FilePersisted, &p_file_persisted);
    put(ss::GetFileSize, &p_get_file_size);
    put(ss::GetFileTimestamp, &p_get_file_timestamp);
    put(ss::GetSyncPlatforms, &p_get_sync_platforms);
    put(ss::GetFileCount, &p_get_file_count);
    put(ss::GetFileNameAndSize, &p_get_file_name_and_size);
    put(ss::GetQuota, &p_get_quota);
    put(ss::IsCloudEnabledForAccount, &p_cloud_account);
    put(ss::IsCloudEnabledForApp, &p_cloud_app);
    put(ss::SetCloudEnabledForApp, &p_set_cloud_app);
    put(ss::GetLocalFileChangeCount, &p_local_changes);
    put(ss::BeginFileWriteBatch, &p_batch);
    put(ss::EndFileWriteBatch, &p_batch_end);
    proxy.vtable = proxy_vt;
}

bool code_at(uintptr_t fn)
{
    MEMORY_BASIC_INFORMATION mbi{};
    if (!fn || !VirtualQuery(reinterpret_cast<void *>(fn), &mbi, sizeof mbi) || mbi.State != MEM_COMMIT)
        return false;
    return mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);
}

std::string module_of(uintptr_t p)
{
    HMODULE m = nullptr;
    wchar_t path[MAX_PATH] = {};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(p), &m) ||
        !GetModuleFileNameW(m, path, MAX_PATH))
        return "no module";
    std::wstring s(path);
    return runtime::narrow(s.substr(s.find_last_of(L"\\/") + 1));
}

ss::Device read_device(void *device)
{
    ss::Device d;
    auto at = reinterpret_cast<uintptr_t>(device);
    d.start = start_addr;
    d.readable = at && mem::read(at, d.vtable) && mem::readable(d.vtable, 0x10) && mem::read(d.vtable + 8, d.start_slot);
    if (!d.readable)
        return d;
    mem::read(at + DEVICE_STORE_IN_CLOUD, d.store_in_cloud);
    if (!mem::read(at + DEVICE_HELPER, d.helper) || !d.helper)
        return d.helper = 0, d;
    if (!mem::readable(d.helper, HELPER_REMOTE + 8) || !mem::read(d.helper + HELPER_REMOTE, d.iface) || !d.iface)
        return d.iface = 0, d;
    if (!mem::read(d.iface, d.iface_vtable) || !mem::readable(d.iface_vtable, 8 * 20))
        return d;
    d.iface_vtable_in_game = mem::in_image(d.iface_vtable);
    d.iface_slots_ok = true;
    for (int i = 0; i < 20; i++) {
        uintptr_t fn = 0;
        d.iface_slots_ok = d.iface_slots_ok && mem::read(d.iface_vtable + 8 * size_t(i), fn) && code_at(fn);
    }
    const char *ud = reinterpret_cast<const char *>(d.helper + HELPER_USER_DATA);
    d.user_data_len = strnlen(ud, ss::USER_DATA_CAP);
    return d;
}

void arm(void *device)
{
    ss::Device d = read_device(device);
    std::string why = ss::check_device(d);
    if (!why.empty())
        fail_closed(why);
    auto *helper = reinterpret_cast<uint8_t *>(d.helper);
    void **slot = reinterpret_cast<void **>(helper + HELPER_REMOTE);
    if (*slot == &proxy)
        return;
    std::string iface_module = module_of(d.iface_vtable);
    {
        std::lock_guard<std::mutex> g(lock);
        remote = std::make_unique<ss::RemoteDir>(std::filesystem::path(saves_dir + L"\\" + runtime::wide(ss::REMOTE)));
    }
    *slot = &proxy;
    if (*slot != &proxy)
        fail_closed("the Steam helper did not take the sandbox's interface");
    armed = true;
    swofty::log("[saves] test sandbox armed (run %s): the save device's Steam remote storage (vtable in %s) is replaced "
                "by the sandbox; saves are read and written in %s; nothing reaches Steam or Steam Cloud (user data "
                "prefix %zu chars, m_storeInCloud %u)",
                run_id.c_str(), iface_module.c_str(), remote_a.c_str(), d.user_data_len, d.store_in_cloud);
    status("armed", "");
}

uint64_t start_entry(void *device)
{
    if (mode == Mode::on)
        arm(device);
    return start_orig(device);
}

uint32_t mount_entry(void *device, void *op, void *save, uint8_t for_saving, uint8_t a5, uint8_t *created)
{
    if (mode == Mode::on && !armed)
        fail_closed(std::string("the save device mounted a save (") + (for_saving ? "for saving" : "for loading") +
                    ") before the sandbox armed: its start ran before the core's hook");
    return mount_orig(device, op, save, for_saving, a5, created);
}

int upload_entry(void *device, void *op, void *a3)
{
    if (mode != Mode::on)
        return upload_orig(device, op, a3);
    swofty::log("[saves] sandbox: the game's corrupt-save upload (#%u) is off in a test run; nothing is zipped or "
                "uploaded",
                ++uploads);
    return 0;
}

bool make_dir(const std::wstring &p)
{
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(p), ec);
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

void read_run()
{
    if (read_once)
        return;
    read_once = true;
    std::string dir = env(ss::RUN_ENV), token = env(ss::TOKEN_ENV);
    if (dir.empty() && token.empty())
        return;
    test_on = true;
    mode = Mode::on;
    if (dir.empty())
        fail_closed(std::string(ss::TOKEN_ENV) + " is set without " + ss::RUN_ENV);
    run_dir = dos_path(dir);
    ss::Run r = ss::parse_run(runtime::read_file(run_dir + L"\\" + runtime::wide(ss::RUN_FILE), 1 << 16), token);
    run_id = r.id;
    if (!r.ok)
        fail_closed(r.why + " (" + runtime::narrow(run_dir) + ")");
    run_info = r;
    if (!r.sandbox) {
        mode = Mode::off;
        swofty::log("[test] test run %s: the test agent's only; no save sandbox (run.json names no saves), so the "
                    "game uses the player's own saves and the agent refuses career load, advance and save now",
                    run_id.c_str());
        return;
    }
    saves_dir = run_dir + L"\\" + runtime::wide(r.saves);
    std::wstring rem = saves_dir + L"\\" + runtime::wide(ss::REMOTE);
    remote_a = runtime::narrow(rem);
    if (!make_dir(rem))
        fail_closed("the sandbox folder " + remote_a + " could not be created");
    build_proxy();
    swofty::log("[saves] test run %s: the save sandbox is required; saves go to %s", run_id.c_str(),
                runtime::narrow(saves_dir).c_str());
}

}

namespace runtime {

bool save_sandbox_test_mode()
{
    return mode == Mode::on;
}

bool save_sandbox_armed()
{
    return armed;
}

uint32_t save_sandbox_writes()
{
    return calls[ss::FileWrite].load();
}

const TestRun *test_run()
{
    static TestRun t;
    read_run();
    if (!test_on || !run_info.ok)
        return nullptr;
    t.dir = run_dir;
    t.id = run_info.id;
    t.token = run_info.token;
    t.sandbox = run_info.sandbox;
    t.agent_v = run_info.agent_v;
    t.dlls = run_info.dlls;
    return &t;
}

bool test_run_live()
{
    const TestRun *t = test_run();
    return t && ss::still_armed(runtime::read_file(t->dir + L"\\" + runtime::wide(ss::RUN_FILE), 1 << 16), t->token);
}

std::vector<CoreMixins> save_sandbox_mixins()
{
    std::vector<CoreMixins> out;
    if (offered)
        return out;
    offered = true;
    read_run();
    if (mode != Mode::on)
        return out;
    if (!code_ready())
        fail_closed("this build's game code still reads encrypted when the core commits; the sandbox installs only "
                    "where the save device's start can be hooked before it runs (the Steam build)");
    for (const char *id : {START, MOUNT, UPLOAD})
        if (!swofty::api->resolve(id))
            fail_closed(std::string(id) + " does not resolve on this build (no verified binding for it in bindings.json)");
    start_addr = swofty::api->resolve(START);
    out.push_back({MIXIN,
                   {{START, SWOFTY_MIXIN_WRAP, 1000, (void *)&start_entry, (void **)&start_orig},
                    {MOUNT, SWOFTY_MIXIN_WRAP, 1000, (void *)&mount_entry, (void **)&mount_orig},
                    {UPLOAD, SWOFTY_MIXIN_WRAP, 1000, (void *)&upload_entry, (void **)&upload_orig}}});
    return out;
}

void save_sandbox_commit()
{
    if (!offered || settled || mode != Mode::on)
        return;
    settled = true;
    if (!core_mixins_active(MIXIN))
        fail_closed(std::string("the wraps of ") + START + ", " + MOUNT + " and " + UPLOAD +
                    " did not install (see the [core] lines above)");
    swofty::log("[saves] test sandbox hooked: %s, %s and %s are wrapped; the sandbox arms when the game starts its "
                "save device",
                START, MOUNT, UPLOAD);
}

}
