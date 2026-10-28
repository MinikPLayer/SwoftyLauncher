#include "runtime.hpp"

#include <swofty/mixin.hpp>

#include <mutex>

#include "perf.hpp"

std::string settings_addon(const void *from, std::string &dll);

namespace {

const char *const RESULTS[] = {"ok", "late", "unavailable", "invalid"};

struct Caller {
    std::string id, dll;
};

std::mutex lock;
json::Value doc;
bool opened = true;

json::Value str(const std::string &s)
{
    json::Value v;
    v.type = json::Value::Str;
    v.s = s;
    return v;
}

json::Value num(double n)
{
    json::Value v;
    v.type = json::Value::Num;
    v.n = n;
    v.s = std::to_string((long long)n);
    return v;
}

json::Value list(const char *const *items, size_t n)
{
    json::Value v;
    v.type = json::Value::Arr;
    for (size_t i = 0; items && i < n; i++)
        if (items[i])
            v.a.push_back(str(items[i]));
    return v;
}

json::Value object(std::vector<std::pair<std::string, json::Value>> kv)
{
    json::Value v;
    v.type = json::Value::Obj;
    v.o = std::move(kv);
    return v;
}

json::Value &array(const char *key)
{
    for (auto &[k, v] : doc.o)
        if (k == key)
            return v;
    json::Value a;
    a.type = json::Value::Arr;
    doc.o.push_back({key, a});
    return doc.o.back().second;
}

Caller caller(const void *from)
{
    Caller c;
    c.id = settings_addon(from, c.dll);
    return c;
}

int done(const Caller &c, const std::string &call, int r, const std::string &why = {})
{
    swofty::log("[assets] api: %s (%s) %s -> %s%s%s", c.id.empty() ? "no addon" : c.id.c_str(), c.dll.c_str(), call.c_str(),
                RESULTS[r], why.empty() ? "" : ": ", why.c_str());
    return r;
}

bool text(const char *s)
{
    return s && *s;
}

std::string addon_file(const Caller &c, const char *file, std::string &why)
{
    if (c.id.empty())
        return why = "no addon in settings.json lists " + c.dll + ", so it has no folder", std::string();
    std::string f = file ? file : "";
    std::replace(f.begin(), f.end(), '\\', '/');
    if (f.empty() || f[0] == '/' || f.find(':') != std::string::npos || ("/" + f + "/").find("/../") != std::string::npos)
        return why = "'" + f + "' is not a relative path inside swofty/addons/" + c.id + "/", std::string();
    std::string rel = "addons/" + c.id + "/" + f;
    uint64_t size = 0;
    if (!runtime::file_size(runtime::under_swofty(rel), &size))
        return why = "swofty/" + rel + " is missing", std::string();
    return rel;
}

int submit(const Caller &c, const std::string &call, const char *feature, const char *key, json::Value entry,
           int (*live)(const json::Value &))
{
    if (!runtime::enabled(feature, nullptr))
        return done(c, call, SWOFTY_ASSET_UNAVAILABLE,
                    std::string("the runtime ") + feature + " feature is not in this core or not turned on");
    entry.o.push_back({"origin", str(c.id.empty() ? c.dll : c.id)});
    {
        std::lock_guard<std::mutex> g(lock);
        if (opened) {
            array(key).a.push_back(std::move(entry));
            return done(c, call, SWOFTY_ASSET_OK);
        }
    }
    int r = live ? live(entry) : SWOFTY_ASSET_LATE;
    return done(c, call, r, r == SWOFTY_ASSET_LATE ? "its point has passed" : "");
}

int late_resource(const json::Value &)
{
    return SWOFTY_ASSET_LATE;
}

int api_available(const char *feature)
{
    return feature && runtime::enabled(feature, nullptr);
}

int api_register_group(const char *group, const char *const *erps, size_t erp_count, const char *const *deps,
                       size_t dep_count, const char *templ)
{
    Caller c = caller(__builtin_return_address(0));
    std::string call = std::string("register_group ") + (group ? group : "(null)");
    if (!text(group) || !erp_count)
        return done(c, call, SWOFTY_ASSET_INVALID, "it needs a group and at least one ERP");
    json::Value e = object({{"group", str(group)}, {"erps", list(erps, erp_count)}, {"deps", list(deps, dep_count)}});
    if (text(templ))
        e.o.push_back({"template", str(templ)});
    return submit(c, call, "group", "asset_groups", std::move(e), runtime::group_add);
}

int api_put_resource(const char *uri, const SwoftyFragment *fragments, size_t count, const char *const *erps,
                     size_t erp_count)
{
    Caller c = caller(__builtin_return_address(0));
    std::string call = std::string("put_resource ") + (uri ? uri : "(null)");
    if (!text(uri) || !fragments || !count)
        return done(c, call, SWOFTY_ASSET_INVALID, "it needs a uri and at least one fragment");
    json::Value frags;
    frags.type = json::Value::Arr;
    for (size_t i = 0; i < count; i++) {
        std::string why, rel = addon_file(c, fragments[i].file, why);
        if (rel.empty())
            return done(c, call, SWOFTY_ASSET_INVALID, "fragment " + std::to_string(i) + ": " + why);
        json::Value f = object({{"file", str(rel)}});
        if (text(fragments[i].fourcc))
            f.o.push_back({"fourcc", str(fragments[i].fourcc)});
        if (fragments[i].flags)
            f.o.push_back({"flags", num(fragments[i].flags)});
        frags.a.push_back(std::move(f));
    }
    json::Value e = object({{"uri", str(uri)}, {"fragments", std::move(frags)}});
    if (erp_count)
        e.o.push_back({"erps", list(erps, erp_count)});
    return submit(c, call, "erp", "resources", std::move(e), late_resource);
}

int db_entry(const Caller &c, const std::string &call, json::Value e)
{
    if (runtime::load_stage() != "early" && runtime::enabled("db", nullptr))
        return done(c, call, SWOFTY_ASSET_LATE, "the core loaded at the window stage, after the game database loaded");
    return submit(c, call, "db", "db_edits", std::move(e), runtime::db_add);
}

int api_db_set(const char *key, const char *type, const char *value)
{
    Caller c = caller(__builtin_return_address(0));
    std::string call = std::string("db_set ") + (key ? key : "(null)") + " = " + (value ? value : "(null)");
    if (!text(key) || !text(type) || !value)
        return done(c, call, SWOFTY_ASSET_INVALID, "it needs a key, a type and a value");
    std::string k = key, t = type;
    bool hash = k.size() == 18 && k.rfind("0x", 0) == 0 && k.find_first_not_of("0123456789abcdefABCDEF", 2) == std::string::npos;
    bool tag = t.find_first_not_of("0123456789") == std::string::npos;
    json::Value e = object({{hash ? "hash" : "key", str(hash ? k.substr(2) : k)},
                            {"type", tag ? num(strtod(t.c_str(), nullptr)) : str(t)},
                            {"value", str(value)}});
    return db_entry(c, call, std::move(e));
}

int api_db_set_raw(uint64_t hash, uint8_t tag, const void *value, size_t size)
{
    Caller c = caller(__builtin_return_address(0));
    std::string v(static_cast<const char *>(value), value ? size : 0);
    std::string call = "db_set " + runtime::hex16(hash) + " tag " + std::to_string(tag) + " = " + v;
    if (!hash || (size && !value) || v.find('\0') != std::string::npos)
        return done(c, call, SWOFTY_ASSET_INVALID, "it needs a hash and a value without NUL bytes");
    char h[17];
    snprintf(h, sizeof h, "%016llx", (unsigned long long)hash);
    json::Value e = object({{"hash", str(h)}, {"type", num(tag)}, {"value", str(v)}});
    return db_entry(c, call, std::move(e));
}

int api_lang_set(const char *language, const char *key, const char *value)
{
    Caller c = caller(__builtin_return_address(0));
    std::string call = std::string("lang_set ") + (language ? language : "(null)") + " " + (key ? key : "(null)");
    if (!text(language) || !text(key) || !value)
        return done(c, call, SWOFTY_ASSET_INVALID, "it needs a language, a key and a text");
    json::Value e = object({{"language", str(runtime::lower(language))}, {"strings", object({{key, str(value)}})}});
    return submit(c, call, "lang", "lang", std::move(e), runtime::lang_add);
}

int api_lang_table(const char *language, const char *file)
{
    Caller c = caller(__builtin_return_address(0));
    std::string call = std::string("lang_table ") + (language ? language : "(null)") + " " + (file ? file : "(null)");
    if (!text(language))
        return done(c, call, SWOFTY_ASSET_INVALID, "it needs a language");
    std::string why, rel = addon_file(c, file, why);
    if (rel.empty())
        return done(c, call, SWOFTY_ASSET_INVALID, why);
    json::Value e = object({{"language", str(runtime::lower(language))}, {"table", str(rel)}});
    return submit(c, call, "lang", "lang", std::move(e), runtime::lang_add);
}

int api_overlay_file(const char *game_path, const char *file)
{
    Caller c = caller(__builtin_return_address(0));
    std::string call = std::string("overlay_file ") + (game_path ? game_path : "(null)") + " " + (file ? file : "(null)");
    std::string path = game_path ? runtime::lower(game_path) : "";
    if (path.empty() || path[0] == '/' || ("/" + path + "/").find("/../") != std::string::npos)
        return done(c, call, SWOFTY_ASSET_INVALID, "the game path must be relative to the game folder");
    std::string why, rel = addon_file(c, file, why);
    if (rel.empty())
        return done(c, call, SWOFTY_ASSET_INVALID, why);
    json::Value e = object({{"path", str(path)}, {"file", str(rel)}});
    return submit(c, call, "file", "overlay_files", std::move(e), runtime::file_add);
}

const SwoftyAssets table = {api_available, api_register_group, api_put_resource, api_db_set,
                            api_db_set_raw, api_lang_set,       api_lang_table,   api_overlay_file};

}

namespace runtime {

const SwoftyAssets *assets_table()
{
    doc.type = json::Value::Obj;
    return &table;
}

const json::Value &sdk()
{
    return doc;
}

void ready()
{
    {
        std::lock_guard<std::mutex> g(lock);
        if (!opened)
            return;
        opened = false;
    }
    assets_setup();
    resources_setup();
    gamedb_setup();
    dbread_setup();
    overlay_setup();
    lang_override_setup();
    events_setup();
    hud_setup();
    focus_setup();
    // postfx only observes barriers, and grade replaces some batches, so postfx subscribes first.
    postfx_setup();
    grade_setup();
    telemetry_setup();
    net_trace_setup();
    netplay_setup();
    netplay_drive_setup();
    perf::start();
}

}
