#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include "imports.hpp"
#include "runtime.hpp"

#include <tlhelp32.h>

#include <safetyhook.hpp>
#include <swofty/mixin.hpp>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <vector>

// Log-only socket trace for the LAN netplay work, on with SWOFTY_NET_TRACE=1. The exe imports ws2_32
// through two import descriptors: the clear code's table, and a second one that the .data1 bodies
// (DirtySock bind/sendto/connect among them) call through. Both are hooked in the IAT. WSASendTo and
// WSAEventSelect are not imported, so they are hooked inline on the ws2_32 exports.
// SWOFTY_NET_TRACE=inline hooks every export inline instead, and SWOFTY_NET_TRACE_BYTES=<n> logs up to
// n payload bytes instead of 16. Every hook calls the original with the caller's arguments, returns its
// result and restores the thread's last error before returning.

namespace {

enum Call {
    C_SOCKET,
    C_WSASOCKETW,
    C_BIND,
    C_CONNECT,
    C_SETSOCKOPT,
    C_IOCTLSOCKET,
    C_GETSOCKNAME,
    C_SENDTO,
    C_SEND,
    C_WSASENDTO,
    C_RECVFROM,
    C_RECV,
    C_WSARECVFROM,
    C_WSARECV,
    C_WSAGETOVERLAPPEDRESULT,
    C_WSAWAITFORMULTIPLEEVENTS,
    C_WSAEVENTSELECT,
    C_WSACREATEEVENT,
    C_WSAIOCTL,
    C_CLOSESOCKET,
    C_COUNT
};

struct Hook {
    const char *name;
    void *detour;
    void *orig;
    void *target;
    int slots;
    safetyhook::InlineHook inl;
};

Hook hooks[C_COUNT];

template <class F> F orig(Call c)
{
    return reinterpret_cast<F>(hooks[c].orig);
}

struct SockInfo {
    int family = 0, type = 0, proto = 0;
    sockaddr_storage local{}, peer{};
    bool probed = false;
};

struct Post {
    SOCKET s;
    char *buf;
    ULONG len;
    sockaddr *from;
    int *fromlen;
    bool recvfrom;
};

struct Mod {
    uintptr_t lo, hi;
    std::string name;
};

using NameFn = int(WSAAPI *)(SOCKET, sockaddr *, int *);
using OptFn = int(WSAAPI *)(SOCKET, int, int, char *, int *);

SRWLOCK lock = SRWLOCK_INIT;
HANDLE file = INVALID_HANDLE_VALUE;
std::unordered_map<uint64_t, uint32_t> counts;
std::unordered_map<SOCKET, SockInfo> socks;
std::unordered_map<void *, Post> posts;
std::vector<Mod> mods;
uintptr_t exe_lo, exe_hi, ws_lo, ws_hi;
NameFn p_getsockname, p_getpeername;
OptFn p_getsockopt;
LARGE_INTEGER qpf, qpc0;
thread_local int busy;
long long hex_max = 16;
uint64_t written, cap = 64ull << 20;
std::atomic<bool> capped;

bool off()
{
    return busy || capped.load(std::memory_order_relaxed);
}

struct Guard {
    DWORD in = GetLastError(), err = 0;
    Guard() { busy++; }
    void pre() const { SetLastError(in); }
    void done() { err = GetLastError(); }
    ~Guard()
    {
        busy--;
        SetLastError(err);
    }
};

void write_line(const char *s, size_t n)
{
    DWORD w;
    if (file == INVALID_HANDLE_VALUE || capped.load(std::memory_order_relaxed))
        return;
    if (written + n > cap) {
        char last[96];
        int k = snprintf(last, sizeof last, "# log cap of %llu bytes reached; trace stopped\n", (unsigned long long)cap);
        WriteFile(file, last, DWORD(k), &w, nullptr);
        capped.store(true, std::memory_order_relaxed);
        return;
    }
    WriteFile(file, s, DWORD(n), &w, nullptr);
    written += n;
}

void raw(const char *fmt, ...)
{
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line - 1, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    size_t len = std::min<size_t>(size_t(n), sizeof line - 2);
    line[len++] = '\n';
    AcquireSRWLockExclusive(&lock);
    write_line(line, len);
    ReleaseSRWLockExclusive(&lock);
}

uint16_t be16(uint16_t v)
{
    return uint16_t((v >> 8) | (v << 8));
}

bool readable(int r, DWORD err)
{
    return r != SOCKET_ERROR || err != WSAEFAULT;
}

bool pending(int r, DWORD err)
{
    return r == 0 || err == WSA_IO_PENDING;
}

bool can_read(const void *p, size_t n)
{
    return p && n && !IsBadReadPtr(p, n);
}

std::string addr(const sockaddr *a, int len = -1)
{
    char b[96];
    if (!a)
        return "-";
    if (a->sa_family == AF_INET && (len < 0 || len >= int(sizeof(sockaddr_in)))) {
        auto *in = reinterpret_cast<const sockaddr_in *>(a);
        auto *ip = reinterpret_cast<const uint8_t *>(&in->sin_addr);
        snprintf(b, sizeof b, "%u.%u.%u.%u:%u", ip[0], ip[1], ip[2], ip[3], be16(in->sin_port));
        return b;
    }
    if (a->sa_family == AF_INET6 && (len < 0 || len >= int(sizeof(sockaddr_in6)))) {
        auto *in = reinterpret_cast<const sockaddr_in6 *>(a);
        auto *w = reinterpret_cast<const uint8_t *>(&in->sin6_addr);
        int k = snprintf(b, sizeof b, "[");
        for (int i = 0; i < 16; i += 2)
            k += snprintf(b + k, sizeof b - k, "%s%x", i ? ":" : "", (w[i] << 8) | w[i + 1]);
        snprintf(b + k, sizeof b - k, "]:%u", be16(in->sin6_port));
        return b;
    }
    if (a->sa_family == 0)
        return "unset";
    snprintf(b, sizeof b, "family%u", a->sa_family);
    return b;
}

std::string addr_safe(const sockaddr *a, int len)
{
    if (!a)
        return "-";
    return can_read(a, len > 0 ? size_t(len) : sizeof(sockaddr)) ? addr(a, len) : "?";
}

std::string hexdump(const void *p, long long n)
{
    if (!p || n <= 0)
        return "-";
    static const char d[] = "0123456789abcdef";
    std::string s;
    auto *b = static_cast<const uint8_t *>(p);
    for (long long i = 0; i < std::min(n, hex_max); i++) {
        s += d[b[i] >> 4];
        s += d[b[i] & 15];
    }
    return s;
}

std::string hexdump_safe(const void *p, long long n)
{
    if (!p || n <= 0)
        return "-";
    return can_read(p, size_t(std::min(n, hex_max))) ? hexdump(p, n) : "?";
}

std::string caller(void *ra)
{
    char b[160];
    auto a = reinterpret_cast<uintptr_t>(ra);
    if (a >= exe_lo && a < exe_hi) {
        snprintf(b, sizeof b, "exe+0x%llx", (unsigned long long)(a - exe_lo));
        return b;
    }
    for (auto &m : mods)
        if (a >= m.lo && a < m.hi) {
            snprintf(b, sizeof b, "%s+0x%llx", m.name.c_str(), (unsigned long long)(a - m.lo));
            return b;
        }
    snprintf(b, sizeof b, "0x%llx", (unsigned long long)a);
    return b;
}

void probe(SOCKET s, SockInfo &k)
{
    bool first = !k.probed;
    k.probed = true;
    int n = sizeof k.local;
    if (p_getsockname)
        p_getsockname(s, reinterpret_cast<sockaddr *>(&k.local), &n);
    n = sizeof k.peer;
    if (p_getpeername && first)
        p_getpeername(s, reinterpret_cast<sockaddr *>(&k.peer), &n);
    if (p_getsockopt && first && !k.type) {
        n = sizeof k.type;
        p_getsockopt(s, SOL_SOCKET, SO_TYPE, reinterpret_cast<char *>(&k.type), &n);
        k.family = k.local.ss_family;
    }
}

std::string sock_desc(SOCKET s)
{
    char b[256];
    if (s == INVALID_SOCKET)
        return "-";
    SockInfo k;
    AcquireSRWLockExclusive(&lock);
    auto it = socks.find(s);
    if (it != socks.end())
        k = it->second;
    ReleaseSRWLockExclusive(&lock);
    if (!k.probed || !k.local.ss_family) {
        probe(s, k);
        AcquireSRWLockExclusive(&lock);
        auto &e = socks[s];
        if (!e.probed || !e.local.ss_family)
            e = k;
        ReleaseSRWLockExclusive(&lock);
    }
    snprintf(b, sizeof b, "sock=0x%llx af=%d type=%d proto=%d local=%s peer=%s", (unsigned long long)s, k.family,
             k.type, k.proto, addr(reinterpret_cast<sockaddr *>(&k.local)).c_str(),
             addr(reinterpret_cast<sockaddr *>(&k.peer)).c_str());
    return b;
}

uint64_t key(Call c, SOCKET s)
{
    return (uint64_t(c) << 56) ^ uint64_t(s);
}

void trace_as(Call c, SOCKET s, const std::string *desc, void *ra, int ret, DWORD err, const char *fmt, va_list ap)
{
    AcquireSRWLockExclusive(&lock);
    uint32_t n = ++counts[key(c, s)];
    ReleaseSRWLockExclusive(&lock);
    if (n > 200 && n % 100)
        return;
    char body[1536];
    vsnprintf(body, sizeof body, fmt, ap);
    std::string sd = desc ? *desc : sock_desc(s), who = caller(ra);
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    double ms = double(q.QuadPart - qpc0.QuadPart) * 1000.0 / double(qpf.QuadPart);
    char line[2048];
    int len = snprintf(line, sizeof line, "%10.3f t%05lu %s #%u %s %s ret=%d err=%lu from=%s\n", ms, GetCurrentThreadId(),
                       hooks[c].name, n, sd.c_str(), body, ret, err, who.c_str());
    if (len <= 0)
        return;
    AcquireSRWLockExclusive(&lock);
    write_line(line, std::min<size_t>(size_t(len), sizeof line - 1));
    ReleaseSRWLockExclusive(&lock);
}

void trace(Call c, SOCKET s, void *ra, int ret, DWORD err, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    trace_as(c, s, nullptr, ra, ret, err, fmt, ap);
    va_end(ap);
}

void trace_desc(Call c, SOCKET s, const std::string &desc, void *ra, int ret, DWORD err, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    trace_as(c, s, &desc, ra, ret, err, fmt, ap);
    va_end(ap);
}

void note_socket(SOCKET s, int af, int type, int proto)
{
    if (s == INVALID_SOCKET)
        return;
    AcquireSRWLockExclusive(&lock);
    SockInfo k;
    k.family = af, k.type = type, k.proto = proto, k.probed = true;
    socks[s] = k;
    for (int c = 0; c < C_COUNT; c++)
        counts.erase(key(Call(c), s));
    ReleaseSRWLockExclusive(&lock);
}

void note_addr(SOCKET s, const sockaddr *a, int len, bool local)
{
    if (!a || len <= 0)
        return;
    AcquireSRWLockExclusive(&lock);
    auto &k = socks[s];
    auto &dst = local ? k.local : k.peer;
    memcpy(&dst, a, std::min<size_t>(size_t(len), sizeof dst));
    ReleaseSRWLockExclusive(&lock);
}

void note_post(void *ov, const Post &p)
{
    if (!ov)
        return;
    AcquireSRWLockExclusive(&lock);
    posts[ov] = p;
    ReleaseSRWLockExclusive(&lock);
}

bool take_post(void *ov, Post &p, bool erase)
{
    AcquireSRWLockExclusive(&lock);
    auto it = posts.find(ov);
    bool ok = it != posts.end();
    if (ok) {
        p = it->second;
        if (erase)
            posts.erase(it);
    }
    ReleaseSRWLockExclusive(&lock);
    return ok;
}

void drop_socket(SOCKET s)
{
    AcquireSRWLockExclusive(&lock);
    socks.erase(s);
    for (int c = 0; c < C_COUNT; c++)
        counts.erase(key(Call(c), s));
    for (auto it = posts.begin(); it != posts.end();)
        it = it->second.s == s ? posts.erase(it) : std::next(it);
    ReleaseSRWLockExclusive(&lock);
}

void *event_of(const WSAOVERLAPPED *ov)
{
    return can_read(ov, sizeof *ov) ? ov->hEvent : nullptr;
}

WSABUF first_buf(const WSABUF *bufs, DWORD n)
{
    return n && can_read(bufs, sizeof *bufs) ? bufs[0] : WSABUF{0, nullptr};
}

int read_int(const int *p)
{
    return can_read(p, sizeof *p) ? *p : -1;
}

#define RA __builtin_return_address(0)
#define PTR(x) static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(x))

SOCKET WSAAPI h_socket(int af, int type, int proto)
{
    using F = SOCKET(WSAAPI *)(int, int, int);
    if (off())
        return orig<F>(C_SOCKET)(af, type, proto);
    Guard g;
    g.pre();
    SOCKET r = orig<F>(C_SOCKET)(af, type, proto);
    g.done();
    note_socket(r, af, type, proto);
    trace(C_SOCKET, r, RA, int(r == INVALID_SOCKET ? -1 : 0), r == INVALID_SOCKET ? g.err : 0, "af=%d type=%d proto=%d",
          af, type, proto);
    return r;
}

SOCKET WSAAPI h_WSASocketW(int af, int type, int proto, LPWSAPROTOCOL_INFOW info, GROUP group, DWORD flags)
{
    using F = SOCKET(WSAAPI *)(int, int, int, LPWSAPROTOCOL_INFOW, GROUP, DWORD);
    if (off())
        return orig<F>(C_WSASOCKETW)(af, type, proto, info, group, flags);
    Guard g;
    g.pre();
    SOCKET r = orig<F>(C_WSASOCKETW)(af, type, proto, info, group, flags);
    g.done();
    note_socket(r, af, type, proto);
    trace(C_WSASOCKETW, r, RA, int(r == INVALID_SOCKET ? -1 : 0), r == INVALID_SOCKET ? g.err : 0,
          "af=%d type=%d proto=%d flags=0x%lx", af, type, proto, flags);
    return r;
}

int WSAAPI h_bind(SOCKET s, const sockaddr *a, int len)
{
    using F = int(WSAAPI *)(SOCKET, const sockaddr *, int);
    if (off())
        return orig<F>(C_BIND)(s, a, len);
    Guard g;
    g.pre();
    int r = orig<F>(C_BIND)(s, a, len);
    g.done();
    if (!r)
        note_addr(s, a, len, true);
    trace(C_BIND, s, RA, r, r ? g.err : 0, "addr=%s len=%d", readable(r, g.err) ? addr(a, len).c_str() : "?", len);
    return r;
}

int WSAAPI h_connect(SOCKET s, const sockaddr *a, int len)
{
    using F = int(WSAAPI *)(SOCKET, const sockaddr *, int);
    if (off())
        return orig<F>(C_CONNECT)(s, a, len);
    Guard g;
    g.pre();
    int r = orig<F>(C_CONNECT)(s, a, len);
    g.done();
    if (readable(r, g.err))
        note_addr(s, a, len, false);
    trace(C_CONNECT, s, RA, r, r ? g.err : 0, "addr=%s len=%d", readable(r, g.err) ? addr(a, len).c_str() : "?", len);
    return r;
}

int WSAAPI h_setsockopt(SOCKET s, int level, int name, const char *val, int len)
{
    using F = int(WSAAPI *)(SOCKET, int, int, const char *, int);
    if (off())
        return orig<F>(C_SETSOCKOPT)(s, level, name, val, len);
    Guard g;
    g.pre();
    int r = orig<F>(C_SETSOCKOPT)(s, level, name, val, len);
    g.done();
    trace(C_SETSOCKOPT, s, RA, r, r ? g.err : 0, "level=0x%x name=0x%x len=%d val=%s", level, name, len,
          readable(r, g.err) ? hexdump(val, len).c_str() : "?");
    return r;
}

int WSAAPI h_ioctlsocket(SOCKET s, long cmd, u_long *arg)
{
    using F = int(WSAAPI *)(SOCKET, long, u_long *);
    if (off())
        return orig<F>(C_IOCTLSOCKET)(s, cmd, arg);
    Guard g;
    g.pre();
    int r = orig<F>(C_IOCTLSOCKET)(s, cmd, arg);
    g.done();
    trace(C_IOCTLSOCKET, s, RA, r, r ? g.err : 0, "cmd=0x%lx arg=%lu", (unsigned long)cmd,
          arg && readable(r, g.err) ? *arg : 0);
    return r;
}

int WSAAPI h_getsockname(SOCKET s, sockaddr *a, int *len)
{
    using F = int(WSAAPI *)(SOCKET, sockaddr *, int *);
    if (off())
        return orig<F>(C_GETSOCKNAME)(s, a, len);
    Guard g;
    g.pre();
    int r = orig<F>(C_GETSOCKNAME)(s, a, len);
    g.done();
    if (!r && len)
        note_addr(s, a, *len, true);
    trace(C_GETSOCKNAME, s, RA, r, r ? g.err : 0, "addr=%s", r ? "-" : addr(a, len ? *len : -1).c_str());
    return r;
}

int WSAAPI h_sendto(SOCKET s, const char *buf, int len, int flags, const sockaddr *to, int tolen)
{
    using F = int(WSAAPI *)(SOCKET, const char *, int, int, const sockaddr *, int);
    if (off())
        return orig<F>(C_SENDTO)(s, buf, len, flags, to, tolen);
    Guard g;
    g.pre();
    int r = orig<F>(C_SENDTO)(s, buf, len, flags, to, tolen);
    g.done();
    bool ok = readable(r, g.err);
    trace(C_SENDTO, s, RA, r, r < 0 ? g.err : 0, "to=%s len=%d flags=0x%x data=%s", ok ? addr(to, tolen).c_str() : "?",
          len, flags, ok ? hexdump(buf, len).c_str() : "?");
    return r;
}

int WSAAPI h_send(SOCKET s, const char *buf, int len, int flags)
{
    using F = int(WSAAPI *)(SOCKET, const char *, int, int);
    if (off())
        return orig<F>(C_SEND)(s, buf, len, flags);
    Guard g;
    g.pre();
    int r = orig<F>(C_SEND)(s, buf, len, flags);
    g.done();
    trace(C_SEND, s, RA, r, r < 0 ? g.err : 0, "len=%d flags=0x%x data=%s", len, flags,
          readable(r, g.err) ? hexdump(buf, len).c_str() : "?");
    return r;
}

int WSAAPI h_WSASendTo(SOCKET s, LPWSABUF bufs, DWORD n, LPDWORD sent, DWORD flags, const sockaddr *to, int tolen,
                       LPWSAOVERLAPPED ov, LPWSAOVERLAPPED_COMPLETION_ROUTINE cr)
{
    using F = int(WSAAPI *)(SOCKET, LPWSABUF, DWORD, LPDWORD, DWORD, const sockaddr *, int, LPWSAOVERLAPPED,
                            LPWSAOVERLAPPED_COMPLETION_ROUTINE);
    if (off())
        return orig<F>(C_WSASENDTO)(s, bufs, n, sent, flags, to, tolen, ov, cr);
    Guard g;
    WSABUF b0 = first_buf(bufs, n);
    std::string data = hexdump_safe(b0.buf, b0.len), dest = addr_safe(to, tolen);
    void *ev = event_of(ov);
    g.pre();
    int r = orig<F>(C_WSASENDTO)(s, bufs, n, sent, flags, to, tolen, ov, cr);
    g.done();
    trace(C_WSASENDTO, s, RA, r, r ? g.err : 0, "to=%s nbufs=%lu len0=%lu sent=%ld data=%s ov=0x%llx ev=0x%llx",
          dest.c_str(), n, b0.len, !r && sent ? long(*sent) : -1L, data.c_str(), PTR(ov), PTR(ev));
    return r;
}

int WSAAPI h_recvfrom(SOCKET s, char *buf, int len, int flags, sockaddr *from, int *fromlen)
{
    using F = int(WSAAPI *)(SOCKET, char *, int, int, sockaddr *, int *);
    if (off())
        return orig<F>(C_RECVFROM)(s, buf, len, flags, from, fromlen);
    Guard g;
    g.pre();
    int r = orig<F>(C_RECVFROM)(s, buf, len, flags, from, fromlen);
    g.done();
    if (r >= 0)
        trace(C_RECVFROM, s, RA, r, 0, "cap=%d flags=0x%x from=%s data=%s", len, flags,
              addr(from, fromlen ? *fromlen : -1).c_str(), hexdump(buf, r).c_str());
    else
        trace(C_RECVFROM, s, RA, r, g.err, "cap=%d flags=0x%x", len, flags);
    return r;
}

int WSAAPI h_recv(SOCKET s, char *buf, int len, int flags)
{
    using F = int(WSAAPI *)(SOCKET, char *, int, int);
    if (off())
        return orig<F>(C_RECV)(s, buf, len, flags);
    Guard g;
    g.pre();
    int r = orig<F>(C_RECV)(s, buf, len, flags);
    g.done();
    trace(C_RECV, s, RA, r, r < 0 ? g.err : 0, "cap=%d flags=0x%x data=%s", len, flags,
          r > 0 ? hexdump(buf, r).c_str() : "-");
    return r;
}

int WSAAPI h_WSARecvFrom(SOCKET s, LPWSABUF bufs, DWORD n, LPDWORD got, LPDWORD flags, sockaddr *from, LPINT fromlen,
                         LPWSAOVERLAPPED ov, LPWSAOVERLAPPED_COMPLETION_ROUTINE cr)
{
    using F = int(WSAAPI *)(SOCKET, LPWSABUF, DWORD, LPDWORD, LPDWORD, sockaddr *, LPINT, LPWSAOVERLAPPED,
                            LPWSAOVERLAPPED_COMPLETION_ROUTINE);
    if (off())
        return orig<F>(C_WSARECVFROM)(s, bufs, n, got, flags, from, fromlen, ov, cr);
    Guard g;
    WSABUF b0 = first_buf(bufs, n);
    int fl_in = fromlen ? read_int(fromlen) : -1;
    void *ev = event_of(ov);
    g.pre();
    int r = orig<F>(C_WSARECVFROM)(s, bufs, n, got, flags, from, fromlen, ov, cr);
    g.done();
    if (ov && pending(r, g.err))
        note_post(ov, Post{s, b0.buf, b0.len, from, fromlen, true});
    trace(C_WSARECVFROM, s, RA, r, r ? g.err : 0,
          "nbufs=%lu buf=0x%llx cap=%lu from_buf=0x%llx fromlen=%d flags_ptr=0x%llx ov=0x%llx ev=0x%llx cr=0x%llx got=%ld",
          n, PTR(b0.buf), b0.len, PTR(from), fl_in, PTR(flags), PTR(ov), PTR(ev), PTR(cr), !r && got ? long(*got) : -1L);
    return r;
}

int WSAAPI h_WSARecv(SOCKET s, LPWSABUF bufs, DWORD n, LPDWORD got, LPDWORD flags, LPWSAOVERLAPPED ov,
                     LPWSAOVERLAPPED_COMPLETION_ROUTINE cr)
{
    using F = int(WSAAPI *)(SOCKET, LPWSABUF, DWORD, LPDWORD, LPDWORD, LPWSAOVERLAPPED,
                            LPWSAOVERLAPPED_COMPLETION_ROUTINE);
    if (off())
        return orig<F>(C_WSARECV)(s, bufs, n, got, flags, ov, cr);
    Guard g;
    WSABUF b0 = first_buf(bufs, n);
    void *ev = event_of(ov);
    g.pre();
    int r = orig<F>(C_WSARECV)(s, bufs, n, got, flags, ov, cr);
    g.done();
    if (ov && pending(r, g.err))
        note_post(ov, Post{s, b0.buf, b0.len, nullptr, nullptr, false});
    trace(C_WSARECV, s, RA, r, r ? g.err : 0, "nbufs=%lu buf=0x%llx cap=%lu ov=0x%llx ev=0x%llx got=%ld", n,
          PTR(b0.buf), b0.len, PTR(ov), PTR(ev), !r && got ? long(*got) : -1L);
    return r;
}

BOOL WSAAPI h_WSAGetOverlappedResult(SOCKET s, LPWSAOVERLAPPED ov, LPDWORD bytes, BOOL wait, LPDWORD flags)
{
    using F = BOOL(WSAAPI *)(SOCKET, LPWSAOVERLAPPED, LPDWORD, BOOL, LPDWORD);
    if (off())
        return orig<F>(C_WSAGETOVERLAPPEDRESULT)(s, ov, bytes, wait, flags);
    Guard g;
    void *ev = event_of(ov);
    g.pre();
    BOOL r = orig<F>(C_WSAGETOVERLAPPEDRESULT)(s, ov, bytes, wait, flags);
    g.done();
    Post p{};
    bool known = take_post(ov, p, r || g.err != WSA_IO_INCOMPLETE);
    DWORD got = r && bytes ? *bytes : 0;
    long long shown = std::min<long long>(got, p.len);
    if (r && known && p.recvfrom)
        trace(C_WSAGETOVERLAPPEDRESULT, s, RA, r, 0,
              "ov=0x%llx ev=0x%llx bytes=%lu flags=0x%lx buf=0x%llx data=%s from_buf=0x%llx from=%s", PTR(ov), PTR(ev),
              got, flags ? *flags : 0, PTR(p.buf), hexdump(p.buf, shown).c_str(), PTR(p.from),
              addr(p.from, p.fromlen ? *p.fromlen : -1).c_str());
    else if (r && known)
        trace(C_WSAGETOVERLAPPEDRESULT, s, RA, r, 0, "ov=0x%llx ev=0x%llx bytes=%lu flags=0x%lx buf=0x%llx data=%s",
              PTR(ov), PTR(ev), got, flags ? *flags : 0, PTR(p.buf), hexdump(p.buf, shown).c_str());
    else
        trace(C_WSAGETOVERLAPPEDRESULT, s, RA, r, r ? 0 : g.err, "ov=0x%llx ev=0x%llx wait=%d bytes=%lu post=%s",
              PTR(ov), PTR(ev), wait, got, known ? "seen" : "unseen");
    return r;
}

DWORD WSAAPI h_WSAWaitForMultipleEvents(DWORD n, const WSAEVENT *ev, BOOL all, DWORD timeout, BOOL alertable)
{
    using F = DWORD(WSAAPI *)(DWORD, const WSAEVENT *, BOOL, DWORD, BOOL);
    if (off())
        return orig<F>(C_WSAWAITFORMULTIPLEEVENTS)(n, ev, all, timeout, alertable);
    Guard g;
    g.pre();
    DWORD r = orig<F>(C_WSAWAITFORMULTIPLEEVENTS)(n, ev, all, timeout, alertable);
    g.done();
    char list[160] = "";
    for (DWORD i = 0, k = 0; ev && i < n && i < 6; i++)
        k += snprintf(list + k, sizeof list - k, "%s0x%llx", i ? "," : "", PTR(ev[i]));
    trace(C_WSAWAITFORMULTIPLEEVENTS, INVALID_SOCKET, RA, int(r), r == WSA_WAIT_FAILED ? g.err : 0,
          "n=%lu events=%s%s all=%d timeout=%ld signalled=0x%llx", n, list, n > 6 ? ",..." : "", all, long(timeout),
          PTR(ev && r < n ? ev[r] : nullptr));
    return r;
}

int WSAAPI h_WSAEventSelect(SOCKET s, WSAEVENT e, long mask)
{
    using F = int(WSAAPI *)(SOCKET, WSAEVENT, long);
    if (off())
        return orig<F>(C_WSAEVENTSELECT)(s, e, mask);
    Guard g;
    g.pre();
    int r = orig<F>(C_WSAEVENTSELECT)(s, e, mask);
    g.done();
    trace(C_WSAEVENTSELECT, s, RA, r, r ? g.err : 0, "event=0x%llx mask=0x%lx", PTR(e), (unsigned long)mask);
    return r;
}

WSAEVENT WSAAPI h_WSACreateEvent()
{
    using F = WSAEVENT(WSAAPI *)();
    if (off())
        return orig<F>(C_WSACREATEEVENT)();
    Guard g;
    g.pre();
    WSAEVENT r = orig<F>(C_WSACREATEEVENT)();
    g.done();
    trace(C_WSACREATEEVENT, INVALID_SOCKET, RA, r ? 0 : -1, r ? 0 : g.err, "event=0x%llx", PTR(r));
    return r;
}

int WSAAPI h_WSAIoctl(SOCKET s, DWORD code, LPVOID in, DWORD inlen, LPVOID out, DWORD outlen, LPDWORD got,
                      LPWSAOVERLAPPED ov, LPWSAOVERLAPPED_COMPLETION_ROUTINE cr)
{
    using F = int(WSAAPI *)(SOCKET, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPWSAOVERLAPPED,
                            LPWSAOVERLAPPED_COMPLETION_ROUTINE);
    if (off())
        return orig<F>(C_WSAIOCTL)(s, code, in, inlen, out, outlen, got, ov, cr);
    Guard g;
    std::string data = hexdump_safe(in, inlen);
    g.pre();
    int r = orig<F>(C_WSAIOCTL)(s, code, in, inlen, out, outlen, got, ov, cr);
    g.done();
    trace(C_WSAIOCTL, s, RA, r, r ? g.err : 0, "code=0x%lx in=%s inlen=%lu outlen=%lu got=%ld ov=0x%llx", code,
          data.c_str(), inlen, outlen, !r && got ? long(*got) : -1L, PTR(ov));
    return r;
}

int WSAAPI h_closesocket(SOCKET s)
{
    using F = int(WSAAPI *)(SOCKET);
    if (off())
        return orig<F>(C_CLOSESOCKET)(s);
    Guard g;
    std::string before = sock_desc(s);
    g.pre();
    int r = orig<F>(C_CLOSESOCKET)(s);
    g.done();
    trace_desc(C_CLOSESOCKET, s, before, RA, r, r ? g.err : 0, "closed");
    if (!r || g.err == WSAENOTSOCK)
        drop_socket(s);
    return r;
}

void table()
{
    auto set = [](Call c, const char *name, void *detour) {
        hooks[c].name = name;
        hooks[c].detour = detour;
    };
    set(C_SOCKET, "socket", reinterpret_cast<void *>(&h_socket));
    set(C_WSASOCKETW, "WSASocketW", reinterpret_cast<void *>(&h_WSASocketW));
    set(C_BIND, "bind", reinterpret_cast<void *>(&h_bind));
    set(C_CONNECT, "connect", reinterpret_cast<void *>(&h_connect));
    set(C_SETSOCKOPT, "setsockopt", reinterpret_cast<void *>(&h_setsockopt));
    set(C_IOCTLSOCKET, "ioctlsocket", reinterpret_cast<void *>(&h_ioctlsocket));
    set(C_GETSOCKNAME, "getsockname", reinterpret_cast<void *>(&h_getsockname));
    set(C_SENDTO, "sendto", reinterpret_cast<void *>(&h_sendto));
    set(C_SEND, "send", reinterpret_cast<void *>(&h_send));
    set(C_WSASENDTO, "WSASendTo", reinterpret_cast<void *>(&h_WSASendTo));
    set(C_RECVFROM, "recvfrom", reinterpret_cast<void *>(&h_recvfrom));
    set(C_RECV, "recv", reinterpret_cast<void *>(&h_recv));
    set(C_WSARECVFROM, "WSARecvFrom", reinterpret_cast<void *>(&h_WSARecvFrom));
    set(C_WSARECV, "WSARecv", reinterpret_cast<void *>(&h_WSARecv));
    set(C_WSAGETOVERLAPPEDRESULT, "WSAGetOverlappedResult", reinterpret_cast<void *>(&h_WSAGetOverlappedResult));
    set(C_WSAWAITFORMULTIPLEEVENTS, "WSAWaitForMultipleEvents",
        reinterpret_cast<void *>(&h_WSAWaitForMultipleEvents));
    set(C_WSAEVENTSELECT, "WSAEventSelect", reinterpret_cast<void *>(&h_WSAEventSelect));
    set(C_WSACREATEEVENT, "WSACreateEvent", reinterpret_cast<void *>(&h_WSACreateEvent));
    set(C_WSAIOCTL, "WSAIoctl", reinterpret_cast<void *>(&h_WSAIoctl));
    set(C_CLOSESOCKET, "closesocket", reinterpret_cast<void *>(&h_closesocket));
}

int hook_iat(HMODULE ws)
{
    int total = 0;
    runtime::each_import(ws, "ws2_32.dll", [&](const runtime::ImportSlot &s) {
        for (auto &h : hooks) {
            if (!s.fn || s.fn != h.target)
                continue;
            void *cur = reinterpret_cast<void *>(*s.slot);
            if (h.orig && h.orig != cur) {
                raw("# iat table %d slot exe+0x%llx (%s) %s holds 0x%llx, not 0x%llx; left alone", s.table,
                    (unsigned long long)s.rva, s.how, h.name, PTR(cur), PTR(h.orig));
                break;
            }
            void *was = h.orig;
            h.orig = cur;
            if (!runtime::write_slot(s.slot, h.detour)) {
                h.orig = was;
                break;
            }
            h.slots++;
            total++;
            raw("# iat table %d slot exe+0x%llx (%s) -> %s", s.table, (unsigned long long)s.rva, s.how, h.name);
            break;
        }
    });
    return total;
}

bool inside_ws2(void *p)
{
    auto a = reinterpret_cast<uintptr_t>(p);
    return a >= ws_lo && a < ws_hi;
}

std::string where(void *p)
{
    HMODULE m = nullptr;
    wchar_t path[MAX_PATH];
    char b[160];
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(p), &m) &&
        GetModuleFileNameW(m, path, MAX_PATH)) {
        const wchar_t *base = wcsrchr(path, L'\\');
        snprintf(b, sizeof b, "%ls+0x%llx", base ? base + 1 : path,
                 (unsigned long long)(reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(m)));
        return b;
    }
    snprintf(b, sizeof b, "0x%llx", PTR(p));
    return b;
}

bool hook_inline(Hook &h)
{
    if (!h.target)
        return false;
    if (!inside_ws2(h.target)) {
        raw("# ws2_32!%s forwards to %s, outside ws2_32; not hooked inline", h.name, where(h.target).c_str());
        return false;
    }
    auto made = safetyhook::InlineHook::create(h.target, h.detour, safetyhook::InlineHook::StartDisabled);
    if (!made) {
        raw("# inline hook on ws2_32!%s at 0x%llx failed", h.name, PTR(h.target));
        return false;
    }
    h.orig = made->original<void *>();
    h.inl = std::move(*made);
    if (auto on = h.inl.enable(); !on) {
        raw("# inline hook on ws2_32!%s at 0x%llx could not be enabled", h.name, PTR(h.target));
        h.inl = {};
        h.orig = nullptr;
        return false;
    }
    raw("# inline hook on ws2_32!%s at %s", h.name, where(h.target).c_str());
    return true;
}

void snapshot_modules()
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE)
        return;
    MODULEENTRY32W me{};
    me.dwSize = sizeof me;
    for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me)) {
        char name[MAX_PATH];
        snprintf(name, sizeof name, "%ls", me.szModule);
        auto lo = reinterpret_cast<uintptr_t>(me.modBaseAddr);
        mods.push_back({lo, lo + me.modBaseSize, name});
    }
    CloseHandle(snap);
}

void setup()
{
    char v[16] = {};
    DWORD n = GetEnvironmentVariableA("SWOFTY_NET_TRACE", v, sizeof v);
    if (!n || n >= sizeof v || !strcmp(v, "0"))
        return;
    bool all_inline = !_stricmp(v, "inline");
    char num[24] = {};
    n = GetEnvironmentVariableA("SWOFTY_NET_TRACE_BYTES", num, sizeof num);
    if (n && n < sizeof num)
        hex_max = std::clamp(atoll(num), 1ll, 512ll);
    n = GetEnvironmentVariableA("SWOFTY_NET_TRACE_CAP_KB", num, sizeof num);
    if (n && n < sizeof num)
        cap = uint64_t(std::clamp(atoll(num), 4ll, 1ll << 22)) << 10;
    HMODULE ws = LoadLibraryW(L"ws2_32.dll");
    if (!ws) {
        swofty::log("[net-trace] ws2_32.dll did not load (%lu); socket trace off", GetLastError());
        return;
    }
    std::wstring path = runtime::swofty_root() + L"net-trace.log";
    file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                       nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        swofty::log("[net-trace] cannot create swofty\\net-trace.log (%lu); socket trace off", GetLastError());
        return;
    }
    QueryPerformanceFrequency(&qpf);
    QueryPerformanceCounter(&qpc0);
    auto base = reinterpret_cast<uint8_t *>(GetModuleHandleW(nullptr));
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + reinterpret_cast<IMAGE_DOS_HEADER *>(base)->e_lfanew);
    exe_lo = reinterpret_cast<uintptr_t>(base);
    exe_hi = exe_lo + nt->OptionalHeader.SizeOfImage;
    auto wsb = reinterpret_cast<uint8_t *>(ws);
    ws_lo = reinterpret_cast<uintptr_t>(ws);
    ws_hi = ws_lo + reinterpret_cast<IMAGE_NT_HEADERS *>(wsb + reinterpret_cast<IMAGE_DOS_HEADER *>(wsb)->e_lfanew)
                        ->OptionalHeader.SizeOfImage;
    snapshot_modules();
    table();
    for (auto &h : hooks)
        h.target = reinterpret_cast<void *>(GetProcAddress(ws, h.name));
    p_getsockname = reinterpret_cast<NameFn>(hooks[C_GETSOCKNAME].target);
    p_getpeername = reinterpret_cast<NameFn>(GetProcAddress(ws, "getpeername"));
    p_getsockopt = reinterpret_cast<OptFn>(GetProcAddress(ws, "getsockopt"));
    raw("# swofty net trace: exe timestamp 0x%08lx size 0x%lx, mode %s, first %lld data bytes, cap %llu bytes; per "
        "(call, socket) the first 200 calls are logged, then every 100th",
        nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage, all_inline ? "inline" : "iat", hex_max,
        (unsigned long long)cap);
    raw("# columns: ms thread call #count sock=... <call fields> ret err from=<caller>");
    int slots = all_inline ? 0 : hook_iat(ws);
    std::string inl;
    for (auto &h : hooks)
        if (!h.slots && hook_inline(h))
            inl += std::string(inl.empty() ? "" : ", ") + h.name;
    std::string missing;
    for (auto &h : hooks)
        if (!h.orig)
            missing += std::string(missing.empty() ? "" : ", ") + h.name;
    raw("# ready: %d iat slot(s); inline: %s; not hooked: %s", slots, inl.empty() ? "none" : inl.c_str(),
        missing.empty() ? "none" : missing.c_str());
    swofty::log("[net-trace] socket trace on (%s): %d game import slot(s), inline %s; writing swofty\\net-trace.log",
                all_inline ? "inline" : "iat", slots, inl.empty() ? "none" : inl.c_str());
}

}

namespace runtime {

void net_trace_setup()
{
    setup();
}

}
