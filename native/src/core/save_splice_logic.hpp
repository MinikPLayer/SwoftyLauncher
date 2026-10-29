#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Pure logic of the save splice (save_splice.cpp): the store of kept root containers, the count the
// splice owes the save it bumped, and the byte layout of one kept entry. No game calls, so the unit
// test runs on the host.

namespace runtime::save_splice {

constexpr size_t NAME_LIMIT = 63;
// W4: the career snapshots' own root container (native/src/core/snapshots/format.hpp), and the bag
// type id its body's layout assumes: type ids are four characters in memory order ('pbag' for a bag,
// 'dstr' for the length-prefixed string it carries the data in), read from the game at arm time.
constexpr const char *ENTRY_NAME = "swofty_registry";
constexpr uint32_t ENTRY_BAG_TYPE = 0x67616270u;

// W4: what the career snapshots plug into the splice (save_splice.cpp): the body kept at a root
// load (null when the save has none), a new body to write at a profile save (false: write the kept
// one, or none), and whether the save wrote it.
struct EntryHooks {
    void (*loaded)(const std::vector<uint8_t> *body) = nullptr;
    bool (*saving)(std::vector<uint8_t> &body) = nullptr;
    void (*written)(bool ok) = nullptr;
};
void set_entry_hooks(const EntryHooks &h);
// Empty when the splice can write the entry; otherwise why it can't (nothing is written then).
std::string entry_blocked();
constexpr size_t MAX_KEPT = 32;
constexpr uint64_t MAX_BODY = 64ull << 20;
constexpr uint64_t MAX_TOTAL = 128ull << 20;

constexpr uint64_t fnv1a64(const char *s)
{
    uint64_t h = 0xcbf29ce484222325ull;
    while (*s)
        h = (h ^ uint8_t(*s++)) * 0x100000001b3ull;
    return h;
}

// Game_SaveData_WriteBag copies a key into a 0x40-byte buffer and the string writer sends its length
// as one byte, so a name the game wrote back is 1 to 63 printable characters.
inline bool name_ok(const std::string &name)
{
    if (name.empty() || name.size() > NAME_LIMIT)
        return false;
    for (unsigned char ch : name)
        if (ch < 0x20 || ch > 0x7e)
            return false;
    return true;
}

struct Kept {
    std::string name;
    std::vector<uint8_t> body;
};

enum class Add { kept, replaced, bad_name, empty, too_big, full };

// The root containers no loaded mode claimed at the last profile load, by name.
class Store {
  public:
    void begin_load()
    {
        kept_.clear();
        total_ = 0;
    }

    Add add(const std::string &name, std::vector<uint8_t> body)
    {
        if (!name_ok(name))
            return Add::bad_name;
        if (body.empty())
            return Add::empty;
        if (body.size() > MAX_BODY)
            return Add::too_big;
        for (auto &k : kept_)
            if (k.name == name) {
                if (total_ - k.body.size() + body.size() > MAX_TOTAL)
                    return Add::too_big;
                total_ = total_ - k.body.size() + body.size();
                k.body = std::move(body);
                return Add::replaced;
            }
        if (kept_.size() >= MAX_KEPT)
            return Add::full;
        if (total_ + body.size() > MAX_TOTAL)
            return Add::too_big;
        total_ += body.size();
        kept_.push_back({name, std::move(body)});
        return Add::kept;
    }

    const std::vector<Kept> &kept() const { return kept_; }
    size_t size() const { return kept_.size(); }
    uint64_t total() const { return total_; }

  private:
    std::vector<Kept> kept_;
    uint64_t total_ = 0;
};

// The kept containers a save may write: those whose name no registered child of the root has, since
// a bag with two children of one name fails to load (bag add-by-name 0x143932a40 refuses a duplicate).
inline std::vector<size_t> pick(const std::vector<Kept> &kept, const std::vector<std::string> &registered)
{
    std::vector<size_t> out;
    for (size_t i = 0; i < kept.size(); i++) {
        bool taken = false;
        for (auto &r : registered)
            taken |= r == kept[i].name;
        if (!taken)
            out.push_back(i);
    }
    return out;
}

// The save the count site bumped: the children site writes exactly those entries into the same save
// (the same result pointer), or none when the count site did not bump this save.
class Owed {
  public:
    uint64_t bump(const void *result, uint64_t count, std::vector<size_t> picked)
    {
        result_ = result;
        picked_ = std::move(picked);
        return count + picked_.size();
    }

    std::vector<size_t> take(const void *result)
    {
        std::vector<size_t> out;
        if (result == result_)
            out.swap(picked_);
        result_ = nullptr;
        picked_.clear();
        return out;
    }

    bool pending() const { return !picked_.empty(); }

  private:
    const void *result_ = nullptr;
    std::vector<size_t> picked_;
};

// One entry of the root's containers block as the reader expects it: the name as a length byte and
// its characters (the stream's vt+0x50 and vt+0x48), the bag type id (u32), then the bag body.
inline std::vector<uint8_t> entry_bytes(const std::string &name, uint32_t bag_type, const std::vector<uint8_t> &body)
{
    std::vector<uint8_t> out;
    out.push_back(uint8_t(name.size()));
    out.insert(out.end(), name.begin(), name.end());
    for (int i = 0; i < 4; i++)
        out.push_back(uint8_t(bag_type >> (8 * i)));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

}
