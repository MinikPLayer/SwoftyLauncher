#include "table.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

namespace swofty_core::registry {

namespace {

std::function<std::string(const void *)> resolver;
thread_local std::string tl_as;
thread_local bool tl_as_set = false;

std::string caller(const void *from)
{
    if (tl_as_set)
        return tl_as;
    return resolver ? resolver(from) : std::string();
}

std::string str(const char *s) { return s ? std::string(s) : std::string(); }

Value parse_text(SwoftyText t, bool &ok)
{
    swofty::json::Error e;
    Value v = swofty::json::parse(std::string_view(t.data ? t.data : "", t.data ? t.size : 0), &e);
    ok = e.message.empty();
    if (!ok)
        set_last_error("JSON at line " + std::to_string(e.line) + ", column " + std::to_string(e.column) + ": " +
                       e.message);
    return v;
}

int put(const std::string &s, char *out, size_t cap, size_t *len)
{
    if (len)
        *len = s.size();
    if (!out || cap < s.size())
        return SWOFTY_TOO_SMALL;
    std::memcpy(out, s.data(), s.size());
    if (cap > s.size())
        out[s.size()] = 0;
    return SWOFTY_ASSET_OK;
}

int put_id(const std::string &s, char *out, size_t cap)
{
    if (!out || !cap)
        return SWOFTY_ASSET_OK;
    if (cap < s.size() + 1) {
        out[0] = 0;
        return SWOFTY_TOO_SMALL;
    }
    std::memcpy(out, s.c_str(), s.size() + 1);
    return SWOFTY_ASSET_OK;
}

Scope scope_of(uint32_t s) { return s == SWOFTY_SCOPE_PROFILE ? Scope::profile : s == SWOFTY_SCOPE_CAREER ? Scope::career : Scope::global; }

int no_caller(const char *what)
{
    set_last_error(std::string(what) + ": the calling addon is unknown (no addon lists this dll)");
    return SWOFTY_ASSET_INVALID;
}

int c_available(void) { return 1; }

int c_declare(const char *registry, SwoftyText schema)
{
    std::string who = caller(__builtin_return_address(0));
    bool ok;
    Value v = parse_text(schema, ok);
    if (!ok)
        return SWOFTY_ASSET_INVALID;
    return store().declare(str(registry), v, false, who);
}

int c_check(const char *registry, SwoftyCheckFn fn, void *user)
{
    std::string who = caller(__builtin_return_address(0));
    return store().check(who, str(registry), fn, user);
}

int c_adapter(const char *registry, uint32_t from, uint32_t to, SwoftyConvertFn up, SwoftyConvertFn down, void *user)
{
    std::string who = caller(__builtin_return_address(0));
    return store().adapter(who, str(registry), from, to, up, down, user);
}

int c_provide(const char *registry, const char *id, SwoftyText value)
{
    std::string who = caller(__builtin_return_address(0));
    if (who.empty())
        return no_caller("provide");
    bool ok;
    Value v = parse_text(value, ok);
    if (!ok)
        return SWOFTY_ASSET_INVALID;
    std::string key = str(id);
    if (key.rfind(who + "/", 0) == 0)
        key = key.substr(who.size() + 1);
    return store().add(who, str(registry), key, v, nullptr, "provide");
}

int c_provide_fn(const char *registry, SwoftyProvideFn fn, SwoftyListFn list, void *user)
{
    std::string who = caller(__builtin_return_address(0));
    if (who.empty())
        return no_caller("provide_fn");
    return store().provide_fn(who, str(registry), fn, list, user);
}

int c_invalidate(const char *registry, const char *id)
{
    std::string s = str(id);
    return store().invalidate(str(registry), id ? &s : nullptr);
}

int c_layer(const char *registry, const char *id, SwoftyText patch, uint32_t scope)
{
    std::string who = caller(__builtin_return_address(0));
    if (who.empty())
        return no_caller("layer");
    bool ok;
    Value v = parse_text(patch, ok);
    if (!ok)
        return SWOFTY_ASSET_INVALID;
    LayerSpec sp;
    sp.addon = who;
    sp.registry = str(registry);
    sp.id = str(id);
    sp.scope = scope_of(scope);
    sp.change.patch = v;
    return store().layer(sp, nullptr);
}

int c_clear_layer(const char *registry, const char *id, uint32_t scope)
{
    std::string who = caller(__builtin_return_address(0));
    return store().clear_layer(who, str(registry), str(id), scope_of(scope));
}

int c_get(const char *registry, const char *id, char *out, size_t cap, size_t *len)
{
    Record r = store().get(str(registry), str(id));
    switch (r.status) {
    case Status::ok:
        return put(swofty::json::dump(r.value), out, cap, len);
    case Status::no_registry:
        set_last_error("get: registry " + str(registry) + " is not declared");
        return SWOFTY_ASSET_UNAVAILABLE;
    case Status::late:
        set_last_error("get: a re-entrant read from inside the registry's own callback");
        return SWOFTY_ASSET_LATE;
    default:
        set_last_error(std::string("get: ") + str(id) + " is " + status_name(r.status));
        return SWOFTY_ASSET_INVALID;
    }
}

int c_status(const char *registry, const char *id) { return static_cast<int>(store().status(str(registry), str(id))); }

int c_origin(const char *registry, const char *id, const char *pointer, char *addon, size_t cap)
{
    std::string o = store().origin(str(registry), str(id), str(pointer));
    if (o.empty())
        return SWOFTY_ASSET_INVALID;
    return put_id(o, addon, cap);
}

int c_first(const char *registry, const char *prefix, char *id, size_t cap)
{
    std::string out;
    if (!store().next(str(registry), str(prefix), nullptr, out))
        return SWOFTY_ASSET_INVALID;
    return put_id(out, id, cap);
}

int c_next(const char *registry, const char *prefix, const char *after, char *id, size_t cap)
{
    std::string out, a = str(after);
    if (!store().next(str(registry), str(prefix), &a, out))
        return SWOFTY_ASSET_INVALID;
    return put_id(out, id, cap);
}

int c_watch(const char *registry, SwoftyRecordFn fn, void *user, uint32_t *token)
{
    return store().watch(str(registry), fn, user, token);
}

int c_unwatch(uint32_t token) { return store().unwatch(token); }

int c_last_error(char *out, size_t cap)
{
    const std::string &e = last_error();
    if (!out || !cap)
        return SWOFTY_ASSET_INVALID;
    std::size_t n = std::min(cap - 1, e.size());
    std::memcpy(out, e.data(), n);
    out[n] = 0;
    return e.empty() ? SWOFTY_ASSET_INVALID : SWOFTY_ASSET_OK;
}

int c_add(const char *registry, const char *key, SwoftyText value, char *id, size_t cap)
{
    std::string who = caller(__builtin_return_address(0));
    if (who.empty())
        return no_caller("add");
    bool ok;
    Value v = parse_text(value, ok);
    if (!ok)
        return SWOFTY_ASSET_INVALID;
    std::string rid;
    int r = store().add(who, str(registry), str(key), v, &rid, "code");
    if (r == SWOFTY_ASSET_OK)
        put_id(rid, id, cap);
    return r;
}

int c_variant(const char *registry, const char *key, const char *base, SwoftyEditFn edit, void *user, char *id,
              size_t cap)
{
    std::string who = caller(__builtin_return_address(0));
    if (who.empty())
        return no_caller("variant");
    Change c;
    c.is_patch = false;
    c.fn = edit;
    c.user = user;
    std::string rid;
    int r = store().variant(who, str(registry), str(key), str(base), c, &rid, "code");
    if (r == SWOFTY_ASSET_OK)
        put_id(rid, id, cap);
    return r;
}

int c_layer_fn(const char *registry, const char *id, SwoftyEditFn edit, void *user, uint32_t scope)
{
    std::string who = caller(__builtin_return_address(0));
    if (who.empty())
        return no_caller("layer_fn");
    LayerSpec sp;
    sp.addon = who;
    sp.registry = str(registry);
    sp.id = str(id);
    sp.scope = scope_of(scope);
    sp.change.is_patch = false;
    sp.change.fn = edit;
    sp.change.user = user;
    return store().layer(sp, nullptr);
}

int c_layer_select(const char *registry, SwoftyMatchFn match, const char *prefix, const char *const *except,
                   size_t except_count, SwoftyEditFn edit, void *user, uint32_t scope)
{
    std::string who = caller(__builtin_return_address(0));
    if (who.empty())
        return no_caller("layer_select");
    LayerSpec sp;
    sp.addon = who;
    sp.registry = str(registry);
    sp.selector = true;
    sp.scope = scope_of(scope);
    sp.match = match;
    sp.match_user = user;
    sp.has_prefix = prefix != nullptr;
    sp.prefix = str(prefix);
    for (size_t i = 0; except && i < except_count; i++)
        sp.except.push_back(str(except[i]));
    sp.change.is_patch = false;
    sp.change.fn = edit;
    sp.change.user = user;
    return store().layer(sp, nullptr);
}

int c_option(const char *id, uint32_t type, SwoftySettingValue *out)
{
    std::string who = caller(__builtin_return_address(0));
    Value v;
    if (who.empty() || !id || !store().read_option(who, id, v))
        return SWOFTY_ASSET_INVALID;
    if (!out)
        return SWOFTY_ASSET_OK;
    static thread_local std::string choice;
    out->type = type;
    if (type == SWOFTY_SETTING_TOGGLE)
        out->toggle = v.is_bool() ? v.as_bool() : v.as_double() != 0 || v.as_string() == "true";
    else if (type == SWOFTY_SETTING_SLIDER)
        out->slider = v.is_number() ? v.as_double() : std::atof(std::string(v.as_string()).c_str());
    else {
        choice = v.is_string() ? std::string(v.as_string()) : swofty::json::dump(v);
        out->choice = choice.c_str();
    }
    return SWOFTY_ASSET_OK;
}

int c_migrate(const char *registry, uint32_t from, uint32_t to, SwoftyConvertFn fn, void *user)
{
    std::string who = caller(__builtin_return_address(0));
    return store().migrate(who, str(registry), from, to, fn, user);
}

int c_layer_ex(const SwoftyLayerDesc *d, uint32_t *layer)
{
    std::string who = caller(__builtin_return_address(0));
    if (who.empty())
        return no_caller("layer_ex");
    if (!d || d->size < sizeof(SwoftyLayerDesc) || !d->registry) {
        set_last_error("layer_ex: no descriptor, or one older than this core reads");
        return SWOFTY_ASSET_INVALID;
    }
    LayerSpec sp;
    sp.addon = who;
    sp.registry = d->registry;
    sp.scope = scope_of(d->scope);
    if (d->id && *d->id) {
        sp.id = d->id;
    } else {
        sp.selector = true;
        sp.match = d->match;
        sp.match_user = d->match_user;
        sp.has_prefix = d->prefix != nullptr;
        sp.prefix = str(d->prefix);
        for (size_t i = 0; d->except && i < d->except_count; i++)
            sp.except.push_back(str(d->except[i]));
        sp.tag = str(d->tag);
        sp.tag_field = str(d->tag_field);
        if (!sp.tag_field.empty() && sp.tag_field[0] != '/')
            sp.tag_field = "/" + sp.tag_field;
    }
    if (d->in.data && d->in.size) {
        bool ok;
        Value in = parse_text(d->in, ok);
        if (!ok)
            return SWOFTY_ASSET_INVALID;
        sp.in = in.is_object() ? Value::array({in}) : in;
    }
    if (d->edit) {
        sp.change.is_patch = false;
        sp.change.fn = d->edit;
        sp.change.user = d->user;
    } else {
        bool ok;
        sp.change.patch = parse_text(d->patch, ok);
        if (!ok || !sp.change.patch.is_object()) {
            set_last_error("layer_ex: neither an edit nor a merge patch object");
            return SWOFTY_ASSET_INVALID;
        }
    }
    return store().layer(sp, layer);
}

int c_layer_in(uint32_t layer, SwoftyText selectors)
{
    std::string who = caller(__builtin_return_address(0));
    bool ok;
    Value v = parse_text(selectors, ok);
    if (!ok)
        return SWOFTY_ASSET_INVALID;
    return store().layer_in(who, layer, v);
}

int c_protect(const char *registry, const char *id, const char *const *fields, size_t count)
{
    std::string who = caller(__builtin_return_address(0));
    if (who.empty())
        return no_caller("protect");
    std::vector<std::string> f;
    for (size_t i = 0; fields && i < count; i++)
        f.push_back(str(fields[i]));
    return store().protect(who, str(registry), str(id), f);
}

int c_tag_add(const char *registry, const char *tag, const char *id)
{
    std::string who = caller(__builtin_return_address(0));
    if (who.empty())
        return no_caller("tag_add");
    return store().tag_add(who, str(registry), str(tag), str(id));
}

int c_tag_contains(const char *registry, const char *tag, const char *id, int *out)
{
    auto m = store().tag_members(str(registry), str(tag));
    if (out)
        *out = std::binary_search(m.begin(), m.end(), str(id)) ? 1 : 0;
    return store().declared(str(registry)) ? SWOFTY_ASSET_OK : SWOFTY_ASSET_UNAVAILABLE;
}

int c_tag_members(const char *registry, const char *tag, char *out, size_t cap, size_t *len)
{
    if (!store().declared(str(registry)))
        return SWOFTY_ASSET_UNAVAILABLE;
    Value a = Value::array();
    for (auto &m : store().tag_members(str(registry), str(tag)))
        a.push_back(Value(m));
    return put(swofty::json::dump(a), out, cap, len);
}

int c_variant_patch(const char *registry, const char *key, const char *base, SwoftyText patch, char *id, size_t cap)
{
    std::string who = caller(__builtin_return_address(0));
    if (who.empty())
        return no_caller("variant_patch");
    bool ok;
    Value v = parse_text(patch, ok);
    if (!ok || !v.is_object())
        return SWOFTY_ASSET_INVALID;
    Change c;
    c.patch = v;
    std::string rid;
    int r = store().variant(who, str(registry), str(key), str(base), c, &rid, "code");
    if (r == SWOFTY_ASSET_OK)
        put_id(rid, id, cap);
    return r;
}

StatIdSource &stat_id_source()
{
    static StatIdSource fn;
    return fn;
}

int c_stat_id(const char *registry, const char *id, int64_t *out)
{
    if (out)
        *out = -1;
    if (!registry || !id || !out)
        return SWOFTY_ASSET_INVALID;
    if (!stat_id_source()) {
        set_last_error("stat_id: the stat-id store is not part of this core yet");
        return SWOFTY_ASSET_UNAVAILABLE;
    }
    return stat_id_source()(registry, id, out);
}

const SwoftyRegistry TABLE = {
    SWOFTY_REGISTRY_VERSION,
    c_available,
    c_declare,
    c_check,
    c_adapter,
    c_provide,
    c_provide_fn,
    c_invalidate,
    c_layer,
    c_clear_layer,
    c_get,
    c_status,
    c_origin,
    c_first,
    c_next,
    c_watch,
    c_unwatch,
    c_last_error,
    c_add,
    c_variant,
    c_layer_fn,
    c_layer_select,
    c_option,
    c_migrate,
    c_layer_ex,
    c_layer_in,
    c_protect,
    c_tag_add,
    c_tag_contains,
    c_tag_members,
    c_variant_patch,
    c_stat_id,
};

std::filesystem::path upath(const std::string &utf8)
{
    std::u8string u(utf8.begin(), utf8.end());
    return std::filesystem::path(u);
}

std::string base_name(const std::string &path)
{
    auto s = path.find_last_of("/\\");
    return s == std::string::npos ? path : path.substr(s + 1);
}

bool is_layer_key(const std::string &key, const std::vector<std::string> &order)
{
    auto s = key.find('/');
    if (s == std::string::npos)
        return false;
    std::string first = key.substr(0, s);
    if (first == "game" || first == "venue" || first == "person" || first == "constructor")
        return true;
    return std::find(order.begin(), order.end(), first) != order.end();
}

}

namespace {
std::unique_ptr<Store> g_store;
}

Store &store()
{
    if (!g_store)
        g_store = std::make_unique<Store>();
    return *g_store;
}

void reset_store() { g_store = std::make_unique<Store>(); }

const SwoftyRegistry *table() { return &TABLE; }

void set_caller_resolver(std::function<std::string(const void *)> fn) { resolver = std::move(fn); }

AsAddon::AsAddon(std::string addon) : prev_(tl_as), had_(tl_as_set)
{
    tl_as = std::move(addon);
    tl_as_set = true;
}

AsAddon::~AsAddon()
{
    tl_as = prev_;
    tl_as_set = had_;
}

int load_data_file(const std::string &addon, const std::string &path, const std::string &text)
{
    swofty::json::Error e;
    Value doc = swofty::json::parse(text, &e);
    if (!e.message.empty() || !doc.is_object()) {
        store().note_refusal(addon, "", "", "invalid",
                             path + ": not a JSON object (" + (e.message.empty() ? "wrong type" : e.message) + ")");
        return SWOFTY_ASSET_INVALID;
    }
    Store &st = store();
    auto order = st.order();
    std::string file = base_name(path);
    int bad = 0;
    for (auto &[name_at, body] : *doc.if_object()) {
        std::string reg = name_at.substr(0, name_at.find('@'));
        if (!body.is_object()) {
            bad++;
            continue;
        }
        if (file == "tags.json") {
            for (auto &[tag, ids] : *body.if_object())
                if (auto a = ids.if_array())
                    for (auto &id : *a)
                        bad += st.tag_add(addon, reg, tag, std::string(id.as_string())) != SWOFTY_ASSET_OK;
            continue;
        }
        for (auto &[key, value] : *body.if_object()) {
            if (key == "$select") {
                if (!value.is_array()) {
                    bad++;
                    continue;
                }
                for (auto &sel : *value.if_array()) {
                    LayerSpec sp;
                    sp.addon = addon;
                    sp.registry = reg;
                    sp.selector = true;
                    sp.source = path;
                    if (auto ids = sel.find("ids"); ids && ids->is_string()) {
                        std::string p(ids->as_string());
                        if (!p.empty() && p.back() == '*')
                            p.pop_back();
                        sp.has_prefix = true;
                        sp.prefix = p;
                    }
                    if (auto ex = sel.find("except"); ex && ex->is_array())
                        for (auto &x : *ex->if_array())
                            sp.except.emplace_back(x.as_string());
                    if (auto w = sel.find("where"); w && w->is_object()) {
                        sp.has_where = true;
                        sp.where = *w;
                    }
                    if (auto t = sel.find("tag")) {
                        if (t->is_string())
                            sp.tag = std::string(t->as_string());
                        else if (t->is_object() && t->size() == 1) {
                            auto &[field, tag] = t->if_object()->entry(0);
                            sp.tag_field = "/" + field;
                            sp.tag = std::string(tag.as_string());
                        }
                    }
                    if (auto in = sel.find("in"))
                        sp.in = in->is_object() ? Value::array({*in}) : *in;
                    if (auto when = sel.find("when"); when && when->is_object())
                        sp.when = *when;
                    sp.change.patch = sel["patch"];
                    if (!sp.change.patch.is_object()) {
                        bad++;
                        continue;
                    }
                    bad += st.layer(sp, nullptr) != SWOFTY_ASSET_OK;
                }
                continue;
            }
            if (!value.is_object()) {
                bad++;
                continue;
            }
            if (file == "layers.json" || is_layer_key(key, order)) {
                LayerSpec sp;
                sp.addon = addon;
                sp.registry = reg;
                sp.id = key;
                sp.source = path;
                sp.change.patch = value;
                bad += st.layer(sp, nullptr) != SWOFTY_ASSET_OK;
            } else if (auto ext = value.find("extends"); ext && ext->is_string()) {
                Change c;
                c.patch = value;
                bad += st.variant(addon, reg, key, std::string(ext->as_string()), c, nullptr, path) != SWOFTY_ASSET_OK;
            } else {
                bad += st.add(addon, reg, key, value, nullptr, path) != SWOFTY_ASSET_OK;
            }
        }
    }
    return bad ? SWOFTY_ASSET_INVALID : SWOFTY_ASSET_OK;
}

IndexResult load_index(const std::string &swofty_dir)
{
    IndexResult out;
    std::string base = swofty_dir;
    if (!base.empty() && base.back() != '/' && base.back() != '\\')
        base += '/';
    std::ifstream f(upath(base + "registry/index.json"), std::ios::binary);
    if (!f)
        return out;
    std::stringstream ss;
    ss << f.rdbuf();
    swofty::json::Error e;
    Value doc = swofty::json::parse(ss.str(), &e);
    if (!e.message.empty() || !doc.is_object())
        return out;
    out.found = true;
    out.profile = std::string(doc["profile"].as_string());
    if (auto o = doc["order"].if_array())
        for (auto &a : *o)
            if (a.is_string())
                out.order.emplace_back(a.as_string());
    if (!out.order.empty())
        store().set_order(out.order);
    if (!out.profile.empty()) {
        Context c = store().context();
        c.profile = out.profile;
        store().set_context(c);
    }
    std::vector<std::string> addons = out.order;
    if (auto data = doc["data"].if_object())
        for (auto &[addon, files] : *data)
            if (std::find(addons.begin(), addons.end(), addon) == addons.end())
                addons.push_back(addon);
    for (auto &addon : addons) {
        const Value &files = doc["data"][addon];
        if (!files.is_array())
            continue;
        for (auto &p : *files.if_array()) {
            std::string rel(p.as_string());
            std::ifstream df(upath(base + rel), std::ios::binary);
            out.files++;
            if (!df) {
                out.failed++;
                store().note_refusal(addon, "", "", "invalid", rel + ": listed in index.json but unreadable");
                continue;
            }
            std::stringstream ds;
            ds << df.rdbuf();
            AsAddon as(addon);
            if (load_data_file(addon, rel, ds.str()) != SWOFTY_ASSET_OK)
                out.failed++;
        }
    }
    return out;
}

void set_stat_id_source(StatIdSource fn)
{
    stat_id_source() = std::move(fn);
}

int write_reports(const std::string &registry_dir, const std::function<void(Value &, Value &)> &extend)
{
    std::string base = registry_dir;
    if (!base.empty() && base.back() != '/' && base.back() != '\\')
        base += '/';
    int r = SWOFTY_ASSET_OK;
    Value effective = store().effective_report(), conflicts = store().conflicts_report();
    if (extend)
        extend(effective, conflicts);
    for (auto &[file, doc] : {std::pair<std::string, Value>{"effective.json", std::move(effective)},
                              std::pair<std::string, Value>{"conflicts.json", std::move(conflicts)}}) {
        std::error_code ec;
        std::filesystem::create_directories(upath(base), ec);
        std::filesystem::path tmp = upath(base + file + ".tmp"), dest = upath(base + file);
        std::string text = swofty::json::dump(doc, 1);
        bool ok;
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            ok = f && f.write(text.data(), static_cast<std::streamsize>(text.size())) && f.flush();
        }
        if (!ok) {
            r = SWOFTY_ASSET_UNAVAILABLE;
            continue;
        }
        std::filesystem::rename(tmp, dest, ec);
        if (ec)
            r = SWOFTY_ASSET_UNAVAILABLE;
    }
    return r;
}

}
