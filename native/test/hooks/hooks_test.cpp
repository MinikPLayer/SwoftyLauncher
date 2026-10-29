// Host unit test for the 1.6 hook points, events and services: dispatch order, priorities and owners,
// kinds, selectors against synthetic contexts, context parts, fault isolation (a handler that throws is
// the test's fault path), addon points and scopes, the per-fire log lines the test suite counts, a game
// point installed on a function in this binary (first subscriber, last subscriber, late install, an
// overwrite it yields to), and services' when ordering. run_hooks_test.sh builds and runs it.
#include "../../src/core/hooks/dispatch.hpp"
#include "../../src/core/hooks/game_point.hpp"
#include "../../src/core/hooks/payloads.hpp"
#include "../../src/core/services/services.hpp"
#include "../sdk_payloads.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

using namespace swofty_core::hooks;
using swofty_core::services::Registry;

static int fails;

static void expect(bool ok, const std::string &what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
    fails += !ok;
}

static std::vector<std::string> lines;
static std::vector<std::string> calls;

struct TestCrash {
    std::string text;
};

static Platform platform()
{
    Platform p;
    p.log = [](const std::string &s) { lines.push_back(s); };
    p.call = [](const Handler &h, SwoftyHookCall *c, bool isolate, Fault *f) -> int {
        try {
            return h.fn(h.user, c);
        } catch (const std::exception &e) {
            if (!isolate)
                throw TestCrash{"Crash in " + h.addon + "'s " + catalogue::phase_name(c->phase) + " handler on " + c->point};
            f->faulted = true;
            f->what = std::string("C++ exception: ") + e.what();
            return 0;
        }
    };
    return p;
}

// The test suite's parser (packages/core/src/testsuite/checks.rs is_event), ported line for line.
static bool is_event(const std::string &line, const std::string &id)
{
    size_t at = line.find("[events] ");
    if (at == std::string::npos)
        return false;
    std::string rest = line.substr(at + 9);
    if (rest.compare(0, id.size(), id))
        return false;
    rest = rest.substr(id.size());
    if (rest.empty())
        return true;
    char c = rest[0];
    if (isalnum((unsigned char)c) || c == '_' || c == '.')
        return false;
    for (const char *n : {" has no ", " hooked at ", " signature matched ", " resolved to ", " at base+"})
        if (!rest.compare(0, strlen(n), n))
            return false;
    return true;
}

static size_t count_event(const std::string &id)
{
    size_t n = 0;
    for (auto &l : lines)
        n += is_event(l, id);
    return n;
}

static bool logged(const std::string &needle)
{
    for (auto &l : lines)
        if (l.find(needle) != std::string::npos)
            return true;
    return false;
}

struct Payload {
    uint32_t size, payload_version;
    int value;
};

struct Tag {
    std::string name;
    int ret = 0;
    int set = -1;
    bool fault = false;
    int seen_replaced = -1;
    int fire_again = 0;
    Table *t = nullptr;
};

static int record(void *user, SwoftyHookCall *c)
{
    auto *tag = static_cast<Tag *>(user);
    calls.push_back(tag->name);
    auto *p = static_cast<Payload *>(c->payload);
    if (tag->set >= 0)
        p->value = tag->set;
    if (c->phase == SWOFTY_PHASE_AFTER)
        tag->seen_replaced = c->replaced;
    if (tag->fire_again && tag->t) {
        Payload again{sizeof again, 1, 0};
        int r = 0;
        tag->fire_again = tag->t->fire("", c->point, &again, &r);
    }
    if (tag->fault)
        throw std::runtime_error("handler fault");
    return tag->ret;
}

static std::string joined()
{
    std::string s;
    for (auto &c : calls)
        s += (s.empty() ? "" : " ") + c;
    return s;
}

static void test_order()
{
    lines.clear();
    Table t(platform());
    t.declare("test.cancel", SWOFTY_POINT_CANCEL, 1, true, false);
    Tag a{"a.before"}, b{"b.before10"}, own{"owner.before"}, c{"c.before"};
    Tag ra{"a.replace"}, rown{"owner.replace"}, rb{"b.replace"};
    Tag af1{"a.after"}, af2{"b.after5"};
    b.set = 7;
    rown.ret = 0;
    rb.ret = 1;
    rb.set = 42;
    uint32_t tok = 0;
    t.attach("a", "test.cancel", SWOFTY_PHASE_BEFORE, 0, "", record, &a, &tok);
    t.attach("b", "test.cancel", SWOFTY_PHASE_BEFORE, 10, "", record, &b, &tok);
    t.attach("owner", "test.cancel", SWOFTY_PHASE_BEFORE, 100, "", record, &own, &tok);
    t.attach("c", "test.cancel", SWOFTY_PHASE_BEFORE, 0, "", record, &c, &tok);
    t.attach("a", "test.cancel", SWOFTY_PHASE_REPLACE, 50, "", record, &ra, &tok);
    t.attach("owner", "test.cancel", SWOFTY_PHASE_REPLACE, -5, "", record, &rown, &tok);
    t.attach("b", "test.cancel", SWOFTY_PHASE_REPLACE, 0, "", record, &rb, &tok);
    t.attach("a", "test.cancel", SWOFTY_PHASE_AFTER, 0, "", record, &af1, &tok);
    t.attach("b", "test.cancel", SWOFTY_PHASE_AFTER, 5, "", record, &af2, &tok);
    t.update([](Facts &f) {
        f.mode = "driver_career";
        f.content = "owner/f1_career_1990";
        f.content_owner = "owner";
        f.owners = {"owner"};
        f.career.emplace();
        f.career->year = 1990;
    });
    calls.clear();
    Payload p{sizeof p, 1, 0};
    int replaced = -1;
    int r = t.fire("", "test.cancel", &p, &replaced);
    expect(r == SWOFTY_ASSET_OK && replaced == 1, "fire: ok, and it reports that a replace handler handled it");
    expect(joined() ==
               "b.before10 a.before c.before owner.before owner.replace a.replace b.replace b.after5 a.after",
           "order: before by priority then load order with the owner last; replace owner first, then priority, until one "
           "returns true; after by priority (" + joined() + ")");
    expect(p.value == 42, "payload changes in before and replace reach the caller");
    expect(af1.seen_replaced == 1, "after handlers see the call as replaced");
    expect(logged("[events] test.cancel (driver_career, owner/f1_career_1990, 1990): 4 before handlers, 3 replace "
                  "handlers, 2 after handlers; replace by b -> handled"),
           "one log line names the context, the counts and who replaced it");
    (void)r;

    calls.clear();
    lines.clear();
    rb.ret = 0;
    rown.ret = 1;
    t.fire("", "test.cancel", &p, &replaced);
    expect(joined() == "b.before10 a.before c.before owner.before owner.replace b.after5 a.after",
           "the owner's replace handler runs first and stops the chain");
    expect(logged("replace by owner (owner) -> handled, 2 other replace not reached (a, b)"),
           "the log names the owner and the replace handlers not reached");

    calls.clear();
    rown.ret = 0;
    bool ran = false;
    t.fire("", "test.cancel", &p, &replaced, [&] { ran = true; });
    expect(ran && replaced == 0, "the game's code runs between replace and after when nothing replaced it");
}

static void test_kinds()
{
    lines.clear();
    Table t(platform());
    t.declare_core();
    Tag x{"x"};
    uint32_t tok = 0;
    int r = t.attach("my-addon", "career.loaded", SWOFTY_PHASE_REPLACE, 0, "", record, &x, &tok);
    expect(r == SWOFTY_ASSET_INVALID &&
               logged("[events] career.loaded has no replace phase (notify event); handler from my-addon not attached"),
           "a replace handler on a notify event is refused when it attaches");
    r = t.attach("my-addon", "career.loaded", SWOFTY_PHASE_BEFORE, 0, "", record, &x, &tok);
    expect(r == SWOFTY_ASSET_INVALID, "a before handler on a notify event is refused");
    r = t.attach("my-addon", "car.vtf.read", SWOFTY_PHASE_REPLACE, 0, "", record, &x, &tok);
    expect(r == SWOFTY_ASSET_INVALID, "a replace handler on a modify event is refused");
    r = t.attach("my-addon", "car.vtf.read", SWOFTY_PHASE_BEFORE, 0, "", record, &x, &tok);
    expect(r == SWOFTY_ASSET_OK, "a before handler on a modify event attaches");
    r = t.attach("other", "other.point", SWOFTY_PHASE_REPLACE, 0, "", record, &x, &tok);
    expect(r == SWOFTY_ASSET_OK && logged("nothing defines other.point yet"),
           "a handler on a point nobody defined yet is held");
    expect(t.undefined().size() == 1 && t.undefined()[0].first == "other.point", "the end-of-load report lists it");
    t.define("other", "other.point", 1, SWOFTY_POINT_NOTIFY, true, "[]");
    expect(t.handlers("other.point") == 0 &&
               logged("[events] other.point has no replace phase (notify event); handler from other not attached"),
           "defining it as notify drops the held replace handler, with the same line");
    for (const char *line : {"[events] career.loaded has no replace phase (notify event); handler from my-addon not attached",
                             "[events] attach after on career.loaded by my-addon -> ok token 3"})
        expect(!is_event(line, "career.loaded"), std::string("the test suite does not count: ") + line);
    lines.clear();
    t.attach("my-addon", "career.starting", SWOFTY_PHASE_REPLACE, 0, "", record, &x, &tok);
    expect(logged("the core runs only after handlers on career.starting"), "career.starting says it runs after handlers only");
    calls.clear();
    Payload p{sizeof p, 1, 0};
    int rep = 0;
    t.fire("", "career.starting", &p, &rep);
    expect(calls.empty() && rep == 0, "career.starting's replace handler is not run");
}

static Facts facts_1990()
{
    Facts f;
    f.mode = "driver_career";
    f.content = "swofty.f1-1990/f1_career_1990_v";
    f.content_registry = "career.content";
    f.content_owner = "community.variant";
    f.chain = {"swofty.f1-1990/f1_career_1990_v", "swofty.f1-1990/f1_career_1990"};
    f.owners = {"community.variant", "swofty.f1-1990"};
    f.career = Facts::Career{"c1", "f1_career_1990", 1990, 1, 3, 16, 120};
    f.weekend = Facts::Weekend{3, "game/imola"};
    f.session = Facts::Session{"race", "race_1", "game/imola", "swofty.f1-1990/f1_1990", 0, 1, 61};
    f.player = Entrant{4, "ayrton_senna", "mclaren", "mclaren_1990", true};
    f.participants = {*f.player, Entrant{5, "alain_prost", "ferrari", "ferrari_1990", false}};
    f.vehicle_packages = {"swofty.f1-1990/f1_1990"};
    return f;
}

static bool sel(const std::string &json, const Facts &f)
{
    Selectors s;
    std::string why;
    if (!Selectors::compile(json, s, &why)) {
        printf("      (compile failed: %s)\n", why.c_str());
        return false;
    }
    return s.match(f);
}

static void test_selectors()
{
    Facts f = facts_1990();
    Facts gp;
    gp.mode = "grand_prix";
    gp.content = "game/f1_2025";
    gp.session = Facts::Session{"qualifying", "q1", "game/monaco", "", 0, 0, 0};
    expect(sel("[]", f) && sel("", gp), "an empty selector array matches everywhere");
    expect(sel(R"([{"content":"swofty.f1-1990/f1_career_1990"}])", f), "content matches a variant of it");
    expect(!sel(R"([{"content_exact":"swofty.f1-1990/f1_career_1990"}])", f), "content_exact does not match the variant");
    expect(sel(R"([{"content_exact":"swofty.f1-1990/f1_career_1990_v"}])", f), "content_exact matches the record itself");
    expect(sel(R"([{"owner":"swofty.f1-1990"}])", f) && !sel(R"([{"owner":"someone"}])", f),
           "owner matches every owner along the chain");
    expect(sel(R"([{"data_group":"f1_career_1990"}])", f) && !sel(R"([{"data_group":"f1_career_1990"}])", gp),
           "data_group needs a career");
    expect(sel(R"([{"mode":"driver_career"}])", f) && sel(R"([{"mode":"grand_prix"}])", gp) &&
               !sel(R"([{"mode":"time_trial"}])", gp),
           "mode");
    expect(sel(R"([{"session_type":["race","sprint"]}])", f) && !sel(R"([{"session_type":["race"]}])", gp),
           "session_type");
    expect(sel(R"([{"track":"game/imola"}])", f) && !sel(R"([{"track":"game/imola"}])", gp), "track");
    expect(sel(R"([{"year":[1988,1991]}])", f) && !sel(R"([{"year":[2016,2019]}])", f) && !sel(R"([{"year":[1,9999]}])", gp),
           "year, inside a career only");
    expect(sel(R"([{"setup_mode":"swofty.f1-1990/f1_1990"}])", f), "setup_mode");
    expect(sel(R"([{"vehicle_package":"swofty.f1-1990/f1_1990"}])", f) && !sel(R"([{"vehicle_package":"x/y"}])", f),
           "vehicle_package");
    expect(sel(R"([{"any":[{"content":"a/x"},{"content":"swofty.f1-1990/f1_career_1990"}]},{"not":{"track":"game/monaco"}}])", f),
           "any and not, and every entry must match");
    expect(!sel(R"([{"mode":"driver_career"},{"track":"game/monaco"}])", f), "one failing entry fails the array");
    expect(!sel(R"([{"not":{}}])", f), "not of the empty selector matches nothing");
    Selectors s;
    std::string why;
    expect(!Selectors::compile(R"([{"colour":"red"}])", s, &why) && why.find("unknown selector") != std::string::npos,
           "an unknown selector is refused");
    expect(!Selectors::compile(R"({"mode":"x"})", s, &why), "selectors must be an array");

    lines.clear();
    Table t(platform());
    t.declare("test.notify", SWOFTY_POINT_NOTIFY, 1, true, false);
    Tag in{"in1990"}, out{"inmonaco"}, every{"everywhere"};
    uint32_t tok = 0;
    t.attach("a", "test.notify", SWOFTY_PHASE_AFTER, 0, R"([{"content":"swofty.f1-1990/f1_career_1990"},{"session_type":["race"]}])",
             record, &in, &tok);
    t.attach("a", "test.notify", SWOFTY_PHASE_AFTER, 0, R"([{"track":"game/monaco"}])", record, &out, &tok);
    t.attach("a", "test.notify", SWOFTY_PHASE_AFTER, 0, "[]", record, &every, &tok);
    expect(t.attach("a", "test.notify", SWOFTY_PHASE_AFTER, 0, R"([{"colour":1}])", record, &every, &tok) ==
               SWOFTY_ASSET_INVALID,
           "attach refuses a bad selector");
    calls.clear();
    Payload p{sizeof p, 1, 0};
    t.fire("", "test.notify", &p, nullptr, {}, std::make_shared<const Facts>(f));
    expect(joined() == "in1990 everywhere", "scoped handlers run only where every selector matches (" + joined() + ")");
    calls.clear();
    t.fire("", "test.notify", &p, nullptr, {}, std::make_shared<const Facts>(gp));
    expect(joined() == "inmonaco everywhere", "and not elsewhere (" + joined() + ")");
}

static std::string part_seen, in_seen;
static Table *part_table;

static int read_parts(void *, SwoftyHookCall *c)
{
    std::string s;
    part_table->context_part(c->context, "career", s);
    part_seen = s;
    part_table->context_part(c->context, "participants", s);
    part_seen += "|" + s;
    part_table->context_part(c->context, "record:career.season", s);
    part_seen += "|" + s;
    int out = -1;
    part_table->context_in(c->context, R"([{"year":[1990,1990]}])", &out);
    in_seen = std::to_string(out);
    return 0;
}

static void test_context()
{
    Platform p = platform();
    p.records = [](const Facts &f, std::string_view registry) {
        return registry == "career.season" ? std::string("{\"year\":") + std::to_string(f.career->year) + "}" : std::string();
    };
    Table t(std::move(p));
    part_table = &t;
    t.declare("test.notify", SWOFTY_POINT_NOTIFY, 1, true, false);
    uint32_t tok = 0;
    t.attach("a", "test.notify", SWOFTY_PHASE_AFTER, 0, "", read_parts, nullptr, &tok);
    Payload pl{sizeof pl, 1, 0};
    t.fire("", "test.notify", &pl, nullptr, {}, std::make_shared<const Facts>(facts_1990()));
    expect(part_seen.find(R"("data_group":"f1_career_1990","year":1990,"season":1,"round":3,"rounds":16,"day":120)") !=
               std::string::npos,
           "context part career is the CareerView JSON");
    expect(part_seen.find(R"({"car":4,"driver":"ayrton_senna","team":"mclaren","vehicle":"mclaren_1990","human":true})") !=
               std::string::npos,
           "context part participants lists every Entrant");
    expect(part_seen.find(R"(|{"year":1990})") != std::string::npos, "record:<registry> comes from the registry's resolver");
    expect(in_seen == "1", "context_in evaluates a selector array against the call's context");
    Facts menu;
    expect(part_json(menu, "career", {}) == "null" && part_json(menu, "session", {}) == "null" &&
               part_json(menu, "mode", {}) == "\"unknown\"",
           "a part that doesn't apply is null");
    std::string s;
    int dummy;
    expect(t.context_part(&dummy, "mode", s) == SWOFTY_ASSET_INVALID, "a context handle outside a call is refused");
}

static void test_faults()
{
    lines.clear();
    std::vector<std::string> listed;
    Platform p = platform();
    p.faulted = [&](const std::string &point, const char *phase, const std::string &addon, const std::string &) {
        listed.push_back(addon + " " + phase + " " + point);
    };
    Table t(std::move(p));
    t.declare("session.lap", SWOFTY_POINT_NOTIFY, 1, true, false);
    t.declare("test.cancel", SWOFTY_POINT_CANCEL, 1, true, false);
    Tag bad{"lapticker"}, good{"other"};
    bad.fault = true;
    uint32_t tok = 0;
    t.attach("someone.lapticker", "session.lap", SWOFTY_PHASE_AFTER, 10, "", record, &bad, &tok);
    t.attach("someone.else", "session.lap", SWOFTY_PHASE_AFTER, 0, "", record, &good, &tok);
    calls.clear();
    Payload pl{sizeof pl, 1, 0};
    int r = t.fire("", "session.lap", &pl, nullptr);
    expect(r == SWOFTY_ASSET_OK && joined() == "lapticker other", "a faulting after handler doesn't stop the others");
    expect(logged("[events] after handler from someone.lapticker faulted on session.lap (C++ exception: handler fault); "
                  "handler detached for this session"),
           "the fault is logged with the addon, the event and the phase");
    expect(listed.size() == 1 && listed[0] == "someone.lapticker after session.lap", "and listed for conflicts.json");
    expect(count_event("session.lap") == 1, "the fault line is not counted as a fire");
    calls.clear();
    t.fire("", "session.lap", &pl, nullptr);
    expect(joined() == "other", "the faulted handler is detached for the session");

    Tag crash{"crasher"};
    crash.fault = true;
    Tag after{"after"};
    t.attach("swofty.career-editor", "test.cancel", SWOFTY_PHASE_REPLACE, 0, "", record, &crash, &tok);
    t.attach("someone", "test.cancel", SWOFTY_PHASE_AFTER, 0, "", record, &after, &tok);
    calls.clear();
    std::string crashed;
    try {
        t.fire("", "test.cancel", &pl, nullptr);
    } catch (const TestCrash &c) {
        crashed = c.text;
    }
    expect(crashed == "Crash in swofty.career-editor's replace handler on test.cancel",
           "a faulting replace handler takes the crash path, naming the addon, the phase and the point");
    expect(joined() == "crasher", "nothing runs after it");
    t.declare("test.after", SWOFTY_POINT_NOTIFY, 1, true, false);
    t.attach("someone", "test.after", SWOFTY_PHASE_AFTER, 0, "", record, &after, &tok);
    calls.clear();
    expect(t.fire("", "test.after", &pl, nullptr) == SWOFTY_ASSET_OK && joined() == "after",
           "the table keeps working after the unwind");
}

static void test_addon_points()
{
    lines.clear();
    Table t(platform());
    t.declare_core();
    expect(t.define("swofty.progressive", "swofty.progressive.seat_change", 1, SWOFTY_POINT_CANCEL, true,
                    R"([{"owner":"swofty.progressive"}])") == SWOFTY_ASSET_OK,
           "define under the addon's own id");
    expect(t.define("someone", "swofty.progressive.other", 1, SWOFTY_POINT_CANCEL, true, "") == SWOFTY_ASSET_INVALID &&
               logged("an addon defines points under its own id (someone.*)"),
           "define under another addon's id is refused");
    expect(t.define("swofty.progressive", "career.loaded", 1, SWOFTY_POINT_NOTIFY, true, "") == SWOFTY_ASSET_INVALID,
           "an addon can't define a core point");
    Tag ticker{"ticker"}, swap{"swap"};
    swap.ret = 1;
    uint32_t tok = 0;
    t.attach("news.ticker", "swofty.progressive.seat_change", SWOFTY_PHASE_AFTER, 0, "", record, &ticker, &tok);
    Payload pl{sizeof pl, 1, 0};
    int rep = 0;
    expect(t.fire("someone", "swofty.progressive.seat_change", &pl, &rep) == SWOFTY_ASSET_INVALID,
           "only the defining addon fires its point");
    expect(t.fire("someone", "career.loaded", &pl, &rep) == SWOFTY_ASSET_INVALID, "an addon can't fire a core event");
    calls.clear();
    lines.clear();
    t.update([](Facts &f) {
        f.mode = "driver_career";
        f.content = "game/f1_career_2025";
    });
    t.fire("swofty.progressive", "swofty.progressive.seat_change", &pl, &rep);
    expect(calls.empty() &&
               logged("[events] swofty.progressive.seat_change fired outside its scope (content game/f1_career_2025); not "
                      "delivered"),
           "an event fired outside its declared scope isn't delivered");
    t.update([](Facts &f) {
        f.content = "swofty.progressive/f1_career_progressive";
        f.content_owner = "swofty.progressive";
        f.owners = {"swofty.progressive"};
    });
    t.attach("whatif", "swofty.progressive.seat_change", SWOFTY_PHASE_REPLACE, 0, "", record, &swap, &tok);
    t.fire("swofty.progressive", "swofty.progressive.seat_change", &pl, &rep);
    expect(joined() == "swap ticker" && rep == 1, "inside its scope it runs, and fire reports the replace");
    Tag loop{"loop"};
    loop.fire_again = 1;
    loop.t = &t;
    t.attach("whatif", "swofty.progressive.seat_change", SWOFTY_PHASE_BEFORE, 0, "", record, &loop, &tok);
    t.fire("swofty.progressive", "swofty.progressive.seat_change", &pl, &rep);
    expect(loop.fire_again == SWOFTY_ASSET_INVALID && logged("a handler fired the point it runs on"),
           "a handler can't fire the point it runs on");
    expect(t.define("x", "x.hook", 1, SWOFTY_POINT_CANCEL, false, "") == SWOFTY_ASSET_OK, "hooks::define is an addon point");
    expect(t.live("x.hook", nullptr) == SWOFTY_ASSET_OK && t.live("nobody.point", nullptr) == SWOFTY_ASSET_UNAVAILABLE,
           "live: ok when defined, unavailable when not");
    expect(t.detach(tok) == SWOFTY_ASSET_OK && t.detach(tok) == SWOFTY_ASSET_INVALID, "detach once");
    expect(t.drop_module(0) == 0, "drop_module ignores module 0");
}

static void test_log_lines()
{
    lines.clear();
    Table t(platform());
    t.declare_core();
    Payload pl{sizeof pl, 1, 0};
    for (int i = 0; i < 3; i++)
        t.fire("", "career.loaded", &pl, nullptr);
    expect(count_event("career.loaded") == 3, "every fire of a core event logs one line, even with 0 handlers");
    expect(logged("[events] career.loaded (unknown): 0 handlers"), "the 0 handlers line names the context");
    for (int i = 0; i < 3; i++)
        t.fire("", "flow.state", &pl, nullptr);
    expect(count_event("flow.state") == 1 && logged("[events] flow.state (unknown): first fire, 0 handlers; later fires are not logged"),
           "a hot event logs its first fire only");
    t.define("a", "a.point", 1, SWOFTY_POINT_CANCEL, false, "");
    for (int i = 0; i < 3; i++)
        t.fire("a", "a.point", &pl, nullptr);
    size_t hooks = 0;
    for (auto &l : lines)
        hooks += l.rfind("[hooks] a.point: first fire", 0) == 0;
    expect(hooks == 1, "an addon hook point logs its first fire only, under [hooks]");
    expect(count_event("career") == 0 && count_event("career.load") == 0, "the parser doesn't match a prefix of the id");
}

// The game point: a harmless function in this binary.
struct AddPayload {
    uint32_t size, payload_version;
    int a, b, result;
};

extern "C" __attribute__((noinline)) int harmless_add(int a, int b)
{
    volatile int x = a;
    volatile int y = b;
    for (volatile int i = 0; i < 1; i = i + 1)
        x = x + 0;
    return x + y;
}

static int (*volatile add_fn)(int, int) = harmless_add;

static Table *game_table;
static GamePoint *game_point;
static int detour_add(int a, int b)
{
    AddPayload p{sizeof p, 1, a, b, 0};
    game_point->run(*game_table, p, [&] { p.result = game_point->original<int>(p.a, p.b); });
    return p.result;
}

static int add_before(void *, SwoftyHookCall *c)
{
    static_cast<AddPayload *>(c->payload)->a *= 2;
    return 0;
}

static int add_replace(void *user, SwoftyHookCall *c)
{
    if (!*static_cast<bool *>(user))
        return 0;
    static_cast<AddPayload *>(c->payload)->result = -1;
    return 1;
}

static volatile int add_after_result;
static int add_after(void *, SwoftyHookCall *c)
{
    add_after_result = static_cast<AddPayload *>(c->payload)->result;
    return 0;
}

static void test_game_point()
{
    lines.clear();
    Table t(platform());
    game_table = &t;
    std::string overwrite, occupied;
    std::vector<std::string> conflicts;
    GameOps ops;
    ops.log = [](const std::string &s) { lines.push_back(s); };
    ops.resolve = [](const std::string &b) { return b == "harmless_add" ? reinterpret_cast<uintptr_t>(&harmless_add) : 0; };
    ops.overwritten_by = [&](const std::string &, uintptr_t) { return overwrite; };
    ops.occupied_by = [&](uintptr_t) { return occupied; };
    ops.conflict = [&](const std::string &point, const std::string &by) { conflicts.push_back(point + " " + by); };
    GamePoint gp("test.add", "harmless_add", reinterpret_cast<void *>(&detour_add), ops);
    game_point = &gp;
    unsigned char prologue[8];
    memcpy(prologue, reinterpret_cast<void *>(&harmless_add), sizeof prologue);
    auto untouched = [&] { return !memcmp(prologue, reinterpret_cast<void *>(&harmless_add), sizeof prologue); };

    t.declare_game("test.add", SWOFTY_POINT_CANCEL, 1, &gp);
    expect(untouched() && add_fn(2, 3) == 5 && !gp.installed(), "no subscriber: the function is untouched");
    bool replace = false;
    uint32_t before = 0, rep = 0, after = 0;
    t.attach("swofty.progressive", "test.add", SWOFTY_PHASE_BEFORE, 0, "", add_before, nullptr, &before);
    t.attach("swofty.career-editor", "test.add", SWOFTY_PHASE_REPLACE, 0, "", add_replace, &replace, &rep);
    t.attach("someone", "test.add", SWOFTY_PHASE_AFTER, 0, "", add_after, nullptr, &after);
    expect(!gp.installed() && untouched() && t.live("test.add", nullptr) == SWOFTY_ASSET_LATE,
           "a subscriber before the window stage: not installed yet, live says late");
    t.arm();
    expect(gp.installed() && !untouched() && t.live("test.add", nullptr) == SWOFTY_ASSET_OK,
           "the window stage installs the detour, live says ok");
    add_after_result = 0;
    expect(add_fn(2, 3) == 7 && add_after_result == 7, "before changes the input, the game's code runs, after sees it");
    replace = true;
    expect(add_fn(2, 3) == -1, "a replace handler stands in for the game's code");
    replace = false;
    t.detach(before);
    t.detach(rep);
    expect(gp.installed(), "still installed while one subscriber is left");
    t.detach(after);
    expect(!gp.installed() && untouched() && add_fn(2, 3) == 5 && t.live("test.add", nullptr) == SWOFTY_ASSET_UNAVAILABLE,
           "the last subscriber leaving uninstalls it and restores the bytes");
    t.attach("late.addon", "test.add", SWOFTY_PHASE_BEFORE, 0, "", add_before, nullptr, &before);
    expect(gp.installed() && add_fn(2, 3) == 7 && logged("[hooks] test.add live on harmless_add (installed late)"),
           "a first subscriber after the window stage installs it at once");
    t.detach(before);
    occupied = "the wrap on harmless_add";
    t.attach("late.addon", "test.add", SWOFTY_PHASE_BEFORE, 0, "", add_before, nullptr, &before);
    expect(!gp.installed() && untouched() && logged("installed while the game runs, it overlaps the wrap on harmless_add"),
           "installed late, it refuses itself when something already sits on the function");
    t.detach(before);
    occupied.clear();

    lines.clear();
    Table t2(platform());
    game_table = &t2;
    overwrite = "old_addon.dll";
    GamePoint gp2("test.add", "harmless_add", reinterpret_cast<void *>(&detour_add), ops);
    game_point = &gp2;
    t2.declare_game("test.add", SWOFTY_POINT_CANCEL, 1, &gp2);
    t2.attach("swofty.progressive", "test.add", SWOFTY_PHASE_BEFORE, 0, "", add_before, nullptr, &before);
    t2.attach("swofty.career-editor", "test.add", SWOFTY_PHASE_AFTER, 0, "", add_after, nullptr, &after);
    t2.arm();
    expect(!gp2.installed() && untouched() && add_fn(2, 3) == 5 && gp2.state() == GamePoint::State::yielded,
           "a raw overwrite of the function wins: the point is not installed and the function is left to it");
    expect(logged("[hooks] test.add unavailable: old_addon.dll overwrites harmless_add; subscribers swofty.progressive, "
                  "swofty.career-editor not run (the launcher lists this as a conflict)"),
           "the point says who overwrites it and which subscribers lose it");
    expect(conflicts.size() == 1 && conflicts[0] == "test.add old_addon.dll", "and lists the conflict");
    expect(t2.live("test.add", nullptr) == SWOFTY_ASSET_UNAVAILABLE, "live says unavailable");
}

struct History {
    uint32_t size;
    int (*career_year)(void);
    int (*follow_mode)(void);
};
static int year_2017() { return 2017; }

static std::vector<std::string> service_calls;
static void got(void *user, const void *table, size_t size, uint32_t version)
{
    service_calls.push_back(std::string(static_cast<const char *>(user)) + " v" + std::to_string(version) + " " +
                            std::to_string(size) + " " +
                            std::to_string(static_cast<const History *>(table)->career_year()));
}

static void test_services()
{
    lines.clear();
    Registry r([](const std::string &s) { lines.push_back(s); });
    static const History v1{sizeof(uint32_t) + sizeof(void *), year_2017, nullptr};
    static const History v2{sizeof(History), year_2017, year_2017};
    r.when("first", "progressive.history", 1, sizeof(uint32_t) + sizeof(void *), got, (void *)"first");
    r.when("second", "progressive.history", 1, sizeof(uint32_t) + sizeof(void *), got, (void *)"second");
    r.when("needs-v2", "progressive.history", 2, sizeof(History), got, (void *)"needs-v2");
    expect(service_calls.empty(), "when before publish waits");
    r.publish("swofty.progressive", "progressive.history", 1, &v1, v1.size);
    expect(service_calls.size() == 2 && service_calls[0] == "first v1 12 2017" && service_calls[1] == "second v1 12 2017",
           "publish runs the waiters in the order they asked");
    expect(logged("progressive.history v1 (12 bytes) from swofty.progressive is older than needs-v2 needs"),
           "a too-old table is logged for the waiter it can't serve");
    r.when("late", "progressive.history", 1, 12, got, (void *)"late");
    expect(service_calls.size() == 3 && service_calls[2] == "late v1 12 2017", "when after publish runs at once");
    expect(r.publish("someone", "progressive.history", 3, &v2, v2.size) == SWOFTY_ASSET_INVALID,
           "another addon can't publish the same name");
    auto missing = r.missing();
    expect(missing.size() == 1 && missing[0].find("needs-v2 waits for progressive.history v2+") != std::string::npos,
           "the end-of-load report names the consumer still waiting");
    r.publish("swofty.progressive", "progressive.history", 2, &v2, v2.size);
    expect(service_calls.size() == 4 && service_calls[3] == "needs-v2 v2 24 2017",
           "a newer version from the same addon serves the waiter that needed it, once");
    expect(r.missing().empty(), "nothing is left waiting");
    expect(r.publish("swofty.progressive", "progressive.history", 1, &v1, v1.size) == SWOFTY_ASSET_INVALID,
           "an older version can't replace a newer one");
    uint32_t v = 0;
    expect(r.published("progressive.history", &v) && v == 2 && !r.published("nobody.service"), "published");
}

static int c_sound(void *, SwoftyHookCall *c)
{
    auto *b = static_cast<SwoftyEvBundleResolve *>(c->payload);
    if (b->size == sizeof *b && b->car == 4 && !strcmp(b->vehicle_id, "game/williams_2026") &&
        !strcmp(b->switch_name_str, "williams") && !strcmp(b->engine_sound_id, "game/williams_2026"))
        b->set_engine_sound_fn(b->user, "mod.esp/v10_screamer");
    return 0;
}

static int cpp_sound(void *user, SwoftyHookCall *c)
{
    auto &b = *static_cast<swofty::sounds::events::BundleResolve *>(c->payload);
    *static_cast<std::string *>(user) = std::string(b.engine_sound().id());
    return 0;
}

static int cap_rating(void *, SwoftyHookCall *c)
{
    auto &f = *static_cast<swofty::cars::events::UpgradesFit *>(c->payload);
    if (f.vehicle().id() == "game/haas_2026" && f.department == swofty::cars::Department::aero)
        f.rating = std::min(f.rating, 0.95f);
    return 0;
}

static int scale_wing(void *, SwoftyHookCall *c)
{
    auto &r = *static_cast<swofty::cars::events::VtfRead *>(c->payload);
    if (r.section() == "F1WingDamage" && r.field() == "DamageScale" && r.upgrade().empty() && r.car.index() == 7)
        r.value *= 2;
    return 0;
}

static int add_parts(void *, SwoftyHookCall *c)
{
    auto &p = *static_cast<const swofty::cars::events::PartsRegister *>(c->payload);
    if (p.vehicle().id() == "game/mclaren_2026") {
        p.add({.node = "front_wing_endplate_l", .detach_at = 0.4f});
        p.add({.node = "mirror_l", .detach_at = 0.6f, .debris = false});
        p.add({.node = ""});
    }
    return 0;
}

static void test_payloads()
{
    Table t(platform());
    t.declare_core();
    auto fire = [&](auto &e) {
        using E = std::remove_reference_t<decltype(e)>;
        e.size = uint32_t(sizeof(E));
        e.payload_version = E::version;
        int rep = 0;
        t.fire("", E::point, &e, &rep);
        return rep == 0;
    };
    uint32_t tok = 0;
    std::string seen;
    t.attach("c-addon", "audio.bundle.resolve", SWOFTY_PHASE_BEFORE, 10, "", c_sound, nullptr, &tok);
    t.attach("cpp-addon", "audio.bundle.resolve", SWOFTY_PHASE_AFTER, 0, "", cpp_sound, &seen, &tok);
    std::string sound = "game/williams_2026";
    std::string vehicle = "game/williams_2026";
    payloads::bundle_resolve(fire, 4, vehicle.c_str(), "williams", sound);
    expect(sound == "mod.esp/v10_screamer" && seen == sound,
           "audio.bundle.resolve: a C handler sets the sound and a C++ handler reads it back");

    t.attach("a", "car.upgrades.fit", SWOFTY_PHASE_BEFORE, 0, "", cap_rating, nullptr, &tok);
    float rating = 0.99f;
    payloads::upgrades_fit(fire, 3, "game/haas_2026", swofty::cars::Department::aero, 80, rating);
    expect(rating == 0.95f, "car.upgrades.fit: a before handler caps the rating");

    t.attach("a", "car.vtf.read", SWOFTY_PHASE_BEFORE, 0, "", scale_wing, nullptr, &tok);
    float value = 0.25f;
    payloads::vtf_read(fire, swofty::cars::live::Car(7), "F1WingDamage", "DamageScale", nullptr, value);
    expect(value == 0.5f, "car.vtf.read: a before handler reads the section and changes the value");

    std::vector<std::string> parts;
    t.attach("a", "car.parts.register", SWOFTY_PHASE_AFTER, 0, "", add_parts, nullptr, &tok);
    payloads::parts_register(fire, 2, "game/mclaren_2026", nullptr, [&](const char *node, float at, bool debris) {
        parts.push_back(std::string(node) + "@" + std::to_string(int(at * 10)) + (debris ? "+" : "-"));
        return int32_t(SWOFTY_ASSET_OK);
    });
    expect(parts == std::vector<std::string>{"front_wing_endplate_l@4+", "mirror_l@6-"},
           "car.parts.register: add() reaches the core's sink and an empty node is refused");
}

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    test_order();
    test_kinds();
    test_selectors();
    test_context();
    test_faults();
    test_addon_points();
    test_log_lines();
    test_game_point();
    test_services();
    test_payloads();
    printf("%s: %d failure(s)\n", fails ? "FAIL" : "ok", fails);
    return fails ? 1 : 0;
}
