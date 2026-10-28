// Host unit test for the events dispatcher. Compiles and runs natively (no game, no Wine): the
// clock is injected as a plain seconds value, so dt, the frame counter and the "seconds since
// attach" time are deterministic. run_events_test.sh builds and runs it.
#include "../src/core/events_dispatch.hpp"

#include <cstdio>
#include <string>
#include <vector>

using Dispatcher = runtime::ev::Dispatcher<>;
using runtime::ev::Phase;

static int fails;

static void expect(bool ok, const std::string &what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
    fails += !ok;
}

static std::vector<SwoftyEvent> g_events;
static void on_event(const SwoftyEvent *ev, void *user)
{
    if (user)
        *static_cast<int *>(user) += 1;
    g_events.push_back(*ev);
}

static std::vector<double> g_dts;
static void on_frame(double dt, void *user)
{
    if (user)
        *static_cast<int *>(user) += 1;
    g_dts.push_back(dt);
}

// A callback that unsubscribes itself and subscribes a fresh handler mid-dispatch, to prove the
// dispatcher does not deadlock or iterate a mutated vector.
static Dispatcher *g_reentrant_disp;
static uint32_t g_reentrant_token;
static int g_reentrant_added;
static void reentrant_frame(double, void *)
{
    g_reentrant_disp->unsubscribe(g_reentrant_token);
    uint32_t t = 0;
    g_reentrant_disp->on_frame(9, on_frame, &g_reentrant_added, &t);
}

// A profile scope that records which probe each callback ran under.
static std::vector<uint32_t> g_scopes;
static int g_open;
struct Recorder {
    explicit Recorder(uint32_t probe)
    {
        g_scopes.push_back(probe);
        g_open++;
    }
    ~Recorder() { g_open--; }
};

static int g_open_seen = -1;
static void open_frame(double, void *)
{
    g_open_seen = g_open;
}

int main()
{
    // Registration and result codes.
    {
        Dispatcher d;
        uint32_t t1 = 0, t2 = 0, t3 = 0;
        expect(d.subscribe(1, SWOFTY_EVENT_SESSION_START, on_event, nullptr, &t1) == SWOFTY_ASSET_OK && t1 != 0,
               "subscribe to a known event returns ok and a token");
        expect(d.on_frame(1, on_frame, nullptr, &t2) == SWOFTY_ASSET_OK && t2 != 0 && t2 != t1,
               "on_frame returns ok and a distinct token");
        expect(d.subscribe(1, "not.an.event", on_event, nullptr, &t3) == SWOFTY_ASSET_INVALID,
               "subscribe to an unknown event name is invalid");
        expect(d.subscribe(1, SWOFTY_EVENT_LAP, nullptr, nullptr, &t3) == SWOFTY_ASSET_INVALID,
               "subscribe with a null callback is invalid");
        expect(d.on_frame(1, nullptr, nullptr, &t3) == SWOFTY_ASSET_INVALID, "on_frame with a null callback is invalid");
        expect(d.unsubscribe(t1) == SWOFTY_ASSET_OK, "unsubscribe of a live token returns ok");
        expect(d.unsubscribe(t1) == SWOFTY_ASSET_INVALID, "unsubscribe of an already-dropped token is invalid");
        expect(d.unsubscribe(0xdead) == SWOFTY_ASSET_INVALID, "unsubscribe of an unknown token is invalid");
        expect(d.count() == 1, "one subscription remains after the drops");
    }

    // The flow and career events are known names and carry their payload pointer through.
    {
        Dispatcher d;
        g_events.clear();
        int hits = 0;
        uint32_t t = 0;
        for (const char *n : {SWOFTY_EVENT_FLOW_STATE, SWOFTY_EVENT_CAREER_LOADED, SWOFTY_EVENT_CAREER_UNLOADED,
                              SWOFTY_EVENT_CAREER_HUB, SWOFTY_EVENT_CAREER_STARTING})
            expect(d.subscribe(2, n, on_event, &hits, &t) == SWOFTY_ASSET_OK, std::string("subscribe ") + n);
        SwoftyCareerState s{};
        s.mode = SWOFTY_CAREER_DRIVER;
        expect(d.fire(SWOFTY_EVENT_CAREER_LOADED, SWOFTY_CAREER_DRIVER, 0, &s, 1.0) == 1 && hits == 1 &&
                   g_events.back().a == SWOFTY_CAREER_DRIVER && g_events.back().data == &s,
               "career.loaded reaches its subscriber with mode and state");
        expect(d.fire(SWOFTY_EVENT_CAREER_HUB, 1, SWOFTY_CAREER_DRIVER, &s, 1.5) == 1 && g_events.back().a == 1,
               "career.hub entered");
        SwoftyCareerStart start{};
        start.mode = SWOFTY_CAREER_MY_TEAM;
        start.blocked = 1;
        expect(d.fire(SWOFTY_EVENT_CAREER_STARTING, SWOFTY_CAREER_MY_TEAM, SWOFTY_CAREER_START_PICK, &start, 1.6) == 1 &&
                   g_events.back().a == SWOFTY_CAREER_MY_TEAM && g_events.back().data == &start,
               "career.starting reaches its subscriber with mode and start data");
        expect(d.drop_owner(2) == 5, "the five subscriptions drop with their module");
    }

    // dt computation across flow ticks.
    {
        Dispatcher d;
        int frames = 0;
        d.on_frame(1, on_frame, &frames, nullptr);
        g_dts.clear();
        expect(d.tick(100.0) == 0.0, "the first flow tick reports dt 0 (no previous frame)");
        expect(d.tick(100.5) == 0.5, "the next tick reports the elapsed seconds");
        expect(d.tick(100.75) == 0.25, "and the one after reports the new delta");
        expect(d.frame() == 3, "three ticks advanced the frame counter to 3");
        expect(frames == 3 && g_dts.size() == 3, "the frame callback fired once per tick");
        expect(d.tick(100.70) == 0.0, "a non-monotonic clock reading clamps dt to 0 rather than going negative");
    }

    // Event payload and selective delivery.
    {
        Dispatcher d;
        int starts = 0;
        d.subscribe(1, SWOFTY_EVENT_SESSION_START, on_event, &starts, nullptr);
        d.subscribe(1, SWOFTY_EVENT_RACE_START, on_event, nullptr, nullptr);
        d.tick(10.0);
        d.tick(10.0); // frame counter now 2
        g_events.clear();
        int payload = 0xabc;
        size_t n = d.fire(SWOFTY_EVENT_SESSION_START, 7, 8, &payload, 12.0);
        expect(n == 1 && starts == 1, "fire delivers only to subscribers of that event name");
        expect(g_events.size() == 1 && g_events[0].a == 7 && g_events[0].b == 8 && g_events[0].data == &payload,
               "the event carries the a/b integers and the data pointer");
        expect(g_events[0].frame == 2 && g_events[0].time == 2.0,
               "the event carries the current frame and seconds since the first clock reading");
        expect(d.fire("not.an.event", 0, 0, nullptr, 13.0) == 0, "firing an unknown event name delivers to no one");
    }

    // Per-owner (per-addon) removal.
    {
        Dispatcher d;
        d.subscribe(100, SWOFTY_EVENT_SESSION_START, on_event, nullptr, nullptr);
        d.on_frame(100, on_frame, nullptr, nullptr);
        d.subscribe(200, SWOFTY_EVENT_SESSION_START, on_event, nullptr, nullptr);
        expect(d.count() == 3, "three subscriptions across two owners");
        expect(d.drop_owner(100) == 2, "dropping an owner removes exactly its subscriptions");
        expect(d.count() == 1, "the other owner's subscription survives");
        g_events.clear();
        d.fire(SWOFTY_EVENT_SESSION_START, 0, 0, nullptr, 1.0);
        expect(g_events.size() == 1, "only the surviving owner's handler fires");
        expect(d.drop_owner(999) == 0, "dropping an owner with no subscriptions removes nothing");
    }

    // Re-entrancy: a callback mutates the dispatcher while it is dispatching.
    {
        Dispatcher d;
        g_reentrant_disp = &d;
        g_reentrant_added = 0;
        d.on_frame(5, reentrant_frame, nullptr, &g_reentrant_token);
        d.tick(1.0); // reentrant_frame runs, drops itself, adds a new on_frame handler
        expect(d.count() == 1, "the self-replacing handler left exactly one subscription");
        d.tick(2.0); // the newly added handler runs this time
        expect(g_reentrant_added == 1, "the handler added mid-dispatch fires on the following tick, not the current one");
    }

    // An addon registers during SwoftyAddonInit or SWOFTY_ON_LOAD, before the core's events setup
    // (a single window-stage load runs every ON_LOAD before runtime::ready()). The registration is
    // queued, and the frame callback fires once setup arms the flow tick.
    {
        Dispatcher d;
        int frames = 0, loads = 0;
        uint32_t tf = 0, tc = 0, td = 0, tl = 0;
        bool qf = false, qc = false, qd = false, ql = true;
        expect(d.phase() == Phase::pending, "a new dispatcher waits for the events setup");
        expect(d.on_frame(7, on_frame, &frames, &tf, &qf) == SWOFTY_ASSET_OK && tf && qf,
               "on_frame before setup is accepted and reported as queued");
        expect(d.subscribe(7, SWOFTY_EVENT_CAREER_LOADED, on_event, &loads, &tc, &qc) == SWOFTY_ASSET_OK && tc && qc,
               "career.loaded before setup is accepted and reported as queued");
        expect(d.subscribe(8, SWOFTY_EVENT_FLOW_STATE, on_event, nullptr, &td, &qd) == SWOFTY_ASSET_OK && qd,
               "flow.state from an addon whose init then fails is queued too");
        expect(d.drop_owner(8) == 1, "the failed addon's queued subscription drops with its module");
        auto held = d.arm(true);
        expect(held.size() == 2 && held[0].token == tf && !held[0].name && held[0].owner == 7 && held[1].token == tc &&
                   held[1].name == runtime::ev::canonical(SWOFTY_EVENT_CAREER_LOADED),
               "arm returns the queued frame callback and subscription in registration order, not the dropped one");
        expect(d.phase() == Phase::live, "setup with the flow tick hooked makes the dispatcher live");
        g_dts.clear();
        d.tick(50.0);
        d.tick(50.25);
        expect(frames == 2 && g_dts.size() == 2 && g_dts[1] == 0.25,
               "the frame callback queued before setup fires on every flow tick after it");
        SwoftyCareerState s{};
        expect(d.fire(SWOFTY_EVENT_CAREER_LOADED, SWOFTY_CAREER_MY_TEAM, 0, &s, 51.0) == 1 && loads == 1,
               "the career.loaded subscription queued before setup fires");
        expect(d.on_frame(7, on_frame, nullptr, &tl, &ql) == SWOFTY_ASSET_OK && !ql,
               "a registration after setup is live, not queued");
        expect(d.arm(false).empty() && d.phase() == Phase::live, "a second arm changes nothing");
    }

    // Setup that finds no flow tick: the queued registrations are reported, on_frame from then on is
    // unavailable, event subscriptions stay accepted (their sources are separate hooks).
    {
        Dispatcher d;
        int frames = 0;
        uint32_t tf = 0, ts = 0, tl = 0;
        bool qs = true;
        d.on_frame(3, on_frame, &frames, &tf);
        auto held = d.arm(false);
        expect(held.size() == 1 && held[0].token == tf && !held[0].name, "arm(false) reports the queued frame callback");
        expect(d.phase() == Phase::failed, "setup without the flow tick leaves the dispatcher failed");
        expect(d.on_frame(3, on_frame, &frames, &tl) == SWOFTY_ASSET_UNAVAILABLE && !tl,
               "on_frame after a failed setup is unavailable and leaves the token untouched");
        expect(d.subscribe(3, SWOFTY_EVENT_SESSION_START, on_event, nullptr, &ts, &qs) == SWOFTY_ASSET_OK && ts && !qs,
               "subscribe after a failed setup is still accepted");
        expect(d.unsubscribe(tf) == SWOFTY_ASSET_OK, "the queued frame callback can still be unsubscribed");
        expect(frames == 0, "nothing fired");
    }

    // Every callback runs inside a profile scope carrying the probe set for its token.
    {
        runtime::ev::Dispatcher<Recorder> d;
        uint32_t tf = 0, te = 0;
        d.on_frame(1, open_frame, nullptr, &tf);
        d.subscribe(1, SWOFTY_EVENT_SESSION_START, on_event, nullptr, &te);
        d.set_probe(tf, 11);
        d.set_probe(te, 12);
        d.arm(true);
        g_scopes.clear();
        d.tick(1.0);
        d.fire(SWOFTY_EVENT_SESSION_START, 0, 0, nullptr, 1.5);
        expect(g_scopes.size() == 2 && g_scopes[0] == 11 && g_scopes[1] == 12,
               "each callback runs in a profile scope with its own probe");
        expect(g_open_seen == 1 && g_open == 0, "the scope is open while the callback runs and closed after it");
    }

    // A career content tile press is published as career.content.pressed; addons must be able to
    // subscribe to it.
    {
        runtime::ev::Dispatcher<Recorder> d;
        uint32_t t = 0;
        expect(runtime::ev::canonical(SWOFTY_EVENT_CAREER_CONTENT_PRESSED) != nullptr,
               "career.content.pressed is a known event");
        expect(d.subscribe(1, SWOFTY_EVENT_CAREER_CONTENT_PRESSED, on_event, nullptr, &t) == SWOFTY_ASSET_OK,
               "career.content.pressed can be subscribed");
    }

    printf("%s\n", fails ? "events dispatcher: FAILED" : "events dispatcher: all checks passed");
    return fails ? 1 : 0;
}
