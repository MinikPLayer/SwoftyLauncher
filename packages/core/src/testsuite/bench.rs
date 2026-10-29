//! The game's own benchmark mode: `-benchmark <file>` boots straight into an
//! all-AI race described by `<game>/benchmark/<file>`.

use super::suite::Env;

pub const DIR: &str = "benchmark";
pub const FILE: &str = "swofty_test.xml";
pub const LAPS: u64 = 3;
pub const CARS: u64 = 20;

/// The game arguments that start the race in [`FILE`].
pub fn args() -> Vec<String> {
    vec!["-benchmark".to_owned(), FILE.to_owned()]
}

/// The XML for a `benchmark` environment. `box` names the setup mode the
/// race runs under.
pub fn xml(env: &Env) -> String {
    let weather = match env.str("weather").unwrap_or("clear") {
        "rain" | "wet" => "wet",
        _ => "clear",
    };
    let mut attrs = vec![
        ("name", env.str("track").unwrap_or_default().to_owned()),
        ("laps", env.uint("laps").unwrap_or(LAPS).to_string()),
        ("weather", weather.to_owned()),
        ("num_cars", env.uint("cars").unwrap_or(CARS).to_string()),
        ("camera_mode", env.str("camera").unwrap_or("cycle").to_owned()),
    ];
    if let Some(d) = env.str("driver") {
        attrs.push(("driver", d.to_owned()));
    }
    if let Some(g) = env.uint("grid_pos") {
        attrs.push(("grid_pos", g.to_string()));
    }
    if let Some(b) = env.str("box") {
        attrs.push(("setup_mode", b.to_owned()));
    }
    let attrs: Vec<String> = attrs
        .iter()
        .map(|(k, v)| format!("{k}=\"{}\"", escape(v)))
        .collect();
    format!(
        "<?xml version=\"1.0\" standalone=\"yes\" ?>\n<config infinite_loop=\"false\" show_fps=\"false\" >\n  <track {} />\n</config>\n",
        attrs.join(" ")
    )
}

pub fn escape(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    for c in s.chars() {
        match c {
            '&' => out.push_str("&amp;"),
            '<' => out.push_str("&lt;"),
            '>' => out.push_str("&gt;"),
            '"' => out.push_str("&quot;"),
            '\'' => out.push_str("&apos;"),
            c => out.push(c),
        }
    }
    out
}
