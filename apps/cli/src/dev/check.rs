use std::collections::BTreeSet;
use std::path::{Path, PathBuf};
use std::process::Command;

use anyhow::Result;
use serde_json::Value;
use swofty_core::manifest::{AddonManifest, MANIFEST_FILE};
use swofty_core::stdlib::manifest::{RegistryManifest, Target};
use swofty_core::store::Store;

use super::lock::LOCK_FILE;

pub const REGISTRY_FILE: &str = "registry.json";

#[derive(Debug, Default)]
pub struct Report {
    pub lines: Vec<String>,
    pub errors: usize,
    pub warnings: usize,
}

impl Report {
    fn error(&mut self, s: impl Into<String>) {
        self.lines.push(format!("error: {}", s.into()));
        self.errors += 1;
    }

    fn warn(&mut self, s: impl Into<String>) {
        self.lines.push(format!("warning: {}", s.into()));
        self.warnings += 1;
    }

    fn ok(&mut self, s: impl Into<String>) {
        self.lines.push(format!("ok: {}", s.into()));
    }
}

pub fn check(store: &Store, sdk: &Option<PathBuf>, folder: &Path) -> Result<Report> {
    let mut r = Report::default();
    let id = match AddonManifest::load(folder) {
        Ok(m) => {
            r.ok(format!("{MANIFEST_FILE}: {} {}", m.id, m.version));
            m.id
        }
        Err(e) => {
            r.error(format!("{MANIFEST_FILE}: {e}"));
            return Ok(r);
        }
    };
    let manifest = super::manifest_json(folder)?;
    let mut needs: Vec<(Vec<String>, bool, String)> = Vec::new();
    match RegistryManifest::load(folder) {
        Ok(None) => {}
        Err(e) => r.error(e),
        Ok(Some(m)) => registry_json(&m, &mut r, &mut needs),
    }
    let has_register = match super::register_dlls(folder, &manifest) {
        Ok(dlls) => !dlls.is_empty(),
        Err(e) => {
            r.warn(format!("{e}"));
            false
        }
    };
    let mut own: BTreeSet<String> = BTreeSet::new();
    match read_json(&folder.join(LOCK_FILE)) {
        None if has_register => r.warn(format!(
            "the DLL has a SWOFTY_REGISTER but there's no {LOCK_FILE}: swofty dev pack writes it"
        )),
        None => {}
        Some(Err(e)) => r.error(format!("{LOCK_FILE}: {e}")),
        Some(Ok(lock)) => {
            if !has_register {
                r.warn(format!(
                    "{LOCK_FILE} is there but no DLL has a SWOFTY_REGISTER: swofty dev pack removes it"
                ));
            }
            own.extend(provided(&lock));
            for n in lock["needs"].as_array().into_iter().flatten() {
                if let Some(rec) = n["record"].as_str() {
                    let optional = n["policy"].as_str() == Some("optional");
                    let mut alts = vec![rec.to_owned()];
                    alts.extend(
                        n["or"]
                            .as_array()
                            .into_iter()
                            .flatten()
                            .filter_map(|o| o.as_str().map(str::to_owned)),
                    );
                    needs.push((alts, !optional, LOCK_FILE.to_owned()));
                }
            }
        }
    }
    let data = data_files(folder);
    own.extend(data_ids(&id, &data));
    if !data.is_empty() {
        validate_data(sdk, folder, &data, &mut r);
        if let Some(sdk) = sdk {
            missing_paths(sdk, folder, &data, &mut r);
        }
    }
    let installed = installed_ids(store, &id);
    for (alts, required, from) in needs {
        if let Some(bad) = alts.iter().find(|a| !a.contains(':')) {
            r.error(format!("{from}: {bad:?} isn't <registry>:<id>"));
            continue;
        }
        let met = alts.iter().any(|a| {
            let rid = a.split_once(':').map_or("", |(_, id)| id);
            rid.starts_with("game/") || own.contains(a) || installed.contains(a)
        });
        if met {
            continue;
        }
        let msg = format!("{from}: no installed addon provides {}", alts.join(" or "));
        if required { r.error(msg) } else { r.warn(msg) }
    }
    Ok(r)
}

fn read_json(path: &Path) -> Option<Result<Value, String>> {
    let text = std::fs::read_to_string(path).ok()?;
    Some(serde_json::from_str(&text).map_err(|e| e.to_string()))
}

fn registry_json(m: &RegistryManifest, r: &mut Report, needs: &mut Vec<(Vec<String>, bool, String)>) {
    for why in &m.skipped {
        r.warn(format!("{REGISTRY_FILE}: {why}; the launcher ignores it"));
    }
    for key in m.extra.keys() {
        r.warn(format!(
            "{REGISTRY_FILE}: the launcher doesn't know the field {key:?} and ignores it"
        ));
    }
    for (list, required) in [(&m.requires, true), (&m.recommends, false)] {
        for (i, n) in list.iter().enumerate() {
            if !required && n.for_.is_none() {
                r.warn(format!(
                    "{REGISTRY_FILE}: recommends[{i}]: add a \"for\" for players"
                ));
            }
            if let Target::Record { registry, id } = &n.target {
                needs.push((
                    vec![format!("{registry}:{id}")],
                    required,
                    REGISTRY_FILE.to_owned(),
                ));
            }
        }
    }
    r.ok(REGISTRY_FILE);
}

pub fn sdk_schema(sdk: &Path, registry: &str) -> Option<Value> {
    let dir = sdk.join("packages/sdk/std/registries").join(registry);
    let mut best: Option<((u32, u32), PathBuf)> = None;
    for e in std::fs::read_dir(&dir).ok()?.flatten() {
        let name = e.file_name().to_string_lossy().into_owned();
        let Some((maj, min)) = name.strip_suffix(".json").and_then(|v| v.split_once('.')) else {
            continue;
        };
        let (Ok(maj), Ok(min)) = (maj.parse::<u32>(), min.parse::<u32>()) else {
            continue;
        };
        if best.as_ref().is_none_or(|(v, _)| (maj, min) > *v) {
            best = Some(((maj, min), e.path()));
        }
    }
    read_json(&best?.1)?.ok()
}

fn missing_paths(sdk: &Path, folder: &Path, files: &[PathBuf], r: &mut Report) {
    for f in files {
        let Some(Ok(doc)) = read_json(f) else { continue };
        let shown = format!("registry/{}", f.file_name().unwrap_or_default().to_string_lossy());
        for (registry, records) in doc.as_object().into_iter().flatten() {
            let Some(schema) = sdk_schema(sdk, registry) else {
                continue;
            };
            for (id, value) in records.as_object().into_iter().flatten() {
                if id.starts_with('$') {
                    continue;
                }
                let mut paths = Vec::new();
                super::paths::schema_paths(&schema, &schema, value, &mut paths, 0);
                for p in paths {
                    if !super::paths::inside(folder, &p) {
                        r.error(format!(
                            "{shown}: {registry} {id} names {p}, which isn't in the addon"
                        ));
                    }
                }
            }
        }
    }
}

fn provided(lock: &Value) -> Vec<String> {
    let mut out = Vec::new();
    for (registry, list) in lock["provides"].as_object().into_iter().flatten() {
        for p in list.as_array().into_iter().flatten() {
            if let Some(id) = p.as_str().or_else(|| p["id"].as_str()) {
                out.push(format!("{registry}:{id}"));
            }
        }
    }
    out
}

fn data_files(folder: &Path) -> Vec<PathBuf> {
    let mut out: Vec<PathBuf> = std::fs::read_dir(folder.join("registry"))
        .map(|rd| {
            rd.flatten()
                .map(|e| e.path())
                .filter(|p| p.extension().is_some_and(|e| e == "json"))
                .collect()
        })
        .unwrap_or_default();
    out.sort();
    out
}

fn data_ids(addon: &str, files: &[PathBuf]) -> Vec<String> {
    let mut out = Vec::new();
    for f in files {
        let Some(Ok(doc)) = read_json(f) else { continue };
        for (registry, records) in doc.as_object().into_iter().flatten() {
            for key in records.as_object().into_iter().flatten().map(|(k, _)| k) {
                if key.starts_with('$') {
                    continue;
                }
                let id = if key.contains('/') {
                    key.clone()
                } else {
                    format!("{addon}/{key}")
                };
                if id.starts_with(&format!("{addon}/")) {
                    out.push(format!("{registry}:{id}"));
                }
            }
        }
    }
    out
}

fn installed_ids(store: &Store, except: &str) -> BTreeSet<String> {
    let mut out = BTreeSet::new();
    let Ok(addons) = store.addons() else { return out };
    for id in addons.keys().filter(|id| *id != except) {
        let dir = store.addons_dir().join(id);
        if let Some(Ok(lock)) = read_json(&dir.join(LOCK_FILE)) {
            out.extend(provided(&lock));
        }
        out.extend(data_ids(id, &data_files(&dir)));
    }
    out
}

fn validate_data(sdk: &Option<PathBuf>, folder: &Path, files: &[PathBuf], r: &mut Report) {
    let (Some(sdk), Ok(py)) = (sdk, super::python()) else {
        r.warn("the data files weren't checked: dev check needs python3 and the SDK's tools/std/validate.py");
        return;
    };
    let names: Vec<String> = files
        .iter()
        .filter_map(|f| f.file_name())
        .map(|n| format!("registry/{}", n.to_string_lossy()))
        .collect();
    let out = Command::new(py)
        .arg(sdk.join("tools/std/validate.py"))
        .arg("--repo")
        .arg(sdk)
        .arg("--data")
        .args(&names)
        .current_dir(folder)
        .output();
    let Ok(out) = out else {
        r.warn("the data files weren't checked: validate.py didn't start");
        return;
    };
    let before = r.errors;
    validate_report(&String::from_utf8_lossy(&out.stdout), &names, r);
    if !out.status.success() && r.errors == before {
        r.error(format!(
            "validate.py failed: {}",
            String::from_utf8_lossy(&out.stderr).trim()
        ));
    }
}

fn validate_report(text: &str, names: &[String], r: &mut Report) {
    let mut current_failed = false;
    for line in text.lines() {
        let (ok, rest) = if let Some(rest) = line.strip_prefix("ok  ") {
            (true, rest)
        } else if let Some(rest) = line.strip_prefix("FAIL") {
            (false, rest)
        } else {
            if current_failed && !line.trim().is_empty() {
                r.lines.push(format!("    {}", line.trim()));
            }
            continue;
        };
        let rest = rest.trim();
        let (path, checked) = match rest.rfind(" (") {
            Some(at) if rest.ends_with(')') => rest.split_at(at),
            _ => (rest, ""),
        };
        let shown = format!("{}{checked}", shown_path(path, names));
        current_failed = !ok;
        if ok {
            r.ok(shown);
        } else {
            r.error(shown);
        }
    }
}

fn shown_path(reported: &str, names: &[String]) -> String {
    let norm = reported.trim().replace('\\', "/").to_lowercase();
    names
        .iter()
        .find(|n| {
            let n = n.to_lowercase();
            norm == n || norm.ends_with(&format!("/{n}"))
        })
        .cloned()
        .unwrap_or_else(|| reported.trim().replace('\\', "/"))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn scratch(name: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("swofty-check-{name}-{}", std::process::id()));
        std::fs::remove_dir_all(&d).ok();
        std::fs::create_dir_all(&d).unwrap();
        d
    }

    fn addon(dir: &Path, id: &str) {
        std::fs::create_dir_all(dir).unwrap();
        std::fs::write(
            dir.join(MANIFEST_FILE),
            format!(r#"{{"schema_version":1,"id":"{id}","name":"X","version":"1.0.0","author":"t","kind":["native"],"compat":{{"game":"f1_25"}}}}"#),
        )
        .unwrap();
    }

    #[test]
    fn validate_paths_show_as_registry_files_on_every_os() {
        let names = vec![
            "registry/layers.json".to_owned(),
            "registry/bad-layers.json".to_owned(),
        ];
        assert_eq!(
            shown_path(
                r"C:\Users\RUNNER~1\AppData\Local\Temp\x\mod.esp\registry\layers.json",
                &names
            ),
            "registry/layers.json"
        );
        assert_eq!(
            shown_path(r"c:\users\runneradmin\x\MOD.ESP\Registry\Bad-Layers.json", &names),
            "registry/bad-layers.json"
        );
        assert_eq!(
            shown_path("/tmp/x/mod.esp/registry/layers.json", &names),
            "registry/layers.json"
        );
        assert_eq!(shown_path("registry/layers.json", &names), "registry/layers.json");
        assert_eq!(shown_path(r"registry\other.json", &names), "registry/other.json");
        let mut r = Report::default();
        validate_report(
            "ok    C:\\Users\\RUNNER~1\\T\\mod.esp\\registry\\layers.json (1.0)\n\
             FAIL  C:\\Users\\RUNNER~1\\T\\mod.esp\\registry\\bad-layers.json (1.0)\n\
             \x20       1.0 /vehicle/game~1ferrari_2025/tyre_wear_scale: not a number\n",
            &names,
            &mut r,
        );
        assert_eq!(
            r.lines,
            [
                "ok: registry/layers.json (1.0)",
                "error: registry/bad-layers.json (1.0)",
                "    1.0 /vehicle/game~1ferrari_2025/tyre_wear_scale: not a number"
            ]
        );
        assert_eq!(r.errors, 1);
    }

    #[test]
    fn data_files_name_paths_that_must_be_in_the_addon() {
        let Ok(sdk) = super::super::sdk_root() else { return };
        let root = scratch("paths");
        let dir = root.join("mod.v12");
        addon(&dir, "mod.v12");
        std::fs::create_dir_all(dir.join("registry")).unwrap();
        std::fs::create_dir_all(dir.join("sounds")).unwrap();
        std::fs::write(dir.join("sounds/v12.bdl"), "bdl").unwrap();
        std::fs::write(
            dir.join("registry/sounds.json"),
            r#"{"engine_sound":{"v12":{"bundle":"sounds/v12.bdl"},"v8":{"bundle":"sounds/v8.bdl"},
                "stock":{"bundle":{"stock":"audio/x.bdl"}}}}"#,
        )
        .unwrap();
        let mut r = Report::default();
        missing_paths(&sdk, &dir, &data_files(&dir), &mut r);
        assert_eq!(
            r.lines,
            ["error: registry/sounds.json: engine_sound v8 names sounds/v8.bdl, which isn't in the addon"]
        );
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn a_broken_manifest_stops_the_check() {
        let root = scratch("manifest");
        std::fs::write(root.join(MANIFEST_FILE), "{").unwrap();
        let r = check(&Store::new(root.join("data")), &None, &root).unwrap();
        assert_eq!(r.errors, 1, "{:?}", r.lines);
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn registry_json_fields_and_references() {
        let root = scratch("refs");
        let store = Store::new(root.join("data"));
        let dir = root.join("mod.esp");
        addon(&dir, "mod.esp");
        std::fs::write(
            dir.join(REGISTRY_FILE),
            r#"{
              "requires": [ { "record": "vehicle:swofty.classic-seasons-2017/ferrari" },
                            { "registry": "career.season", "version": "^1" },
                            { "record": "vehicle:game/ferrari_2025" },
                            { "id": "swofty.classic-seasons-2017", "version": ">=0.6.0" },
                            { "record": "no-colon" },
                            { "registry": "career.season", "version": "not a range" } ],
              "recommends": [ { "record": "track:venue/sochi" } ],
              "breaks": [ { "addon": "mod.old", "version": "<1" }, { "version": "1" } ],
              "colour": "red"
            }"#,
        )
        .unwrap();
        let r = check(&store, &None, &dir).unwrap();
        let text = r.lines.join("\n");
        assert!(text.contains("doesn't know the field \"colour\""), "{text}");
        assert!(
            text.contains("warning: registry.json: requires[4]: record \"no-colon\""),
            "{text}"
        );
        assert!(text.contains("warning: registry.json: requires[5]: "), "{text}");
        assert!(
            text.contains("warning: registry.json: breaks[1]: no id"),
            "{text}"
        );
        assert!(
            !text.contains("requires[3]") && !text.contains("breaks[0]"),
            "{text}"
        );
        assert!(text.contains("recommends[0]: add a \"for\""), "{text}");
        assert!(text.contains(
            "error: registry.json: no installed addon provides vehicle:swofty.classic-seasons-2017/ferrari"
        ));
        assert!(text.contains("warning: registry.json: no installed addon provides track:venue/sochi"));
        assert!(!text.contains("game/ferrari_2025"));
        assert_eq!((r.errors, r.warnings), (1, 6), "{text}");

        let cs = store.addons_dir().join("swofty.classic-seasons-2017");
        addon(&cs, "swofty.classic-seasons-2017");
        std::fs::write(
            cs.join(LOCK_FILE),
            r#"{"generated_by":"x","provides":{"vehicle":[{"id":"swofty.classic-seasons-2017/ferrari","label":"Ferrari"}]},"needs":[]}"#,
        )
        .unwrap();
        let sochi = store.addons_dir().join("mod.sochi");
        addon(&sochi, "mod.sochi");
        std::fs::create_dir_all(sochi.join("registry")).unwrap();
        std::fs::write(
            sochi.join("registry/tracks.json"),
            r#"{"track":{"venue/sochi":{}}}"#,
        )
        .unwrap();
        let r = check(&store, &None, &dir).unwrap();
        let text = r.lines.join("\n");
        assert!(!text.contains("no installed addon provides vehicle"), "{text}");
        assert!(
            text.contains("track:venue/sochi"),
            "an id outside mod.sochi's own isn't provided by it: {text}"
        );
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn lock_needs_and_data_files() {
        let root = scratch("data");
        let store = Store::new(root.join("data"));
        let dir = root.join("mod.esp");
        addon(&dir, "mod.esp");
        std::fs::write(
            dir.join(LOCK_FILE),
            r#"{"generated_by":"x","provides":{"engine_sound":["mod.esp/v10"]},
                "needs":[{"record":"engine_sound:mod.esp/v10","policy":"required"},
                         {"record":"livery:mod.l/red","policy":"optional"},
                         {"record":"livery:mod.l/blue","or":["engine_sound:mod.esp/v10"],"policy":"required"}]}"#,
        )
        .unwrap();
        std::fs::create_dir_all(dir.join("registry")).unwrap();
        std::fs::write(
            dir.join("registry/layers.json"),
            r#"{"vehicle":{"swofty.classic-seasons-2017/ferrari":{"engine_sound":"mod.esp/v10","livery":null}}}"#,
        )
        .unwrap();
        std::fs::write(
            dir.join("registry/bad-layers.json"),
            r#"{"vehicle":{"game/ferrari_2025":{"tyre_wear_scale":"fast"}}}"#,
        )
        .unwrap();
        let sdk = super::super::sdk_root().ok();
        let r = check(&store, &sdk, &dir).unwrap();
        let text = r.lines.join("\n");
        assert!(
            text.contains("warning: registry.lock.json is there but no DLL"),
            "{text}"
        );
        assert!(
            text.contains("warning: registry.lock.json: no installed addon provides livery:mod.l/red"),
            "{text}"
        );
        assert!(!text.contains("engine_sound:mod.esp/v10"), "{text}");
        assert!(!text.contains("mod.l/blue"), "a met fallback is met: {text}");
        if super::super::python().is_ok() {
            assert!(text.contains("ok: registry/layers.json"), "{text}");
            assert!(text.contains("error: registry/bad-layers.json"), "{text}");
            assert_eq!(r.errors, 1, "{text}");
        }
        std::fs::remove_dir_all(&root).ok();
    }
}
