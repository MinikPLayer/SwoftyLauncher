// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career: experiment E1 (DESIGN.md), a developer tool. Only with the option dev_probe on
// (off by default), on a Team HQ or career load of any Driver Career, the seat writes in
// swofty/addons/swofty.progressive/data/probe.json are applied once per career:
//   {
//     "finalise": "auto" | "always" | "never",   default auto: only when a driver changed team or seat
//     "patch_weekend": true,                       also patch the current weekend's copy of each row
//     "repeat": false,                             ignore data/probe.applied
//     "seats": [ { "driver": <label or stat id>,
//                  "vehicle_ref": <entrants.vehicles label, stat id, or a packed "0x..." value>,
//                  "look": <look label, stat id, or a packed "0x..." value>,
//                  "vehicle_customisation", "badge", "driver_reg_value": <packed value or "none">,
//                  "number": <1..99>, "allow_player": false } ]
//   }
// Every entry is planned before anything is written; a bad entry writes nothing. The rows are
// written in one batch through Career_Set_EntrantData_Thunk, each with the row's own identity,
// position and the rest of its data; then every other row must read back unchanged and every
// written row as planned, or the batch is written back. One finalise at the end, per "finalise".
// data/probe.applied gets "<probe hash> <career key> season <n> <result>" so a probe is applied
// once per career; delete that file or change probe.json to apply again.

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>
#include <swofty/storage.hpp>

#include "../../../../native/src/core/json.hpp"
#include "names.hpp"
#include "progressive.hpp"
#include "seats.hpp"

SWOFTY_SETTING(bool, dev_probe, false);

namespace pg {

namespace {

using names::Kind;

constexpr const char *kProbePath = "data/probe.json";
constexpr const char *kAppliedPath = "data/probe.applied";

struct Plan {
    std::string what;
    uint32_t driver = 0;
    Row row;
    EntrantWriteData data{};
    uint16_t position = 0;
    uint8_t number = kNoNumber;
};

std::string g_last_skip;
std::string g_refused;

uint64_t fnv64(const std::string &s)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 0x100000001b3ull;
    }
    return h;
}

std::string hash_text(uint64_t h)
{
    char b[20];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(h));
    return b;
}

bool integral(const json::Value &v, double lo, double hi)
{
    return v.type == json::Value::Num && v.n >= lo && v.n <= hi && v.n == std::floor(v.n);
}

bool packed(const std::string &s, uint32_t &out)
{
    if (s == "none") {
        out = kNoKey;
        return true;
    }
    if (s.size() < 3 || s[0] != '0' || (s[1] != 'x' && s[1] != 'X'))
        return false;
    char *end = nullptr;
    unsigned long long v = std::strtoull(s.c_str(), &end, 16);
    if (!end || *end || v > 0xffffffffull)
        return false;
    out = static_cast<uint32_t>(v);
    return true;
}

// 0 absent, 1 resolved into out, -1 refused (err says why).
int key_value(const json::Value &v, Kind k, uint32_t old, uint32_t &out, std::string &err)
{
    if (v.type == json::Value::Null)
        return 0;
    uint32_t s = 0;
    if (v.type == json::Value::Str && packed(v.s, out))
        return 1;
    if (v.type == json::Value::Str) {
        if (!names::stat(k, v.s, s)) {
            err = std::string("no ") + names::kind_name(k) + " row labelled '" + v.s + "' in the gamedb";
            return -1;
        }
    } else if (integral(v, 0, 0x3ffe)) {
        s = static_cast<uint32_t>(v.n);
        if (!names::known(k, s)) {
            err = std::string("no ") + names::kind_name(k) + " row with stat_id " + std::to_string(s) + " in the gamedb";
            return -1;
        }
    } else if (integral(v, 0x10000, 0xffffffffu)) {
        out = static_cast<uint32_t>(v.n);
        return 1;
    } else {
        err = std::string(names::kind_name(k)) + " must be a label, a stat id or a packed \"0x...\" value";
        return -1;
    }
    out = k == Kind::vehicle && !none(old) ? (s << 16) | (old & 0xffffu) : s << 16;
    return 1;
}

int raw_value(const json::Value &v, const char *field, uint32_t &out, std::string &err)
{
    if (v.type == json::Value::Null)
        return 0;
    if (v.type == json::Value::Str && packed(v.s, out))
        return 1;
    if (integral(v, 0, 0xffffffffu)) {
        out = static_cast<uint32_t>(v.n);
        return 1;
    }
    err = std::string(field) + " must be a packed value (number or \"0x...\") or \"none\"";
    return -1;
}

bool plan_entry(uintptr_t e, const std::vector<Row> &rows, const json::Value &j, size_t index, Plan &p,
                std::string &err)
{
    char tag[32];
    std::snprintf(tag, sizeof tag, "seats[%zu]", index);
    p.what = tag;
    const json::Value &jd = j["driver"];
    uint32_t ds = 0;
    if (jd.type == json::Value::Str) {
        if (!names::stat(Kind::driver, jd.s, ds)) {
            err = p.what + ": no driver labelled '" + jd.s + "' in entrants.drivers";
            return false;
        }
    } else if (integral(jd, 0, 0x3ffe)) {
        ds = static_cast<uint32_t>(jd.n);
    } else {
        err = p.what + ": driver must be an entrants.drivers label or a stat id";
        return false;
    }
    p.driver = ds << 16;
    const Row *r = row_of(rows, p.driver);
    if (!r) {
        err = p.what + ": driver " + names::key_text(Kind::driver, p.driver) +
              " has no single entrant row in this career (missing or listed twice)";
        return false;
    }
    p.row = *r;
    bool allow_player = j["allow_player"].type == json::Value::Bool && j["allow_player"].b;
    if (human(r->rec) && !allow_player) {
        err = p.what + ": that is the player's row; set \"allow_player\": true to write it";
        return false;
    }
    if (!unique_identity(e, *r)) {
        err = p.what + ": the row's identity is not unique in the line-up";
        return false;
    }
    p.data = own_data(*r);
    p.position = r->rec.team_position;
    p.number = r->rec.car_number;
    std::string why;
    int got = key_value(j["vehicle_ref"], Kind::vehicle, r->rec.vehicle_ref, p.data.vehicle_ref, why);
    if (got < 0) {
        err = p.what + ": vehicle_ref: " + why;
        return false;
    }
    if (got > 0 && none(p.data.vehicle_ref)) {
        err = p.what + ": vehicle_ref: taking the car off a race seat would drop it from the grid";
        return false;
    }
    if (got > 0 && !has_car(r->rec)) {
        err = p.what + ": vehicle_ref: the driver has no race seat (no car on his row), a car would add one to the grid";
        return false;
    }
    if (key_value(j["look"], Kind::look, r->rec.driver_customisation, p.data.driver_customisation, why) < 0) {
        err = p.what + ": look: " + why;
        return false;
    }
    if (none(p.data.driver_customisation) && !teamless(r->rec)) {
        err = p.what + ": look: a seated driver without a look hangs the next session load";
        return false;
    }
    struct Raw {
        const char *field;
        uint32_t *to;
    } raws[] = {{"vehicle_customisation", &p.data.vehicle_customisation},
                {"badge", &p.data.badge_stat_id},
                {"driver_reg_value", &p.data.driver_reg_value}};
    for (auto &x : raws)
        if (raw_value(j[x.field], x.field, *x.to, why) < 0) {
            err = p.what + ": " + why;
            return false;
        }
    const json::Value &jn = j["number"];
    if (jn.type != json::Value::Null) {
        if (!integral(jn, 1, 99)) {
            err = p.what + ": number must be 1..99";
            return false;
        }
        p.number = static_cast<uint8_t>(jn.n);
        for (auto &o : rows)
            if (o.at != r->at && o.rec.car_number == p.number) {
                err = p.what + ": number " + std::to_string(p.number) + " is held by " +
                      names::key_text(Kind::driver, driver_of(o.rec));
                return false;
            }
    }
    return true;
}

EntrantRecord expected(const Plan &p)
{
    EntrantRecord x = p.row.rec;
    x.driver_customisation = p.data.driver_customisation;
    x.team_stat_id = p.data.team_stat_id;
    x.vehicle_ref = p.data.vehicle_ref;
    x.driver_reg_value = p.data.driver_reg_value;
    x.vehicle_customisation = p.data.vehicle_customisation;
    x.badge_stat_id = p.data.badge_stat_id;
    x.const_ffff0012 = p.data.seat_value;
    x.team_position = p.position;
    x.car_number = p.number;
    return x;
}

bool unchanged(const Plan &p)
{
    EntrantRecord x = expected(p);
    return std::memcmp(&x, &p.row.rec, sizeof x) == 0;
}

std::string diff_offsets(const EntrantRecord &a, const EntrantRecord &b)
{
    std::string out;
    auto *x = reinterpret_cast<const uint8_t *>(&a);
    auto *y = reinterpret_cast<const uint8_t *>(&b);
    for (size_t i = 0; i < sizeof(EntrantRecord); ++i)
        if (x[i] != y[i]) {
            char t[16];
            std::snprintf(t, sizeof t, "%s+0x%02zx", out.empty() ? "" : ",", i);
            out += t;
        }
    return out;
}

bool already_applied(const std::string &marker)
{
    std::string text;
    if (swofty::storage::read(kAppliedPath, text) != swofty::storage::Result::ok)
        return false;
    size_t at = 0;
    while ((at = text.find(marker, at)) != std::string::npos) {
        size_t eol = text.find('\n', at);
        std::string line = text.substr(at, eol == std::string::npos ? std::string::npos : eol - at);
        if (line.size() >= 3 && line.compare(line.size() - 3, 3, " ok") == 0)
            return true;
        at += marker.size();
    }
    return false;
}

void record(const std::string &marker, uint32_t season_index, const char *result)
{
    std::string line = marker + " season " + std::to_string(season_index) + " " + result + "\n";
    swofty::storage::Result r = swofty::storage::append(kAppliedPath, line);
    if (r != swofty::storage::Result::ok)
        swofty::log("[progressive] probe: could not record the result in %s (%s)", kAppliedPath,
                    swofty::storage::name(r));
}

void skip_once(const std::string &why)
{
    if (why == g_last_skip)
        return;
    g_last_skip = why;
    swofty::log("[progressive] probe: %s", why.c_str());
}

void roll_back(uintptr_t e, const std::vector<Plan> &written)
{
    size_t ok = 0;
    for (auto it = written.rbegin(); it != written.rend(); ++it) {
        std::vector<Row> rows;
        const Row *now = entrant_rows(e, rows) ? row_of(rows, it->driver) : nullptr;
        if (now && write_row(e, *now, own_data(it->row), it->row.rec.team_position, it->row.rec.car_number))
            ++ok;
    }
    swofty::log("[progressive] probe: wrote back %zu/%zu row(s)%s", ok, written.size(),
                ok == written.size() ? "" : "; RELOAD THE SAVE without saving");
}

void patch_weekend(uintptr_t e, const Plan &p)
{
    uintptr_t w = weekend_row(e, p.row.rec);
    if (!w) {
        swofty::log("[progressive] probe: %s weekend row: none (the next weekend copies the career rows when it starts)",
                    p.what.c_str());
        return;
    }
    EntrantRecord before{};
    swofty::mem::read(w, before);
    struct Field {
        size_t off;
        uint32_t career, planned, weekend;
    } fields[] = {
        {offsetof(EntrantRecord, driver_customisation), p.row.rec.driver_customisation, p.data.driver_customisation,
         before.driver_customisation},
        {offsetof(EntrantRecord, vehicle_ref), p.row.rec.vehicle_ref, p.data.vehicle_ref, before.vehicle_ref},
        {offsetof(EntrantRecord, driver_reg_value), p.row.rec.driver_reg_value, p.data.driver_reg_value,
         before.driver_reg_value},
        {offsetof(EntrantRecord, vehicle_customisation), p.row.rec.vehicle_customisation, p.data.vehicle_customisation,
         before.vehicle_customisation},
        {offsetof(EntrantRecord, badge_stat_id), p.row.rec.badge_stat_id, p.data.badge_stat_id, before.badge_stat_id},
    };
    size_t n = 0;
    bool ok = true;
    for (auto &f : fields)
        if (f.career != f.planned && f.weekend != f.planned) {
            ok = patch(w, f.off, f.planned) && ok;
            ++n;
        }
    if (p.row.rec.car_number != p.number && before.car_number != p.number) {
        ok = patch_u8(w, offsetof(EntrantRecord, car_number), p.number) && ok;
        ++n;
    }
    EntrantRecord after{};
    swofty::mem::read(w, after);
    swofty::log("[progressive] probe: %s weekend row 0x%llx: %zu field(s) %s; before %s", p.what.c_str(),
                static_cast<unsigned long long>(w), n, ok ? "patched" : "NOT all patched", row_text(before).c_str());
    swofty::log("[progressive] probe: %s weekend row after %s", p.what.c_str(), row_text(after).c_str());
}

} // namespace

void forget_career()
{
    g_last_skip.clear();
    g_refused.clear();
}

void probe(const swofty::career::State &state)
{
    if (!dev_probe || swofty::career::mode(state) != swofty::career::Mode::driver_career)
        return;
    if (!swofty::storage::available()) {
        skip_once("the core's storage library is unavailable; no probe");
        return;
    }
    std::string text;
    if (swofty::storage::read(kProbePath, text) != swofty::storage::Result::ok) {
        skip_once(std::string("no ") + kProbePath + " in swofty/addons/swofty.progressive/");
        return;
    }
    json::Value j;
    if (!json::parse(text, j) || j.type != json::Value::Obj) {
        skip_once(std::string(kProbePath) + " is not a JSON object");
        return;
    }
    if (!names::load()) {
        skip_once("the gamedb labels are not loaded yet");
        return;
    }
    uintptr_t e = entity();
    SeasonInfo season_now;
    if (!e || !season(e, season_now)) {
        skip_once("no career entity or championship");
        return;
    }
    std::string hash = hash_text(fnv64(text));
    std::string key = state.key[0] ? state.key : "unknown-career";
    std::string marker = hash + " " + key;
    bool repeat = j["repeat"].type == json::Value::Bool && j["repeat"].b;
    if (marker == g_refused)
        return;
    if (!repeat && already_applied(marker)) {
        skip_once("probe " + hash + " is already applied to " + key + " (delete " + kAppliedPath +
                  " or change the probe to apply it again)");
        return;
    }
    std::string fin = j["finalise"].str("auto");
    if (fin != "auto" && fin != "always" && fin != "never") {
        skip_once("finalise must be auto, always or never");
        return;
    }
    bool weekend = j["patch_weekend"].type != json::Value::Bool || j["patch_weekend"].b;
    const json::Value &js = j["seats"];
    if (js.type != json::Value::Arr || js.a.empty()) {
        skip_once("probe.json has no seats[]");
        return;
    }
    if (entity_busy(e)) {
        skip_once("the game is in the middle of an entrant update; trying again at the next Team HQ");
        return;
    }
    std::vector<Row> rows;
    if (!entrant_rows(e, rows)) {
        skip_once("the entrant rows are unreadable");
        return;
    }
    g_last_skip.clear();
    swofty::log("[progressive] probe %s: %zu entr%s for %s, season index %u, finalise %s, weekend rows %s", hash.c_str(),
                js.a.size(), js.a.size() == 1 ? "y" : "ies", key.c_str(), season_now.index, fin.c_str(),
                weekend ? "patched" : "left");

    std::vector<Plan> plans;
    for (size_t i = 0; i < js.a.size(); ++i) {
        Plan p;
        std::string err;
        if (js.a[i].type != json::Value::Obj || !plan_entry(e, rows, js.a[i], i, p, err)) {
            if (err.empty())
                err = "seats[" + std::to_string(i) + "] is not an object";
            swofty::log("[progressive] probe %s REFUSED, nothing written: %s", hash.c_str(), err.c_str());
            record(marker, season_now.index, "refused");
            g_refused = marker;
            return;
        }
        for (auto &q : plans)
            if (q.driver == p.driver) {
                swofty::log("[progressive] probe %s REFUSED, nothing written: %s names the same driver as %s",
                            hash.c_str(), p.what.c_str(), q.what.c_str());
                record(marker, season_now.index, "refused");
                g_refused = marker;
                return;
            }
        plans.push_back(p);
    }

    Baseline baseline(rows);
    std::vector<Plan> written;
    for (auto &p : plans) {
        swofty::log("[progressive] probe: %s before %s", p.what.c_str(), row_text(p.row.rec).c_str());
        if (unchanged(p)) {
            swofty::log("[progressive] probe: %s already holds these values; not written", p.what.c_str());
            continue;
        }
        std::vector<Row> now_rows;
        const Row *now = entrant_rows(e, now_rows) ? row_of(now_rows, p.driver) : nullptr;
        if (!now || std::memcmp(&now->rec, &p.row.rec, sizeof(EntrantRecord)) != 0 ||
            !write_row(e, *now, p.data, p.position, p.number)) {
            swofty::log("[progressive] probe: %s FAILED to write (row moved, changed or no unique identity)",
                        p.what.c_str());
            roll_back(e, written);
            record(marker, season_now.index, "failed");
            return;
        }
        written.push_back(p);
    }

    std::vector<Row> after;
    bool ok = entrant_rows(e, after) && after.size() == rows.size();
    size_t others = 0;
    for (size_t i = 0; ok && i < rows.size(); ++i) {
        const Plan *mine = nullptr;
        for (auto &p : written)
            if (p.row.at == rows[i].at)
                mine = &p;
        if (after[i].at != rows[i].at) {
            ok = false;
            break;
        }
        if (!mine) {
            if (std::memcmp(&after[i].rec, &rows[i].rec, sizeof(EntrantRecord)) != 0) {
                ++others;
                swofty::log("[progressive] probe: row %zu (%s) changed though it was not written: bytes %s", i,
                            names::key_text(Kind::driver, driver_of(rows[i].rec)).c_str(),
                            diff_offsets(rows[i].rec, after[i].rec).c_str());
            }
            continue;
        }
        EntrantRecord want = expected(*mine);
        const EntrantRecord &got = after[i].rec;
        bool fields = got.driver_customisation == want.driver_customisation && got.vehicle_ref == want.vehicle_ref &&
                      got.team_stat_id == want.team_stat_id && got.driver_reg_value == want.driver_reg_value &&
                      got.vehicle_customisation == want.vehicle_customisation &&
                      got.badge_stat_id == want.badge_stat_id && driver_of(got) == driver_of(want) &&
                      got.car_number == want.car_number;
        if (!fields) {
            ok = false;
            swofty::log("[progressive] probe: %s did not read back as planned: bytes %s differ", mine->what.c_str(),
                        diff_offsets(want, got).c_str());
        } else if (std::memcmp(&want, &got, sizeof want) != 0) {
            swofty::log("[progressive] probe: %s WARNING bytes %s differ from the plan outside the written fields",
                        mine->what.c_str(), diff_offsets(want, got).c_str());
        }
        swofty::log("[progressive] probe: %s after  %s", mine->what.c_str(), row_text(got).c_str());
    }
    if (!ok || others) {
        swofty::log("[progressive] probe %s FAILED: %s", hash.c_str(),
                    others ? "other rows changed" : "the written rows did not read back as planned");
        roll_back(e, written);
        record(marker, season_now.index, "failed");
        return;
    }

    if (weekend)
        for (auto &p : written)
            patch_weekend(e, p);

    uint64_t changes = lineup_changes(e, baseline);
    bool post = fin == "always" || (fin == "auto" && changes && changes != ~0ull);
    uint64_t posted = 0;
    bool fin_ok = true;
    if (post)
        fin_ok = finalise(e, baseline, posted);
    swofty::log("[progressive] probe %s applied: %zu row(s) written, %zu unchanged, %zu other rows unchanged; "
                "line-up changes %llu; finalise %s",
                hash.c_str(), written.size(), plans.size() - written.size(), rows.size() - written.size(),
                static_cast<unsigned long long>(changes),
                !post ? "not posted" : fin_ok ? "posted (MessageDriverMovesFinalised_upgrades, _recognition, plain)"
                                              : "FAILED to post");
    record(marker, season_now.index, "ok");
    report("after probe", state);
}

} // namespace pg
