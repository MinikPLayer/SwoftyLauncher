#include "store.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <thread>
#include <tuple>

namespace swofty_core::registry {

namespace {

constexpr int MAX_DEPTH = 8;

thread_local std::string tl_error;
thread_local int tl_callback_depth = 0;
thread_local std::set<std::string> *tl_computing = nullptr;

struct CallbackGuard {
    CallbackGuard() { ++tl_callback_depth; }
    ~CallbackGuard() { --tl_callback_depth; }
};

std::string owner_of(const std::string &id)
{
    auto s = id.find('/');
    return s == std::string::npos ? std::string() : id.substr(0, s);
}

bool is_key(const std::string &k)
{
    if (k.empty() || k.front() == '/' || k.back() == '/')
        return false;
    for (char c : k)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
              c == '.' || c == '/'))
            return false;
    return k.find("//") == std::string::npos;
}

bool null_like(const Value *v) { return !v || v->is_null(); }

bool same(const Value &a, const Value &b);

bool same_ptr(const Value *a, const Value *b)
{
    if (null_like(a) || null_like(b))
        return null_like(a) && null_like(b);
    return same(*a, *b);
}

bool same(const Value &a, const Value &b)
{
    if (a.is_number() && b.is_number()) {
        if (a.is_int() && b.is_int())
            return a.as_int() == b.as_int();
        return a.as_double() == b.as_double();
    }
    if (a.is_null() || b.is_null())
        return a.is_null() && b.is_null();
    if (a.type() != b.type())
        return false;
    if (auto ao = a.if_object()) {
        auto bo = b.if_object();
        for (auto &[k, v] : *ao)
            if (!same_ptr(&v, bo->find(k)))
                return false;
        for (auto &[k, v] : *bo)
            if (!ao->find(k) && !v.is_null())
                return false;
        return true;
    }
    if (auto aa = a.if_array()) {
        auto ba = b.if_array();
        if (aa->size() != ba->size())
            return false;
        for (std::size_t i = 0; i < aa->size(); i++)
            if (!same((*aa)[i], (*ba)[i]))
                return false;
        return true;
    }
    return a == b;
}

bool truthy(const Value &v)
{
    if (v.is_bool())
        return v.as_bool();
    if (v.is_number())
        return v.as_double() != 0;
    if (auto s = v.if_string())
        return !s->empty() && *s != "false" && *s != "off" && *s != "0";
    return !v.is_null();
}

std::vector<std::string> split_pattern(const std::string &p)
{
    std::vector<std::string> out;
    std::size_t i = 1;
    while (i <= p.size() && !p.empty()) {
        auto j = p.find('/', i);
        if (j == std::string::npos)
            j = p.size();
        out.push_back(p.substr(i, j - i));
        i = j + 1;
    }
    return out;
}

struct Hit {
    std::string pointer;
    std::string element;
    Value *value;
};

void walk(Value &v, const std::vector<std::string> &segs, std::size_t i, std::string ptr, std::string element,
          std::vector<Hit> &out)
{
    if (i == segs.size()) {
        out.push_back({ptr, element, &v});
        return;
    }
    if (segs[i] == "*") {
        if (auto a = v.if_array())
            for (std::size_t k = 0; k < a->size(); k++) {
                std::string p = ptr + "/" + std::to_string(k);
                walk((*a)[k], segs, i + 1, p, p, out);
            }
        return;
    }
    if (auto o = v.if_object())
        if (auto c = o->find(segs[i]))
            walk(*c, segs, i + 1, ptr + "/" + swofty::json::escape(segs[i]), element, out);
}

const std::vector<std::string> *keyed(const Def &def, const std::string &pattern)
{
    for (auto &[p, f] : def.keys)
        if (p == pattern)
            return &f;
    return nullptr;
}

bool holds(const Value &e, const std::vector<std::string> &fields)
{
    if (!e.is_object())
        return false;
    for (auto &f : fields) {
        auto v = e.find(f);
        if (!v || v->is_array() || v->is_object())
            return false;
    }
    return true;
}

bool same_key(const Value &a, const Value &b, const std::vector<std::string> &fields)
{
    for (auto &f : fields)
        if (!same_ptr(a.find(f), b.find(f)))
            return false;
    return true;
}

void diff(const Def &def, const Value &a, const Value &b, std::string &ptr, std::string &pat,
          std::vector<std::string> &out)
{
    if (same(a, b))
        return;
    if (a.is_object() && b.is_object()) {
        std::vector<std::string> names;
        for (auto &[k, v] : *b.if_object())
            names.push_back(k);
        for (auto &[k, v] : *a.if_object())
            if (!b.find(k))
                names.push_back(k);
        static const Value null_value;
        for (auto &k : names) {
            const Value *va = a.find(k), *vb = b.find(k);
            if (same_ptr(va, vb))
                continue;
            auto pl = ptr.size(), tl = pat.size();
            std::string seg = "/" + swofty::json::escape(k);
            ptr += seg;
            pat += seg;
            diff(def, va ? *va : null_value, vb ? *vb : null_value, ptr, pat, out);
            ptr.resize(pl);
            pat.resize(tl);
        }
        return;
    }
    if (a.is_array() && b.is_array()) {
        if (auto fields = keyed(def, pat)) {
            bool all = true;
            for (auto &e : *a.if_array())
                all = all && holds(e, *fields);
            for (auto &e : *b.if_array())
                all = all && holds(e, *fields);
            if (all) {
                for (auto &e : *b.if_array()) {
                    std::string seg = swofty::json::key_segment(e, *fields);
                    const Value *old = nullptr;
                    for (auto &o : *a.if_array())
                        if (same_key(o, e, *fields)) {
                            old = &o;
                            break;
                        }
                    if (!old) {
                        out.push_back(ptr + "/" + seg);
                        continue;
                    }
                    auto pl = ptr.size(), tl = pat.size();
                    ptr += "/" + seg;
                    pat += "/*";
                    diff(def, *old, e, ptr, pat, out);
                    ptr.resize(pl);
                    pat.resize(tl);
                }
                return;
            }
        }
    }
    out.push_back(ptr);
}

std::vector<std::string> changed(const Def &def, const Value &a, const Value &b)
{
    std::vector<std::string> out;
    std::string ptr, pat;
    diff(def, a, b, ptr, pat, out);
    return out;
}

bool under(const std::string &p, const std::string &parent)
{
    return p.size() > parent.size() && p.compare(0, parent.size(), parent) == 0 && p[parent.size()] == '/';
}

bool present(const Value &v, const std::string &ptr)
{
    if (ptr.empty())
        return !v.is_null();
    auto g = v.get(ptr);
    return g && !g->is_null();
}

std::vector<std::string> split_alts(std::string s, bool &optional)
{
    optional = !s.empty() && s.back() == '?';
    if (optional)
        s.pop_back();
    std::vector<std::string> alts;
    std::size_t i = 0;
    for (;;) {
        auto j = s.find('|', i);
        std::string a = s.substr(i, j == std::string::npos ? std::string::npos : j - i);
        if (!a.empty() && a.back() == '?') {
            a.pop_back();
            optional = true;
        }
        alts.push_back(a);
        if (j == std::string::npos)
            break;
        i = j + 1;
    }
    return alts;
}

Value strip_meta(const Value &v)
{
    if (auto o = v.if_object()) {
        Value out = Value::object();
        for (auto &[k, m] : *o)
            if (k != "$select" && k != "$protect")
                (*out.if_object()).insert(k, m);
        return out;
    }
    return v;
}

template <class F> int call_sized(std::string &buf, F f)
{
    std::size_t cap = 4096;
    for (int tries = 0; tries < 6; tries++) {
        buf.assign(cap, '\0');
        std::size_t len = 0;
        int r = f(buf.data(), cap, &len);
        if (r == SWOFTY_TOO_SMALL && len > cap) {
            cap = len + 1;
            continue;
        }
        if (r != SWOFTY_ASSET_OK)
            return r;
        buf.resize(len && len <= cap ? len : std::strlen(buf.c_str()));
        while (!buf.empty() && buf.back() == '\0')
            buf.pop_back();
        return r;
    }
    return SWOFTY_ASSET_INVALID;
}

const Value *schema_node(const Value *node, const Value &root)
{
    for (int hops = 0; node && hops < 8; hops++) {
        auto ref = node->find("$ref");
        if (!ref || !ref->is_string())
            return node;
        std::string r(ref->as_string());
        node = r.rfind("#/", 0) == 0 ? root.get(r.substr(1)) : nullptr;
    }
    return node;
}

Value overlay(const Value &in, const Value &out, const Value *node, const Value &root, int depth)
{
    node = schema_node(node, root);
    if (!node || depth > 32)
        return out;
    if (in.is_object() && out.is_object()) {
        const Value *props = node->find("properties");
        if (!props || !props->is_object())
            return out;
        Value res = in;
        for (auto &[k, v] : *out.if_object()) {
            const Value *sub = props->find(k), *iv = in.find(k);
            res[k] = sub && iv ? overlay(*iv, v, sub, root, depth + 1) : v;
        }
        for (auto &[k, sub] : *props->if_object())
            if (!out.find(k))
                res.erase(k);
        return res;
    }
    if (in.is_array() && out.is_array()) {
        const Value *xk = node->find("x-key"), *items = node->find("items");
        if (!xk || !xk->is_array() || !items)
            return out;
        std::vector<std::string> fields;
        for (auto &f : *xk->if_array())
            fields.emplace_back(f.as_string());
        Value res = Value::array();
        for (auto &e : *out.if_array()) {
            const Value *old = nullptr;
            if (holds(e, fields))
                for (auto &o : *in.if_array())
                    if (holds(o, fields) && same_key(o, e, fields)) {
                        old = &o;
                        break;
                    }
            res.push_back(old ? overlay(*old, e, items, root, depth + 1) : e);
        }
        return res;
    }
    return out;
}

}

const char *applies_name(Applies a)
{
    switch (a) {
    case Applies::boot:
        return "boot";
    case Applies::content:
        return "content";
    case Applies::session:
        return "session";
    default:
        return "read";
    }
}

const char *status_name(Status s)
{
    switch (s) {
    case Status::ok:
        return "ok";
    case Status::no_record:
        return "no_record";
    case Status::no_registry:
        return "no_registry";
    case Status::invalid:
        return "invalid";
    default:
        return "late";
    }
}

const char *scope_name(Scope s) { return s == Scope::global ? "global" : s == Scope::profile ? "profile" : "career"; }

std::string &last_error() { return tl_error; }
void set_last_error(std::string why) { tl_error = std::move(why); }

Validator &default_validator()
{
    static PassThrough pass;
    return pass;
}

namespace {

void schema_walk(const Value &root, const Value &node, const std::string &pat, Def &def, int depth)
{
    if (depth > 16 || !node.is_object())
        return;
    const Value *n = &node;
    if (auto ref = node.find("$ref"); ref && ref->is_string()) {
        std::string r(ref->as_string());
        if (r.rfind("#/", 0) == 0)
            if (auto t = root.get(r.substr(1)))
                n = t;
    }
    if (auto x = n->find("x-ref"); x && x->is_string() && !pat.empty() && pat != "/extends")
        def.refs.emplace_back(pat, std::string(x->as_string()));
    if (auto props = n->find("properties"); props && props->is_object())
        for (auto &[k, v] : *props->if_object())
            schema_walk(root, v, pat + "/" + swofty::json::escape(k), def, depth + 1);
    if (auto xk = n->find("x-key"); xk && xk->is_array()) {
        std::vector<std::string> fields;
        for (auto &f : *xk->if_array())
            if (f.is_string())
                fields.emplace_back(f.as_string());
        if (!fields.empty())
            def.keys.emplace_back(pat, fields);
    }
    if (auto items = n->find("items"); items && items->is_object())
        schema_walk(root, *items, pat + "/*", def, depth + 1);
}

}

Def parse_schema(const std::string &name, const Value &schema)
{
    Def def;
    def.name = name;
    std::string applies(schema["x-applies"].as_string("read"));
    def.applies = applies == "boot" ? Applies::boot : applies == "content" ? Applies::content
                  : applies == "session"                                   ? Applies::session
                                                                           : Applies::read;
    std::string ver(schema["x-version"].as_string("1.0"));
    auto dot = ver.find('.');
    def.major = std::atoi(ver.substr(0, dot).c_str());
    def.minor = dot == std::string::npos ? 0 : std::atoi(ver.substr(dot + 1).c_str());
    if (def.major < 1)
        def.major = 1;
    def.snapshot = schema["x-snapshot"].as_bool(false);
    def.budget = static_cast<std::size_t>(schema["x-snapshot-budget"].as_int(65536));
    if (auto sh = schema.find("x-shared"); sh && sh->is_object()) {
        def.shared = true;
        def.provider_registry = std::string((*sh)["provider_registry"].as_string());
        def.provides = std::string((*sh)["provides"].as_string());
        def.resolved = std::string((*sh)["resolved"].as_string());
        def.shared_prefix = std::string((*sh)["prefix"].as_string());
        if (def.shared_prefix.empty())
            def.shared_prefix = name.substr(name.find_last_of('.') + 1);
    }
    schema_walk(schema, schema, "", def, 0);
    def.schema = swofty::json::dump(schema);
    return def;
}

struct Prov {
    std::string addon;
    bool variant = false;
    Value value;
    std::string base;
    Change change;
    uint64_t seq = 0;
    int op = -1;
    std::string source;
};

struct Dyn {
    std::string addon;
    SwoftyProvideFn fn;
    SwoftyListFn list;
    void *user;
    uint64_t seq;
    std::map<std::string, std::optional<Value>> cache;
    bool listed = false;
    std::vector<std::string> ids;
};

struct Layer {
    uint32_t handle = 0;
    LayerSpec spec;
    std::string owner;
    uint64_t seq = 0;
    int op = -1;
};

struct TagEntry {
    std::string id, addon;
};

struct Adapter {
    uint32_t from, to;
    SwoftyConvertFn up, down;
    void *user;
};

struct Res {
    Status st = Status::no_record;
    Value v;
    std::string owner, why;
    std::map<std::string, std::string> origins;
    std::map<std::string, std::vector<std::string>> changers;
    std::map<std::string, std::vector<std::string>> blocked;
};

struct Eval {
    uint64_t gen = ~0ull;
    std::map<std::string, Res> top;
    std::vector<Refusal> refusals;
    std::vector<Conflict> conflicts;
    std::set<uint32_t> dropped;
    std::vector<std::string> ids;
};

struct Reg {
    Def def;
    std::map<int, Def> majors;
    bool stock = false;
    std::map<std::string, std::vector<Prov>> provs;
    std::vector<Dyn> dyns;
    std::vector<Layer> layers;
    std::map<std::string, std::vector<TagEntry>> tags;
    std::map<std::string, std::vector<std::string>> protect;
    std::vector<std::pair<SwoftyCheckFn, void *>> checks;
    std::vector<Adapter> adapters;
    std::map<std::string, std::shared_ptr<const Value>> schema_of;
    Eval eval;
};

struct Watcher {
    uint32_t token;
    std::string registry;
    SwoftyRecordFn fn;
    void *user;
};

struct Store::Impl {
    mutable std::recursive_mutex mu;
    std::map<std::string, Reg> regs;
    std::vector<std::string> order;
    std::map<std::string, int> index;
    std::map<std::string, int> ops;
    Validator *validator = &default_validator();
    std::function<bool(const std::string &, const std::string &, Value &)> option_source;
    std::function<bool(const std::string &)> override_source;
    std::function<void(const std::string &)> log;
    Context ctx;
    bool registering = true;
    uint64_t seq = 0, gen = 1;
    uint32_t next_handle = 1, next_token = 1;
    std::vector<Refusal> refusals;
    std::vector<Watcher> watchers;
    std::atomic<bool> loading{false};
    std::mutex wait_mu;
    std::condition_variable loaded;
    std::atomic<bool (*)()> is_flow{nullptr};
    std::map<std::string, std::vector<std::function<void()>>> pending;
    std::set<uint32_t> pending_handles;
    std::map<uint32_t, std::vector<Value>> pending_in;
    int forced_op = -1;
    uint64_t forced_seq = 0;
    uint32_t forced_handle = 0;
    int forced_registering = -1;

    void wait_for_load()
    {
        if (!loading || tl_callback_depth || tl_computing)
            return;
        auto flow = is_flow.load();
        if (flow && flow()) {
            std::unique_lock w(wait_mu);
            loaded.wait(w, [&] { return !loading.load(); });
        }
    }

    uint64_t take_seq()
    {
        if (forced_seq) {
            uint64_t q = forced_seq;
            forced_seq = 0;
            return q;
        }
        return ++seq;
    }

    bool registering_now() const { return forced_registering >= 0 ? forced_registering != 0 : registering; }

    template <class F> int defer(const std::string &registry, int op, F &&replay)
    {
        uint64_t q = ++seq;
        int reg_state = registering_now() ? 1 : 0;
        std::string profile = ctx.profile, career = ctx.career;
        pending[registry.substr(0, registry.find('@'))].push_back([this, op, q, reg_state, profile, career, replay] {
            Context saved = ctx;
            ctx.profile = profile;
            ctx.career = career;
            forced_op = op;
            forced_seq = q;
            forced_registering = reg_state;
            replay();
            forced_op = -1;
            forced_seq = 0;
            forced_registering = -1;
            ctx.profile = saved.profile;
            ctx.career = saved.career;
        });
        return SWOFTY_ASSET_OK;
    }
    std::map<std::pair<std::string, std::string>, std::optional<Value>> option_reads;
    std::set<std::string> unchecked, reported_invalid;
    SnapshotSource snapshot;

    bool read_option(const std::string &addon, const std::string &id, Value &out)
    {
        bool ok = option_source && option_source(addon, id, out);
        option_reads[{addon, id}] = ok ? std::optional<Value>(out) : std::nullopt;
        return ok;
    }

    void recheck_options()
    {
        for (auto &[k, v] : option_reads) {
            Value now;
            bool ok = option_source && option_source(k.first, k.second, now);
            if (ok != v.has_value() || (ok && !same(now, *v))) {
                note("[registry] option " + k.second + " of " + k.first +
                     " changed; the layers that read it run again at the next read");
                option_reads.clear();
                touch();
                return;
            }
        }
    }

    void note(const std::string &s)
    {
        if (log)
            log(s);
    }

    int addon_index(const std::string &addon)
    {
        if (addon == "game" || addon.empty())
            return 0;
        auto it = index.find(addon);
        if (it != index.end())
            return it->second;
        int n = static_cast<int>(index.size()) + 1;
        index[addon] = n;
        order.push_back(addon);
        return n;
    }

    int next_op(const std::string &addon)
    {
        if (forced_op >= 0) {
            int o = forced_op;
            forced_op = -1;
            return o;
        }
        return ops[addon]++;
    }

    int refuse(const std::string &addon, const std::string &registry, const std::string &id, int op,
               const std::string &reason, const std::string &message, int result)
    {
        refusals.push_back({addon, registry, id, reason, message, op});
        set_last_error(message);
        note("[registry] " + registry + (id.empty() ? "" : " " + id) + ": " + message);
        return result;
    }

    void touch() { ++gen; }

    Reg *reg(const std::string &name)
    {
        auto at = name.find('@');
        auto it = regs.find(at == std::string::npos ? name : name.substr(0, at));
        return it == regs.end() ? nullptr : &it->second;
    }

    bool late_for(const Reg &r) const { return !registering_now() && r.def.applies == Applies::boot; }

    void evaluate(Reg &r);
};

namespace {

struct Engine {
    Store::Impl &s;
    Reg &r;
    const Context &ctx;
    const std::set<uint32_t> &dropped;
    std::map<uint32_t, std::set<std::string>> failures;
    std::vector<Refusal> variant_refusals;
    std::map<std::string, Res> memo;
    std::map<std::string, Value> basevs;
    std::map<std::string, int> chains;
    std::map<std::string, std::string> shared_winner;
    std::map<std::string, std::vector<std::string>> shared_addons;
    std::map<std::string, std::vector<std::string>> tag_cache;

    Engine(Store::Impl &st, Reg &rg, const std::set<uint32_t> &dr) : s(st), r(rg), ctx(st.ctx), dropped(dr) {}
    Engine(Store::Impl &st, Reg &rg, const std::set<uint32_t> &dr, const Context &c)
        : s(st), r(rg), ctx(c), dropped(dr) {}

    Reg *other(const std::string &name) { return s.reg(name); }

    std::optional<Value> dynamic(Reg &g, const std::string &id)
    {
        std::string owner = owner_of(id);
        for (auto &d : g.dyns) {
            if (d.addon != owner)
                continue;
            auto it = d.cache.find(id);
            if (it == d.cache.end()) {
                std::string buf;
                int rc;
                {
                    CallbackGuard cb;
                    auto *prev = tl_computing;
                    std::set<std::string> merged = prev ? *prev : std::set<std::string>{};
                    merged.insert(g.def.name);
                    tl_computing = &merged;
                    rc = call_sized(buf, [&](char *o, std::size_t cap, std::size_t *len) {
                        return d.fn(d.user, g.def.name.c_str(), id.c_str(), o, cap, len);
                    });
                    tl_computing = prev;
                }
                std::optional<Value> v;
                if (rc == SWOFTY_ASSET_OK) {
                    swofty::json::Error e;
                    Value p = swofty::json::parse(buf, &e);
                    if (e.message.empty() && p.is_object())
                        v = p;
                }
                it = d.cache.emplace(id, v).first;
            }
            if (it->second)
                return it->second;
        }
        return std::nullopt;
    }

    const Prov *prov(Reg &g, const std::string &id)
    {
        auto it = g.provs.find(id);
        if (it == g.provs.end() || it->second.empty())
            return nullptr;
        return &it->second.back();
    }

    bool has_provider(Reg &g, const std::string &id, bool &is_variant)
    {
        is_variant = false;
        if (g.def.shared)
            return g.provs.count(id) && !winner(g, id).empty();
        if (dynamic(g, id))
            return true;
        auto p = prov(g, id);
        if (!p)
            return false;
        is_variant = p->variant;
        return true;
    }

    int chain_state(Reg &g, const std::string &id, std::vector<std::string> *ids = nullptr)
    {
        std::vector<std::string> path;
        std::set<std::string> seen;
        std::string cur = id;
        int state = 0;
        for (;;) {
            bool v;
            if (!has_provider(g, cur, v)) {
                state = 1;
                break;
            }
            path.push_back(cur);
            if (!v || dynamic(g, cur))
                break;
            if (!seen.insert(cur).second) {
                state = 2;
                break;
            }
            if (path.size() > MAX_DEPTH + 1) {
                state = 3;
                break;
            }
            cur = prov(g, cur)->base;
        }
        if (ids) {
            std::reverse(path.begin(), path.end());
            *ids = path;
        }
        return state;
    }

    bool exists(Reg &g, const std::string &id) { return chain_state(g, id) == 0; }

    bool resolves(const std::string &registry, const std::string &id)
    {
        Reg *g = other(registry);
        if (!g) {
            if (s.unchecked.insert(registry).second)
                s.note("[registry] " + r.def.name + ": references into " + registry +
                       " are not checked (nothing declares that registry)");
            return true;
        }
        if (id.rfind("game/", 0) == 0 && !g->stock && !g->def.shared)
            return true;
        if (id.rfind("person/", 0) == 0 && registry == "driver.person") {
            std::string key = id.substr(7);
            for (auto &rid : provider_records(*g)) {
                auto slash = rid.rfind('/');
                if (rid.substr(slash == std::string::npos ? 0 : slash + 1) == key && exists(*g, rid))
                    return true;
            }
            return false;
        }
        if (id.rfind("person/", 0) == 0 && registry == "driver.identity") {
            Engine ge(s, *g, g->eval.dropped);
            for (auto &rid : provider_records(*g)) {
                if (!ge.exists(*g, rid))
                    continue;
                Value b = ge.basev(rid);
                if (b["person"].is_string() && b["person"].as_string() == id)
                    return true;
            }
            return false;
        }
        for (auto &[name, sh] : s.regs)
            if (sh.def.shared && sh.def.provider_registry == registry && !sh.def.shared_prefix.empty() &&
                id.rfind(sh.def.shared_prefix + "/", 0) == 0)
                return exists(sh, id);
        return exists(*g, id);
    }

    std::string pick(const std::string &registry, const std::string &text, bool &optional, bool &ok)
    {
        auto alts = split_alts(text, optional);
        for (auto &a : alts)
            if (resolves(registry, a)) {
                ok = true;
                return a;
            }
        ok = false;
        return alts.empty() ? std::string() : alts.front();
    }

    std::vector<std::string> provider_records(Reg &pr)
    {
        std::vector<std::string> out;
        for (auto &[id, v] : pr.provs)
            if (!v.empty())
                out.push_back(id);
        for (auto &d : pr.dyns)
            for (auto &id : listed(d))
                out.push_back(id);
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }

    std::string winner(Reg &g, const std::string &id)
    {
        auto it = shared_winner.find(id);
        if (it != shared_winner.end())
            return it->second;
        shared_winner[id] = "";
        Reg *pr = other(g.def.provider_registry);
        std::vector<std::tuple<int, uint64_t, std::string>> found;
        if (pr) {
            Engine pe(s, *pr, pr->eval.dropped);
            for (auto &rid : provider_records(*pr)) {
                if (!pe.exists(*pr, rid))
                    continue;
                Value b = pe.basev(rid);
                const Value *list = b.get(g.def.provides);
                if (!list || !list->is_array())
                    continue;
                bool hit = false;
                for (auto &e : *list->if_array())
                    if (e.is_string() && e.as_string() == id)
                        hit = true;
                if (!hit)
                    continue;
                auto p = pe.prov(*pr, rid);
                found.emplace_back(s.addon_index(owner_of(rid)), p ? p->seq : 0, rid);
            }
        }
        std::sort(found.begin(), found.end());
        std::vector<std::string> addons;
        for (auto &[i, q, rid] : found) {
            std::string a = owner_of(rid);
            if (std::find(addons.begin(), addons.end(), a) == addons.end())
                addons.push_back(a);
        }
        shared_addons[id] = addons;
        std::string w = found.empty() ? std::string() : std::get<2>(found.back());
        shared_winner[id] = w;
        return w;
    }

    std::vector<std::string> listed(Dyn &d)
    {
        if (!d.listed) {
            d.listed = true;
            if (d.list) {
                CallbackGuard cb;
                std::vector<std::string> ids;
                d.list(
                    d.user, r.def.name.c_str(),
                    [](void *c, const char *id) {
                        if (id)
                            static_cast<std::vector<std::string> *>(c)->emplace_back(id);
                    },
                    &ids);
                for (auto &id : ids)
                    if (owner_of(id) == d.addon)
                        d.ids.push_back(id);
            }
        }
        return d.ids;
    }

    std::vector<std::string> all_ids(Reg &g)
    {
        std::vector<std::string> out;
        for (auto &[id, v] : g.provs)
            if (!v.empty())
                out.push_back(id);
        for (auto &d : g.dyns)
            for (auto &id : listed(d))
                out.push_back(id);
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        std::vector<std::string> kept;
        for (auto &id : out) {
            bool v;
            if (has_provider(g, id, v))
                kept.push_back(id);
        }
        return kept;
    }

    bool run_change(const Change &c, const Value &before, Value &after)
    {
        after = before;
        if (c.is_patch) {
            swofty::json::Keys keys;
            for (auto &[p, f] : r.def.keys)
                keys.add(p, f);
            swofty::json::Error e;
            if (!swofty::json::merge_patch(after, c.patch, keys, &e) && !e.message.empty())
                return false;
            return true;
        }
        if (!c.fn)
            return false;
        std::string in = swofty::json::dump(before), out;
        int rc;
        {
            CallbackGuard cb;
            auto *prev = tl_computing;
            std::set<std::string> merged = prev ? *prev : std::set<std::string>{};
            merged.insert(r.def.name);
            tl_computing = &merged;
            rc = call_sized(out, [&](char *o, std::size_t cap, std::size_t *len) {
                return c.fn(c.user, SwoftyText{in.data(), in.size()}, o, cap, len);
            });
            tl_computing = prev;
        }
        if (rc != SWOFTY_ASSET_OK)
            return false;
        swofty::json::Error e;
        Value v = swofty::json::parse(out, &e);
        if (!e.message.empty() || !v.is_object())
            return false;
        after = c.schema ? overlay(before, v, c.schema.get(), *c.schema, 0) : v;
        return true;
    }

    Value basev(const std::string &id)
    {
        auto it = basevs.find(id);
        if (it != basevs.end())
            return it->second;
        basevs[id] = Value();
        std::vector<std::string> path;
        Value v;
        if (chain_state(r, id, &path) == 0 && !path.empty()) {
            if (auto dv = dynamic(r, path[0]))
                v = *dv;
            else if (auto p = prov(r, path[0]))
                v = p->value;
            for (std::size_t i = 1; i < path.size(); i++) {
                Value next;
                if (run_change(prov(r, path[i])->change, v, next))
                    v = next;
            }
        }
        basevs[id] = v;
        return v;
    }

    bool where_ok(const Value &where, const Value &v)
    {
        auto o = where.if_object();
        if (!o)
            return false;
        for (auto &[k, want] : *o) {
            const Value *got = k.size() && k[0] == '/' ? v.get(k) : v.find(k);
            if (want.is_array()) {
                bool any = false;
                for (auto &w : *want.if_array())
                    any = any || same_ptr(got, &w);
                if (!any)
                    return false;
            } else if (want.is_object() && (want.find("min") || want.find("max"))) {
                if (!got || !got->is_number())
                    return false;
                double x = got->as_double();
                if (auto mn = want.find("min"); mn && x < mn->as_double())
                    return false;
                if (auto mx = want.find("max"); mx && x > mx->as_double())
                    return false;
            } else if (!same_ptr(got, &want)) {
                return false;
            }
        }
        return true;
    }

    std::vector<std::string> members(Reg &g, const std::string &tag)
    {
        std::string key = g.def.name + "\n" + tag;
        auto it = tag_cache.find(key);
        if (it != tag_cache.end())
            return it->second;
        std::set<std::string> base;
        auto t = g.tags.find(tag);
        Engine ge(s, g, g.eval.dropped);
        if (t != g.tags.end())
            for (auto &e : t->second)
                if (ge.exists(g, e.id))
                    base.insert(e.id);
        std::set<std::string> out = base;
        for (auto &id : ge.all_ids(g)) {
            std::vector<std::string> path;
            if (ge.chain_state(g, id, &path) != 0)
                continue;
            for (auto &p : path)
                if (base.count(p))
                    out.insert(id);
        }
        std::vector<std::string> v(out.begin(), out.end());
        tag_cache[key] = v;
        return v;
    }

    std::string ref_target(const std::string &pointer)
    {
        auto segs = split_pattern(pointer);
        for (auto &[pat, target] : r.def.refs) {
            auto ps = split_pattern(pat);
            if (ps.size() != segs.size())
                continue;
            bool ok = true;
            for (std::size_t i = 0; i < ps.size() && ok; i++)
                ok = ps[i] == "*" || ps[i] == segs[i];
            if (ok)
                return target;
        }
        return {};
    }

    bool sel_matches(const Layer &L, const std::string &id)
    {
        const LayerSpec &sp = L.spec;
        if (sp.has_prefix && id.compare(0, sp.prefix.size(), sp.prefix) != 0)
            return false;
        if (std::find(sp.except.begin(), sp.except.end(), id) != sp.except.end())
            return false;
        if (sp.match || sp.has_where) {
            Value b = basev(id);
            if (sp.has_where && !where_ok(sp.where, b))
                return false;
            if (sp.match) {
                std::string text = swofty::json::dump(b);
                CallbackGuard cb;
                if (!sp.match(sp.match_user, SwoftyText{text.data(), text.size()}))
                    return false;
            }
        }
        if (!sp.tag.empty()) {
            if (sp.tag_field.empty()) {
                auto m = members(r, sp.tag);
                if (!std::binary_search(m.begin(), m.end(), id))
                    return false;
            } else {
                Value b = basev(id);
                const Value *f = b.get(sp.tag_field);
                if (!f || !f->is_string())
                    return false;
                std::string target = ref_target(sp.tag_field);
                Reg *tg = target.empty() ? nullptr : other(target);
                if (!tg)
                    return false;
                bool opt, ok;
                std::string rid = pick(target, std::string(f->as_string()), opt, ok);
                auto m = members(*tg, sp.tag);
                if (!std::binary_search(m.begin(), m.end(), rid))
                    return false;
            }
        }
        return true;
    }

    bool in_ok(const Value &sel)
    {
        if (auto a = sel.if_array()) {
            for (auto &e : *a)
                if (!in_ok(e))
                    return false;
            return true;
        }
        auto o = sel.if_object();
        if (!o)
            return true;
        for (auto &[k, v] : *o) {
            std::string sv(v.as_string());
            bool ok;
            if (k == "mode")
                ok = ctx.mode == sv;
            else if (k == "content")
                ok = std::find(ctx.content.begin(), ctx.content.end(), sv) != ctx.content.end();
            else if (k == "content_exact")
                ok = !ctx.content.empty() && ctx.content.front() == sv;
            else if (k == "data_group")
                ok = ctx.data_group == sv;
            else if (k == "setup_mode")
                ok = ctx.setup_mode == sv;
            else if (k == "owner")
                ok = !ctx.content.empty() && owner_of(ctx.content.front()) == sv;
            else if (k == "track")
                ok = ctx.track == sv;
            else if (k == "vehicle_package")
                ok = ctx.vehicle_package == sv;
            else if (k == "session_type") {
                ok = false;
                if (v.is_array()) {
                    for (auto &e : *v.if_array())
                        ok = ok || e.as_string() == ctx.session_type;
                } else
                    ok = ctx.session_type == sv;
            } else if (k == "year") {
                if (!ctx.has_year)
                    ok = false;
                else if (v.is_array())
                    ok = ctx.year >= v[0].as_int() && ctx.year <= v[v.size() > 1 ? 1 : 0].as_int();
                else
                    ok = ctx.year == v.as_int();
            } else if (k == "any") {
                ok = false;
                if (auto list = v.if_array())
                    for (auto &e : *list)
                        ok = ok || in_ok(e);
            } else if (k == "not")
                ok = !in_ok(v);
            else
                ok = false;
            if (!ok)
                return false;
        }
        return true;
    }

    bool when_ok(const std::string &addon, const Value &when)
    {
        if (!when.is_object() || when.size() == 0)
            return true;
        std::string option(when["option"].as_string());
        Value got;
        if (!s.read_option(addon, option, got))
            return false;
        if (auto is = when.find("is"))
            return same(got, *is);
        if (auto in = when.find("in")) {
            if (auto a = in->if_array())
                for (auto &e : *a)
                    if (same(got, e))
                        return true;
            return false;
        }
        if (when.find("on"))
            return truthy(got);
        if (when.find("off"))
            return !truthy(got);
        return true;
    }

    bool active(const Layer &L)
    {
        if (dropped.count(L.handle))
            return false;
        if (L.spec.scope == Scope::profile && L.owner != ctx.profile)
            return false;
        if (L.spec.scope == Scope::career && (ctx.career.empty() || L.owner != ctx.career))
            return false;
        if (!L.spec.in.is_null() && !in_ok(L.spec.in))
            return false;
        return when_ok(L.spec.addon, L.spec.when);
    }

    std::vector<const Layer *> selectors_for(const std::string &id)
    {
        std::vector<const Layer *> out;
        for (auto &L : r.layers)
            if (L.spec.selector && active(L) && sel_matches(L, id))
                out.push_back(&L);
        return out;
    }

    bool valid(const Value &v, std::string &why)
    {
        if (!v.is_object()) {
            why = "the value is not an object";
            return false;
        }
        if (!s.validator->validate(r.def, v, why))
            return false;
        if (!r.checks.empty()) {
            std::string text = swofty::json::dump(v);
            std::vector<std::string> errors;
            for (auto &[fn, user] : r.checks) {
                CallbackGuard cb;
                fn(
                    user, SwoftyText{text.data(), text.size()},
                    [](void *c, const char *path, const char *msg) {
                        static_cast<std::vector<std::string> *>(c)->push_back(std::string(path ? path : "") + ": " +
                                                                              (msg ? msg : ""));
                    },
                    &errors);
            }
            if (!errors.empty()) {
                why = errors.front();
                return false;
            }
        }
        Value copy = v;
        for (auto &[pat, target] : r.def.refs) {
            std::vector<Hit> hits;
            walk(copy, split_pattern(pat), 0, "", "", hits);
            for (auto &h : hits) {
                if (!h.value->is_string())
                    continue;
                bool opt, ok;
                pick(target, std::string(h.value->as_string()), opt, ok);
                if (!ok && !opt) {
                    why = h.pointer + " " + std::string(h.value->as_string()) + " does not resolve in " + target;
                    return false;
                }
            }
        }
        return true;
    }

    bool apply(Res &R, const Change &c, const std::string &addon, const std::string &id, const Layer *L)
    {
        Value after;
        if (!run_change(c, R.v, after))
            return false;
        std::vector<std::pair<std::string, std::string>> blocked;
        if (L && L->spec.selector && addon != owner_of(id) && !(s.override_source && s.override_source(addon))) {
            auto pr = r.protect.find(id);
            if (pr != r.protect.end())
                for (auto &field : pr->second) {
                    std::string p = field.empty() || field[0] == '/' ? field : "/" + field;
                    const Value *b = R.v.get(p), *a = after.get(p);
                    if (same_ptr(b, a))
                        continue;
                    if (null_like(b))
                        after.remove(p);
                    else
                        after.set(p, *b);
                    blocked.emplace_back(p, addon);
                }
        }
        std::string why;
        if (!valid(after, why))
            return false;
        for (auto &p : changed(r.def, R.v, after)) {
            for (auto it = R.origins.begin(); it != R.origins.end();)
                it = under(it->first, p) ? R.origins.erase(it) : std::next(it);
            R.origins[p] = addon;
            R.changers[p].push_back(addon);
        }
        for (auto &[p, a] : blocked)
            R.blocked[p].push_back(a);
        R.v = std::move(after);
        for (auto it = R.origins.begin(); it != R.origins.end();)
            it = present(R.v, it->first) ? std::next(it) : R.origins.erase(it);
        return true;
    }

    Res compute(const std::string &id, const std::set<uint32_t> &excluded)
    {
        std::string key = id;
        for (auto h : excluded)
            key += "\x1f" + std::to_string(h);
        auto it = memo.find(key);
        if (it != memo.end())
            return it->second;
        Res R;
        if (excluded.empty() && frozen(id, R)) {
            memo[key] = R;
            return R;
        }
        std::vector<std::string> path;
        int cs = chain_state(r, id, &path);
        if (cs == 1) {
            memo[key] = R;
            return R;
        }
        if (cs != 0) {
            R.st = Status::invalid;
            R.why = cs == 2 ? "its chain of bases loops" : "its chain of bases is too deep";
            memo[key] = R;
            return R;
        }
        R.owner = owner_of(path.front());
        if (path.size() == 1) {
            Value v;
            if (r.def.shared) {
                auto p = prov(r, id);
                v = p ? p->value : Value::object();
                std::string w = winner(r, id);
                if (w.empty()) {
                    memo[key] = R;
                    return R;
                }
                v.set(r.def.resolved, Value(w));
                R.owner = "game";
            } else if (auto dv = dynamic(r, id)) {
                v = *dv;
            } else {
                v = prov(r, id)->value;
            }
            std::string why;
            if (!valid(v, why)) {
                R.st = Status::invalid;
                R.why = why;
                memo[key] = R;
                return R;
            }
            R.st = Status::ok;
            R.v = v;
        } else {
            std::set<uint32_t> ex = excluded;
            for (auto *L : selectors_for(id))
                ex.insert(L->handle);
            Res B = compute(path[path.size() - 2], ex);
            if (B.st != Status::ok) {
                R.st = B.st == Status::no_record ? Status::no_record : Status::invalid;
                if (R.st == Status::invalid)
                    R.why = "its base " + path[path.size() - 2] + " is invalid" + (B.why.empty() ? "" : ": " + B.why);
                memo[key] = R;
                return R;
            }
            std::string root_owner = R.owner;
            R = B;
            R.owner = root_owner;
            const Prov *p = prov(r, id);
            if (!apply(R, p->change, p->addon, id, nullptr)) {
                if (excluded.empty())
                    variant_refusals.push_back({p->addon, r.def.name, id, "invalid", "the variant's edit failed", p->op});
                Res bad;
                bad.st = Status::invalid;
                bad.why = "the variant's edit failed";
                memo[key] = bad;
                return bad;
            }
        }
        apply_layers(R, id, excluded, false);
        memo[key] = R;
        return R;
    }

    // W4: a career's frozen value for id, converted to the registry's major, plus the career's layers.
    bool frozen(const std::string &id, Res &R)
    {
        if (!s.snapshot || ctx.career.empty() || !r.def.snapshot)
            return false;
        Frozen f;
        {
            CallbackGuard cb;
            f = s.snapshot(r.def.name, id, ctx);
        }
        if (!f.found || !f.value.is_object())
            return false;
        Value v = f.value;
        if (f.major && f.major != r.def.major) {
            const Adapter *a = nullptr;
            for (auto &x : r.adapters)
                if (int(x.from) == f.major && int(x.to) == r.def.major && (!a || (a->down && !x.down)))
                    a = &x;
            std::string in = swofty::json::dump(v), conv;
            int rc = SWOFTY_ASSET_UNAVAILABLE;
            if (a) {
                CallbackGuard cb;
                rc = call_sized(conv, [&](char *o, std::size_t cap, std::size_t *len) {
                    return a->up(a->user, SwoftyText{in.data(), in.size()}, o, cap, len);
                });
            }
            swofty::json::Error e;
            Value up = rc == SWOFTY_ASSET_OK ? swofty::json::parse(conv, &e) : Value();
            if (rc != SWOFTY_ASSET_OK || !e.message.empty() || !up.is_object()) {
                s.note("[registry] " + r.def.name + " " + id + ": snapshot of schema " + std::to_string(f.major) +
                       " has no " + (a ? "working " : "") + "migration to " + std::to_string(r.def.major) +
                       "; this career reads it live");
                return false;
            }
            v = std::move(up);
        }
        R = Res{};
        R.st = Status::ok;
        R.v = std::move(v);
        R.owner = owner_of(id);
        apply_layers(R, id, {}, true);
        return true;
    }

    void apply_layers(Res &R, const std::string &id, const std::set<uint32_t> &excluded, bool career_only)
    {
        std::vector<const Layer *> ls;
        for (auto &L : r.layers) {
            if (!active(L) || (career_only && L.spec.scope != Scope::career))
                continue;
            if (L.spec.selector ? (!excluded.count(L.handle) && sel_matches(L, id)) : L.spec.id == id)
                ls.push_back(&L);
        }
        std::stable_sort(ls.begin(), ls.end(), [&](const Layer *a, const Layer *b) {
            auto ka = std::make_tuple(s.addon_index(a->spec.addon), int(a->spec.scope), a->spec.selector ? 0 : 1, a->seq);
            auto kb = std::make_tuple(s.addon_index(b->spec.addon), int(b->spec.scope), b->spec.selector ? 0 : 1, b->seq);
            return ka < kb;
        });
        for (auto *L : ls)
            if (!apply(R, L->spec.change, L->spec.addon, id, L))
                failures[L->handle].insert(id);
    }

    void finalize(Res &R)
    {
        if (R.st != Status::ok)
            return;
        struct Removal {
            std::string array;
            std::size_t index;
        };
        std::vector<Removal> removals;
        std::vector<std::string> nulls;
        for (auto &[pat, target] : r.def.refs) {
            std::vector<Hit> hits;
            walk(R.v, split_pattern(pat), 0, "", "", hits);
            for (auto &h : hits) {
                if (!h.value->is_string())
                    continue;
                bool opt, ok;
                std::string rid = pick(target, std::string(h.value->as_string()), opt, ok);
                if (ok) {
                    *h.value = Value(rid);
                    continue;
                }
                if (!opt) {
                    R.st = Status::invalid;
                    R.why = h.pointer + " " + std::string(h.value->as_string()) + " does not resolve in " + target;
                    return;
                }
                if (h.element.empty()) {
                    nulls.push_back(h.pointer);
                } else {
                    auto slash = h.element.find_last_of('/');
                    removals.push_back({h.element.substr(0, slash),
                                        static_cast<std::size_t>(std::stoull(h.element.substr(slash + 1)))});
                }
            }
        }
        for (auto &p : nulls)
            R.v.set(p, Value());
        std::sort(removals.begin(), removals.end(), [](const Removal &a, const Removal &b) {
            return a.array != b.array ? a.array > b.array : a.index > b.index;
        });
        removals.erase(std::unique(removals.begin(), removals.end(),
                                   [](const Removal &a, const Removal &b) { return a.array == b.array && a.index == b.index; }),
                       removals.end());
        for (auto &rm : removals)
            if (auto a = R.v.get(rm.array); a && a->is_array() && rm.index < a->if_array()->size())
                a->if_array()->erase(a->if_array()->begin() + static_cast<std::ptrdiff_t>(rm.index));
        for (auto it = R.origins.begin(); it != R.origins.end();)
            it = present(R.v, it->first) ? std::next(it) : R.origins.erase(it);
    }
};

std::string origin_in(const Res &R, const std::string &pointer)
{
    auto it = R.origins.find(pointer);
    if (it != R.origins.end())
        return it->second;
    std::string best, who;
    for (auto &[p, a] : R.origins)
        if (under(pointer, p) && p.size() > best.size())
            best = p, who = a;
    return who.empty() ? R.owner : who;
}

}

void Store::Impl::evaluate(Reg &r)
{
    if (!option_reads.empty() && !tl_callback_depth)
        recheck_options();
    if (r.eval.gen == gen)
        return;
    Eval ev;
    ev.gen = gen;
    std::set<std::string> computing;
    auto *prev = tl_computing;
    std::set<std::string> merged = prev ? *prev : std::set<std::string>{};
    merged.insert(r.def.name);
    tl_computing = &merged;
    for (int pass = 0; pass < 64; pass++) {
        Engine e(*this, r, ev.dropped);
        ev.ids = e.all_ids(r);
        ev.top.clear();
        for (auto &id : ev.ids) {
            Res R = e.compute(id, {});
            e.finalize(R);
            ev.top[id] = std::move(R);
        }
        std::vector<uint32_t> fresh;
        for (auto &[h, ids] : e.failures)
            if (!ev.dropped.count(h))
                fresh.push_back(h);
        if (fresh.empty()) {
            ev.refusals.insert(ev.refusals.end(), e.variant_refusals.begin(), e.variant_refusals.end());
            for (auto &[id, R] : ev.top) {
                if (R.st == Status::invalid && !R.why.empty() && reported_invalid.insert(r.def.name + " " + id + " " + R.why).second)
                    note("[registry] " + r.def.name + " " + id + " is invalid: " + R.why);
                if (R.st != Status::ok)
                    continue;
                for (auto &[p, who] : R.changers) {
                    std::vector<std::string> u;
                    for (auto &a : who)
                        if (std::find(u.begin(), u.end(), a) == u.end())
                            u.push_back(a);
                    if (u.size() < 2)
                        continue;
                    std::stable_sort(u.begin(), u.end(),
                                     [&](const std::string &a, const std::string &b) { return addon_index(a) < addon_index(b); });
                    ev.conflicts.push_back({"field", r.def.name, id, p, u, origin_in(R, p)});
                }
                for (auto &[p, who] : R.blocked) {
                    std::vector<std::string> u;
                    for (auto &a : who)
                        if (std::find(u.begin(), u.end(), a) == u.end())
                            u.push_back(a);
                    std::stable_sort(u.begin(), u.end(),
                                     [&](const std::string &a, const std::string &b) { return addon_index(a) < addon_index(b); });
                    ev.conflicts.push_back({"protected", r.def.name, id, p, u, origin_in(R, p)});
                }
            }
            for (auto &[id, list] : r.provs) {
                if (r.def.shared || list.size() < 2 || list.back().addon == "game")
                    continue;
                ev.conflicts.push_back({"provider", r.def.name, id, "", {list.back().addon}, list.back().addon});
            }
            if (r.def.shared)
                for (auto &id : ev.ids) {
                    auto &adds = e.shared_addons[id];
                    if (adds.size() > 1)
                        ev.conflicts.push_back({"provider", r.def.name, id, r.def.resolved, adds, owner_of(e.winner(r, id))});
                }
            std::map<std::string, std::vector<std::string>> graph;
            for (auto &[id, list] : r.provs)
                if (!list.empty() && list.back().variant)
                    graph[id] = {list.back().base};
            std::set<std::string> reported;
            for (auto &[start, b] : graph) {
                std::vector<std::string> path;
                std::string cur = start;
                std::set<std::string> seen;
                while (graph.count(cur) && !seen.count(cur) && !e.dynamic(r, cur)) {
                    seen.insert(cur);
                    path.push_back(cur);
                    cur = graph[cur].front();
                }
                if (!seen.count(cur))
                    continue;
                auto from = std::find(path.begin(), path.end(), cur);
                std::vector<std::string> loop(from, path.end());
                std::string worst;
                std::pair<int, uint64_t> wk{-1, 0};
                for (auto &id : loop) {
                    auto &p = r.provs[id].back();
                    std::pair<int, uint64_t> k{addon_index(p.addon), p.seq};
                    if (k > wk)
                        wk = k, worst = id;
                }
                if (!worst.empty() && reported.insert(worst).second) {
                    auto &p = r.provs[worst].back();
                    ev.refusals.push_back({p.addon, r.def.name, worst, "cycle", "the variant closes a cycle of bases", p.op});
                }
            }
            for (auto &[id, b] : graph) {
                std::vector<std::string> path;
                if (e.chain_state(r, id, &path) == 3 && e.chain_state(r, b.front()) == 0) {
                    auto &p = r.provs[id].back();
                    ev.refusals.push_back({p.addon, r.def.name, id, "too_deep", "the variant chain is deeper than 8", p.op});
                }
            }
            break;
        }
        for (auto h : fresh) {
            ev.dropped.insert(h);
            for (auto &L : r.layers)
                if (L.handle == h) {
                    std::string first = *e.failures[h].begin();
                    ev.refusals.push_back({L.spec.addon, r.def.name, first, "invalid",
                                           "layer dropped: its edit's result failed validation on " + first, L.op});
                    note("[registry] " + r.def.name + " " + first + ": layer of " + L.spec.addon + " (" +
                         scope_name(L.spec.scope) + ") dropped: its result failed validation");
                }
        }
    }
    tl_computing = prev;
    std::sort(ev.refusals.begin(), ev.refusals.end(), [](const Refusal &a, const Refusal &b) {
        return std::tie(a.addon, a.registry, a.id, a.op, a.reason) < std::tie(b.addon, b.registry, b.id, b.op, b.reason);
    });
    ev.refusals.erase(std::unique(ev.refusals.begin(), ev.refusals.end(),
                                  [](const Refusal &a, const Refusal &b) {
                                      return std::tie(a.addon, a.registry, a.id, a.op, a.reason) ==
                                             std::tie(b.addon, b.registry, b.id, b.op, b.reason);
                                  }),
                      ev.refusals.end());
    r.eval = std::move(ev);
}

Store::Store() : d(new Impl) {}
Store::~Store() { delete d; }

void Store::set_order(const std::vector<std::string> &order)
{
    std::lock_guard g(d->mu);
    d->order.clear();
    d->index.clear();
    for (auto &a : order)
        if (a != "game")
            d->addon_index(a);
    d->touch();
}

std::vector<std::string> Store::order() const
{
    std::lock_guard g(d->mu);
    return d->order;
}

void Store::set_validator(Validator *v)
{
    std::lock_guard g(d->mu);
    d->validator = v ? v : &default_validator();
    d->touch();
}

void Store::set_option_source(std::function<bool(const std::string &, const std::string &, Value &)> fn)
{
    std::lock_guard g(d->mu);
    d->option_source = std::move(fn);
    d->touch();
}

void Store::set_override_source(std::function<bool(const std::string &)> fn)
{
    std::lock_guard g(d->mu);
    d->override_source = std::move(fn);
    d->touch();
}

void Store::set_log(std::function<void(const std::string &)> fn)
{
    std::lock_guard g(d->mu);
    d->log = std::move(fn);
}

void Store::set_context(const Context &ctx)
{
    std::lock_guard g(d->mu);
    d->ctx = ctx;
    d->touch();
}

Context Store::context() const
{
    std::lock_guard g(d->mu);
    return d->ctx;
}

void Store::end_register()
{
    std::lock_guard g(d->mu);
    d->registering = false;
}

bool Store::registering() const
{
    std::lock_guard g(d->mu);
    return d->registering;
}

void Store::begin_career_load()
{
    std::lock_guard w(d->wait_mu);
    d->loading = true;
}

void Store::end_career_load()
{
    {
        std::lock_guard w(d->wait_mu);
        d->loading = false;
    }
    d->loaded.notify_all();
    std::lock_guard g(d->mu);
    d->touch();
}

void Store::set_flow_thread(bool fn()) { d->is_flow = fn; }

int Store::declare_def(const Def &def, const std::string &addon, const Value *schema)
{
    std::lock_guard g(d->mu);
    auto it = d->regs.find(def.name);
    if (it == d->regs.end()) {
        Reg r;
        r.def = def;
        r.majors[def.major] = def;
        if (!addon.empty() && schema)
            r.schema_of[addon] = std::make_shared<const Value>(*schema);
        d->regs.emplace(def.name, std::move(r));
        if (def.name == "vehicle" || def.name == "vehicle.handling")
            d->regs[def.name].tags["c/f1_cars"];
        if (def.name == "track")
            d->regs[def.name].tags["c/street_circuits"];
        d->touch();
        auto p = d->pending.find(def.name);
        if (p != d->pending.end()) {
            auto replays = std::move(p->second);
            d->pending.erase(p);
            d->note("[registry] " + def.name + ": declared; applying " + std::to_string(replays.size()) +
                    " call(s) made before it was declared, in their original order");
            for (auto &fn : replays)
                fn();
        }
        return SWOFTY_ASSET_OK;
    }
    Reg &r = it->second;
    if (!addon.empty() && schema)
        r.schema_of[addon] = std::make_shared<const Value>(*schema);
    auto m = r.majors.find(def.major);
    if (m == r.majors.end() || m->second.minor < def.minor)
        r.majors[def.major] = def;
    if (r.def.core && !def.core)
        return SWOFTY_ASSET_OK;
    if (def.core || (def.major == r.def.major && def.minor > r.def.minor)) {
        r.def = def;
        d->touch();
    }
    return SWOFTY_ASSET_OK;
}

int Store::declare(const std::string &name, const Value &schema, bool core, const std::string &addon)
{
    if (name.empty() || !schema.is_object()) {
        set_last_error("declare: a registry name and a schema object are needed");
        return SWOFTY_ASSET_INVALID;
    }
    Def def = parse_schema(name, schema);
    def.core = core;
    return declare_def(def, addon, &schema);
}

bool Store::declared(const std::string &name) const
{
    std::lock_guard g(d->mu);
    return d->reg(name) != nullptr;
}

int Store::add_base(const std::string &registry, const std::string &key, const Value &value)
{
    std::lock_guard g(d->mu);
    Reg *r = d->reg(registry);
    if (!r)
        return SWOFTY_ASSET_UNAVAILABLE;
    std::string id = r->def.shared && !r->def.shared_prefix.empty() ? r->def.shared_prefix + "/" + key : "game/" + key;
    r->provs[id].push_back({"game", false, strip_meta(value), {}, {}, ++d->seq, -1, "core"});
    d->touch();
    return SWOFTY_ASSET_OK;
}

int Store::define_tag(const std::string &registry, const std::string &tag, const std::vector<std::string> &members)
{
    std::lock_guard g(d->mu);
    Reg *r = d->reg(registry);
    if (!r)
        return SWOFTY_ASSET_UNAVAILABLE;
    auto &t = r->tags[tag];
    for (auto &m : members)
        t.push_back({m, "game"});
    d->touch();
    return SWOFTY_ASSET_OK;
}

void Store::stock_loaded(const std::string &registry, bool loaded)
{
    std::lock_guard g(d->mu);
    if (Reg *r = d->reg(registry)) {
        r->stock = loaded;
        d->touch();
    }
}

int Store::add(const std::string &addon, const std::string &registry, const std::string &key, const Value &value,
               std::string *id, const std::string &source)
{
    std::lock_guard g(d->mu);
    d->addon_index(addon);
    int op = d->next_op(addon);
    std::string rid = addon + "/" + key;
    if (tl_callback_depth)
        return d->refuse(addon, registry, rid, op, "late", "add from inside a registry callback is refused", SWOFTY_ASSET_LATE);
    Reg *r = d->reg(registry);
    if (!r) {
        if (id)
            *id = rid;
        return d->defer(registry, op, [this, addon, registry, key, value, source] { add(addon, registry, key, value, nullptr, source); });
    }
    if (addon.empty() || !is_key(key))
        return d->refuse(addon, registry, rid, op, "invalid", "add: '" + key + "' is not a key", SWOFTY_ASSET_INVALID);
    if (d->late_for(*r))
        return d->refuse(addon, registry, rid, op, "late",
                         "add by " + addon + " refused (late): " + registry +
                             " applies at boot; move the call into SWOFTY_REGISTER",
                         SWOFTY_ASSET_LATE);
    if (!value.is_object())
        return d->refuse(addon, registry, rid, op, "invalid", "add: the value is not an object", SWOFTY_ASSET_INVALID);
    auto &list = r->provs[rid];
    if (!list.empty())
        d->note("[registry] " + registry + " " + rid + ": added twice by " + addon + " (" + list.back().source +
                ", then " + source + "); the later wins. The conflicts report lists it.");
    list.push_back({addon, false, strip_meta(value), {}, {}, d->take_seq(), op, source});
    if (auto pf = value.find("$protect"); pf && pf->is_array())
        for (auto &f : *pf->if_array())
            if (f.is_string())
                r->protect[rid].emplace_back(f.as_string());
    if (id)
        *id = rid;
    d->touch();
    return SWOFTY_ASSET_OK;
}

int Store::put_older(const std::string &addon, const std::string &registry, const std::string &key, const Value *value,
                     const std::string &source)
{
    std::lock_guard g(d->mu);
    d->addon_index(addon);
    std::string rid = addon + "/" + key;
    Reg *r = d->reg(registry);
    if (!r)
        return SWOFTY_ASSET_UNAVAILABLE;
    if (addon.empty() || !is_key(key))
        return d->refuse(addon, registry, rid, d->next_op(addon), "invalid", "add: '" + key + "' is not a key",
                         SWOFTY_ASSET_INVALID);
    auto it = r->provs.find(rid);
    bool had = it != r->provs.end() && !it->second.empty();
    if (!value) {
        if (had) {
            r->provs.erase(it);
            d->touch();
        }
        return SWOFTY_ASSET_OK;
    }
    if (!value->is_object())
        return d->refuse(addon, registry, rid, d->next_op(addon), "invalid", "add: the value is not an object",
                         SWOFTY_ASSET_INVALID);
    if (had) {
        Prov &p = it->second.back();
        if (same(p.value, *value))
            return SWOFTY_ASSET_OK;
        p.value = strip_meta(*value);
    } else {
        r->provs[rid].push_back({addon, false, strip_meta(*value), {}, {}, d->take_seq(), d->next_op(addon), source});
        d->note("[registry] " + registry + " " + rid + ": provided by " + addon + " (" + source + ")");
    }
    d->touch();
    return SWOFTY_ASSET_OK;
}

std::vector<std::string> Store::layer_targets(const std::string &registry)
{
    std::lock_guard g(d->mu);
    std::vector<std::string> out;
    if (Reg *r = d->reg(registry))
        for (auto &L : r->layers)
            if (!L.spec.selector && !L.spec.id.empty())
                out.push_back(L.spec.id);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

int Store::variant(const std::string &addon, const std::string &registry, const std::string &key, const std::string &base,
                   const Change &change, std::string *id, const std::string &source)
{
    std::lock_guard g(d->mu);
    d->addon_index(addon);
    int op = d->next_op(addon);
    std::string rid = addon + "/" + key;
    if (tl_callback_depth)
        return d->refuse(addon, registry, rid, op, "late", "variant from inside a registry callback is refused",
                         SWOFTY_ASSET_LATE);
    Reg *r = d->reg(registry);
    if (!r) {
        if (id)
            *id = rid;
        return d->defer(registry, op,
                        [this, addon, registry, key, base, change, source] { variant(addon, registry, key, base, change, nullptr, source); });
    }
    if (addon.empty() || !is_key(key) || base.empty())
        return d->refuse(addon, registry, rid, op, "invalid", "variant: bad key or base", SWOFTY_ASSET_INVALID);
    if (d->late_for(*r))
        return d->refuse(addon, registry, rid, op, "late",
                         "variant by " + addon + " refused (late): " + registry + " applies at boot",
                         SWOFTY_ASSET_LATE);
    Change c = change;
    if (!c.is_patch && !c.schema)
        if (auto so = r->schema_of.find(addon); so != r->schema_of.end())
            c.schema = so->second;
    if (c.is_patch) {
        if (auto pf = c.patch.find("$protect"); pf && pf->is_array())
            for (auto &f : *pf->if_array())
                if (f.is_string())
                    r->protect[rid].emplace_back(f.as_string());
        c.patch = strip_meta(c.patch);
        c.patch.erase("extends");
    }
    r->provs[rid].push_back({addon, true, {}, base, c, d->take_seq(), op, source});
    if (id)
        *id = rid;
    d->note("[registry] " + registry + " " + rid + ": variant of " + base);
    d->touch();
    return SWOFTY_ASSET_OK;
}

int Store::provide_fn(const std::string &addon, const std::string &registry, SwoftyProvideFn fn, SwoftyListFn list,
                      void *user)
{
    std::lock_guard g(d->mu);
    d->addon_index(addon);
    int op = d->next_op(addon);
    Reg *r = d->reg(registry);
    if (!r)
        return d->refuse(addon, registry, "", op, "unavailable", "provide: registry " + registry + " is not declared",
                         SWOFTY_ASSET_UNAVAILABLE);
    if (!fn || addon.empty())
        return d->refuse(addon, registry, "", op, "invalid", "provide: no callback", SWOFTY_ASSET_INVALID);
    if (d->late_for(*r))
        return d->refuse(addon, registry, "", op, "late", "provide by " + addon + " refused (late): " + registry +
                                                               " applies at boot", SWOFTY_ASSET_LATE);
    r->dyns.push_back({addon, fn, list, user, d->take_seq(), {}, false, {}});
    d->touch();
    return SWOFTY_ASSET_OK;
}

int Store::invalidate(const std::string &registry, const std::string *id)
{
    std::vector<std::pair<SwoftyRecordFn, void *>> fire;
    std::vector<std::string> ids;
    {
        std::lock_guard g(d->mu);
        Reg *r = d->reg(registry);
        if (!r)
            return SWOFTY_ASSET_UNAVAILABLE;
        for (auto &dy : r->dyns) {
            if (id)
                dy.cache.erase(*id);
            else
                dy.cache.clear();
            dy.listed = false;
            dy.ids.clear();
        }
        d->touch();
        if (id)
            ids.push_back(*id);
        else {
            d->evaluate(*r);
            ids = r->eval.ids;
        }
        for (auto &w : d->watchers)
            if (w.registry == r->def.name)
                fire.emplace_back(w.fn, w.user);
    }
    for (auto &[fn, user] : fire) {
        for (auto &i : ids)
            fn(user, registry.c_str(), i.c_str());
        fn(user, registry.c_str(), "");
    }
    return SWOFTY_ASSET_OK;
}

int Store::layer(const LayerSpec &spec, uint32_t *handle)
{
    std::lock_guard g(d->mu);
    d->addon_index(spec.addon);
    int op = d->next_op(spec.addon);
    std::string rid = spec.selector ? std::string() : spec.id;
    if (tl_callback_depth)
        return d->refuse(spec.addon, spec.registry, rid, op, "late", "layer from inside a registry callback is refused",
                         SWOFTY_ASSET_LATE);
    Reg *r = d->reg(spec.registry);
    if (!r) {
        uint32_t h = d->next_handle++;
        d->pending_handles.insert(h);
        if (handle)
            *handle = h;
        return d->defer(spec.registry, op, [this, spec, h] {
            d->forced_handle = h;
            layer(spec, nullptr);
            d->forced_handle = 0;
        });
    }
    if (!spec.change.is_patch && spec.change.fn && !spec.change.schema) {
        auto so = r->schema_of.find(spec.addon);
        if (so != r->schema_of.end()) {
            LayerSpec with = spec;
            with.change.schema = so->second;
            d->forced_op = op;
            return layer(with, handle);
        }
    }
    if (spec.addon.empty() || (!spec.selector && spec.id.empty()))
        return d->refuse(spec.addon, spec.registry, rid, op, "invalid", "layer: no target", SWOFTY_ASSET_INVALID);
    if (spec.in.is_array() && spec.in.size() && r->def.applies == Applies::boot)
        return d->refuse(spec.addon, spec.registry, rid, op, "boot_filtered",
                         "layer from " + spec.addon + " has .in(), but " + spec.registry + " applies at boot; refused",
                         SWOFTY_ASSET_INVALID);
    if (d->late_for(*r))
        return d->refuse(spec.addon, spec.registry, rid, op, "late",
                         "layer from " + spec.addon + " refused (late): " + spec.registry +
                             " applies at boot; move the call into SWOFTY_REGISTER",
                         SWOFTY_ASSET_LATE);
    std::string owner;
    if (spec.scope == Scope::profile)
        owner = d->ctx.profile;
    if (spec.scope == Scope::career) {
        if (d->ctx.career.empty())
            return d->refuse(spec.addon, spec.registry, rid, op, "late",
                             "career layer from " + spec.addon + " refused (late): no career is loaded", SWOFTY_ASSET_LATE);
        owner = d->ctx.career;
    }
    Layer L;
    L.spec = spec;
    if (L.spec.change.is_patch)
        L.spec.change.patch = strip_meta(L.spec.change.patch);
    if (!L.spec.in.is_array())
        L.spec.in = Value();
    L.owner = owner;
    L.seq = d->take_seq();
    L.op = op;
    if (d->forced_handle) {
        L.handle = d->forced_handle;
        d->forced_handle = 0;
        d->pending_handles.erase(L.handle);
    } else
        L.handle = d->next_handle++;
    if (auto pi = d->pending_in.find(L.handle); pi != d->pending_in.end()) {
        if (!L.spec.in.is_array())
            L.spec.in = Value::array();
        for (auto &sel : pi->second)
            if (auto a = sel.if_array())
                for (auto &e : *a)
                    L.spec.in.push_back(e);
            else
                L.spec.in.push_back(sel);
        d->pending_in.erase(pi);
        if (r->def.applies == Applies::boot)
            return d->refuse(spec.addon, spec.registry, rid, op, "boot_filtered",
                             "layer from " + spec.addon + " has .in(), but " + spec.registry + " applies at boot; refused",
                             SWOFTY_ASSET_INVALID);
    }
    if (!spec.selector)
        std::erase_if(r->layers, [&](const Layer &o) {
            return !o.spec.selector && o.spec.addon == spec.addon && o.spec.id == spec.id && o.spec.scope == spec.scope &&
                   o.owner == owner;
        });
    r->layers.push_back(std::move(L));
    if (handle)
        *handle = r->layers.back().handle;
    if (!d->registering_now())
        d->note("[registry] layer from " + spec.addon + " on " + spec.registry + (rid.empty() ? "" : " " + rid) +
                " registered at run time; applies from the next read of that data");
    d->touch();
    return SWOFTY_ASSET_OK;
}

int Store::layer_in(const std::string &addon, uint32_t handle, const Value &selectors)
{
    std::lock_guard g(d->mu);
    for (auto &[name, r] : d->regs)
        for (auto it = r.layers.begin(); it != r.layers.end(); ++it) {
            if (it->handle != handle)
                continue;
            if (it->spec.addon != addon && !addon.empty())
                return SWOFTY_ASSET_INVALID;
            if (r.def.applies == Applies::boot) {
                int op = it->op;
                std::string id = it->spec.selector ? std::string() : it->spec.id;
                std::string who = it->spec.addon;
                r.layers.erase(it);
                d->touch();
                return d->refuse(who, name, id, op, "boot_filtered",
                                 "layer from " + who + " has .in(), but " + name + " applies at boot; refused",
                                 SWOFTY_ASSET_INVALID);
            }
            if (!it->spec.in.is_array())
                it->spec.in = Value::array();
            if (auto a = selectors.if_array())
                for (auto &e : *a)
                    it->spec.in.push_back(e);
            else if (selectors.is_object())
                it->spec.in.push_back(selectors);
            d->touch();
            return SWOFTY_ASSET_OK;
        }
    if (d->pending_handles.count(handle)) {
        d->pending_in[handle].push_back(selectors);
        return SWOFTY_ASSET_OK;
    }
    set_last_error("layer_in: no layer with that handle");
    return SWOFTY_ASSET_INVALID;
}

int Store::clear_layer(const std::string &addon, const std::string &registry, const std::string &id, Scope scope)
{
    std::lock_guard g(d->mu);
    d->next_op(addon);
    Reg *r = d->reg(registry);
    if (!r)
        return SWOFTY_ASSET_UNAVAILABLE;
    std::string owner = scope == Scope::profile ? d->ctx.profile : scope == Scope::career ? d->ctx.career : "";
    auto n = std::erase_if(r->layers, [&](const Layer &o) {
        return !o.spec.selector && o.spec.addon == addon && o.spec.id == id && o.spec.scope == scope && o.owner == owner;
    });
    d->touch();
    return n ? SWOFTY_ASSET_OK : SWOFTY_ASSET_OK;
}

int Store::tag_add(const std::string &addon, const std::string &registry, const std::string &tag, const std::string &id)
{
    std::lock_guard g(d->mu);
    d->addon_index(addon);
    int op = d->next_op(addon);
    Reg *r = d->reg(registry);
    if (!r)
        return d->refuse(addon, registry, id, op, "unavailable", "tag_add: registry " + registry + " is not declared",
                         SWOFTY_ASSET_UNAVAILABLE);
    bool own_tag = tag.rfind(addon + "/", 0) == 0;
    if (!r->tags.count(tag) && !own_tag)
        return d->refuse(addon, registry, id, op, "invalid",
                         "tag_add: " + tag + " is not a tag the core defines on " + registry, SWOFTY_ASSET_INVALID);
    r->tags[tag].push_back({id, addon});
    d->touch();
    return SWOFTY_ASSET_OK;
}

int Store::protect(const std::string &addon, const std::string &registry, const std::string &id,
                   const std::vector<std::string> &fields)
{
    std::lock_guard g(d->mu);
    d->addon_index(addon);
    int op = d->next_op(addon);
    Reg *r = d->reg(registry);
    if (!r)
        return d->refuse(addon, registry, id, op, "unavailable", "protect: registry " + registry + " is not declared",
                         SWOFTY_ASSET_UNAVAILABLE);
    if (owner_of(id) != addon)
        return d->refuse(addon, registry, id, op, "invalid", "protect: " + addon + " can protect only its own records",
                         SWOFTY_ASSET_INVALID);
    auto &list = r->protect[id];
    for (auto &f : fields)
        if (std::find(list.begin(), list.end(), f) == list.end())
            list.push_back(f);
    d->touch();
    return SWOFTY_ASSET_OK;
}

int Store::check(const std::string &addon, const std::string &registry, SwoftyCheckFn fn, void *user)
{
    std::lock_guard g(d->mu);
    Reg *r = d->reg(registry);
    if (!r || !fn)
        return SWOFTY_ASSET_UNAVAILABLE;
    if (addon != "game" && addon != "swofty.core" && registry.rfind(addon + ".", 0) != 0 && registry != addon) {
        set_last_error("check: only the registry's owner adds its rules");
        return SWOFTY_ASSET_INVALID;
    }
    r->checks.emplace_back(fn, user);
    d->touch();
    return SWOFTY_ASSET_OK;
}

int Store::adapter(const std::string &, const std::string &registry, uint32_t from, uint32_t to, SwoftyConvertFn up,
                   SwoftyConvertFn down, void *user)
{
    std::lock_guard g(d->mu);
    Reg *r = d->reg(registry);
    if (!r || !up || from == to)
        return SWOFTY_ASSET_INVALID;
    r->adapters.push_back({from, to, up, down, user});
    return SWOFTY_ASSET_OK;
}

int Store::migrate(const std::string &, const std::string &registry, uint32_t from, uint32_t to, SwoftyConvertFn fn,
                   void *user)
{
    std::lock_guard g(d->mu);
    Reg *r = d->reg(registry);
    if (!r || !fn || from == to)
        return SWOFTY_ASSET_INVALID;
    r->adapters.push_back({from, to, fn, nullptr, user});
    return SWOFTY_ASSET_OK;
}

int Store::watch(const std::string &registry, SwoftyRecordFn fn, void *user, uint32_t *token)
{
    std::lock_guard g(d->mu);
    if (!fn)
        return SWOFTY_ASSET_INVALID;
    uint32_t t = d->next_token++;
    d->watchers.push_back({t, registry, fn, user});
    if (token)
        *token = t;
    return SWOFTY_ASSET_OK;
}

int Store::unwatch(uint32_t token)
{
    std::lock_guard g(d->mu);
    auto n = std::erase_if(d->watchers, [&](const Watcher &w) { return w.token == token; });
    return n ? SWOFTY_ASSET_OK : SWOFTY_ASSET_INVALID;
}

Record Store::get(const std::string &registry_at, const std::string &id)
{
    d->wait_for_load();
    std::unique_lock g(d->mu);
    Record out;
    Reg *r = d->reg(registry_at);
    if (!r) {
        out.status = Status::no_registry;
        return out;
    }
    if (tl_computing && tl_computing->count(r->def.name)) {
        out.status = Status::late;
        return out;
    }
    d->evaluate(*r);
    auto it = r->eval.top.find(id);
    Res R;
    if (it != r->eval.top.end())
        R = it->second;
    else {
        Engine e(*d, *r, r->eval.dropped);
        R = e.compute(id, {});
        e.finalize(R);
    }
    out.status = R.st;
    out.owner = R.owner;
    out.why = R.why;
    if (R.st != Status::ok)
        return out;
    out.value = R.v;
    out.origins = R.origins;
    auto at = registry_at.find('@');
    if (at != std::string::npos) {
        uint32_t want = static_cast<uint32_t>(std::atoi(registry_at.c_str() + at + 1));
        uint32_t have = static_cast<uint32_t>(r->def.major);
        if (want && want != have) {
            const Adapter *a = nullptr;
            bool up = false;
            for (auto &x : r->adapters)
                if (x.from == have && x.to == want)
                    a = &x, up = true;
                else if (x.from == want && x.to == have && x.down)
                    a = &x, up = false;
            if (!a) {
                out.status = Status::no_registry;
                out.value = Value();
                return out;
            }
            std::string in = swofty::json::dump(out.value), conv;
            CallbackGuard cb;
            int rc = call_sized(conv, [&](char *o, std::size_t cap, std::size_t *len) {
                return (up ? a->up : a->down)(a->user, SwoftyText{in.data(), in.size()}, o, cap, len);
            });
            swofty::json::Error e;
            Value v = rc == SWOFTY_ASSET_OK ? swofty::json::parse(conv, &e) : Value();
            if (rc != SWOFTY_ASSET_OK || !e.message.empty()) {
                out.status = Status::invalid;
                out.value = Value();
                return out;
            }
            out.value = v;
        }
    }
    return out;
}

Status Store::status(const std::string &registry_at, const std::string &id) { return get(registry_at, id).status; }

std::string Store::origin(const std::string &registry, const std::string &id, const std::string &pointer)
{
    d->wait_for_load();
    std::lock_guard g(d->mu);
    Reg *r = d->reg(registry);
    if (!r)
        return {};
    d->evaluate(*r);
    auto it = r->eval.top.find(id);
    if (it == r->eval.top.end() || it->second.st != Status::ok)
        return {};
    std::string p = pointer.empty() || pointer[0] == '/' ? pointer : "/" + pointer;
    return origin_in(it->second, p);
}

std::size_t Store::pending(const std::string &registry) const
{
    std::lock_guard g(d->mu);
    std::size_t n = 0;
    for (auto &[name, calls] : d->pending)
        if (registry.empty() || name == registry)
            n += calls.size();
    return n;
}

bool Store::next(const std::string &registry, const std::string &prefix, const std::string *after, std::string &id)
{
    d->wait_for_load();
    std::lock_guard g(d->mu);
    Reg *r = d->reg(registry);
    if (!r)
        return false;
    d->evaluate(*r);
    auto &top = r->eval.top;
    auto it = after ? top.upper_bound(*after) : top.lower_bound(prefix);
    for (; it != top.end(); ++it) {
        if (it->first.compare(0, prefix.size(), prefix) != 0)
            return false;
        if (it->second.st != Status::no_record) {
            id = it->first;
            return true;
        }
    }
    return false;
}

std::vector<std::string> Store::list(const std::string &registry, const std::string &prefix)
{
    d->wait_for_load();
    std::lock_guard g(d->mu);
    std::vector<std::string> out;
    Reg *r = d->reg(registry);
    if (!r)
        return out;
    d->evaluate(*r);
    for (auto &[id, R] : r->eval.top)
        if (R.st != Status::no_record && id.compare(0, prefix.size(), prefix) == 0)
            out.push_back(id);
    return out;
}

std::vector<std::string> Store::tag_members(const std::string &registry, const std::string &tag)
{
    d->wait_for_load();
    std::lock_guard g(d->mu);
    Reg *r = d->reg(registry);
    if (!r)
        return {};
    Engine e(*d, *r, r->eval.dropped);
    return e.members(*r, tag);
}

void Store::set_snapshot_source(SnapshotSource fn)
{
    std::lock_guard g(d->mu);
    d->snapshot = std::move(fn);
    d->touch();
}

Record Store::get_live(const std::string &registry, const std::string &id, const Context &ctx)
{
    std::lock_guard g(d->mu);
    Record out;
    Reg *r = d->reg(registry);
    if (!r) {
        out.status = Status::no_registry;
        return out;
    }
    if (tl_computing && tl_computing->count(r->def.name)) {
        out.status = Status::late;
        return out;
    }
    d->evaluate(*r);
    Context c = ctx;
    c.career.clear();
    Engine e(*d, *r, r->eval.dropped, c);
    Res R = e.compute(id, {});
    e.finalize(R);
    out.status = R.st;
    out.owner = R.owner;
    out.why = R.why;
    if (R.st == Status::ok) {
        out.value = std::move(R.v);
        out.origins = std::move(R.origins);
    }
    return out;
}

std::optional<Def> Store::def(const std::string &registry) const
{
    std::lock_guard g(d->mu);
    auto at = registry.find('@');
    auto it = d->regs.find(at == std::string::npos ? registry : registry.substr(0, at));
    if (it == d->regs.end())
        return std::nullopt;
    return it->second.def;
}

std::vector<std::string> Store::chain(const std::string &registry, const std::string &id)
{
    std::lock_guard g(d->mu);
    Reg *r = d->reg(registry);
    if (!r)
        return {};
    Engine e(*d, *r, r->eval.dropped);
    std::vector<std::string> path;
    if (e.chain_state(*r, id, &path) != 0)
        return {};
    std::reverse(path.begin(), path.end());
    return path;
}

std::vector<std::string> Store::registries() const
{
    std::lock_guard g(d->mu);
    std::vector<std::string> out;
    for (auto &[n, r] : d->regs)
        out.push_back(n);
    return out;
}

std::vector<Refusal> Store::refusals()
{
    std::lock_guard g(d->mu);
    std::vector<Refusal> out = d->refusals;
    for (auto &[n, r] : d->regs) {
        d->evaluate(r);
        out.insert(out.end(), r.eval.refusals.begin(), r.eval.refusals.end());
    }
    return out;
}

std::vector<Conflict> Store::conflicts()
{
    std::lock_guard g(d->mu);
    std::vector<Conflict> out;
    for (auto &[n, r] : d->regs) {
        d->evaluate(r);
        out.insert(out.end(), r.eval.conflicts.begin(), r.eval.conflicts.end());
    }
    return out;
}

std::size_t Store::layer_count(const std::string &addon, const std::string &registry) const
{
    std::lock_guard g(d->mu);
    std::size_t n = 0;
    for (auto &[name, r] : d->regs)
        if (registry.empty() || name == registry)
            for (auto &L : r.layers)
                n += L.spec.addon == addon;
    return n;
}

void Store::set_op_index(const std::string &addon, int op)
{
    std::lock_guard g(d->mu);
    d->ops[addon] = op;
}

bool Store::read_option(const std::string &addon, const std::string &id, Value &out)
{
    std::lock_guard g(d->mu);
    return d->read_option(addon, id, out);
}

void Store::note_refusal(const std::string &addon, const std::string &registry, const std::string &id,
                         const std::string &reason, const std::string &message)
{
    std::lock_guard g(d->mu);
    d->refuse(addon, registry, id, -1, reason, message, 0);
}

Value Store::effective_report()
{
    std::lock_guard g(d->mu);
    Value out = Value::object();
    for (auto &[name, r] : d->regs) {
        d->evaluate(r);
        Value reg = Value::object();
        for (auto &[id, R] : r.eval.top) {
            Value rec = Value::object();
            rec["status"] = Value(status_name(R.st));
            if (R.st == Status::ok) {
                rec["value"] = R.v;
                Value o = Value::object();
                for (auto &[p, a] : R.origins)
                    o[p] = Value(a);
                rec["origins"] = o;
                rec["owner"] = Value(R.owner);
            } else if (R.st == Status::invalid && !R.why.empty()) {
                rec["reason"] = Value(R.why);
            }
            if (r.def.snapshot)
                rec["snapshot"] = Value(std::to_string(r.def.major) + "." + std::to_string(r.def.minor));
            reg[id] = rec;
        }
        out[name] = reg;
    }
    return out;
}

Value Store::conflicts_report()
{
    auto cs = conflicts();
    auto rs = refusals();
    Value list = Value::array();
    for (auto &c : cs) {
        Value e = Value::object();
        e["kind"] = Value(c.kind);
        e["registry"] = Value(c.registry);
        e["id"] = Value(c.id);
        e["path"] = Value(c.pointer);
        Value a = Value::array();
        for (auto &x : c.addons)
            a.push_back(Value(x));
        e["addons"] = a;
        e["wins"] = Value(c.winner);
        list.push_back(e);
    }
    for (auto &r : rs) {
        Value e = Value::object();
        e["kind"] = Value(r.reason == "invalid" ? "invalid_layer" : "refused");
        e["registry"] = Value(r.registry);
        e["id"] = Value(r.id);
        e["addon"] = Value(r.addon);
        e["reason"] = Value(r.reason);
        e["error"] = Value(r.message);
        list.push_back(e);
    }
    Value out = Value::object();
    out["registry"] = list;
    return out;
}

}
