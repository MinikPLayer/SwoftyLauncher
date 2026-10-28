// SPDX-License-Identifier: AGPL-3.0-or-later

#include "fuel_logic.hpp"

#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

using namespace progressive::fuel;

namespace {

int failures = 0;

void expect(bool ok, const char *what, int line)
{
    if (!ok) {
        std::printf("FAIL line %d: %s\n", line, what);
        ++failures;
    }
}

#define EXPECT(x) expect((x), #x, __LINE__)

// A flat fake of the game's memory: one buffer, addresses from kBase.
struct Fake {
    static constexpr uintptr_t kBase = 0x10000;
    std::vector<uint8_t> mem = std::vector<uint8_t>(0x40000);
    uintptr_t next = kBase + 0x100;

    uintptr_t alloc(size_t n)
    {
        uintptr_t at = next;
        next += (n + 15) & ~size_t(15);
        return at;
    }
    uint8_t *at(uintptr_t a) { return mem.data() + (a - kBase); }
    template <class T> void put(uintptr_t a, T v) { std::memcpy(at(a), &v, sizeof v); }
    template <class T> T get(uintptr_t a)
    {
        T v;
        std::memcpy(&v, at(a), sizeof v);
        return v;
    }
    bool read(uintptr_t a, void *out, size_t n)
    {
        if (a < kBase || a + n > kBase + mem.size())
            return false;
        std::memcpy(out, at(a), n);
        return true;
    }
    bool c_string(uintptr_t a, std::string &out)
    {
        out.clear();
        if (a < kBase || a >= kBase + mem.size())
            return false;
        for (const char *p = reinterpret_cast<const char *>(at(a)); *p; ++p)
            out.push_back(*p);
        return !out.empty();
    }
};

struct Values {
    float burn, p25, p100;
    uint64_t rows;
};

uintptr_t record(Fake &f, uint8_t index, const char *name)
{
    uintptr_t r = f.alloc(0x100), s = f.alloc(32);
    std::strcpy(reinterpret_cast<char *>(f.at(s)), name);
    f.put<uint8_t>(r + kRecordIndex, index);
    f.put<uint64_t>(r + kRecordName, s);
    return r;
}

uintptr_t track(Fake &f, uintptr_t rec, Values v)
{
    uintptr_t o = f.alloc(0x290);
    f.put<uint64_t>(o + kTrackRecord, rec);
    f.put<float>(o + kBurn, v.burn);
    f.put<uint64_t>(o + kRowCount, v.rows);
    for (uint64_t r = 0; r < v.rows; ++r) {
        uintptr_t row = o + kRows + r * kRowSize;
        f.put<float>(row + kRowPercent100, v.p100);
        f.put<float>(row + kRowPercent25, v.p25);
        f.put<float>(row + kRowPercent100Again, v.p100);
        f.put<float>(row + 0x04, 4.9f + r);
    }
    f.put<float>(o + kQualOneShot, v.burn * 3.5f);
    f.put<float>(o + kQualShort, v.burn * 3.5f);
    f.put<float>(o + kQualFull, v.burn * 3.5f);
    return o;
}

struct SetSpec {
    uint32_t modes, seasons, formats;
    std::vector<std::pair<uint32_t, uintptr_t>> tracks;
};

uintptr_t system(Fake &f, const std::vector<SetSpec> &specs)
{
    uintptr_t sys = f.alloc(0x230), sets = f.alloc(kSetSize * specs.size());
    f.put<uint64_t>(sys + kSetsBegin, sets);
    f.put<uint64_t>(sys + kSetsEnd, sets + kSetSize * specs.size());
    for (size_t i = 0; i < specs.size(); ++i) {
        uintptr_t s = sets + i * kSetSize;
        uint32_t n = 0;
        for (auto &t : specs[i].tracks)
            n = std::max(n, t.first + 1);
        uintptr_t items = f.alloc(kItemSize * n);
        for (auto &t : specs[i].tracks)
            f.put<uint64_t>(items + t.first * kItemSize + kItemTrack, t.second);
        f.put<uint64_t>(s + kSetTracksBegin, items);
        f.put<uint64_t>(s + kSetTracksEnd, items + kItemSize * n);
        f.put<uint32_t>(s + kSetModes, specs[i].modes);
        f.put<uint32_t>(s + kSetSeasons, specs[i].seasons);
        f.put<uint32_t>(s + kSetFormats, specs[i].formats);
    }
    return sys;
}

constexpr uint32_t bit(uint32_t b)
{
    return 1u << b;
}

struct World {
    Fake f;
    uintptr_t sys = 0;
    uintptr_t mel26 = 0, sil26 = 0, imo26 = 0, mel26s = 0, mel25 = 0, stray26 = 0;
};

void build(World &w, bool stock = true)
{
    Fake &f = w.f;
    uintptr_t mel = record(f, 0, "melbourne"), sil = record(f, 1, "silverstone"), imo = record(f, 2, "imola");
    uintptr_t other = record(f, 3, "sochi");
    w.mel25 = track(f, mel, {1.74f, 30.6f, 110.0f, 1});
    uintptr_t sil25 = track(f, sil, {1.88f, 29.2f, 110.0f, 1}), imo25 = track(f, imo, {1.61f, 30.0f, 110.0f, 1});
    uintptr_t sochi25 = track(f, other, {1.92f, 30.5f, 109.8f, 1});
    uintptr_t mel25s = track(f, mel, {1.74f, 13.2f, 37.6f, 2}), sil25s = track(f, sil, {1.88f, 12.3f, 36.8f, 2});
    w.mel26 = track(f, mel, {stock ? 1.30f : 1.10f, 23.1f, 81.4f, 1});
    w.sil26 = track(f, sil, {1.31f, 20.7f, 74.0f, 1});
    w.imo26 = track(f, imo, {1.61f, 30.0f, 110.0f, 1});
    w.stray26 = track(f, record(f, 3, "sochi"), {1.20f, 22.0f, 80.0f, 1});
    w.mel26s = track(f, mel, {1.30f, 10.1f, 28.3f, 2});
    uintptr_t sil26s = track(f, sil, {1.31f, 8.9f, 25.9f, 2});
    // Set order as the game builds them: a classic set first (no f1 bit), then f1_2025, sprint, f1_2026, sprint.
    w.sys = system(f, {
                          {bit(1), bit(5) | bit(6) | bit(7), bit(0) | bit(1), {{0, w.mel25}}},
                          {bit(0) | bit(3) | bit(8), bit(7), bit(0),
                           {{0, w.mel25}, {1, sil25}, {2, imo25}, {3, sochi25}}},
                          {bit(0) | bit(3), bit(7), bit(1), {{0, mel25s}, {1, sil25s}}},
                          {bit(13) | bit(14) | bit(15) | bit(3), bit(8), bit(0),
                           {{0, w.mel26}, {1, w.sil26}, {2, w.imo26}, {3, w.stray26}}},
                          {bit(13) | bit(3), bit(8), bit(1), {{0, w.mel26s}, {1, sil26s}}},
                          {bit(13), bit(8), bit(0), {{0, w.mel25}}},
                      });
}

void test_read_and_pick()
{
    World w;
    build(w);
    std::vector<Set> sets;
    std::string why;
    EXPECT(read_sets(w.f, w.sys, sets, why));
    EXPECT(sets.size() == 6);
    const Set *src = pick(sets, kSource), *dst = pick(sets, kTarget);
    const Set *ss = pick(sets, kSprintSource), *sd = pick(sets, kSprintTarget);
    EXPECT(src == &sets[1]);
    EXPECT(dst == &sets[3]);
    EXPECT(ss == &sets[2]);
    EXPECT(sd == &sets[4]);
    EXPECT(check(*src, kSource, why));
    EXPECT(check(*dst, kTarget, why));
    EXPECT(check(*ss, kSprintSource, why));
    EXPECT(check(*sd, kSprintTarget, why));
    EXPECT(!check(*src, kTarget, why));
    EXPECT(why.find("f1_2026 melbourne reads 1.74 kg/lap") != std::string::npos);
    EXPECT(find(*dst, "silverstone")->fuel.percent100() == 74.0f);
}

void test_validation_refuses_changed_values()
{
    World w;
    build(w, false);
    std::vector<Set> sets;
    std::string why;
    EXPECT(read_sets(w.f, w.sys, sets, why));
    EXPECT(!check(*pick(sets, kTarget), kTarget, why));
    EXPECT(why.find("melbourne") != std::string::npos);
    World w2;
    build(w2);
    std::vector<Set> stock;
    EXPECT(read_sets(w2.f, w2.sys, stock, why));
    Set none = *pick(stock, kTarget);
    std::erase_if(none.tracks, [](const Track &t) { return t.name == "silverstone"; });
    EXPECT(!check(none, kTarget, why));
    EXPECT(why.find("has no silverstone") != std::string::npos);
}

void test_source_fallback()
{
    World w;
    build(w);
    std::vector<Set> sets;
    std::string why;
    EXPECT(read_sets(w.f, w.sys, sets, why));
    EXPECT(pick_source(sets, kSource, why) == &sets[1]);
    // The season-2025 set without fuel of its own: the copy takes the first set with the stock 2025 values.
    sets[1].tracks[0].fuel.burn = 0;
    Set root = sets[1];
    root.tracks[0].fuel.burn = 1.74f;
    root.modes = root.seasons = root.formats = 0;
    sets.push_back(root);
    EXPECT(pick_source(sets, kSource, why) == &sets.back());
    sets.pop_back();
    EXPECT(pick_source(sets, kSource, why) == nullptr);
    EXPECT(why.find("f1 (season 2025) melbourne reads 0.00") != std::string::npos);
}

void test_layout_refusals()
{
    World w;
    build(w);
    std::vector<Set> sets;
    std::string why;
    w.f.put<uint64_t>(w.sys + kSetsEnd, w.f.get<uint64_t>(w.sys + kSetsBegin) + 0x20);
    EXPECT(!read_sets(w.f, w.sys, sets, why));
    EXPECT(why.find("0x30-byte") != std::string::npos);

    World w2;
    build(w2);
    w2.f.put<uint8_t>(w2.f.get<uint64_t>(w2.mel26 + kTrackRecord) + kRecordIndex, 7);
    sets.clear();
    EXPECT(!read_sets(w2.f, w2.sys, sets, why));
    EXPECT(why.find("record index 7") != std::string::npos);
    EXPECT(!read_sets(w2.f, 0x5, sets, why));
}

void test_plan_and_merge()
{
    World w;
    build(w);
    std::vector<Set> sets;
    std::string why;
    EXPECT(read_sets(w.f, w.sys, sets, why));
    size_t skipped = 0;
    auto pairs = plan(*pick(sets, kTarget), *pick(sets, kSource), skipped);
    // melbourne, silverstone, imola match; sochi's slot holds an object of another sochi record.
    EXPECT(pairs.size() == 3);
    EXPECT(skipped == 1);
    EXPECT(pairs[0].dst->object == w.mel26 && pairs[0].src->object == w.mel25);
    Fuel m = merged(pairs[0].dst->fuel, pairs[0].src->fuel);
    EXPECT(m.burn == 1.74f && m.percent25() == 30.6f && m.percent100() == 110.0f && m.count == 1);
    EXPECT(m.full_quali == pairs[0].src->fuel.full_quali);
    EXPECT(same(merged(pairs[2].dst->fuel, pairs[2].src->fuel), pairs[2].dst->fuel));

    auto sprint = plan(*pick(sets, kSprintTarget), *pick(sets, kSprintSource), skipped);
    EXPECT(sprint.size() == 2 && skipped == 0);
    Fuel s = merged(sprint[0].dst->fuel, sprint[0].src->fuel);
    EXPECT(s.count == 2 && s.row_float(1, kRowPercent25) == 13.2f && s.percent100() == 37.6f);

    // A source with another row count is skipped.
    Set src = *pick(sets, kSource);
    src.tracks[0].fuel.count = 2;
    plan(*pick(sets, kTarget), src, skipped);
    EXPECT(skipped == 2);
}

void test_restore_bookkeeping()
{
    World w;
    build(w);
    std::vector<Set> sets;
    std::string why;
    EXPECT(read_sets(w.f, w.sys, sets, why));
    const Track *t = find(*pick(sets, kTarget), "melbourne");
    Saved s{t->object, t->name, t->fuel, merged(t->fuel, find(*pick(sets, kSource), "melbourne")->fuel)};
    EXPECT(undo_action(s, true, s.after) == Undo::restore);
    EXPECT(undo_action(s, true, s.before) == Undo::changed);
    EXPECT(undo_action(s, false, s.after) == Undo::unreadable);
    Fuel tweaked = s.after;
    tweaked.full_quali += 1;
    EXPECT(undo_action(s, true, tweaked) == Undo::changed);

    Fuel now;
    EXPECT(read_fuel(w.f, t->object, now) && same(now, s.before));
}

} // namespace

int main()
{
    test_read_and_pick();
    test_validation_refuses_changed_values();
    test_source_fallback();
    test_layout_refusals();
    test_plan_and_merge();
    test_restore_bookkeeping();
    if (failures) {
        std::printf("fuel_logic_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("fuel_logic_test: ok\n");
    return 0;
}
