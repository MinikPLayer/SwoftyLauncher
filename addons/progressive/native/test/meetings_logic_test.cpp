// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Secret meetings in a follow_driver career (meetings_logic.hpp): the gate, the walk over the
// contracts manager's secretMeetings slots on a fake heap, and the log tally.

#include "meetings_logic.hpp"

#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

using namespace progressive::meetings;

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

constexpr Layout kLayout{0x3a8, 0x3b0, 0x18, 0x40, 0x28, 0x30, 0x00, 0x14, 0x20};

struct Heap {
    std::map<uintptr_t, uint8_t> bytes;

    template <class T> void put(uintptr_t a, T v)
    {
        uint8_t b[sizeof(T)];
        std::memcpy(b, &v, sizeof v);
        for (size_t i = 0; i < sizeof v; ++i)
            bytes[a + i] = b[i];
    }

    template <class T> bool read(uintptr_t a, T &out) const
    {
        uint8_t b[sizeof(T)];
        for (size_t i = 0; i < sizeof(T); ++i) {
            auto it = bytes.find(a + i);
            if (it == bytes.end())
                return false;
            b[i] = it->second;
        }
        std::memcpy(&out, b, sizeof out);
        return true;
    }
};

constexpr uintptr_t kContracts = 0x10000;
constexpr uintptr_t kSlots = 0x20000;

void slots(Heap &h, std::initializer_list<uintptr_t> recs)
{
    h.put<uintptr_t>(kContracts + 0x3a8, kSlots);
    h.put<uintptr_t>(kContracts + 0x3b0, kSlots + kSlot * recs.size());
    uintptr_t p = kSlots;
    for (uintptr_t r : recs) {
        h.put<uintptr_t>(p, 0xa110c);
        h.put<uintptr_t>(p + 8, r);
        p += kSlot;
    }
}

void record(Heap &h, uintptr_t r, uint32_t driver, uintptr_t meeting)
{
    h.put<uintptr_t>(r + 0x18, meeting);
    h.put<uint32_t>(r + 0x40, driver);
}

void meeting(Heap &h, uintptr_t m, uint32_t id, uint8_t reason, uint32_t team)
{
    h.put<uint32_t>(m + 0x00, id);
    h.put<uint8_t>(m + 0x14, reason);
    h.put<uint32_t>(m + 0x20, team);
}

} // namespace

int main()
{
    EXPECT(suppress(true, 1, 1));
    EXPECT(suppress(true, 1, 0));
    EXPECT(!suppress(true, 0, 1));
    EXPECT(!suppress(true, -1, 1));
    EXPECT(!suppress(false, 1, 1));
    EXPECT(!suppress(true, 1, 2));

    {
        Heap h;
        slots(h, {0x30000, 0x31000});
        record(h, 0x30000, 0x000c0010, 0x40000);
        meeting(h, 0x40000, 0, kOffered, 0x00070000);
        record(h, 0x31000, 0x000d0010, 0);
        auto c = current(h, kContracts, kLayout);
        EXPECT(c.size() == 1);
        EXPECT(c.size() == 1 && c[0].record == 0x30000 && c[0].meeting == 0x40000);
        EXPECT(c.size() == 1 && c[0].driver == 0x000c0010 && c[0].id == 0 && c[0].reason == kOffered &&
               c[0].team == 0x00070000);
        EXPECT(c.size() == 1 && describe(c[0]) == "id 0 with team 7 for driver 12 (offered)");
        EXPECT(records(h, kContracts, kLayout).size() == 2);
    }

    {
        Heap h;
        slots(h, {});
        EXPECT(current(h, kContracts, kLayout).empty());
    }

    {
        Heap h;
        EXPECT(current(h, kContracts, kLayout).empty());
        EXPECT(current(h, 0, kLayout).empty());
    }

    {
        Heap h;
        h.put<uintptr_t>(kContracts + 0x3a8, kSlots);
        h.put<uintptr_t>(kContracts + 0x3b0, kSlots + 8);
        EXPECT(records(h, kContracts, kLayout).empty());
        h.put<uintptr_t>(kContracts + 0x3b0, kSlots - kSlot);
        EXPECT(records(h, kContracts, kLayout).empty());
        h.put<uintptr_t>(kContracts + 0x3b0, kSlots + kSlot * (kMaxRecords + 1));
        EXPECT(records(h, kContracts, kLayout).empty());
    }

    {
        Heap h;
        slots(h, {0, 0x30000});
        record(h, 0x30000, 0x000c0010, 0x40000);
        meeting(h, 0x40000, 2, kAccepted, 0x00030000);
        auto c = current(h, kContracts, kLayout);
        EXPECT(c.size() == 1 && c[0].id == 2 && describe(c[0]) == "id 2 with team 3 for driver 12 (accepted)");
    }

    {
        Heap h;
        slots(h, {0x30000});
        record(h, 0x30000, 0x000c0010, 0x40000);
        h.put<uint32_t>(0x40000, 1);
        EXPECT(current(h, kContracts, kLayout).empty());
    }

    {
        Heap h;
        constexpr uintptr_t r = 0x30000, b = 0x50000;
        h.put<uintptr_t>(r + 0x28, b);
        h.put<uintptr_t>(r + 0x30, b + 2 * kSlot);
        h.put<uintptr_t>(b, 0xa110c);
        h.put<uintptr_t>(b + 8, 0x41000);
        h.put<uintptr_t>(b + kSlot, 0xa110c);
        h.put<uintptr_t>(b + kSlot + 8, 0);
        meeting(h, 0x41000, 1, kDeclined, 0x00090000);
        uintptr_t begin = 0;
        std::vector<Past> past;
        EXPECT(previous(h, r, kLayout, begin, past));
        EXPECT(begin == b && past.size() == 2);
        EXPECT(past.size() == 2 && past[0].slot == b && past[0].allocator == 0xa110c && past[0].meeting == 0x41000 &&
               past[0].id == 1 && past[0].reason == kDeclined && past[0].team == 0x00090000);
        EXPECT(past.size() == 2 && past[1].slot == b + kSlot && past[1].meeting == 0);

        h.put<uintptr_t>(r + 0x30, b);
        EXPECT(previous(h, r, kLayout, begin, past) && past.empty() && begin == b);
        h.put<uintptr_t>(r + 0x30, b + 8);
        EXPECT(!previous(h, r, kLayout, begin, past));
        h.put<uintptr_t>(r + 0x30, b - kSlot);
        EXPECT(!previous(h, r, kLayout, begin, past));
        h.put<uintptr_t>(r + 0x30, b + 3 * kSlot);
        EXPECT(!previous(h, r, kLayout, begin, past));
        EXPECT(!previous(h, 0, kLayout, begin, past));
    }

    EXPECT(std::string(reason_name(kDeclined)) == "declined");
    EXPECT(std::string(reason_name(9)) == "unknown reason");
    EXPECT(describe(1, 0x00050000, 0x00200010) == "id 1 with team 5 for driver 32");

    {
        Tally t;
        int logged = 0;
        for (int i = 0; i < kLogFirst + 3; ++i)
            logged += t.note() ? 1 : 0;
        EXPECT(logged == kLogFirst);
        EXPECT(t.take() == kLogFirst + 3);
        EXPECT(t.take() == 0);
        EXPECT(t.total == kLogFirst + 3);
        t.note();
        EXPECT(t.take() == 1);
        t.reset();
        EXPECT(t.total == 0 && t.note());
    }

    if (failures) {
        std::printf("meetings_logic_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("meetings_logic_test: ok\n");
    return 0;
}
