#include "../../src/core/registry/table.hpp"

#include <swofty/fake_core.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <memory>
#include <optional>
#include <fstream>
#include <regex>
#include <sstream>

using namespace swofty_core::registry;
using swofty::json::Value;

namespace {

std::string read_file(const std::string &p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool type_ok(const std::string &t, const Value &v)
{
    if (t == "object")
        return v.is_object();
    if (t == "array")
        return v.is_array();
    if (t == "string")
        return v.is_string();
    if (t == "boolean")
        return v.is_bool();
    if (t == "null")
        return v.is_null();
    if (t == "number")
        return v.is_number();
    if (t == "integer")
        return v.is_int() || (v.is_double() && std::floor(v.as_double()) == v.as_double());
    return true;
}

bool check_schema(const Value &s, const Value &v, const std::string &at, std::string &why)
{
    if (!s.is_object())
        return true;
    if (auto t = s.find("type")) {
        bool ok = false;
        if (t->is_string())
            ok = type_ok(std::string(t->as_string()), v);
        else if (auto a = t->if_array())
            for (auto &x : *a)
                ok = ok || type_ok(std::string(x.as_string()), v);
        if (!ok) {
            why = at + ": wrong type";
            return false;
        }
    }
    if (auto e = s.find("enum"); e && e->is_array()) {
        bool ok = false;
        for (auto &x : *e->if_array())
            ok = ok || x == v;
        if (!ok) {
            why = at + ": not one of the enum values";
            return false;
        }
    }
    if (v.is_number()) {
        if (auto m = s.find("minimum"); m && v.as_double() < m->as_double()) {
            why = at + ": below the minimum";
            return false;
        }
        if (auto m = s.find("maximum"); m && v.as_double() > m->as_double()) {
            why = at + ": above the maximum";
            return false;
        }
    }
    if (auto p = s.find("pattern"); p && v.is_string())
        if (!std::regex_search(std::string(v.as_string()), std::regex(std::string(p->as_string())))) {
            why = at + ": does not match the pattern";
            return false;
        }
    if (v.is_object()) {
        if (auto r = s.find("required"); r && r->is_array())
            for (auto &k : *r->if_array())
                if (!v.find(k.as_string()) || v[k.as_string()].is_null()) {
                    why = at + "/" + std::string(k.as_string()) + ": required";
                    return false;
                }
        if (auto props = s.find("properties"); props && props->is_object())
            for (auto &[k, sub] : *props->if_object())
                if (auto c = v.find(k); c && !c->is_null())
                    if (!check_schema(sub, *c, at + "/" + k, why))
                        return false;
    }
    if (v.is_array())
        if (auto items = s.find("items"))
            for (std::size_t i = 0; i < v.size(); i++)
                if (!check_schema(*items, v[i], at + "/" + std::to_string(i), why))
                    return false;
    return true;
}

class CaseValidator final : public Validator {
public:
    std::map<std::string, Value> schemas;
    bool validate(const Def &def, const Value &value, std::string &error) override
    {
        auto it = schemas.find(def.name);
        return it == schemas.end() || check_schema(it->second, value, "", error);
    }
};

Value &node_at(Value &schema, const std::string &pattern)
{
    Value *n = &schema;
    std::size_t i = 1;
    while (i <= pattern.size() && !pattern.empty()) {
        auto j = pattern.find('/', i);
        if (j == std::string::npos)
            j = pattern.size();
        std::string seg = pattern.substr(i, j - i);
        if (seg == "*")
            n = &(*n)["items"];
        else
            n = &(*n)["properties"][seg];
        i = j + 1;
    }
    return *n;
}

struct Steps {
    Value steps;
};

int steps_edit(void *user, SwoftyText in, char *out, size_t cap, size_t *len)
{
    auto *st = static_cast<Steps *>(user);
    Value v = swofty::json::parse(std::string_view(in.data, in.size));
    for (auto &s : *st->steps.if_array()) {
        std::string op(s["op"].as_string()), path(s["path"].as_string());
        Value *at = v.get(path);
        if (!at)
            return SWOFTY_ASSET_INVALID;
        if (op == "set")
            *at = s["value"];
        else if (!at->is_number())
            return SWOFTY_ASSET_INVALID;
        else if (op == "add")
            *at = at->is_int() && s["value"].is_int() ? Value(at->as_int() + s["value"].as_int())
                                                       : Value(at->as_double() + s["value"].as_double());
        else if (op == "mul")
            *at = at->is_int() && s["value"].is_int() ? Value(at->as_int() * s["value"].as_int())
                                                       : Value(at->as_double() * s["value"].as_double());
    }
    std::string text = swofty::json::dump(v);
    if (len)
        *len = text.size();
    if (cap < text.size())
        return SWOFTY_TOO_SMALL;
    std::memcpy(out, text.data(), text.size());
    return SWOFTY_ASSET_OK;
}

struct WhenEdit {
    std::string addon;
    Value when;
    bool is_patch;
    Value patch;
    Steps steps;
    swofty::json::Keys keys;
};

thread_local std::string tl_addon;

int when_edit(void *user, SwoftyText in, char *out, size_t cap, size_t *len)
{
    auto *w = static_cast<WhenEdit *>(user);
    Value got;
    std::string option(w->when["option"].as_string());
    bool have = store().read_option(w->addon, option, got);
    bool on = false;
    if (have) {
        auto truthy = [](const Value &v) { return v.is_bool() ? v.as_bool() : v.is_number() ? v.as_double() != 0 : !v.is_null(); };
        if (auto is = w->when.find("is"))
            on = got == *is;
        else if (auto list = w->when.find("in")) {
            for (auto &e : *list->if_array())
                on = on || got == e;
        } else if (w->when.find("on"))
            on = truthy(got);
        else if (w->when.find("off"))
            on = !truthy(got);
    }
    if (!on) {
        if (len)
            *len = in.size;
        if (cap < in.size)
            return SWOFTY_TOO_SMALL;
        std::memcpy(out, in.data, in.size);
        return SWOFTY_ASSET_OK;
    }
    if (!w->is_patch)
        return steps_edit(&w->steps, in, out, cap, len);
    Value v = swofty::json::parse(std::string_view(in.data, in.size));
    swofty::json::merge_patch(v, w->patch, w->keys);
    std::string text = swofty::json::dump(v);
    if (len)
        *len = text.size();
    if (cap < text.size())
        return SWOFTY_TOO_SMALL;
    std::memcpy(out, text.data(), text.size());
    return SWOFTY_ASSET_OK;
}

struct Where {
    Value where;
};

int where_match(void *user, SwoftyText base)
{
    auto *w = static_cast<Where *>(user);
    Value v = swofty::json::parse(std::string_view(base.data, base.size));
    for (auto &[k, want] : *w->where.if_object()) {
        const Value *got = v.find(k);
        auto eq = [&](const Value &x) {
            if (!got || got->is_null())
                return x.is_null();
            if (got->is_number() && x.is_number())
                return got->as_double() == x.as_double();
            return *got == x;
        };
        if (want.is_array()) {
            bool any = false;
            for (auto &x : *want.if_array())
                any = any || eq(x);
            if (!any)
                return 0;
        } else if (want.is_object() && (want.find("min") || want.find("max"))) {
            if (!got || !got->is_number())
                return 0;
            if (auto m = want.find("min"); m && got->as_double() < m->as_double())
                return 0;
            if (auto m = want.find("max"); m && got->as_double() > m->as_double())
                return 0;
        } else if (!eq(want))
            return 0;
    }
    return 1;
}

struct Provided {
    std::string addon;
    Value records;
};

int provide_cb(void *user, const char *, const char *id, char *out, size_t cap, size_t *len)
{
    auto *p = static_cast<Provided *>(user);
    std::string sid(id);
    if (sid.rfind(p->addon + "/", 0) != 0)
        return SWOFTY_ASSET_INVALID;
    const Value *v = p->records.find(sid.substr(p->addon.size() + 1));
    if (!v || v->is_null())
        return SWOFTY_ASSET_INVALID;
    std::string text = swofty::json::dump(*v);
    if (len)
        *len = text.size();
    if (cap < text.size())
        return SWOFTY_TOO_SMALL;
    std::memcpy(out, text.data(), text.size());
    return SWOFTY_ASSET_OK;
}

void list_cb(void *user, const char *, void (*add)(void *, const char *), void *ctx)
{
    auto *p = static_cast<Provided *>(user);
    for (auto &[k, v] : *p->records.if_object())
        if (!v.is_null())
            add(ctx, (p->addon + "/" + k).c_str());
}

std::vector<std::unique_ptr<Steps>> keep_steps;
std::vector<std::unique_ptr<WhenEdit>> keep_when;
std::vector<std::unique_ptr<Where>> keep_where;
std::vector<std::unique_ptr<Provided>> keep_provided;

Context context_of(const Value &c)
{
    Context ctx;
    ctx.profile = std::string(c["profile"].as_string("default"));
    if (c["career"].is_string())
        ctx.career = std::string(c["career"].as_string());
    ctx.mode = std::string(c["mode"].as_string());
    ctx.data_group = std::string(c["data_group"].as_string());
    ctx.setup_mode = std::string(c["setup_mode"].as_string());
    if (auto a = c["content"].if_array())
        for (auto &x : *a)
            ctx.content.emplace_back(x.as_string());
    return ctx;
}

swofty::json::Keys keys_of(const Value &reg)
{
    swofty::json::Keys k;
    if (auto kf = reg["key_fields"].if_object())
        for (auto &[p, f] : *kf) {
            std::vector<std::string> fields;
            for (auto &x : *f.if_array())
                fields.emplace_back(x.as_string());
            k.add(p, fields);
        }
    return k;
}

bool g_fake = false;
std::unique_ptr<swofty::fake::Core> g_core;

void setup(const Value &cs, const Value &ectx, CaseValidator &validator)
{
    const SwoftyFakeEngine *engine = nullptr;
    if (g_fake) {
        g_core.reset();
        g_core = std::make_unique<swofty::fake::Core>("game");
        engine = swofty_registry_engine();
    }
    reset_store();
    keep_steps.clear();
    keep_when.clear();
    keep_where.clear();
    keep_provided.clear();
    Store &st = store();
    const SwoftyRegistry *t = g_fake ? g_core->api().registry : table();
    std::vector<std::string> order;
    for (auto &a : *cs["addons"].if_array())
        order.emplace_back(a["id"].as_string());
    st.set_order(order);
    validator.schemas.clear();
    st.set_validator(&validator);
    Value options = ectx["options"], settings = ectx["settings"];
    st.set_option_source([options](const std::string &addon, const std::string &id, Value &out) {
        const Value *o = options.find(addon);
        const Value *v = o ? o->find(id) : nullptr;
        if (!v)
            return false;
        out = *v;
        return true;
    });
    st.set_override_source([settings](const std::string &addon) {
        return settings[addon]["also_change_year_packs"].as_bool(false);
    });
    for (auto &[name, reg] : *cs["registries"].if_object()) {
        Value schema = Value::object();
        schema["x-registry"] = Value(name);
        schema["x-applies"] = reg["applies"];
        schema["type"] = Value("object");
        if (auto kf = reg["key_fields"].if_object())
            for (auto &[p, f] : *kf) {
                Value &n = node_at(schema, p);
                n["type"] = Value("array");
                n["x-key"] = f;
            }
        if (auto refs = reg["refs"].if_object())
            for (auto &[p, target] : *refs)
                node_at(schema, p)["x-ref"] = target;
        if (auto sh = reg.find("shared"); sh && sh->is_object()) {
            Value x = *sh;
            std::string n = name;
            x["prefix"] = Value(n.substr(n.find_last_of('.') + 1));
            schema["x-shared"] = x;
        }
        std::string text = swofty::json::dump(schema);
        t->declare(name.c_str(), SwoftyText{text.data(), text.size()});
        st.stock_loaded(name, true);
        if (auto s = reg.find("schema"))
            validator.schemas[name] = *s;
    }
    for (auto &b : *cs["bases"].if_array())
        st.add_base(std::string(b["registry"].as_string()), std::string(b["key"].as_string()), b["value"]);
    if (auto tags = cs["tags"].if_object())
        for (auto &[reg, ts] : *tags)
            for (auto &[tag, ids] : *ts.if_object()) {
                std::vector<std::string> m;
                for (auto &x : *ids.if_array())
                    m.emplace_back(x.as_string());
                st.define_tag(reg, tag, m);
            }
    std::set<std::string> disabled;
    if (auto d = ectx["disabled"].if_array())
        for (auto &x : *d)
            disabled.emplace(x.as_string());
    for (int stage = 0; stage < 2; stage++) {
        if (stage == 1) {
            if (engine)
                engine->end_register();
            else
                st.end_register();
        }
        for (auto &a : *cs["addons"].if_array()) {
            std::string addon(a["id"].as_string());
            if (!a["enabled"].as_bool(true) || disabled.count(addon))
                continue;
            std::optional<AsAddon> as;
            if (engine)
                engine->begin_addon(addon.c_str());
            else
                as.emplace(addon);
            tl_addon = addon;
            const Value &ops = a["ops"];
            for (std::size_t i = 0; i < ops.size(); i++) {
                const Value &op = ops[i];
                bool on_load = op["stage"].as_string("register") == "on_load";
                if (on_load != (stage == 1))
                    continue;
                st.set_op_index(addon, static_cast<int>(i));
                std::string kind(op["op"].as_string()), reg(op["registry"].as_string());
                Context c = st.context();
                std::string scope(op["scope"].as_string("global"));
                uint32_t sc = scope == "profile" ? SWOFTY_SCOPE_PROFILE : scope == "career" ? SWOFTY_SCOPE_CAREER : SWOFTY_SCOPE_GLOBAL;
                if (scope == "profile")
                    c.profile = std::string(op["profile"].as_string());
                if (scope == "career")
                    c.career = std::string(op["career"].as_string());
                st.set_context(c);
                char id[256];
                if (kind == "add") {
                    std::string v = swofty::json::dump(op["value"]);
                    t->add(reg.c_str(), std::string(op["key"].as_string()).c_str(), SwoftyText{v.data(), v.size()}, id,
                           sizeof id);
                } else if (kind == "variant") {
                    std::string key(op["key"].as_string()), base(op["base"].as_string());
                    if (op.find("patch")) {
                        std::string v = swofty::json::dump(op["patch"]);
                        t->variant_patch(reg.c_str(), key.c_str(), base.c_str(), SwoftyText{v.data(), v.size()}, id,
                                         sizeof id);
                    } else {
                        keep_steps.push_back(std::make_unique<Steps>(Steps{op["edit"]}));
                        t->variant(reg.c_str(), key.c_str(), base.c_str(), steps_edit, keep_steps.back().get(), id,
                                   sizeof id);
                    }
                } else if (kind == "provide") {
                    keep_provided.push_back(std::make_unique<Provided>(Provided{addon, op["records"]}));
                    t->provide_fn(reg.c_str(), provide_cb, list_cb, keep_provided.back().get());
                } else if (kind == "layer") {
                    bool has_in = op.find("in") != nullptr, has_when = op.find("when") != nullptr;
                    bool sel = op.find("select") != nullptr;
                    std::string target(op["target"].as_string());
                    if (!sel && !has_in && !has_when && op.find("patch")) {
                        std::string v = swofty::json::dump(op["patch"]);
                        t->layer(reg.c_str(), target.c_str(), SwoftyText{v.data(), v.size()}, sc);
                        continue;
                    }
                    if (!sel && !has_in && !has_when) {
                        keep_steps.push_back(std::make_unique<Steps>(Steps{op["edit"]}));
                        t->layer_fn(reg.c_str(), target.c_str(), steps_edit, keep_steps.back().get(), sc);
                        continue;
                    }
                    SwoftyLayerDesc d{};
                    d.size = sizeof d;
                    d.scope = sc;
                    d.registry = reg.c_str();
                    std::string prefix, tag, tag_field, in_text, patch_text;
                    std::vector<std::string> except;
                    std::vector<const char *> except_ptrs;
                    if (!sel)
                        d.id = target.c_str();
                    else {
                        const Value &s = op["select"];
                        if (auto ids = s.find("ids")) {
                            prefix = std::string(ids->as_string());
                            prefix.pop_back();
                            d.prefix = prefix.c_str();
                        }
                        if (auto ex = s.find("except"))
                            for (auto &x : *ex->if_array())
                                except.emplace_back(x.as_string());
                        for (auto &x : except)
                            except_ptrs.push_back(x.c_str());
                        d.except = except_ptrs.data();
                        d.except_count = except_ptrs.size();
                        if (auto w = s.find("where")) {
                            keep_where.push_back(std::make_unique<Where>(Where{*w}));
                            d.match = where_match;
                            d.match_user = keep_where.back().get();
                        }
                        if (auto tg = s.find("tag")) {
                            if (tg->is_string())
                                tag = std::string(tg->as_string());
                            else {
                                auto &[field, tv] = tg->if_object()->entry(0);
                                tag_field = "/" + field;
                                tag = std::string(tv.as_string());
                                d.tag_field = tag_field.c_str();
                            }
                            d.tag = tag.c_str();
                        }
                    }
                    if (has_in) {
                        in_text = swofty::json::dump(op["in"]);
                        d.in = SwoftyText{in_text.data(), in_text.size()};
                    }
                    if (has_when) {
                        auto w = std::make_unique<WhenEdit>();
                        w->addon = addon;
                        w->when = op["when"];
                        w->is_patch = op.find("patch") != nullptr;
                        w->patch = op["patch"];
                        w->steps.steps = op["edit"];
                        w->keys = keys_of(cs["registries"][reg]);
                        keep_when.push_back(std::move(w));
                        d.edit = when_edit;
                        d.user = keep_when.back().get();
                    } else if (op.find("patch")) {
                        patch_text = swofty::json::dump(op["patch"]);
                        d.patch = SwoftyText{patch_text.data(), patch_text.size()};
                    } else {
                        keep_steps.push_back(std::make_unique<Steps>(Steps{op["edit"]}));
                        d.edit = steps_edit;
                        d.user = keep_steps.back().get();
                    }
                    uint32_t h = 0;
                    t->layer_ex(&d, &h);
                } else if (kind == "clear_layer") {
                    t->clear_layer(reg.c_str(), std::string(op["target"].as_string()).c_str(), sc);
                } else if (kind == "tag_add") {
                    t->tag_add(reg.c_str(), std::string(op["tag"].as_string()).c_str(),
                               std::string(op["id"].as_string()).c_str());
                } else if (kind == "protect") {
                    std::vector<std::string> f;
                    std::vector<const char *> fp;
                    for (auto &x : *op["fields"].if_array())
                        f.emplace_back(x.as_string());
                    for (auto &x : f)
                        fp.push_back(x.c_str());
                    t->protect(reg.c_str(), std::string(op["id"].as_string()).c_str(), fp.data(), fp.size());
                }
            }
        }
    }
    if (st.registering())
        st.end_register();
    st.set_context(context_of(ectx));
}

bool num_eq(double a, double b) { return std::fabs(a - b) <= 1e-6 * std::max({1.0, std::fabs(a), std::fabs(b)}); }

bool equal(const Value &a, const Value &b)
{
    if (a.is_null() || b.is_null())
        return a.is_null() && b.is_null();
    if (a.is_number() && b.is_number())
        return num_eq(a.as_double(), b.as_double());
    if (a.type() != b.type())
        return false;
    if (auto ao = a.if_object()) {
        auto bo = b.if_object();
        for (auto &[k, v] : *ao) {
            const Value *w = bo->find(k);
            if (!equal(v, w ? *w : Value()))
                return false;
        }
        for (auto &[k, v] : *bo)
            if (!ao->find(k) && !v.is_null())
                return false;
        return true;
    }
    if (auto aa = a.if_array()) {
        if (aa->size() != b.size())
            return false;
        for (std::size_t i = 0; i < aa->size(); i++)
            if (!equal((*aa)[i], b[i]))
                return false;
        return true;
    }
    return a == b;
}

Value result_of(const std::string &reg, const std::string &id)
{
    Record r = store().get(reg, id);
    if (g_fake && r.status == Status::ok) {
        auto v = g_core->effective(reg, id);
        if (!v)
            r.status = Status::invalid;
        else
            r.value = *v;
    }
    Value o = Value::object();
    o["status"] = Value(status_name(r.status));
    if (r.status == Status::ok) {
        o["value"] = r.value;
        Value org = Value::object();
        for (auto &[p, a] : r.origins)
            org[p] = Value(a);
        o["origins"] = org;
    }
    return o;
}

Value conflicts_json()
{
    Value out = Value::array();
    for (auto &c : store().conflicts()) {
        Value e = Value::object();
        e["kind"] = Value(c.kind);
        e["registry"] = Value(c.registry);
        e["id"] = Value(c.id);
        e["pointer"] = Value(c.pointer);
        Value a = Value::array();
        for (auto &x : c.addons)
            a.push_back(Value(x));
        e["addons"] = a;
        e["winner"] = Value(c.winner);
        out.push_back(e);
    }
    return out;
}

Value refusals_json()
{
    Value out = Value::array();
    for (auto &r : store().refusals()) {
        if (r.op < 0)
            continue;
        Value e = Value::object();
        e["addon"] = Value(r.addon);
        e["registry"] = Value(r.registry);
        e["id"] = Value(r.id);
        e["op"] = Value(r.op);
        e["reason"] = Value(r.reason);
        out.push_back(e);
    }
    return out;
}

std::vector<std::string> list_via_table(const std::string &reg, const std::string &prefix)
{
    std::vector<std::string> out;
    const SwoftyRegistry *t = table();
    char id[256];
    if (t->first(reg.c_str(), prefix.c_str(), id, sizeof id) != SWOFTY_ASSET_OK)
        return out;
    for (;;) {
        out.emplace_back(id);
        std::string after = out.back();
        if (t->next(reg.c_str(), prefix.c_str(), after.c_str(), id, sizeof id) != SWOFTY_ASSET_OK)
            break;
    }
    return out;
}

std::vector<std::string> tags_via_table(const std::string &reg, const std::string &tag)
{
    char buf[8192];
    size_t len = 0;
    std::vector<std::string> out;
    if (table()->tag_members(reg.c_str(), tag.c_str(), buf, sizeof buf, &len) != SWOFTY_ASSET_OK)
        return out;
    Value a = swofty::json::parse(std::string_view(buf, len));
    for (auto &x : *a.if_array())
        out.emplace_back(x.as_string());
    return out;
}

std::string canon_entry(const Value &e)
{
    Value c = Value::object();
    std::vector<std::string> keys;
    for (auto &[k, v] : *e.if_object())
        keys.push_back(k);
    std::sort(keys.begin(), keys.end());
    for (auto &k : keys)
        c[k] = e[k];
    return swofty::json::dump(c);
}

std::multiset<std::string> as_set(const Value &list)
{
    std::multiset<std::string> s;
    if (auto a = list.if_array())
        for (auto &e : *a)
            s.insert(canon_entry(e));
    return s;
}

int run_case(const std::string &path, bool verbose)
{
    Value cs = swofty::json::parse(read_file(path));
    CaseValidator validator;
    int failures = 0;
    std::string name = path.substr(path.find_last_of('/') + 1);
    const Value &evals = cs["evaluations"];
    for (std::size_t ei = 0; ei < evals.size(); ei++) {
        const Value &ev = evals[ei];
        setup(cs, ev["context"], validator);
        auto fail = [&](const std::string &what, const std::string &want, const std::string &got) {
            failures++;
            std::printf("FAIL %s eval %zu: %s\n  want %s\n  got  %s\n", name.c_str(), ei, what.c_str(), want.c_str(),
                        got.c_str());
        };
        if (auto ex = ev["expect"].if_object())
            for (auto &[reg, ids] : *ex)
                for (auto &[id, want] : *ids.if_object()) {
                    Value got = result_of(reg, id);
                    std::string what = reg + " " + id;
                    if (got["status"].as_string() != want["status"].as_string()) {
                        fail(what + " status", swofty::json::dump(want), swofty::json::dump(got));
                        continue;
                    }
                    if (want["status"].as_string() != "ok")
                        continue;
                    if (!equal(got["value"], want["value"]))
                        fail(what + " value", swofty::json::dump(want["value"]), swofty::json::dump(got["value"]));
                    if (!(got["origins"] == want["origins"]))
                        fail(what + " origins", swofty::json::dump(want["origins"]), swofty::json::dump(got["origins"]));
                }
        Value conf = conflicts_json(), refs = refusals_json();
        if (as_set(conf) != as_set(ev["conflicts"]))
            fail("conflicts", swofty::json::dump(ev["conflicts"]), swofty::json::dump(conf));
        if (as_set(refs) != as_set(ev["refusals"]))
            fail("refusals", swofty::json::dump(ev["refusals"]), swofty::json::dump(refs));
        if (auto ls = ev["lists"].if_array())
            for (auto &l : *ls) {
                auto got = list_via_table(std::string(l["registry"].as_string()), std::string(l["prefix"].as_string()));
                Value g = Value::array();
                for (auto &x : got)
                    g.push_back(Value(x));
                if (!(g == l["ids"]))
                    fail("list " + std::string(l["registry"].as_string()) + " '" + std::string(l["prefix"].as_string()) +
                             "'",
                         swofty::json::dump(l["ids"]), swofty::json::dump(g));
            }
        if (auto ts = ev["tag_members"].if_array())
            for (auto &l : *ts) {
                auto got = tags_via_table(std::string(l["registry"].as_string()), std::string(l["tag"].as_string()));
                Value g = Value::array();
                for (auto &x : got)
                    g.push_back(Value(x));
                if (!(g == l["ids"]))
                    fail("tag " + std::string(l["tag"].as_string()), swofty::json::dump(l["ids"]), swofty::json::dump(g));
            }
    }
    if (verbose || failures)
        std::printf("%s %s\n", failures ? "FAIL" : "ok  ", name.c_str());
    return failures;
}

Value dump_case(const std::string &path)
{
    Value cs = swofty::json::parse(read_file(path));
    CaseValidator validator;
    Value out = Value::array();
    for (auto &ev : *cs["evaluations"].if_array()) {
        setup(cs, ev["context"], validator);
        Value e = Value::object();
        Value recs = Value::object();
        for (auto &reg : store().registries()) {
            Value r = Value::object();
            std::set<std::string> ids;
            for (auto &id : list_via_table(reg, ""))
                ids.insert(id);
            if (auto ex = ev["expect"][reg].if_object())
                for (auto &[id, w] : *ex)
                    ids.insert(id);
            for (auto &id : ids)
                r[id] = result_of(reg, id);
            recs[reg] = r;
        }
        e["records"] = recs;
        Value c = conflicts_json(), rf = refusals_json();
        std::vector<std::string> cl, rl;
        for (auto &x : *c.if_array())
            cl.push_back(canon_entry(x));
        for (auto &x : *rf.if_array())
            rl.push_back(canon_entry(x));
        std::sort(cl.begin(), cl.end());
        std::sort(rl.begin(), rl.end());
        Value ca = Value::array(), ra = Value::array();
        for (auto &x : cl)
            ca.push_back(swofty::json::parse(x));
        for (auto &x : rl)
            ra.push_back(swofty::json::parse(x));
        e["conflicts"] = ca;
        e["refusals"] = ra;
        out.push_back(e);
    }
    return out;
}

int edit_copy(void *, SwoftyText in, char *out, size_t cap, size_t *len)
{
    Value v = swofty::json::parse(std::string_view(in.data, in.size));
    v["n"] = Value(v["n"].as_int() + 1);
    std::string text = swofty::json::dump(v);
    *len = text.size();
    if (cap < *len)
        return SWOFTY_TOO_SMALL;
    std::memcpy(out, text.data(), *len);
    return SWOFTY_ASSET_OK;
}

int bench(const std::string &perf_log)
{
    reset_store();
    Store &st = store();
    const SwoftyRegistry *t = table();
    std::string schema = R"({"x-registry":"bench.thing","x-applies":"read","type":"object","properties":{"items":{"type":"array","x-key":["key"],"items":{"type":"object"}}}})";
    t->declare("bench.thing", SwoftyText{schema.data(), schema.size()});
    const int records = 2000, addons = 8;
    std::vector<std::string> order;
    for (int a = 0; a < addons; a++)
        order.push_back("mod.b" + std::to_string(a));
    st.set_order(order);
    for (int i = 0; i < records; i++)
        st.add_base("bench.thing", "r" + std::to_string(i),
                    swofty::json::parse(R"({"name":"x","n":)" + std::to_string(i) + R"(,"items":[{"key":"a","v":1}]})"));
    for (int a = 0; a < addons; a++) {
        AsAddon as(order[a]);
        for (int i = a; i < records; i += addons) {
            std::string id = "game/r" + std::to_string(i), p = R"({"items":[{"key":"b","v":2}]})";
            t->layer("bench.thing", id.c_str(), SwoftyText{p.data(), p.size()}, SWOFTY_SCOPE_GLOBAL);
        }
        SwoftyLayerDesc d{};
        d.size = sizeof d;
        d.registry = "bench.thing";
        d.prefix = "game/r1";
        d.edit = edit_copy;
        t->layer_ex(&d, nullptr);
    }
    st.end_register();
    auto t0 = std::chrono::steady_clock::now();
    char buf[4096];
    size_t len;
    t->get("bench.thing", "game/r0", buf, sizeof buf, &len);
    auto t1 = std::chrono::steady_clock::now();
    const int reads = 200000;
    std::size_t ok = 0;
    for (int i = 0; i < reads; i++) {
        std::string id = "game/r" + std::to_string(i % records);
        ok += t->get("bench.thing", id.c_str(), buf, sizeof buf, &len) == SWOFTY_ASSET_OK;
    }
    auto t2 = std::chrono::steady_clock::now();
    double build_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double per_read_us = std::chrono::duration<double, std::micro>(t2 - t1).count() / reads;
    char line[512];
    std::snprintf(line, sizeof line,
                  "[perf] registry read: %d records, %d addons, %zu layers; first read (full evaluation) %.1f ms; "
                  "%d cached reads %.3f us each (%zu ok)\n",
                  records, addons, st.layer_count("", "") + records + addons, build_ms, reads, per_read_us, ok);
    std::fputs(line, stdout);
    if (!perf_log.empty())
        if (FILE *f = std::fopen(perf_log.c_str(), "a")) {
            std::fputs(line, f);
            std::fclose(f);
        }
    return ok == static_cast<std::size_t>(reads) ? 0 : 1;
}

}

int main(int argc, char **argv)
{
    if (argc >= 3 && !std::strcmp(argv[1], "--dump")) {
        std::fputs(swofty::json::dump(dump_case(argv[2])).c_str(), stdout);
        std::fputs("\n", stdout);
        return 0;
    }
    if (argc >= 2 && !std::strcmp(argv[1], "--fake")) {
        g_fake = true;
        argv++;
        argc--;
    }
    if (argc >= 2 && !std::strcmp(argv[1], "--bench"))
        return bench(argc >= 3 ? argv[2] : "");
    std::string dir = argc >= 2 ? argv[1] : "../../packages/sdk/std/tests/cases";
    std::vector<std::string> files;
    if (DIR *d = opendir(dir.c_str())) {
        while (auto *e = readdir(d))
            if (std::strstr(e->d_name, ".json"))
                files.push_back(dir + "/" + e->d_name);
        closedir(d);
    }
    std::sort(files.begin(), files.end());
    int failed = 0;
    for (auto &f : files)
        failed += run_case(f, argc >= 3);
    g_core.reset();
    std::printf("registry corpus%s: %zu case(s), %d failure(s)\n", g_fake ? " through swofty/fake_core.hpp" : "",
                files.size(), failed);
    return failed ? 1 : 0;
}
