#pragma once

#include <swofty_api.h>
#include <swofty/json.hpp>

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <shared_mutex>
#include <string>
#include <vector>

#include "validate.hpp"

namespace swofty_core::registry {

using Value = swofty::json::Value;

enum class Applies { boot, content, session, read };
enum class Scope { global, profile, career };
enum class Status { ok = SWOFTY_STATUS_OK, no_record, no_registry, invalid, late };

const char *applies_name(Applies a);
const char *status_name(Status s);
const char *scope_name(Scope s);

struct Context {
    std::string profile = "default";
    std::string career;
    std::string mode, data_group, setup_mode, session_type, track, vehicle_package;
    std::vector<std::string> content;
    int year = 0;
    bool has_year = false;
};

struct Def {
    std::string name;
    Applies applies = Applies::read;
    int major = 1, minor = 0;
    bool snapshot = false;
    std::size_t budget = 65536;
    std::vector<std::pair<std::string, std::vector<std::string>>> keys;
    std::vector<std::pair<std::string, std::string>> refs;
    bool shared = false;
    std::string provider_registry, provides, resolved, shared_prefix;
    std::string schema;
    bool core = false;
};

Def parse_schema(const std::string &name, const Value &schema);

struct Change {
    bool is_patch = true;
    Value patch;
    SwoftyEditFn fn = nullptr;
    void *user = nullptr;
    std::shared_ptr<const Value> schema;
};

struct LayerSpec {
    std::string addon, registry, id;
    Scope scope = Scope::global;
    bool selector = false;
    SwoftyMatchFn match = nullptr;
    void *match_user = nullptr;
    bool has_where = false;
    Value where;
    bool has_prefix = false;
    std::string prefix;
    std::vector<std::string> except;
    std::string tag, tag_field;
    Value in;
    Value when;
    Change change;
    std::string source;
};

struct Refusal {
    std::string addon, registry, id, reason, message;
    int op = -1;
};

struct Conflict {
    std::string kind, registry, id, pointer;
    std::vector<std::string> addons;
    std::string winner;
};

struct Record {
    Status status = Status::no_record;
    Value value;
    std::map<std::string, std::string> origins;
    std::string owner, why;
};

// Career snapshots (W4, native/src/core/snapshots/). While a career is loaded (Context::career set),
// the store asks the snapshot source for a record's frozen value before it computes it. A found value
// replaces base and every global and profile layer; the career's own layers still apply on top. A
// value frozen with another major is converted through the registry's migrate, else its adapter's
// up; when neither exists the record reads live. The source must not call into the store.
struct Frozen {
    bool found = false;
    Value value;
    int major = 0;
};
using SnapshotSource = std::function<Frozen(const std::string &registry, const std::string &id, const Context &ctx)>;

class Store {
public:
    Store();
    ~Store();
    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;

    void set_order(const std::vector<std::string> &order);
    std::vector<std::string> order() const;
    void set_validator(Validator *v);
    void set_option_source(std::function<bool(const std::string &addon, const std::string &id, Value &out)> fn);
    void set_override_source(std::function<bool(const std::string &addon)> fn);
    void set_log(std::function<void(const std::string &)> fn);
    void set_context(const Context &ctx);
    Context context() const;
    void end_register();
    bool registering() const;
    void begin_career_load();
    void end_career_load();
    void set_flow_thread(bool is_flow_thread_fn());

    int declare(const std::string &name, const Value &schema, bool core = false, const std::string &addon = {});
    int declare_def(const Def &def, const std::string &addon = {}, const Value *schema = nullptr);
    std::size_t pending(const std::string &registry = {}) const;
    bool declared(const std::string &name) const;
    int add_base(const std::string &registry, const std::string &key, const Value &value);
    int define_tag(const std::string &registry, const std::string &tag, const std::vector<std::string> &members);
    void stock_loaded(const std::string &registry, bool loaded);

    int add(const std::string &addon, const std::string &registry, const std::string &key, const Value &value,
            std::string *id, const std::string &source = {});
    int put_older(const std::string &addon, const std::string &registry, const std::string &key, const Value *value,
                  const std::string &source);
    std::vector<std::string> layer_targets(const std::string &registry);
    int variant(const std::string &addon, const std::string &registry, const std::string &key, const std::string &base,
                const Change &change, std::string *id, const std::string &source = {});
    int provide_fn(const std::string &addon, const std::string &registry, SwoftyProvideFn fn, SwoftyListFn list,
                   void *user);
    int invalidate(const std::string &registry, const std::string *id);
    int layer(const LayerSpec &spec, uint32_t *handle);
    int layer_in(const std::string &addon, uint32_t handle, const Value &selectors);
    int clear_layer(const std::string &addon, const std::string &registry, const std::string &id, Scope scope);
    int tag_add(const std::string &addon, const std::string &registry, const std::string &tag, const std::string &id);
    int protect(const std::string &addon, const std::string &registry, const std::string &id,
                const std::vector<std::string> &fields);
    int check(const std::string &addon, const std::string &registry, SwoftyCheckFn fn, void *user);
    int adapter(const std::string &addon, const std::string &registry, uint32_t from, uint32_t to, SwoftyConvertFn up,
                SwoftyConvertFn down, void *user);
    int migrate(const std::string &addon, const std::string &registry, uint32_t from, uint32_t to, SwoftyConvertFn fn,
                void *user);
    int watch(const std::string &registry, SwoftyRecordFn fn, void *user, uint32_t *token);
    int unwatch(uint32_t token);

    Record get(const std::string &registry_at, const std::string &id);
    Status status(const std::string &registry_at, const std::string &id);
    std::string origin(const std::string &registry, const std::string &id, const std::string &pointer);
    std::vector<std::string> list(const std::string &registry, const std::string &prefix);
    bool next(const std::string &registry, const std::string &prefix, const std::string *after, std::string &id);
    std::vector<std::string> tag_members(const std::string &registry, const std::string &tag);
    std::vector<std::string> chain(const std::string &registry, const std::string &id);
    std::vector<std::string> registries() const;
    std::vector<Refusal> refusals();
    std::vector<Conflict> conflicts();
    std::size_t layer_count(const std::string &addon, const std::string &registry) const;
    void set_op_index(const std::string &addon, int op);
    bool read_option(const std::string &addon, const std::string &id, Value &out);
    void note_refusal(const std::string &addon, const std::string &registry, const std::string &id,
                      const std::string &reason, const std::string &message);

    Value effective_report();
    Value conflicts_report();

    // W4 career snapshots: the read hook (set again after the frozen set changes, so cached values
    // are recomputed), the value a freeze stores (base and global and profile layers for ctx, with no
    // snapshot and no career layers), and a registry's declaration.
    void set_snapshot_source(SnapshotSource fn);
    Record get_live(const std::string &registry, const std::string &id, const Context &ctx);
    std::optional<Def> def(const std::string &registry) const;

    struct Impl;

private:
    Impl *d;
};

std::string &last_error();
void set_last_error(std::string why);

}
