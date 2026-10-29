#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <swofty/json.hpp>

// The facts every handler of every event and hook point sees: the moment's mode, content, career,
// weekend, session, player and participants. The core keeps one current Facts (set from its lifecycle
// moments) and hands each fire an immutable snapshot of it. Each part crosses the C ABI as the JSON
// shape of the C++ views in swofty/events.hpp; record:<registry> comes from a resolver the registry
// installs. Selectors (the JSON arrays of swofty_api.h) are compiled once and evaluated against a
// snapshot.

namespace swofty_core::hooks {

struct Entrant {
    uint32_t car = 0;
    std::string driver, team, vehicle;
    bool human = false;
};

struct Facts {
    std::string mode;
    std::string content, content_registry, content_owner;
    std::vector<std::string> chain, owners;
    struct Career {
        std::string id, data_group;
        int year = 0, season = 0, round = 0, rounds = 0, day = 0;
    };
    struct Weekend {
        int round = 0;
        std::string track;
    };
    struct Session {
        std::string type, session, track, setup_mode;
        int group = 0, weather = 0, laps = 0;
    };
    std::optional<Career> career;
    std::optional<Weekend> weekend;
    std::optional<Session> session;
    std::optional<Entrant> player;
    std::vector<Entrant> participants;
    std::vector<std::string> vehicle_packages;
    uint64_t epoch = 0;
};

using RecordFn = std::function<std::string(const Facts &, std::string_view registry)>;

inline swofty::json::Value entrant_json(const Entrant &e)
{
    auto ref = [](const std::string &id) { return id.empty() ? swofty::json::Value() : swofty::json::Value(id); };
    return swofty::json::Value::object({{"car", swofty::json::Value(int64_t(e.car))},
                                        {"driver", ref(e.driver)},
                                        {"team", ref(e.team)},
                                        {"vehicle", ref(e.vehicle)},
                                        {"human", swofty::json::Value(e.human)}});
}

inline std::string part_json(const Facts &f, std::string_view part, const RecordFn &records)
{
    using swofty::json::Value;
    auto strings = [](const std::vector<std::string> &v) {
        Value a = Value::array();
        for (auto &s : v)
            a.push_back(Value(s));
        return a;
    };
    auto ref = [](const std::string &id) { return id.empty() ? Value() : Value(id); };
    if (part == "mode")
        return swofty::json::dump(Value(f.mode.empty() ? std::string("unknown") : f.mode));
    if (part == "content")
        return swofty::json::dump(Value::object({{"id", Value(f.content)},
                                                 {"registry", Value(f.content_registry)},
                                                 {"owner", Value(f.content_owner)},
                                                 {"chain", strings(f.chain)},
                                                 {"owners", strings(f.owners)}}));
    if (part == "career") {
        if (!f.career)
            return "null";
        auto &c = *f.career;
        return swofty::json::dump(Value::object({{"id", Value(c.id)},
                                                 {"data_group", Value(c.data_group)},
                                                 {"year", Value(c.year)},
                                                 {"season", Value(c.season)},
                                                 {"round", Value(c.round)},
                                                 {"rounds", Value(c.rounds)},
                                                 {"day", Value(c.day)}}));
    }
    if (part == "weekend") {
        if (!f.weekend)
            return "null";
        return swofty::json::dump(Value::object({{"round", Value(f.weekend->round)}, {"track", ref(f.weekend->track)}}));
    }
    if (part == "session") {
        if (!f.session)
            return "null";
        auto &s = *f.session;
        return swofty::json::dump(Value::object({{"type", Value(s.type.empty() ? std::string("practice") : s.type)},
                                                 {"session", Value(s.session)},
                                                 {"group", Value(s.group)},
                                                 {"track", ref(s.track)},
                                                 {"weather", Value(s.weather)},
                                                 {"laps", Value(s.laps)},
                                                 {"setup_mode", Value(s.setup_mode)}}));
    }
    if (part == "player")
        return f.player ? swofty::json::dump(entrant_json(*f.player)) : std::string("null");
    if (part == "participants") {
        Value a = Value::array();
        for (auto &e : f.participants)
            a.push_back(entrant_json(e));
        return swofty::json::dump(a);
    }
    if (part.starts_with("record:")) {
        std::string out = records ? records(f, part.substr(7)) : std::string();
        return out.empty() ? std::string("null") : out;
    }
    return {};
}

inline std::string summary(const Facts &f)
{
    std::string s = f.mode.empty() ? std::string("unknown") : f.mode;
    if (!f.content.empty())
        s += ", " + f.content;
    if (f.career && f.career->year)
        s += ", " + std::to_string(f.career->year);
    return s;
}

// A compiled selector array: every entry must match. Entries are the shapes swofty_api.h lists; an
// empty object matches everywhere, an unknown key makes compile() fail.
class Selectors {
public:
    enum class Op { content, content_exact, owner, data_group, mode, session_type, track, year, setup_mode,
                    vehicle_package, any, not_, all };
    struct Node {
        Op op = Op::all;
        std::vector<std::string> values;
        int from = 0, to = 0;
        std::vector<Node> kids;
    };

    bool empty() const { return root_.kids.empty(); }
    const std::string &text() const { return text_; }

    static bool compile(std::string_view json, Selectors &out, std::string *why)
    {
        out = Selectors();
        if (json.empty())
            return true;
        swofty::json::Error err;
        swofty::json::Value v = swofty::json::parse(json, &err);
        if (!err.ok()) {
            if (why)
                *why = "the selector array is not JSON: " + err.message;
            return false;
        }
        if (v.is_null())
            return true;
        if (!v.is_array()) {
            if (why)
                *why = "the selectors are not a JSON array";
            return false;
        }
        for (auto &e : *v.if_array()) {
            Node n;
            if (!entry(e, n, why))
                return false;
            out.root_.kids.push_back(std::move(n));
        }
        out.text_ = std::string(json);
        return true;
    }

    bool match(const Facts &f) const { return eval(root_, f); }

private:
    static bool strings(const swofty::json::Value &v, std::vector<std::string> &out)
    {
        if (auto s = v.if_string()) {
            out.push_back(*s);
            return true;
        }
        auto a = v.if_array();
        if (!a)
            return false;
        for (auto &x : *a) {
            auto s = x.if_string();
            if (!s)
                return false;
            out.push_back(*s);
        }
        return true;
    }

    static bool entry(const swofty::json::Value &v, Node &n, std::string *why)
    {
        auto o = v.if_object();
        if (!o) {
            if (why)
                *why = "a selector entry is not an object";
            return false;
        }
        n.op = Op::all;
        for (auto &[key, val] : *o) {
            Node k;
            static const std::pair<const char *, Op> named[] = {
                {"content", Op::content},     {"content_exact", Op::content_exact}, {"owner", Op::owner},
                {"data_group", Op::data_group}, {"mode", Op::mode},                 {"session_type", Op::session_type},
                {"track", Op::track},         {"setup_mode", Op::setup_mode},       {"vehicle_package", Op::vehicle_package}};
            bool known = false;
            for (auto &[name, op] : named)
                if (key == name) {
                    known = true;
                    k.op = op;
                    if (!strings(val, k.values)) {
                        if (why)
                            *why = "selector \"" + key + "\" takes a string or an array of strings";
                        return false;
                    }
                }
            if (!known && key == "year") {
                known = true;
                k.op = Op::year;
                auto a = val.if_array();
                if (val.is_int()) {
                    k.from = k.to = int(val.as_int());
                } else if (a && a->size() == 2 && (*a)[0].is_int() && (*a)[1].is_int()) {
                    k.from = int((*a)[0].as_int());
                    k.to = int((*a)[1].as_int());
                } else {
                    if (why)
                        *why = "selector \"year\" takes [from, to] or a year";
                    return false;
                }
            }
            if (!known && (key == "any" || key == "not")) {
                known = true;
                k.op = key == "any" ? Op::any : Op::not_;
                if (key == "any") {
                    auto a = val.if_array();
                    if (!a) {
                        if (why)
                            *why = "selector \"any\" takes an array of selectors";
                        return false;
                    }
                    for (auto &x : *a) {
                        Node c;
                        if (!entry(x, c, why))
                            return false;
                        k.kids.push_back(std::move(c));
                    }
                } else {
                    Node c;
                    if (!entry(val, c, why))
                        return false;
                    k.kids.push_back(std::move(c));
                }
            }
            if (!known) {
                if (why)
                    *why = "unknown selector \"" + key + "\"";
                return false;
            }
            n.kids.push_back(std::move(k));
        }
        return true;
    }

    static bool has(const std::vector<std::string> &list, const std::string &v)
    {
        for (auto &x : list)
            if (x == v)
                return true;
        return false;
    }

    static bool any_of(const std::vector<std::string> &want, const std::string &v)
    {
        return !v.empty() && has(want, v);
    }

    static bool eval(const Node &n, const Facts &f)
    {
        switch (n.op) {
        case Op::all:
            for (auto &k : n.kids)
                if (!eval(k, f))
                    return false;
            return true;
        case Op::any:
            for (auto &k : n.kids)
                if (eval(k, f))
                    return true;
            return false;
        case Op::not_:
            return !eval(n.kids[0], f);
        case Op::content:
            for (auto &w : n.values)
                if ((!f.content.empty() && f.content == w) || has(f.chain, w))
                    return true;
            return false;
        case Op::content_exact:
            return any_of(n.values, f.content);
        case Op::owner:
            for (auto &w : n.values)
                if (f.content_owner == w || has(f.owners, w))
                    return true;
            return false;
        case Op::data_group:
            return f.career && any_of(n.values, f.career->data_group);
        case Op::mode:
            return any_of(n.values, f.mode);
        case Op::session_type:
            return f.session && any_of(n.values, f.session->type);
        case Op::track:
            return (f.session && any_of(n.values, f.session->track)) || (f.weekend && any_of(n.values, f.weekend->track));
        case Op::year:
            return f.career && f.career->year >= n.from && f.career->year <= n.to;
        case Op::setup_mode:
            return f.session && any_of(n.values, f.session->setup_mode);
        case Op::vehicle_package:
            for (auto &w : n.values)
                if (has(f.vehicle_packages, w))
                    return true;
            return false;
        }
        return false;
    }

    Node root_;
    std::string text_;
};

// One fire's context, the object a SwoftyHookCall's context points at. It holds a snapshot of the
// facts and caches each JSON part the first time a handler asks for it.
class Context {
public:
    Context(std::shared_ptr<const Facts> facts, const RecordFn *records) : facts_(std::move(facts)), records_(records) {}

    const Facts &facts() const { return *facts_; }

    const std::string &part(std::string_view name) const
    {
        std::lock_guard<std::mutex> g(lock_);
        for (auto &p : parts_)
            if (p.first == name)
                return p.second;
        parts_.push_back({std::string(name), part_json(*facts_, name, records_ ? *records_ : RecordFn())});
        return parts_.back().second;
    }

private:
    std::shared_ptr<const Facts> facts_;
    const RecordFn *records_;
    mutable std::mutex lock_;
    mutable std::deque<std::pair<std::string, std::string>> parts_;
};

}
