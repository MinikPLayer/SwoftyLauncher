#pragma once

#include <swofty/json.hpp>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Registry Path fields reach the game through the core's file route: a game path the game opens (an engine
// bundle, audio/2025/bundles/<name>.bdl) is redirected, each time it is opened, to a file in the addon's own folder
// (<game>/swofty/addons/<addon>/<file>). A Path is relative to the folder of the addon that wrote it. Each
// redirect is a claim with a scope ("boot", or a content or session scope that put_back ends); when two addons
// claim the same game path, the later in load order wins, as files and mixins do. Pure: the core's glue
// (files.cpp) gives the Sink that checks files and installs routes, the host test gives its own.

namespace swofty_core::apply::files {

using Value = swofty::json::Value;

inline constexpr std::size_t GAME_PATH_MAX = 0x103;

struct Normal {
    std::string path, why;
    bool ok() const { return why.empty(); }
};

inline bool game_char(unsigned char c)
{
    return c >= 0x20 && c < 0x7f && std::string_view("<>|:?*()!\"").find(char(c)) == std::string_view::npos;
}

inline Normal clean(std::string_view in, bool lower, const char *what)
{
    Normal n;
    std::string s(in);
    std::replace(s.begin(), s.end(), '\\', '/');
    if (s.empty())
        return n.why = std::string(what) + " is empty", n;
    for (unsigned char c : s) {
        if (c >= 0x80)
            return n.why = std::string(what) + " '" + s + "' has a non-ASCII character; the game opens files by narrow paths", n;
        if (!game_char(c))
            return n.why = std::string(what) + " '" + s + "' has a character the game's paths reject (<>|:?*()!\" or a control character)", n;
    }
    if (s.front() == '/')
        return n.why = std::string(what) + " '" + s + "' is absolute; it must be relative", n;
    if (s.back() == '/')
        return n.why = std::string(what) + " '" + s + "' names a folder, not a file", n;
    std::string out;
    std::size_t at = 0;
    while (at <= s.size()) {
        std::size_t end = s.find('/', at);
        if (end == std::string::npos)
            end = s.size();
        std::string_view seg(s.data() + at, end - at);
        if (seg == "..")
            return n.why = std::string(what) + " '" + s + "' leaves its folder (..)", n;
        if (!seg.empty() && seg != ".") {
            if (!out.empty())
                out += '/';
            out += seg;
        }
        at = end + 1;
    }
    if (out.empty())
        return n.why = std::string(what) + " '" + s + "' names no file", n;
    if (lower)
        for (char &c : out)
            if (c >= 'A' && c <= 'Z')
                c = char(c + 32);
    n.path = std::move(out);
    return n;
}

inline Normal game_path(std::string_view in)
{
    std::string_view s = in;
    if (s.size() >= 6 && (s.substr(0, 6) == "/game/" || s.substr(0, 6) == "\\game\\"))
        s.remove_prefix(6);
    Normal n = clean(s, true, "the game path");
    if (n.ok() && (n.path == "swofty" || n.path.rfind("swofty/", 0) == 0))
        n.why = "the game path '" + n.path + "' is inside swofty/, which the file route never redirects";
    return n;
}

inline bool addon_id_ok(std::string_view id)
{
    if (id.empty() || id == "." || id == ".." || id == "core")
        return false;
    for (unsigned char c : id)
        if (c == '/' || c == '\\' || !game_char(c))
            return false;
    return true;
}

struct Resolved {
    std::string file, target, why;
    bool ok() const { return why.empty(); }
};

// A Path written by `addon`, as the file under the swofty folder it names: addons/<addon>/<file>.
inline Resolved resolve(std::string_view addon, std::string_view file)
{
    Resolved r;
    if (!addon_id_ok(addon))
        return r.why = "'" + std::string(addon) + "' is not an addon id with a folder", r;
    Normal n = clean(file, false, "the file");
    if (!n.ok())
        return r.why = n.why, r;
    r.file = n.path;
    r.target = "addons/" + std::string(addon) + "/" + n.path;
    return r;
}

struct Claim {
    std::string path, addon, file, target, registry, id, scope;
    std::size_t seq = 0;
};

struct Sink {
    std::string prefix = "swofty/";
    std::function<bool(const std::string &target)> exists;
    std::function<bool(const std::string &game_path)> in_game;
    std::function<bool(const std::string &game_path, const Claim *winner, std::string &why)> route;
};

enum class Kind { routed, shadowed, kept, missing, invalid, unavailable };

inline const char *kind_name(Kind k)
{
    switch (k) {
    case Kind::routed:
        return "routed";
    case Kind::shadowed:
        return "shadowed";
    case Kind::kept:
        return "kept";
    case Kind::missing:
        return "missing";
    case Kind::invalid:
        return "invalid";
    case Kind::unavailable:
        return "unavailable";
    }
    return "?";
}

struct Outcome {
    Kind kind = Kind::invalid;
    std::string path, addon, line, conflict;
    bool refused() const { return kind == Kind::missing || kind == Kind::invalid || kind == Kind::unavailable; }
};

struct Conflict {
    std::string path, registry, id, wins;
    std::vector<std::string> addons;
};

class Router {
public:
    explicit Router(Sink sink = {}) : sink_(std::move(sink)) {}

    void set_sink(Sink sink) { sink_ = std::move(sink); }
    void set_order(std::vector<std::string> order) { order_ = std::move(order); }
    const std::string &prefix() const { return sink_.prefix; }

    Outcome redirect(std::string_view raw_path, const std::string &addon, std::string_view file,
                     const std::string &registry, const std::string &id, const std::string &scope)
    {
        Outcome o;
        o.addon = addon;
        std::string who = registry + " " + id;
        Normal p = game_path(raw_path);
        if (!p.ok()) {
            o.line = who + ": not redirected: " + p.why;
            return o;
        }
        o.path = p.path;
        Resolved r = resolve(addon, file);
        if (!r.ok()) {
            o.line = who + ": " + o.path + " not redirected: " + r.why;
            return o;
        }
        std::string shown = addon + "/" + r.file;
        if (6 + sink_.prefix.size() + r.target.size() > GAME_PATH_MAX) {
            o.line = who + ": " + o.path + " not redirected: /game/" + sink_.prefix + r.target + " is longer than the " +
                     std::to_string(GAME_PATH_MAX) + " characters the game's paths hold";
            return o;
        }
        if (sink_.exists && !sink_.exists(r.target)) {
            bool there = sink_.in_game && sink_.in_game(o.path);
            o.kind = there ? Kind::kept : Kind::missing;
            o.line = who + ": " + sink_.prefix + r.target + " is not installed; " +
                     (there ? "the game folder's " + o.path + " is used (an older build operation put it there)"
                            : o.path + " is not redirected and the game folder has no such file");
            return o;
        }
        auto &list = claims_[o.path];
        for (auto &held : list)
            if (held.addon == addon && held.target == r.target && held.scope == scope && held.id == id) {
                const Claim *w = winner(o.path);
                bool mine = w && w->seq == held.seq;
                o.kind = mine ? Kind::routed : Kind::shadowed;
                o.line = mine ? "redirect " + o.path + " -> " + shown + " (" + who + ")"
                              : who + ": " + o.path + " -> " + shown + " is not used; " + w->addon + "'s " + w->file +
                                    " wins (later in load order)";
                return o;
            }
        Claim c{o.path, addon, r.file, r.target, registry, id, scope, ++seq_};
        const Claim *before = winner(o.path);
        Claim before_copy = before ? *before : Claim{};
        bool had = before != nullptr;
        list.push_back(c);
        const Claim *now = winner(o.path);
        bool mine = now && now->seq == c.seq;
        if (mine && (!had || before_copy.target != now->target || before_copy.registry != now->registry ||
                     before_copy.id != now->id)) {
            std::string why;
            if (sink_.route && !sink_.route(o.path, now, why)) {
                list.pop_back();
                if (list.empty())
                    claims_.erase(o.path);
                o.kind = Kind::unavailable;
                o.line = who + ": " + o.path + " not redirected: " + why;
                return o;
            }
        }
        o.kind = mine ? Kind::routed : Kind::shadowed;
        o.line = mine ? "redirect " + o.path + " -> " + shown + " (" + who + ")"
                      : who + ": " + o.path + " -> " + shown + " is not used; " + now->addon + "'s " + now->file +
                            " wins (later in load order)";
        if (auto x = conflict_at(o.path))
            o.conflict = conflict_line(*x);
        return o;
    }

    std::vector<std::string> put_back(const std::string &scope)
    {
        std::vector<std::string> lines;
        std::vector<std::string> paths;
        for (auto &[p, list] : claims_)
            for (auto &c : list)
                if (c.scope == scope) {
                    paths.push_back(p);
                    break;
                }
        for (auto &p : paths) {
            const Claim *before = winner(p);
            Claim was = before ? *before : Claim{};
            auto &list = claims_[p];
            list.erase(std::remove_if(list.begin(), list.end(), [&](const Claim &c) { return c.scope == scope; }),
                       list.end());
            const Claim *now = winner(p);
            if (now && now->target == was.target && now->id == was.id)
                continue;
            std::string why;
            bool ok = !sink_.route || sink_.route(p, now, why);
            lines.push_back("put back " + p + ": " +
                            (now ? "now " + now->addon + "/" + now->file + " (" + now->registry + " " + now->id + ")"
                                 : std::string("the game's own file again")) +
                            (ok ? "" : "; the route could not change: " + why));
            if (list.empty())
                claims_.erase(p);
        }
        return lines;
    }

    const Claim *winner(const std::string &path) const
    {
        auto it = claims_.find(path);
        if (it == claims_.end() || it->second.empty())
            return nullptr;
        const Claim *best = nullptr;
        for (auto &c : it->second)
            if (!best || rank(c.addon) > rank(best->addon) || (rank(c.addon) == rank(best->addon) && c.seq > best->seq))
                best = &c;
        return best;
    }

    std::vector<Conflict> conflicts() const
    {
        std::vector<Conflict> out;
        for (auto &[p, list] : claims_)
            if (auto c = conflict_at(p))
                out.push_back(*c);
        return out;
    }

    Value conflicts_json() const
    {
        Value list = Value::array();
        for (auto &c : conflicts()) {
            Value e = Value::object();
            e["kind"] = Value("file");
            e["registry"] = Value(c.registry);
            e["id"] = Value(c.id);
            e["path"] = Value(c.path);
            Value a = Value::array();
            for (auto &x : c.addons)
                a.push_back(Value(x));
            e["addons"] = a;
            e["wins"] = Value(c.wins);
            list.push_back(e);
        }
        return list;
    }

    std::size_t size() const
    {
        std::size_t n = 0;
        for (auto &[p, list] : claims_)
            n += winner(p) != nullptr;
        return n;
    }

    void clear()
    {
        claims_.clear();
        seq_ = 0;
    }

    static std::string conflict_line(const Conflict &c)
    {
        std::string who;
        for (auto &a : c.addons)
            who += (who.empty() ? "" : ", ") + a;
        return "file conflict: " + c.path + " is redirected by " + who + "; " + c.wins + " wins (" + c.registry + " " +
               c.id + ", later in load order; reorder addons to change it)";
    }

private:
    std::size_t rank(const std::string &addon) const
    {
        auto it = std::find(order_.begin(), order_.end(), addon);
        return it == order_.end() ? order_.size() : std::size_t(it - order_.begin());
    }

    std::optional<Conflict> conflict_at(const std::string &path) const
    {
        auto it = claims_.find(path);
        const Claim *w = winner(path);
        if (it == claims_.end() || !w)
            return std::nullopt;
        bool other = false;
        for (auto &c : it->second)
            other = other || (c.addon != w->addon && c.target != w->target);
        if (!other)
            return std::nullopt;
        Conflict x{path, w->registry, w->id, w->addon, {}};
        std::vector<const Claim *> all;
        for (auto &c : it->second)
            all.push_back(&c);
        std::stable_sort(all.begin(), all.end(), [&](const Claim *a, const Claim *b) { return rank(a->addon) < rank(b->addon); });
        for (auto *c : all)
            if (std::find(x.addons.begin(), x.addons.end(), c->addon) == x.addons.end())
                x.addons.push_back(c->addon);
        return x;
    }

    Sink sink_;
    std::vector<std::string> order_;
    std::map<std::string, std::vector<Claim>> claims_;
    std::size_t seq_ = 0;
};

// The addon whose value a record's field holds: the layer or variant that last changed it, else the record's owner.
inline std::string owner_of_field(const std::map<std::string, std::string> &origins, const std::string &owner,
                                  const std::string &pointer)
{
    auto it = origins.find(pointer);
    if (it != origins.end())
        return it->second;
    std::string best, who;
    for (auto &[p, a] : origins)
        if ((p.empty() || pointer.rfind(p + "/", 0) == 0) && p.size() >= best.size() && !a.empty())
            best = p, who = a;
    return who.empty() ? owner : who;
}

}
