---
title: Strings
description: Change the text of lng_* keys while the game runs, for every language or one, with swofty/lang.hpp.
---

# Strings

`swofty/lang.hpp` changes the text a `lng_*` key shows while the game runs, for every language or for one.

```cpp [native/src/my_plugin.cpp]
#include <cstring>

#include <swofty/career.hpp>
#include <swofty/events.hpp>
#include <swofty/lang.hpp>

namespace lg = swofty::lang;

constexpr const char *kSeason = "lng_f1_2026_championship_season";

// Events fire on the flow thread, where refresh() belongs
void on_career(const SwoftyEvent *ev, void *)
{
    if (!lg::available())
        return;
    auto *s = static_cast<const SwoftyCareerState *>(ev->data);
    bool driver = !std::strcmp(ev->name, SWOFTY_EVENT_CAREER_LOADED) &&
                  swofty::career::mode(*s) == swofty::career::Mode::driver_career;
    if (driver) {
        lg::set(kSeason, "2017 FIA Formula One World Championship™");         // every language ("*")
        lg::set(kSeason, "Championnat du Monde de Formule 1 FIA 2017", "fre");      // French only; wins over "*"
    } else {
        lg::clear(kSeason); // "*" drops every language's override of the key
    }
    lg::refresh(); // labels bound to the key read it again
    swofty::log("[my-addon] season title %s", driver ? "set" : "cleared");
}

SWOFTY_ON_LOAD
{
    swofty::events::subscribe(SWOFTY_EVENT_CAREER_LOADED, on_career);
    swofty::events::subscribe(SWOFTY_EVENT_CAREER_UNLOADED, on_career);
}

SWOFTY_ADDON()
```

```json [swofty.json]
{
  "schema_version": 1,
  "id": "author.my-addon",
  "name": "My Addon",
  "version": "1.0.0",
  "author": "Your name",
  "kind": ["native"],
  "compat": { "game": "f1_25" },
  "requires": [{ "id": "swofty.core", "version": ">=0.1.0" }],
  "operations": { "native": [{ "dll": "native/my_plugin.dll" }] }
}
```

```text [swofty/loader.log]
[lang] override: ready, hooked on first use (find found, language table found, module hook on)
[lang] locale module 00000000005A7080 initialised (0); current language eng
[lang] override: Loc_LangSet_Find hooked at base+0x3acf800
[lang] override: refresh sent language 0 to 1 listener(s)
[my-addon] season title set
```

String overrides differ from `lang` operations and `lang_set` in the [asset API](/reference/addons/asset-api): those are built into the game's language data while it loads and can't change afterwards.

- **Header:** `swofty/lang.hpp`, namespace `swofty::lang`
- **Table:** `SwoftyApi::lang` (`SwoftyLang`), API version 8
- **Feature:** `lang`, stable
- **Thread:** `set`, `clear` and `clear_all` from any thread; `refresh` on the flow thread

Every call returns a `swofty::lang::Result`: `ok`, `unavailable` or `invalid` (see [result codes](./#result-codes)), and `name(r)` turns it into a string. A core older than API v8 returns `unavailable` from every call. To build one plugin against a core with and without the API, guard the calls with `#if SWOFTY_API_VERSION >= 8` and check `available()` at runtime.

## available

```cpp
if (swofty::lang::available()) {
    // the core implements overrides and found the game's string lookup (Loc_LangSet_Find)
}
```

When the core doesn't find the lookup, every call returns `unavailable`:

```text
[lang] override: off: Loc_LangSet_Find not found (find missing, language table found, module hook on)
```

## set

```cpp
lg::set("lng_f1_2026_championship_season", "2017 FIA Formula One World Championship™"); // every language
lg::set("lng_f1_2026_championship_season", "Campionato 2017", "ita");                         // only while Italian is the game's language
lg::set("lng_my_addon_label", "Works too");   // a key the game's tables lack
```

- `language` `"*"` (the default) applies to every language. A language code (`eng`, `fre`, ...) applies only while it is the game's language and wins over `"*"`. Codes are not case-sensitive.
- `text` is UTF-8 and is copied.
- An override also works for a key the game's tables lack.
- An empty key returns `invalid`.

Progressive Career sets a season's race weekend titles, one override per key and language, from its `data/titles.json`:

```cpp
// addons/progressive/native/src/season.cpp, trimmed
for (auto &t : progressive::titles::for_year(g_titles, year))
    if (swofty::lang::set(t.key, t.text, t.language) == swofty::lang::Result::ok)
        ++set;
swofty::log("[progressive] event titles %d (%s): %zu override(s)", year, why, set);
```

```text
[progressive] event titles 2017 (career): 334 override(s)
```

## clear and clear_all

```cpp
lg::clear("lng_f1_2026_championship_season", "ita"); // drops the Italian override only
lg::clear("lng_f1_2026_championship_season");        // "*": drops every override of the key
lg::clear_all();                                     // drops every override, of every addon
```

## refresh

```cpp
lg::refresh(); // once, after a batch of set/clear calls, on the flow thread
```

`refresh()` sends the current language to the game's language listeners, the notification the game sends after it loads a language. It returns `unavailable` until the game's locale module has started.

```text
[lang] override: refresh sent language 0 to 1 listener(s)
```

Call it at most once after a batch of changes, on the flow thread (for example from a core [event](./events) handler). In the game's unprotected code only the locale module's start-up sends that notification, so which labels re-read their text on `refresh()` is not yet known.

## How it applies

Every `lng_*` lookup goes through the language set's find (`Loc_LangSet_Find`). The core hooks it when an addon sets its first override; until then the game's lookup runs untouched. While overrides exist, a key with an override returns the override text and every other key reads the game's tables.

```text
[lang] override: ready, hooked on first use (find found, language table found, module hook on)
[lang] override: Loc_LangSet_Find hooked at base+0x3acf800      the first set()
```

- A change applies the next time the game looks the key up. Screens and labels opened afterwards show it; text already on screen keeps its old value until the game looks it up again.
- The core stores each distinct text once and keeps it until the game exits, so a string the game got from an override stays valid after the override is cleared.
