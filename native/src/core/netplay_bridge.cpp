#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include "imports.hpp"
#include "netplay_session.hpp"
#include "runtime.hpp"

#include <swofty/mixin.hpp>

#include <atomic>
#include <cstdio>
#include <initializer_list>
#include <string>

// In-game half of the netplay bridge. The launcher starts the game with SWOFTY_NETPLAY set, which
// installs these hooks; they pass everything through until a room is configured at runtime
// (netplay_session.hpp, set by the netplay command channel), and read the room on every call.
// SWOFTY_NETPLAY=self=<N>;base=<BASE> also configures that room at startup. In a room, member M is
// 10.77.0.M:21414 to the game and 127.0.0.1:BASE+M on this machine, where the launcher relays it. Only
// sockaddrs change, never payload bytes:
//   sendto (every ws2_32 import slot): 10.77.0.M:21414, M != self, goes to 127.0.0.1:BASE+M.
//   recvfrom (every ws2_32 import slot): a datagram from 127.0.0.1:BASE+M, M != self, reads as from
//     10.77.0.M:21414. The LAN socket's receive thread (0x144048650) calls it blocking through table 1.
//   GetAdaptersAddresses (iphlpapi): for AF_INET, the adapter the game takes as its own (the first one
//     Up, not loopback, with a unicast address; Ne_Get_LocalIPv4_Thunk 0x143986830) reports
//     10.77.0.<self> as that first unicast, so the host's ConnectionInfo is the address joiners dial.
//   bind: a successful AF_INET bind to port 21414 is the game socket; from then on swofty\netplay.json
//     carries the room (self and base 0 without one) and is rewritten when the room changes.
// The exe does not import WSASendTo and the LAN socket never calls it, so it is not hooked. Hooks go
// on top of whatever the slot holds (the socket trace's hooks when SWOFTY_NET_TRACE is on, so the trace
// logs loopback addresses). Each hook returns the original's result and leaves its last error.

namespace {

constexpr uint16_t GAME_PORT = 21414;
constexpr uint32_t VNET = 0x0a4d0000;
constexpr uint32_t LOOPBACK = 0x7f000001;

enum Fn { F_SENDTO, F_RECVFROM, F_BIND, F_GAA, F_COUNT };

struct Hook {
    const char *name;
    void *detour, *target, *orig;
    int slots;
    std::string where;
};

Hook hooks[F_COUNT];
std::atomic<bool> armed;
std::atomic<int> game_port;
wchar_t status_path[MAX_PATH * 2];
SRWLOCK status_lock = SRWLOCK_INIT;
std::atomic<bool> status_written;
std::atomic<uint64_t> seen[2][4];
std::atomic<int> own_logs;

enum Dir { D_OUT, D_IN };

using SendToFn = int(WSAAPI *)(SOCKET, const char *, int, int, const sockaddr *, int);
using RecvFromFn = int(WSAAPI *)(SOCKET, char *, int, int, sockaddr *, int *);
using BindFn = int(WSAAPI *)(SOCKET, const sockaddr *, int);
using GaaFn = ULONG(WINAPI *)(ULONG, ULONG, PVOID, PIP_ADAPTER_ADDRESSES, PULONG);

struct Room {
    int self, base;
};

bool room(Room &r)
{
    return runtime::netplay_session::snapshot(r.self, r.base);
}

uint16_t be16(uint16_t v)
{
    return __builtin_bswap16(v);
}

uint32_t be32(uint32_t v)
{
    return __builtin_bswap32(v);
}

bool number(const char *&p, long &out)
{
    if (*p < '0' || *p > '9')
        return false;
    long v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p++ - '0');
        if (v > 65535)
            return false;
    }
    out = v;
    return true;
}

bool parse(const char *v, int &self, int &base)
{
    long s = -1, b = -1;
    for (const char *p = v;;) {
        long *dst = !strncmp(p, "self=", 5) ? &s : !strncmp(p, "base=", 5) ? &b : nullptr;
        if (!dst || *dst >= 0)
            return false;
        p += 5;
        if (!number(p, *dst))
            return false;
        if (!*p)
            break;
        if (*p++ != ';')
            return false;
    }
    if (s < 1 || s > 254 || b < 1 || b + 254 > 65535 || (b < GAME_PORT && b + 254 >= GAME_PORT))
        return false;
    self = int(s);
    base = int(b);
    return true;
}

void first(Dir d, int m, int base)
{
    uint64_t bit = 1ull << (m & 63);
    auto &w = seen[d][m >> 6];
    if ((w.load(std::memory_order_relaxed) & bit) || (w.fetch_or(bit) & bit))
        return;
    DWORD err = GetLastError();
    if (d == D_OUT)
        swofty::log("[netplay] first datagram to member %d: 10.77.0.%d:%u sent to 127.0.0.1:%d", m, m, GAME_PORT,
                    base + m);
    else
        swofty::log("[netplay] first datagram from member %d: 127.0.0.1:%d received as 10.77.0.%d:%u", m,
                    base + m, m, GAME_PORT);
    SetLastError(err);
}

int virtual_member(const sockaddr *a, int len, int self)
{
    if (!a || len < int(sizeof(sockaddr_in)) || len > int(sizeof(sockaddr_storage)) || a->sa_family != AF_INET)
        return 0;
    auto *in = reinterpret_cast<const sockaddr_in *>(a);
    uint32_t ip = be32(in->sin_addr.s_addr);
    if ((ip & 0xffffff00) != VNET || in->sin_port != be16(GAME_PORT))
        return 0;
    int m = int(ip & 0xff);
    return m >= 1 && m <= 254 && m != self ? m : 0;
}

int WSAAPI h_sendto(SOCKET s, const char *buf, int len, int flags, const sockaddr *to, int tolen)
{
    auto real = reinterpret_cast<SendToFn>(hooks[F_SENDTO].orig);
    Room room_now;
    int m = room(room_now) ? virtual_member(to, tolen, room_now.self) : 0;
    if (!m)
        return real(s, buf, len, flags, to, tolen);
    sockaddr_storage copy;
    memcpy(&copy, to, size_t(tolen));
    auto *in = reinterpret_cast<sockaddr_in *>(&copy);
    in->sin_addr.s_addr = be32(LOOPBACK);
    in->sin_port = be16(uint16_t(room_now.base + m));
    int r = real(s, buf, len, flags, reinterpret_cast<const sockaddr *>(&copy), tolen);
    first(D_OUT, m, room_now.base);
    return r;
}

int WSAAPI h_recvfrom(SOCKET s, char *buf, int len, int flags, sockaddr *from, int *fromlen)
{
    int r = reinterpret_cast<RecvFromFn>(hooks[F_RECVFROM].orig)(s, buf, len, flags, from, fromlen);
    Room room_now;
    if (r < 0 || !from || !fromlen || *fromlen < int(sizeof(sockaddr_in)) || from->sa_family != AF_INET ||
        !room(room_now))
        return r;
    auto *in = reinterpret_cast<sockaddr_in *>(from);
    int m = int(be16(in->sin_port)) - room_now.base;
    if (in->sin_addr.s_addr != be32(LOOPBACK) || m < 1 || m > 254 || m == room_now.self)
        return r;
    in->sin_addr.s_addr = be32(VNET | uint32_t(m));
    in->sin_port = be16(GAME_PORT);
    first(D_IN, m, room_now.base);
    return r;
}

void write_status()
{
    AcquireSRWLockExclusive(&status_lock);
    Room r;
    if (!room(r))
        r = {0, 0};
    int port = game_port.load();
    char body[128];
    snprintf(body, sizeof body, "{\"pid\":%lu,\"self\":%d,\"base\":%d,\"game_port\":%d}\n", GetCurrentProcessId(),
             r.self, r.base, port);
    bool ok = runtime::write_file(status_path, body);
    if (ok)
        status_written = true;
    DWORD err = ok ? 0 : GetLastError();
    ReleaseSRWLockExclusive(&status_lock);
    if (ok)
        swofty::log("[netplay] bridge on, self=%d base=%d game_port=%d", r.self, r.base, port);
    else
        swofty::log("[netplay] bridge on, self=%d base=%d game_port=%d; swofty\\netplay.json not written (%lu)", r.self,
                    r.base, port, err);
}

int WSAAPI h_bind(SOCKET s, const sockaddr *a, int len)
{
    int r = reinterpret_cast<BindFn>(hooks[F_BIND].orig)(s, a, len);
    if (r || !a || len < int(sizeof(sockaddr_in)) || a->sa_family != AF_INET ||
        reinterpret_cast<const sockaddr_in *>(a)->sin_port != be16(GAME_PORT))
        return r;
    DWORD err = GetLastError();
    game_port = GAME_PORT;
    write_status();
    SetLastError(err);
    return r;
}

void own_address(IP_ADAPTER_ADDRESSES *a, int self)
{
    while (a && !(a->OperStatus == IfOperStatusUp && a->IfType != IF_TYPE_SOFTWARE_LOOPBACK && a->FirstUnicastAddress))
        a = a->Next;
    bool say = own_logs.fetch_add(1, std::memory_order_relaxed) < 8;
    if (!a) {
        if (say)
            swofty::log("[netplay] no adapter is up with an IPv4 address; the game finds no own address to report");
        return;
    }
    sockaddr *sa = a->FirstUnicastAddress->Address.lpSockaddr;
    if (!sa || sa->sa_family != AF_INET)
        return;
    auto *in = reinterpret_cast<sockaddr_in *>(sa);
    auto *was = reinterpret_cast<const uint8_t *>(&in->sin_addr);
    if (say)
        swofty::log("[netplay] own address: adapter %lu %u.%u.%u.%u reported as 10.77.0.%d", a->IfIndex, was[0], was[1],
                    was[2], was[3], self);
    in->sin_addr.s_addr = be32(VNET | uint32_t(self));
}

ULONG WINAPI h_GetAdaptersAddresses(ULONG family, ULONG flags, PVOID reserved, PIP_ADAPTER_ADDRESSES list, PULONG size)
{
    ULONG r = reinterpret_cast<GaaFn>(hooks[F_GAA].orig)(family, flags, reserved, list, size);
    Room room_now;
    if (r != ERROR_SUCCESS || family != AF_INET || !list || !room(room_now))
        return r;
    DWORD err = GetLastError();
    own_address(list, room_now.self);
    SetLastError(err);
    return r;
}

void install(HMODULE lib, const char *dll, std::initializer_list<Fn> fns)
{
    runtime::each_import(lib, dll, [&](const runtime::ImportSlot &s) {
        for (Fn f : fns) {
            Hook &h = hooks[f];
            if (!s.fn || s.fn != h.target)
                continue;
            void *cur = reinterpret_cast<void *>(*s.slot);
            if (h.orig && h.orig != cur) {
                swofty::log("[netplay] %s table %d slot exe+0x%llx holds 0x%llx, not 0x%llx; left alone", h.name, s.table,
                            (unsigned long long)s.rva, (unsigned long long)uintptr_t(cur),
                            (unsigned long long)uintptr_t(h.orig));
                break;
            }
            void *was = h.orig;
            h.orig = cur;
            if (!runtime::write_slot(s.slot, h.detour)) {
                h.orig = was;
                break;
            }
            char at[64];
            snprintf(at, sizeof at, "%sexe+0x%llx", h.slots ? "," : "", (unsigned long long)s.rva);
            h.where += at;
            h.slots++;
            break;
        }
    });
}

void setup()
{
    char v[96] = {};
    DWORD n = GetEnvironmentVariableA("SWOFTY_NETPLAY", v, sizeof v);
    if (!n)
        return;
    int self = 0, base = 0;
    bool too_long = n >= sizeof v, given = !too_long && parse(v, self, base);
    if (!given && (too_long || strchr(v, '=')))
        swofty::log("[netplay] SWOFTY_NETPLAY=\"%s\" is not self=<1-254>;base=<port>; armed without a room",
                    too_long ? "(too long)" : v);
    if (given)
        runtime::netplay_session::configure(self, base);
    swprintf(status_path, sizeof status_path / sizeof *status_path, L"%lsnetplay.json", runtime::swofty_root().c_str());
    DeleteFileW(status_path);
    HMODULE ws = LoadLibraryW(L"ws2_32.dll"), ip = LoadLibraryW(L"iphlpapi.dll");
    hooks[F_SENDTO] = {"sendto", reinterpret_cast<void *>(&h_sendto), nullptr, nullptr, 0, {}};
    hooks[F_RECVFROM] = {"recvfrom", reinterpret_cast<void *>(&h_recvfrom), nullptr, nullptr, 0, {}};
    hooks[F_BIND] = {"bind", reinterpret_cast<void *>(&h_bind), nullptr, nullptr, 0, {}};
    hooks[F_GAA] = {"GetAdaptersAddresses", reinterpret_cast<void *>(&h_GetAdaptersAddresses), nullptr, nullptr, 0, {}};
    for (Fn f : {F_SENDTO, F_RECVFROM, F_BIND})
        hooks[f].target = ws ? reinterpret_cast<void *>(GetProcAddress(ws, hooks[f].name)) : nullptr;
    hooks[F_GAA].target = ip ? reinterpret_cast<void *>(GetProcAddress(ip, hooks[F_GAA].name)) : nullptr;
    if (ws)
        install(ws, "ws2_32.dll", {F_SENDTO, F_RECVFROM, F_BIND});
    if (ip)
        install(ip, "iphlpapi.dll", {F_GAA});
    armed = true;
    std::string slots, missing;
    for (auto &h : hooks) {
        slots += std::string(slots.empty() ? "" : "; ") + h.name + " " + (h.slots ? h.where : "-");
        if (!h.slots)
            missing += std::string(missing.empty() ? "" : ", ") + h.name;
    }
    Room r;
    char now[96];
    if (room(r))
        snprintf(now, sizeof now, "room self=%d base=%d%s", r.self, r.base, given ? " from SWOFTY_NETPLAY" : "");
    else
        snprintf(now, sizeof now, "no room yet");
    swofty::log("[netplay] bridge armed: %s; %s", slots.c_str(), now);
    if (!missing.empty())
        swofty::log("[netplay] bridge incomplete: no game import slot for %s", missing.c_str());
}

}

namespace runtime {

void netplay_bridge_setup()
{
    setup();
}

void netplay_bridge_status_changed()
{
    if (!armed)
        return;
    for (auto &d : seen)
        for (auto &w : d)
            w = 0;
    own_logs = 0;
    Room r;
    if (room(r))
        swofty::log("[netplay] room configured, self=%d base=%d (members at 127.0.0.1:%d-%d)", r.self, r.base, r.base + 1,
                    r.base + 254);
    else
        swofty::log("[netplay] room cleared; the bridge passes everything through");
    if (game_port)
        write_status();
}

void netplay_exit()
{
    if (status_written.exchange(false))
        DeleteFileW(status_path);
}

}
