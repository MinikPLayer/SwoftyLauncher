// Host test of the test agent's protocol and runner (src/core/testagent/agent_logic.hpp) and of the
// SWOFTY_TEST coroutine runner (packages/sdk/include/swofty/test.hpp).
#include "../src/core/testagent/agent_logic.hpp"

#include <swofty/test.hpp>

#include <cstdio>
#include <map>

namespace ta = testagent;

static int fails;
static void expect(bool ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  :" : "FAIL:", what);
    fails += !ok;
}

static bool has(const std::string &s, const char *part)
{
    return s.find(part) != std::string::npos;
}

struct FakeGame : ta::Game {
    bool home = false;
    std::vector<std::string> routes, log;
    std::vector<ta::PadState> pads;
    int capture_polls = 0;
    std::string screen = "|fe_title||";
    int test_steps = 0;

    ta::Outcome hello() override { return ta::Outcome::ok("{\"v\":1}"); }
    ta::Outcome route(const std::string &n) override
    {
        if (!home)
            return ta::Outcome::unavailable("no home view model");
        routes.push_back(n);
        return ta::Outcome::ok();
    }
    ta::Outcome skip(int d, int s) override { return ta::Outcome::ok("{\"d\":" + std::to_string(d) + ",\"s\":" + std::to_string(s) + "}"); }
    ta::Outcome advance(int) override
    {
        if (!hq_vm)
            return ta::Outcome::unavailable("no Team HQ view model");
        ++advances;
        return ta::Outcome::ok();
    }
    ta::Outcome ai_drives_player(bool on) override { return ta::Outcome::ok(on ? "1" : "0"); }
    ta::Outcome read(const std::string &p) override
    {
        if (p == "car[0].chassis.mass")
            return ta::Outcome::ok("{\"value\":798.5,\"type\":\"f32\"}");
        return ta::Outcome::unavailable("no reader for " + p);
    }
    ta::Outcome set(const std::string &, const json::Value &v) override { return ta::Outcome::ok(ta::num(v.n)); }
    ta::Outcome capture(const std::string &) override
    {
        capture_polls = 0;
        return ta::Outcome::ok();
    }
    ta::Outcome capture_poll() override
    {
        return ++capture_polls < 3 ? ta::Outcome::pending() : ta::Outcome::ok("{\"file\":\"frames/x.png\"}");
    }
    ta::Outcome save_now() override { return ta::Outcome::unavailable("no save trigger"); }
    int save_polls = 0, advance_polls = 0;
    ta::Outcome save_poll() override { return ++save_polls < 3 ? ta::Outcome::pending() : ta::Outcome::ok("{\"writes\":2}"); }
    ta::Outcome advance_poll() override
    {
        return ++advance_polls < 3 ? ta::Outcome::pending() : ta::Outcome::ok("{\"from_day\":10,\"to_day\":17}");
    }
    ta::Outcome quit() override { return ta::Outcome::ok(); }
    ta::Outcome pad(const ta::PadState &p) override
    {
        pads.push_back(p);
        return ta::Outcome::ok();
    }
    std::string phase() override { return screen; }
    ta::Outcome test_start(const std::string &fn) override
    {
        return fn == "t" ? ta::Outcome::ok() : ta::Outcome::unavailable("no test DLL registers " + fn);
    }
    ta::Outcome test_poll() override { return ++test_steps < 2 ? ta::Outcome::pending() : ta::Outcome::ok("{\"expects\":[]}"); }
    uint64_t log_lines(uint64_t from, std::vector<std::string> &out) override
    {
        if (from == UINT64_MAX)
            return log.size();
        for (uint64_t i = from; i < log.size(); i++)
            out.push_back(log[i]);
        return log.size();
    }

    bool sandbox = false, title_bound = true, engaged = false, fe_career = false, resume = false, hq_vm = false;
    int presses = 0, tiles = 0, cards = 0, advances = 0;
    std::string tile_error, card_error, session_why = "the garage's go-to-track step is not found";
    std::vector<std::string> notes;

    ta::Outcome title_press() override
    {
        if (!title_bound)
            return ta::Outcome::unavailable("the title's sync point is not bound on this build");
        ++presses;
        return ta::Outcome::ok();
    }
    std::string title_state() override { return engaged ? "engaged" : "waiting: the request has not called back"; }
    ta::Outcome career_tile(const std::string &mode) override
    {
        if (!fe_career)
            return ta::Outcome::unavailable("no fe_career view model yet");
        ++tiles;
        if (!tile_error.empty())
            return ta::Outcome::fail(tile_error);
        return ta::Outcome::ok("{\"route\":\"solo_career_continue_" + std::string(mode == "driver_career" ? "driver" : "my_team") + "\"}");
    }
    ta::Outcome resume_press(const std::string &slot) override
    {
        if (!resume)
            return ta::Outcome::unavailable("the resume screen's cards are not filled yet");
        ++cards;
        if (!card_error.empty())
            return ta::Outcome::fail(card_error);
        return ta::Outcome::ok("{\"index\":0,\"slot\":" + (slot == "newest" ? std::string("3") : slot) + "}");
    }
    ta::Outcome session_start() override { return ta::Outcome::unavailable(session_why); }
    bool sandboxed() override { return sandbox; }
    void note(const std::string &l) override { notes.push_back(l); }
};

static std::vector<std::string> run(ta::Runner &r, uint64_t &now, int ticks, uint64_t step = 10)
{
    std::vector<std::string> out;
    for (int i = 0; i < ticks; i++, now += step)
        r.tick(now, out);
    while (out.size() < 8)
        out.push_back("");
    return out;
}

static size_t answered(const std::vector<std::string> &out)
{
    size_t n = 0;
    for (auto &l : out)
        n += !l.empty();
    return n;
}

static void submit(ta::Runner &r, const std::string &line)
{
    r.submit(ta::parse_command(line));
}

static void parsing()
{
    ta::Command c = ta::parse_command("{\"seq\":3,\"cmd\":\"route\",\"args\":{\"route\":\"career\"},\"timeout_ms\":500}");
    expect(c.seq == 3 && c.cmd == "route" && c.args["route"].str() == "career" && c.timeout_ms == 500 && c.error.empty(),
           "a command line parses");
    expect(ta::parse_command("not json").error == "not a JSON object", "a broken line is an error");
    expect(ta::parse_command("{\"cmd\":\"x\"}").error == "missing seq", "a line without seq is an error");
    expect(ta::parse_command("{\"seq\":1,\"cmd\":\"wait\",\"timeout_ms\":999999999}").timeout_ms == ta::MAX_WAIT_MS,
           "time limits are capped");
    expect(ta::result_json(4, ta::Outcome::unavailable("no \"vm\""), 7) ==
               "{\"seq\":4,\"status\":\"unavailable\",\"error\":\"no \\\"vm\\\"\",\"ms\":7}",
           "a result line escapes its error");
    ta::CarPath p;
    std::string why;
    expect(ta::parse_car_path("car[player].f1_aero.lift_scale", p, why) && p.player && p.system == "f1_aero" &&
               p.field == "lift_scale",
           "car[player].system.field parses");
    expect(ta::parse_car_path("car[12].chassis.mass", p, why) && p.index == 12, "car[12] parses");
    expect(!ta::parse_car_path("car[x].chassis.mass", p, why) && has(why, "0 to 25"), "a bad car index is refused");
    uint16_t m;
    expect(ta::button("a", m) && m == 0x1000 && ta::button("rt", m) && ta::pad_of(m).rt == 255 && !ta::button("mfd", m),
           "buttons map to XInput masks");
}

static void runner()
{
    FakeGame g;
    ta::Runner r(g);
    uint64_t now = 1000;
    submit(r, "{\"seq\":1,\"cmd\":\"route\",\"args\":{\"route\":\"career\"}}");
    auto out = run(r, now, 1);
    expect(answered(out) == 1 && has(out[0], "\"seq\":1,\"status\":\"unavailable\",\"error\":\"no home view model\""),
           "route without a home view model answers unavailable, not a crash");
    g.home = true;
    submit(r, "{\"seq\":2,\"cmd\":\"route\",\"args\":{\"route\":\"career\"}}");
    submit(r, "{\"seq\":3,\"cmd\":\"read\",\"args\":{\"path\":\"car[0].chassis.mass\"}}");
    out = run(r, now, 1);
    expect(answered(out) == 1 && has(out[0], "\"seq\":2,\"status\":\"ok\"") && g.routes.size() == 1,
           "one command per tick: the route ran");
    out = run(r, now, 1);
    expect(answered(out) == 1 && has(out[0], "\"value\":{\"value\":798.5"), "then the read answered with its value");
    submit(r, "{\"seq\":4,\"cmd\":\"skip\",\"args\":{\"to\":\"session\"}}");
    submit(r, "{\"seq\":5,\"cmd\":\"skip\",\"args\":{\"to\":\"sideways\"}}");
    out = run(r, now, 2);
    expect(has(out[0], "{\"d\":4,\"s\":15}"), "skip to session posts destination 4, the race by default");
    expect(has(out[1], "\"status\":\"error\"") && has(out[1], "sideways"), "an unknown skip is an error");

    submit(r, "{\"seq\":6,\"cmd\":\"wait\",\"args\":{\"event\":\"race.start\"},\"timeout_ms\":100}");
    out = run(r, now, 3);
    expect(!answered(out) && r.busy(), "a wait holds the runner while the event has not fired");
    r.fires().fired("race.start");
    out = run(r, now, 1);
    expect(answered(out) == 1 && has(out[0], "\"seq\":6,\"status\":\"ok\"") && has(out[0], "\"fires\":1"),
           "the wait answers on the fire");
    submit(r, "{\"seq\":7,\"cmd\":\"wait\",\"args\":{\"event\":\"race.start\"},\"timeout_ms\":50}");
    out = run(r, now, 10);
    expect(answered(out) == 1 && has(out[0], "\"status\":\"timeout\""), "a second wait needs a second fire, and times out");
    r.fires().fired("lap");
    submit(r, "{\"seq\":8,\"cmd\":\"wait\",\"args\":{\"event\":\"lap\"}}");
    out = run(r, now, 1);
    expect(answered(out) == 1 && has(out[0], "\"status\":\"ok\""), "a fire before the wait counts");

    submit(r, "{\"seq\":9,\"cmd\":\"pad\",\"args\":{\"press\":\"a\",\"then\":\"right\",\"ms\":30}}");
    out = run(r, now, 20);
    expect(answered(out) == 1 && has(out[0], "\"seq\":9,\"status\":\"ok\""), "a press and a then press finish");
    expect(g.pads.size() == 4 && g.pads[0].buttons == 0x1000 && g.pads[1].buttons == 0 && g.pads[2].buttons == 0x0008 &&
               g.pads[3].buttons == 0,
           "the pad holds a, lets go, holds right, lets go");
    submit(r, "{\"seq\":10,\"cmd\":\"pad\",\"args\":{\"press\":\"mfd_fuel\"}}");
    out = run(r, now, 1);
    expect(has(out[0], "unknown button 'mfd_fuel'"), "an unknown button is an error");

    submit(r, "{\"seq\":11,\"cmd\":\"capture\",\"args\":{\"name\":\"team_hq\"}}");
    out = run(r, now, 5);
    expect(answered(out) == 1 && has(out[0], "frames/x.png"), "a capture answers once the frame is written");
    submit(r, "{\"seq\":12,\"cmd\":\"capture\",\"args\":{\"name\":\"../x\"}}");
    out = run(r, now, 1);
    expect(has(out[0], "\"status\":\"error\""), "a capture name with a path is refused");

    submit(r, "{\"seq\":13,\"cmd\":\"at\",\"args\":{\"phase\":\"home\"},\"timeout_ms\":1000}");
    out = run(r, now, 2);
    expect(!answered(out), "at waits for its screen");
    g.screen = "|fe_home|StateFrontend|";
    out = run(r, now, 1);
    expect(answered(out) == 1 && has(out[0], "\"status\":\"ok\""), "at answers on fe_home for home");

    g.log = {"[core] one"};
    submit(r, "{\"seq\":14,\"cmd\":\"log\",\"args\":{\"pattern\":\"\\\\[rnd\\\\] blocked\"},\"timeout_ms\":1000}");
    out = run(r, now, 2);
    g.log.push_back("[rnd] blocked tab");
    out = run(r, now, 1);
    expect(answered(out) == 1 && has(out[0], "[rnd] blocked tab"), "a log wait matches a line written after it started");
    submit(r, "{\"seq\":15,\"cmd\":\"log\",\"args\":{\"pattern\":\"(\"}}");
    out = run(r, now, 1);
    expect(has(out[0], "not a regular expression"), "a broken pattern is an error");

    submit(r, "{\"seq\":16,\"cmd\":\"advance\",\"args\":{\"days\":7}}");
    submit(r, "{\"seq\":17,\"cmd\":\"save\",\"args\":{\"what\":\"now\"}}");
    submit(r, "{\"seq\":18,\"cmd\":\"run_test\",\"args\":{\"fn\":\"t\"}}");
    submit(r, "{\"seq\":19,\"cmd\":\"run_test\",\"args\":{\"fn\":\"nope\"}}");
    submit(r, "{\"seq\":20,\"cmd\":\"bogus\"}");
    out = run(r, now, 8);
    expect(answered(out) == 5, "every command answers");
    expect(has(out[0], "\"status\":\"error\"") && has(out[0], "advance runs only in the save sandbox") &&
               has(out[1], "save now runs only in the save sandbox"),
           "advance and save now are refused outside the save sandbox");
    expect(has(out[2], "\"seq\":18,\"status\":\"ok\""), "run_test answers when the test ends");
    expect(has(out[3], "no test DLL registers nope"), "an unknown test function is unavailable");
    expect(has(out[4], "unknown command 'bogus'"), "an unknown command is an error");
}

static void flow(ta::Runner &r, const char *name)
{
    r.fires().fired(std::string("flow:") + name);
}

static_assert(ta::CareerTimes{}.title == 15000, "the title wait is 15 s, then the CLI presses the pad");

static ta::Runner &fast(ta::Runner &r)
{
    auto &t = r.career_times();
    t.title = 400, t.route = 300, t.tile = 300, t.card = 300, t.team_hq = 600, t.advance = 300, t.hub = 600, t.session = 300;
    t.retry = 50;
    return r;
}

static void career()
{
    {
        FakeGame g;
        ta::Runner r(g);
        uint64_t now = 1000;
        submit(r, "{\"seq\":1,\"cmd\":\"career\",\"args\":{\"op\":\"load\",\"slot\":\"newest\",\"at\":\"team_hq\"}}");
        auto out = run(r, now, 1);
        expect(has(out[0], "\"status\":\"error\"") && has(out[0], "career load runs only in the save sandbox"),
               "career load is refused outside the save sandbox");
        g.sandbox = true;
        submit(r, "{\"seq\":2,\"cmd\":\"career\",\"args\":{\"slot\":\"x1\"}}");
        submit(r, "{\"seq\":3,\"cmd\":\"career\",\"args\":{\"at\":\"pit_lane\"}}");
        submit(r, "{\"seq\":4,\"cmd\":\"career\",\"args\":{\"mode\":\"grand_prix\"}}");
        out = run(r, now, 3);
        expect(has(out[0], "a career slot is a number or newest") && has(out[1], "team_hq, weekend_hub or session") &&
                   has(out[2], "driver_career or my_team_career"),
               "a bad slot, place or mode is an error");
    }
    {
        FakeGame g;
        g.sandbox = true;
        ta::Runner r(g);
        fast(r);
        uint64_t now = 1000;
        submit(r, "{\"seq\":1,\"cmd\":\"career\",\"args\":{\"slot\":3,\"at\":\"team_hq\",\"mode\":\"my_team_career\"}}");
        auto out = run(r, now, 5);
        expect(!answered(out) && g.presses >= 1, "at the title the load presses it without input and waits");
        g.engaged = true;
        out = run(r, now, 3);
        expect(!answered(out) && g.routes.empty(), "then route career waits for the home view model");
        g.home = true;
        out = run(r, now, 6);
        expect(g.routes.size() == 1 && g.routes[0] == "career" && g.tiles == 0, "route career fires once the home view model is there");
        g.fe_career = true;
        out = run(r, now, 6);
        expect(g.tiles == 1 && g.cards == 0, "the career tile's action runs once fe_career is up");
        g.resume = true;
        out = run(r, now, 6);
        expect(g.cards == 1 && !answered(out), "the resume card is pressed, then the load waits for Team HQ");
        flow(r, "StateTeamHQ");
        out = run(r, now, 1);
        expect(answered(out) == 1 && has(out[0], "\"status\":\"ok\"") && has(out[0], "\"at\":\"team_hq\"") &&
                   has(out[0], "\"slot\":\"3\"") && has(out[0], "\"card\":{\"index\":0"),
               "StateTeamHQ answers the load");
        expect(g.notes.size() == 1 && g.notes[0] == "[test] career ready at team_hq (slot 3)",
               "the agent logs the line the CLI waits for");
    }
    {
        FakeGame g;
        g.sandbox = g.engaged = g.home = g.fe_career = g.resume = true;
        ta::Runner r(g);
        fast(r);
        uint64_t now = 1000;
        flow(r, "StateTeamHQ");
        submit(r, "{\"seq\":1,\"cmd\":\"career\",\"args\":{\"at\":\"weekend_hub\"}}");
        auto out = run(r, now, 4);
        expect(!answered(out) && g.cards == 1, "a Team HQ entered before the press does not count");
        flow(r, "StateTeamHQ");
        out = run(r, now, 3);
        expect(!answered(out) && g.advances == 0, "weekend_hub: the advance waits for Team HQ's advance view model");
        g.hq_vm = true;
        out = run(r, now, 3);
        expect(g.advances == 1 && !answered(out), "then advances once and waits for the weekend");
        flow(r, "StateSession");
        out = run(r, now, 1);
        expect(has(out[0], "\"status\":\"ok\"") && has(out[0], "\"at\":\"weekend_hub\"") &&
                   g.notes.back() == "[test] career ready at weekend_hub (slot newest)",
               "StateSession answers weekend_hub");
    }
    {
        FakeGame g;
        g.sandbox = g.engaged = g.home = g.fe_career = g.resume = g.hq_vm = true;
        ta::Runner r(g);
        fast(r);
        uint64_t now = 1000;
        submit(r, "{\"seq\":1,\"cmd\":\"career\",\"args\":{\"at\":\"session\"}}");
        auto out = run(r, now, 3);
        flow(r, "StateTeamHQ");
        out = run(r, now, 3);
        flow(r, "StateSession");
        out = run(r, now, 2);
        expect(has(out[0], "\"status\":\"unavailable\"") &&
                   has(out[0], "career load at session: the garage's go-to-track step is not found"),
               "at session answers unavailable with why");
    }
    struct Case {
        const char *what, *step;
        void (*set)(FakeGame &);
    } cases[] = {
        {"a title that never passes times out naming the press", "the title screen's press did not happen within 0 s",
         [](FakeGame &) {}},
        {"an unbound title falls back to the pad and says so", "pressing the virtual pad's a instead",
         [](FakeGame &g) { g.title_bound = false; }},
        {"no home view model times out naming route career", "route career from the main menu did not happen within 0 s (no home view model)",
         [](FakeGame &g) { g.engaged = true; }},
        {"no fe_career times out naming the tile", "the career tile's continue action did not happen", [](FakeGame &g) { g.engaged = g.home = true; }},
        {"no cards time out naming the resume card", "the resume card's press did not happen", [](FakeGame &g) { g.engaged = g.home = g.fe_career = true; }},
        {"no Team HQ times out naming it", "the career loading into Team HQ (StateTeamHQ) did not happen",
         [](FakeGame &g) { g.engaged = g.home = g.fe_career = g.resume = true; }},
    };
    for (auto &k : cases) {
        FakeGame g;
        g.sandbox = true;
        k.set(g);
        ta::Runner r(g);
        fast(r);
        uint64_t now = 1000;
        submit(r, "{\"seq\":1,\"cmd\":\"career\",\"args\":{}}");
        auto out = run(r, now, 200);
        expect(answered(out) == 1 && has(out[0], "\"status\":\"timeout\"") && has(out[0], k.step), k.what);
    }
    {
        FakeGame g;
        g.sandbox = g.engaged = g.home = g.fe_career = true;
        g.tile_error = "the My Team tile has no saved career (its action routes to my_team_career)";
        ta::Runner r(g);
        fast(r);
        uint64_t now = 1000;
        submit(r, "{\"seq\":1,\"cmd\":\"career\",\"args\":{}}");
        auto out = run(r, now, 20);
        expect(has(out[0], "\"status\":\"error\"") && has(out[0], "the career tile's continue action: the My Team tile has no saved"),
               "a tile without a save fails at once with its step");
    }
    {
        FakeGame g;
        ta::Runner r(g);
        fast(r);
        uint64_t now = 1000;
        submit(r, "{\"seq\":1,\"cmd\":\"title\"}");
        auto out = run(r, now, 3);
        expect(!answered(out) && g.presses >= 1, "title presses without input and waits");
        flow(r, "StateFrontend");
        out = run(r, now, 1);
        expect(has(out[0], "\"seq\":1,\"status\":\"ok\"") && g.notes.back() == "[test] title passed without input",
               "the front end flow answers title");
        g.title_bound = false;
        ta::Runner r2(g);
        submit(r2, "{\"seq\":2,\"cmd\":\"title\"}");
        out = run(r2, now, 1);
        expect(has(out[0], "\"status\":\"unavailable\"") && has(out[0], "not bound"),
               "an unbound title answers unavailable, so the CLI falls back to the pad");
    }
}

static void sandboxed_ops()
{
    FakeGame g;
    g.sandbox = true;
    ta::Runner r(g);
    uint64_t now = 1000;
    submit(r, "{\"seq\":1,\"cmd\":\"advance\",\"args\":{\"days\":7}}");
    auto out = run(r, now, 2);
    expect(has(out[0], "\"status\":\"unavailable\"") && has(out[0], "no Team HQ view model"),
           "in the sandbox, advance without Team HQ answers unavailable");
    g.hq_vm = true;
    submit(r, "{\"seq\":2,\"cmd\":\"advance\",\"args\":{\"days\":7}}");
    out = run(r, now, 2);
    expect(!answered(out) && g.advances == 1, "advance presses once and waits for the advance to stop");
    out = run(r, now, 3);
    expect(answered(out) == 1 && has(out[0], "\"to_day\":17"), "then answers with the days");
    submit(r, "{\"seq\":3,\"cmd\":\"save\",\"args\":{\"what\":\"now\"}}");
    out = run(r, now, 3);
    expect(has(out[0], "\"status\":\"unavailable\"") && has(out[0], "no save trigger"), "an unbound save now answers unavailable");
}

// The coroutine runner: a fake host answers the test's ops.
SWOFTY_TEST(waits_and_checks)(swofty::test::Context &t) -> swofty::test::Task
{
    bool at = co_await t.at("team_hq", 60);
    t.expect(at, "reached team_hq");
    t.expect(t.read<double>("car[0].chassis.mass") > 790, "the car is heavy enough");
    t.expect(t.read<bool>("ui.tab.visible") == false, "the tab is hidden");
    co_await t.route("laptop_rnd");
    t.log_text("routed");
}

SWOFTY_TEST(a_failing_op)(swofty::test::Context &t) -> swofty::test::Task
{
    bool ok = co_await t.log("never", 1);
    t.expect(!ok, "the log wait failed");
}

SWOFTY_TESTS()

struct Host {
    std::map<uint64_t, int> answers;
    std::vector<std::string> started, expects, logs;
    int polls = 0;
    int fail_ops = 0;
};

static void coroutines()
{
    Host h;
    SwoftyTestHost host{sizeof(SwoftyTestHost), SWOFTY_TEST_HOST_VERSION, &h,
                        [](void *s, const char *line) -> uint64_t {
                            auto *h = static_cast<Host *>(s);
                            h->started.push_back(line);
                            return h->started.size();
                        },
                        [](void *s, uint64_t op, char *out, size_t cap) -> int {
                            auto *h = static_cast<Host *>(s);
                            if (++h->polls % 2)
                                return 0;
                            snprintf(out, cap, "{\"seq\":%llu}", (unsigned long long)op);
                            return h->fail_ops ? 2 : 1;
                        },
                        [](void *s, int ok, const char *what) {
                            static_cast<Host *>(s)->expects.push_back(std::string(ok ? "+" : "-") + what);
                        },
                        [](void *s, const char *text) { static_cast<Host *>(s)->logs.push_back(text); },
                        [](void *, const char *path, char *out, size_t cap) -> int {
                            if (!std::strcmp(path, "car[0].chassis.mass"))
                                snprintf(out, cap, "{\"value\":798.5,\"type\":\"f32\"}");
                            else if (!std::strcmp(path, "ui.tab.visible"))
                                snprintf(out, cap, "{\"value\":false,\"type\":\"bool\"}");
                            else {
                                snprintf(out, cap, "no reader");
                                return 0;
                            }
                            return 1;
                        }};
    expect(std::string(SwoftyTestNames()) == "waits_and_checks\na_failing_op\n", "SWOFTY_TESTS lists both tests");
    expect(SwoftyTestStart("nope", &host) == nullptr, "an unknown test does not start");
    void *run = SwoftyTestStart("waits_and_checks", &host);
    int r = 0, steps = 0;
    while ((r = SwoftyTestStep(run)) == 0 && steps < 50)
        steps++;
    SwoftyTestEnd(run);
    expect(r == 1, "the test passes once its ops answer");
    expect(h.started.size() == 2 && has(h.started[0], "\"cmd\":\"at\"") && has(h.started[0], "\"timeout_ms\":60000") &&
               has(h.started[1], "\"route\":\"laptop_rnd\""),
           "each co_await started its agent command");
    expect(h.expects.size() == 3 && h.expects[0] == "+reached team_hq" && h.expects[1] == "+the car is heavy enough" &&
               h.expects[2] == "+the tab is hidden",
           "expectations reached the host in order");
    expect(h.logs.size() == 1 && h.logs[0] == "routed", "t.log_text reached the host");
    expect(steps >= 4, "the test waited across steps instead of blocking");

    Host f;
    f.fail_ops = 1;
    host.self = &f;
    run = SwoftyTestStart("a_failing_op", &host);
    steps = 0;
    while ((r = SwoftyTestStep(run)) == 0 && steps < 50)
        steps++;
    SwoftyTestEnd(run);
    expect(r == 2, "a failed op fails the test");
    expect(f.expects.size() == 2 && f.expects[0][0] == '-' && has(f.expects[0], "\"cmd\":\"log\"") &&
               f.expects[1] == "+the log wait failed",
           "the failed op is a failed expectation, and the test went on");
}

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    career();
    sandboxed_ops();
    parsing();
    runner();
    coroutines();
    printf("%s (%d failure(s))\n", fails ? "SOME FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
