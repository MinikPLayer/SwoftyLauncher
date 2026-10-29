#pragma once

#include <swofty/json.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace swofty_core::apply::room {

constexpr uint64_t STOCK_SLOTS = uint64_t(1) << 20;
constexpr uint64_t CAP = 1677721;
constexpr uint64_t PROBE_RECORDS = 32;
constexpr uint64_t MARGIN = 4096;

constexpr uint64_t grows_at(uint64_t slots)
{
    return slots < 0x28f5c28f5c28f5dull ? slots * 80 / 100 : (slots / 100) * 80;
}

constexpr uint64_t slots_for(uint64_t records)
{
    uint64_t n = 8;
    while (n && grows_at(n) < records)
        n <<= 1;
    return n;
}

struct Target {
    uint64_t want = 0, capped_from = 0;
    bool capped = false;
};

constexpr Target target(uint64_t file, uint64_t older_edits, uint64_t registry_keys, uint64_t cap = CAP)
{
    Target t;
    t.want = file + older_edits + registry_keys + PROBE_RECORDS + MARGIN;
    if (t.want > cap) {
        t.capped_from = t.want;
        t.want = cap;
        t.capped = true;
    }
    return t;
}

constexpr uint64_t left(uint64_t slots, uint64_t count, uint64_t margin = MARGIN)
{
    uint64_t g = grows_at(slots);
    return g > count + margin ? g - count - margin : 0;
}

struct AddonAsk {
    std::string addon;
    uint64_t new_keys = 0, rows = 0;
};

struct AddonVerdict {
    std::string addon;
    uint64_t asked = 0, rows = 0;
    bool admitted = false;
};

struct Budget {
    uint64_t room = 0, admitted_keys = 0, refused_keys = 0, asked_keys = 0;
    std::vector<AddonVerdict> addons;
};

inline Budget admit(uint64_t room, const std::vector<AddonAsk> &asks)
{
    Budget b;
    b.room = room;
    uint64_t free = room;
    for (auto &a : asks) {
        AddonVerdict v{a.addon, a.new_keys, a.rows, false};
        b.asked_keys += a.new_keys;
        if (a.new_keys <= free) {
            v.admitted = true;
            free -= a.new_keys;
            b.admitted_keys += a.new_keys;
        } else {
            b.refused_keys += a.new_keys;
        }
        b.addons.push_back(v);
    }
    return b;
}

struct Report {
    uint64_t pe_timestamp = 0;
    std::string presize = "not_needed", reason, profile;
    uint64_t slots = 0, file_records = 0, reserved_for = 0;
    uint64_t older_asked = 0, older_new = 0;
    uint64_t registry_asked = 0, registry_new = 0, registry_refused = 0;
    std::map<std::string, std::pair<uint64_t, uint64_t>> addons;
    uint64_t probes = 0, count = 0, margin = MARGIN;
    uint32_t info_inc = 0;
    bool loaded = false;
};

inline swofty::json::Value to_json(const Report &r)
{
    using swofty::json::Value;
    Value o = Value::object();
    o["version"] = Value(1);
    o["pe_timestamp"] = Value(int64_t(r.pe_timestamp));
    o["presize"] = Value(r.presize);
    if (!r.reason.empty())
        o["reason"] = Value(r.reason);
    o["profile"] = Value(r.profile.empty() ? std::string("default") : r.profile);
    if (r.loaded) {
        o["slots"] = Value(int64_t(r.slots));
        o["grows_at"] = Value(int64_t(grows_at(r.slots)));
        o["file_records"] = Value(int64_t(r.file_records));
    }
    if (r.reserved_for)
        o["reserved_for"] = Value(int64_t(r.reserved_for));
    o["older_edits"] = Value::object({{"asked", Value(int64_t(r.older_asked))}, {"new_keys", Value(int64_t(r.older_new))}});
    Value addons = Value::object();
    for (auto &[id, aa] : r.addons)
        addons[id] = Value::object({{"asked", Value(int64_t(aa.first))}, {"applied", Value(int64_t(aa.second))}});
    o["registry"] = Value::object({{"asked_keys", Value(int64_t(r.registry_asked))},
                                   {"new_keys", Value(int64_t(r.registry_new))},
                                   {"refused_keys", Value(int64_t(r.registry_refused))},
                                   {"addons", addons}});
    o["probes"] = Value(int64_t(r.probes));
    if (r.loaded) {
        o["count"] = Value(int64_t(r.count));
        o["margin"] = Value(int64_t(r.margin));
        o["room"] = Value(int64_t(grows_at(r.slots)) - int64_t(r.count) - int64_t(r.margin));
        o["info_inc"] = Value(int64_t(r.info_inc));
    }
    return o;
}

inline std::string thousands(uint64_t n)
{
    std::string s = std::to_string(n), out;
    int k = 0;
    for (auto it = s.rbegin(); it != s.rend(); ++it) {
        if (k && k % 3 == 0)
            out.insert(out.begin(), ',');
        out.insert(out.begin(), *it);
        k++;
    }
    return out;
}

inline std::string line(const Report &r)
{
    std::string older = thousands(r.older_asked), fresh = thousands(r.registry_asked);
    if (!r.loaded && r.presize == "unavailable")
        return "[registry] room: pre-size unavailable" + (r.reason.empty() ? std::string() : " (" + r.reason + ")") +
               "; the value map was not measured (older addons' edits " + older + "; 1.6 records " + fresh + ")";
    if (!r.loaded)
        return "[registry] room: nothing to size (no older addons' database edits, no 1.6 database records); the value "
               "map is the game's own";
    std::string used = thousands(r.count) + " of " + thousands(grows_at(r.slots)) + " records used";
    if (r.presize == "applied")
        return "[registry] room: value map sized for " + thousands(grows_at(r.slots)) +
               " records before load (older addons' edits " + older + "; 1.6 records " + fresh + "); " + used;
    if (r.presize == "unavailable")
        return "[registry] room: pre-size unavailable" + (r.reason.empty() ? std::string() : " (" + r.reason + ")") +
               "; " + used + " (older addons' edits " + older + "; 1.6 records " + fresh + ")";
    return "[registry] room: the game's own size was enough (older addons' edits " + older + "; 1.6 records " + fresh +
           "); " + used;
}

}
