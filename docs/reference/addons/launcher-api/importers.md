---
title: Importers
description: How a launcher plugin claims files and folders dropped on the launcher window - imports by extension, claims decided by the program, and the import call.
---

# Importers

An importer takes files dropped on the launcher window: the plugin claims them by extension, or decides for itself, and the launcher sends them to its `import` call.

## An importer in full

The template claims `.exmod` files and copies each into its data folder:

```json [swofty.json]
"launcher": {
  "program": { "windows": "bin/example-helper.exe", "linux": "bin/example-helper" },
  "sections": [{ "id": "items", "title": "Example", "icon": "package" }],
  "imports": ["exmod"]
}
```

```rust [src/main.rs]
fn import(&self, host: &mut Host, paths: &[PathBuf]) -> Result<Vec<String>, String> {
    std::fs::create_dir_all(self.items_dir()).map_err(|e| e.to_string())?;
    let mut lines = Vec::new();
    for (i, path) in paths.iter().enumerate() {
        if !is_item(path) {
            lines.push(format!("Skipped {}: not a .{EXTENSION} file", path.display()));
            continue;
        }
        let name = path.file_name().and_then(|n| n.to_str())
            .ok_or_else(|| format!("{} has no name", path.display()))?;
        // fills the progress card, 0.0 to 1.0
        host.progress(i as f32 / paths.len() as f32, &format!("Importing {name}"));
        std::fs::copy(path, self.items_dir().join(name)).map_err(|e| format!("{name}: {e}"))?;
        lines.push(format!("Imported {name}"));
    }
    Ok(lines) // one line per file
}
```

```text
launcher  {"id":3,"method":"import","params":{"paths":["/home/you/Downloads/okayama.exmod","/home/you/Downloads/suzuka.exmod"]}}
plugin    {"method":"progress","params":{"done":0.0,"text":"Importing okayama.exmod"}}
plugin    {"method":"progress","params":{"done":0.5,"text":"Importing suzuka.exmod"}}
plugin    {"id":3,"result":["Imported okayama.exmod","Imported suzuka.exmod"]}
```

- Files routed to your plugin arrive together in one `import` call, as absolute paths.
- `import` has 300 seconds; each `progress` resets the wait.
- A [`pick` button](./views#buttons) gets files from the file picker instead, and the template passes them to the same function.

## How a drop is routed

1. The launcher claims addons first: a folder with `swofty.json`, a `.swoftyaddon` file (or a legacy `.swoftymod`), or a zip that holds an addon. Those install as addons.
2. It checks every plugin with `imports` or `claims`, in registry order. A plugin with `imports` claims a file when its extension matches, ignoring case. A plugin with `claims` decides for itself.
3. A claimed file goes to that plugin's `import`. An unclaimed one opens a dialog that names it, with **Go to Simplified Importer** and **Read the addon docs**.

## claims

Matching extensions can't tell a mod's zip from any other zip, and can't claim a folder. To decide for yourself, set `claims` to `true`:

```json [swofty.json]
"launcher": {
  "program": { "windows": "bin/importer.exe", "linux": "bin/importer" },
  "imports": ["zip"],
  "claims": true
}
```

The launcher then runs your program on its own for each drop, and reads one line:

```text
$ bin/importer claims /home/you/Downloads/okayama.zip
yes
```

```rust [src/main.rs]
fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    if let [verb, path] = args.as_slice() && verb == "claims" {
        // yes, maybe or no; anything else falls back to imports
        println!("{}", if looks_like_a_mod(std::path::Path::new(path)) { "yes" } else { "no" });
        return;
    }
    // …the protocol loop
}
```

- The answer is `yes`, `maybe` or `no`. A `maybe` wins only when no other plugin says `yes` or `maybe`.
- Answer within 5 seconds, without the protocol: there is no `hello` and no profile. The working directory is the addon folder, and stdin and stderr are closed.
- If your program fails, stalls or prints anything else, the launcher falls back to `imports`.
- A Rust helper that uses `swofty_addon::helper::run_helper` answers from its `Importer::claims` for you ([The Rust side](./protocol#the-rust-side)).
