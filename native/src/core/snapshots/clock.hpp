#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "../registry/store.hpp"

// When a career's seasons start (docs/std/scopes.md#career-snapshots): which year a season index is,
// when the core fires career.created and career.season.start, and who may say a season's year. No game
// calls: runtime.cpp feeds it the career moments, the Championship's season index at an idle Team HQ
// and the owner's reports, and fires what it decides; the host test drives it directly.
//
// The year of a season index: the effective career.season records of the career's data group, sorted by
// year, index i being the i-th year. No records, two records for one year or a gap between years leave it
// unknown. A career whose data group an addon owns (its career content or career.mode record) takes its
// years from that owner's career::report_season alone: its index can carry the owner's own offsets (a
// later first year, test seasons), so the core logs what the records say and waits for the report.
// career.season.start fires once per career and year: once this session, and not again for a per-season
// career whose saved entry already holds that year's snapshot.

namespace swofty_core::snapshots::seasons {

namespace reg = swofty_core::registry;

constexpr int FIRST_YEAR = 1950, LAST_YEAR = 2100;

struct YearMap {
    std::string data_group;
    std::vector<int> years;
    std::string why;
};

inline YearMap year_map(const std::string &data_group, std::vector<int> years)
{
    YearMap m;
    m.data_group = data_group;
    std::sort(years.begin(), years.end());
    if (years.empty()) {
        m.why = "no career.season record has data group " + (data_group.empty() ? std::string("(none)") : data_group);
        return m;
    }
    for (size_t i = 1; i < years.size(); i++) {
        if (years[i] == years[i - 1]) {
            m.why = "two career.season records of data group " + data_group + " are for " + std::to_string(years[i]);
            return m;
        }
        if (years[i] != years[i - 1] + 1) {
            m.why = "the career.season records of data group " + data_group + " skip from " +
                    std::to_string(years[i - 1]) + " to " + std::to_string(years[i]);
            return m;
        }
    }
    m.years = std::move(years);
    return m;
}

inline YearMap year_map(reg::Store &store, const std::string &data_group)
{
    std::vector<int> years;
    if (!data_group.empty()) {
        reg::Context ctx = store.context();
        ctx.data_group = data_group;
        for (auto &id : store.list("career.season", "")) {
            reg::Record r = store.get_live("career.season", id, ctx);
            if (r.status != reg::Status::ok || r.value["data_group"].as_string() != data_group)
                continue;
            if (r.value["year"].is_int())
                years.push_back(int(r.value["year"].as_int()));
        }
    }
    return year_map(data_group, std::move(years));
}

inline int year_at(const YearMap &m, int index, std::string &why)
{
    if (!m.why.empty()) {
        why = m.why;
        return 0;
    }
    if (index < 0 || size_t(index) >= m.years.size()) {
        why = "season index " + std::to_string(index) + " is past the " + std::to_string(m.years.size()) +
              " career.season record(s) of data group " + m.data_group + " (" + std::to_string(m.years.front()) + "-" +
              std::to_string(m.years.back()) + ")";
        return 0;
    }
    return m.years[size_t(index)];
}

struct Moment {
    bool fire = false;
    int year = 0, index = 0;
    std::vector<std::string> lines;
};

enum class Report { ok, late, invalid, refused };

inline const char *name(Report r)
{
    return r == Report::ok ? "ok" : r == Report::late ? "late" : r == Report::invalid ? "invalid" : "refused";
}

struct Season {
    std::string career, data_group, owner;
    int index = -1;
    bool ready = false;
    std::string not_ready;
};

// Whether a profile save writes the entry when SWOFTY_REGISTRY_SAVE is unset.
constexpr bool SAVE_BY_DEFAULT = false;
// A career's id: <mode>:<slot_guid as 16 hex digits>, or <mode>:<slot_id> without a guid or with
// KEY_BY_GUID off (re/tests/T132.md: whether two careers of one driver get different guids).
constexpr bool KEY_BY_GUID = true;

// SWOFTY_REGISTRY_SAVE: empty when the entry is written (on or 1, or unset with SAVE_BY_DEFAULT, and the
// save splice can write it), otherwise why not. Any other value is dry.
inline std::string save_blocked(const std::string &env, bool splice_live, const std::string &splice_blocked)
{
    bool on = env == "on" || env == "1" || (env.empty() && SAVE_BY_DEFAULT);
    if (!on)
        return env.empty() ? std::string("SWOFTY_REGISTRY_SAVE is unset, and saving defaults to dry")
                           : "SWOFTY_REGISTRY_SAVE=" + env + " (on or 1 writes)";
    return splice_live ? splice_blocked : std::string("the save splice is off");
}

// The mode part of a career id: the save container name of the career content that owns the data group,
// else <data group>.<driver_career|my_team>.
inline std::string mode_part(const std::string &save_name, const std::string &data_group, const std::string &kind)
{
    if (!save_name.empty())
        return save_name;
    return data_group.empty() ? kind : data_group + "." + kind;
}

inline std::string career_id(const std::string &mode, uint64_t slot_id, uint64_t slot_guid)
{
    if (KEY_BY_GUID && slot_guid) {
        char b[17];
        static const char *hex = "0123456789abcdef";
        for (int i = 0; i < 16; i++)
            b[i] = hex[(slot_guid >> (60 - 4 * i)) & 0xf];
        b[16] = 0;
        return mode + ":" + b;
    }
    return mode + ":" + std::to_string(slot_id);
}

// A new career's id until its save slot exists: <mode>:new<n>, replaced by career_id at the first save.
inline std::string provisional_id(const std::string &mode, unsigned n) { return mode + ":new" + std::to_string(n); }

inline bool is_provisional(const std::string &id) { return id.find(":new") != std::string::npos && id.find(':') != std::string::npos; }

// The bytes an entry of body bytes adds to the profile save: its root child (name length byte, name, bag
// type id, body) less the one the save already held.
inline long long entry_growth(size_t name, size_t body, bool had, size_t had_body)
{
    long long head = 1 + (long long)name + 4;
    return head + (long long)body - (had ? head + (long long)had_body : 0);
}

class Clock {
public:
    using Started = std::function<bool(const std::string &career, int year)>;

    void set_started(Started f) { started_fn_ = std::move(f); }

    void starting(bool new_career) { pending_new_ = new_career; }

    bool loaded(const std::string &career)
    {
        bool fresh = pending_new_ && !career.empty();
        pending_new_ = false;
        return fresh;
    }

    void unloaded()
    {
        pending_new_ = false;
        queued_.reset();
    }

    Moment hub(const Season &s, const YearMap &map)
    {
        Moment m;
        if (queued_ && !s.career.empty()) {
            Queued q = *queued_;
            queued_.reset();
            Season qs = s;
            qs.index = q.index;
            Report r = report(q.caller, qs, q.year, map, m);
            if (r == Report::ok) {
                m.lines.insert(m.lines.begin(), "[registry] career " + s.career + " now has an id; the queued career::report_season(" +
                                                    std::to_string(q.year) + ", " + std::to_string(q.index) + ") from " +
                                                    q.caller + " is replayed");
                return m;
            }
            m.fire = false;
        }
        if (s.career.empty() || s.index < 0)
            return m;
        std::string head = "[registry] career " + s.career + " season index " + std::to_string(s.index);
        auto r = reported_.find(s.career);
        if (r != reported_.end() && r->second == s.index)
            return m;
        std::string why;
        int computed = year_at(map, s.index, why);
        if (!s.owner.empty()) {
            once(m, head + ": data group " + s.data_group + " is " + s.owner + "'s; the core takes its year from " +
                        s.owner + "'s career::report_season (" +
                        (computed ? "the records put " + std::to_string(computed) + " here" : why) + ")");
            return m;
        }
        if (!s.ready) {
            once(m, head + ": not ready (" + s.not_ready + "); career.season.start waits for a later Team HQ");
            return m;
        }
        if (!computed) {
            once(m, head + ": no career.season.start (" + why + ")");
            return m;
        }
        if (started(s.career, computed)) {
            once(m, head + " is " + std::to_string(computed) + ", which already started in this career");
            return m;
        }
        started_.insert({s.career, computed});
        m.fire = true, m.year = computed, m.index = s.index;
        m.lines.push_back(head + " is " + std::to_string(computed) + " (season " + std::to_string(s.index + 1) +
                          " of the career.season records of data group " + s.data_group + ", " +
                          std::to_string(map.years.front()) + "-" + std::to_string(map.years.back()) +
                          "); career.season.start");
        return m;
    }

    Report report(const std::string &caller, const Season &s, int year, const YearMap &map, Moment &m)
    {
        m = {};
        std::string head = "[registry] career::report_season(" + std::to_string(year) + ", " +
                           std::to_string(s.index) + ") from " + (caller.empty() ? std::string("an unknown module") : caller);
        if (s.career.empty()) {
            bool queue = !s.owner.empty() && caller == s.owner && year >= FIRST_YEAR && year <= LAST_YEAR && s.index >= 0;
            if (queue)
                queued_ = Queued{caller, year, s.index};
            m.lines.push_back(head + ": late (no career is loaded)" +
                              (queue ? "; queued until the career has an id (the next idle Team HQ replays it)" : ""));
            return Report::late;
        }
        if (year < FIRST_YEAR || year > LAST_YEAR || s.index < 0) {
            m.lines.push_back(head + ": invalid (a year " + std::to_string(FIRST_YEAR) + "-" +
                              std::to_string(LAST_YEAR) + " and a season index from 0 are needed)");
            return Report::invalid;
        }
        if (s.owner.empty()) {
            m.lines.push_back(head + ": refused (no addon owns data group " +
                              (s.data_group.empty() ? std::string("(none)") : s.data_group) +
                              "; the core works out its seasons from the career.season records)");
            return Report::refused;
        }
        if (caller != s.owner) {
            m.lines.push_back(head + ": refused (only " + s.owner + ", which owns data group " + s.data_group +
                              ", reports its seasons)");
            return Report::refused;
        }
        reported_[s.career] = s.index;
        std::string why;
        int computed = year_at(map, s.index, why);
        std::string note = computed == year ? "" :
                           computed ? "; the records put " + std::to_string(computed) +
                                          " at this index (a later first year or test seasons), and the reported year wins"
                                    : "; " + why + ", and the reported year stands";
        if (started(s.career, year)) {
            m.lines.push_back(head + ": ok, " + std::to_string(year) + " already started in career " + s.career + note);
            return Report::ok;
        }
        started_.insert({s.career, year});
        m.fire = true, m.year = year, m.index = s.index;
        m.lines.push_back(head + ": ok, career " + s.career + " season index " + std::to_string(s.index) + " is " +
                          std::to_string(year) + note + "; career.season.start");
        return Report::ok;
    }

    void rename(const std::string &from, const std::string &to)
    {
        if (from.empty() || to.empty() || from == to)
            return;
        std::set<std::pair<std::string, int>> moved;
        for (auto &[c, y] : started_)
            moved.insert({c == from ? to : c, y});
        started_ = std::move(moved);
        if (auto r = reported_.find(from); r != reported_.end()) {
            int index = r->second;
            reported_.erase(r);
            reported_[to] = index;
        }
    }

    bool queued() const { return queued_.has_value(); }

private:
    struct Queued {
        std::string caller;
        int year = 0, index = 0;
    };
    std::optional<Queued> queued_;
    Started started_fn_;
    std::set<std::pair<std::string, int>> started_;
    std::map<std::string, int> reported_;
    std::set<std::string> said_;
    bool pending_new_ = false;

    bool started(const std::string &career, int year) const
    {
        return started_.count({career, year}) || (started_fn_ && started_fn_(career, year));
    }

    void once(Moment &m, const std::string &line)
    {
        if (said_.insert(line).second)
            m.lines.push_back(line);
    }
};

}
