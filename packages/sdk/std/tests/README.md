# The registry corpus

The shared test corpus for the registry rules: bases, variants, providers, layers, selectors, tags, protected fields, scopes, option-bound layers, `.in()` filters, references, validation and conflicts. The registry core (W3), the live apply paths (W6), the recorder and fake core (W9) and `tools/std/reference.py` all run every case and must give the same results. A disagreement fails the build of the side that disagrees.

```text
packages/sdk/std/tests/
  README.md                this file
  check_cases.py           checks that every case is well formed (python3, standard library only)
  schema/case.schema.json  the case format as a JSON Schema (draft 2020-12)
  cases/NNN-<slug>.json    one rule per case
```

```sh
python3 packages/sdk/std/tests/check_cases.py     # every case well formed; exit code 1 otherwise
```

The corpus covers resolution only. Snapshots and migration are tested by the save fixtures in `packages/sdk/std/fixtures/` (W4). When each registry applies in the game, and the put-back at the end of content, are W6's in-game tests. `watch`, `invalidate` and adapters are not in the corpus.

## A case

```json
{
  "name": "two addons set one field: the later wins and the conflict names both",
  "rule": "load order decides between layers ... (layers.md#precedence)",
  "registries": { "test.thing": { "applies": "read", "key_fields": { "/items": ["key"] } } },
  "bases": [ { "addon": "game", "registry": "test.thing", "key": "a", "value": { "name": "a", "n": 10, "kind": "f1", "items": [] } } ],
  "tags": {},
  "addons": [
    { "id": "mod.a", "ops": [ { "op": "layer", "registry": "test.thing", "target": "game/a",
                                "edit": [ { "op": "add", "path": "/n", "value": 1 } ] } ] },
    { "id": "mod.b", "ops": [ { "op": "layer", "registry": "test.thing", "target": "game/a",
                                "edit": [ { "op": "mul", "path": "/n", "value": 2 } ] } ] }
  ],
  "evaluations": [ {
    "context": { "profile": "default", "career": null },
    "expect": { "test.thing": { "game/a": { "status": "ok", "value": { "name": "a", "n": 22, "kind": "f1", "items": [] },
                                            "origins": { "/n": "mod.b" } } } },
    "conflicts": [ { "kind": "field", "registry": "test.thing", "id": "game/a", "pointer": "/n",
                     "addons": ["mod.a", "mod.b"], "winner": "mod.b" } ],
    "refusals": []
  } ]
}
```

### Top-level keys

- `name`: one line saying what the case shows.
- `rule`: the rule under test, with the page and anchor in `docs/std/` that states it. Where the docs leave a choice, the rule spells out the computation.
- `registries`: every registry the case uses, by name.
  - `applies`: `boot`, `content`, `session` or `read` (`RegistryOptions::applies`).
  - `key_fields`: keyed lists (`Keyed<T, &T::k...>`, `x-key`), as pointer pattern -> key fields in order. `*` in a pattern stands for every element of a list: `"/teams"`, `"/teams/*/drivers"`.
  - `refs`: the references the runner resolves and checks, as pointer pattern -> target registry: `"/rounds/*/venue": "track.venue"`. A string field not listed here is plain data.
  - `schema`: a JSON Schema subset the effective value must pass after every edit (`type`, `properties`, `required`, `minimum`, `maximum`, `enum`, `pattern`).
  - `shared`: a registry of shared ids (`venue/`, `person/`, `constructor/`). `provider_registry` holds the records that provide them, `provides` is the pointer of the list of shared ids in those records, `resolved` is the pointer in the shared record that the runner sets to the winning provider's id.
- `bases`: the core's stock records, id `game/<key>`.
- `tags`: the core's tags, registry -> tag id -> member ids. Members whose record is missing are part of the data.
- `addons`: every addon of the case, in load order. `game` is not listed; it is always on and first. `enabled: false` turns an addon off in every evaluation.
- `evaluations`: one or more reads of the same setup, each with its own context.

### Ops

Each addon's `ops` run in the order listed. `stage` is `register` (the default: `SWOFTY_REGISTER`) or `on_load` (`SWOFTY_ON_LOAD` or later).

```json
{ "op": "add", "registry": "vehicle", "key": "redbull_2021", "value": { "label": "rb16b" } }
{ "op": "variant", "registry": "test.thing", "key": "a2", "base": "game/a", "patch": { "name": "a2" } }
{ "op": "provide", "registry": "career.driver_ratings", "records": { "2017/c": { "...": "..." }, "2017/b": null } }
{ "op": "layer", "registry": "test.thing", "target": "game/a", "patch": { "n": 2 }, "scope": "profile", "profile": "p1" }
{ "op": "layer", "registry": "test.thing", "select": { "ids": "swofty.cs/*", "except": ["swofty.cs/x"] }, "patch": { "kind": "classic" } }
{ "op": "layer", "registry": "vehicle.handling", "select": { "where": { "class": "f1" } },
  "when": { "option": "damage", "is": "heavy" }, "patch": { "damage_scale": 1.6 } }
{ "op": "layer", "registry": "test.thing", "target": "game/a", "patch": { "n": 1 }, "in": [ { "mode": "driver_career" } ] }
{ "op": "clear_layer", "registry": "test.thing", "target": "game/b" }
{ "op": "tag_add", "registry": "vehicle", "tag": "c/f1_cars", "id": "mod.f1-2021-port/redbull_2021" }
{ "op": "protect", "registry": "vehicle", "id": "swofty.classic-seasons-2017/ferrari", "fields": ["performance_tier"] }
```

- `add`: `Registry::add`. The id is `<addon>/<key>`.
- `variant`: `Registry::variant`, or a data file's `"extends"`. `patch` or `edit` is the variant's own change.
- `provide`: a dynamic provider (`Registry::provide`) that answers `<addon>/<key>` for each key with that value, and `null` (`std::nullopt`) for a key whose value is `null`. Its list callback lists the keys with a value.
- `layer`: `Registry::layer` on one `target`, or on a `select`. `patch` is a data file's merge patch; `edit` stands in for a code edit (below).
  - `scope`: `global` (default), `profile` with the `profile` it belongs to, or `career` with the `career` it belongs to.
  - `in`: `LayerHandle::in` and a `$select` entry's `"in"`. Every selector in the list must match the context. One selector is one of `{ "mode" }`, `{ "content" }` (the content or a variant of it), `{ "content_exact" }`, `{ "data_group" }`, `{ "setup_mode" }`, `{ "owner" }`, `{ "any": [...] }`, `{ "not": {...} }`.
  - `when`: `swofty::option<T>()` in an edit, and a `$select` entry's `"when"`: `{ "option", "is": v }`, `{ "option", "in": [...] }`, `{ "option", "on": true }`, `{ "option", "off": true }`. The option is one of the layer's own addon's options.
- `select`: `ids` is `"*"` or `"<prefix>/*"` (`under(prefix)`), `except` lists full ids, `where` matches fields of the base value (a value, a list of values, or `{ "min", "max" }`, inclusive), `tag` is a tag id (`tagged(tag)`) or `{ "<ref field>": "<tag id>" }` (`tagged(&T::field, tag)`, the tag of the field's target registry). Every condition given must match.
- `clear_layer`: `Registry::clear_layer` for the scope given (`scope`, `profile`, `career` as in `layer`).
- `tag_add`: `Registry::tag_add`.
- `protect`: `Registry::protect`, or `"$protect"` in a data file. Fields are pointers without the leading `/`.

### Patches and edits

A `patch` is a JSON Merge Patch (RFC 7396) with keyed lists:

- An object merges field by field. `null` removes a field.
- A keyed list (`key_fields`) merges element by element. A patch element names its key fields; it merges into the element with the same key, or is appended at the end when the list has none. `"$delete": true` removes the element with that key, and changes nothing when the list has none.
- Any other list, and any scalar, is replaced whole.

An `edit` is a list of steps, standing in for a code edit that reads the value it changes:

```json
[ { "op": "set", "path": "/teams/slot=4/team", "value": "game/honda" },
  { "op": "add", "path": "/n", "value": 1 },
  { "op": "mul", "path": "/tyre_wear_scale", "value": 1.2 } ]
```

`path` is a pointer in the keyed form, and the element or field it names must exist. `set` writes the value, `add` adds a number, `mul` multiplies by one.

### Evaluations

```json
{
  "context": { "profile": "p1", "career": "c1", "mode": "time_trial",
               "content": ["mod.esp/f1_2017_v10", "swofty.classic-seasons-2017/f1_2017"],
               "data_group": null, "setup_mode": "swofty_f1",
               "options": { "kizi.wet": { "wet": true } },
               "settings": { "someone.pace-equaliser": { "also_change_year_packs": true } },
               "disabled": ["mod.direct"] },
  "expect": { "<registry>": { "<id>": { "status": "ok", "value": {}, "origins": {} },
                              "<id>": { "status": "no_record" } } },
  "conflicts": [ { "kind": "field|protected|provider", "registry", "id", "pointer", "addons": [], "winner" } ],
  "refusals": [ { "addon", "registry", "id", "op", "reason": "invalid|boot_filtered|late|cycle|too_deep" } ],
  "lists": [ { "registry", "prefix", "ids": [] } ],
  "tag_members": [ { "registry", "tag", "ids": [] } ]
}
```

- `context`: the moment the read happens. Every key is optional. Defaults: `profile` `"default"`, `career`, `mode`, `content`, `data_group`, `setup_mode` `null`, `options` and `settings` `{}`, `disabled` `[]`. `content` is the active content's variant chain, the content itself first. `disabled` turns addons off for this evaluation.
- `expect`: the records checked, with their `status` (`ok`, `no_record`, `no_registry`, `invalid`, `late`). An `ok` record has its complete effective value, as a reader sees it, and its `origins`. Records the case doesn't name are not checked.
- `origins`: pointer -> the addon whose edit last changed that pointer, for every pointer some edit in the record's chain changed and that is still in the value. Pointers name keyed elements by key in key-field order (`/teams/slot=1/seats`, `/changes/round=15,slot=8,seat=1/in`), and list elements by index. A pointer not listed comes from the provider: `origin()` returns the provider's addon id for it, or the addon of the longest listed prefix.
- `conflicts`: every conflict of the evaluation.
  - `field`: two or more addons' edits changed one pointer of one record. `addons` lists each one once, in load order. `winner` is the pointer's origin.
  - `protected`: a selector's change to a protected field was dropped. `addons` lists the selectors' addons; `winner` is the pointer's origin (the provider when no edit set it).
  - `provider`: one id provided twice by its addon (`pointer` `""`), or a shared id provided by more than one enabled addon (`pointer` the `resolved` pointer).
- `refusals`: every op the core refused or dropped, with its addon, registry, the record id (for a selector, the record it failed on), the op's index in the addon's `ops`, and the reason. A refusal made at registration is listed in every evaluation.
- `lists`: `Registry::all(prefix)`, sorted.
- `tag_members`: `Tag::members()`, sorted.

## Running a case

1. **Declare** every registry in `registries`. Add `bases` as `game/<key>` and the core tags in `tags`.
2. **Register** each enabled addon in load order (an addon is enabled when `enabled` isn't `false` and it isn't in the context's `disabled`). Run its ops in order through the API under test: the C table, `Registry<json::Value>`, or the reference. Record refusals.
3. **Resolve** each record of `expect`, `lists` and `tag_members` for the evaluation's context, and collect origins, conflicts and refusals.
4. **Compare** with the evaluation. Repeat for each evaluation; registration may be redone per evaluation.

### How a record resolves

- **The provider value.** A base, an `add` (a second `add` of the same key replaces the first), or a `provide` answer, which beats an `add` of the same id. A shared id takes the core's base and sets `resolved` to the id of the last enabled provider record in load order.
- **A variant:** `effective(base without S) + the variant's patch + the layers on the variant`, where `S` is every selector that matches the variant. The chain resolves from the root. A selector that matches the variant runs once, among the variant's own layers. A selector that matches the base and not the variant reaches the variant through the base.
- **The layers on a record**, in this order: addons in load order; within an addon, `global`, then `profile`, then `career`; within a scope, selectors before single-id layers; then registration order. A layer is active when its addon is on, its scope's profile or career is the context's, every `in` selector matches and its `when` matches the addon's option. One single-id layer exists per addon, record and scope: a second replaces the first, and `clear_layer` removes it.
- **A selector matches** on the record's base value: its chain's provider values (the root's value and each variant's patch), with no layer applied. Tag membership follows variants: a variant is in every tag its base is in.
- **Each edit's result is validated:** the registry's `schema`, and every required ref resolving. A failing layer is dropped whole, on every record it touches.
- **Protected fields:** a change by another addon's selector to a pointer the record's owner protects is reverted, unless that addon's `also_change_year_packs` setting is on.
- **References resolve last,** for the reader: `"a|b"` becomes the first id that resolves, `"a?"` becomes `"a"` or, when `a` doesn't resolve, removes the nearest enclosing list element. A required ref that doesn't resolve makes the record `invalid`.

### Comparing

- Values compare as JSON after canonical key order. A field set to `null` and a missing field are equal.
- Numbers compare as doubles: equal when `|a - b| <= 1e-6 * max(1, |a|, |b|)`. A `float` field written by C++ (`1.2f`) equals `1.2`.
- List order matters. Keyed lists keep the provider's order, with appended elements at the end.
- `conflicts` and `refusals` compare as sets and must match exactly. `origins` must match exactly.
- The corpus never relies on schema defaults: every value lists every field it has.

## The registries the cases use

The real registries keep their real names and field names, cut down to the fields a case needs: `career.season` (`teams` keyed by `slot`, `changes` by `round`, `slot`, `seat`), `career.driver_ratings`, `career.calendar` (`rounds` keyed by `round`), `content.package` (`teams` keyed by `key`), `vehicle`, `vehicle.handling` (`class` is `cls` in C++), `ai.vehicle_config`, `engine_sound`, `track`, `track.venue`, `team.identity`, `team.logo` (`images` by context), `suit`, `gloves` and `wearable.season_set`. `test.thing` is a registry for tests only: `{ "name": string, "n": number, "kind": string, "items": [ { "key", "v" } ] }`, with `items` keyed by `key`.

## Rules the corpus fixes

Where the docs leave a choice, the corpus takes these:

1. An addon that adds one key twice: the later add wins, listed as a `provider` conflict with pointer `""`. It isn't refused.
2. A layer on a record nobody provides is kept, not refused, and applies once the record exists, whatever the provider's place in load order.
3. An addon's layers order by scope (global, profile, career), then selectors before single-id layers, then registration order.
4. A variant resolves as `effective(base without S) + patch + layers on the variant`. A base layer later in load order than a selector `S` still lands before `S` on the variant: case 015 gives 31.
5. `where` and tag tests on a variant read its chain's provider values with no layers applied.
6. A variant's patch is an edit by its addon: it records origins and takes part in field conflicts. A root `add` or `provide` is not an edit.
7. A variant's conflicts include the edits it inherits from its base.
8. An edit that leaves a value as it was records no origin and no conflict.
9. Origins are recorded per object field, per keyed element (an appended element at the element's pointer) and per whole other list or scalar. Removed elements leave the origins.
10. The variant that closes a cycle (the later in load order) is refused with `cycle`. Every variant in the cycle, and every variant built on one, is `invalid`.
11. A provider value with a required ref that doesn't resolve is `invalid`. A layer that introduces one is dropped with `invalid`.
12. An optional ref outside any list element becomes `null` when it doesn't resolve.
13. A shared id with no enabled provider is `no_record`, though the core declares it.
14. A selector whose edit fails on one record is dropped on every record, with one refusal naming the first failing record in id order.
15. `protect` holds against other addons' selectors only, on the record named only (not its variants).
16. The `in` list of a layer is an AND. `content` matches any id of the active content's variant chain, `content_exact` the first only.
17. A case gives a value for every option its layers bind.
18. On an `Applies::boot` registry, `add`, `variant` and `layer` at `on_load` are refused as `late`, and a layer with `in` is refused as `boot_filtered`.
19. Refusals made at registration are listed in every evaluation.
20. `Tag::members()` lists variants of members, and skips members whose record is missing or whose addon is off.
