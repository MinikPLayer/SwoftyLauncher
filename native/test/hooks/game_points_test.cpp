// Host unit test for the core's four game hook points (game_points.hpp): each detour runs on a harmless
// stand-in in this binary with the game function's prototype. It checks that nothing is patched without a
// subscriber, each point's payload (arguments plus what the probe reads), the phase order (before by
// priority, replace until one handles it, the game's code, after with the outcome), that before handlers'
// changes reach the game's code, that a replace handler stands in for it, that a raw wrap installed
// before the window stage chains below the point, and that a raw overwrite wins: the point yields,
// reports the conflict and leaves the function to it. run_game_points_test.sh builds and runs it.
#include "../../src/core/hooks/game_points.hpp"
#include "../sdk_payloads.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace swofty_core::hooks;
namespace gp = swofty_core::hooks::game_points;
namespace ce = swofty::career::events;
namespace te = swofty::tracks::events;

static int fails;
static void expect(bool ok, const std::string &what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
    fails += !ok;
}

static std::vector<std::string> lines, trail;
static bool logged(const std::string &s)
{
    for (auto &l : lines)
        if (l.find(s) != std::string::npos)
            return true;
    return false;
}

static std::string joined()
{
    std::string s;
    for (auto &t : trail)
        s += (s.empty() ? "" : " > ") + t;
    return s;
}

static std::string hex(uint64_t v)
{
    char b[24];
    snprintf(b, sizeof b, "0x%llx", (unsigned long long)v);
    return b;
}

// The stand-ins: the four game prototypes, each recording its call.
extern "C" __attribute__((noinline)) void sa_add_activity(void *calendar, const void *activity, uint32_t day)
{
    volatile uint32_t d = day;
    for (volatile int i = 0; i < 1; i = i + 1)
        d = d + 0;
    trail.push_back("game add_activity day " + std::to_string(d) + (calendar ? "" : " no calendar") +
                    (activity ? "" : " no activity"));
}

static uint32_t decided = 0xd2985448;
extern "C" __attribute__((noinline)) uint32_t *sa_decide(void *state, uint32_t *link)
{
    volatile uint32_t v = decided;
    for (volatile int i = 0; i < 1; i = i + 1)
        v = v + 0;
    if (link)
        *link = v;
    trail.push_back(std::string("game decide") + (state ? "" : " no state"));
    return link;
}

extern "C" __attribute__((noinline)) void sa_sign(void *contracts, void *contract, uint64_t driver, uint32_t replaced,
                                                  uint8_t notify)
{
    volatile uint64_t d = driver;
    for (volatile int i = 0; i < 1; i = i + 1)
        d = d + 0;
    trail.push_back("game sign driver " + hex(d) + " replaced " + hex(replaced) + " notify " + std::to_string(notify) +
                    (contracts && contract ? "" : " no contract"));
}

extern "C" __attribute__((noinline)) float sa_fuel(void *setup)
{
    volatile float kg = 105.5f;
    for (volatile int i = 0; i < 1; i = i + 1)
        kg = kg + 0.0f;
    trail.push_back(std::string("game fuel") + (setup ? "" : " no setup"));
    return kg;
}

static void (*volatile add_fn)(void *, const void *, uint32_t) = sa_add_activity;
static uint32_t *(*volatile decide_fn)(void *, uint32_t *) = sa_decide;
static void (*volatile sign_fn)(void *, void *, uint64_t, uint32_t, uint8_t) = sa_sign;
static float (*volatile fuel_fn)(void *) = sa_fuel;

struct Prologues {
    unsigned char b[4][8];
    void take()
    {
        memcpy(b[0], reinterpret_cast<void *>(&sa_add_activity), 8);
        memcpy(b[1], reinterpret_cast<void *>(&sa_decide), 8);
        memcpy(b[2], reinterpret_cast<void *>(&sa_sign), 8);
        memcpy(b[3], reinterpret_cast<void *>(&sa_fuel), 8);
    }
    bool same(int i) const
    {
        const void *f[4] = {reinterpret_cast<void *>(&sa_add_activity), reinterpret_cast<void *>(&sa_decide),
                            reinterpret_cast<void *>(&sa_sign), reinterpret_cast<void *>(&sa_fuel)};
        return !memcmp(b[i], f[i], 8);
    }
    bool all() const { return same(0) && same(1) && same(2) && same(3); }
};

static std::map<std::string, std::string> overwrites;
static std::vector<std::string> conflicts;

static GameOps ops()
{
    GameOps o;
    o.log = [](const std::string &s) { lines.push_back(s); };
    o.resolve = [](const std::string &b) -> uintptr_t {
        if (b == gp::ADD_ACTIVITY.binding)
            return reinterpret_cast<uintptr_t>(&sa_add_activity);
        if (b == gp::DECIDE.binding)
            return reinterpret_cast<uintptr_t>(&sa_decide);
        if (b == gp::SIGN.binding)
            return reinterpret_cast<uintptr_t>(&sa_sign);
        if (b == gp::FUEL.binding)
            return reinterpret_cast<uintptr_t>(&sa_fuel);
        return 0;
    };
    o.overwritten_by = [](const std::string &b, uintptr_t) {
        auto it = overwrites.find(b);
        return it == overwrites.end() ? std::string() : it->second;
    };
    o.occupied_by = [](uintptr_t) { return std::string(); };
    o.conflict = [](const std::string &point, const std::string &by) { conflicts.push_back(point + " " + by); };
    return o;
}

static int player_calendar, other_calendar, contract_event, contracts_component, decision_state, fuel_setup;
static unsigned char contract_buf[0x30];

static gp::Probe probe()
{
    gp::Probe p;
    p.add_activity = [](ce::AddActivity &a) {
        a.type = a.activity == &contract_event ? ce::AddActivity::contract_event : 0x0a;
        a.driver = a.calendar == &player_calendar ? 7u << 16 : a.calendar == &other_calendar ? 9u << 16 : 0;
        a.player = a.calendar == &player_calendar;
    };
    p.contract_sign = [](ce::ContractSign &s) {
        memcpy(&s.team, static_cast<unsigned char *>(s.contract), 4);
        s.type = static_cast<unsigned char *>(s.contract)[0x0e];
    };
    p.fuel_track = [](void *setup, uint32_t &type) {
        type = 15;
        return std::string(setup == &fuel_setup ? "game/melbourne" : "");
    };
    return p;
}

struct Platform0 {
    static Platform make()
    {
        Platform p;
        p.log = [](const std::string &s) { lines.push_back(s); };
        return p;
    }
};

template <class P, class F> static uint32_t on(Table &t, const char *addon, uint32_t phase, int priority, F fn)
{
    struct Box {
        F fn;
    };
    auto *box = new Box{fn};
    uint32_t token = 0;
    t.attach(
        addon, P::point, phase, priority, "",
        [](void *user, SwoftyHookCall *c) -> int {
            P &p = *static_cast<P *>(c->payload);
            return static_cast<Box *>(user)->fn(p, *c) ? 1 : 0;
        },
        box, &token);
    return token;
}

struct T1 {};
struct T2 {};
struct T3 {};

static void test_untouched_without_subscribers()
{
    lines.clear();
    Prologues pro;
    pro.take();
    Table t(Platform0::make());
    t.declare_core();
    gp::Set<T1>::declare(t, ops(), probe());
    t.arm();
    expect(pro.all(), "no subscriber: arming the window stage patches none of the four functions");
    for (auto *s : {&gp::ADD_ACTIVITY, &gp::DECIDE, &gp::SIGN, &gp::FUEL})
        expect(t.live(s->point, nullptr) == SWOFTY_ASSET_UNAVAILABLE,
               std::string(s->point) + ": live() says unavailable with no subscriber");
    trail.clear();
    add_fn(&player_calendar, &contract_event, 40);
    expect(joined() == "game add_activity day 40", "and the game's code runs as before: " + joined());
}

static void test_points()
{
    lines.clear();
    Prologues pro;
    pro.take();
    Table t(Platform0::make());
    t.declare_core();
    gp::Set<T2>::declare(t, ops(), probe());

    std::vector<uint32_t> tokens;
    ce::AddActivity seen{};
    bool seen_replaced = false;
    tokens.push_back(on<ce::AddActivity>(t, "swofty.career-editor", SWOFTY_PHASE_BEFORE, 0, [](ce::AddActivity &a, auto &) {
        trail.push_back("before(0) day " + std::to_string(a.day));
        return false;
    }));
    tokens.push_back(on<ce::AddActivity>(t, "someone.calendar", SWOFTY_PHASE_BEFORE, 10, [](ce::AddActivity &a, auto &) {
        trail.push_back("before(10) day " + std::to_string(a.day));
        if (a.type == ce::AddActivity::contract_event && !a.player)
            a.day = 50;
        return false;
    }));
    tokens.push_back(on<ce::AddActivity>(t, "swofty.progressive", SWOFTY_PHASE_REPLACE, 0, [](ce::AddActivity &a, auto &) {
        bool mine = a.type == ce::AddActivity::contract_event && a.player;
        trail.push_back(std::string("replace ") + (mine ? "handled" : "passed"));
        return mine;
    }));
    tokens.push_back(on<ce::AddActivity>(t, "swofty.career-editor", SWOFTY_PHASE_AFTER, 0,
                                         [&](ce::AddActivity &a, SwoftyHookCall &c) {
                                             seen = a;
                                             seen_replaced = c.replaced != 0;
                                             trail.push_back(std::string("after ") + (c.replaced ? "replaced" : "ran"));
                                             return false;
                                         }));
    expect(pro.all() && t.live(gp::ADD_ACTIVITY.point, nullptr) == SWOFTY_ASSET_LATE,
           "a subscriber before the window stage: nothing patched yet, live() says late");
    t.arm();
    expect(!pro.same(0) && pro.same(1) && pro.same(2) && pro.same(3),
           "the window stage installs only the point with subscribers (add_activity)");
    uint32_t version = 0;
    expect(t.live(gp::ADD_ACTIVITY.point, &version) == SWOFTY_ASSET_OK && version == ce::AddActivity::version,
           "live() says ok, payload v1");
    expect(logged("[hooks] career.calendar.add_activity live on Game_CareerActivities_AddActivity (window stage): "
                  "subscribers swofty.career-editor, someone.calendar, swofty.progressive"),
           "the install line names the binding and the subscribers");

    trail.clear();
    add_fn(&player_calendar, &contract_event, 40);
    expect(joined() == "before(10) day 40 > before(0) day 40 > replace handled > after replaced",
           "the player's contract event: before by priority, Progressive's replace handles it, the game's code "
           "doesn't run, after sees replaced: " +
               joined());
    expect(seen.size == sizeof(ce::AddActivity) && seen.payload_version == 1 && seen.calendar == &player_calendar &&
               seen.activity == &contract_event && seen.day == 40 && seen.type == 0x0f && seen.driver == 7u << 16 &&
               seen.player && seen_replaced,
           "the payload: calendar, activity, day from the arguments; type, driver and player from the probe");
    trail.clear();
    add_fn(&other_calendar, &contract_event, 40);
    expect(joined() == "before(10) day 40 > before(0) day 50 > replace passed > game add_activity day 50 > after ran",
           "another driver's contract event: a before handler moves the day and the game's code adds it on the new "
           "day: " +
               joined());
    expect(seen.day == 50 && !seen.player && seen.driver == 9u << 16, "after sees the moved day and the other driver");

    tokens.push_back(on<ce::ContractDecide>(t, "swofty.progressive", SWOFTY_PHASE_AFTER, 0,
                                            [](ce::ContractDecide &d, SwoftyHookCall &c) {
                                                trail.push_back("after link " + hex(d.link) +
                                                                (c.replaced ? " replaced" : ""));
                                                return false;
                                            }));
    static bool force_negotiate = false;
    tokens.push_back(on<ce::ContractDecide>(t, "swofty.f1-1990", SWOFTY_PHASE_REPLACE, 0, [](ce::ContractDecide &d, auto &) {
        if (!force_negotiate)
            return false;
        d.link = ce::ContractDecide::negotiate;
        trail.push_back("replace negotiate");
        return true;
    }));
    expect(!pro.same(1), "the first subscriber after the window stage installs decide at once");
    trail.clear();
    uint32_t link = 0;
    uint32_t *r = decide_fn(&decision_state, &link);
    expect(r == &link && link == ce::ContractDecide::ends_this_season &&
               joined() == "game decide > after link 0xd2985448",
           "decide: the game's link comes back to the game and after handlers see it: " + joined());
    force_negotiate = true;
    trail.clear();
    link = 0;
    r = decide_fn(&decision_state, &link);
    expect(r == &link && link == ce::ContractDecide::negotiate &&
               joined() == "replace negotiate > after link 0x5e3c3c2d replaced",
           "decide: a replace handler's link is what the game gets, and its code doesn't run: " + joined());
    force_negotiate = false;

    ce::ContractSign signed_seen{};
    tokens.push_back(on<ce::ContractSign>(t, "swofty.career-editor", SWOFTY_PHASE_BEFORE, 0, [](ce::ContractSign &s, auto &) {
        s.notify = false;
        s.replaced = 0x00050000;
        trail.push_back("before sign team " + hex(s.team) + " type " + std::to_string(s.type));
        return false;
    }));
    tokens.push_back(on<ce::ContractSign>(t, "swofty.progressive", SWOFTY_PHASE_AFTER, 0,
                                          [&](ce::ContractSign &s, auto &) {
                                              signed_seen = s;
                                              trail.push_back("after sign");
                                              return false;
                                          }));
    uint32_t team = 0x01dc0000;
    memcpy(contract_buf, &team, 4);
    contract_buf[0x0e] = 0;
    trail.clear();
    sign_fn(&contracts_component, contract_buf, 0xdead000000070000ull, ce::ContractSign::work_out_replaced, 1);
    expect(joined() == "before sign team 0x1dc0000 type 0 > game sign driver 0xdead000000070000 replaced 0x50000 notify 0 "
                       "> after sign",
           "sign: before handlers' replaced and notify reach the game's code; the driver's upper bits pass through: " +
               joined());
    expect(signed_seen.contracts == &contracts_component && signed_seen.contract == contract_buf &&
               signed_seen.driver == 0x00070000 && signed_seen.team == team && !signed_seen.notify &&
               signed_seen.size == sizeof(ce::ContractSign),
           "sign: the payload carries contracts, contract, the driver key and the contract's team from the probe");

    static bool replace_fuel = false;
    te::FuelLoad fuel_seen{};
    static std::string fuel_seen_track;
    tokens.push_back(on<te::FuelLoad>(t, "swofty.progressive", SWOFTY_PHASE_REPLACE, 0, [](te::FuelLoad &f, auto &) {
        if (!replace_fuel || f.kind != te::FuelLoad::race)
            return false;
        f.kg = 140.0f;
        trail.push_back("replace fuel " + std::string(f.track().id()));
        return true;
    }));
    tokens.push_back(on<te::FuelLoad>(t, "someone.fuel-log", SWOFTY_PHASE_AFTER, 0, [&](te::FuelLoad &f, auto &) {
        fuel_seen = f;
        fuel_seen_track = f.track_id;
        char b[64];
        snprintf(b, sizeof b, "after fuel %.1f", f.kg);
        trail.push_back(b);
        return false;
    }));
    trail.clear();
    float kg = fuel_fn(&fuel_setup);
    expect(kg == 105.5f && joined() == "game fuel > after fuel 105.5",
           "fuel: the game's load comes back to the game and after handlers see it: " + joined());
    expect(fuel_seen.session_type == 15 && fuel_seen.kind == te::FuelLoad::race &&
               fuel_seen_track == "game/melbourne",
           "fuel: the payload names the track and the session's kind");
    replace_fuel = true;
    trail.clear();
    kg = fuel_fn(&fuel_setup);
    expect(kg == 140.0f && joined() == "replace fuel game/melbourne > after fuel 140.0",
           "fuel: a replace handler's load is what the game gets: " + joined());
    replace_fuel = false;

    for (auto tok : tokens)
        t.detach(tok);
    expect(pro.all(), "the last subscriber of each point leaving restores every function's bytes");
    trail.clear();
    add_fn(&player_calendar, &contract_event, 40);
    expect(joined() == "game add_activity day 40", "and the game's code runs alone again");
}

// A raw wrap from an older addon, installed by the mixin commit before the window stage.
static SafetyHookInline raw_wrap;
static void raw_add_activity(void *calendar, const void *activity, uint32_t day)
{
    trail.push_back("raw wrap day " + std::to_string(day));
    raw_wrap.call<void>(calendar, activity, day);
}

static void test_raw_mixins()
{
    lines.clear();
    conflicts.clear();
    Prologues pro;
    pro.take();
    auto h = safetyhook::InlineHook::create(reinterpret_cast<void *>(&sa_add_activity), reinterpret_cast<void *>(&raw_add_activity));
    expect(bool(h), "a raw wrap installs on add_activity (the older addon's SWOFTY_WRAP)");
    raw_wrap = std::move(*h);
    unsigned char wrapped[8];
    memcpy(wrapped, reinterpret_cast<void *>(&sa_add_activity), 8);

    overwrites[gp::SIGN.binding] = "old_addon.dll";
    Table t(Platform0::make());
    t.declare_core();
    gp::Set<T3>::declare(t, ops(), probe());
    std::vector<uint32_t> tokens;
    tokens.push_back(on<ce::AddActivity>(t, "swofty.progressive", SWOFTY_PHASE_BEFORE, 0, [](ce::AddActivity &a, auto &) {
        trail.push_back("before day " + std::to_string(a.day));
        a.day += 1;
        return false;
    }));
    tokens.push_back(on<ce::AddActivity>(t, "swofty.progressive", SWOFTY_PHASE_AFTER, 0, [](ce::AddActivity &, auto &) {
        trail.push_back("after");
        return false;
    }));
    tokens.push_back(on<ce::ContractSign>(t, "swofty.progressive", SWOFTY_PHASE_AFTER, 0, [](ce::ContractSign &, auto &) {
        trail.push_back("after sign");
        return false;
    }));
    t.arm();
    trail.clear();
    add_fn(&player_calendar, &contract_event, 40);
    expect(joined() == "before day 40 > raw wrap day 41 > game add_activity day 41 > after",
           "a raw wrap on the same function chains below the point: " + joined());
    expect(pro.same(2) && conflicts.size() == 1 && conflicts[0] == "career.contract.sign old_addon.dll",
           "a raw overwrite of Career_Sign_DriverContract wins: the point is not installed and the conflict is listed");
    expect(logged("[hooks] career.contract.sign unavailable: old_addon.dll overwrites Career_Sign_DriverContract; "
                  "subscribers swofty.progressive not run (the launcher lists this as a conflict)"),
           "the yield line names the overwriting addon and the subscribers that lose the point");
    expect(t.live(gp::SIGN.point, nullptr) == SWOFTY_ASSET_UNAVAILABLE &&
               t.live(gp::ADD_ACTIVITY.point, nullptr) == SWOFTY_ASSET_OK,
           "live(): unavailable for the yielded point, ok for the one installed beside the raw wrap");
    trail.clear();
    sign_fn(&contracts_component, contract_buf, 0x00070000, ce::ContractSign::work_out_replaced, 1);
    expect(joined() == "game sign driver 0x70000 replaced 0x3fff0000 notify 1",
           "the yielded point's function runs as the overwrite leaves it, without the subscribers: " + joined());
    for (auto tok : tokens)
        t.detach(tok);
    expect(!memcmp(wrapped, reinterpret_cast<void *>(&sa_add_activity), 8),
           "the point leaving restores the raw wrap's bytes, not the game's");
    trail.clear();
    add_fn(&player_calendar, &contract_event, 40);
    expect(joined() == "raw wrap day 40 > game add_activity day 40", "the raw wrap keeps working alone: " + joined());
    raw_wrap.reset();
    expect(pro.all(), "and with it gone the function is the game's again");
    overwrites.clear();
}

static void test_payload_layout()
{
    using F = te::FuelLoad;
    expect(swofty_core::hooks::payloads::fuel_kind(1) == F::practice && swofty_core::hooks::payloads::fuel_kind(4) == F::practice &&
               swofty_core::hooks::payloads::fuel_kind(5) == F::qualifying &&
               swofty_core::hooks::payloads::fuel_kind(14) == F::qualifying &&
               swofty_core::hooks::payloads::fuel_kind(15) == F::race && swofty_core::hooks::payloads::fuel_kind(17) == F::race &&
               swofty_core::hooks::payloads::fuel_kind(18) == F::other && swofty_core::hooks::payloads::fuel_kind(0) == F::race,
           "the session kinds follow the game's table (1-4 practice, 5-14 qualifying, 15-17 race, 18 other, the rest race)");
    expect(sizeof(ce::ContractDecide) == sizeof(SwoftyEvContractDecide) && sizeof(ce::ContractSign) == sizeof(SwoftyEvContractSign) &&
               sizeof(te::FuelLoad) == sizeof(SwoftyEvFuelLoad),
           "the payloads match their C mirrors");
    const catalogue::Event *e = catalogue::find("session.fuel.load");
    expect(e && e->fired == catalogue::Fired::game && !strcmp(e->payload, "swofty::tracks::events::FuelLoad"),
           "the catalogue names session.fuel.load a game point with its payload type");
}

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    test_untouched_without_subscribers();
    test_points();
    test_raw_mixins();
    test_payload_layout();
    printf("%s: %d failure(s)\n", fails ? "FAIL" : "ok", fails);
    return fails ? 1 : 0;
}
