// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Test-only stand-in for the core's SwoftyRegistry and SwoftyStorage tables. It runs register.cpp's
// SWOFTY_REGISTER through the DLL's own SwoftyAddonRegister export and writes every call it receives as
// JSON: declares with their schema, adds with their key, the id it answered and the record value. Any
// other registry call is captured as unexpected. Storage reads come from the addon folder given on the
// command line.
//
//   fake_core <addon folder> <out.json> [api version]

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include <swofty/json.hpp>
#include <swofty_api.h>

extern "C" int SwoftyAddonRegister(const SwoftyApi *a);

namespace {

namespace json = swofty::json;

std::string g_root;
json::Value g_calls = json::Value::object({{"declare", json::Value::array()},
                                           {"add", json::Value::array()},
                                           {"unexpected", json::Value::array()},
                                           {"storage", json::Value::array()},
                                           {"log", json::Value::array()}});

std::string text(SwoftyText t) { return t.data ? std::string(t.data, t.size) : std::string(); }

bool slurp(const char *path, std::string &out)
{
    std::ifstream f(g_root + "/" + path, std::ios::binary);
    if (!f)
        return false;
    std::ostringstream s;
    s << f.rdbuf();
    out = s.str();
    return true;
}

void fake_log(const char *fmt, ...)
{
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "%s\n", buf);
    g_calls["log"].push_back(buf);
}

int available() { return 1; }

int declare(const char *registry, SwoftyText schema)
{
    json::Error err;
    json::Value s = json::parse(text(schema), &err);
    g_calls["declare"].push_back(json::Value::object(
        {{"registry", registry}, {"schema", err.ok() ? s : json::Value(text(schema))}, {"schema_ok", err.ok()}}));
    return SWOFTY_ASSET_OK;
}

int add(const char *registry, const char *key, SwoftyText value, char *id, size_t cap)
{
    std::string full = std::string("swofty.progressive/") + key;
    json::Error err;
    json::Value v = json::parse(text(value), &err);
    g_calls["add"].push_back(json::Value::object({{"registry", registry},
                                                  {"key", key},
                                                  {"id", full},
                                                  {"value", err.ok() ? v : json::Value(text(value))},
                                                  {"value_ok", err.ok()},
                                                  {"bytes", static_cast<int64_t>(value.size)}}));
    if (full.size() + 1 > cap)
        return SWOFTY_ASSET_INVALID;
    std::memcpy(id, full.c_str(), full.size() + 1);
    return SWOFTY_ASSET_OK;
}

int last_error(char *out, size_t cap)
{
    if (cap)
        out[0] = 0;
    return SWOFTY_ASSET_OK;
}

int unexpected(const char *what)
{
    g_calls["unexpected"].push_back(what);
    return SWOFTY_ASSET_UNAVAILABLE;
}

int storage_available() { return 1; }

int storage_size(const char *path, uint64_t *out)
{
    std::string s;
    bool ok = slurp(path, s);
    g_calls["storage"].push_back(json::Value::object({{"call", "size"}, {"path", path}, {"ok", ok}}));
    if (!ok)
        return SWOFTY_ASSET_INVALID;
    *out = s.size();
    return SWOFTY_ASSET_OK;
}

int storage_read(const char *path, void *buf, size_t cap, size_t *len)
{
    std::string s;
    bool ok = slurp(path, s);
    g_calls["storage"].push_back(json::Value::object({{"call", "read"}, {"path", path}, {"ok", ok}}));
    if (!ok || s.size() > cap)
        return SWOFTY_ASSET_INVALID;
    std::memcpy(buf, s.data(), s.size());
    *len = s.size();
    return SWOFTY_ASSET_OK;
}

int storage_write(const char *, const void *, size_t) { return unexpected("storage.write"); }
int storage_append(const char *, const void *, size_t) { return unexpected("storage.append"); }
int storage_remove(const char *) { return unexpected("storage.remove"); }
int storage_list(const char *, char *, size_t, size_t *) { return unexpected("storage.list"); }

int r_check(const char *, SwoftyCheckFn, void *) { return unexpected("check"); }
int r_adapter(const char *, uint32_t, uint32_t, SwoftyConvertFn, SwoftyConvertFn, void *) { return unexpected("adapter"); }
int r_provide(const char *, const char *, SwoftyText) { return unexpected("provide"); }
int r_provide_fn(const char *, SwoftyProvideFn, SwoftyListFn, void *) { return unexpected("provide_fn"); }
int r_invalidate(const char *, const char *) { return unexpected("invalidate"); }
int r_layer(const char *, const char *, SwoftyText, uint32_t) { return unexpected("layer"); }
int r_clear_layer(const char *, const char *, uint32_t) { return unexpected("clear_layer"); }
int r_get(const char *, const char *, char *, size_t, size_t *) { return unexpected("get"); }
int r_status(const char *, const char *) { return unexpected("status"); }
int r_origin(const char *, const char *, const char *, char *, size_t) { return unexpected("origin"); }
int r_first(const char *, const char *, char *, size_t) { return unexpected("first"); }
int r_next(const char *, const char *, const char *, char *, size_t) { return unexpected("next"); }
int r_watch(const char *, SwoftyRecordFn, void *, uint32_t *) { return unexpected("watch"); }
int r_unwatch(uint32_t) { return unexpected("unwatch"); }
int r_variant(const char *, const char *, const char *, SwoftyEditFn, void *, char *, size_t) { return unexpected("variant"); }
int r_layer_fn(const char *, const char *, SwoftyEditFn, void *, uint32_t) { return unexpected("layer_fn"); }
int r_layer_select(const char *, SwoftyMatchFn, const char *, const char *const *, size_t, SwoftyEditFn, void *, uint32_t)
{
    return unexpected("layer_select");
}
int r_option(const char *, uint32_t, SwoftySettingValue *) { return unexpected("option"); }
int r_migrate(const char *, uint32_t, uint32_t, SwoftyConvertFn, void *) { return unexpected("migrate"); }
int r_layer_ex(const SwoftyLayerDesc *, uint32_t *) { return unexpected("layer_ex"); }
int r_layer_in(uint32_t, SwoftyText) { return unexpected("layer_in"); }
int r_protect(const char *, const char *, const char *const *, size_t) { return unexpected("protect"); }
int r_tag_add(const char *, const char *, const char *) { return unexpected("tag_add"); }
int r_tag_contains(const char *, const char *, const char *, int *) { return unexpected("tag_contains"); }
int r_tag_members(const char *, const char *, char *, size_t, size_t *) { return unexpected("tag_members"); }
int r_variant_patch(const char *, const char *, const char *, SwoftyText, char *, size_t) { return unexpected("variant_patch"); }
int r_stat_id(const char *, const char *, int64_t *) { return unexpected("stat_id"); }

const SwoftyRegistry kRegistry = {
    .version = 1,
    .available = available,
    .declare = declare,
    .check = r_check,
    .adapter = r_adapter,
    .provide = r_provide,
    .provide_fn = r_provide_fn,
    .invalidate = r_invalidate,
    .layer = r_layer,
    .clear_layer = r_clear_layer,
    .get = r_get,
    .status = r_status,
    .origin = r_origin,
    .first = r_first,
    .next = r_next,
    .watch = r_watch,
    .unwatch = r_unwatch,
    .last_error = last_error,
    .add = add,
    .variant = r_variant,
    .layer_fn = r_layer_fn,
    .layer_select = r_layer_select,
    .option = r_option,
    .migrate = r_migrate,
    .layer_ex = r_layer_ex,
    .layer_in = r_layer_in,
    .protect = r_protect,
    .tag_add = r_tag_add,
    .tag_contains = r_tag_contains,
    .tag_members = r_tag_members,
    .variant_patch = r_variant_patch,
    .stat_id = r_stat_id,
};

const SwoftyStorage kStorage = {
    .available = storage_available,
    .read = storage_read,
    .write = storage_write,
    .append = storage_append,
    .remove = storage_remove,
    .size = storage_size,
    .list = storage_list,
};

}

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <addon folder> <out.json> [api version]\n", argv[0]);
        return 2;
    }
    g_root = argv[1];
    SwoftyApi api{};
    api.version = argc > 3 ? static_cast<uint32_t>(std::atoi(argv[3])) : SWOFTY_API_VERSION;
    api.log = fake_log;
    api.storage = &kStorage;
    api.registry = &kRegistry;
    int r = SwoftyAddonRegister(&api);
    g_calls["returned"] = r;
    g_calls["api_version"] = static_cast<int64_t>(api.version);
    std::ofstream out(argv[2], std::ios::binary);
    out << json::dump(g_calls, 1);
    return out ? 0 : 1;
}
