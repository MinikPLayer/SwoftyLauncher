#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <safetyhook.hpp>
#include <swofty/mem.hpp>

#include "../src/core/perf.hpp"
#include "../src/loader/alog.h"

std::string settings_addon(const void *, std::string &dll)
{
    dll = "bench.dll";
    return "bench";
}

namespace blackbox {
void note(const char *, const char *, const char *) {}
}

namespace runtime {
const std::wstring &swofty_root()
{
    static const std::wstring root = L".\\";
    return root;
}
}

namespace {

double freq_ns;

double now_ns()
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return double(t.QuadPart) * freq_ns;
}

template <class F> double per_call_ns(int n, F &&f)
{
    double t0 = now_ns();
    for (int i = 0; i < n; i++)
        f(i);
    return (now_ns() - t0) / n;
}

void row(const char *what, double ns)
{
    if (ns >= 1e6)
        printf("%-58s %10.2f ms\n", what, ns / 1e6);
    else if (ns >= 1e3)
        printf("%-58s %10.2f us\n", what, ns / 1e3);
    else
        printf("%-58s %10.1f ns\n", what, ns);
    fflush(stdout);
}

volatile uint64_t sink;

void bench_clocks()
{
    SYSTEMTIME st;
    FILETIME ft;
    LARGE_INTEGER li;
    row("GetLocalTime", per_call_ns(200000, [&](int) { GetLocalTime(&st); sink += st.wMilliseconds; }));
    row("GetSystemTimeAsFileTime", per_call_ns(200000, [&](int) { GetSystemTimeAsFileTime(&ft); sink += ft.dwLowDateTime; }));
    row("GetSystemTimePreciseAsFileTime",
        per_call_ns(200000, [&](int) { GetSystemTimePreciseAsFileTime(&ft); sink += ft.dwLowDateTime; }));
    row("QueryPerformanceCounter", per_call_ns(200000, [&](int) { QueryPerformanceCounter(&li); sink += li.QuadPart; }));
    row("GetTickCount64", per_call_ns(200000, [&](int) { sink += GetTickCount64(); }));
    row("GetCurrentThreadId", per_call_ns(200000, [&](int) { sink += GetCurrentThreadId(); }));
}

CRITICAL_SECTION sync_lock;

void sync_emit(HANDLE h, const char *body)
{
    char buf[2048];
    SYSTEMTIME t;
    GetLocalTime(&t);
    int n = snprintf(buf, sizeof buf, "%04u-%02u-%02u %02u:%02u:%02u.%03u [%5lu] ", t.wYear, t.wMonth, t.wDay, t.wHour,
                     t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentThreadId());
    int m = (int)strlen(body);
    if (m > (int)sizeof buf - n - 3)
        m = (int)sizeof buf - n - 3;
    memcpy(buf + n, body, m);
    n += m;
    buf[n++] = '\r';
    buf[n++] = '\n';
    DWORD w;
    EnterCriticalSection(&sync_lock);
    WriteFile(h, buf, n, &w, NULL);
    LeaveCriticalSection(&sync_lock);
}

void async_emit(HANDLE h, const char *body)
{
    FILETIME now;
    GetSystemTimePreciseAsFileTime(&now);
    DWORD tid = GetCurrentThreadId(), w;
    unsigned m = (unsigned)strlen(body);
    if (alog_push(body, m, &now, tid))
        return;
    char buf[ALOG_LINE];
    int n = alog_format_line(buf, body, m, &now, tid);
    EnterCriticalSection(&sync_lock);
    WriteFile(h, buf, n, &w, NULL);
    LeaveCriticalSection(&sync_lock);
}

void body_line(char *out, size_t cap, int i)
{
    snprintf(out, cap, "[game] UICom: registerViewModelForViewModelBinding: [view_model_data.swofty_addon_%d] node %d "
                       "bound to element <unknown/ui-node-base>",
             i % 9, i);
}

HANDLE open_append(const wchar_t *path)
{
    return CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, NULL);
}

uint64_t count_lines(const wchar_t *path)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return 0;
    static char buf[1 << 16];
    DWORD got;
    uint64_t n = 0;
    while (ReadFile(f, buf, sizeof buf, &got, NULL) && got)
        for (DWORD i = 0; i < got; i++)
            n += buf[i] == '\n';
    CloseHandle(f);
    return n;
}

bool ordered(const wchar_t *path, int threads)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    std::string all;
    static char buf[1 << 16];
    DWORD got;
    while (ReadFile(f, buf, sizeof buf, &got, NULL) && got)
        all.append(buf, got);
    CloseHandle(f);
    std::vector<long> last(64, -1);
    std::vector<unsigned long> tids;
    size_t pos = 0;
    while (pos < all.size()) {
        size_t end = all.find('\n', pos);
        if (end == std::string::npos)
            break;
        std::string line = all.substr(pos, end - pos);
        pos = end + 1;
        size_t b = line.find('['), n = line.find(" node ");
        if (b == std::string::npos || n == std::string::npos)
            return false;
        unsigned long tid = strtoul(line.c_str() + b + 1, nullptr, 10);
        long i = strtol(line.c_str() + n + 6, nullptr, 10);
        size_t k = 0;
        while (k < tids.size() && tids[k] != tid)
            k++;
        if (k == tids.size())
            tids.push_back(tid);
        if (k >= last.size() || i <= last[k])
            return false;
        last[k] = i;
    }
    return int(tids.size()) == threads;
}

void bench_log(const std::wstring &dir)
{
    InitializeCriticalSection(&sync_lock);
    char body[512];
    const int N = 100000;
    std::wstring sp = dir + L"\\sync.log", ap = dir + L"\\async.log";

    HANDLE h = open_append(sp.c_str());
    double sync1 = per_call_ns(N, [&](int i) { body_line(body, sizeof body, i); sync_emit(h, body); });
    CloseHandle(h);
    row("log line, current sync emit (1 thread, 100k lines)", sync1);
    printf("  sync.log lines: %llu\n", (unsigned long long)count_lines(sp.c_str()));

    h = open_append(ap.c_str());
    alog_start(h);
    double t0 = now_ns();
    double async1 = per_call_ns(N, [&](int i) { body_line(body, sizeof body, i); async_emit(h, body); });
    alog_stop();
    double total = now_ns() - t0;
    CloseHandle(h);
    row("log line, async push (1 thread, 100k lines, producer cost)", async1);
    row("  async end-to-end incl. drain, per line", total / N);
    printf("  async.log lines: %llu (producer helped drain %ld time(s), bypassed %ld)\n",
           (unsigned long long)count_lines(ap.c_str()), alog_state.helped, alog_state.bypassed);

    row("format only (snprintf body + GetLocalTime header)", per_call_ns(N, [&](int i) {
            char buf[2048];
            body_line(body, sizeof body, i);
            SYSTEMTIME t;
            GetLocalTime(&t);
            sink += snprintf(buf, sizeof buf, "%04u-%02u-%02u %02u:%02u:%02u.%03u [%5lu] %s", t.wYear, t.wMonth, t.wDay,
                             t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentThreadId(), body);
        }));

    const int T = 4, PER = 25000;
    for (int mode = 0; mode < 2; mode++) {
        std::wstring path = dir + (mode ? L"\\async4.log" : L"\\sync4.log");
        h = open_append(path.c_str());
        alog_state = Alog{};
        if (mode)
            alog_start(h);
        std::vector<double> per(T);
        std::vector<std::thread> th;
        for (int k = 0; k < T; k++)
            th.emplace_back([&, k] {
                char b[512];
                per[k] = per_call_ns(PER, [&](int i) {
                    body_line(b, sizeof b, i);
                    if (mode)
                        async_emit(h, b);
                    else
                        sync_emit(h, b);
                });
            });
        for (auto &t : th)
            t.join();
        if (mode)
            alog_stop();
        CloseHandle(h);
        double worst = 0;
        for (double v : per)
            worst = v > worst ? v : worst;
        row(mode ? "log line, async push (4 threads, worst thread)" : "log line, current sync emit (4 threads, worst thread)",
            worst);
        printf("  lines: %llu, per-thread order %s, helped %ld, bypassed %ld\n", (unsigned long long)count_lines(path.c_str()),
               ordered(path.c_str(), T) ? "kept" : "BROKEN", alog_state.helped, alog_state.bypassed);
    }

    h = open_append((dir + L"\\burst.log").c_str());
    alog_state = Alog{};
    alog_start(h);
    std::vector<double> lat;
    lat.reserve(20000);
    for (int i = 0; i < 20000; i++) {
        body_line(body, sizeof body, i);
        double a = now_ns();
        async_emit(h, body);
        lat.push_back(now_ns() - a);
    }
    alog_stop();
    CloseHandle(h);
    std::sort(lat.begin(), lat.end());
    printf("  async burst of 20k lines: p50 %.0f ns, p99 %.0f ns, p99.9 %.0f ns, max %.0f ns, helped %ld\n", lat[10000],
           lat[19800], lat[19980], lat.back(), alog_state.helped);
}

void bench_vq()
{
    MEMORY_BASIC_INFORMATION m;
    int local = 0;
    row("VirtualQuery, stack", per_call_ns(50000, [&](int) { sink += VirtualQuery(&local, &m, sizeof m); }));
    row("VirtualQuery, exe image", per_call_ns(50000, [&](int) {
            sink += VirtualQuery(reinterpret_cast<void *>(&bench_vq), &m, sizeof m);
        }));
    for (size_t mb : {16, 256, 1024, 4096}) {
        size_t size = mb << 20;
        auto *p = static_cast<uint8_t *>(VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!p) {
            printf("VirtualAlloc %zu MB failed\n", mb);
            continue;
        }
        for (size_t o = 0; o < size; o += 1 << 20)
            p[o] = 1;
        char what[96];
        snprintf(what, sizeof what, "VirtualQuery, middle of a %zu MB committed region", mb);
        row(what, per_call_ns(20000, [&](int i) { sink += VirtualQuery(p + size / 2 + (i & 255) * 4096, &m, sizeof m); }));
        VirtualFree(p, 0, MEM_RELEASE);
    }
    {
        size_t size = 256u << 20;
        auto *p = static_cast<uint8_t *>(VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_READWRITE));
        for (size_t o = 0; o < size; o += 128 * 1024)
            VirtualAlloc(p + o, 64 * 1024, MEM_COMMIT, PAGE_READWRITE);
        row("VirtualQuery, 256 MB reserve with 2048 committed islands", per_call_ns(20000, [&](int i) {
                sink += VirtualQuery(p + size / 2 + (i & 255) * 128 * 1024, &m, sizeof m);
            }));
        VirtualFree(p, 0, MEM_RELEASE);
    }
    {
        size_t size = 1024u << 20;
        auto *p = static_cast<uint8_t *>(VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        row("SDK mem::readable, 8 bytes, cache hit (same page)",
            per_call_ns(200000, [&](int i) { sink += swofty::mem::readable(uintptr_t(p + size / 2 + (i & 63) * 8), 8); }));
        row("SDK mem::readable, 8 bytes, 4096 pages cycling (cache misses)", per_call_ns(200000, [&](int i) {
                sink += swofty::mem::readable(uintptr_t(p + size / 2 + uintptr_t(i & 4095) * 4096), 8);
            }));
        VirtualFree(p, 0, MEM_RELEASE);
    }
}

__attribute__((noinline)) int site_fn(int i)
{
    __asm__ volatile("" ::: "memory");
    return i * 3 + 1;
}

uint8_t *hook_rec;

bool core_readable(const void *p, size_t n)
{
    MEMORY_BASIC_INFORMATION m{};
    if (!p || !VirtualQuery(p, &m, sizeof m) || m.State != MEM_COMMIT || (m.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return false;
    return uintptr_t(p) + n <= uintptr_t(m.BaseAddress) + m.RegionSize;
}

void body_empty(safetyhook::Context &) {}

void body_read_site(safetyhook::Context &)
{
    uint32_t cls = core_readable(hook_rec + 0x70, 4) ? *reinterpret_cast<uint32_t *>(hook_rec + 0x70) : 0;
    bool owner = core_readable(hook_rec + 0xa0, 16) && *reinterpret_cast<void **>(hook_rec + 0xa0) == &body_empty;
    sink += cls + owner;
}

void body_read_site_lazy(safetyhook::Context &)
{
    uint64_t v;
    std::memcpy(&v, hook_rec + 0xa0, 8);
    sink += v == reinterpret_cast<uint64_t>(&body_empty);
}

void bench_midhook()
{
    hook_rec = static_cast<uint8_t *>(VirtualAlloc(nullptr, 1u << 30, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) + (512u << 20);
    const int N = 200000;
    double base = per_call_ns(N, [&](int i) { sink += site_fn(i); });
    row("site call, no hook", base);
    struct V {
        const char *what;
        void (*fn)(safetyhook::Context &);
    } vs[] = {{"site call + MidHook, empty body", body_empty},
              {"site call + MidHook, read_site body (2 VirtualQuery, 1 GB heap)", body_read_site},
              {"site call + MidHook, owner check without VirtualQuery", body_read_site_lazy}};
    for (auto &v : vs) {
        auto h = safetyhook::MidHook::create(reinterpret_cast<void *>(&site_fn), v.fn);
        if (!h) {
            printf("MidHook create failed\n");
            continue;
        }
        row(v.what, per_call_ns(N, [&](int i) { sink += site_fn(i); }));
    }
}

__attribute__((noinline)) int probed_fn(int i, uint32_t id)
{
    perf::Scope p(id);
    return site_fn(i);
}

__attribute__((noinline)) int paused_fn(int i, uint32_t id)
{
    perf::Scope p(id);
    perf::Pause game;
    return site_fn(i);
}

void bench_profile()
{
    perf::attach();
    uint32_t id = perf::probe("bench", "probe"), inner = perf::probe("bench", "inner");
    const int N = 2000000;
    double base = per_call_ns(N, [&](int i) { sink += site_fn(i); });
    row("call, no probe", base);
    row("call inside perf::Scope", per_call_ns(N, [&](int i) { sink += probed_fn(i, id); }) - base);
    row("call inside perf::Scope + perf::Pause", per_call_ns(N, [&](int i) { sink += paused_fn(i, id); }) - base);
    row("two nested perf::Scope", per_call_ns(N, [&](int i) {
            perf::Scope outer(id);
            sink += probed_fn(i, inner);
        }) - base);
    for (int threads : {4, 16}) {
        std::vector<std::thread> ts;
        double t0 = now_ns();
        for (int t = 0; t < threads; t++)
            ts.emplace_back([&] {
                uint64_t acc = 0;
                for (int i = 0; i < N / 4; i++)
                    acc += probed_fn(i, id);
                sink += acc;
            });
        for (auto &t : ts)
            t.join();
        char what[64];
        snprintf(what, sizeof what, "perf::Scope, %d threads on one probe (per call per thread)", threads);
        row(what, (now_ns() - t0) / double(N / 4));
    }
    row("perf::frame_claim when not due", per_call_ns(N, [&](int) { sink += perf::frame_claim(); }));
    double t0 = now_ns();
    perf::flush("bench");
    row("perf.log report written", now_ns() - t0);
}

void bench_files(const std::wstring &dir)
{
    std::wstring sd = dir + L"\\storage";
    CreateDirectoryW(sd.c_str(), nullptr);
    std::string snap(66497, 'x');
    std::wstring path = sd + L"\\snapshot.json", tmp = path + L".tmp";
    row("storage write 66 KB (tmp + WriteFile + MoveFileEx), per op", per_call_ns(300, [&](int) {
            HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            DWORD put;
            WriteFile(f, snap.data(), DWORD(snap.size()), &put, nullptr);
            CloseHandle(f);
            MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
        }));
    WIN32_FILE_ATTRIBUTE_DATA a;
    row("GetFileAttributesExW (inbox doorbell)", per_call_ns(5000, [&](int) {
            sink += GetFileAttributesExW(sd.c_str(), GetFileExInfoStandard, &a);
        }));
    row("FindFirstFile/FindNextFile listing of a 2-entry folder", per_call_ns(2000, [&](int) {
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW((sd + L"\\*").c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE) {
                while (FindNextFileW(h, &fd))
                    sink++;
                FindClose(h);
            }
        }));
    HMODULE m;
    row("GetModuleHandleExW FROM_ADDRESS (storage caller lookup)", per_call_ns(50000, [&](int) {
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&bench_files), &m);
            sink += uintptr_t(m);
        }));
}

}

int main(int argc, char **argv)
{
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    freq_ns = 1e9 / double(f.QuadPart);
    wchar_t cwd[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, cwd);
    std::wstring dir = cwd;
    std::string only = argc > 1 ? argv[1] : "";
    if (only.empty() || only == "clocks")
        bench_clocks();
    if (only.empty() || only == "log")
        bench_log(dir);
    if (only.empty() || only == "vq")
        bench_vq();
    if (only.empty() || only == "midhook")
        bench_midhook();
    if (only.empty() || only == "files")
        bench_files(dir);
    if (only.empty() || only == "profile")
        bench_profile();
    return 0;
}
