#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "swofty_api.h"

#include "catalogue.hpp"
#include "context.hpp"

// Hook points and events (standard library 1.6). A point is a name with a kind (notify: after; modify:
// before and after; cancel: before, replace and after), a payload version and, for an addon's point,
// the addon that defined it and the scope it fires in. Handlers attach to a phase with a priority and
// a selector array. fire() runs before (priority, then load order, the content owner's last), replace
// (the content owner's first, then priority and load order, until one returns non-zero), the game's
// own code when a game point passes one and nothing replaced it, then after (priority, then load
// order). A faulting after handler is detached and reported; the Platform decides what a fault in
// before or replace does (the core lets it crash the game, naming the addon). Everything the core
// needs from Windows comes in through Platform, so the host tests run this header unchanged.

namespace swofty_core::hooks {

struct Handler {
    uint32_t token = 0;
    std::string addon;
    uint32_t phase = SWOFTY_PHASE_AFTER;
    int32_t priority = 0;
    uint64_t order = 0;
    Selectors scope;
    SwoftyHookFn fn = nullptr;
    void *user = nullptr;
    uint64_t module = 0;
    std::atomic<bool> live{true};
    uint32_t probe = 0;
};

struct Fault {
    bool faulted = false;
    std::string what;
};

// What a game point's detour needs from the mixin layer: install on the first subscriber, uninstall
// on the last. install returns false when the point stays unavailable (and says why in its own log).
struct GameLink {
    virtual ~GameLink() = default;
    virtual bool install(bool late, const std::string &subscribers) = 0;
    virtual void uninstall() = 0;
    virtual bool installed() const = 0;
};

struct Platform {
    std::function<void(const std::string &)> log;
    std::function<int(const Handler &, SwoftyHookCall *, bool isolate, Fault *)> call;
    std::function<void(const std::string &point, const char *phase, const std::string &addon, const std::string &what)>
        faulted;
    std::function<void(Handler &, const std::string &point)> attached;
    // An addon's place in load order (1 = first; 0 = unknown, ranked after every known addon in the
    // order it first attached).
    std::function<uint64_t(const std::string &addon, uint64_t module)> load_order;
    RecordFn records;
    // Every event fire, before delivery (the test agent's waits).
    std::function<void(const std::string &event, bool hot)> fired;
};

// A direct point's handlers by phase, already ordered (priority, then load order), rebuilt when a handler
// attaches or detaches. fire_direct reads it with one short lock and builds no context.
struct DirectLine {
    std::vector<std::shared_ptr<Handler>> before, replace, after;
};

struct Point {
    std::string name;
    uint32_t kind = SWOFTY_POINT_CANCEL;
    bool event = false, defined = false, core = false, hot = false, after_only = false, unfired = false;
    uint32_t payload_version = 0;
    std::string owner;
    Selectors scope;
    std::vector<std::shared_ptr<Handler>> handlers;
    std::atomic<bool> fired{false};
    GameLink *game = nullptr;
    std::shared_ptr<const DirectLine> line;
    bool line_dirty = true;
};

class Table {
public:
    explicit Table(Platform p = {}) : p_(std::move(p))
    {
        if (!p_.call)
            p_.call = [](const Handler &h, SwoftyHookCall *c, bool, Fault *) { return h.fn(h.user, c); };
        facts_ = std::make_shared<const Facts>();
    }

    void declare_core()
    {
        for (auto &e : catalogue::EVENTS)
            declare(e.id, e.kind, e.version, true, e.hot, e.fired == catalogue::Fired::after_only,
                    e.fired == catalogue::Fired::no || e.fired == catalogue::Fired::game);
    }

    void declare(const std::string &name, uint32_t kind, uint32_t version, bool event, bool hot, bool after_only = false,
                 bool unfired = false)
    {
        std::vector<std::string> refused;
        {
            std::lock_guard<std::mutex> g(lock_);
            Point &p = point(name);
            p.kind = kind, p.payload_version = version, p.event = event, p.hot = hot, p.after_only = after_only;
            p.unfired = unfired;
            p.defined = p.core = true;
            refused = prune(p);
        }
        for (auto &line : refused)
            log(line);
    }

    // A game hook point: a core point whose detour sits on a game function, installed while it has
    // subscribers (from the window stage on).
    void declare_game(const std::string &name, uint32_t kind, uint32_t version, GameLink *link, bool hot = false)
    {
        declare(name, kind, version, false, hot);
        bool install = false;
        std::string subs;
        {
            std::lock_guard<std::mutex> g(lock_);
            Point &p = point(name);
            p.game = link;
            install = armed_ && !p.handlers.empty();
            subs = subscribers(p);
        }
        if (install)
            link->install(true, subs);
    }

    int define(const std::string &addon, const std::string &name, uint32_t version, uint32_t kind, bool event,
               std::string_view scope_json)
    {
        const char *tag = event ? "[events]" : "[hooks]";
        if (kind > SWOFTY_POINT_CANCEL || name.empty())
            return refuse(std::string(tag) + " define " + name + " by " + who(addon) + " refused: no such kind");
        if (addon.empty() || name.size() <= addon.size() + 1 || name.compare(0, addon.size(), addon) ||
            name[addon.size()] != '.')
            return refuse(std::string(tag) + " define " + name + " by " + who(addon) +
                          " refused: an addon defines points under its own id (" + who(addon) + ".*)");
        Selectors scope;
        std::string why;
        if (!Selectors::compile(scope_json, scope, &why))
            return refuse(std::string(tag) + " define " + name + " by " + addon + " refused: " + why);
        std::vector<std::string> refused;
        {
            std::lock_guard<std::mutex> g(lock_);
            Point &p = point(name);
            if (p.core)
                return refuse_locked(std::string(tag) + " define " + name + " by " + addon + " refused: a core point");
            if (p.defined && p.owner != addon)
                return refuse_locked(std::string(tag) + " define " + name + " by " + addon + " refused: " + p.owner +
                                     " defined it first");
            p.defined = true;
            p.owner = addon;
            p.kind = kind;
            p.event = event;
            p.payload_version = std::max(p.payload_version, version);
            p.scope = std::move(scope);
            refused = prune(p);
        }
        for (auto &line : refused)
            log(line);
        log(std::string(tag) + " define " + name + " (" + catalogue::kind_name(kind) + ", payload v" +
            std::to_string(version) + (scope_json.empty() || scope_json == "[]" ? "" : ", scope " + std::string(scope_json)) +
            ") by " + addon + " -> ok");
        return SWOFTY_ASSET_OK;
    }

    int attach(const std::string &addon, const std::string &name, uint32_t phase, int32_t priority,
               std::string_view selectors, SwoftyHookFn fn, void *user, uint32_t *token, uint64_t module = 0)
    {
        if (!fn || name.empty() || phase > SWOFTY_PHASE_AFTER)
            return refuse("[hooks] attach to " + name + " by " + who(addon) + " refused: no handler, point or phase");
        auto h = std::make_shared<Handler>();
        std::string why;
        if (!Selectors::compile(selectors, h->scope, &why))
            return refuse("[hooks] attach " + std::string(catalogue::phase_name(phase)) + " on " + name + " by " +
                          who(addon) + " refused: " + why);
        h->addon = addon, h->phase = phase, h->priority = priority, h->fn = fn, h->user = user, h->module = module;
        GameLink *install = nullptr;
        std::string line, subs;
        {
            std::lock_guard<std::mutex> g(lock_);
            Point &p = point(name);
            if (p.defined && !catalogue::has_phase(p.kind, phase))
                return refuse_locked(no_phase(p, phase, addon));
            h->token = ++next_token_;
            h->order = rank(addon, module) << 32 | ++next_order_;
            p.handlers.push_back(h);
            p.line_dirty = true;
            tokens_[h->token] = name;
            if (p.game && p.handlers.size() == 1 && armed_ && !p.game->installed())
                install = p.game;
            subs = subscribers(p);
            const char *tag = p.event ? "[events]" : "[hooks]";
            line = std::string(tag) + " attach " + catalogue::phase_name(phase) + " on " + name + " by " + who(addon) +
                   (priority ? " (priority " + std::to_string(priority) + ")" : "") +
                   (h->scope.empty() ? "" : " in " + h->scope.text()) + " -> ok token " + std::to_string(h->token);
            if (!p.defined)
                line += " (nothing defines " + name + " yet; held until it is)";
            else if (p.unfired)
                line += "; the core doesn't fire " + name + " in this version, so live() reports it unavailable";
            else if (p.after_only && phase != SWOFTY_PHASE_AFTER)
                line += "; the core runs only after handlers on " + name + " in this version";
            else if (p.game && !armed_)
                line += " (the point installs at the window stage)";
        }
        if (p_.attached)
            p_.attached(*h, name);
        if (token)
            *token = h->token;
        log(line);
        if (install)
            install->install(true, subs);
        return SWOFTY_ASSET_OK;
    }

    int detach(uint32_t token)
    {
        GameLink *uninstall = nullptr;
        {
            std::lock_guard<std::mutex> g(lock_);
            auto t = tokens_.find(token);
            if (t == tokens_.end())
                return SWOFTY_ASSET_INVALID;
            Point &p = point(t->second);
            tokens_.erase(t);
            for (size_t i = 0; i < p.handlers.size(); i++)
                if (p.handlers[i]->token == token) {
                    p.handlers[i]->live = false;
                    p.handlers.erase(p.handlers.begin() + i);
                    p.line_dirty = true;
                    break;
                }
            if (p.game && p.handlers.empty() && p.game->installed())
                uninstall = p.game;
        }
        if (uninstall)
            uninstall->uninstall();
        return SWOFTY_ASSET_OK;
    }

    size_t drop_module(uint64_t module)
    {
        std::vector<uint32_t> tokens;
        {
            std::lock_guard<std::mutex> g(lock_);
            for (auto &[name, p] : points_)
                for (auto &h : p->handlers)
                    if (module && h->module == module)
                        tokens.push_back(h->token);
        }
        for (auto t : tokens)
            detach(t);
        return tokens.size();
    }

    // The window stage, after the mixin conflict pass: every game point with a subscriber installs.
    void arm()
    {
        std::vector<std::pair<GameLink *, std::string>> todo;
        {
            std::lock_guard<std::mutex> g(lock_);
            if (armed_)
                return;
            armed_ = true;
            for (auto &[name, p] : points_)
                if (p->game && !p->handlers.empty() && !p->game->installed())
                    todo.push_back({p->game, subscribers(*p)});
        }
        for (auto &[link, subs] : todo)
            link->install(false, subs);
    }

    bool armed()
    {
        std::lock_guard<std::mutex> g(lock_);
        return armed_;
    }

    int live(const std::string &name, uint32_t *version)
    {
        std::lock_guard<std::mutex> g(lock_);
        auto it = points_.find(name);
        if (it == points_.end() || !it->second->defined)
            return SWOFTY_ASSET_UNAVAILABLE;
        Point &p = *it->second;
        if (version)
            *version = p.payload_version;
        if (p.game && !p.game->installed())
            return p.handlers.empty() || armed_ ? SWOFTY_ASSET_UNAVAILABLE : SWOFTY_ASSET_LATE;
        if (p.unfired)
            return SWOFTY_ASSET_UNAVAILABLE;
        return SWOFTY_ASSET_OK;
    }

    // by: the firing addon ("" for the core). original runs between replace and after when no replace
    // handler handled the call. facts overrides the current facts for this one fire.
    int fire(const std::string &by, const std::string &name, void *payload, int *replaced,
             const std::function<void()> &original = {}, std::shared_ptr<const Facts> facts = nullptr)
    {
        if (replaced)
            *replaced = 0;
        if (!payload || *static_cast<const uint32_t *>(payload) < 8)
            return refuse("[hooks] fire " + name + " by " + who(by) +
                          " refused: the payload must start with uint32_t size (>= 8) and payload_version");
        std::shared_ptr<Point> pp;
        std::vector<std::shared_ptr<Handler>> all;
        bool first = false;
        {
            std::lock_guard<std::mutex> g(lock_);
            auto it = points_.find(name);
            if (it == points_.end() || !it->second->defined)
                return refuse_locked("[hooks] fire " + name + " by " + who(by) + " refused: nothing defines " + name);
            pp = it->second;
            if (!by.empty() && (pp->core || pp->owner != by))
                return refuse_locked("[hooks] fire " + name + " by " + by + " refused: only " +
                                     (pp->core ? std::string("the core") : pp->owner) + " fires it");
            all = pp->handlers;
            first = !pp->fired.exchange(true);
        }
        Point &p = *pp;
        for (auto *a : active())
            if (a == &p)
                return refuse("[hooks] fire " + name + " refused: a handler fired the point it runs on");
        auto snap = facts ? std::move(facts) : current();
        if (p.event && p_.fired)
            p_.fired(name, p.hot);
        const char *tag = p.event ? "[events]" : "[hooks]";
        if (!p.scope.empty() && !p.scope.match(*snap)) {
            log(std::string(tag) + " " + name + " fired outside its scope (" + where(*snap) + "); not delivered");
            return SWOFTY_ASSET_OK;
        }
        auto ctx = std::make_shared<Context>(snap, &p_.records);
        LiveContext live_ctx(*this, ctx);
        Active on(&p);

        struct Pick {
            Handler *h;
            bool owner;
        };
        std::vector<Pick> before, replace, after;
        for (auto &h : all) {
            if (!h->live || !catalogue::has_phase(p.kind, h->phase))
                continue;
            if (p.after_only && h->phase != SWOFTY_PHASE_AFTER)
                continue;
            if (!h->scope.empty() && !h->scope.match(*snap))
                continue;
            bool owner = is_owner(*snap, h->addon);
            (h->phase == SWOFTY_PHASE_BEFORE ? before : h->phase == SWOFTY_PHASE_REPLACE ? replace : after)
                .push_back({h.get(), owner});
        }
        auto ordered = [](const Pick &a, const Pick &b) {
            return a.h->priority != b.h->priority ? a.h->priority > b.h->priority : a.h->order < b.h->order;
        };
        std::stable_sort(before.begin(), before.end(),
                         [&](const Pick &a, const Pick &b) { return a.owner != b.owner ? b.owner : ordered(a, b); });
        std::stable_sort(replace.begin(), replace.end(),
                         [&](const Pick &a, const Pick &b) { return a.owner != b.owner ? a.owner : ordered(a, b); });
        std::stable_sort(after.begin(), after.end(), ordered);

        SwoftyHookCall call{sizeof(SwoftyHookCall), SWOFTY_PHASE_BEFORE, p.name.c_str(), payload, 0, ctx.get()};
        size_t ran_before = 0, ran_replace = 0, ran_after = 0;
        for (auto &x : before) {
            if (!x.h->live)
                continue;
            call.phase = SWOFTY_PHASE_BEFORE;
            Fault f;
            p_.call(*x.h, &call, false, &f);
            ran_before++;
        }
        const Pick *handled = nullptr;
        size_t handled_at = 0;
        for (size_t i = 0; i < replace.size(); i++) {
            auto &x = replace[i];
            if (!x.h->live)
                continue;
            call.phase = SWOFTY_PHASE_REPLACE;
            Fault f;
            ran_replace++;
            if (p_.call(*x.h, &call, false, &f)) {
                handled = &x;
                handled_at = i;
                break;
            }
        }
        call.replaced = handled ? 1 : 0;
        if (!handled && original)
            original();
        std::vector<std::string> faults;
        for (auto &x : after) {
            if (!x.h->live)
                continue;
            call.phase = SWOFTY_PHASE_AFTER;
            Fault f;
            p_.call(*x.h, &call, true, &f);
            ran_after++;
            if (f.faulted) {
                detach(x.h->token);
                x.h->live = false;
                faults.push_back(p.event ? std::string(tag) + " after handler from " + who(x.h->addon) + " faulted on " +
                                               name + " (" + f.what + "); handler detached for this session"
                                         : std::string(tag) + " " + name + ": after handler from " + who(x.h->addon) +
                                               " faulted (" + f.what + "); handler detached for this session");
                if (p_.faulted)
                    p_.faulted(name, "after", x.h->addon, f.what);
            }
        }
        if (replaced)
            *replaced = call.replaced;
        if (p.event ? (!p.hot || first) : first) {
            std::string line = std::string(tag) + " " + name + (p.event ? " (" + summary(*snap) + ")" : "") + ": ";
            if (p.hot || !p.event)
                line += "first fire, ";
            line += counts(ran_before, ran_replace, ran_after);
            if (handled) {
                line += "; replace by " + who(handled->h->addon) + (handled->owner ? " (owner)" : "") + " -> handled";
                std::string skipped;
                size_t n = 0;
                for (size_t i = handled_at + 1; i < replace.size(); i++, n++)
                    skipped += (skipped.empty() ? "" : ", ") + who(replace[i].h->addon);
                if (n)
                    line += ", " + std::to_string(n) + " other replace not reached (" + skipped + ")";
            } else if (ran_replace) {
                line += "; no replace handled it";
            }
            if (p.hot || !p.event)
                line += "; later fires are not logged";
            log(line);
        }
        for (auto &f : faults)
            log(f);
        return SWOFTY_ASSET_OK;
    }

    // The point a hot game hook point fires through fire_direct, found once.
    Point *direct_point(const std::string &name)
    {
        std::lock_guard<std::mutex> g(lock_);
        return &point(name);
    }

    bool has_handlers(const Point &p)
    {
        std::lock_guard<std::mutex> g(lock_);
        return !p.handlers.empty();
    }

    // The same phases as fire() for a point that fires every frame for every car: no context table and no
    // allocation. Handlers see a null SwoftyHookCall::context (hooks->context needs one), selectors on
    // when()/in() still match against the current facts, and the content owner gets no precedence. original
    // runs between replace and after when no replace handler handled the call.
    int fire_direct(Point &p, void *payload, int *replaced, void (*original)(void *), void *user)
    {
        if (replaced)
            *replaced = 0;
        std::shared_ptr<const DirectLine> line;
        bool first;
        {
            std::lock_guard<std::mutex> g(lock_);
            if (p.line_dirty) {
                auto next = std::make_shared<DirectLine>();
                for (auto &h : p.handlers) {
                    if (!h->live || !catalogue::has_phase(p.kind, h->phase))
                        continue;
                    (h->phase == SWOFTY_PHASE_BEFORE ? next->before : h->phase == SWOFTY_PHASE_REPLACE ? next->replace : next->after)
                        .push_back(h);
                }
                auto ordered = [](const std::shared_ptr<Handler> &a, const std::shared_ptr<Handler> &b) {
                    return a->priority != b->priority ? a->priority > b->priority : a->order < b->order;
                };
                std::stable_sort(next->before.begin(), next->before.end(), ordered);
                std::stable_sort(next->replace.begin(), next->replace.end(), ordered);
                std::stable_sort(next->after.begin(), next->after.end(), ordered);
                p.line = std::move(next);
                p.line_dirty = false;
            }
            line = p.line;
            first = !p.fired.exchange(true);
        }
        auto run_original = [&] {
            if (original)
                original(user);
        };
        if (!line || (line->before.empty() && line->replace.empty() && line->after.empty())) {
            run_original();
            return SWOFTY_ASSET_OK;
        }
        for (auto *a : active())
            if (a == &p) {
                run_original();
                return SWOFTY_ASSET_OK;
            }
        Active on(&p);
        std::shared_ptr<const Facts> snap;
        auto in_scope = [&](const Handler &h) {
            if (h.scope.empty())
                return true;
            if (!snap)
                snap = current();
            return h.scope.match(*snap);
        };
        SwoftyHookCall call{sizeof(SwoftyHookCall), SWOFTY_PHASE_BEFORE, p.name.c_str(), payload, 0, nullptr};
        size_t ran_before = 0, ran_replace = 0, ran_after = 0;
        for (auto &h : line->before) {
            if (!h->live || !in_scope(*h))
                continue;
            call.phase = SWOFTY_PHASE_BEFORE;
            Fault f;
            p_.call(*h, &call, false, &f);
            ran_before++;
        }
        const Handler *handled = nullptr;
        for (auto &h : line->replace) {
            if (!h->live || !in_scope(*h))
                continue;
            call.phase = SWOFTY_PHASE_REPLACE;
            Fault f;
            ran_replace++;
            if (p_.call(*h, &call, false, &f)) {
                handled = h.get();
                break;
            }
        }
        call.replaced = handled ? 1 : 0;
        if (!handled)
            run_original();
        std::vector<std::string> faults;
        for (auto &h : line->after) {
            if (!h->live || !in_scope(*h))
                continue;
            call.phase = SWOFTY_PHASE_AFTER;
            Fault f;
            p_.call(*h, &call, true, &f);
            ran_after++;
            if (f.faulted) {
                detach(h->token);
                h->live = false;
                faults.push_back("[hooks] " + p.name + ": after handler from " + who(h->addon) + " faulted (" + f.what +
                                 "); handler detached for this session");
                if (p_.faulted)
                    p_.faulted(p.name, "after", h->addon, f.what);
            }
        }
        if (replaced)
            *replaced = call.replaced;
        if (first) {
            std::string l = "[hooks] " + p.name + ": first fire, " + counts(ran_before, ran_replace, ran_after);
            if (handled)
                l += "; replace by " + who(handled->addon) + " -> handled";
            else if (ran_replace)
                l += "; no replace handled it";
            log(l + "; later fires are not logged");
        }
        for (auto &f : faults)
            log(f);
        return SWOFTY_ASSET_OK;
    }

    int context_part(const void *handle, std::string_view part, std::string &out)
    {
        auto ctx = find_context(handle);
        if (!ctx)
            return SWOFTY_ASSET_INVALID;
        out = ctx->part(part);
        return out.empty() ? SWOFTY_ASSET_INVALID : SWOFTY_ASSET_OK;
    }

    int context_in(const void *handle, std::string_view selectors, int *out)
    {
        auto ctx = find_context(handle);
        Selectors s;
        if (!ctx || !Selectors::compile(selectors, s, nullptr))
            return SWOFTY_ASSET_INVALID;
        if (out)
            *out = s.match(ctx->facts()) ? 1 : 0;
        return SWOFTY_ASSET_OK;
    }

    std::shared_ptr<const Facts> current()
    {
        std::lock_guard<std::mutex> g(facts_lock_);
        return facts_;
    }

    template <class F> void update(F &&fn)
    {
        std::lock_guard<std::mutex> g(facts_lock_);
        Facts next = *facts_;
        fn(next);
        next.epoch = facts_->epoch + 1;
        facts_ = std::make_shared<const Facts>(std::move(next));
    }

    // The points with handlers that nothing defines, for the end-of-load report.
    std::vector<std::pair<std::string, std::string>> undefined()
    {
        std::lock_guard<std::mutex> g(lock_);
        std::vector<std::pair<std::string, std::string>> out;
        for (auto &[name, p] : points_)
            if (!p->defined && !p->handlers.empty())
                out.push_back({name, subscribers(*p)});
        std::sort(out.begin(), out.end());
        return out;
    }

    size_t handlers(const std::string &name)
    {
        std::lock_guard<std::mutex> g(lock_);
        auto it = points_.find(name);
        return it == points_.end() ? 0 : it->second->handlers.size();
    }

    void log(const std::string &line)
    {
        if (p_.log)
            p_.log(line);
    }

private:
    struct LiveContext {
        LiveContext(Table &t, std::shared_ptr<Context> c) : t(t), c(c.get())
        {
            std::lock_guard<std::mutex> g(t.ctx_lock_);
            t.contexts_.push_back(std::move(c));
        }
        ~LiveContext()
        {
            std::lock_guard<std::mutex> g(t.ctx_lock_);
            for (size_t i = t.contexts_.size(); i-- > 0;)
                if (t.contexts_[i].get() == c) {
                    t.contexts_.erase(t.contexts_.begin() + i);
                    break;
                }
        }
        Table &t;
        const Context *c;
    };

    static std::vector<const Point *> &active()
    {
        thread_local std::vector<const Point *> stack;
        return stack;
    }

    struct Active {
        explicit Active(const Point *p) { active().push_back(p); }
        ~Active() { active().pop_back(); }
    };

    std::shared_ptr<Context> find_context(const void *handle)
    {
        std::lock_guard<std::mutex> g(ctx_lock_);
        for (auto &c : contexts_)
            if (c.get() == handle)
                return c;
        return nullptr;
    }

    Point &point(const std::string &name)
    {
        auto &slot = points_[name];
        if (!slot) {
            slot = std::make_shared<Point>();
            slot->name = name;
        }
        return *slot;
    }

    uint64_t rank(const std::string &addon, uint64_t module)
    {
        uint64_t known = p_.load_order ? p_.load_order(addon, module) : 0;
        if (known)
            return known;
        auto it = ranks_.find(addon);
        if (it != ranks_.end())
            return it->second;
        uint64_t r = (uint64_t(1) << 20) + ranks_.size();
        ranks_[addon] = r;
        return r;
    }

    static std::string who(const std::string &addon) { return addon.empty() ? std::string("the core") : addon; }

    static std::string where(const Facts &f)
    {
        return f.content.empty() ? "mode " + (f.mode.empty() ? std::string("menu") : f.mode) : "content " + f.content;
    }

    static bool is_owner(const Facts &f, const std::string &addon)
    {
        if (addon.empty())
            return false;
        if (f.content_owner == addon)
            return true;
        for (auto &o : f.owners)
            if (o == addon)
                return true;
        return false;
    }

    static std::string counts(size_t b, size_t r, size_t a)
    {
        if (!b && !r && !a)
            return "0 handlers";
        std::string s;
        auto add = [&](size_t n, const char *phase) {
            if (!n)
                return;
            s += (s.empty() ? "" : ", ") + std::to_string(n) + " " + phase + (n == 1 ? " handler" : " handlers");
        };
        add(b, "before");
        add(r, "replace");
        add(a, "after");
        return s;
    }

    static std::string subscribers(const Point &p)
    {
        std::vector<std::string> names;
        for (auto &h : p.handlers)
            if (std::find(names.begin(), names.end(), who(h->addon)) == names.end())
                names.push_back(who(h->addon));
        std::string s;
        for (auto &n : names)
            s += (s.empty() ? "" : ", ") + n;
        return s;
    }

    static std::string no_phase(const Point &p, uint32_t phase, const std::string &addon)
    {
        return std::string(p.event ? "[events] " : "[hooks] ") + p.name + " has no " + catalogue::phase_name(phase) +
               " phase (" + catalogue::kind_name(p.kind) + (p.event ? " event" : " point") + "); handler from " +
               who(addon) + " not attached";
    }

    std::vector<std::string> prune(Point &p)
    {
        std::vector<std::string> lines;
        for (size_t i = p.handlers.size(); i-- > 0;) {
            auto &h = p.handlers[i];
            if (catalogue::has_phase(p.kind, h->phase))
                continue;
            lines.insert(lines.begin(), no_phase(p, h->phase, h->addon));
            h->live = false;
            tokens_.erase(h->token);
            p.handlers.erase(p.handlers.begin() + i);
            p.line_dirty = true;
        }
        return lines;
    }

    int refuse(const std::string &line)
    {
        log(line);
        return SWOFTY_ASSET_INVALID;
    }

    int refuse_locked(const std::string &line) { return refuse(line); }

    Platform p_;
    std::mutex lock_;
    std::unordered_map<std::string, std::shared_ptr<Point>> points_;
    std::unordered_map<uint32_t, std::string> tokens_;
    std::unordered_map<std::string, uint64_t> ranks_;
    uint32_t next_token_ = 0x80000000u;
    uint64_t next_order_ = 0;
    bool armed_ = false;
    std::mutex facts_lock_;
    std::shared_ptr<const Facts> facts_;
    std::mutex ctx_lock_;
    std::vector<std::shared_ptr<Context>> contexts_;
};

}
