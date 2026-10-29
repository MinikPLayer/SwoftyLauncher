---
title: The build reply
description: The asset ops a launcher plugin returns from build, with lines, warnings, items and fields.
---

# The build reply

A plugin that sets `"build": true` returns asset ops from `build`, and the launcher applies them with the profile's other ops.

## A build hook in full

The template writes the items that are on to a file in `cache_dir`, and puts it into the game:

```json [swofty.json]
"launcher": {
  "program": { "windows": "bin/example-helper.exe", "linux": "bin/example-helper" },
  "build": true
}
```

```rust [src/main.rs]
fn build(&self, host: &mut Host) -> Result<Value, String> {
    let on = self.enabled(host)?;
    let text = on.join("\n");
    let file = self.cache_dir.join("enabled.txt");
    // rewrite only on a change
    if std::fs::read_to_string(&file).ok().as_deref() != Some(text.as_str()) {
        std::fs::write(&file, &text).map_err(|e| e.to_string())?;
    }
    Ok(json!({
        "ops": [{
            "op": "file.put",
            "source": { "root": "cache", "path": "enabled.txt" },   // cache_dir/enabled.txt
            "target": "example-launcher-addon/enabled.txt"          // a path in the game folder
        }],
        "lines": [format!("{} items on", on.len())]
    }))
}
```

```text
launcher  {"id":4,"method":"build","params":{"options":{"size":24},"features":{"stable":["group","erp","db","lang","file"],"experimental":[]}}}
plugin    {"id":0,"method":"get_state","params":null}
launcher  {"id":0,"result":{"on":["okayama.exmod"]}}
plugin    {"id":4,"result":{"ops":[{"op":"file.put","source":{"root":"cache","path":"enabled.txt"},"target":"example-launcher-addon/enabled.txt"}],"lines":["1 items on"]}}
```

## The reply

```json
{
  "ops": [ { "op": "file.put", "source": { "root": "cache", "path": "enabled.txt" }, "target": "example-launcher-addon/enabled.txt" } ],
  "items": [ "okayama" ],
  "lines": ["1 item on"],
  "warnings": [],
  "fields": { "entrants.teams.team": ["_metadata_", "stat_id", "display_name_code"] }
}
```

Every key may be left out.

### ops

- Write each op exactly as you would in an [op file](../asset-api#op-files): set the `op` field to `<kind>.<name>`, such as `file.put`, `db.set`, `erp.patch-pkg`, `lang.set` or `warn`.
- For a `source` inside the addon folder, use a relative path (`"art/okayama.dds"`). Use `{ "root": "data" | "cache", "path": "…" }` to read from `data_dir` or `cache_dir`.
- The launcher tags every op with this addon's id as its origin, routes them like declared ops, and reports conflicts by origin ([Asset API](../asset-api#deliveries)).
- The build calls `build` for each enabled addon in load order, after that addon's declared ops.
- How these ops reach the game while it boots: [Changes at runtime](../runtime-assets#from-a-build-hook).

### features

`build.params.features` lists the op kinds the installed loader supports, `stable` and `experimental`: `group`, `erp`, `db`, `lang`, `file`. Check them before returning ops, and drop any op the loader cannot apply.

```json
{ "options": { "size": 24 }, "features": { "stable": ["file"], "experimental": [] } }
```

`options` holds the profile's value for every option your addon declares ([Settings](../settings)).

### lines and warnings

The build prints `lines` and `warnings` with the addon id as a prefix.

### items

When your addon loads several items, such as the mods in a library, `items` names the item each op comes from, by position in `ops`, with `null` for an op that belongs to no item:

```json
{ "ops": [ { "op": "file.put", … }, { "op": "file.put", … }, { "op": "lang.set", … } ],
  "items": [ "okayama", "okayama", null ] }
```

- The launcher adds the item to the op's origin, so each item gets its own runtime overlay archive and conflicts name it.
- Leave `items` out, or shorter than `ops`, and the remaining ops have no item.

### fields

*Launcher 1.0.12.* The field names a `db.add-row` copies, in the same shape as an [op file's `fields`](/reference/ops/database): an object keyed by `<table>.<row>`, each holding a list of field names.

```json
{
  "ops": [ { "op": "db.add-row", "table": "entrants.teams",
             "from": "entrants.teams.team[_metadata_=mclaren_26]",
             "set": { "_metadata_": "team12_26" } } ],
  "fields": { "entrants.teams.team": ["_metadata_", "stat_id", "display_name_code"] }
}
```

- `fields` applies to every `db.add-row` of your addon, from the reply and from your op files.
- An op file's own `fields` takes precedence for the tables it lists.
- An `add-row` whose table is in neither fails with `field names unknown`.
- A launcher before 1.0.12 ignores `fields`: set `"compat": { "launcher": ">=1.0.12" }` when you return `add-row` ops.
