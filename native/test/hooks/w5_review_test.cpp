// Review checks for the W5 dispatch table (host test, same build as run_hooks_test.sh). Each check
// states what the spec or the SDK promises; a FAIL is a finding, not a broken test.
#include "../../src/core/hooks/dispatch.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace swofty_core::hooks;

static int fails;

static void expect(bool ok, const std::string &what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
    fails += !ok;
}

static std::vector<std::string> calls;

static int note(void *user, SwoftyHookCall *)
{
    calls.push_back(static_cast<const char *>(user));
    return 0;
}

static void live_on_unfired_events()
{
    Table t;
    t.declare_core();
    uint32_t v = 0;
    int add = t.live("career.calendar.add_activity", &v);
    int season = t.live("career.season.start", &v);
    expect(add != SWOFTY_ASSET_OK,
           "live(career.calendar.add_activity) is not ok on a core that never fires it (catalogue Fired::no); "
           "hooks.md tells addons to fall back when live<AddActivity>() is false (got " + std::to_string(add) + ")");
    expect(season == SWOFTY_ASSET_OK,
           "live(career.season.start) is ok: the core fires it at a season's start (got " + std::to_string(season) + ")");
}

static void unknown_mode_is_not_grand_prix()
{
    Table t;
    t.declare_core();
    calls.clear();
    uint32_t tok = 0;
    t.attach("gp.addon", "career.hub", SWOFTY_PHASE_AFTER, 0, R"([{"mode":"grand_prix"}])", note, (void *)"gp", &tok);
    t.attach("tt.addon", "career.hub", SWOFTY_PHASE_AFTER, 0, R"([{"mode":"time_trial"}])", note, (void *)"tt", &tok);
    struct {
        uint32_t size, payload_version;
    } s{8, 1};
    int r = 0;
    t.fire("", "career.hub", &s, &r);
    expect(calls.empty(), "with no mode known (menus, Time Trial: nothing sets Facts::mode except career.loaded), a "
                          "{\"mode\":\"grand_prix\"} handler does not run (ran: " +
                              std::to_string(calls.size()) + ")");
}

static void load_order_is_attach_order()
{
    Platform p;
    p.load_order = [](const std::string &addon, uint64_t) -> uint64_t {
        return addon == "first.in.load.order" ? 1 : addon == "second.in.load.order" ? 2 : 0;
    };
    Table t(std::move(p));
    t.declare("x.notify", SWOFTY_POINT_NOTIFY, 1, true, false);
    calls.clear();
    uint32_t tok = 0;
    t.attach("second.in.load.order", "x.notify", SWOFTY_PHASE_AFTER, 0, "", note, (void *)"second", &tok);
    t.attach("first.in.load.order", "x.notify", SWOFTY_PHASE_AFTER, 0, "", note, (void *)"first", &tok);
    struct {
        uint32_t size, payload_version;
    } s{8, 1};
    int r = 0;
    t.fire("", "x.notify", &s, &r);
    std::string got = calls.size() == 2 ? calls[0] + " " + calls[1] : "?";
    expect(got == "first second",
           "equal priority runs in load order even when the earlier addon attaches later (a deferred addon's "
           "SWOFTY_ON_LOAD runs in a late pass); the Table has no load-order input, so it runs " + got);
}

int main()
{
    live_on_unfired_events();
    unknown_mode_is_not_grand_prix();
    load_order_is_attach_order();
    printf("%s\n", fails ? "w5 review test: findings above" : "w5 review test passed");
    return fails ? 1 : 0;
}
