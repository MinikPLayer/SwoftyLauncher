//! A run's report: `report.json`, `junit.xml` and the summary.

use serde::{Deserialize, Serialize};

use super::checks::Failure;
use super::plan::rows_text;

pub const SCHEMA: u32 = 1;
pub const JSON_FILE: &str = "report.json";
pub const JUNIT_FILE: &str = "junit.xml";
pub const SUMMARY_FILE: &str = "summary.txt";

/// The exit codes of `swofty test run`.
pub mod exit {
    pub const PASS: u8 = 0;
    pub const FAIL: u8 = 1;
    pub const INFRA: u8 = 2;
    pub const BUSY: u8 = 3;
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Status {
    Pass,
    Fail,
    Error,
    Skipped,
    NotRun,
}

impl Status {
    pub fn word(self) -> &'static str {
        match self {
            Status::Pass => "pass",
            Status::Fail => "FAIL",
            Status::Error => "ERROR",
            Status::Skipped => "skipped",
            Status::NotRun => "not run",
        }
    }
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct StepReport {
    pub step: String,
    pub status: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub ms: Option<u64>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct TestReport {
    pub suite: String,
    pub id: String,
    pub env: String,
    pub batch: usize,
    pub status: Status,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub ms: Option<u64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub reason: Option<String>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub failures: Vec<Failure>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub steps: Vec<StepReport>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct BatchReport {
    pub index: usize,
    pub env: String,
    pub key: String,
    pub addons: Vec<String>,
    pub started_game: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub ready_ms: Option<u64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub ms: Option<u64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub session: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub end: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub error: Option<String>,
    /// The game folder was put back after the batch: the rollback finished
    /// and nothing the run staged is left.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub clean: Option<bool>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub notes: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Report {
    pub schema: u32,
    pub run: String,
    pub launcher: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub game_build: Option<String>,
    pub out: String,
    pub ms: u64,
    pub game_starts: usize,
    pub batches: Vec<BatchReport>,
    pub tests: Vec<TestReport>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub errors: Vec<String>,
    pub exit_code: u8,
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Totals {
    pub pass: usize,
    pub fail: usize,
    pub error: usize,
    pub skipped: usize,
    pub not_run: usize,
}

impl Report {
    pub fn new(run: &str, out: &str) -> Self {
        Report {
            schema: SCHEMA,
            run: run.to_owned(),
            launcher: crate::update::current_version().to_string(),
            game_build: None,
            out: out.to_owned(),
            ms: 0,
            game_starts: 0,
            batches: Vec::new(),
            tests: Vec::new(),
            errors: Vec::new(),
            exit_code: exit::PASS,
        }
    }

    pub fn totals(&self) -> Totals {
        let mut t = Totals::default();
        for r in &self.tests {
            match r.status {
                Status::Pass => t.pass += 1,
                Status::Fail => t.fail += 1,
                Status::Error => t.error += 1,
                Status::Skipped => t.skipped += 1,
                Status::NotRun => t.not_run += 1,
            }
        }
        t
    }

    /// 2 when the harness or the game let the run down (an error, a test
    /// that could not run, a game folder left unclean), else 1 when a test
    /// failed, else 0.
    pub fn compute_exit(&self) -> u8 {
        let t = self.totals();
        let unclean = self.batches.iter().any(|b| b.clean == Some(false));
        if !self.errors.is_empty() || t.error > 0 || t.not_run > 0 || unclean {
            exit::INFRA
        } else if t.fail > 0 {
            exit::FAIL
        } else {
            exit::PASS
        }
    }

    pub fn json(&self) -> String {
        serde_json::to_string_pretty(self).unwrap_or_default() + "\n"
    }

    /// The human view, as `swofty test run` prints it.
    pub fn summary(&self) -> String {
        let n = self.tests.len();
        let mut out = format!(
            "swofty test: {n} test{} in {} game start{}\n",
            if n == 1 { "" } else { "s" },
            self.game_starts,
            if self.game_starts == 1 { "" } else { "s" }
        );
        for b in &self.batches {
            let rows: Vec<(String, String)> = self
                .tests
                .iter()
                .filter(|t| t.batch == b.index)
                .map(|t| (t.id.clone(), row_note(t)))
                .collect();
            out.push_str(&rows_text(&b.env, &rows));
            if let Some(e) = &b.error {
                out.push_str(&format!("      {e}\n"));
            }
            for note in &b.notes {
                out.push_str(&format!("      {note}\n"));
            }
        }
        for e in &self.errors {
            out.push_str(&format!("error: {e}\n"));
        }
        let t = self.totals();
        let mut parts = vec![format!("{} passed", t.pass)];
        for (k, word) in [
            (t.fail, "failed"),
            (t.error, "errors"),
            (t.not_run, "not run"),
            (t.skipped, "skipped"),
        ] {
            if k > 0 {
                parts.push(format!("{k} {word}"));
            }
        }
        out.push_str(&format!(
            "{}  ·  report: {}/{JSON_FILE}, {JUNIT_FILE}\n",
            parts.join(", "),
            self.out.trim_end_matches('/')
        ));
        out
    }

    pub fn junit(&self) -> String {
        let mut suites: Vec<&str> = Vec::new();
        for t in &self.tests {
            if !suites.contains(&t.suite.as_str()) {
                suites.push(&t.suite);
            }
        }
        let tot = self.totals();
        let mut out = String::from("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        out.push_str(&format!(
            "<testsuites name=\"swofty test\" tests=\"{}\" failures=\"{}\" errors=\"{}\" skipped=\"{}\" time=\"{}\">\n",
            self.tests.len(),
            tot.fail,
            tot.error + tot.not_run,
            tot.skipped,
            secs(self.ms)
        ));
        for s in suites {
            let tests: Vec<&TestReport> = self.tests.iter().filter(|t| t.suite == s).collect();
            let count = |st: &[Status]| tests.iter().filter(|t| st.contains(&t.status)).count();
            let ms: u64 = tests.iter().filter_map(|t| t.ms).sum();
            out.push_str(&format!(
                "  <testsuite name=\"{}\" tests=\"{}\" failures=\"{}\" errors=\"{}\" skipped=\"{}\" time=\"{}\">\n",
                xml(s),
                tests.len(),
                count(&[Status::Fail]),
                count(&[Status::Error, Status::NotRun]),
                count(&[Status::Skipped]),
                secs(ms)
            ));
            for t in tests {
                let open = format!(
                    "    <testcase classname=\"{}\" name=\"{}\" time=\"{}\"",
                    xml(&t.suite),
                    xml(&t.id),
                    secs(t.ms.unwrap_or(0))
                );
                let body = match t.status {
                    Status::Pass => None,
                    Status::Fail => {
                        let first = t.failures.first();
                        let message = first
                            .map(|f| f.text())
                            .or_else(|| t.reason.clone())
                            .unwrap_or_default();
                        let detail: Vec<String> = t
                            .failures
                            .iter()
                            .map(|f| {
                                let ev = f
                                    .evidence
                                    .as_deref()
                                    .map(|e| format!(" ({e})"))
                                    .unwrap_or_default();
                                format!("{}: {}{ev}", f.check, f.text())
                            })
                            .collect();
                        Some(format!(
                            "<failure message=\"{}\">{}</failure>",
                            xml(&message),
                            xml(&detail.join("\n"))
                        ))
                    }
                    Status::Error | Status::NotRun => Some(format!(
                        "<error message=\"{}\"/>",
                        xml(t.reason.as_deref().unwrap_or("error"))
                    )),
                    Status::Skipped => Some(format!(
                        "<skipped message=\"{}\"/>",
                        xml(t.reason.as_deref().unwrap_or("skipped"))
                    )),
                };
                match body {
                    None => out.push_str(&format!("{open}/>\n")),
                    Some(b) => out.push_str(&format!("{open}>\n      {b}\n    </testcase>\n")),
                }
            }
            out.push_str("  </testsuite>\n");
        }
        out.push_str("</testsuites>\n");
        out
    }
}

fn row_note(t: &TestReport) -> String {
    let word = t.status.word();
    let time = t.ms.filter(|_| t.status == Status::Pass).map(duration);
    let detail = match t.status {
        Status::Pass => None,
        Status::Fail => t.failures.first().map(Failure::text).or_else(|| t.reason.clone()),
        _ => t.reason.clone(),
    };
    match (time, detail) {
        (Some(d), _) => format!("{word:6} ({d})"),
        (None, Some(d)) if t.status == Status::Skipped => format!("{word}: {d}"),
        (None, Some(d)) => format!("{word:6} {d}"),
        (None, None) => word.to_owned(),
    }
}

/// `1 m 52 s`, `40 s`.
pub fn duration(ms: u64) -> String {
    let s = (ms + 500) / 1000;
    match s {
        0..60 => format!("{s} s"),
        _ => format!("{} m {} s", s / 60, s % 60),
    }
}

fn secs(ms: u64) -> String {
    format!("{:.3}", ms as f64 / 1000.0)
}

pub fn xml(s: &str) -> String {
    super::bench::escape(s)
}
