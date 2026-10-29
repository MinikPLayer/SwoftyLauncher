//! Checks read from a session's files: loader.log, perf.log and how the
//! session ended.

use regex::Regex;
use serde::{Deserialize, Serialize};

use super::suite::{Check, Kind, Wait};

/// The loader's first line in a session.
pub const ATTACHED: &str = "attached, pid ";
/// The core's flow line for the main menu.
pub const MAIN_MENU: &str = "[career] flow \"#957880c6\"";
/// The core's flow line for the title screen, which waits for a button.
pub const TITLE: &str = "[career] flow \"#a4d3a4b2\"";
const EVENTS: &str = "[events] ";

/// What a test's environment waits for in loader.log before its steps run.
pub fn ready_line(kind: Kind) -> &'static str {
    match kind {
        Kind::Boot | Kind::Route => MAIN_MENU,
        _ => ATTACHED,
    }
}

pub fn ready_text(kind: Kind) -> &'static str {
    match kind {
        Kind::Boot | Kind::Route => "the main menu",
        _ => "the loader's attached line",
    }
}

pub fn count_log(log: &str, pattern: &str) -> u64 {
    match Regex::new(pattern) {
        Ok(re) => log.lines().filter(|l| re.is_match(l)).count() as u64,
        Err(_) => 0,
    }
}

/// How often the core logged event `id`: lines `[events] <id>` followed by
/// its details, not the lines about subscribing to it.
pub fn count_event(log: &str, id: &str) -> u64 {
    log.lines().filter(|l| is_event(l, id)).count() as u64
}

fn is_event(line: &str, id: &str) -> bool {
    let Some(at) = line.find(EVENTS) else {
        return false;
    };
    let Some(rest) = line[at + EVENTS.len()..].strip_prefix(id) else {
        return false;
    };
    match rest.chars().next() {
        None => true,
        Some(c) if c.is_ascii_alphanumeric() || c == '_' || c == '.' => false,
        Some(_) => !NOT_FIRED.iter().any(|n| rest.starts_with(n)),
    }
}

/// The core's lines about an event's hook or handlers, not the event itself.
const NOT_FIRED: &[&str] = &[
    " has no ",
    " hooked at ",
    " signature matched ",
    " resolved to ",
    " at base+",
];

pub fn waited(log: &str, wait: &Wait) -> bool {
    match wait {
        Wait::Event(id) => count_event(log, id) > 0,
        Wait::Log(p) => count_log(log, p) > 0,
    }
}

pub fn wait_text(wait: &Wait) -> String {
    match wait {
        Wait::Event(id) => format!("event {id}"),
        Wait::Log(p) => format!("a log line /{p}/"),
    }
}

/// The frame time p99 of perf.log's last report.
pub fn p99(perf: &str) -> Option<f64> {
    let re = Regex::new(r"frame times: .*\bp99 ([0-9]+(?:\.[0-9]+)?) ms").ok()?;
    perf.lines()
        .rev()
        .find_map(|l| re.captures(l))
        .and_then(|c| c[1].parse().ok())
}

#[derive(Debug, Clone, Default)]
pub struct Evidence {
    pub log: String,
    pub perf: Option<String>,
    /// How the session ended when it crashed.
    pub crash: Option<String>,
    pub log_path: Option<String>,
    pub perf_path: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Failure {
    pub check: String,
    pub expected: String,
    pub actual: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub evidence: Option<String>,
}

impl Failure {
    pub fn text(&self) -> String {
        format!("expected {}, got {}", self.expected, self.actual)
    }
}

pub fn name(c: &Check) -> String {
    match c {
        Check::Log { pattern, .. } => format!("log /{pattern}/"),
        Check::Event { id, .. } => format!("event {id}"),
        Check::NoCrash => "no_crash".into(),
        Check::Perf { .. } => "perf p99_ms".into(),
        Check::Agent { text } => text.clone(),
    }
}

/// `Ok` when the check holds; agent checks never reach here.
pub fn evaluate(c: &Check, e: &Evidence) -> Result<(), Failure> {
    let fail = |expected: String, actual: String, evidence: Option<&String>| Failure {
        check: name(c),
        expected,
        actual,
        evidence: evidence.cloned(),
    };
    match c {
        Check::Log { pattern, count } => {
            let n = count_log(&e.log, pattern);
            match count.holds(n) {
                true => Ok(()),
                false => Err(fail(
                    format!("log /{pattern}/ {}", count.text()),
                    times(n),
                    e.log_path.as_ref(),
                )),
            }
        }
        Check::Event { id, count } => {
            let n = count_event(&e.log, id);
            match count.holds(n) {
                true => Ok(()),
                false => Err(fail(
                    format!("event {id} {}", count.text()),
                    times(n),
                    e.log_path.as_ref(),
                )),
            }
        }
        Check::NoCrash => match &e.crash {
            None => Ok(()),
            Some(how) => Err(fail("no crash".into(), how.clone(), e.log_path.as_ref())),
        },
        Check::Perf { p99_max, p99_min } => {
            let bounds = match (p99_min, p99_max) {
                (Some(a), Some(b)) => format!("p99 frame time {a} to {b} ms"),
                (None, Some(b)) => format!("p99 frame time at most {b} ms"),
                (Some(a), None) => format!("p99 frame time at least {a} ms"),
                (None, None) => "a p99 frame time".into(),
            };
            let Some(p) = e.perf.as_deref().and_then(p99) else {
                return Err(fail(
                    bounds,
                    "no frame times (the session left no perf.log report)".into(),
                    e.perf_path.as_ref(),
                ));
            };
            let ok = p99_max.is_none_or(|m| p <= m) && p99_min.is_none_or(|m| p >= m);
            match ok {
                true => Ok(()),
                false => Err(fail(bounds, format!("{p} ms"), e.perf_path.as_ref())),
            }
        }
        Check::Agent { text } => Err(fail(text.clone(), "not run".into(), None)),
    }
}

fn times(n: u64) -> String {
    match n {
        1 => "1 time".into(),
        n => format!("{n} times"),
    }
}
