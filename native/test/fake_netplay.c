// Fake game for the netplay bridge test. Before the window trigger loads the core it puts spies into its
// own sendto (both ws2_32 import tables), recvfrom and GetAdaptersAddresses import slots, so the
// bridge hooks on top of them the way it hooks on top of the socket trace: a spy either passes the call
// to ws2_32/iphlpapi or records what the bridge handed down and answers with a made-up result and last
// error. The real flow binds the game socket to 21414 through the ordinal table, sends to 10.77.0.2
// through the ordinal table's sendto, has a helper socket at 127.0.0.1:BASE+2 answer from that port, and
// reads the answer with recvfrom. FAKE_NETPLAY_HOOKS=1 says the bridge should be armed, FAKE_NETPLAY_ROOM=1
// that it should be in a room, and FAKE_NETPLAY_SELF/FAKE_NETPLAY_BASE carry the member number and base.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

__declspec(dllimport) int WSAAPI t2_bind(SOCKET, const struct sockaddr *, int);
__declspec(dllimport) int WSAAPI t2_sendto(SOCKET, const char *, int, int, const struct sockaddr *, int);
__declspec(dllimport) int WSAAPI t2_closesocket(SOCKET);

typedef int(WSAAPI *SendToFn)(SOCKET, const char *, int, int, const struct sockaddr *, int);
typedef int(WSAAPI *RecvFromFn)(SOCKET, char *, int, int, struct sockaddr *, int *);
typedef int(WSAAPI *BindFn)(SOCKET, const struct sockaddr *, int);
typedef ULONG(WINAPI *GaaFn)(ULONG, ULONG, PVOID, PIP_ADAPTER_ADDRESSES, PULONG);

enum { SPY_PASS, SPY_FAKE };

static SendToFn real_sendto;
static RecvFromFn real_recvfrom;
static BindFn real_bind;
static GaaFn real_gaa;

static int send_mode, recv_mode, gaa_mode;
static int spy_ret;
static DWORD spy_err;
static unsigned char sent_to[128];
static int sent_tolen;
static const void *sent_to_ptr;
static unsigned char fake_from[128];
static int fake_fromlen;

static int WSAAPI spy_sendto(SOCKET s, const char *buf, int len, int flags, const struct sockaddr *to, int tolen)
{
    if (send_mode == SPY_PASS)
        return real_sendto(s, buf, len, flags, to, tolen);
    sent_tolen = tolen;
    sent_to_ptr = to;
    memset(sent_to, 0, sizeof sent_to);
    if (to && tolen > 0)
        memcpy(sent_to, to, tolen < (int)sizeof sent_to ? (size_t)tolen : sizeof sent_to);
    SetLastError(spy_err);
    return spy_ret;
}

static int WSAAPI spy_recvfrom(SOCKET s, char *buf, int len, int flags, struct sockaddr *from, int *fromlen)
{
    if (recv_mode == SPY_PASS)
        return real_recvfrom(s, buf, len, flags, from, fromlen);
    memcpy(from, fake_from, (size_t)fake_fromlen);
    *fromlen = fake_fromlen;
    SetLastError(spy_err);
    return spy_ret;
}

#define NADAPTERS 5
#define NUNICAST 6
static const unsigned char fake_ips[NUNICAST][4] = {{192, 168, 50, 10}, {127, 0, 0, 1},    {192, 168, 50, 13},
                                                    {192, 168, 50, 14}, {192, 168, 50, 15}, {0, 0, 0, 0}};

static ULONG fake_list_size(void)
{
    return NADAPTERS * sizeof(IP_ADAPTER_ADDRESSES) + NUNICAST * sizeof(IP_ADAPTER_UNICAST_ADDRESS) +
           NUNICAST * sizeof(struct sockaddr_in);
}

// Down ethernet, Up loopback, Up ethernet without a unicast address, Up wifi with two (the one the
// game takes), Up ethernet after it.
static void fake_list(IP_ADAPTER_ADDRESSES *a)
{
    memset(a, 0, fake_list_size());
    IP_ADAPTER_UNICAST_ADDRESS *u = (IP_ADAPTER_UNICAST_ADDRESS *)(a + NADAPTERS);
    struct sockaddr_in *sa = (struct sockaddr_in *)(u + NUNICAST);
    static const ULONG oper[NADAPTERS] = {IfOperStatusDown, IfOperStatusUp, IfOperStatusUp, IfOperStatusUp, IfOperStatusUp};
    static const ULONG type[NADAPTERS] = {IF_TYPE_ETHERNET_CSMACD, IF_TYPE_SOFTWARE_LOOPBACK, IF_TYPE_ETHERNET_CSMACD,
                                          IF_TYPE_IEEE80211, IF_TYPE_ETHERNET_CSMACD};
    static const int first_uni[NADAPTERS] = {0, 1, -1, 2, 4};
    for (int i = 0; i < NUNICAST; i++) {
        sa[i].sin_family = AF_INET;
        memcpy(&sa[i].sin_addr, fake_ips[i], 4);
        u[i].Length = sizeof u[i];
        u[i].Address.lpSockaddr = (struct sockaddr *)&sa[i];
        u[i].Address.iSockaddrLength = sizeof sa[i];
    }
    u[2].Next = &u[3];
    for (int i = 0; i < NADAPTERS; i++) {
        a[i].Length = sizeof a[i];
        a[i].IfIndex = 10 + i;
        a[i].OperStatus = oper[i];
        a[i].IfType = type[i];
        a[i].FirstUnicastAddress = first_uni[i] < 0 ? NULL : &u[first_uni[i]];
        a[i].Next = i + 1 < NADAPTERS ? &a[i + 1] : NULL;
    }
}

static ULONG WINAPI spy_gaa(ULONG family, ULONG flags, PVOID reserved, PIP_ADAPTER_ADDRESSES list, PULONG size)
{
    if (gaa_mode == SPY_PASS)
        return real_gaa(family, flags, reserved, list, size);
    SetLastError(spy_err);
    if (!list || *size < fake_list_size()) {
        *size = fake_list_size();
        return ERROR_BUFFER_OVERFLOW;
    }
    fake_list(list);
    return ERROR_SUCCESS;
}

typedef struct {
    ULONG_PTR *slot;
    ULONG_PTR value;
    void *fn;
    int table;
    const char *dll;
} Slot;
static Slot slots[512];
static int nslots;

static void scan(const char *dll, HMODULE lib)
{
    BYTE *base = (BYTE *)GetModuleHandleW(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    int table = 0;
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir->VirtualAddress); d->Name; d++) {
        if (_stricmp((char *)base + d->Name, dll))
            continue;
        table++;
        IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + d->OriginalFirstThunk);
        IMAGE_THUNK_DATA *iat = (IMAGE_THUNK_DATA *)(base + d->FirstThunk);
        for (; names->u1.AddressOfData && nslots < 512; names++, iat++) {
            void *fn = IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)
                           ? (void *)GetProcAddress(lib, MAKEINTRESOURCEA(IMAGE_ORDINAL(names->u1.Ordinal)))
                           : (void *)GetProcAddress(lib, (char *)((IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData))->Name);
            slots[nslots++] = (Slot){&iat->u1.Function, 0, fn, table, dll};
        }
    }
}

static void put(ULONG_PTR *slot, void *value)
{
    DWORD old;
    VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old);
    *slot = (ULONG_PTR)value;
    VirtualProtect(slot, sizeof *slot, old, &old);
}

static int fails;
static void expect(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  :" : "FAIL:", what);
    fails += !ok;
}

static int read_file(const wchar_t *path, char *out, DWORD cap)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                           0, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return -1;
    DWORD n = 0;
    ReadFile(f, out, cap - 1, &n, NULL);
    out[n] = 0;
    CloseHandle(f);
    return (int)n;
}

static struct sockaddr_in in4(unsigned a, unsigned b, unsigned c, unsigned d, unsigned port)
{
    struct sockaddr_in s;
    memset(&s, 0, sizeof s);
    s.sin_family = AF_INET;
    s.sin_addr.s_addr = htonl((a << 24) | (b << 16) | (c << 8) | d);
    s.sin_port = htons((unsigned short)port);
    return s;
}

static int same_in4(const void *p, struct sockaddr_in want)
{
    const struct sockaddr_in *s = p;
    return s->sin_family == AF_INET && s->sin_addr.s_addr == want.sin_addr.s_addr && s->sin_port == want.sin_port;
}

static const char *show(const void *p)
{
    static char b[4][48];
    static int k;
    const struct sockaddr_in *s = p;
    const unsigned char *ip = (const unsigned char *)&s->sin_addr;
    char *o = b[k++ & 3];
    snprintf(o, 48, "%u.%u.%u.%u:%u", ip[0], ip[1], ip[2], ip[3], ntohs(s->sin_port));
    return o;
}

static int self_no, base_port, on, hooked;

static void spy_send(SendToFn via, struct sockaddr_in to, int tolen, struct sockaddr_in want, const char *what)
{
    unsigned char buf[64];
    memset(buf, 0xab, sizeof buf);
    memcpy(buf, &to, sizeof to);
    send_mode = SPY_FAKE;
    spy_ret = 4;
    spy_err = 0;
    sent_tolen = -1;
    int r = via(INVALID_SOCKET, "spy!", 4, 0, (struct sockaddr *)buf, tolen);
    send_mode = SPY_PASS;
    char line[200];
    snprintf(line, sizeof line, "%s: %s with tolen %d reaches ws2_32 as %s (want %s)", what, show(&to), tolen,
             show(sent_to), show(&want));
    expect(r == 4 && sent_tolen == tolen && same_in4(sent_to, want) &&
               !memcmp(sent_to + sizeof to, buf + sizeof to, tolen > (int)sizeof to ? (size_t)tolen - sizeof to : 0) &&
               same_in4(buf, to),
           line);
}

static void spy_recv(struct sockaddr_in from, int fromlen, int ret, DWORD err, struct sockaddr_in want, const char *what)
{
    memset(fake_from, 0, sizeof fake_from);
    memcpy(fake_from, &from, sizeof from);
    fake_fromlen = fromlen;
    recv_mode = SPY_FAKE;
    spy_ret = ret;
    spy_err = err;
    char buf[8];
    struct sockaddr_in got;
    memset(&got, 0, sizeof got);
    int gotlen = sizeof got;
    SetLastError(0x5555);
    int r = recvfrom(INVALID_SOCKET, buf, sizeof buf, 0, (struct sockaddr *)&got, &gotlen);
    DWORD e = GetLastError();
    recv_mode = SPY_PASS;
    char line[200];
    snprintf(line, sizeof line, "%s: from %s (fromlen %d, ret %d) reads as %s (want %s), ret %d, last error %lu", what,
             show(&from), fromlen, ret, show(&got), show(&want), r, e);
    expect(r == ret && e == err && gotlen == fromlen && same_in4(&got, want), line);
}

static unsigned short port_of(SOCKET s)
{
    struct sockaddr_in a;
    int n = sizeof a;
    getsockname(s, (struct sockaddr *)&a, &n);
    return ntohs(a.sin_port);
}

static SOCKET udp_at(struct sockaddr_in at)
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    DWORD ms = 3000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&ms, sizeof ms);
    if (bind(s, (struct sockaddr *)&at, sizeof at)) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

static unsigned short loopback_port_outside(unsigned lo, unsigned hi)
{
    SOCKET held[32];
    int count = 0;
    unsigned short port = 0;
    while (count < 32) {
        SOCKET s = udp_at(in4(127, 0, 0, 1, 0));
        if (s == INVALID_SOCKET)
            break;
        unsigned short p = port_of(s);
        held[count++] = s;
        if (p < lo || p > hi) {
            port = p;
            break;
        }
    }
    while (count > 0)
        closesocket(held[--count]);
    return port;
}

static void from_source(SOCKET game, struct sockaddr_in src, const char *what)
{
    char line[200];
    SOCKET s = udp_at(src);
    if (s == INVALID_SOCKET) {
        printf("skip: %s (cannot bind %s here)\n", what, show(&src));
        return;
    }
    struct sockaddr_in to = in4(127, 0, 0, 1, 21414);
    sendto(s, "other-source!!!!", 16, 0, (struct sockaddr *)&to, sizeof to);
    char buf[64];
    struct sockaddr_in from;
    int fl = sizeof from;
    int n = recvfrom(game, buf, sizeof buf, 0, (struct sockaddr *)&from, &fl);
    if (!src.sin_port)
        src.sin_port = htons(port_of(s));
    snprintf(line, sizeof line, "%s: a datagram from %s reads as from %s", what, show(&src), show(&from));
    expect(n == 16 && !memcmp(buf, "other-source!!!!", 16) && same_in4(&from, src), line);
    closesocket(s);
}

static ULONG first_ip(IP_ADAPTER_ADDRESSES *a)
{
    return a->FirstUnicastAddress && a->FirstUnicastAddress->Address.lpSockaddr
               ? ((struct sockaddr_in *)a->FirstUnicastAddress->Address.lpSockaddr)->sin_addr.s_addr
               : 0;
}

static IP_ADAPTER_ADDRESSES *picked(IP_ADAPTER_ADDRESSES *a)
{
    while (a && !(a->OperStatus == IfOperStatusUp && a->IfType != IF_TYPE_SOFTWARE_LOOPBACK && a->FirstUnicastAddress))
        a = a->Next;
    return a;
}

static void check_fake_adapters(void)
{
    char line[200];
    ULONG size = 0;
    gaa_mode = SPY_FAKE;
    spy_err = 0x6161;
    SetLastError(0);
    ULONG r = GetAdaptersAddresses(AF_INET, 0x2e, NULL, NULL, &size);
    expect(r == ERROR_BUFFER_OVERFLOW && size == fake_list_size() && GetLastError() == 0x6161,
           "the size probe keeps ERROR_BUFFER_OVERFLOW, the size and the last error");
    static IP_ADAPTER_ADDRESSES list[64];
    ULONG small = 64;
    r = GetAdaptersAddresses(AF_INET, 0x2e, NULL, list, &small);
    expect(r == ERROR_BUFFER_OVERFLOW && small == fake_list_size(), "a short buffer keeps ERROR_BUFFER_OVERFLOW");
    size = sizeof list;
    spy_err = 0x6262;
    SetLastError(0);
    r = GetAdaptersAddresses(AF_INET, 0x2e, NULL, list, &size);
    DWORD e = GetLastError();
    expect(r == ERROR_SUCCESS && e == 0x6262, "GetAdaptersAddresses(AF_INET) keeps its result and last error");
    int bad = 0;
    IP_ADAPTER_UNICAST_ADDRESS *u = (IP_ADAPTER_UNICAST_ADDRESS *)(list + NADAPTERS);
    for (int i = 0; i < NUNICAST; i++) {
        unsigned char want[4];
        memcpy(want, fake_ips[i], 4);
        if (on && i == 2)
            want[0] = 10, want[1] = 77, want[2] = 0, want[3] = (unsigned char)self_no;
        struct sockaddr_in *sa = (struct sockaddr_in *)u[i].Address.lpSockaddr;
        const unsigned char *got = (const unsigned char *)&sa->sin_addr;
        if (memcmp(got, want, 4)) {
            bad++;
            printf("  unicast %d is %u.%u.%u.%u, want %u.%u.%u.%u\n", i, got[0], got[1], got[2], got[3], want[0], want[1],
                   want[2], want[3]);
        }
    }
    snprintf(line, sizeof line, "%s (adapter 13's first unicast; down, loopback, no-unicast, later adapters and the second unicast untouched)",
             on ? "only the adapter the game picks reports 10.77.0.<self>" : "no adapter address changes");
    expect(!bad, line);
    size = sizeof list;
    r = GetAdaptersAddresses(AF_UNSPEC, 0x2e, NULL, list, &size);
    struct sockaddr_in *sa = (struct sockaddr_in *)u[2].Address.lpSockaddr;
    expect(r == ERROR_SUCCESS && !memcmp(&sa->sin_addr, fake_ips[2], 4), "GetAdaptersAddresses(AF_UNSPEC) is untouched");
    gaa_mode = SPY_PASS;
}

static void check_real_adapters(void)
{
    char line[200];
    ULONG size = 0;
    ULONG r = GetAdaptersAddresses(AF_INET, 0x2e, NULL, NULL, &size);
    expect(r == ERROR_BUFFER_OVERFLOW && size > 0, "the real size probe answers ERROR_BUFFER_OVERFLOW with a size");
    size += 4096;
    IP_ADAPTER_ADDRESSES *hooked = malloc(size), *plain = malloc(size);
    ULONG hs = size, ps = size;
    ULONG hr = GetAdaptersAddresses(AF_INET, 0x2e, NULL, hooked, &hs);
    ULONG pr = real_gaa(AF_INET, 0x2e, NULL, plain, &ps);
    expect(hr == pr && hr == ERROR_SUCCESS, "the real GetAdaptersAddresses succeeds through the hook");
    if (hr || pr)
        return;
    IP_ADAPTER_ADDRESSES *hp = picked(hooked), *pp = picked(plain);
    if (!pp) {
        printf("skip: no adapter is up with an IPv4 address under this Wine\n");
        return;
    }
    int bad = 0, n = 0;
    for (IP_ADAPTER_ADDRESSES *h = hooked, *p = plain; h || p; h = h ? h->Next : NULL, p = p ? p->Next : NULL, n++) {
        if (!h || !p) {
            bad++;
            break;
        }
        ULONG want = on && p == pp ? htonl(0x0a4d0000u | (ULONG)self_no) : first_ip(p);
        bad += first_ip(h) != want || h->IfIndex != p->IfIndex;
    }
    ULONG was = first_ip(pp);
    const unsigned char *w = (const unsigned char *)&was;
    snprintf(line, sizeof line, "real adapters (%d): adapter %lu %u.%u.%u.%u %s, every other address as plain iphlpapi gives it",
             n, pp->IfIndex, w[0], w[1], w[2], w[3], on ? "reports 10.77.0.<self>" : "is unchanged");
    expect(!bad && hp && hp->IfIndex == pp->IfIndex, line);
}

static int wait_log(const wchar_t *path, const char *needle)
{
    static char text[1 << 20];
    for (int i = 0; i < 300; i++) {
        if (read_file(path, text, sizeof text) > 0 && strstr(text, needle))
            return 1;
        Sleep(50);
    }
    return 0;
}

static int in_core(ULONG_PTR p)
{
    HMODULE core = GetModuleHandleW(L"swofty_core.dll"), m = NULL;
    return core && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                      (LPCWSTR)p, &m) && m == core;
}

int main(void)
{
    char v[32] = "";
    GetEnvironmentVariableA("FAKE_NETPLAY_HOOKS", v, sizeof v);
    hooked = !strcmp(v, "1");
    GetEnvironmentVariableA("FAKE_NETPLAY_ROOM", v, sizeof v);
    on = hooked && !strcmp(v, "1");
    GetEnvironmentVariableA("FAKE_NETPLAY_SELF", v, sizeof v);
    self_no = atoi(v);
    GetEnvironmentVariableA("FAKE_NETPLAY_BASE", v, sizeof v);
    base_port = atoi(v);
    GetEnvironmentVariableA("SWOFTY_NET_TRACE", v, sizeof v);
    int traced = v[0] && strcmp(v, "0");
    wchar_t root[MAX_PATH], log_path[MAX_PATH], status_path[MAX_PATH];
    GetModuleFileNameW(NULL, root, MAX_PATH);
    *(wcsrchr(root, L'\\') + 1) = 0;
    swprintf(log_path, MAX_PATH, L"%lsswofty\\loader.log", root);
    swprintf(status_path, MAX_PATH, L"%lsswofty\\netplay.json", root);

    DWORD dummy;
    expect(GetFileVersionInfoSizeW(L"C:\\windows\\system32\\kernel32.dll", &dummy) != 0, "version.dll forwarding works");
    WSADATA wd;
    expect(WSAStartup(MAKEWORD(2, 2), &wd) == 0, "WSAStartup");
    HMODULE ws = GetModuleHandleW(L"ws2_32.dll"), ip = GetModuleHandleW(L"iphlpapi.dll");
    real_sendto = (SendToFn)(void *)GetProcAddress(ws, "sendto");
    real_recvfrom = (RecvFromFn)(void *)GetProcAddress(ws, "recvfrom");
    real_bind = (BindFn)(void *)GetProcAddress(ws, "bind");
    real_gaa = (GaaFn)(void *)GetProcAddress(ip, "GetAdaptersAddresses");
    scan("ws2_32.dll", ws);
    scan("iphlpapi.dll", ip);
    int spied = 0, binds = 0;
    for (int i = 0; i < nslots; i++) {
        Slot *s = &slots[i];
        if (s->fn == (void *)real_sendto)
            put(s->slot, (void *)spy_sendto), spied++;
        else if (s->fn == (void *)real_recvfrom)
            put(s->slot, (void *)spy_recvfrom), spied++;
        else if (s->fn == (void *)real_gaa)
            put(s->slot, (void *)spy_gaa), spied++;
        binds += s->fn == (void *)real_bind;
        s->value = *s->slot;
    }
    expect(spied == 4 && binds == 2, "spies in both sendto slots, recvfrom and GetAdaptersAddresses; bind in both tables");

    WNDCLASSEXW wc = {.cbSize = sizeof wc, .lpfnWndProc = DefWindowProcW, .hInstance = GetModuleHandleW(NULL),
                      .lpszClassName = L"EGO_WindowClass"};
    extern void *__imp_RegisterClassExW;
    ATOM(WINAPI * rc)(const WNDCLASSEXW *) = (void *)__imp_RegisterClassExW;
    rc(&wc);
    expect(GetModuleHandleW(L"swofty_core.dll") != NULL, "the window trigger loaded the core");
    expect(wait_log(log_path, hooked ? "[netplay] bridge armed" : "[core] LAN Game entry in Local Multiplayer"),
           hooked ? "the bridge logged that it is armed" : "netplay setup ran");
    Sleep(hooked ? 0 : 200);
    if (!hooked) {
        static char text[1 << 20];
        expect(read_file(log_path, text, sizeof text) > 0 && !strstr(text, "[netplay]"),
               "without SWOFTY_NETPLAY the log has no [netplay] lines");
    }

    int bad = 0, changes = 0;
    for (int i = 0; i < nslots; i++) {
        Slot *s = &slots[i];
        int bridged = s->fn == (void *)real_sendto || s->fn == (void *)real_recvfrom || s->fn == (void *)real_gaa ||
                      s->fn == (void *)real_bind;
        int changed = *s->slot != s->value;
        changes += changed;
        if (hooked && bridged && !in_core(*s->slot)) {
            bad++;
            printf("  %s table %d slot %d does not point into the core\n", s->dll, s->table, i);
        } else if ((!hooked || !bridged) && !traced && changed) {
            bad++;
            printf("  %s table %d slot %d changed\n", s->dll, s->table, i);
        }
    }
    char line[256];
    snprintf(line, sizeof line, "%s (%d slot(s) changed)",
             hooked ? "the six bridged slots point into the core" : "no import slot changed", changes);
    expect(!bad, line);

    SOCKET game = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    DWORD ms = 3000;
    setsockopt(game, SOL_SOCKET, SO_RCVTIMEO, (const char *)&ms, sizeof ms);
    struct sockaddr_in gaddr = in4(127, 0, 0, 1, 21414);
    SetLastError(0x4242);
    int br = t2_bind(game, (struct sockaddr *)&gaddr, sizeof gaddr);
    DWORD be = GetLastError();
    expect(br == 0, "bind the game socket to 127.0.0.1:21414 through the ordinal table");
    SOCKET tmp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in any = in4(127, 0, 0, 1, 0);
    SetLastError(0x4242);
    real_bind(tmp, (struct sockaddr *)&any, sizeof any);
    DWORD de = GetLastError();
    closesocket(tmp);
    snprintf(line, sizeof line, "the game socket's bind leaves the last error as plain bind does (%lu, plain %lu)", be, de);
    expect(be == de, line);
    char status[256];
    int sn = read_file(status_path, status, sizeof status);
    if (hooked) {
        char want[160];
        snprintf(want, sizeof want, "{\"pid\":%lu,\"self\":%d,\"base\":%d,\"game_port\":21414}\n", GetCurrentProcessId(),
                 on ? self_no : 0, on ? base_port : 0);
        snprintf(line, sizeof line, "netplay.json is %s", want);
        line[strlen(line) - 1] = 0;
        expect(sn > 0 && !strcmp(status, want), line);
    } else {
        expect(sn < 0, "no netplay.json");
    }

    struct sockaddr_in v2 = in4(10, 77, 0, 2, 21414), m2 = in4(127, 0, 0, 1, (unsigned)base_port + 2);
    SOCKET helper = udp_at(m2);
    expect(helper != INVALID_SOCKET, "the member 2 helper binds 127.0.0.1:BASE+2");
    char buf[64];
    struct sockaddr_in from;
    int fl;
    if (on && bad) {
        printf("skip: the real flow (the bridge is not in the slots, so it would send to 10.77.0.2)\n");
    } else if (on) {
        int n = t2_sendto(game, "hello-member-two", 16, 0, (struct sockaddr *)&v2, sizeof v2);
        expect(n == 16 && same_in4(&v2, in4(10, 77, 0, 2, 21414)), "the game sends 16 bytes to 10.77.0.2:21414 through the ordinal table");
        fl = sizeof from;
        n = recvfrom(helper, buf, sizeof buf, 0, (struct sockaddr *)&from, &fl);
        snprintf(line, sizeof line, "the helper at BASE+2 got the datagram from the game socket %s", show(&from));
        expect(n == 16 && !memcmp(buf, "hello-member-two", 16) && same_in4(&from, gaddr), line);
        n = sendto(helper, "reply-from-two!!", 16, 0, (struct sockaddr *)&gaddr, sizeof gaddr);
        expect(n == 16, "the helper replies to 127.0.0.1:21414 untouched");
        fl = sizeof from;
        n = recvfrom(game, buf, sizeof buf, 0, (struct sockaddr *)&from, &fl);
        snprintf(line, sizeof line, "the game reads the reply as from %s (want 10.77.0.2:21414)", show(&from));
        expect(n == 16 && !memcmp(buf, "reply-from-two!!", 16) && fl == sizeof from && same_in4(&from, v2), line);
    } else {
        closesocket(helper);
        helper = INVALID_SOCKET;
        spy_send(t2_sendto, v2, sizeof v2, v2, "no room: a send to member 2");
        from_source(game, m2, "no room: member 2's port");
    }
    if (helper != INVALID_SOCKET)
        closesocket(helper);
    from_source(game, in4(127, 0, 0, 1, loopback_port_outside((unsigned)base_port, (unsigned)base_port + 255)),
                "an ephemeral loopback port");
    from_source(game, in4(127, 0, 0, 1, (unsigned)base_port + (unsigned)self_no), "this member's own port BASE+self");
    from_source(game, in4(127, 0, 0, 1, (unsigned)base_port), "BASE+0");
    if (base_port + 255 <= 65535)
        from_source(game, in4(127, 0, 0, 1, (unsigned)base_port + 255), "BASE+255");
    from_source(game, in4(127, 0, 0, 2, (unsigned)base_port + 3), "127.0.0.2:BASE+3");

    struct sockaddr_in vself = in4(10, 77, 0, (unsigned)self_no, 21414);
    struct sockaddr_in m254 = in4(127, 0, 0, 1, (unsigned)base_port + 254), m3 = in4(127, 0, 0, 1, (unsigned)base_port + 3);
    spy_send(t2_sendto, v2, sizeof v2, on ? m2 : v2, "ordinal table");
    spy_send(t2_sendto, v2, 28, on ? m2 : v2, "a longer sockaddr keeps its tail and length");
    spy_send(t2_sendto, in4(10, 77, 0, 254, 21414), 16, on ? m254 : in4(10, 77, 0, 254, 21414), "member 254");
    spy_send(sendto, in4(10, 77, 0, 3, 21414), 16, on ? m3 : in4(10, 77, 0, 3, 21414), "name table");
    spy_send(t2_sendto, vself, 16, vself, "this member's own virtual address");
    spy_send(t2_sendto, in4(10, 77, 0, 2, 21415), 16, in4(10, 77, 0, 2, 21415), "another port");
    spy_send(t2_sendto, in4(10, 77, 0, 0, 21414), 16, in4(10, 77, 0, 0, 21414), "10.77.0.0");
    spy_send(t2_sendto, in4(10, 77, 0, 255, 21414), 16, in4(10, 77, 0, 255, 21414), "10.77.0.255");
    spy_send(t2_sendto, in4(10, 77, 1, 2, 21414), 16, in4(10, 77, 1, 2, 21414), "10.77.1.2");
    spy_send(t2_sendto, v2, 8, v2, "a short tolen");

    struct sockaddr_in v4 = in4(10, 77, 0, 4, 21414);
    send_mode = SPY_FAKE;
    spy_ret = 16;
    spy_err = 0x4242;
    SetLastError(0);
    int r = t2_sendto(game, "first-to-four!!!", 16, 0, (struct sockaddr *)&v4, sizeof v4);
    DWORD e = GetLastError();
    expect(r == 16 && e == 0x4242 && same_in4(&v4, in4(10, 77, 0, 4, 21414)) && (sent_to_ptr != (void *)&v4) == on,
           "the first send to member 4 keeps the result and last error, and the caller's sockaddr");
    spy_ret = SOCKET_ERROR;
    spy_err = WSAEHOSTUNREACH;
    SetLastError(0);
    r = t2_sendto(game, "fail", 4, 0, (struct sockaddr *)&v4, sizeof v4);
    e = GetLastError();
    expect(r == SOCKET_ERROR && e == WSAEHOSTUNREACH, "a failed redirected send keeps SOCKET_ERROR and WSAEHOSTUNREACH");
    send_mode = SPY_PASS;
    SetLastError(0);
    r = t2_sendto(INVALID_SOCKET, "fail", 4, 0, (struct sockaddr *)&v2, sizeof v2);
    e = GetLastError();
    SetLastError(0);
    int pr = real_sendto(INVALID_SOCKET, "fail", 4, 0, (struct sockaddr *)&m2, sizeof m2);
    DWORD pe = GetLastError();
    snprintf(line, sizeof line, "a send on a bad socket fails as plain ws2_32 does (%d/%lu, plain %d/%lu)", r, e, pr, pe);
    expect(r == pr && e == pe && e == WSAENOTSOCK, line);

    struct sockaddr_in v3 = in4(10, 77, 0, 3, 21414);
    spy_recv(m3, 16, 16, 0x4343, on ? v3 : m3, "first from member 3");
    spy_recv(m3, 16, 0, 0x4344, on ? v3 : m3, "an empty datagram");
    spy_recv(m254, 16, 16, 0, on ? in4(10, 77, 0, 254, 21414) : m254, "member 254");
    spy_recv(in4(127, 0, 0, 1, (unsigned)base_port + (unsigned)self_no), 16, 16, 0,
             in4(127, 0, 0, 1, (unsigned)base_port + (unsigned)self_no), "this member's own port");
    spy_recv(in4(127, 0, 0, 1, (unsigned)base_port), 16, 16, 0, in4(127, 0, 0, 1, (unsigned)base_port), "BASE+0");
    spy_recv(in4(127, 0, 0, 2, (unsigned)base_port + 3), 16, 16, 0, in4(127, 0, 0, 2, (unsigned)base_port + 3), "127.0.0.2");
    spy_recv(m3, 8, 16, 0, m3, "a short fromlen");
    spy_recv(m3, 16, SOCKET_ERROR, WSAECONNRESET, m3, "a failed receive");
    if (base_port + 255 <= 65535)
        spy_recv(in4(127, 0, 0, 1, (unsigned)base_port + 255), 16, 16, 0, in4(127, 0, 0, 1, (unsigned)base_port + 255),
                 "BASE+255");

    check_fake_adapters();
    check_real_adapters();

    expect(t2_closesocket(game) == 0, "closesocket the game socket");
    if (hooked)
        expect(read_file(status_path, status, sizeof status) > 0, "netplay.json is still there before exit");
    printf("%s (%d failure(s))\n", fails ? "FAILED" : "ALL PASSED", fails);
    ExitProcess(fails != 0);
}
