// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career: secret meetings in a follow_driver career (progressive_follow.dll). Pure logic,
// built on the host by native/test.
//
//   suppress  a Progressive career whose team moves follow the driver, with one human, gets no secret
//             meetings: Game_DriverContracts_OfferSecretMeeting does nothing for it.
//   current   the current meeting of every driver record in the contracts manager's secretMeetings
//             slots (16 bytes {allocator, record}), read through a memory reader, with the layout
//             offsets from the KB headers.
//   previous  a record's previous meetings (16-byte slots {allocator, meeting}), which the season
//             change frees; a follow career frees them at Team HQ the same way.
//   Tally     what the offer wrap suppressed: the first few are logged one by one, every Team HQ logs
//             the count since the last one.

#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace progressive::meetings {

constexpr uint8_t kOffered = 3;
constexpr uint8_t kAccepted = 1;
constexpr uint8_t kDeclined = 0;
constexpr size_t kSlot = 16;
constexpr size_t kMaxRecords = 64;
constexpr size_t kMaxPrevious = 256;
constexpr int kLogFirst = 5;

struct Layout {
    size_t list_begin, list_end;
    size_t record_current, record_driver, record_previous_begin, record_previous_end;
    size_t meeting_id, meeting_reason, meeting_team;
};

struct Past {
    uintptr_t slot = 0;
    uintptr_t allocator = 0;
    uintptr_t meeting = 0;
    uint32_t id = 0;
    uint8_t reason = 0;
    uint32_t team = 0;
};

struct Current {
    uintptr_t record = 0;
    uintptr_t meeting = 0;
    uint32_t driver = 0;
    uint32_t id = 0;
    uint8_t reason = 0;
    uint32_t team = 0;
};

inline bool suppress(bool progressive, int follow_mode, uint32_t humans)
{
    return progressive && follow_mode == 1 && humans <= 1;
}

inline const char *reason_name(uint8_t r)
{
    switch (r) {
    case kOffered:
        return "offered";
    case kAccepted:
        return "accepted";
    case kDeclined:
        return "declined";
    default:
        return "unknown reason";
    }
}

inline std::string describe(uint32_t id, uint32_t team, uint32_t driver)
{
    char b[96];
    std::snprintf(b, sizeof b, "id %u with team %u for driver %u", id, team >> 16, driver >> 16);
    return b;
}

inline std::string describe(const Current &c)
{
    return describe(c.id, c.team, c.driver) + " (" + reason_name(c.reason) + ")";
}

template <class Mem> std::vector<uintptr_t> records(const Mem &m, uintptr_t contracts, const Layout &l)
{
    std::vector<uintptr_t> out;
    uintptr_t b = 0, e = 0;
    if (!contracts || !m.read(contracts + l.list_begin, b) || !m.read(contracts + l.list_end, e))
        return out;
    if (!b || e < b || (e - b) % kSlot || (e - b) / kSlot > kMaxRecords)
        return out;
    for (uintptr_t p = b; p < e; p += kSlot) {
        uintptr_t r = 0;
        if (m.read(p + 8, r) && r)
            out.push_back(r);
    }
    return out;
}

template <class Mem> std::vector<Current> current(const Mem &m, uintptr_t contracts, const Layout &l)
{
    std::vector<Current> out;
    for (uintptr_t r : records(m, contracts, l)) {
        Current c;
        c.record = r;
        if (!m.read(r + l.record_current, c.meeting) || !c.meeting || !m.read(r + l.record_driver, c.driver))
            continue;
        if (!m.read(c.meeting + l.meeting_id, c.id) || !m.read(c.meeting + l.meeting_reason, c.reason) ||
            !m.read(c.meeting + l.meeting_team, c.team))
            continue;
        out.push_back(c);
    }
    return out;
}

template <class Mem> bool previous(const Mem &m, uintptr_t record, const Layout &l, uintptr_t &begin,
                                   std::vector<Past> &out)
{
    out.clear();
    uintptr_t b = 0, e = 0;
    if (!record || !m.read(record + l.record_previous_begin, b) || !m.read(record + l.record_previous_end, e))
        return false;
    if (b == e) {
        begin = b;
        return true;
    }
    if (!b || e < b || (e - b) % kSlot || (e - b) / kSlot > kMaxPrevious)
        return false;
    for (uintptr_t p = b; p < e; p += kSlot) {
        Past x;
        x.slot = p;
        if (!m.read(p, x.allocator) || !m.read(p + 8, x.meeting))
            return false;
        if (x.meeting && (!m.read(x.meeting + l.meeting_id, x.id) || !m.read(x.meeting + l.meeting_reason, x.reason) ||
                          !m.read(x.meeting + l.meeting_team, x.team)))
            return false;
        out.push_back(x);
    }
    begin = b;
    return true;
}

struct Tally {
    std::atomic<int> logged{0};
    std::atomic<int> since_hub{0};
    std::atomic<int> total{0};

    bool note()
    {
        since_hub.fetch_add(1);
        total.fetch_add(1);
        return logged.fetch_add(1) < kLogFirst;
    }

    int take()
    {
        return since_hub.exchange(0);
    }

    void reset()
    {
        logged = 0;
        since_hub = 0;
        total = 0;
    }
};

}
