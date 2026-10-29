---
title: Drivers
description: Drivers in code - driver identity, shared person ids, heads, looks and stock ratings as typed registries, with the ratings, number and head events and the driver operations.
---

# Drivers

Add new drivers, give stock drivers new heads and helmets, and change ratings as the game reads them. Driver Manager's Mika Salo, for example, is one `drivers::Identity` record that starts from every key of the stock driver Jarno Opmeer, with a look and a ratings record of its own.

A few terms used on this page:

- A **driver** is a `drivers::Identity` record: one row in the game's `entrants.drivers` table, with names, nationality, number, head and look.
- A **head** is a `drivers::Head` record: a face archive, its morph poses and a rig row.
- A **look** is a `drivers::Look` record: the helmet, suit, gloves, pose and podium emote a driver wears.
- A **person** is a real racing driver, with an id under `person/` that the core owns. Every pack that adds Rio Haryanto says its driver is `person/rio_haryanto`.
- A **layer** changes values on a record that already exists. A **variant** is a separate record that inherits another record's values and overrides some of them.

## The header

<Example root="swofty-sdk" code-file="include/swofty/systems/drivers.hpp">
<template #code>

```cpp
#include <swofty/registry.hpp>

namespace swofty::drivers {

using Result = swofty::Result;

enum class BodyType { male, female };
enum class Stat : int32_t { experience, racecraft, awareness, pace, focus };   // one driver statistic

struct Person {                                    // a real driver; the core owns the person/ ids
    static constexpr Version version{1, 0};
    std::string name;                              // "Rio Haryanto"
    std::string nationality;                       // "indonesia"
    Ref<Identity> driver;                          // resolved: the last enabled driver in load order that is this person
    json::Value extra;
};
SWOFTY_RECORD(Person, name, nationality, driver)

struct Names {
    Text first;                                    // first_name
    Text last;                                     // last_name
    Text full;                                     // full_name
    std::string code;                              // initials: "VER"
};
SWOFTY_RECORD(Names, first, last, full, code)

struct Head {                                      // an entrants.animation.driver_heads row and its files
    static constexpr Version version{1, 0};
    Path archive;                                  // character_package/heads/<...>/<head>/
    Path morph;                                    // morph_poses/<head>.erp
    Ref<Head> rig_from;                            // the head row it copies (game/male_ls222, game/female_ls112)
    BodyType body = BodyType::male;                // body_type
    bool customisable = false;                     // available_for_customisation
    StatId stat_id = StatId::automatic;            // heads from 265
    json::Value extra;
};
SWOFTY_RECORD(Head, archive, morph, rig_from, body, customisable, stat_id)

struct Look {                                      // a driver_customisations row
    static constexpr Version version{1, 0};
    Ref<wearables::Helmet> helmet;                 // the helmet
    Ref<wearables::Suit> suit;                     // the suit
    Ref<wearables::Gloves> gloves;                 // the gloves
    std::string pose;                              // one of the 59 poses
    std::string podium_emote;                      // one of the 52 emotes
    StatId stat_id = StatId::automatic;            // look ids
    json::Value extra;
};
SWOFTY_RECORD(Look, helmet, suit, gloves, pose, podium_emote, stat_id)

struct Identity {                                  // one entrants.drivers row
    static constexpr Version version{1, 0};
    Ref<Identity> donor;                           // the row it copies, all 129 keys (game/jarno_opmeer)
    Ref<Person> person;                            // the real person it is; empty: a fictional driver
    Names names;                                   // the driver's names
    std::string nationality;                       // one of entrants.nationality (90)
    int number = 0;                                // car_number
    Ref<Head> head;                                // the head
    Ref<Look> look;                                // helmet, suit, gloves, pose; a driver needs one
    std::string audio_name;                        // the commentary name (entrants.audio_names)
    int seasons_completed = 0;                     // f1_seasons_completed
    int garage_slot = 0;                           // garage_slot_preference
    std::vector<std::string> pools;                // driver pools with a free-agent row: "f1_2026_driver_pool"
    StatId stat_id = StatId::automatic;            // driver ids 0-254, 58 free
    json::Value extra;
};
SWOFTY_RECORD(Identity, donor, person, names, nationality, number, head, look, audio_name, seasons_completed,
              garage_slot, pools, stat_id)

struct Curve {                                     // a rating from the first season to the driver's peak (not swofty::Curve)
    float start = 0;
    float peak = 0;
};
SWOFTY_RECORD(Curve, start, peak)

struct Stats {                                     // a DriverStatisticsHolder: the stock ratings
    static constexpr Version version{1, 0};
    Ref<Identity> driver;
    Curve experience, racecraft, awareness, pace, focus;
    json::Value extra;
};
SWOFTY_RECORD(Stats, driver, experience, racecraft, awareness, pace, focus)

inline Registry<Person> people{"driver.person", { .applies = Applies::boot }};
inline Registry<Identity> identities{"driver.identity", { .applies = Applies::boot }};
inline Registry<Head> heads{"driver.head", { .applies = Applies::boot }};
inline Registry<Look> looks{"driver.look", { .applies = Applies::boot }};   // career-scope layers: content
inline Registry<Stats> stats{"driver.stats", { .applies = Applies::boot }};

Result set_look(Ref<Identity> driver, Ref<Look> look);     // refuses an empty look

}

namespace swofty::drivers::events {

struct RatingsRead {                               // Career_Get_DriverStatValue: before
    static constexpr const char *point = "driver.ratings.read";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    const char *driver_id;
    Stat stat;                                     // experience, racecraft, awareness, pace, focus
    float value;                                   // before handlers may change it

    Ref<Identity> driver() const { return swofty::detail::payload_ref<Identity>(driver_id); }
};

struct NumberAssign {                              // Career_DriverNumbers_AssignFree: before, replace
    static constexpr const char *point = "driver.number.assign";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    const char *driver_id;
    int32_t number;

    Ref<Identity> driver() const { return swofty::detail::payload_ref<Identity>(driver_id); }
};

struct HeadResolve {                               // Ent_EntrantsSystem_FindDriverHead: before
    static constexpr const char *point = "driver.head.resolve";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    const char *driver_id;
    const char *head_id;
    int32_t (*set_head_fn)(void *user, const char *id);
    void *user;

    Ref<Identity> driver() const { return swofty::detail::payload_ref<Identity>(driver_id); }
    Ref<Head> head() const { return swofty::detail::payload_ref<Head>(head_id); }
    Result set_head(const Ref<Head> &head)
    {
        if (!set_head_fn)
            return Result::unavailable;
        std::string id(head.id());
        return swofty::detail::result(set_head_fn(user, id.c_str()));
    }
};

}
```

</template>
</Example>

The career operations on drivers, `career::set_car_number` and `career::set_driver_ratings`, are on [Career](/std/systems/career).

## Driver Manager, in code

<Split>

Driver Manager creates new drivers from a launcher page. The page writes them to the addon's storage, and the addon registers each one in the game.

- **A variant of Jarno Opmeer.** The new driver starts from every key of `game/jarno_opmeer` and changes its own fields. Layers on Jarno's row reach it until it sets a field itself.
- **The core allocates the ids** from the install's stat-id store: driver ids in 208 to 253, look ids from 366. An id is reused only when no career uses it.
- **The look is required.** A seated driver with no look hangs the next session load, and the core refuses an identity without one.
- About 150 records per driver: the row, the look, the statistics holder, the entrant and the pool rows.
- **No `load_after: ["*"]`.** The addon references records, not row positions another mod may have moved.

<template #example>
<Example root="swofty.driver-manager" code-file="native/src/drivers.cpp" data-file="registry/drivers.json" files="swofty.json, launcher/src/main.rs, bin/driver-manager-helper.exe">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/storage.hpp>
#include <swofty/systems/drivers.hpp>
#include <swofty/systems/wearables.hpp>

#include "drivers_json.hpp"   // parses the page's drivers.json

using namespace swofty;

SWOFTY_REGISTER
{
    std::string json;
    if (storage::read("drivers.json", json) != storage::Result::ok)
        return;

    for (const dm::Driver &d : dm::parse(json)) {
        auto look = drivers::looks.add(d.key + "_look", {
            .helmet = wearables::helmets[d.helmet],
            .suit = wearables::suits[d.suit],
            .gloves = wearables::gloves[d.gloves],
            .pose = d.pose,
            .podium_emote = d.emote,
        });

        auto driver = drivers::identities.variant(d.key, drivers::identities["game/jarno_opmeer"],
            [=](drivers::Identity &i) {
                if (!d.person.empty())
                    i.person = drivers::people[d.person];             // "person/mika_salo"
                i.names = { Text::literal(d.first), Text::literal(d.last), Text::literal(d.full), d.code };
                i.nationality = d.nationality;
                i.number = d.number;
                i.head = drivers::heads[d.head];
                i.look = look;
                i.audio_name = d.commentary_name;
                i.seasons_completed = d.seasons;
                i.garage_slot = d.garage_slot;
                if (d.active)
                    i.pools = { "f1_2026_driver_pool", "f1_2025_driver_pool" };
            });

        drivers::stats.add(d.key, {
            .driver = driver,
            .experience = { d.experience.start, d.experience.peak },
            .racecraft  = { d.racecraft.start,  d.racecraft.peak },
            .awareness  = { d.awareness.start,  d.awareness.peak },
            .pace       = { d.pace.start,       d.pace.peak },
            .focus      = { d.focus.start,      d.focus.peak },
        });
    }
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
// registry/drivers.json: one driver, written by hand
{
  "driver.look": {
    "mika_salo_look": { "helmet": "game/helmet_tier01_livery03", "suit": "game/suit_generic_01",
                        "gloves": "game/gloves_generic_01", "pose": "arms_crossed", "podium_emote": "fist_pump" }
  },
  "driver.identity": {
    "mika_salo": {
      "extends": "game/jarno_opmeer", "person": "person/mika_salo",
      "names": { "first": { "text": "Mika" }, "last": { "text": "Salo" }, "full": { "text": "Mika Salo" }, "code": "SAL" },
      "nationality": "finland", "number": 17, "head": "game/male_ls222", "look": "swofty.driver-manager/mika_salo_look",
      "audio_name": "salo", "pools": ["f1_2026_driver_pool", "f1_2025_driver_pool"]
    }
  },
  "driver.stats": {
    "mika_salo": { "driver": "swofty.driver-manager/mika_salo",
                   "pace": { "start": 78, "peak": 84 }, "racecraft": { "start": 75, "peak": 82 } }
  }
}
```

</template>
</Example>
</template>
</Split>

```text
[registry] register: swofty.driver-manager (driver_manager.dll) 3 driver.identity, 3 driver.look, 3 driver.stats (early stage, 0.6 ms)
[registry] stat ids: driver.identity swofty.driver-manager/mika_salo -> 208 (pool 208-254, 45 free after this)
[registry] stat ids: driver.look swofty.driver-manager/mika_salo_look -> 366
[registry] driver.identity swofty.driver-manager/mika_salo: extends game/jarno_opmeer (129 keys), is person/mika_salo
[registry] applied at boot: driver.identity 3, driver.look 3, driver.stats 3 (452 database records; 11 strings)
```

## Real people

<Split>

Two 2016 packs can both add Rio Haryanto. Each says its driver is `person/rio_haryanto`, the same way a track provides a venue.

- **A `person/` id works as a driver reference.** `drivers::identities["person/rio_haryanto"]` resolves to the last enabled driver in load order that is that person.
- **Career addons and line-ups name people,** not one pack's keys. Progressive's 2016 season then works with whichever 2016 pack provides its drivers.
- **Each pack's driver is still its own row** with its own stat id. The conflicts view lists both.

<template #example>
<Example root="swofty.classic-seasons-2016" code-file="native/src/drivers.cpp" data-file="registry/drivers.json" files="swofty.json, native/src/content.cpp">
<template #code>

```cpp
SWOFTY_REGISTER
{
    drivers::identities.variant("haryanto", drivers::identities["game/jarno_opmeer"], [](drivers::Identity &i) {
        i.person = drivers::people["person/rio_haryanto"];
        i.names = { Text::key("lng_cs_rio"), Text::key("lng_cs_haryanto"), Text::key("lng_cs_rio_haryanto"), "HAR" };
        i.nationality = "indonesia";
        i.number = 88;
    });

    // elsewhere: a seat names the person, whichever pack provides him
    auto haryanto = drivers::identities["person/rio_haryanto"];
}
```

</template>
<template #data>

```json
{
  "driver.identity": {
    "haryanto": { "extends": "game/jarno_opmeer", "person": "person/rio_haryanto",
                  "names": { "first": "lng_cs_rio", "last": "lng_cs_haryanto", "full": "lng_cs_rio_haryanto", "code": "HAR" },
                  "nationality": "indonesia", "number": 88 }
  }
}
```

</template>
</Example>
</template>
</Split>

```text
[registry] driver.person person/rio_haryanto: provided by swofty.classic-seasons-2016/haryanto, crl.f1-2016-pack/haryanto;
           resolves to crl.f1-2016-pack/haryanto (later in load order)
```

## Heads

A head is its archive, its morph poses and a rig row copied from a stock head. An imported face mod provides head records, and Driver Manager lists them with `drivers::heads.all()`:

```cpp
auto senna = drivers::heads.add("senna", {
    .archive = file("heads/senna/"),
    .morph = file("heads/morph_poses/senna.erp"),
    .rig_from = drivers::heads["game/male_ls222"],
});
drivers::identities.layer(drivers::identities["swofty.f1-1990/ayrton_senna"], [=](drivers::Identity &i) { i.head = senna; });
```

```text
[registry] driver.head mod.senna-face/senna: rig from game/male_ls222, 2 asset groups added to the manifest, stat id 286
```

## Stock drivers

<Split>

A change to a stock driver is an edit on `game/<driver>`. Driver Manager sets the head and helmet of drivers the game already has, and Helmet Exchange and Classic Seasons change the same looks. Each edit names the fields it changes, load order decides each field, and the conflicts report shows the contest.

<template #example>
<Example root="swofty.driver-manager" code-file="native/src/stock.cpp" data-file="registry/layers.json" files="swofty.json">
<template #code>

```cpp
SWOFTY_REGISTER
{
    drivers::identities.layer(drivers::identities["game/daniil_kvyat"], [](drivers::Identity &i) {
        i.head = drivers::heads["mod.kvyat-face/kvyat"].or_else(i.head);
    });
    drivers::looks.layer(drivers::looks["game/daniil_kvyat"], [](drivers::Look &l) {
        l.helmet = wearables::helmets["giggles.helmet-exchange/kvyat_2017"].optional();
    });
}
```

</template>
<template #data>

```json
{
  "driver.identity": { "game/daniil_kvyat": { "head": "mod.kvyat-face/kvyat|game/kvyat" } },
  "driver.look":     { "game/daniil_kvyat": { "helmet": "giggles.helmet-exchange/kvyat_2017?" } }
}
```

</template>
</Example>
</template>
</Split>

## Ratings, numbers and heads at run time

```cpp
// driver.ratings.read: the game reads statistics once at boot; this runs when the core builds the ratings table
events::on<drivers::events::RatingsRead>().in(events::owner("swofty.progressive"))
    .before([](drivers::events::RatingsRead &r, const events::Context &ctx) {
        r.value = progressive::season_rating(r.driver(), r.stat, ctx.career()->year, r.value);
    });

// driver.number.assign: keep a driver's own number
events::on<drivers::events::NumberAssign>().replace([](drivers::events::NumberAssign &n, const events::Context &) {
    if (n.driver().id() != "swofty.f1-1990/ayrton_senna") return false;
    n.number = 27;
    return true;
});

drivers::set_look(drivers::identities["game/lewis_hamilton"], drivers::looks["swofty.classic-seasons-2017/hamilton"]);
```

A career's season ratings are `career.driver_ratings` records (see [Career](/std/systems/career)). A career freezes each season's ratings when that season starts. A ratings fix for 2019 reaches a running career that hasn't reached 2019 yet. A career mode can freeze everything at creation instead (`career::Mode::freeze`).

## Readiness and limits

- **Partial to ready.** The ratings getters (`Career_Get_DriverOverallRating`, `Career_Get_DriverStatValue`), the number check (`Career_DriverNumbers_CheckExisting`) and the entrant parse (`Sess_Parse_Entrant`) are L2. Heads and looks (`Ent_EntrantsSystem_FindDriverHead`, `Career_DriverNumbers_AssignFree`) are L1, but proven by data.
- **46 free driver ids** in 208 to 253. The core allocates them from the install's store, and two addons that pick ids by hand collide.
- **About 150 records per new driver:** the row, the statistics holder, the accolade row, the entrant row, wear rates, seasonal infos, audio names and the head.
- **A driver with no look hangs the session.** The core refuses a `driver.identity` that has a `donor` and no look.
- **A `driver.identity` with no `donor` copies no row.** It names a driver row that a build operation or the game made, so other records can refer to it. Classic Seasons' shared drivers (`felipe_nasr`, `esteban_gutierrez` and others) are records of this kind.
- **Driver records apply at boot.** `driver.identity` copies its `donor`'s `entrants.drivers` row (a variant copies its base's) with the names (a literal or a lang key), initials, nationality, number, head, commentary name, seasons completed and garage slot and any other key of the row set by a field of the same name (`date_of_birth.year`); the donor's wear-rate row is copied under the new label and the donor's statistics holder under the new label unless a `driver.stats` record names the driver. `driver.stats` sets the five ratings curves on the holder: `@v2@minx:0,maxx:10,miny:0,maxy:100,0:<start>,0:<start>,0.9:<peak>,1:<peak>`, start and peak out of 100. `driver.head` copies its `rig_from` row (the first head of its body type when there is none), writes the face and morph archives as asset groups under `animation_package/scenes/cinematics/drivers/heads` and `morph_poses`, and a folder in `archive` or `morph` means `<folder><key>.erp`. `driver.look` adds a row to the official driver customisations folder naming the helmet, suit and gloves and linked to the driver whose identity names it. `driver.person` writes nothing: `person/<key>` resolves to the last enabled identity in load order that names it, in package seats, `set_look` and the registry's references.
- **A package seat with no `look`** takes its driver's look. `pools` on an identity is not applied.
- **`drivers::set_look`** puts a driver in a look in the loaded Driver Career through the career operations (table version 6), by the line-up executor's looks. It is unavailable on a core without table version 6 and invalid for a look or driver the game did not load.
- **Statistics are read once at boot.** `driver.stats` applies at boot. In a career, `driver.ratings.read` changes them. It's a hot hook point: its handlers run once per driver and statistic when the core builds the ratings table (a box picked, a career loaded, a session loaded), and the game's calls read the table.
- **Ratings in Grand Prix and Time Trial** are the base ratings in every box. Whether a Grand Prix session reads `DriverStatisticsComponent` at all needs reverse engineering.
- **Heads are found by label only.** A generic head looks wrong with a face map. Use a scanned donor head. Only 9 real retro heads ship in `character_package/heads/male_driver/`.
- **The entrant parse reads the team label unchecked,** and a missing team crashes. The core checks the reference first.
