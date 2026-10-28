// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career - the 2025 race weekend fuel in the 2016-2025 seasons (progressive_fuel.dll).
//
// A Progressive career races every season in the 2026 Driver Career's mode, so its weekends read the
// f1_2026 track data, whose fuel loads and fuel burn per lap are sized for the 2026 power unit; the
// 2016-2025 season cars carry the 2025 one and run short by about a quarter. While progressive.dll shows a
// season before 2026 (ProgressiveNamesYear, set at Team HQ and at career load), this copies every track's
// fuel of the set GP 2025 reads (the f1 numbers) into the f1_2026 set, and sprint_f1_2025's into
// sprint_f1_2026, in the game's TRACK_DATA system (fuel_logic.hpp). It puts the saved bytes back when the
// career reaches 2026, when the career unloads, and when any other career or mode starts. Nothing is written
// unless Melbourne and Silverstone show their stock values in all four sets. A separate DLL, so a failure here
// leaves the rest of Progressive running.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <swofty/career.hpp>
#include <swofty/events.hpp>
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include "../fuel_logic.hpp"

namespace {

namespace pf = progressive::fuel;

constexpr uint64_t kRetryMs = 2000;
constexpr int kFirstYear = 2016;
constexpr int kLastYear = 2025;

struct GameMem {
    bool read(uintptr_t at, void *out, size_t n)
    {
        if (!at || !swofty::mem::readable(at, n))
            return false;
        std::memcpy(out, reinterpret_cast<const void *>(at), n);
        return true;
    }
    bool c_string(uintptr_t at, std::string &out) { return swofty::mem::c_string(at, out, 64); }
};

bool writable(uintptr_t at, size_t n)
{
    if (!swofty::mem::readable(at, n))
        return false;
    uintptr_t p = at, end = at + n;
    while (p < end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<const void *>(p), &mbi, sizeof mbi) || mbi.State != MEM_COMMIT)
            return false;
        DWORD prot = mbi.Protect & 0xff;
        if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS) ||
            !(prot == PAGE_READWRITE || prot == PAGE_EXECUTE_READWRITE))
            return false;
        uintptr_t next = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (next <= p)
            return false;
        p = next;
    }
    return true;
}

bool put(uintptr_t at, const void *src, size_t n)
{
    if (!writable(at, n))
        return false;
    std::memcpy(reinterpret_cast<void *>(at), src, n);
    return true;
}

bool write_fuel(uintptr_t obj, const pf::Fuel &f)
{
    return put(obj + pf::kBurn, &f.burn, sizeof f.burn) && put(obj + pf::kRows, f.rows, f.count * pf::kRowSize) &&
           put(obj + pf::kQualOneShot, &f.one_shot, sizeof f.one_shot) &&
           put(obj + pf::kQualShort, &f.short_quali, sizeof f.short_quali) &&
           put(obj + pf::kQualFull, &f.full_quali, sizeof f.full_quali);
}

using YearFn = int (*)(void);
YearFn g_year_fn = nullptr;
uint64_t g_year_next = 0;

int names_year()
{
    if (!g_year_fn) {
        uint64_t now = GetTickCount64();
        if (now < g_year_next)
            return 0;
        g_year_next = now + kRetryMs;
        HMODULE m = GetModuleHandleW(L"progressive.dll");
        g_year_fn = m ? reinterpret_cast<YearFn>(reinterpret_cast<void *>(GetProcAddress(m, "ProgressiveNamesYear")))
                      : nullptr;
        if (!g_year_fn)
            return 0;
    }
    return g_year_fn();
}

std::vector<pf::Saved> g_saved;
int g_patched_year = 0;
uint64_t g_next_try = 0;
std::string g_last_why;

uintptr_t track_data_system(std::string &why)
{
    auto e = reinterpret_cast<uintptr_t>(swofty::career::entity());
    if (!e) {
        why = "no championship entity yet";
        return 0;
    }
    uintptr_t sys = 0;
    GameMem m;
    if (!pf::read_u64(m, e + pf::kEntityTrackData, sys) || !swofty::mem::object_like(sys)) {
        why = "the championship entity's +0xf58 is not a game object";
        return 0;
    }
    return sys;
}

bool collect(const pf::Table &t, const std::vector<pf::Set> &sets, const pf::Set *&out, std::string &why)
{
    out = pf::pick(sets, t);
    if (!out) {
        why = std::string("no track data set answers the ") + t.label + " query";
        return false;
    }
    return pf::check(*out, t, why);
}

size_t copy(const pf::Set &dst, const pf::Set &src, std::vector<pf::Saved> &saved, size_t &failed)
{
    size_t skipped = 0, done = 0;
    for (auto &p : pf::plan(dst, src, skipped)) {
        pf::Saved s;
        s.object = p.dst->object;
        s.name = p.dst->name;
        s.before = p.dst->fuel;
        s.after = pf::merged(p.dst->fuel, p.src->fuel);
        if (pf::same(s.before, s.after))
            continue;
        if (!write_fuel(s.object, s.after)) {
            write_fuel(s.object, s.before);
            ++failed;
            continue;
        }
        saved.push_back(std::move(s));
        ++done;
    }
    return done;
}

void apply(int year)
{
    std::string why;
    GameMem m;
    std::vector<pf::Set> sets;
    const pf::Set *src = nullptr, *dst = nullptr, *sprint_src = nullptr, *sprint_dst = nullptr;
    uintptr_t sys = track_data_system(why);
    bool ok = sys && pf::read_sets(m, sys, sets, why) && collect(pf::kTarget, sets, dst, why) &&
              collect(pf::kSprintTarget, sets, sprint_dst, why) &&
              (src = pf::pick_source(sets, pf::kSource, why)) != nullptr &&
              (sprint_src = pf::pick_source(sets, pf::kSprintSource, why)) != nullptr;
    if (!ok) {
        if (why != g_last_why)
            swofty::log("[progressive] fuel %d: the 2026 fuel stays (%s); nothing written", year, why.c_str());
        g_last_why = why;
        return;
    }
    g_last_why.clear();
    std::string before;
    if (const pf::Track *mel = pf::find(*dst, "melbourne"))
        before = pf::fmt_fuel(mel->fuel);
    size_t failed = 0;
    size_t n = copy(*dst, *src, g_saved, failed);
    size_t ns = copy(*sprint_dst, *sprint_src, g_saved, failed);
    g_patched_year = year;
    pf::Fuel now;
    std::string after = "?";
    if (const pf::Track *mel = pf::find(*dst, "melbourne"); mel && pf::read_fuel(m, mel->object, now))
        after = pf::fmt_fuel(now);
    swofty::log("[progressive] fuel %d: the 2025 fuel for %zu track(s) of f1_2026 and %zu of sprint_f1_2026 "
                "(from sets %zu and %zu of %zu, %zu write(s) refused); melbourne %s -> %s",
                year, n, ns, static_cast<size_t>(src - sets.data()), static_cast<size_t>(sprint_src - sets.data()),
                sets.size(), failed, before.c_str(), after.c_str());
}

void restore(const char *why)
{
    if (g_saved.empty())
        return;
    GameMem m;
    size_t back = 0, changed = 0, gone = 0;
    std::string mel;
    for (auto &s : g_saved) {
        pf::Fuel now;
        bool readable = pf::read_fuel(m, s.object, now);
        switch (pf::undo_action(s, readable, now)) {
        case pf::Undo::restore:
            if (write_fuel(s.object, s.before)) {
                ++back;
                if (s.name == "melbourne" && mel.empty())
                    mel = pf::fmt_fuel(s.after) + " -> " + pf::fmt_fuel(s.before);
            } else {
                ++gone;
            }
            break;
        case pf::Undo::changed:
            ++changed;
            break;
        case pf::Undo::unreadable:
            ++gone;
            break;
        }
    }
    swofty::log("[progressive] fuel: the 2026 fuel is back for %zu track(s) (%s; %zu changed since, %zu "
                "unreadable or refused)%s%s",
                back, why, changed, gone, mel.empty() ? "" : "; melbourne ", mel.c_str());
    g_saved.clear();
    g_patched_year = 0;
}

void frame(double, void *)
{
    int year = names_year();
    bool want = year >= kFirstYear && year <= kLastYear;
    if (!want) {
        if (!g_saved.empty())
            restore(year ? "the career reached 2026" : "no Progressive season before 2026 is shown");
        g_patched_year = 0;
        g_last_why.clear();
        return;
    }
    if (g_patched_year)
        return;
    uint64_t now = GetTickCount64();
    if (now < g_next_try)
        return;
    g_next_try = now + kRetryMs;
    apply(year);
}

void on_unloaded(const SwoftyEvent *, void *)
{
    restore("the career unloaded");
}

} // namespace

SWOFTY_ON_LOAD
{
    uint32_t token = 0;
    swofty::events::Result r = swofty::events::on_frame(frame, nullptr, &token);
    swofty::events::Result u = swofty::events::subscribe(SWOFTY_EVENT_CAREER_UNLOADED, on_unloaded, nullptr, &token);
    swofty::log("[progressive] fuel: seasons before 2026 race on the 2025 fuel (frame %s, career.unloaded %s)",
                swofty::events::name(r), swofty::events::name(u));
}

SWOFTY_ADDON()
