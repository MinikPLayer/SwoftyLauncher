use std::io::{BufRead, Lines, StdinLock, StdoutLock, Write};
use std::path::{Path, PathBuf};
use std::time::SystemTime;

use serde_json::{Value, json};

const API: u64 = 1;
const EXTENSION: &str = "exmod";
const COLOURS: [u64; 3] = [1, 5, 25];

struct Host {
    lines: Lines<StdinLock<'static>>,
    out: StdoutLock<'static>,
    next: u64,
}

impl Host {
    fn send(&mut self, message: &Value) {
        writeln!(self.out, "{message}")
            .and_then(|()| self.out.flush())
            .ok();
    }

    fn call(&mut self, method: &str, params: Value) -> Result<Value, String> {
        let id = self.next;
        self.next += 1;
        self.send(&json!({ "id": id, "method": method, "params": params }));
        let line = match self.lines.next() {
            Some(Ok(line)) => line,
            _ => std::process::exit(0),
        };
        let reply: Value = serde_json::from_str(&line).map_err(|e| e.to_string())?;
        match reply.get("error") {
            Some(e) => Err(e.to_string()),
            None => Ok(reply["result"].clone()),
        }
    }

    fn progress(&mut self, done: f32, text: &str) {
        self.send(&json!({ "method": "progress", "params": { "done": done, "text": text } }));
    }

    fn invalidate(&mut self, section: &str) {
        self.send(&json!({ "method": "invalidate", "params": { "section": section } }));
    }

    fn path(&mut self, method: &str) -> Option<String> {
        self.call(method, Value::Null).ok()?.as_str().map(str::to_owned)
    }
}

struct Addon {
    data_dir: PathBuf,
    cache_dir: PathBuf,
    vanilla: bool,
    seen: Option<SystemTime>,
    running: bool,
    tab: String,
    colours: bool,
}

fn default_dashboard() -> Value {
    let driver = |id: &str, name: &str, team: &str, rating: u32, form: &str, colour: &str| json!({ "id": id, "name": name, "team": team, "rating": rating, "form": form, "colour": colour });
    json!({
        "budget": 120, "speed": 40, "name": "Example Racing", "mode": "dry", "lock": false, "colour": "#8B7FD6",
        "drivers": [
            driver("ada", "Ada Lovelace", "Analytical", 91, "good", "#E10600"),
            driver("alan", "Alan Turing", "Bletchley", 87, "good", "#1868DB"),
            driver("grace", "Grace Hopper", "Cobol", 84, "warn", "#00A19C"),
            driver("linus", "Linus Torvalds", "Kernel", 79, "muted", "#FF8000"),
            driver("margaret", "Margaret Hamilton", "Apollo", 93, "good", "#52E252"),
            driver("dennis", "Dennis Ritchie", "Bell Labs", 76, "bad", "#B6BABD")
        ]
    })
}

fn at_least(version: &str, wanted: [u64; 3]) -> bool {
    let core = version.split(['-', '+']).next().unwrap_or_default();
    let parts: Vec<u64> = core.split('.').map(|p| p.parse().unwrap_or(0)).collect();
    parts.len() == 3 && parts[..] >= wanted[..]
}

impl Addon {
    fn items_dir(&self) -> PathBuf {
        self.data_dir.join("items")
    }

    fn dashboard_file(&self) -> PathBuf {
        self.data_dir.join("dashboard.json")
    }

    fn dashboard(&self) -> Value {
        std::fs::read(self.dashboard_file())
            .ok()
            .and_then(|d| serde_json::from_slice(&d).ok())
            .unwrap_or_else(default_dashboard)
    }

    fn save_dashboard(&self, d: &Value) -> Result<(), String> {
        std::fs::create_dir_all(&self.data_dir).map_err(|e| e.to_string())?;
        std::fs::write(self.dashboard_file(), d.to_string()).map_err(|e| e.to_string())
    }

    fn modified(&self) -> Option<SystemTime> {
        std::fs::metadata(self.dashboard_file())
            .and_then(|m| m.modified())
            .ok()
    }

    fn overview(&mut self, host: &mut Host) -> Result<Value, String> {
        let d = self.dashboard();
        self.seen = self.modified();
        let status = host.call("game_status", Value::Null).unwrap_or(Value::Null);
        self.running = status["running"] == true;
        let addon_dir = host.path("addon_game_dir");
        let saves = host.path("steam_save_dir");
        let items = self.items();
        let on = self.enabled(host)?;
        let live = match (self.running, status["session"].as_str()) {
            (true, Some(_)) => json!({ "text": "LIVE", "tone": "good", "dot": true }),
            (true, None) => json!({ "text": "Running without Swofty", "tone": "warn", "dot": true }),
            (false, _) => json!({ "text": "Offline", "tone": "muted", "dot": true }),
        };
        let speed = d["speed"].as_f64().unwrap_or(0.0);
        let drivers = d["drivers"].as_array().cloned().unwrap_or_default();
        let average =
            drivers.iter().filter_map(|r| r["rating"].as_f64()).sum::<f64>() / drivers.len().max(1) as f64;
        let rows: Vec<Value> = drivers
            .iter()
            .map(|r| {
                let rating = r["rating"].as_f64().unwrap_or(0.0);
                let name = r["name"].as_str().unwrap_or_default();
                let (first, last) = name.rsplit_once(' ').unwrap_or(("", name));
                let active = r["active"] != false;
                let mut row = json!({ "id": r["id"], "subtitle": r["team"], "cells": [
                    { "text": if active { "On" } else { "Off" }, "edit": { "toggle": { "value": active } } },
                    // The surname in bold; `text` stays the whole name for search and sort.
                    { "text": name, "spans": [ { "text": format!("{first} ") }, { "text": last, "bold": true } ] },
                    { "text": r["team"], "tone": "muted" },
                    { "text": format!("{rating}"), "bar": rating / 100.0,
                      "edit": { "number": { "min": 0, "max": 100, "step": 1 } } },
                    { "text": match r["form"].as_str() { Some("good") => "Strong", Some("warn") => "Patchy", Some("bad") => "Poor", _ => "Steady" },
                      "tone": r["form"], "pill": true }
                  ],
                  // Clicking the row opens this beside the table.
                  "title": name,
                  "detail": [
                      { "stats": [ { "label": "Rating", "value": format!("{rating}") }, { "label": "Team", "value": r["team"] } ] },
                      { "facts": [ { "label": "Driver id", "value": r["id"] }, { "label": "Active", "value": if active { "Yes" } else { "No" } } ] }
                  ] });
                if self.colours {
                    let colour = r["colour"].as_str().unwrap_or("#737988");
                    row["cells"].as_array_mut().into_iter().for_each(|c| c.push(json!({ "text": colour, "edit": { "colour": {} } })));
                }
                row
            })
            .collect();
        let mut columns = vec![
            json!({ "key": "active", "title": "Active", "width": 72 }),
            json!({ "key": "name", "title": "Driver", "sort": true }),
            json!({ "key": "team", "title": "Team", "sort": true }),
            json!({ "key": "rating", "title": "Rating", "sort": true, "width": 150 }),
            json!({ "key": "form", "title": "Form", "width": 96 }),
        ];
        let mut fields = vec![
            json!({ "number": { "id": "budget", "label": "Budget", "value": d["budget"], "min": 0, "max": 1000, "step": 10, "unit": "k" } }),
            json!({ "text": { "id": "name", "label": "Team name", "value": d["name"], "max_len": 32 } }),
            json!({ "slider": { "id": "speed", "label": "Speed", "value": d["speed"], "min": 0, "max": 100, "step": 5 } }),
            json!({ "select": { "id": "mode", "label": "Weather", "value": d["mode"], "options": [
                { "value": "dry", "label": "Dry" }, { "value": "light", "label": "Light rain" }, { "value": "heavy", "label": "Heavy rain" } ] } }),
            json!({ "toggle": { "id": "lock", "label": "Lock budget", "value": d["lock"], "help": "Stops the budget from changing." } }),
        ];
        // Colour fields and cells need launcher 1.5.25; an older one fails the view.
        if self.colours {
            columns.push(json!({ "key": "colour", "title": "Colour", "width": 110 }));
            fields
                .push(json!({ "colour": { "id": "colour", "label": "Team colour", "value": d["colour"] } }));
        }
        let path_or = |p: &Option<String>| p.clone().unwrap_or_else(|| "Not found".into());
        let mut blocks = vec![
            json!({ "hero": {
                "title": d["name"],
                "subtitle": format!("Profile {} · {} items, {} on", if self.vanilla { "vanilla" } else { "modded" }, items.len(), on.len()),
                "art": { "monogram": d["name"].as_str().unwrap_or("E") },
                "pills": [ live, { "text": if d["lock"] == true { "Budget locked" } else { "Budget open" }, "tone": "accent" } ],
                "facts": [
                    { "label": "Addon folder", "value": path_or(&addon_dir) },
                    { "label": "Saves", "value": path_or(&saves) }
                ],
                "buttons": [ { "id": "reset", "label": "Reset", "icon": "undo", "style": "ghost",
                    "kind": { "confirm": { "title": "Reset the dashboard?", "text": "The settings and ratings go back to the defaults." } } } ]
            } }),
            json!({ "tabs": { "id": "overview", "active": self.tab, "items": [
                { "id": "status", "label": "Status", "icon": "gauge" },
                { "id": "drivers", "label": "Drivers", "icon": "users", "badge": drivers.len().to_string() }
            ] } }),
        ];
        if self.tab == "drivers" {
            blocks.push(
                json!({ "card": { "title": "Drivers", "subtitle": "Click a rating to edit it, or a driver for details", "blocks": [
                { "table": {
                    "id": "drivers",
                    "columns": columns,
                    "rows": rows,
                    "sort": { "column": "rating", "desc": true },
                    "search": { "placeholder": "Search drivers" },
                    "compact": true
                } }
            ] } }),
            );
        } else {
            blocks.push(json!({ "tiles": [
                { "label": "Budget", "value": format!("${}k", d["budget"]), "delta": "+$12k", "hint": "since the last race", "icon": "wallet" },
                { "label": "Speed", "value": format!("{speed}"), "hint": "of 100", "progress": speed / 100.0, "tone": "accent", "icon": "gauge" },
                { "label": "Average rating", "value": format!("{average:.1}"), "delta": "-0.4", "hint": "6 drivers", "icon": "users" },
                { "label": "Items on", "value": format!("{} / {}", on.len(), items.len()), "icon": "package" }
            ] }));
            blocks.push(json!({ "grid": { "columns": [3, 2], "blocks": [
                { "card": { "title": "Settings", "subtitle": "Applied together with Apply", "blocks": [
                    { "form": { "id": "settings", "columns": 2, "fields": fields } }
                ] } },
                { "card": { "title": "Facilities", "pills": [ { "text": "3 upgrading", "tone": "warn" } ], "blocks": [
                    { "meters": [
                        { "label": "Wind tunnel", "value": 3, "max": 4, "text": "Phase 3 of 4", "tone": "good" },
                        { "label": "Simulator", "value": 2, "max": 4, "text": "Phase 2 of 4" },
                        { "label": "Factory", "value": 1, "max": 4, "text": "Phase 1 of 4", "tone": "warn" },
                        { "label": "Resource points", "value": 340, "max": 500, "text": "340 / 500 RP" }
                    ] }
                ] } }
            ] } }));
        }
        Ok(json!({
            "subtitle": "Every launcher block, live from dashboard.json",
            "tick": true,
            "actions": [ { "id": "open", "label": "Open data folder", "icon": "folder-open" } ],
            "body": { "blocks": blocks },
            "footer": "Edit dashboard.json in the data folder and this page follows it."
        }))
    }

    fn overview_event(&mut self, event: &Value) -> Result<Value, String> {
        if let Some(tab) = event["tab"]["tab"].as_str() {
            self.tab = tab.to_owned();
            return Ok(json!("reload"));
        }
        let mut d = self.dashboard();
        if let Some(submit) = event.get("submit") {
            let values = &submit["values"];
            match (submit["form"].as_str(), submit["row"].as_str()) {
                (Some("settings"), _) => {
                    for key in ["budget", "speed", "name", "mode", "lock", "colour"] {
                        if let Some(v) = values.get(key) {
                            d[key] = v.clone();
                        }
                    }
                    self.save_dashboard(&d)?;
                    return Ok(json!({ "notice": format!("Saved {}", submit["changed"]) }));
                }
                (Some("drivers"), Some(row)) => {
                    // A rating cell sends a number, the Active switch true or false, a colour cell "#RRGGBB".
                    let (key, value) = match (values["active"].as_bool(), values["colour"].as_str()) {
                        (Some(on), _) => ("active", json!(on)),
                        (None, Some(colour)) => ("colour", json!(colour)),
                        (None, None) => (
                            "rating",
                            json!(
                                values["rating"]
                                    .as_u64()
                                    .ok_or("the rating is not a whole number")?
                            ),
                        ),
                    };
                    if let Some(r) = d["drivers"]
                        .as_array_mut()
                        .into_iter()
                        .flatten()
                        .find(|r| r["id"] == row)
                    {
                        r[key] = value;
                    }
                    self.save_dashboard(&d)?;
                    return Ok(json!("reload"));
                }
                _ => {}
            }
        }
        match event["click"]["id"].as_str() {
            Some("reset") => {
                self.save_dashboard(&default_dashboard())?;
                Ok(json!("reload"))
            }
            Some("open") => {
                std::fs::create_dir_all(&self.data_dir).map_err(|e| e.to_string())?;
                Ok(json!({ "open": self.data_dir }))
            }
            _ => Ok(json!("none")),
        }
    }

    fn tick(&mut self, host: &mut Host, section: &str) -> Result<Value, String> {
        if section == "overview" {
            let running = host.call("game_status", Value::Null)?["running"] == true;
            if self.modified() != self.seen || running != self.running {
                host.invalidate(section);
            }
        }
        Ok(Value::Null)
    }

    fn items(&self) -> Vec<String> {
        let mut names: Vec<String> = std::fs::read_dir(self.items_dir())
            .into_iter()
            .flatten()
            .filter_map(|e| e.ok()?.file_name().into_string().ok())
            .collect();
        names.sort();
        names
    }

    fn enabled(&self, host: &mut Host) -> Result<Vec<String>, String> {
        let state = host.call("get_state", Value::Null)?;
        Ok(serde_json::from_value(state["on"].clone()).unwrap_or_default())
    }

    fn view(&self, host: &mut Host) -> Result<Value, String> {
        let pick = json!({
            "id": "pick",
            "label": "Import…",
            "icon": "upload",
            "kind": { "pick": { "title": "Import example items", "extensions": [EXTENSION], "folders": false, "multiple": true } }
        });
        let items = self.items();
        if items.is_empty() {
            return Ok(json!({
                "subtitle": "No items",
                "body": { "empty": {
                    "title": "No example items",
                    "text": format!("Drop a .{EXTENSION} file on the launcher, or import one."),
                    "action": pick
                } }
            }));
        }
        let on = self.enabled(host)?;
        let rows: Vec<Value> = items
            .iter()
            .map(|name| {
                let size = std::fs::metadata(self.items_dir().join(name)).map_or(0, |m| m.len());
                json!({
                    "id": name,
                    "title": name,
                    "subtitle": format!("{size} bytes"),
                    "art": { "monogram": name.chars().next().unwrap_or('?').to_uppercase().to_string() },
                    "trailing": { "switch": { "id": name, "on": on.contains(name), "enabled": !self.vanilla } }
                })
            })
            .collect();
        Ok(json!({
            "subtitle": format!("{} items, {} on", items.len(), on.len()),
            "actions": [pick],
            "body": { "list": { "rows": rows, "reorder": false, "search": { "placeholder": "Search items" } } }
        }))
    }

    fn event(&self, host: &mut Host, event: &Value) -> Result<Value, String> {
        if let Some(toggle) = event.get("toggle") {
            let name = toggle["id"].as_str().unwrap_or_default().to_owned();
            let mut on = self.enabled(host)?;
            on.retain(|n| *n != name);
            if toggle["on"].as_bool() == Some(true) {
                on.push(name);
            }
            host.call("set_state", json!({ "on": on }))?;
            return Ok(json!("reload"));
        }
        if let Some(picked) = event.get("picked") {
            let paths: Vec<PathBuf> = serde_json::from_value(picked["paths"].clone()).unwrap_or_default();
            let lines = self.import(host, &paths)?;
            return Ok(json!({ "notice": lines.join("\n") }));
        }
        Ok(json!("none"))
    }

    fn import(&self, host: &mut Host, paths: &[PathBuf]) -> Result<Vec<String>, String> {
        std::fs::create_dir_all(self.items_dir()).map_err(|e| e.to_string())?;
        let mut lines = Vec::new();
        for (i, path) in paths.iter().enumerate() {
            if !is_item(path) {
                lines.push(format!("Skipped {}: not a .{EXTENSION} file", path.display()));
                continue;
            }
            let name = path
                .file_name()
                .and_then(|n| n.to_str())
                .ok_or_else(|| format!("{} has no name", path.display()))?;
            host.progress(i as f32 / paths.len() as f32, &format!("Importing {name}"));
            std::fs::copy(path, self.items_dir().join(name)).map_err(|e| format!("{name}: {e}"))?;
            lines.push(format!("Imported {name}"));
        }
        Ok(lines)
    }

    fn build(&self, host: &mut Host) -> Result<Value, String> {
        let on = self.enabled(host)?;
        let text = on.join("\n");
        let file = self.cache_dir.join("enabled.txt");
        if std::fs::read_to_string(&file).ok().as_deref() != Some(text.as_str()) {
            std::fs::write(&file, &text).map_err(|e| e.to_string())?;
        }
        Ok(json!({
            "ops": [{
                "op": "file.put",
                "source": { "root": "cache", "path": "enabled.txt" },
                "target": "example-launcher-addon/enabled.txt"
            }],
            "lines": [format!("{} items on", on.len())]
        }))
    }

    fn command(&self, host: &mut Host) -> Result<Value, String> {
        let on = self.enabled(host)?;
        let lines: Vec<String> = self
            .items()
            .into_iter()
            .map(|n| format!("{} {n}", if on.contains(&n) { "on " } else { "off" }))
            .collect();
        Ok(json!(lines.join("\n")))
    }

    fn summary(&self, host: &mut Host) -> Result<Value, String> {
        let on = self.enabled(host)?;
        if on.is_empty() {
            return Ok(Value::Null);
        }
        Ok(json!({ "label": "EXAMPLE", "items": on }))
    }
}

fn path(value: &Value) -> PathBuf {
    PathBuf::from(value.as_str().unwrap_or_default())
}

fn main() {
    let stdin = std::io::stdin();
    let mut host = Host {
        lines: stdin.lock().lines(),
        out: std::io::stdout().lock(),
        next: 0,
    };
    let mut addon = Addon {
        data_dir: PathBuf::new(),
        cache_dir: PathBuf::new(),
        vanilla: false,
        seen: None,
        running: false,
        tab: "status".into(),
        colours: false,
    };
    while let Some(Ok(line)) = host.lines.next() {
        let Ok(message) = serde_json::from_str::<Value>(&line) else {
            eprintln!("not JSON: {line}");
            continue;
        };
        let params = &message["params"];
        let method = message["method"].as_str().unwrap_or_default();
        let Some(id) = message["id"].as_u64() else {
            if method == "profile" {
                addon.vanilla = params["vanilla"].as_bool().unwrap_or(false);
            }
            continue;
        };
        let result = match method {
            "hello" => {
                addon.data_dir = path(&params["data_dir"]);
                addon.cache_dir = path(&params["cache_dir"]);
                addon.vanilla = params["profile"]["vanilla"].as_bool().unwrap_or(false);
                addon.colours = at_least(params["launcher"].as_str().unwrap_or_default(), COLOURS);
                Ok(json!({ "api": API }))
            }
            "view" if params["section"] == "overview" => addon.overview(&mut host),
            "view" => addon.view(&mut host),
            "event" if params["section"] == "overview" => addon.overview_event(&params["event"]),
            "event" => addon.event(&mut host, &params["event"]),
            "tick" => addon.tick(&mut host, params["section"].as_str().unwrap_or_default()),
            "import" => {
                let paths: Vec<PathBuf> = serde_json::from_value(params["paths"].clone()).unwrap_or_default();
                addon.import(&mut host, &paths).map(|lines| json!(lines))
            }
            "build" => addon.build(&mut host),
            "command" => addon.command(&mut host),
            "summary" => addon.summary(&mut host),
            other => Err(format!("unknown method {other}")),
        };
        let reply = match result {
            Ok(result) => json!({ "id": id, "result": result }),
            Err(error) => json!({ "id": id, "error": error }),
        };
        host.send(&reply);
    }
}

fn is_item(path: &Path) -> bool {
    path.extension()
        .is_some_and(|e| e.eq_ignore_ascii_case(EXTENSION))
}
