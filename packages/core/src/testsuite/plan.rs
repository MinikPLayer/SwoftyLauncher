//! Grouping tests into batches: tests with the same environment, addons and
//! settings share one game start.

use std::collections::BTreeMap;

use serde::Serialize;
use serde_json::Value;

use super::suite::{Env, Kind, Selector, Suite, Test};

#[derive(Debug, Clone)]
pub struct Plan {
    pub batches: Vec<Batch>,
}

#[derive(Debug, Clone)]
pub struct Batch {
    pub key: String,
    pub env: Env,
    pub addons: Vec<String>,
    pub settings: BTreeMap<String, BTreeMap<String, Value>>,
    pub tests: Vec<Test>,
}

impl Batch {
    pub fn label(&self) -> String {
        label(&self.env)
    }

    /// Whether running this batch starts the game: it holds a test that runs.
    pub fn starts_game(&self) -> bool {
        self.tests.iter().any(|t| t.skip_reason().is_none())
    }
}

/// The environment as the summary shows it: `benchmark bahrain 3 laps`.
pub fn label(env: &Env) -> String {
    let mut out = vec![env.kind.name().to_owned()];
    let s = |k: &str| env.str(k).map(str::to_owned);
    match env.kind {
        Kind::None | Kind::Boot => {}
        Kind::Benchmark => {
            out.extend(s("track"));
            out.push(match env.uint("laps").unwrap_or(super::bench::LAPS) {
                1 => "1 lap".to_owned(),
                n => format!("{n} laps"),
            });
            out.extend(env.uint("cars").map(|c| format!("{c} cars")));
            out.extend(s("weather").filter(|w| w != "clear"));
            out.extend(s("box"));
        }
        Kind::Route => {
            if let Some(Value::Array(r)) = env.params.get("routes") {
                out.push(r.iter().filter_map(Value::as_str).collect::<Vec<_>>().join(" > "));
            }
        }
        Kind::Career => {
            out.extend(s("fixture"));
            out.extend(s("at").map(|a| format!("at {a}")));
        }
        Kind::GrandPrix | Kind::TimeTrial => {
            for k in ["box", "track", "car", "session"] {
                out.extend(s(k));
            }
        }
    }
    out.join(" ")
}

/// A JSON value with object keys sorted, so equal environments give equal keys.
pub fn canonical(v: &Value) -> String {
    match v {
        Value::Object(o) => {
            let sorted: BTreeMap<&String, String> = o.iter().map(|(k, v)| (k, canonical(v))).collect();
            let parts: Vec<String> = sorted
                .iter()
                .map(|(k, v)| format!("{}:{v}", Value::String((*k).clone())))
                .collect();
            format!("{{{}}}", parts.join(","))
        }
        Value::Array(a) => format!("[{}]", a.iter().map(canonical).collect::<Vec<_>>().join(",")),
        other => other.to_string(),
    }
}

pub fn env_key(t: &Test) -> String {
    let env: serde_json::Map<String, Value> =
        t.env.params.iter().map(|(k, v)| (k.clone(), v.clone())).collect();
    let mut addons = t.addons.clone();
    addons.sort();
    addons.dedup();
    let settings = serde_json::to_value(&t.settings).unwrap_or_default();
    let mut key = format!(
        "{} {} addons={} settings={}",
        t.env.kind.name(),
        canonical(&Value::Object(env)),
        addons.join(","),
        canonical(&settings)
    );
    if t.fresh {
        key.push_str(&format!(" fresh={}", t.full_id()));
    }
    key
}

/// The selected tests, batched; cheap environments first, then in the order
/// the files list them.
pub fn plan(suites: &[Suite], select: &Selector) -> Plan {
    let mut batches: Vec<Batch> = Vec::new();
    for t in suites.iter().flat_map(|s| &s.tests).filter(|t| select.matches(t)) {
        let key = env_key(t);
        match batches.iter_mut().find(|b| b.key == key) {
            Some(b) => b.tests.push(t.clone()),
            None => {
                let mut addons = t.addons.clone();
                addons.sort();
                addons.dedup();
                batches.push(Batch {
                    key,
                    env: t.env.clone(),
                    addons,
                    settings: t.settings.clone(),
                    tests: vec![t.clone()],
                })
            }
        }
    }
    batches.sort_by_key(|b| b.env.kind);
    Plan { batches }
}

impl Plan {
    pub fn tests(&self) -> usize {
        self.batches.iter().map(|b| b.tests.len()).sum()
    }

    pub fn game_starts(&self) -> usize {
        self.batches.iter().filter(|b| b.starts_game()).count()
    }

    pub fn headline(&self) -> String {
        let n = self.tests();
        let starts = self.game_starts();
        format!(
            "{n} test{} in {starts} game start{}",
            if n == 1 { "" } else { "s" },
            if starts == 1 { "" } else { "s" }
        )
    }

    /// `swofty test plan`'s listing.
    pub fn text(&self) -> String {
        let mut out = format!("swofty test plan: {}\n", self.headline());
        for b in &self.batches {
            let rows: Vec<(String, String)> = b
                .tests
                .iter()
                .map(|t| {
                    let note = match t.skip_reason() {
                        Some(why) => format!("skipped: {why}"),
                        None => String::new(),
                    };
                    (t.id.clone(), note)
                })
                .collect();
            out.push_str(&rows_text(&b.label(), &rows));
        }
        out
    }

    pub fn json(&self) -> PlanJson {
        PlanJson {
            tests: self.tests(),
            game_starts: self.game_starts(),
            batches: self
                .batches
                .iter()
                .map(|b| BatchJson {
                    env: b.label(),
                    key: b.key.clone(),
                    starts_game: b.starts_game(),
                    addons: b.addons.clone(),
                    tests: b
                        .tests
                        .iter()
                        .map(|t| TestJson {
                            suite: t.suite.clone(),
                            id: t.id.clone(),
                            skipped: t.skip_reason().map(str::to_owned),
                        })
                        .collect(),
                })
                .collect(),
        }
    }
}

#[derive(Debug, Serialize)]
pub struct PlanJson {
    pub tests: usize,
    pub game_starts: usize,
    pub batches: Vec<BatchJson>,
}

#[derive(Debug, Serialize)]
pub struct BatchJson {
    pub env: String,
    pub key: String,
    pub starts_game: bool,
    pub addons: Vec<String>,
    pub tests: Vec<TestJson>,
}

#[derive(Debug, Serialize)]
pub struct TestJson {
    pub suite: String,
    pub id: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub skipped: Option<String>,
}

const LABEL_WIDTH: usize = 34;
const ID_WIDTH: usize = 28;

/// One batch's lines: the environment, dotted out to the first test, then
/// one test per line, each with its note.
pub fn rows_text(label: &str, rows: &[(String, String)]) -> String {
    let mut out = String::new();
    for (i, (id, note)) in rows.iter().enumerate() {
        let left = if i == 0 {
            let dots = LABEL_WIDTH.saturating_sub(label.chars().count() + 1).max(2);
            format!("{label} {}", ".".repeat(dots))
        } else {
            " ".repeat(LABEL_WIDTH.max(label.chars().count() + 3))
        };
        let line = if note.is_empty() {
            format!("  {left} {id}")
        } else {
            format!("  {left} {id:ID_WIDTH$} {note}")
        };
        out.push_str(line.trim_end());
        out.push('\n');
    }
    out
}
