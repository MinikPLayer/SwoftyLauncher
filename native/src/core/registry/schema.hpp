#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <swofty/json.hpp>

// The registry's schema check: the JSON Schema subset of packages/sdk/std/registries/<name>/<ver>.json
// (and of the schemas addons declare), compiled once and run on every edit's result and every data file.
// It follows tools/std/stdschema.py, the reference CI runs, keyword for keyword:
//
// - type, enum, const, properties, required, additionalProperties, items, minItems, maxItems,
//   uniqueItems, minimum, maximum, exclusiveMinimum, exclusiveMaximum, minLength, maxLength, pattern,
//   allOf, anyOf, oneOf, true/false schemas, and $ref to a local JSON Pointer ("#/$defs/SeasonTeam").
//   Annotations (title, description, default, ...) and the top-level x- metadata are read, never checked.
// - x-key marks a keyed list: each element is an object with its key fields set (not null), no two
//   elements share a key, and "$delete": true (only in patch mode) removes the element and holds only
//   its key fields.
// - x-ref marks a record id: "<namespace>/<key>", with "?" (optional) or "|" fallbacks when x-missing
//   allows them. With a resolver, a required id that resolves to nothing is an error too.
// - Patch mode checks a layer (a JSON Merge Patch): required fields may be left out, null removes a
//   field (but not a required one), and a keyed list's minItems and maxItems wait for the merged result.
// - pattern is a regular expression run by a linear-time matcher (no backtracking): literals, ".",
//   classes, \d \w \s and their negations, groups, "|", "*", "+", "?", "{m,n}", ^, $, \b, \A, \Z.
//   \w, \d and \b follow Python's Unicode classes for Latin, Greek, Cyrillic, Armenian, Hebrew, Arabic, kana,
//   CJK and Hangul (other scripts count as non-word); anything else (lookaround, backreferences, flags)
//   fails the compile.
//
// Errors carry a JSON Pointer ("/teams/0/slot", "/" for the value itself) and a message. Nothing throws.

namespace swofty_core::registry {

class Schema {
public:
    struct Error {
        std::string pointer;
        std::string message;
    };

    enum class Mode : std::uint8_t { records, patch };

    // registry is the x-ref value as the schema spells it ("driver.identity", or "team.identity@2"),
    // id one alternative of the reference without its modifiers. Return true when the record exists, or
    // when the caller can't tell yet.
    using RefResolver = std::function<bool(std::string_view registry, std::string_view id)>;

    struct Options {
        Mode mode = Mode::records;
        std::size_t max_errors = 64;
        // Name a keyed list's element by its key ("/teams/slot=1/seats/0") instead of its index.
        bool keyed_paths = false;
        // Deeper values are an error, not a stack overflow (only a recursive schema descends that far).
        int max_depth = 64;
        // Schema steps for one validate; a hostile schema (anyOf over anyOf) stops here with an error.
        std::size_t max_steps = 20'000'000;
        const RefResolver *resolve = nullptr;
    };

    struct Result {
        std::vector<Error> errors;
        bool truncated = false;
        bool ok() const noexcept { return errors.empty(); }
    };

    struct Version {
        int major = 0;
        int minor = 0;
    };

    // Compiles a schema document. On failure returns nothing and, with err, says where and why.
    static std::optional<Schema> compile(const swofty::json::Value &schema, Error *err = nullptr);
    static std::optional<Schema> compile(std::string_view text, Error *err = nullptr);

    Schema(Schema &&) noexcept;
    Schema &operator=(Schema &&) noexcept;
    ~Schema();

    Result validate(const swofty::json::Value &value, const Options &options) const;
    Result validate(const swofty::json::Value &value, Mode mode = Mode::records) const;
    bool valid(const swofty::json::Value &value, Mode mode = Mode::records) const;

    // Top-level metadata. registry() is empty and version() {0, 0} when the schema doesn't say.
    const std::string &registry() const noexcept;
    Version version() const noexcept;
    const std::string &applies() const noexcept;
    bool snapshot() const noexcept;
    std::int64_t snapshot_budget() const noexcept;
    const std::string &adapter() const noexcept;

    // The keyed lists, for swofty::json::merge_patch: "/teams" -> {"slot"}, "/teams/*/drivers" -> ...
    // They are found through properties and the items of keyed lists, as stdschema.merge_patch finds them.
    const swofty::json::Keys &keys() const noexcept;
    // The same as (path, fields) pairs.
    const std::vector<std::pair<std::string, std::vector<std::string>>> &keyed_lists() const noexcept;

    struct Impl;

private:
    Schema();
    std::unique_ptr<Impl> impl_;
};

// A data file (an addon's registry/*.json, or an example): {"<registry>[@<major>]": {"<key>": record}},
// or, for a layers file, {"<registry>": {"<namespace>/<key>": merge patch, "$select": [selector entries]}}.
// Mirrors validate_data in tools/std/validate.py, with two rules of 1.6:
//
// - A record's key is the addon's own and may hold "/" ("2017/game/lewis_hamilton"), but no whitespace,
//   '?', '|' or '*', no leading '/' or '$', and no empty part (stdschema.valid_key).
// - A record with "extends" is a variant (validate.py's validate_variant): "extends" must be a full
//   record id, the rest is checked in patch mode, and, when DataOptions::base finds the base, the
//   effective value (the base's own effective value, through variants of variants, with this record
//   merged on by swofty::json::merge_patch with keys()) is checked as a record too. A merge that fails
//   (an element without its key fields) skips that check, as the patch check already names it. A loop of
//   bases is a problem at /extends.
struct DataSchemas {
    // The schemas to check a registry's records against (every minor of a major in CI; the core's one).
    std::vector<std::pair<std::string, const Schema *>> versions;
    // Why there are none, when versions is empty. Empty too means the registry is unknown, which
    // DataOptions::skip_unknown skips.
    std::string missing;
};

// A variant's base by (registry, the base's full id): the record as written, which may extend another
// (the check follows the chain), or null when it isn't known. The value only needs to live through the
// call.
using BaseResolver = std::function<const swofty::json::Value *(std::string_view registry, std::string_view id)>;

struct DataOptions {
    bool layers = false;
    // Only this registry may appear (an example's folder).
    std::string only;
    // Registries the lookup has no schema for are skipped rather than reported.
    bool skip_unknown = false;
    std::size_t max_errors = 64;
    const Schema::RefResolver *resolve = nullptr;
    const BaseResolver *base = nullptr;
};

struct DataReport {
    std::vector<Schema::Error> problems;
    std::vector<Schema::Error> missing;
    std::vector<std::pair<std::string, std::vector<Schema::Error>>> by_version;
    bool truncated = false;
    bool ok() const noexcept;
    std::vector<Schema::Error> *errors_of(std::string_view version);
    void touch(std::string_view version);
};

// lookup(name, major) gives the schemas for a registry; major is 0 when the key names none.
using DataLookup = std::function<DataSchemas(std::string_view name, int major)>;

DataReport check_data(const swofty::json::Value &doc, const DataLookup &lookup, const DataOptions &options = {});

// One record of registry (or, with options.layers, one layer) against the given schemas, "$protect"
// stripped first.
DataReport check_record(const swofty::json::Value &record, std::string_view registry, const DataSchemas &versions,
                        const DataOptions &options = {});

// A record key a data file may use (see check_data).
bool is_record_key(std::string_view key);

// base + rel, where rel is a pointer inside the value at base ("/" is that value itself).
std::string pointer_join(std::string_view base, std::string_view rel);

// A record id as data files write a reference: "<namespace>/<key>" (stdschema.RECORD_ID).
bool is_record_id(std::string_view id);

} // namespace swofty_core::registry
