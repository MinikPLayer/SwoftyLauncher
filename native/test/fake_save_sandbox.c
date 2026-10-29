// Fake game for the save sandbox Wine test: a NeSaveSystem2Win-shaped save device (vtable slot +0x08 =
// the start, Steam helper at +0x30 with the remote storage interface at +0x108, m_storeInCloud at +0x3e8)
// and stand-ins for Ne_SaveSystem2Win_Start, _Mount and the corrupt-save upload, each with a tagged
// prologue (mov rax, "SWFT....") that run_save_sandbox_test.sh writes into bindings.json. The "Steam"
// interface writes into STEAM_DIR and logs every call to its log, so the test sees whether anything
// reached it. The early trigger loads the loader and the core; then the game starts its device and
// saves, loads, lists and deletes through helper+0x108, as the real device does.
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
static void expect(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  :" : "FAIL:", what);
    fails += !ok;
}

static char steam_dir[MAX_PATH], steam_log[MAX_PATH];
static int steam_calls;

static void steam_note(const char *what, const char *name)
{
    steam_calls++;
    FILE *f = fopen(steam_log, "a");
    if (f) {
        fprintf(f, "REAL STEAM %s %s\n", what, name ? name : "");
        fclose(f);
    }
}

static int steam_write(void *self, const char *name, const void *data, int32_t size)
{
    (void)self;
    steam_note("FileWrite", name);
    char p[MAX_PATH];
    snprintf(p, sizeof p, "%s\\%s", steam_dir, name);
    FILE *f = fopen(p, "wb");
    if (!f)
        return 0;
    fwrite(data, 1, (size_t)size, f);
    fclose(f);
    return 1;
}

static int32_t steam_read(void *self, const char *name, void *data, int32_t size)
{
    (void)self, (void)data, (void)size;
    steam_note("FileRead", name);
    return 0;
}

static uint64_t steam_any(void *self)
{
    (void)self;
    steam_note("other slot", NULL);
    return 0;
}

static int32_t steam_count(void *self)
{
    (void)self;
    steam_note("GetFileCount", NULL);
    return 0;
}

typedef struct Iface {
    void **vtable;
} Iface;

static Iface steam_iface;

static uint8_t device[0x3f0], helper[0x110];
static void *device_vt[16];

uint64_t start_impl(void *dev)
{
    (void)dev;
    printf("      the game's own start ran\n");
    return 0;
}

static int mounts;
uint32_t mount_impl(void *dev, void *op, void *save, uint8_t for_saving, uint8_t a5, uint8_t *created)
{
    (void)dev, (void)op, (void)save, (void)a5;
    mounts++;
    if (created)
        *created = for_saving;
    return 0;
}

static int upload_body_ran;
int upload_impl(void *dev, void *op, void *a3)
{
    (void)dev, (void)op, (void)a3;
    upload_body_ran = 1;
    Iface *global = &steam_iface;
    ((uint64_t(*)(void *, const char *))global->vtable[7])(global, "corrupt_saves#76561198000000000.zip");
    return 0;
}

uint64_t fake_start(void *dev);
uint32_t fake_mount(void *dev, void *op, void *save, uint8_t for_saving, uint8_t a5, uint8_t *created);
int fake_upload(void *dev, void *op, void *a3);

#define TAG(a) ".byte 0x48,0xB8,0x53,0x57,0x46,0x54," a "\n"
__asm__(".section .xtls,\"xr\"\n"
        ".globl fake_start\nfake_start:\n" TAG("0x53,0x56,0x53,0x54") "jmp start_impl\n"
        ".globl fake_mount\nfake_mount:\n" TAG("0x53,0x56,0x4D,0x54") "jmp mount_impl\n"
        ".globl fake_upload\nfake_upload:\n" TAG("0x53,0x56,0x55,0x50") "jmp upload_impl\n"
        ".text\n");

typedef int (*Write_t)(void *, const char *, const void *, int32_t);
typedef int32_t (*Read_t)(void *, const char *, void *, int32_t);
typedef int (*Name_t)(void *, const char *);
typedef int32_t (*Size_t)(void *, const char *);
typedef int64_t (*Time_t)(void *, const char *);
typedef int32_t (*Count_t)(void *);
typedef const char *(*NameAt_t)(void *, int, int32_t *);
typedef int (*Batch_t)(void *);
typedef uint64_t (*Share_t)(void *, const char *);

static void *slot(void *iface, int i)
{
    return (*(void ***)iface)[i];
}

static uint8_t data_file[65536], index_file[416];

static void through_the_interface(void)
{
    void *rs = *(void **)(helper + 0x108);
    expect(rs != &steam_iface, "the device's remote storage is no longer Steam's interface");
    for (size_t i = 0; i < sizeof data_file; i++)
        data_file[i] = (uint8_t)(i * 2654435761u >> 13);
    for (size_t i = 0; i < sizeof index_file; i++)
        index_file[i] = (uint8_t)(i * 40503u >> 3);
    expect(((Batch_t)slot(rs, 57))(rs), "BeginFileWriteBatch");
    expect(((Write_t)slot(rs, 0))(rs, "savegame@profile#NXDSMWW.BWW", data_file, sizeof data_file),
           "FileWrite the profile data file");
    expect(((Write_t)slot(rs, 0))(rs, "savegame@profile#QKRHMYXE", index_file, sizeof index_file),
           "FileWrite the profile index");
    expect(((Write_t)slot(rs, 0))(rs, "savegame@profile_backup#QKRHMYXE", index_file, 16), "FileWrite a backup");
    expect(((Batch_t)slot(rs, 58))(rs), "EndFileWriteBatch");
    expect(!((Write_t)slot(rs, 0))(rs, "..\\escape", index_file, 16), "FileWrite of a name that leaves the folder fails");
    expect(((Count_t)slot(rs, 18))(rs) == 3, "GetFileCount lists the three files");
    int32_t size = -1;
    const char *n0 = ((NameAt_t)slot(rs, 19))(rs, 0, &size);
    expect(!strcmp(n0, "savegame@profile#NXDSMWW.BWW") && size == (int32_t)sizeof data_file,
           "GetFileNameAndSize(0) is the data file");
    expect(((Name_t)slot(rs, 13))(rs, "savegame@profile#QKRHMYXE"), "FileExists");
    expect(((Size_t)slot(rs, 15))(rs, "savegame@profile#QKRHMYXE") == (int32_t)sizeof index_file, "GetFileSize");
    expect(((Time_t)slot(rs, 16))(rs, "savegame@profile#QKRHMYXE") > 1700000000, "GetFileTimestamp in Unix seconds");
    static uint8_t back[65536 + 64];
    expect(((Read_t)slot(rs, 1))(rs, "savegame@profile#NXDSMWW.BWW", back, sizeof back) == (int32_t)sizeof data_file &&
               !memcmp(back, data_file, sizeof data_file),
           "FileRead returns the data file byte for byte");
    expect(((Name_t)slot(rs, 6))(rs, "savegame@profile_backup#QKRHMYXE"), "FileDelete");
    expect(((Count_t)slot(rs, 18))(rs) == 2, "GetFileCount after the delete");
    expect(((Share_t)slot(rs, 7))(rs, "savegame@profile#QKRHMYXE") == 0, "FileShare is not served: it answers 0");
    uint8_t created = 9;
    expect(fake_mount(device, NULL, NULL, 1, 0, &created) == 0 && mounts == 1, "a mount for saving goes through");
    expect(fake_upload(device, NULL, NULL) == 0 && !upload_body_ran, "the corrupt-save upload does not run");
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *sd = getenv("T_STEAM_DIR");
    snprintf(steam_dir, sizeof steam_dir, "%s", sd ? sd : "steam-remote");
    snprintf(steam_log, sizeof steam_log, "%s\\steam.log", steam_dir);
    CreateDirectoryA(steam_dir, NULL);
    static void *svt[64];
    for (int i = 0; i < 64; i++)
        svt[i] = (void *)steam_any;
    svt[0] = (void *)steam_write;
    svt[1] = (void *)steam_read;
    svt[18] = (void *)steam_count;
    void **heap_vt = malloc(sizeof svt);
    memcpy(heap_vt, svt, sizeof svt);
    steam_iface.vtable = heap_vt;

    for (int i = 0; i < 16; i++)
        device_vt[i] = (void *)start_impl;
    device_vt[1] = (void *)fake_start;
    *(void ***)device = device_vt;
    if (!getenv("T_NO_HELPER"))
        *(uint8_t **)(device + 0x30) = helper;
    device[0x3e8] = getenv("T_LOCAL") ? 0 : 1;
    strcpy((char *)helper, "C:\\Program Files (x86)\\Steam\\userdata\\1\\3059520\\local");
    *(Iface **)(helper + 0x108) = &steam_iface;

    DWORD dummy;
    expect(GetFileVersionInfoSizeW(L"C:\\windows\\system32\\kernel32.dll", &dummy) != 0, "version.dll forwarding works");
    HANDLE early = CreateMutexW(NULL, FALSE, L"Codemasters F1 25");
    expect(early && GetModuleHandleW(L"swofty_core.dll"), "the early trigger loaded the core");
    WNDCLASSEXW wc = {.cbSize = sizeof wc, .lpfnWndProc = DefWindowProcW, .hInstance = GetModuleHandleW(NULL),
                      .lpszClassName = L"EGO_WindowClass"};
    extern void *__imp_RegisterClassExW;
    ATOM(WINAPI * rc)(const WNDCLASSEXW *) = (void *)__imp_RegisterClassExW;
    rc(&wc);
    printf("      starting the save device\n");
    ((uint64_t(*)(void *))device_vt[1])(device);
    if (getenv("T_EXPECT_NORMAL")) {
        void *rs = *(void **)(helper + 0x108);
        expect(rs == &steam_iface, "no test run: the device keeps Steam's interface");
        ((Write_t)slot(rs, 0))(rs, "savegame@profile#QKRHMYXE", index_file, sizeof index_file);
        expect(steam_calls == 1, "no test run: a save goes to Steam");
        expect(fake_upload(device, NULL, NULL) == 0 && upload_body_ran, "no test run: the upload is the game's own");
    } else {
        through_the_interface();
        expect(steam_calls == 0, "nothing reached Steam");
    }
    if (getenv("T_LINGER_MS"))
        Sleep((DWORD)atoi(getenv("T_LINGER_MS")));
    printf("%s (%d failure(s))\n", fails ? "SOME FAILED" : "ALL PASSED", fails);
    fflush(stdout);
    ExitProcess(fails ? 1 : 0);
}
