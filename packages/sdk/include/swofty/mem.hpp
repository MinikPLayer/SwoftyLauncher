// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#endif

// Guarded reads of game memory. Every read checks that the bytes are committed and readable
// before touching them, so a stale or wrong pointer yields false instead of a crash. The
// VirtualQuery answers are cached per thread for a short time (ttl_ms) in a small per-page table,
// and regions of at least big_region bytes also in a small per-region table, so every page of the
// game's large heap views shares one answer. VirtualQuery under Wine/Proton costs time linear in
// the rest of the region (about 7 us per GB), so the region table is what makes the checks cheap
// enough for per-frame use on large heaps; invalidate() drops every thread's cache
// at once (call it after the game frees or remaps memory you were reading, or never: entries
// expire on their own). in_image() and object_like() tell game objects from garbage: a C++ object
// of the game starts with a vtable inside the exe image. c_string() reads a NUL-terminated string
// and accepts it only as printable UTF-8, so a pointer's own bytes never pass as a name.

namespace swofty::mem {

inline constexpr uint32_t ttl_ms = 250;
inline constexpr size_t big_region = size_t(1) << 20;

namespace detail {

inline size_t utf8_sequence(const unsigned char *s, size_t n)
{
    unsigned char c = s[0];
    if (c < 0x80)
        return (c >= 0x20 && c != 0x7f) || c == '\t' || c == '\n' || c == '\r' ? 1 : 0;
    size_t len = c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
    if (!len || len > n)
        return 0;
    for (size_t i = 1; i < len; i++)
        if ((s[i] & 0xc0) != 0x80)
            return 0;
    uint32_t cp = len == 2   ? (c & 0x1fu) << 6 | (s[1] & 0x3fu)
                  : len == 3 ? (c & 0x0fu) << 12 | (s[1] & 0x3fu) << 6 | (s[2] & 0x3fu)
                             : (c & 0x07u) << 18 | (s[1] & 0x3fu) << 12 | (s[2] & 0x3fu) << 6 | (s[3] & 0x3fu);
    bool overlong = (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000);
    bool surrogate = cp >= 0xd800 && cp <= 0xdfff;
    bool c1 = cp >= 0x80 && cp <= 0x9f;
    return overlong || surrogate || c1 || cp > 0x10ffff ? 0 : len;
}

}

// Valid UTF-8 without control characters (tab, newline and carriage return are allowed).
inline bool printable_utf8(const char *s, size_t n)
{
    auto *u = reinterpret_cast<const unsigned char *>(s);
    for (size_t i = 0; i < n;) {
        size_t k = detail::utf8_sequence(u + i, n - i);
        if (!k)
            return false;
        i += k;
    }
    return true;
}

inline bool printable_utf8(std::string_view s)
{
    return printable_utf8(s.data(), s.size());
}

#ifdef _WIN32

namespace detail {

struct Page {
    uintptr_t page = ~uintptr_t(0);
    uintptr_t lo = 0, hi = 0;
    uint64_t stamp = 0;
    uint32_t generation = 0;
    bool ok = false;
};

inline std::atomic<uint32_t> generation{1};

// Set by Trust: how long a big region's answer stays good on this thread.
inline thread_local uint32_t region_ttl = 0;

struct Image {
    uintptr_t lo = 0, hi = 0;
};

inline Image image()
{
    static const Image img = [] {
        Image r;
        auto *base = reinterpret_cast<const uint8_t *>(GetModuleHandleW(nullptr));
        if (!base)
            return r;
        auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(base + reinterpret_cast<const IMAGE_DOS_HEADER *>(base)->e_lfanew);
        r.lo = reinterpret_cast<uintptr_t>(base);
        r.hi = r.lo + nt->OptionalHeader.SizeOfImage;
        return r;
    }();
    return img;
}

inline bool query(uintptr_t at, uintptr_t &hi)
{
    constexpr size_t slots = 64, region_slots = 8;
    thread_local Page cache[slots];
    thread_local Page regions[region_slots];
    thread_local size_t region_next;
    uintptr_t page = at >> 12;
    Page &e = cache[page % slots];
    uint64_t now = GetTickCount64();
    uint32_t gen = generation.load(std::memory_order_relaxed);
    if (e.page == page && e.generation == gen && now - e.stamp < ttl_ms && at >= e.lo && at < e.hi) {
        hi = e.hi;
        return e.ok;
    }
    uint64_t region_ttl_ms = region_ttl > ttl_ms ? region_ttl : ttl_ms;
    for (Page &r : regions)
        if (r.generation == gen && now - r.stamp < region_ttl_ms && at >= r.lo && at < r.hi) {
            e = {page, r.lo, r.hi, r.stamp, gen, r.ok};
            hi = r.hi;
            return r.ok;
        }
    MEMORY_BASIC_INFORMATION m{};
    if (VirtualQuery(reinterpret_cast<const void *>(at), &m, sizeof m) != sizeof m)
        return false;
    uintptr_t lo = reinterpret_cast<uintptr_t>(m.BaseAddress), end = lo + m.RegionSize;
    if (end <= at)
        return false;
    bool ok = m.State == MEM_COMMIT && (m.Protect & 0xEE) && !(m.Protect & (PAGE_GUARD | PAGE_NOACCESS));
    e = {page, lo, end, now, gen, ok};
    if (end - lo >= big_region)
        regions[region_next++ % region_slots] = e;
    hi = end;
    return ok;
}

}

// Drop every thread's cached VirtualQuery answers.
inline void invalidate()
{
    detail::generation.fetch_add(1, std::memory_order_relaxed);
}

// While one is alive, this thread's reads keep a big region's answer for ms
// instead of ttl_ms. A VirtualQuery inside one of the game's multi-GB heaps
// walks the rest of the region and can take milliseconds, so a poll of
// objects that live as long as those heaps uses one to pay it once a while.
class Trust {
public:
    explicit Trust(uint32_t ms) : was_(detail::region_ttl) { detail::region_ttl = ms; }
    ~Trust() { detail::region_ttl = was_; }
    Trust(const Trust &) = delete;
    Trust &operator=(const Trust &) = delete;

private:
    uint32_t was_;
};

// n bytes at p are committed and readable.
inline bool readable(uintptr_t p, size_t n)
{
    if (!p || p + n < p)
        return false;
    for (uintptr_t at = p, end = p + (n ? n : 1); at < end;) {
        uintptr_t hi = 0;
        if (!detail::query(at, hi))
            return false;
        at = hi;
    }
    return true;
}

inline bool readable(const void *p, size_t n)
{
    return readable(reinterpret_cast<uintptr_t>(p), n);
}

// Copy a T from addr into out when its bytes are readable.
template <class T> bool read(uintptr_t addr, T &out)
{
    if (!readable(addr, sizeof(T)))
        return false;
    std::memcpy(&out, reinterpret_cast<const void *>(addr), sizeof(T));
    return true;
}

template <class T> bool read(const void *addr, T &out)
{
    return read(reinterpret_cast<uintptr_t>(addr), out);
}

// The address lies inside the game exe's image (code, vtables, static data).
inline bool in_image(uintptr_t p)
{
    detail::Image img = detail::image();
    return p >= img.lo && p < img.hi;
}

inline bool in_image(const void *p)
{
    return in_image(reinterpret_cast<uintptr_t>(p));
}

// A readable, 8-byte aligned pointer whose first word is a vtable inside the image.
inline bool object_like(uintptr_t p)
{
    uintptr_t vt = 0;
    return p && !(p & 7) && read(p, vt) && !(vt & 7) && in_image(vt);
}

inline bool object_like(const void *p)
{
    return object_like(reinterpret_cast<uintptr_t>(p));
}

// Read the NUL-terminated string at p into out. True only for 1 to max-1 bytes of printable
// UTF-8 followed by a NUL; out is empty otherwise.
inline bool c_string(uintptr_t p, std::string &out, size_t max = 256)
{
    out.clear();
    if (!p || max < 2)
        return false;
    for (size_t i = 0; i < max; i++) {
        uintptr_t at = p + i;
        if ((i == 0 || !(at & 0xfff)) && !readable(at, 1))
            return out.clear(), false;
        char c = *reinterpret_cast<const char *>(at);
        if (!c)
            return !out.empty() && printable_utf8(out) ? true : (out.clear(), false);
        out.push_back(c);
    }
    out.clear();
    return false;
}

inline bool c_string(const void *p, std::string &out, size_t max = 256)
{
    return c_string(reinterpret_cast<uintptr_t>(p), out, max);
}

#endif

}
