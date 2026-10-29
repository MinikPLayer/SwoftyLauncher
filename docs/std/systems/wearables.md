---
title: Helmets, suits and gloves
description: Helmets, suits, gloves and team kits per season in code - typed registries, the catalogue event and the look operation.
---

# Helmets, suits and gloves

Add helmets drivers can pick, give a team its suits and gloves for a season, or put a helmet on one driver for a whole career. Helmet Exchange's Hamilton 2017 helmet, for example, is one `wearables::Helmet` record: its archive, the stock helmet it starts from, its name in the picker and the driver who can pick it.

A few terms used on this page:

- A **helmet**, **suit** or pair of **gloves** is a record with its archive and colours: one item row in the game's customisation tables.
- A **season set** is a `wearables::SeasonSet`: a team's suit and gloves for one year.
- A driver wears items through a **look**, a `drivers::Look` record (see [Drivers](/std/systems/drivers)).
- A **career-scope** edit applies only inside one career. A **layer** changes values on a record that already exists.

## The header

<Example root="swofty-sdk" code-file="include/swofty/systems/wearables.hpp">
<template #code>

```cpp
#include <swofty/registry.hpp>

namespace swofty::wearables {

using Result = swofty::Result;

struct Helmet {                                    // a customisation.custom_avatar.helmets row and its archive
    static constexpr Version version{1, 0};
    Path archive;                                  // the helmet archive (or the resources of one)
    Ref<Helmet> from;                              // the shell and row it starts from
    Text name;                                     // its name in the picker
    Ref<drivers::Identity> driver;                 // who can pick it; empty: any driver
    std::optional<std::array<Colour, 4>> colours;  // colour_1..4_default; empty: the core takes them from the `from` row
    bool official_team_item = false;               // is_official_team_item
    StatId stat_id = StatId::automatic;            // the item id
    json::Value extra;
};
SWOFTY_RECORD(Helmet, archive, from, name, driver, colours, official_team_item, stat_id)

struct Suit {                                      // a suits row and its archive
    static constexpr Version version{1, 0};
    Path archive;
    Ref<Suit> from;
    Ref<teams::Identity> team;                     // the team it belongs to
    std::optional<std::array<Colour, 4>> colours;  // colour_N_default
    StatId stat_id = StatId::automatic;
    json::Value extra;
};
SWOFTY_RECORD(Suit, archive, from, team, colours, stat_id)

struct Gloves {                                    // a gloves row and its archive
    static constexpr Version version{1, 0};
    Path archive;
    Ref<Gloves> from;
    Ref<teams::Identity> team;
    std::optional<std::array<Colour, 4>> colours;
    StatId stat_id = StatId::automatic;
    json::Value extra;
};
SWOFTY_RECORD(Gloves, archive, from, team, colours, stat_id)

struct SeasonSet {                                 // one team_customisation_for_year row
    static constexpr Version version{1, 0};
    Ref<teams::Identity> team;                     // the team
    int season = 0;                                // the year
    Ref<Suit> suit;                                // its suit that season
    Ref<Gloves> gloves;                            // its gloves that season
    json::Value extra;
};
SWOFTY_RECORD(SeasonSet, team, season, suit, gloves)

inline Registry<Helmet> helmets{"helmet", { .applies = Applies::boot }};
inline Registry<Suit> suits{"suit", { .applies = Applies::boot }};
inline Registry<Gloves> gloves{"gloves", { .applies = Applies::boot }};
inline Registry<SeasonSet> season_sets{"wearable.season_set", { .applies = Applies::boot }};

}

namespace swofty::wearables::events {

struct CatalogueBuild {                            // Game_Build_CustomisationCatalogue: after
    static constexpr const char *point = "wearable.catalogue.build";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    int32_t (*add_fn)(void *user, const char *registry, const char *id);
    void *user;

    Result add(const Ref<Helmet> &item) { return add_item("helmet", item.id()); }
    Result add(const Ref<Suit> &item) { return add_item("suit", item.id()); }
    Result add(const Ref<Gloves> &item) { return add_item("gloves", item.id()); }

private:
    Result add_item(const char *registry, std::string_view id)
    {
        if (!add_fn)
            return Result::unavailable;
        std::string s(id);
        return swofty::detail::result(add_fn(user, registry, s.c_str()));
    }
};

}
```

</template>
</Example>

A driver wears items through a [look](/std/systems/drivers): `drivers::set_look` and `drivers::looks`.

## Helmet Exchange, in code

<Split>

Helmet Exchange turns helmet mods into helmets of their own, instead of replacing a driver's helmet. Its launcher page converts each mod on the player's PC and writes the list to the addon's storage. The addon registers each helmet in the game.

- **Who can pick it:** the driver the mod was made for (in any year or team), another driver, or any driver.
- **The career choice** is a career-scope edit on the player's look. It's worn in every session of that career and nowhere else.
- In the game, helmet names end in (HX).
- **An imported helmet applies everywhere.** The importer doesn't ask where. Applies to on the importer page puts it on another driver too, and the original driver keeps it.

<template #example>
<Example root="giggles.helmet-exchange" code-file="native/src/helmets.cpp" data-file="registry/helmets.json" files="swofty.json, helmets/hamilton_2017_helmet.erp, launcher/main.go, THIRD_PARTY_NOTICES.md">
<template #code>

```cpp
#include <swofty/events.hpp>
#include <swofty/registry.hpp>
#include <swofty/storage.hpp>
#include <swofty/systems/drivers.hpp>
#include <swofty/systems/wearables.hpp>

#include "imports.hpp"   // parses the page's helmets.json

using namespace swofty;

SWOFTY_REGISTER
{
    std::string json;
    if (storage::read("helmets.json", json) != storage::Result::ok)
        return;

    for (const hx::Import &h : hx::parse(json)) {
        if (!h.enabled)
            continue;
        wearables::helmets.add(h.key, {
            .archive = file("helmets/" + h.key + "_helmet.erp"),
            .from = wearables::helmets[h.shell],                         // game/lewis_hamilton for career helmets
            .name = Text::literal(h.name + " (HX)"),
            .driver = h.any_driver ? Ref<drivers::Identity>{} : drivers::identities[h.driver],
            .colours = h.colours,
        });
    }
}

SWOFTY_ON_LOAD
{
    // the Helmets tab saved a choice for this career: wear it in every session of it
    events::on<career::events::Loaded>().after([](const career::events::Loaded &, const events::Context &ctx) {
        auto pick = hx::career_choice(ctx.career()->id);
        if (pick.empty())
            return;
        drivers::looks.layer(drivers::looks[ctx.player()->driver.id()], [pick](drivers::Look &l) {
            l.helmet = wearables::helmets["giggles.helmet-exchange/" + pick];
        }, Scope::career);
    });
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
// registry/helmets.json: one imported helmet, written by hand
{
  "helmet": {
    "hamilton_2017": {
      "archive": "helmets/hamilton_2017_helmet.erp", "from": "game/lewis_hamilton",
      "name": { "text": "Hamilton 2017 (HX)" }, "driver": "game/lewis_hamilton",
      "colours": ["#FFE600", "#00A19B", "#7A2A8B", "#FFFFFF"]
    }
  }
}
```

The career choice has no data form: it needs the Helmets tab's code.

</template>
</Example>
</template>
</Split>

```text
[registry] register: giggles.helmet-exchange (helmet_exchange.dll) 12 helmet (early stage, 0.5 ms)
[registry] stat ids: helmet giggles.helmet-exchange/hamilton_2017 -> 1041
[registry] applied at boot: helmet 12 (12 rows, 12 asset groups with deps on the team livery group, added to the asset manifest)
[registry] layer from giggles.helmet-exchange on driver.look game/lewis_hamilton registered at run time (career); applies from the next session load
[registry] content career 3f2a active: 1 scoped change applied (driver.look game/lewis_hamilton /helmet)
```

## Team kit per season

<Split>

Classic Seasons gives each season's teams their suits and gloves. A season set is one `team_customisation_for_year` row, and the suit and gloves are records of their own. A season's own team kit wins over the base kit in that season.

<template #example>
<Example root="swofty.classic-seasons-2016" code-file="native/src/kit.cpp" data-file="registry/kit.json" files="swofty.json, kit/mercedes_2016_suit.erp, kit/mercedes_2016_gloves.erp">
<template #code>

```cpp
SWOFTY_REGISTER
{
    auto mercedes = teams::identities["swofty.classic-seasons-2016/mercedes"];
    auto suit = wearables::suits.add("mercedes_2016", {
        .archive = file("kit/mercedes_2016_suit.erp"), .from = wearables::suits["game/mercedes"], .team = mercedes,
    });
    auto gloves = wearables::gloves.add("mercedes_2016", {
        .archive = file("kit/mercedes_2016_gloves.erp"), .from = wearables::gloves["game/mercedes"], .team = mercedes,
    });
    wearables::season_sets.add("mercedes_2016", { .team = mercedes, .season = 2016, .suit = suit, .gloves = gloves });
}
```

</template>
<template #data>

```json
{
  "suit":   { "mercedes_2016": { "archive": "kit/mercedes_2016_suit.erp",   "from": "game/mercedes", "team": "swofty.classic-seasons-2016/mercedes" } },
  "gloves": { "mercedes_2016": { "archive": "kit/mercedes_2016_gloves.erp", "from": "game/mercedes", "team": "swofty.classic-seasons-2016/mercedes" } },
  "wearable.season_set": {
    "mercedes_2016": { "team": "swofty.classic-seasons-2016/mercedes", "season": 2016,
                       "suit": "swofty.classic-seasons-2016/mercedes_2016", "gloves": "swofty.classic-seasons-2016/mercedes_2016" }
  }
}
```

</template>
</Example>
</template>
</Split>

## Items without rows

```cpp
// wearable.catalogue.build: add a helmet to the customisation catalogue at run time
events::on<wearables::events::CatalogueBuild>().before([](wearables::events::CatalogueBuild &c, const events::Context &) {
    c.add(wearables::helmets["giggles.helmet-exchange/hamilton_2017"]);
});
```

## Readiness and limits

- **Records: ready,** shipped at scale. The Classic Seasons shared addon has 124 season helmets (ids 868 to 1040, 253 MB) and suits with ids 974 to 1034.
- **Helmet, suit and gloves records apply at boot.** Each is a row copied from its `from` row (the table's first row when there is none) with the row's `id` allocated above the table's highest id (up to 4095), the four `colour_N_default` keys from `colours` (signed ARGB, `#RRGGBB` opaque) or the `from` row's, `name`, `official_team_item`, and any other key of the row set by a field of the same name in the record. A record with an `archive` also gets an asset group: a team's suit and gloves that name the same archive share `team_livery/<key>/<key>`, a driver's suit and gloves use `drivers/male/<key>/<key>` and a helmet `drivers/male/<key>/<key>_helmet`. The group copies the `from` row's group and lists the `from` row's archive first. `asset_group`, `material_override`, `feet_model`, `logos_model_override_pattern`, `logos_model` and `attachments_model` are set from the key by the stock name pattern (`_male` and `_body_logos_[GENDER]` for a team suit, `_glove_logos_[GENDER]` for team gloves). A record without an `archive` keeps the `from` row's group and takes only its colours.
- **A season set writes a kit row only when it names a suit or gloves.** The row goes into the year row for its `season`, or the nearest year row when the game has none (the game's pick falls back to any year of the team). `linked_team` is the team's label; `official_driver_customisations` lists the looks of the drivers in the record's `drivers` list. A season set with neither suit nor gloves writes no row: each driver of a team whose seats all have looks wears his own look.
- **Events: unknown (L1).** `Game_Build_CustomisationCatalogue`, the helmet decal, spawn and thumbnail functions are L1.
- **Applied in game:** a helmet, suit and gloves record each applied at boot with their asset groups, and `driver.look` and `driver.identity` records applied. A donor must be a real row (`game/2025-sauber`, `game/2025-redbull_max_verstappen`); a donor with no row applies nothing and logs `no row ... to copy`.
- **In-game checks (T136, T137) are open:** which look, team and season reach the spawner in the garage, the grid and cutscenes, and whether a look no entrant names is loaded.
- **Every item row needs every printf-built colour key** (`colour_N_default`). The catalogue build crashes on a row without them. The core fills them from `colours` or the `from` row.
- **Asset groups need deps on the team livery group,** or cutscenes crash. The core adds them to the asset manifest at boot with the deps, and the game reads the archives from the addon folder.
- **Customisable designs** (`tier01_liveryNN`) have no diffuse. The game composites them from `_m` masks with the row's default colours.
- **Converted F1 23 materials render invisible,** and `.mipmaps` sidecar names must match exactly, year suffix included.
- **The Helmets tab** lists up to 63 helmets per driver.
- **Per-event helmets** (a helmet for one race weekend) have no record.
