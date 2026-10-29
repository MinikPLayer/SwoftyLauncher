---
title: How changes reach seasons and careers
description: How each game system's data passes from the stock game to what every year shares, to each Grand Prix and Time Trial box, to a career and a session; which addon wins when two change the same thing; and what happens when an addon is removed.
---

# How changes reach seasons and careers

Your addon can change what every year shares, change one year, or change things inside a single career or session. Where you make the change decides who sees it and whose value wins.

Install a Red Bull livery. Every year that uses the stock Red Bull now shows it. Classic Seasons' 2017 Red Bull has a livery of its own, so the 2017 box keeps that one. A running career reads its cars and liveries from its boxes, so it picks up the new livery while keeping the ratings and rules it saved when it began. A fuel change made for one session lasts until that session ends.

Every system reads its data through the same five levels. Each level starts with the values of the one above it and can set values of its own:

1. **The stock game** supplies the original records under `game/`: car models for each model year, teams, drivers, tracks, tyres and rules. A record is one item of data, such as a car or a driver.
2. **Every year** shares one set of data, called the **base** in code. It holds the stock records, everything addons and imports add, and the changes they make to stock records.
3. **A box** is one of the year tiles a player picks in Grand Prix or Time Trial, or one season of a career. In code it's a **content package**. It inherits the base and can set its own values.
4. **A career** uses the box chosen for each of its seasons, plus changes scoped to that career. It picks each season's box when it's created, and freezes each season's line-ups, ratings and rules when that season starts.
5. **A session** can make temporary changes. They're undone when the session ends.

A **layer** changes values on a record that already exists. A **variant** is a separate record that inherits another record's values and overrides some of them. A year pack, for example, makes its 2017 car as a variant of a stock car, then gives it a different livery and engine sound.

## Three rules

1. **Adding to the base adds to every year.** An added track joins every year's track list. A livery, helmet, engine sound or team logo layered on a base record reaches every box and career that uses that record, unless the box sets its own.
2. **A box's own value wins over the base.** If a year pack sets its car's livery, addons that layer the base car's livery can't replace it. Only the fields the box sets are protected. Every other field still comes from the base.
3. **Changes to the same record compete by load order.** An addon that layers a box's own record competes directly with that box, and the later one in load order wins. Retargeting an import to that record works the same way.

```cpp
// Rule 1: Hockenheim joins the base, so every box offers it.
tracks::tracks.add("hockenheim", { .base = tracks::tracks["game/austria"],
                                   .provides = { tracks::venues["venue/hockenheimring"] } });

// Rule 2: Classic Seasons 2017's Red Bull sets its own livery and sound.
auto rb17 = cars::vehicles.variant("redbull", cars::vehicles["game/redbull_2025"], [](cars::Vehicle &v) {
    v.livery = liveries::liveries["swofty.classic-seasons-2017/redbull"];
    v.engine_sound = sounds::engine_sounds["swofty.classic-seasons-2017/renault"];
});

// Rule 3: layering the 2017 car itself competes with the box, by load order.
cars::vehicles.layer(cars::vehicles["swofty.classic-seasons-2017/redbull"],
                     [](cars::Vehicle &v) { v.livery = liveries::liveries["someone/rb13_monaco"]; });
```

## How far a change reaches

Layer a base record when you want the change everywhere that record is inherited. Changing the stock Red Bull's livery reaches every year that uses it, and a year with its own livery keeps it.

Use a **selector** when you want to change every record that matches a condition. A selector layers each matching record itself, so it reaches a year pack's own cars as well as stock cars, and its change competes on each record by load order.

A year pack can mark fields as **protected**: history values such as its cars' pace and AI tiers. A selector skips protected fields, unless the player turns on **Also change year packs** for that addon. A layer that names the year's record directly still competes by load order.

```cpp
// Change the base car. Years that set their own livery keep it.
cars::vehicles.layer(cars::vehicles["game/redbull_2025"], [](cars::Vehicle &v) { v.livery = mine; });

// Change every F1 car's AI, including the cars year packs own.
// Fields a year pack protects are skipped unless the player allows it for this addon.
auto f1 = where<ai::VehicleConfig>([](const ai::VehicleConfig &c) {
    return c.cls == ai::Class::f1 || c.cls == ai::Class::f1_26;
});
ai::vehicle_configs.layer(f1, [](ai::VehicleConfig &c) { c.racecraft.overtake_aggression = 0.85f; });
```

- Cosmetic and content changes, such as liveries, helmets, sounds, logos and tracks, layer the base. Years that set their own keep them.
- Behaviour and tuning changes, such as AI, damage, physics fixes and difficulty, use a selector. They reach every year.
- The importer layers the base by default. Car ports and season packs ask once whether they go everywhere or only into Grand Prix and Time Trial, and default to Grand Prix and Time Trial. **Applies to** adds a box's own record as another target and keeps the base one.

## Each system

Every system makes the same choice in code: change what every year shares, set a year's own value, or scope the change to a box, a career or a session. Each example below shows where that change goes. The system pages have the full records.

### Cars

A car's model, livery, sound, power unit and handling are fields on its `cars::Vehicle` record. Handling applies as each car is built, at every session load, and `cars::live` changes a car already on track (see [Car physics and handling](/std/systems/car-physics)).

```cpp
// Every year that uses the stock McLaren: layer the base record.
cars::vehicles.layer(cars::vehicles["game/mclaren_2025"], [](cars::Vehicle &v) {
    v.model = file("models/mcl39/");
});

// One year: your own car, a variant of a stock donor. What you set wins in your box.
auto mclaren08 = cars::vehicles.variant("mclaren", cars::vehicles["game/mclaren_2025"], [](cars::Vehicle &v) {
    v.performance_tier = 1;
});
cars::vehicles.protect(mclaren08, { "performance_tier" });   // other addons' selectors leave it alone

// Every F1 car's handling, years included: a selector over the core tag.
cars::handling.layer(tagged<cars::Handling>(cars::handling.tag("c/f1_cars")), [](cars::Handling &h) {
    h.damage_scale *= 1.5f;
});
```

### Liveries

A livery names the car it fits. Put it on the car record to replace the livery, or mark it special to add a choice to the livery picker.

```cpp
auto repaint = liveries::liveries.add("repaint", {
    .vehicle = cars::vehicles["game/redbull_2025"],
    .textures = file("liveries/redbull/"),
});

// Every year that inherits the stock Red Bull's livery. A year with its own livery keeps it.
cars::vehicles.layer(cars::vehicles["game/redbull_2025"], [=](cars::Vehicle &v) { v.livery = repaint; });

// Or an extra choice in the livery picker, in every box that uses the car.
liveries::liveries.add("repaint_special", {
    .vehicle = cars::vehicles["game/redbull_2025"],
    .archive = file("liveries/redbull_special.erp"),
    .special = true,
});
```

### Teams

```cpp
// Every year: change the stock team's colours.
teams::identities.layer(teams::identities["game/williams"], [](teams::Identity &t) {
    t.colours.primary = Colour("#00A0DE");
});

// One year: the year's own team record, a variant of the stock team.
auto williams90 = teams::identities.variant("williams", teams::identities["game/williams"], [](teams::Identity &t) {
    t.names.title = Text::key("lng_1990_williams");
    t.colours.primary = Colour("#1E3A8A");
});

// One box or one career: scope the name's text, not the team.
ui::strings.add("lng_team_williams", {
    .text = { { "*", "Canon Williams" } },
    .within = events::data_group("f1_career_decades"),
});
```

### Drivers

A driver's number and look in one year go in that year's box (see [Seasons and GP/TT content](/std/content)). A new driver joins the base, so they're a free agent in every box and on every career's market.

```cpp
// Every year: a new head on the stock driver.
drivers::identities.layer(drivers::identities["game/lewis_hamilton"], [](drivers::Identity &d) {
    d.head = drivers::heads["mod.faces/hamilton"];
});

// A new driver, in every year.
drivers::identities.add("jack_crawford", {
    .donor = drivers::identities["game/oliver_bearman"],
    .names = { .first = Text::key("lng_jack"), .last = Text::key("lng_crawford") },
    .nationality = "US",
    .number = 33,
});

// One career season: the driver's ratings that year, frozen when the season starts.
career::ratings.add("2008/game/lewis_hamilton", {
    .season = career::seasons["mod.decades/2008"],
    .driver = drivers::identities["game/lewis_hamilton"],
    .experience = 70, .racecraft = 88, .awareness = 84, .pace = 92,
});
```

### Helmets and suits

```cpp
auto helmet = wearables::helmets.add("hamilton_2008", {
    .archive = file("helmets/hamilton_2008.erp"),
    .driver = drivers::identities["game/lewis_hamilton"],
});

// Every year that doesn't set its own helmet for him.
drivers::looks.layer(drivers::looks["game/lewis_hamilton"], [=](drivers::Look &l) { l.helmet = helmet; });

// One career: the helmet only that career uses.
drivers::looks.layer(drivers::looks["game/lewis_hamilton"], [=](drivers::Look &l) { l.helmet = helmet; }, Scope::career);

// One season's team kit, which wins over the base kit.
wearables::season_sets.add("mclaren_2008", {
    .team = teams::identities["mod.season-2008/mclaren"],
    .season = 2008,
    .suit = wearables::suits["mod.season-2008/mclaren"],
    .gloves = wearables::gloves["mod.season-2008/mclaren"],
});
```

### Engine sounds

```cpp
auto v10 = sounds::engine_sounds.add("v10", {
    .bundle = file("sounds/v10.bdl"),
    .settings_from = sounds::engine_sounds["game/ferrari_2025"],
});

// Every year that inherits the stock Ferrari's sound. A car with its own sound keeps it.
cars::vehicles.layer(cars::vehicles["game/ferrari_2025"], [=](cars::Vehicle &v) { v.engine_sound = v10; });

// A year pack whose cars use a stock sound file pins it. An import that overwrites the file can't reach them.
sounds::engine_sounds.add("ferrari", {
    .bundle = stock("audio/2025/bundles/ferrari_2025.bdl"),
    .settings_from = sounds::engine_sounds["game/ferrari_2025"],
});
```

### Tracks

A new track joins the base. Each box offers it where it fits: enough garages for its teams, room for its grid, AI data for its cars, and Time Trial support where the box has Time Trial. A career's calendar picks tracks by venue, and a raced track becomes one of the career's dependencies.

```cpp
// Every year: a new track.
tracks::tracks.add("hockenheim", {
    .base = tracks::tracks["game/austria"],
    .provides = { tracks::venues["venue/hockenheimring"] },
});

// One box: its own lap count at one track.
tracks::per_package.add("f1_2008/monaco", {
    .package = content::packages["mod.season-2008/f1_2008"],
    .track = tracks::tracks["game/monaco"],
    .laps = 78,
});

// Track data applies when a session loads: the fuel for every session at this track.
tracks::data.layer(tracks::data["mod.hockenheim/hockenheim/f1_2025"], [](tracks::TrackData &d) {
    d.fuel.burn_per_lap = 1.62f;
});
```

### Tyres

A new compound joins the base. A box picks its tyre class, which applies when its session loads. A career freezes each season's tyre rules when the season starts.

```cpp
// Every year: a new compound's physics.
tyres::physical.add("c6", {
    .package = cars::packages["game/f1_2025"],
    .code = tyres::Code::hso,
    .vcmpd = file("tyres/c6.vcmpd"),
    .slick = true,
});

// One box: its tyre class.
content::packages.layer(content::packages["mod.season-2008/f1_2008"], [](content::Package &p) {
    p.rules.tyre_class = tyres::classes["mod.season-2008/bridgestone"];
});
```

### Power units and DRS

A car's power unit is a field on its record. A box's DRS rule is part of its rules and applies when its session loads.

```cpp
// One year's car: its power unit.
cars::vehicles.layer(cars::vehicles["mod.season-2008/ferrari"], [](cars::Vehicle &v) {
    v.power_unit = power::units["game/pu_2025"];
});

// One box: no DRS.
auto no_drs = power::drs_rules.add("none", { .race_start = 200, .red_flag_restart = 200, .safety_car_end = 200 });
content::packages.layer(content::packages["mod.season-2008/f1_2008"], [=](content::Package &p) {
    p.rules.drs = no_drs;
});
```

### Session rules

A box's points, refuelling and format are its rules. A career freezes each season's championship rules when the season starts.

```cpp
auto points = sessions::points.add("2008", { .points = { 10, 8, 6, 5, 4, 3, 2, 1 } });

content::packages.layer(content::packages["mod.season-2008/f1_2008"], [=](content::Package &p) {
    p.rules.points = points;
    p.rules.refuelling = true;
});
```

### Weather

```cpp
// Every year: a track's climate.
weather::climates.layer(weather::climates["game/silverstone"], [](weather::Climate &c) {
    c.temperature_max = 24;
});

// One session: a scripted forecast.
weather::forecasts.add("spa_race", {
    .within = events::track("game/spa"),
    .session = sessions::Session::race,
    .slots = { { weather::Kind::clear, 20min, 0min }, { weather::Kind::heavy_rain, 60min, 3min } },
});
```

### AI

A selector reaches every matching car, years included, except fields a year pack protects. `.in(...)` limits where a layer applies.

```cpp
auto f1_cars = tagged(&ai::VehicleConfig::vehicle, cars::vehicles.tag("c/f1_cars"));

// Every F1 car, in every box and career.
ai::vehicle_configs.layer(f1_cars, [](ai::VehicleConfig &c) { c.racecraft.overtake_aggression = 0.85f; });

// Only in Grand Prix and Time Trial.
ai::vehicle_configs.layer(f1_cars, [](ai::VehicleConfig &c) { c.racecraft.defend_aggression = 0.8f; })
    .in(events::setup_mode("swofty_f1"));

// A year pack keeps its own tuning from other addons' selectors.
ai::vehicle_configs.protect(ai::vehicle_configs["mod.season-2008/ferrari"], { "racecraft" });
```

### Career data

```cpp
// A career mode of your own: a variant of a stock mode, so it inherits changes to it.
career::modes.variant("decades", career::modes["game/f1_career_2026"], [](career::ModeRecord &m) {
    m.data_group = "f1_career_decades";
});

// One career: a change saved with that career only.
career::seasons.layer(career::seasons["mod.decades/2009"], [](career::Season &s) {
    s.teams.at(1).seats[0] = drivers::identities["game/fernando_alonso"];
}, Scope::career);
```

### Menu text and screens

A string with no `within` changes the text everywhere. A string with `within` changes it only there. Menu tiles and boxes add to the menus and never replace another addon's.

```cpp
// Everywhere.
ui::strings.add("lng_team_haas", { .text = { { "*", "Haas F1 Team" } } });

// Only in one box.
ui::strings.add("lng_team_alpine", {
    .text = { { "*", "Renault" } },
    .within = events::content_exact("mod.season-2008/f1_2008"),
});
```

### Graphics

Graphics exist only in the base. A look applies in every box, and looks stack in load order.

```cpp
graphics::looks.add("warm", { .lut = file("looks/warm.cube"), .strength = 0.8f });
```

## When two addons change the same thing

What decides it is the record each change targets:

- **Two imports change the same base record.** The later one in load order wins each field they both set. Raw parts are decided per resource. Two full liveries on one car are the exception: the later one replaces the earlier one whole, and partial liveries stack on top.
- **An import changes a base record and a year pack sets its own value.** The import changes the base. The year pack keeps its own value in its box.
- **An import also targets a year's own record with Applies to.** It competes with the year pack on that record, by load order, and still changes the base.
- **Two year packs provide the same year.** Each makes its own box under its own key. Both appear, and the player picks one.
- **A year pack layers another year pack's box.** They compete on that record, by load order.
- **A selector changes a field a box also sets.** The selector wins, because it layers the box's record directly, unless the box protects that field. A protected field is skipped unless the player turns on Also change year packs for that addon.
- **A career layer and any other change.** The career layer wins inside that career.

A Red Bull livery imported onto the base can't replace the 2017 box's own livery. Adding the 2017 car as a target makes the two compete by load order there.

The core logs every one of these with both addons and the field. It writes them to `registry/conflicts.json` when a session loads, and the launcher's conflicts view shows the last one.

## When an addon is removed

Removing an addon changes what loads. What happens depends on what it supplied and whether a career used it:

- **Cosmetics fall back to the next value up the chain.** Liveries, helmets, sounds, logos and text: removing a livery reveals the one it covered. Careers carry on.
- **Content a career used stays required.** A track it raced, a car it drove or a team in its line-up is in the career's dependency record. The launcher warns before Play, and a guard stops the load with a popup if it's missing.
- **Content a career skipped isn't required.** An optional track the career never raced is recorded as skipped.
- **Removing a year pack removes its boxes.** Careers created in those boxes need the pack, with the same warning and guard.
- **Removing a selector change undoes it at the next load.** Take out an AI or damage tweak and careers carry on with what's left.

## What a career keeps

When a career is created, it records which box each season uses. When each season starts, it copies that season's:

- line-up and ratings: `career.season` and `career.driver_ratings`;
- tyre and championship rules.

A fix to a year's data reaches every career that hasn't reached that year yet. Drivers and tracks installed mid-career join at the next season. A career's owner can freeze every season at creation instead, and the career's hub can offer a **Freeze content** toggle.

It doesn't copy content. Cars, liveries, tracks and sounds are read live from the boxes, so a better livery or a fixed track reaches a running career, and the career keeps its frozen ratings and rules. The content a career depends on is protected by its dependency record: see [Dependencies and guards](/std/dependencies).

The only save data the registry writes is each career's `swofty_registry` container: its frozen data and its dependency record. Turning addons on or off never changes a save. It changes what loads, and guards stop any load that would lose progress.
