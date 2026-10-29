#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// The game database file (game_package/gamedatabases/gamedatabases.2025.bin): a record count, then per
// record its FNV-1a-64 key (low and high halves), its tag, the masked text's length, the text and a 0. The
// mask runs over every record's text in turn (tools/gamedb.py).

namespace swofty_core::apply::content::gvdb {

struct Record {
    int tag = -1;
    std::string text;
};

inline uint64_t fnv64(std::string_view s)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 0x100000001b3ull;
    }
    return h;
}

inline constexpr char MASK[] = "tG5MYmBZEZZnrH7yg2qTC4qj42Jcq9";
inline constexpr std::size_t MASK_LEN = sizeof MASK - 1, MASK_START = 10;

inline bool read(const unsigned char *data, std::size_t size, std::unordered_map<uint64_t, Record> &out)
{
    constexpr std::size_t n = MASK_LEN, start = MASK_START;
    if (size < 4)
        return false;
    uint32_t count;
    std::memcpy(&count, data, 4);
    std::size_t p = 4, stream = 0;
    out.reserve(out.size() + count);
    for (uint32_t i = 0; i < count; i++) {
        if (p + 13 > size)
            return false;
        uint32_t lo, hi, len;
        std::memcpy(&lo, data + p, 4);
        std::memcpy(&hi, data + p + 4, 4);
        int tag = data[p + 8];
        std::memcpy(&len, data + p + 9, 4);
        if (p + 14 + len > size || data[p + 13 + len] != 0)
            return false;
        Record r{tag, std::string(len, '\0')};
        for (uint32_t k = 0; k < len; k++)
            r.text[k] = char((data[p + 13 + k] - unsigned(MASK[(start + stream + k) % n])) & 0xff);
        stream += len;
        out[(uint64_t(hi) << 32) | lo] = std::move(r);
        p += 14 + len;
    }
    return p == size;
}

// The file kept whole with a sorted index of its keys: a record's text is unmasked when it is read.
class File {
public:
    bool load(std::string bytes)
    {
        data_ = std::move(bytes);
        idx_.clear();
        auto d = reinterpret_cast<const unsigned char *>(data_.data());
        std::size_t size = data_.size();
        if (size < 4 || size > 0xffffffffu)
            return false;
        uint32_t count;
        std::memcpy(&count, d, 4);
        idx_.reserve(count);
        std::size_t p = 4, stream = 0;
        for (uint32_t i = 0; i < count; i++) {
            if (p + 13 > size)
                return fail();
            uint32_t lo, hi, len;
            std::memcpy(&lo, d + p, 4);
            std::memcpy(&hi, d + p + 4, 4);
            std::memcpy(&len, d + p + 9, 4);
            if (p + 14 + len > size || d[p + 13 + len] != 0)
                return fail();
            idx_.push_back({(uint64_t(hi) << 32) | lo, uint32_t(p), uint32_t(stream % MASK_LEN)});
            stream += len;
            p += 14 + len;
        }
        if (p != size)
            return fail();
        std::stable_sort(idx_.begin(), idx_.end(), [](const Entry &a, const Entry &b) { return a.key < b.key; });
        std::vector<Entry> last;
        last.reserve(idx_.size());
        for (std::size_t i = 0; i < idx_.size(); i++)
            if (i + 1 == idx_.size() || idx_[i + 1].key != idx_[i].key)
                last.push_back(idx_[i]);
        idx_.swap(last);
        return true;
    }

    std::optional<Record> get(uint64_t key) const
    {
        auto it = std::lower_bound(idx_.begin(), idx_.end(), key, [](const Entry &e, uint64_t k) { return e.key < k; });
        if (it == idx_.end() || it->key != key)
            return std::nullopt;
        auto d = reinterpret_cast<const unsigned char *>(data_.data()) + it->pos;
        uint32_t len;
        std::memcpy(&len, d + 9, 4);
        Record r{d[8], std::string(len, '\0')};
        for (uint32_t k = 0; k < len; k++)
            r.text[k] = char((d[13 + k] - unsigned(MASK[(MASK_START + it->stream + k) % MASK_LEN])) & 0xff);
        return r;
    }

    bool has(uint64_t key) const
    {
        auto it = std::lower_bound(idx_.begin(), idx_.end(), key, [](const Entry &e, uint64_t k) { return e.key < k; });
        return it != idx_.end() && it->key == key;
    }

    std::size_t size() const { return idx_.size(); }
    bool empty() const { return idx_.empty(); }

    void clear()
    {
        std::string().swap(data_);
        std::vector<Entry>().swap(idx_);
    }

private:
    struct Entry {
        uint64_t key;
        uint32_t pos, stream;
    };

    bool fail()
    {
        clear();
        return false;
    }

    std::string data_;
    std::vector<Entry> idx_;
};

}

