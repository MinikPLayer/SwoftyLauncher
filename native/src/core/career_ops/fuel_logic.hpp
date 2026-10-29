// tracks::set_track_fuel's layout and rules, moved from Progressive Career's race weekend fuel (addons/progressive/
// native/src/fuel_logic.hpp, progressive_fuel.dll, proven in 1.2.2). The game keeps every track data set in
// memory in the TRACK_DATA system (KB struct TrackDataSystem, read by 0x142e44b50, queried by
// Game_TrackData_GetTrack 0x142e60e00): sets at +0x68..+0x70 (0x30 bytes each; mode, season and weekend format
// bit masks at +0x20/+0x24/+0x28, a track's object by track index in the 16-byte items at +0x08..+0x10), the
// CareerEntity's +0xf58 points at it. The getter returns the first set whose masks hold the query's bits. A
// track object (KB struct TrackDataTrack, 0x290 bytes) holds fuel_burn_per_lap +0xf0, the RaceFuelSettings
// rows (0x34 bytes, count at +0x1a0) from +0x100 and the qualifying fuel at +0x1b4/+0x1bc/+0x1c0. A data set
// is used only when Melbourne and Silverstone show its stock values, which proves the layout before anything
// is written. Pure logic over a memory reader, so native/test builds it on the host.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace career_ops::fuel {

constexpr uintptr_t kEntityTrackData = 0xf58;
constexpr uintptr_t kSetsBegin = 0x68;
constexpr uintptr_t kSetsEnd = 0x70;
constexpr size_t kSetSize = 0x30;
constexpr uintptr_t kSetTracksBegin = 0x08;
constexpr uintptr_t kSetTracksEnd = 0x10;
constexpr uintptr_t kSetModes = 0x20;
constexpr uintptr_t kSetSeasons = 0x24;
constexpr uintptr_t kSetFormats = 0x28;
constexpr size_t kItemSize = 0x10;
constexpr uintptr_t kItemTrack = 0x08;
constexpr uintptr_t kTrackRecord = 0x00;
constexpr uintptr_t kRecordIndex = 0x01;
constexpr uintptr_t kRecordName = 0xd0;
constexpr uintptr_t kBurn = 0xf0;
constexpr uintptr_t kRows = 0x100;
constexpr size_t kRowSize = 0x34;
constexpr size_t kMaxRows = 3;
constexpr uintptr_t kRowCount = 0x1a0;
constexpr uintptr_t kQualOneShot = 0x1b4;
constexpr uintptr_t kQualShort = 0x1bc;
constexpr uintptr_t kQualFull = 0x1c0;
constexpr uintptr_t kRowPercent100 = 0x00;
constexpr uintptr_t kRowPercent25 = 0x10;
constexpr uintptr_t kRowPercent100Again = 0x1c;

constexpr size_t kMaxSets = 64;
constexpr size_t kMaxTracks = 256;

constexpr uint32_t kModeF1 = 0;
constexpr uint32_t kModeF1_26 = 13;
constexpr uint32_t kSeason2025 = 7;
constexpr uint32_t kSeason2026 = 8;
constexpr uint32_t kFormatDefault = 0;
constexpr uint32_t kFormatSprint = 1;

struct Fuel {
    float burn = 0;
    uint64_t count = 0;
    uint8_t rows[kRowSize * kMaxRows] = {};
    float one_shot = 0;
    float short_quali = 0;
    float full_quali = 0;

    float row_float(size_t row, uintptr_t at) const
    {
        float v = 0;
        std::memcpy(&v, rows + row * kRowSize + at, sizeof v);
        return v;
    }
    float percent25() const { return row_float(0, kRowPercent25); }
    float percent100() const { return row_float(0, kRowPercent100); }
};

inline bool same(const Fuel &a, const Fuel &b)
{
    return std::memcmp(&a.burn, &b.burn, sizeof a.burn) == 0 && a.count == b.count &&
           std::memcmp(a.rows, b.rows, sizeof a.rows) == 0 &&
           std::memcmp(&a.one_shot, &b.one_shot, sizeof a.one_shot) == 0 &&
           std::memcmp(&a.short_quali, &b.short_quali, sizeof a.short_quali) == 0 &&
           std::memcmp(&a.full_quali, &b.full_quali, sizeof a.full_quali) == 0;
}

// The fuel the copy writes: the source's burn, rows and qualifying fuel over the destination's row count.
inline Fuel merged(const Fuel &dst, const Fuel &src)
{
    Fuel out = dst;
    out.burn = src.burn;
    std::memcpy(out.rows, src.rows, dst.count * kRowSize);
    out.one_shot = src.one_shot;
    out.short_quali = src.short_quali;
    out.full_quali = src.full_quali;
    return out;
}

struct Track {
    uint32_t index = 0;
    uintptr_t object = 0;
    uintptr_t record = 0;
    std::string name;
    Fuel fuel;
};

struct Set {
    uintptr_t at = 0;
    uint32_t modes = 0, seasons = 0, formats = 0;
    std::vector<Track> tracks;
};

// Stock values the layout has to show before anything is written (kg per lap, kg at 25 % and 100 %).
struct Expect {
    const char *track;
    float burn, percent25, percent100;
};

struct Table {
    const char *label;
    uint32_t mode, season, format;
    Expect checks[2];
};

inline constexpr Table kSource{"f1 (season 2025)", kModeF1, kSeason2025, kFormatDefault,
                               {{"melbourne", 1.74f, 30.6f, 110.0f}, {"silverstone", 1.88f, 29.2f, 110.0f}}};
inline constexpr Table kTarget{"f1_2026", kModeF1_26, kSeason2026, kFormatDefault,
                               {{"melbourne", 1.30f, 23.1f, 81.4f}, {"silverstone", 1.31f, 20.7f, 74.0f}}};
inline constexpr Table kSprintSource{"sprint_f1_2025", kModeF1, kSeason2025, kFormatSprint,
                                     {{"melbourne", 1.74f, 13.2f, 37.6f}, {"silverstone", 1.88f, 12.3f, 36.8f}}};
inline constexpr Table kSprintTarget{"sprint_f1_2026", kModeF1_26, kSeason2026, kFormatSprint,
                                     {{"melbourne", 1.30f, 10.1f, 28.3f}, {"silverstone", 1.31f, 8.9f, 25.9f}}};

inline bool close_to(float a, float b)
{
    return std::fabs(a - b) < 0.005f;
}

inline bool matches(const Set &s, const Table &t)
{
    auto has = [](uint32_t mask, uint32_t bit) { return bit < 32 && ((mask >> bit) & 1u); };
    return has(s.modes, t.mode) && has(s.seasons, t.season) && has(s.formats, t.format);
}

// The set the game's getter returns for the table's query: the first whose masks hold its bits.
inline const Set *pick(const std::vector<Set> &sets, const Table &t)
{
    for (auto &s : sets)
        if (matches(s, t))
            return &s;
    return nullptr;
}

inline const Track *find(const Set &s, std::string_view name)
{
    for (auto &t : s.tracks)
        if (t.name == name)
            return &t;
    return nullptr;
}

inline std::string fmt_fuel(const Fuel &f)
{
    char b[96];
    std::snprintf(b, sizeof b, "%.2f kg/lap, %.1f/%.1f kg", f.burn, f.percent25(), f.percent100());
    return b;
}

// The set holds the table's stock values on both check tracks, with one to three race rows.
inline bool check(const Set &s, const Table &t, std::string &why)
{
    for (auto &e : t.checks) {
        const Track *tr = find(s, e.track);
        if (!tr) {
            why = std::string(t.label) + " has no " + e.track;
            return false;
        }
        const Fuel &f = tr->fuel;
        if (f.count < 1 || f.count > kMaxRows) {
            why = std::string(t.label) + " " + e.track + " has " + std::to_string(f.count) + " race fuel row(s)";
            return false;
        }
        if (!close_to(f.burn, e.burn) || !close_to(f.percent25(), e.percent25) ||
            !close_to(f.percent100(), e.percent100) || !close_to(f.row_float(0, kRowPercent100Again), e.percent100)) {
            char b[160];
            std::snprintf(b, sizeof b, "%s %s reads %s, stock is %.2f kg/lap, %.1f/%.1f kg", t.label, e.track,
                          fmt_fuel(f).c_str(), e.burn, e.percent25, e.percent100);
            why = b;
            return false;
        }
    }
    return true;
}

// The set to copy from: the one the getter returns for the table's query, or else the first set that holds
// the table's stock values (a set that takes all its fuel from its parent may not carry it).
inline const Set *pick_source(const std::vector<Set> &sets, const Table &t, std::string &why)
{
    const Set *s = pick(sets, t);
    if (s && check(*s, t, why))
        return s;
    std::string first = s ? why : std::string("no track data set answers the ") + t.label + " query";
    for (auto &c : sets) {
        std::string ignored;
        if (&c != s && check(c, t, ignored))
            return &c;
    }
    why = first;
    return nullptr;
}

struct Pair {
    const Track *dst = nullptr;
    const Track *src = nullptr;
};

// Tracks of both sets with the same track record and race row count, matched by track index.
inline std::vector<Pair> plan(const Set &dst, const Set &src, size_t &skipped)
{
    std::vector<Pair> out;
    skipped = 0;
    for (auto &d : dst.tracks) {
        const Track *s = nullptr;
        for (auto &c : src.tracks)
            if (c.index == d.index) {
                s = &c;
                break;
            }
        if (!s || s->record != d.record || s->object == d.object || s->fuel.count != d.fuel.count ||
            d.fuel.count < 1 || d.fuel.count > kMaxRows) {
            ++skipped;
            continue;
        }
        out.push_back({&d, s});
    }
    return out;
}

// One written track: the bytes before and after, so a restore only puts back what is still ours.
struct Saved {
    uintptr_t object = 0;
    std::string name;
    Fuel before;
    Fuel after;
};

enum class Undo { restore, changed, unreadable };

inline Undo undo_action(const Saved &s, bool readable, const Fuel &now)
{
    if (!readable)
        return Undo::unreadable;
    return same(now, s.after) ? Undo::restore : Undo::changed;
}

// Reads the game layout through a memory reader M with bool M::read(uintptr_t, void *, size_t) and
// bool M::c_string(uintptr_t, std::string &).
template <class M> bool read_fuel(M &m, uintptr_t obj, Fuel &f)
{
    if (!m.read(obj + kBurn, &f.burn, sizeof f.burn) || !m.read(obj + kRowCount, &f.count, sizeof f.count))
        return false;
    if (f.count > kMaxRows)
        return true;
    return m.read(obj + kRows, f.rows, sizeof f.rows) && m.read(obj + kQualOneShot, &f.one_shot, sizeof f.one_shot) &&
           m.read(obj + kQualShort, &f.short_quali, sizeof f.short_quali) &&
           m.read(obj + kQualFull, &f.full_quali, sizeof f.full_quali);
}

template <class M> bool read_u64(M &m, uintptr_t at, uintptr_t &out)
{
    uint64_t v = 0;
    if (!m.read(at, &v, sizeof v))
        return false;
    out = static_cast<uintptr_t>(v);
    return true;
}

template <class M> bool read_sets(M &m, uintptr_t system, std::vector<Set> &sets, std::string &why)
{
    uintptr_t begin = 0, end = 0;
    if (!read_u64(m, system + kSetsBegin, begin) || !read_u64(m, system + kSetsEnd, end)) {
        why = "the track data sets are unreadable";
        return false;
    }
    if (!begin || end < begin || (end - begin) % kSetSize || (end - begin) / kSetSize > kMaxSets ||
        end == begin) {
        why = "the track data set vector is not a vector of 0x30-byte sets";
        return false;
    }
    for (uintptr_t at = begin; at < end; at += kSetSize) {
        Set s;
        s.at = at;
        uintptr_t tb = 0, te = 0;
        if (!m.read(at + kSetModes, &s.modes, 4) || !m.read(at + kSetSeasons, &s.seasons, 4) ||
            !m.read(at + kSetFormats, &s.formats, 4) || !read_u64(m, at + kSetTracksBegin, tb) ||
            !read_u64(m, at + kSetTracksEnd, te)) {
            why = "a track data set is unreadable";
            return false;
        }
        if (te < tb || (te - tb) % kItemSize || (te - tb) / kItemSize > kMaxTracks) {
            why = "a track data set's track vector is not a vector of 16-byte items";
            return false;
        }
        for (uintptr_t it = tb; it < te; it += kItemSize) {
            Track t;
            if (!read_u64(m, it + kItemTrack, t.object)) {
                why = "a track item is unreadable";
                return false;
            }
            if (!t.object)
                continue;
            t.index = static_cast<uint32_t>((it - tb) / kItemSize);
            uintptr_t name = 0;
            uint8_t index = 0;
            if (!read_u64(m, t.object + kTrackRecord, t.record) || !t.record ||
                !m.read(t.record + kRecordIndex, &index, 1) || !read_u64(m, t.record + kRecordName, name) ||
                !m.c_string(name, t.name) || !read_fuel(m, t.object, t.fuel)) {
                why = "a track object is unreadable";
                return false;
            }
            if (index != t.index) {
                why = "a track object's record index " + std::to_string(index) + " is not its slot " +
                      std::to_string(t.index);
                return false;
            }
            s.tracks.push_back(std::move(t));
        }
        sets.push_back(std::move(s));
    }
    return true;
}

// The data sets the core knows, by the name an operation request uses.
struct Named {
    const char *name;
    const Table *table;
};

inline constexpr Named kSets[] = {
    {"f1_2025", &kSource}, {"sprint_f1_2025", &kSprintSource}, {"f1_2026", &kTarget}, {"sprint_f1_2026", &kSprintTarget}};

inline const Table *table_named(std::string_view name)
{
    for (auto &n : kSets)
        if (name == n.name)
            return n.table;
    return nullptr;
}

// The fuel a burn-per-lap request writes: the track's own rows and qualifying fuel, the new burn.
inline Fuel with_burn(const Fuel &dst, float burn)
{
    Fuel out = dst;
    out.burn = burn;
    return out;
}

// The pairs of one copy for one track code, or every track for "*".
inline std::vector<Pair> plan_track(const Set &dst, const Set &src, std::string_view track, size_t &skipped)
{
    std::vector<Pair> all = plan(dst, src, skipped), out;
    for (auto &p : all)
        if (track == "*" || p.dst->name == track)
            out.push_back(p);
    return out;
}

}
