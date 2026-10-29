---
title: Database read
description: Read the loaded F1 25 game database from a plugin with swofty/db.hpp - scalar fields, whole values and table cursors.
---

# Database read

`swofty/db.hpp` reads the game database the game has loaded, by the same dotted keys the [database operations](/reference/ops/database) write.

```cpp [native/src/my_plugin.cpp]
#include <string>
#include <unordered_map>

#include <swofty/db.hpp>
#include <swofty/events.hpp>

namespace db = swofty::db;

std::unordered_map<int64_t, std::string> g_drivers; // stat_id -> the row's label
bool g_loaded = false;

void load_drivers()
{
    db::Cursor cur{}; // you own the cursor: zero it before open()
    for (db::Result r = db::open("entrants.drivers", &cur); r == db::Result::ok; r = db::step(&cur)) {
        db::Value id{};
        if (db::cell(&cur, "stat_id", &id) != db::Result::ok)
            continue;
        if (const char *label = db::row_name(&cur)) // the row's _metadata_, or nullptr
            g_drivers[id.i] = label;                // copy every string you keep
    }
    g_loaded = true;
    swofty::log("[my-addon] %zu driver(s) in entrants.drivers", g_drivers.size());
}

void on_frame(double, void *)
{
    if (g_loaded || !db::available()) // false until the game has loaded its database
        return;
    int64_t secs = 0;
    if (db::int_at("rules.stop_go_penalty.penalty_duration", &secs) == db::Result::ok)
        swofty::log("[my-addon] stop-go penalty is %lld s", (long long)secs);
    load_drivers();
}

SWOFTY_ON_LOAD
{
    swofty::events::on_frame(on_frame); // the database can load after the plugin: poll until it is there
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
  "requires": [{ "id": "swofty.core", "version": ">=0.2.11" }],
  "operations": { "native": [{ "dll": "native/my_plugin.dll" }] }
}
```

```text [swofty/loader.log]
[dbread] window stage: database ready (available), no "db_inspect" entries in swofty/assets.json
[my-addon] stop-go penalty is 5 s
[my-addon] 197 driver(s) in entrants.drivers
```

- **Header:** `swofty/db.hpp`, namespace `swofty::db`
- **Table:** `SwoftyApi::db` (`SwoftyDb`), API version 5
- **Feature:** `db`, stable (shared with the database write operations)
- **Thread:** the caller's thread; any thread works

Every call returns a `swofty::db::Result`: `ok`, `late`, `unavailable` or `invalid` (see [result codes](./#result-codes)), and `name(r)` turns it into a string. Before the database has loaded, every call returns `unavailable`. On a loader older than version 5, or with a null table, the wrapper returns `unavailable`.

## Keys

A key is the dotted path of one field, the same path a [database operation](/reference/ops/database) writes, hashed with fnv1a64. Look the paths up in the [game database reference](/reference/gamedb).

```cpp
"rules.stop_go_penalty.penalty_duration"   // one setting
"entrants.drivers.driver_12.stat_id"        // a field of row 12 of entrants.drivers
"entrants.drivers._driver_instancecount_"   // a row count: <container>._<Row>_instancecount_
"entrants.drivers.driver_12._metadata_"     // a row's label
```

An empty key, or a key that names no record, returns `invalid`.

## available

```cpp
if (!swofty::db::available())
    return; // the database has not loaded yet
```

`available()` returns `true` once the game database has loaded. Guard every call with it.

## Scalar fields

When you know a field's type, read it straight into a C++ value:

```cpp
int64_t secs = 0;
db::int_at("rules.stop_go_penalty.penalty_duration", &secs);   // the record's 32-bit integer, widened

double f = 0;
db::float_at(float_key, &f);                                    // the record's 32-bit float, widened

int32_t on = 0;
db::bool_at(bool_key, &on);                                     // 0 or 1

const char *name = nullptr;
db::string_at("entrants.drivers.driver_12._metadata_", &name);  // decoded UTF-8, see Strings
```

Each one reads the record as that type and does not check the record's own type, so pick the call that matches the field.

## get

`get` returns the whole typed value. Its `tag` says which of `i`, `f` or `s` holds the value:

```cpp
db::Value v{};
if (db::get("rules.stop_go_penalty.penalty_duration", &v) == db::Result::ok) {
    switch (static_cast<db::Tag>(v.tag)) {
    case db::Tag::integer:
    case db::Tag::boolean:
        swofty::log("[my-addon] int %lld", (long long)v.i);
        break;
    case db::Tag::real:
        swofty::log("[my-addon] float %g", v.f);
        break;
    default: // string, enumeration, id
        swofty::log("[my-addon] text %s", v.s ? v.s : "(none)");
    }
}

uint64_t hash = 0xcbf29ce484222325ull;   // fnv1a64 of the dotted path, hashed yourself
for (char c : std::string_view("rules.stop_go_penalty.penalty_duration"))
    hash = (hash ^ uint8_t(c)) * 0x100000001b3ull;
db::get(hash, &v);                        // the same read as get("rules.stop_go_penalty.penalty_duration", &v)
```

`get` returns `unavailable` for a record whose type the core can't tell on this build.

| `swofty::db::Tag` | `SWOFTY_DB_*` | Holds the value |
|---|---|---|
| `integer` | `INT` (0) | `i` |
| `real` | `FLOAT` (1) | `f` |
| `string` | `STRING` (2) | `s` |
| `enumeration` | `ENUM` (3) | `s` |
| `id` | `ID` (4) | `s` |
| `boolean` | `BOOL` (5) | `i` |

## Strings

The game stores database text XOR-encoded (`GdbStringRecord`, key `k_Gdb_StringXorKey`). From core 0.2.11, `string_at`, the `s` of a string `get` or `cell`, and `row_name` return the decoded text: printable UTF-8 of at most 16 KiB, or no string. Earlier cores returned the encoded bytes.

```cpp
const char *s = nullptr;
db::Result r = db::string_at("entrants.drivers.driver_12._metadata_", &s);
// ok:          s is the decoded text
// invalid:     no record, or a record that does not decode
// unavailable: this core can't decode strings on this build
std::string keep = r == db::Result::ok ? s : ""; // copy a string you keep
```

- A record that does not decode gives `invalid` from `string_at`, and a null `s` from `get`, `cell` and `row_name`.
- When the core can't decode on this build (the key or the record layout is missing), `string_at` returns `unavailable` and the log says which:

```text
[dbread] string reads unavailable: k_Gdb_StringXorKey does not resolve on this build
[dbread] string reads unavailable: this core was built without the GdbStringRecord layout
```

- The core keeps one decoded copy per record, so a returned pointer normally stays valid for the whole session.
- That table holds at most 32768 strings (8 MiB). Past that, a string's pointer stays valid only until the next `db` call on the same thread, and the log has one line:

```text
[dbread] string table full (<n> strings, <n> bytes); further strings are valid until the next db call on the calling thread
```

## Tables

`open` puts a cursor on a table's first row, `step` moves it on, `cell` reads one field of the current row and `row_name` gives the row's label. You own the `SwoftyDbCursor`, so zero-initialise it before `open` or `row`.

```cpp
db::Cursor cur{};
for (db::Result r = db::open("entrants.teams", &cur); r == db::Result::ok; r = db::step(&cur)) {
    db::Value id{};
    db::cell(&cur, "stat_id", &id);
    swofty::log("[my-addon] team %llu of %llu: %s, stat_id %lld", (unsigned long long)cur.index,
                (unsigned long long)cur.count, db::row_name(&cur), (long long)id.i);
}
```

- `open(table, cursor)` returns `ok` at the first row, or `late` when the table has no rows.
- `step(cursor)` returns `ok` at the next row, or `late` when the table is exhausted.
- `cell(cursor, field, out)` reads one field of the current row; `invalid` when the row has no such field.
- `row_name(cursor)` returns the current row's label (its `_metadata_`), or `nullptr`.
- `cursor.index` is the current row and `cursor.count` the row count.

The core finds the rows from the table's path: it drops a trailing `s` from the last segment and reads `<table>._<stem>_instancecount_` rows named `<table>.<stem>_<n>`. `open("entrants.teams")` walks `entrants.teams.team_0` onwards. The stem is case-sensitive, so a table whose rows are named another way returns `late` from `open`: `tracks` holds `tracks.Track_<n>` (43 rows in the shipping file) and `open("tracks")` looks for `tracks.track_<n>`. Read the count and build the keys yourself for those. Progressive Career does this for the statistics table, whose rows are `StatisticInfo_<n>`:

```cpp
// addons/progressive/native/src/names.cpp, trimmed
template <class F> void each_row(const std::string &table, const std::string &stem, F f)
{
    int64_t count = 0;
    if (swofty::db::int_at(table + "._" + stem + "_instancecount_", &count) != swofty::db::Result::ok || count <= 0)
        return;
    for (int64_t i = 0; i < count; ++i)
        f(table + "." + stem + "_" + std::to_string(i));
}

each_row("tracks", "Track", [](const std::string &row) { /* row is "tracks.Track_<n>" */ });
each_row("driver_statistics.driver_statistics.driver_statistics_info", "StatisticInfo",
         [](const std::string &row) {
             int64_t id = 0;
             swofty::db::int_at(row + ".stat_id", &id);
         });
```

## row

`row` puts a cursor on the row whose label (`_metadata_`) is `label`, searching rows 0 to `_<row>_instancecount_` - 1:

```cpp
db::Cursor cur{};
if (db::row("entrants.teams", "ferrari", &cur) == db::Result::ok) {
    db::Value id{};
    db::cell(&cur, "stat_id", &id);
    swofty::log("[my-addon] ferrari is row %llu, stat_id %lld", (unsigned long long)cur.index, (long long)id.i);
}
// invalid: no row has that label
```

## Inspecting from the log

At the window stage the core logs whether the database is ready, then reads the keys and tables listed under `"db_inspect"` and `"db_inspect_tables"` in `swofty/assets.json`, if there are any:

```text
[dbread] window stage: database not loaded; nothing to inspect
[dbread] window stage: database ready (available), no "db_inspect" entries in swofty/assets.json
[dbread] window stage: database ready; <n> key(s), <n> table(s) to inspect
[dbread] inspect <key> as <type>: get ok tag <int|float|bool> i=<i> f=<f>
[dbread] inspect <key>: int_at ok <value>
[dbread] table <table> field <field>: open ok, <n> row(s)
[dbread] table <table>[<index>] name="<label>" <field> ok tag int i=<value>
[dbread] table <table> row "<label>": ok index <index> name="<label>" <field> i=<value>
```

## Design

[The native-libraries design](/design/native-libraries#_4-database-read) has the design of this library.
