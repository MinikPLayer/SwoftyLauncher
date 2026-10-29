---
title: UI and strings
description: Screens, tiles, texture slices, routes, options, HUD widgets and strings in code, with era boxes, the UI events and the view-model operations.
---

# UI and strings

With the UI system you change what the player sees in the menus and on the HUD: tiles and boxes, the images in the game's texture arrays, your own screens and routes, options, HUD widgets and text. Classic Seasons 2017, for example, adds an F1 2017 box to the Grand Prix picker and a 2017 Toro Rosso logo to the driver select screen.

A **texture array** is one of the game's image sheets, made of named **slices**. A **route** is the game's link from a press to the screen it opens. A **view model** is the data a screen binds to, by path.

::: info In 1.6.0
The header `swofty/systems/ui.hpp` has `ui::strings` (`lang.string`) and `ui::addon_options` (`addon.option`). Screens, tiles, texture slices, routes, options and HUD widgets ship later; those sections describe that design.
:::

## The header

<Split>

`swofty/systems/ui.hpp` has seven registries:

- `ui::texture_slices`: one named slice of a texture array (`ui.texture_slice`);
- `ui::tiles`: a home, career, F1 World or car-category tile (`ui.tile`);
- `ui::screens`: an addon screen (`ui.screen`);
- `ui::routes`: a front-end route (`ui.route`);
- `ui::options`: a game option and its restriction tags (`ui.option`);
- `ui::hud_widgets`: an addon HUD element (`hud.widget`);
- `ui::strings`: one text key in every language (`lang.string`).

Grand Prix and Time Trial era boxes are `content::era_boxes`, in `swofty/systems/content.hpp` (see [Seasons and GP/TT content](/std/content)).

<template #example>
<Example root="swofty-sdk" code-file="include/swofty/systems/ui.hpp">
<template #code>

```cpp
#pragma once
#include <swofty/registry.hpp>
#include <swofty/events.hpp>

namespace swofty::ui {

struct Route;

struct TextureSlice {
    std::string array;                         // the texture array: "overrides.teams.driver_select_team_logo"
    std::string name;                          // the slice name the UI asks for: "cs_f1_2017"
    Path image;                                // png or svg; the core makes the BC7 slice and its mips
};
inline Registry<TextureSlice> texture_slices{"ui.texture_slice", { .applies = Applies::boot }};   // (every archive that carries the array)

enum class Place { home, career, f1_world, car_category };

struct Tile {
    Place place;
    Text title;
    Ref<TextureSlice> image;
    Ref<Route> route;                          // what a press opens
    events::Selector gate;                     // shown only where it matches; empty: always
};
inline Registry<Tile> tiles{"ui.tile", { .applies = Applies::read }};

struct Screen {
    Path file;                                 // the .ui file
    std::string flow;                          // the flow archive it joins: "flow_frontend"
    std::string anchor;                        // where it opens from
    std::string view_model;                    // the view-model path it binds
};
inline Registry<Screen> screens{"ui.screen", { .applies = Applies::boot }};

struct Route {
    std::string file;                          // routes_frontend_general.uir, ...
    std::string clone_of;                      // the route group it copies
    Ref<Screen> opens;
};
inline Registry<Route> routes{"ui.route", { .applies = Applies::boot }};

struct Option {
    std::string id;                            // options_system option
    std::vector<std::string> restriction_tags; // where it shows: "gamesetup_f1", ...
};
inline Registry<Option> options{"ui.option", { .applies = Applies::boot }};

struct HudWidget {
    std::string view_model;                    // the VM path it binds
    Path tmpl;                                 // its template
};
inline Registry<HudWidget> hud_widgets{"hud.widget", { .applies = Applies::session }};

struct String {
    std::map<std::string, std::string> text;   // per language; "*" for every language
    events::Selector within;                   // a career, a content, a mode; empty: everywhere
};
inline Registry<String> strings{"lang.string", { .applies = Applies::read }};   // the id is the lng key; read from the core's string table

namespace events {

struct EraTiles {                              // site_FeTile_EraTilesBuilt
    static constexpr const char *point = "ui.era.tiles";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    void *list;                                // the era tile list, built; after handlers may append
};

struct VmResolve {                             // Vm_Resolve_RegistryNode
    static constexpr const char *point = "ui.vm.resolve";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    std::string_view path;                     // the view-model path the screen asks for
    void *node;                                // before handlers may supply their own node
};

struct LangLookup {                            // Loc_LangSet_Find
    static constexpr const char *point = "lang.lookup";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    std::string_view key, language;
    std::string_view text;                     // before handlers may supply the text
};

// also: HomeTiles (ui.home.tiles), CareerContentTiles (ui.career_content.tiles), CarCategoryTiles
// (ui.car_category.tiles), TrackSelectBuild (ui.track_select.build), ScreenBuilt (ui.screen.built),
// RouteOpen (ui.route.open), MfdPages (hud.mfd.pages)

} // namespace events

Result set_text(std::string_view vm_path, std::string_view text);   // the Vm_Set_* family
Result set_bool(std::string_view vm_path, bool value);
Result set_ref(std::string_view vm_path, std::string_view target);

} // namespace swofty::ui
```

</template>
</Example>
</template>
</Split>

## An era box and a team logo

<Split>

Classic Seasons 2017 shows F1 2017 on the Grand Prix and Time Trial picker, with its own box art. It adds Toro Rosso's 2017 logo for the driver select screen, under the name its team record asks for.

Each image is one slice in a named texture array. Classic Seasons, fanfreluche's cars and ByDaniX's F1 2012 each ship a whole driver-select logo array, and whole arrays overwrite each other. As slices they all apply: each addon adds or changes its own names.

<template #example>
<Example root="swofty.classic-seasons-2017" code-file="native/src/ui.cpp" data-file="registry/ui.json" files="swofty.json, native/CMakeLists.txt, art/era/f1_2017.png, art/logos/toro_rosso.png, lang/strings.json">
<template #code>

```cpp
#include <swofty/registry.hpp>
#include <swofty/events.hpp>
#include <swofty/systems/ui.hpp>
#include <swofty/systems/content.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    ui::texture_slices.add("era_f1_2017", {
        .array = "overrides.home_screen.tiles_solo_6x6",
        .name = "cs_f1_2017",
        .image = file("art/era/f1_2017.png"),
    });

    content::era_boxes.add("f1_2017", {
        .package = content::packages["swofty.classic-seasons-2017/f1_2017"],
        .title = "lng_cs_era_2017",
        .image = "#overrides.home_screen.tiles_solo_6x6[cs_f1_2017]",   // the slice above
        .order = 2017,
    });

    ui::texture_slices.add("logo_toro_rosso", {
        .array = "overrides.teams.driver_select_team_logo",
        .name = "cs17_toro_rosso",                         // the name the 2017 Toro Rosso team record shows
        .image = file("art/logos/toro_rosso.png"),
    });

    ui::strings.add("lng_cs_era_2017", { .text = { { "*", "F1 2017" } } });
}

SWOFTY_ON_LOAD
{
    events::on<ui::events::EraTiles>().after([](const ui::events::EraTiles &, const events::Context &) {
        swofty::log("[cs-2017] era picker built");
    });
}

SWOFTY_ADDON()
```

</template>
<template #data>

```json
{
  "ui.texture_slice": {
    "era_f1_2017":     { "array": "overrides.home_screen.tiles_solo_6x6", "name": "cs_f1_2017", "image": "art/era/f1_2017.png" },
    "logo_toro_rosso": { "array": "overrides.teams.driver_select_team_logo", "name": "cs17_toro_rosso",
                         "image": "art/logos/toro_rosso.png" }
  },
  "era.box": {
    "f1_2017": { "package": "swofty.classic-seasons-2017/f1_2017", "title": "lng_cs_era_2017",
                 "image": "#overrides.home_screen.tiles_solo_6x6[cs_f1_2017]", "order": 2017 }
  },
  "lang.string": {
    "lng_cs_era_2017": { "text": { "*": "F1 2017" } }
  }
}
```

</template>
</Example>
</template>
</Split>

```text
[registry] register: swofty.classic-seasons-2017 (cs2017_ui.dll) 2 ui.texture_slice, 1 era.box, 1 lang.string
           (early stage, 0.3 ms)
[registry] applied at boot: ui.texture_slice 2 (2 arrays grown in 6 archives, as overlay archives); era.box 1 and lang.string 1 on read
[registry] ui.texture_slice overrides.teams.driver_select_team_logo: 34 slices from 3 addons (swofty.classic-seasons-2017,
           fanfreluche.cars, bydanix.f1-2012), 0 contested
[events] ui.era.tiles: 1 after handler (swofty.classic-seasons-2017)
[cs-2017] era picker built
```

The core builds the slices into overlay archives that it adds to the game's asset manifest at boot. The data version is the same records. Only the after handler needs code.

## Tiles, screens and routes

```cpp
auto settings = ui::screens.add("settings", {
    .file = file("ui/settings.ui"), .flow = "flow_frontend", .anchor = "addon_settings", .view_model = "mod.settings",
});
auto open = ui::routes.add("open_settings", { .file = "routes_frontend_general.uir", .clone_of = "options", .opens = settings });

ui::tiles.add("settings_tile", {
    .place = ui::Place::home,
    .title = "lng_mod_settings",
    .image = ui::texture_slices["mod.settings/tile"],
    .route = open,
    .gate = events::mode(events::Mode::driver_career),
});
```

## Options and HUD widgets

```cpp
ui::options.layer(ui::options["game/parc_ferme_rules"], [](ui::Option &o) {
    o.restriction_tags.push_back("gamesetup_f1_1990");
});

ui::hud_widgets.add("tyre_letters", { .view_model = "hud.f1990.tyre_letter", .tmpl = file("ui/tyre_letter.ui") });
```

## Strings

A string can apply in one career or one box only. Stock keys change there and nowhere else:

```cpp
// F1 1990 careers call the first one-shot session "Qualifying 1"
ui::strings.add("lng_sprintshootout_title", {
    .text = { { "*", "Qualifying 1" } },
    .within = events::content("swofty.f1-1990/f1_career_1990"),
});

// a 2012 box shows the team's 2012 name, and every other box keeps the stock one
ui::strings.add("lng_team_alpine", {
    .text = { { "*", "Lotus" } },
    .within = events::content_exact("bydanix.f1-2012/f1_2012"),
});
```

The game looks text up every frame. The core builds a table of the effective strings when content becomes active and when a session loads, and `lang.lookup` reads that table. The same goes for the other hook points the game calls every frame, such as team logos and view-model nodes. A handler on one of them runs on every call, so it should only read a table of its own.

## Events and operations

```cpp
// Progressive's view-model hook: its own team names on the hub
events::on<ui::events::VmResolve>().in(events::content("swofty.progressive/f1_career_progressive"))
    .before([](ui::events::VmResolve &r, const events::Context &) {
        if (r.path.starts_with("hud_progressive_"))
            r.node = progressive::vm_node(r.path);
    });

ui::set_text("career.hub.title", "Season 1990");
swofty::popup::show("Tyres", "Goodyear brings B, C, D and E this weekend.");
swofty::hud::toast("Driver change", "Gasly replaces Kvyat");
```

## Readiness and limits

- **Ready:** tiles, era boxes, view models, routes, popups and strings. The front-end and view-model lanes are among the most L2 and L3 code outside the career.
- **Partial:** the HUD (one HUD view model is bound; the other ~30 are class names only), `.idf` layout write-back (shapes are baked meshes).
- **Fixed counts in native screens:** era tiles, four R&D departments, eight MFD pages, Team HQ tabs. Data-bound lists (R&D upgrade tiles, settings rows) take new rows with no UI patch.
- **A rejected node ends the whole screen build:** the screen comes up blank.
- **Growing a texture array** changes the archive layout. The core grows every archive that carries the array.
- **Renames** leave stale hashes behind. Third-party whole-file `screens/*.erp` replacements still overwrite each other.
- **Strings:** the in-game loader is required (no strings without it). Speech can't be edited. Text with counts in it ("tenth and final season") is hard-coded.
