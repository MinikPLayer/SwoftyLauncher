// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <swofty/json.hpp>
#include <swofty/registry.hpp>
#include <swofty_api.h>

// The fake core (standard library 1.6): a SwoftyApi whose registry, hooks and services tables record every
// call instead of applying it. Unit tests run an addon's SWOFTY_REGISTER on the host with it and read back
// what the addon registered; swofty dev pack's recorder (packages/sdk/recorder) uses the same recording to
// write registry.lock.json.
//
// The registry's rules (load order, scopes, selectors, variants, origins) live in one place, the core's
// registry (native/src/core/registry/). The fake core never applies them itself: it forwards every
// registry call to an engine, the core's registry built for the host, through SwoftyFakeEngine. Without an
// engine, effective() and origin() report pending and only the recording answers.
//
//   swofty::fake::Core core{"tails.whatif-history"};
//   core.option("damage", "heavy");
//   core.run_register();
//   assert(core.ids("career.season").size() == 10);
//   assert(core.layers("career.season").size() == 1);
//   auto v = core.apply(*core.layers("career.season")[0], *core.base("career.season", "swofty.progressive/2017"));
//
// Build the test for the host with -DSWOFTY_FAKE_CORE_ENGINE=<function> and link the engine to get
// effective values and origins; the function returns the engine's SwoftyFakeEngine.

extern "C" {
typedef struct SwoftyFakeEngine {
    uint32_t size;
    const SwoftyRegistry *registry;
    void (*begin_addon)(const char *addon_id);
    void (*end_register)(void);
    void (*reset)(void);
} SwoftyFakeEngine;
}

#if defined(SWOFTY_FAKE_CORE_ENGINE)
extern "C" const SwoftyFakeEngine *SWOFTY_FAKE_CORE_ENGINE(void);
#endif

namespace swofty::fake {

inline const char *scope_name(uint32_t s)
{
    return s == SWOFTY_SCOPE_PROFILE ? "profile" : s == SWOFTY_SCOPE_CAREER ? "career" : s == SWOFTY_SCOPE_GLOBAL ? "global" : "unknown";
}

inline const char *phase_name(uint32_t p)
{
    return p == SWOFTY_PHASE_BEFORE ? "before" : p == SWOFTY_PHASE_REPLACE ? "replace" : p == SWOFTY_PHASE_AFTER ? "after" : "unknown";
}

inline const char *kind_name(uint32_t k)
{
    return k == SWOFTY_POINT_NOTIFY ? "notify" : k == SWOFTY_POINT_MODIFY ? "modify" : k == SWOFTY_POINT_CANCEL ? "cancel" : "unknown";
}

struct Layer {
    std::string registry;
    std::string id;
    bool match = false;
    std::string prefix;
    std::vector<std::string> except;
    std::string tag;
    std::string tag_field;
    uint32_t scope = SWOFTY_SCOPE_GLOBAL;
    SwoftyEditFn edit = nullptr;
    void *user = nullptr;
    json::Value patch;
    json::Value in = json::Value::array();
    uint32_t handle = 0;
    json::Value result;
};

struct Record {
    std::string op;
    std::string registry;
    std::string key;
    std::string id;
    std::string base;
    json::Value value;
};

class Core {
public:
    explicit Core(std::string addon_id = "test.addon", const SwoftyFakeEngine *engine = default_engine())
        : addon_(std::move(addon_id)), engine_(engine && engine->registry ? engine : nullptr)
    {
        fill_tables();
        current() = this;
        previous_ = swofty::api;
        swofty::api = &api_;
#if defined(_WIN32)
        LoadLibraryW(L"swofty_core.dll");
#endif
    }
    Core(const Core &) = delete;
    Core &operator=(const Core &) = delete;
    ~Core()
    {
        if (engine_ && engine_->reset)
            engine_->reset();
        swofty::api = previous_;
        if (current() == this)
            current() = nullptr;
    }

    static const SwoftyFakeEngine *default_engine()
    {
#if defined(SWOFTY_FAKE_CORE_ENGINE)
        return SWOFTY_FAKE_CORE_ENGINE();
#else
        return nullptr;
#endif
    }

    const SwoftyApi &api() const { return api_; }
    const std::string &addon() const { return addon_; }
    bool rules() const { return engine_ != nullptr; }

    void option(std::string id, json::Value value)
    {
        for (auto &o : options_)
            if (o.first == id) {
                o.second = std::move(value);
                return;
            }
        options_.emplace_back(std::move(id), std::move(value));
    }

    int run_register(int (*entry)(const SwoftyApi *) = nullptr)
    {
        if (engine_ && engine_->begin_addon)
            engine_->begin_addon(addon_.c_str());
        for (const auto &[name, schema] : declared())
            redeclare(name, schema);
        int r = entry ? entry(&api_) : swofty::detail::run_register(&api_);
        if (engine_ && engine_->end_register)
            engine_->end_register();
        result_ = r;
        return r;
    }

    template <class T> bool stock(const Registry<T> &reg, std::string_view key, const T &value)
    {
        std::string name(reg.name()), schema = reg.schema(), v;
        if constexpr (std::is_same_v<T, json::Value>)
            v = json::dump(value);
        else
            v = to_json(value);
        if (!schema.empty()) {
            redeclare(name, schema);
            bool known = false;
            for (const auto &d : declared())
                known = known || d.first == name;
            if (!known)
                declared().emplace_back(name, schema);
        }
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        stock_.push_back(Record{"stock", name, std::string(key), "game/" + std::string(key), {}, parse({v.data(), v.size()})});
        if (!engine() || !engine()->add)
            return true;
        if (engine_->begin_addon)
            engine_->begin_addon("game");
        char id[256] = {};
        int r = engine()->add(name.c_str(), std::string(key).c_str(), SwoftyText{v.data(), v.size()}, id, sizeof id);
        if (engine_->begin_addon)
            engine_->begin_addon(addon_.c_str());
        return r == SWOFTY_ASSET_OK;
    }

    const std::vector<Record> &records() const { return records_; }
    const std::vector<Layer> &all_layers() const { return layers_; }
    const std::vector<std::string> &unexpected() const { return unexpected_; }
    const std::vector<std::string> &log() const { return log_; }
    const json::Value &refused() const { return refused_; }

    std::string current_call() const
    {
        return in_call_.load() ? std::string(call_.data(), strnlen(call_.data(), call_.size())) : std::string();
    }

    void run_layer_edits()
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        for (auto &l : layers_) {
            if (!l.edit) {
                l.result = l.patch;
                continue;
            }
            json::Value base = json::Value::object();
            if (!l.id.empty())
                if (auto v = this->base(l.registry, l.id))
                    base = *v;
            enter("layer edit on " + l.registry + " " + (l.id.empty() ? "(selector " + l.prefix + ")" : l.id));
            std::string in = json::dump(base), out;
            int r = call_edit(l.edit, l.user, in, out);
            leave();
            if (r != SWOFTY_ASSET_OK)
                continue;
            json::Error err;
            json::Value v = json::parse(out, &err);
            if (err.ok())
                l.result = std::move(v);
        }
    }

    std::vector<std::string> ids(std::string_view registry, std::string_view prefix = {}) const
    {
        std::vector<std::string> out;
        for (const auto &r : records_)
            if (r.registry == registry && std::string_view(r.id).starts_with(prefix))
                out.push_back(r.id);
        return out;
    }

    std::optional<json::Value> base(std::string_view registry, std::string_view id) const
    {
        for (auto it = records_.rbegin(); it != records_.rend(); ++it)
            if (it->registry == registry && it->id == id && !it->value.is_null())
                return it->value;
        for (auto it = stock_.rbegin(); it != stock_.rend(); ++it)
            if (it->registry == registry && it->id == id && !it->value.is_null())
                return it->value;
        return std::nullopt;
    }

    std::vector<const Layer *> layers(std::string_view registry, std::string_view id = {}) const
    {
        std::vector<const Layer *> out;
        for (const auto &l : layers_)
            if (l.registry == registry && (id.empty() || l.id == id))
                out.push_back(&l);
        return out;
    }

    std::optional<json::Value> apply(const Layer &layer, const json::Value &value) const
    {
        if (layer.edit) {
            std::string in = json::dump(value), out;
            if (call_edit(layer.edit, layer.user, in, out) != SWOFTY_ASSET_OK)
                return std::nullopt;
            json::Error err;
            json::Value v = json::parse(out, &err);
            if (!err.ok())
                return std::nullopt;
            return v;
        }
        json::Value v = value;
        if (!json::merge_patch(v, layer.patch))
            return std::nullopt;
        return v;
    }

    std::optional<json::Value> effective(std::string_view registry, std::string_view id) const
    {
        if (!engine_ || !engine_->registry->get)
            return std::nullopt;
        std::string reg(registry), rid(id), buf(4096, '\0');
        size_t len = 0;
        int r = engine_->registry->get(reg.c_str(), rid.c_str(), buf.data(), buf.size(), &len);
        if (r == SWOFTY_TOO_SMALL) {
            buf.assign(len + 1, '\0');
            r = engine_->registry->get(reg.c_str(), rid.c_str(), buf.data(), buf.size(), &len);
        }
        if (r != SWOFTY_ASSET_OK)
            return std::nullopt;
        buf.resize(std::min(len, buf.size()));
        json::Error err;
        json::Value v = json::parse(buf, &err);
        if (!err.ok())
            return std::nullopt;
        return v;
    }

    template <class T> std::optional<T> effective(const Registry<T> &reg, Ref<T> id) const
    {
        auto v = effective(reg.name(), id.id());
        if (!v)
            return std::nullopt;
        T out{};
        if constexpr (std::is_same_v<T, json::Value>)
            out = *v;
        else if (!from_json(json::dump(*v), out))
            return std::nullopt;
        return out;
    }

    std::string origin(std::string_view registry, std::string_view id, std::string_view pointer) const
    {
        if (!engine_ || !engine_->registry->origin)
            return {};
        char addon[128] = {};
        std::string reg(registry), rid(id), ptr(pointer);
        if (engine_->registry->origin(reg.c_str(), rid.c_str(), ptr.c_str(), addon, sizeof addon) != SWOFTY_ASSET_OK)
            return {};
        return std::string(addon, strnlen(addon, sizeof addon));
    }

    json::Value recording() const
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        json::Value out = json::Value::object();
        out["recorder"] = 1;
        out["addon"] = addon_;
        out["result"] = result_;
        json::Value regs = json::Value::object();
        for (const auto &[name, schema] : registries_)
            regs[name] = schema;
        out["registries"] = std::move(regs);
        json::Value recs = json::Value::array();
        for (const auto &r : records_) {
            json::Value e = json::Value::object();
            e["op"] = r.op;
            e["registry"] = r.registry;
            if (!r.key.empty())
                e["key"] = r.key;
            e["id"] = r.id;
            if (!r.base.empty())
                e["base"] = r.base;
            e["value"] = r.value;
            recs.push_back(std::move(e));
        }
        out["records"] = std::move(recs);
        out["providers"] = providers_;
        json::Value layers = json::Value::array();
        for (const auto &l : layers_) {
            json::Value e = json::Value::object();
            e["registry"] = l.registry;
            if (!l.id.empty())
                e["id"] = l.id;
            else {
                json::Value sel = json::Value::object();
                if (l.match)
                    sel["where"] = true;
                if (!l.prefix.empty())
                    sel["prefix"] = l.prefix;
                if (!l.except.empty()) {
                    json::Value ex = json::Value::array();
                    for (const auto &x : l.except)
                        ex.push_back(x);
                    sel["except"] = std::move(ex);
                }
                if (!l.tag.empty())
                    sel["tag"] = l.tag;
                if (!l.tag_field.empty())
                    sel["tag_field"] = l.tag_field;
                e["select"] = std::move(sel);
            }
            e["scope"] = scope_name(l.scope);
            if (l.edit)
                e["edit"] = true;
            else
                e["patch"] = l.patch;
            if (l.in.size())
                e["in"] = l.in;
            if (!l.result.is_null())
                e["result"] = l.result;
            layers.push_back(std::move(e));
        }
        out["layers"] = std::move(layers);
        out["calls"] = calls_;
        out["subscriptions"] = subscriptions_;
        out["points"] = points_;
        json::Value services = json::Value::object();
        services["published"] = published_;
        services["required"] = required_;
        out["services"] = std::move(services);
        out["events"] = events_;
        out["reads"] = reads_;
        out["options"] = options_read_;
        json::Value log = json::Value::array();
        for (const auto &l : log_)
            log.push_back(l);
        out["log"] = std::move(log);
        json::Value un = json::Value::array();
        for (const auto &u : unexpected_)
            un.push_back(u);
        out["unexpected"] = std::move(un);
        out["refused"] = refused_;
        out["engine"] = engine_ != nullptr;
        return out;
    }

    std::recursive_mutex &mutex() const { return mutex_; }

private:
    static Core *&current()
    {
        static Core *c = nullptr;
        return c;
    }

    static std::vector<std::pair<std::string, std::string>> &declared()
    {
        static std::vector<std::pair<std::string, std::string>> d;
        return d;
    }

    void redeclare(const std::string &name, const std::string &schema)
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        for (const auto &r : registries_)
            if (r.first == name)
                return;
        SwoftyText t{schema.data(), schema.size()};
        registries_.emplace_back(name, parse(t));
        if (engine() && engine()->declare)
            engine()->declare(name.c_str(), t);
    }

    void enter(const std::string &what)
    {
        std::snprintf(call_.data(), call_.size(), "%s", what.c_str());
        in_call_ = true;
    }

    void leave() { in_call_ = false; }

    std::string engine_error() const
    {
        char buf[512] = {};
        if (engine() && engine()->last_error)
            engine()->last_error(buf, sizeof buf);
        return std::string(buf, strnlen(buf, sizeof buf));
    }

    bool accepted(int r, const char *call, const std::string &registry, const std::string &what)
    {
        if (r == SWOFTY_ASSET_OK)
            return true;
        refused_.push_back(json::Value::object(
            {{"call", call}, {"registry", registry}, {"id", what}, {"result", r}, {"error", engine_error()}}));
        return false;
    }

    static std::string str(const char *s) { return s ? std::string(s) : std::string(); }
    static std::string str(SwoftyText t) { return t.data ? std::string(t.data, t.size) : std::string(); }

    static json::Value parse(SwoftyText t)
    {
        if (!t.data)
            return nullptr;
        json::Error err;
        json::Value v = json::parse(std::string_view(t.data, t.size), &err);
        if (!err.ok())
            return json::Value::object({{"invalid_json", std::string(t.data, t.size)}});
        return v;
    }

    static int call_edit(SwoftyEditFn fn, void *user, const std::string &in, std::string &out)
    {
        out.assign(4096, '\0');
        size_t len = 0;
        int r = fn(user, SwoftyText{in.data(), in.size()}, out.data(), out.size(), &len);
        if (r == SWOFTY_TOO_SMALL) {
            out.assign(len + 1, '\0');
            r = fn(user, SwoftyText{in.data(), in.size()}, out.data(), out.size(), &len);
        }
        out.resize(r == SWOFTY_ASSET_OK ? std::min(len, out.size()) : 0);
        return r;
    }

    static int put_id(const std::string &id, char *out, size_t cap)
    {
        if (!out || cap == 0)
            return SWOFTY_ASSET_OK;
        if (id.size() + 1 > cap)
            return SWOFTY_TOO_SMALL;
        std::memcpy(out, id.c_str(), id.size() + 1);
        return SWOFTY_ASSET_OK;
    }

    static int put_text(const std::string &text, char *out, size_t cap, size_t *len)
    {
        if (len)
            *len = text.size();
        if (!out || text.size() > cap)
            return SWOFTY_TOO_SMALL;
        std::memcpy(out, text.data(), text.size());
        if (text.size() < cap)
            out[text.size()] = 0;
        return SWOFTY_ASSET_OK;
    }

    std::string own_id(const char *key) const { return addon_ + "/" + str(key); }

    void call(std::string name, json::Value detail = json::Value::object())
    {
        detail["call"] = std::move(name);
        calls_.push_back(std::move(detail));
    }

    void read(const char *call, const char *registry, const char *id)
    {
        reads_.push_back(json::Value::object({{"call", call}, {"registry", str(registry)}, {"id", str(id)}}));
    }

    const SwoftyRegistry *engine() const { return engine_ ? engine_->registry : nullptr; }

    json::Value lookup_base(const std::string &registry, const std::string &id) const
    {
        if (auto v = base(registry, id))
            return *v;
        if (auto v = effective(registry, id))
            return *v;
        return json::Value::object();
    }

#define SWOFTY_FAKE_LOCK(c) std::lock_guard<std::recursive_mutex> swofty_fake_lock((c)->mutex_)
#define SWOFTY_FAKE_FORWARD(field, ...)                                                                               \
    (c->engine() && c->engine()->field ? c->engine()->field(__VA_ARGS__) : SWOFTY_ASSET_OK)

    static int r_available(void) { return 1; }

    static int r_declare(const char *registry, SwoftyText schema)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        std::string name = str(registry);
        bool seen = false;
        for (auto &[n, s] : c->registries_)
            if (n == name) {
                seen = true;
                if (s.is_null())
                    s = parse(schema);
            }
        if (!seen)
            c->registries_.emplace_back(name, parse(schema));
        bool known = false;
        for (const auto &d : declared())
            known = known || d.first == name;
        if (!known)
            declared().emplace_back(name, str(schema));
        return SWOFTY_FAKE_FORWARD(declare, registry, schema);
    }

    static int r_check(const char *registry, SwoftyCheckFn fn, void *user)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        c->call("check", json::Value::object({{"registry", str(registry)}}));
        return SWOFTY_FAKE_FORWARD(check, registry, fn, user);
    }

    static int r_adapter(const char *registry, uint32_t from, uint32_t to, SwoftyConvertFn up, SwoftyConvertFn down,
                         void *user)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        c->call("adapter", json::Value::object({{"registry", str(registry)}, {"from", from}, {"to", to}}));
        return SWOFTY_FAKE_FORWARD(adapter, registry, from, to, up, down, user);
    }

    static int r_migrate(const char *registry, uint32_t from, uint32_t to, SwoftyConvertFn fn, void *user)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        c->call("migrate", json::Value::object({{"registry", str(registry)}, {"from", from}, {"to", to}}));
        return SWOFTY_FAKE_FORWARD(migrate, registry, from, to, fn, user);
    }

    static int r_provide(const char *registry, const char *id, SwoftyText value)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        int r = SWOFTY_FAKE_FORWARD(provide, registry, id, value);
        if (c->accepted(r, "provide", str(registry), str(id)))
            c->records_.push_back(Record{"provide", str(registry), {}, str(id), {}, parse(value)});
        return r;
    }

    static int r_provide_fn(const char *registry, SwoftyProvideFn fn, SwoftyListFn list, void *user)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        int r = SWOFTY_FAKE_FORWARD(provide_fn, registry, fn, list, user);
        if (c->accepted(r, "provide_fn", str(registry), ""))
            c->providers_.push_back(json::Value::object({{"registry", str(registry)}, {"list", list != nullptr}}));
        return r;
    }

    static int r_invalidate(const char *registry, const char *id)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        c->call("invalidate", json::Value::object({{"registry", str(registry)}, {"id", str(id)}}));
        return SWOFTY_FAKE_FORWARD(invalidate, registry, id);
    }

    static int r_layer(const char *registry, const char *id, SwoftyText patch, uint32_t scope)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        Layer l;
        l.registry = str(registry);
        l.id = str(id);
        l.patch = parse(patch);
        l.scope = scope;
        int r = SWOFTY_FAKE_FORWARD(layer, registry, id, patch, scope);
        if (c->accepted(r, "layer", l.registry, l.id))
            c->layers_.push_back(std::move(l));
        return r;
    }

    static int r_clear_layer(const char *registry, const char *id, uint32_t scope)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        c->call("clear_layer",
                json::Value::object({{"registry", str(registry)}, {"id", str(id)}, {"scope", scope_name(scope)}}));
        return SWOFTY_FAKE_FORWARD(clear_layer, registry, id, scope);
    }

    static int r_get(const char *registry, const char *id, char *out, size_t cap, size_t *len)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        c->read("get", registry, id);
        if (c->engine() && c->engine()->get)
            return c->engine()->get(registry, id, out, cap, len);
        std::string reg = str(registry);
        reg = reg.substr(0, reg.find('@'));
        auto v = c->base(reg, str(id));
        return v ? put_text(json::dump(*v), out, cap, len) : SWOFTY_ASSET_INVALID;
    }

    static int r_status(const char *registry, const char *id)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        c->read("status", registry, id);
        if (c->engine() && c->engine()->status)
            return c->engine()->status(registry, id);
        return c->base(str(registry), str(id)) ? SWOFTY_STATUS_OK : SWOFTY_STATUS_NO_RECORD;
    }

    static int r_origin(const char *registry, const char *id, const char *pointer, char *addon, size_t cap)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        c->read("origin", registry, id);
        if (c->engine() && c->engine()->origin)
            return c->engine()->origin(registry, id, pointer, addon, cap);
        return SWOFTY_ASSET_UNAVAILABLE;
    }

    static int r_first(const char *registry, const char *prefix, char *id, size_t cap)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        c->read("first", registry, prefix);
        if (c->engine() && c->engine()->first)
            return c->engine()->first(registry, prefix, id, cap);
        auto ids = c->ids(str(registry), str(prefix));
        if (ids.empty())
            return SWOFTY_ASSET_INVALID;
        std::sort(ids.begin(), ids.end());
        return put_id(ids.front(), id, cap);
    }

    static int r_next(const char *registry, const char *prefix, const char *after, char *id, size_t cap)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        if (c->engine() && c->engine()->next)
            return c->engine()->next(registry, prefix, after, id, cap);
        auto ids = c->ids(str(registry), str(prefix));
        std::sort(ids.begin(), ids.end());
        std::string a = str(after);
        for (const auto &x : ids)
            if (x > a)
                return put_id(x, id, cap);
        return SWOFTY_ASSET_INVALID;
    }

    static int r_watch(const char *registry, SwoftyRecordFn fn, void *user, uint32_t *token)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        c->call("watch", json::Value::object({{"registry", str(registry)}}));
        if (c->engine() && c->engine()->watch)
            return c->engine()->watch(registry, fn, user, token);
        if (token)
            *token = ++c->tokens_;
        return SWOFTY_ASSET_OK;
    }

    static int r_unwatch(uint32_t token)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        return SWOFTY_FAKE_FORWARD(unwatch, token);
    }

    static int r_last_error(char *out, size_t cap)
    {
        Core *c = current();
        if (c->engine() && c->engine()->last_error)
            return c->engine()->last_error(out, cap);
        if (out && cap)
            out[0] = 0;
        return SWOFTY_ASSET_OK;
    }

    static int r_add(const char *registry, const char *key, SwoftyText value, char *id, size_t cap)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        std::string rid = c->own_id(key);
        if (c->engine() && c->engine()->add) {
            char got[256] = {};
            int r = c->engine()->add(registry, key, value, got, sizeof got);
            if (!c->accepted(r, "add", str(registry), rid))
                return r;
            rid = std::string(got, strnlen(got, sizeof got));
            c->records_.push_back(Record{"add", str(registry), str(key), rid, {}, parse(value)});
            return put_id(rid, id, cap);
        }
        c->records_.push_back(Record{"add", str(registry), str(key), rid, {}, parse(value)});
        return put_id(rid, id, cap);
    }

    static int r_variant(const char *registry, const char *key, const char *base_id, SwoftyEditFn edit, void *user,
                         char *id, size_t cap)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        std::string rid = c->own_id(key), reg = str(registry), base = str(base_id);
        if (c->engine() && c->engine()->variant) {
            char got[256] = {};
            int r = c->engine()->variant(registry, key, base_id, edit, user, got, sizeof got);
            if (!c->accepted(r, "variant", reg, rid))
                return r;
            rid = std::string(got, strnlen(got, sizeof got));
        }
        json::Value value = nullptr;
        if (edit) {
            std::string in = json::dump(c->lookup_base(reg, base)), out;
            c->enter("variant edit of " + reg + " " + rid);
            int er = call_edit(edit, user, in, out);
            c->leave();
            if (er == SWOFTY_ASSET_OK) {
                json::Error err;
                value = json::parse(out, &err);
                if (!err.ok())
                    value = nullptr;
            }
        }
        c->records_.push_back(Record{"variant", reg, str(key), rid, base, std::move(value)});
        return put_id(rid, id, cap);
    }

    static int r_variant_patch(const char *registry, const char *key, const char *base_id, SwoftyText patch, char *id,
                               size_t cap)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        std::string rid = c->own_id(key), reg = str(registry), base = str(base_id);
        if (c->engine() && c->engine()->variant_patch) {
            char got[256] = {};
            int r = c->engine()->variant_patch(registry, key, base_id, patch, got, sizeof got);
            if (!c->accepted(r, "variant", reg, rid))
                return r;
            rid = std::string(got, strnlen(got, sizeof got));
        }
        json::Value value = c->lookup_base(reg, base);
        json::merge_patch(value, parse(patch));
        c->records_.push_back(Record{"variant", reg, str(key), rid, base, std::move(value)});
        return put_id(rid, id, cap);
    }

    static int r_layer_fn(const char *registry, const char *id, SwoftyEditFn edit, void *user, uint32_t scope)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        Layer l;
        l.registry = str(registry);
        l.id = str(id);
        l.edit = edit;
        l.user = user;
        l.scope = scope;
        int r = SWOFTY_FAKE_FORWARD(layer_fn, registry, id, edit, user, scope);
        if (c->accepted(r, "layer", l.registry, l.id))
            c->layers_.push_back(std::move(l));
        return r;
    }

    static int r_layer_select(const char *registry, SwoftyMatchFn match, const char *prefix, const char *const *except,
                              size_t except_count, SwoftyEditFn edit, void *user, uint32_t scope)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        Layer l;
        l.registry = str(registry);
        l.match = match != nullptr;
        l.prefix = str(prefix);
        for (size_t i = 0; except && i < except_count; i++)
            l.except.push_back(str(except[i]));
        l.edit = edit;
        l.user = user;
        l.scope = scope;
        int r = SWOFTY_FAKE_FORWARD(layer_select, registry, match, prefix, except, except_count, edit, user, scope);
        if (c->accepted(r, "layer", l.registry, l.prefix))
            c->layers_.push_back(std::move(l));
        return r;
    }

    static int r_option(const char *id, uint32_t type, SwoftySettingValue *out)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        std::string name = str(id);
        const json::Value *v = nullptr;
        for (const auto &o : c->options_)
            if (o.first == name)
                v = &o.second;
        c->options_read_.push_back(json::Value::object({{"id", name}, {"value", v ? *v : json::Value()}}));
        if (!v || !out)
            return SWOFTY_ASSET_INVALID;
        out->type = type;
        if (type == SWOFTY_SETTING_TOGGLE)
            out->toggle = v->is_bool() ? v->as_bool() : v->as_double() != 0 || v->as_string() == "true";
        else if (type == SWOFTY_SETTING_SLIDER)
            out->slider = v->is_number() ? v->as_double() : std::atof(std::string(v->as_string()).c_str());
        else {
            c->choices_.push_back(v->is_string() ? std::string(v->as_string()) : json::dump(*v));
            out->choice = c->choices_.back().c_str();
        }
        return SWOFTY_ASSET_OK;
    }

    static int r_layer_ex(const SwoftyLayerDesc *d, uint32_t *handle)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        if (!d)
            return SWOFTY_ASSET_INVALID;
        Layer l;
        l.registry = str(d->registry);
        l.id = str(d->id);
        l.match = d->match != nullptr;
        l.prefix = str(d->prefix);
        for (size_t i = 0; d->except && i < d->except_count; i++)
            l.except.push_back(str(d->except[i]));
        l.tag = str(d->tag);
        l.tag_field = str(d->tag_field);
        l.scope = d->scope;
        l.edit = d->edit;
        l.user = d->user;
        if (!d->edit)
            l.patch = parse(d->patch);
        if (d->in.data && d->in.size)
            l.in = parse(d->in);
        uint32_t h = 0;
        int r = SWOFTY_ASSET_OK;
        if (c->engine() && c->engine()->layer_ex)
            r = c->engine()->layer_ex(d, &h);
        else
            h = static_cast<uint32_t>(c->layers_.size() + 1);
        l.handle = h;
        if (c->accepted(r, "layer", l.registry, l.id.empty() ? l.prefix : l.id))
            c->layers_.push_back(std::move(l));
        if (handle)
            *handle = h;
        return r;
    }

    static int r_layer_in(uint32_t handle, SwoftyText selectors)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        for (auto &l : c->layers_)
            if (l.handle == handle && handle)
                l.in = parse(selectors);
        return SWOFTY_FAKE_FORWARD(layer_in, handle, selectors);
    }

    static int r_protect(const char *registry, const char *id, const char *const *fields, size_t count)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        json::Value f = json::Value::array();
        for (size_t i = 0; fields && i < count; i++)
            f.push_back(str(fields[i]));
        c->call("protect", json::Value::object({{"registry", str(registry)}, {"id", str(id)}, {"fields", f}}));
        return SWOFTY_FAKE_FORWARD(protect, registry, id, fields, count);
    }

    static int r_tag_add(const char *registry, const char *tag, const char *id)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        c->call("tag_add", json::Value::object({{"registry", str(registry)}, {"tag", str(tag)}, {"id", str(id)}}));
        return SWOFTY_FAKE_FORWARD(tag_add, registry, tag, id);
    }

    static int r_tag_contains(const char *registry, const char *tag, const char *id, int *out)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        c->read("tag_contains", registry, id);
        if (c->engine() && c->engine()->tag_contains)
            return c->engine()->tag_contains(registry, tag, id, out);
        return SWOFTY_ASSET_UNAVAILABLE;
    }

    static int r_tag_members(const char *registry, const char *tag, char *out, size_t cap, size_t *len)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        c->read("tag_members", registry, tag);
        if (c->engine() && c->engine()->tag_members)
            return c->engine()->tag_members(registry, tag, out, cap, len);
        return SWOFTY_ASSET_UNAVAILABLE;
    }

    static int r_stat_id(const char *registry, const char *id, int64_t *out)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        c->read("stat_id", registry, id);
        if (c->engine() && c->engine()->stat_id)
            return c->engine()->stat_id(registry, id, out);
        return SWOFTY_ASSET_UNAVAILABLE;
    }

    static int h_live(const char *point, uint32_t *)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        c->read("live", point, nullptr);
        return SWOFTY_ASSET_UNAVAILABLE;
    }

    void subscribe(const char *point, uint32_t phase, int32_t priority, json::Value in, uint32_t *token)
    {
        json::Value e = json::Value::object({{"point", str(point)}, {"phase", phase_name(phase)}, {"priority", priority}});
        if (in.size())
            e["in"] = std::move(in);
        subscriptions_.push_back(std::move(e));
        if (token)
            *token = ++tokens_;
    }

    static int h_attach(const char *point, uint32_t phase, int32_t priority, SwoftyHookFn fn, void *, uint32_t *token)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        if (!fn || !point || !*point || phase > SWOFTY_PHASE_AFTER)
            return c->refuse_hook("attach", point, "no handler, point or phase");
        c->subscribe(point, phase, priority, json::Value::array(), token);
        return SWOFTY_ASSET_OK;
    }

    static int h_attach_in(const char *point, uint32_t phase, int32_t priority, SwoftyText selectors, SwoftyHookFn fn,
                           void *, uint32_t *token)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        if (!fn || !point || !*point || phase > SWOFTY_PHASE_AFTER)
            return c->refuse_hook("attach", point, "no handler, point or phase");
        c->subscribe(point, phase, priority, parse(selectors), token);
        return SWOFTY_ASSET_OK;
    }

    static int h_detach(uint32_t) { return SWOFTY_ASSET_OK; }

    static int h_define(const char *point, uint32_t version)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        if (!c->own_point(point))
            return c->refuse_hook("define", point, "an addon defines points under its own id");
        c->points_.push_back(json::Value::object({{"point", str(point)}, {"payload_version", version}}));
        return SWOFTY_ASSET_OK;
    }

    bool own_point(const char *point) const
    {
        std::string p = str(point);
        return p.size() > addon_.size() + 1 && p.compare(0, addon_.size(), addon_) == 0 && p[addon_.size()] == '.';
    }

    int refuse_hook(const char *call, const char *point, const char *why)
    {
        refused_.push_back(json::Value::object({{"call", call}, {"point", str(point)}, {"error", why}}));
        return SWOFTY_ASSET_INVALID;
    }

    static int h_define_in(const char *point, uint32_t version, uint32_t kind, SwoftyText scope)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        if (kind > SWOFTY_POINT_CANCEL)
            return c->refuse_hook("define", point, "no such kind");
        if (!c->own_point(point))
            return c->refuse_hook("define", point, "an addon defines points under its own id");
        json::Value e =
            json::Value::object({{"point", str(point)}, {"payload_version", version}, {"kind", kind_name(kind)}});
        json::Value s = parse(scope);
        if (!s.is_null() && s.size())
            e["scope"] = std::move(s);
        c->points_.push_back(std::move(e));
        return SWOFTY_ASSET_OK;
    }

    static int h_fire(const char *point, void *, int *replaced)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        c->call("fire", json::Value::object({{"point", str(point)}}));
        if (replaced)
            *replaced = 0;
        return SWOFTY_ASSET_UNAVAILABLE;
    }

    static int h_context(const void *, const char *, char *, size_t, size_t *) { return SWOFTY_ASSET_UNAVAILABLE; }
    static int h_context_in(const void *, SwoftyText, int *out)
    {
        if (out)
            *out = 0;
        return SWOFTY_ASSET_UNAVAILABLE;
    }

    static int s_publish(const char *name, uint32_t version, const void *, size_t size)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        c->published_.push_back(
            json::Value::object({{"name", str(name)}, {"version", version}, {"size", static_cast<uint64_t>(size)}}));
        return SWOFTY_ASSET_OK;
    }

    static int s_when(const char *name, uint32_t min_version, size_t min_size, SwoftyServiceFn, void *)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        c->required_.push_back(json::Value::object(
            {{"name", str(name)}, {"min_version", min_version}, {"min_size", static_cast<uint64_t>(min_size)}}));
        return SWOFTY_ASSET_OK;
    }

    static int e_available(void) { return 1; }

    static int e_subscribe(const char *event, SwoftyEventFn, void *, uint32_t *token)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        c->events_.push_back(json::Value::object({{"event", str(event)}}));
        if (token)
            *token = ++c->tokens_;
        return SWOFTY_ASSET_OK;
    }

    static int e_on_frame(SwoftyFrameFn, void *, uint32_t *token)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        c->events_.push_back(json::Value::object({{"event", "frame"}}));
        if (token)
            *token = ++c->tokens_;
        return SWOFTY_ASSET_OK;
    }

    static int e_unsubscribe(uint32_t) { return SWOFTY_ASSET_OK; }

    static void a_log(const char *fmt, ...)
    {
        Core *c = current();
        SWOFTY_FAKE_LOCK(c);
        char buf[1024];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buf, sizeof buf, fmt ? fmt : "", ap);
        va_end(ap);
        c->log_.emplace_back(buf);
    }

    static int a_setting(const char *id, uint32_t type, SwoftySettingValue *out) { return r_option(id, type, out); }

    void unexpected_call(const char *what)
    {
        SWOFTY_FAKE_LOCK(this);
        unexpected_.emplace_back(what);
    }

    template <int N> static uintptr_t stub()
    {
        static const char *const names[] = {"api.module_base", "api.scan",           "api.resolve",
                                            "api.register_mixins", "api.fields", "api.watch_setting"};
        if (Core *c = current())
            c->unexpected_call(names[N]);
        return 0;
    }

#undef SWOFTY_FAKE_FORWARD
#undef SWOFTY_FAKE_LOCK

    void fill_tables()
    {
        registry_ = SwoftyRegistry{};
        registry_.version = SWOFTY_REGISTRY_VERSION;
        registry_.available = &r_available;
        registry_.declare = &r_declare;
        registry_.check = &r_check;
        registry_.adapter = &r_adapter;
        registry_.provide = &r_provide;
        registry_.provide_fn = &r_provide_fn;
        registry_.invalidate = &r_invalidate;
        registry_.layer = &r_layer;
        registry_.clear_layer = &r_clear_layer;
        registry_.get = &r_get;
        registry_.status = &r_status;
        registry_.origin = &r_origin;
        registry_.first = &r_first;
        registry_.next = &r_next;
        registry_.watch = &r_watch;
        registry_.unwatch = &r_unwatch;
        registry_.last_error = &r_last_error;
        registry_.add = &r_add;
        registry_.variant = &r_variant;
        registry_.layer_fn = &r_layer_fn;
        registry_.layer_select = &r_layer_select;
        registry_.option = &r_option;
        registry_.migrate = &r_migrate;
        registry_.layer_ex = &r_layer_ex;
        registry_.layer_in = &r_layer_in;
        registry_.protect = &r_protect;
        registry_.tag_add = &r_tag_add;
        registry_.tag_contains = &r_tag_contains;
        registry_.tag_members = &r_tag_members;
        registry_.variant_patch = &r_variant_patch;
        registry_.stat_id = &r_stat_id;

        hooks_ = SwoftyHooks{};
        hooks_.version = SWOFTY_HOOKS_VERSION;
        hooks_.available = &r_available;
        hooks_.live = &h_live;
        hooks_.attach = &h_attach;
        hooks_.detach = &h_detach;
        hooks_.define = &h_define;
        hooks_.fire = &h_fire;
        hooks_.attach_in = &h_attach_in;
        hooks_.define_in = &h_define_in;
        hooks_.context = &h_context;
        hooks_.context_in = &h_context_in;

        services_ = SwoftyServices{};
        services_.version = SWOFTY_SERVICES_VERSION;
        services_.available = &r_available;
        services_.publish = &s_publish;
        services_.when = &s_when;

        events_table_ = SwoftyEvents{};
        events_table_.available = &e_available;
        events_table_.subscribe = &e_subscribe;
        events_table_.on_frame = &e_on_frame;
        events_table_.unsubscribe = &e_unsubscribe;

        api_ = SwoftyApi{};
        api_.version = SWOFTY_API_VERSION;
        api_.log = &a_log;
        api_.module_base = reinterpret_cast<uintptr_t (*)(void)>(&stub<0>);
        api_.scan = reinterpret_cast<int (*)(const char *, const char *, uintptr_t *)>(&stub<1>);
        api_.resolve = reinterpret_cast<uintptr_t (*)(const char *)>(&stub<2>);
        api_.register_mixins = reinterpret_cast<int (*)(const SwoftyMixin *, size_t)>(&stub<3>);
        api_.fields = reinterpret_cast<void *(*)(const void *, size_t)>(&stub<4>);
        api_.setting = &a_setting;
        api_.watch_setting = reinterpret_cast<int (*)(const char *, SwoftySettingFn, void *)>(&stub<5>);
        api_.events = &events_table_;
        api_.registry = &registry_;
        api_.hooks = &hooks_;
        api_.services = &services_;
    }

public:
    const SwoftyRegistry *registry_table() const { return &registry_; }
    const SwoftyHooks *hooks_table() const { return &hooks_; }
    const SwoftyServices *services_table() const { return &services_; }

private:
    std::string addon_;
    const SwoftyFakeEngine *engine_ = nullptr;
    const SwoftyApi *previous_ = nullptr;
    int result_ = 0;
    SwoftyApi api_{};
    SwoftyRegistry registry_{};
    SwoftyHooks hooks_{};
    SwoftyServices services_{};
    SwoftyEvents events_table_{};
    mutable std::recursive_mutex mutex_;
    uint32_t tokens_ = 0;
    std::vector<std::pair<std::string, json::Value>> registries_;
    std::vector<Record> records_;
    std::vector<Record> stock_;
    std::vector<Layer> layers_;
    json::Value providers_ = json::Value::array();
    json::Value calls_ = json::Value::array();
    json::Value subscriptions_ = json::Value::array();
    json::Value points_ = json::Value::array();
    json::Value published_ = json::Value::array();
    json::Value required_ = json::Value::array();
    json::Value events_ = json::Value::array();
    json::Value reads_ = json::Value::array();
    std::vector<std::pair<std::string, json::Value>> options_;
    json::Value options_read_ = json::Value::array();
    std::deque<std::string> choices_;
    std::vector<std::string> log_;
    std::vector<std::string> unexpected_;
    json::Value refused_ = json::Value::array();
    std::array<char, 256> call_{};
    std::atomic<bool> in_call_{false};
};

}
