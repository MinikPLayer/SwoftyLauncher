#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "swofty_api.h"
#include <swofty/mem.hpp>

namespace dbread {

constexpr uint64_t FNV64_BASIS = 0xcbf29ce484222325ull;
constexpr uint64_t FNV64_PRIME = 0x100000001b3ull;

constexpr size_t REC_SCALAR = 0x8;
constexpr size_t REC_META = 0x30;

inline uint64_t fnv_cont(uint64_t h, const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        h ^= static_cast<unsigned char>(s[i]);
        h *= FNV64_PRIME;
    }
    return h;
}

inline uint64_t fnv_cont(uint64_t h, const char *s)
{
    return fnv_cont(h, s, strlen(s));
}

inline uint64_t fnv64(const char *s, size_t n)
{
    return fnv_cont(FNV64_BASIS, s, n);
}

inline uint64_t fnv64(const std::string &s)
{
    return fnv_cont(FNV64_BASIS, s.data(), s.size());
}

inline uint64_t fnv_index(uint64_t h, uint64_t index)
{
    char buf[24];
    int n = snprintf(buf, sizeof buf, "%llu", static_cast<unsigned long long>(index));
    return fnv_cont(h, buf, size_t(n));
}

inline std::string row_stem(const std::string &table)
{
    size_t dot = table.rfind('.');
    std::string seg = dot == std::string::npos ? table : table.substr(dot + 1);
    if (!seg.empty() && seg.back() == 's')
        seg.pop_back();
    return seg;
}

struct TableKeys {
    uint64_t count_key;
    uint64_t row_prefix;
    bool ok;
};

inline TableKeys table_keys(const std::string &table)
{
    std::string stem = row_stem(table);
    if (table.empty() || stem.empty())
        return {0, 0, false};
    uint64_t base = fnv64(table);
    uint64_t count_key = fnv_cont(fnv_cont(fnv_cont(base, "._"), stem.c_str(), stem.size()), "_instancecount_");
    uint64_t row_prefix = fnv_cont(fnv_cont(fnv_cont(base, "."), stem.c_str(), stem.size()), "_");
    return {count_key, row_prefix, true};
}

inline uint64_t row_key(uint64_t row_prefix, uint64_t index)
{
    return fnv_index(row_prefix, index);
}

inline uint64_t cell_key(uint64_t row_prefix, uint64_t index, const char *field)
{
    return fnv_cont(fnv_cont(row_key(row_prefix, index), "."), field);
}

inline uint32_t fnv32(const char *s)
{
    uint32_t h = 0x811c9dc5u;
    for (; *s; s++)
        h = (h ^ uint8_t(*s)) * 0x01000193u;
    return h;
}

// A row's label is its _metadata_ field, a string record whose +0x30 holds the FNV-1a-32 of the
// text. The row path itself (<table>.<row>_<i>) has no record. Gdb_Find_RowByLabel (0x1445a2bd0)
// scans rows 0.._instancecount_-1 the same way.
inline uint64_t label_key(uint64_t row_prefix, uint64_t index)
{
    return cell_key(row_prefix, index, "_metadata_");
}

inline int32_t read_i32(const void *record)
{
    return *reinterpret_cast<const int32_t *>(static_cast<const uint8_t *>(record) + REC_SCALAR);
}

inline float read_f32(const void *record)
{
    return *reinterpret_cast<const float *>(static_cast<const uint8_t *>(record) + REC_SCALAR);
}

inline uint8_t read_u8(const void *record)
{
    return *(static_cast<const uint8_t *>(record) + REC_SCALAR);
}

constexpr size_t TEXT_MAX = 16384;

// Decode a gamedb string: size bytes of enc including the terminator, plain[i] =
// key[(i - 1) & 3] ^ enc[i] (the game's inline decode, k_Gdb_StringXorKey). True for printable
// UTF-8 of at most TEXT_MAX - 1 bytes; an empty string (size 1) is valid.
inline bool decode_text(const uint8_t *enc, uint64_t size, const uint8_t key[4], std::string &out)
{
    out.clear();
    if (!enc || !size || size > TEXT_MAX)
        return false;
    out.resize(size_t(size - 1));
    for (uint64_t i = 0; i + 1 < size; i++)
        out[size_t(i)] = char(key[(i - 1) & 3] ^ enc[i]);
    if (out.find('\0') != std::string::npos || !swofty::mem::printable_utf8(out)) {
        out.clear();
        return false;
    }
    return true;
}

inline void encode_text(const char *plain, const uint8_t key[4], std::string &out)
{
    size_t n = strlen(plain);
    out.assign(n + 1, '\0');
    for (size_t i = 0; i < n; i++)
        out[i] = char(key[(i - 1) & 3] ^ uint8_t(plain[i]));
}

inline uint32_t read_meta(const void *record)
{
    return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(record) + REC_META);
}

template <class Record> int64_t find_row(Record &&record, uint64_t row_prefix, uint64_t count, const char *label)
{
    uint32_t want = fnv32(label);
    for (uint64_t i = 0; i < count; i++) {
        const void *rec = record(label_key(row_prefix, i));
        if (rec && read_meta(rec) == want)
            return int64_t(i);
    }
    return -1;
}

inline void decode(const void *record, int tag, SwoftyDbValue *out)
{
    out->tag = uint8_t(SWOFTY_DB_STRING);
    out->i = 0;
    out->f = 0;
    out->s = nullptr;
    switch (tag) {
    case SWOFTY_DB_INT:
        out->tag = SWOFTY_DB_INT;
        out->i = read_i32(record);
        out->f = double(out->i);
        break;
    case SWOFTY_DB_FLOAT:
        out->tag = SWOFTY_DB_FLOAT;
        out->f = read_f32(record);
        out->i = int64_t(out->f);
        break;
    case SWOFTY_DB_BOOL:
        out->tag = SWOFTY_DB_BOOL;
        out->i = read_u8(record) ? 1 : 0;
        out->f = double(out->i);
        break;
    default:
        out->tag = SWOFTY_DB_STRING;
        out->i = read_meta(record);
        break;
    }
}


// The game database map is a robin_hood map that grows (to twice its slots) when an insert finds it at 80% of
// its power-of-two slot count. The game sizes it while it loads the file; growing it after that leaves the
// game's pointers into the old slots dangling and crashes the game at boot (Gdb_Init_TableCursor under
// Loc_LocaleModule_Init). grows_at is the record count at which the next insert grows a map of `slots`.
constexpr uint64_t grows_at(uint64_t slots)
{
    return slots / 100 * 80 + slots % 100 * 80 / 100;
}

// Whether `added` new records fit in a map of `slots` holding `count`, with `spare` left for the core's class
// probes and the game's own later inserts.
constexpr bool fits_without_growing(uint64_t count, uint64_t added, uint64_t slots, uint64_t spare)
{
    return count + added + spare <= grows_at(slots);
}

}
