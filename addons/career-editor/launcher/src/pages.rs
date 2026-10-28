use std::path::PathBuf;

use serde_json::{Value, json};

use crate::App;
use crate::host::Host;
use crate::live::{Bridge, GameBuild, Live};
use crate::store::{Entry, now_ms};
use crate::ui::*;
use crate::undo::{Undo, inverse};

const STATS: [&str; 5] = ["experience", "racecraft", "awareness", "pace", "focus"];
const DEPTS: [&str; 5] = ["Aero", "Chassis", "Durability", "Powertrain", "Tyres"];
const W_SESSIONS: [(&str, &str); 8] = [
    ("p1", "Practice 1"),
    ("p2", "Practice 2"),
    ("p3", "Practice 3"),
    ("q", "Qualifying"),
    ("ss", "Sprint Shootout"),
    ("r", "Race"),
    ("r2", "Race 2"),
    ("r3", "Race 3"),
];
const W_STATES: [(&str, &str); 6] = [
    ("clear", "Clear"),
    ("light_cloud", "Light cloud"),
    ("overcast", "Overcast"),
    ("light_rain", "Light rain"),
    ("heavy_rain", "Heavy rain"),
    ("storm", "Storm"),
];

fn mode_label(mode: &str) -> &'static str {
    match mode {
        "my_team" => "My Team",
        "driver_career" => "Driver Career",
        _ => "No career",
    }
}

fn phase_label(phase: &str) -> &'static str {
    match phase {
        "team_hq" => "Team HQ",
        "team_hq_busy" => "Team HQ (busy)",
        "weekend_hub" => "Weekend hub",
        "session" => "In a session",
        "loading" => "Loading",
        "frontend" => "Menus",
        _ => "Unknown",
    }
}

fn opt(v: &Value) -> Option<String> {
    match v {
        Value::Null => None,
        Value::String(s) => Some(s.clone()),
        Value::Number(n) => Some(n.to_string()),
        other => Some(other.to_string()),
    }
}

// ---- driver names -------------------------------------------------------------------------------

fn driver_names(live: &Live) -> Vec<(i64, String, String, bool)> {
    live.section("drivers")
        .and_then(|d| d["drivers"].as_array())
        .map(|a| {
            a.iter()
                .filter_map(|d| {
                    Some((
                        d["driverStatId"].as_i64()?,
                        title_case(d["name"].as_str().unwrap_or("driver")),
                        d["initials"].as_str().unwrap_or_default().to_owned(),
                        d["player"].as_bool().unwrap_or(false),
                    ))
                })
                .collect()
        })
        .unwrap_or_default()
}

fn driver_name(names: &[(i64, String, String, bool)], stat_id: i64) -> String {
    names
        .iter()
        .find(|n| n.0 == stat_id)
        .map_or_else(|| format!("Driver #{stat_id}"), |n| n.1.clone())
}

fn pretty_team(raw: &str) -> String {
    match raw {
        "redbull" | "red_bull" => "Red Bull Racing".into(),
        "astonmartin" | "aston_martin" | "forceindia" => "Aston Martin".into(),
        "racingbulls" | "racing_bulls" | "tororosso" => "Racing Bulls".into(),
        "renault" => "Alpine".into(),
        "mclaren" => "McLaren".into(),
        "user_defined" => "Your team".into(),
        other => title_case(other),
    }
}

fn team_label(live: &Live, id: i64) -> String {
    live.section("drivers")
        .and_then(|d| d["drivers"].as_array())
        .and_then(|a| a.iter().find(|d| d["team"]["teamStatId"].as_i64() == Some(id)))
        .and_then(|d| d["team"]["name"].as_str())
        .map_or_else(|| team_name(id), pretty_team)
}

fn team_name(id: i64) -> String {
    const TEAMS: [&str; 10] = [
        "Mercedes",
        "Ferrari",
        "Red Bull Racing",
        "Williams",
        "Aston Martin",
        "Alpine",
        "Racing Bulls",
        "Haas",
        "McLaren",
        "Sauber",
    ];
    usize::try_from(id)
        .ok()
        .and_then(|i| TEAMS.get(i))
        .map_or_else(|| format!("Team #{id}"), |n| (*n).to_owned())
}

// ---- page frame ---------------------------------------------------------------------------------

fn common_notices(app: &App, live: &Live, anytime_page: bool) -> Vec<Value> {
    let mut n = Vec::new();
    if !app.enabled {
        n.push(notice("Career Mode Editor is off in this profile, so the game plugin won't load. Turn it on and restart the game.", "warn"));
    }
    if !live.mailbox_known {
        n.push(notice(
            "The launcher hasn't found F1 25 yet. Locate the game on the Library page.",
            "warn",
        ));
        return n;
    }
    let pending = app.store.pending();
    if !pending.is_empty() {
        let blocked = if live.live() {
            live.snap["bridge"]["blocked"]["why"]
                .as_str()
                .map(|w| format!(" ({w})"))
                .unwrap_or_default()
        } else {
            " (they apply once the game is running and ready)".to_owned()
        };
        n.push(json!({
            "text": format!("{} edit{} queued{blocked}.", pending.len(), if pending.len() == 1 { "" } else { "s" }),
            "tone": "accent",
            "action": { "id": "withdraw", "label": "Withdraw" }
        }));
    }
    if let Some(e) = app
        .store
        .history
        .last()
        .filter(|e| !e.ok && now_ms().saturating_sub(e.t) < 120_000)
    {
        n.push(notice(
            &format!(
                "{} failed: {}",
                if e.label.is_empty() {
                    op_label(&e.op)
                } else {
                    e.label.clone()
                },
                e.error.clone().unwrap_or_default()
            ),
            "bad",
        ));
    } else if let Some(e) = app
        .store
        .history
        .last()
        .filter(|e| e.ok && now_ms().saturating_sub(e.t) < 120_000)
        && let Some(w) = not_applied(e)
    {
        n.push(notice(
            &format!(
                "{} was stored but not applied yet: {w}",
                if e.label.is_empty() {
                    op_label(&e.op)
                } else {
                    e.label.clone()
                }
            ),
            "warn",
        ));
    }
    if let Some(err) = &live.snap_error {
        n.push(notice(
            &format!("Editor data unreadable ({err}). Showing the last good snapshot."),
            "bad",
        ));
    }
    if !live.game_running() {
        n.push(notice("F1 25 isn't running. Start it and load a career to edit. Backups and history work while it's closed.", "muted"));
        return n;
    }
    match live.bridge {
        Bridge::Missing => n.push(notice(&missing_text(live.build.as_ref()), "warn")),
        Bridge::Stale => n.push(notice(
            &format!(
                "The game plugin stopped answering (last snapshot {}). Edits are paused.",
                ago(live.snap["written"].as_u64().unwrap_or(0))
            ),
            "warn",
        )),
        Bridge::Live => {
            if !live.career() {
                n.push(notice(
                    "No career loaded. Load a My Team or Driver Career save in the game.",
                    "accent",
                ));
            } else if anytime_page {
                if !live.anytime() {
                    let why = live.status()["why_anytime"]
                        .as_str()
                        .unwrap_or("the game is busy");
                    n.push(notice(&format!("Edits wait: {why}."), "warn"));
                }
            } else if !live.safe() {
                let why = live.why().unwrap_or_else(|| "the game is busy".into());
                n.push(notice(
                    &format!("Edits wait: {why}. Queued edits apply as soon as Team HQ is idle."),
                    "warn",
                ));
            }
        }
    }
    n
}

fn missing_text(build: Option<&GameBuild>) -> String {
    match build {
        Some(b) if b.unsupported => format!(
            "This F1 25{} isn't a game version the mods were made for, so the editor's game plugin can't start. Update the game in Steam or the EA app.",
            b.version.as_ref().map_or_else(String::new, |v| format!(" ({v})"))
        ),
        _ => "The game is running but its editor plugin hasn't written a snapshot. Check that this profile has Career Mode Editor on, then restart the game.".into(),
    }
}

fn frame(app: &App, live: &Live, subtitle: String, blocks: Vec<Value>, anytime_page: bool) -> Value {
    let footer = if live.live() {
        format!(
            "Game plugin {} · snapshot {}",
            live.snap["addon"]["version"].as_str().unwrap_or("?"),
            ago(live.snap["written"].as_u64().unwrap_or(0))
        )
    } else {
        "Edits go through the game's own code paths and only apply in safe states.".to_owned()
    };
    json!({
        "subtitle": subtitle,
        "tick": true,
        "notices": common_notices(app, live, anytime_page),
        "body": { "blocks": blocks },
        "footer": footer,
    })
}

fn status_pills(live: &Live) -> Vec<Value> {
    let mut p = Vec::new();
    if live.game_running() {
        p.push(dot("Game running", "good"));
    } else {
        p.push(dot("Game closed", "muted"));
    }
    match live.bridge {
        Bridge::Live => p.push(pill("Editor live", "good")),
        Bridge::Stale if live.game_running() => p.push(pill("Editor not answering", "warn")),
        _ => {}
    }
    if live.career() {
        p.push(pill(mode_label(live.mode()), "accent"));
        if live.safe() {
            p.push(dot("Team HQ · ready", "good"));
        } else {
            let phase = live.status()["phase"].as_str().unwrap_or("unknown");
            p.push(dot(&format!("{} · edits wait", phase_label(phase)), "warn"));
        }
    }
    p
}

fn season_line(live: &Live) -> String {
    let s = live.status();
    let mut parts = vec![mode_label(live.mode()).to_owned()];
    if let Some(season) = int(&s["season"]) {
        match int(&s["year"]) {
            Some(y) => parts.push(format!("Season {season} ({y})")),
            None => parts.push(format!("Season {season}")),
        }
    }
    if let (Some(r), Some(n)) = (int(&s["round"]), int(&s["rounds"])) {
        parts.push(format!("Round {r} of {n}"));
    }
    if let Some(t) = current_track(live) {
        parts.push(t);
    }
    parts.join(" · ")
}

fn current_track(live: &Live) -> Option<String> {
    let round = live
        .section("calendar")
        .and_then(|c| c["rounds"].as_array())
        .and_then(|r| r.iter().find(|r| r["current"] == true).cloned())
        .unwrap_or_else(|| json!({ "weekend": live.status()["weekend"] }));
    crate::calendar::round_track(&round, None).map(|t| t.label())
}

fn profile(app: &App) -> Option<&Value> {
    match &app.profile_tracks {
        Some(Ok(p)) => Some(p),
        _ => None,
    }
}

fn disabled_reason(live: &Live, op: &str) -> Option<String> {
    live.gate(op).err()
}

fn form_help(live: &Live, op: &str) -> Option<String> {
    match live.gate(op) {
        Err(e) => Some(e),
        Ok(()) if !live.applies_now(op) => Some("Queued edits apply as soon as Team HQ is idle.".into()),
        Ok(()) => None,
    }
}

// ---- views --------------------------------------------------------------------------------------

pub const TABS: [(&str, &str, &str); 7] = [
    ("overview", "Overview", "gauge"),
    ("team", "Team", "wallet"),
    ("drivers", "Drivers", "users"),
    ("rnd", "R&D", "wrench"),
    ("season", "Season", "trophy"),
    ("weather", "Weather", "cloud-rain"),
    ("history", "History", "history"),
];

fn tab_disabled(live: &Live, tab: &str) -> bool {
    let need = match tab {
        "drivers" => "drivers",
        "rnd" => "rnd",
        "season" => "season",
        "weather" => "weather",
        _ => return false,
    };
    live.career()
        && live.section(need).is_none()
        && live.snap["bridge"]["sections"]
            .as_array()
            .is_some_and(|a| !a.iter().any(|x| x["name"] == need))
}

fn tabs_block(app: &App, live: &Live, active: &str) -> Value {
    let items: Vec<Value> = TABS
        .iter()
        .map(|(id, label, icon)| {
            let mut t = json!({ "id": id, "label": label, "icon": icon });
            if tab_disabled(live, id) {
                t["disabled"] = json!(true);
            }
            if *id == "history" {
                let queued = app.store.pending().len();
                if queued > 0 {
                    t["badge"] = json!(queued.to_string());
                }
            }
            t
        })
        .collect();
    json!({ "tabs": { "id": "page", "items": items, "active": active } })
}

pub fn view(app: &mut App, host: &mut Host, _section: &str) -> Value {
    let live = app.live(host);
    remember_weekends(app, &live);
    let tab = if TABS.iter().any(|t| t.0 == app.tab) && !tab_disabled(&live, &app.tab) {
        app.tab.clone()
    } else {
        "overview".to_owned()
    };
    if tab == "season" {
        app.profile_tracks = Some(crate::tracks::fetch(host, &app.launcher));
    }
    let mut v = match tab.as_str() {
        "team" => team(app, &live),
        "drivers" => drivers(app, &live),
        "rnd" => rnd(app, &live),
        "season" => season(app, &live),
        "weather" => weather(app, &live),
        "history" => history(app, &live),
        _ => overview(app, &live),
    };
    let mut blocks = vec![status_hero(app, &live), tabs_block(app, &live, &tab)];
    if let Some(b) = v["body"]["blocks"].as_array() {
        blocks.extend(b.iter().cloned());
    }
    v["body"] = json!({ "blocks": blocks });
    v["subtitle"] = json!(if live.career() {
        format!(
            "{} · {}",
            season_line(&live),
            TABS.iter().find(|t| t.0 == tab).map_or("", |t| t.1)
        )
    } else {
        title_of_status(&live)
    });
    v
}

fn status_hero(app: &App, live: &Live) -> Value {
    let s = live.status();
    let title = if !live.game_running() {
        "F1 25 is closed".to_owned()
    } else if !live.live() {
        "Waiting for the game".to_owned()
    } else if live.career() {
        if live.my_team() {
            "My Team career".to_owned()
        } else {
            "Driver Career".to_owned()
        }
    } else {
        "No career loaded".to_owned()
    };
    let subtitle = if live.career() {
        season_line(live)
    } else if live.game_running() {
        "Load a My Team or Driver Career save to start editing".to_owned()
    } else {
        "Start the game and load a career. Everything on these pages updates live.".to_owned()
    };
    let mut facts = Vec::new();
    if live.live() {
        facts.push(fact(
            "Phase",
            phase_label(s["phase"].as_str().unwrap_or("unknown")),
        ));
        if let Some(d) = int(&s["day"]) {
            facts.push(fact("Career day", d.to_string()));
        }
        if let Some(st) = s["state"].as_str() {
            facts.push(fact("Game state", st.to_owned()));
        }
        facts.push(fact("Snapshot", ago(live.snap["written"].as_u64().unwrap_or(0))));
    }
    let backups = app.store.backups();
    facts.push(fact(
        "Last backup",
        backups
            .first()
            .map_or_else(|| "none yet".into(), |b| ago(b.created)),
    ));
    let hero = json!({ "hero": {
        "title": title,
        "subtitle": subtitle,
        "art": { "monogram": match live.mode() { "my_team" => "MT", "driver_career" => "DC", _ => "CE" } },
        "pills": status_pills(live),
        "facts": facts,
        "buttons": [ button("backup_now", "Back up saves", "save", "outline"), button("refresh", "Refresh", "refresh", "ghost") ],
    }});
    hero
}

fn overview(app: &App, live: &Live) -> Value {
    let s = live.status();
    let mut blocks = Vec::new();

    if live.career() {
        let mut tiles = Vec::new();
        let econ = live.section("economy");
        if live.my_team() {
            match econ.and_then(|e| int(&e["cash"]["balance"])) {
                Some(c) => tiles.push(json!({ "label": "Cash", "value": money_short(c), "hint": money(c), "icon": "wallet" })),
                None => tiles.push(json!({ "label": "Cash", "value": "—", "hint": "not available", "icon": "wallet", "tone": "muted" })),
            }
        }
        match econ.and_then(|e| int(&e["rp"]["wallet"])) {
            Some(rp) => {
                let per = econ.and_then(|e| e["rp"]["per"].as_str()).unwrap_or("team");
                tiles.push(json!({ "label": "Resource points", "value": group(rp), "hint": format!("{per} wallet"), "icon": "zap", "tone": "accent" }));
            }
            None => tiles.push(json!({ "label": "Resource points", "value": "—", "hint": "not available", "icon": "zap", "tone": "muted" })),
        }
        let names = driver_names(live);
        if let Some(player) = live
            .section("drivers")
            .and_then(|d| d["drivers"].as_array())
            .and_then(|a| a.iter().find(|d| d["player"] == true))
        {
            let ov = int(&player["overall"]).unwrap_or(0);
            tiles.push(json!({ "label": "Your overall", "value": ov.to_string(), "hint": title_case(player["name"].as_str().unwrap_or("you")),
                               "icon": "user", "progress": ov as f64 / 100.0 }));
        }
        if let Some(st) = live.section("standings") {
            let player_id = s["player"]["driver"].as_i64();
            let row = st["drivers"].as_array().and_then(|a| {
                a.iter()
                    .find(|d| player_id.is_some() && d["key"].as_i64() == player_id)
                    .or_else(|| {
                        let pid = names.iter().find(|n| n.3).map(|n| n.0);
                        a.iter()
                            .find(|d| pid.is_some() && d["driver_stat_id"].as_i64() == pid)
                    })
            });
            if let Some(r) = row {
                tiles.push(json!({ "label": "Championship", "value": format!("P{}", int(&r["position"]).unwrap_or(0)),
                                   "hint": format!("{} pts", int(&r["points"]).unwrap_or(0)), "icon": "trophy" }));
            }
        }
        if let Some(team) = live
            .section("rnd")
            .and_then(|r| r["teams"].as_array())
            .and_then(|a| a.iter().find(|t| t["player"] == true))
        {
            let fitted = int(&team["counts"]["fitted"]).unwrap_or(0);
            let total = int(&team["counts"]["total"]).unwrap_or(0).max(1);
            tiles.push(
                json!({ "label": "R&D fitted", "value": format!("{fitted}/{total}"), "icon": "wrench",
                               "progress": fitted as f64 / total as f64, "tone": "good" }),
            );
        }
        if let (Some(r), Some(n)) = (int(&s["round"]), int(&s["rounds"])) {
            tiles.push(
                json!({ "label": "Season progress", "value": format!("Round {r}"), "hint": format!("of {n}"),
                               "icon": "flag", "progress": r as f64 / n.max(1) as f64 }),
            );
        }
        if !tiles.is_empty() {
            blocks.push(json!({ "tiles": tiles }));
        }
    }

    let recent: Vec<Value> = app
        .store
        .history
        .iter()
        .rev()
        .filter(|e| e.op != "bridge.ping" && e.op != "rnd.team_detail" && in_scope(live, e))
        .take(8)
        .map(|e| history_row(app, e, false))
        .collect();
    let recent_block = if recent.is_empty() {
        text(
            "No edits yet. Changes you make on these pages show up here with their before and after values.",
            "muted",
        )
    } else {
        json!({ "table": { "id": "recent", "columns": history_columns(false), "rows": recent, "compact": true } })
    };
    let pending = app.store.pending();
    let bridge = &live.snap["bridge"];
    let mut side = Vec::new();
    if live.live() {
        let mut bridge_facts = vec![
            fact("Queued", pending.len().to_string()),
            fact(
                "Applied this game",
                bridge["applied"].as_u64().map_or("—".into(), |v| v.to_string()),
            ),
            fact(
                "Failed",
                bridge["failed"].as_u64().map_or("—".into(), |v| v.to_string()),
            ),
        ];
        if let Some(ops) = bridge["ops"].as_array() {
            bridge_facts.push(fact("Edits offered", ops.len().to_string()));
        }
        if let Some(secs) = bridge["sections"].as_array() {
            bridge_facts.push(fact("Data sections", secs.len().to_string()));
        }
        side.push(json!({ "facts": bridge_facts }));
        if let Some(b) = bridge["blocked"]["why"].as_str() {
            side.push(text(&format!("Waiting: {b}"), "warn"));
        }
        let off: Vec<String> = bridge["ops"]
            .as_array()
            .into_iter()
            .flatten()
            .chain(bridge["sections"].as_array().into_iter().flatten())
            .filter(|o| !o["off"].is_null())
            .filter_map(|o| o["name"].as_str().map(str::to_owned))
            .collect();
        if !off.is_empty() {
            side.push(text(
                &format!("Switched off after a fault: {}", off.join(", ")),
                "bad",
            ));
        }
    } else {
        let seen = live.snap["written"].as_u64().map_or_else(|| "never".into(), ago);
        side.push(json!({ "facts": [
            fact("Queued", pending.len().to_string()),
            fact("Game plugin last seen", seen),
            fact("Edits on record", app.store.history.iter().filter(|e| e.ok && e.op != "bridge.ping").count().to_string()),
        ] }));
        side.push(text(
            "The game plugin writes its status while F1 25 runs with this addon on.",
            "muted",
        ));
    }
    blocks.push(grid(
        json!([2, 1]),
        vec![
            card_full(
                "Recent edits",
                None,
                vec![],
                vec![button("go_history", "History", "history", "ghost")],
                vec![recent_block],
            ),
            card("Editor", side),
        ],
    ));

    if live.live() {
        let flow: Vec<Value> = s["flow"]
            .as_array()
            .into_iter()
            .flatten()
            .map(|f| {
                let st = f["status"].as_str().unwrap_or("?");
                json!({ "cells": [
                    f["state"].as_str().unwrap_or("?"),
                    { "text": st, "pill": true, "tone": if st == "Updating" { "good" } else { "accent" } },
                    { "text": f["since"].as_u64().map_or_else(|| "—".into(), ago), "sort": f["since"].as_u64().unwrap_or(0) }
                ] })
            })
            .collect();
        let d = &s["detect"];
        let mut detect = vec![json!({ "facts": [
            fact("Career id", opt(&s["career_id"]).unwrap_or_else(|| "—".into())),
            fact("Components", d["components"].as_str().unwrap_or("—").to_owned()),
            fact("Mode from components", mode_label(d["component_mode"].as_str().unwrap_or("none"))),
            fact("Team HQ said", mode_label(d["team_hq_answer"].as_str().unwrap_or("none"))),
        ] })];
        if !flow.is_empty() {
            detect.push(json!({ "table": { "id": "flow", "columns": ["Flow state", "Status", { "key": "for", "title": "Since", "align": "right" }], "rows": flow, "compact": true } }));
        }
        blocks.push(card("Game state", detect));
    }
    let sub = if live.career() {
        format!(
            "Live from F1 25 · {}",
            opt(&s["career_id"]).map_or_else(|| "career".into(), |id| format!("career {id}"))
        )
    } else {
        title_of_status(live)
    };
    frame(app, live, sub, blocks, false)
}

fn page_sub(live: &Live) -> String {
    if live.career() {
        season_line(live)
    } else {
        title_of_status(live)
    }
}

fn title_of_status(live: &Live) -> String {
    if !live.game_running() {
        "Game closed".into()
    } else if !live.live() {
        "Waiting for the game plugin".into()
    } else {
        "No career loaded".into()
    }
}

// ---- team ---------------------------------------------------------------------------------------

fn team(app: &App, live: &Live) -> Value {
    let mut blocks = Vec::new();
    let econ = live.section("economy");
    let cash = econ.and_then(|e| int(&e["cash"]["balance"]));
    let rp = econ.and_then(|e| int(&e["rp"]["wallet"]));
    let mut tiles = Vec::new();
    if live.my_team() || !live.career() {
        tiles.push(json!({ "label": "Cash", "value": cash.map_or("—".into(), money_short), "hint": cash.map_or("team account".into(), money), "icon": "wallet" }));
    }
    tiles.push(json!({ "label": "Resource points", "value": rp.map_or("—".into(), group), "icon": "zap", "tone": "accent",
                       "hint": econ.and_then(|e| e["rp"]["per"].as_str()).map_or("wallet".into(), |p| format!("{p} wallet")) }));
    if let Some(f) = live.section("facilities").and_then(|f| f["f"].as_array()) {
        let lv: i64 = f.iter().filter_map(|x| int(&x["lv"])).sum();
        tiles.push(json!({ "label": "Facility levels", "value": lv.to_string(), "hint": format!("across {} facilities", f.len()), "icon": "wrench" }));
    }
    blocks.push(json!({ "tiles": tiles }));

    let cash_op = "economy.cash.set";
    let rp_op = "economy.rp.set";
    let mut fields = Vec::new();
    if live.my_team() || !live.career() {
        let mut f = json!({ "id": "cash", "label": "Team cash", "value": cash.unwrap_or(0), "min": 0, "max": 2147483647i64, "step": 1, "unit": "$",
                            "help": "Posted as a bank transaction, so Finance History shows it." });
        if let Some(why) = disabled_reason(live, cash_op) {
            f["disabled"] = json!(true);
            f["help"] = json!(why);
        }
        fields.push(json!({ "number": f }));
    }
    let mut f = json!({ "id": "rp", "label": "Resource points", "value": rp.unwrap_or(0), "min": 0, "max": 999999, "step": 1, "unit": "RP",
                        "help": "Goes through the game's own RP award path." });
    if let Some(why) = disabled_reason(live, rp_op) {
        f["disabled"] = json!(true);
        f["help"] = json!(why);
    }
    fields.push(json!({ "number": f }));
    let any_ok = live.can(rp_op) || (live.my_team() && live.can(cash_op));
    let mut quick = Vec::new();
    if live.my_team() && live.can("economy.cash.add") {
        quick.push(button("cash_add:10000000", "+$10M", "trending-up", "outline"));
        quick.push(button("cash_add:50000000", "+$50M", "trending-up", "outline"));
    }
    if live.can("economy.rp.add") {
        quick.push(button("rp_add:1000", "+1,000 RP", "zap", "outline"));
        quick.push(button("rp_add:10000", "+10,000 RP", "zap", "outline"));
    }
    let mut finance = vec![
        json!({ "form": { "id": "finance", "columns": 2, "fields": fields, "apply": "Apply", "disabled": !any_ok } }),
    ];
    if !quick.is_empty() {
        finance.push(json!({ "buttons": quick }));
    }
    if let Some(h) = form_help(live, rp_op).filter(|_| any_ok) {
        finance.push(text(&h, "muted"));
    }
    let mut cards = vec![card_full(
        "Finance",
        Some("Cash and resource points"),
        vec![],
        vec![],
        finance,
    )];
    if live.career() && !live.my_team() {
        cards.push(unavailable(
            "Cash",
            "Driver Career has no team bank, so cash, facilities, sponsors and the cost cap are My Team only. Resource points are your driver's wallet.",
        ));
    }

    if let Some(c) = econ.map(|e| &e["cash"]).filter(|c| c.is_object()) {
        let rows: Vec<Value> = c["history"]
            .as_array()
            .into_iter()
            .flatten()
            .rev()
            .map(|h| {
                let amount = int(&h["amount"]).unwrap_or(0);
                json!({ "cells": [
                    { "text": int(&h["day"]).map_or("—".into(), |d| format!("Day {d}")), "sort": int(&h["day"]).unwrap_or(0) },
                    title_case(h["type_name"].as_str().unwrap_or("").trim_start_matches('e')),
                    { "text": signed_money_short(amount), "tone": if amount >= 0 { "good" } else { "bad" }, "sort": amount }
                ] })
            })
            .collect();
        let mut b = Vec::new();
        if let Some(acc) = c["accounts"].as_array().filter(|a| a.len() > 1) {
            b.push(json!({ "facts": acc.iter().map(|a| fact(&format!("Account {}", int(&a["category"]).unwrap_or(0)), money_short(int(&a["balance"]).unwrap_or(0)))).collect::<Vec<_>>() }));
        }
        if rows.is_empty() {
            b.push(text("No transactions yet.", "muted"));
        } else {
            b.push(json!({ "table": { "id": "cash_history", "columns": ["Day", "Type", { "key": "amount", "title": "Amount", "align": "right", "sort": true }], "rows": rows, "compact": true } }));
        }
        cards.push(card("Cash history", b));
    }
    blocks.push(grid(json!([3, 2]), cards));

    if live.my_team() || !live.career() {
        blocks.push(facilities_card(live));
    }
    if live.my_team() {
        blocks.push(grid(json!([1, 1]), vec![renown_card(live), costcap_card(live)]));
        blocks.push(sponsors_card(live));
    }
    let sub = if live.career() {
        season_line(live)
    } else {
        title_of_status(live)
    };
    frame(app, live, sub, blocks, false)
}

fn facilities_card(live: &Live) -> Value {
    let Some(f) = live.section("facilities") else {
        return unavailable("Facilities", "Not available in this mode or build.");
    };
    if f["ready"].as_bool() == Some(false) {
        return unavailable(
            "Facilities",
            &format!(
                "Not ready: {}",
                f["reason"].as_str().unwrap_or("the facility data isn't loaded")
            ),
        );
    }
    let can_level = live.can("facilities.set_level");
    const NAMES: [&str; 3] = ["Engineering", "Personnel", "Corporate"];
    let rows: Vec<Value> = f["f"]
        .as_array()
        .into_iter()
        .flatten()
        .map(|x| {
            let ty = int(&x["ty"]).unwrap_or(0);
            let level = int(&x["lv"]).unwrap_or(0);
            let building: Vec<String> = x["bld"]
                .as_array()
                .into_iter()
                .flatten()
                .map(|b| format!("node {} ({} of {} d left)", b["i"], int(&b["dl"]).unwrap_or(0), int(&b["dd"]).unwrap_or(0)))
                .collect();
            let mut lvl = json!({ "text": format!("{level} / 4"), "bar": level as f64 / 4.0, "tone": "good", "sort": level });
            if can_level {
                lvl["edit"] = json!({ "number": { "value": level, "min": 1, "max": 4, "step": 1 } });
            }
            json!({ "id": ty.to_string(), "cells": [
                NAMES.get(ty as usize).copied().unwrap_or("Facility"),
                lvl,
                { "text": format!("{}/{}", int(&x["nb"]).unwrap_or(0), int(&x["nt"]).unwrap_or(0)), "sort": int(&x["nb"]).unwrap_or(0) },
                { "text": int(&x["wc"]).map_or("—".into(), |w| w.to_string()), "sort": int(&x["wc"]).unwrap_or(0) },
                if building.is_empty() { json!({ "text": "idle", "tone": "muted" }) } else { json!({ "text": building.join(", "), "tone": "accent" }) }
            ] })
        })
        .collect();
    let mut buttons = Vec::new();
    if live.can("facilities.complete") {
        buttons.push(confirm(
            button("fac_complete", "Finish all builds", "zap", "primary"),
            "Finish every facility build now?",
            "The game's completion function runs for each build and fires its rewards. Undo needs a backup.",
        ));
    }
    let blocks = vec![
        json!({ "table": { "id": "facilities", "columns": [
            "Facility", { "key": "level", "title": "Level", "width": 140, "sort": true },
            { "key": "nodes", "title": "Upgrades built", "align": "right", "width": 130 },
            { "key": "workforce", "title": "Workforce cap", "align": "right", "width": 130 }, "Building"
        ], "rows": rows, "compact": true } }),
        text(
            if can_level {
                "Click a level to raise it: every upgrade up to that phase completes through the game, rewards included. Lowering isn't offered."
            } else {
                "Levels are read-only until the facility ops are available."
            },
            "muted",
        ),
    ];
    card_full(
        "Facilities",
        Some("Undo: restore a backup"),
        vec![pill("Backup-only undo", "warn")],
        buttons,
        blocks,
    )
}

fn renown_card(live: &Live) -> Value {
    let Some(r) = live
        .section("economy")
        .map(|e| &e["renown"])
        .filter(|r| r.is_object())
    else {
        return unavailable("Renown", "Not available in this mode or build.");
    };
    let points = r["points"].as_f64().unwrap_or(0.0);
    let level = int(&r["level"]).unwrap_or(0);
    let tiers: Vec<f64> = r["tiers"]
        .as_array()
        .into_iter()
        .flatten()
        .filter_map(Value::as_f64)
        .collect();
    let next = tiers.iter().copied().find(|t| *t > points);
    let prev = tiers.iter().copied().filter(|t| *t <= points).fold(0.0, f64::max);
    let mut blocks = vec![json!({ "meters": [ {
        "label": format!("Tier {level} of {}", tiers.len()),
        "value": points - prev, "max": next.map_or(1.0, |n| (n - prev).max(1.0)),
        "text": next.map_or_else(|| format!("{points:.0} · top tier"), |n| format!("{points:.0} / {n:.0}")),
        "tone": "accent" } ] })];
    let op = "economy.renown.set";
    let mut f = json!({ "id": "renown", "label": "Renown points", "value": points.round() as i64, "min": 0, "max": 100000, "step": 1,
                        "help": "Posted as the game's own renown award, so tier rewards follow." });
    if let Some(why) = disabled_reason(live, op) {
        f["disabled"] = json!(true);
        f["help"] = json!(why);
    }
    blocks.push(json!({ "form": { "id": "renown", "fields": [ { "number": f } ], "apply": "Set", "disabled": !live.can(op) } }));
    if live.can("economy.renown.add") {
        blocks.push(json!({ "buttons": [ button("renown_add:50", "+50", "trending-up", "outline"), button("renown_add:250", "+250", "trending-up", "outline") ] }));
    }
    card_full(
        "Renown",
        None,
        vec![pill("Undo available", "good")],
        vec![],
        blocks,
    )
}

fn costcap_card(live: &Live) -> Value {
    let Some(c) = live
        .section("economy")
        .map(|e| &e["cost_cap"])
        .filter(|c| c.is_object())
    else {
        return unavailable("Cost cap", "Not available in this mode or build.");
    };
    let cur = int(&c["current_season"]).unwrap_or(0);
    let seasons = c["seasons"].as_array().cloned().unwrap_or_default();
    let now = seasons
        .iter()
        .find(|s| int(&s["season"]) == Some(cur))
        .or(seasons.last());
    let mut blocks = Vec::new();
    let expenses = now.and_then(|s| int(&s["expenses"])).unwrap_or(0);
    if let Some(s) = now {
        let cap = int(&s["cost_cap"]).unwrap_or(0).max(1);
        let ratio = expenses as f64 / cap as f64;
        blocks.push(json!({ "meters": [ { "label": "This season's spending", "value": expenses, "max": cap,
            "text": format!("{} of {}", money_short(expenses), money_short(cap)), "tone": if ratio > 0.95 { "bad" } else if ratio > 0.8 { "warn" } else { "good" } } ] }));
    } else {
        blocks.push(text("No cost cap data for this season yet.", "muted"));
    }
    let op = "economy.costcap.refund";
    let mut f = json!({ "id": "amount", "label": "Refund", "value": 0, "min": 0, "max": expenses.clamp(0, 100_000_000), "step": 1, "unit": "$",
                        "help": "A damage refund: cash goes up and this season's spending goes down." });
    if let Some(why) = disabled_reason(live, op) {
        f["disabled"] = json!(true);
        f["help"] = json!(why);
    }
    blocks.push(json!({ "form": { "id": "costcap", "fields": [ { "number": f } ], "apply": "Refund", "disabled": !live.can(op) } }));
    card_full(
        "Cost cap",
        None,
        vec![pill("Backup-only undo", "warn")],
        vec![],
        blocks,
    )
}

fn sponsors_card(live: &Live) -> Value {
    let Some(sp) = live
        .section("economy")
        .map(|e| &e["sponsors"])
        .filter(|s| s.is_object())
    else {
        return unavailable("Sponsors", "Not available in this mode or build.");
    };
    let primary = int(&sp["primary"]).unwrap_or(-1);
    let rows: Vec<Value> = sp["list"]
        .as_array()
        .into_iter()
        .flatten()
        .map(|x| {
            let raw = x["name"].as_str().unwrap_or("");
            let name = if raw.is_empty() { format!("Sponsor #{}", x["id"]) } else { title_case(raw.trim_start_matches("sponsor_")) };
            let max = int(&x["max_loyalty"]).unwrap_or(0).max(1);
            let loyalty = int(&x["loyalty"]).unwrap_or(0);
            json!({ "cells": [
                name,
                if int(&x["id"]) == Some(primary) { json!({ "text": "Primary", "pill": true, "tone": "accent" }) }
                else if x["active"] == true { json!({ "text": "Active", "pill": true, "tone": "good" }) }
                else { json!({ "text": "Inactive", "pill": true, "tone": "muted" }) },
                { "text": format!("{loyalty} XP"), "bar": (loyalty as f64 / max as f64).min(1.0), "sort": loyalty },
                { "text": int(&x["level"]).unwrap_or(0).to_string(), "sort": int(&x["level"]).unwrap_or(0) }
            ] })
        })
        .collect();
    let mut blocks = vec![json!({ "table": { "id": "sponsors", "columns": [
        "Sponsor", { "key": "role", "title": "Role", "width": 120 }, { "key": "loyalty", "title": "Loyalty", "width": 220, "sort": true },
        { "key": "level", "title": "Level", "align": "right", "width": 80 } ], "rows": rows, "compact": true, "empty": "No sponsors" } })];
    let op = "economy.sponsor.loyalty.add";
    if primary >= 0 {
        let reasons = [
            "Race weekend (primary)",
            "Race weekend (secondary)",
            "Seasonal goal",
            "Facility request",
            "Department event",
            "Activity",
        ];
        let opts: Vec<Value> = reasons
            .iter()
            .enumerate()
            .map(|(i, l)| json!({ "value": i.to_string(), "label": *l }))
            .collect();
        blocks.push(json!({ "form": { "id": "loyalty", "columns": 2, "apply": "Add loyalty", "disabled": !live.can(op), "fields": [
            { "number": { "id": "amount", "label": "Loyalty XP for the primary sponsor", "value": 0, "min": 0, "max": 100000, "step": 1 } },
            { "select": { "id": "reason", "label": "Counted as", "value": "0", "options": opts,
                          "help": "The game scales the amount by your perks for that reason and claims any loyalty rewards it reaches." } }
        ] } }));
    } else {
        blocks.push(text("No primary sponsor, so there's no loyalty to add.", "muted"));
    }
    if let Some(h) = form_help(live, op) {
        blocks.push(text(&h, "muted"));
    }
    card_full(
        "Sponsors",
        None,
        vec![pill("Backup-only undo", "warn")],
        vec![],
        blocks,
    )
}

// The racing number on the driver's entrant row; a snapshot from before 0.2.6 only has the gamedb default.
fn race_number(d: &Value) -> Option<i64> {
    match d.get("raceNumber") {
        Some(n) => int(n),
        None => int(&d["number"]),
    }
}

fn number_cell(d: &Value, editable: bool) -> Value {
    let n = race_number(d);
    let mut c = json!({ "text": n.map_or("—".into(), |n| n.to_string()), "sort": n.unwrap_or(0) });
    if editable {
        c["edit"] = json!({ "number": { "value": n.unwrap_or(0), "min": 1, "max": 99, "step": 1 } });
    }
    c
}

fn numbers_card(live: &Live) -> Value {
    let op = "drivers.number.set";
    let drivers = live
        .section("drivers")
        .and_then(|d| d["drivers"].as_array())
        .cloned()
        .unwrap_or_default();
    let mut opts = vec![json!({ "value": "", "label": "Choose a driver…" })];
    for d in &drivers {
        let n = race_number(d).map_or_else(|| "no number".into(), |n| format!("#{n}"));
        let team = d["team"]["name"]
            .as_str()
            .map_or_else(|| "no team".into(), pretty_team);
        let you = if d["player"] == true { " (you)" } else { "" };
        opts.push(json!({ "value": d["driverStatId"].to_string(),
            "label": format!("{}{you} · {team} · {n}", title_case(d["name"].as_str().unwrap_or("driver"))) }));
    }
    let blocks = vec![
        json!({ "form": { "id": "race_number", "columns": 3, "apply": "Set number", "disabled": !live.can(op), "fields": [
            { "select": { "id": "driver", "label": "Driver", "value": "", "options": opts } },
            { "number": { "id": "number", "label": "Race number", "value": 1, "min": 1, "max": 99, "step": 1 } },
            { "toggle": { "id": "swap", "label": "If another driver has it, give him this driver's number", "value": false } }
        ] } }),
        text(
            &form_help(live, op).unwrap_or_else(|| {
                "The number on the car, the grid and the standings, saved with the career. Numbers are unique in the championship, so a number another driver has needs the swap. You can also click a number in the ratings table.".into()
            }),
            "muted",
        ),
    ];
    card_full(
        "Race numbers",
        None,
        vec![pill("Undo available", "good")],
        vec![],
        blocks,
    )
}

fn number_op(live: &Live, id: i64, number: i64, swap: bool) -> Result<(String, Value), String> {
    let d = live
        .section("drivers")
        .and_then(|d| d["drivers"].as_array())
        .and_then(|a| a.iter().find(|d| d["driverStatId"].as_i64() == Some(id)).cloned())
        .ok_or("that driver isn't in the snapshot any more")?;
    if !(1..=99).contains(&number) {
        return Err("Pick a number from 1 to 99.".into());
    }
    let mut args = json!({ "driverStatId": id, "number": number, "swap": swap });
    if let Some(n) = race_number(&d) {
        args["expect"] = json!(n);
    }
    let label = format!(
        "{}: number {number}",
        title_case(d["name"].as_str().unwrap_or("driver"))
    );
    Ok((label, json!({ "op": "drivers.number.set", "args": args })))
}

fn swap_card(live: &Live) -> Value {
    let op = "drivers.swap";
    let drivers = live
        .section("drivers")
        .and_then(|d| d["drivers"].as_array())
        .cloned()
        .unwrap_or_default();
    let my_team = live.my_team();
    let mut opts = vec![json!({ "value": "", "label": "Choose a driver…" })];
    for d in drivers
        .iter()
        .filter(|d| !d["seat"].is_null() && d["player"] != true)
    {
        let has_team = !d["team"].is_null();
        let has_car = d["seat"]["hasCar"] == true;
        // My Team: drivers with a team (race seat or reserve). Driver Career: race drivers and
        // free agents only (R&D there is per race driver, so reserves can't be swapped).
        let (ok, role) = match (my_team, has_team, has_car) {
            (true, true, true) | (false, true, true) => (true, "race seat"),
            (true, true, false) => (true, "reserve"),
            (false, false, false) => (true, "Free agent"),
            _ => (false, ""),
        };
        if !ok {
            continue;
        }
        let team = d["team"]["name"]
            .as_str()
            .map_or_else(|| "no team".into(), pretty_team);
        let label = if has_team {
            format!(
                "{} · {team} · {role}",
                title_case(d["name"].as_str().unwrap_or("driver"))
            )
        } else {
            format!("{} · {role}", title_case(d["name"].as_str().unwrap_or("driver")))
        };
        opts.push(json!({ "value": d["driverStatId"].to_string(), "label": label }));
    }
    let help = if my_team {
        "Applies now, mid-season: each driver takes the other's team, car and seat through the game's own driver-move path. Contracts keep the team they were signed with. Free agents, such as drivers made with Driver Manager, aren't listed here: the experimental Free agent into a seat card below signs one. Your own seat is locked."
    } else {
        "Applies now, mid-season, through the game's own Driver Career signing path. Two race drivers trade seats. A free agent takes a race seat the way the game signs one: the seat's driver is released (contract ended) and becomes a free agent, and the game re-assigns the team's R&D. Each driver who takes a seat signs that seat's contract, and one without a race number gets a free one. Your own seat is locked."
    };
    let mut blocks = vec![
        json!({ "form": { "id": "swap", "columns": 2, "apply": "Swap seats", "disabled": !live.can(op), "fields": [
            { "select": { "id": "a", "label": "Driver", "value": "", "options": opts } },
            { "select": { "id": "b", "label": "Swaps seats with", "value": "", "options": opts } }
        ] } }),
        text(&form_help(live, op).unwrap_or_else(|| help.into()), "muted"),
    ];
    if !my_team {
        blocks.push(text(
            "Drivers with no place in this championship's line-up (the Ratings table leaves them out) aren't listed: the game gives a driver a line-up place only when it builds a season's line-up.",
            "muted",
        ));
        blocks.extend(late_free_agent_blocks(live));
    }
    card_full(
        "Swap seats",
        None,
        vec![pill("Undo available", "good"), pill("Experimental", "warn")],
        vec![],
        blocks,
    )
}

// Driver Career: Driver Manager drivers made after the career started are in the statistics table
// (`otherDrivers`, label `dm_<id>`) but have no line-up row; drivers.freeAgent.add gives one a
// teamless row and a contract, as the career setup makes them for a pool free agent.
fn late_free_agent_blocks(live: &Live) -> Vec<Value> {
    let op = "drivers.freeAgent.add";
    let late: Vec<Value> = live
        .section("drivers")
        .and_then(|d| d["otherDrivers"].as_array())
        .map(|a| {
            a.iter()
                .filter(|d| d["name"].as_str().is_some_and(|n| n.starts_with("dm_")))
                .cloned()
                .collect()
        })
        .unwrap_or_default();
    if late.is_empty() || !live.has_op(op) {
        return vec![];
    }
    let mut opts = vec![json!({ "value": "", "label": "Choose a Driver Manager driver…" })];
    for d in &late {
        let label = d["name"].as_str().unwrap_or("driver");
        let initials = d["initials"]
            .as_str()
            .map_or_else(String::new, |i| format!("{i} · "));
        let overall = int(&d["overall"]).map_or_else(String::new, |o| format!(" · overall {o}"));
        opts.push(
            json!({ "value": d["driverStatId"].to_string(), "label": format!("{initials}{label}{overall}") }),
        );
    }
    let mut blocks = vec![
        json!({ "form": { "id": "free_agent_add", "columns": 1, "apply": "Add to line-up as free agent", "disabled": !live.can(op), "fields": [
            { "select": { "id": "driver", "label": "Driver Manager driver without a line-up place", "value": "", "options": opts } }
        ] } }),
        text(
            "Experimental: back up your save first. The driver joins the line-up as a free agent with his Driver Manager look, no team, no car and no race number, and gets an empty contract, as the game makes them for the free agents of a new career. He then shows in the Driver list above and can take a race seat. There is no undo apart from the save backups.",
            "muted",
        ),
    ];
    if let Some(h) = form_help(live, op) {
        blocks.push(text(&h, "muted"));
    }
    blocks
}

// Driver Career: a race driver needs an active contract for the team he races for. The snapshot's
// `contract` is null when the game keeps no contract object for him.
fn needs_contract(d: &Value) -> bool {
    d["player"] != true
        && d["seat"]["hasCar"] == true
        && !d["team"].is_null()
        && d.get("contract").is_some()
        && !(d["contract"]["active"] == true && d["contract"]["teamStatId"] == d["team"]["teamStatId"])
}

fn dc_contracts_card(live: &Live) -> Option<Value> {
    let drivers = live.section("drivers")?["drivers"].as_array()?;
    if !drivers.iter().any(|d| d.get("contract").is_some()) {
        return None;
    }
    let op = "drivers.contracts.repair";
    let missing: Vec<&Value> = drivers.iter().filter(|d| needs_contract(d)).collect();
    if missing.is_empty() {
        return Some(card(
            "Contracts",
            vec![text(
                "Every race driver has a contract with the team he races for.",
                "muted",
            )],
        ));
    }
    let rows: Vec<Value> = missing
        .iter()
        .map(|d| {
            let c = &d["contract"];
            let state = if c.is_null() {
                "No contract kept".to_owned()
            } else if c["active"] != true {
                "Contract ended".to_owned()
            } else {
                format!(
                    "Contract with {}",
                    team_label(live, int(&c["teamStatId"]).unwrap_or(-1))
                )
            };
            json!({ "cells": [
                title_case(d["name"].as_str().unwrap_or("driver")),
                d["team"]["name"].as_str().map_or_else(|| "—".into(), pretty_team),
                { "text": state, "pill": true, "tone": "bad" }
            ] })
        })
        .collect();
    let blocks = vec![
        json!({ "table": { "id": "dc_contracts", "columns": ["Driver", "Races for", "Contract"], "rows": rows, "compact": true } }),
        text(
            &form_help(live, op).unwrap_or_else(|| {
                "These drivers race without a contract for their team, so the game's contract screens leave their seat out. Each signs the contract of a teammate's seat, through the game's own contract step.".into()
            }),
            "muted",
        ),
    ];
    Some(card_full(
        "Contracts",
        Some("Race drivers without a contract for their team"),
        vec![pill("Backup-only undo", "warn")],
        if live.can(op) {
            vec![button(
                "repair_contracts",
                "Sign their seat contracts",
                "",
                "primary",
            )]
        } else {
            vec![]
        },
        blocks,
    ))
}

fn contracts_card(live: &Live) -> Value {
    if live.career()
        && !live.my_team()
        && let Some(c) = dc_contracts_card(live)
    {
        return c;
    }
    let Some(c) = live
        .section("drivers")
        .map(|d| &d["contracts"])
        .filter(|c| c.is_object())
    else {
        return unavailable(
            "Contracts",
            if live.career() && !live.my_team() {
                "Contract edits are My Team only."
            } else {
                "Not available in this mode or build."
            },
        );
    };
    let names = driver_names(live);
    let editable = live.can("drivers.contract.set");
    let money_cell = |v: &Value| {
        let n = int(v).unwrap_or(0);
        let mut c = json!({ "text": money_short(n), "sort": n });
        if editable {
            c["edit"] =
                json!({ "number": { "value": n, "min": 0, "max": 1_000_000_000, "step": 1, "unit": "$" } });
        }
        c
    };
    let mut rows = Vec::new();
    for (list, label) in [("decided", "Signed"), ("pending", "Pending")] {
        for r in c[list].as_array().into_iter().flatten() {
            let id = int(&r["driverStatId"]).unwrap_or(-1);
            let team = int(&r["newTeamStatId"]).unwrap_or(-1);
            rows.push(json!({ "id": format!("{list}:{id}:{team}"), "cells": [
                driver_name(&names, id),
                team_label(live, team),
                { "text": label, "pill": true, "tone": if list == "decided" { "good" } else { "accent" } },
                money_cell(&r["salary"]), money_cell(&r["signingBonus"]), money_cell(&r["teamPerfMoney"]), money_cell(&r["teamPerfRaceValue"])
            ] }));
        }
    }
    let blocks = vec![
        json!({ "table": { "id": "contracts", "columns": [
            { "key": "driver", "title": "Driver", "sort": true }, { "key": "team", "title": "Team", "sort": true }, { "key": "list", "title": "State", "width": 100 },
            { "key": "salary", "title": "Salary", "align": "right", "sort": true, "width": 110 },
            { "key": "signingBonus", "title": "Signing bonus", "align": "right", "width": 120 },
            { "key": "teamPerfMoney", "title": "Team bonus", "align": "right", "width": 110 },
            { "key": "teamPerfRaceValue", "title": "Race bonus", "align": "right", "width": 110 }
        ], "rows": rows, "compact": true, "search": { "placeholder": "Search contracts" }, "empty": "No contracts" } }),
        text(
            if editable {
                "Click an amount to edit it. This writes the contract record's money fields directly; the game's payout readers weren't traced."
            } else {
                "Contract money can be edited in Team HQ in a My Team career."
            },
            "muted",
        ),
    ];
    card_full(
        "Contracts",
        Some("Money fields only: teams, days, clauses and length stay as signed"),
        vec![pill("Unsafe", "bad"), pill("Undo available", "good")],
        vec![],
        blocks,
    )
}

fn moves_card(live: &Live) -> Value {
    let names = driver_names(live);
    let moves = live
        .section("drivers")
        .and_then(|d| d["recordedMoves"].as_array())
        .cloned()
        .unwrap_or_default();
    let rows: Vec<Value> = moves
        .iter()
        .rev()
        .map(|m| {
            let reason = match int(&m["reason"]) {
                Some(0) => "Signing".to_owned(),
                Some(3) => "Benched".to_owned(),
                Some(r) => format!("Reason {r}"),
                None => "—".to_owned(),
            };
            json!({ "cells": [
                { "text": int(&m["weekendIndex"]).map_or("—".into(), |w| format!("Round {}", w + 1)), "sort": int(&m["weekendIndex"]).unwrap_or(0) },
                driver_name(&names, int(&m["driverStatId"]).unwrap_or(-1)),
                team_label(live, int(&m["oldTeamStatId"]).unwrap_or(-1)),
                team_label(live, int(&m["newTeamStatId"]).unwrap_or(-1)),
                { "text": reason, "tone": "muted" }
            ] })
        })
        .collect();
    let body = if rows.is_empty() {
        text("No driver moves recorded in this career yet.", "muted")
    } else {
        json!({ "table": { "id": "moves", "columns": [ { "key": "when", "title": "When", "width": 100, "sort": true }, "Driver", "From", "To", "Why" ], "rows": rows, "compact": true } })
    };
    card("Driver moves", vec![body])
}

// My Team only: the experimental free agent signing (drivers.swap with experimentalFreeAgent).
fn free_agent_card(live: &Live) -> Option<Value> {
    if !live.my_team() {
        return None;
    }
    let op = "drivers.swap";
    let drivers = live.section("drivers")?["drivers"].as_array()?.clone();
    let label = |d: &Value, role: &str| {
        let team = d["team"]["name"]
            .as_str()
            .map_or_else(|| "no team".into(), pretty_team);
        format!(
            "{} · {team}{role}",
            title_case(d["name"].as_str().unwrap_or("driver"))
        )
    };
    let mut free = vec![json!({ "value": "", "label": "Choose a free agent…" })];
    let mut seats = vec![json!({ "value": "", "label": "Choose a race seat…" })];
    for d in drivers
        .iter()
        .filter(|d| !d["seat"].is_null() && d["player"] != true)
    {
        let id = d["driverStatId"].to_string();
        if d["team"].is_null() {
            free.push(json!({ "value": id, "label": label(d, "") }));
        } else if d["seat"]["hasCar"] == true {
            seats.push(json!({ "value": id, "label": label(d, " · race seat") }));
        }
    }
    let blocks = vec![
        json!({ "form": { "id": "free_agent", "columns": 2, "apply": "Sign into the seat", "disabled": !live.can(op), "fields": [
            { "select": { "id": "free", "label": "Free agent", "value": "", "options": free } },
            { "select": { "id": "seat", "label": "Takes the seat of", "value": "", "options": seats } }
        ] } }),
        text(
            "Experimental, not tested in game yet: back up your save first. The free agent takes the seat now, and the seat's driver becomes a free agent. The free agent also takes over that driver's contract records with the team (next season's move, the decided contract and this season's signing), so the season change keeps him in the seat. Undo puts both back.",
            "muted",
        ),
    ];
    Some(card_full(
        "Free agent into a seat",
        Some("My Team"),
        vec![pill("Experimental", "warn"), pill("Undo available", "good")],
        vec![],
        blocks,
    ))
}

fn points_card(live: &Live, names: &[(i64, String, String, bool)]) -> Value {
    let st = live.section("standings");
    let op = "season.points.adjust";
    let mut opts = vec![json!({ "value": "", "label": "Choose a driver…" })];
    let mut drivers: Vec<Value> = st
        .and_then(|s| s["drivers"].as_array())
        .cloned()
        .unwrap_or_default();
    drivers.sort_by_key(|d| int(&d["position"]).unwrap_or(99));
    for d in &drivers {
        let id = int(&d["driver_stat_id"]).unwrap_or(-1);
        opts.push(json!({ "value": id.to_string(), "label": format!("P{} {} · {} pts", int(&d["position"]).unwrap_or(0), driver_name(names, id), int(&d["points"]).unwrap_or(0)) }));
    }
    let mut blocks = vec![
        json!({ "form": { "id": "points", "columns": 2, "apply": "Adjust points", "disabled": !live.can(op), "fields": [
        { "select": { "id": "driver", "label": "Driver", "value": "", "options": opts } },
        { "number": { "id": "delta", "label": "Points to add (negative to take away)", "value": 0, "min": -500, "max": 500, "step": 1 } }
    ] } }),
    ];
    let adj = st
        .and_then(|s| s["adjustments"].as_array())
        .cloned()
        .unwrap_or_default();
    if !adj.is_empty() {
        let rows: Vec<Value> = adj
            .iter()
            .map(|a| {
                let p = int(&a["points"]).unwrap_or(0);
                json!({ "cells": [
                    driver_name(names, int(&a["driver_stat_id"]).unwrap_or(-1)),
                    { "text": if p > 0 { format!("+{p}") } else { p.to_string() }, "tone": if p >= 0 { "good" } else { "bad" }, "sort": p },
                    if a["pending"] == true { json!({ "text": "Pending", "pill": true, "tone": "warn" }) }
                    else if a["live"] == true { json!({ "text": "In the standings", "pill": true, "tone": "good" }) }
                    else { json!({ "text": "Stored", "pill": true, "tone": "muted" }) }
                ] })
            })
            .collect();
        blocks.push(json!({ "table": { "id": "adjustments", "columns": ["Driver", { "key": "pts", "title": "Adjustment", "align": "right", "width": 120 }, { "key": "state", "title": "State", "width": 160 }], "rows": rows, "compact": true } }));
    }
    if st.is_some_and(|s| s["adjustments_pending"] == true) {
        blocks.push(text("Round 1 has no sessions yet, so the game skips adjustments for now. They apply at the next standings rebuild.", "warn"));
    }
    blocks.push(text(
        &form_help(live, op).unwrap_or_else(|| {
            "Adjustments are kept by the editor and re-applied every time the game rebuilds the standings; constructors follow their drivers. Without this addon they disappear and nothing in the save changes.".into()
        }),
        "muted",
    ));
    let mut buttons = Vec::new();
    if !adj.is_empty() && live.can("season.points.clear") {
        buttons.push(confirm(
            button("points_clear", "Clear all", "eraser", "ghost"),
            "Clear every points adjustment this season?",
            "The standings go back to what the results give.",
        ));
    }
    card_full(
        "Points adjustments",
        None,
        vec![pill("Undo available", "good")],
        buttons,
        blocks,
    )
}

fn remember_weekends(app: &mut App, live: &Live) {
    let (Some(id), Some(cal)) = (live.career_id(), live.section("calendar")) else {
        return;
    };
    let mut known: Vec<String> = app.store.prefs["known_weekends"][&id]
        .as_array()
        .into_iter()
        .flatten()
        .filter_map(|w| w.as_str().map(str::to_owned))
        .collect();
    let before = known.len();
    for w in cal["rounds"]
        .as_array()
        .into_iter()
        .flatten()
        .filter_map(|r| r["weekend"].as_str())
        .filter(|w| !crate::calendar::index_name(w))
    {
        if !known.iter().any(|k| k == w) {
            known.push(w.to_owned());
        }
    }
    if known.len() != before {
        let mut all = app.store.prefs["known_weekends"].clone();
        if !all.is_object() {
            all = json!({});
        }
        all[&id] = json!(known);
        app.store.set_pref("known_weekends", all);
    }
}

fn known_weekends(app: &App, live: &Live) -> Vec<String> {
    let id = live.career_id().unwrap_or_default();
    let mut v: Vec<String> = app.store.prefs["known_weekends"][&id]
        .as_array()
        .into_iter()
        .flatten()
        .filter_map(|w| w.as_str().map(str::to_owned))
        .collect();
    for e in &app.store.history {
        if e.career.as_deref() == Some(id.as_str()) && e.op.starts_with("season.calendar.") {
            for side in [&e.before, &e.after] {
                for w in side["rounds"]
                    .as_array()
                    .into_iter()
                    .flatten()
                    .filter_map(Value::as_str)
                {
                    v.push(w.to_owned());
                }
            }
        }
    }
    v.sort();
    v.dedup();
    v
}

fn season_weekends(cal: &Value) -> Vec<String> {
    cal["rounds"]
        .as_array()
        .into_iter()
        .flatten()
        .filter_map(|r| r["weekend"].as_str().map(str::to_owned))
        .collect()
}

fn season_track_ids(cal: &Value) -> Vec<u8> {
    cal["rounds"]
        .as_array()
        .into_iter()
        .flatten()
        .filter_map(|r| {
            r["track_stat_id"]
                .as_u64()
                .filter(|&id| id < 255)
                .map(|id| id as u8)
        })
        .collect()
}

fn track_catalogue(app: &App, live: &Live, profile: &Value) -> crate::tracks::Catalogue {
    let cal = live.section("calendar").cloned().unwrap_or(Value::Null);
    crate::tracks::catalogue(
        profile,
        &season_weekends(&cal),
        &season_track_ids(&cal),
        &known_weekends(app, live),
    )
}

fn pick_label(app: &App, live: &Live, weekend: &str) -> String {
    profile(app)
        .and_then(|p| {
            track_catalogue(app, live, p)
                .picks
                .into_iter()
                .find(|x| x.weekend == weekend)
                .map(|x| x.label)
        })
        .unwrap_or_else(|| track_name(weekend))
}

fn round_track_label(app: &App, live: &Live, round: i64) -> String {
    live.section("calendar")
        .and_then(|c| c["rounds"].as_array())
        .and_then(|rs| rs.iter().find(|x| int(&x["round"]) == Some(round)))
        .map_or_else(
            || "unknown track".into(),
            |x| crate::calendar::short_name(x, profile(app)),
        )
}

fn round_options(rounds: &[&Value], prof: Option<&Value>, placeholder: &str) -> Vec<Value> {
    let mut opts = vec![json!({ "value": "", "label": placeholder })];
    opts.extend(rounds.iter().map(|r| {
        json!({ "value": r["round"].to_string(), "label": format!("Round {} · {}", r["round"], crate::calendar::round_label(r, prof)) })
    }));
    opts
}

fn verify_weekend(app: &App, host: &mut Host, live: &Live, args: &mut Value) -> Result<(), String> {
    match crate::tracks::fetch(host, &app.launcher) {
        Ok(profile) => crate::tracks::verify(args, Some(&track_catalogue(app, live, &profile))),
        Err(_) => crate::tracks::verify(args, None),
    }
}

fn upcoming(cal: &Value) -> (i64, Vec<Value>) {
    let from = int(&cal["editable_from"]).unwrap_or(i64::MAX);
    let rounds: Vec<Value> = cal["rounds"]
        .as_array()
        .into_iter()
        .flatten()
        .filter(|r| int(&r["round"]).unwrap_or(0) >= from)
        .cloned()
        .collect();
    (from, rounds)
}

fn pending_move(app: &App, cal: &Value) -> Option<(usize, usize)> {
    let (first, future) = upcoming(cal);
    let idx = |r: Option<i64>| {
        r.and_then(|r| usize::try_from(r - first).ok())
            .filter(|&i| i < future.len())
    };
    let from = idx(app.cal_move.0)?;
    Some((from, idx(app.cal_move.1).unwrap_or(from)))
}

fn move_card(app: &App, live: &Live, cal: &Value) -> Value {
    let op = "season.calendar.reorder";
    let prof = profile(app);
    let (first, future) = upcoming(cal);
    let movable = future.len() >= 2 && future.iter().all(|r| r["editable"] == true);
    let mut blocks = Vec::new();
    if !movable {
        blocks.push(text(
            if future.len() < 2 {
                "Fewer than two rounds are left after this weekend, so there's nothing to move."
            } else {
                "A round after this weekend has already started, so the order can't change."
            },
            "muted",
        ));
        return card_full(
            "Move a round",
            None,
            vec![pill("Experimental", "warn")],
            vec![],
            blocks,
        );
    }
    let picked = pending_move(app, cal);
    let refs: Vec<&Value> = future.iter().collect();
    let from_opts = round_options(&refs, prof, "Choose a round…");
    let to_opts: Vec<Value> = future
        .iter()
        .map(|r| json!({ "value": r["round"].to_string(), "label": format!("Round {} · now {}", r["round"], crate::calendar::short_name(r, prof)) }))
        .collect();
    let (from_v, to_v) = match picked {
        Some((f, t)) => (future[f]["round"].to_string(), future[t]["round"].to_string()),
        None => (String::new(), future[0]["round"].to_string()),
    };
    let disabled = !live.can(op);
    blocks.push(
        json!({ "form": { "id": "cal_move", "columns": 2, "live": true, "disabled": disabled, "fields": [
        { "select": { "id": "from", "label": "Move", "value": from_v, "options": from_opts } },
        { "select": { "id": "to", "label": "To", "value": to_v, "options": to_opts } } ] } }),
    );
    let names: Vec<String> = future
        .iter()
        .map(|r| crate::calendar::short_name(r, prof))
        .collect();
    match picked {
        Some((f, t)) if f != t => {
            blocks.push(text(&crate::calendar::move_preview(&names, first, f, t), "accent"));
            blocks.push(json!({ "buttons": [ button("cal_move_apply", &format!("Move {} to round {}", names[f], first + t as i64), "arrow-right-left", "primary"),
                                             button("cal_move_clear", "Clear", "", "ghost") ] }));
        }
        Some((f, _)) => blocks.push(text(&format!("Pick the round {} should become.", names[f]), "muted")),
        None => blocks.push(text("Pick a round, then the round it should become. The rounds in between shift by one; past rounds and this weekend never move.", "muted")),
    }
    if let Some(h) = form_help(live, op) {
        blocks.push(text(&h, "muted"));
    }
    card_full(
        "Move a round",
        None,
        vec![pill("Experimental", "warn"), pill("Undo available", "good")],
        vec![],
        blocks,
    )
}

fn count_note(stops: Option<i64>, rounds: usize) -> String {
    match stops {
        Some(n) if n == rounds as i64 => format!(
            "The game keeps {n} race weekends in this season, so it keeps {n} rounds. To change the calendar, replace or move a round."
        ),
        Some(n) => format!(
            "The game keeps {n} race weekends in this season but the calendar has {rounds} rounds. Repair the round count before you race on."
        ),
        None => "Rounds can't be added or removed until the game's race dates for this season are readable. Replace or move a round instead.".into(),
    }
}

fn calendar_count_card(live: &Live, cal: &Value) -> Option<Value> {
    let rounds = cal["rounds"].as_array().map_or(0, Vec::len);
    let stops = int(&cal["race_weekends"])?;
    if stops == rounds as i64 {
        return None;
    }
    let op = "season.calendar.repair";
    let what = if stops > rounds as i64 {
        format!(
            "This season has {rounds} rounds, but the game still has {stops} race weekends on its calendar. After round {rounds} the game would go to a race weekend that no longer exists and crash. Repairing adds {} round(s) at the end, using tracks from this career's other seasons. Replace them afterwards if you want other tracks.",
            stops - rounds as i64
        )
    } else {
        format!(
            "This season has {rounds} rounds, but the game only has {stops} race weekends on its calendar, so the season can't finish. Repairing removes the last {} round(s) that haven't started.",
            rounds as i64 - stops
        )
    };
    Some(card_full(
        "Round count",
        Some("The calendar doesn't match the game's race weekends"),
        vec![pill("Backup-only undo", "warn")],
        if live.can(op) {
            vec![button("repair_calendar", "Repair the round count", "", "primary")]
        } else {
            vec![]
        },
        vec![text(&what, "warn")],
    ))
}

fn calendar_ops_card(app: &App, live: &Live, cal: &Value) -> Value {
    let rounds = cal["rounds"].as_array().cloned().unwrap_or_default();
    let in_season = season_weekends(cal);
    let editable: Vec<&Value> = rounds.iter().filter(|r| r["editable"] == true).collect();
    let round_opts = round_options(&editable, profile(app), "Choose a round…");
    let mut notes: Vec<String> = Vec::new();
    let mut modded_line = None;
    let (spare_opts, any) = match &app.profile_tracks {
        Some(Ok(profile)) => {
            let cat = track_catalogue(app, live, profile);
            let installed: Vec<String> = profile["tracks"]
                .as_array()
                .into_iter()
                .flatten()
                .filter(|t| t["modded"] == true && t["playable"] == true)
                .map(|t| crate::calendar::from_profile(t).name)
                .collect();
            modded_line = Some(if installed.is_empty() {
                "No modded tracks are in this profile's last build. To race one, import the track's zip on the Mods page, turn it on in this profile and press Play to build; it then shows under Modded tracks in the track menus below.".to_owned()
            } else {
                format!(
                    "Modded tracks in this profile ({}) are listed under Modded tracks in the track menus below. To add another, import its zip on the Mods page, turn it on and press Play to build.",
                    installed.join(", ")
                )
            });
            if profile["built"] != true {
                notes.push("This profile hasn't been built yet, so only the stock tracks are listed. Modded tracks appear once it builds (Play builds it).".into());
            }
            for (label, why) in &cat.refused {
                notes.push(format!("{label} isn't offered: {why}."));
            }
            let placeholder = if cat.picks.is_empty() {
                "No other tracks in this profile…"
            } else {
                "Choose a track…"
            };
            (cat.options(placeholder, true), !cat.picks.is_empty())
        }
        other => {
            if let Some(Err(why)) = other {
                notes.push(why.clone());
            }
            let spare: Vec<String> = known_weekends(app, live)
                .into_iter()
                .filter(|w| !in_season.contains(w) && !crate::calendar::index_name(w))
                .collect();
            let mut opts = vec![
                json!({ "value": "", "label": if spare.is_empty() { "No other tracks known yet…" } else { "Choose a track…" } }),
            ];
            opts.extend(
                spare
                    .iter()
                    .map(|w| json!({ "value": w, "label": track_name(w) })),
            );
            (opts, !spare.is_empty())
        }
    };
    let first = int(&cal["editable_from"]).unwrap_or(1);
    let last = rounds.len() as i64 + 1;
    let can_edit = !editable.is_empty();
    let stops = int(&cal["race_weekends"]);
    let (can_add, can_remove) = match stops {
        Some(n) => (n > rounds.len() as i64, n < rounds.len() as i64),
        None => (false, false),
    };
    let mut blocks = vec![
        json!({ "form": { "id": "cal_add", "columns": 2, "apply": "Add round", "disabled": !can_add || !any || !live.can("season.calendar.add_round"), "fields": [
            { "select": { "id": "weekend", "label": "Add a round: track", "value": "", "options": spare_opts } },
            { "number": { "id": "at", "label": "As round", "value": last, "min": first, "max": last, "step": 1, "help": format!("{first} to {last}; later rounds move back one") } } ] } }),
        json!({ "form": { "id": "cal_set", "columns": 2, "apply": "Replace track", "disabled": !(can_edit && any && live.can("season.calendar.set_round")), "fields": [
            { "select": { "id": "round", "label": "Replace the track of", "value": "", "options": round_opts } },
            { "select": { "id": "weekend", "label": "With", "value": "", "options": spare_opts } } ] } }),
        json!({ "form": { "id": "cal_remove", "columns": 2, "apply": "Remove round", "disabled": !(can_remove && can_edit && live.can("season.calendar.remove_round")), "fields": [
            { "select": { "id": "round", "label": "Remove", "value": "", "options": round_opts } } ] } }),
    ];
    if let Some(m) = modded_line {
        blocks.insert(0, text(&m, "neutral"));
    }
    blocks.push(text(
        "Only rounds after this weekend that haven't started can change. A track already on this calendar isn't offered again. Sprint format can't be changed live.",
        "muted",
    ));
    blocks.push(text(&count_note(stops, rounds.len()), "muted"));
    for n in notes {
        blocks.push(text(&n, "muted"));
    }
    card_full(
        "Add, replace or remove rounds",
        None,
        vec![pill("Experimental", "warn"), pill("Undo available", "good")],
        vec![],
        blocks,
    )
}

fn drivers(app: &App, live: &Live) -> Value {
    let mut blocks = Vec::new();
    let Some(d) = live.section("drivers") else {
        blocks.push(unavailable(
            "Driver ratings",
            if live.career() {
                "Not available in this mode or build."
            } else {
                "Load a career to see the grid."
            },
        ));
        return frame(app, live, page_sub(live), blocks, false);
    };
    if d["available"].as_bool() == Some(false) {
        blocks.push(unavailable(
            "Driver ratings",
            &format!(
                "Not ready: {}",
                d["reason"]
                    .as_str()
                    .unwrap_or("the ratings component isn't loaded")
            ),
        ));
        return frame(app, live, season_line(live), blocks, false);
    }
    let list = d["drivers"].as_array().cloned().unwrap_or_default();
    let mut stat_names: Vec<String> = d["stats"]
        .as_array()
        .into_iter()
        .flatten()
        .filter_map(|s| s["name"].as_str().map(str::to_owned))
        .collect();
    if stat_names.is_empty() {
        stat_names = STATS.iter().map(|s| (*s).to_owned()).collect();
    }
    stat_names.sort_by_key(|n| STATS.iter().position(|s| s == n).unwrap_or(99));
    let best = list.iter().max_by_key(|x| int(&x["overall"]).unwrap_or(0));
    let player = list.iter().find(|x| x["player"] == true);
    let mut tiles = vec![json!({ "label": "Drivers", "value": list.len().to_string(), "icon": "users" })];
    if let Some(p) = player {
        tiles.push(json!({ "label": "You", "value": int(&p["overall"]).unwrap_or(0).to_string(), "hint": title_case(p["name"].as_str().unwrap_or("")), "icon": "user", "tone": "accent" }));
    }
    if let Some(b) = best {
        tiles.push(json!({ "label": "Top rated", "value": int(&b["overall"]).unwrap_or(0).to_string(), "hint": title_case(b["name"].as_str().unwrap_or("")), "icon": "trophy" }));
    }
    let patched = d["patch"]["usesPatchedCurves"].as_bool().unwrap_or(false);
    tiles.push(json!({ "label": "Ratings curves", "value": if patched { "Patched" } else { "Base" }, "hint": format!("career progress {:.0}%", d["progress"].as_f64().unwrap_or(0.0) * 100.0), "icon": "trending-up" }));
    blocks.push(json!({ "tiles": tiles }));

    let op = "drivers.ratings.set";
    let editable = live.can(op);
    let numbers = live.can("drivers.number.set");
    let mode = app.store.pref_str("rating_mode", "flat");
    let mut columns = vec![
        json!({ "key": "driver", "title": "Driver", "sort": true }),
        json!({ "key": "team", "title": "Team", "sort": true, "width": 150 }),
        json!({ "key": "num", "title": "#", "align": "right", "width": 48, "sort": true }),
        json!({ "key": "overall", "title": "Overall", "width": 130, "sort": true }),
    ];
    for s in &stat_names {
        columns
            .push(json!({ "key": s, "title": title_case(s), "align": "right", "width": 104, "sort": true }));
    }
    let rows: Vec<Value> = list
        .iter()
        .map(|x| {
            let ov = int(&x["overall"]).unwrap_or(0);
            let me = x["player"] == true;
            let name = title_case(x["name"].as_str().unwrap_or("driver"));
            let mut cells = vec![
                json!({ "text": if me { format!("{name} (you)") } else { name } }),
                json!({ "text": x["team"]["name"].as_str().map_or_else(|| "—".into(), pretty_team),
                        "tone": if x["seat"]["hasCar"] == true { "neutral" } else { "muted" } }),
                number_cell(x, numbers),
                json!({ "text": ov.to_string(), "bar": ov as f64 / 100.0, "tone": if ov >= 90 { "good" } else if ov >= 80 { "accent" } else { "neutral" }, "sort": ov }),
            ];
            for s in &stat_names {
                let r = &x["ratings"][s];
                let v = int(&r["value"]);
                let mut c = json!({ "text": v.map_or("—".into(), |v| v.to_string()), "sort": v.unwrap_or(-1) });
                if editable && v.is_some() {
                    c["edit"] = json!({ "number": { "value": v, "min": r["min"].as_f64().unwrap_or(0.0), "max": r["max"].as_f64().unwrap_or(100.0), "step": 1 } });
                }
                if r["boost"].as_f64().unwrap_or(0.0) != 0.0 {
                    c["tone"] = json!("accent");
                }
                cells.push(c);
            }
            let mut row = json!({ "id": x["driverStatId"].to_string(), "cells": cells });
            if me {
                row["tone"] = json!("accent");
            }
            row
        })
        .collect();
    let mode_form = json!({ "form": { "id": "rating_mode", "live": true, "disabled": !editable, "fields": [
        { "select": { "id": "mode", "label": "How an edit changes the rating", "value": mode, "options": [
            { "value": "flat", "label": "Flat: hold the new value for the rest of the career" },
            { "value": "shift", "label": "Shift: move the whole curve, keep growth and decline" } ] } }
    ] } });
    let mut rb = vec![mode_form];
    rb.push(json!({ "table": { "id": "ratings", "columns": columns, "rows": rows, "sort": { "column": "overall", "desc": true },
                               "search": { "placeholder": "Search drivers" }, "compact": true, "empty": "No drivers match" } }));
    rb.push(text(
        &match form_help(live, op) {
            Some(h) => h,
            None => "Click a rating to edit it. The game's own rating getter reads the result back, and History can undo it.".into(),
        },
        "muted",
    ));
    blocks.push(card_full(
        "Ratings",
        Some("Every driver on the grid, AI and you"),
        vec![pill("Undo available", "good")],
        vec![],
        rb,
    ));
    blocks.push(numbers_card(live));
    blocks.push(grid(json!([1, 1]), vec![swap_card(live), moves_card(live)]));
    blocks.push(contracts_card(live));
    if let Some(c) = free_agent_card(live) {
        blocks.push(c);
    }
    frame(app, live, season_line(live), blocks, false)
}

fn state_tone(state: i64) -> &'static str {
    match state {
        3 => "good",
        2 | 7 => "accent",
        1 | 6 | 8 => "neutral",
        4 | 5 => "bad",
        _ => "muted",
    }
}

fn state_label(state: i64) -> &'static str {
    match state {
        0 => "Locked",
        1 => "Available",
        2 => "Researching",
        3 => "Fitted",
        4 => "Failed",
        5 => "Invalid",
        6 => "Researched",
        7 => "Developing",
        8 => "Ready to fit",
        _ => "?",
    }
}

fn rnd_team_data(app: &App, live: &Live) -> (Value, bool) {
    let Some(r) = live.section("rnd") else {
        return (Value::Null, false);
    };
    let chosen = app.store.prefs["rnd_team"].as_i64();
    let player = &r["player"];
    if (chosen.is_none() || chosen == player["team"].as_i64()) && player.is_object() {
        return (player.clone(), true);
    }
    let summary = r["teams"]
        .as_array()
        .and_then(|a| a.iter().find(|t| t["team"].as_i64() == chosen))
        .cloned();
    let detail = app
        .store
        .history
        .iter()
        .rev()
        .find(|e| {
            e.ok && e.op == "rnd.team_detail" && e.after["team"].as_i64() == chosen && in_scope(live, e)
        })
        .map(|e| e.after.clone());
    match (detail, summary) {
        (Some(d), _) => (d, true),
        (None, Some(s)) => (s, false),
        (None, None) if player.is_object() => (player.clone(), true),
        _ => (Value::Null, false),
    }
}

fn rnd(app: &App, live: &Live) -> Value {
    let mut blocks = Vec::new();
    let Some(r) = live
        .section("rnd")
        .filter(|r| r["ready"].as_bool() != Some(false))
    else {
        let why = live
            .section("rnd")
            .and_then(|r| r["reason"].as_str())
            .map(|w| format!("Not ready: {w}"))
            .unwrap_or_else(|| {
                if live.career() {
                    "Not available in this mode or build.".into()
                } else {
                    "Load a career to see R&D.".into()
                }
            });
        blocks.push(unavailable("Research & development", &why));
        return frame(app, live, page_sub(live), blocks, false);
    };
    let teams = r["teams"].as_array().cloned().unwrap_or_default();
    let (team, full) = rnd_team_data(app, live);
    let team_key = team["team"].as_i64().unwrap_or(0);
    let is_player = team["player"] == true;
    let mut options: Vec<Value> = teams
        .iter()
        .map(|t| {
            let id = t["team"].as_i64().unwrap_or(0);
            let label = if t["player"] == true {
                "Your team".to_owned()
            } else {
                format!(
                    "{} · {:.0}% fitted",
                    team_label(live, t["team_stat_id"].as_i64().unwrap_or(id >> 16)),
                    t["fitted_ratio"].as_f64().unwrap_or(0.0) * 100.0
                )
            };
            json!({ "value": id.to_string(), "label": label })
        })
        .collect();
    if options.is_empty() && r["player"].is_object() {
        options.push(json!({ "value": r["player"]["team"].to_string(), "label": "Your team" }));
    }
    let c = &team["counts"];
    let n = |k: &str| int(&c[k]).unwrap_or(0);
    let total = n("total").max(1);
    blocks.push(json!({ "tiles": [
        { "label": "Fitted", "value": n("fitted").to_string(), "hint": format!("of {total}"), "progress": n("fitted") as f64 / total as f64, "tone": "good", "icon": "circle-check" },
        { "label": "In progress", "value": (n("researching") + n("developing")).to_string(), "hint": format!("{} researching · {} developing", n("researching"), n("developing")), "icon": "timer", "tone": "accent" },
        { "label": "Ready", "value": (n("available") + n("researched") + n("pending_fitting")).to_string(), "hint": "available, researched or ready to fit", "icon": "zap" },
        { "label": "Locked", "value": (n("unavailable") + n("locked")).to_string(), "hint": format!("{} facility-locked", n("locked")), "icon": "lock", "tone": "muted" },
    ] }));
    let meters: Vec<Value> = team["depts"]
        .as_array()
        .into_iter()
        .flatten()
        .map(|d| {
            let t = int(&d["t"]).unwrap_or(0).max(1);
            let f = int(&d["f"]).unwrap_or(0);
            let name = DEPTS.get(int(&d["d"]).unwrap_or(0) as usize).copied().unwrap_or("?");
            json!({ "label": name, "value": f, "max": t,
                    "text": format!("{f}/{t} fitted · {} in progress · {} available", int(&d["p"]).unwrap_or(0), int(&d["a"]).unwrap_or(0)), "tone": "good" })
        })
        .collect();
    let has_tyres = team["depts"]
        .as_array()
        .is_some_and(|a| a.iter().any(|d| int(&d["d"]) == Some(4)));
    let mut dept_opts = vec![json!({ "value": "-1", "label": "Every department" })];
    for (i, name) in DEPTS.iter().enumerate() {
        if i < 4 || has_tyres {
            dept_opts.push(json!({ "value": i.to_string(), "label": *name }));
        }
    }
    let can_bulk = live.can("rnd.complete");
    let bulk = json!({ "form": { "id": "rnd_bulk", "columns": 3, "apply": "Run", "disabled": !can_bulk, "fields": [
        { "select": { "id": "dept", "label": "Department", "value": "-1", "options": dept_opts } },
        { "select": { "id": "action", "label": "Action", "value": "", "options": [
            { "value": "", "label": "Choose an action…" },
            { "value": "complete", "label": "Complete instantly (free)" },
            { "value": "unlock", "label": "Unlock facility locks" },
            { "value": "reset", "label": "Reset to the start" } ] } },
        { "select": { "id": "stage", "label": "Complete up to", "value": "full", "options": [
            { "value": "full", "label": "Fitted" }, { "value": "develop", "label": "Developed" }, { "value": "research", "label": "Researched" } ] } }
    ] } });
    let team_form = json!({ "form": { "id": "rnd_team", "live": true, "fields": [
        { "select": { "id": "team", "label": "Team", "value": team_key.to_string(), "options": options } } ] } });
    let mut left = vec![team_form, bulk];
    if let Some(h) = form_help(live, "rnd.complete") {
        left.push(text(&h, "muted"));
    }
    left.push(text(
        "Complete, unlock and reset can only be undone by restoring a backup.",
        "muted",
    ));
    let lifecycle = if r["lifecycle"] == "research_only" {
        "Research only"
    } else {
        "Research, develop, fit"
    };
    blocks.push(grid(
        json!([3, 2]),
        vec![
            card_full(
                "Bulk actions",
                Some(&format!("{total} upgrades · {lifecycle}")),
                vec![pill("Backup-only undo", "warn")],
                vec![],
                left,
            ),
            card(
                "Departments",
                if meters.is_empty() {
                    vec![text("No departments listed.", "muted")]
                } else {
                    vec![json!({ "meters": meters })]
                },
            ),
        ],
    ));

    if !full {
        let mut b = vec![text(
            "The snapshot carries only a summary for AI teams. Load the full tree to see and edit single upgrades.",
            "muted",
        )];
        let mut buttons = Vec::new();
        if live.can("rnd.team_detail") {
            buttons.push(button(
                &format!("rnd_detail:{team_key}"),
                "Load full tree",
                "refresh",
                "primary",
            ));
        }
        if buttons.is_empty() {
            b.push(text(
                "Loading an AI team's tree isn't available in this build.",
                "muted",
            ));
        }
        blocks.push(card_full("Upgrades", None, vec![], buttons, b));
        return frame(app, live, page_sub(live), blocks, false);
    }
    let day = r["day"].as_f64().unwrap_or(0.0);
    let ups = team["u"].as_array().cloned().unwrap_or_default();
    let rows: Vec<Value> = ups
        .iter()
        .map(|u| {
            let st = int(&u["s"]).unwrap_or(0);
            let locked = int(&u["l"]) == Some(1);
            let mut label = if locked { "Facility locked".to_owned() } else { state_label(st).to_owned() };
            if let (Some(b), Some(e)) = (u["b"].as_f64(), u["e"].as_f64())
                && e > b {
                    label = format!("{label} {:.0}%", ((day - b) / (e - b) * 100.0).clamp(0.0, 100.0));
                }
            let why = match u["r"].as_str() {
                Some("prerequisites") => "needs earlier upgrades".to_owned(),
                Some("facility_locked") => format!("facility level {}", int(&u["ul"]).unwrap_or(0)),
                Some("adaptation_pending") => "adaptation pending".to_owned(),
                Some(other) => other.to_owned(),
                None => String::new(),
            };
            let dept = int(&u["d"]).unwrap_or(0) as usize;
            json!({ "id": u["h"].to_string(), "cells": [
                title_case(u["n"].as_str().unwrap_or("upgrade")),
                DEPTS.get(dept).copied().unwrap_or("?"),
                { "text": label, "pill": true, "tone": if locked { "muted" } else { state_tone(st) }, "sort": st },
                { "text": why, "tone": "muted" },
                { "text": group(int(&u["c"]).unwrap_or(0)), "sort": int(&u["c"]).unwrap_or(0) },
                { "text": int(&u["dc"]).map_or("—".into(), group), "sort": int(&u["dc"]).unwrap_or(0) },
                { "text": match int(&u["dt"]) { Some(dt) => format!("{} + {dt} d", int(&u["t"]).unwrap_or(0)), None => format!("{} d", int(&u["t"]).unwrap_or(0)) }, "sort": int(&u["t"]).unwrap_or(0) }
            ] })
        })
        .collect();
    let mut up_opts = vec![json!({ "value": "", "label": "Choose an upgrade…" })];
    up_opts.extend(ups.iter().map(|u| json!({ "value": u["h"].to_string(),
        "label": format!("{} · {}", title_case(u["n"].as_str().unwrap_or("upgrade")), state_label(int(&u["s"]).unwrap_or(0))) })));
    let mut one = Vec::new();
    if up_opts.len() > 1 {
        one.push(json!({ "form": { "id": "rnd_one", "columns": 2, "apply": "Run", "disabled": !can_bulk, "fields": [
            { "select": { "id": "upgrade", "label": "Upgrade", "value": "", "options": up_opts } },
            { "select": { "id": "action", "label": "Action", "value": "complete", "options": [
                { "value": "complete", "label": "Complete instantly" },
                { "value": "unlock", "label": "Unlock" },
                { "value": "refund", "label": "Cancel and refund RP" },
                { "value": "fit", "label": "Fit to the car" },
                { "value": "unfit", "label": "Take off the car" },
                { "value": "reset", "label": "Reset" } ] } }
        ] } }));
    }
    one.push(json!({ "table": { "id": "upgrades", "columns": [
        { "key": "name", "title": "Upgrade", "sort": true }, { "key": "dept", "title": "Department", "sort": true, "width": 120 },
        { "key": "state", "title": "State", "sort": true, "width": 150 }, { "key": "why", "title": "Blocked by", "width": 170 },
        { "key": "rc", "title": "Research RP", "align": "right", "sort": true, "width": 110 },
        { "key": "dc", "title": "Develop RP", "align": "right", "sort": true, "width": 110 },
        { "key": "days", "title": "Days", "align": "right", "width": 90 }
    ], "rows": rows, "search": { "placeholder": "Search upgrades" }, "compact": true, "empty": "No upgrades match" } }));
    let title = if is_player {
        "Your upgrades".to_owned()
    } else {
        format!(
            "{} upgrades",
            team_label(live, team["team_stat_id"].as_i64().unwrap_or(0))
        )
    };
    blocks.push(card_full(
        &title,
        Some("Costs and days are the gamedb base before team modifiers"),
        vec![],
        vec![],
        one,
    ));
    frame(app, live, season_line(live), blocks, false)
}

// ---- season -------------------------------------------------------------------------------------

fn season(app: &App, live: &Live) -> Value {
    let mut blocks = Vec::new();
    let names = driver_names(live);
    let s = live
        .section("season")
        .filter(|s| s["ready"].as_bool() != Some(false));
    let st = live
        .section("standings")
        .filter(|s| s["ready"].as_bool() != Some(false));
    let cal = live
        .section("calendar")
        .filter(|s| s["ready"].as_bool() != Some(false));
    if s.is_none() && st.is_none() && cal.is_none() {
        blocks.push(unavailable(
            "Season",
            if live.career() {
                "Not available in this mode or build."
            } else {
                "Load a career to see the season."
            },
        ));
        return frame(app, live, page_sub(live), blocks, false);
    }
    let mut tiles = Vec::new();
    if let Some(s) = s {
        let r = int(&s["round"]).unwrap_or(0);
        let n = int(&s["rounds"]).unwrap_or(0).max(1);
        tiles.push(json!({ "label": "Round", "value": format!("{r} of {n}"), "hint": s["weekend"].as_str().map(track_name).unwrap_or_default(), "progress": r as f64 / n as f64, "icon": "flag" }));
        tiles.push(json!({ "label": "Season", "value": int(&s["season_number"]).unwrap_or(1).to_string(),
                           "hint": int(&s["year"]).map_or_else(|| s["name"].as_str().unwrap_or("").to_owned(), |y| y.to_string()), "icon": "calendar" }));
    }
    let drivers_rows = st
        .and_then(|x| x["drivers"].as_array())
        .cloned()
        .unwrap_or_default();
    let mut sorted = drivers_rows.clone();
    sorted.sort_by_key(|d| int(&d["position"]).unwrap_or(99));
    if let Some(l) = sorted.first() {
        tiles.push(json!({ "label": "Leader", "value": driver_name(&names, int(&l["driver_stat_id"]).unwrap_or(-1)), "hint": format!("{} pts", int(&l["points"]).unwrap_or(0)), "icon": "trophy" }));
    }
    let player_id = names.iter().find(|n| n.3).map(|n| n.0);
    if let Some(p) = sorted
        .iter()
        .find(|d| player_id.is_some() && int(&d["driver_stat_id"]) == player_id)
    {
        tiles.push(json!({ "label": "You", "value": format!("P{}", int(&p["position"]).unwrap_or(0)), "hint": format!("{} pts · {} wins", int(&p["points"]).unwrap_or(0), int(&p["wins"]).unwrap_or(0)), "icon": "user", "tone": "accent" }));
    }
    if !tiles.is_empty() {
        blocks.push(json!({ "tiles": tiles }));
    }

    if let Some(st) = st {
        let drows: Vec<Value> = sorted
            .iter()
            .map(|d| {
                let id = int(&d["driver_stat_id"]).unwrap_or(-1);
                let me = Some(id) == player_id;
                let mut row = json!({ "id": id.to_string(), "cells": [
                    { "text": format!("P{}", int(&d["position"]).unwrap_or(0)), "sort": int(&d["position"]).unwrap_or(99) },
                    driver_name(&names, id),
                    { "text": int(&d["team_stat_id"]).map(|t| team_label(live, t)).unwrap_or_else(|| "—".into()), "tone": "muted" },
                    { "text": int(&d["points"]).unwrap_or(0).to_string(), "sort": int(&d["points"]).unwrap_or(0) },
                    { "text": int(&d["wins"]).unwrap_or(0).to_string(), "sort": int(&d["wins"]).unwrap_or(0) },
                    { "text": int(&d["podiums"]).unwrap_or(0).to_string(), "sort": int(&d["podiums"]).unwrap_or(0) }
                ] });
                if me {
                    row["tone"] = json!("accent");
                }
                row
            })
            .collect();
        let mut teams = st["teams"].as_array().cloned().unwrap_or_default();
        teams.sort_by_key(|t| int(&t["position"]).unwrap_or(99));
        let trows: Vec<Value> = teams
            .iter()
            .map(|t| json!({ "cells": [
                { "text": format!("P{}", int(&t["position"]).unwrap_or(0)), "sort": int(&t["position"]).unwrap_or(99) },
                team_label(live, int(&t["team_stat_id"]).unwrap_or(0)),
                { "text": int(&t["points"]).unwrap_or(0).to_string(), "sort": int(&t["points"]).unwrap_or(0) },
                { "text": int(&t["wins"]).unwrap_or(0).to_string(), "sort": int(&t["wins"]).unwrap_or(0) }
            ] }))
            .collect();
        let num =
            |k: &str, t: &str| json!({ "key": k, "title": t, "align": "right", "sort": true, "width": 70 });
        blocks.push(grid(json!([3, 2]), vec![
            card("Drivers' championship", vec![json!({ "table": { "id": "standings", "columns": [
                { "key": "pos", "title": "Pos", "sort": true, "width": 60 }, { "key": "driver", "title": "Driver", "sort": true }, "Team",
                num("pts", "Pts"), num("wins", "Wins"), num("pod", "Podiums") ], "rows": drows, "search": { "placeholder": "Search drivers" }, "compact": true } })]),
            card("Constructors", vec![json!({ "table": { "id": "teams", "columns": [
                { "key": "pos", "title": "Pos", "sort": true, "width": 60 }, "Team", num("pts", "Pts"), num("wins", "Wins") ], "rows": trows, "compact": true } })]),
        ]));
    }

    if let Some(cal) = cal {
        let prof = profile(app);
        let rounds = cal["rounds"].as_array().cloned().unwrap_or_default();
        let rows: Vec<Value> = rounds
            .iter()
            .map(|r| {
                let (txt, tone) = if r["current"] == true {
                    ("This weekend", "accent")
                } else if r["completed"] == true {
                    ("Done", "muted")
                } else if r["editable"] == true {
                    ("Upcoming", "neutral")
                } else {
                    ("Started", "muted")
                };
                let t = crate::calendar::round_track(r, prof);
                let name = t.as_ref().map_or_else(|| "Unknown track".to_owned(), |t| t.name.clone());
                let country = match &t {
                    Some(t) if t.modded => json!({ "text": "Modded track", "tone": "accent" }),
                    Some(t) => json!({ "text": t.country.clone().unwrap_or_else(|| "—".into()), "tone": "muted" }),
                    None => json!({ "text": "—", "tone": "muted" }),
                };
                let mut row = json!({ "cells": [
                    { "text": int(&r["round"]).unwrap_or(0).to_string(), "sort": int(&r["round"]).unwrap_or(0) },
                    name,
                    country,
                    { "text": txt, "pill": true, "tone": tone },
                    match r["sprint"].as_bool() { Some(true) => json!({ "text": "Sprint", "tone": "accent" }), Some(false) => json!("—"), None => json!({ "text": "—", "tone": "muted" }) }
                ] });
                if r["current"] == true {
                    row["tone"] = json!("accent");
                }
                row
            })
            .collect();
        let mut b = vec![json!({ "table": { "id": "calendar", "columns": [
            { "key": "rd", "title": "Rd", "width": 56, "sort": true }, "Track", { "key": "country", "title": "Country", "width": 170 },
            { "key": "status", "title": "Status", "width": 140 }, { "key": "format", "title": "Format", "width": 90 } ],
            "rows": rows, "compact": true } })];
        if crate::calendar::index_named(&rounds) && rounds.iter().all(|r| r["track_stat_id"].is_null()) {
            b.push(text("This career names its rounds by number, so the track of each round shows once the Career Editor addon is updated.", "warn"));
        }
        if let Some(c) = calendar_count_card(live, cal) {
            blocks.push(c);
        }
        blocks.push(card("Calendar", b));
        blocks.push(move_card(app, live, cal));
        blocks.push(calendar_ops_card(app, live, cal));
    }
    if st.is_some() {
        blocks.push(points_card(live, &names));
    }
    blocks.push(card_full(
        "Race results and sprint format",
        None,
        vec![pill("Not in this build", "muted")],
        vec![],
        vec![text("Editing a session result and switching a round's sprint format aren't available live yet; points adjustments above change the standings instead.", "muted")],
    ));
    frame(app, live, season_line(live), blocks, false)
}

// ---- weather ------------------------------------------------------------------------------------

fn wstate_label(s: &str) -> String {
    W_STATES
        .iter()
        .find(|x| x.0 == s)
        .map_or_else(|| title_case(s), |x| x.1.to_owned())
}

fn weather(app: &App, live: &Live) -> Value {
    let mut blocks = Vec::new();
    let Some(w) = live
        .section("weather")
        .filter(|w| w["available"].as_bool() != Some(false))
    else {
        blocks.push(unavailable(
            "Weather",
            if live.career() {
                "Not available in this mode or build."
            } else {
                "Load a career to plan the weather."
            },
        ));
        return frame(app, live, page_sub(live), blocks, true);
    };
    let weekend = opt(&w["default_weekend"]).or_else(|| opt(&w["weekend"]));
    let seed = &w["seed"];
    let locked = seed["locked"].as_bool().unwrap_or(false);
    let plans = w["plans"].as_array().map_or(0, Vec::len);
    let active = int(&w["active_dataset"]);
    let active_name = active
        .and_then(|a| W_SESSIONS.get(a as usize))
        .map_or("none".into(), |s| s.1.to_owned());
    blocks.push(json!({ "tiles": [
        { "label": "Weekend", "value": weekend.clone().unwrap_or_else(|| "—".into()), "hint": "weather weekend index", "icon": "calendar" },
        { "label": "Live session", "value": active_name, "icon": "timer" },
        { "label": "Seed", "value": if locked { "Pinned" } else { "Game's" }, "hint": opt(&seed["base"]).unwrap_or_default(), "icon": "refresh", "tone": if locked { "accent" } else { "neutral" } },
        { "label": "Plans stored", "value": plans.to_string(), "hint": "weekends with a plan", "icon": "cloud-rain" },
    ] }));

    let rows: Vec<Value> = W_SESSIONS
        .iter()
        .map(|(key, label)| {
            let s = &w["sessions"][key];
            let states: Vec<String> = s["states"]
                .as_array()
                .into_iter()
                .flatten()
                .take(6)
                .map(|x| format!("{} → {:.0}%", wstate_label(x["state"].as_str().unwrap_or("?")), x["end"].as_f64().unwrap_or(0.0) * 100.0))
                .collect();
            let not_applied = s["not_applied"].as_str().map(str::to_owned);
            let (txt, tone) = if s["locked"] == true {
                ("Locked", "muted")
            } else if not_applied.is_some() {
                ("Planned · not applied", "warn")
            } else if s["rebuilt"] == true {
                ("Planned · live", "good")
            } else if s["planned"] == true {
                ("Planned", "accent")
            } else if s["active"] == true {
                ("Game forecast", "neutral")
            } else {
                ("Not built", "muted")
            };
            json!({ "id": *key, "cells": [
                *label,
                { "text": txt, "pill": true, "tone": tone },
                { "text": s["length_s"].as_f64().map_or("—".into(), |l| format!("{:.0} min", l / 60.0)), "sort": s["length_s"].as_f64().unwrap_or(0.0) },
                match &not_applied {
                    Some(why) => json!({ "text": why, "tone": "warn" }),
                    None => json!({ "text": if states.is_empty() { "—".into() } else { states.join(" · ") }, "tone": if s["planned"] == true { "accent" } else { "neutral" } }),
                }
            ] })
        })
        .collect();
    blocks.push(card("This weekend's forecast", vec![json!({ "table": { "id": "wsessions", "columns": [
        { "key": "session", "title": "Session", "width": 150 }, { "key": "status", "title": "Status", "width": 150 },
        { "key": "len", "title": "Length", "align": "right", "width": 90 }, "Forecast (state → until)" ], "rows": rows, "compact": true } })]));

    let op = "weather.plan.set";
    let can = live.can(op);
    let state_opts: Vec<Value> = W_STATES
        .iter()
        .map(|(k, l)| json!({ "value": k, "label": l }))
        .collect();
    let mut start_opts = vec![json!({ "value": "", "label": "Game's own" })];
    start_opts.extend(state_opts.iter().cloned());
    let mut session_opts = vec![json!({ "value": "", "label": "Choose a session…" })];
    session_opts.extend(W_SESSIONS.iter().map(|(k, l)| json!({ "value": k, "label": l })));
    let mut fields = vec![
        json!({ "select": { "id": "session", "label": "Session", "value": "", "options": session_opts } }),
        json!({ "select": { "id": "start", "label": "Starts", "value": "", "options": start_opts } }),
        json!({ "number": { "id": "laps", "label": "Race laps", "value": 0, "min": 0, "max": 200, "step": 1, "help": "Needed for lap-based changes" } }),
    ];
    let defaults = [
        (15, "light_rain", 50),
        (25, "heavy_rain", 85),
        (40, "overcast", 0),
    ];
    let kinds = [
        json!({ "value": "off", "label": "Not used" }),
        json!({ "value": "lap", "label": "At lap" }),
        json!({ "value": "minute", "label": "At minute" }),
        json!({ "value": "percent", "label": "At % of the session" }),
    ];
    for (i, (at, state, _)) in defaults.iter().enumerate() {
        let n = i + 1;
        fields.push(json!({ "select": { "id": format!("p{n}_kind"), "label": format!("Change {n}"), "value": if n == 1 { "lap" } else { "off" }, "options": kinds } }));
        fields.push(json!({ "number": { "id": format!("p{n}_at"), "label": "When", "value": at, "min": 0, "max": 240, "step": 1 } }));
        fields.push(json!({ "select": { "id": format!("p{n}_state"), "label": "Weather", "value": state, "options": state_opts } }));
    }
    for (i, (_, _, intensity)) in defaults.iter().enumerate() {
        let n = i + 1;
        fields.push(json!({ "slider": { "id": format!("p{n}_int"), "label": format!("Change {n} intensity"), "value": intensity, "min": 0, "max": 100, "step": 5, "unit": "%", "help": "0% leaves it to the game" } }));
    }
    fields.push(
        json!({ "toggle": { "id": "replace", "label": "Replace the weekend's whole plan", "value": false } }),
    );
    let mut pb = vec![
        json!({ "form": { "id": "weather_plan", "columns": 3, "apply": "Save plan", "disabled": !can, "fields": fields } }),
    ];
    pb.push(text(
        &form_help(live, op).unwrap_or_else(|| "Plans apply the next time the weekend hub builds the forecast, or at once for the live weekend. Sessions that have started are locked.".into()),
        "muted",
    ));
    let mut buttons = Vec::new();
    if live.can("weather.plan.clear") {
        buttons.push(confirm(
            button("weather_clear", "Clear plan", "eraser", "outline"),
            "Clear the weekend's weather plan?",
            "Every session goes back to the game's own forecast at the next forecast build. A session that has started keeps its weather until it's loaded again.",
        ));
    }
    blocks.push(card_full(
        "Script a session",
        Some(&format!(
            "Weekend {}",
            weekend.clone().unwrap_or_else(|| "next".into())
        )),
        vec![pill("Works in the weekend hub", "good")],
        buttons,
        pb,
    ));

    let mut seed_buttons = Vec::new();
    if live.can("weather.seed.lock") {
        seed_buttons.push(button("seed_lock", "Pin this seed", "lock", "outline"));
    }
    if live.can("weather.seed.reroll") {
        seed_buttons.push(button("seed_reroll", "Reroll", "refresh", "outline"));
    }
    if live.can("weather.seed.unlock") && locked {
        seed_buttons.push(button("seed_unlock", "Use the game's seed", "undo", "ghost"));
    }
    blocks.push(card_full(
        "Realistic weather seed",
        None,
        vec![pill("Experimental", "warn")],
        seed_buttons,
        vec![
            json!({ "facts": [ fact("Base", opt(&seed["base"]).unwrap_or_else(|| "—".into())), fact("Forced", opt(&seed["forced"]).unwrap_or_else(|| "—".into())),
                               fact("Pinned", opt(&seed["pinned"]).unwrap_or_else(|| "no".into())) ] }),
            text("A pinned or rerolled seed changes the game's own realistic forecast from the next forecast build. Planned sessions keep their plan.", "muted"),
        ],
    ));
    frame(app, live, season_line(live), blocks, true)
}

// ---- history ------------------------------------------------------------------------------------

fn history_columns(full: bool) -> Value {
    let mut c = vec![
        json!({ "key": "time", "title": "When", "width": 96, "sort": true }),
        json!({ "key": "edit", "title": "Edit" }),
        json!({ "key": "change", "title": "Before → after" }),
        json!({ "key": "result", "title": "Result", "width": 110 }),
    ];
    if full {
        c.push(json!({ "key": "undo", "title": "Undo", "width": 120 }));
    }
    json!(c)
}

fn summarize(e: &Entry) -> String {
    let pick = |v: &Value| -> String {
        if let Some(x) = v["expenses"].as_i64() {
            return format!("{} spent", money_short(x));
        }
        if let Some(m) = v["sessions"].as_object() {
            if m.is_empty() {
                return "no sessions".into();
            }
            return m
                .iter()
                .map(|(k, x)| {
                    let n = x["timeline"].as_array().map_or(0, Vec::len);
                    if x.is_null() {
                        format!("{k}: game forecast")
                    } else {
                        format!("{k}: {n} change(s)")
                    }
                })
                .collect::<Vec<_>>()
                .join(", ");
        }
        if let Some(l) = v["locked"].as_bool() {
            return if l {
                format!("seed {} pinned", v["seed"])
            } else {
                "game's seed".into()
            };
        }
        if let Some(c) = v["cash"].as_i64() {
            return money(c);
        }
        if let Some(r) = v["rp"].as_i64() {
            return format!("{} RP", group(r));
        }
        if let Some(r) = v["ratings"].as_object() {
            return r
                .iter()
                .map(|(k, x)| format!("{} {}", title_case(k), x["value"]))
                .collect::<Vec<_>>()
                .join(", ");
        }
        if let Some(u) = v["upgrades"].as_array() {
            return format!("{} upgrade(s)", u.len());
        }
        if let Some(r) = v["renown"].as_f64() {
            return format!("{r:.0} renown");
        }
        if let Some(l) = v["loyalty"].as_i64() {
            return format!("{l} loyalty XP");
        }
        if let Some(x) = v["expenses"].as_i64() {
            return format!("{} spent", money_short(x));
        }
        if v["a"].is_object() && v["b"].is_object() {
            return format!(
                "#{} team {} · #{} team {}",
                v["a"]["driverStatId"], v["a"]["teamStatId"], v["b"]["driverStatId"], v["b"]["teamStatId"]
            );
        }
        if let Some(sal) = v["salary"].as_i64() {
            return format!("salary {}", money_short(sal));
        }
        if let Some(a) = v["adjustment"].as_i64() {
            return format!(
                "{a:+} pts{}",
                if v["pending"] == true { " (pending)" } else { "" }
            );
        }
        if let Some(a) = v["adjustments"].as_array() {
            return format!("{} adjustment(s)", a.len());
        }
        if let Some(r) = v["rounds"].as_array() {
            return format!("{} rounds", r.len());
        }
        short_json(v)
    };
    if e.before.is_null() && e.after.is_null() {
        return "—".into();
    }
    format!("{} → {}", pick(&e.before), pick(&e.after))
}

fn op_label(op: &str) -> String {
    match op {
        "economy.cash.set" => "Set cash".into(),
        "economy.cash.add" => "Add cash".into(),
        "economy.rp.set" => "Set resource points".into(),
        "economy.rp.add" => "Add resource points".into(),
        "drivers.ratings.set" => "Driver ratings".into(),
        "rnd.complete" => "Complete R&D".into(),
        "rnd.unlock" => "Unlock R&D".into(),
        "rnd.reset" => "Reset R&D".into(),
        "rnd.refund" => "Refund R&D".into(),
        "rnd.fit" => "Fit upgrade".into(),
        "facilities.complete" => "Finish facility builds".into(),
        "facilities.set_level" => "Facility level".into(),
        "season.calendar.reorder" => "Reorder calendar".into(),
        "weather.plan.set" => "Weather plan".into(),
        "weather.plan.clear" => "Clear weather plan".into(),
        "weather.seed.lock" => "Pin weather seed".into(),
        "weather.seed.reroll" => "Reroll weather seed".into(),
        "weather.seed.unlock" => "Unpin weather seed".into(),
        "drivers.number.set" => "Race number".into(),
        "bridge.ping" => "Ping".into(),
        "drivers.swap" => "Swap seats".into(),
        "drivers.contract.set" => "Contract".into(),
        "drivers.contracts.repair" => "Sign seat contracts".into(),
        "drivers.freeAgent.add" => "Add free agent".into(),
        "economy.renown.set" => "Set renown".into(),
        "economy.renown.add" => "Add renown".into(),
        "economy.sponsor.loyalty.add" => "Sponsor loyalty".into(),
        "economy.costcap.refund" => "Cost cap refund".into(),
        "season.points.adjust" => "Points adjustment".into(),
        "season.points.clear" => "Clear points adjustments".into(),
        "season.result.set" => "Race result".into(),
        "season.calendar.set_round" => "Replace a round".into(),
        "season.calendar.add_round" => "Add a round".into(),
        "season.calendar.remove_round" => "Remove a round".into(),
        "season.calendar.repair" => "Repair the round count".into(),
        "season.calendar.toggle_sprint" => "Sprint format".into(),
        "batch" => "Batch".into(),
        other => other.to_owned(),
    }
}

fn error_code(e: &Entry) -> String {
    let err = e.error.clone().unwrap_or_else(|| "failed".into());
    err.split_once(':')
        .map_or(err.clone(), |(c, _)| c.trim().to_owned())
}

fn error_detail(e: &Entry) -> String {
    let err = e.error.clone().unwrap_or_else(|| "failed".into());
    match err.split_once(':') {
        Some((_, d)) if !d.trim().is_empty() => d.trim().to_owned(),
        _ => err,
    }
}

fn not_applied(e: &Entry) -> Option<String> {
    match &e.after["not_applied"] {
        Value::Null => None,
        Value::String(s) => Some(s.clone()),
        Value::Object(m) if m.is_empty() => None,
        Value::Object(m) => Some(
            m.iter()
                .map(|(k, v)| format!("{k}: {}", v.as_str().unwrap_or("not applied")))
                .collect::<Vec<_>>()
                .join("; "),
        ),
        other => Some(other.to_string()),
    }
}

fn history_row(app: &App, e: &Entry, full: bool) -> Value {
    let undone = app.store.undone(e.seq, e.i);
    let not_applied = not_applied(e);
    let (rt, rtone) = if !e.ok {
        (error_code(e), "bad")
    } else if not_applied.is_some() {
        ("Stored, not applied".into(), "warn")
    } else if e.undo_of.is_some() {
        ("Undo applied".into(), "accent")
    } else if undone {
        ("Undone".into(), "muted")
    } else {
        ("Applied".into(), "good")
    };
    let mut cells = vec![
        json!({ "text": clock(e.t), "sort": e.t }),
        json!(if e.label.is_empty() {
            op_label(&e.op)
        } else {
            e.label.clone()
        }),
        json!({ "text": if e.ok { not_applied.map_or_else(|| summarize(e), |w| format!("{} ({w})", summarize(e))) } else { error_detail(e) },
                "tone": if e.ok { "neutral" } else { "bad" } }),
        json!({ "text": rt, "pill": true, "tone": rtone }),
    ];
    if full {
        let u = match inverse(e) {
            _ if undone => json!({ "text": "undone", "tone": "muted" }),
            _ if e.undo_of.is_some() => json!({ "text": "—", "tone": "muted" }),
            Undo::Op(_) | Undo::Ops(_) => json!({ "text": "available", "tone": "good" }),
            Undo::BackupOnly => json!({ "text": "backup only", "tone": "warn" }),
            Undo::None(_) => json!({ "text": "—", "tone": "muted" }),
        };
        cells.push(u);
    }
    json!({ "id": format!("{}:{}", e.seq, e.i), "cells": cells })
}

fn in_scope(live: &Live, e: &Entry) -> bool {
    match (live.career_id(), &e.career) {
        (Some(now), Some(then)) => &now == then,
        _ => true,
    }
}

fn undoable<'a>(app: &'a App, live: &Live) -> Vec<&'a Entry> {
    app.store
        .history
        .iter()
        .rev()
        .filter(|e| {
            e.ok && in_scope(live, e)
                && e.undo_of.is_none()
                && !app.store.undone(e.seq, e.i)
                && matches!(inverse(e), Undo::Op(_) | Undo::Ops(_))
        })
        .take(40)
        .collect()
}

fn history(app: &App, live: &Live) -> Value {
    let mut blocks = Vec::new();
    let ok = app
        .store
        .history
        .iter()
        .filter(|e| e.ok && e.op != "bridge.ping" && e.op != "rnd.team_detail" && in_scope(live, e))
        .count();
    let failed = app
        .store
        .history
        .iter()
        .filter(|e| !e.ok && in_scope(live, e))
        .count();
    let pending = app.store.pending();
    let backups = app.store.backups();
    blocks.push(json!({ "tiles": [
        { "label": "Edits applied", "value": ok.to_string(), "icon": "circle-check", "tone": "good" },
        { "label": "Failed", "value": failed.to_string(), "icon": "triangle-alert", "tone": if failed > 0 { "bad" } else { "neutral" } },
        { "label": "Queued", "value": pending.len().to_string(), "icon": "clock", "tone": if pending.is_empty() { "neutral" } else { "accent" } },
        { "label": "Backups", "value": backups.len().to_string(), "hint": backups.first().map(|b| format!("last {}", ago(b.created))).unwrap_or_else(|| "none yet".into()), "icon": "save" },
    ] }));

    if !pending.is_empty() {
        let why = if live.live() {
            live.snap["bridge"]["blocked"]["why"]
                .as_str()
                .unwrap_or("its turn")
        } else {
            "the game to start"
        };
        let rows: Vec<Value> = pending
            .iter()
            .map(|p| json!({ "cells": [ clock(p.seq), p.label.clone(), { "text": format!("{} op(s)", p.ops), "tone": "muted" }, { "text": why, "tone": "warn" } ] }))
            .collect();
        blocks.push(card_full("Queued edits", None, vec![], vec![button("withdraw", "Withdraw all", "trash", "danger")], vec![
            json!({ "table": { "id": "pending", "columns": ["Sent", "Edit", "Ops", "Waiting for"], "rows": rows, "compact": true } }),
        ]));
    }

    let rows: Vec<Value> = app
        .store
        .history
        .iter()
        .rev()
        .filter(|e| e.op != "bridge.ping" && e.op != "rnd.team_detail" && in_scope(live, e))
        .take(200)
        .map(|e| history_row(app, e, true))
        .collect();
    let undo_list = undoable(app, live);
    let mut hb = Vec::new();
    if !undo_list.is_empty() {
        let mut opts = vec![json!({ "value": "", "label": "Choose an edit…" })];
        opts.extend(undo_list
            .iter()
            .map(|e| json!({ "value": format!("{}:{}", e.seq, e.i), "label": format!("{} · {} · {}", clock(e.t), op_label(&e.op), summarize(e)) })));
        let can = live.career() && live.live();
        hb.push(json!({ "form": { "id": "undo_pick", "apply": "Undo", "disabled": !can, "fields": [
            { "select": { "id": "entry", "label": "Undo an edit", "value": "", "options": opts,
                          "help": "Undo re-applies the value from before the edit, through the same game path." } } ] } }));
    }
    if rows.is_empty() {
        hb.push(text("No edits yet.", "muted"));
    } else {
        hb.push(json!({ "table": { "id": "history", "columns": history_columns(true), "rows": rows,
                                   "search": { "placeholder": "Search history" }, "compact": true, "sort": { "column": "time", "desc": true } } }));
    }
    let undo_btn = if undo_list.is_empty() || !live.career() {
        vec![]
    } else {
        vec![button("undo_last", "Undo last edit", "undo", "outline")]
    };
    blocks.push(card_full(
        "Edit history",
        Some("Every edit with the values the game read back"),
        vec![],
        undo_btn,
        hb,
    ));

    let running = live.game_running();
    let brows: Vec<Value> = backups
        .iter()
        .map(|b| {
            json!({ "id": b.name, "cells": [
            { "text": clock(b.created), "sort": b.created },
            b.reason.clone(),
            { "text": b.files.to_string(), "sort": b.files },
            { "text": bytes(b.bytes), "sort": b.bytes },
            { "text": b.career.clone().unwrap_or_else(|| "—".into()), "tone": "muted" }
        ] })
        })
        .collect();
    let mut bb = Vec::new();
    if brows.is_empty() {
        bb.push(text(
            "No backups yet. One is taken automatically before the first edit of every game session.",
            "muted",
        ));
    } else {
        bb.push(json!({ "table": { "id": "backups", "columns": [
            { "key": "when", "title": "When", "sort": true, "width": 120 }, "Why", { "key": "files", "title": "Files", "align": "right", "width": 70 },
            { "key": "size", "title": "Size", "align": "right", "width": 90 }, "Career" ], "rows": brows, "compact": true, "sort": { "column": "when", "desc": true } } }));
        let mut opts = vec![json!({ "value": "", "label": "Choose a backup…" })];
        opts.extend(
            backups
                .iter()
                .map(|b| json!({ "value": b.name, "label": format!("{} · {}", clock(b.created), b.reason) })),
        );
        bb.push(json!({ "form": { "id": "restore", "columns": 2, "apply": "Restore", "disabled": running, "fields": [
            { "select": { "id": "backup", "label": "Backup", "value": "", "options": opts } },
            { "toggle": { "id": "sure", "label": "Overwrite the game's saves with this backup", "value": false,
                          "help": if running { "Close F1 25 first." } else { "Pause Steam Cloud for F1 25, or let it upload afterwards." } } } ] } }));
    }
    let mut buttons = vec![
        button("backup_now", "Back up now", "save", "primary"),
        button("open_backups", "Open folder", "folder-open", "ghost"),
    ];
    if !backups.is_empty() {
        buttons.push(button("delete_oldest", "Delete oldest", "trash", "ghost"));
    }
    blocks.push(card_full(
        "Save backups",
        Some("Steam Cloud save folder copies, restorable on the same Steam account"),
        vec![],
        buttons,
        bb,
    ));
    frame(
        app,
        live,
        format!(
            "{ok} edit(s){} · {} backup(s)",
            if live.career_id().is_some() {
                " in this career"
            } else {
                ""
            },
            backups.len()
        ),
        blocks,
        false,
    )
}

// ---- events -------------------------------------------------------------------------------------

fn send(
    app: &mut App,
    host: &mut Host,
    live: &Live,
    label: &str,
    ops: Vec<Value>,
    undo_of: Option<(u64, usize)>,
) -> Result<Value, String> {
    for o in &ops {
        live.gate(o["op"].as_str().unwrap_or_default())?;
    }
    let mut warning = String::new();
    let boot = live.snap["boot"].as_u64().unwrap_or(0);
    let key = live
        .game_session
        .clone()
        .or_else(|| live.pid.map(|p| format!("pid-{p}-boot-{boot}")))
        .or_else(|| (boot > 0).then(|| format!("boot-{boot}")));
    if let Some(key) = key
        && app.store.session_marker().as_deref() != Some(key.as_str())
    {
        match host
            .call("steam_save_dir", Value::Null)
            .ok()
            .and_then(|v| v.as_str().map(PathBuf::from))
        {
            Some(dir) => {
                app.store
                    .backup(
                        &dir,
                        "Before the first edit of this game session",
                        live.career_id().as_deref(),
                        live.game_session.as_deref(),
                    )
                    .map_err(|e| format!("The automatic save backup failed, so the edit wasn't sent: {e}"))?;
                app.store.set_session_marker(&key);
            }
            None => {
                warning = " No Steam save folder was found, so no backup was taken.".into();
                app.store.set_session_marker(&key);
            }
        }
    }
    app.store.send(label, ops.clone(), undo_of, live.career_id())?;
    let first = ops.first().and_then(|o| o["op"].as_str()).unwrap_or_default();
    let when = if live.applies_now(first) {
        "Sent to the game.".to_owned()
    } else {
        let why = if live.op_anytime(first) {
            live.status()["why_anytime"].as_str().map(str::to_owned)
        } else {
            live.why()
        };
        format!(
            "Queued: it applies when the game is ready ({}).",
            why.unwrap_or_else(|| "waiting".into())
        )
    };
    Ok(json!({ "notice": format!("{label}: {when}{warning}") }))
}

fn values(ev: &Value) -> &Value {
    &ev["submit"]["values"]
}

fn changed(ev: &Value) -> Vec<String> {
    ev["submit"]["changed"]
        .as_array()
        .into_iter()
        .flatten()
        .filter_map(|c| c.as_str().map(str::to_owned))
        .collect()
}

pub fn event(app: &mut App, host: &mut Host, section: &str, ev: &Value) -> Result<Value, String> {
    let live = app.live(host);
    if let Some(tab) = ev["tab"]["tab"].as_str() {
        app.tab = tab.to_owned();
        return Ok(json!("reload"));
    }
    if let Some(id) = ev["click"]["id"].as_str() {
        return click(app, host, &live, section, id);
    }
    if ev.get("submit").is_some() {
        let form = ev["submit"]["form"].as_str().unwrap_or_default().to_owned();
        return submit(app, host, &live, &form, ev);
    }
    Ok(json!("none"))
}

fn click(app: &mut App, host: &mut Host, live: &Live, _section: &str, id: &str) -> Result<Value, String> {
    if let Some(n) = id.strip_prefix("cash_add:") {
        let amount: i64 = n.parse().map_err(|_| "bad amount")?;
        return send(
            app,
            host,
            live,
            &format!("Add {}", money_short(amount)),
            vec![json!({ "op": "economy.cash.add", "args": { "amount": amount } })],
            None,
        );
    }
    if let Some(n) = id.strip_prefix("renown_add:") {
        let amount: i64 = n.parse().map_err(|_| "bad amount")?;
        return send(
            app,
            host,
            live,
            &format!("Add {amount} renown"),
            vec![json!({ "op": "economy.renown.add", "args": { "amount": amount } })],
            None,
        );
    }
    if let Some(t) = id.strip_prefix("rnd_detail:") {
        let team: i64 = t.parse().map_err(|_| "bad team")?;
        return send(
            app,
            host,
            live,
            "Load an AI team's R&D tree",
            vec![json!({ "op": "rnd.team_detail", "args": { "team": team } })],
            None,
        );
    }
    if id == "repair_contracts" {
        return send(
            app,
            host,
            live,
            "Sign seat contracts",
            vec![json!({ "op": "drivers.contracts.repair", "args": {} })],
            None,
        );
    }
    if id == "repair_calendar" {
        return send(
            app,
            host,
            live,
            "Repair the round count",
            vec![json!({ "op": "season.calendar.repair", "args": {} })],
            None,
        );
    }
    if id == "points_clear" {
        return send(
            app,
            host,
            live,
            "Clear points adjustments",
            vec![json!({ "op": "season.points.clear", "args": {} })],
            None,
        );
    }
    if let Some(n) = id.strip_prefix("rp_add:") {
        let amount: i64 = n.parse().map_err(|_| "bad amount")?;
        return send(
            app,
            host,
            live,
            &format!("Add {} RP", group(amount)),
            vec![json!({ "op": "economy.rp.add", "args": { "amount": amount } })],
            None,
        );
    }
    match id {
        "refresh" | "noop" => Ok(json!("reload")),
        "go_history" => {
            app.tab = "history".into();
            Ok(json!("reload"))
        }
        "withdraw" => {
            let n = app.store.withdraw_all();
            Ok(json!({ "notice": format!("Withdrew {n} queued edit(s).") }))
        }
        "backup_now" => {
            let dir = host
                .call("steam_save_dir", Value::Null)?
                .as_str()
                .map(PathBuf::from)
                .ok_or("No Steam save folder was found for F1 25.")?;
            let b = app.store.backup(
                &dir,
                "Manual backup",
                live.career_id().as_deref(),
                live.game_session.as_deref(),
            )?;
            Ok(json!({ "notice": format!("Backed up {} file(s), {}.", b.files, bytes(b.bytes)) }))
        }
        "open_backups" => {
            std::fs::create_dir_all(app.store.backups_dir()).ok();
            Ok(json!({ "open": app.store.backups_dir() }))
        }
        "delete_oldest" => {
            let Some(b) = app.store.backups().last().cloned() else {
                return Ok(json!("reload"));
            };
            app.store.delete_backup(&b.name)?;
            Ok(json!({ "notice": format!("Deleted the backup from {}.", clock(b.created)) }))
        }
        "fac_complete" => send(
            app,
            host,
            live,
            "Finish facility builds",
            vec![json!({ "op": "facilities.complete", "args": {} })],
            None,
        ),
        "seed_lock" => send(
            app,
            host,
            live,
            "Pin the weather seed",
            vec![json!({ "op": "weather.seed.lock", "args": {} })],
            None,
        ),
        "seed_reroll" => send(
            app,
            host,
            live,
            "Reroll the weather seed",
            vec![json!({ "op": "weather.seed.reroll", "args": {} })],
            None,
        ),
        "seed_unlock" => send(
            app,
            host,
            live,
            "Use the game's weather seed",
            vec![json!({ "op": "weather.seed.unlock", "args": {} })],
            None,
        ),
        "weather_clear" => {
            let mut args = json!({ "started": true });
            if let Some(w) = live
                .section("weather")
                .and_then(|w| w["default_weekend"].as_i64())
            {
                args["weekend"] = json!(w);
            }
            send(
                app,
                host,
                live,
                "Clear the weather plan",
                vec![json!({ "op": "weather.plan.clear", "args": args })],
                None,
            )
        }
        "cal_move_apply" => {
            let cal = live.section("calendar").cloned().unwrap_or(Value::Null);
            let (f, t) = pending_move(app, &cal)
                .filter(|(f, t)| f != t)
                .ok_or("Pick a round and the round it should become.")?;
            let (first, future) = upcoming(&cal);
            let names: Vec<String> = future
                .iter()
                .filter_map(|r| r["weekend"].as_str().map(str::to_owned))
                .collect();
            if names.len() != future.len() {
                return Err("The calendar snapshot is incomplete; wait a moment and try again.".into());
            }
            let label = format!(
                "Move {} to round {}",
                crate::calendar::short_name(&future[f], profile(app)),
                first + t as i64
            );
            let order = crate::calendar::move_order(&names, f, t);
            let r = send(
                app,
                host,
                live,
                &label,
                vec![json!({ "op": "season.calendar.reorder", "args": { "order": order } })],
                None,
            );
            if r.is_ok() {
                app.cal_move = (None, None);
            }
            r
        }
        "cal_move_clear" => {
            app.cal_move = (None, None);
            Ok(json!("reload"))
        }
        "undo_last" => {
            let e = undoable(app, live)
                .first()
                .map(|e| (*e).clone())
                .ok_or("Nothing to undo.")?;
            undo(app, host, live, &e)
        }
        other => Err(format!("unknown button {other}")),
    }
}

fn undo(app: &mut App, host: &mut Host, live: &Live, e: &Entry) -> Result<Value, String> {
    match inverse(e) {
        Undo::Op(mut op) => {
            if matches!(
                op["op"].as_str(),
                Some("season.calendar.set_round" | "season.calendar.add_round")
            ) {
                verify_weekend(app, host, live, &mut op["args"]).ok();
            }
            send(
                app,
                host,
                live,
                &format!("Undo {}", op_label(&e.op).to_lowercase()),
                vec![op],
                Some((e.seq, e.i)),
            )
        }
        Undo::Ops(ops) => send(
            app,
            host,
            live,
            &format!("Undo {}", op_label(&e.op).to_lowercase()),
            ops,
            Some((e.seq, e.i)),
        ),
        Undo::BackupOnly => Err("This edit can only be undone by restoring a backup.".into()),
        Undo::None(why) => Err(format!("This edit can't be undone: {why}.")),
    }
}

fn rnd_team_arg(app: &App, live: &Live) -> Value {
    let (team, _) = rnd_team_data(app, live);
    if team["player"] == true || team.is_null() {
        json!("player")
    } else {
        team["team"].clone()
    }
}

fn submit(app: &mut App, host: &mut Host, live: &Live, form: &str, ev: &Value) -> Result<Value, String> {
    let v = values(ev);
    let ch = changed(ev);
    match form {
        "finance" => {
            let econ = live.section("economy").cloned().unwrap_or(Value::Null);
            let mut ops = Vec::new();
            let mut parts = Vec::new();
            if ch.iter().any(|c| c == "cash") {
                let value = int(&v["cash"]).ok_or("cash must be a number")?;
                let mut o = json!({ "op": "economy.cash.set", "args": { "value": value } });
                if let Some(e) = int(&econ["cash"]["balance"]) {
                    o["expect"] = json!(e);
                }
                ops.push(o);
                parts.push(format!("cash {}", money(value)));
            }
            if ch.iter().any(|c| c == "rp") {
                let value = int(&v["rp"]).ok_or("RP must be a number")?;
                let mut o = json!({ "op": "economy.rp.set", "args": { "value": value } });
                if let Some(e) = int(&econ["rp"]["wallet"]) {
                    o["expect"] = json!(e);
                }
                ops.push(o);
                parts.push(format!("{} RP", group(value)));
            }
            if ops.is_empty() {
                return Ok(json!("reload"));
            }
            send(
                app,
                host,
                live,
                &format!("Set {}", parts.join(" and ")),
                ops,
                None,
            )
        }
        "rating_mode" => {
            let m = v["mode"].as_str().unwrap_or("flat").to_owned();
            app.store.set_pref("rating_mode", json!(m));
            Ok(json!("reload"))
        }
        "ratings" if ch.iter().any(|c| c == "num") => {
            let row = ev["submit"]["row"].as_str().ok_or("no row")?;
            let id: i64 = row.parse().map_err(|_| "bad driver id")?;
            let number = int(&v["num"]).ok_or("the number must be a whole number")?;
            let (label, o) = number_op(live, id, number, false)?;
            send(app, host, live, &label, vec![o], None)
        }
        "race_number" => {
            let id: i64 = v["driver"]
                .as_str()
                .and_then(|x| x.parse().ok())
                .ok_or("Choose a driver.")?;
            let number = int(&v["number"]).ok_or("Enter a number from 1 to 99.")?;
            let (label, o) = number_op(live, id, number, v["swap"] == true)?;
            send(app, host, live, &label, vec![o], None)
        }
        "ratings" => {
            let row = ev["submit"]["row"].as_str().ok_or("no row")?;
            let id: i64 = row.parse().map_err(|_| "bad driver id")?;
            let drivers = live
                .section("drivers")
                .and_then(|d| d["drivers"].as_array())
                .cloned()
                .unwrap_or_default();
            let d = drivers
                .iter()
                .find(|d| d["driverStatId"].as_i64() == Some(id))
                .ok_or("that driver isn't in the snapshot any more")?;
            let mut ratings = serde_json::Map::new();
            let mut expect = serde_json::Map::new();
            for c in &ch {
                let val = v[c].as_f64().ok_or("ratings must be numbers")?;
                ratings.insert(c.clone(), json!(val));
                if let Some(now) = d["ratings"][c]["value"].as_f64() {
                    expect.insert(c.clone(), json!(now));
                }
            }
            if ratings.is_empty() {
                return Ok(json!("reload"));
            }
            let name = title_case(d["name"].as_str().unwrap_or("driver"));
            let what = ratings
                .iter()
                .map(|(k, x)| {
                    format!(
                        "{} {}",
                        title_case(k),
                        x.as_f64().map_or_else(|| x.to_string(), |f| format!("{f:.0}"))
                    )
                })
                .collect::<Vec<_>>()
                .join(", ");
            let mode = app.store.pref_str("rating_mode", "flat");
            send(
                app,
                host,
                live,
                &format!("{name}: {what}"),
                vec![
                    json!({ "op": "drivers.ratings.set", "args": { "driverStatId": id, "ratings": ratings, "mode": mode, "expect": expect } }),
                ],
                None,
            )
        }
        "facilities" => {
            let row = ev["submit"]["row"].as_str().ok_or("no row")?;
            let fac: i64 = row.parse().map_err(|_| "bad facility id")?;
            let fname = ["Engineering", "Personnel", "Corporate"]
                .get(fac as usize)
                .map_or_else(|| format!("Facility {fac}"), |n| (*n).to_owned());
            let level = int(&v["level"]).ok_or("level must be a number")?;
            send(
                app,
                host,
                live,
                &format!("{fname} to level {level}"),
                vec![json!({ "op": "facilities.set_level", "args": { "facility": fac, "level": level } })],
                None,
            )
        }
        "rnd_team" => {
            let t: i64 = v["team"]
                .as_str()
                .and_then(|s| s.parse().ok())
                .ok_or("bad team")?;
            app.store.set_pref("rnd_team", json!(t));
            Ok(json!("reload"))
        }
        "rnd_bulk" => {
            let dept: i64 = v["dept"].as_str().and_then(|s| s.parse().ok()).unwrap_or(-1);
            let action = v["action"]
                .as_str()
                .filter(|a| !a.is_empty())
                .ok_or("Choose an action.")?;
            let mut args = json!({ "team": rnd_team_arg(app, live), "dept": dept, "all": true });
            let op = match action {
                "unlock" => "rnd.unlock",
                "reset" => "rnd.reset",
                _ => {
                    args["stage"] = json!(v["stage"].as_str().unwrap_or("full"));
                    "rnd.complete"
                }
            };
            let dname = if dept < 0 {
                "every department".to_owned()
            } else {
                DEPTS.get(dept as usize).copied().unwrap_or("?").to_owned()
            };
            send(
                app,
                host,
                live,
                &format!("{} {dname}", op_label(op)),
                vec![json!({ "op": op, "args": args })],
                None,
            )
        }
        "rnd_one" => {
            let hash: i64 = v["upgrade"]
                .as_str()
                .and_then(|s| s.parse().ok())
                .ok_or("Choose an upgrade.")?;
            let action = v["action"].as_str().unwrap_or("complete");
            let mut args = json!({ "team": rnd_team_arg(app, live), "upgrade": hash });
            let op = match action {
                "unlock" => "rnd.unlock",
                "refund" => "rnd.refund",
                "reset" => "rnd.reset",
                "fit" => {
                    args["fitted"] = json!(true);
                    "rnd.fit"
                }
                "unfit" => {
                    args["fitted"] = json!(false);
                    "rnd.fit"
                }
                _ => "rnd.complete",
            };
            let (team, _) = rnd_team_data(app, live);
            let name = team["u"]
                .as_array()
                .and_then(|a| a.iter().find(|u| u["h"].as_i64() == Some(hash)))
                .and_then(|u| u["n"].as_str().map(title_case))
                .unwrap_or_else(|| format!("upgrade {hash}"));
            send(
                app,
                host,
                live,
                &format!("{}: {name}", op_label(op)),
                vec![json!({ "op": op, "args": args })],
                None,
            )
        }
        "cal_move" => {
            let pick = |k: &str| v[k].as_str().and_then(|x| x.parse::<i64>().ok());
            if ch.iter().any(|c| c == "from") {
                app.cal_move = (pick("from"), None);
            }
            if ch.iter().any(|c| c == "to") {
                app.cal_move.1 = pick("to");
            }
            Ok(json!("reload"))
        }
        "weather_plan" => {
            let key = v["session"]
                .as_str()
                .filter(|k| !k.is_empty())
                .ok_or("Choose a session.")?
                .to_owned();
            let laps = int(&v["laps"]).unwrap_or(0);
            let mut timeline = Vec::new();
            for n in 1..=3 {
                timeline.extend(weather_point(v, n, laps)?);
            }
            if timeline.is_empty() {
                return Err("Add at least one weather change.".into());
            }
            let mut session = json!({ "timeline": timeline });
            if laps > 0 {
                session["laps"] = json!(laps);
            }
            if let Some(s) = v["start"].as_str().filter(|s| !s.is_empty()) {
                session["start"] = json!(s);
            }
            let mut sessions = serde_json::Map::new();
            sessions.insert(key.clone(), session);
            let mut args =
                json!({ "sessions": sessions, "replace": v["replace"].as_bool().unwrap_or(false) });
            if let Some(w) = live
                .section("weather")
                .and_then(|w| w["default_weekend"].as_i64())
            {
                args["weekend"] = json!(w);
            }
            let label = W_SESSIONS
                .iter()
                .find(|s| s.0 == key)
                .map_or(key.clone(), |s| s.1.to_owned());
            send(
                app,
                host,
                live,
                &format!("Weather plan for {label}"),
                vec![json!({ "op": "weather.plan.set", "args": args })],
                None,
            )
        }
        "renown" => {
            let value = v["renown"].as_f64().ok_or("renown must be a number")?;
            let mut o = json!({ "op": "economy.renown.set", "args": { "value": value } });
            if let Some(now) = live
                .section("economy")
                .and_then(|e| e["renown"]["points"].as_f64())
            {
                o["expect"] = json!(now);
            }
            send(
                app,
                host,
                live,
                &format!("Set renown to {value:.0}"),
                vec![o],
                None,
            )
        }
        "costcap" => {
            let amount = int(&v["amount"])
                .filter(|a| *a > 0)
                .ok_or("Enter an amount above 0.")?;
            let mut o = json!({ "op": "economy.costcap.refund", "args": { "amount": amount } });
            let c = live
                .section("economy")
                .map(|e| e["cost_cap"].clone())
                .unwrap_or(Value::Null);
            let cur = int(&c["current_season"]).unwrap_or(0);
            if let Some(x) = c["seasons"]
                .as_array()
                .and_then(|a| a.iter().find(|s| int(&s["season"]) == Some(cur)))
                .and_then(|s| int(&s["expenses"]))
            {
                o["expect"] = json!(x);
            }
            send(
                app,
                host,
                live,
                &format!("Cost cap refund {}", money_short(amount)),
                vec![o],
                None,
            )
        }
        "loyalty" => {
            let amount = int(&v["amount"])
                .filter(|a| *a > 0)
                .ok_or("Enter loyalty XP above 0.")?;
            let reason: i64 = v["reason"].as_str().and_then(|r| r.parse().ok()).unwrap_or(0);
            send(
                app,
                host,
                live,
                &format!("Add {amount} sponsor loyalty"),
                vec![
                    json!({ "op": "economy.sponsor.loyalty.add", "args": { "amount": amount, "reason": reason } }),
                ],
                None,
            )
        }
        "swap" => {
            let a: i64 = v["a"]
                .as_str()
                .and_then(|x| x.parse().ok())
                .ok_or("Choose the first driver.")?;
            let b: i64 = v["b"]
                .as_str()
                .and_then(|x| x.parse().ok())
                .ok_or("Choose the second driver.")?;
            if a == b {
                return Err("Choose two different drivers.".into());
            }
            let drivers = live
                .section("drivers")
                .and_then(|d| d["drivers"].as_array())
                .cloned()
                .unwrap_or_default();
            let find = |id: i64| {
                drivers
                    .iter()
                    .find(|d| d["driverStatId"].as_i64() == Some(id))
                    .cloned()
                    .unwrap_or(Value::Null)
            };
            let (da, db) = (find(a), find(b));
            if da["team"].is_null() && db["team"].is_null() {
                return Err("Two free agents have no seat to trade; pick at least one race driver.".into());
            }
            let name = |d: &Value| title_case(d["name"].as_str().unwrap_or("driver"));
            send(
                app,
                host,
                live,
                &format!("Swap {} and {}", name(&da), name(&db)),
                vec![json!({ "op": "drivers.swap", "args": { "a": a, "b": b,
                     "expect": { "aTeamStatId": da["team"]["teamStatId"], "bTeamStatId": db["team"]["teamStatId"] } } })],
                None,
            )
        }
        "free_agent_add" => {
            let id: i64 = v["driver"]
                .as_str()
                .and_then(|x| x.parse().ok())
                .ok_or("Choose a Driver Manager driver.")?;
            let name = live
                .section("drivers")
                .and_then(|d| d["otherDrivers"].as_array())
                .and_then(|a| a.iter().find(|d| d["driverStatId"].as_i64() == Some(id)))
                .and_then(|d| d["name"].as_str().map(str::to_owned))
                .ok_or("That driver isn't in the snapshot any more.")?;
            send(
                app,
                host,
                live,
                &format!("Add {name} to the line-up as a free agent"),
                vec![json!({ "op": "drivers.freeAgent.add", "args": { "driverStatId": id } })],
                None,
            )
        }
        "free_agent" => {
            let id = |k: &str| v[k].as_str().and_then(|x| x.parse::<i64>().ok());
            let (Some(free), Some(seat)) = (id("free"), id("seat")) else {
                return Err("Choose a free agent and a race seat.".into());
            };
            let drivers = live
                .section("drivers")
                .and_then(|d| d["drivers"].as_array())
                .cloned()
                .unwrap_or_default();
            let find = |id: i64| {
                drivers
                    .iter()
                    .find(|d| d["driverStatId"].as_i64() == Some(id))
                    .cloned()
            };
            let (Some(df), Some(ds)) = (find(free), find(seat)) else {
                return Err("That driver isn't in the snapshot any more.".into());
            };
            let name = |d: &Value| title_case(d["name"].as_str().unwrap_or("driver"));
            send(
                app,
                host,
                live,
                &format!("Sign {} into {}'s seat", name(&df), name(&ds)),
                vec![
                    json!({ "op": "drivers.swap", "args": { "a": seat, "b": free, "experimentalFreeAgent": true,
                     "expect": { "aTeamStatId": ds["team"]["teamStatId"], "bTeamStatId": Value::Null } } }),
                ],
                None,
            )
        }
        "contracts" => {
            let row = ev["submit"]["row"].as_str().ok_or("no row")?;
            let mut parts = row.splitn(3, ':');
            let (list, id, team) = (
                parts.next().unwrap_or("decided"),
                parts
                    .next()
                    .and_then(|x| x.parse::<i64>().ok())
                    .ok_or("bad row")?,
                parts.next().and_then(|x| x.parse::<i64>().ok()),
            );
            let rec = live
                .section("drivers")
                .and_then(|d| d["contracts"][list].as_array())
                .and_then(|a| {
                    a.iter()
                        .find(|r| {
                            r["driverStatId"].as_i64() == Some(id)
                                && (team.is_none() || r["newTeamStatId"].as_i64() == team)
                        })
                        .cloned()
                })
                .ok_or("that contract isn't in the snapshot any more")?;
            let mut args = json!({ "driverStatId": id, "list": list });
            if let Some(t) = team {
                args["newTeamStatId"] = json!(t);
            }
            let mut expect = serde_json::Map::new();
            for c in &ch {
                let val = int(&v[c]).ok_or("amounts must be whole numbers")?;
                args[c.as_str()] = json!(val);
                expect.insert(c.clone(), rec[c.as_str()].clone());
            }
            if ch.is_empty() {
                return Ok(json!("reload"));
            }
            args["expect"] = Value::Object(expect);
            let names = driver_names(live);
            send(
                app,
                host,
                live,
                &format!("{} contract", driver_name(&names, id)),
                vec![json!({ "op": "drivers.contract.set", "args": args })],
                None,
            )
        }
        "points" => {
            let d: i64 = v["driver"]
                .as_str()
                .and_then(|x| x.parse().ok())
                .ok_or("Choose a driver.")?;
            let delta = int(&v["delta"])
                .filter(|x| *x != 0)
                .ok_or("Enter a points change other than 0.")?;
            let names = driver_names(live);
            send(
                app,
                host,
                live,
                &format!("{} {delta:+} pts", driver_name(&names, d)),
                vec![json!({ "op": "season.points.adjust", "args": { "driver": d, "delta": delta } })],
                None,
            )
        }
        "cal_remove" => {
            let r: i64 = v["round"]
                .as_str()
                .and_then(|x| x.parse().ok())
                .ok_or("Choose a round.")?;
            send(
                app,
                host,
                live,
                &format!("Remove round {r} ({})", round_track_label(app, live, r)),
                vec![json!({ "op": "season.calendar.remove_round", "args": { "round": r } })],
                None,
            )
        }
        "cal_set" => {
            let r: i64 = v["round"]
                .as_str()
                .and_then(|x| x.parse().ok())
                .ok_or("Choose a round.")?;
            let w = v["weekend"]
                .as_str()
                .filter(|w| !w.is_empty())
                .ok_or("Choose a track.")?;
            let mut args = json!({ "round": r, "weekend": w });
            verify_weekend(app, host, live, &mut args)?;
            send(
                app,
                host,
                live,
                &format!(
                    "Round {r} ({}) becomes {}",
                    round_track_label(app, live, r),
                    pick_label(app, live, w)
                ),
                vec![json!({ "op": "season.calendar.set_round", "args": args })],
                None,
            )
        }
        "cal_add" => {
            let w = v["weekend"]
                .as_str()
                .filter(|w| !w.is_empty())
                .ok_or("Choose a track.")?;
            let at = int(&v["at"]).ok_or("Choose where the round goes.")?;
            let mut args = json!({ "weekend": w, "at": at });
            verify_weekend(app, host, live, &mut args)?;
            send(
                app,
                host,
                live,
                &format!("Add {} as round {at}", pick_label(app, live, w)),
                vec![json!({ "op": "season.calendar.add_round", "args": args })],
                None,
            )
        }
        "undo_pick" => {
            let pick = v["entry"]
                .as_str()
                .filter(|p| !p.is_empty())
                .ok_or("Choose an edit to undo.")?;
            let (s, i) = pick.split_once(':').ok_or("bad entry")?;
            let (s, i): (u64, usize) = (
                s.parse().map_err(|_| "bad entry")?,
                i.parse().map_err(|_| "bad entry")?,
            );
            let e = app
                .store
                .history
                .iter()
                .find(|e| e.seq == s && e.i == i)
                .cloned()
                .ok_or("that edit isn't in the history")?;
            if !in_scope(live, &e) {
                return Err("That edit belongs to another career.".into());
            }
            undo(app, host, live, &e)
        }
        "restore" => {
            if live.game_running() {
                return Err("Close F1 25 before restoring a backup.".into());
            }
            if v["sure"].as_bool() != Some(true) {
                return Err("Turn on the overwrite switch to confirm the restore.".into());
            }
            let name = v["backup"]
                .as_str()
                .filter(|p| !p.is_empty())
                .ok_or("Choose a backup.")?
                .to_owned();
            let dir = host
                .call("steam_save_dir", Value::Null)?
                .as_str()
                .map(PathBuf::from)
                .ok_or("No Steam save folder was found for F1 25.")?;
            let safety = app
                .store
                .backup(&dir, &format!("Before restoring {name}"), None, None)?;
            let n = app.store.restore(&name, &dir)?;
            Ok(
                json!({ "notice": format!("Restored {n} file(s) from {name}. The saves from before the restore are in backup {}.", safety.name) }),
            )
        }
        other => Err(format!("unknown form {other}")),
    }
}

fn weather_point(v: &Value, n: usize, laps: i64) -> Result<Option<Value>, String> {
    let kind = v[format!("p{n}_kind")].as_str().unwrap_or("off");
    if kind == "off" {
        return Ok(None);
    }
    let at = v[format!("p{n}_at")].as_f64().unwrap_or(0.0);
    let mut p = json!({ "state": v[format!("p{n}_state")].as_str().unwrap_or("clear") });
    let intensity = v[format!("p{n}_int")].as_f64().unwrap_or(50.0);
    if intensity > 0.0 {
        p["intensity"] = json!(intensity / 100.0);
    }
    match kind {
        "lap" => {
            if laps <= 0 {
                return Err("Set the race laps to plan by lap.".into());
            }
            p["at_lap"] = json!(at.max(1.0) as i64);
        }
        "minute" => p["at_minute"] = json!(at),
        _ => p["at_fraction"] = json!((at / 100.0).clamp(0.0, 1.0)),
    }
    Ok(Some(p))
}

pub fn summary(app: &mut App) -> Value {
    let snap = app.store.snapshot().clone();
    let written = snap["written"].as_u64().unwrap_or(0);
    if written == 0
        || now_ms().saturating_sub(written) > crate::live::STALE_MS
        || snap["status"]["career"] != true
    {
        return Value::Null;
    }
    let mode = mode_label(snap["status"]["mode"].as_str().unwrap_or("none"));
    let mut items = vec![mode.to_owned()];
    if let (Some(r), Some(n)) = (int(&snap["status"]["round"]), int(&snap["status"]["rounds"])) {
        items.push(format!("Round {r}/{n}"));
    }
    json!({ "label": "CAREER", "items": items })
}

#[cfg(test)]
mod weather_form_tests {
    use super::*;

    #[test]
    fn a_weather_change_at_zero_intensity_leaves_it_to_the_game() {
        let v = json!({ "p1_kind": "percent", "p1_at": 33, "p1_state": "light_rain", "p1_int": 0,
                        "p2_kind": "lap", "p2_at": 20, "p2_state": "heavy_rain", "p2_int": 85, "p3_kind": "off" });
        assert_eq!(
            weather_point(&v, 1, 0).unwrap(),
            Some(json!({ "state": "light_rain", "at_fraction": 0.33 }))
        );
        assert_eq!(
            weather_point(&v, 2, 53).unwrap(),
            Some(json!({ "state": "heavy_rain", "intensity": 0.85, "at_lap": 20 }))
        );
        assert!(weather_point(&v, 2, 0).is_err());
        assert_eq!(weather_point(&v, 3, 53).unwrap(), None);
    }
}

#[cfg(test)]
mod missing_notice_tests {
    use super::*;

    #[test]
    fn a_missing_snapshot_on_an_unsupported_build_says_so() {
        let old = GameBuild {
            version: Some("1.0.136.6820".into()),
            unsupported: true,
        };
        assert_eq!(
            missing_text(Some(&old)),
            "This F1 25 (1.0.136.6820) isn't a game version the mods were made for, so the editor's game plugin can't start. Update the game in Steam or the EA app."
        );
        let supported = GameBuild {
            version: Some("1.0.141.7865".into()),
            unsupported: false,
        };
        for b in [Some(&supported), None] {
            assert!(
                missing_text(b)
                    .starts_with("The game is running but its editor plugin hasn't written a snapshot.")
            );
        }
        let unknown = GameBuild {
            version: None,
            unsupported: true,
        };
        assert!(missing_text(Some(&unknown)).starts_with("This F1 25 isn't a game version"));
    }
}

#[cfg(test)]
mod race_number_tests {
    use super::*;

    #[test]
    fn a_race_number_edit_sends_the_number_the_driver_has() {
        let snap = json!({
            "written": now_ms(),
            "status": { "game": "running", "career": true, "mode": "driver_career", "safe": true },
            "bridge": { "ops": [{ "name": "drivers.number.set" }] },
            "sections": { "drivers": { "drivers": [
                { "driverStatId": 9, "name": "max_verstappen", "number": 1, "raceNumber": 3, "team": Value::Null, "seat": { "hasCar": true }, "player": false },
                { "driverStatId": 30, "name": "d30", "number": 99, "raceNumber": Value::Null, "team": Value::Null, "seat": { "hasCar": false }, "player": false },
                { "driverStatId": 40, "name": "d40", "number": 7, "team": Value::Null, "seat": { "hasCar": false }, "player": false }
            ] } },
        });
        let live = Live::new(None, &snap, true, None, None);
        let (label, o) = number_op(&live, 9, 33, true).unwrap();
        assert_eq!(label, "Max Verstappen: number 33");
        assert_eq!(
            o,
            json!({ "op": "drivers.number.set", "args": { "driverStatId": 9, "number": 33, "swap": true, "expect": 3 } })
        );
        let (_, o) = number_op(&live, 30, 12, false).unwrap();
        assert!(o["args"].get("expect").is_none());
        assert!(number_op(&live, 9, 0, false).is_err() && number_op(&live, 9, 100, false).is_err());
        assert!(number_op(&live, 77, 5, false).is_err());
        let drivers = live.section("drivers").unwrap()["drivers"]
            .as_array()
            .unwrap()
            .clone();
        assert_eq!(number_cell(&drivers[0], true)["edit"]["number"]["value"], 3);
        assert_eq!(number_cell(&drivers[1], false), json!({ "text": "—", "sort": 0 }));
        assert_eq!(number_cell(&drivers[2], false)["text"], "7");
        let card = serde_json::to_string(&numbers_card(&live)).unwrap();
        assert!(card.contains("Max Verstappen · no team · #3") && card.contains("D30 · no team · no number"));
    }
}

#[cfg(test)]
mod late_free_agent_tests {
    use super::*;

    fn live_with(others: Value, ops: Value) -> Live {
        let snap = json!({
            "written": now_ms(),
            "status": { "game": "running", "career": true, "mode": "driver_career", "safe": true },
            "bridge": { "ops": ops },
            "sections": { "drivers": { "available": true, "drivers": [], "otherDrivers": others } }
        });
        Live::new(None, &snap, true, None, None)
    }

    #[test]
    fn lists_only_driver_manager_drivers_without_a_row() {
        let live = live_with(
            json!([
                { "driverStatId": 208, "name": "dm_208", "initials": "SWO", "overall": 71 },
                { "driverStatId": 120, "name": "some_f2_driver", "overall": 60 }
            ]),
            json!([{ "name": "drivers.freeAgent.add" }]),
        );
        let text = serde_json::to_string(&late_free_agent_blocks(&live)).unwrap();
        assert!(text.contains("SWO · dm_208 · overall 71"));
        assert!(!text.contains("some_f2_driver"));
        assert!(text.contains("\"id\":\"free_agent_add\""));
    }

    #[test]
    fn hidden_without_the_op_or_without_late_drivers() {
        let dm = json!([{ "driverStatId": 208, "name": "dm_208" }]);
        assert!(late_free_agent_blocks(&live_with(dm, json!([]))).is_empty());
        assert!(
            late_free_agent_blocks(&live_with(
                json!([{ "driverStatId": 120, "name": "some_f2_driver" }]),
                json!([{ "name": "drivers.freeAgent.add" }])
            ))
            .is_empty()
        );
    }
}

#[cfg(test)]
mod swap_card_tests {
    use super::*;

    #[test]
    fn my_team_swaps_leave_free_agents_out_and_say_why() {
        let driver = |id: u32, team: Value, car: bool| json!({ "driverStatId": id, "name": format!("d{id}"), "team": team, "seat": { "hasCar": car }, "player": false });
        let redbull = json!({ "teamStatId": 2, "name": "redbull" });
        let snap = |mode: &str| {
            json!({
                "written": now_ms(),
                "status": { "game": "running", "career": true, "mode": mode, "safe": true },
                "bridge": { "ops": [{ "name": "drivers.swap" }] },
                "sections": { "drivers": { "drivers": [
                    driver(9, redbull.clone(), true), driver(30, redbull.clone(), false), driver(208, Value::Null, false)
                ] } },
            })
        };
        let card = |mode: &str| {
            let live = Live::new(None, &snap(mode), true, None, None);
            serde_json::to_string(&swap_card(&live)).unwrap()
        };
        let my_team = card("my_team");
        assert!(
            my_team.contains("D9 · Red Bull Racing · race seat")
                && my_team.contains("D30 · Red Bull Racing · reserve")
        );
        assert!(!my_team.contains("D208"));
        assert!(my_team.contains(
            "Free agents, such as drivers made with Driver Manager, aren't listed here: the experimental Free agent into a seat card below signs one."
        ));
        let career = card("driver_career");
        assert!(career.contains("D208 · Free agent") && !career.contains("D30 "));
        assert!(
            career.contains("the game gives a driver a line-up place only when it builds a season's line-up")
        );
        let live = Live::new(None, &snap("my_team"), true, None, None);
        let fa = serde_json::to_string(&free_agent_card(&live).unwrap()).unwrap();
        assert!(
            fa.contains("D208 · no team")
                && fa.contains("D9 · Red Bull Racing · race seat")
                && !fa.contains("D30")
        );
        assert!(free_agent_card(&Live::new(None, &snap("driver_career"), true, None, None)).is_none());
    }

    #[test]
    fn driver_career_lists_race_drivers_without_a_seat_contract() {
        let vcarb = json!({ "teamStatId": 482, "name": "racing_bulls" });
        let driver = |id: u32, car: bool, contract: Value| json!({ "driverStatId": id, "name": format!("d{id}"), "team": vcarb, "seat": { "hasCar": car }, "player": false, "contract": contract });
        let snap = |drivers: Vec<Value>| {
            json!({
                "written": now_ms(),
                "status": { "game": "running", "career": true, "mode": "driver_career", "safe": true },
                "bridge": { "ops": [{ "name": "drivers.contracts.repair" }] },
                "sections": { "drivers": { "drivers": drivers } },
            })
        };
        let render = |drivers: Vec<Value>| {
            let live = Live::new(None, &snap(drivers), true, None, None);
            serde_json::to_string(&contracts_card(&live)).unwrap()
        };
        let ok = json!({ "teamStatId": 482, "active": true });
        let lost = render(vec![
            driver(150, true, ok.clone()),
            driver(174, true, json!({ "teamStatId": null, "active": false })),
            driver(9, true, Value::Null),
            driver(30, false, Value::Null),
        ]);
        assert!(
            lost.contains("D174")
                && lost.contains("Contract ended")
                && lost.contains("D9")
                && lost.contains("No contract kept")
        );
        assert!(!lost.contains("D150") && !lost.contains("D30") && lost.contains("repair_contracts"));
        let fine = render(vec![driver(150, true, ok.clone()), driver(174, true, ok)]);
        assert!(fine.contains("Every race driver has a contract") && !fine.contains("repair_contracts"));
        let old = render(vec![
            json!({ "driverStatId": 1, "team": vcarb, "seat": { "hasCar": true }, "player": false }),
        ]);
        assert!(old.contains("Contract edits are My Team only."));
    }
}

#[cfg(test)]
mod calendar_count_tests {
    use super::*;

    fn cal(rounds: usize, stops: Value) -> Value {
        let rs: Vec<Value> = (0..rounds)
            .map(|i| json!({ "round": i + 1, "weekend": i.to_string(), "editable": i > 0 }))
            .collect();
        json!({ "rounds": rs, "race_weekends": stops, "editable_from": 2 })
    }

    fn live() -> Live {
        let snap = json!({
            "written": now_ms(),
            "status": { "game": "running", "career": true, "mode": "driver_career", "safe": true },
            "bridge": { "ops": [{ "name": "season.calendar.repair" }] },
            "sections": {},
        });
        Live::new(None, &snap, true, None, None)
    }

    #[test]
    fn a_matching_round_count_needs_no_repair_and_locks_add_and_remove() {
        assert!(calendar_count_card(&live(), &cal(24, json!(24))).is_none());
        assert!(calendar_count_card(&live(), &cal(24, Value::Null)).is_none());
        assert!(count_note(Some(24), 24).contains("keeps 24 rounds"));
        assert!(count_note(None, 24).contains("can't be added or removed"));
    }

    #[test]
    fn a_short_or_long_calendar_offers_the_repair() {
        let short =
            serde_json::to_string(&calendar_count_card(&live(), &cal(20, json!(24))).unwrap()).unwrap();
        assert!(short.contains("repair_calendar") && short.contains("adds 4 round(s) at the end"));
        let long =
            serde_json::to_string(&calendar_count_card(&live(), &cal(25, json!(24))).unwrap()).unwrap();
        assert!(long.contains("repair_calendar") && long.contains("removes the last 1 round(s)"));
        assert_eq!(op_label("season.calendar.repair"), "Repair the round count");
    }
}
