// Host unit test for swofty::json (packages/sdk/include/swofty/json.hpp): the JSONTestSuite parse cases
// in suite/ (y_ must parse and round-trip, n_ must be refused, i_ must not crash), the RFC 7396 merge
// patch vectors, RFC 6901 pointers and the keyed form, keyed-list merges with "$delete", number edge
// cases, and a fuzz loop over parse, dump, pointers and merge patches. run_json_test.sh builds and runs it.
#include <swofty/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

namespace json = swofty::json;
using json::Value;

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
    json::Error e;
    Value v = json::parse(text, &e);
    if (!e.ok()) {
        printf("FAIL: test input does not parse: %.*s (%s)\n", int(text.size()), text.data(), e.message.c_str());
        ++fails;
    }
    return v;
}

static std::string slurp(const std::filesystem::path &p)
{
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

static bool round_trips(const Value &v)
{
    for (int indent : {-1, 0, 2, 4}) {
        auto text = json::dump(v, indent);
        json::Error e;
        Value back = json::parse(text, &e);
        if (!e.ok() || !(back == v) || json::dump(back, indent) != text)
            return false;
        if (back.type() != v.type())
            return false;
    }
    return true;
}

static void suite(const std::filesystem::path &dir)
{
    int y = 0, n = 0, i = 0, i_accepted = 0;
    std::vector<std::filesystem::path> files;
    for (auto &entry : std::filesystem::directory_iterator(dir))
        if (entry.path().extension() == ".json")
            files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    for (auto &path : files) {
        auto name = path.filename().string();
        auto text = slurp(path);
        json::Error e;
        Value v = json::parse(text, &e);
        if (name.starts_with("y_")) {
            ++y;
            expect(e.ok(), "suite " + name + " parses (" + e.message + ")");
            expect(round_trips(v), "suite " + name + " round-trips");
        } else if (name.starts_with("n_")) {
            ++n;
            expect(!e.ok(), "suite " + name + " is refused");
            expect(v.is_null(), "suite " + name + " gives null when refused");
        } else if (name.starts_with("i_")) {
            ++i;
            if (e.ok()) {
                ++i_accepted;
                expect(round_trips(v), "suite " + name + " round-trips when accepted");
            }
        }
    }
    json::Error e;
    json::parse(std::string(100000, '['), &e);
    expect(!e.ok(), "suite n_structure_100000_opening_arrays is refused");
    std::string open;
    for (int k = 0; k < 50000; ++k)
        open += "[{\"\":";
    open += "\n";
    json::parse(open, &e);
    expect(!e.ok(), "suite n_structure_open_array_object is refused");
    printf("suite: %d y_, %d n_ (+2 generated), %d i_ (%d accepted)\n", y, n, i, i_accepted);
    expect(y == 95 && n == 186 && i == 35, "suite has every y_, n_ and i_ case");
}

static void rfc7396()
{
    struct Case {
        const char *target, *patch, *result;
    };
    const Case cases[] = {
        {R"({"a":"b"})", R"({"a":"c"})", R"({"a":"c"})"},
        {R"({"a":"b"})", R"({"b":"c"})", R"({"a":"b","b":"c"})"},
        {R"({"a":"b"})", R"({"a":null})", R"({})"},
        {R"({"a":"b","b":"c"})", R"({"a":null})", R"({"b":"c"})"},
        {R"({"a":["b"]})", R"({"a":"c"})", R"({"a":"c"})"},
        {R"({"a":"c"})", R"({"a":["b"]})", R"({"a":["b"]})"},
        {R"({"a":{"b":"c"}})", R"({"a":{"b":"d","c":null}})", R"({"a":{"b":"d"}})"},
        {R"({"a":[{"b":"c"}]})", R"({"a":[1]})", R"({"a":[1]})"},
        {R"(["a","b"])", R"(["c","d"])", R"(["c","d"])"},
        {R"({"a":"b"})", R"(["c"])", R"(["c"])"},
        {R"({"a":"foo"})", R"(null)", R"(null)"},
        {R"({"a":"foo"})", R"("bar")", R"("bar")"},
        {R"({"e":null})", R"({"a":1})", R"({"e":null,"a":1})"},
        {R"([1,2])", R"({"a":"b","c":null})", R"({"a":"b"})"},
        {R"({})", R"({"a":{"bb":{"ccc":null}}})", R"({"a":{"bb":{}}})"},
    };
    int k = 0;
    for (auto &c : cases) {
        Value t = P(c.target);
        json::Error e;
        bool ok = json::merge_patch(t, P(c.patch), {}, &e);
        expect(ok && e.ok() && json::dump(t) == c.result,
               "rfc7396 appendix A #" + std::to_string(++k) + ": " + json::dump(t) + " == " + c.result);
    }
    Value doc = P(R"({
      "title": "Goodbye!",
      "author" : { "givenName" : "John", "familyName" : "Doe" },
      "tags":[ "example", "sample" ],
      "content": "This will be unchanged"
    })");
    Value patch = P(R"({
      "title": "Hello!",
      "phoneNumber": "+01-123-456-7890",
      "author": { "familyName": null },
      "tags": [ "example" ]
    })");
    Value out = json::merged(doc, patch);
    expect(json::dump(out) == R"({"title":"Hello!","author":{"givenName":"John"},"tags":["example"],)"
                               R"("content":"This will be unchanged","phoneNumber":"+01-123-456-7890"})",
           "rfc7396 section 3 example, members in order: " + json::dump(out));
    expect(json::dump(doc["author"]) == R"({"givenName":"John","familyName":"Doe"})", "merged leaves its target alone");
}

static void pointers()
{
    Value doc = P(R"({"foo":["bar","baz"],"":0,"a/b":1,"c%d":2,"e^f":3,"g|h":4,"i\\j":5,"k\"l":6," ":7,"m~n":8})");
    auto at = [&](const char *p) { return doc.get(p); };
    expect(at("") == &doc, "rfc6901: \"\" is the whole document");
    expect(at("/foo") && json::dump(*at("/foo")) == R"(["bar","baz"])", "rfc6901: /foo");
    expect(at("/foo/0") && *at("/foo/0") == Value("bar"), "rfc6901: /foo/0");
    expect(at("/") && *at("/") == Value(0), "rfc6901: /");
    expect(at("/a~1b") && *at("/a~1b") == Value(1), "rfc6901: /a~1b");
    expect(at("/c%d") && *at("/c%d") == Value(2), "rfc6901: /c%d");
    expect(at("/e^f") && *at("/e^f") == Value(3), "rfc6901: /e^f");
    expect(at("/g|h") && *at("/g|h") == Value(4), "rfc6901: /g|h");
    expect(at("/i\\j") && *at("/i\\j") == Value(5), "rfc6901: /i\\j");
    expect(at("/k\"l") && *at("/k\"l") == Value(6), "rfc6901: /k\"l");
    expect(at("/ ") && *at("/ ") == Value(7), "rfc6901: / (space)");
    expect(at("/m~0n") && *at("/m~0n") == Value(8), "rfc6901: /m~0n");
    expect(at("foo/1") && *at("foo/1") == Value("baz"), "the leading slash is optional");
    expect(!at("/foo/2") && !at("/foo/-") && !at("/foo/01") && !at("/foo/x") && !at("/nope") && !at("/foo/0/x"),
           "missing places give null");
    expect(!at("/m~2n") && !at("/m~") && !at("/a~1b~"), "a bad escape gives null");
    expect(!at("/foo/-1") && !at("/foo/99999999999999999999999"), "a negative or huge index gives null");

    Value season = P(R"({
      "year": 2017,
      "teams": [
        { "slot": 1, "team": "game/mercedes", "seats": ["game/lewis_hamilton", "game/valtteri_bottas"] },
        { "slot": 4, "team": "game/mclaren", "seats": ["game/fernando_alonso", "game/stoffel_vandoorne"] },
        { "slot": "x,y", "team": "odd" },
        { "slot": "7", "team": "string seven" }
      ],
      "changes": [ { "round": 15, "slot": 8, "seat": 1, "out": "game/daniil_kvyat" } ]
    })");
    auto s = [&](const char *p) { return season.get(p); };
    expect(s("teams/slot=1/seats/0") && *s("teams/slot=1/seats/0") == Value("game/lewis_hamilton"),
           "keyed: teams/slot=1/seats/0");
    expect(s("/teams/slot=4/team") && *s("/teams/slot=4/team") == Value("game/mclaren"), "keyed: /teams/slot=4/team");
    expect(s("/changes/round=15,slot=8,seat=1/out") && *s("/changes/round=15,slot=8,seat=1/out") == Value("game/daniil_kvyat"),
           "keyed with three fields");
    expect(!s("/changes/round=15,slot=8,seat=2") && s("/changes/round=15,slot=8") == s("/changes/0"),
           "a key needs every field it names to match, and fewer fields match more loosely");
    expect(!s("/teams/slot=2") && !s("/teams/team=nope"), "a key that matches nothing gives null");
    expect(s("/teams/slot=\"x,y\"/team") && *s("/teams/slot=\"x,y\"/team") == Value("odd"), "a quoted key value");
    expect(s("/teams/slot=7/team") && *s("/teams/slot=7/team") == Value("string seven"),
           "a bare key value matches a string field");
    expect(s("/teams/slot=\"7\"/team") && *s("/teams/slot=\"7\"/team") == Value("string seven"),
           "a quoted key value matches only strings");
    expect(!s("/teams/slot=\"1\""), "a quoted key value doesn't match a number");
    expect(s("/teams/slot=1.0/team") && *s("/teams/slot=1.0/team") == Value("game/mercedes"),
           "a bare key value matches a number by value");
    expect(!s("/teams/=1") && !s("/teams/slot=\"1") && !s("/teams/slot=1,") && !s("/teams/slot=1,,x=2"),
           "malformed keyed segments give null");
    expect(json::key_segment(*s("/teams/slot=1"), {"slot"}) == "slot=1", "key_segment of a number key");
    expect(json::key_segment(*s("/changes/0"), {"round", "slot", "seat"}) == "round=15,slot=8,seat=1",
           "key_segment of a three-field key");
    expect(json::key_segment(*s("/teams/2"), {"slot"}) == "slot=\"x,y\"", "key_segment quotes a value with a comma");
    expect(json::key_segment(*s("/teams/3"), {"slot"}) == "slot=\"7\"", "key_segment quotes a string that looks like a number");
    expect(json::key_segment(P(R"({"id":"a/b~c"})"), {"id"}) == "id=a~1b~0c", "key_segment escapes / and ~");
    expect(season.get("/teams/" + json::key_segment(P(R"({"slot":"x,y"})"), {"slot"})) == s("/teams/2"),
           "a key_segment finds its element again");
    expect(json::key_segment(P(R"({"slot":[1]})"), {"slot"}).empty() && json::key_segment(P("{}"), {"slot"}).empty(),
           "key_segment is empty without a scalar key");

    Value w = season;
    expect(w.set("/teams/slot=1/seats/0", "game/fernando_alonso") &&
               *w.get("teams/slot=1/seats/0") == Value("game/fernando_alonso"),
           "set through a keyed segment");
    bool appended = w.set("/teams/slot=11/team", "game/manor");
    expect(appended && json::dump(*w.get("/teams/4")) == R"({"slot":11,"team":"game/manor"})",
           "set appends a missing keyed element with its key");
    expect(w.set("/changes/round=3,slot=2,seat=0", P(R"({"out":"a"})")) &&
               json::dump(*w.get("/changes/1")) == R"({"round":3,"slot":2,"seat":0,"out":"a"})",
           "set of a whole keyed element adds its key fields first");
    expect(w.set("/teams/slot=\"q\"/team", "quoted") && *w.get("/teams/5/slot") == Value("q"),
           "a quoted key makes a string key field");
    expect(w.set("/teams/slot=1/seats/-", "game/third") && w.get("/teams/slot=1/seats")->size() == 3,
           "- appends to an array");
    expect(w.set("/teams/slot=1/seats/3", "game/fourth") && w.get("/teams/slot=1/seats")->size() == 4,
           "the index equal to the size appends");
    Value before = w;
    json::Error e;
    expect(!w.set("/teams/slot=1/seats/9", 1, &e) && !e.ok() && w == before, "an index past the end is refused");
    expect(!w.set("/year/x", 1, &e) && w == before, "set refuses to go through a number");
    expect(!w.set("/teams/slot=1/team/x/y", 1, &e) && w == before, "set refuses to go through a string");
    expect(!w.set("/teams/abc", 1, &e) && w == before, "a segment that is no index or key of an array is refused");
    expect(!w.set("/m~2", 1, &e) && w == before, "a bad escape is refused");
    bool built = w.set("/new/deep/-/x", 5);
    expect(built && json::dump(*w.get("/new")) == R"({"deep":[{"x":5}]})", "set creates objects and arrays on the way");
    expect(w.set("/new/=odd", 1) && *w.get("/new/=odd") == Value(1), "an object member may have '=' in it");
    Value root = 5;
    expect(root.set("", P("[1]")) && json::dump(root) == "[1]", "set of the root replaces it");
    expect(w.remove("/teams/slot=4") && !w.get("/teams/slot=4"), "remove a keyed element");
    expect(w.remove("/changes/round=15,slot=8,seat=1") && w.get("/changes")->size() == 1,
           "remove a keyed element with three fields");
    expect(w.remove("/teams/slot=1/seats/0") && *w.get("/teams/slot=1/seats/0") == Value("game/valtteri_bottas"),
           "remove an array element");
    expect(w.remove("year") && !w.get("year"), "remove a member");
    expect(!w.remove("year") && !w.remove("") && !w.remove("/teams/slot=99") && !w.remove("/teams/77") &&
               !w.remove("/teams/slot=1/team/x"),
           "remove of what isn't there is false");
    expect(json::escape("a/b~c") == "a~1b~0c", "escape");
}

static const json::Keys season_keys = {{"/teams", {"slot"}}, {"/changes", {"round", "slot", "seat"}}};

static void keyed()
{
    Value base = P(R"({
      "data_group": "f1_career_progressive",
      "year": 2017,
      "teams": [
        { "slot": 1, "team": "game/mercedes", "seats": ["game/lewis_hamilton", "game/valtteri_bottas"] },
        { "slot": 4, "team": "game/mclaren",  "seats": ["game/fernando_alonso", "game/stoffel_vandoorne"] },
        { "slot": 11, "team": null, "seats": [] }
      ],
      "changes": [
        { "round": 15, "slot": 8, "seat": 1, "out": "game/daniil_kvyat", "in": "game/pierre_gasly", "kind": "swap" }
      ]
    })");
    Value layer = P(R"({
      "teams": [
        { "slot": 1, "seats": ["game/fernando_alonso", "game/valtteri_bottas"] },
        { "slot": 4, "seats": ["game/lewis_hamilton", "game/stoffel_vandoorne"] },
        { "slot": 11, "team": "game/manor", "seats": ["game/pascal_wehrlein", "game/esteban_ocon"] }
      ],
      "changes": [
        { "round": 15, "slot": 8, "seat": 1, "$delete": true }
      ]
    })");
    Value effective = P(R"({
      "data_group": "f1_career_progressive",
      "year": 2017,
      "teams": [
        { "slot": 1, "team": "game/mercedes", "seats": ["game/fernando_alonso", "game/valtteri_bottas"] },
        { "slot": 4, "team": "game/mclaren",  "seats": ["game/lewis_hamilton", "game/stoffel_vandoorne"] },
        { "slot": 11, "team": "game/manor", "seats": ["game/pascal_wehrlein", "game/esteban_ocon"] }
      ],
      "changes": []
    })");
    Value out = base;
    json::Error e;
    expect(json::merge_patch(out, layer, season_keys, &e) && e.ok(), "the layers.md layer applies");
    expect(json::dump(out) == json::dump(effective), "the layers.md layer gives the documented effective value: " +
                                                         json::dump(out, 2));
    expect(json::merged(base, layer).get("/teams/0/team") == nullptr,
           "without keys the same patch replaces the list whole, as in RFC 7396");

    auto run = [](const char *target, const char *patch, const json::Keys &keys) {
        Value t = P(target);
        json::Error e;
        if (!json::merge_patch(t, P(patch), keys, &e))
            return "error: " + e.message;
        return json::dump(t);
    };
    json::Keys k = {{"/list", {"id"}}};
    expect(run(R"({"list":[{"id":1,"a":1},{"id":2,"a":2}]})", R"({"list":[{"id":3,"a":3}]})", k) ==
               R"({"list":[{"id":1,"a":1},{"id":2,"a":2},{"id":3,"a":3}]})",
           "a key the target lacks is appended");
    expect(run(R"({"list":[{"id":1,"a":1,"b":1}]})", R"({"list":[{"id":1,"a":null,"c":{"x":null,"y":1}}]})", k) ==
               R"({"list":[{"id":1,"b":1,"c":{"y":1}}]})",
           "a keyed element merges as a merge patch, null removing a field");
    expect(run(R"({"list":[{"id":1},{"id":2}]})", R"({"list":[{"id":2,"$delete":true},{"id":9,"$delete":true}]})", k) ==
               R"({"list":[{"id":1}]})",
           "$delete removes the element with that key and ignores a missing one");
    expect(run(R"({"list":[{"id":1}]})", R"({"list":[{"id":1,"$delete":false,"a":1}]})", k) ==
               R"({"list":[{"id":1,"a":1}]})",
           "\"$delete\": false merges and is dropped");
    expect(run(R"({"list":[{"id":1}]})", R"({"list":[{"id":5,"$delete":false}]})", k) == R"({"list":[{"id":1},{"id":5}]})",
           "\"$delete\": false on a new key appends without it");
    expect(run(R"({"list":[{"id":1},{"id":2}]})", R"({"list":[]})", k) == R"({"list":[{"id":1},{"id":2}]})",
           "an empty patch list changes nothing");
    expect(run(R"({"x":1})", R"({"list":[]})", k) == R"({"x":1,"list":[]})" &&
               run(R"({"list":"text"})", R"({"list":[]})", k) == R"({"list":[]})",
           "an empty patch list on a missing list (or one that isn't a list) gives an empty list");
    expect(run(R"({"list":[{"id":1},{"id":2}]})", R"({"list":[{"a":1}]})", k).starts_with("error:"),
           "an element without its key field is an error");
    expect(run(R"({"list":[{"id":1},{"id":2}]})", R"({"list":[{"id":1},3]})", k).starts_with("error:"),
           "an element that isn't an object is an error");
    expect(run(R"({"list":[{"id":1}]})", R"({"list":[{"id":null}]})", k).starts_with("error:") &&
               run(R"({"list":[{"id":1}]})", R"({"list":[{"id":[1]}]})", k).starts_with("error:"),
           "a null or list key isn't a key: an error");
    expect(run(R"({"list":[{"id":1}]})", R"({"list":[{"a":1},{"id":1,"$delete":true}]})", k).starts_with("error:"),
           "one keyless element fails the whole list, $delete and all");
    {
        Value t = P(R"({"x":1,"list":[{"id":1}]})"), keep = t;
        json::Error e;
        expect(!json::merge_patch(t, P(R"({"x":2,"list":[{"id":2},{"b":1}]})"), k, &e) && t == keep &&
                   e.message.find("/list/1") != std::string::npos,
               "a keyless element leaves the target as it was and is named: " + e.message);
    }
    expect(run(R"({"seats":[1,2]})", R"({"seats":[]})", k) == R"({"seats":[]})" &&
               run(R"({"seats":[1,2]})", R"({"seats":[{"a":1}]})", k) == R"({"seats":[{"a":1}]})",
           "a list that isn't keyed replaces whole, an empty one too");
    expect(run(R"({"list":[{"id":1}]})", R"({"list":[{"id":1,"$delete":"yes"}]})", k).starts_with("error:"),
           "a $delete that isn't a bool is an error");
    {
        Value t = P(R"({"list":[{"id":1}],"x":1})"), keep = t;
        json::Error e;
        expect(!json::merge_patch(t, P(R"({"x":2,"list":[{"id":1,"$delete":7}]})"), k, &e) && t == keep && !e.ok(),
               "a refused patch leaves the target as it was");
        expect(e.message.find("/list/0") != std::string::npos, "the error names the element: " + e.message);
    }
    expect(run(R"({"list":[{"id":1}]})", R"({"other":[{"id":1,"$delete":true}]})", k) ==
               R"({"list":[{"id":1}],"other":[{"id":1,"$delete":true}]})",
           "$delete in a list that isn't keyed is plain data");
    expect(run(R"({"list":"text"})", R"({"list":[{"id":1,"a":1}]})", k) == R"({"list":[{"id":1,"a":1}]})",
           "a keyed patch on a target that isn't a list starts from an empty list");
    expect(run(R"({})", R"({"list":[{"id":1,"a":null,"b":2},{"id":2,"$delete":true}]})", k) ==
               R"({"list":[{"id":1,"b":2}]})",
           "a keyed patch on a missing list builds it, nulls and deletes left out");
    expect(run(R"({"list":[{"id":1,"a":1}]})", R"({"list":[{"id":1,"a":2},{"id":1,"b":3}]})", k) ==
               R"({"list":[{"id":1,"a":2,"b":3}]})",
           "the same key twice merges in order");
    expect(run(R"({"list":[{"id":1,"a":1},{"id":2}]})", R"({"list":[{"id":1,"$delete":true},{"id":1,"b":1}]})", k) ==
               R"({"list":[{"id":2},{"id":1,"b":1}]})",
           "delete then add the same key appends a fresh element");
    expect(run(R"({"list":[{"id":1},{"id":"1"}]})", R"({"list":[{"id":"1","s":true}]})", k) ==
               R"({"list":[{"id":1},{"id":"1","s":true}]})",
           "a string key and a number key differ");
    expect(run(R"({"list":[{"id":1}]})", R"({"list":[{"id":1.0,"f":1}]})", k) == R"({"list":[{"id":1.0,"f":1}]})",
           "number keys match by value, and the patch's key field merges like any other");
    expect(run(R"({"list":[{"id":[1]}]})", R"({"list":[{"id":1}]})", k) == R"({"list":[{"id":[1]},{"id":1}]})",
           "a target element without a scalar key never matches");

    json::Keys nested = {{"/teams", {"slot"}}, {"/teams/*/drivers", {"name"}}, {"cars", {"team", "number"}}};
    expect(run(R"({"teams":[{"slot":1,"drivers":[{"name":"a","n":1},{"name":"b","n":2}]}]})",
               R"({"teams":[{"slot":1,"drivers":[{"name":"b","n":20},{"name":"a","$delete":true},{"name":"c"}]}]})",
               nested) == R"({"teams":[{"slot":1,"drivers":[{"name":"b","n":20},{"name":"c"}]}]})",
           "a keyed list inside a keyed element merges by its own key");
    expect(run(R"({"teams":[{"slot":1,"drivers":[{"name":"a"}]}]})",
               R"({"teams":[{"slot":1,"drivers":[{"name":"a","$delete":"x"}]}]})", nested)
               .starts_with("error:"),
           "a nested bad $delete is an error");
    expect(run(R"({"cars":[{"team":"f","number":5,"x":1},{"team":"f","number":7}]})",
               R"({"cars":[{"team":"f","number":7,"x":2},{"team":"m","number":5}]})", nested) ==
               R"({"cars":[{"team":"f","number":5,"x":1},{"team":"f","number":7,"x":2},{"team":"m","number":5}]})",
           "a two-field key needs both fields to match (a path without its slash works)");
    expect(run(R"([{"k":1,"a":1}])", R"([{"k":1,"b":2}])", json::Keys{{"", {"k"}}}) == R"([{"k":1,"a":1,"b":2}])",
           "the root itself can be a keyed list");
    expect(run(R"({"teams":[{"slot":1,"drivers":[{"name":"a"}]}]})", R"({"teams":[{"slot":1,"drivers":[{"x":1}]}]})",
               nested)
               .starts_with("error:"),
           "a nested element without its key is an error");
    expect(run(R"({"teams":[{"slot":1,"drivers":[{"name":"a"}]}]})", R"({"teams":[{"slot":1,"drivers":[]}]})", nested) ==
               R"({"teams":[{"slot":1,"drivers":[{"name":"a"}]}]})",
           "a nested empty keyed list changes nothing");
    expect(run(R"({"teams":[{"slot":1,"drivers":[{"name":"a"}]}]})", R"({"teams":[{"slot":1,"$delete":true,"drivers":[{"x":1}]}]})",
               nested) == R"({"teams":[]})",
           "a $delete element's other fields aren't merged, so they aren't checked");
    {
        json::Keys added;
        added.add("list", {"id"}).add("/list", {"key"});
        expect(added.find("/list") && added.find("list") && (*added.find("/list"))[0] == "key" && !added.find("/other"),
               "Keys::add replaces a path's fields and find takes the path with or without its slash");
    }
}

static void numbers()
{
    auto kind = [](std::string_view text) {
        json::Error e;
        Value v = json::parse(text, &e);
        if (!e.ok())
            return std::string("error");
        return std::string(v.is_int() ? "int " : v.is_double() ? "double " : "other ") + json::dump(v);
    };
    expect(kind("0") == "int 0", "0 is an int");
    expect(kind("-0") == "int 0", "-0 is the int 0");
    expect(kind("-0.0") == "double -0.0", "-0.0 stays a negative zero double");
    expect(kind("1.0") == "double 1.0", "1.0 stays a double: " + kind("1.0"));
    expect(kind("1e2") == "double 100.0", "1e2 is a double: " + kind("1e2"));
    expect(kind("1E+2") == "double 100.0", "1E+2 is a double");
    expect(kind("9223372036854775807") == "int 9223372036854775807", "int64 max is an int");
    expect(kind("-9223372036854775808") == "int -9223372036854775808", "int64 min is an int");
    expect(kind("9223372036854775808") == "double 9223372036854775808.0", "past int64 is a double: " +
                                                                               kind("9223372036854775808"));
    expect(kind("-9223372036854775809") == "double -9223372036854775808.0", "below int64 is a double");
    expect(kind("1e5") == "double 1e+05" && kind("123456789e300") == "double 1.23456789e+308", "exponents dump as such");
    expect(kind("9007199254740993") == "int 9007199254740993", "an int past 2^53 keeps every digit");
    expect(kind("0.1") == "double 0.1", "0.1 dumps shortest");
    expect(kind("1.7976931348623157e308") == "double 1.7976931348623157e+308", "the largest double");
    expect(kind("5e-324") == "double 5e-324", "the smallest denormal");
    expect(kind("1e400") == "error" && kind("-1e400") == "error" && kind("123456789e301") == "error",
           "a double too large is refused");
    expect(kind("1e-400") == "double 0.0" && kind("-1e-400") == "double -0.0" && kind("0.000001e-330") == "double 0.0",
           "a double too small is zero");
    expect(kind("0e99999999999999999999") == "double 0.0", "zero with a huge exponent is zero");
    expect(kind("1" + std::string(400, '0')) == "error", "a 401-digit integer is refused as out of range");
    expect(kind(("0." + std::string(400, '0') + "1").c_str()) == "double 0.0", "a long fraction underflows to zero");
    expect(kind(("1." + std::string(1000, '1')).c_str()).starts_with("double 1.11111"), "a 1000-digit fraction parses");
    expect(kind("01") == "error" && kind("+1") == "error" && kind(".5") == "error" && kind("1.") == "error" &&
               kind("1e") == "error" && kind("-") == "error" && kind("0x1") == "error" && kind("NaN") == "error" &&
               kind("Infinity") == "error" && kind("1.5e+") == "error",
           "malformed numbers are refused");
    expect(json::dump(Value(1.0 / 0.0)) == "null" && json::dump(Value(std::nan(""))) == "null",
           "inf and NaN dump as null");
    expect(Value(std::uint64_t(18446744073709551615ull)).is_double(), "a uint64 past int64 becomes a double");
    expect(Value(std::uint32_t(4000000000u)).as_int() == 4000000000, "a uint32 is an int");
    expect(Value(3) == Value(3.0) && !(Value(3) == Value(3.5)) && !(Value(1) == Value(true)),
           "numbers compare by value, not kind");
    expect(Value(3.0).as_int() == 3 && Value(3.5).as_int(-1) == -1 && Value(1e300).as_int(-1) == -1 &&
               Value(std::nan("")).as_int(-1) == -1,
           "as_int takes only whole doubles in range");
    expect(Value(7).as_double() == 7.0 && Value("7").as_double(-1) == -1, "as_double");
    double samples[] = {0.1, 1.0 / 3, 2.5e-308, 123456.789, 1e21, 1e-7, 4.9e-324, 1.7976931348623157e308, -2.0};
    for (double d : samples) {
        Value back = json::parse(json::dump(Value(d)));
        expect(back.is_double() && back.as_double() == d, "double round-trips exactly: " + json::dump(Value(d)));
    }
}

static void values()
{
    Value v;
    expect(v.is_null() && v.type() == json::Type::null && json::dump(v) == "null", "a default value is null");
    v["b"] = 1;
    v["a"] = "x";
    v["c"]["d"] = true;
    v["b"] = 2;
    expect(json::dump(v) == R"({"b":2,"a":"x","c":{"d":true}})", "members keep insertion order: " + json::dump(v));
    expect(json::dump(v, 2) == "{\n  \"b\": 2,\n  \"a\": \"x\",\n  \"c\": {\n    \"d\": true\n  }\n}",
           "pretty dump: " + json::dump(v, 2));
    expect(json::dump(P(R"({"a":[],"b":{},"c":[1,[2]]})"), 1) == "{\n \"a\": [],\n \"b\": {},\n \"c\": [\n  1,\n  [\n   2\n  ]\n ]\n}",
           "pretty dump of empty and nested containers");
    expect(json::dump(P("[1,2]"), 0) == "[\n1,\n2\n]", "indent 0 puts each item on its own line");
    const Value &c = v;
    expect(c["nope"].is_null() && c["b"][0].is_null() && c[3].is_null() && c["c"]["d"].as_bool(), "const lookups");
    expect(c["a"].as_string() == "x" && c["b"].as_string("dflt") == "dflt" && c["a"].as_int(9) == 9 &&
               c["a"].as_bool(true),
           "typed accessors give the fallback for another type");
    expect(v.size() == 3 && v.erase("a") && !v.erase("a") && v.size() == 2 && !v.contains("a"), "erase");
    Value a;
    a[2] = "z";
    a.push_back(4);
    a[-1] = 5;
    expect(json::dump(a) == R"([null,null,"z",4,5])", "array writes grow it: " + json::dump(a));
    Value s = "text";
    s["k"] = 1;
    expect(json::dump(s) == R"({"k":1})", "writing a member makes an object of a scalar");
    Value obj = Value::object({{"x", 1}, {"y", Value::array({1, "two", nullptr, 2.5, false})}});
    expect(json::dump(obj) == R"({"x":1,"y":[1,"two",null,2.5,false]})", "object and array builders");
    expect(P(R"({"a":1,"b":[1,2]})") == P(R"({"b":[1,2],"a":1})") && !(P("[1,2]") == P("[2,1]")),
           "object equality ignores order, array equality doesn't");
    expect(json::dump(P(R"({"a":1,"b":2,"a":3})")) == R"({"a":3,"b":2})",
           "a repeated key keeps its first place and its last value");
    {
        json::Error e;
        auto strict = json::ParseOptions::strict();
        Value dup = json::parse(R"({"a":{"x":1,"y":2,"x":3}})", strict, &e);
        expect(!e.ok() && e.message == "duplicate key" && dup.is_null(), "strict parsing refuses a repeated key, nested too");
        std::string many = "{";
        for (int k = 0; k < 40; ++k)
            many += "\"k" + std::to_string(k) + "\":" + std::to_string(k) + ",";
        many += "\"k17\":0}";
        json::parse(many, strict, &e);
        expect(!e.ok(), "strict parsing finds a repeat past the object's index threshold");
        Value fine = json::parse(R"({"a":1,"b":{"a":2}})", strict, &e);
        expect(e.ok() && json::dump(fine) == R"({"a":1,"b":{"a":2}})", "strict parsing takes the same key in different objects");
        json::parse("\xEF\xBB\xBF{}", strict, &e);
        expect(!e.ok(), "strict parsing refuses a byte order mark");
        json::parse("\xEF\xBB\xBF{}", &e);
        expect(e.ok(), "the default still skips a byte order mark");
        json::parse(R"({"a":1,"a":2})", json::ParseOptions{}, &e);
        expect(e.ok(), "default options keep today's behaviour");
    }
    Value big;
    for (int k = 0; k < 2000; ++k)
        big["k" + std::to_string(k)] = k;
    big.erase("k5");
    big["k5"] = -5;
    bool all = big.size() == 2000 && big["k1999"].as_int() == 1999 && big["k5"].as_int() == -5;
    Value copy = big;
    for (int k = 0; k < 2000; ++k)
        all = all && copy["k" + std::to_string(k)].as_int() == (k == 5 ? -5 : k);
    auto *o = copy.if_object();
    all = all && o && o->entry(1999).first == "k5";
    o->insert_front("first", 0);
    all = all && o->entry(0).first == "first" && copy["k10"].as_int() == 10 && copy.size() == 2001;
    expect(all, "large objects look members up by key after copies, erases and inserts");
    expect(json::dump(Value("a\"b\\c\n\t\x01\x1f/é")) == "\"a\\\"b\\\\c\\n\\t\\u0001\\u001f/é\"", "string escapes");
    expect(json::dump(Value("bad \xff\xc3 utf8")) == "\"bad \\ufffd\\ufffd utf8\"",
           "a string that isn't UTF-8 dumps with replacement characters");
    expect(json::parse(R"("𝄞 é \u0000")").as_string() == std::string("\xF0\x9D\x84\x9E \xC3\xA9 \0", 9),
           "\\u escapes, surrogate pairs and NUL");
    expect(json::parse(R"("\ud800x")").as_string() == "\xEF\xBF\xBDx", "a lone surrogate becomes U+FFFD");
    expect(json::parse(R"("\ud800A")").as_string() == "\xEF\xBF\xBD" "A", "a high surrogate before a non-low");
    json::Error e;
    json::parse("{\n  \"a\": 1,\n  \"b\": tru\n}", &e);
    expect(!e.ok() && e.line == 3 && e.column == 8 && e.offset == 19,
           "errors give line and column: " + std::to_string(e.line) + ":" + std::to_string(e.column) + " " + e.message);
    json::parse("\xEF\xBB\xBF{}", &e);
    expect(e.ok(), "a UTF-8 byte order mark is skipped");
    json::parse("", &e);
    expect(!e.ok(), "an empty document is refused");
    expect(json::parse("not json").is_null(), "parse without an Error gives null on failure");
    std::string deep(json::max_depth, '['), shut(json::max_depth, ']');
    json::parse(deep + shut, &e);
    expect(e.ok(), "max_depth levels parse");
    json::parse("[" + deep + shut + "]", &e);
    expect(!e.ok(), "one level past max_depth is refused");
}

static Value reference_merge(const Value &target, const Value &patch)
{
    if (!patch.is_object())
        return patch;
    Value out = target.is_object() ? target : Value(json::Object());
    for (auto &[name, v] : *patch.if_object()) {
        if (v.is_null())
            out.erase(name);
        else
            out[name] = reference_merge(out[name], v);
    }
    return out;
}

static Value random_value(std::mt19937_64 &rng, int depth)
{
    auto pick = [&](int n) { return static_cast<int>(rng() % static_cast<std::uint64_t>(n)); };
    int t = pick(depth > 4 ? 6 : 9);
    static const char *keys[] = {"id", "a", "b", "slot", "$delete", "", "x/y", "m~n", "é", "k"};
    switch (t) {
    case 0:
        return Value();
    case 1:
        return Value(pick(2) == 1);
    case 2:
        return Value(static_cast<std::int64_t>(rng()) >> pick(64));
    case 3: {
        double d;
        std::uint64_t bits = rng();
        std::memcpy(&d, &bits, sizeof d);
        return std::isfinite(d) ? Value(d) : Value(pick(1000) / 7.0);
    }
    case 4:
    case 5: {
        std::string s;
        int n = pick(6);
        static const char *parts[] = {"a", "b", "\"", "\\", "\n", "/", "~", "=", ",", "é", "\x01", "1"};
        for (int k = 0; k < n; ++k)
            s += parts[pick(12)];
        return Value(s);
    }
    case 6:
    case 7: {
        Value a = Value::array();
        int n = pick(5);
        for (int k = 0; k < n; ++k)
            a.push_back(random_value(rng, depth + 1));
        return a;
    }
    default: {
        Value o = Value::object();
        int n = pick(5);
        for (int k = 0; k < n; ++k)
            o[keys[pick(10)]] = random_value(rng, depth + 1);
        if (pick(2))
            o["id"] = pick(4);
        return o;
    }
    }
}

static void walk(Value &root, Value &v, const std::string &ptr, int &bad)
{
    if (root.get(ptr) != &v)
        ++bad;
    if (auto o = v.if_object())
        for (auto &[name, child] : *o)
            walk(root, child, ptr + "/" + json::escape(name), bad);
    if (auto a = v.if_array())
        for (std::size_t k = 0; k < a->size(); ++k) {
            walk(root, (*a)[k], ptr + "/" + std::to_string(k), bad);
            auto seg = json::key_segment((*a)[k], {"id"});
            if (!seg.empty()) {
                auto first = std::find_if(a->begin(), a->end(), [&](const Value &e) {
                    auto id = e.find("id");
                    return id && (*id == *(*a)[k].find("id")) && id->type() == (*a)[k].find("id")->type();
                });
                if (root.get(ptr + "/" + seg) != &*first)
                    ++bad;
            }
        }
}

static void show(const char *what, const std::string &text)
{
    static int shown;
    if (shown++ >= 6)
        return;
    std::string esc;
    for (unsigned char c : text.substr(0, 300))
        if (c >= 0x20 && c < 0x7f)
            esc += static_cast<char>(c);
        else {
            char b[8];
            snprintf(b, sizeof b, "\\x%02x", c);
            esc += b;
        }
    printf("fuzz %s: %s\n", what, esc.c_str());
}

static void fuzz(int rounds)
{
    std::mt19937_64 rng(0x5eed1ee7);
    auto pick = [&](int n) { return static_cast<int>(rng() % static_cast<std::uint64_t>(n)); };
    std::vector<std::string> seeds;
    for (auto &entry : std::filesystem::directory_iterator("json/suite"))
        if (entry.path().extension() == ".json")
            seeds.push_back(slurp(entry.path()));
    std::sort(seeds.begin(), seeds.end());
    const char alphabet[] = "{}[]:,\"\\/-+.0123456789eEtrufalsn \t\r\nu\xc3\xa9\xff\x00";
    int accepted = 0, round_trip_bad = 0, merge_bad = 0, pointer_bad = 0, keyed_bad = 0;
    for (int r = 0; r < rounds; ++r) {
        std::string text = seeds[static_cast<std::size_t>(pick(static_cast<int>(seeds.size())))];
        int edits = 1 + pick(4);
        for (int k = 0; k < edits; ++k) {
            auto at = text.empty() ? 0 : static_cast<std::size_t>(pick(static_cast<int>(text.size()) + 1));
            char c = alphabet[pick(sizeof alphabet - 1)];
            switch (pick(4)) {
            case 0:
                text.insert(at, 1, c);
                break;
            case 1:
                if (at < text.size())
                    text[at] = c;
                break;
            case 2:
                if (at < text.size())
                    text.erase(at, 1);
                break;
            default:
                if (!text.empty()) {
                    auto from = static_cast<std::size_t>(pick(static_cast<int>(text.size())));
                    text.insert(at, text.substr(from, static_cast<std::size_t>(pick(8))));
                }
            }
        }
        json::Error e;
        Value v = json::parse(text, &e);
        if (e.ok()) {
            ++accepted;
            if (!round_trips(v)) {
                ++round_trip_bad;
                show("round trip", text);
            }
        } else if (!v.is_null() || e.message.empty() || e.offset > text.size()) {
            ++round_trip_bad;
            show("refusal", text);
        }

        Value target = random_value(rng, 0), patch = random_value(rng, 0);
        if (!(json::merged(target, patch) == reference_merge(target, patch)))
            ++merge_bad;
        if (!round_trips(target)) {
            ++round_trip_bad;
            show("built value", json::dump(target));
        }

        int bad = 0;
        walk(target, target, "", bad);
        if (bad)
            show("walk", json::dump(target));
        pointer_bad += bad;
        std::string ptr;
        int segs = pick(4);
        for (int k = 0; k < segs; ++k)
            ptr += std::string("/") + std::vector<const char *>{"id", "a", "0", "1", "-", "id=1", "id=\"x\"", "slot=0,id=2", "~1", "~", "="}[static_cast<std::size_t>(pick(11))];
        Value before = target, marker = Value::object({{"marker", r}});
        json::Error pe;
        if (target.set(ptr, marker, &pe)) {
            auto got = target.get(ptr);
            if (ptr.find_first_of("-=") == std::string::npos && (!got || !(got->find("marker") && *got->find("marker") == Value(r)))) {
                ++pointer_bad;
                show("set", ptr + " on " + json::dump(before));
            }
        } else if (!(target == before) || pe.ok()) {
            ++pointer_bad;
            show("refused set", ptr + " on " + json::dump(before));
        }
        target.remove(ptr);

        json::Keys keys = {{"", {"id"}}, {"/a", {"id"}}, {"/b", {"id", "slot"}}, {"/a/*/b", {"id"}}};
        Value base = random_value(rng, 0), kp = random_value(rng, 0);
        Value once = base;
        json::Error ke;
        if (json::merge_patch(once, kp, keys, &ke)) {
            Value twice = once;
            if (!json::merge_patch(twice, kp, keys, &ke) || !(twice == once)) {
                ++keyed_bad;
                show("keyed twice", json::dump(base) + " + " + json::dump(kp));
            }
        } else if (ke.ok()) {
            ++keyed_bad;
            show("keyed error", json::dump(kp));
        }
    }
    printf("fuzz: %d rounds, %d mutated documents accepted\n", rounds, accepted);
    expect(round_trip_bad == 0, "fuzz: parse, dump and parse agree (" + std::to_string(round_trip_bad) + " bad)");
    expect(merge_bad == 0, "fuzz: merge_patch without keys matches the RFC 7396 pseudocode (" +
                               std::to_string(merge_bad) + " bad)");
    expect(pointer_bad == 0, "fuzz: pointers find what a walk finds, and set either applies or changes nothing (" +
                                 std::to_string(pointer_bad) + " bad)");
    expect(keyed_bad == 0, "fuzz: a keyed merge patch applied twice is the same as once (" + std::to_string(keyed_bad) +
                               " bad)");
}

int main(int argc, char **argv)
{
    int rounds = argc > 1 ? std::atoi(argv[1]) : 20000;
    suite("json/suite");
    rfc7396();
    pointers();
    keyed();
    numbers();
    values();
    fuzz(rounds);
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
