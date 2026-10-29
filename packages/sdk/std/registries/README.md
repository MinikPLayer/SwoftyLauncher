# Registry schemas

One folder per core registry, named after the registry (`Registry<T>::name()`):

```text
registries/
  career.season/
    1.0.json          the schema of major 1, minor 0; a released file is never edited again
    status.json       { "status", "since" } (see docs/std/versioning.md#status)
    CHANGES.md        one line per version: what changed and in which launcher
    examples/         data files that are valid for every schema of their major
      must-fail/      data files that every schema of their major refuses
```

The 1.6.0 registries: `career.season`, `career.driver_ratings`, `content.package`, `era.box`, `vehicle`, `vehicle.package`, `engine_sound`, `team.constructor`, `team.identity`, `team.logo`, `team.pit_times`, `driver.person`, `driver.identity`, `driver.head`, `driver.look`, `driver.stats`, `helmet`, `suit`, `gloves`, `wearable.season_set`.

`career.calendar`, `career.mode` and `vehicle.handling` have schemas; the 1.6.0 core stores their records and applies nothing from them.

The 1.5 libraries store what addons give them in six more: `career.content` (career contents), `hub.tab` (Team HQ tabs), `career.wizard` (the career wizard), `career.character` (characters), `lang.string` (strings) and `addon.option` (addon options). The core declares them and adds a record under the calling addon's id for each 1.5 call; `lang.string` records are `game/<lng key>`, and a 1.5 `lang::set` writes the calling addon's global layer of one.

## A schema file

A schema describes one record's value: the JSON form of the record struct, as data files write it and as the core's validation reads it. It uses a subset of JSON Schema:

- `type`, `enum`, `const`, `properties`, `required`, `additionalProperties`, `items`, `minItems`, `maxItems`, `uniqueItems`, `minimum`, `maximum`, `exclusiveMinimum`, `exclusiveMaximum`, `minLength`, `maxLength`, `pattern`, `anyOf`, `oneOf`, `allOf`, `$defs`, `$ref` (only to `#/$defs/<name>`, with no sibling keywords but annotations and `default`), `default`.
- Annotations: `$schema`, `title`, `description`, `$comment`.
- `tools/std/schemacheck.py` checks the subset, the layout and the version rules; `tools/std/validate.py` checks the examples.

The registry keywords, at the top level:

- `x-registry`: the registry name.
- `x-version`: the record struct's `version`, `"<major>.<minor>"`.
- `x-applies`: `RegistryOptions::applies`: `boot`, `content`, `session` or `read`.
- `x-snapshot`: `RegistryOptions::snapshot`. `x-snapshot-budget`: `snapshot_budget` in bytes, present when `x-snapshot` is true.
- `x-shared`: a registry of shared ids (`team.constructor` holds `constructor/<key>`): `provider_registry` is the registry whose records provide them, `provides` the pointer of the list of ids in those records, `resolved` the pointer in the shared record that holds the winning provider's id (the last enabled provider in load order).
- `x-adapter`: on the first file of a later major, `"<name>@<previous major>"`, the adapter that converts between them. No 1.6.0 registry has a second major.

The field keywords:

- `x-ref`: the value is a `Ref<T>` to a record of the named registry: a full record id, checked by the validator, with the modifiers below.
- `x-key`: the array is a `Keyed<T, &T::field...>` list; its elements are matched by these fields. Every key field is required in an element, and two elements never share a key.
- `x-missing`: what may happen when the referenced record is missing (see docs/std/dependencies.md#when-a-reference-is-missing).

## Values

- Keys are the struct's field names, in the struct's order. `cls` is written `"class"`.
- `Ref<T>`: the full id of a record (`"game/ferrari_2025"`, `"swofty.progressive/2017"`), or `null` for an empty reference. A data file can add modifiers: `"venue/sochi?"` is optional (the element that holds it is dropped when nothing provides it), and `"venue/sepang|venue/bahrain"` is a fallback chain.
- `Text`: a string (a lang key) or `{ "literal": "<text>" }`.
- `Path`: a file or folder in the addon's folder (`"sounds/v10.bdl"`, `"cars/ferrari/"`), `{ "stock": "<game path>" }` for the game's original file, `{ "erp", "prefix", "kinds" }` or `{ "erp", "resources" }` for resources in an archive, or `null` for an empty path.
- `Colour`: `"#RRGGBB"` or `"#AARRGGBB"`.
- `StatId`: an integer `>= 0`, or `"auto"`. Absent is `"auto"`.
- `Curve`: an array of `[x, y]` number pairs.
- An `enum`: the enumerator's name as a string.
- `std::optional<T>`: absent or `null`.
- `std::array<T, N>`: an array of exactly `N` items. `std::vector<T>`: an array. `std::map<K, V>`: an object.
- `std::chrono::year_month_day`: `"YYYY-MM-DD"`.
- `Keyed<T, ...>`: an array of objects, with `x-key`.

## Records and data files

- A record is open: `additionalProperties` is true at the top level. A field the schema doesn't list is a field from a newer minor; readers keep it in `extra`. A nested struct is closed: `additionalProperties` is false.
- `required` lists what an effective base record must have.
- `"extends": "<base id>"` makes a data-file record a variant: its value is a merge patch on the base's effective value. It is checked against the schema after it resolves, as the merged value.
- `"$protect": ["<field>", ...]` inside your own record lists the fields other addons' selectors skip.
- `"$delete": true`, with only an element's key fields, removes that element from a keyed list in a layer. A record never holds it.
- A layer is a JSON Merge Patch: `required` doesn't apply, and `null` removes a field (never a required one). Keyed lists merge by key; other lists are replaced whole.
- `"$select"` holds selector layers: a list of entries with `ids`, `where` or `tag` (and optionally `except`, `in`, `when`) and a `patch` (see docs/std/layers.md).

## Examples

Every example is a data file, keyed by the registry name:

- `examples/<name>.json`: records, `{ "<registry>": { "<key>": { ... } } }`. A key is the addon's own key, without a `/`.
- `examples/<name>layer.json` and `examples/<name>layers.json` (a file name ending in `layer` or `layers`): layers, `{ "<registry>": { "<full id>": { patch }, "$select": [ ... ] } }`.
- `examples/must-fail/*.json`: the same forms, each refused by the schema. Each failing record or patch holds a `"$comment"` naming why it fails.
- `<name>@<major>.json` is checked against that major only, and `<name>@<major>.<minor>.json` against that one schema.
- `tools/std/validate.py` checks a record with `"extends"` as written, as a whole record, so a variant example restates the base's required fields.
