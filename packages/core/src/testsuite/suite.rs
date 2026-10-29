//! Test files (`tests/*.test.json`): loading, validation and selection.
//! Every object is checked key by key, so a typo is an error that names the
//! file, the test and the key instead of a test that silently checks less.

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use regex::Regex;
use serde_json::{Map, Value};

use super::Error;

pub const SCHEMA: u64 = 1;
pub const SUFFIX: &str = ".test.json";
pub const TESTS_DIR: &str = "tests";

#[derive(Debug, Clone, PartialEq)]
pub struct Suite {
    pub file: PathBuf,
    pub id: String,
    pub tests: Vec<Test>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Test {
    pub suite: String,
    pub id: String,
    /// The test file it came from.
    pub file: PathBuf,
    pub tags: Vec<String>,
    pub env: Env,
    pub steps: Vec<Step>,
    pub checks: Vec<Check>,
    pub function: Option<String>,
    pub timeout_s: Option<u64>,
    pub addons: Vec<String>,
    pub settings: BTreeMap<String, BTreeMap<String, Value>>,
    pub fresh: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum Kind {
    None,
    Boot,
    Benchmark,
    Route,
    Career,
    GrandPrix,
    TimeTrial,
}

impl Kind {
    pub const ALL: [Kind; 7] = [
        Kind::None,
        Kind::Boot,
        Kind::Benchmark,
        Kind::Route,
        Kind::Career,
        Kind::GrandPrix,
        Kind::TimeTrial,
    ];

    pub fn name(self) -> &'static str {
        match self {
            Kind::None => "none",
            Kind::Boot => "boot",
            Kind::Benchmark => "benchmark",
            Kind::Route => "route",
            Kind::Career => "career",
            Kind::GrandPrix => "grand_prix",
            Kind::TimeTrial => "time_trial",
        }
    }

    fn parse(name: &str) -> Option<Self> {
        Self::ALL.into_iter().find(|k| k.name() == name)
    }

    fn params(self) -> &'static [(&'static str, Ty, bool)] {
        use Ty::*;
        match self {
            Kind::None | Kind::Boot => &[],
            Kind::Benchmark => &[
                ("track", Str, true),
                ("laps", UInt, false),
                ("cars", UInt, false),
                ("weather", Str, false),
                ("box", Str, false),
                ("grid_pos", UInt, false),
                ("camera", Str, false),
                ("driver", Str, false),
            ],
            Kind::Route => &[("routes", StrList, true)],
            Kind::Career => &[("fixture", Str, true), ("at", Str, false), ("slot", UInt, false)],
            Kind::GrandPrix | Kind::TimeTrial => &[
                ("box", Str, false),
                ("track", Str, false),
                ("car", Str, false),
                ("team", Str, false),
                ("session", Str, false),
                ("weather", Str, false),
            ],
        }
    }

    /// Why a test in this environment can't run yet.
    pub fn needs(self) -> Option<&'static str> {
        match self {
            Kind::None | Kind::Boot | Kind::Benchmark | Kind::Route | Kind::Career => None,
            Kind::GrandPrix | Kind::TimeTrial => Some(NEEDS_SESSION_SETUP),
        }
    }
}

pub const NEEDS_AGENT: &str = "needs the test agent";
pub const NEEDS_SESSION_SETUP: &str = "needs session setup inside the game's screens";

#[derive(Debug, Clone, PartialEq)]
pub struct Env {
    pub kind: Kind,
    pub params: BTreeMap<String, Value>,
}

impl Env {
    pub fn str(&self, key: &str) -> Option<&str> {
        self.params.get(key).and_then(Value::as_str)
    }

    pub fn uint(&self, key: &str) -> Option<u64> {
        self.params.get(key).and_then(Value::as_u64)
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct Step {
    pub action: String,
    pub host: Option<Wait>,
    pub timeout_s: Option<u64>,
    pub text: String,
}

/// The steps the CLI runs itself, from the session's loader.log.
#[derive(Debug, Clone, PartialEq)]
pub enum Wait {
    Event(String),
    Log(String),
}

#[derive(Debug, Clone, PartialEq)]
pub enum Check {
    Log {
        pattern: String,
        count: Count,
    },
    Event {
        id: String,
        count: Count,
    },
    NoCrash,
    Perf {
        p99_max: Option<f64>,
        p99_min: Option<f64>,
    },
    Agent {
        text: String,
    },
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Count {
    pub min: Option<u64>,
    pub max: Option<u64>,
}

impl Count {
    pub fn holds(self, n: u64) -> bool {
        self.min.is_none_or(|m| n >= m) && self.max.is_none_or(|m| n <= m)
    }

    pub fn text(self) -> String {
        match (self.min, self.max) {
            (Some(a), Some(b)) if a == b => format!("exactly {a} time(s)"),
            (Some(a), Some(b)) => format!("{a} to {b} time(s)"),
            (Some(a), None) => format!("at least {a} time(s)"),
            (None, Some(b)) => format!("at most {b} time(s)"),
            (None, None) => "any number of times".into(),
        }
    }
}

impl Test {
    /// Why this test is skipped, if it is.
    pub fn skip_reason(&self) -> Option<&'static str> {
        self.env.kind.needs()
    }

    /// The test talks to the in-game test agent: a step or check the CLI
    /// can't do from the logs, a test function, a route environment or a
    /// career, which the agent continues.
    pub fn needs_agent(&self) -> bool {
        matches!(self.env.kind, Kind::Route | Kind::Career)
            || self.function.is_some()
            || self.steps.iter().any(|s| s.host.is_none())
            || self.checks.iter().any(|c| matches!(c, Check::Agent { .. }))
    }

    pub fn full_id(&self) -> String {
        format!("{}/{}", self.suite, self.id)
    }

    /// The test DLL a `fn` test runs from: `native/tests.dll` next to the
    /// test file, else the only DLL in that folder.
    pub fn dll(&self) -> Option<PathBuf> {
        self.function.as_ref()?;
        let dir = self.file.parent()?.join("native");
        let named = dir.join("tests.dll");
        if named.is_file() {
            return Some(named);
        }
        let mut dlls: Vec<PathBuf> = std::fs::read_dir(&dir)
            .ok()?
            .flatten()
            .map(|e| e.path())
            .filter(|p| p.extension().is_some_and(|x| x.eq_ignore_ascii_case("dll")))
            .collect();
        match dlls.len() {
            1 => dlls.pop(),
            _ => None,
        }
    }
}

#[derive(Debug, Clone, Default)]
pub struct Selector {
    pub tags: Vec<String>,
    pub ids: Vec<String>,
}

impl Selector {
    pub fn matches(&self, t: &Test) -> bool {
        let tagged = self.tags.is_empty() || self.tags.iter().any(|tag| t.tags.contains(tag));
        let named = self.ids.is_empty() || self.ids.iter().any(|id| *id == t.id || *id == t.full_id());
        tagged && named
    }

    pub fn is_empty(&self) -> bool {
        self.tags.is_empty() && self.ids.is_empty()
    }
}

/// The suites under `path`: a `.test.json` file, an addon folder with a
/// `tests/` folder, or a folder of test files.
pub fn load(path: &Path) -> Result<Vec<Suite>, Error> {
    let files = files(path)?;
    if files.is_empty() {
        return Err(Error(format!(
            "{}: no test files (tests/*{SUFFIX}) found",
            path.display()
        )));
    }
    let addon = addon_of(path);
    let mut suites: Vec<Suite> = Vec::new();
    for f in files {
        let text = std::fs::read_to_string(&f).map_err(|e| Error(format!("{}: {e}", f.display())))?;
        let s = parse(&text, &f, addon.as_deref())?;
        if let Some(other) = suites.iter().find(|o| o.id == s.id) {
            return Err(Error(format!(
                "{}: suite {} is also in {}; each suite id belongs to one file",
                f.display(),
                s.id,
                other.file.display()
            )));
        }
        suites.push(s);
    }
    Ok(suites)
}

fn files(path: &Path) -> Result<Vec<PathBuf>, Error> {
    if path.is_file() {
        return Ok(vec![path.to_path_buf()]);
    }
    if !path.is_dir() {
        return Err(Error(format!("{}: no such file or folder", path.display())));
    }
    let dir = if path.join(TESTS_DIR).is_dir() {
        path.join(TESTS_DIR)
    } else {
        path.to_path_buf()
    };
    let rd = std::fs::read_dir(&dir).map_err(|e| Error(format!("{}: {e}", dir.display())))?;
    let mut out: Vec<PathBuf> = rd
        .flatten()
        .map(|e| e.path())
        .filter(|p| p.is_file() && p.to_string_lossy().ends_with(SUFFIX))
        .collect();
    out.sort();
    Ok(out)
}

/// The addon whose folder holds the tests: its id is the default addon set.
fn addon_of(path: &Path) -> Option<String> {
    let start = if path.is_file() { path.parent()? } else { path };
    let dirs = [Some(start), start.parent()];
    dirs.into_iter().flatten().find_map(|d| {
        let text = std::fs::read_to_string(d.join(crate::manifest::MANIFEST_FILE)).ok()?;
        let v: Value = serde_json::from_str(&text).ok()?;
        v.get("id")?.as_str().map(str::to_owned)
    })
}

struct At<'a> {
    file: &'a Path,
    path: String,
}

impl At<'_> {
    fn at(&self, more: &str) -> Self {
        At {
            file: self.file,
            path: if self.path.is_empty() {
                more.to_owned()
            } else {
                format!("{}.{more}", self.path)
            },
        }
    }

    fn err(&self, what: impl std::fmt::Display) -> Error {
        if self.path.is_empty() {
            Error(format!("{}: {what}", self.file.display()))
        } else {
            Error(format!("{}: {}: {what}", self.file.display(), self.path))
        }
    }

    fn object<'v>(&self, v: &'v Value) -> Result<&'v Map<String, Value>, Error> {
        v.as_object()
            .ok_or_else(|| self.err(format!("expected an object, got {}", kind_of(v))))
    }

    fn keys(&self, o: &Map<String, Value>, allowed: &[&str]) -> Result<(), Error> {
        match o.keys().find(|k| !allowed.contains(&k.as_str())) {
            Some(k) if allowed.is_empty() => {
                Err(self.err(format!("unknown key \"{k}\" (this takes no keys)")))
            }
            Some(k) => Err(self.err(format!(
                "unknown key \"{k}\" (expected one of: {})",
                allowed.join(", ")
            ))),
            None => Ok(()),
        }
    }

    fn str<'v>(&self, v: &'v Value) -> Result<&'v str, Error> {
        v.as_str()
            .ok_or_else(|| self.err(format!("expected a string, got {}", kind_of(v))))
    }

    fn uint(&self, v: &Value) -> Result<u64, Error> {
        v.as_u64()
            .ok_or_else(|| self.err(format!("expected a whole number of 0 or more, got {v}")))
    }

    fn num(&self, v: &Value) -> Result<f64, Error> {
        v.as_f64()
            .ok_or_else(|| self.err(format!("expected a number, got {v}")))
    }

    fn bool(&self, v: &Value) -> Result<bool, Error> {
        v.as_bool()
            .ok_or_else(|| self.err(format!("expected true or false, got {v}")))
    }

    fn strs(&self, v: &Value) -> Result<Vec<String>, Error> {
        let a = v
            .as_array()
            .ok_or_else(|| self.err(format!("expected a list of strings, got {}", kind_of(v))))?;
        a.iter()
            .enumerate()
            .map(|(i, x)| self.at(&format!("[{i}]")).str(x).map(str::to_owned))
            .collect()
    }

    fn regex(&self, pattern: &str) -> Result<(), Error> {
        Regex::new(pattern)
            .map(|_| ())
            .map_err(|e| self.err(format!("not a valid regular expression: {e}")))
    }
}

fn kind_of(v: &Value) -> &'static str {
    match v {
        Value::Null => "null",
        Value::Bool(_) => "true or false",
        Value::Number(_) => "a number",
        Value::String(_) => "a string",
        Value::Array(_) => "a list",
        Value::Object(_) => "an object",
    }
}

#[derive(Clone, Copy)]
enum Ty {
    Str,
    UInt,
    StrList,
}

struct Defaults {
    timeout_s: Option<u64>,
    addons: Option<Vec<String>>,
    settings: BTreeMap<String, BTreeMap<String, Value>>,
}

/// Parse one test file. `addon` is the addon whose folder holds it, the
/// default addon set of its tests.
pub fn parse(text: &str, file: &Path, addon: Option<&str>) -> Result<Suite, Error> {
    let root = At {
        file,
        path: String::new(),
    };
    let v: Value = serde_json::from_str(text).map_err(|e| root.err(format!("not valid JSON: {e}")))?;
    let o = root.object(&v)?;
    root.keys(o, &["schema", "suite", "defaults", "tests"])?;
    match o.get("schema") {
        Some(s) if s.as_u64() == Some(SCHEMA) => {}
        Some(s) => {
            return Err(root.at("schema").err(format!(
                "schema {s} is not supported; this launcher reads schema {SCHEMA}"
            )));
        }
        None => return Err(root.err(format!("missing \"schema\": {SCHEMA}"))),
    }
    let suite = match o.get("suite") {
        Some(s) => root.at("suite").str(s)?.to_owned(),
        None => return Err(root.err("missing \"suite\" (the suite's id, usually the addon's)")),
    };
    if !valid_id(&suite) {
        return Err(root.at("suite").err(format!(
            "\"{suite}\" is not a valid id (letters, digits, '.', '-' and '_')"
        )));
    }
    let defaults = match o.get("defaults") {
        Some(d) => defaults(&root.at("defaults"), d)?,
        None => Defaults {
            timeout_s: None,
            addons: None,
            settings: BTreeMap::new(),
        },
    };
    let list = o
        .get("tests")
        .ok_or_else(|| root.err("missing \"tests\""))?
        .as_array()
        .ok_or_else(|| root.at("tests").err("expected a list of tests"))?;
    let mut tests: Vec<Test> = Vec::new();
    for (i, t) in list.iter().enumerate() {
        let name = t.get("id").and_then(Value::as_str).unwrap_or("");
        let at = root.at(&if name.is_empty() {
            format!("tests[{i}]")
        } else {
            format!("tests[{i}] ({name})")
        });
        let t = test(&at, t, &suite, &defaults, addon)?;
        if tests.iter().any(|o| o.id == t.id) {
            return Err(at.err(format!("test id \"{}\" is used twice in this suite", t.id)));
        }
        tests.push(t);
    }
    Ok(Suite {
        file: file.to_path_buf(),
        id: suite,
        tests,
    })
}

fn valid_id(s: &str) -> bool {
    !s.is_empty()
        && s.bytes()
            .all(|b| b.is_ascii_alphanumeric() || matches!(b, b'.' | b'-' | b'_'))
}

fn defaults(at: &At, v: &Value) -> Result<Defaults, Error> {
    let o = at.object(v)?;
    at.keys(o, &["timeout_s", "addons", "settings"])?;
    Ok(Defaults {
        timeout_s: o
            .get("timeout_s")
            .map(|v| at.at("timeout_s").uint(v))
            .transpose()?,
        addons: o.get("addons").map(|v| at.at("addons").strs(v)).transpose()?,
        settings: o
            .get("settings")
            .map(|v| settings(&at.at("settings"), v))
            .transpose()?
            .unwrap_or_default(),
    })
}

fn settings(at: &At, v: &Value) -> Result<BTreeMap<String, BTreeMap<String, Value>>, Error> {
    let o = at.object(v)?;
    let mut out = BTreeMap::new();
    for (addon, opts) in o {
        let opts = at.at(addon).object(opts)?;
        out.insert(
            addon.clone(),
            opts.iter().map(|(k, v)| (k.clone(), v.clone())).collect(),
        );
    }
    Ok(out)
}

const TEST_KEYS: &[&str] = &[
    "id",
    "tags",
    "env",
    "steps",
    "assert",
    "fn",
    "timeout_s",
    "addons",
    "settings",
    "fresh",
];

fn test(at: &At, v: &Value, suite: &str, d: &Defaults, addon: Option<&str>) -> Result<Test, Error> {
    let o = at.object(v)?;
    at.keys(o, TEST_KEYS)?;
    let id = at
        .at("id")
        .str(o.get("id").ok_or_else(|| at.err("missing \"id\""))?)?
        .to_owned();
    if !valid_id(&id) {
        return Err(at.at("id").err(format!(
            "\"{id}\" is not a valid id (letters, digits, '.', '-' and '_')"
        )));
    }
    let env = env(
        &at.at("env"),
        o.get("env")
            .ok_or_else(|| at.err("missing \"env\" (where the test starts, such as {\"kind\": \"boot\"})"))?,
    )?;
    let steps = match o.get("steps") {
        Some(v) => {
            let list = v
                .as_array()
                .ok_or_else(|| at.at("steps").err("expected a list of steps"))?;
            list.iter()
                .enumerate()
                .map(|(i, s)| step(&at.at(&format!("steps[{i}]")), s))
                .collect::<Result<Vec<_>, _>>()?
        }
        None => Vec::new(),
    };
    let checks = match o.get("assert") {
        Some(v) => {
            let list = v
                .as_array()
                .ok_or_else(|| at.at("assert").err("expected a list of checks"))?;
            list.iter()
                .enumerate()
                .map(|(i, c)| check(&at.at(&format!("assert[{i}]")), c))
                .collect::<Result<Vec<_>, _>>()?
        }
        None => Vec::new(),
    };
    let function = o
        .get("fn")
        .map(|v| at.at("fn").str(v).map(str::to_owned))
        .transpose()?;
    if steps.is_empty() && checks.is_empty() && function.is_none() {
        return Err(at.err("a test needs \"steps\", \"assert\" or \"fn\""));
    }
    let mut settings_map = d.settings.clone();
    if let Some(v) = o.get("settings") {
        for (addon, opts) in settings(&at.at("settings"), v)? {
            settings_map.entry(addon).or_default().extend(opts);
        }
    }
    let addons = match o.get("addons") {
        Some(v) => at.at("addons").strs(v)?,
        None => d
            .addons
            .clone()
            .unwrap_or_else(|| addon.map(|a| vec![a.to_owned()]).unwrap_or_default()),
    };
    Ok(Test {
        suite: suite.to_owned(),
        id,
        file: at.file.to_path_buf(),
        tags: o
            .get("tags")
            .map(|v| at.at("tags").strs(v))
            .transpose()?
            .unwrap_or_default(),
        env,
        steps,
        checks,
        function,
        timeout_s: o
            .get("timeout_s")
            .map(|v| at.at("timeout_s").uint(v))
            .transpose()?
            .or(d.timeout_s),
        addons,
        settings: settings_map,
        fresh: o
            .get("fresh")
            .map(|v| at.at("fresh").bool(v))
            .transpose()?
            .unwrap_or(false),
    })
}

fn env(at: &At, v: &Value) -> Result<Env, Error> {
    let o = at.object(v)?;
    let names: Vec<&str> = Kind::ALL.iter().map(|k| k.name()).collect();
    let kind = match o.get("kind") {
        Some(k) => {
            let name = at.at("kind").str(k)?;
            Kind::parse(name).ok_or_else(|| {
                at.at("kind").err(format!(
                    "unknown kind \"{name}\" (expected one of: {})",
                    names.join(", ")
                ))
            })?
        }
        None => return Err(at.err(format!("missing \"kind\" (one of: {})", names.join(", ")))),
    };
    let spec = kind.params();
    let allowed: Vec<&str> = std::iter::once("kind").chain(spec.iter().map(|p| p.0)).collect();
    if spec.is_empty()
        && let Some(k) = o.keys().find(|k| *k != "kind")
    {
        return Err(at.err(format!(
            "unknown key \"{k}\" (a {} environment takes only \"kind\")",
            kind.name()
        )));
    }
    at.keys(o, &allowed)?;
    let mut params = BTreeMap::new();
    for &(key, ty, required) in spec {
        let Some(v) = o.get(key) else {
            if required {
                return Err(at.err(format!("a {} environment needs \"{key}\"", kind.name())));
            }
            continue;
        };
        let here = at.at(key);
        match ty {
            Ty::Str => {
                here.str(v)?;
            }
            Ty::UInt => {
                here.uint(v)?;
            }
            Ty::StrList => {
                if here.strs(v)?.is_empty() {
                    return Err(here.err("expected at least one entry"));
                }
            }
        }
        params.insert(key.to_owned(), v.clone());
    }
    if kind == Kind::Career
        && let Some(a) = params.get("at").and_then(Value::as_str)
        && !super::career::AT.contains(&a)
    {
        return Err(at.at("at").err(format!(
            "unknown place \"{a}\" (expected one of: {})",
            super::career::AT.join(", ")
        )));
    }
    if kind == Kind::Benchmark {
        if let Some(w) = params.get("weather").and_then(Value::as_str)
            && !matches!(w, "clear" | "wet" | "rain")
        {
            return Err(at
                .at("weather")
                .err(format!("unknown weather \"{w}\" (expected clear, wet or rain)")));
        }
        for (key, max) in [("cars", 26), ("grid_pos", 26), ("laps", 100)] {
            if let Some(n) = params.get(key).and_then(Value::as_u64)
                && !(1..=max).contains(&n)
            {
                return Err(at.at(key).err(format!("expected 1 to {max}, got {n}")));
            }
        }
    }
    Ok(Env { kind, params })
}

/// Each step's action and the other keys it takes.
const STEPS: &[(&str, &[&str])] = &[
    ("wait", &[]),
    ("route", &[]),
    ("skip", &[]),
    ("advance", &[]),
    ("ai_drives_player", &[]),
    ("pad", &[]),
    ("set", &[]),
    ("read", &["as"]),
    ("sample", &[]),
    ("capture", &["name"]),
    ("save", &[]),
];

fn step(at: &At, v: &Value) -> Result<Step, Error> {
    let o = at.object(v)?;
    let actions: Vec<&str> = STEPS.iter().map(|s| s.0).collect();
    let found: Vec<&str> = o
        .keys()
        .map(String::as_str)
        .filter(|k| actions.contains(k))
        .collect();
    let action = match found.as_slice() {
        [one] => *one,
        [] => {
            return Err(at.err(format!(
                "a step needs one action (one of: {})",
                actions.join(", ")
            )));
        }
        many => return Err(at.err(format!("a step has one action, this has {}", many.join(" and ")))),
    };
    let extra = STEPS.iter().find(|s| s.0 == action).map_or(&[][..], |s| s.1);
    let allowed: Vec<&str> = [action, "timeout_s"]
        .into_iter()
        .chain(extra.iter().copied())
        .collect();
    at.keys(o, &allowed)?;
    let value = &o[action];
    let here = at.at(action);
    let host = match action {
        "wait" => {
            let w = here.object(value)?;
            here.keys(w, &["event", "log"])?;
            match (w.get("event"), w.get("log")) {
                (Some(e), None) => Some(Wait::Event(here.at("event").str(e)?.to_owned())),
                (None, Some(l)) => {
                    let pattern = here.at("log").str(l)?;
                    here.at("log").regex(pattern)?;
                    Some(Wait::Log(pattern.to_owned()))
                }
                _ => return Err(here.err("wait for one of \"event\" or \"log\"")),
            }
        }
        "route" | "skip" | "save" | "capture" | "read" => {
            here.str(value)?;
            None
        }
        "ai_drives_player" => {
            here.bool(value)?;
            None
        }
        "advance" => {
            let a = here.object(value)?;
            here.keys(a, &["days"])?;
            here.at("days")
                .uint(a.get("days").ok_or_else(|| here.err("missing \"days\""))?)?;
            None
        }
        "sample" => {
            let s = here.object(value)?;
            here.keys(s, &["read", "every_ms", "for_s", "as"])?;
            for key in ["read", "as"] {
                here.at(key)
                    .str(s.get(key).ok_or_else(|| here.err(format!("missing \"{key}\"")))?)?;
            }
            for key in ["every_ms", "for_s"] {
                if let Some(n) = s.get(key) {
                    here.at(key).uint(n)?;
                }
            }
            None
        }
        _ => {
            here.object(value)?;
            None
        }
    };
    for key in extra {
        if let Some(v) = o.get(*key) {
            at.at(key).str(v)?;
        }
    }
    Ok(Step {
        action: action.to_owned(),
        host,
        timeout_s: o
            .get("timeout_s")
            .map(|v| at.at("timeout_s").uint(v))
            .transpose()?,
        text: v.to_string(),
    })
}

/// Each check's kind and the other keys it takes.
const CHECKS: &[(&str, &[&str])] = &[
    ("log", &["min", "max", "count"]),
    ("event", &["min", "max", "count"]),
    ("no_crash", &[]),
    ("perf", &[]),
    ("value", &["between", "greater_than", "less_than", "equals"]),
    ("series", &["rises_by", "falls_by"]),
];

fn check(at: &At, v: &Value) -> Result<Check, Error> {
    let o = at.object(v)?;
    let kinds: Vec<&str> = CHECKS.iter().map(|c| c.0).collect();
    let found: Vec<&str> = o
        .keys()
        .map(String::as_str)
        .filter(|k| kinds.contains(k))
        .collect();
    let kind = match found.as_slice() {
        [one] => *one,
        [] => return Err(at.err(format!("a check needs one of: {}", kinds.join(", ")))),
        many => {
            return Err(at.err(format!(
                "a check checks one thing, this has {}",
                many.join(" and ")
            )));
        }
    };
    let extra = CHECKS.iter().find(|c| c.0 == kind).map_or(&[][..], |c| c.1);
    let allowed: Vec<&str> = std::iter::once(kind).chain(extra.iter().copied()).collect();
    at.keys(o, &allowed)?;
    let value = &o[kind];
    let here = at.at(kind);
    let count = || -> Result<Count, Error> {
        let get = |k: &str| o.get(k).map(|v| at.at(k).uint(v)).transpose();
        let (min, max, exact) = (get("min")?, get("max")?, get("count")?);
        if exact.is_some() && (min.is_some() || max.is_some()) {
            return Err(at.err("\"count\" is an exact count; use it without \"min\" and \"max\""));
        }
        let c = match exact {
            Some(n) => Count {
                min: Some(n),
                max: Some(n),
            },
            None if min.is_none() && max.is_none() => Count {
                min: Some(1),
                max: None,
            },
            None => Count { min, max },
        };
        if let (Some(a), Some(b)) = (c.min, c.max)
            && a > b
        {
            return Err(at.err(format!("\"min\" {a} is more than \"max\" {b}")));
        }
        Ok(c)
    };
    Ok(match kind {
        "log" => {
            let pattern = here.str(value)?;
            here.regex(pattern)?;
            Check::Log {
                pattern: pattern.to_owned(),
                count: count()?,
            }
        }
        "event" => Check::Event {
            id: here.str(value)?.to_owned(),
            count: count()?,
        },
        "no_crash" => match here.bool(value)? {
            true => Check::NoCrash,
            false => return Err(here.err("leave the check out instead of setting it to false")),
        },
        "perf" => {
            let p = here.object(value)?;
            here.keys(p, &["p99_ms"])?;
            let at99 = here.at("p99_ms");
            let p99 = at99.object(p.get("p99_ms").ok_or_else(|| here.err("missing \"p99_ms\""))?)?;
            at99.keys(p99, &["max", "min"])?;
            let p99_max = p99.get("max").map(|v| at99.at("max").num(v)).transpose()?;
            let p99_min = p99.get("min").map(|v| at99.at("min").num(v)).transpose()?;
            if p99_max.is_none() && p99_min.is_none() {
                return Err(at99.err("give \"max\", \"min\" or both"));
            }
            Check::Perf { p99_max, p99_min }
        }
        _ => {
            here.str(value)?;
            Check::Agent { text: v.to_string() }
        }
    })
}
