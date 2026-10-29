---
title: The registry
description: Registries, records, typed handles, ids, shared tags and record structs in the standard library. You register records in code with SWOFTY_REGISTER.
---

# The registry

A **registry** is a named, typed collection of **records** that every addon in the profile shares. A record is one item of data, such as a car, an engine sound or one season of a career. You add records, and change other addons' records, in code through the registry's **handle**: a C++ object, such as `cars::vehicles`, declared in a system header.

<Split>

An engine sound pack adds its sound under its own key and points the 2017 Ferrari at it. `add` provides a record. `layer` changes someone else's.

Each registry is an `inline Registry<T>` in a system header. `T` is the record struct, so the compiler checks every field, and records point at each other with `Ref<T>`.

The **Without code** tab is the same addon with no DLL: the same records as data files in `registry/`.

<template #example>
<Example root="mod.esp" code-file="native/src/esp.cpp" data-file="registry/sounds.json" files="swofty.json, sounds/v10_screamer.bdl, native/esp.dll, registry/layers.json">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/sounds.hpp>
#include <swofty/systems/cars.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    // provide under your own key: the id is "mod.esp/v10_screamer"
    auto v10 = sounds::engine_sounds.add("v10_screamer", {
        .bundle = file("sounds/v10_screamer.bdl"),
        .settings_from = sounds::engine_sounds["game/ferrari_2025"],
    });

    // change another addon's record: an edit, run on its effective value in load order
    cars::vehicles.layer(cars::vehicles["swofty.classic-seasons-2017/ferrari"], [=](cars::Vehicle &v) {
        v.engine_sound = v10;
    });
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
// registry/sounds.json: your records, under your own keys
{ "engine_sound": { "v10_screamer": { "bundle": "sounds/v10_screamer.bdl", "settings_from": "game/ferrari_2025" } } }
```

```json
// registry/layers.json: changes to other addons' records, by full id
{ "vehicle": { "swofty.classic-seasons-2017/ferrari": { "engine_sound": "mod.esp/v10_screamer" } } }
```

</template>
</Example>
</template>
</Split>

`swofty/loader.log`:

```text
[registry] register: mod.esp (esp.dll) 1 engine_sound, 1 layer on vehicle (early stage, 0.4 ms)
[registry] vehicle swofty.classic-seasons-2017/ferrari: edit by mod.esp changed /engine_sound -> mod.esp/v10_screamer
[registry] applied at boot: engine_sound 1 (1 bundle path redirected to mod.esp), vehicle 1 (3 database records)
```

## The handle

```cpp
namespace swofty {

enum class Applies { boot, content, session, read };   // when the game reads the registry's data
enum class Scope { global, profile, career };          // how long a layer lasts and what it applies to
enum class Status { ok, no_record, no_registry, invalid, late };   // why get returned nothing

struct RegistryOptions {
    Applies applies = Applies::read;                   // x-applies
    bool snapshot = false;                             // frozen into careers, season by season (x-snapshot)
    std::size_t snapshot_budget = 65536;               // bytes after compression, per season of one career (x-snapshot-budget)
};

struct Text {                                          // a lang key or a literal; a plain string converts to Text::key
    static Text key(std::string_view lang_key);
    static Text literal(std::string_view text);
};
struct Colour { Colour(std::string_view hex); Colour(uint32_t argb); };   // "#22A45D" or 0xFF22A45D
struct Curve { static Curve flat(float from, float to, float value); };  // a game curve: one value over a range
struct VtfSection { static VtfSection read(Path vtf); };               // one VTF section, decoded

template <class T> class Ref {                         // a typed reference to a record, by id
public:
    std::string_view id() const;
    Ref optional() const;                              // missing: the element that holds it is dropped
    Ref or_else(Ref other) const;                      // missing: the fallback
    bool resolved() const;
};

template <class T> using Edit = std::function<void(T &)>;

template <class T> class Select {                      // a selector: every match, now and later
public:
    Select except(std::vector<Ref<T>> ids) const;      // every match but these ids
};
template <class T> Select<T> where(std::function<bool(const T &)> match);   // runs on the base value
template <class T> Select<T> under(std::string_view id_prefix);             // ids under a prefix

template <class T> class Tag {                         // a shared, named set of records ("c/f1_cars")
public:
    std::string_view id() const;
    bool contains(Ref<T> record) const;                // effective membership; a variant is in its base's tags
    std::vector<Ref<T>> members() const;
};
template <class T> Select<T> tagged(Tag<T> tag);                            // every record in the tag
template <class T, class U> Select<T> tagged(Ref<U> T::*field, Tag<U> tag); // every record whose field is in the tag

template <class T> class LayerHandle {                 // what layer returns
public:
    LayerHandle &in(events::Selector where);           // apply only there: a setup mode, a career, a box
};

template <class V> V option(std::string_view id);      // one of your addon's options; an edit that reads it re-runs when it changes
Path file(std::string_view path);                      // a file in your addon's folder
Path stock(std::string_view game_path);                // the game's original file, whatever an import replaced

template <class T> class Registry {
public:
    Registry(std::string_view name, RegistryOptions opts = {});
    std::string_view name() const;                                   // "vehicle", for logs, origins and data files

    // provide (your own keys)
    Ref<T> add(std::string_view key, T value);                       // base record "<your id>/<key>"
    Ref<T> variant(std::string_view key, Ref<T> base, Edit<T> edit); // extends base; base's layers flow in
    void provide(std::function<std::optional<T>(std::string_view id)> fn,
                 std::function<void(std::function<void(std::string_view)>)> list = {});   // dynamic
    void invalidate(Ref<T> id = {});
    void check(std::function<void(const T &, Errors &)> fn);         // the owner's own rules
    void protect(Ref<T> own_record, std::vector<std::string_view> fields);   // fields other addons' selectors skip

    // change (anyone's records)
    LayerHandle<T> layer(Ref<T> target, Edit<T> edit, Scope scope = Scope::global);
    LayerHandle<T> layer(Select<T> which, Edit<T> edit, Scope scope = Scope::global);   // every match, now and later
    void clear_layer(Ref<T> target, Scope scope = Scope::global);    // drop this addon's layer

    // tags
    Tag<T> tag(std::string_view id) const;                           // "c/f1_cars"
    void tag_add(Tag<T> tag, Ref<T> member);                         // put a record in a tag

    // read
    Ref<T> operator[](std::string_view id) const;                   // reference by id (checked at resolve)
    std::optional<T> get(Ref<T> id) const;                           // the effective value
    Status status(Ref<T> id) const;                                  // why get returned nothing
    std::vector<Ref<T>> all(std::string_view prefix = {}) const;
    std::string origin(Ref<T> id, std::string_view field) const;     // which addon set a field ("engine_sound")
    Token watch(std::function<void(Ref<T>)> fn);
};

// a second major of a registry: convert both ways, and upgrade old snapshots
template <class A, class B> void adapter(Registry<A> &from, Registry<B> &to, std::function<B(A)> up, std::function<A(B)> down);
template <class A, class B> void migrate(Registry<A> &from, Registry<B> &to, std::function<B(A)> fn);

}
```

- `add`, `variant`: [Keys, bases and variants](/std/bases).
- `provide`, `invalidate`: [Providers](/std/providers).
- `get`, `status`, `all`, `origin`, `watch`: [Reading](/std/reading).
- `layer`, `LayerHandle::in`, `clear_layer`, `check`, `protect`, `where`, `under`, `tagged`, `option`: [Layers](/std/layers).
- `tag`, `tag_add`, `Tag`: [Tags](#tags), below.
- `Scope`, `snapshot`, `snapshot_budget`: [Scopes and snapshots](/std/scopes).
- `RegistryOptions::applies`, `Applies`, `stock`: [When changes apply](/std/build).
- `Ref`: [Dependencies and guards](/std/dependencies). `adapter`, `migrate`: [Versioning](/std/versioning).

`SWOFTY_REGISTER` runs once per game start, at the early stage, addon by addon in load order. `SWOFTY_ON_LOAD` runs at the stage your manifest asks for. Registries the game reads only at boot (`Applies::boot`) take records and layers from `SWOFTY_REGISTER`. Every other registry also takes them from `SWOFTY_ON_LOAD` and later, while the game runs, and the change takes effect the next time the game reads that data.

## The core's registries

The core owns the registries that describe the game. Their handles live in one header per system under `swofty/systems/`, next to the system's events (`swofty::<system>::events`), hook payloads and operations (`swofty::cars::set_attribute(...)`):

```text
swofty/registry.hpp            Registry<T>, Ref<T>, Edit<T>, Select<T>, Tag<T>, LayerHandle<T>, Scope, RegistryOptions,
                               Applies, Status, SWOFTY_RECORD, Keyed<T, &T::key...>, SWOFTY_REGISTER,
                               swofty::option<T>(), file(), stock(), where(), under(), tagged(), adapter(), migrate()
swofty/systems/cars.hpp        swofty::cars      vehicles, packages, handling
swofty/systems/liveries.hpp    swofty::liveries  liveries
swofty/systems/teams.hpp       swofty::teams     constructors, identities, logos, pit_times
swofty/systems/drivers.hpp     swofty::drivers   people, identities, heads, looks, stats
swofty/systems/wearables.hpp   swofty::wearables helmets, suits, gloves, season_sets
swofty/systems/sounds.hpp      swofty::sounds    engine_sounds
swofty/systems/career.hpp      swofty::career    modes, contents, seasons, calendars, ratings, characters, wizards,
                                                 hub_tabs
swofty/systems/content.hpp     swofty::content   packages (Grand Prix and Time Trial seasons), era_boxes
swofty/systems/ui.hpp          swofty::ui        strings, addon_options
swofty/systems/sessions.hpp    swofty::sessions  Rules, set_control (the registries come later)
swofty/systems/tracks.hpp      swofty::tracks    set_track_fuel, copy_track_fuel, restore_track_fuel (the registries come in 1.6.x)
swofty/systems/ai.hpp          swofty::ai        the ai.controller.update payload
swofty/systems/spectator.hpp   swofty::spectator set_active, follow, set_camera, current
```

Thirty registries are in the headers, each with a schema. The core applies most of them. For `career.calendar`, `career.mode` and `vehicle.handling` it stores the records and applies nothing; at boot it logs `[registry] <name>: N record(s); this core applies nothing from it` for any registry that has records and no applier. The market, contract, R&D, facility, economy, sponsor, car audio, damage model and car parts registries, and the tracks, tyres, power, session, weather, AI, UI-screen and graphics registries of the [Systems catalogue](/std/systems/), come in later headers; their pages describe the design.

The system headers sit under `swofty/systems/`, apart from the headers older addons use, such as `swofty/career.hpp`, and their names never clash. A handle's `name()` is the registry name the logs, origins and data files use: `cars::vehicles` is `vehicle`, `career::seasons` is `career.season`. Every system has a page in the [Systems catalogue](/std/systems/).

## Records are structs

A record is a plain struct. `SWOFTY_RECORD` lists its fields. That list is the record's shape in data files, in Send Logs and in origin paths.

```cpp
// swofty/systems/career.hpp (abridged)
namespace swofty::career {

struct SeasonTeam {
    int slot = 0;                                   // 1-11, the career team that carries this season's team
    Ref<teams::Identity> team;                      // empty: the slot is empty this season
    std::array<Ref<drivers::Identity>, 2> seats;    // seat 0, seat 1
};
SWOFTY_RECORD(SeasonTeam, slot, team, seats)

enum class ChangeKind { swap, stand_in };

struct SeasonChange {
    int round = 0, slot = 0, seat = 0;
    Ref<drivers::Identity> out, in;
    ChangeKind kind = ChangeKind::swap;
    std::optional<int> until_round;                 // stand_in only
};
SWOFTY_RECORD(SeasonChange, round, slot, seat, out, in, kind, until_round)

struct Season {
    static constexpr Version version{1, 0};         // the schema this header was built against
    std::string data_group;
    int year = 0;
    Keyed<SeasonTeam, &SeasonTeam::slot> teams;     // matched by slot, not by position
    Keyed<SeasonChange, &SeasonChange::round, &SeasonChange::slot, &SeasonChange::seat> changes;
    json::Value extra;                              // fields from a newer minor, kept for the round trip
};
SWOFTY_RECORD(Season, data_group, year, teams, changes)

inline Registry<Season> seasons{"career.season", { .applies = Applies::read, .snapshot = true, .snapshot_budget = 65536 }};

}
```

- **`Ref<T>`** points at a record of another registry by id. Validation checks that it exists. `.optional()` and `.or_else(other)` say what happens when it doesn't (see [Dependencies and guards](/std/dependencies)).
- **`Keyed<T, &T::field...>`** is a list whose elements are matched by key. Edits, origins and conflicts name an element by its key (`teams/slot=1`), and a data file's layer changes one element without restating the list. A key can be several fields, as in `changes`. `at(key)` returns the element and appends it when it's missing, `find(key)` returns a pointer or null, and `erase(key)` removes it.
- **`extra`** holds fields this header doesn't know. Code built against 1.0 reads a 1.1 record and writes it back without losing anything (see [the tolerant reader rule](/std/versioning#the-tolerant-reader-rule)).
- **`stat_id`** is the number the game's database uses for a car, team, driver, track or mode. A record's `stat_id` is `StatId::automatic` unless you set it, and the core allocates one (see [Stat ids](#stat-ids)).
- **`RegistryOptions`** set when the game reads the registry's data and whether careers freeze it:
  - `applies` defaults to `Applies::read`: the game, or another addon, reads the data through the registry each time it needs it. A registry the game reads at boot, when content becomes active or when a session loads says so (see [When changes apply](/std/build)).
  - `snapshot` defaults to `false`. With `true`, each career freezes the registry's records season by season (see [Career snapshots](/std/scopes#career-snapshots)).
  - `snapshot_budget` defaults to 65536: the most bytes, after compression, that one season of one career may freeze from this registry.

## Your own registry

An addon declares a registry for its own data the same way the core does: a struct and an `inline Registry<T>`. Put them in a header you ship. Other addons include it to add, layer and read.

<Split>

Tails' "what if" addon owns a registry of career setup presets. Its name starts with the addon's id.

The header is the registry's public shape. Other addons include it to add presets of their own or layer Tails' presets.

<template #example>
<Example root="tails.whatif-history" code-file="api/whatif/presets.hpp" files="swofty.json, native/src/whatif.cpp, native/whatif.dll">
<template #code>

```cpp
#pragma once
#include <swofty/registry.hpp>

namespace whatif {

enum class Rnd { real_life, hybrid };
enum class TeamMoves { follow_driver, choose_team };

struct Preset {
    static constexpr swofty::Version version{1, 0};
    std::string name;                                 // [a-z0-9_]+
    int start_year = 2016;                            // 2016-2026
    Rnd rnd = Rnd::real_life;
    TeamMoves team_moves = TeamMoves::follow_driver;
};
SWOFTY_RECORD(Preset, name, start_year, rnd, team_moves)

inline swofty::Registry<Preset> presets{"tails.whatif.presets"};

}
```

</template>
</Example>
</template>
</Split>

`check` adds the registry's own rules on top of the struct: ranges, patterns, anything the types can't say. It runs on the result of every edit:

```cpp
// native/src/whatif.cpp
SWOFTY_REGISTER
{
    whatif::presets.check([](const whatif::Preset &p, swofty::Errors &err) {
        if (p.start_year < 2016 || p.start_year > 2026)
            err.add("start_year", "must be 2016-2026");
        if (!swofty::is_key(p.name))
            err.add("name", "lowercase letters, digits and _ only");
    });
    whatif::presets.add("real_2016", { .name = "real_2016", .start_year = 2016 });
}
```

An addon that doesn't have your header can still read your registry, untyped:

```cpp
swofty::Registry<swofty::json::Value> presets{"tails.whatif.presets"};
if (auto p = presets.get(presets["tails.whatif-history/real_2016"])) {
    int year = (*p)["start_year"].as_int();                 // 2016
    std::string moves = (*p)["team_moves"].as_string();     // "follow_driver"
}
```

## Ids

Every record has an id: the id of the addon that provides it, a `/`, and the key it was added under.

```text
career.season                                  <- registry
  swofty.progressive/2016                      <- record id: <addon id>/<key>
  swofty.progressive/2017                      { data_group: "f1_career_progressive", year: 2017, teams: [...] }
career.driver_ratings
  swofty.progressive/2017/game/max_verstappen       { driver: "game/max_verstappen", experience: 74, ... }
engine_sound
  game/ferrari_2025                            <- the core's record of the stock game
track.venue
  venue/sochi                                  <- a shared id the core owns; a track addon provides it
```

- **`add("key")` makes `<your addon id>/<key>`.** No addon can provide a record under another addon's id.
- **The core's records** describe the stock game and use `game/`: `game/ferrari_2025`, `game/f1_2026`.
- **Use `/` to build keys out of parts.** A prefix then lists a group: `swofty.progressive/` lists every Progressive season (see [Listing](/std/reading#listing)).

### Shared ids for real places, people and teams

The core owns three kinds of shared ids. Each names something real that several addons can bring:

- `venue/sochi`, `venue/hockenheimring`: a real circuit. A track record provides it.
- `person/rio_haryanto`, `person/alain_prost`: a real driver. A driver record provides it.
- `constructor/force_india`, `constructor/williams`: a real team. A team record provides it.

Other records point at the shared id instead of at one addon's record. A 2016 calendar asks for `venue/sochi`, and a 2016 line-up asks for `person/rio_haryanto`. The reference resolves to whichever enabled addon provides that id. When two addons provide it, the last one in load order wins (see [Shared ids](/std/providers#shared-ids)). The [Tracks](/std/systems/tracks), [Drivers](/std/systems/drivers) and [Teams](/std/systems/teams) pages show the records that provide them.

### Stat ids

Database-backed records (cars, teams, drivers, tracks, setup modes, looks) need a `stat_id`. The core allocates the ones left at `StatId::automatic`, in the game:

- **One store per install.** The core keeps every id it has allocated in `$DATA/registry/ids.json` (the launcher's data folder), keyed by record id. A record gets the same id at every game start.
- **An id is reused only when no career uses it.** When an addon is removed, its records' ids stay reserved while any career still refers to them.
- **Each career carries its own id map.** The career's save entry holds the stat id of every record the career refers to. A save that Steam Cloud brings to another PC loads with the ids it was saved with, and a track or a driver can't change places.

An explicit `stat_id` you ship stays your record's id from then on. Other addons and saved careers refer to it.

```text
[registry] stat ids: driver.identity swofty.f1-1990/alain_prost -> 241 (pool 208-254, 57 free after this)
[registry] stat ids: track mod.sochi/sochi -> 18 (kept from ids.json)
```

## Tags

A **tag** is a shared, named set of records in one registry. The core defines the shared tags, such as `c/f1_cars` on `vehicle` (and on `vehicle.handling`, for the same cars' handling) and `c/street_circuits` on `track`. Any addon can add its records to one. Selectors, event scopes and a box's track list can target a tag, and they reach records that addons add later.

<Split>

A converted 2021 car joins the core's set of F1 cars. An AI addon that changes every car in the set then changes this one too.

`tag` looks a tag up by id. `tag_add` puts one of your records in it. A variant is in every tag its base is in, so Classic Seasons' 2017 Ferrari, a variant of `game/ferrari_2025`, is in `c/f1_cars` without saying so.

In a data file, `registry/tags.json` lists the full ids you add, under the registry and the tag.

<template #example>
<Example root="mod.f1-2021-port" code-file="native/src/register.cpp" data-file="registry/tags.json" files="swofty.json, native/port.dll, registry/vehicles.json">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/cars.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    auto rb21 = cars::vehicles.add("redbull_2021", { .label = "rb16b", .donor = cars::vehicles["game/redbull_2025"] });

    cars::vehicles.tag_add(cars::vehicles.tag("c/f1_cars"), rb21);
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
{ "vehicle": { "c/f1_cars": ["mod.f1-2021-port/redbull_2021"] } }
```

</template>
</Example>
</template>
</Split>

```cpp
// another addon: every F1 car's AI config, whoever added the car, now and later
auto f1_cars = cars::vehicles.tag("c/f1_cars");
ai::vehicle_configs.layer(tagged(&ai::VehicleConfig::vehicle, f1_cars), [](ai::VehicleConfig &c) {
    c.racecraft.overtake_aggression = 0.85f;
});
```

- **Only the core defines `c/` tags.** Addons add records to them.
- **Members are optional.** A member whose addon is off is skipped. A tag never makes another addon required.
- **Membership is read when the data is read.** A selector on a boot registry reads its tag at boot. A box reads the tags in its track list when it becomes active. An event scope reads its tag when the event fires.
- **In a data file's selector,** `"tag": "c/f1_cars"` picks the tag's members (see [Layers](/std/layers#selectors)).

## Rules

- **Registry names** are dotted and lowercase. The core's are named after their system (`vehicle`, `track.data`, `tyre.class`, `career.season`, `session.mode`, `lang.string`, `era.box`, `hub.tab`, `addon.option`), and each system page lists them. An addon's own registry starts with its id.
- **Values** are at most 1 MB as JSON.
- **A record has one base** from one [provider](/std/providers), or it's a [variant](/std/bases#build-a-variant-with-extends) of another record. Every change on top of it is a [layer](/std/layers).
- **Every write is validated** (the struct, every `Ref`, the owner's `check`) before it becomes visible.

## Without code

An addon with no DLL ships records as data files in `registry/*.json`. The core finds them by folder name, with no manifest entry. A file holds records by registry name, then key:

```json
// registry/sounds.json
{ "engine_sound": { "v10_screamer": { "bundle": "sounds/v10_screamer.bdl", "settings_from": "game/ferrari_2025" } } }
```

Keys are your own: `v10_screamer` becomes `<your id>/v10_screamer`. A `Ref` is written as the record's full id. Tag members go in `registry/tags.json`. Fields you protect go in your own record as `"$protect": ["performance_tier"]`.

The SDK ships a JSON Schema for each core registry and version, for checking data files: `packages/sdk/std/registries/<name>/<major>.<minor>.json`. It's generated from the headers, and its keywords match the code:

- `x-registry` is `Registry<T>::name()`, and `x-version` is `T::version`.
- `x-applies` is `RegistryOptions::applies`: `boot`, `content`, `session` or `read`.
- `x-snapshot` and `x-snapshot-budget` are `RegistryOptions::snapshot` and `snapshot_budget`.
- `x-key` marks a `Keyed<T, &T::field...>` list, and `x-ref` marks a `Ref<T>`.
- `type`, `enum` and `required` come from the struct's field types.
- `minimum`, `maximum` and `pattern` come from the owner's `check`.

The core declares every registry the SDK headers define (30) itself at the start of the register pass, so a data-only addon needs no DLL to add records to any of them, and its `registry/index.json` entry is all the core needs to find it. A data-only addon can't declare a registry of its own. It can add records to any registry, add them to any `c/` tag and layer any record.
