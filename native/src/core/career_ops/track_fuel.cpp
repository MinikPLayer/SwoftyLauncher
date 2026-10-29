#include "../runtime.hpp"

#include <swofty/game.hpp>
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "fuel_logic.hpp"
#include "track_fuel.hpp"

// tracks::set_track_fuel (SwoftyCareerOps version 4): Progressive Career's race weekend fuel copy (addons/
// progressive/native/src/ui/fuel.cpp) moved into the core and driven by a request. It writes a track's fuel in
// one of the game's track data sets, from the same track in another set or as a burn per lap, keeps the bytes
// it found and puts them back when the career unloads or on a restore request. A set is written only when the
// sets the request names show their stock values on the check tracks; a restore puts back only what still
// holds the bytes the core wrote.

namespace {

namespace pf = career_ops::fuel;

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
        if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS) || !(prot == PAGE_READWRITE || prot == PAGE_EXECUTE_READWRITE))
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

std::mutex g_lock;
std::vector<pf::Saved> g_saved;

uintptr_t track_data_system(std::string &why)
{
    const SwoftyCareer *c = runtime::career_table();
    auto e = c && c->entity ? reinterpret_cast<uintptr_t>(c->entity()) : 0;
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

struct Job {
    std::string set, from;
    const pf::Set *dst = nullptr, *src = nullptr;
    size_t written = 0, unchanged = 0, skipped = 0;
};

struct Outcome {
    size_t written = 0, unchanged = 0, skipped = 0, failed = 0, restored = 0, changed_since = 0, gone = 0;
    std::string melbourne;
};

void remember(pf::Saved s)
{
    for (auto &o : g_saved)
        if (o.object == s.object) {
            o.after = s.after;
            return;
        }
    g_saved.push_back(std::move(s));
}

void write_pairs(Job &j, const std::vector<pf::Pair> &pairs, const float *burn, Outcome &out)
{
    for (auto &p : pairs) {
        pf::Saved s;
        s.object = p.dst->object;
        s.name = p.dst->name;
        s.before = p.dst->fuel;
        s.after = burn ? pf::with_burn(p.dst->fuel, *burn) : pf::merged(p.dst->fuel, p.src->fuel);
        if (pf::same(s.before, s.after)) {
            ++j.unchanged;
            continue;
        }
        if (!write_fuel(s.object, s.after)) {
            write_fuel(s.object, s.before);
            ++out.failed;
            continue;
        }
        if (s.name == "melbourne" && out.melbourne.empty())
            out.melbourne = j.set + " melbourne " + pf::fmt_fuel(s.before) + " -> " + pf::fmt_fuel(s.after);
        remember(std::move(s));
        ++j.written;
    }
}

void restore_all(Outcome &out)
{
    GameMem m;
    for (auto &s : g_saved) {
        pf::Fuel now;
        bool readable = pf::read_fuel(m, s.object, now);
        switch (pf::undo_action(s, readable, now)) {
        case pf::Undo::restore:
            if (write_fuel(s.object, s.before)) {
                ++out.restored;
                if (s.name == "melbourne" && out.melbourne.empty())
                    out.melbourne = "melbourne " + pf::fmt_fuel(s.after) + " -> " + pf::fmt_fuel(s.before);
            } else {
                ++out.gone;
            }
            break;
        case pf::Undo::changed:
            ++out.changed_since;
            break;
        case pf::Undo::unreadable:
            ++out.gone;
            break;
        }
    }
    g_saved.clear();
}

std::string quote(const std::string &s)
{
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\')
            out += '\\';
        if (c >= 0x20)
            out += char(c);
    }
    return out + "\"";
}

int put_report(const std::string &text, char *out, size_t cap, size_t *len)
{
    if (len)
        *len = text.size();
    if (!out && !cap)
        return SWOFTY_ASSET_OK;
    if (!out || cap < text.size() + 1)
        return SWOFTY_TOO_SMALL;
    std::memcpy(out, text.c_str(), text.size() + 1);
    return SWOFTY_ASSET_OK;
}

std::string report_json(const std::string &result, const std::string &why, const std::vector<Job> &jobs, const Outcome &o)
{
    std::string r = "{\"result\":" + quote(result) + ",\"why\":" + quote(why) + ",\"written\":" + std::to_string(o.written) +
                    ",\"unchanged\":" + std::to_string(o.unchanged) + ",\"skipped\":" + std::to_string(o.skipped) +
                    ",\"failed\":" + std::to_string(o.failed) + ",\"restored\":" + std::to_string(o.restored) +
                    ",\"changed_since\":" + std::to_string(o.changed_since) + ",\"sets\":[";
    for (size_t i = 0; i < jobs.size(); ++i)
        r += std::string(i ? "," : "") + "{\"set\":" + quote(jobs[i].set) + ",\"from\":" + quote(jobs[i].from) +
             ",\"written\":" + std::to_string(jobs[i].written) + ",\"unchanged\":" + std::to_string(jobs[i].unchanged) + "}";
    return r + "],\"melbourne\":" + quote(o.melbourne) + "}";
}

void on_unloaded(const SwoftyEvent *, void *)
{
    std::lock_guard<std::mutex> l(g_lock);
    if (g_saved.empty())
        return;
    Outcome o;
    restore_all(o);
    swofty::log("[career-ops] set_track_fuel: the career unloaded; %zu track(s) put back (%zu changed since, %zu "
                "unreadable or refused)%s%s",
                o.restored, o.changed_since, o.gone, o.melbourne.empty() ? "" : "; ", o.melbourne.c_str());
}

}

namespace career_ops {

int set_track_fuel(const std::string &who, SwoftyText request, char *out, size_t cap, size_t *len)
{
    std::string text = request.data ? std::string(request.data, request.size) : std::string(), summary, why, track;
    json::Value j;
    std::vector<Job> jobs;
    Outcome o;
    int r = SWOFTY_ASSET_OK;
    std::string result = "ok";
    float burn = 0;
    bool has_burn = false, restore = false;
    if (!json::parse(text, j) || j.type != json::Value::Obj) {
        r = SWOFTY_ASSET_INVALID;
        summary = "the request is not a JSON object";
    } else {
        why = j["why"].str();
        track = j["track"].str("*");
        restore = j["restore"].type == json::Value::Bool && j["restore"].b;
        if (j["copy"].type == json::Value::Arr)
            for (auto &c : j["copy"].a)
                jobs.push_back({c["set"].str(), c["from"].str()});
        else if (!restore)
            jobs.push_back({j["set"].str(), j["from"].str()});
        if (j["burn_per_lap"].type == json::Value::Num) {
            has_burn = true;
            burn = static_cast<float>(j["burn_per_lap"].n);
        }
        if (!restore && jobs.empty()) {
            r = SWOFTY_ASSET_INVALID;
            summary = "the request names no data set";
        }
        for (auto &jb : jobs) {
            if (r != SWOFTY_ASSET_OK)
                break;
            if (!pf::table_named(jb.set) || (!has_burn && !pf::table_named(jb.from))) {
                r = SWOFTY_ASSET_INVALID;
                summary = "unknown data set \"" + (pf::table_named(jb.set) ? jb.from : jb.set) +
                          "\" (known: f1_2025, sprint_f1_2025, f1_2026, sprint_f1_2026)";
            } else if (has_burn && !(burn > 0.0f && burn < 20.0f)) {
                r = SWOFTY_ASSET_INVALID;
                summary = "burn_per_lap must be above 0 and below 20 kg";
            } else if (!has_burn && jb.set == jb.from) {
                r = SWOFTY_ASSET_INVALID;
                summary = "a data set is copied from another one";
            }
        }
    }
    std::lock_guard<std::mutex> l(g_lock);
    if (r == SWOFTY_ASSET_OK && restore) {
        restore_all(o);
        summary = std::to_string(o.restored) + " track(s) put back (" + std::to_string(o.changed_since) + " changed since, " +
                  std::to_string(o.gone) + " unreadable or refused)" + (o.melbourne.empty() ? "" : "; " + o.melbourne);
    } else if (r == SWOFTY_ASSET_OK) {
        GameMem m;
        std::vector<pf::Set> sets;
        uintptr_t sys = track_data_system(summary);
        bool ok = sys && pf::read_sets(m, sys, sets, summary);
        for (auto &jb : jobs) {
            if (!ok)
                break;
            ok = collect(*pf::table_named(jb.set), sets, jb.dst, summary);
            if (ok && !has_burn)
                ok = (jb.src = pf::pick_source(sets, *pf::table_named(jb.from), summary)) != nullptr;
        }
        if (!ok) {
            r = sys ? SWOFTY_ASSET_UNAVAILABLE : SWOFTY_ASSET_LATE;
            summary = "nothing written: " + summary;
        } else {
            for (auto &jb : jobs) {
                size_t skipped = 0;
                std::vector<pf::Pair> pairs;
                if (has_burn) {
                    for (auto &t : jb.dst->tracks)
                        if ((track == "*" || t.name == track) && t.fuel.count >= 1 && t.fuel.count <= pf::kMaxRows)
                            pairs.push_back({&t, &t});
                } else {
                    pairs = pf::plan_track(*jb.dst, *jb.src, track, skipped);
                }
                jb.skipped = skipped;
                write_pairs(jb, pairs, has_burn ? &burn : nullptr, o);
                o.written += jb.written;
                o.unchanged += jb.unchanged;
                o.skipped += jb.skipped;
            }
            if (!o.written && !o.unchanged && track != "*") {
                r = SWOFTY_ASSET_INVALID;
                summary = "no track \"" + track + "\" in the data set(s)";
            } else {
                for (auto &jb : jobs)
                    summary += (summary.empty() ? "" : ", ") + jb.set + (has_burn ? "" : " from " + jb.from) + ": " +
                               std::to_string(jb.written) + " written, " + std::to_string(jb.unchanged) + " unchanged";
                summary += "; " + std::to_string(o.failed) + " write(s) refused" + (o.melbourne.empty() ? "" : "; " + o.melbourne) +
                           "; put back on career.unloaded";
            }
        }
    }
    if (r != SWOFTY_ASSET_OK)
        result = r == SWOFTY_ASSET_INVALID ? "refused" : "failed";
    char burn_text[32] = "";
    if (has_burn)
        std::snprintf(burn_text, sizeof burn_text, " %.2f kg/lap", burn);
    std::string head = restore ? std::string("restore") : track + burn_text;
    if (!why.empty())
        head += " (" + why + ")";
    swofty::log("[career-ops] set_track_fuel %s from %s: %s", head.c_str(), who.c_str(), summary.c_str());
    int w = put_report(report_json(result, summary, jobs, o), out, cap, len);
    return w == SWOFTY_TOO_SMALL && r == SWOFTY_ASSET_OK ? SWOFTY_TOO_SMALL : r;
}

void track_fuel_setup()
{
    const SwoftyEvents *ev = runtime::events_table();
    uint32_t token = 0;
    if (ev && ev->subscribe)
        ev->subscribe(SWOFTY_EVENT_CAREER_UNLOADED, on_unloaded, nullptr, &token);
}

}
