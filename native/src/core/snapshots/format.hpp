#pragma once
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <zstd.h>

// The swofty_registry save entry, without the game (packages/sdk/std/fixtures/format.md has the byte
// layout). The payload is a list of tagged items; a career is one item, and inside it the freeze mode,
// the stat-id map, the registry majors used and one item per season hold the frozen records of each
// snapshot registry, compressed one registry and season at a time. Every item keeps its own bytes, so
// what this core does not know (a newer tag, an unknown registry, a newer major) is written back byte
// for byte, and a re-encode only changes the items this core froze.
//
// The game carries the payload as a property bag (one root child of the profile save, through the
// save splice): a "format" string and the payload in base64, split over "d000".."d999" string
// properties (type 'dstr': u32 length and the characters).

namespace swofty_core::snapshots::format {

using Bytes = std::vector<uint8_t>;

constexpr uint16_t ENTRY_MAJOR = 1, ENTRY_MINOR = 0;
constexpr const char *ENTRY_NAME = "swofty_registry";
constexpr const char *FORMAT_TAG = "swofty_registry/1";
constexpr size_t CAREER_CAP = 256 * 1024;
constexpr size_t ENTRY_CAP = 4u << 20;
constexpr size_t CHUNK = 32000;
constexpr size_t MAX_CHUNKS = 1000;
constexpr size_t MAX_PAYLOAD = 16u << 20;
constexpr size_t MAX_RECORDS_RAW = 8u << 20;

constexpr uint32_t fourcc(const char (&s)[5])
{
    return uint32_t(uint8_t(s[0])) | uint32_t(uint8_t(s[1])) << 8 | uint32_t(uint8_t(s[2])) << 16 |
           uint32_t(uint8_t(s[3])) << 24;
}
constexpr uint32_t TYPE_BAG = fourcc("pbag");
constexpr uint32_t TYPE_DSTR = fourcc("dstr");

enum Top : uint16_t { T_CAREER = 1, T_TEST_PAD = 0x7f01 };
enum CareerTag : uint16_t { C_ID = 1, C_FREEZE = 2, C_IDMAP = 3, C_MAJORS = 4, C_SEASON = 5 };
enum SeasonTag : uint16_t { S_YEAR = 1, S_REGISTRY = 2, S_REFUSED = 3 };
enum RegistryTag : uint16_t { R_NAME = 1, R_VERSION = 2, R_RECORDS = 3 };

enum class Freeze : uint8_t { per_season = 0, at_creation = 1 };

inline uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
    }
    return ~c;
}

struct Writer {
    Bytes out;
    void u8(uint8_t v) { out.push_back(v); }
    void raw(const void *p, size_t n)
    {
        auto *b = static_cast<const uint8_t *>(p);
        out.insert(out.end(), b, b + n);
    }
    void u16(uint16_t v) { u8(uint8_t(v)), u8(uint8_t(v >> 8)); }
    void u32(uint32_t v)
    {
        for (int i = 0; i < 4; i++)
            u8(uint8_t(v >> (8 * i)));
    }
    void u64(uint64_t v)
    {
        for (int i = 0; i < 8; i++)
            u8(uint8_t(v >> (8 * i)));
    }
    void item(uint16_t tag, const void *p, size_t n)
    {
        u16(tag);
        u32(uint32_t(n));
        raw(p, n);
    }
    void item(uint16_t tag, const Bytes &b) { item(tag, b.data(), b.size()); }
    void item(uint16_t tag, const std::string &s) { item(tag, s.data(), s.size()); }
};

struct Reader {
    const uint8_t *p;
    size_t n, at = 0;
    bool ok = true;
    Reader(const uint8_t *data, size_t size) : p(data), n(size) {}
    explicit Reader(const Bytes &b) : p(b.data()), n(b.size()) {}
    bool need(size_t k)
    {
        ok = ok && k <= n - at;
        return ok;
    }
    bool done() const { return at == n; }
    uint8_t u8() { return need(1) ? p[at++] : 0; }
    uint16_t u16()
    {
        if (!need(2))
            return 0;
        uint16_t v = uint16_t(p[at] | p[at + 1] << 8);
        at += 2;
        return v;
    }
    uint32_t u32()
    {
        if (!need(4))
            return 0;
        uint32_t v = 0;
        for (int i = 0; i < 4; i++)
            v |= uint32_t(p[at + i]) << (8 * i);
        at += 4;
        return v;
    }
    uint64_t u64()
    {
        if (!need(8))
            return 0;
        uint64_t v = 0;
        for (int i = 0; i < 8; i++)
            v |= uint64_t(p[at + i]) << (8 * i);
        at += 8;
        return v;
    }
};

// One tagged item: its tag and body, and the whole item's bytes for a byte-for-byte write-back.
struct Item {
    uint16_t tag = 0;
    Bytes body;
    Bytes encoded() const
    {
        Writer w;
        w.item(tag, body);
        return w.out;
    }
};

inline bool items(const uint8_t *p, size_t n, std::vector<Item> &out)
{
    Reader r(p, n);
    while (r.ok && !r.done()) {
        Item it;
        it.tag = r.u16();
        uint32_t len = r.u32();
        if (!r.need(len))
            return false;
        it.body.assign(p + r.at, p + r.at + len);
        r.at += len;
        out.push_back(std::move(it));
    }
    return r.ok;
}
inline bool items(const Bytes &b, std::vector<Item> &out) { return items(b.data(), b.size(), out); }

inline std::string text(const Bytes &b) { return std::string(b.begin(), b.end()); }

inline bool compress(const std::string &in, Bytes &out)
{
    size_t cap = ZSTD_compressBound(in.size());
    out.assign(cap, 0);
    size_t z = ZSTD_compress(out.data(), cap, in.data(), in.size(), 9);
    if (ZSTD_isError(z))
        return false;
    out.resize(z);
    return true;
}

inline bool decompress(const Bytes &in, std::string &out, size_t limit = MAX_RECORDS_RAW)
{
    unsigned long long size = ZSTD_getFrameContentSize(in.data(), in.size());
    if (size == ZSTD_CONTENTSIZE_ERROR || size == ZSTD_CONTENTSIZE_UNKNOWN || size > limit)
        return false;
    out.assign(size_t(size), '\0');
    size_t got = ZSTD_decompress(out.data(), out.size(), in.data(), in.size());
    return !ZSTD_isError(got) && got == size;
}

// One snapshot registry's frozen records for one season. version is what it was written with; body
// is its raw item list (decoded only when read), so it round-trips byte for byte.
struct Frozen {
    std::string registry;
    uint16_t major = 1, minor = 0;
    Bytes records;
    std::vector<Item> extra;

    Bytes encode() const
    {
        Writer w;
        w.item(R_NAME, registry);
        Writer v;
        v.u16(major), v.u16(minor);
        w.item(R_VERSION, v.out);
        w.item(R_RECORDS, records);
        for (auto &e : extra) {
            Bytes b = e.encoded();
            w.raw(b.data(), b.size());
        }
        return w.out;
    }

    static bool decode(const Bytes &b, Frozen &out)
    {
        std::vector<Item> list;
        if (!items(b, list))
            return false;
        bool name = false, version = false, recs = false;
        for (auto &it : list) {
            if (it.tag == R_NAME && !name)
                out.registry = text(it.body), name = true;
            else if (it.tag == R_VERSION && !version && it.body.size() >= 4) {
                Reader r(it.body);
                out.major = r.u16(), out.minor = r.u16(), version = true;
            } else if (it.tag == R_RECORDS && !recs)
                out.records = it.body, recs = true;
            else
                out.extra.push_back(it);
        }
        return name && version && recs && !out.registry.empty();
    }
};

struct Season {
    int32_t year = 0;
    std::vector<Frozen> frozen;
    std::vector<std::string> refused;
    std::vector<Item> extra;

    const Frozen *find(const std::string &registry) const
    {
        for (auto &f : frozen)
            if (f.registry == registry)
                return &f;
        return nullptr;
    }

    Bytes encode() const
    {
        Writer w;
        Writer y;
        y.u32(uint32_t(year));
        w.item(S_YEAR, y.out);
        for (auto &f : frozen)
            w.item(S_REGISTRY, f.encode());
        for (auto &r : refused)
            w.item(S_REFUSED, r);
        for (auto &e : extra) {
            Bytes b = e.encoded();
            w.raw(b.data(), b.size());
        }
        return w.out;
    }

    static bool decode(const Bytes &b, Season &out)
    {
        std::vector<Item> list;
        if (!items(b, list))
            return false;
        bool year = false;
        for (auto &it : list) {
            if (it.tag == S_YEAR && !year && it.body.size() == 4) {
                Reader r(it.body);
                out.year = int32_t(r.u32()), year = true;
            } else if (it.tag == S_REGISTRY) {
                Frozen f;
                if (!Frozen::decode(it.body, f))
                    return false;
                out.frozen.push_back(std::move(f));
            } else if (it.tag == S_REFUSED)
                out.refused.push_back(text(it.body));
            else
                out.extra.push_back(it);
        }
        return year;
    }
};

struct Career {
    std::string id;
    Freeze freeze = Freeze::per_season;
    std::string idmap = "{}";
    std::string majors = "{}";
    std::vector<Season> seasons;
    std::vector<Item> extra;
    // A career whose section could not be read is kept as its raw section, written back unchanged.
    bool unreadable = false;
    Bytes raw;

    Season *season(int32_t year)
    {
        for (auto &s : seasons)
            if (s.year == year)
                return &s;
        return nullptr;
    }
    const Season *season(int32_t year) const { return const_cast<Career *>(this)->season(year); }

    Bytes encode() const
    {
        if (unreadable)
            return raw;
        Writer w;
        w.item(C_ID, id);
        uint8_t f = uint8_t(freeze);
        w.item(C_FREEZE, &f, 1);
        Bytes map;
        compress(idmap, map);
        w.item(C_IDMAP, map);
        w.item(C_MAJORS, majors);
        for (auto &s : seasons)
            w.item(C_SEASON, s.encode());
        for (auto &e : extra) {
            Bytes b = e.encoded();
            w.raw(b.data(), b.size());
        }
        return w.out;
    }

    size_t size() const { return encode().size(); }

    static bool decode(const Bytes &b, Career &out)
    {
        std::vector<Item> list;
        if (!items(b, list))
            return false;
        bool id = false;
        for (auto &it : list) {
            if (it.tag == C_ID && !id)
                out.id = text(it.body), id = true;
            else if (it.tag == C_FREEZE && it.body.size() == 1)
                out.freeze = it.body[0] == 1 ? Freeze::at_creation : Freeze::per_season;
            else if (it.tag == C_IDMAP) {
                if (!decompress(it.body, out.idmap, 4u << 20))
                    return false;
            } else if (it.tag == C_MAJORS)
                out.majors = text(it.body);
            else if (it.tag == C_SEASON) {
                Season s;
                if (!Season::decode(it.body, s))
                    return false;
                out.seasons.push_back(std::move(s));
            } else
                out.extra.push_back(it);
        }
        return id && !out.id.empty();
    }
};

// The career id of a section that did not decode, when its id item at least reads.
inline std::string raw_id(const Bytes &b)
{
    std::vector<Item> list;
    if (!items(b, list) || list.empty() || list[0].tag != C_ID)
        return {};
    return text(list[0].body);
}

struct Entry {
    uint16_t major = ENTRY_MAJOR, minor = ENTRY_MINOR;
    std::vector<Career> careers;
    std::vector<Item> extra;

    Career *career(const std::string &id)
    {
        for (auto &c : careers)
            if (c.id == id)
                return &c;
        return nullptr;
    }

    Bytes encode() const
    {
        Writer body;
        for (auto &c : careers)
            body.item(T_CAREER, c.encode());
        for (auto &e : extra) {
            Bytes b = e.encoded();
            body.raw(b.data(), b.size());
        }
        Writer w;
        w.raw("SWRG", 4);
        w.u16(major), w.u16(minor);
        w.u32(crc32(body.out.data(), body.out.size()));
        w.raw(body.out.data(), body.out.size());
        return w.out;
    }
};

enum class Decoded { ok, corrupt, newer };

inline const char *decoded_name(Decoded d) { return d == Decoded::ok ? "ok" : d == Decoded::newer ? "newer" : "corrupt"; }

inline Decoded decode(const Bytes &b, Entry &out, std::string &why)
{
    out = Entry{};
    if (b.size() < 12 || memcmp(b.data(), "SWRG", 4) != 0)
        return why = "no SWRG header", Decoded::corrupt;
    Reader r(b);
    r.at = 4;
    out.major = r.u16(), out.minor = r.u16();
    uint32_t crc = r.u32();
    if (out.major > ENTRY_MAJOR)
        return why = "entry format " + std::to_string(out.major) + "." + std::to_string(out.minor) +
                     " is newer than this core's " + std::to_string(ENTRY_MAJOR),
               Decoded::newer;
    if (out.major < 1)
        return why = "entry format 0 is not a format", Decoded::corrupt;
    if (crc32(b.data() + 12, b.size() - 12) != crc)
        return why = "its checksum does not match", Decoded::corrupt;
    std::vector<Item> list;
    if (!items(b.data() + 12, b.size() - 12, list))
        return why = "an item runs past the end", Decoded::corrupt;
    for (auto &it : list) {
        if (it.tag != T_CAREER) {
            out.extra.push_back(it);
            continue;
        }
        Career c;
        if (!Career::decode(it.body, c)) {
            c = Career{};
            c.id = raw_id(it.body);
            c.unreadable = true;
            c.raw = it.body;
        }
        out.careers.push_back(std::move(c));
    }
    return Decoded::ok;
}

inline std::string base64(const Bytes &b)
{
    static const char *A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((b.size() + 2) / 3 * 4);
    for (size_t i = 0; i < b.size(); i += 3) {
        uint32_t v = uint32_t(b[i]) << 16 | (i + 1 < b.size() ? uint32_t(b[i + 1]) << 8 : 0) |
                     (i + 2 < b.size() ? uint32_t(b[i + 2]) : 0);
        out += A[v >> 18 & 63];
        out += A[v >> 12 & 63];
        out += i + 1 < b.size() ? A[v >> 6 & 63] : '=';
        out += i + 2 < b.size() ? A[v & 63] : '=';
    }
    return out;
}

inline bool unbase64(const std::string &s, Bytes &out)
{
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z')
            return c - 'A';
        if (c >= 'a' && c <= 'z')
            return c - 'a' + 26;
        if (c >= '0' && c <= '9')
            return c - '0' + 52;
        return c == '+' ? 62 : c == '/' ? 63 : -1;
    };
    if (s.size() % 4)
        return false;
    out.clear();
    out.reserve(s.size() / 4 * 3);
    for (size_t i = 0; i < s.size(); i += 4) {
        int a = val(s[i]), b = val(s[i + 1]);
        int c = s[i + 2] == '=' ? -2 : val(s[i + 2]), d = s[i + 3] == '=' ? -2 : val(s[i + 3]);
        bool last = i + 4 == s.size();
        if (a < 0 || b < 0 || c == -1 || d == -1 || (c == -2 && d != -2) || ((c == -2 || d == -2) && !last))
            return false;
        uint32_t v = uint32_t(a) << 18 | uint32_t(b) << 12 | uint32_t(c < 0 ? 0 : c) << 6 | uint32_t(d < 0 ? 0 : d);
        out.push_back(uint8_t(v >> 16));
        if (c >= 0)
            out.push_back(uint8_t(v >> 8));
        if (d >= 0)
            out.push_back(uint8_t(v));
    }
    return true;
}

inline void key(Writer &w, const std::string &k)
{
    w.u8(uint8_t(k.size()));
    w.raw(k.data(), k.size());
}

inline void dstr(Writer &w, const std::string &name, const std::string &value)
{
    key(w, name);
    w.u32(TYPE_DSTR);
    w.u32(uint32_t(value.size()));
    w.raw(value.data(), value.size());
}

inline std::string chunk_name(size_t i)
{
    char n[8];
    snprintf(n, sizeof n, "d%03zu", i);
    return n;
}

// The bag body the save splice writes for the entry (the codec header 1, the entry count, then each
// entry), the same bytes Game_SaveData_WriteBag writes for a bag of these string properties.
inline bool bag_body(const Bytes &payload, Bytes &out)
{
    std::string b64 = base64(payload);
    size_t chunks = (b64.size() + CHUNK - 1) / CHUNK;
    if (chunks > MAX_CHUNKS || payload.size() > MAX_PAYLOAD)
        return false;
    Writer w;
    w.u64(1);
    w.u64(1 + chunks);
    dstr(w, "format", FORMAT_TAG);
    for (size_t i = 0; i < chunks; i++)
        dstr(w, chunk_name(i), b64.substr(i * CHUNK, CHUNK));
    out = std::move(w.out);
    return true;
}

// The payload out of the bag body the splice kept at a profile load (Game_SaveData_WriteBag's bytes,
// whose entry order need not be the order written). Anything but the string properties above is
// refused: this is not an entry this core wrote.
inline bool parse_bag(const Bytes &body, Bytes &payload, std::string &why)
{
    Reader r(body);
    if (r.u64() != 1)
        return why = "the bag header is not 1", false;
    uint64_t n = r.u64();
    if (!r.ok || n == 0 || n > MAX_CHUNKS + 1)
        return why = "the bag has " + std::to_string(n) + " entries", false;
    std::string format;
    std::map<size_t, std::string> chunks;
    for (uint64_t i = 0; i < n; i++) {
        size_t klen = r.u8();
        if (!r.need(klen))
            return why = "an entry name runs past the end", false;
        std::string k(reinterpret_cast<const char *>(body.data() + r.at), klen);
        r.at += klen;
        uint32_t type = r.u32();
        if (!r.ok || type != TYPE_DSTR)
            return why = "entry " + k + " is not a string", false;
        uint32_t len = r.u32();
        if (!r.need(len))
            return why = "entry " + k + " runs past the end", false;
        std::string v(reinterpret_cast<const char *>(body.data() + r.at), len);
        r.at += len;
        if (k == "format")
            format = v;
        else if (k.size() == 4 && k[0] == 'd' && isdigit(uint8_t(k[1])) && isdigit(uint8_t(k[2])) &&
                 isdigit(uint8_t(k[3])))
            chunks[size_t(std::stoul(k.substr(1)))] = std::move(v);
        else
            return why = "entry " + k + " is not one this core writes", false;
    }
    if (!r.done())
        return why = "bytes follow the last entry", false;
    if (format != FORMAT_TAG)
        return why = "its format is '" + format + "'", false;
    std::string all;
    size_t next = 0;
    for (auto &[i, c] : chunks) {
        if (i != next++)
            return why = "chunk " + chunk_name(next - 1) + " is missing", false;
        all += c;
    }
    if (!unbase64(all, payload))
        return why = "its data is not base64", false;
    return true;
}

}
