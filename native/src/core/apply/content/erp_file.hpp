#pragma once

#include <zstd.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

// An ERP archive (version 3 and later) read from a byte source and written again with some fragments replaced
// (tools/erpw.py does the same): the header is kept, the index is rebuilt with the new offsets, sizes and hashes
// of the changed resources, and every other fragment is copied as it is stored.

namespace swofty_core::apply::content::erpfile {

inline constexpr uint8_t ZSTD_COMP = 0x11;
inline constexpr int ZSTD_LEVEL = 9;
inline constexpr size_t COPY_CHUNK = size_t(1) << 20;

struct Source {
    uint64_t size = 0;
    std::function<bool(uint64_t off, size_t n, char *out)> read;
};

using Sink = std::function<bool(const char *, size_t)>;

struct Frag {
    char name[4];
    uint64_t off = 0, size = 0, packed = 0;
    uint32_t flags = 0;
    uint8_t comp = 0;
};

struct Entry {
    std::string name, mid;
    std::vector<Frag> frags;
    char hash[16] = {};
};

struct Archive {
    std::string header;
    uint32_t version = 0;
    uint64_t data_off = 0;
    std::vector<Entry> entries;

    const Entry *find(const std::string &name) const
    {
        for (auto &e : entries)
            if (e.name == name)
                return &e;
        return nullptr;
    }
};

inline uint32_t rd32(const std::string &s, size_t at)
{
    uint32_t v = 0;
    std::memcpy(&v, s.data() + at, 4);
    return v;
}

inline uint64_t rd64(const std::string &s, size_t at)
{
    uint64_t v = 0;
    std::memcpy(&v, s.data() + at, 8);
    return v;
}

inline bool open(const Source &src, Archive &out, std::string &why)
{
    out = {};
    out.header.resize(0x30);
    if (src.size < 0x30 || !src.read(0, 0x30, out.header.data()) || rd32(out.header, 0) != 0x4b505245)
        return why = "not an ERP archive", false;
    out.version = rd32(out.header, 4);
    uint64_t info_off = rd64(out.header, 0x10), info_size = rd64(out.header, 0x18);
    out.data_off = rd64(out.header, 0x20);
    if (out.version < 3 || info_off != 0x30 || info_size < 8 || info_size > (256u << 20) || out.data_off != info_off + info_size ||
        out.data_off > src.size)
        return why = "unsupported ERP header", false;
    std::string idx(size_t(info_size), '\0');
    if (!src.read(info_off, idx.size(), idx.data()))
        return why = "the index could not be read", false;
    uint32_t n = rd32(idx, 0);
    size_t unk = out.version >= 4 ? 6 : 4, p = 8;
    for (uint32_t i = 0; i < n; i++) {
        if (p + 6 > idx.size())
            return why = "the index is cut off", false;
        uint32_t esz = rd32(idx, p);
        uint16_t nlen = uint16_t(uint8_t(idx[p + 4]) | uint8_t(idx[p + 5]) << 8);
        size_t q = p + 6;
        if (!nlen || p + 4 + esz > idx.size() || q + nlen + 16 + unk + 1 > idx.size())
            return why = "an index entry is cut off", false;
        Entry e;
        e.name.assign(idx.data() + q, nlen - 1);
        q += nlen;
        e.mid.assign(idx.data() + q, 16 + unk);
        q += 16 + unk;
        uint8_t nf = uint8_t(idx[q++]);
        if (q + size_t(nf) * 0x21 + 16 != p + 4 + esz)
            return why = "an index entry has a different size than its fragments", false;
        for (uint8_t j = 0; j < nf; j++, q += 0x21) {
            Frag f;
            std::memcpy(f.name, idx.data() + q, 4);
            f.off = rd64(idx, q + 4);
            f.size = rd64(idx, q + 12);
            f.flags = rd32(idx, q + 20);
            f.comp = uint8_t(idx[q + 24]);
            f.packed = rd64(idx, q + 25);
            if (f.off + f.packed > src.size - out.data_off)
                return why = "a fragment lies past the end of the file", false;
            e.frags.push_back(f);
        }
        std::memcpy(e.hash, idx.data() + q, 16);
        p = q + 16;
        out.entries.push_back(std::move(e));
    }
    return true;
}

inline bool fragment(const Source &src, const Archive &a, const Frag &f, std::string &out, std::string &why)
{
    std::string raw(size_t(f.packed), '\0');
    if (!src.read(a.data_off + f.off, raw.size(), raw.data()))
        return why = "a fragment could not be read", false;
    if (f.comp != ZSTD_COMP) {
        out = std::move(raw);
        return true;
    }
    out.assign(size_t(f.size), '\0');
    size_t got = ZSTD_decompress(out.data(), out.size(), raw.data(), raw.size());
    if (ZSTD_isError(got) || got != f.size)
        return why = "a fragment does not decompress", false;
    return true;
}

inline void md5(const std::string &data, char out[16])
{
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af,
        0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
        0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
        0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97,
        0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
        0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const int R[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9,  14, 20, 5, 9,
                              14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                              4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
    std::string m = data;
    m += char(0x80);
    while (m.size() % 64 != 56)
        m += '\0';
    uint64_t bits = uint64_t(data.size()) * 8;
    m.append(reinterpret_cast<const char *>(&bits), 8);
    for (size_t o = 0; o < m.size(); o += 64) {
        uint32_t w[16];
        std::memcpy(w, m.data() + o, 64);
        uint32_t a = a0, b = b0, c = c0, d = d0;
        for (int i = 0; i < 64; i++) {
            uint32_t f;
            int g;
            if (i < 16)
                f = (b & c) | (~b & d), g = i;
            else if (i < 32)
                f = (d & b) | (~d & c), g = (5 * i + 1) % 16;
            else if (i < 48)
                f = b ^ c ^ d, g = (3 * i + 5) % 16;
            else
                f = c ^ (b | ~d), g = (7 * i) % 16;
            uint32_t t = d;
            d = c;
            c = b;
            uint32_t x = a + f + K[i] + w[g];
            b = b + ((x << R[i]) | (x >> (32 - R[i])));
            a = t;
        }
        a0 += a, b0 += b, c0 += c, d0 += d;
    }
    std::memcpy(out, &a0, 4);
    std::memcpy(out + 4, &b0, 4);
    std::memcpy(out + 8, &c0, 4);
    std::memcpy(out + 12, &d0, 4);
}

using Changes = std::map<std::string, std::vector<std::optional<std::string>>>;

inline bool rewrite(const Source &src, const Archive &a, const Changes &changes, const Sink &sink, std::string &why)
{
    struct Out {
        Entry e;
        std::vector<std::optional<std::string>> packed;
    };
    std::vector<Out> outs;
    uint64_t off = 0, total_frags = 0;
    for (auto &orig : a.entries) {
        Out o{orig, std::vector<std::optional<std::string>>(orig.frags.size())};
        auto it = changes.find(orig.name);
        if (it != changes.end()) {
            std::string all;
            for (size_t i = 0; i < orig.frags.size(); i++) {
                std::string data;
                if (i < it->second.size() && it->second[i])
                    data = *it->second[i];
                else if (!fragment(src, a, orig.frags[i], data, why))
                    return false;
                all += data;
                if (i < it->second.size() && it->second[i]) {
                    Frag &f = o.e.frags[i];
                    f.size = data.size();
                    if (f.comp == ZSTD_COMP) {
                        std::string z(ZSTD_compressBound(data.size()), '\0');
                        size_t n = ZSTD_compress(z.data(), z.size(), data.data(), data.size(), ZSTD_LEVEL);
                        if (ZSTD_isError(n))
                            return why = "a fragment does not compress", false;
                        z.resize(n);
                        o.packed[i] = std::move(z);
                    } else
                        o.packed[i] = std::move(data);
                    f.packed = o.packed[i]->size();
                }
            }
            md5(all, o.e.hash);
        }
        for (auto &f : o.e.frags) {
            f.off = off;
            off += f.packed;
            total_frags++;
        }
        outs.push_back(std::move(o));
    }
    std::string idx;
    for (auto &o : outs) {
        std::string e;
        std::string nm = o.e.name + '\0';
        uint16_t nlen = uint16_t(nm.size());
        e.append(reinterpret_cast<const char *>(&nlen), 2);
        e += nm + o.e.mid + char(o.e.frags.size());
        for (auto &f : o.e.frags) {
            e.append(f.name, 4);
            e.append(reinterpret_cast<const char *>(&f.off), 8);
            e.append(reinterpret_cast<const char *>(&f.size), 8);
            e.append(reinterpret_cast<const char *>(&f.flags), 4);
            e += char(f.comp);
            e.append(reinterpret_cast<const char *>(&f.packed), 8);
        }
        e.append(o.e.hash, 16);
        uint32_t esz = uint32_t(e.size());
        idx.append(reinterpret_cast<const char *>(&esz), 4);
        idx += e;
    }
    uint32_t nres = uint32_t(outs.size()), nfr = uint32_t(total_frags);
    std::string info(reinterpret_cast<const char *>(&nres), 4);
    info.append(reinterpret_cast<const char *>(&nfr), 4);
    info += idx;
    std::string hdr = a.header;
    uint64_t io = 0x30, isz = info.size(), dof = 0x30 + info.size();
    std::memcpy(&hdr[0x10], &io, 8);
    std::memcpy(&hdr[0x18], &isz, 8);
    std::memcpy(&hdr[0x20], &dof, 8);
    if (!sink(hdr.data(), hdr.size()) || !sink(info.data(), info.size()))
        return why = "the output could not be written", false;
    std::string buf;
    for (size_t r = 0; r < outs.size(); r++)
        for (size_t i = 0; i < outs[r].e.frags.size(); i++) {
            if (outs[r].packed[i]) {
                if (!sink(outs[r].packed[i]->data(), outs[r].packed[i]->size()))
                    return why = "the output could not be written", false;
                continue;
            }
            const Frag &f = a.entries[r].frags[i];
            for (uint64_t done = 0; done < f.packed;) {
                size_t n = size_t(std::min<uint64_t>(COPY_CHUNK, f.packed - done));
                buf.resize(n);
                if (!src.read(a.data_off + f.off + done, n, buf.data()))
                    return why = "a fragment could not be read", false;
                if (!sink(buf.data(), n))
                    return why = "the output could not be written", false;
                done += n;
            }
        }
    return true;
}

}
