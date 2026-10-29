#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
#include "apply/moments.hpp"
#include "blackbox.hpp"
#include "crash_heap.hpp"
#include "json.hpp"
#include "perf.hpp"
#include "safetyhook.hpp"
#include "swofty_api.h"

std::string core_code_owner(uintptr_t addr);

namespace runtime {
std::string load_stage();
void on_stage(void (*fn)(int));
void hud_fatal();
void netplay_exit();
}

// Crash log and crash reports. The crash log writes each fatal-class exception, the top-level filter
// and calls to ExitProcess and TerminateProcess to loader.log. A crash report (swofty\crash-<time>.txt
// and a minidump beside it) is written once per process: for an exception that reaches the top-level
// filter, for the game's own fatal paths (a fatal Ne heap error, Ne_FatalAbort, a heap corruption or
// fail-fast exception, a stack overflow), and for an exit with a non-zero code or one that follows a
// fatal-class exception within EXIT_AFTER_FATAL_MS. A thread started at attach writes it while the
// reporting thread waits, so a stack overflow or a corrupt stack still gets one. An exit before the
// window stage logs its caller stack whatever its code; a clean one there still gets no report.

namespace {

constexpr LONG MAX_REPORTS = 16;
constexpr int MAX_FRAMES = 32;
constexpr LONG MAX_SEEN = 256;
constexpr int REPORT_FRAMES = 64;
constexpr int THREAD_FRAMES = 8;
constexpr int SCAN_FRAMES = 48;
constexpr size_t STACK_SCAN = 0x8000;
constexpr size_t TEXT_CAP = size_t(4) << 20;
constexpr int LOG_LINES = 200;
constexpr size_t LOG_TAIL = 256 << 10;
constexpr ULONGLONG PRIOR_MS = 60000;
constexpr ULONGLONG EXIT_AFTER_FATAL_MS = 10000;
constexpr uint64_t DUMP_STACKS = uint64_t(48) << 20;
constexpr int KEEP_REPORTS = 8, KEEP_DUMPS = 3;
constexpr DWORD TEXT_WAIT_MS = 20000, DUMP_WAIT_MS = 60000, FULL_DUMP_WAIT_MS = 600000;
constexpr DWORD CODE_EXIT = 0xE5F10001, CODE_GAME_FATAL = 0xE5F10002;

constexpr uint32_t KB_STAMP = 1789118616;
constexpr uint32_t RVA_REPORT_MEMORY_ERROR = 0x395f6e0, RVA_HEAP_ERROR = 0x39657d0;
constexpr uint32_t EA_STAMP = 1789128892, EA_RVA_REPORT_MEMORY_ERROR = 0x394c770;
constexpr const char *PATTERN_REPORT_MEMORY_ERROR =
    "41 57 41 56 41 55 41 54 56 57 55 53 48 81 EC B8 02 00 00 C5 78 29 8C 24 A0 02 00 00 C5 79 29 84 24 90 02 00 00 "
    "C5 F9 29 BC 24 80 02 00 00";
constexpr uint8_t HEAP_ERROR_PREFIX[] = {0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56,
                                         0x57, 0x53, 0x48, 0x83, 0xEC, 0x18, 0x48, 0x8D, 0x6C, 0x24,
                                         0x10, 0x89, 0xD6, 0x48, 0x89, 0xCF, 0x48, 0x8B, 0x5D, 0x78};
constexpr uint8_t FATAL_ABORT[] = {0xC7, 0x04, 0x25, 0x01, 0x00, 0x00, 0x00, 0xAD, 0xDE, 0xAD, 0xDE};

struct Symbol {
    uint32_t rva;
    std::string name;
};

enum Kind { K_EXCEPTION, K_FATAL, K_EXIT };
enum DumpMode { DUMP_OFF, DUMP_NORMAL, DUMP_FULL };

struct Snapshot {
    alignas(16) CONTEXT ctx;
    EXCEPTION_RECORD rec;
    DWORD tid;
    ULONGLONG ms;
    uintptr_t lo, hi;
};

struct Request {
    alignas(16) CONTEXT ctx;
    EXCEPTION_RECORD rec;
    Kind kind;
    bool has_exception, has_prior, loader_lock;
    DWORD tid;
    uintptr_t lo, hi;
    ULONGLONG ms;
    char reason[640];
    char blame[320];
    uint32_t probes[32];
    int nprobes;
    Snapshot prior;
};

struct HeapError {
    bool set, fatal, stats;
    int code;
    DWORD tid;
    ULONGLONG ms;
    uintptr_t heap;
    uint64_t capacity, used, free_bytes, largest;
    char name[64];
    char msg[768];
};

struct Mod {
    uintptr_t base;
    size_t size;
    char name[64];
    char path[260];
};

struct Out {
    HANDLE file = INVALID_HANDLE_VALUE;
    char *b = nullptr;
    size_t cap = 0, n = 0, written = 0;
    bool kept = true;

    void put(const char *f, ...) __attribute__((format(printf, 2, 3)))
    {
        if (!b)
            return;
        if (cap - n < 8192)
            flush(true);
        va_list ap;
        va_start(ap, f);
        int k = vsnprintf(b + n, cap - n, f, ap);
        va_end(ap);
        if (k > 0)
            n = std::min(cap - 1, n + size_t(k));
    }

    void flush(bool reset = false)
    {
        if (file != INVALID_HANDLE_VALUE && n > written) {
            DWORD w;
            WriteFile(file, b + written, DWORD(n - written), &w, nullptr);
        }
        written = n;
        if (reset)
            n = written = 0, kept = false;
    }
};

SwoftyApi *api;
void (*loader_log)(const char *, ...);
HMODULE exe, self;
uintptr_t exe_base;
size_t exe_size;
uint32_t exe_stamp;
DWORD start_tick;
ULONGLONG start_ms;
std::vector<Symbol> symbols;
bool symbols_match, kb_build;
LPTOP_LEVEL_EXCEPTION_FILTER previous;
safetyhook::InlineHook exit_hook, terminate_hook, nt_terminate_hook, heap_error_hook;
uintptr_t seen[MAX_SEEN];
volatile LONG seen_count, reports, busy, exited, claimed, writing, snapshot_lock, fatal_count, window_reached;
bool ea_build;
std::wstring diag_path, swofty_dir;
std::string addon_text;
DumpMode dump_mode = DUMP_NORMAL;
using DumpFn = BOOL(WINAPI *)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                              PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
DumpFn write_dump;
using ThreadInfoFn = LONG(NTAPI *)(HANDLE, int, void *, ULONG, ULONG *);
ThreadInfoFn query_thread;
using DescriptionFn = HRESULT(WINAPI *)(HANDLE, PWSTR *);
DescriptionFn thread_description;
const char *(*wine_version)();
uintptr_t heap_error_fn, report_memory_error;
HANDLE go_event, text_event, done_event;
DWORD writer_tid;
uintptr_t writer_stack_top;
HANDLE volatile suspended, dump_file = INVALID_HANDLE_VALUE;
volatile LONG step_at, writer_faults;
DWORD fault_code;
uintptr_t fault_ip;
Request req;
Snapshot last_fatal;
HeapError heap_error;
Out out;
Mod mods[512];
int nmods;
wchar_t report_path[MAX_PATH], dump_path[MAX_PATH];
char report_name[64], dump_note[256];

blackbox::Ring<256> events;
blackbox::Ring<64> warnings;

std::string fmt(const char *f, ...)
{
    char b[512];
    va_list ap;
    va_start(ap, f);
    vsnprintf(b, sizeof b, f, ap);
    va_end(ap);
    return b;
}

void diag_line(const std::string &s)
{
    if (!ea_build || diag_path.empty())
        return;
    HANDLE f = CreateFileW(diag_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    char line[1200];
    int n = snprintf(line, sizeof line, "%04u-%02u-%02u %02u:%02u:%02u.%03u [%5lu] %s\r\n", t.wYear, t.wMonth, t.wDay,
                     t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentThreadId(), s.c_str());
    if (n > 0) {
        DWORD w;
        WriteFile(f, line, (DWORD)(n > (int)sizeof line ? (int)sizeof line : n), &w, nullptr);
    }
    CloseHandle(f);
}

/* Like api->log, but on a non-matching (EA/other) build also mirrors the line into the dedicated
   swofty\ea-diagnostics.log, so the friend's single file carries the symbolised crash report. */
void cl(const char *f, ...)
{
    char b[1024];
    va_list ap;
    va_start(ap, f);
    vsnprintf(b, sizeof b, f, ap);
    va_end(ap);
    api->log("%s", b);
    diag_line(b);
}

/* Every api->log call from core and the addons passes through here on its way to the loader, so the
   black box sees the game log's warnings and the telemetry's session lines without another hook. */
void tap(const char *f, ...)
{
    char b[2048];
    va_list ap;
    va_start(ap, f);
    vsnprintf(b, sizeof b, f, ap);
    va_end(ap);
    if (!strncmp(b, "[game] ", 7) && (strstr(b, " (warn): ") || strstr(b, " (error): ")))
        warnings.put(GetTickCount64(), GetCurrentThreadId(), "game", b + 7);
    else if (!strncmp(b, "[telemetry] session ", 20))
        events.put(GetTickCount64(), GetCurrentThreadId(), "session", b + 20);
    loader_log("%s", b);
}

bool readable(uintptr_t a, size_t n)
{
    for (uintptr_t p = a & ~0xfffull; p < a + n; p += 0x1000) {
        MEMORY_BASIC_INFORMATION m;
        if (!VirtualQuery(reinterpret_cast<void *>(p), &m, sizeof m) || m.State != MEM_COMMIT ||
            (m.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || !(m.Protect & 0xEE))
            return false;
    }
    return true;
}

struct Reader {
    uintptr_t page = 1;
    bool ok = false;

    bool read(uintptr_t a, void *dst, size_t n)
    {
        if (!n || a + n < a)
            return false;
        for (uintptr_t p = a & ~0xfffull; p < a + n; p += 0x1000) {
            if (p != page) {
                page = p;
                ok = readable(p, 1);
            }
            if (!ok)
                return false;
        }
        memcpy(dst, reinterpret_cast<const void *>(a), n);
        return true;
    }
};

const char *code_name(DWORD c)
{
    switch (c) {
    case EXCEPTION_ACCESS_VIOLATION: return "ACCESS_VIOLATION";
    case EXCEPTION_IN_PAGE_ERROR: return "IN_PAGE_ERROR";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_PRIV_INSTRUCTION: return "PRIV_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_INT_OVERFLOW: return "INT_OVERFLOW";
    case EXCEPTION_STACK_OVERFLOW: return "STACK_OVERFLOW";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "NONCONTINUABLE_EXCEPTION";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "DATATYPE_MISALIGNMENT";
    case 0xC0000374: return "HEAP_CORRUPTION";
    case 0xC0000409: return "STACK_BUFFER_OVERRUN";
    case EXCEPTION_BREAKPOINT: return "BREAKPOINT";
    case 0xE06D7363: return "C++ exception";
    case CODE_EXIT: return "swofty exit report";
    case CODE_GAME_FATAL: return "swofty game fatal error";
    }
    return "exception";
}

bool fatal(DWORD c)
{
    return c == EXCEPTION_ACCESS_VIOLATION || c == EXCEPTION_IN_PAGE_ERROR || c == EXCEPTION_ILLEGAL_INSTRUCTION ||
           c == EXCEPTION_PRIV_INSTRUCTION || c == EXCEPTION_INT_DIVIDE_BY_ZERO || c == EXCEPTION_INT_OVERFLOW ||
           c == EXCEPTION_STACK_OVERFLOW || c == EXCEPTION_ARRAY_BOUNDS_EXCEEDED ||
           c == EXCEPTION_NONCONTINUABLE_EXCEPTION || c == EXCEPTION_DATATYPE_MISALIGNMENT || c == 0xC0000374 ||
           c == 0xC0000409;
}

std::string module_name(HMODULE m)
{
    char path[MAX_PATH] = "?";
    GetModuleFileNameA(m, path, MAX_PATH);
    const char *slash = strrchr(path, '\\');
    return slash ? slash + 1 : path;
}

const IMAGE_SECTION_HEADER *section_of(uintptr_t mb, uintptr_t a)
{
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(mb);
    if (!readable(mb, sizeof *dos) || dos->e_magic != IMAGE_DOS_SIGNATURE ||
        !readable(mb + dos->e_lfanew, sizeof(IMAGE_NT_HEADERS)))
        return nullptr;
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(mb + dos->e_lfanew);
    auto s = IMAGE_FIRST_SECTION(nt);
    if (!readable(reinterpret_cast<uintptr_t>(s), nt->FileHeader.NumberOfSections * sizeof *s))
        return nullptr;
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, s++)
        if (a - (mb + s->VirtualAddress) < (s->Misc.VirtualSize ? s->Misc.VirtualSize : s->SizeOfRawData))
            return s;
    return nullptr;
}

void section_into(uintptr_t mb, uintptr_t a, char *out)
{
    auto s = section_of(mb, a);
    if (!s) {
        strcpy(out, "?");
        return;
    }
    memcpy(out, s->Name, 8);
    out[8] = 0;
}

std::string section_name(uintptr_t a)
{
    char s[9];
    section_into(reinterpret_cast<uintptr_t>(exe), a, s);
    return s;
}

PRUNTIME_FUNCTION primary(PRUNTIME_FUNCTION f, DWORD64 base)
{
    for (int i = 0; i < 8; i++) {
        auto ui = reinterpret_cast<const uint8_t *>(base + f->UnwindData);
        if (!readable(reinterpret_cast<uintptr_t>(ui), 4) || !((ui[0] >> 3) & UNW_FLAG_CHAININFO))
            break;
        f = reinterpret_cast<PRUNTIME_FUNCTION>(const_cast<uint8_t *>(ui) + 4 + 2 * ((ui[2] + 1) & ~1));
    }
    return f;
}

const char *kb_name(uint32_t start)
{
    auto it = std::lower_bound(symbols.begin(), symbols.end(), start, [](const Symbol &x, uint32_t r) { return x.rva < r; });
    return it != symbols.end() && it->rva == start ? it->name.c_str() : nullptr;
}

std::string where(uintptr_t a)
{
    HMODULE m = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(a), &m) || !m) {
        std::string owner = core_code_owner(a);
        return fmt("0x%llx", (unsigned long long)a) + (owner.empty() ? " (no module)" : " (" + owner + ")");
    }
    auto mb = reinterpret_cast<uintptr_t>(m);
    std::string s = fmt("%s+0x%llx", module_name(m).c_str(), (unsigned long long)(a - mb));
    if (m == exe)
        s += " " + section_name(a);
    DWORD64 ib = 0;
    auto f = RtlLookupFunctionEntry(a, &ib, nullptr);
    if (!f)
        return s;
    f = primary(f, ib);
    uint32_t start = f->BeginAddress, off = uint32_t(a - ib - start);
    if (m != exe) {
        std::string owner = core_code_owner(ib + start);
        return owner.empty() ? s : s + " (" + owner + fmt("+0x%x)", off);
    }
    if (const char *name = kb_name(start))
        return s + fmt(" %s+0x%x", name, off);
    return s + fmt(" fn base+0x%x+0x%x", start, off);
}

/* The module list read from the PEB without the loader lock, which an exiting or crashing thread may
   hold; GetModuleHandleEx would wait for it. */
void snapshot_modules()
{
    nmods = 0;
    auto teb = reinterpret_cast<uintptr_t>(NtCurrentTeb());
    uintptr_t peb = *reinterpret_cast<uintptr_t *>(teb + 0x60), ldr = 0;
    if (!readable(peb + 0x18, 8) || !(ldr = *reinterpret_cast<uintptr_t *>(peb + 0x18)) || !readable(ldr + 0x10, 16))
        return;
    uintptr_t head = ldr + 0x10, e = *reinterpret_cast<uintptr_t *>(head);
    for (int i = 0; e != head && i < 1024 && nmods < int(sizeof mods / sizeof *mods); i++) {
        if (!readable(e, 0x68))
            break;
        Mod &m = mods[nmods];
        m.base = *reinterpret_cast<uintptr_t *>(e + 0x30);
        m.size = *reinterpret_cast<uint32_t *>(e + 0x40);
        auto copy = [](uintptr_t us, char *dst, size_t cap) {
            uint16_t len = *reinterpret_cast<uint16_t *>(us);
            auto w = *reinterpret_cast<const wchar_t **>(us + 8);
            dst[0] = 0;
            if (!w || !readable(reinterpret_cast<uintptr_t>(w), len))
                return;
            int k = WideCharToMultiByte(CP_UTF8, 0, w, len / 2, dst, int(cap - 1), nullptr, nullptr);
            dst[k > 0 ? k : 0] = 0;
        };
        copy(e + 0x58, m.name, sizeof m.name);
        copy(e + 0x48, m.path, sizeof m.path);
        if (m.base && m.size)
            nmods++;
        e = *reinterpret_cast<uintptr_t *>(e);
    }
    std::sort(mods, mods + nmods, [](const Mod &a, const Mod &b) { return a.base < b.base; });
}

const Mod *mod_at(uintptr_t a)
{
    const Mod *m = std::upper_bound(mods, mods + nmods, a, [](uintptr_t x, const Mod &y) { return x < y.base; });
    if (m == mods)
        return nullptr;
    --m;
    return a - m->base < m->size ? m : nullptr;
}

void where_into(char *outb, size_t cap, uintptr_t a, bool owners)
{
    const Mod *m = mod_at(a);
    if (!m) {
        std::string owner = owners ? core_code_owner(a) : std::string();
        snprintf(outb, cap, "0x%llx (%s)", (unsigned long long)a, owner.empty() ? "no module" : owner.c_str());
        return;
    }
    char sec[9];
    section_into(m->base, a, sec);
    int k = snprintf(outb, cap, "%s+0x%llx %s", m->name, (unsigned long long)(a - m->base), sec);
    if (k < 0 || size_t(k) >= cap)
        return;
    DWORD64 ib = 0;
    auto f = RtlLookupFunctionEntry(a, &ib, nullptr);
    if (!f)
        return;
    f = primary(f, ib);
    uint32_t start = f->BeginAddress, off = uint32_t(a - ib - start);
    if (m->base == exe_base) {
        const char *name = kb_name(start);
        if (name)
            snprintf(outb + k, cap - k, " %s+0x%x", name, off);
        else
            snprintf(outb + k, cap - k, " fn base+0x%x+0x%x", start, off);
        return;
    }
    std::string owner = owners ? core_code_owner(ib + start) : std::string();
    if (!owner.empty())
        snprintf(outb + k, cap - k, " (%s+0x%x)", owner.c_str(), off);
    else
        snprintf(outb + k, cap - k, " fn +0x%x+0x%x", start, off);
}

template <class F> int walk(CONTEXT ctx, uintptr_t lo, uintptr_t hi, int max, F frame)
{
    int n = 0;
    for (int i = 0; i < max && ctx.Rip; i++) {
        if (!frame(n++, ctx))
            break;
        if (ctx.Rsp < lo || ctx.Rsp >= hi)
            break;
        DWORD64 ib = 0, before = ctx.Rsp;
        auto f = RtlLookupFunctionEntry(ctx.Rip, &ib, nullptr);
        if (f) {
            void *hd;
            DWORD64 ef;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, ib, ctx.Rip, f, &ctx, &hd, &ef, nullptr);
        } else {
            if (!readable(ctx.Rsp, 8))
                break;
            ctx.Rip = *reinterpret_cast<DWORD64 *>(ctx.Rsp);
            ctx.Rsp += 8;
        }
        if (ctx.Rsp < before)
            break;
    }
    return n;
}

void backtrace(CONTEXT ctx, bool skip_self)
{
    auto tib = reinterpret_cast<NT_TIB *>(NtCurrentTeb());
    auto lo = reinterpret_cast<uintptr_t>(tib->StackLimit), hi = reinterpret_cast<uintptr_t>(tib->StackBase);
    for (int i = 0, shown = 0; i < MAX_FRAMES && ctx.Rip; i++) {
        HMODULE m = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(ctx.Rip), &m);
        if (!skip_self || m != self)
            cl("[crash]   #%-2d %s", shown++, where(ctx.Rip).c_str());
        if (ctx.Rsp < lo || ctx.Rsp >= hi)
            break;
        DWORD64 ib = 0;
        auto f = RtlLookupFunctionEntry(ctx.Rip, &ib, nullptr);
        if (f) {
            void *hd;
            DWORD64 ef;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, ib, ctx.Rip, f, &ctx, &hd, &ef, nullptr);
        } else {
            if (!readable(ctx.Rsp, 8))
                break;
            ctx.Rip = *reinterpret_cast<DWORD64 *>(ctx.Rsp);
            ctx.Rsp += 8;
        }
    }
}

bool first_time(uintptr_t ip)
{
    LONG n = seen_count < MAX_SEEN ? seen_count : MAX_SEEN;
    for (LONG i = 0; i < n; i++)
        if (seen[i] == ip)
            return false;
    LONG i = InterlockedIncrement(&seen_count) - 1;
    if (i < MAX_SEEN)
        seen[i] = ip;
    else if (i == MAX_SEEN)
        cl("[crash] %ld distinct fault addresses seen; no further first-chance exceptions are logged", MAX_SEEN);
    return i < MAX_SEEN;
}

void report(const char *when, EXCEPTION_POINTERS *e)
{
    auto r = e->ExceptionRecord;
    auto c = e->ContextRecord;
    LONG n = InterlockedIncrement(&reports);
    cl("[crash] #%ld %s %s (0x%08lx) on thread %lu, %lu s after load, at %s", n, when, code_name(r->ExceptionCode),
             r->ExceptionCode, GetCurrentThreadId(), (GetTickCount() - start_tick) / 1000, where(c->Rip).c_str());
    if ((r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || r->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
        r->NumberParameters >= 2) {
        auto op = r->ExceptionInformation[0];
        cl("[crash]   %s 0x%llx", op == 0 ? "reading" : op == 1 ? "writing" : op == 8 ? "executing (DEP)" : "accessing",
                 (unsigned long long)r->ExceptionInformation[1]);
    }
    if (r->ExceptionCode == EXCEPTION_STACK_OVERFLOW)
        return;
    cl("[crash]   rax=%016llx rbx=%016llx rcx=%016llx rdx=%016llx", c->Rax, c->Rbx, c->Rcx, c->Rdx);
    cl("[crash]   rsi=%016llx rdi=%016llx rbp=%016llx rsp=%016llx", c->Rsi, c->Rdi, c->Rbp, c->Rsp);
    cl("[crash]   r8 =%016llx r9 =%016llx r10=%016llx r11=%016llx", c->R8, c->R9, c->R10, c->R11);
    cl("[crash]   r12=%016llx r13=%016llx r14=%016llx r15=%016llx", c->R12, c->R13, c->R14, c->R15);
    if (readable(c->Rip, 16)) {
        auto b = reinterpret_cast<const uint8_t *>(c->Rip);
        cl("[crash]   code: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X", b[0],
                 b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    }
    cl("[crash]   stack:");
    backtrace(*c, false);
}

bool enter()
{
    return InterlockedCompareExchange(&busy, (LONG)GetCurrentThreadId(), 0) == 0;
}

void leave()
{
    InterlockedExchange(&busy, 0);
}

void stack_bounds(uintptr_t &lo, uintptr_t &hi)
{
    auto tib = reinterpret_cast<NT_TIB *>(NtCurrentTeb());
    lo = reinterpret_cast<uintptr_t>(tib->StackLimit);
    hi = reinterpret_cast<uintptr_t>(tib->StackBase);
}

bool holds_loader_lock()
{
    auto teb = reinterpret_cast<uintptr_t>(NtCurrentTeb());
    uintptr_t peb = *reinterpret_cast<uintptr_t *>(teb + 0x60);
    if (!readable(peb + 0x110, 8))
        return false;
    uintptr_t cs = *reinterpret_cast<uintptr_t *>(peb + 0x110);
    return cs && readable(cs + 0x10, 8) &&
           *reinterpret_cast<uintptr_t *>(cs + 0x10) == uintptr_t(GetCurrentThreadId());
}

bool deliberate_abort(const EXCEPTION_RECORD *r, const CONTEXT *c)
{
    return r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2 &&
           r->ExceptionInformation[0] == 1 && r->ExceptionInformation[1] == 1 && readable(c->Rip, sizeof FATAL_ABORT) &&
           !memcmp(reinterpret_cast<const void *>(c->Rip), FATAL_ABORT, sizeof FATAL_ABORT);
}

void keep_fatal(EXCEPTION_POINTERS *e)
{
    InterlockedIncrement(&fatal_count);
    if (InterlockedCompareExchange(&snapshot_lock, 1, 0))
        return;
    last_fatal.rec = *e->ExceptionRecord;
    last_fatal.rec.ExceptionRecord = nullptr;
    memcpy(&last_fatal.ctx, e->ContextRecord, sizeof(CONTEXT));
    last_fatal.tid = GetCurrentThreadId();
    last_fatal.ms = GetTickCount64();
    stack_bounds(last_fatal.lo, last_fatal.hi);
    InterlockedExchange(&snapshot_lock, 0);
}

void write_report();

/* Standard library 1.6: the hook-point guard names the before or replace handler whose fault is about
   to crash the game; a report written on that thread soon after leads with it. */
char blame_text[320];
DWORD blame_tid;
ULONGLONG blame_ms;
constexpr ULONGLONG BLAME_MS = 10000;

bool request_report(Kind kind, const char *reason, const EXCEPTION_RECORD *rec, const CONTEXT *ctx)
{
    if (InterlockedCompareExchange(&claimed, 1, 0))
        return false;
    req.kind = kind;
    req.tid = GetCurrentThreadId();
    req.ms = GetTickCount64();
    stack_bounds(req.lo, req.hi);
    req.blame[0] = 0;
    if (blame_text[0] && blame_tid == req.tid && req.ms - blame_ms < BLAME_MS)
        snprintf(req.blame, sizeof req.blame, "%s", blame_text);
    if (req.blame[0])
        snprintf(req.reason, sizeof req.reason, "%s; %s", req.blame, reason);
    else
        snprintf(req.reason, sizeof req.reason, "%s", reason);
    req.has_exception = rec != nullptr;
    if (rec) {
        req.rec = *rec;
        req.rec.ExceptionRecord = nullptr;
    }
    memcpy(&req.ctx, ctx, sizeof(CONTEXT));
    req.nprobes = perf::open_probes(req.probes, 32);
    req.loader_lock = holds_loader_lock();
    req.has_prior = false;
    if (kind == K_EXIT && !InterlockedCompareExchange(&snapshot_lock, 1, 0)) {
        if (last_fatal.ms && req.ms - last_fatal.ms < PRIOR_MS) {
            req.prior = last_fatal;
            req.has_prior = true;
        }
        InterlockedExchange(&snapshot_lock, 0);
    }
    if (writer_tid && GetCurrentThreadId() != writer_tid) {
        SetEvent(go_event);
        HANDLE text[2] = {text_event, done_event};
        WaitForMultipleObjects(2, text, FALSE, TEXT_WAIT_MS);
        WaitForSingleObject(done_event, dump_mode == DUMP_FULL ? FULL_DUMP_WAIT_MS : DUMP_WAIT_MS);
    } else if (kind != K_EXCEPTION || req.rec.ExceptionCode != EXCEPTION_STACK_OVERFLOW) {
        write_report();
    }
    return true;
}

void already(const char *what)
{
    if (claimed && report_name[0])
        cl("[crash] %s; swofty\\%s already has this process's crash report", what, report_name);
}

bool writer_fault(EXCEPTION_POINTERS *e, bool breakpoint);

LONG CALLBACK veh(EXCEPTION_POINTERS *e)
{
    DWORD code = e->ExceptionRecord->ExceptionCode;
    if (writer_fault(e, false))
        return EXCEPTION_CONTINUE_EXECUTION;
    if (!fatal(code))
        return EXCEPTION_CONTINUE_SEARCH;
    keep_fatal(e);
    bool abort = deliberate_abort(e->ExceptionRecord, e->ContextRecord);
    bool certain = abort || code == 0xC0000374 || code == 0xC0000409 || code == EXCEPTION_STACK_OVERFLOW;
    if (!enter())
        return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t ip = e->ContextRecord->Rip;
    std::string at;
    if (first_time(ip)) {
        at = where(ip);
        if (reports < MAX_REPORTS)
            report("first-chance (the game may still handle it)", e);
        else
            cl("[crash] first-chance %s (0x%08lx) at %s; report cap reached, no details", code_name(code), code,
                     at.c_str());
        blackbox::note("exception", fmt("first-chance %s at ", code_name(code)).c_str(), at.c_str());
    }
    leave();
    if (certain) {
        if (code != EXCEPTION_STACK_OVERFLOW)
            runtime::hud_fatal();
        if (at.empty() && code != EXCEPTION_STACK_OVERFLOW)
            at = where(ip);
        std::string why = abort ? "the game aborted itself (Ne_FatalAbort writes 0xDEADDEAD to address 1) at " + at
                                : fmt("%s (0x%08lx) at ", code_name(code), code) + at;
        if (!request_report(abort ? K_FATAL : K_EXCEPTION, why.c_str(), e->ExceptionRecord, e->ContextRecord))
            already(abort ? "Ne_FatalAbort" : code_name(code));
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI unhandled(EXCEPTION_POINTERS *e)
{
    DWORD code = e->ExceptionRecord->ExceptionCode;
    if (enter()) {
        cl("[crash] unhandled %s (0x%08lx) at %s reached the top-level filter; %s", code_name(code), code,
                 where(e->ContextRecord->Rip).c_str(),
                 previous ? "passing it to the game's filter" : "the process ends here");
        if (!fatal(code) || first_time(e->ContextRecord->Rip))
            report("unhandled", e);
        leave();
    }
    std::string why = fmt("unhandled %s (0x%08lx)", code_name(code), code);
    auto r = e->ExceptionRecord;
    if ((code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR) && r->NumberParameters >= 2)
        why += fmt(" %s 0x%llx",
                   r->ExceptionInformation[0] == 1   ? "writing"
                   : r->ExceptionInformation[0] == 8 ? "executing"
                                                     : "reading",
                   (unsigned long long)r->ExceptionInformation[1]);
    why += " at " + where(e->ContextRecord->Rip);
    if (!request_report(K_EXCEPTION, why.c_str(), r, e->ContextRecord))
        already("unhandled exception");
    return previous ? previous(e) : EXCEPTION_CONTINUE_SEARCH;
}

void window_stage(int)
{
    InterlockedExchange(&window_reached, 1);
}

void on_exit(const char *fn, UINT code)
{
    perf::flush(fn);
    runtime::netplay_exit();
    if (InterlockedExchange(&exited, 1) || !enter())
        return;
    bool early = !window_reached, stack = code || early;
    cl("[crash] %s(%u) on thread %lu, %lu s after load%s%s", fn, code, GetCurrentThreadId(),
             (GetTickCount() - start_tick) / 1000, early ? ", before the game's window" : "",
             stack ? "; caller stack:" : "");
    blackbox::note("exit", fmt("%s(%u)", fn, code).c_str());
    alignas(16) CONTEXT ctx;
    RtlCaptureContext(&ctx);
    if (stack)
        backtrace(ctx, true);
    leave();
    ULONGLONG now = GetTickCount64(), since = last_fatal.ms ? now - last_fatal.ms : ~0ull;
    if (code || since < EXIT_AFTER_FATAL_MS) {
        std::string why = fmt("%s(%u = 0x%x) on thread %lu", fn, code, code, GetCurrentThreadId());
        if (!code)
            why += fmt(", %.1f s after a fatal-class exception", double(since) / 1000);
        if (!request_report(K_EXIT, why.c_str(), nullptr, &ctx))
            already(fn);
    }
}

void WINAPI hooked_exit(UINT code)
{
    on_exit("ExitProcess", code);
    exit_hook.unsafe_call<void>(code);
}

BOOL WINAPI hooked_terminate(HANDLE process, UINT code)
{
    if (GetProcessId(process) == GetCurrentProcessId())
        on_exit("TerminateProcess", code);
    return terminate_hook.unsafe_call<BOOL>(process, code);
}

LONG NTAPI hooked_nt_terminate(HANDLE process, LONG status)
{
    if (!exited && (!process || GetProcessId(process) == GetCurrentProcessId()))
        on_exit(process ? "NtTerminateProcess" : "NtTerminateProcess(other threads)", UINT(status));
    return nt_terminate_hook.unsafe_call<LONG>(process, status);
}

bool code_at(uintptr_t a)
{
    const Mod *m = mod_at(a);
    if (!m)
        return false;
    auto s = section_of(m->base, a);
    return s && (s->Characteristics & IMAGE_SCN_MEM_EXECUTE);
}

bool heap_vtable(uintptr_t vt)
{
    if (vt - exe_base >= exe_size || !readable(vt + 0x118, 8))
        return false;
    uintptr_t fn = *reinterpret_cast<uintptr_t *>(vt + 0x118);
    if (heap_error_fn)
        return fn == heap_error_fn;
    return fn - exe_base < exe_size && readable(fn, sizeof HEAP_ERROR_PREFIX) &&
           !memcmp(reinterpret_cast<const void *>(fn), HEAP_ERROR_PREFIX, sizeof HEAP_ERROR_PREFIX);
}

bool ne_heap(uintptr_t obj, neheap::Heap &h)
{
    if (obj < 0x10000 || (obj & 7) || !readable(obj, 0x118))
        return false;
    Reader rd;
    return heap_vtable(*reinterpret_cast<uintptr_t *>(obj)) && neheap::decode(rd, obj, h);
}

void clean(char *s)
{
    for (; *s; s++)
        if (*s == '\r' || *s == '\n' || *s == '\t')
            *s = ' ';
}

void on_heap_error(int code, const char *msg, bool is_fatal, void *heap)
{
    HeapError &h = heap_error;
    h = {};
    h.code = code;
    h.fatal = is_fatal;
    h.tid = GetCurrentThreadId();
    h.ms = GetTickCount64();
    h.heap = reinterpret_cast<uintptr_t>(heap);
    if (msg && readable(reinterpret_cast<uintptr_t>(msg), 1)) {
        size_t n = strnlen(msg, sizeof h.msg - 1);
        memcpy(h.msg, msg, n);
        h.msg[n] = 0;
        clean(h.msg);
    }
    neheap::Heap info;
    if (ne_heap(h.heap, info)) {
        snprintf(h.name, sizeof h.name, "%s", info.name);
        auto vt = *reinterpret_cast<void ***>(heap);
        using Stat = uint64_t (*)(void *);
        using Largest = uint64_t (*)(void *, int);
        h.capacity = reinterpret_cast<Stat>(vt[0x60 / 8])(heap);
        h.free_bytes = reinterpret_cast<Stat>(vt[0x68 / 8])(heap);
        h.used = reinterpret_cast<Stat>(vt[0x70 / 8])(heap);
        h.largest = reinterpret_cast<Largest>(vt[0x80 / 8])(heap, 1);
        h.stats = true;
    }
    h.set = true;
    char line[blackbox::TEXT];
    snprintf(line, sizeof line, "0x%x%s in '%s': %s", code, is_fatal ? " (fatal)" : "", h.name, h.msg);
    blackbox::note("heap", line);
    cl("[crash] Ne heap error 0x%x (%s) on thread %lu: %s; heap %p '%s'%s", code, is_fatal ? "fatal" : "not fatal",
       h.tid, h.msg, heap, h.name,
       h.stats ? fmt(", capacity %llu, used %llu, free %llu, largest free %llu", (unsigned long long)h.capacity,
                     (unsigned long long)h.used, (unsigned long long)h.free_bytes, (unsigned long long)h.largest)
                     .c_str()
               : "");
    if (!is_fatal)
        return;
    alignas(16) CONTEXT ctx;
    RtlCaptureContext(&ctx);
    std::string why = fmt("fatal Ne heap error 0x%x in heap '%s': %s", code, h.name, h.msg);
    if (!request_report(K_FATAL, why.c_str(), nullptr, &ctx))
        already("fatal Ne heap error");
}

int hooked_report_memory_error(int code, const char *msg, char is_fatal, char brk, uint64_t a5, void *heap)
{
    on_heap_error(code, msg, is_fatal != 0, heap);
    return heap_error_hook.unsafe_call<int>(code, msg, is_fatal, brk, a5, heap);
}

void load_symbols(const std::wstring &path, DWORD stamp)
{
    FILE *f = _wfopen(path.c_str(), L"rb");
    if (!f)
        return cl("[crash] symbols.tsv missing; exe frames show rva only");
    char line[512];
    bool match = false;
    while (fgets(line, sizeof line, f)) {
        char name[256];
        unsigned rva;
        unsigned long ts;
        if (line[0] == '#') {
            match = sscanf(line, "# %*s %*s %lu", &ts) == 1 && ts == stamp;
            symbols_match |= match;
        } else if (match && sscanf(line, "%x\t%255[^\t\r\n]", &rva, name) == 2) {
            symbols.push_back({rva, name});
        }
    }
    fclose(f);
    std::sort(symbols.begin(), symbols.end(), [](const Symbol &a, const Symbol &b) { return a.rva < b.rva; });
    cl(match ? "[crash] symbols.tsv: %zu KB function name(s) for this exe"
                   : "[crash] symbols.tsv is for another build (%zu names loaded); exe frames show rva only",
             symbols.size());
}

void local_stamp(char *b, size_t n, bool file_name)
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    snprintf(b, n, file_name ? "%04u%02u%02u-%02u%02u%02u" : "%04u-%02u-%02u %02u:%02u:%02u.%03u", t.wYear, t.wMonth,
             t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
}

void hex_dump(uintptr_t from, size_t n, uintptr_t mark)
{
    Reader rd;
    for (uintptr_t a = from & ~0xfull; a < from + n; a += 16) {
        uint8_t row[16];
        bool ok = rd.read(a, row, 16);
        out.put("    %016llx%s", (unsigned long long)a, a <= mark && mark < a + 16 ? ">" : " ");
        for (int i = 0; i < 16; i++)
            ok ? out.put(" %02x", row[i]) : out.put(" ??");
        out.put("  ");
        for (int i = 0; i < 16; i++)
            out.put("%c", ok && row[i] >= 0x20 && row[i] < 0x7f ? row[i] : '.');
        out.put("\n");
    }
}

const char *protect_name(DWORD p)
{
    switch (p & 0xff) {
    case PAGE_NOACCESS: return "no access";
    case PAGE_READONLY: return "read";
    case PAGE_READWRITE: return "read/write";
    case PAGE_WRITECOPY: return "write-copy";
    case PAGE_EXECUTE: return "execute";
    case PAGE_EXECUTE_READ: return "execute/read";
    case PAGE_EXECUTE_READWRITE: return "execute/read/write";
    case PAGE_EXECUTE_WRITECOPY: return "execute/write-copy";
    }
    return "?";
}

neheap::Heap heaps[8];
int nheaps;

void add_heap(uintptr_t obj)
{
    for (int i = 0; i < nheaps; i++)
        if (heaps[i].obj == obj)
            return;
    neheap::Heap h;
    if (nheaps < 8 && ne_heap(obj, h))
        heaps[nheaps++] = h;
}

const neheap::Heap *heap_for(uintptr_t a)
{
    for (int i = 0; i < nheaps; i++)
        if (heaps[i].contains(a))
            return &heaps[i];
    return nullptr;
}

void region(uintptr_t a, const Request &r)
{
    MEMORY_BASIC_INFORMATION m{};
    if (!VirtualQuery(reinterpret_cast<void *>(a), &m, sizeof m)) {
        out.put("not a valid address");
        return;
    }
    auto base = reinterpret_cast<uintptr_t>(m.BaseAddress);
    if (m.State == MEM_FREE) {
        out.put("free (unallocated) region 0x%llx +0x%llx", (unsigned long long)base, (unsigned long long)m.RegionSize);
        return;
    }
    out.put("%s %s region 0x%llx +0x%llx (allocation 0x%llx), %s", m.State == MEM_RESERVE ? "reserved" : "committed",
            m.Type == MEM_IMAGE ? "image" : m.Type == MEM_MAPPED ? "mapped" : "private", (unsigned long long)base,
            (unsigned long long)m.RegionSize, (unsigned long long)reinterpret_cast<uintptr_t>(m.AllocationBase),
            m.State == MEM_COMMIT ? protect_name(m.Protect) : "no access");
    if (m.Protect & PAGE_GUARD)
        out.put(", guard page");
    if (const Mod *mod = mod_at(a)) {
        char sec[9];
        section_into(mod->base, a, sec);
        out.put("; %s+0x%llx %s", mod->name, (unsigned long long)(a - mod->base), sec);
    }
    if (a >= r.lo && a < r.hi)
        out.put("; stack of thread %lu (rsp%+lld)", r.tid, (long long)(a - r.ctx.Rsp));
    if (const neheap::Heap *h = heap_for(a))
        out.put("; Ne heap '%s' +0x%llx", h->name, (unsigned long long)(a - h->start));
}

void block_line(const char *label, const neheap::Block &b)
{
    auto g = [](int s) { return s < 0 ? "n/a" : s ? "intact" : "CORRUPT"; };
    out.put("    %-9s header 0x%llx: list prev %+d next %+d (x16), block 0x%x bytes, user 0x%x bytes at 0x%llx, start "
            "guard %s, end guard %s\n",
            label, (unsigned long long)b.header, b.prev_rel, b.next_rel, b.block_size, b.user_size,
            (unsigned long long)b.user, g(b.start_guard), g(b.end_guard));
}

void heap_block(const neheap::Heap &h, uintptr_t a, const char *why, bool as_user)
{
    Reader rd;
    neheap::Block b;
    out.put("  %s 0x%llx in heap '%s' (+0x%llx):\n", why, (unsigned long long)a, h.name,
            (unsigned long long)(a - h.start));
    bool found = false;
    if (as_user) {
        uintptr_t header = a - h.guard - 0x10;
        if (neheap::block_at(rd, h, header, b)) {
            block_line("block", b);
            found = true;
        } else if (neheap::raw_header(rd, h, header, b)) {
            out.put("    header at 0x%llx does not match a block of this heap (block size 0x%x, user size 0x%x: a block "
                    "of that user size is 0x%x bytes):\n",
                    (unsigned long long)header, b.block_size, b.user_size, neheap::block_size_for(b.user_size, h.guard));
            hex_dump(header - 0x20, 0x60, header);
        }
        if (!found) {
            neheap::Block p;
            if (neheap::containing(rd, h, header - 1, p, 1 << 18)) {
                block_line("previous", p);
                out.put("    the previous block's user data ends 0x%llx bytes before this header%s\n",
                        (unsigned long long)(header - (p.user + p.user_size)),
                        p.end_guard == 0 ? "; its end guard is overwritten, so a write ran past its end towards this header"
                                         : "");
                uintptr_t end = p.user + p.user_size;
                hex_dump(end >= p.user + 0x20 ? end - 0x20 : p.user, 0x60, end);
            }
            return;
        }
    } else if (neheap::containing(rd, h, a, b, 1 << 18)) {
        block_line("block", b);
        if (a < b.header + 0x10)
            out.put("    0x%llx is in the block header\n", (unsigned long long)a);
        else if (a < b.user)
            out.put("    0x%llx is in the start guard\n", (unsigned long long)a);
        else if (a < b.user + b.user_size)
            out.put("    0x%llx is user data +0x%llx\n", (unsigned long long)a, (unsigned long long)(a - b.user));
        else
            out.put("    0x%llx is past the user data (end guard or padding)\n", (unsigned long long)a);
        found = true;
    }
    if (!found) {
        out.put("    no allocated block header found within 256 KB before it (free space or a corrupt header)\n");
        return;
    }
    neheap::Block p, n;
    if (neheap::before(rd, h, b.header, p, 1 << 18))
        block_line("previous", p);
    else
        out.put("    previous  no allocated block ends at this header (free space, or a corrupt header before it)\n");
    if (neheap::block_at(rd, h, b.end(), n))
        block_line("next", n);
    else if (b.end() < h.start + h.size) {
        out.put("    next      0x%llx is not an allocated block header (free space or corrupt):\n",
                (unsigned long long)b.end());
        hex_dump(b.end(), 0x20, b.end());
    }
}

void scan_message_pointers(const char *msg, uintptr_t *found, int &n, int max)
{
    for (const char *p = msg; *p && n < max; p++) {
        const char *s = p;
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
            s += 2;
        int digits = 0;
        uint64_t v = 0;
        while (digits < 17 && isxdigit((unsigned char)s[digits])) {
            char c = s[digits];
            v = v * 16 + uint64_t(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
            digits++;
        }
        if (digits >= 8 && digits <= 16) {
            found[n++] = uintptr_t(v);
            p = s + digits - 1;
        }
    }
}

const char *reg_names[] = {"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
                           "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15"};

uint64_t reg(const CONTEXT &c, int i)
{
    return (&c.Rax)[i];
}

void section_registers(const Request &r)
{
    const CONTEXT &c = r.ctx;
    out.put("\n== Registers\n");
    out.put("  rax=%016llx rbx=%016llx rcx=%016llx rdx=%016llx\n", c.Rax, c.Rbx, c.Rcx, c.Rdx);
    out.put("  rsi=%016llx rdi=%016llx rbp=%016llx rsp=%016llx\n", c.Rsi, c.Rdi, c.Rbp, c.Rsp);
    out.put("  r8 =%016llx r9 =%016llx r10=%016llx r11=%016llx\n", c.R8, c.R9, c.R10, c.R11);
    out.put("  r12=%016llx r13=%016llx r14=%016llx r15=%016llx\n", c.R12, c.R13, c.R14, c.R15);
    out.put("  rip=%016llx eflags=%08lx cs=%04x ss=%04x ds=%04x es=%04x fs=%04x gs=%04x mxcsr=%08lx\n", c.Rip, c.EFlags,
            c.SegCs, c.SegSs, c.SegDs, c.SegEs, c.SegFs, c.SegGs, c.MxCsr);
    const M128A *x = &c.Xmm0;
    for (int i = 0; i < 16; i += 2)
        out.put("  xmm%-2d=%016llx%016llx xmm%-2d=%016llx%016llx\n", i, (unsigned long long)x[i].High,
                (unsigned long long)x[i].Low, i + 1, (unsigned long long)x[i + 1].High, (unsigned long long)x[i + 1].Low);
}

bool call_before(uintptr_t v)
{
    uint8_t b[8];
    Reader rd;
    if (!rd.read(v - 8, b, 8))
        return false;
    if (b[3] == 0xE8)
        return true;
    for (int len : {2, 3, 4, 6, 7})
        if (b[8 - len] == 0xFF && ((b[8 - len + 1] >> 3) & 7) == 2)
            return true;
    return false;
}

void section_stack(const CONTEXT &ctx, uintptr_t lo, uintptr_t hi, const char *title)
{
    out.put("\n== %s\n", title);
    char w[512];
    int frames = walk(ctx, lo, hi, REPORT_FRAMES, [&](int i, const CONTEXT &c) {
        where_into(w, sizeof w, c.Rip, true);
        out.put("  #%-2d %016llx rsp=%016llx  %s\n", i, (unsigned long long)c.Rip, (unsigned long long)c.Rsp, w);
        return true;
    });
    if (frames <= 1)
        out.put("  (the unwind stopped at the first frame)\n");
    out.put("\n== Return addresses found on the stack (rsp to +0x%zx)\n", STACK_SCAN);
    uintptr_t end = std::min<uintptr_t>(hi, ctx.Rsp + STACK_SCAN);
    int shown = 0;
    Reader rd;
    for (uintptr_t p = ctx.Rsp & ~7ull; p + 8 <= end && shown < SCAN_FRAMES; p += 8) {
        uint64_t v;
        if (!rd.read(p, &v, 8) || !code_at(v) || !call_before(v))
            continue;
        where_into(w, sizeof w, v, true);
        out.put("  rsp+0x%-5llx %016llx  %s\n", (unsigned long long)(p - ctx.Rsp), (unsigned long long)v, w);
        shown++;
    }
    if (!shown)
        out.put("  none\n");
}

void section_memory(const Request &r)
{
    const CONTEXT &c = r.ctx;
    out.put("\n== Code around rip\n");
    hex_dump(c.Rip - 64, 128, c.Rip);
    out.put("\n== Memory at the registers\n");
    uintptr_t done[20];
    int ndone = 0;
    for (int i = 0; i < 16; i++) {
        uint64_t v = reg(c, i);
        if (v < 0x10000)
            continue;
        out.put("  %s=%016llx: ", reg_names[i], (unsigned long long)v);
        region(v, r);
        out.put("\n");
        bool dup = false;
        for (int k = 0; k < ndone; k++)
            dup |= v - done[k] + 64 < 192;
        if (dup || !readable(v, 1))
            continue;
        done[ndone++] = v;
        uintptr_t from = readable(v - 64, 64) ? v - 64 : v;
        hex_dump(from, i == 4 ? 256 : v - from + 128, v);
    }
}

void section_heap(const Request &r)
{
    if (heap_error.set)
        add_heap(heap_error.heap);
    for (int i = 0; i < 16; i++)
        add_heap(reg(r.ctx, i));
    Reader rd;
    for (uintptr_t p = r.ctx.Rsp & ~7ull; p < r.ctx.Rsp + 0x800 && p + 8 <= r.hi; p += 8) {
        uint64_t v;
        if (rd.read(p, &v, 8))
            add_heap(v);
    }
    if (!heap_error.set && !nheaps)
        return;
    out.put("\n== Ne heap\n");
    if (heap_error.set) {
        const HeapError &h = heap_error;
        out.put("  last heap error: 0x%x (%s) on thread %lu, %.1f s before this report: %s\n", h.code,
                h.fatal ? "fatal" : "not fatal", h.tid, double(r.ms - h.ms) / 1000, h.msg);
        if (h.stats)
            out.put("  heap %p '%s': capacity %llu, used %llu, free %llu, largest free %llu bytes\n",
                    reinterpret_cast<void *>(h.heap), h.name, (unsigned long long)h.capacity, (unsigned long long)h.used,
                    (unsigned long long)h.free_bytes, (unsigned long long)h.largest);
    }
    for (int i = 0; i < nheaps; i++) {
        const neheap::Heap &h = heaps[i];
        out.put("  heap object 0x%llx '%s' (vtable %s+0x%llx): range 0x%llx +0x%llx, guard %llu bytes, allocated "
                "%llu bytes, allocated list head 0x%llx\n",
                (unsigned long long)h.obj, h.name, mod_at(exe_base) ? mod_at(exe_base)->name : "exe",
                (unsigned long long)(h.vtable - exe_base), (unsigned long long)h.start, (unsigned long long)h.size,
                (unsigned long long)h.guard, (unsigned long long)h.allocated, (unsigned long long)h.list);
    }
    uintptr_t from_msg[8];
    int nmsg = 0;
    if (heap_error.set)
        scan_message_pointers(heap_error.msg, from_msg, nmsg, 8);
    for (int i = 0; i < nmsg; i++)
        if (const neheap::Heap *h = heap_for(from_msg[i]))
            heap_block(*h, from_msg[i], "pointer from the error message", true);
    if (r.has_exception && r.rec.NumberParameters >= 2 &&
        (r.rec.ExceptionCode == EXCEPTION_ACCESS_VIOLATION || r.rec.ExceptionCode == EXCEPTION_IN_PAGE_ERROR))
        if (const neheap::Heap *h = heap_for(r.rec.ExceptionInformation[1]))
            heap_block(*h, r.rec.ExceptionInformation[1], "faulting address", false);
    for (int i = 0; i < 16; i++) {
        uint64_t v = reg(r.ctx, i);
        if (i == 4)
            continue;
        if (const neheap::Heap *h = heap_for(v)) {
            char why[32];
            snprintf(why, sizeof why, "register %s", reg_names[i]);
            heap_block(*h, v, why, false);
        }
    }
}

void section_exception(const Request &r)
{
    if (!r.has_exception)
        return;
    const EXCEPTION_RECORD &e = r.rec;
    char w[512];
    where_into(w, sizeof w, reinterpret_cast<uintptr_t>(e.ExceptionAddress), true);
    out.put("\n== Exception\n  %s (0x%08lx), flags 0x%lx, at %s\n", code_name(e.ExceptionCode), e.ExceptionCode,
            e.ExceptionFlags, w);
    for (DWORD i = 0; i < e.NumberParameters && i < EXCEPTION_MAXIMUM_PARAMETERS; i++)
        out.put("  parameter %lu: 0x%llx\n", i, (unsigned long long)e.ExceptionInformation[i]);
    if ((e.ExceptionCode == EXCEPTION_ACCESS_VIOLATION || e.ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
        e.NumberParameters >= 2) {
        auto op = e.ExceptionInformation[0];
        out.put("  %s 0x%llx: ", op == 0 ? "reading" : op == 1 ? "writing" : op == 8 ? "executing (DEP)" : "accessing",
                (unsigned long long)e.ExceptionInformation[1]);
        region(e.ExceptionInformation[1], r);
        out.put("\n");
    }
}

void section_state(const Request &r)
{
    out.put("\n== Game state (from the black box)\n");
    blackbox::Copy c;
    for (const char *k : {"flow", "screen", "session", "race", "career", "heap", "exception"}) {
        if (events.last(k, c))
            out.put("  last %-9s %s (%.1f s before this report)\n", k, c.text, double(r.ms - c.ms) / 1000);
        else
            out.put("  last %-9s none recorded\n", k);
    }
    out.put("  track and car count: core does not read them\n");
}

void section_blackbox(const Request &r)
{
    out.put("\n== Black box: last events (%llu recorded, oldest first)\n", (unsigned long long)events.total());
    if (!events.each([&](const blackbox::Copy &c) {
            out.put("  -%9.3f s [%5lu] %-9s %s\n", double(r.ms - c.ms) / 1000, (unsigned long)c.tid, c.kind, c.text);
        }))
        out.put("  none\n");
    out.put("\n== Black box: last game log warnings and errors (%llu recorded, oldest first)\n",
            (unsigned long long)warnings.total());
    if (!warnings.each([&](const blackbox::Copy &c) {
            out.put("  -%9.3f s [%5lu] %s\n", double(r.ms - c.ms) / 1000, (unsigned long)c.tid, c.text);
        }))
        out.put("  none\n");
}

void section_probes(const Request &r)
{
    out.put("\n== Swofty probes (hook and callback activity)\n");
    static char text[48 << 10];
    text[0] = 0;
    perf::crash_text(text, sizeof text, r.probes, r.nprobes);
    for (char *line = text; *line;) {
        char *nl = strchr(line, '\n');
        size_t len = nl ? size_t(nl - line) : strlen(line);
        out.put("  %.*s\n", int(len), line);
        line += len + (nl ? 1 : 0);
    }
}

void section_modules()
{
    out.put("\n== Addons (swofty\\settings.json)\n%s", addon_text.c_str());
    out.put("\n== Loaded modules (%d)\n", nmods);
    for (int i = 0; i < nmods; i++) {
        const Mod &m = mods[i];
        uint32_t stamp = 0;
        auto dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(m.base);
        if (readable(m.base, sizeof *dos) && readable(m.base + dos->e_lfanew, sizeof(IMAGE_NT_HEADERS)))
            stamp = reinterpret_cast<const IMAGE_NT_HEADERS *>(m.base + dos->e_lfanew)->FileHeader.TimeDateStamp;
        out.put("  %016llx %10llx  stamp %08x  %s\n", (unsigned long long)m.base, (unsigned long long)m.size, stamp,
                m.path[0] ? m.path : m.name);
    }
}

/* Each thread is suspended only while its context is read and its first frames are named; nothing in
   that window allocates, since a suspended thread may hold the heap lock. */
void section_threads(const Request &r)
{
    out.put("\n== Threads\n");
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        out.put("  the thread list is not available (error %lu)\n", GetLastError());
        return;
    }
    THREADENTRY32 te{};
    te.dwSize = sizeof te;
    DWORD pid = GetCurrentProcessId();
    int count = 0;
    static char frames[THREAD_FRAMES][320];
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid)
            continue;
        count++;
        DWORD tid = te.th32ThreadID;
        char desc[96] = "";
        HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid);
        if (h && thread_description) {
            PWSTR d = nullptr;
            if (SUCCEEDED(thread_description(h, &d)) && d) {
                WideCharToMultiByte(CP_UTF8, 0, d, -1, desc, sizeof desc - 1, nullptr, nullptr);
                LocalFree(d);
            }
        }
        out.put("  thread %lu, priority %ld%s%s%s%s\n", tid, te.tpBasePri, desc[0] ? " \"" : "", desc, desc[0] ? "\"" : "",
                tid == r.tid                         ? " (the reporting thread, see the stack above)"
                : tid == GetCurrentThreadId()        ? " (the crash report writer)"
                : r.has_prior && tid == r.prior.tid ? " (the thread of the earlier exception, see above)"
                                                     : "");
        if (!h || tid == r.tid || tid == GetCurrentThreadId()) {
            if (h)
                CloseHandle(h);
            continue;
        }
        int n = 0;
        alignas(16) CONTEXT ctx{};
        ctx.ContextFlags = CONTEXT_FULL;
        if (SuspendThread(h) != DWORD(-1)) {
            suspended = h;
            if (GetThreadContext(h, &ctx)) {
                uintptr_t lo = 0, hi = 0;
                struct {
                    LONG status;
                    void *teb;
                    uintptr_t pid, tid;
                    uintptr_t affinity;
                    LONG priority, base;
                } tbi{};
                if (query_thread && query_thread(h, 0, &tbi, sizeof tbi, nullptr) >= 0 && tbi.teb &&
                    readable(reinterpret_cast<uintptr_t>(tbi.teb), sizeof(NT_TIB))) {
                    auto tib = static_cast<NT_TIB *>(tbi.teb);
                    lo = reinterpret_cast<uintptr_t>(tib->StackLimit);
                    hi = reinterpret_cast<uintptr_t>(tib->StackBase);
                }
                n = walk(ctx, lo, hi ? hi : ctx.Rsp + 1, THREAD_FRAMES, [&](int i, const CONTEXT &c) {
                    where_into(frames[i], sizeof frames[i], c.Rip, false);
                    return true;
                });
            }
            suspended = nullptr;
            ResumeThread(h);
        }
        CloseHandle(h);
        for (int i = 0; i < n; i++)
            out.put("      #%d %s\n", i, frames[i]);
    }
    CloseHandle(snap);
    out.put("  %d thread(s)\n", count);
}

void section_log()
{
    out.put("\n== loader.log (last %d lines)\n", LOG_LINES);
    HANDLE f = CreateFileW((swofty_dir + L"loader.log").c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        out.put("  loader.log could not be opened (error %lu)\n", GetLastError());
        return;
    }
    static char tail[LOG_TAIL + 1];
    LARGE_INTEGER size{}, at{};
    GetFileSizeEx(f, &size);
    at.QuadPart = size.QuadPart > LONGLONG(LOG_TAIL) ? size.QuadPart - LONGLONG(LOG_TAIL) : 0;
    SetFilePointerEx(f, at, nullptr, FILE_BEGIN);
    DWORD got = 0;
    ReadFile(f, tail, DWORD(LOG_TAIL), &got, nullptr);
    CloseHandle(f);
    tail[got] = 0;
    int lines = 0;
    char *start = tail + got;
    if (start > tail && start[-1] == '\n')
        start--;
    while (start > tail && lines < LOG_LINES)
        if (*--start == '\n')
            lines++;
    if (*start == '\n')
        start++;
    char *w = start;
    for (char *p = start; *p; p++)
        if (*p != '\r')
            *w++ = *p;
    *w = 0;
    out.put("%s%s", start, got && tail[got - 1] != '\n' ? "\n" : "");
}

struct DumpBudget {
    DWORD tid;
    uint64_t stacks, cap;
    int dropped;
};

BOOL CALLBACK dump_callback(PVOID param, PMINIDUMP_CALLBACK_INPUT in, PMINIDUMP_CALLBACK_OUTPUT o)
{
    auto b = static_cast<DumpBudget *>(param);
    if (!in || !o)
        return TRUE;
    if (in->CallbackType == ThreadCallback && b->cap) {
        uint64_t size = in->Thread.StackEnd > in->Thread.StackBase ? in->Thread.StackEnd - in->Thread.StackBase : 0;
        if (in->Thread.ThreadId != b->tid && b->stacks + size > b->cap) {
            o->ThreadWriteFlags &= ~ULONG(ThreadWriteStack);
            b->dropped++;
        } else {
            b->stacks += size;
        }
    }
    return TRUE;
}

void minidump(Request &r)
{
    if (dump_mode == DUMP_OFF) {
        snprintf(dump_note, sizeof dump_note, "not written (SWOFTY_CRASH_DUMP=off)");
        return;
    }
    if (!write_dump) {
        snprintf(dump_note, sizeof dump_note, "not written: dbghelp.dll has no MiniDumpWriteDump");
        return;
    }
    if (r.loader_lock) {
        snprintf(dump_note, sizeof dump_note,
                 "not written: the reporting thread holds the loader lock, which the dump writer's thread needs");
        return;
    }
    HANDLE f = CreateFileW(dump_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    dump_file = f;
    if (f == INVALID_HANDLE_VALUE) {
        snprintf(dump_note, sizeof dump_note, "not written: cannot create the file (error %lu)", GetLastError());
        return;
    }
    EXCEPTION_RECORD rec{};
    alignas(16) CONTEXT ctx = r.ctx;
    DWORD tid = r.tid;
    if (r.has_exception) {
        rec = r.rec;
    } else if (r.has_prior) {
        rec = r.prior.rec;
        ctx = r.prior.ctx;
        tid = r.prior.tid;
    } else {
        rec.ExceptionCode = r.kind == K_EXIT ? CODE_EXIT : CODE_GAME_FATAL;
        rec.ExceptionAddress = reinterpret_cast<void *>(ctx.Rip);
        rec.NumberParameters = 1;
        rec.ExceptionInformation[0] = r.kind == K_FATAL && heap_error.set ? ULONG_PTR(heap_error.code) : 0;
    }
    EXCEPTION_POINTERS ptrs{&rec, &ctx};
    MINIDUMP_EXCEPTION_INFORMATION ex{tid, &ptrs, FALSE};
    MINIDUMP_USER_STREAM comment{CommentStreamA, ULONG(out.kept ? out.n : 0), out.b};
    MINIDUMP_USER_STREAM_INFORMATION streams{comment.BufferSize ? 1ul : 0ul, &comment};
    DumpBudget budget{tid, 0, dump_mode == DUMP_FULL ? 0 : DUMP_STACKS, 0};
    MINIDUMP_CALLBACK_INFORMATION cb{dump_callback, &budget};
    auto type = MINIDUMP_TYPE(MiniDumpWithThreadInfo | MiniDumpWithProcessThreadData | MiniDumpWithUnloadedModules |
                              MiniDumpWithFullMemoryInfo |
                              (dump_mode == DUMP_FULL ? MiniDumpWithFullMemory | MiniDumpWithHandleData
                                                      : MiniDumpWithIndirectlyReferencedMemory));
    DWORD t0 = GetTickCount();
    BOOL ok = write_dump(GetCurrentProcess(), GetCurrentProcessId(), f, type, &ex, &streams, &cb);
    DWORD err = ok ? 0 : GetLastError();
    LARGE_INTEGER size{};
    GetFileSizeEx(f, &size);
    CloseHandle(f);
    dump_file = INVALID_HANDLE_VALUE;
    if (!ok) {
        DeleteFileW(dump_path);
        snprintf(dump_note, sizeof dump_note, "MiniDumpWriteDump failed (error %lu)", err);
        return;
    }
    char name[64];
    WideCharToMultiByte(CP_UTF8, 0, wcsrchr(dump_path, L'\\') + 1, -1, name, sizeof name, nullptr, nullptr);
    snprintf(dump_note, sizeof dump_note, "%s, %.1f MB, %s, written in %.1f s%s", name,
             double(size.QuadPart) / (1 << 20), dump_mode == DUMP_FULL ? "all memory" : "thread stacks and registers",
             double(GetTickCount() - t0) / 1000,
             budget.dropped ? fmt("; %d thread stack(s) left out past the %llu MB budget", budget.dropped,
                                  (unsigned long long)(DUMP_STACKS >> 20))
                                  .c_str()
                            : "");
}

void report_names()
{
    char stamp[32];
    local_stamp(stamp, sizeof stamp, true);
    for (int i = 0; i < 100; i++) {
        snprintf(report_name, sizeof report_name - 4, i ? "crash-%s-%d" : "crash-%s", stamp, i + 1);
        wchar_t wname[64];
        MultiByteToWideChar(CP_UTF8, 0, report_name, -1, wname, 64);
        swprintf(report_path, MAX_PATH, L"%ls%ls.txt", swofty_dir.c_str(), wname);
        swprintf(dump_path, MAX_PATH, L"%ls%ls.dmp", swofty_dir.c_str(), wname);
        out.file = CreateFileW(report_path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                               nullptr);
        if (out.file != INVALID_HANDLE_VALUE || GetLastError() != ERROR_FILE_EXISTS)
            break;
    }
    strcat(report_name, ".txt");
}

void summary_line(const Request &r)
{
    bool prior = r.has_prior && !r.has_exception;
    std::string frames;
    char w[512];
    int kept = 0;
    walk(prior ? r.prior.ctx : r.ctx, prior ? r.prior.lo : r.lo, prior ? r.prior.hi : r.hi, 24,
         [&](int, const CONTEXT &c) {
             const Mod *m = mod_at(c.Rip);
             if (m && m->base == reinterpret_cast<uintptr_t>(self))
                 return true;
             where_into(w, sizeof w, c.Rip, false);
             frames += (frames.empty() ? "" : " < ") + std::string(w);
             return ++kept < 4;
         });
    cl("[crash] crash report swofty\\%s: %s; %sstack %s; minidump %s", report_name, r.reason,
       prior ? "earlier exception's " : "", frames.c_str(), dump_note);
}

void step_header(Request &r)
{
    char now[32];
    local_stamp(now, sizeof now, false);
    if (r.blame[0])
        out.put("Swofty crash report: %s\n\n", r.blame);
    else
        out.put("Swofty crash report\n\n");
    out.put("written      %s local time\n", now);
    out.put("reason       %s\n", r.reason);
    out.put("thread       %lu%s\n", r.tid, r.kind == K_EXIT ? " (the exiting thread)" : "");
    out.put("process      pid %lu, %.1f s after the core loaded\n", GetCurrentProcessId(),
            double(r.ms - start_ms) / 1000);
    wchar_t path[MAX_PATH] = L"?";
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    unsigned long long size = 0;
    if (GetFileAttributesExW(path, GetFileExInfoStandard, &fa))
        size = ((unsigned long long)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(
        exe_base + reinterpret_cast<const IMAGE_DOS_HEADER *>(exe_base)->e_lfanew);
    out.put("exe          %ls, %llu bytes, PE timestamp %lu, image base 0x%llx\n", path, size,
            (unsigned long)nt->FileHeader.TimeDateStamp, (unsigned long long)exe_base);
    out.put("symbols      %zu KB function name(s)%s\n", symbols.size(),
            symbols_match ? "" : " (symbols.tsv is for another build or missing)");
    out.put("runtime      %s\n", wine_version ? fmt("Wine %s", wine_version()).c_str() : "Windows");
    out.put("first-chance fatal-class exceptions before this report: %ld\n", fatal_count);
}

void step_prior(Request &r)
{
    if (!r.has_prior)
        return;
    const Snapshot &p = r.prior;
    char w[512];
    where_into(w, sizeof w, p.ctx.Rip, true);
    out.put("\n== Earlier fatal-class exception: %s (0x%08lx) on thread %lu, %.1f s before this report, at %s\n",
            code_name(p.rec.ExceptionCode), p.rec.ExceptionCode, p.tid, double(r.ms - p.ms) / 1000, w);
    for (DWORD i = 0; i < p.rec.NumberParameters && i < 4; i++)
        out.put("  parameter %lu: 0x%llx\n", i, (unsigned long long)p.rec.ExceptionInformation[i]);
    static Request pr;
    pr = r;
    pr.ctx = p.ctx;
    pr.lo = p.lo;
    pr.hi = p.hi;
    pr.tid = p.tid;
    section_registers(pr);
    section_stack(p.ctx, p.lo, p.hi, "Stack of the earlier exception");
}

void step_close(Request &)
{
    CloseHandle(out.file);
    out.file = INVALID_HANDLE_VALUE;
}

struct Step {
    const char *name;
    void (*run)(Request &);
};

/* The report is written one step at a time, so a fault in one step costs that step only: the writer
   notes it and goes on with the next (writer_resume). */
const Step STEPS[] = {
    {"the header", step_header},
    {"the Exception section", [](Request &r) { section_exception(r); }},
    {"the Registers section", [](Request &r) { section_registers(r); }},
    {"the Stack section",
     [](Request &r) { section_stack(r.ctx, r.lo, r.hi, "Stack (unwound with the modules' exception data)"); }},
    {"the Earlier fatal-class exception section", step_prior},
    {"the Code around rip and Memory sections", [](Request &r) { section_memory(r); }},
    {"the Ne heap section", [](Request &r) { section_heap(r); }},
    {"the Game state section", [](Request &r) { section_state(r); }},
    {"the Black box sections", [](Request &r) { section_blackbox(r); }},
    {"the Swofty probes section", [](Request &r) { section_probes(r); }},
    {"the registry reports", [](Request &) { swofty_core::apply::moments::crash_reports(); }},
    {"the Addons and Loaded modules sections", [](Request &) { section_modules(); }},
    {"the Threads section",
     [](Request &r) {
         if (r.loader_lock)
             out.put("\n== Threads\n  not listed: the reporting thread holds the loader lock\n");
         else
             section_threads(r);
     }},
    {"the loader.log section", [](Request &) { section_log(); }},
    {"the Minidump section",
     [](Request &r) {
         SetEvent(text_event);
         minidump(r);
         out.put("\n== Minidump\n  %s\n", dump_note);
     }},
    {"closing the report", step_close},
    {"the summary line", [](Request &r) { summary_line(r); }},
};
constexpr LONG NSTEPS = LONG(sizeof STEPS / sizeof *STEPS), CLOSE_STEP = NSTEPS - 2, MAX_WRITER_FAULTS = 8;

void run_steps()
{
    while (step_at < NSTEPS) {
        STEPS[step_at].run(req);
        out.flush();
        step_at = step_at + 1;
    }
}

void write_report()
{
    Request &r = req;
    InterlockedExchange(&writing, 1);
    cl("[crash] writing a crash report: %s", r.reason);
    snapshot_modules();
    report_names();
    out.n = out.written = 0;
    out.kept = true;
    step_at = 0;
    run_steps();
    InterlockedExchange(&writing, 0);
}

[[noreturn]] void writer_loop()
{
    for (;;) {
        InterlockedExchange(&writing, 0);
        SetEvent(text_event);
        SetEvent(done_event);
        WaitForSingleObject(go_event, INFINITE);
        write_report();
    }
}

/* Entered in place of the faulting instruction, on a clean part of the writer's own stack. A thread
   the Threads section had suspended is resumed and a half-written minidump is dropped; the report
   notes what faulted where and goes on with the next step. */
[[noreturn]] void writer_resume()
{
    if (HANDLE h = suspended) {
        suspended = nullptr;
        ResumeThread(h);
        CloseHandle(h);
    }
    LONG step = step_at, n = InterlockedIncrement(&writer_faults);
    if (step == CLOSE_STEP - 1 && dump_file != INVALID_HANDLE_VALUE) {
        CloseHandle(dump_file);
        dump_file = INVALID_HANDLE_VALUE;
        DeleteFileW(dump_path);
        out.put("\n== Minidump\n  not written: the writer faulted while writing it\n");
    }
    char w[512];
    where_into(w, sizeof w, fault_ip, false);
    out.put("\n[%s is cut short: the writer hit %s (0x%08lx) at %s]\n", STEPS[step].name, code_name(fault_code),
            fault_code, w);
    out.flush();
    cl("[crash] the crash report writer hit %s (0x%08lx) at %s in %s of swofty\\%s; %s", code_name(fault_code),
       fault_code, w, STEPS[step].name, report_name,
       n >= MAX_WRITER_FAULTS ? "too many faults, the report ends here" : "it goes on with the next step");
    step_at = n >= MAX_WRITER_FAULTS ? std::max(step + 1, CLOSE_STEP) : step + 1;
    run_steps();
    writer_loop();
}

/* A fatal-class fault on the writer thread while it writes moves it to writer_resume instead of
   ending the process. veh checks it first, before any other handler; a breakpoint is checked last
   (last_chance), since other code plants int3 hooks in APIs the writer calls (the offline hook does in
   GetModuleFileNameW) and its own handler takes those. */
bool writer_fault(EXCEPTION_POINTERS *e, bool breakpoint)
{
    if (!writing || GetCurrentThreadId() != writer_tid || !writer_stack_top)
        return false;
    DWORD code = e->ExceptionRecord->ExceptionCode;
    if (breakpoint ? code != EXCEPTION_BREAKPOINT : !fatal(code))
        return false;
    fault_code = code;
    fault_ip = reinterpret_cast<uintptr_t>(e->ExceptionRecord->ExceptionAddress);
    e->ContextRecord->Rip = reinterpret_cast<DWORD64>(&writer_resume);
    e->ContextRecord->Rsp = ((writer_stack_top - 0x4000) & ~0xfull) - 8;
    return true;
}

LONG CALLBACK last_chance(EXCEPTION_POINTERS *e)
{
    return writer_fault(e, true) ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
}

DWORD WINAPI writer(void *)
{
    uintptr_t lo;
    stack_bounds(lo, writer_stack_top);
    WaitForSingleObject(go_event, INFINITE);
    write_report();
    writer_loop();
}

void read_addons(const std::wstring &path)
{
    std::string text;
    if (FILE *f = _wfopen(path.c_str(), L"rb")) {
        char b[65536];
        for (size_t n; (n = fread(b, 1, sizeof b, f));)
            text.append(b, n);
        fclose(f);
    }
    json::Value doc;
    if (text.empty() || !json::parse(text, doc)) {
        addon_text = text.empty() ? "  no settings.json\n" : "  settings.json is not valid JSON\n";
        return;
    }
    const json::Value &list = doc.field("addons", "mods");
    addon_text = fmt("  profile '%s', %zu addon(s)\n", doc["profile"].str("?").c_str(), list.a.size());
    for (auto &a : list.a) {
        std::string dlls;
        for (auto &d : a["dlls"].a)
            dlls += (dlls.empty() ? "" : ", ") + d.str();
        addon_text += fmt("  %-36s %-10s %-3s %s\n", a["id"].str("?").c_str(), a["version"].str("?").c_str(),
                          a["enabled"].type == json::Value::Bool && !a["enabled"].b ? "off" : "on",
                          dlls.empty() ? "(no dll)" : dlls.c_str());
    }
}

void prune(const wchar_t *pattern, int keep)
{
    std::vector<std::wstring> names;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((swofty_dir + pattern).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do
        names.push_back(fd.cFileName);
    while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(names.begin(), names.end());
    for (size_t i = 0; i + keep < names.size(); i++)
        DeleteFileW((swofty_dir + names[i]).c_str());
}

uintptr_t function_at(uint32_t rva, const uint8_t *expect, size_t n)
{
    uintptr_t a = exe_base + rva;
    return rva < exe_size && readable(a, n) && !memcmp(reinterpret_cast<const void *>(a), expect, n) ? a : 0;
}

uintptr_t kb_function(uint32_t rva, const uint8_t *expect, size_t n)
{
    return kb_build ? function_at(rva, expect, n) : 0;
}

// A fixed-byte scan of the exe's executable sections, used only to find Ne_ReportMemoryError on a
// build with no known address for it. It stays in the crash code and does not go through the
// loader's scan (whose cache and log lines are the binding resolver's). It asks Windows about each
// memory region once rather than about every page.
uintptr_t scan_exe(const uint8_t *pat, size_t n)
{
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(exe_base);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(exe_base + dos->e_lfanew);
    auto s = IMAGE_FIRST_SECTION(nt);
    uintptr_t found = 0;
    size_t hits = 0;
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, s++) {
        if (!(s->Characteristics & IMAGE_SCN_MEM_EXECUTE))
            continue;
        uintptr_t start = exe_base + s->VirtualAddress, end = start + (s->Misc.VirtualSize ? s->Misc.VirtualSize : s->SizeOfRawData);
        for (uintptr_t p = start; p < end;) {
            MEMORY_BASIC_INFORMATION m;
            if (!VirtualQuery(reinterpret_cast<void *>(p), &m, sizeof m))
                break;
            uintptr_t next = std::min(reinterpret_cast<uintptr_t>(m.BaseAddress) + m.RegionSize, end);
            if (next <= p)
                break;
            bool ok = m.State == MEM_COMMIT && !(m.Protect & (PAGE_GUARD | PAGE_NOACCESS)) && (m.Protect & 0xEE);
            auto *a = reinterpret_cast<const uint8_t *>(p), *hi = reinterpret_cast<const uint8_t *>(next);
            for (; ok && size_t(hi - a) >= n; a++) {
                a = static_cast<const uint8_t *>(memchr(a, pat[0], size_t(hi - a) - n + 1));
                if (!a)
                    break;
                if (!memcmp(a, pat, n)) {
                    found = reinterpret_cast<uintptr_t>(a);
                    if (++hits >= 2)
                        return 0;
                }
            }
            p = next;
        }
    }
    return hits == 1 ? found : 0;
}

void hook_heap_errors()
{
    heap_error_fn = kb_function(RVA_HEAP_ERROR, HEAP_ERROR_PREFIX, sizeof HEAP_ERROR_PREFIX);
    uint8_t expect[64];
    size_t n = 0;
    for (const char *p = PATTERN_REPORT_MEMORY_ERROR; *p && n < sizeof expect;) {
        char *end;
        expect[n++] = uint8_t(strtoul(p, &end, 16));
        p = *end ? end + 1 : end;
    }
    report_memory_error = kb_function(RVA_REPORT_MEMORY_ERROR, expect, n);
    const char *how = "KB address";
    if (!report_memory_error && exe_stamp == EA_STAMP) {
        report_memory_error = function_at(EA_RVA_REPORT_MEMORY_ERROR, expect, n);
        how = "EA app build address";
    }
    if (!report_memory_error) {
        report_memory_error = scan_exe(expect, n);
        how = "found by its prologue";
    }
    if (!report_memory_error) {
        cl("[crash] Ne_ReportMemoryError: its prologue matched 0 or several times; a fatal heap error is reported at "
           "the abort only");
        return;
    }
    auto h = safetyhook::InlineHook::create(reinterpret_cast<void *>(report_memory_error),
                                            reinterpret_cast<void *>(&hooked_report_memory_error));
    if (h)
        heap_error_hook = std::move(*h);
    cl("[crash] Ne_ReportMemoryError at base+0x%llx %s (%s); heap objects are recognised by %s",
       (unsigned long long)(report_memory_error - exe_base), h ? "hooked" : "NOT hooked",
       how, heap_error_fn ? "their error method at the KB address" : "the error method's prologue");
}

}

/* Called by the core once the game's code reads decrypted, which on a packed build is the decrypted stage. */
void crash_game_hooks()
{
    hook_heap_errors();
}

namespace blackbox {

void note(const char *kind, const char *a, const char *b)
{
    events.put(GetTickCount64(), GetCurrentThreadId(), kind, a, b);
}

}

namespace runtime {

void crash_blame(const char *text)
{
    snprintf(blame_text, sizeof blame_text, "%s", text ? text : "");
    blame_ms = GetTickCount64();
    blame_tid = GetCurrentThreadId();
}

}

void crash_attach(SwoftyApi *a, const std::wstring &dir)
{
    api = a;
    start_tick = GetTickCount();
    window_reached = runtime::load_stage() != "early";
    runtime::on_stage(window_stage);
    start_ms = GetTickCount64();
    exe = GetModuleHandleW(nullptr);
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&crash_attach), &self);
    auto base = reinterpret_cast<uintptr_t>(exe);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + reinterpret_cast<const IMAGE_DOS_HEADER *>(base)->e_lfanew);
    exe_base = base;
    exe_size = nt->OptionalHeader.SizeOfImage;
    exe_stamp = nt->FileHeader.TimeDateStamp;
    char v[16] = {0};
    ea_build = GetEnvironmentVariableA("SWOFTY_EA_DIAG", v, sizeof v) && v[0] == '1';
    diag_path = ea_build ? dir + L"..\\..\\ea-diagnostics.log" : std::wstring();
    wchar_t full[MAX_PATH * 2];
    DWORD len = GetFullPathNameW((dir + L"..\\..\\").c_str(), MAX_PATH * 2, full, nullptr);
    swofty_dir = len && len < MAX_PATH * 2 ? full : dir + L"..\\..\\";
    if (!swofty_dir.empty() && swofty_dir.back() != L'\\')
        swofty_dir += L'\\';
    loader_log = a->log;
    a->log = tap;
    load_symbols(dir + L"symbols.tsv", nt->FileHeader.TimeDateStamp);
    kb_build = symbols_match && nt->FileHeader.TimeDateStamp == KB_STAMP;
    read_addons(swofty_dir + L"settings.json");
    v[0] = 0;
    GetEnvironmentVariableA("SWOFTY_CRASH_DUMP", v, sizeof v);
    dump_mode = !_stricmp(v, "full") ? DUMP_FULL : !_stricmp(v, "off") || !strcmp(v, "0") ? DUMP_OFF : DUMP_NORMAL;
    wchar_t sys[MAX_PATH];
    UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    if (n && n < MAX_PATH - 16) {
        wcscpy(sys + n, L"\\dbghelp.dll");
        if (HMODULE dh = LoadLibraryW(sys))
            write_dump = reinterpret_cast<DumpFn>(reinterpret_cast<void *>(GetProcAddress(dh, "MiniDumpWriteDump")));
    }
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll"), k32 = GetModuleHandleW(L"kernel32.dll");
    HMODULE kb = GetModuleHandleW(L"kernelbase.dll");
    query_thread = reinterpret_cast<ThreadInfoFn>(reinterpret_cast<void *>(GetProcAddress(ntdll, "NtQueryInformationThread")));
    wine_version = reinterpret_cast<const char *(*)()>(reinterpret_cast<void *>(GetProcAddress(ntdll, "wine_get_version")));
    auto describe = reinterpret_cast<HRESULT(WINAPI *)(HANDLE, PCWSTR)>(
        reinterpret_cast<void *>(GetProcAddress(kb ? kb : k32, "SetThreadDescription")));
    thread_description = reinterpret_cast<DescriptionFn>(
        reinterpret_cast<void *>(GetProcAddress(kb ? kb : k32, "GetThreadDescription")));
    out.cap = TEXT_CAP;
    out.b = static_cast<char *>(VirtualAlloc(nullptr, TEXT_CAP, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    go_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    text_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    done_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE t = out.b && go_event && text_event && done_event
                   ? CreateThread(nullptr, 1 << 20, writer, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, &writer_tid)
                   : nullptr;
    if (t) {
        if (describe)
            describe(t, L"swofty crash report writer");
        CloseHandle(t);
    } else {
        writer_tid = 0;
    }
    prune(L"crash-*.txt", KEEP_REPORTS);
    prune(L"crash-*.dmp", KEEP_DUMPS);
    AddVectoredExceptionHandler(1, veh);
    AddVectoredExceptionHandler(0, last_chance);
    previous = SetUnhandledExceptionFilter(unhandled);
    auto e = safetyhook::InlineHook::create(reinterpret_cast<void *>(GetProcAddress(k32, "ExitProcess")), hooked_exit);
    auto tp = safetyhook::InlineHook::create(reinterpret_cast<void *>(GetProcAddress(k32, "TerminateProcess")), hooked_terminate);
    auto ntt = safetyhook::InlineHook::create(reinterpret_cast<void *>(GetProcAddress(ntdll, "NtTerminateProcess")),
                                              hooked_nt_terminate);
    if (e)
        exit_hook = std::move(*e);
    if (tp)
        terminate_hook = std::move(*tp);
    if (ntt)
        nt_terminate_hook = std::move(*ntt);
    cl("[crash] crash log armed: fatal exceptions (first %ld in full, deduplicated by address), the top-level "
             "filter%s, ExitProcess %s, TerminateProcess %s",
             MAX_REPORTS, previous ? " (chained to the game's)" : "", e ? "hooked" : "NOT hooked",
             tp ? "hooked" : "NOT hooked");
    cl("[crash] crash reports: swofty\\crash-<time>.txt with a %s minidump%s, written by %s; NtTerminateProcess %s",
       dump_mode == DUMP_FULL ? "full-memory" : dump_mode == DUMP_OFF ? "no" : "stacks-only",
       dump_mode == DUMP_NORMAL ? " (SWOFTY_CRASH_DUMP=full for all memory, off for none)" : "",
       writer_tid ? "a dedicated thread" : "the reporting thread", ntt ? "hooked" : "NOT hooked");
}
