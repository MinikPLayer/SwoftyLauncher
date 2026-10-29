// SPDX-License-Identifier: MIT
// swofty_record.exe: loads an addon DLL against the fake core, calls its SwoftyAddonRegister and writes
// what it registered as JSON. SwoftyAddonInit is never called. A crash, a C++ exception, a hang or a call
// outside the registry, hooks and services is reported in the JSON and in the exit code.
//
//   swofty_record.exe <addon.dll> <out.json> [--manifest swofty.json] [--timeout seconds] [--recording-only]
//
// Every registry call goes through the core's registry engine (native/src/core/registry, linked in), so a
// record the game refuses is listed under "refused" and never as a record; --recording-only records
// without it. After SWOFTY_REGISTER, each layer edit runs once on its record's value (or {}) so the
// references and options it reads are recorded.
//
// Exit codes: 0 recorded (or no SwoftyAddonRegister), 1 usage, 2 the DLL or the recorder's
// swofty_core.dll didn't load, 3 crashed, 4 threw, 5 timed out, 6 called something unexpected.
#include <windows.h>

#include <shellapi.h>

#include <atomic>
#include <cstdio>
#include <exception>
#include <string>
#include <vector>

#include <swofty/fake_core.hpp>

extern "C" const SwoftyFakeEngine *swofty_registry_engine(void);

namespace {

swofty::fake::Core *core = nullptr;
std::wstring out_path;
std::string dll_name;
std::atomic<bool> finished{false};

std::string narrow(const std::wstring &w)
{
    if (w.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}


std::string hex(uint64_t v)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, "0x%llx", static_cast<unsigned long long>(v));
    return buf;
}

std::string base_name(const std::wstring &path)
{
    size_t at = path.find_last_of(L"\\/");
    return narrow(at == std::wstring::npos ? path : path.substr(at + 1));
}

bool write_file(const std::wstring &path, const std::string &text)
{
    FILE *f = _wfopen(path.c_str(), L"wb");
    if (!f)
        return false;
    bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    return std::fclose(f) == 0 && ok;
}

[[noreturn]] void finish(const char *status, swofty::json::Value detail, int code)
{
    if (finished.exchange(true)) {
        Sleep(INFINITE);
    }
    swofty::json::Value out;
    std::string in_call = core ? core->current_call() : std::string();
    bool locked = false;
    if (core)
        for (int i = 0; i < 50 && !(locked = core->mutex().try_lock()); i++)
            Sleep(10);
    if (locked) {
        out = core->recording();
        core->mutex().unlock();
    } else
        out = swofty::json::Value::object(
            {{"recorder", 1}, {"addon", core ? core->addon() : std::string()}, {"records", swofty::json::Value::array()}});
    if (!in_call.empty()) {
        if (!detail.is_object())
            detail = swofty::json::Value::object();
        detail["in"] = in_call;
    }
    out["dll"] = dll_name;
    out["status"] = status;
    if (!detail.is_null())
        out["error"] = std::move(detail);
    if (!write_file(out_path, swofty::json::dump(out, 2) + "\n"))
        std::fprintf(stderr, "swofty_record: can't write %s\n", narrow(out_path).c_str());
    if (code != 0)
        std::fprintf(stderr, "swofty_record: %s: %s\n", dll_name.c_str(), status);
    std::fflush(stderr);
    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(code));
    for (;;)
        Sleep(INFINITE);
}

LONG WINAPI on_crash(EXCEPTION_POINTERS *e)
{
    swofty::json::Value d = swofty::json::Value::object();
    if (e && e->ExceptionRecord) {
        auto *r = e->ExceptionRecord;
        auto addr = reinterpret_cast<uintptr_t>(r->ExceptionAddress);
        d["code"] = hex(r->ExceptionCode);
        HMODULE mod = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(r->ExceptionAddress), &mod) &&
            mod) {
            wchar_t name[MAX_PATH] = {};
            GetModuleFileNameW(mod, name, MAX_PATH);
            d["module"] = base_name(name);
            d["offset"] = hex(addr - reinterpret_cast<uintptr_t>(mod));
        } else
            d["address"] = hex(addr);
        if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2) {
            d["access"] = r->ExceptionInformation[0] == 8 ? "execute" : r->ExceptionInformation[0] ? "write" : "read";
            d["target"] = hex(r->ExceptionInformation[1]);
        }
    }
    finish("crashed", std::move(d), 3);
}

void on_terminate()
{
    finish("exception", swofty::json::Value::object({{"what", "the addon threw a C++ exception"}}), 4);
}

DWORD WINAPI watchdog(void *arg)
{
    DWORD ms = static_cast<DWORD>(reinterpret_cast<uintptr_t>(arg));
    Sleep(ms);
    finish("timeout", swofty::json::Value::object({{"seconds", static_cast<int64_t>(ms / 1000)}}), 5);
}

std::string read_file(const std::wstring &path)
{
    std::string out;
    FILE *f = _wfopen(path.c_str(), L"rb");
    if (!f)
        return out;
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
        out.append(buf, n);
    std::fclose(f);
    return out;
}

std::wstring exe_dir()
{
    wchar_t self[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring s(self);
    size_t at = s.find_last_of(L"\\/");
    return at == std::wstring::npos ? L"." : s.substr(0, at);
}

}

int main()
{
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> pos;
    std::wstring manifest;
    DWORD timeout_ms = 30000;
    bool recording_only = false;
    for (int i = 1; argv && i < argc; i++) {
        std::wstring a = argv[i];
        if (a == L"--manifest" && i + 1 < argc)
            manifest = argv[++i];
        else if (a == L"--recording-only")
            recording_only = true;
        else if (a == L"--timeout" && i + 1 < argc)
            timeout_ms = static_cast<DWORD>(_wtoi(argv[++i])) * 1000;
        else
            pos.push_back(a);
    }
    if (pos.size() != 2) {
        std::fprintf(stderr, "usage: swofty_record.exe <addon.dll> <out.json> [--manifest swofty.json] [--timeout s]\n");
        return 1;
    }
    out_path = pos[1];
    dll_name = base_name(pos[0]);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter(&on_crash);
    std::set_terminate(&on_terminate);

    std::string addon_id = "addon";
    std::vector<std::pair<std::string, swofty::json::Value>> defaults;
    if (!manifest.empty()) {
        swofty::json::Error err;
        swofty::json::Value m = swofty::json::parse(read_file(manifest), &err);
        if (!err.ok())
            finish("bad_manifest", swofty::json::Value::object({{"what", err.message}}), 1);
        if (m["id"].is_string())
            addon_id = std::string(m["id"].as_string());
        if (auto *opts = m["options"].if_array())
            for (const auto &o : *opts)
                if (o["id"].is_string())
                    defaults.emplace_back(std::string(o["id"].as_string()), o["default"]);
    }

    HMODULE stub = LoadLibraryW((exe_dir() + L"\\swofty_core.dll").c_str());
    auto tables = stub ? reinterpret_cast<void (*)(const SwoftyRegistry *, const SwoftyHooks *, const SwoftyServices *)>(
                             GetProcAddress(stub, "SwoftyRecorderTables"))
                       : nullptr;
    if (!tables)
        finish("load_failed",
               swofty::json::Value::object({{"what", "the recorder's swofty_core.dll is missing next to swofty_record.exe"},
                                            {"code", static_cast<int64_t>(GetLastError())}}),
               2);

    static swofty::fake::Core recorder{addon_id, recording_only ? nullptr : swofty_registry_engine()};
    core = &recorder;
    for (auto &[id, v] : defaults)
        recorder.option(id, v);
    tables(recorder.registry_table(), recorder.hooks_table(), recorder.services_table());

    CreateThread(nullptr, 0, &watchdog, reinterpret_cast<void *>(static_cast<uintptr_t>(timeout_ms)), 0, nullptr);

    HMODULE addon = LoadLibraryExW(pos[0].c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!addon)
        finish("load_failed",
               swofty::json::Value::object({{"what", "LoadLibrary failed"}, {"code", static_cast<int64_t>(GetLastError())}}),
               2);
    auto reg = reinterpret_cast<SwoftyAddonRegisterFn>(GetProcAddress(addon, "SwoftyAddonRegister"));
    if (!reg)
        finish("no_register", nullptr, 0);
    try {
        recorder.run_register(reg);
        recorder.run_layer_edits();
    } catch (...) {
        finish("exception", swofty::json::Value::object({{"what", "the addon threw a C++ exception"}}), 4);
    }
    if (!recorder.unexpected().empty())
        finish("unexpected", swofty::json::Value::object({{"what", "the addon called outside the registry, hooks and services"}}),
               6);
    finish("ok", nullptr, 0);
}
