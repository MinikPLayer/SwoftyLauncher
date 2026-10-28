// Host unit test for the save splice (save_splice_logic.hpp). A small model of the game's bag stream
// format (re/db/subsystems/game.md#save-splice: header 1, entry count, then key length byte, key,
// u32 type id and value per entry) stands in for Game_SaveData_WriteBag and the reader 0x143934620.
// It checks that a profile save's containers block with the kept entries spliced in after a bumped
// count reads back as the registered children plus the kept bags, unchanged. run_events_test.sh
// builds and runs it.
#include "../src/core/save_splice_logic.hpp"

#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sp = runtime::save_splice;

static int fails;

static void expect(bool ok, const std::string &what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
    fails += !ok;
}

constexpr uint32_t BAG = 0x6b1c3f01;
constexpr uint32_t BLOB = 0x2d77a145;

struct Bag {
    std::vector<std::pair<std::string, std::unique_ptr<Bag>>> bags;
    std::vector<std::pair<std::string, std::vector<uint8_t>>> blobs;

    bool operator==(const Bag &o) const
    {
        if (blobs != o.blobs || bags.size() != o.bags.size())
            return false;
        for (size_t i = 0; i < bags.size(); i++)
            if (bags[i].first != o.bags[i].first || !(*bags[i].second == *o.bags[i].second))
                return false;
        return true;
    }
};

struct Out {
    std::vector<uint8_t> bytes;
    void u8(uint8_t v) { bytes.push_back(v); }
    void raw(const void *p, size_t n)
    {
        auto *b = static_cast<const uint8_t *>(p);
        bytes.insert(bytes.end(), b, b + n);
    }
    void u32(uint32_t v) { raw(&v, 4); }
    void u64(uint64_t v) { raw(&v, 8); }
    void key(const std::string &k)
    {
        u8(uint8_t(k.size()));
        raw(k.data(), k.size());
    }
};

static void write_bag(Out &o, const Bag &b)
{
    o.u64(1);
    o.u64(b.bags.size() + b.blobs.size());
    for (auto &[k, v] : b.blobs) {
        o.key(k);
        o.u32(BLOB);
        o.u32(uint32_t(v.size()));
        o.raw(v.data(), v.size());
    }
    for (auto &[k, v] : b.bags) {
        o.key(k);
        o.u32(BAG);
        write_bag(o, *v);
    }
}

struct In {
    const std::vector<uint8_t> &b;
    size_t at = 0;
    bool ok = true;
    bool need(size_t n)
    {
        ok = ok && at + n <= b.size();
        return ok;
    }
    uint64_t u64()
    {
        uint64_t v = 0;
        if (need(8))
            memcpy(&v, &b[at], 8), at += 8;
        return v;
    }
    uint32_t u32()
    {
        uint32_t v = 0;
        if (need(4))
            memcpy(&v, &b[at], 4), at += 4;
        return v;
    }
    std::string key()
    {
        if (!need(1))
            return {};
        size_t n = b[at++];
        if (!need(n))
            return {};
        std::string s(reinterpret_cast<const char *>(&b[at]), n);
        at += n;
        return s;
    }
};

static bool read_bag(In &in, Bag &out)
{
    if (in.u64() != 1)
        return false;
    uint64_t n = in.u64();
    for (uint64_t i = 0; i < n && in.ok; i++) {
        std::string k = in.key();
        uint32_t type = in.u32();
        if (type == BAG) {
            auto child = std::make_unique<Bag>();
            if (!read_bag(in, *child))
                return false;
            out.bags.emplace_back(k, std::move(child));
        } else if (type == BLOB) {
            uint32_t len = in.u32();
            if (!in.need(len))
                return false;
            out.blobs.emplace_back(k, std::vector<uint8_t>(in.b.begin() + in.at, in.b.begin() + in.at + len));
            in.at += len;
        } else {
            return false;
        }
    }
    return in.ok;
}

static std::unique_ptr<Bag> career(const std::string &tag, int slots)
{
    auto mode = std::make_unique<Bag>();
    mode->blobs.emplace_back("uid", std::vector<uint8_t>{1, 2, 3, 4});
    auto mgr = std::make_unique<Bag>();
    for (int i = 0; i < slots; i++)
        mgr->blobs.emplace_back("data", std::vector<uint8_t>(300 + i, uint8_t(tag[0] + i)));
    mode->bags.emplace_back("SaveSlotGameModeComponent", std::move(mgr));
    return mode;
}

// The inline containers block of the root: key, bag type, header, count, then the children. The
// splice bumps the count before it is written and writes the kept entries right after it.
static std::vector<uint8_t> containers_block(const std::vector<std::pair<std::string, const Bag *>> &registered,
                                             const sp::Store &kept, sp::Owed &owed, const void *result,
                                             bool splice)
{
    Out o;
    o.key("containers");
    o.u32(BAG);
    o.u64(1);
    uint64_t count = registered.size();
    if (splice && kept.size()) {
        std::vector<std::string> names;
        for (auto &[name, bag] : registered)
            names.push_back(name);
        count = owed.bump(result, count, sp::pick(kept.kept(), names));
    }
    o.u64(count);
    for (size_t i : owed.take(result)) {
        auto e = sp::entry_bytes(kept.kept()[i].name, BAG, kept.kept()[i].body);
        o.raw(e.data(), e.size());
    }
    for (auto &[name, bag] : registered) {
        o.key(name);
        o.u32(BAG);
        write_bag(o, *bag);
    }
    return o.bytes;
}

static bool read_block(const std::vector<uint8_t> &bytes, Bag &out)
{
    In in{bytes};
    if (in.key() != "containers" || in.u32() != BAG)
        return false;
    return read_bag(in, out) && in.at == bytes.size();
}

int main()
{
    auto stock26 = career("s", 3);
    auto stock24 = career("t", 1);
    auto progressive = career("p", 2);
    auto f1990 = career("n", 1);

    sp::Store store;
    store.begin_load();
    Out p, n;
    write_bag(p, *progressive);
    write_bag(n, *f1990);
    expect(store.add("driver_career_progressive", p.bytes) == sp::Add::kept, "a kept container is stored");
    expect(store.add("driver_career_1990", n.bytes) == sp::Add::kept, "a second one is stored");
    expect(store.size() == 2 && store.total() == p.bytes.size() + n.bytes.size(), "the store counts both");

    sp::Owed owed;
    int result = 0;
    auto block = containers_block({{"driver_career_26", stock26.get()}, {"driver_career_24", stock24.get()}}, store,
                                  owed, &result, true);
    Bag got;
    expect(read_block(block, got), "the spliced block reads back to its end");
    expect(got.bags.size() == 4, "it holds the two registered and the two kept containers");
    std::map<std::string, const Bag *> by;
    for (auto &[k, v] : got.bags)
        by[k] = v.get();
    expect(by.count("driver_career_progressive") && *by["driver_career_progressive"] == *progressive,
           "the kept Progressive container reads back unchanged");
    expect(by.count("driver_career_1990") && *by["driver_career_1990"] == *f1990,
           "the kept 1990 container reads back unchanged");
    expect(by.count("driver_career_26") && *by["driver_career_26"] == *stock26,
           "a registered container is unchanged");
    expect(!owed.pending(), "the owed count was paid");

    auto plain = containers_block({{"driver_career_26", stock26.get()}}, store, owed, &result, false);
    Bag stock;
    expect(read_block(plain, stock) && stock.bags.size() == 1, "without the splice the block is the stock one");

    int other = 0;
    owed.bump(&result, 1, {0, 1});
    expect(owed.take(&other).empty(), "a different save never pays another save's owed count");
    expect(!owed.pending(), "the mismatch clears the owed count");

    auto late = career("l", 1);
    auto dup = containers_block({{"driver_career_26", stock26.get()}, {"driver_career_1990", late.get()}}, store, owed,
                                &result, true);
    Bag got_dup;
    expect(read_block(dup, got_dup), "a kept name that a registered child also has still reads");
    size_t n1990 = 0;
    for (auto &[k, v] : got_dup.bags)
        n1990 += k == "driver_career_1990";
    expect(n1990 == 1 && got_dup.bags.size() == 3, "the registered child wins and the kept copy is left out");
    expect(sp::pick(store.kept(), {"driver_career_progressive", "driver_career_1990"}).empty(),
           "nothing is picked when every kept name is registered");

    Out broken;
    broken.key("containers");
    broken.u32(BAG);
    broken.u64(1);
    broken.u64(2);
    broken.key("driver_career_26");
    broken.u32(BAG);
    write_bag(broken, *stock26);
    Bag none;
    expect(!read_block(broken.bytes, none), "a bumped count without its entries does not read: the splice must pay");

    expect(store.add("driver_career_progressive", n.bytes) == sp::Add::replaced, "a name kept twice keeps the last");
    expect(store.size() == 2, "and stays one entry");
    expect(store.add("", p.bytes) == sp::Add::bad_name, "an empty name is refused");
    expect(store.add(std::string(64, 'x'), p.bytes) == sp::Add::bad_name, "a name over 63 characters is refused");
    expect(store.add("tab\tname", p.bytes) == sp::Add::bad_name, "a name with a control character is refused");
    expect(store.add("x", {}) == sp::Add::empty, "an empty body is refused");
    store.begin_load();
    expect(store.size() == 0 && store.total() == 0, "a new load starts empty");
    for (size_t i = 0; i < sp::MAX_KEPT; i++)
        store.add("c" + std::to_string(i), {1});
    expect(store.add("one_more", {1}) == sp::Add::full, "the store holds at most MAX_KEPT containers");

    expect(sp::fnv1a64("") == 0xcbf29ce484222325ull, "fnv1a64 of the empty string is the offset basis");
    expect(sp::fnv1a64("driver_career_progressive") == sp::fnv1a64("driver_career_progressive"), "fnv1a64 is stable");

    auto e = sp::entry_bytes("ab", 0x01020304, {9, 8});
    expect(e == std::vector<uint8_t>{2, 'a', 'b', 4, 3, 2, 1, 9, 8}, "an entry is length, name, type id, body");

    printf("%s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
