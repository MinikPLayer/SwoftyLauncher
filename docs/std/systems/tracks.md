---
title: Tracks
description: Tracks in code - tracks, venues, per-season track data, the track list each Grand Prix and Time Trial box offers, world layers, slots and AI lines as typed registries, with imported tracks and the track events and operations.
---

# Tracks

Add a track to the game, give it lap counts, fuel and tyres for each season, and decide which Grand Prix and Time Trial boxes offer it. Hockenheim, for example, is one `tracks::Track` record: it starts from a copy of Austria, provides the real circuit `venue/hockenheimring`, and shows up in every box it fits.

A few terms used on this page:

- A **track** is a `tracks::Track` record: the track's files, its database rows and its names.
- A **venue** is a real circuit, with an id under `venue/` that the core owns. Calendars name venues, not tracks. A track **provides** one or more venues.
- A **data set** is one of the game's sets of track data, such as `f1_2026` or `sprint_f1_2026`. Every track has a `tracks::TrackData` block in each set: laps, fuel, tyres, pit lane, names and the weekend's date.
- A **box** is one of the year tiles a player picks in Grand Prix or Time Trial. In code it's a [content package](/std/content).
- The **base** is the data every year shares. A track an addon adds goes into the base.

::: info In 1.6.0
The header `swofty/systems/tracks.hpp` has `tracks::set_track_fuel`, `copy_track_fuel`, `restore_track_fuel`, `tracks::Fuel` and the `session.fuel.load` payload `tracks::events::FuelLoad`. The registries on this page (`tracks`, `venues`, `data`, `per_package`, `layers`, `slots`, `ai_lines`) and `tracks::set_data` ship in 1.6.x; their sections describe that design and are not callable in 1.6.0.
:::

## The header

<Example root="swofty-sdk" code-file="include/swofty/systems/tracks.hpp">
<template #code>

```cpp
#include <swofty/registry.hpp>

namespace swofty::tracks {

struct Venue {                                     // a real circuit; the core owns the venue/ ids
    std::string name;                              // "Hockenheimring"
    std::string country;                           // "germany"
    Ref<Track> track;                              // resolved: the last enabled track in load order that provides it
};

struct TrackNames {                                // TrackNameData: menus read names here, not from tracks.Track
    Text title;                                    // the track's title
    Text gp;                                       // the grand prix's name
    std::string acronym;                           // "HOC"
    Text city;                                     // the city
};

struct Track {                                     // a tracks.Track row, its files, and its rows in every data set
    std::string code;                              // the track code: its folder and row label
    std::string name;                              // shown in logs and the conflicts report
    Ref<Track> base;                               // the stock track its rows are cloned from, in every data set (a variant)
    Ref<Track> slot;                               // a dormant stock slot to fill (game/sochi); empty: a new row
    std::vector<Ref<Venue>> provides;              // the real circuits it is
    TrackNames names;                              // its names
    Path files;                                    // track/<code>.erp, mipmaps/, ui/
    float length_metres = 0;                       // length_metres
    int corners = 0;                               // numberOfCorners
    float latitude = 0, longitude = 0;             // for daylight and weather
    bool time_trial = false;                       // offered in Time Trial boxes; stock tracks are true
    StatId stat_id = StatId::automatic;            // from the install's stat-id store; careers carry their own map
};

struct Fuel { float burn_per_lap; };               // TrackGeneralData.fuel_burn_per_lap (+ RaceFuelSettings)
struct Tyres { std::string softer, medium, harder; };   // TyreData: compound_softer / _medium / _harder
struct Pit { int speed_limit; float stop_time_dry, stop_time_wet; };   // TrackPitData
struct Date { int day, month; };                   // weekend_day, weekend_month

struct TrackData {                                 // one TrackData block; id "<track id>/<data set>"
    Ref<Track> track;                              // the track
    std::string data_set;                          // a TrackDataSet: "f1_2026", "sprint_f1_2026", "classic" ...
    int laps = 0;                                  // number_of_laps
    Fuel fuel;                                     // fuel
    Tyres tyres;                                   // the dry compounds
    Ref<tyres::Class> tyre_class;                  // or a tyre class (see Tyres)
    Pit pit;                                       // pit lane
    Ref<cars::Vehicle> safety_car;                 // TrackSafetyCarData.safety_car_vehicle
    TrackNames names;                              // per-set names
    Date date;                                     // the weekend's date
};

struct PackageTrack {                              // one box's data for one track; id "<package id>/<key>"
    Ref<content::Package> package;                 // the box's package
    Ref<Track> track;                              // one track; empty with no venue: the track whose code ends the id
    Ref<Venue> venue;                              // or a venue: the data goes to whichever track the venue resolves to
    bool offered = true;                           // false: the box leaves this track out
    int order = 0;                                 // its place under "More tracks"; 0 keeps the base order
    Ref<Layer> world;                              // the layout or world version for this box
    std::optional<int> laps;                       // empty fields come from the base track data, layers included
    std::optional<Fuel> fuel;
    Ref<tyres::Class> tyre_class;
    std::optional<Pit> pit;
    std::optional<TrackNames> names;
    std::optional<Date> date;
};

struct World { std::string asset_group; std::string eri; };   // worldAssetGroup, worldEri

struct Layer {                                     // a track_data.track_layers row: a world per mode
    Ref<Track> track;
    std::string mode;                              // "f1", "f2", "myteam", "swofty_f1" ...
    World world;                                   // <track>_f1_26.wep (22 grid, 11 garages) or _myteam (24, 12)
    int grid = 22;                                 // grid slots the world has
    int garages = 11;                              // garages the world has
};

struct Slots {                                     // grid slots, garages and pit boxes added to one world
    Ref<Layer> layer;
    std::vector<Transform> grid, garages, pit_boxes;
};

struct AiLine {                                    // baked AI data: ai/<package folder>/<asset_filename>/*.erp
    Ref<Track> track;
    Ref<cars::Package> package;
    Path archive;
};

struct LeftOut { Ref<Track> track; std::string reason; };   // "10 garages; the box has 11 teams"

struct TrackList {                                 // what a box offers in one mode
    std::vector<Ref<Track>> calendar;              // the season's real calendar, in round order
    std::vector<Ref<Track>> more;                  // every other track that fits: "More tracks"
    std::vector<LeftOut> left_out;                 // the tracks that don't fit this box, and why
};

inline Registry<Track> tracks{"track", { .applies = Applies::boot }};
inline Registry<Venue> venues{"track.venue", { .applies = Applies::boot }};
inline Registry<TrackData> data{"track.data", { .applies = Applies::session }};
inline Registry<PackageTrack> per_package{"track.per_package", { .applies = Applies::content }};
inline Registry<Layer> layers{"track.layer", { .applies = Applies::boot }};
inline Registry<Slots> slots{"track.slots", { .applies = Applies::boot }};   // patched into the worlds for the install's grid size
inline Registry<AiLine> ai_lines{"track.ai_line", { .applies = Applies::session }};

TrackList track_list(Ref<content::Package> box, events::Mode mode);   // grand_prix or time_trial
Result set_data(Ref<Track> track, Edit<TrackData> edit);         // 1.6.x: the loaded track object; put back on unload
Result set_track_fuel(Ref<Track> track, const Fuel &fuel, std::string_view data_set = "f1_2026");   // shipped; core ops table v4
Result copy_track_fuel(std::string_view track, std::string_view data_set, std::string_view from, std::string_view why = {});   // shipped; "*" is every track
Result restore_track_fuel(std::string_view why = {});             // shipped; also done when the career unloads

}

namespace swofty::tracks::events {

struct DataGet {                                   // Game_TrackData_GetTrack: after
    static constexpr const char *point = "track.data.get";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    std::string_view data_set;                     // the set the game reads
    Ref<Track> track;
    TrackData &data;                               // the live object; after handlers may change it
};

struct FuelLoad {                                  // the race fuel lookup at session load (cancel)
    static constexpr const char *point = "session.fuel.load";
    static constexpr uint32_t version = 1;
    enum Kind : uint8_t { practice = 0, qualifying = 1, race = 2, other = 3 };
    uint32_t size, payload_version;
    const char *track_id;
    uint32_t session_type;
    float kg;                                      // the race fuel load; before and replace handlers may set it
    uint8_t kind;

    Ref<Track> track() const { return swofty::detail::payload_ref<Track>(track_id); }
};

}
```

</template>
</Example>

Race fuel at session load is `session.fuel.load` (`tracks::events::FuelLoad`). A box's race fuel rule is `sessions::Rules::race_fuel_kg` on [Sessions and weekends](/std/systems/sessions).

## Hockenheim, in code

<Split>

Hockenheim is a new track, not a stock slot. It clones Austria's rows in every data set and provides `venue/hockenheimring`.

- **Every data set gets a block,** cloned from Austria's as a variant. A variant is a record that inherits another record's values and overrides some of them. A layer, a change to an existing record, on Austria's data reaches Hockenheim until Hockenheim sets that field itself.
- **A selector** layers every record that matches, here every one of Hockenheim's data blocks. An edit on one set changes that set only.
- **The core allocates the stat id,** the number the game's database uses for the row. It comes from the install's stat-id store and stays the same at every later start.
- **The world** keeps the stock counts: `hockenheim_f1_26` has 22 grid slots and 11 garages, and the My Team world has 24 and 12.
- **Time Trial is off.** `time_trial` stays `false` for an added track. Time Trial on added tracks crashes or freezes, and the cause needs reverse engineering.

<template #example>
<Example root="mod.hockenheim" code-file="native/src/track.cpp" data-file="registry/track.json" files="swofty.json, track/hockenheim.erp, mipmaps/hockenheim_d.tif.mipmaps, ui/minimap.json, ui/images.json, lang/track.json">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/tracks.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    auto hockenheim = tracks::tracks.add("hockenheim", {
        .code = "hockenheim",
        .name = "Hockenheimring",
        .base = tracks::tracks["game/austria"],
        .provides = { tracks::venues["venue/hockenheimring"] },
        .names = { .title = Text::key("lng_hockenheim_title"), .gp = Text::key("lng_hockenheim_gp"), .acronym = "HOC" },
        .files = file("track/"),
        .length_metres = 4574, .corners = 17, .latitude = 49.3278f, .longitude = 8.5658f,
    });

    // every data set: 67 laps, its own fuel and pit lane
    tracks::data.layer(under<tracks::TrackData>("mod.hockenheim/hockenheim/"), [](tracks::TrackData &d) {
        d.laps = 67;
        d.fuel.burn_per_lap = 1.62f;
        d.pit.speed_limit = 80;
    });

    // the 2025 and 2026 sets: the 2019 compounds
    for (const char *set : { "f1_2025", "sprint_f1_2025", "f1_2026", "sprint_f1_2026" })
        tracks::data.layer(tracks::data[std::string("mod.hockenheim/hockenheim/") + set], [](tracks::TrackData &d) {
            d.tyres = { .softer = "C4", .medium = "C3", .harder = "C2" };
        });
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
// registry/track.json
{
  "track": {
    "hockenheim": {
      "code": "hockenheim", "name": "Hockenheimring", "base": "game/austria",
      "provides": ["venue/hockenheimring"],
      "names": { "title": "lng_hockenheim_title", "gp": "lng_hockenheim_gp", "acronym": "HOC" },
      "files": "track/", "length_metres": 4574, "corners": 17, "latitude": 49.3278, "longitude": 8.5658
    }
  },
  "track.data": {
    "$select": [
      { "ids": "mod.hockenheim/hockenheim/*", "patch": { "laps": 67, "fuel": { "burn_per_lap": 1.62 }, "pit": { "speed_limit": 80 } } }
    ],
    "mod.hockenheim/hockenheim/f1_2025":        { "tyres": { "softer": "C4", "medium": "C3", "harder": "C2" } },
    "mod.hockenheim/hockenheim/sprint_f1_2025": { "tyres": { "softer": "C4", "medium": "C3", "harder": "C2" } },
    "mod.hockenheim/hockenheim/f1_2026":        { "tyres": { "softer": "C4", "medium": "C3", "harder": "C2" } },
    "mod.hockenheim/hockenheim/sprint_f1_2026": { "tyres": { "softer": "C4", "medium": "C3", "harder": "C2" } }
  }
}
```

</template>
</Example>
</template>
</Split>

```text
[registry] register: mod.hockenheim (hockenheim.dll) 1 track, 5 layers on track.data (early stage, 0.3 ms)
[registry] track mod.hockenheim/hockenheim: base game/austria, new row, provides venue/hockenheimring
[tracks]   hockenheim: stat id 43 (install store, ids.json)
[registry] applied at boot: track 1 (19 data sets cloned from game/austria; 77 asset groups; 1,712 database records)
[registry] applied at session load: track.data mod.hockenheim/hockenheim/f1_2026 (laps 67, fuel 1.62, tyres C4/C3/C2)
```

## A calendar that uses it, optionally

<Split>

Classic Seasons 2016 races the 2016 calendar. Round 12 is the German Grand Prix at Hockenheim, which only a track addon provides.

- **A round names a venue.** It resolves live to the last enabled track in load order that provides the venue, the same way every other contest between addons is decided (see [Shared ids](/std/providers#shared-ids)).
- **`.optional()`** drops the round when no enabled track provides the venue.
- **`.or_else(...)`** names a stand-in: with no Sepang, round 16 runs at Bahrain.

<template #example>
<Example root="swofty.classic-seasons-2016" code-file="native/src/calendar.cpp" data-file="registry/calendar.json" files="swofty.json, lang/season.json">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/career.hpp>
#include <swofty/systems/tracks.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    career::calendars.add("f1_2016", { .rounds = {
        { .round = 1,  .race = Text::key("lng_gp_australia"), .venue = tracks::venues["venue/albert_park"] },
        { .round = 4,  .race = Text::key("lng_gp_russia"),    .venue = tracks::venues["venue/sochi"].optional() },
        { .round = 12, .race = Text::key("lng_gp_germany"),   .venue = tracks::venues["venue/hockenheimring"].optional() },
        { .round = 16, .race = Text::key("lng_gp_malaysia"),
          .venue = tracks::venues["venue/sepang"].or_else(tracks::venues["venue/bahrain"]) },
    }});
}
```

</template>
<template #data>

```json
// registry/calendar.json
{
  "career.calendar": {
    "f1_2016": { "rounds": [
      { "round": 1,  "race": "lng_gp_australia", "venue": "venue/albert_park" },
      { "round": 4,  "race": "lng_gp_russia",    "venue": "venue/sochi?" },
      { "round": 12, "race": "lng_gp_germany",   "venue": "venue/hockenheimring?" },
      { "round": 16, "race": "lng_gp_malaysia",  "venue": "venue/sepang|venue/bahrain" }
    ] }
  }
}
```

</template>
</Example>
</template>
</Split>

```text
[registry] career.calendar swofty.classic-seasons-2016/f1_2016: round 12 venue/hockenheimring -> mod.hockenheim/hockenheim
[registry] career.calendar swofty.classic-seasons-2016/f1_2016: round 4 dropped (venue/sochi: no enabled track provides it; optional)
```

The same calendar serves boxes and careers:

- **A box** shows it first in its track list (see the next section).
- **A career** can race each season's real calendar. The career wizard offers the choice, and new careers default to real calendars. The core rebuilds the calendar at each season break from that season's calendar (`career::Season::calendar`), and drops a round whose venue no enabled track provides.

A [guard](/std/dependencies) stops a saved career whose track has gone missing.

## The tracks a box offers

<Split>

Every track in the base is offered in every box where it fits. The track select screen shows the box's season first, then the rest:

1. **The season's real calendar,** in round order. The box names its calendar with `content::Package::calendar`. A round whose venue no enabled track provides is dropped.
2. **"More tracks":** every other track in the base that fits, in the base order. A track an addon adds next month appears here without the box knowing about it.

A track that doesn't fit is left out of that box, with the reason in the log. A track fits a box when all four of these hold:

- **Garages.** The world the box's setup mode loads at that track has a garage for every team in the box. The setup mode is the game's rule set a weekend runs under, and every Grand Prix and Time Trial box shares `swofty_f1`.
- **Grid size.** That world, with any extra slots from `tracks::slots`, has a grid slot for every car the box runs. Grid size is one boot setting per install, and each box is clamped to it.
- **AI data.** Every car in the box has AI data at the track: an AI line for its `ai_package_folder/asset_filename`, and the track's `ai_track_difficulties` entry for its package.
- **Time Trial support.** In a Time Trial box, the track has `time_trial` set.

<template #example>
<Example root="swofty-core" code-file="src/tracks/track_list.cpp" files="include/swofty/systems/tracks.hpp, src/tracks/fits.cpp">
<template #code>

```cpp
// what tracks::track_list() does when the player picks a box
tracks::TrackList tracks::track_list(Ref<content::Package> box, events::Mode mode)
{
    TrackList list;
    auto offer = [&](Ref<Track> track, std::vector<Ref<Track>> &into) {
        Fit fit = fits(box, track, mode);           // fits.cpp: garages, grid size, AI data, Time Trial support
        if (fit.ok)
            into.push_back(track);
        else
            list.left_out.push_back({ track, fit.reason });
    };

    // 1. the season's real calendar, in round order
    auto package = content::packages.get(box);
    if (auto calendar = career::calendars.get(package->calendar))
        for (const career::Round &round : calendar->rounds)
            if (auto venue = venues.get(round.venue))      // no enabled provider: the round is dropped
                offer(venue->track, list.calendar);

    // 2. "More tracks": every other track in the base
    for (Ref<Track> track : tracks.all())
        if (!contains(list.calendar, track) && offered(box, track))   // PackageTrack::offered and ::order
            offer(track, list.more);

    return list;
}
```

</template>
</Example>
</template>
</Split>

```text
[tracks] box swofty.classic-seasons-2016/f1_2016 (grand_prix): calendar 20 of 21 rounds (venue/sochi: no track),
         more tracks 9; left out mod.miami24/igora_drive (its swofty_f1 world has 10 garages; the box has 11 teams)
[tracks] box game/f1_2026 (time_trial): 26 tracks; left out 4 added tracks (no Time Trial support)
```

The track select screen's list builder needs reverse engineering (`ui.track_select.build`, `ui::events::TrackSelectBuild`). Until it's understood, the screen shows every track in the base in the game's own order. A guard on the screen stops a track that doesn't fit before the session loads:

```text
Igora Drive can't run in F1 2016
  Its garages hold 10 teams. F1 2016 has 11.
  [Back]
```

## A box's own track data

<Split>

A box can also give a track its own data: laps, fuel, tyre class, pit lane, names, date and world version. Each entry is a `tracks::PackageTrack`.

- **Name the venue,** not a track code. The data goes to whichever track the venue resolves to: Hockenheim from any author gets it.
- **An empty field comes from the base track data,** and layers on the base reach every box that doesn't set the field.
- **`offered = false`** leaves a track out of the box. **`order`** places a track under "More tracks".
- **A variant package** (F1 2017 V10) inherits its base package's track data.
- **It applies when the box is picked.** The core writes the box's data into the track data the game reads (`Game_TrackData_GetTrack`), and puts it back when the box is left.

<template #example>
<Example root="swofty.classic-seasons-2016" code-file="native/src/tracks.cpp" data-file="registry/tracks.json" files="swofty.json, native/src/calendar.cpp, native/src/content.cpp">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/systems/content.hpp>
#include <swofty/systems/tracks.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    auto box = content::packages["swofty.classic-seasons-2016/f1_2016"];

    // 2016 data at Hockenheim, whoever provides it; nothing applies while no track does
    tracks::per_package.add("f1_2016/hockenheimring", {
        .package = box,
        .venue = tracks::venues["venue/hockenheimring"],
        .laps = 67,
        .fuel = tracks::Fuel{ .burn_per_lap = 1.71f },
    });

    // Monaco's 2016 lap count
    tracks::per_package.add("f1_2016/monaco", { .package = box, .track = tracks::tracks["game/monaco"], .laps = 78 });
}
```

</template>
<template #data>

```json
// registry/tracks.json
{
  "track.per_package": {
    "f1_2016/hockenheimring": { "package": "swofty.classic-seasons-2016/f1_2016", "venue": "venue/hockenheimring",
                                "laps": 67, "fuel": { "burn_per_lap": 1.71 } },
    "f1_2016/monaco":         { "package": "swofty.classic-seasons-2016/f1_2016", "track": "game/monaco", "laps": 78 }
  }
}
```

</template>
</Example>
</template>
</Split>

```text
[registry] content swofty.classic-seasons-2016/f1_2016 active: 2 scoped changes applied
           (mod.hockenheim/hockenheim via venue/hockenheimring: laps 67, fuel 1.71; game/monaco: laps 78)
[registry] content swofty.classic-seasons-2016/f1_2016 ended: 2 scoped changes put back
[registry] content game/f1_2026 active: base track data (mod.hockenheim/hockenheim included)
```

A 2018-only layout addon edits only the 2018 box's entry:

```cpp
tracks::per_package.layer(tracks::per_package["swofty.classic-seasons-2018/f1_2018/hockenheimring"], [](tracks::PackageTrack &t) {
    t.world = tracks::layers["mod.hockenheim-2018/hockenheim_2018"];
});
```

## Imported tracks

<Split>

The importer turns a track zip into a track addon once. The core activates it in the game at every start, and the launcher only places its files.

1. **The importer converts the zip** into `swofty.json`, `ops/track.json` (`track.add`, kept so a 1.5 launcher still installs it), the track's files (`track/<code>.erp`, `mipmaps/`, `ui/`) and `registry/track.json`, a data-only track record.
2. **The importer never guesses a venue from the track's code.** `mod.bahrain24` is Sepang, `mod.hanoi` is Valencia and `mod.miami24` is Igora Drive. It matches the venue on the track's name and shows it on the importer page.
3. **Package fixes happen once, at import:** stand-in worlds, mip paths, passing space on the AI line.
4. **The launcher places the files** at their game paths (`2025_asset_groups/environment_package/tracks/<code>/...`). With a track record present, a 1.6 launcher's `track.add` places files only and leaves the database to the core.
5. **At boot, before the database loads,** the core clones the base track's rows in every data set as a variant, adds the asset groups to the game's asset manifest, adds the menu entries and names, takes the stat id from the install's store and provides the venue. The game reads the track's archives from the addon folder.
6. **Calendars** that ask for the venue resolve live. Per-season track data applies at session load.
7. **Tracks imported with an older launcher:** on upgrade, the importer writes `registry/track.json` from the existing `track/manifest.json`, with no re-import. A 1.5 launcher still does the full activation from `track.add`.

<template #example>
<Example root="mod.sochi" code-file="native/src/track.cpp" data-file="registry/track.json" files="swofty.json, ops/track.json, track/sochi.erp, track/manifest.json, mipmaps/sochi_d.tif.mipmaps, README.md">
<template #code>

```cpp
// the same track, made as an addon with code instead of imported
#include <swofty/registry.hpp>
#include <swofty/systems/tracks.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    tracks::tracks.add("sochi", {
        .code = "sochi",
        .name = "Sochi Autodrom",
        .base = tracks::tracks["game/baku"],
        .slot = tracks::tracks["game/sochi"],                   // the stock row without assets
        .provides = { tracks::venues["venue/sochi"] },
        .files = file("track/"),
    });

    for (const char *set : { "f1_2025", "sprint_f1_2025", "f1_2026", "sprint_f1_2026" })
        tracks::data.layer(tracks::data[std::string("mod.sochi/sochi/") + set], [](tracks::TrackData &d) {
            d.tyres = { .softer = "C5", .medium = "C4", .harder = "C3" };
        });
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
// registry/track.json, written by the importer
{
  "track": {
    "sochi": { "code": "sochi", "name": "Sochi Autodrom", "base": "game/baku", "slot": "game/sochi",
               "provides": ["venue/sochi"], "files": "track/" }
  },
  "track.data": {
    "mod.sochi/sochi/f1_2025":        { "tyres": { "softer": "C5", "medium": "C4", "harder": "C3" } },
    "mod.sochi/sochi/sprint_f1_2025": { "tyres": { "softer": "C5", "medium": "C4", "harder": "C3" } },
    "mod.sochi/sochi/f1_2026":        { "tyres": { "softer": "C5", "medium": "C4", "harder": "C3" } },
    "mod.sochi/sochi/sprint_f1_2026": { "tyres": { "softer": "C5", "medium": "C4", "harder": "C3" } }
  }
}
```

</template>
</Example>
</template>
</Split>

```text
[import]   mod.sochi: registry/track.json written from track/manifest.json (base game/baku, slot game/sochi,
           venue/sochi from the track name "Sochi Autodrom", 4 data overrides)
[registry] track mod.sochi/sochi: fills game/sochi, base game/baku, provides venue/sochi
[registry] applied at boot: track 1 (19 data sets cloned from game/baku; 77 asset groups added to the manifest;
           1,688 database records, 474 changed)
```

Two addons claiming one stock slot is a provider clash. The second is refused at boot, with both named:

```text
[registry] track mod.igora_d/igora: slot game/miami24 is already filled by mod.miami24/igora_drive; refused
```

Two tracks that provide the same venue both stay in the base. The venue, and every calendar round that names it, goes to the one later in load order.

## Worlds, slots and AI lines

A bigger grid needs more grid slots, garages and pit boxes than a world has. Custom Grid Size provides them per world. Grid size is one boot setting per install, and the core patches the slots for that size into the world archives at boot:

```cpp
tracks::slots.add("monza_myteam_30", {
    .layer = tracks::layers["game/monza/myteam"],
    .grid = grid_cs::slots("monza", 30), .garages = grid_cs::garages("monza", 30), .pit_boxes = grid_cs::boxes("monza", 30),
});
```

A layer row gives a track a world for one mode:

```cpp
// the classic world for Hockenheim
tracks::layers.add("hockenheim_swofty_f1", {
    .track = tracks::tracks["mod.hockenheim/hockenheim"], .mode = "swofty_f1",
    .world = { "environment_package/tracks/hockenheim", "hockenheim_f1_26" }, .grid = 22, .garages = 11,
});
```

## Track data at run time

`track.data.get` is a hot hook point: the game calls it from 110 places. Its handlers run once per track when the core builds the point's table (a box picked, a career loaded, a session loaded), and the game's calls read the table.

```cpp
// track.data.get: Progressive copies 2025 fuel into the 2026 track objects for a season
events::on<tracks::events::DataGet>().in(events::owner("swofty.progressive"))
    .after([](tracks::events::DataGet &g, const events::Context &ctx) {
        if (g.data_set == "f1_2026" && ctx.career()->year <= 2025)
            g.data.fuel = progressive::fuel_2025(g.track);
    });

tracks::set_data(tracks::tracks["game/monaco"], [](tracks::TrackData &d) { d.laps = 78; });   // put back on unload
```

```text
[ops] tracks::set_data game/monaco: laps 50 -> 78 (career 3f2a); put back on career.unloaded
```

## Readiness and limits

- **Records: ready.** About a dozen community tracks ship as addons.
- **A box's own track data: ready for numbers.** `Game_TrackData_GetTrack` is L2, and `CareerEntity+0xf58` (the TrackDataSystem) is L1 with an independent agree. Progressive 1.2.2 proves the object layout for fuel and laps (0x290 bytes, fuel at +0xf0/+0x100/+0x1a0/+0x1b4). Names, date and tyre class are string or pointer fields in the same objects, and their layout needs reverse engineering.
- **The box's track list: partial.** `content_sets` `available_tracks` and `sorted_track_order` are known in data. The runtime list builder behind `ui.track_select.build` isn't in the KB yet. A guard covers tracks that don't fit.
- **Stock slots without assets:** `paul_ricard`, `portimao`, `sochi`, `hanoi`, `texas_nat`, `suzuka_east`, `bahrain_pad`, `silverstone_int`. `bahrain24` is empty.
- **Stack-built keys:** a new track must carry `CareerHubFirstVisit_N`, `CareerHubLastVisit_N`, `TrackHighlights_N` and `TrackVideo_N` (`tools/track_keys.py`).
- **Track data is found by mode mask,** and sub-tables inherit from the parent set one table at a time.
- **AI packs** are per car folder (`ai_package_folder/asset_filename`). A car package with its own AI data, such as F1 1990's `f1_1990`, has an `ai_track_difficulties.<package folder>` key only on the tracks it was made for. A missing key reads the miss sentinel, not null.
- **Worlds:** `_f1_26` worlds have 22 grid slots and 11 garages, `_myteam` 24 and 12, and `_f1` 10 garages. An 11-team box crashed on `_f1` worlds in `Sess_Assign_GarageSlotsByPreference`. Classic Seasons fixed it with a layer copy of `f1_26` for `swofty_f1`.
- **Grid slots per track** differ. Custom Grid Size's table gives Imola, Singapore and Abu Dhabi 26, and Monaco 28.
- **Unknown:** the invitational slot limit (8 tracks without the loader), Time Trial on added tracks (crashes or freezes), and the 28-car session-enter crash on community maps.
- **Size:** a track adds about 1,650 to 1,884 records and changes about 470 to 490. Autopolis alone sets 400.
