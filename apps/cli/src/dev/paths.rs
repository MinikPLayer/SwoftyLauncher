use std::path::Path;

use serde_json::Value;

const PATH_DEF: &str = "#/$defs/path";

pub fn inside(folder: &Path, rel: &str) -> bool {
    let norm = rel.replace('\\', "/");
    if norm.is_empty() || norm.starts_with('/') || Path::new(rel).is_absolute() {
        return false;
    }
    let parts: Vec<&str> = norm.split('/').collect();
    if parts.contains(&"..") || parts[0].contains(':') {
        return false;
    }
    folder.join(&norm).exists()
}

pub fn file_strings(folder: &Path, value: &Value, out: &mut Vec<String>) {
    match value {
        Value::String(s) => {
            let norm = s.replace('\\', "/");
            if inside(folder, s) && folder.join(&norm).is_file() && !out.contains(&norm) {
                out.push(norm);
            }
        }
        Value::Array(a) => a.iter().for_each(|v| file_strings(folder, v, out)),
        Value::Object(o) => o.values().for_each(|v| file_strings(folder, v, out)),
        _ => {}
    }
}

pub fn schema_paths(root: &Value, node: &Value, value: &Value, out: &mut Vec<String>, depth: usize) {
    if depth > 64 || node.is_null() {
        return;
    }
    let mut node = node;
    for _ in 0..16 {
        let Some(r) = node["$ref"].as_str() else { break };
        if r == PATH_DEF {
            if let Some(s) = value.as_str()
                && !out.iter().any(|o| o == s)
            {
                out.push(s.to_owned());
            }
            return;
        }
        match r.strip_prefix('#').and_then(|p| root.pointer(p)) {
            Some(n) => node = n,
            None => return,
        }
    }
    for key in ["oneOf", "anyOf", "allOf"] {
        for alt in node[key].as_array().into_iter().flatten() {
            schema_paths(root, alt, value, out, depth + 1);
        }
    }
    match value {
        Value::Object(obj) => {
            for (k, v) in obj {
                let sub = match &node["properties"][k] {
                    Value::Null => &node["additionalProperties"],
                    p => p,
                };
                if sub.is_object() {
                    schema_paths(root, sub, v, out, depth + 1);
                }
            }
        }
        Value::Array(items) => {
            for (i, v) in items.iter().enumerate() {
                let sub = match &node["prefixItems"][i] {
                    Value::Null => &node["items"],
                    p => p,
                };
                if sub.is_object() {
                    schema_paths(root, sub, v, out, depth + 1);
                }
            }
        }
        _ => {}
    }
}

pub fn recorded_values(rec: &Value) -> Vec<(&str, &Value)> {
    let mut out = Vec::new();
    for r in rec["records"].as_array().into_iter().flatten() {
        if let Some(reg) = r["registry"].as_str() {
            out.push((reg, &r["value"]));
        }
    }
    for l in rec["layers"].as_array().into_iter().flatten() {
        if let Some(reg) = l["registry"].as_str() {
            out.push((reg, &l["result"]));
            out.push((reg, &l["patch"]));
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn finds_paths_by_schema_and_by_file() {
        let dir = std::env::temp_dir().join(format!("swofty-paths-{}", std::process::id()));
        std::fs::create_dir_all(dir.join("sounds")).unwrap();
        std::fs::write(dir.join("sounds/v10.bdl"), "x").unwrap();
        assert!(inside(&dir, "sounds/v10.bdl"));
        assert!(inside(&dir, "sounds\\v10.bdl"));
        assert!(inside(&dir, "sounds"));
        assert!(!inside(&dir, "../x"));
        assert!(!inside(&dir, "/etc/passwd"));
        assert!(!inside(&dir, "C:/x"));
        assert!(!inside(&dir, "sounds/missing.bdl"));
        let mut found = Vec::new();
        file_strings(
            &dir,
            &json!({ "bundle": "sounds/v10.bdl", "x": ["sounds", "nope.bdl", "sounds\\v10.bdl"] }),
            &mut found,
        );
        assert_eq!(found, ["sounds/v10.bdl"]);
        let schema = json!({
            "properties": { "bundle": { "$ref": "#/$defs/path" },
                            "parts": { "type": "array", "items": { "properties": { "model": { "$ref": "#/$defs/path" } } } },
                            "name": { "type": "string" } },
            "$defs": { "path": { "oneOf": [ { "type": "null" }, { "type": "string" } ] } }
        });
        let mut paths = Vec::new();
        schema_paths(
            &schema,
            &schema,
            &json!({ "bundle": "sounds/v10.bdl", "name": "sounds/v10.bdl", "parts": [ { "model": "cars/a.erp" }, { "model": null } ] }),
            &mut paths,
            0,
        );
        assert_eq!(paths, ["sounds/v10.bdl", "cars/a.erp"]);
        std::fs::remove_dir_all(&dir).ok();
    }
}
