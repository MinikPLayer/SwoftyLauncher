#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>
#include "swofty_api.h"
#include "pattern_scan.h"
#include "data_folder.h"
#include "alog.h"

#define HOST_EXE L"F1_25.exe"
#define EXPECTED_SIZE 408063872ULL
#define EXPECTED_STAMP 1789118616UL
#define WINDOW_TIMEOUT_MS 1800000
#define POLL_MS 50

static const char *const fwd_names[] = {
    "GetFileVersionInfoA", "GetFileVersionInfoByHandle", "GetFileVersionInfoExA", "GetFileVersionInfoExW",
    "GetFileVersionInfoSizeA", "GetFileVersionInfoSizeExA", "GetFileVersionInfoSizeExW", "GetFileVersionInfoSizeW",
    "GetFileVersionInfoW", "VerFindFileA", "VerFindFileW", "VerInstallFileA", "VerInstallFileW",
    "VerLanguageNameA", "VerLanguageNameW", "VerQueryValueA", "VerQueryValueW",
};
#define NFWD (sizeof fwd_names / sizeof *fwd_names)
__attribute__((used)) void *swofty_fwd[NFWD];

#define FWD(i, n) __asm__(".text\n.globl fwd_" #n "\n.def fwd_" #n "; .scl 2; .type 32; .endef\n" \
                          "fwd_" #n ": jmp *swofty_fwd+" #i "*8(%rip)\n");
FWD(0, GetFileVersionInfoA)
FWD(1, GetFileVersionInfoByHandle)
FWD(2, GetFileVersionInfoExA)
FWD(3, GetFileVersionInfoExW)
FWD(4, GetFileVersionInfoSizeA)
FWD(5, GetFileVersionInfoSizeExA)
FWD(6, GetFileVersionInfoSizeExW)
FWD(7, GetFileVersionInfoSizeW)
FWD(8, GetFileVersionInfoW)
FWD(9, VerFindFileA)
FWD(10, VerFindFileW)
FWD(11, VerInstallFileA)
FWD(12, VerInstallFileW)
FWD(13, VerLanguageNameA)
FWD(14, VerLanguageNameW)
FWD(15, VerQueryValueA)
FWD(16, VerQueryValueW)

static ULONG_PTR WINAPI fwd_missing(void)
{
    SetLastError(ERROR_PROC_NOT_FOUND);
    return 0;
}

typedef struct {
    char name[9];
    uintptr_t start;
    size_t size;
    DWORD flags;
} Section;

static HANDLE log_file = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION log_lock;
static wchar_t exe_path[MAX_PATH], game_dir[MAX_PATH];
static uintptr_t image_base;
static Section sections[96];
static int section_count, addons_loaded, addons_ready;
static volatile LONG load_state, window_done;
static DWORD attach_tick, load_thread;
static HANDLE loaded_event;
static int early_enabled, early_env, loaded_early;
static char early_source[160] = "off";
static wchar_t early_plugins[64][MAX_PATH], plugins[256][MAX_PATH];
static int early_count, plugin_count;

static HANDLE diag_file = INVALID_HANDLE_VALUE;
static int ea_diag;
static LPTOP_LEVEL_EXCEPTION_FILTER prev_filter;
static volatile LONG diag_crash_reports, activation_logged;

static int log_async;

static void emit(HANDLE h, const char *body)
{
    if (h == INVALID_HANDLE_VALUE)
        return;
    FILETIME now;
    GetSystemTimePreciseAsFileTime(&now);
    DWORD tid = GetCurrentThreadId(), w;
    unsigned int m = (unsigned int)strlen(body);
    char buf[ALOG_LINE];
    if (h == log_file && log_async) {
        if (alog_push(body, m, &now, tid)) {
            if (!strncmp(body, "[crash]", 7))
                alog_flush();
            return;
        }
        if (alog_lock_bounded(&alog_state, ALOG_FLUSH_WAIT_MS)) {
            alog_drain_locked(&alog_state);
            WriteFile(h, buf, (DWORD)alog_format_line(buf, body, m, &now, tid), &w, NULL);
            LeaveCriticalSection(&alog_state.drain_lock);
            return;
        }
    }
    int n = alog_format_line(buf, body, m, &now, tid);
    EnterCriticalSection(&log_lock);
    WriteFile(h, buf, n, &w, NULL);
    LeaveCriticalSection(&log_lock);
}

static void vlog(const char *fmt, va_list ap)
{
    char body[2048];
    vsnprintf(body, sizeof body, fmt, ap);
    emit(log_file, body);
}

static void slog(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog(fmt, ap);
    va_end(ap);
}

/* EA/non-matching-build diagnostics: everything written with ediag goes to both loader.log and the
   dedicated swofty\ea-diagnostics.log so the friend sends one focused file. */
static void ediag(const char *fmt, ...)
{
    char body[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof body, fmt, ap);
    va_end(ap);
    emit(log_file, body);
    emit(diag_file, body);
    alog_flush();
}

static uintptr_t module_base(void)
{
    return image_base;
}

static int parse_pattern(const char *p, int *out, int max)
{
    int n = 0;
    while (*p) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        if (n == max)
            return -1;
        if (*p == '?') {
            out[n++] = -1;
            while (*p == '?')
                p++;
        } else {
            char *end;
            long v = strtol(p, &end, 16);
            if (end == p || end - p > 2 || v < 0 || v > 255)
                return -1;
            out[n++] = (int)v;
            p = end;
        }
    }
    return n;
}

static int is_readable(const MEMORY_BASIC_INFORMATION *m)
{
    return m->State == MEM_COMMIT && (m->Protect & 0xEE) && !(m->Protect & (PAGE_GUARD | PAGE_NOACCESS));
}

static int scan(const char *section, const char *pattern, uintptr_t *out)
{
    int pat[256], n = parse_pattern(pattern, pat, 256), a1 = -1, a2 = -1;
    uintptr_t first = 0;
    int hits = 0, searched = 0;
    if (out)
        *out = 0;
    if (n <= 0 || !pick_anchors(pat, n, &a1, &a2)) {
        slog("scan: invalid pattern '%s'", pattern);
        return 0;
    }
    for (int s = 0; s < section_count; s++) {
        const Section *sec = &sections[s];
        if (section ? strcmp(sec->name, section) != 0 : !(sec->flags & IMAGE_SCN_MEM_EXECUTE))
            continue;
        searched++;
        uintptr_t a = sec->start, end = sec->start + sec->size, run = 0;
        while (a < end) {
            MEMORY_BASIC_INFORMATION m;
            if (!VirtualQuery((void *)a, &m, sizeof m))
                break;
            uintptr_t next = (uintptr_t)m.BaseAddress + m.RegionSize;
            if (next > end)
                next = end;
            if (is_readable(&m)) {
                if (!run)
                    run = a;
            } else if (run) {
                uintptr_t f = 0;
                int h = scan_range((const uint8_t *)run, (const uint8_t *)a, pat, n, a1, a2, &f);
                if (h && !hits)
                    first = f;
                hits += h;
                run = 0;
            }
            a = next;
        }
        if (run) {
            uintptr_t f = 0;
            int h = scan_range((const uint8_t *)run, (const uint8_t *)a, pat, n, a1, a2, &f);
            if (h && !hits)
                first = f;
            hits += h;
        }
    }
    if (!searched)
        slog("scan: section '%s' not found", section ? section : "<executable>");
    if (hits == 1) {
        slog("scan %s [%s] -> 1 hit at 0x%llx (base+0x%llx)", section ? section : "<exec>", pattern,
             (unsigned long long)first, (unsigned long long)(first - image_base));
        if (out)
            *out = first;
    } else if (hits) {
        slog("scan %s [%s] -> %d hits (first at 0x%llx), rejected: need exactly 1", section ? section : "<exec>",
             pattern, hits, (unsigned long long)first);
    } else {
        slog("scan %s [%s] -> 0 hits, rejected: need exactly 1", section ? section : "<exec>", pattern);
    }
    return hits;
}

static SwoftyApi api = {1, slog, module_base, scan, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static SwoftyApi api_v1 = {1, slog, module_base, scan, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static SwoftyCoreInitFn core_init;
static SwoftyCoreCommitFn core_commit;
static SwoftyCoreStageFn core_stage;
static struct {
    wchar_t name[MAX_PATH];
    const char *symbol;
    SwoftyAddonInitFn init;
} deferred[256];
static int deferred_count;

static const wchar_t *const ROOTS[] = {L"addons", L"mods"};

#define PICK_ALL 0
#define PICK_EARLY 1
#define PICK_REST 2

typedef struct {
    int root;
    wchar_t *name;
} Dll;

static int dll_cmp(const void *a, const void *b)
{
    const Dll *x = a, *y = b;
    int c = _wcsicmp(x->name, y->name);
    return c ? c : x->root - y->root;
}

static void finish_init(const wchar_t *name, const char *symbol, int r)
{
    slog("addon %ls: %s returned %d (%s)", name, symbol, r, r ? "ok" : "FAILED");
    if (r)
        addons_ready++;
}

static void load_addon(const wchar_t *dir, const wchar_t *name, int core_dir)
{
    wchar_t path[MAX_PATH * 2];
    swprintf(path, MAX_PATH * 2, L"%ls\\%ls", dir, name);
    slog("addon %ls: loading %ls", name, path);
    HMODULE m = LoadLibraryW(path);
    if (!m) {
        slog("addon %ls: LoadLibrary failed, error %lu", name, GetLastError());
        return;
    }
    addons_loaded++;
    SwoftyCoreAttachFn attach = (SwoftyCoreAttachFn)(void *)GetProcAddress(m, "SwoftyCoreAttach");
    if (core_dir && attach && !core_init) {
        SwoftyCoreInitFn ci = (SwoftyCoreInitFn)(void *)GetProcAddress(m, "SwoftyCoreInit");
        SwoftyCoreCommitFn cc = (SwoftyCoreCommitFn)(void *)GetProcAddress(m, "SwoftyCoreCommit");
        if (ci && cc && attach(&api)) {
            core_init = ci;
            core_commit = cc;
            /* v1 plugins scan through the core as well, so on a packed build they are refused encrypted code. */
            api_v1.scan = api.scan;
            core_stage = (SwoftyCoreStageFn)(void *)GetProcAddress(m, "SwoftyCoreStage");
            slog("addon %ls: provides the mixin API v%u%s", name, api.version, core_stage ? ", stage handlers" : "");
        } else {
            slog("addon %ls: exports SwoftyCoreAttach but did not attach", name);
        }
    }
    const char *symbol = "SwoftyAddonInit", *api_symbol = "SwoftyAddonApi";
    SwoftyAddonInitFn init = (SwoftyAddonInitFn)(void *)GetProcAddress(m, symbol);
    if (!init) {
        symbol = "SwoftyModInit", api_symbol = "SwoftyModApi";
        init = (SwoftyAddonInitFn)(void *)GetProcAddress(m, symbol);
    }
    if (!init) {
        slog("addon %ls: loaded at %p but exports no SwoftyAddonInit or SwoftyModInit, left idle", name, (void *)m);
        return;
    }
    const uint32_t *wants = (const uint32_t *)(void *)GetProcAddress(m, api_symbol);
    if (!wants || *wants < 2) {
        if (deferred_count == 256) {
            slog("addon %ls: too many API v1 plugins, left idle", name);
            return;
        }
        wcsncpy(deferred[deferred_count].name, name, MAX_PATH - 1);
        deferred[deferred_count].symbol = symbol;
        deferred[deferred_count++].init = init;
        slog("addon %ls: loaded at %p, API v1 plugin, %s runs after the mixin commit", name, (void *)m, symbol);
        return;
    }
    slog("addon %ls: loaded at %p, calling %s (API v%u)", name, (void *)m, symbol, *wants);
    finish_init(name, symbol, core_init ? core_init(name, init) : init(&api));
}

static int list_dlls(int root, const wchar_t *sub, Dll *out, int n)
{
    wchar_t query[MAX_PATH * 2];
    swprintf(query, MAX_PATH * 2, L"%ls\\swofty\\%ls%ls%ls\\*.dll", game_dir, ROOTS[root], *sub ? L"\\" : L"", sub);
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(query, &fd);
    if (f == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND)
            slog("addons: %ls: cannot list folder (error %lu)", query, e);
        return n;
    }
    int found = 0;
    do {
        size_t len = wcslen(fd.cFileName);
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && len > 4 && !_wcsicmp(fd.cFileName + len - 4, L".dll") &&
            n < 256)
            out[n].root = root, out[n++].name = _wcsdup(fd.cFileName), found++;
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    slog("addons: swofty\\%ls%ls%ls: %d dll(s)%s", ROOTS[root], *sub ? L"\\" : L"", sub, found,
         root ? " (legacy folder)" : "");
    return n;
}

static int is_early(const wchar_t *name)
{
    for (int i = 0; i < early_count; i++)
        if (!_wcsicmp(early_plugins[i], name))
            return 1;
    return 0;
}

static const wchar_t *base_of(const wchar_t *path)
{
    const wchar_t *s = wcsrchr(path, L'\\');
    return s ? s + 1 : path;
}

static int listed(const wchar_t *name, int before)
{
    for (int i = 0; i < before; i++)
        if (!_wcsicmp(base_of(plugins[i]), name))
            return 1;
    return 0;
}

static void load_plugins(int pick)
{
    for (int i = 0; i < plugin_count; i++) {
        const wchar_t *name = base_of(plugins[i]);
        wchar_t dir[MAX_PATH * 2];
        swprintf(dir, MAX_PATH * 2, L"%ls\\swofty\\%ls", game_dir, plugins[i]);
        dir[wcslen(dir) - wcslen(name) - 1] = 0;
        if (listed(name, i))
            slog("addon %ls: listed twice in settings.json, %ls skipped", name, plugins[i]);
        else if (pick == PICK_EARLY && !is_early(name))
            slog("addon %ls: loads at the window stage", name);
        else if (pick != PICK_REST || !is_early(name))
            load_addon(dir, name, 0);
    }
}

static void load_dir(const wchar_t *sub, int core_dir, int pick)
{
    Dll dlls[256];
    int n = 0;
    for (int r = 0; r < (int)(sizeof ROOTS / sizeof *ROOTS); r++)
        n = list_dlls(r, sub, dlls, n);
    if (!n)
        slog("addons: swofty\\addons%ls%ls: no .dll files", *sub ? L"\\" : L"", sub);
    qsort(dlls, n, sizeof *dlls, dll_cmp);
    for (int i = 0; i < n; i++) {
        wchar_t dir[MAX_PATH * 2];
        swprintf(dir, MAX_PATH * 2, L"%ls\\swofty\\%ls%ls%ls", game_dir, ROOTS[dlls[i].root], *sub ? L"\\" : L"", sub);
        if (i && !_wcsicmp(dlls[i].name, dlls[i - 1].name))
            slog("addon %ls: also in %ls, skipped", dlls[i].name, dir);
        else if (!core_dir && listed(dlls[i].name, plugin_count))
            slog("addon %ls: settings.json lists it in an addon folder, the copy in %ls is skipped", dlls[i].name, dir);
        else if (pick == PICK_EARLY && !is_early(dlls[i].name))
            slog("addon %ls: loads at the window stage", dlls[i].name);
        else if (pick != PICK_REST || !is_early(dlls[i].name))
            load_addon(dir, dlls[i].name, core_dir);
    }
    for (int i = 0; i < n; i++)
        free(dlls[i].name);
}

typedef struct {
    char text[1024];
    size_t len;
} DialogText;

static void append_utf8(DialogText *t, const wchar_t *w)
{
    if (!w[0] || t->len + 4 >= sizeof t->text)
        return;
    if (t->len)
        t->text[t->len++] = ' ';
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, t->text + t->len, (int)(sizeof t->text - t->len), NULL, NULL);
    t->len = n > 0 ? t->len + (size_t)n - 1 : t->len;
    t->text[t->len] = 0;
}

static BOOL CALLBACK dialog_static(HWND h, LPARAM out)
{
    wchar_t cls[16] = {0}, text[512] = {0};
    GetClassNameW(h, cls, 16);
    if (!_wcsicmp(cls, L"Static") && GetWindowTextW(h, text, 512))
        append_utf8((DialogText *)out, text);
    return TRUE;
}

/* The EA App build shows its activation and launch errors as dialogs before any game window exists; their text
   is the clue a report needs, and a dialog is not the game reaching its window. */
static void note_dialog(HWND h)
{
    static HWND logged;
    if (h == logged)
        return;
    logged = h;
    wchar_t title[256] = {0};
    GetWindowTextW(h, title, 256);
    DialogText t = {{0}, 0}, body = {{0}, 0};
    append_utf8(&t, title);
    EnumChildWindows(h, dialog_static, (LPARAM)&body);
    if (ea_diag)
        ediag("dialog shown: %s: %s", t.text, body.text);
    else
        slog("dialog shown: %s: %s", t.text, body.text);
}

static BOOL CALLBACK find_window(HWND h, LPARAM out)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(h) || GetWindow(h, GW_OWNER))
        return TRUE;
    wchar_t cls[16] = {0};
    GetClassNameW(h, cls, 16);
    if (!wcscmp(cls, L"#32770")) {
        note_dialog(h);
        return TRUE;
    }
    *(HWND *)out = h;
    return FALSE;
}

typedef ATOM(WINAPI *RegisterClassExWFn)(const WNDCLASSEXW *);
typedef HWND(WINAPI *CreateWindowExWFn)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE,
                                         LPVOID);
typedef HANDLE(WINAPI *CreateMutexWFn)(LPSECURITY_ATTRIBUTES, BOOL, LPCWSTR);
static ATOM WINAPI trigger_RegisterClassExW(const WNDCLASSEXW *wc);
static HWND WINAPI trigger_CreateWindowExW(DWORD ex, LPCWSTR cls, LPCWSTR name, DWORD style, int x, int y, int w,
                                           int h, HWND parent, HMENU menu, HINSTANCE inst, LPVOID param);
static HANDLE WINAPI trigger_CreateMutexW(LPSECURITY_ATTRIBUTES sa, BOOL owner, LPCWSTR name);

#define STAGE_EARLY 1
#define STAGE_LATE 2
#define EARLY_MUTEX L"Codemasters F1 25"
#define SECOND_COPY "single instance:"

static struct {
    const char *dll, *mod, *name;
    void *hook, **slot, *orig;
    int stage;
} iat[] = {
    {"USER32.dll", "USER32", "RegisterClassExW", (void *)trigger_RegisterClassExW, NULL, NULL, STAGE_LATE},
    {"USER32.dll", "USER32", "CreateWindowExW", (void *)trigger_CreateWindowExW, NULL, NULL, STAGE_LATE},
    {"KERNEL32.dll", "KERNEL32", "CreateMutexW", (void *)trigger_CreateMutexW, NULL, NULL, STAGE_EARLY},
};
#define NIAT (sizeof iat / sizeof *iat)
#define IAT_REGISTER 0
#define IAT_CREATE 1
#define IAT_MUTEX 2

static int late_pending;

static int set_slot(void **slot, void *value)
{
    DWORD old;
    if (!VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old))
        return 0;
    InterlockedExchangePointer(slot, value);
    VirtualProtect(slot, sizeof *slot, old, &old);
    return 1;
}

/* The EA App exe's import table lists only Activation64.dll; its packer fills the game's real imports at load,
   after this attach. There the early trigger is a breakpoint on the entry of CreateMutexW itself, which this
   handler turns into a call of entry_CreateMutexW from the same caller. */
static HANDLE WINAPI entry_CreateMutexW(LPSECURITY_ATTRIBUTES sa, BOOL owner, LPCWSTR name);

static struct {
    const char *mod;
    uint8_t *at, orig;
} entry_bp[2];
static volatile LONG entry_bps;

static LONG CALLBACK entry_veh(EXCEPTION_POINTERS *e)
{
    if (e->ExceptionRecord->ExceptionCode == EXCEPTION_BREAKPOINT)
        for (LONG i = 0; i < entry_bps; i++)
            if (e->ExceptionRecord->ExceptionAddress == entry_bp[i].at) {
                e->ContextRecord->Rip = (DWORD64)(uintptr_t)entry_CreateMutexW;
                return EXCEPTION_CONTINUE_EXECUTION;
            }
    return EXCEPTION_CONTINUE_SEARCH;
}

static int poke(uint8_t *at, uint8_t value)
{
    DWORD old;
    if (!VirtualProtect(at, 1, PAGE_EXECUTE_READWRITE, &old))
        return 0;
    *at = value;
    VirtualProtect(at, 1, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, 1);
    return 1;
}

static int arm_entry(void)
{
    static const char *const mods[] = {"KERNEL32", "KERNELBASE"};
    PVOID veh = AddVectoredExceptionHandler(1, entry_veh);
    int armed = 0;
    for (int i = 0; veh && i < 2; i++) {
        HMODULE m = GetModuleHandleA(mods[i]);
        uint8_t *at = m ? (uint8_t *)(void *)GetProcAddress(m, "CreateMutexW") : NULL;
        if (!at || *at == 0xCC || (entry_bps && entry_bp[0].at == at))
            continue;
        entry_bp[entry_bps].mod = mods[i];
        entry_bp[entry_bps].at = at;
        entry_bp[entry_bps].orig = *at;
        InterlockedIncrement(&entry_bps);
        if (poke(at, 0xCC)) {
            armed++;
            slog("trigger armed: breakpoint on the entry of %s!CreateMutexW at %p, early stage", mods[i], (void *)at);
        }
    }
    if (veh && !entry_bps)
        RemoveVectoredExceptionHandler(veh);
    return armed;
}

static void disarm_entry(void)
{
    for (LONG i = 0; i < entry_bps; i++)
        if (*entry_bp[i].at == 0xCC && poke(entry_bp[i].at, entry_bp[i].orig))
            slog("trigger disarmed: %s!CreateMutexW entry restored", entry_bp[i].mod);
}

static void hook_iat(void)
{
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(image_base + ((const IMAGE_DOS_HEADER *)image_base)->e_lfanew);
    const IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    const IMAGE_DATA_DIRECTORY *iatdir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT];
    uintptr_t iat_lo = image_base + iatdir->VirtualAddress, iat_hi = iat_lo + iatdir->Size;
    for (const IMAGE_IMPORT_DESCRIPTOR *d = (const IMAGE_IMPORT_DESCRIPTOR *)(image_base + dir->VirtualAddress);
         dir->VirtualAddress && d->Name; d++) {
        const char *dll = (const char *)(image_base + d->Name);
        if (!d->OriginalFirstThunk)
            continue;
        const IMAGE_THUNK_DATA64 *names = (const IMAGE_THUNK_DATA64 *)(image_base + d->OriginalFirstThunk);
        void **slots = (void **)(image_base + d->FirstThunk);
        for (int i = 0; names[i].u1.AddressOfData; i++) {
            if (IMAGE_SNAP_BY_ORDINAL64(names[i].u1.Ordinal))
                continue;
            const char *fn = (const char *)((const IMAGE_IMPORT_BY_NAME *)(image_base + names[i].u1.AddressOfData))->Name;
            for (size_t k = 0; k < NIAT; k++)
                if (!iat[k].slot && !strcmp(dll, iat[k].dll) && !strcmp(fn, iat[k].name) &&
                    (!iatdir->Size || ((uintptr_t)&slots[i] >= iat_lo && (uintptr_t)&slots[i] < iat_hi))) {
                    iat[k].slot = &slots[i];
                    iat[k].orig = slots[i];
                }
        }
    }
    for (size_t k = 0; k < NIAT; k++) {
        if (iat[k].stage == STAGE_EARLY && !early_enabled)
            continue;
        if (iat[k].slot && iat[k].orig && set_slot(iat[k].slot, iat[k].hook)) {
            slog("trigger armed: game IAT slot %s!%s at %p (was %p)%s", iat[k].mod, iat[k].name, (void *)iat[k].slot,
                 iat[k].orig, iat[k].stage == STAGE_EARLY ? ", early stage" : "");
            continue;
        }
        iat[k].slot = NULL;
        if (iat[k].stage == STAGE_LATE) {
            late_pending = 1;
            slog("trigger: game IAT slot %s!%s not found or not writable", iat[k].mod, iat[k].name);
            continue;
        }
        slog("trigger: game IAT slot %s!%s not found or not writable; the exe's import table is filled at load, so "
             "the early stage fires on the entry of the function",
             iat[k].mod, iat[k].name);
        if (!arm_entry())
            slog("trigger: no breakpoint on the entry of CreateMutexW could be set; the early stage cannot fire");
    }
}

/* Once the game's own code runs, the packer has filled its import table: the window-stage slots are the one
   pointer in the image's data that holds each function's address. */
static void arm_filled_iat(void)
{
    void *want[NIAT] = {0}, **at[NIAT] = {0};
    int hits[NIAT] = {0}, pending = 0;
    for (size_t k = 0; k < NIAT; k++)
        if (iat[k].stage == STAGE_LATE && !iat[k].slot) {
            HMODULE m = GetModuleHandleA(iat[k].mod);
            want[k] = m ? (void *)GetProcAddress(m, iat[k].name) : NULL;
            pending += want[k] != NULL;
        }
    if (!pending)
        return;
    for (int s = 0; s < section_count; s++) {
        uintptr_t a = sections[s].start, end = a + sections[s].size;
        MEMORY_BASIC_INFORMATION m;
        if (sections[s].flags & IMAGE_SCN_MEM_EXECUTE)
            continue;
        for (; a < end && VirtualQuery((void *)a, &m, sizeof m); a = (uintptr_t)m.BaseAddress + m.RegionSize) {
            uintptr_t stop = (uintptr_t)m.BaseAddress + m.RegionSize < end ? (uintptr_t)m.BaseAddress + m.RegionSize : end;
            if (!is_readable(&m))
                continue;
            for (void **p = (void **)((a + 7) & ~(uintptr_t)7); (uintptr_t)(p + 1) <= stop; p++)
                for (size_t k = 0; k < NIAT; k++)
                    if (want[k] && *p == want[k] && !hits[k]++)
                        at[k] = p;
        }
    }
    for (size_t k = 0; k < NIAT; k++) {
        if (!want[k])
            continue;
        iat[k].orig = want[k];
        iat[k].slot = hits[k] == 1 ? at[k] : NULL;
        if (iat[k].slot && set_slot(iat[k].slot, iat[k].hook)) {
            slog("trigger armed: game IAT slot %s!%s at %p (was %p), in the import table the exe filled at load",
                 iat[k].mod, iat[k].name, (void *)iat[k].slot, want[k]);
            continue;
        }
        iat[k].slot = NULL;
        slog("trigger: %s!%s is in %d place(s) of the exe's data, not in one import slot; the window stage waits for "
             "the main window",
             iat[k].mod, iat[k].name, hits[k]);
    }
}

static void unhook_iat(int stage)
{
    if (stage != STAGE_LATE)
        disarm_entry();
    for (size_t k = 0; k < NIAT; k++)
        if ((!stage || iat[k].stage == stage) && iat[k].slot && *iat[k].slot == iat[k].hook &&
            set_slot(iat[k].slot, iat[k].orig))
            slog("trigger disarmed: %s!%s slot restored to %p", iat[k].mod, iat[k].name, iat[k].orig);
}

static int late_armed(void)
{
    for (size_t k = 0; k < NIAT; k++)
        if (iat[k].stage == STAGE_LATE && iat[k].slot && *iat[k].slot == iat[k].hook)
            return 1;
    return 0;
}

static const Section *section_of(uintptr_t a)
{
    for (int i = 0; i < section_count; i++)
        if (a - sections[i].start < sections[i].size)
            return &sections[i];
    return NULL;
}

static void open_diag_log(void)
{
    wchar_t dir[MAX_PATH * 2], path[MAX_PATH * 2], prev[MAX_PATH * 2];
    swprintf(dir, MAX_PATH * 2, L"%ls\\swofty", game_dir);
    CreateDirectoryW(dir, NULL);
    swprintf(path, MAX_PATH * 2, L"%ls\\ea-diagnostics.log", dir);
    swprintf(prev, MAX_PATH * 2, L"%ls\\ea-diagnostics.prev.log", dir);
    MoveFileExW(path, prev, MOVEFILE_REPLACE_EXISTING);
    diag_file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

/* The engine's runtime code section: the first EXECUTE|READ, non-writable section (EA .rdata, Steam
   .xtls), falling back to the first executable section. The packed RWX blobs are not it. */
static const Section *code_section(void)
{
    const Section *exec = NULL;
    for (int i = 0; i < section_count; i++) {
        DWORD f = sections[i].flags;
        if ((f & IMAGE_SCN_MEM_EXECUTE) && (f & IMAGE_SCN_MEM_READ) && !(f & IMAGE_SCN_MEM_WRITE))
            return &sections[i];
        if ((f & IMAGE_SCN_MEM_EXECUTE) && !exec)
            exec = &sections[i];
    }
    return exec;
}

static int page_live(uintptr_t a, MEMORY_BASIC_INFORMATION *m)
{
    return VirtualQuery((void *)a, m, sizeof *m) && m->State == MEM_COMMIT && (m->Protect & 0xEE) &&
           !(m->Protect & (PAGE_GUARD | PAGE_NOACCESS));
}

static double page_entropy(const uint8_t *p, size_t n)
{
    unsigned long cnt[256] = {0};
    for (size_t i = 0; i < n; i++)
        cnt[p[i]]++;
    double h = 0;
    for (int i = 0; i < 256; i++)
        if (cnt[i]) {
            double pr = (double)cnt[i] / (double)n;
            h -= pr * log2(pr);
        }
    return h;
}

static const char *entropy_verdict(double e)
{
    return e < 6.5 ? "decrypted/plaintext" : e > 7.5 ? "packed/encrypted" : "mixed";
}

static void sample_code_entropy(const char *when)
{
    const Section *s = code_section();
    if (!s) {
        ediag("entropy (%s): no code section found", when);
        return;
    }
    const int PAGES = 8;
    ediag("entropy (%s): section %s base 0x%llx size 0x%llx, %d pages (entropy <6.5 decrypted, ~8.0 still packed)", when,
          s->name, (unsigned long long)s->start, (unsigned long long)s->size, PAGES);
    double sum = 0, mn = 9, mx = 0;
    int ok = 0;
    for (int k = 0; k < PAGES; k++) {
        size_t off = s->size > (size_t)PAGES * 0x1000 ? (size_t)((double)k / PAGES * (double)(s->size - 0x1000)) & ~(size_t)0xfff
                                                      : (size_t)k * 0x1000;
        uintptr_t a = s->start + off;
        MEMORY_BASIC_INFORMATION m;
        if (!page_live(a, &m)) {
            ediag("  page +0x%08llx: not readable (protect 0x%lx state 0x%lx)", (unsigned long long)off,
                  (unsigned long)m.Protect, (unsigned long)m.State);
            continue;
        }
        double e = page_entropy((const uint8_t *)a, 0x1000);
        ok++;
        sum += e;
        if (e < mn)
            mn = e;
        if (e > mx)
            mx = e;
        ediag("  page +0x%08llx @0x%llx: entropy %.2f -> %s", (unsigned long long)off, (unsigned long long)a, e,
              entropy_verdict(e));
    }
    if (ok)
        ediag("entropy (%s): %d page(s), avg %.2f min %.2f max %.2f -> %s", when, ok, sum / ok, mn, mx,
              (sum / ok) < 6.5 ? "DECRYPTED in bulk" : (sum / ok) > 7.5 ? "STILL PACKED (encrypted)" : "PARTIAL/mixed");
}

/* First match of a masked byte pattern in a loaded module's committed,
   readable, executable pages. Bounded by SizeOfImage; pages that are not live
   are skipped so a protected build cannot fault the diagnostic. Returns the
   match address or NULL. */
static uint8_t *scan_module(HMODULE mod, const uint8_t *pat, const uint8_t *msk, size_t len)
{
    uint8_t *base = (uint8_t *)mod;
    MEMORY_BASIC_INFORMATION hm;
    if (!page_live((uintptr_t)base, &hm))
        return NULL;
    uint32_t lfanew = *(uint32_t *)(base + 0x3C);
    uint8_t *nt = base + lfanew;
    uint32_t size_of_image = *(uint32_t *)(nt + 0x18 + 0x38); /* optional header SizeOfImage */
    uint8_t *end = base + size_of_image;
    const DWORD exec = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    uint8_t *p = base;
    MEMORY_BASIC_INFORMATION m;
    while (p < end && VirtualQuery(p, &m, sizeof m)) {
        uint8_t *region = (uint8_t *)m.BaseAddress;
        uint8_t *rend = region + m.RegionSize;
        if (rend > end)
            rend = end;
        if (m.State == MEM_COMMIT && (m.Protect & exec) && !(m.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
            for (uint8_t *q = region; q + len <= rend; q++) {
                size_t j = 0;
                for (; j < len; j++)
                    if ((q[j] & msk[j]) != (pat[j] & msk[j]))
                        break;
                if (j == len)
                    return q;
            }
        }
        uint8_t *next = region + m.RegionSize;
        if (next <= p)
            break;
        p = next;
    }
    return NULL;
}

/* Report a resolved gate: where the guarding branch sits, the bytes around it,
   and whether offline-play has flipped it. The branch is two bytes into the
   match (after `test al,al` / `test eax,eax`); 0xEB means answered (jmp),
   0x74/0x75 means not yet. */
static void report_gate(HMODULE m, const char *label, uint8_t *hit, size_t len, const char *when_patched,
                        const char *when_not)
{
    char dump[64];
    int o = 0;
    for (size_t i = 0; i < len && o < (int)sizeof dump - 3; i++)
        o += sprintf(dump + o, "%02X ", hit[i]);
    if (o)
        dump[o - 1] = 0;
    uintptr_t rva = (uintptr_t)(hit - (uint8_t *)m);
    uint8_t br = hit[2];
    const char *verdict = br == 0xEB ? when_patched : (br == 0x75 || br == 0x74) ? when_not : "UNKNOWN (unexpected branch byte)";
    ediag("%s: site RVA 0x%llx bytes [%s]; branch RVA 0x%llx byte 0x%02X -> %s", label, (unsigned long long)rva,
          dump, (unsigned long long)(rva + 2), br, verdict);
}

static void check_activation_gate(void)
{
    char env[8] = {0};
    DWORD n = GetEnvironmentVariableA("SWOFTY_OFFLINE_ACTIVATION", env, sizeof env);
    HMODULE m = GetModuleHandleW(L"Activation64.dll");
    if (!m) {
        ediag("gate 2 (activation/EADRM, Core\\Activation64.dll): module not loaded yet; "
              "SWOFTY_OFFLINE_ACTIVATION=%s",
              n ? env : "(unset)");
        return;
    }
    if (InterlockedCompareExchange(&activation_logged, 1, 0) != 0)
        return;
    ediag("gate 2 (activation/EADRM, Core\\Activation64.dll base %p): SWOFTY_OFFLINE_ACTIVATION=%s", (void *)m,
          n ? env : "(unset)");

    /* The activation verdict: test al,al ; jne ok ; mov dword [r12],0x186aa (exit
       100010 'EA App not installed'). The jne (two bytes in) is wildcarded. */
    const uint8_t vp[] = {0x84, 0xC0, 0x00, 0x00, 0x41, 0xC7, 0x04, 0x24, 0xAA, 0x86, 0x01, 0x00};
    const uint8_t vm[] = {0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t *vhit = scan_module(m, vp, vm, sizeof vp);
    if (vhit)
        report_gate(m, "  verdict (100010/0x186aa path)", vhit, sizeof vp,
                    "PASS (0xEB = patched; this activation path is bypassed)",
                    "FAIL (0x75 = NOT patched; verifier would abort with exit 100010)");
    else
        ediag("  verdict (100010/0x186aa path): pattern not found; send Core\\Activation64.dll (this build "
              "differs from the one offline-play was built against)");

    /* The entitlement exit guard: test al,al/eax ; jcc ok ; mov ecx,0xFFFFFFFF ;
       call [exit]. Its false answer is a clean exit(-1) -- the launch failure
       seen on this EA build -- and offline-play flips the jcc to a jmp. */
    const uint8_t xp[] = {0x84, 0xC0, 0x00, 0x00, 0xB9, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x15};
    const uint8_t xm[] = {0xFE, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t *xhit = scan_module(m, xp, xm, sizeof xp);
    if (xhit)
        report_gate(m, "  exit guard (entitlement exit -1)", xhit, sizeof xp,
                    "PASS (0xEB = patched; exit(-1) is skipped, the session continues)",
                    "FAIL (0x75 = NOT patched; the game exits with code -1 here)");
    else
        ediag("  exit guard (entitlement exit -1): pattern not found; send Core\\Activation64.dll (this build "
              "differs from the one offline-play was built against)");

    /* The historical fixed offset, kept for continuity with earlier logs. */
    uintptr_t rva = 0xe550;
    uint8_t *at = (uint8_t *)m + rva;
    MEMORY_BASIC_INFORMATION mi;
    if (page_live((uintptr_t)at, &mi)) {
        uint8_t br = at[2];
        const char *v = br == 0xEB ? "answered (jmp)"
                       : (br == 0x75 || br == 0x74) ? "NOT answered" : "unexpected byte";
        ediag("  reference: RVA 0x%llx = 0x%02X (test al,al); verdict branch RVA 0x%llx = 0x%02X -> %s",
              (unsigned long long)rva, *at, (unsigned long long)(rva + 2), br, v);
    }
}

static void gate3_verdict(const char *when, int reached_window)
{
    ediag("gate 3 (EA account sign-in): %s (%s); %s", reached_window ? "PASS" : "not yet observed", when,
          reached_window ? "the engine reached the window stage, so EA sign-in completed"
                         : "if the game is stalled on an EA sign-in prompt this gate is pending; if it exited or "
                           "crashed first, the crash line above is the cause, not this gate");
}

static const char *exc_name(DWORD c)
{
    switch (c) {
    case EXCEPTION_ACCESS_VIOLATION: return "ACCESS_VIOLATION";
    case EXCEPTION_IN_PAGE_ERROR: return "IN_PAGE_ERROR";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_PRIV_INSTRUCTION: return "PRIV_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_STACK_OVERFLOW: return "STACK_OVERFLOW";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "NONCONTINUABLE_EXCEPTION";
    case 0xC0000374: return "HEAP_CORRUPTION";
    case 0xC0000409: return "STACK_BUFFER_OVERRUN / fail-fast";
    case 0xE06D7363: return "C++ exception";
    }
    return "exception";
}

static int exc_fatal(DWORD c)
{
    return c == EXCEPTION_ACCESS_VIOLATION || c == EXCEPTION_IN_PAGE_ERROR || c == EXCEPTION_ILLEGAL_INSTRUCTION ||
           c == EXCEPTION_PRIV_INSTRUCTION || c == EXCEPTION_INT_DIVIDE_BY_ZERO || c == EXCEPTION_STACK_OVERFLOW ||
           c == EXCEPTION_NONCONTINUABLE_EXCEPTION || c == 0xC0000374 || c == 0xC0000409;
}

static void crash_where(uintptr_t a, char *out, size_t cap)
{
    HMODULE m = NULL;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)a, &m) &&
        m) {
        wchar_t wp[MAX_PATH] = L"?";
        GetModuleFileNameW(m, wp, MAX_PATH);
        const wchar_t *bn = wcsrchr(wp, L'\\');
        bn = bn ? bn + 1 : wp;
        uintptr_t mb = (uintptr_t)m;
        if ((uintptr_t)m == image_base) {
            const Section *s = section_of(a);
            snprintf(out, cap, "%ls+0x%llx [%s] (base+0x%llx)", bn, (unsigned long long)(a - mb),
                     s ? s->name : "?", (unsigned long long)(a - mb));
        } else {
            snprintf(out, cap, "%ls+0x%llx", bn, (unsigned long long)(a - mb));
        }
    } else {
        snprintf(out, cap, "0x%llx (no owning module)", (unsigned long long)a);
    }
}

static void crash_backtrace(CONTEXT ctx)
{
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
    uintptr_t lo = (uintptr_t)tib->StackLimit, hi = (uintptr_t)tib->StackBase;
    char w[320];
    for (int i = 0, shown = 0; i < 16 && ctx.Rip; i++) {
        crash_where(ctx.Rip, w, sizeof w);
        ediag("  #%-2d %s", shown++, w);
        if (ctx.Rsp < lo || ctx.Rsp >= hi)
            break;
        DWORD64 ib = 0;
        PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(ctx.Rip, &ib, NULL);
        if (f) {
            PVOID hd;
            DWORD64 ef;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, ib, ctx.Rip, f, &ctx, &hd, &ef, NULL);
        } else {
            MEMORY_BASIC_INFORMATION m;
            if (!page_live(ctx.Rsp, &m))
                break;
            ctx.Rip = *(DWORD64 *)ctx.Rsp;
            ctx.Rsp += 8;
        }
    }
}

static void crash_report(const char *when, EXCEPTION_POINTERS *e)
{
    EXCEPTION_RECORD *r = e->ExceptionRecord;
    char w[320];
    crash_where((uintptr_t)r->ExceptionAddress, w, sizeof w);
    ediag("CRASH (%s): %s (0x%08lx) on thread %lu, %lu ms after attach, at %s", when, exc_name(r->ExceptionCode),
          (unsigned long)r->ExceptionCode, GetCurrentThreadId(), GetTickCount() - attach_tick, w);
    if ((r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || r->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
        r->NumberParameters >= 2) {
        ULONG_PTR op = r->ExceptionInformation[0];
        ediag("  %s 0x%llx", op == 0 ? "reading" : op == 1 ? "writing" : op == 8 ? "executing (DEP)" : "accessing",
              (unsigned long long)r->ExceptionInformation[1]);
    }
    ediag("  stack (loader unwind, module+RVA):");
    crash_backtrace(*e->ContextRecord);
}

static LONG CALLBACK loader_veh(EXCEPTION_POINTERS *e)
{
    DWORD code = e->ExceptionRecord->ExceptionCode;
    if (ea_diag && exc_fatal(code) && InterlockedIncrement(&diag_crash_reports) <= 8)
        crash_report("first-chance (the game may still handle it)", e);
    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI loader_unhandled(EXCEPTION_POINTERS *e)
{
    if (ea_diag) {
        ediag("CRASH: unhandled exception reached the loader top-level filter; %s",
              prev_filter ? "passing it to the game's filter" : "the process ends here");
        crash_report("unhandled", e);
    }
    return prev_filter ? prev_filter(e) : EXCEPTION_CONTINUE_SEARCH;
}

static void ea_stage_probe(const char *when, int window)
{
    if (!ea_diag)
        return;
    sample_code_entropy(when);
    check_activation_gate();
    if (window) {
        gate3_verdict(when, 1);
    }
}

static void finish_batch(int first)
{
    if (core_commit)
        slog("addons: mixin commit done, %d mixin addon(s) active", core_commit());
    for (int i = first; i < deferred_count; i++) {
        slog("addon %ls: calling %s (API v1)", deferred[i].name, deferred[i].symbol);
        finish_init(deferred[i].name, deferred[i].symbol, deferred[i].init(&api_v1));
    }
    slog("addons: done, %d addon(s) loaded, %d initialised", addons_loaded, addons_ready);
}

static void load_addons_once(const char *why, LPCWSTR detail, int stage)
{
    if (InterlockedCompareExchange(&load_state, 1, 0) != 0)
        return;
    load_thread = GetCurrentThreadId();
    if (detail && !((uintptr_t)detail >> 16))
        detail = NULL;
    loaded_early = stage == STAGE_EARLY;
    slog("trigger: %s%s%ls%s on thread %lu, %lu ms after attach; loading addons now%s", why,
         detail ? (loaded_early ? " name '" : " class '") : "", detail ? detail : L"", detail ? "'" : "",
         GetCurrentThreadId(), GetTickCount() - attach_tick, loaded_early ? " (early stage)" : "");
    if (early_enabled && !loaded_early)
        slog("early stage: the early anchor never fired; early-stage mixins (game database, config and file-system "
             "overlay of files read before the window) missed their first calls");
    SetEnvironmentVariableW(L"SWOFTY_LOAD_STAGE", loaded_early ? L"early" : L"window");
    ea_stage_probe(loaded_early ? "early-stage trigger (core loading)" : "window-stage trigger (core loading)",
                   !loaded_early);
    unhook_iat(loaded_early ? STAGE_EARLY : STAGE_LATE);
    if (loaded_early)
        arm_filled_iat();
    load_dir(L"core", 1, PICK_ALL);
    load_plugins(loaded_early ? PICK_EARLY : PICK_ALL);
    load_dir(L"", 0, loaded_early ? PICK_EARLY : PICK_ALL);
    finish_batch(0);
    if (!loaded_early)
        unhook_iat(0);
    InterlockedExchange(&load_state, 2);
    if (loaded_event)
        SetEvent(loaded_event);
}

static void load_rest(void)
{
    int first = deferred_count;
    slog("addons: loading the addons without \"early\": true at the window stage");
    load_plugins(PICK_REST);
    load_dir(L"", 0, PICK_REST);
    finish_batch(first);
}

static void wait_loaded(const char *who)
{
    if (load_state != 1 || !loaded_event || load_thread == GetCurrentThreadId())
        return;
    DWORD t0 = GetTickCount();
    DWORD r = WaitForSingleObject(loaded_event, 60000);
    slog("%s: waited %lu ms for the addon load running on another thread (%s)", who, GetTickCount() - t0,
         r == WAIT_OBJECT_0 ? "done" : "timed out");
}

static void stage_window(const char *why)
{
    if (!early_enabled || load_state != 2 || InterlockedExchange(&window_done, 1))
        return;
    unhook_iat(0);
    slog("stage: window (%s), addons were loaded at the %s stage", why, loaded_early ? "early" : "window");
    if (loaded_early)
        load_rest();
    if (core_stage)
        core_stage(SWOFTY_STAGE_WINDOW);
    ea_stage_probe("window stage", 1);
}

static int plain_code(const Section *sec)
{
    return sec && (sec->flags & IMAGE_SCN_MEM_EXECUTE) && !(sec->flags & IMAGE_SCN_MEM_WRITE);
}

static void game_call(const char *fn, void *ret, LPCWSTR cls)
{
    const Section *sec = section_of((uintptr_t)ret);
    char why[160];
    snprintf(why, sizeof why, "USER32!%s from %p (%s)", fn, ret, sec ? sec->name : "outside the exe");
    if (plain_code(sec)) {
        load_addons_once(why, cls, STAGE_LATE);
        if (early_enabled) {
            wait_loaded("trigger");
            stage_window(why);
        }
    } else if (early_enabled ? load_state != 2 : !load_state)
        slog("trigger skipped: %s is not plain game code, still waiting", why);
}

static ATOM WINAPI trigger_RegisterClassExW(const WNDCLASSEXW *wc)
{
    game_call("RegisterClassExW", __builtin_return_address(0), wc ? wc->lpszClassName : NULL);
    return ((RegisterClassExWFn)iat[IAT_REGISTER].orig)(wc);
}

static HWND WINAPI trigger_CreateWindowExW(DWORD ex, LPCWSTR cls, LPCWSTR name, DWORD style, int x, int y, int w,
                                           int h, HWND parent, HMENU menu, HINSTANCE inst, LPVOID param)
{
    game_call("CreateWindowExW", __builtin_return_address(0), cls);
    return ((CreateWindowExWFn)iat[IAT_CREATE].orig)(ex, cls, name, style, x, y, w, h, parent, menu, inst, param);
}

static int early_mutex(void *ret, LPCWSTR name)
{
    static volatile LONG skips;
    if (load_state == 2)
        return 0;
    const Section *sec = section_of((uintptr_t)ret);
    if (name && !wcscmp(name, EARLY_MUTEX) && plain_code(sec)) {
        char why[160];
        snprintf(why, sizeof why, "KERNEL32!CreateMutexW from %p (%s)", ret, sec->name);
        load_addons_once(why, name, STAGE_EARLY);
        wait_loaded("early trigger");
        return 1;
    }
    if (InterlockedIncrement(&skips) <= 8)
        slog("early trigger skipped: KERNEL32!CreateMutexW from %p (%s) name '%ls' is not the game's single-instance "
             "mutex from plain game code",
             ret, sec ? sec->name : "outside the exe", name ? name : L"(none)");
    return 0;
}

static void game_mutex_made(HANDLE h, LPCWSTR name)
{
    DWORD err = GetLastError();
    if (h && err == ERROR_ALREADY_EXISTS)
        slog(SECOND_COPY " the game's mutex '%ls' already exists, so another copy of F1 25 is running; F1 25 "
                         "closes a second copy",
             name);
    SetLastError(err);
}

static HANDLE WINAPI trigger_CreateMutexW(LPSECURITY_ATTRIBUTES sa, BOOL owner, LPCWSTR name)
{
    int game_mutex = early_mutex(__builtin_return_address(0), name);
    HANDLE h = ((CreateMutexWFn)iat[IAT_MUTEX].orig)(sa, owner, name);
    if (game_mutex)
        game_mutex_made(h, name);
    return h;
}

static HANDLE WINAPI entry_CreateMutexW(LPSECURITY_ATTRIBUTES sa, BOOL owner, LPCWSTR name)
{
    int game_mutex = early_mutex(__builtin_return_address(0), name);
    HANDLE h = CreateMutexExW(sa, name, owner ? CREATE_MUTEX_INITIAL_OWNER : 0, MUTEX_ALL_ACCESS);
    if (game_mutex)
        game_mutex_made(h, name);
    return h;
}

static int early_flag(void)
{
    wchar_t path[MAX_PATH * 2];
    swprintf(path, MAX_PATH * 2, L"%ls\\swofty\\assets.json", game_dir);
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return -1;
    static char buf[1 << 20];
    DWORD got = 0;
    ReadFile(f, buf, sizeof buf - 1, &got, NULL);
    CloseHandle(f);
    buf[got] = 0;
    const char *p = strstr(buf, "\"early_stage\"");
    if (!p)
        return -1;
    p += 13;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    if (*p++ != ':')
        return -1;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    return !strncmp(p, "true", 4) ? 1 : !strncmp(p, "false", 5) ? 0 : -1;
}

static const char *json_list(const char *buf, const char *key)
{
    size_t k = strlen(key);
    for (const char *p = buf; (p = strstr(p, key)); p += k) {
        const char *q = p + k;
        while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n')
            q++;
        if (*q++ != ':')
            continue;
        while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n')
            q++;
        if (*q == '[')
            return q + 1;
    }
    return NULL;
}

static int read_list(const char *buf, const char *key, wchar_t (*out)[MAX_PATH], int max)
{
    int count = 0;
    for (const char *p = json_list(buf, key); p && *p && *p != ']' && count < max;) {
        const char *q = strchr(p, '"'), *e = q ? strchr(q + 1, '"') : NULL;
        const char *close = strchr(p, ']');
        if (!e || (close && q > close))
            break;
        int n = MultiByteToWideChar(CP_UTF8, 0, q + 1, (int)(e - q - 1), out[count], MAX_PATH - 1);
        out[count][n] = 0;
        if (n)
            count++;
        p = e + 1;
    }
    return count;
}

static int inside(wchar_t *path)
{
    wchar_t wrapped[MAX_PATH + 2];
    for (wchar_t *c = path; *c; c++)
        if (*c == L'/')
            *c = L'\\';
    swprintf(wrapped, MAX_PATH + 2, L"\\%ls\\", path);
    return path[0] != L'\\' && !wcschr(path, L':') && !wcsstr(wrapped, L"\\..\\") && !wcsstr(wrapped, L"\\.\\");
}

static void read_settings(void)
{
    wchar_t path[MAX_PATH * 2];
    swprintf(path, MAX_PATH * 2, L"%ls\\swofty\\settings.json", game_dir);
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return;
    static char buf[4 << 20];
    DWORD got = 0;
    ReadFile(f, buf, sizeof buf - 1, &got, NULL);
    CloseHandle(f);
    buf[got] = 0;
    early_count = read_list(buf, "\"early_plugins\"", early_plugins, 64);
    for (int i = 0; i < early_count; i++)
        slog("early stage: plugin %ls has \"early\": true", early_plugins[i]);
    int n = read_list(buf, "\"plugins\"", plugins, 256);
    for (int i = 0; i < n; i++) {
        if (!inside(plugins[i]))
            slog("addons: plugin %ls in settings.json is not a path inside swofty\\, skipped", plugins[i]);
        else if (plugin_count++ < i)
            wcscpy(plugins[plugin_count - 1], plugins[i]);
    }
    if (plugin_count)
        slog("addons: settings.json lists %d plugin(s) in addon folders", plugin_count);
}

static void read_early_flag(void)
{
    char env[8] = {0};
    if (GetEnvironmentVariableA("SWOFTY_EARLY_STAGE", env, sizeof env) && env[0]) {
        early_enabled = env[0] == '1' || env[0] == 't' || env[0] == 'T';
        early_env = 1;
        snprintf(early_source, sizeof early_source, "%s (SWOFTY_EARLY_STAGE=%s)", early_enabled ? "on" : "off", env);
        return;
    }
    int v = early_flag();
    if (v < 0) {
        snprintf(early_source, sizeof early_source, "off (no \"early_stage\" in swofty\\assets.json)");
        return;
    }
    early_enabled = v;
    snprintf(early_source, sizeof early_source, "%s (\"early_stage\": %s in swofty\\assets.json)", v ? "on" : "off",
             v ? "true" : "false");
}

static DWORD WINAPI worker(LPVOID unused)
{
    (void)unused;
    slog("fallback watcher: waiting for a visible main window if no trigger fires (timeout %d s)",
         WINDOW_TIMEOUT_MS / 1000);
    while (!load_state) {
        HWND w = NULL;
        EnumWindows(find_window, (LPARAM)&w);
        if (w) {
            wchar_t cls[128] = {0};
            GetClassNameW(w, cls, 128);
            load_addons_once("main window visible (fallback, IAT trigger never fired)", cls, STAGE_LATE);
            stage_window("main window visible (fallback)");
            return 0;
        }
        if (GetTickCount() - attach_tick > WINDOW_TIMEOUT_MS &&
            InterlockedCompareExchange(&load_state, 3, 0) == 0) {
            unhook_iat(0);
            slog("fallback watcher: no trigger and no window after %d s, addons NOT loaded", WINDOW_TIMEOUT_MS / 1000);
            if (ea_diag) {
                ediag("fallback: no IAT trigger and no window after %d s; the game never reached the engine/menu",
                      WINDOW_TIMEOUT_MS / 1000);
                check_activation_gate();
                gate3_verdict("timeout", 0);
                sample_code_entropy("timeout (no window)");
            }
            return 0;
        }
        Sleep(POLL_MS);
    }
    while (late_pending && load_state == 1)
        Sleep(POLL_MS);
    if (!late_pending || !loaded_early || window_done || late_armed()) {
        slog("fallback watcher: trigger already fired, exiting");
        return 0;
    }
    slog("fallback watcher: addons loaded at the early stage and no window trigger is armed; waiting for a visible "
         "main window for the window stage");
    while (!window_done) {
        HWND w = NULL;
        EnumWindows(find_window, (LPARAM)&w);
        if (w) {
            stage_window("main window visible (fallback)");
            return 0;
        }
        if (GetTickCount() - attach_tick > WINDOW_TIMEOUT_MS) {
            slog("fallback watcher: no window after %d s, the window stage did not run", WINDOW_TIMEOUT_MS / 1000);
            return 0;
        }
        Sleep(POLL_MS);
    }
    slog("fallback watcher: window stage already ran, exiting");
    return 0;
}

static void capture_sections(void)
{
    image_base = (uintptr_t)GetModuleHandleW(NULL);
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(image_base + ((const IMAGE_DOS_HEADER *)image_base)->e_lfanew);
    const IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections && section_count < 96; i++, s++) {
        Section *d = &sections[section_count++];
        memcpy(d->name, s->Name, 8);
        d->name[8] = 0;
        d->start = image_base + s->VirtualAddress;
        d->size = s->Misc.VirtualSize ? s->Misc.VirtualSize : s->SizeOfRawData;
        d->flags = s->Characteristics;
    }
}

static wchar_t log_path[MAX_PATH * 2], refused_path[MAX_PATH * 2];
static DWORD refused_error;

static HANDLE open_rotated(const wchar_t *dir, const wchar_t *name, int tries, DWORD *error)
{
    wchar_t prev[MAX_PATH * 2];
    CreateDirectoryW(dir, NULL);
    swprintf(log_path, MAX_PATH * 2, L"%ls\\%ls.log", dir, name);
    swprintf(prev, MAX_PATH * 2, L"%ls\\%ls.prev.log", dir, name);
    /* The hook also appends to this file and may hold it open; a failed rotation is fine, we append in place.
       FILE_SHARE_DELETE lets the next run rotate even while we hold it. */
    MoveFileExW(log_path, prev, MOVEFILE_REPLACE_EXISTING);
    HANDLE h = INVALID_HANDLE_VALUE;
    /* The hook appends to the same file and briefly holds it during its own startup; retry past that window. */
    while (tries-- > 0) {
        h = CreateFileW(log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        *error = h == INVALID_HANDLE_VALUE ? GetLastError() : 0;
        if (h != INVALID_HANDLE_VALUE)
            break;
        Sleep(100);
    }
    return h;
}

/* A game started from an EA app without the rights to change its folder (C:\Program Files\EA Games) cannot
   create its log there. The log then goes to the launcher's data folder, %LOCALAPPDATA%\Swofty\state, where
   the session and Send Logs look for it. */
static void open_log(void)
{
    wchar_t dir[MAX_PATH * 2];
    swprintf(dir, MAX_PATH * 2, L"%ls\\swofty", game_dir);
    log_file = open_rotated(dir, L"loader", 20, &refused_error);
    if (log_file != INVALID_HANDLE_VALUE)
        return;
    wcscpy(refused_path, log_path);
    if (!swofty_data_folder(dir, MAX_PATH))
        return;
    CreateDirectoryW(dir, NULL);
    wcscat(dir, L"\\state");
    DWORD error;
    log_file = open_rotated(dir, L"loader-fallback", 1, &error);
}

static const char *integrity_name(DWORD rid)
{
    return rid >= SECURITY_MANDATORY_SYSTEM_RID   ? "system"
           : rid >= SECURITY_MANDATORY_HIGH_RID   ? "high"
           : rid >= SECURITY_MANDATORY_MEDIUM_RID ? "medium"
           : rid >= SECURITY_MANDATORY_LOW_RID    ? "low"
                                                  : "untrusted";
}

/* What a report needs to tell why the game folder refused the log: the error, and the rights the game runs
   with next to the launcher's. */
static void log_refused(void)
{
    slog("log: %ls could not be opened (error %lu); this is the fallback log %ls", refused_path, refused_error,
         log_path);
    char level[16] = "unknown", sid[192] = "unknown", name[64] = "(unset)", layers[256] = "(unset)";
    TOKEN_ELEVATION_TYPE type = 0;
    DWORD virt = 0, n;
    HANDLE t;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t)) {
        union {
            TOKEN_MANDATORY_LABEL label;
            TOKEN_USER user;
            BYTE raw[256];
        } info;
        if (GetTokenInformation(t, TokenIntegrityLevel, &info, sizeof info, &n)) {
            PSID s = info.label.Label.Sid;
            snprintf(level, sizeof level, "%s", integrity_name(*GetSidSubAuthority(s, *GetSidSubAuthorityCount(s) - 1)));
        }
        GetTokenInformation(t, TokenElevationType, &type, sizeof type, &n);
        GetTokenInformation(t, TokenVirtualizationEnabled, &virt, sizeof virt, &n);
        char *text;
        if (GetTokenInformation(t, TokenUser, &info, sizeof info, &n) && ConvertSidToStringSidA(info.user.User.Sid, &text)) {
            snprintf(sid, sizeof sid, "%s", text);
            LocalFree(text);
        }
        CloseHandle(t);
    }
    DWORD k = GetEnvironmentVariableA("USERNAME", name, sizeof name);
    if (!k || k >= sizeof name)
        strcpy(name, "(unset)");
    k = GetEnvironmentVariableA("__COMPAT_LAYER", layers, sizeof layers);
    if (!k || k >= sizeof layers)
        strcpy(layers, k ? "(too long)" : "(unset)");
    slog("process: user %s (%s), %s integrity, %s, UAC virtualization %s, __COMPAT_LAYER=%s", name, sid, level,
         type == TokenElevationTypeFull      ? "elevated"
         : type == TokenElevationTypeLimited ? "not elevated (an administrator's limited token)"
                                             : "not elevated",
         virt ? "on" : "off", layers);
}

/* Hook trampolines must sit within 2 GB of the code they patch. On a build whose core loads at the window
   stage the game has filled the address space around the image by then, so the pool is reserved here, at
   attach, and the core takes it over (SwoftyLoaderHookPool). */
#define HOOK_POOL_SIZE (2u << 20)
#define HOOK_POOL_REACH 0x40000000ULL
static uintptr_t hook_pool;

static uintptr_t reserve_at(uintptr_t at)
{
    return (uintptr_t)VirtualAlloc((void *)at, HOOK_POOL_SIZE, MEM_RESERVE, PAGE_NOACCESS);
}

static void reserve_hook_pool(void)
{
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(image_base + ((const IMAGE_DOS_HEADER *)image_base)->e_lfanew);
    uintptr_t end = image_base + nt->OptionalHeader.SizeOfImage;
    MEMORY_BASIC_INFORMATION m;
    for (uintptr_t p = end; !hook_pool && p < end + HOOK_POOL_REACH && VirtualQuery((void *)p, &m, sizeof m);
         p = (uintptr_t)m.BaseAddress + m.RegionSize) {
        uintptr_t at = (p + 0xFFFF) & ~(uintptr_t)0xFFFF;
        if (m.State == MEM_FREE && at + HOOK_POOL_SIZE <= (uintptr_t)m.BaseAddress + m.RegionSize)
            hook_pool = reserve_at(at);
    }
    uintptr_t lo = image_base > HOOK_POOL_REACH + 0x10000 ? image_base - HOOK_POOL_REACH : 0x10000;
    for (uintptr_t at = (image_base - HOOK_POOL_SIZE) & ~(uintptr_t)0xFFFF;
         !hook_pool && at >= lo && at < image_base && VirtualQuery((void *)at, &m, sizeof m);) {
        if (m.State == MEM_FREE && m.RegionSize >= HOOK_POOL_SIZE && (hook_pool = reserve_at(at)))
            break;
        uintptr_t next = ((m.State == MEM_FREE ? at + m.RegionSize : (uintptr_t)m.AllocationBase) - HOOK_POOL_SIZE) &
                         ~(uintptr_t)0xFFFF;
        at = next < at ? next : at - 0x10000;
    }
    if (hook_pool)
        slog("hook pool: %u KB reserved at %p (%s the image) for the core's hook trampolines", HOOK_POOL_SIZE >> 10,
             (void *)hook_pool, hook_pool >= end ? "above" : "below");
    else
        slog("hook pool: no free %u KB within 1 GB of the image at attach; the core searches for trampoline memory "
             "itself", HOOK_POOL_SIZE >> 10);
}

__declspec(dllexport) int SwoftyLoaderHookPool(uintptr_t *at, size_t *size)
{
    uintptr_t p = (uintptr_t)InterlockedExchangePointer((void *volatile *)&hook_pool, NULL);
    if (!p)
        return 0;
    *at = p;
    *size = HOOK_POOL_SIZE;
    return 1;
}

static void log_build(void)
{
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(image_base + ((const IMAGE_DOS_HEADER *)image_base)->e_lfanew);
    WIN32_FILE_ATTRIBUTE_DATA fa;
    unsigned long long size = 0;
    if (GetFileAttributesExW(exe_path, GetFileExInfoStandard, &fa))
        size = ((unsigned long long)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
    unsigned long stamp = nt->FileHeader.TimeDateStamp;
    int ok = size == EXPECTED_SIZE && stamp == EXPECTED_STAMP;
    /* Engage the build-mismatch diagnostics only for a real, large game build that is not the analysed
       one (the EA App exe is ~327 MiB); never for a small host such as the test harness's fake exe. */
    ea_diag = !ok && size >= ((unsigned long long)64 << 20);
    slog("exe: size %llu (analysed %llu), PE timestamp %lu (analysed %lu) -> %s", size, EXPECTED_SIZE, stamp,
         EXPECTED_STAMP, ok ? "MATCHES the analysed build" : "DIFFERS from the analysed build, signatures may fail");
    slog("exe: image base %p, %d sections", (void *)image_base, section_count);
    for (int i = 0; i < section_count; i++)
        slog("  section %-8s %p size 0x%llx flags 0x%08lx", sections[i].name, (void *)sections[i].start,
             (unsigned long long)sections[i].size, sections[i].flags);
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_DETACH)
        alog_flush_at_exit();
    if (reason != DLL_PROCESS_ATTACH)
        return TRUE;
    DisableThreadLibraryCalls(self);

    wchar_t sys[MAX_PATH * 2];
    UINT len = GetSystemDirectoryW(sys, MAX_PATH);
    wcscpy(sys + len, L"\\version.dll");
    HMODULE real = LoadLibraryW(sys);
    if (real == self)
        real = NULL;
    int missing = 0;
    for (size_t i = 0; i < NFWD; i++) {
        swofty_fwd[i] = real ? (void *)GetProcAddress(real, fwd_names[i]) : NULL;
        if (!swofty_fwd[i]) {
            swofty_fwd[i] = (void *)fwd_missing;
            missing++;
        }
    }

    GetModuleFileNameW(NULL, exe_path, MAX_PATH);
    wchar_t *slash = wcsrchr(exe_path, L'\\');
    const wchar_t *exe_name = slash ? slash + 1 : exe_path;
    if (_wcsicmp(exe_name, HOST_EXE) != 0)
        return TRUE;

    wcscpy(game_dir, exe_path);
    game_dir[slash ? slash - exe_path : 0] = 0;
    InitializeCriticalSection(&log_lock);
    open_log();
    {
        char sync[8] = {0};
        DWORD n = GetEnvironmentVariableA("SWOFTY_LOG_SYNC", sync, sizeof sync);
        log_async = !(n && n < sizeof sync && sync[0] && sync[0] != '0') && alog_start(log_file);
    }
    capture_sections();

    wchar_t self_path[MAX_PATH] = L"?", real_path[MAX_PATH] = L"(not loaded)";
    GetModuleFileNameW(self, self_path, MAX_PATH);
    if (real)
        GetModuleFileNameW(real, real_path, MAX_PATH);
    slog("swofty loader (version.dll proxy) attached, pid %lu", GetCurrentProcessId());
    if (refused_error)
        log_refused();
    slog("log: %s", log_async ? "asynchronous writer (batched, crash lines flush at once; SWOFTY_LOG_SYNC=1 writes each line synchronously)"
                              : "synchronous, one write per line");
    slog("host: %ls", exe_path);
    slog("proxy: %ls", self_path);
    slog("real version.dll: %ls (requested %ls)", real_path, sys);
    slog("forwards: %d of %d resolved", (int)NFWD - missing, (int)NFWD);
    for (size_t i = 0; i < NFWD; i++)
        if (swofty_fwd[i] == (void *)fwd_missing)
            slog("  forward %s missing in the real dll (calls return 0)", fwd_names[i]);
    log_build();
    reserve_hook_pool();
    attach_tick = GetTickCount();
    if (ea_diag) {
        SetEnvironmentVariableW(L"SWOFTY_EA_DIAG", L"1");
        open_diag_log();
        ediag("== Swofty Launcher EA-build self-diagnostics ==");
        ediag("host %ls, pid %lu; this exe does NOT match the analysed (Steam) build, so build-mismatch diagnostics "
              "engage automatically (the friend enables nothing); lines go here and to loader.log",
              exe_path, GetCurrentProcessId());
        {
            const IMAGE_NT_HEADERS *nt =
                (const IMAGE_NT_HEADERS *)(image_base + ((const IMAGE_DOS_HEADER *)image_base)->e_lfanew);
            WIN32_FILE_ATTRIBUTE_DATA fa;
            unsigned long long size = 0;
            if (GetFileAttributesExW(exe_path, GetFileExInfoStandard, &fa))
                size = ((unsigned long long)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
            const Section *cs = code_section();
            ediag("build: size %llu (analysed Steam %llu), PE timestamp %lu (analysed %lu), image base %p, %d sections",
                  size, (unsigned long long)EXPECTED_SIZE, (unsigned long)nt->FileHeader.TimeDateStamp,
                  (unsigned long)EXPECTED_STAMP, (void *)image_base, section_count);
            for (int i = 0; i < section_count; i++)
                ediag("  section %-8s %p size 0x%llx flags 0x%08lx%s", sections[i].name, (void *)sections[i].start,
                      (unsigned long long)sections[i].size, (unsigned long)sections[i].flags,
                      cs == &sections[i] ? "  <- engine code section" : "");
        }
        ediag("gate 1 (Javelin launch guard, parent 'Game launch error 0x%%08x'): PASS (executing inside %ls, pid "
              "%lu; the parent allowed the child to start; offline-play handles this gate upstream)",
              HOST_EXE, GetCurrentProcessId());
        check_activation_gate();
        gate3_verdict("attach", 0);
        sample_code_entropy("attach / core-init");
        AddVectoredExceptionHandler(1, loader_veh);
        prev_filter = SetUnhandledExceptionFilter(loader_unhandled);
        ediag("crash handler armed at loader attach: a fatal fault or the ~16s pre-window exit is logged as "
              "'CRASH ... module+RVA, exception' before the core even loads");
    }
    read_early_flag();
    slog("early stage: %s", early_source);
    read_settings();
    if (early_count && !early_enabled && !early_env) {
        early_enabled = 1;
        slog("early stage: on for %d early plugin(s); SwoftyMod loads with them", early_count);
    }
    if (ea_diag && !early_enabled) {
        early_enabled = 1;
        ediag("forcing early-stage core load on this non-matching build so the core's crash handler and per-binding "
              "resolver engage before any pre-window crash (report-only: nothing resolves on encrypted code, so no "
              "mixin is installed)");
    }
    if (early_enabled)
        loaded_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    hook_iat();

    HANDLE t = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    if (t) {
        slog("fallback watcher thread started");
        CloseHandle(t);
    } else {
        slog("CreateThread failed, error %lu; relying on the IAT trigger only", GetLastError());
    }
    return TRUE;
}
