//! The `career` environment: the save sandbox, a fixture's saves copied into
//! it, then the test agent continues the fixture's career to the place the
//! test names and the CLI waits for the agent to say it is there.

use serde_json::{Value, json};

use super::suite::Env;

/// Where a career environment starts: `at` in the test file.
pub const AT: [&str; 3] = ["team_hq", "weekend_hub", "session"];
pub const DEFAULT_AT: &str = "team_hq";

/// The agent logs this once the career is at the place asked for:
/// `[test] career ready at team_hq (slot 3)`.
pub const READY_PREFIX: &str = "[test] career ready at ";

pub fn at(env: &Env) -> &str {
    env.str("at").unwrap_or(DEFAULT_AT)
}

/// The agent command for the environment, `career load <slot|newest> at
/// <at>` as `swofty test shell` reads it: continue the career in `slot`, or
/// the newest one when the test names none, in the fixture's career mode
/// when it names one, and go on to `at`.
pub fn agent_command(env: &Env, mode: Option<&str>) -> (String, Value) {
    let mut args = json!({
        "op": "load",
        "slot": env.uint("slot").map_or(json!("newest"), |s| json!(s)),
        "at": at(env),
    });
    if let Some(m) = mode.filter(|m| matches!(*m, "driver_career" | "my_team_career")) {
        args["mode"] = json!(m);
    }
    ("career".to_owned(), args)
}

/// The loader.log text that says the environment is ready.
pub fn ready_line(env: &Env) -> String {
    format!("{READY_PREFIX}{}", at(env))
}
