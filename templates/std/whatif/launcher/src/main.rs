// SPDX-License-Identifier: MIT
use std::io::{BufRead, Lines, StdinLock, StdoutLock, Write};
use std::path::PathBuf;

use serde_json::{Map, Value, json};

const API: u64 = 1;
const SECTION: &str = "history";
const PROGRESSIVE: &str = "swofty.progressive/";
const TARGET: &str = "swofty/addons/tails.whatif-history/profile/whatif.json";
const RATINGS: [&str; 4] = ["experience", "racecraft", "awareness", "pace"];

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
}

struct Page {
    cache_dir: PathBuf,
    vanilla: bool,
    season: String,
}

// This profile's edits, the same shape as profile/whatif.json:
// { "seats":   { "2017": { "1": ["game/fernando_alonso", "game/valtteri_bottas"] } },
//   "ratings": { "2017": { "game/fernando_alonso": { "pace": 96 } } } }
fn edits(host: &mut Host) -> Value {
    match host.call("get_state", Value::Null) {
        Ok(v) if v.is_object() => v,
        _ => json!({ "seats": {}, "ratings": {} }),
    }
}

// The values the core applied in the last session, or null before the game has run once
fn last_session(host: &mut Host) -> Value {
    host.call("registry_dir", Value::Null)
        .ok()
        .and_then(|d| d.as_str().map(|d| format!("{d}/effective.json")))
        .and_then(|p| std::fs::read_to_string(p).ok())
        .and_then(|t| serde_json::from_str(&t).ok())
        .unwrap_or(Value::Null)
}

fn seasons(last: &Value, mine: &Value) -> Vec<String> {
    let mut years: Vec<String> = last["career.season"]
        .as_object()
        .into_iter()
        .flatten()
        .filter_map(|(id, _)| id.strip_prefix(PROGRESSIVE).map(str::to_owned))
        .chain(
            mine["seats"]
                .as_object()
                .into_iter()
                .flatten()
                .map(|(y, _)| y.clone()),
        )
        .collect();
    if years.is_empty() {
        years = (2016..=2026).map(|y| y.to_string()).collect();
    }
    years.sort();
    years.dedup();
    years
}

fn object(v: &mut Value) -> &mut Map<String, Value> {
    if !v.is_object() {
        *v = json!({});
    }
    v.as_object_mut().expect("an object")
}

impl Page {
    fn view(&mut self, host: &mut Host) -> Value {
        let mine = edits(host);
        let last = last_session(host);
        let years = seasons(&last, &mine);
        if !years.contains(&self.season) {
            self.season = years.iter().find(|y| *y == "2017").unwrap_or(&years[0]).clone();
        }
        let year = self.season.as_str();
        let season = &last["career.season"][format!("{PROGRESSIVE}{year}")]["value"];
        let seat_edits = &mine["seats"][year];

        let grid: Vec<Value> = season["teams"]
            .as_array()
            .into_iter()
            .flatten()
            .map(|team| {
                let slot = team["slot"].to_string();
                let seats = seat_edits.get(&slot).unwrap_or(&team["seats"]);
                let edited = seat_edits.get(&slot).is_some();
                let seat = |i: usize| {
                    let mut cell = json!({ "text": seats[i].as_str().unwrap_or(""),
                                           "edit": { "text": { "placeholder": "game/<driver>" } } });
                    if edited {
                        cell["tone"] = json!("accent");
                    }
                    cell
                };
                json!({ "id": slot, "cells": [team["team"].as_str().unwrap_or("(no team)"), seat(0), seat(1)] })
            })
            .collect();

        let prefix = format!("{PROGRESSIVE}{year}/");
        let ratings: Vec<Value> = last["career.driver_ratings"]
            .as_object()
            .into_iter()
            .flatten()
            .filter_map(|(id, r)| Some((id.strip_prefix(&prefix)?, &r["value"])))
            .map(|(driver, r)| {
                let mut cells = vec![json!(driver)];
                for field in RATINGS {
                    let value = mine["ratings"][year][driver].get(field).unwrap_or(&r[field]);
                    cells.push(json!({ "text": value.to_string(),
                                       "edit": { "number": { "min": 0, "max": 100, "step": 1 } } }));
                }
                json!({ "id": driver, "cells": cells })
            })
            .collect();

        let tabs: Vec<Value> = years.iter().map(|y| json!({ "id": y, "label": y })).collect();
        let mut blocks = vec![json!({ "tabs": { "id": "season", "active": year, "items": tabs } })];
        if grid.is_empty() {
            blocks.push(json!({ "empty": { "title": format!("No {year} grid yet"),
                "text": "Start the game once with Progressive Career on, and its grids show here." } }));
        } else {
            blocks.push(json!({ "table": { "id": "grid", "rows": grid, "columns": [
                { "key": "team", "title": "Team" }, { "key": "seat1", "title": "Seat 1" }, { "key": "seat2", "title": "Seat 2" } ] } }));
        }
        if !ratings.is_empty() {
            blocks.push(json!({ "table": { "id": "ratings", "rows": ratings, "search": { "placeholder": "Search drivers" },
                "columns": [ { "key": "driver", "title": "Driver", "sort": true },
                             { "key": "experience", "title": "Experience" }, { "key": "racecraft", "title": "Racecraft" },
                             { "key": "awareness", "title": "Awareness" }, { "key": "pace", "title": "Pace", "sort": true } ] } }));
        }
        json!({
            "subtitle": if self.vanilla { "The vanilla profile can't keep edits" } else { "Your grids for Progressive Career" },
            "actions": [ { "id": "reset", "label": format!("Reset {year}"), "icon": "undo" } ],
            "body": { "blocks": blocks },
            "footer": "Edits apply in this profile from the next Play. A career keeps the grid of every season it has started."
        })
    }

    fn event(&mut self, host: &mut Host, event: &Value) -> Result<Value, String> {
        if let Some(year) = event["tab"]["tab"].as_str() {
            self.season = year.to_owned();
            return Ok(json!("reload"));
        }
        let mut mine = edits(host);
        let year = self.season.clone();
        if event["click"]["id"] == "reset" {
            object(&mut mine["seats"]).remove(&year);
            object(&mut mine["ratings"]).remove(&year);
        } else if let Some(submit) = event.get("submit") {
            let (Some(form), Some(row)) = (submit["form"].as_str(), submit["row"].as_str()) else {
                return Ok(json!("none"));
            };
            let values = &submit["values"];
            if form == "grid" {
                let slot: i64 = row.parse().map_err(|_| format!("{row} isn't a slot"))?;
                let last = last_session(host);
                let team = last["career.season"][format!("{PROGRESSIVE}{year}")]["value"]["teams"]
                    .as_array()
                    .into_iter()
                    .flatten()
                    .find(|t| t["slot"] == slot)
                    .cloned()
                    .unwrap_or(Value::Null);
                let current = mine["seats"][&year].get(row).unwrap_or(&team["seats"]).clone();
                let pick = |key: &str, i: usize| {
                    values
                        .get(key)
                        .unwrap_or(&current[i])
                        .as_str()
                        .unwrap_or("")
                        .trim()
                        .to_owned()
                };
                let seats = [pick("seat1", 0), pick("seat2", 1)];
                if seats.iter().any(|s| !s.is_empty() && !s.contains('/')) {
                    return Err("a driver is an id such as game/fernando_alonso".into());
                }
                object(
                    object(&mut mine["seats"])
                        .entry(year.clone())
                        .or_insert(json!({})),
                )
                .insert(row.to_owned(), json!(seats));
            } else if form == "ratings" {
                let fields = object(
                    object(&mut mine["ratings"])
                        .entry(year.clone())
                        .or_insert(json!({})),
                )
                .entry(row.to_owned())
                .or_insert(json!({}));
                for field in RATINGS {
                    if let Some(n) = values.get(field).and_then(Value::as_f64) {
                        if !(0.0..=100.0).contains(&n) {
                            return Err(format!("{field} goes from 0 to 100"));
                        }
                        object(fields).insert(field.to_owned(), json!(n));
                    }
                }
            } else {
                return Ok(json!("none"));
            }
        } else {
            return Ok(json!("none"));
        }
        host.call("set_state", mine)?;
        Ok(json!("reload"))
    }

    // The build step: put this profile's edits where the DLL reads them, an empty file when it has none
    fn build(&self, host: &mut Host, params: &Value) -> Result<Value, String> {
        let file_ops = params["features"]["stable"]
            .as_array()
            .is_some_and(|f| f.iter().any(|k| k == "file"));
        if !file_ops {
            return Ok(
                json!({ "warnings": ["this loader can't put files; the what-if edits are left out"] }),
            );
        }
        let mine = edits(host);
        std::fs::create_dir_all(&self.cache_dir).map_err(|e| e.to_string())?;
        std::fs::write(self.cache_dir.join("whatif.json"), mine.to_string()).map_err(|e| e.to_string())?;
        let seasons = mine["seats"].as_object().map_or(0, Map::len);
        Ok(json!({
            "ops": [{ "op": "file.put", "source": { "root": "cache", "path": "whatif.json" }, "target": TARGET }],
            "lines": [format!("{seasons} season{} edited", if seasons == 1 { "" } else { "s" })]
        }))
    }
}

fn main() {
    let mut host = Host {
        lines: std::io::stdin().lock().lines(),
        out: std::io::stdout().lock(),
        next: 0,
    };
    let mut page = Page {
        cache_dir: PathBuf::new(),
        vanilla: false,
        season: "2017".into(),
    };
    while let Some(Ok(line)) = host.lines.next() {
        let Ok(message) = serde_json::from_str::<Value>(&line) else {
            continue;
        };
        let params = &message["params"];
        let method = message["method"].as_str().unwrap_or_default();
        let Some(id) = message["id"].as_u64() else {
            if method == "profile" {
                page.vanilla = params["vanilla"].as_bool().unwrap_or(false);
            }
            continue;
        };
        let result = match method {
            "hello" => {
                page.cache_dir = PathBuf::from(params["cache_dir"].as_str().unwrap_or_default());
                page.vanilla = params["profile"]["vanilla"].as_bool().unwrap_or(false);
                Ok(json!({ "api": API }))
            }
            "view" if params["section"] == SECTION => Ok(page.view(&mut host)),
            "event" if params["section"] == SECTION => page.event(&mut host, &params["event"]),
            "build" => page.build(&mut host, params),
            other => Err(format!("unknown method {other}")),
        };
        let reply = match result {
            Ok(result) => json!({ "id": id, "result": result }),
            Err(error) => json!({ "id": id, "error": error }),
        };
        host.send(&reply);
    }
}
