// Host unit test for the core's schema check (native/src/core/registry/schema.{hpp,cpp}): each keyword,
// patch mode, keyed lists and "$delete", x-ref with a fake resolver, the pattern matcher, data files with
// "$select", "$protect", keys with "/" and "extends" variants, the error cap, hostile schemas and values,
// and a timed run over a 1 MB record set. run_schema_test.sh builds and runs it.
#include "../../src/core/registry/schema.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <random>
#include <string>
#include <vector>

using swofty_core::registry::check_data;
using swofty_core::registry::check_record;
using swofty_core::registry::DataOptions;
using swofty_core::registry::DataSchemas;
using swofty_core::registry::Schema;
using Value = swofty::json::Value;
using Mode = Schema::Mode;

static int fails, checks;

static void expect(bool ok, const std::string &what)
{
    ++checks;
    if (!ok) {
        printf("FAIL: %s\n", what.c_str());
        ++fails;
    }
}

static Value P(std::string_view text)
{
    swofty::json::Error e;
    Value v = swofty::json::parse(text, &e);
    if (!e.ok()) {
        printf("FAIL: test input does not parse: %.*s (%s)\n", int(text.size()), text.data(), e.message.c_str());
        ++fails;
    }
    return v;
}

static Schema S(std::string_view text)
{
    Schema::Error err;
    auto s = Schema::compile(text, &err);
    if (!s) {
        printf("FAIL: schema does not compile: %.*s (%s: %s)\n", int(text.size()), text.data(), err.pointer.c_str(),
               err.message.c_str());
        ++fails;
        return std::move(*Schema::compile(std::string_view("{}")));
    }
    return std::move(*s);
}

static std::string join(const std::vector<std::string> &v)
{
    std::string out;
    for (auto &s : v)
        out += (out.empty() ? "" : " ") + s;
    return "[" + out + "]";
}

static std::vector<std::string> pointers(const Schema::Result &r)
{
    std::vector<std::string> out;
    for (auto &e : r.errors)
        out.push_back(e.pointer);
    return out;
}

static void errs(const Schema &s, std::string_view value, std::vector<std::string> want, Mode mode = Mode::records,
                 const Schema::Options *opts = nullptr)
{
    Schema::Options o = opts ? *opts : Schema::Options{};
    o.mode = mode;
    auto r = s.validate(P(value), o);
    auto got = pointers(r);
    std::string what = std::string(value) + (mode == Mode::patch ? " (patch)" : "") + ": want " + join(want) + ", got " + join(got);
    if (got != want && !r.errors.empty())
        what += " (first: " + r.errors[0].message + ")";
    expect(got == want, what);
    expect(s.valid(P(value), mode) == want.empty(), std::string(value) + ": valid() agrees with validate()");
}

static void ok(const Schema &s, std::string_view value, Mode mode = Mode::records)
{
    errs(s, value, {}, mode);
}

static void bad(const Schema &s, std::string_view value, Mode mode = Mode::records)
{
    errs(s, value, {"/"}, mode);
}

static void no_compile(std::string_view schema, std::string_view why)
{
    Schema::Error err;
    auto s = Schema::compile(schema, &err);
    expect(!s && !err.message.empty(), std::string("refuses ") + std::string(schema) + " (" + std::string(why) + ")");
}

static bool matches(std::string_view pattern, std::string_view text)
{
    std::string schema = "{\"type\":\"string\",\"pattern\":" + swofty::json::dump(Value(pattern)) + "}";
    Schema::Error err;
    auto s = Schema::compile(std::string_view(schema), &err);
    if (!s) {
        printf("FAIL: pattern %.*s does not compile: %s\n", int(pattern.size()), pattern.data(), err.message.c_str());
        ++fails;
        return false;
    }
    return s->valid(Value(text));
}

static void types()
{
    auto i = S(R"({"type":"integer"})");
    ok(i, "2");
    ok(i, "2.0");
    ok(i, "-0.0");
    bad(i, "2.5");
    bad(i, "\"2\"");
    bad(i, "true");
    bad(i, "null");
    auto n = S(R"({"type":"number"})");
    ok(n, "2");
    ok(n, "2.5");
    bad(n, "false");
    auto sn = S(R"({"type":["string","null"]})");
    ok(sn, "\"x\"");
    ok(sn, "null");
    bad(sn, "1");
    auto o = S(R"({"type":"object"})");
    ok(o, "{}");
    bad(o, "[]");
    auto a = S(R"({"type":"array"})");
    ok(a, "[]");
    bad(a, "{}");
    auto b = S(R"({"type":"boolean"})");
    ok(b, "false");
    bad(b, "0");
    ok(S("true"), "[1,{}]");
    bad(S("false"), "1");
    ok(S("{}"), "\"anything\"");
}

static void values()
{
    auto e = S(R"({"enum":[1,"a",null,{"x":1,"y":[true]}]})");
    ok(e, "1.0");
    ok(e, "\"a\"");
    ok(e, "null");
    ok(e, R"({"y":[true],"x":1})");
    bad(e, "true");
    bad(e, R"({"x":1})");
    bad(e, "2");
    auto t = S(R"({"enum":[true]})");
    bad(t, "1");
    auto c = S(R"({"const":[1,2]})");
    ok(c, "[1.0,2]");
    bad(c, "[2,1]");
    auto range = S(R"({"minimum":1,"maximum":2.5})");
    ok(range, "1");
    ok(range, "2.5");
    bad(range, "0.999");
    bad(range, "3");
    ok(range, "\"not a number\"");
    auto x = S(R"({"exclusiveMinimum":0,"exclusiveMaximum":10})");
    bad(x, "0");
    bad(x, "10.0");
    ok(x, "0.0001");
    auto big = S(R"({"maximum":9007199254740993})");
    ok(big, "9007199254740993");
    bad(big, "9007199254740994");
    auto len = S(R"({"minLength":2,"maxLength":3})");
    ok(len, "\"éé\"");
    bad(len, "\"é\"");
    bad(len, "\"abcd\"");
    ok(len, "5");
}

static void patterns()
{
    expect(matches("^[a-z0-9_]+$", "f1_career_progressive"), "lower snake");
    expect(!matches("^[a-z0-9_]+$", "F1 Career"), "lower snake refuses spaces");
    expect(matches("^[a-z0-9_]+$", "abc\n"), "$ matches before a final newline, as Python does");
    expect(!matches("^[a-z0-9_]+$", "abc\n\n"), "but not before two");
    expect(matches("^#([0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})$", "#a0B1c2"), "colour 6");
    expect(matches("^#([0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})$", "#a0B1c2FF"), "colour 8");
    expect(!matches("^#([0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})$", "#a0B1c2F"), "colour 7");
    expect(matches("^[0-9]{4}-[0-9]{2}-[0-9]{2}$", "2017-03-26"), "date");
    expect(!matches("^[0-9]{4}-[0-9]{2}-[0-9]{2}$", "2017-3-26"), "short date");
    expect(matches("^eaid://", "eaid://abc"), "prefix");
    expect(!matches("^eaid://", "x eaid://abc"), "prefix anchored");
    expect(matches("^[^/\\\\][^\\\\]*$", "sounds/v10.bdl"), "relative path");
    expect(!matches("^[^/\\\\][^\\\\]*$", "/abs"), "absolute path");
    expect(!matches("^[^/\\\\][^\\\\]*$", "a\\b"), "backslash");
    expect(matches("b", "abc"), "search is unanchored");
    expect(matches("", "x"), "empty pattern");
    expect(matches("\\d+\\.\\d", "v1.5"), "digit escapes");
    expect(!matches("^\\w+$", "a-b"), "word class");
    expect(matches("^\\w+$", "P\u00e9rez") && !matches("^\\W$", "\u00e9"), "word class takes accented letters, as Python does");
    expect(matches("^\\w+$", "a_b9"), "word class 2");
    expect(matches("a\\sb", "a\tb"), "space class");
    expect(matches("\\bcat\\b", "a cat."), "word boundary");
    expect(!matches("\\bcat\\b", "concat"), "word boundary 2");
    expect(matches("^(?:ab)+$", "ababab"), "non-capturing group");
    expect(matches("^(?P<x>a|b)c$", "bc"), "named group");
    expect(matches("^a{2,}$", "aaaa"), "open repeat");
    expect(!matches("^a{2,3}$", "aaaa"), "bounded repeat");
    expect(matches("^a{,2}$", ""), "repeat with no minimum");
    expect(matches("^a+?b$", "aaab"), "lazy repeat");
    expect(matches("a{", "a{"), "a brace that isn't a repeat is a literal");
    expect(matches("^{a}$", "{a}"), "braces around a letter are literals");
    expect(matches("^[]a]+$", "]a]"), "] first in a class");
    expect(matches("^[a-]+$", "-a-"), "- last in a class");
    expect(matches("^[\\w.-]+$", "a.b-c"), "escape in a class");
    expect(matches("^\\x41\\u00e9$", "Aé"), "hex escapes");
    expect(matches("^.$", "é"), "a code point is one character");
    expect(!matches("^.$", "\n"), ". skips a newline");
    expect(matches("\\Aab\\Z", "ab"), "\\A and \\Z");
    expect(!matches("ab\\Z", "ab\n"), "\\Z is the very end");
    std::string long_a(100000, 'a');
    auto t0 = std::chrono::steady_clock::now();
    expect(!matches("^(a*)*b$", long_a + "c"), "nested repeats on a long string");
    expect(!matches("^(a|aa)+$", long_a + "b"), "ambiguous alternation on a long string");
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    expect(ms < 5000, "the matcher is linear (" + std::to_string(ms) + " ms)");
    for (auto p : {"(?=a)", "(?<=a)b", "(a)\\1", "a**", "*a", "[a", "(a", "a)", "\\q", "a{2,1}", "a{5000}", "^*", "a*+",
                   "[z-a]", "[\\d-z]", "(?i)a"})
        no_compile(std::string("{\"pattern\":") + swofty::json::dump(Value(p)) + "}", p);
}

static void objects()
{
    auto s = S(R"({"type":"object","required":["a","b"],"properties":{"a":{"type":"integer"},"b":{"type":"string"}},
                   "additionalProperties":false})");
    ok(s, R"({"a":1,"b":"x"})");
    errs(s, R"({"a":1})", {"/b"});
    errs(s, R"({"a":"1","b":"x","c":1})", {"/a", "/c"});
    errs(s, R"({})", {"/a", "/b"});
    errs(s, R"({"a~/":1,"a":1,"b":""})", {"/a~0~1"});
    auto extra = S(R"({"additionalProperties":{"type":"integer"}})");
    ok(extra, R"({"x":1,"y":2})");
    errs(extra, R"({"x":1,"y":"2"})", {"/y"});
    auto items = S(R"({"type":"array","items":{"type":"integer"},"minItems":1,"maxItems":2})");
    ok(items, "[1]");
    errs(items, "[]", {"/"});
    errs(items, "[1,2,3]", {"/"});
    errs(items, "[1,\"x\"]", {"/1"});
    auto unique = S(R"({"uniqueItems":true})");
    errs(unique, "[1,2,1.0]", {"/2"});
    ok(unique, "[[1],[1.0]]");
    errs(unique, R"([{"a":1,"b":2},{"b":2,"a":1}])", {"/1"});
    ok(unique, "[true,1]");
}

static void combinators()
{
    auto all = S(R"({"allOf":[{"type":"integer"},{"minimum":3}]})");
    ok(all, "4");
    bad(all, "2");
    errs(all, "\"x\"", {"/"});
    auto any = S(R"({"anyOf":[{"type":"integer"},{"type":"string","minLength":2}]})");
    ok(any, "1");
    ok(any, "\"ab\"");
    bad(any, "\"a\"");
    auto one = S(R"({"oneOf":[{"type":"integer","minimum":0},{"const":"auto"}]})");
    ok(one, "3");
    ok(one, "\"auto\"");
    bad(one, "\"automatic\"");
    auto two = S(R"({"oneOf":[{"type":"number"},{"type":"integer"}]})");
    bad(two, "1");
    ok(two, "1.5");
    auto refs = S(R"({"$defs":{"T":{"type":"object","properties":{"n":{"type":"integer"},"kids":{"type":"array","items":{"$ref":"#/$defs/T"}}}}},
                      "allOf":[{"$ref":"#/$defs/T"}]})");
    ok(refs, R"({"n":1,"kids":[{"n":2,"kids":[{"n":3}]}]})");
    errs(refs, R"({"n":1,"kids":[{"n":2,"kids":[{"n":"3"}]}]})", {"/kids/0/kids/0/n"});
    auto ann = S(R"({"$defs":{"I":{"type":"integer"}},"properties":{"a":{"$ref":"#/$defs/I","description":"d","default":1}}})");
    errs(ann, R"({"a":"x"})", {"/a"});
    auto esc = S(R"({"$defs":{"a/b":{"type":"integer"}},"properties":{"a":{"$ref":"#/$defs/a~1b"}}})");
    errs(esc, R"({"a":"x"})", {"/a"});
    no_compile(R"({"$defs":{"A":{"$ref":"#/$defs/B"},"B":{"$ref":"#/$defs/A"}},"$ref":"#/$defs/A"})", "a $ref loop");
    no_compile(R"({"properties":{"a":{"$ref":"#/$defs/I","type":"string"}},"$defs":{"I":{}}})", "a $ref sibling");
    no_compile(R"({"properties":{"a":{"$ref":"other.json#/x"}}})", "a non-local $ref");
    no_compile(R"({"properties":{"a":{"$ref":"#/$defs/nope"}}})", "an unresolved $ref");
    no_compile(R"({"type":"integr"})", "an unknown type");
    no_compile(R"({"type":[]})", "no type");
    no_compile(R"({"minimum":"1"})", "a string bound");
    no_compile(R"({"maxItems":-1})", "a negative count");
    no_compile(R"({"anyOf":[]})", "an empty anyOf");
    no_compile(R"({"items":[{}]})", "tuple items");
    no_compile(R"({"x-key":[]})", "an empty x-key");
    no_compile(R"([1])", "a schema that is a list");
    no_compile(R"({"$defs":{"I":{}},"$ref":"#/$defs/I"})", "a root $ref next to $defs, as stdschema refuses it");
}

static const char *season = R"({
  "x-registry": "career.season", "x-version": "1.2", "x-applies": "read", "x-snapshot": true, "x-snapshot-budget": 65536,
  "type": "object", "required": ["year", "teams"],
  "properties": {
    "year": {"type": "integer"},
    "label": {"type": "string"},
    "teams": {"type": "array", "x-key": "slot", "minItems": 2, "items": {"$ref": "#/$defs/Team"}},
    "changes": {"type": "array", "x-key": ["round", "slot"], "items": {"type": "object", "required": ["round", "slot", "kind"],
      "properties": {"round": {"type": "integer"}, "slot": {"type": "integer"}, "kind": {"enum": ["swap"]}}}},
    "tags": {"type": "array", "items": {"type": "object", "required": ["name"], "properties": {"name": {"type": "string"}}}}
  },
  "$defs": {"Team": {"type": "object", "required": ["slot", "seats"], "properties": {
    "slot": {"type": "integer", "minimum": 1, "maximum": 11},
    "team": {"type": ["string", "null"], "x-ref": "team.identity", "x-missing": {"default": "required", "allow": ["required", "optional"]}},
    "seats": {"type": "array", "maxItems": 2, "items": {"type": "string", "x-ref": "driver.identity"}},
    "drivers": {"type": "array", "x-key": "name", "items": {"type": "object", "required": ["name"], "properties": {"name": {"type": "string"}}}}
  }}}
})";

static void keyed()
{
    auto s = S(season);
    ok(s, R"({"year":2017,"teams":[{"slot":1,"seats":[]},{"slot":2,"seats":["game/a"]}]})");
    errs(s, R"({"year":2017,"teams":[{"slot":1,"seats":[]}]})", {"/teams"});
    errs(s, R"({"year":2017,"teams":[1,{"seats":[]},{"slot":null,"seats":[]},{"slot":1,"seats":[]},{"slot":1.0,"seats":[]}]})",
         {"/teams/0", "/teams/1", "/teams/2", "/teams/4"});
    errs(s, R"({"year":2017,"teams":[{"slot":1,"seats":[],"$delete":true},{"slot":2,"seats":[]}]})", {"/teams/0/$delete"});
    errs(s, R"({"year":2017,"teams":[{"slot":1,"seats":[]},{"slot":2,"seats":[]}],"changes":[{"round":1,"slot":2,"kind":"swap"},{"round":1,"slot":2,"kind":"swap"},{"round":2,"slot":2,"kind":"swap"}]})",
         {"/changes/1"});
    ok(s, R"({"teams":[{"slot":1,"$delete":true}],"changes":[{"round":1,"slot":2,"$delete":true}]})", Mode::patch);
    errs(s, R"({"teams":[{"slot":1,"$delete":1}]})", {"/teams/0/$delete"}, Mode::patch);
    errs(s, R"({"teams":[{"slot":1,"seats":[],"$delete":true}]})", {"/teams/0"}, Mode::patch);
    errs(s, R"({"teams":[{"slot":3,"team":5}]})", {"/teams/0/team"}, Mode::patch);
    ok(s, R"({"teams":[{"slot":3,"team":null}]})", Mode::patch);
    errs(s, R"({"teams":[{"slot":3,"seats":null}]})", {"/teams/0/seats"}, Mode::patch);
    errs(s, R"({"year":null,"label":null})", {"/year"}, Mode::patch);
    errs(s, R"({"tags":[{}]})", {"/tags/0/name"}, Mode::patch);
    Schema::Options keyed_paths;
    keyed_paths.keyed_paths = true;
    errs(s, R"({"year":2017,"teams":[{"slot":1,"seats":[]},{"slot":4,"seats":["a","game/b","game/c"],"drivers":[{"name":"x"},{"name":"x"}]}]})",
         {"/teams/slot=4/seats", "/teams/slot=4/seats/0", "/teams/slot=4/drivers/1"}, Mode::records, &keyed_paths);
    std::map<std::string, std::string> keys;
    for (auto &[p, f] : s.keyed_lists()) {
        std::string fields;
        for (auto &x : f)
            fields += (fields.empty() ? "" : ",") + x;
        keys[p] = fields;
    }
    expect(keys == std::map<std::string, std::string>{{"/teams", "slot"}, {"/teams/*/drivers", "name"}, {"/changes", "round,slot"}},
           "keyed lists of the schema");
    expect(s.keys().find("/teams") && s.keys().find("/teams/*/drivers") && !s.keys().find("/tags"), "merge keys");
    expect(s.registry() == "career.season" && s.version().major == 1 && s.version().minor == 2 && s.applies() == "read" &&
               s.snapshot() && s.snapshot_budget() == 65536 && s.adapter().empty(),
           "top-level metadata");
    Value target = P(R"({"year":2017,"teams":[{"slot":1,"seats":["game/a"]},{"slot":2,"seats":[]}]})");
    expect(swofty::json::merge_patch(target, P(R"({"teams":[{"slot":2,"seats":["game/b"]}]})"), s.keys()) &&
               swofty::json::dump(target) == R"({"year":2017,"teams":[{"slot":1,"seats":["game/a"]},{"slot":2,"seats":["game/b"]}]})",
           "keys() drives a keyed merge");
}

static void refs()
{
    auto s = S(season);
    std::string base = R"({"year":2017,"teams":[{"slot":2,"seats":[]},)";
    errs(s, base + R"({"slot":1,"team":5,"seats":["lewis",7,"game/a?","game/a|game/b"]}]})",
         {"/teams/1/team", "/teams/1/seats", "/teams/1/seats/0", "/teams/1/seats/1"});
    errs(s, base + R"({"slot":1,"team":"game/a|game/b","seats":[]}]})", {"/teams/1/team"});
    ok(s, base + R"({"slot":1,"team":"game/a?","seats":["game/b"]}]})");
    ok(s, base + R"({"slot":1,"team":null,"seats":[]}]})");
    std::vector<std::string> asked;
    Schema::RefResolver resolver = [&](std::string_view registry, std::string_view id) {
        asked.push_back(std::string(registry) + " " + std::string(id));
        return id == "game/known" || id == "game/b";
    };
    Schema::Options o;
    o.resolve = &resolver;
    errs(s, base + R"({"slot":1,"team":"game/gone","seats":["game/known","game/gone2","game/gone|game/b"]}]})",
         {"/teams/1/team", "/teams/1/seats", "/teams/1/seats/1"}, Mode::records, &o);
    expect(asked.size() >= 4 && asked[0] == "team.identity game/gone" && asked[1] == "driver.identity game/known",
           "the resolver gets the registry and the id without modifiers");
    errs(s, base + R"({"slot":1,"team":"game/gone?","seats":["game/b"]}]})", {}, Mode::records, &o);
    auto lenient = S(R"({"properties":{"e":{"type":"string","x-ref":"engine_sound","x-missing":{"default":"fallback","to":"/donor"}}}})");
    errs(lenient, R"({"e":"game/gone"})", {}, Mode::records, &o);
    errs(lenient, R"({"e":"game/gone?"})", {"/e"}, Mode::records, &o);
    expect(swofty_core::registry::is_record_id("swofty.progressive/2017") && !swofty_core::registry::is_record_id("2017") &&
               !swofty_core::registry::is_record_id("Game/x") && !swofty_core::registry::is_record_id("game/a b") &&
               !swofty_core::registry::is_record_id("game/") && swofty_core::registry::is_record_id("game/2017/x"),
           "record ids");
}

static void limits()
{
    auto s = S(R"({"type":"array","items":{"type":"integer"}})");
    std::string many = "[";
    for (int i = 0; i < 1000; ++i)
        many += std::string(i ? "," : "") + "\"x\"";
    many += "]";
    auto r = s.validate(P(many));
    expect(r.errors.size() == 64 && r.truncated, "errors stop at 64 and say they were cut");
    Schema::Options none;
    none.max_errors = 0;
    auto r0 = s.validate(P(many), none);
    expect(!r0.ok(), "no room for errors is still not ok");

    auto tree = S(R"({"type":"object","additionalProperties":{"$ref":"#"}})");
    std::string deep;
    for (int i = 0; i < 200; ++i)
        deep += "{\"a\":";
    deep += "{}";
    for (int i = 0; i < 200; ++i)
        deep += "}";
    auto rd = tree.validate(P(deep));
    expect(!rd.ok() && rd.errors[0].message.find("nested deeper") != std::string::npos, "a deep value under a recursive schema stops");
    Value built = Value::object();
    Value *at = &built;
    for (int i = 0; i < 5000; ++i)
        at = &((*at)["a"] = Value::object());
    expect(!tree.valid(built), "a value built 5000 deep doesn't overflow the stack");

    auto loop = S(R"({"allOf":[{"$ref":"#"}]})");
    auto rl = loop.validate(P("1"));
    expect(!rl.ok() && rl.errors[0].message.find("recurses") != std::string::npos, "a schema that recurses on one value stops");

    std::string fan = R"({"$defs":{)";
    for (int i = 0; i < 40; ++i)
        fan += "\"d" + std::to_string(i) + "\":{\"anyOf\":[{\"$ref\":\"#/$defs/d" + std::to_string(i + 1) +
               "\"},{\"$ref\":\"#/$defs/d" + std::to_string(i + 1) + "\"}]},";
    fan += R"("d40":false},"allOf":[{"$ref":"#/$defs/d0"}]})";
    auto f = S(fan);
    Schema::Options small;
    small.max_steps = 100000;
    auto t0 = std::chrono::steady_clock::now();
    auto rf = f.validate(P("1"), small);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    expect(!rf.ok() && rf.errors.back().message.find("steps") != std::string::npos && ms < 2000,
           "an exponential schema stops at the step budget (" + std::to_string(ms) + " ms)");

    auto pat = S(R"({"type":"string","pattern":"^[a-z_]+$","maxLength":2000000})");
    std::string big(1 << 20, 'a');
    expect(pat.valid(Value(big)), "a 1 MB string through the pattern");
    swofty::json::Error perr;
    swofty::json::parse(std::string(300, '[') + std::string(300, ']'), &perr);
    expect(!perr.ok(), "the parser refuses values nested past its depth before the check sees them");
}

static DataSchemas lookup_of(const std::map<std::string, const Schema *> &by_name, std::string_view name)
{
    DataSchemas d;
    auto it = by_name.find(std::string(name));
    if (it != by_name.end())
        d.versions.emplace_back("1.0", it->second);
    return d;
}

static std::vector<std::string> report_pointers(const swofty_core::registry::DataReport &r)
{
    std::vector<std::string> out;
    for (auto &p : r.problems)
        out.push_back("problem " + p.pointer);
    for (auto &p : r.missing)
        out.push_back("missing " + p.pointer);
    for (auto &[v, errs] : r.by_version)
        for (auto &e : errs)
            out.push_back(v + " " + e.pointer);
    return out;
}

static void data_files()
{
    auto s = S(season);
    std::map<std::string, const Schema *> by_name{{"career.season", &s}};
    auto lookup = [&](std::string_view name, int) { return lookup_of(by_name, name); };
    auto check = [&](std::string_view doc, bool layers, std::vector<std::string> want, const DataOptions *opts = nullptr) {
        DataOptions o = opts ? *opts : DataOptions{};
        o.layers = layers;
        auto got = report_pointers(check_data(P(doc), lookup, o));
        expect(got == want, std::string(doc) + ": want " + join(want) + ", got " + join(got));
    };
    std::string team2 = R"({"slot":1,"seats":[]},{"slot":2,"seats":[]})";
    check(R"({"career.season":{"2017":{"year":2017,"teams":[)" + team2 + R"(]}}})", false, {});
    check(R"({"career.season":{"2017/game/lewis_hamilton":{"year":2017,"teams":[)" + team2 + R"(]}}})", false, {});
    check(R"({"career.season":{"a//b":{},"/a":{},"":{},"a b":{},"$x":{},"a/":{},"a?":{}}})", false,
          {"problem /career.season/a~1~1b", "problem /career.season/~1a", "problem /career.season/", "problem /career.season/a b",
           "problem /career.season/$x", "problem /career.season/a~1", "problem /career.season/a?"});
    check(R"({"career.season":{"x":{"year":"2017","teams":[)" + team2 + R"(]}}})", false, {"1.0 /career.season/x/year"});
    check(R"({"career.season":{"x":{"$protect":["teams"],"year":2017,"teams":[)" + team2 + R"(]}}})", false, {});
    check(R"({"career.season":{"x":{"$protect":"teams","year":2017,"teams":[)" + team2 + R"(]}}})", false,
          {"problem /career.season/x/$protect"});
    check(R"({"Career":{},"vehicle":{},"career.season@2":[]})", false, {"problem /Career", "problem /career.season@2", "missing /vehicle"});
    check(R"([])", false, {"problem /"});
    DataOptions skip;
    skip.skip_unknown = true;
    check(R"({"vehicle":{}})", false, {}, &skip);
    check(R"({"career.season":{"swofty.progressive/2017":{"year":null,"teams":[{"slot":1,"$delete":true}]},"2017":{},"game/x":[]}})", true,
          {"problem /career.season/2017", "problem /career.season/game~1x", "1.0 /career.season/swofty.progressive~12017/year"});
    check(R"({"career.season":{"$select":[{"ids":"*","patch":{"year":"x"}},{"where":{},"bogus":1,"patch":{}},{"in":{}},3]}})", true,
          {"problem /career.season/$select/1", "problem /career.season/$select/2", "problem /career.season/$select/2/patch",
           "problem /career.season/$select/3", "1.0 /career.season/$select/0/patch/year"});
    check(R"({"career.season":{"$select":[]}})", true, {"problem /career.season/$select"});

    check(R"({"career.season":{"v10":{"extends":"swofty.cs/f1_2017","label":"V10","teams":[{"slot":1,"$delete":true}]}}})", false, {});
    check(R"({"career.season":{"v10":{"extends":"swofty.cs/f1_2017","year":"x"}}})", false, {"1.0 /career.season/v10/year"});
    Value base_ok = P(R"({"year":2017,"teams":[{"slot":1,"seats":[]},{"slot":2,"seats":[]},{"slot":3,"seats":[]}]})");
    Value base_short = P(R"({"teams":[{"slot":1,"seats":[]},{"slot":2,"seats":[]}]})");
    std::vector<std::string> asked;
    swofty_core::registry::BaseResolver base = [&](std::string_view registry, std::string_view id) -> const Value * {
        asked.push_back(std::string(registry) + " " + std::string(id));
        return id == "swofty.cs/f1_2017" ? &base_ok : id == "swofty.cs/short" ? &base_short : nullptr;
    };
    DataOptions with_base;
    with_base.base = &base;
    check(R"({"career.season":{"v10":{"extends":"swofty.cs/f1_2017","teams":[{"slot":1,"$delete":true}]}}})", false, {}, &with_base);
    expect(asked.size() == 1 && asked[0] == "career.season swofty.cs/f1_2017", "the base resolver gets the registry and the base id");
    check(R"({"career.season":{"v10":{"extends":"swofty.cs/f1_2017","teams":[{"slot":1,"$delete":true},{"slot":2,"$delete":true}]}}})",
          false, {"1.0 /career.season/v10/teams"}, &with_base);
    check(R"({"career.season":{"v10":{"extends":"swofty.cs/short","label":"x"}}})", false, {"1.0 /career.season/v10/year"}, &with_base);
    check(R"({"career.season":{"v10":{"extends":"swofty.cs/unknown","label":"x"}}})", false, {}, &with_base);
    check(R"({"career.season":{"v10":{"extends":"v8","label":"x"},"v9":{"extends":null}}})", false,
          {"problem /career.season/v10/extends", "problem /career.season/v9/extends"}, &with_base);
    check(R"({"career.season":{"v10":{"extends":"swofty.cs/short","year":"x"}}})", false, {"1.0 /career.season/v10/year"}, &with_base);

    Value chain_a = P(R"({"extends":"swofty.cs/b","label":"a"})");
    Value chain_b = P(R"({"extends":"swofty.cs/a","label":"b"})");
    Value chain_c = P(R"({"extends":"swofty.cs/f1_2017","teams":[{"slot":2,"$delete":true}]})");
    swofty_core::registry::BaseResolver chains = [&](std::string_view, std::string_view id) -> const Value * {
        return id == "swofty.cs/a" ? &chain_a : id == "swofty.cs/b" ? &chain_b : id == "swofty.cs/c" ? &chain_c
               : id == "swofty.cs/f1_2017" ? &base_ok : nullptr;
    };
    DataOptions with_chains;
    with_chains.base = &chains;
    check(R"({"career.season":{"x":{"extends":"swofty.cs/a"}}})", false, {"problem /career.season/x/extends"}, &with_chains);
    check(R"({"career.season":{"x":{"extends":"swofty.cs/c","teams":[{"slot":3,"$delete":true}]}}})", false, {"1.0 /career.season/x/teams"},
          &with_chains);
    check(R"({"career.season":{"x":{"extends":"swofty.cs/c","teams":[{"slot":11,"seats":[]}]}}})", false, {}, &with_chains);

    auto merge = [&](std::string_view base, std::string_view patch) {
        Value t = P(base);
        swofty::json::Error e;
        return swofty::json::merge_patch(t, P(patch), s.keys(), &e) ? swofty::json::dump(t) : "error";
    };
    expect(merge(R"({"year":2017,"label":"a","teams":[{"slot":1,"seats":["game/a"],"team":"game/t"},{"slot":2,"seats":[]}]})",
                 R"({"label":null,"teams":[{"slot":1,"seats":["game/b"]},{"slot":2,"$delete":true},{"slot":5,"team":null,"seats":[]}]})") ==
               R"({"year":2017,"teams":[{"slot":1,"seats":["game/b"],"team":"game/t"},{"slot":5,"seats":[]}]})",
           "keys() merges keyed lists by key, removing $delete elements and nulls");
    expect(merge(R"({"teams":[{"slot":1,"seats":[]}]})", R"({"teams":[]})") == R"({"teams":[{"slot":1,"seats":[]}]})",
           "an empty keyed list changes nothing");
    expect(merge(R"({"teams":[{"slot":1,"seats":[]}]})", R"({"teams":[{"seats":[]}]})") == "error",
           "an element without its key fields fails the merge");
    check(R"({"career.season":{"x":{"extends":"swofty.cs/f1_2017","teams":[{"seats":[]}]}}})", false, {"1.0 /career.season/x/teams/0"},
          &with_base);

    auto one = check_record(P(R"({"$protect":["year"],"year":2017,"teams":[)" + team2 + "]}"), "career.season", lookup_of(by_name, "career.season"));
    expect(one.ok(), "check_record strips $protect");
    auto none = check_record(P("{}"), "career.season", DataSchemas{{}, "no schema"});
    expect(!none.ok() && none.missing.size() == 1, "check_record without schemas");
}

static void speed()
{
    auto s = S(season);
    Value doc = Value::object();
    Value &records = doc["career.season"] = Value::object();
    std::size_t bytes = 0;
    int n = 0;
    while (bytes < (1u << 20)) {
        Value r = Value::object();
        r["year"] = 1950 + n % 150;
        r["label"] = "season number " + std::to_string(n);
        Value teams = Value::array();
        for (int slot = 1; slot <= 11; ++slot) {
            Value t = Value::object();
            t["slot"] = slot;
            t["team"] = "game/team_" + std::to_string(slot);
            t["seats"] = Value::array({"game/driver_" + std::to_string(slot * 2), "game/driver_" + std::to_string(slot * 2 + 1)});
            t["drivers"] = Value::array({Value::object({{"name", "a"}}), Value::object({{"name", "b"}})});
            teams.push_back(std::move(t));
        }
        r["teams"] = std::move(teams);
        Value changes = Value::array();
        for (int k = 0; k < 4; ++k)
            changes.push_back(Value::object({{"round", k + 1}, {"slot", k + 2}, {"kind", "swap"}}));
        r["changes"] = std::move(changes);
        bytes += swofty::json::dump(r).size();
        records["s" + std::to_string(n++)] = std::move(r);
    }
    std::map<std::string, const Schema *> by_name{{"career.season", &s}};
    auto lookup = [&](std::string_view name, int) { return lookup_of(by_name, name); };
    Schema::RefResolver resolver = [](std::string_view, std::string_view id) { return id.size() > 5; };
    DataOptions o;
    o.resolve = &resolver;
    auto t0 = std::chrono::steady_clock::now();
    auto r = check_data(doc, lookup, o);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    expect(r.ok(), "the 1 MB record set is valid");
    printf("speed: validated %d records, %zu bytes of JSON, in %.1f ms (%.1f MB/s)\n", n, bytes, ms, bytes / 1048576.0 / (ms / 1000.0));
    expect(ms < 5000, "1 MB validates in under 5 s");
}

static void fuzz(int rounds)
{
    std::mt19937 rng(1606);
    const std::string alphabet = "ab()[]{}|*+?.^$\\-,:=<>!P0129dwsbBAZxu^ \n";
    auto pick = [&](const std::string &from, int n) {
        std::string out;
        for (int k = 0; k < n; ++k)
            out += from[rng() % from.size()];
        return out;
    };
    int compiled = 0;
    for (int i = 0; i < rounds; ++i) {
        std::string pattern = pick(alphabet, 1 + static_cast<int>(rng() % 12));
        std::string schema = "{\"pattern\":" + swofty::json::dump(Value(pattern)) + "}";
        auto s = Schema::compile(std::string_view(schema));
        if (!s)
            continue;
        ++compiled;
        for (int k = 0; k < 4; ++k)
            (void)s->valid(Value(pick("ab()-_ \n{}\xc3\xa9", static_cast<int>(rng() % 20))));
    }
    const char *kinds[] = {"{}", "true", "false", R"({"type":"integer"})", R"({"x-key":"k","items":{"$ref":"#"}})",
                           R"({"anyOf":[{"$ref":"#"},{"type":"null"}]})", R"({"additionalProperties":{"$ref":"#"},"required":["k"]})",
                           R"({"items":{"$ref":"#"},"uniqueItems":true,"maxItems":3})", R"({"x-ref":"r","x-missing":{"allow":["optional"]}})"};
    const char *values[] = {"null", "1", "1.5", "\"game/a?\"", "[]", "{}", R"([{"k":1},{"k":1.0,"$delete":true}])",
                            R"({"k":{"k":[null]}})", R"([[[]],[[]]])", R"({"$delete":true,"k":"x|y"})"};
    int combos = 0;
    for (auto *k : kinds)
        for (auto *v : values)
            for (auto mode : {Mode::records, Mode::patch}) {
                auto s = Schema::compile(std::string_view(k));
                if (!s)
                    continue;
                Schema::Options o;
                o.mode = mode;
                o.keyed_paths = (combos & 1) != 0;
                auto r = s->validate(P(v), o);
                expect(r.ok() == s->valid(P(v), mode), std::string(k) + " on " + v + ": valid() agrees with validate()");
                ++combos;
            }
    printf("fuzz: %d patterns, %d compiled; %d schema and value pairs\n", rounds, compiled, combos);
}

int main(int argc, char **argv)
{
    int rounds = argc > 1 ? std::atoi(argv[1]) : 20000;
    types();
    values();
    patterns();
    objects();
    combinators();
    keyed();
    refs();
    limits();
    data_files();
    speed();
    fuzz(rounds);
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
