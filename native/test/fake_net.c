// Fake game for the socket trace test. The window trigger loads the loader and the core; with
// SWOFTY_NET_TRACE set this file then runs the receive pump's pattern (an overlapped WSARecvFrom
// waited on with WSAWaitForMultipleEvents and completed with WSAGetOverlappedResult) against a second
// socket's sendto, plus the other traced calls, and checks that every result, buffer and last error is
// what plain ws2_32 gives. WSASendTo and WSAEventSelect go through GetProcAddress so they reach the
// trace's inline hooks. t2_bind, t2_sendto and t2_closesocket come from a second ws2_32 import
// descriptor, by ordinal, as the game's .data1 code imports them. Before the trigger it records every
// ws2_32 import slot and the first bytes of every traced export, and after it checks which of them the
// trace changed for the mode it runs in.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

__declspec(dllimport) int WSAAPI t2_bind(SOCKET, const struct sockaddr *, int);
__declspec(dllimport) int WSAAPI t2_sendto(SOCKET, const char *, int, int, const struct sockaddr *, int);
__declspec(dllimport) int WSAAPI t2_closesocket(SOCKET);

static const char *traced[] = {"socket", "WSASocketW", "bind", "connect", "setsockopt", "ioctlsocket", "getsockname",
                               "sendto", "send", "WSASendTo", "recvfrom", "recv", "WSARecvFrom", "WSARecv",
                               "WSAGetOverlappedResult", "WSAWaitForMultipleEvents", "WSAEventSelect",
                               "WSACreateEvent", "WSAIoctl", "closesocket"};
#define NTRACED (sizeof traced / sizeof *traced)

typedef struct {
    void **slot;
    void *value, *fn;
    int table;
} Slot;
static Slot slots[256];
static int nslots, ntables;
static void *exports[NTRACED];
static unsigned char export_bytes[NTRACED][16];

static int fails;
static void expect(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  :" : "FAIL:", what);
    fails += !ok;
}

static void scan(HMODULE ws)
{
    BYTE *base = (BYTE *)GetModuleHandleW(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir->VirtualAddress); d->Name; d++) {
        if (_stricmp((char *)base + d->Name, "ws2_32.dll"))
            continue;
        ntables++;
        IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + d->OriginalFirstThunk);
        IMAGE_THUNK_DATA *iat = (IMAGE_THUNK_DATA *)(base + d->FirstThunk);
        for (; names->u1.AddressOfData && nslots < 256; names++, iat++) {
            void *fn = IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)
                           ? (void *)GetProcAddress(ws, MAKEINTRESOURCEA(IMAGE_ORDINAL(names->u1.Ordinal)))
                           : (void *)GetProcAddress(ws, (char *)((IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData))->Name);
            slots[nslots++] = (Slot){(void **)&iat->u1.Function, (void *)iat->u1.Function, fn, ntables};
        }
    }
    for (size_t i = 0; i < NTRACED; i++) {
        exports[i] = (void *)GetProcAddress(ws, traced[i]);
        if (exports[i])
            memcpy(export_bytes[i], exports[i], 16);
    }
}

static int is_traced(void *fn)
{
    for (size_t i = 0; i < NTRACED; i++)
        if (fn && fn == exports[i])
            return 1;
    return 0;
}

static int imported(void *fn)
{
    for (int i = 0; i < nslots; i++)
        if (slots[i].fn == fn)
            return 1;
    return 0;
}

static void check_patches(HMODULE ws, const char *mode)
{
    int iat = !strcmp(mode, "1"), inl = !strcmp(mode, "inline");
    int bad_slots = 0, hooked = 0, per_table[3] = {0};
    for (int i = 0; i < nslots; i++) {
        int changed = *slots[i].slot != slots[i].value, want = iat && is_traced(slots[i].fn);
        hooked += changed;
        if (changed && slots[i].table < 3)
            per_table[slots[i].table]++;
        if (changed != want) {
            bad_slots++;
            printf("  slot %d of table %d: changed=%d, want %d\n", i, slots[i].table, changed, want);
        }
    }
    char what[160];
    snprintf(what, sizeof what, "every ws2_32 import slot in %d table(s) is as the mode leaves it (%d hooked)", ntables, hooked);
    expect(ntables == 2 && !bad_slots, what);
    if (iat)
        expect(per_table[1] && per_table[2], "both ws2_32 import tables have hooked slots");
    int bad_exports = 0, forwarded = 0;
    for (size_t i = 0; i < NTRACED; i++) {
        if (!exports[i])
            continue;
        HMODULE m = NULL;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)exports[i], &m);
        int inside = m == ws, changed = memcmp(export_bytes[i], exports[i], 16) != 0;
        int want = inside && (inl || (iat && !imported(exports[i])));
        forwarded += !inside;
        if (changed != want) {
            bad_exports++;
            printf("  export %s: inside=%d changed=%d, want %d\n", traced[i], inside, changed, want);
        }
    }
    snprintf(what, sizeof what, "every traced export's code is as the mode leaves it (%d forwarded out of ws2_32, never patched)",
             forwarded);
    expect(!bad_exports, what);
}

static int read_log(const wchar_t *path, char *out, DWORD cap)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return -1;
    DWORD n = 0;
    ReadFile(f, out, cap - 1, &n, NULL);
    out[n] = 0;
    CloseHandle(f);
    return (int)n;
}

static unsigned short port_of(SOCKET s)
{
    struct sockaddr_in a;
    int n = sizeof a;
    getsockname(s, (struct sockaddr *)&a, &n);
    return ntohs(a.sin_port);
}

static struct sockaddr_in loop_addr(unsigned short port)
{
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(port);
    return a;
}

int main(void)
{
    static char text[1 << 20];
    wchar_t log_path[MAX_PATH];
    GetModuleFileNameW(NULL, log_path, MAX_PATH);
    wcscpy(wcsrchr(log_path, L'\\') + 1, L"swofty\\net-trace.log");
    char mode[16] = "";
    GetEnvironmentVariableA("SWOFTY_NET_TRACE", mode, sizeof mode);
    int on = mode[0] && strcmp(mode, "0");

    DWORD dummy;
    expect(GetFileVersionInfoSizeW(L"C:\\windows\\system32\\kernel32.dll", &dummy) != 0, "version.dll forwarding works");
    HMODULE ws = GetModuleHandleW(L"ws2_32.dll");
    scan(ws);
    WNDCLASSEXW wc = {.cbSize = sizeof wc, .lpfnWndProc = DefWindowProcW, .hInstance = GetModuleHandleW(NULL),
                      .lpszClassName = L"EGO_WindowClass"};
    extern void *__imp_RegisterClassExW;
    ATOM(WINAPI * rc)(const WNDCLASSEXW *) = (void *)__imp_RegisterClassExW;
    rc(&wc);
    expect(GetModuleHandleW(L"swofty_core.dll") != NULL, "the window trigger loaded the core");

    int ready = 0;
    for (int i = 0; i < 150 && !ready; i++) {
        ready = read_log(log_path, text, sizeof text) > 0 && strstr(text, "# ready:");
        if (!ready)
            Sleep(on ? 100 : 20);
    }
    check_patches(ws, on ? mode : "off");
    if (!on) {
        expect(!ready && read_log(log_path, text, sizeof text) < 0, "without SWOFTY_NET_TRACE no trace log is written");
        printf("%s (%d failure(s))\n", fails ? "FAILED" : "ALL PASSED", fails);
        ExitProcess(fails != 0);
    }
    expect(ready, "the trace wrote its ready line");

    WSADATA wd;
    expect(WSAStartup(MAKEWORD(2, 2), &wd) == 0, "WSAStartup");

    SOCKET rs = WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, WSA_FLAG_OVERLAPPED);
    expect(rs != INVALID_SOCKET, "WSASocketW made the receive socket");
    int rcvbuf = 1 << 16;
    expect(setsockopt(rs, SOL_SOCKET, SO_RCVBUF, (const char *)&rcvbuf, sizeof rcvbuf) == 0, "setsockopt SO_RCVBUF");
    u_long nb = 1;
    expect(ioctlsocket(rs, FIONBIO, &nb) == 0, "ioctlsocket FIONBIO");
    struct sockaddr_in any = loop_addr(0);
    expect(bind(rs, (struct sockaddr *)&any, sizeof any) == 0, "bind the receive socket to 127.0.0.1:0");
    unsigned short rport = port_of(rs);
    expect(rport != 0, "getsockname gives the bound port");

    SOCKET ss = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    expect(ss != INVALID_SOCKET, "socket made the send socket");
    expect(bind(ss, (struct sockaddr *)&any, sizeof any) == 0, "bind the send socket");
    unsigned short sport = port_of(ss);

    struct sockaddr_in taken = loop_addr(rport);
    SOCKET dup = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    int br = bind(dup, (struct sockaddr *)&taken, sizeof taken);
    int be = WSAGetLastError();
    expect(br == SOCKET_ERROR && be == WSAEADDRINUSE, "a bind on a taken port fails with WSAEADDRINUSE after the hook");

    WSAEVENT ev = WSACreateEvent();
    expect(ev != WSA_INVALID_EVENT, "WSACreateEvent");
    WSAOVERLAPPED ov;
    memset(&ov, 0, sizeof ov);
    ov.hEvent = ev;
    char buf[0x4f0];
    WSABUF wb = {sizeof buf, buf};
    DWORD flags = 0;
    struct sockaddr_in from;
    memset(&from, 0, sizeof from);
    int fromlen = sizeof from;
    int pr = WSARecvFrom(rs, &wb, 1, NULL, &flags, (struct sockaddr *)&from, &fromlen, &ov, NULL);
    int pe = WSAGetLastError();
    expect(pr == SOCKET_ERROR && pe == WSA_IO_PENDING, "the overlapped WSARecvFrom is pending with WSA_IO_PENDING after the hook");

    static const char msg[] = "swofty-net-trace";
    struct sockaddr_in to = loop_addr(rport);
    expect(sendto(ss, msg, 16, 0, (struct sockaddr *)&to, sizeof to) == 16, "sendto the receive socket");

    DWORD w = WSAWaitForMultipleEvents(1, &ev, FALSE, 5000, FALSE);
    expect(w == WSA_WAIT_EVENT_0, "WSAWaitForMultipleEvents saw the completion");
    WSAResetEvent(ev);
    DWORD got = 0, gflags = 0;
    BOOL done = WSAGetOverlappedResult(rs, &ov, &got, FALSE, &gflags);
    expect(done && got == 16 && !memcmp(buf, msg, 16), "WSAGetOverlappedResult completed with the 16 bytes");
    expect(from.sin_family == AF_INET && ntohs(from.sin_port) == sport && fromlen == sizeof from,
           "the from address is the sender's 127.0.0.1 port");

    typedef int(WSAAPI * SendToFn)(SOCKET, LPWSABUF, DWORD, LPDWORD, DWORD, const struct sockaddr *, int,
                                   LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);
    typedef int(WSAAPI * SelectFn)(SOCKET, WSAEVENT, long);
    SendToFn wsasendto = (SendToFn)(void *)GetProcAddress(ws, "WSASendTo");
    SelectFn evsel = (SelectFn)(void *)GetProcAddress(ws, "WSAEventSelect");
    static const char msg2[] = "via-WSASendTo!!!";
    WSABUF sb = {16, (char *)msg2};
    DWORD sent = 0;
    expect(wsasendto(ss, &sb, 1, &sent, 0, (struct sockaddr *)&to, sizeof to, NULL, NULL) == 0 && sent == 16,
           "WSASendTo through GetProcAddress");
    char rb[64];
    struct sockaddr_in rf;
    int rfl = sizeof rf;
    int rn = -1;
    for (int i = 0; i < 100 && rn < 0; i++) {
        rn = recvfrom(rs, rb, sizeof rb, 0, (struct sockaddr *)&rf, &rfl);
        if (rn < 0)
            Sleep(10);
    }
    expect(rn == 16 && !memcmp(rb, msg2, 16) && ntohs(rf.sin_port) == sport, "recvfrom got the WSASendTo datagram");

    expect(connect(ss, (struct sockaddr *)&to, sizeof to) == 0, "connect the send socket");
    expect(send(ss, "connected-send!!", 16, 0) == 16, "send on the connected socket");
    rn = -1;
    for (int i = 0; i < 100 && rn < 0; i++) {
        rn = recv(rs, rb, sizeof rb, 0);
        if (rn < 0)
            Sleep(10);
    }
    expect(rn == 16 && !memcmp(rb, "connected-send!!", 16), "recv got the connected datagram");

    SOCKET t2s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    expect(t2_bind(t2s, (struct sockaddr *)&any, sizeof any) == 0, "bind through the ordinal import table");
    expect(t2_sendto(t2s, "table-two-sendto", 16, 0, (struct sockaddr *)&to, sizeof to) == 16,
           "sendto through the ordinal import table");
    rn = -1;
    for (int i = 0; i < 100 && rn < 0; i++) {
        rn = recvfrom(rs, rb, sizeof rb, 0, (struct sockaddr *)&rf, &rfl);
        if (rn < 0)
            Sleep(10);
    }
    expect(rn == 16 && !memcmp(rb, "table-two-sendto", 16), "recvfrom got the ordinal-table datagram");
    expect(t2_closesocket(t2s) == 0, "closesocket through the ordinal import table");

    SOCKET es = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    WSAEVENT ev2 = WSACreateEvent();
    expect(evsel(es, ev2, FD_READ) == 0, "WSAEventSelect through GetProcAddress");

    SOCKET flood = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in sink = loop_addr(sport);
    int okn = 0;
    for (int i = 0; i < 450; i++)
        okn += sendto(flood, "rate", 4, 0, (struct sockaddr *)&sink, sizeof sink) == 4;
    expect(okn == 450, "450 sendto calls on one socket");

    SOCKET unbound = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    SetLastError(0x1234);
    int ur = closesocket(unbound);
    DWORD ue = GetLastError();
    expect(ur == 0 && ue == 0x1234, "closing an unbound socket leaves the caller's last error as it was");
    expect(closesocket(es) == 0 && closesocket(flood) == 0 && closesocket(dup) == 0, "closesocket");
    expect(closesocket(ss) == 0 && closesocket(rs) == 0, "closesocket the pump sockets");
    int cr = closesocket(rs);
    int ce = WSAGetLastError();
    expect(cr == SOCKET_ERROR && ce == WSAENOTSOCK, "a second closesocket fails with WSAENOTSOCK after the hook");
    WSACloseEvent(ev);
    WSACloseEvent(ev2);

    char ports[64];
    snprintf(ports, sizeof ports, "%u %u", rport, sport);
    HANDLE pf = CreateFileW(L"ports.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD wn;
    WriteFile(pf, ports, (DWORD)strlen(ports), &wn, NULL);
    CloseHandle(pf);
    printf("%s (%d failure(s))\n", fails ? "FAILED" : "ALL PASSED", fails);
    ExitProcess(fails != 0);
}
