---
title: Events
description: The game's lifecycle as events, from boot to the chequered flag, each with the context of everything; scoped handlers; content owners who override events or decide who hooks them; what happens when a handler faults; and events any addon defines for others.
---

# Events

An **event** is a moment in the game's lifecycle: a career's season starts, a session loads, a car makes a pit stop. Your addon subscribes to the moments it cares about, filtered to the content it cares about, and the core calls your handler with a **context**: the mode, the content being played and the addon that provides it, the career, the season and round, the weekend, the session, the player, every participant, and each one's registry records.

For example, F1 1990 caps the rain chance of every race in its own career with one `before` handler on `session.weather.setup`, scoped to its career. In every other career and in Grand Prix, the handler is never called.

## Events in full

<Split>

F1 1990 reacts to three moments, only inside its own career: it logs each season start, caps the rain chance of every race, and builds the grid from the better of two qualifying sessions.

Each event's payload lives in its system's header, in `swofty::<system>::events`. Subscriptions are code only. The core lists them in `registry/effective.json` for Send Logs.

<template #example>
<Example root="swofty.f1-1990" code-file="native/src/events.cpp" files="swofty.json, native/CMakeLists.txt, native/src/grid.cpp">
<template #code>

```cpp
#include <swofty/events.hpp>
#include <swofty/systems/career.hpp>
#include <swofty/systems/sessions.hpp>
#include <swofty/systems/weather.hpp>

using namespace swofty;

static const events::Selector k1990 = events::content("swofty.f1-1990/f1_career_1990");

SWOFTY_ON_LOAD
{
    // after: observe the moment
    events::on<career::events::SeasonStart>().in(k1990).after([](const career::events::SeasonStart &e, const events::Context &ctx) {
        swofty::log("[f1-1990] season %d starts, round %d of %d, player %s", e.year,
                    ctx.career()->round, ctx.career()->rounds, ctx.player()->driver.id().data());
    });

    // before: change what the game is about to use
    events::on<weather::events::Setup>().in(k1990).when(events::session_type({ events::SessionType::race }))
        .before([](weather::events::Setup &w, const events::Context &) { w.rain_chance = std::min(w.rain_chance, 30u); });

    // replace: stand in for the game; return true when handled
    events::on<sessions::events::GroupResult>().in(k1990).replace([](sessions::events::GroupResult &g, const events::Context &ctx) {
        return f1990::rank_best_of(g, ctx.session()->group, 1, 2);   // the grid from the better of two qualifying sessions
    });
}

SWOFTY_ADDON()
```

</template>
</Example>
</template>
</Split>

```text
[events] career.season.start (driver_career, swofty.f1-1990/f1_career_1990, 1990): 1 after handler
[f1-1990] season 1990 starts, round 1 of 16, player ayrton_senna
[events] session.grid.group_result in swofty.f1-1990/f1_career_1990: replace by swofty.f1-1990 -> handled
```

## Kinds and phases

An event is a [hook point](/std/hooks), so it has the same three phases: `before` changes the payload, `replace` stands in for the game's code, and `after` observes the result. Each event is one of three kinds, and the kind decides which phases it has:

- **notify** has `after` only. The core saw the moment.
- **modify** has `before` and `after`. The core hands the changed payload back to the game.
- **cancel** has `before`, `replace` and `after`. The core wraps the game's code, and a `replace` handler can stand in for it.

```text
core reaches the moment
  fills the payload and takes a context
  drops handlers whose scope doesn't match            (no call, no cost)
  before    priority, then load order; the content owner's run LAST
  replace   the content owner's FIRST, then priority, then load order, until one returns true
  after     priority, then load order; subscribe() handlers run here
```

Asking for a phase an event doesn't have is refused when you attach:

```text
[events] career.loaded has no replace phase (notify event); handler from my-addon not attached
```

## The lifecycle

Every event the core fires, with its kind:

```text
game
├─ game.boot                       notify    every addon's SWOFTY_ON_LOAD has run
├─ game.ready                      notify    the first front-end frame
├─ flow.state                      notify    any front-end flow state changes
├─ menu.route                      cancel    a front-end route runs; replace = it doesn't
├─ menu.screen                     notify    a screen opens
├─ game.pause / game.resume        notify
├─ game.saving                     modify    the profile is about to be written
├─ mode.select                     cancel    the player picks a mode
├─ content.select                  notify    a GP/TT box or a career tile is pressed
│
├─ career
│  ├─ career.starting              cancel    a career route; replace = career::block
│  ├─ career.created               notify    a new career exists; the core gives it an id and records each season's box
│  ├─ career.loaded                notify
│  ├─ career.hub                   notify    Team HQ is idle
│  ├─ career.season.start          notify    a season begins (the first, then each after the break);
│  │                                         the season's line-ups, ratings and rules are frozen here (career::Freeze::per_season)
│  ├─ career.time.advance          notify    time starts or stops advancing
│  ├─ career.day                   notify    the career day changes
│  ├─ career.calendar.add_activity     cancel   an activity is added to a calendar
│  ├─ career.calendar.remove_activity  cancel
│  ├─ career.rnd.start / .cancel   cancel    research or development starts or is cancelled
│  ├─ career.facility.complete     notify
│  ├─ career.weekend.start / .end  notify
│  ├─ career.contracts.open        notify    the contract scene opens
│  ├─ career.contract.decide       cancel    the game decides whether to negotiate
│  ├─ career.contract.sign         cancel
│  ├─ career.contract.changed      notify
│  ├─ career.driver.moved          notify
│  ├─ career.season.process        cancel    the season change processes contracts and moves
│  ├─ career.moves.finalised       notify    the line-up changes for the new season are final
│  ├─ career.season.end            notify
│  ├─ career.season.break          notify    the next season's calendar is built here
│  ├─ career.saving                modify    the last moment to write a career layer into this save
│  └─ career.unloaded              notify
│
├─ weekend
│  ├─ weekend.start                notify
│  └─ weekend.end                  notify
│
└─ session
   ├─ session.load                 modify
   ├─ session.weather.setup        modify
   ├─ session.fuel.load            cancel
   ├─ session.start                notify
   ├─ session.state.enter          notify    the session enters a state
   ├─ session.grid                 notify    the grid rundown
   ├─ session.formation_lap        notify
   ├─ race.start                   notify
   ├─ session.sector               notify
   ├─ session.lap                  notify
   ├─ session.pit / .pit_stop      notify
   ├─ session.flag                 notify
   ├─ session.safety_car.deploy    notify    the safety car is deployed
   ├─ session.red_flag             notify
   ├─ session.penalty.issue        cancel
   ├─ session.car.status           notify    retired, disqualified, terminal damage
   ├─ session.player.control       cancel    who drives the player's car
   ├─ car.controllers.rebuilt      notify    the game built a car's controller set (session start, and any rebuild after it)
   ├─ ai.controller.update         cancel    each AI car's throttle, brake and steering, every frame
   ├─ hud.spectator.visible        modify    whether the spectator HUD shows
   ├─ session.finish               notify
   ├─ session.grid.group_result    cancel    a session group's result, which sets a grid
   ├─ session.results.finalise     notify    the session's results are final
   ├─ session.podium               notify
   ├─ session.end                  notify    finished, abandoned or restarted
   └─ session.unload               notify
```

The headers of 1.6.0 define typed payloads for these points:

```text
career.season.start              swofty/systems/career.hpp   career::events::SeasonStart
career.calendar.add_activity     swofty/systems/career.hpp   career::events::AddActivity
career.contract.decide           swofty/systems/career.hpp   career::events::ContractDecide
career.contract.sign             swofty/systems/career.hpp   career::events::ContractSign
career.standings.rebuild         swofty/systems/career.hpp   career::events::StandingsRebuild
session.fuel.load                swofty/systems/tracks.hpp   tracks::events::FuelLoad
session.player.control           swofty/systems/sessions.hpp sessions::events::PlayerControl
ai.controller.update             swofty/systems/ai.hpp       ai::events::ControllerUpdate
hud.spectator.visible            swofty/systems/spectator.hpp spectator::events::HudVisible
car.built / car.reset / car.vtf.read / car.parts.register / car.upgrades.fit / car.controllers.rebuilt
                                 swofty/systems/cars.hpp     cars::events
audio.bundle.resolve             swofty/systems/sounds.hpp   sounds::events::BundleResolve
driver.ratings.read / driver.number.assign / driver.head.resolve
                                 swofty/systems/drivers.hpp  drivers::events
team.logo.resolve / team.name.resolve
                                 swofty/systems/teams.hpp    teams::events
wearable.catalogue.build         swofty/systems/wearables.hpp wearables::events::CatalogueBuild
```

`ai.controller.update` and `car.controllers.rebuilt` need the in-game checks T145 and T144. The other points in the tree (`session.state.enter`, `session.weather.setup`, `session.safety_car.deploy`, `session.pit_stop`, `session.results.finalise`, `career.season.process` and the rest of the session group) are the design for later headers; addons made for 1.5 and earlier subscribe to some of these moments by shorter names, which still work: see [Compatibility](/std/compatibility#events).

**Every start has one end.** The core keeps a lifecycle stack. If the player quits from the pause menu, the core fires the missing ends itself, with `reason = abandoned`:

```text
[events] stack: game > career(driver_career, swofty.f1-1990/f1_career_1990) > season 1990 > weekend r3 > session qualifying_1
player quits to the main menu
[events] session.end (reason abandoned, synthesised)
[events] weekend.end (reason abandoned)
[career] career.unloaded (mode driver_career)
```

## The context

Every handler of every event, and of every game hook point, gets the same view:

```cpp
namespace swofty::events {

class Context {
public:
    Mode mode() const;                               // grand_prix, time_trial, driver_career, my_team, ...
    const ContentView &content() const;              // what the player is playing, its owner, its variant chain
    const std::optional<CareerView> &career() const; // data group, year, season, round of rounds, day
    const std::optional<WeekendView> &weekend() const;
    const std::optional<SessionView> &session() const;   // type, group, track, weather, laps, setup mode
    const std::optional<Entrant> &player() const;
    std::span<const Entrant> participants() const;   // car, driver, team, vehicle, human
    bool in(const Selector &s) const;

    // the effective record of this moment's own thing: the season of the career, the package of a GP,
    // the track of a session. Inside a career it reads the season's frozen copy.
    template <class T> std::optional<T> record() const;
};

}
```

```cpp
on<sessions::events::Load>().after([](const sessions::events::Load &, const Context &ctx) {
    auto season = ctx.record<career::Season>();                  // the career's season, as frozen at its start
    auto track  = tracks::tracks.get(ctx.session()->track);
    for (const auto &e : ctx.participants())
        if (e.human)
            swofty::log("[my-addon] you drive %s for %s at %s", e.driver.id().data(), e.team.id().data(), track->name.c_str());
});
```

The context is a small handle over facts the core caches once per lifecycle change. Reading it costs nothing until you call a getter:

```text
[events] epoch 17: session.load (driver_career, swofty.f1-1990/f1_career_1990 owner swofty.f1-1990, 1990 r3, qualifying_1, game/imola)
[events] ctx.participants for epoch 17: 26 entrants in 0.21 ms
```

- **A field that doesn't apply is empty:** `session()` in the menus, `career()` in Grand Prix.
- **Live per-car values** (position, lap, tyres) are in the event's payload, not the context.
- **The name** is `swofty::events::Context`. `swofty::Context` is a different type: the register file that mid-hooks use.

## Scope

A handler names where it runs. The core checks the scope before calling, so a scoped handler costs nothing anywhere else:

```cpp
on<E>().in(content("swofty.classic-seasons-2017/f1_2017"))   // that package and every variant of it
on<E>().in(content_exact("swofty.classic-seasons-2017/f1_2017"))
on<E>().in(owner("swofty.f1-1990"))                          // any content that addon provides
on<E>().in(data_group("f1_career_progressive"))
on<E>().in(mode(Mode::time_trial))
on<E>().when(session_type({ SessionType::race, SessionType::sprint }))
on<E>().when(track("game/monaco"))
on<E>().when(year(2016, 2019))
on<E>().in(any({ content("a/x"), content("b/y") })).when(not_(track("game/monaco")))
on<E>().in(setup_mode("swofty.f1-1990/f1_1990"))                  // every session run under that setup mode
on<E>().in(vehicle_package("swofty.f1-1990/f1_1990"))              // per car: cars of that package
on<E>().in(f1990::scope)                                           // the scope your system declares (a Selector)
```

A rule written for the 2017 package also runs in "F1 2017 (V10)", which is a variant of it. Use `content_exact` for the one record only.

Layers take the same selectors to say where they apply (see [Layers](/std/layers)).

## Content owners

The **owner** of a moment is the addon that provides the content being played, and every owner along its variant chain. Inside its own content, an owner can do two things other addons can't.

### Override an event

The owner's `replace` runs first in its own content, so it decides how the game handles the moment there:

```cpp
// Classic Seasons 2017: races in the 2017 package start on 2017 fuel loads
on<tracks::events::FuelLoad>().in(content("swofty.classic-seasons-2017/f1_2017"))
    .replace([](tracks::events::FuelLoad &f, const Context &) {
        f.kg = cs::fuel_2017(f.track());
        return true;
    });
```

```text
[events] session.fuel.load in swofty.classic-seasons-2017/f1_2017: replace by swofty.classic-seasons-2017 (owner, first)
         -> handled; 1 other replace not reached (someone.fuelsaver)
```

### Decide who hooks an event

Every addon's handlers run in every content unless the owner says otherwise. The owner still has the last word through ordering: its `before` runs last and its `replace` first. An owner can also choose, per event and phase, to keep other addons' `before` and `replace` handlers out of its own content, and name the ones it allows:

```cpp
// swofty.f1-1990: a record of events::policies ("event.policy"), keyed by the content it owns
SWOFTY_REGISTER
{
    using events::Access;
    events::policies.add("f1_career_1990", { .events = {
        { "session.grid.group_result", { .replace = Access::owner, .allow = { "community.goodyear-1990-plus" } } },
        { "career.contract.decide",    { .replace = Access::owner, .before = Access::owner } },
        { "*",                         { .replace = Access::open } },
    } });
}
```

```json
// the same, without code: registry/policy.json
{ "event.policy": { "f1_career_1990": { "events": {
    "session.grid.group_result": { "replace": "owner", "allow": ["community.goodyear-1990-plus"] },
    "career.contract.decide":    { "replace": "owner", "before": "owner" },
    "*":                         { "replace": "open" } } } } }
```

A policy can only narrow who runs, and only a little:

- **`after` is never kept out.** Every addon can always observe every moment.
- **`subscribe` handlers are never kept out.** `swofty::events::subscribe` attaches an `after` handler, and raw mixins sit outside the chain.
- **It applies inside the owner's own content only.** Grand Prix, other careers and other packages are unaffected.
- **Other addons, and the player, can only open it.** A layer can add to `allow` or turn `owner` back into `open`, never close it: the schema refuses a closing layer. The player opens it per profile from the conflicts view.
- **Every skip is logged and listed in the launcher:**

```text
[events] policy swofty.f1-1990/f1_career_1990: session.grid.group_result replace is owner-only; skipped community.gridrules
         (allowed: community.goodyear-1990-plus; the player can open it in Profile > Conflicts)
```

## When a handler faults

The core calls every handler inside a guard that catches access violations and C++ exceptions. A C++ exception from a handler is caught in your DLL by the SDK's entry function and handed to the guard with its `what()` text, so it never unwinds into the core. What happens next depends on the phase:

- **An `after` handler** is isolated. The core logs the fault with the addon, the event and the phase, detaches that handler for the rest of the session, and lists it in `conflicts.json`. The game carries on, and the addon's tile in ADDON SETTINGS and in the launcher shows a status line.
- **A `before` or `replace` handler** crashes the game. The crash report's first line names the addon, the event and the phase.

```text
[events] after handler from someone.lapticker faulted on session.lap (access violation at lapticker.dll+0x1a2f);
         handler detached for this session
[events] after handler from someone.lapticker faulted on session.lap (C++ exception: stoi from lapticker.dll+0x2b40);
         handler detached for this session
```

```text
Swofty crash report: Crash in swofty.career-editor's replace handler on career.contract.decide
  (access violation at career_editor.dll+0x4c18)
```

Game hook points follow the same rule (see [Hooks](/std/hooks#when-a-handler-faults)).

## Your own events

Any addon can define events under its own id and fire them from its code. Other addons subscribe to them like core events, with full context and scope. An event can be global, or declared to fire only inside some content.

```cpp
// api/progressive_api/events.hpp, shipped by Progressive for other addons to include
namespace progressive_api {

struct SeatChange {
    static constexpr const char *point = "swofty.progressive.seat_change";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    int year, round;
    uint32_t slot, seat;
    const char *out_driver, *in_driver;   // record ids
    int replaced;                         // set by the core: a replace handler made the change instead
};

}
```

```cpp
// progressive.dll: define it, then fire it where Progressive applies a mid-season swap
SWOFTY_ON_LOAD
{
    swofty::events::define<progressive_api::SeatChange>(swofty::events::owner("swofty.progressive"));
}

void apply_change(const Change &c)
{
    progressive_api::SeatChange e{ sizeof e, 1, c.year, c.round, c.slot, c.seat, c.out.c_str(), c.in.c_str(), 0 };
    if (swofty::events::fire(e))          // false: a replace handler made the swap itself
        swofty::career::set_seat(c.slot, c.seat, swofty::drivers::identities[c.in]);
}
```

```cpp
// another addon: a news ticker for Progressive careers
#include <progressive_api/events.hpp>

SWOFTY_ON_LOAD
{
    using namespace swofty::events;
    on<progressive_api::SeatChange>().after([](const progressive_api::SeatChange &e, const Context &ctx) {
        swofty::hud::toast("Driver change", std::string(e.in_driver) + " replaces " + e.out_driver);
    });
}
```

- **The name starts with your addon's id.** The core refuses a `define` under another addon's id.
- **The scope is enforced.** An event fired outside the scope it was declared with isn't delivered:

```text
[events] swofty.progressive.seat_change fired outside its scope (content game/f1_career_2025); not delivered
```

- **Versions** follow the same rules as hook payloads: append fields and raise `version`, never remove (see [Versioning](/std/versioning)).
- **When a second addon needs the same moment,** the core adopts it into its own catalogue under a core name, and your name keeps working as an alias.
- **Subscribers by name** don't need your header: `on("swofty.progressive.seat_change").after(...)` gets the payload as bytes plus the full context.

## Launcher events

Launcher plugins get a `lifecycle` notification for the launcher's own moments:

```text
launcher  {"method":"lifecycle","params":{"event":"profile.switched","profile":"progressive"}}
launcher  {"method":"lifecycle","params":{"event":"addons.changed","profile":"progressive","changed":["mod.sochi"]}}
launcher  {"method":"lifecycle","params":{"event":"build.done","profile":"progressive","ok":true}}
launcher  {"method":"lifecycle","params":{"event":"play.starting","profile":"progressive"}}
launcher  {"method":"lifecycle","params":{"event":"game.ended","profile":"progressive","exit_code":0}}
launcher  {"method":"lifecycle","params":{"event":"logs.sending"}}
```

A plugin subscribes by listing them in its `launcher` block: `"lifecycle": ["build.done", "game.ended"]`.

## Adding an event

Adding a core event takes one catalogue entry, a hand-written payload, one fire call and a test. The catalogue is `packages/sdk/std/events/catalogue.json`:

```json
{
  "name": "session.pit_stop",
  "since": "1.6.0",
  "status": "experimental",
  "kind": "notify",
  "thread": "session",
  "parent": "session",
  "payload": { "c": "SwoftyEvPitStop", "cpp": "swofty::sessions::events::PitStop", "header": "swofty/systems/sessions.hpp", "version": 1 },
  "context": { "session": "always", "player": "always", "participants": "always", "career": "in_career" },
  "source": { "how": "bus", "message": "MessagePitStateChanged", "binding": "Ne_Subscribe_BusMessage" },
  "doc": "A car stops in its pit box and leaves it."
}
```

```cpp
// swofty/systems/sessions.hpp
namespace swofty::sessions::events {
struct PitStop {
    static constexpr const char *point = "session.pit_stop";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    uint32_t car;            // vehicle index in the session
    float stationary_s;      // time in the box
    uint8_t tyres_changed;   // 0 or 1
};
}
```

```cpp
// native/src/core/session_events.cpp: the one fire call, from the pit state message
void on_pit_state_changed(const MessagePitStateChanged &m)
{
    if (m.state == PitState::LeavingPitBox) {
        swofty::sessions::events::PitStop e{ sizeof e, 1, m.car, pit_timer(m.car).stop(), tyres_changed(m.car) };
        swofty::events::fire(e);
    }
}
```

`tools/std/eventcheck.py` checks the rest in CI:

```text
ok   62 events, every name unique and dotted; released entries unchanged since v1.6.0
ok   swofty/systems/sessions.hpp: struct sessions::events::PitStop, point == "session.pit_stop", version == 1
ok   native/src/core: a fire of session.pit_stop (session_events.cpp:88)
ok   native/test: a test names session.pit_stop
ok   docs: a section for session.pit_stop
```

- **The KB first.** An event that changes the game's behaviour (`modify`, `cancel`) needs every binding it uses at L2 with an independent verify. A `notify` event may use a binding at L1: it only reads.
- **Per-frame moments** use tables the core builds at content and session moments, as game hook points do (see [Hooks](/std/hooks#hook-points-the-game-calls-every-frame)).
- **Threads:** the core logs the thread of each event's first fire, and checks it against the catalogue:

```text
[events] first session.pit on thread 0x3a08 (flow thread 0x2f1c): catalogue says session, ok
```
