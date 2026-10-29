use std::path::{Path, PathBuf};
use std::time::Duration;

use super::checks::{self, Evidence};
use super::lock::{self, GameLock};
use super::plan;
use super::report::{BatchReport, Report, Status, TestReport, exit};
use super::suite::{self, Check, Count, Kind, Selector, Wait};

const DOC_EXAMPLE: &str = r#"{
  "schema": 1,
  "suite": "swofty.rnd-tyres-thermal",
  "tests": [
    {
      "id": "tyre_temps_rise_on_track",
      "tags": ["smoke"],
      "env": { "kind": "benchmark", "track": "bahrain", "laps": 3, "cars": 10 },
      "steps": [
        { "wait": { "event": "race.start" }, "timeout_s": 90 },
        { "sample": { "read": "car[0].tyres[fl].temperature", "every_ms": 500, "for_s": 60, "as": "fl" } }
      ],
      "assert": [
        { "series": "fl", "rises_by": 8 },
        { "log": "\\[rnd-tyres\\] thermal model on" },
        { "no_crash": true }
      ]
    },
    {
      "id": "rnd_tab_hidden_in_team_hq",
      "env": { "kind": "career", "fixture": "fixtures/my-team-r3", "at": "team_hq" },
      "fn": "rnd_tab_hidden"
    }
  ]
}"#;

fn parse(text: &str) -> Result<suite::Suite, String> {
    suite::parse(text, Path::new("tests/x.test.json"), Some("my.addon")).map_err(|e| e.0)
}

fn one_test(test: &str) -> Result<suite::Test, String> {
    parse(&format!(r#"{{"schema":1,"suite":"s","tests":[{test}]}}"#)).map(|s| s.tests[0].clone())
}

fn temp(name: &str) -> PathBuf {
    let dir = std::env::temp_dir().join(format!("swofty-testsuite-{name}-{}", std::process::id()));
    std::fs::remove_dir_all(&dir).ok();
    std::fs::create_dir_all(&dir).unwrap();
    dir
}

#[test]
fn the_documented_test_file_parses() {
    let s = parse(DOC_EXAMPLE).unwrap();
    assert_eq!(s.id, "swofty.rnd-tyres-thermal");
    assert_eq!(s.tests.len(), 2);
    let t = &s.tests[0];
    assert_eq!(t.env.kind, Kind::Benchmark);
    assert_eq!(t.env.str("track"), Some("bahrain"));
    assert_eq!(t.steps[0].host, Some(Wait::Event("race.start".into())));
    assert_eq!(t.steps[0].timeout_s, Some(90));
    assert_eq!(t.steps[1].host, None, "sample needs the agent");
    assert!(matches!(&t.checks[1], Check::Log { count, .. } if *count == Count { min: Some(1), max: None }));
    assert_eq!(t.checks[2], Check::NoCrash);
    assert_eq!(t.addons, ["my.addon"], "the addon whose folder holds the tests");
    assert_eq!(t.skip_reason(), None, "the agent runs sample");
    assert!(t.needs_agent());
    assert_eq!(s.tests[1].skip_reason(), None, "the agent continues careers");
    assert!(s.tests[1].needs_agent());
}

#[test]
fn every_documented_step_and_check_parses() {
    let steps = [
        r#"{ "wait": { "event": "career.season.start" }, "timeout_s": 120 }"#,
        r#"{ "route": "contracts" }"#,
        r#"{ "skip": "session" }"#,
        r#"{ "advance": { "days": 7 } }"#,
        r#"{ "ai_drives_player": true }"#,
        r#"{ "pad": { "press": "mfd_fuel", "then": "right" } }"#,
        r#"{ "set": { "car[player].f1_aero.lift_scale": 1.05 } }"#,
        r#"{ "read": "career.money", "as": "money_before" }"#,
        r#"{ "capture": "frame", "name": "team_hq" }"#,
        r#"{ "save": "now" }"#,
    ];
    let checks = [
        r#"{ "log": "\\[career\\] season 2019 starts", "min": 1 }"#,
        r#"{ "value": "car[0].chassis.mass", "between": [790, 830] }"#,
        r#"{ "value": "money_after", "greater_than": "money_before" }"#,
        r#"{ "event": "car.reset", "count": 2 }"#,
        r#"{ "no_crash": true }"#,
        r#"{ "perf": { "p99_ms": { "max": 40 } } }"#,
    ];
    let t = one_test(&format!(
        r#"{{"id":"all","env":{{"kind":"boot"}},"steps":[{}],"assert":[{}]}}"#,
        steps.join(","),
        checks.join(",")
    ))
    .unwrap();
    assert_eq!(t.steps.len(), steps.len());
    assert_eq!(t.checks.len(), checks.len());
    assert_eq!(t.steps.iter().filter(|s| s.host.is_some()).count(), 1);
    assert!(matches!(
        t.checks[3],
        Check::Event {
            count: Count {
                min: Some(2),
                max: Some(2)
            },
            ..
        }
    ));
    assert_eq!(
        t.checks[5],
        Check::Perf {
            p99_max: Some(40.0),
            p99_min: None
        }
    );
    for env in [
        r#"{ "kind": "benchmark", "track": "monaco", "laps": 2, "cars": 20, "weather": "rain", "box": "swofty.classic-seasons-2017/f1_2017" }"#,
        r#"{ "kind": "route", "routes": ["career", "team_hq", "contracts"] }"#,
        r#"{ "kind": "career", "fixture": "fixtures/progressive-2018-r10", "at": "weekend_hub" }"#,
        r#"{ "kind": "time_trial", "box": "mod.season-2008/f1_2008", "track": "venue/albert_park", "car": "mod.season-2008/ferrari" }"#,
        r#"{ "kind": "none" }"#,
    ] {
        one_test(&format!(
            r#"{{"id":"e","env":{env},"assert":[{{"no_crash":true}}]}}"#
        ))
        .unwrap();
    }
}

#[test]
fn unknown_keys_are_refused_with_where_they_are() {
    let cases = [
        (
            r#"{"schema":1,"suite":"s","tests":[],"test":[]}"#,
            "tests/x.test.json: unknown key \"test\" (expected one of: schema, suite, defaults, tests)",
        ),
        (
            r#"{"schema":1,"suite":"s","tests":[{"id":"a","env":{"kind":"boot"},"asert":[]}]}"#,
            "tests[0] (a): unknown key \"asert\"",
        ),
        (
            r#"{"schema":1,"suite":"s","tests":[{"id":"a","env":{"kind":"boot","track":"x"},"assert":[{"no_crash":true}]}]}"#,
            "tests[0] (a).env: unknown key \"track\" (a boot environment takes only \"kind\")",
        ),
        (
            r#"{"schema":1,"suite":"s","tests":[{"id":"a","env":{"kind":"benchmark","track":"x","lap":2},"assert":[{"no_crash":true}]}]}"#,
            "env: unknown key \"lap\" (expected one of: kind, track, laps, cars, weather, box, grid_pos, camera, driver)",
        ),
        (
            r#"{"schema":1,"suite":"s","tests":[{"id":"a","env":{"kind":"boot"},"steps":[{"wait":{"event":"x"},"timeot_s":3}]}]}"#,
            "tests[0] (a).steps[0]: unknown key \"timeot_s\" (expected one of: wait, timeout_s)",
        ),
        (
            r#"{"schema":1,"suite":"s","tests":[{"id":"a","env":{"kind":"boot"},"steps":[{"sample":{"read":"x","as":"y","evry_ms":5}}]}]}"#,
            "steps[0].sample: unknown key \"evry_ms\"",
        ),
        (
            r#"{"schema":1,"suite":"s","tests":[{"id":"a","env":{"kind":"boot"},"assert":[{"log":"x","mni":1}]}]}"#,
            "assert[0]: unknown key \"mni\" (expected one of: log, min, max, count)",
        ),
        (
            r#"{"schema":1,"suite":"s","tests":[{"id":"a","env":{"kind":"boot"},"assert":[{"perf":{"p99_ms":{"maximum":4}}}]}]}"#,
            "assert[0].perf.p99_ms: unknown key \"maximum\" (expected one of: max, min)",
        ),
    ];
    for (text, want) in cases {
        let err = parse(text).unwrap_err();
        assert!(err.contains(want), "{err}\n  wanted: {want}");
    }
}

#[test]
fn bad_values_say_what_was_expected() {
    let cases = [
        (
            r#"{"schema":2,"suite":"s","tests":[]}"#,
            "schema 2 is not supported",
        ),
        (r#"{"suite":"s","tests":[]}"#, "missing \"schema\": 1"),
        (
            r#"{"schema":1,"suite":"s","tests":[{"id":"a"}]}"#,
            "missing \"env\"",
        ),
        (
            r#"{"schema":1,"suite":"s","tests":[{"id":"a","env":{"kind":"race"},"assert":[]}]}"#,
            "unknown kind \"race\" (expected one of: none, boot, benchmark",
        ),
        (
            r#"{"schema":1,"suite":"s","tests":[{"id":"a","env":{"kind":"benchmark"},"assert":[]}]}"#,
            "a benchmark environment needs \"track\"",
        ),
        (
            r#"{"schema":1,"suite":"s","tests":[{"id":"a","env":{"kind":"benchmark","track":"x","cars":40},"assert":[]}]}"#,
            "env.cars: expected 1 to 26, got 40",
        ),
        (
            r#"{"schema":1,"suite":"s","tests":[{"id":"a","env":{"kind":"boot"}}]}"#,
            "a test needs \"steps\", \"assert\" or \"fn\"",
        ),
        (
            r#"{"schema":1,"suite":"s","tests":[{"id":"a","env":{"kind":"boot"},"assert":[{"log":"(unclosed"}]}]}"#,
            "assert[0].log: not a valid regular expression",
        ),
        (
            r#"{"schema":1,"suite":"s","tests":[{"id":"a","env":{"kind":"boot"},"assert":[{"log":"x","count":1,"min":1}]}]}"#,
            "\"count\" is an exact count",
        ),
        (
            r#"{"schema":1,"suite":"s","tests":[{"id":"a","env":{"kind":"boot"},"steps":[{"route":"a","skip":"session"}]}]}"#,
            "a step has one action, this has route and skip",
        ),
        (
            r#"{"schema":1,"suite":"s","tests":[{"id":"a","env":{"kind":"boot"},"assert":[{"no_crash":true}]},{"id":"a","env":{"kind":"boot"},"assert":[{"no_crash":true}]}]}"#,
            "test id \"a\" is used twice",
        ),
        (
            r#"{"schema":1,"suite":"s","tests":[{"id":"a b"}]}"#,
            "\"a b\" is not a valid id",
        ),
        ("{", "not valid JSON"),
    ];
    for (text, want) in cases {
        let err = parse(text).unwrap_err();
        assert!(err.contains(want), "{err}\n  wanted: {want}");
    }
}

#[test]
fn defaults_fill_what_a_test_leaves_out() {
    let s = parse(
        r#"{"schema":1,"suite":"s","defaults":{"timeout_s":60,"addons":["a.one"],"settings":{"a.one":{"x":1}}},
            "tests":[
              {"id":"t1","env":{"kind":"boot"},"assert":[{"no_crash":true}]},
              {"id":"t2","env":{"kind":"boot"},"timeout_s":5,"addons":["a.two"],"settings":{"a.one":{"y":2}},"assert":[{"no_crash":true}]}
            ]}"#,
    )
    .unwrap();
    assert_eq!(s.tests[0].timeout_s, Some(60));
    assert_eq!(s.tests[0].addons, ["a.one"]);
    assert_eq!(s.tests[1].timeout_s, Some(5));
    assert_eq!(s.tests[1].addons, ["a.two"]);
    assert_eq!(s.tests[1].settings["a.one"].len(), 2);
}

#[test]
fn loading_finds_an_addons_tests_folder() {
    let root = temp("load");
    let addon = root.join("my-addon");
    std::fs::create_dir_all(addon.join("tests")).unwrap();
    std::fs::write(addon.join("swofty.json"), r#"{"id":"me.addon"}"#).unwrap();
    std::fs::write(addon.join("tests/b.test.json"), DOC_EXAMPLE).unwrap();
    std::fs::write(
        addon.join("tests/a.test.json"),
        r#"{"schema":1,"suite":"me.smoke","tests":[{"id":"boots","tags":["smoke"],"env":{"kind":"boot"},"assert":[{"no_crash":true}]}]}"#,
    )
    .unwrap();
    std::fs::write(addon.join("tests/notes.json"), "not a test file").unwrap();
    let suites = suite::load(&addon).unwrap();
    assert_eq!(
        suites.iter().map(|s| s.id.as_str()).collect::<Vec<_>>(),
        ["me.smoke", "swofty.rnd-tyres-thermal"]
    );
    assert_eq!(suites[0].tests[0].addons, ["me.addon"]);
    let one = suite::load(&addon.join("tests/a.test.json")).unwrap();
    assert_eq!(
        one[0].tests[0].addons,
        ["me.addon"],
        "a single file finds its addon too"
    );
    let empty = root.join("empty");
    std::fs::create_dir_all(&empty).unwrap();
    assert!(suite::load(&empty).unwrap_err().0.contains("no test files"));
    assert!(
        suite::load(&root.join("nope"))
            .unwrap_err()
            .0
            .contains("no such file")
    );
    std::fs::write(addon.join("tests/c.test.json"), DOC_EXAMPLE).unwrap();
    assert!(
        suite::load(&addon)
            .unwrap_err()
            .0
            .contains("each suite id belongs to one file")
    );
    std::fs::remove_dir_all(&root).ok();
}

fn suite_with(tests: &[&str]) -> suite::Suite {
    parse(&format!(
        r#"{{"schema":1,"suite":"s","tests":[{}]}}"#,
        tests.join(",")
    ))
    .unwrap()
}

#[test]
fn tests_with_the_same_environment_share_a_game_start() {
    let s = suite_with(&[
        r#"{"id":"b1","env":{"kind":"benchmark","track":"bahrain","laps":3},"assert":[{"no_crash":true}]}"#,
        r#"{"id":"m1","tags":["smoke"],"env":{"kind":"boot"},"assert":[{"no_crash":true}]}"#,
        r#"{"id":"b2","env":{"laps":3,"track":"bahrain","kind":"benchmark"},"assert":[{"log":"x"}]}"#,
        r#"{"id":"m2","env":{"kind":"boot"},"assert":[{"log":"y"}]}"#,
        r#"{"id":"m3","env":{"kind":"boot"},"fresh":true,"assert":[{"log":"y"}]}"#,
        r#"{"id":"m4","env":{"kind":"boot"},"addons":["other"],"assert":[{"log":"y"}]}"#,
        r#"{"id":"r1","env":{"kind":"route","routes":["career"]},"assert":[{"no_crash":true}]}"#,
        r#"{"id":"c1","env":{"kind":"career","fixture":"f"},"assert":[{"no_crash":true}]}"#,
        r#"{"id":"a1","env":{"kind":"boot"},"steps":[{"route":"contracts"}],"assert":[{"log":"y"}]}"#,
    ]);
    let p = plan::plan(std::slice::from_ref(&s), &Selector::default());
    let shape: Vec<(String, Vec<&str>)> = p
        .batches
        .iter()
        .map(|b| (b.label(), b.tests.iter().map(|t| t.id.as_str()).collect()))
        .collect();
    assert_eq!(
        shape,
        [
            ("boot".to_owned(), vec!["m1", "m2", "a1"]),
            ("boot".to_owned(), vec!["m3"]),
            ("boot".to_owned(), vec!["m4"]),
            ("benchmark bahrain 3 laps".to_owned(), vec!["b1", "b2"]),
            ("route career".to_owned(), vec!["r1"]),
            ("career f".to_owned(), vec!["c1"]),
        ]
    );
    assert_eq!(p.tests(), 9);
    assert_eq!(p.game_starts(), 6, "the career batch runs through the agent");
    assert_eq!(p.headline(), "9 tests in 6 game starts");
    let text = p.text();
    assert!(
        text.starts_with("swofty test plan: 9 tests in 6 game starts\n"),
        "{text}"
    );
    assert!(
        text.contains("  boot ............................. m1\n"),
        "{text}"
    );
    assert!(!text.contains("a1                           skipped"), "{text}");
    assert!(
        p.batches[0].tests[2].needs_agent(),
        "a route step talks to the agent"
    );
    assert!(!p.batches[0].tests[0].needs_agent());
    assert!(p.batches[5].tests[0].needs_agent(), "a career talks to the agent");
    assert!(!text.contains("c1                           skipped"), "{text}");

    let smoke = Selector {
        tags: vec!["smoke".into()],
        ids: vec![],
    };
    let p = plan::plan(std::slice::from_ref(&s), &smoke);
    assert_eq!(p.tests(), 1);
    let by_id = Selector {
        tags: vec![],
        ids: vec!["s/b2".into(), "m3".into()],
    };
    let p = plan::plan(std::slice::from_ref(&s), &by_id);
    assert_eq!(p.tests(), 2);
    assert_eq!(p.game_starts(), 2);
    let json = serde_json::to_value(p.json()).unwrap();
    assert_eq!(json["game_starts"], 2);
}

#[test]
fn canonical_keys_ignore_key_order() {
    let a: serde_json::Value = serde_json::from_str(r#"{"b":1,"a":{"y":2,"x":[1,{"q":1,"p":2}]}}"#).unwrap();
    let b: serde_json::Value = serde_json::from_str(r#"{"a":{"x":[1,{"p":2,"q":1}],"y":2},"b":1}"#).unwrap();
    assert_eq!(plan::canonical(&a), plan::canonical(&b));
    assert_eq!(
        plan::canonical(&a),
        r#"{"a":{"x":[1,{"p":2,"q":1}],"y":2},"b":1}"#
    );
}

#[test]
fn the_benchmark_xml_has_the_race() {
    let t = one_test(
        r#"{"id":"b","env":{"kind":"benchmark","track":"monaco","laps":2,"cars":12,"weather":"rain","box":"a&b/c","grid_pos":4},"assert":[{"no_crash":true}]}"#,
    )
    .unwrap();
    let xml = super::bench::xml(&t.env);
    assert_eq!(
        xml,
        "<?xml version=\"1.0\" standalone=\"yes\" ?>\n<config infinite_loop=\"false\" show_fps=\"false\" >\n  \
         <track name=\"monaco\" laps=\"2\" weather=\"wet\" num_cars=\"12\" camera_mode=\"cycle\" grid_pos=\"4\" \
         setup_mode=\"a&amp;b/c\" />\n</config>\n"
    );
    let plain =
        one_test(r#"{"id":"b","env":{"kind":"benchmark","track":"bahrain"},"assert":[{"no_crash":true}]}"#)
            .unwrap();
    assert!(super::bench::xml(&plain.env).contains("laps=\"3\" weather=\"clear\" num_cars=\"20\""));
    assert_eq!(super::bench::args(), ["-benchmark", "swofty_test.xml"]);
}

const LOG: &str = "\
[14:13:47.100] swofty loader (version.dll proxy) attached, pid 25512
[14:13:50.000] [events] subscribe race.start by my.addon -> ok
[14:13:51.000] [rnd-tyres] thermal model on
[14:14:10.000] [career] flow \"#957880c6\" Entering -> Active
[14:14:12.000] [events] race.start (bahrain, 3 laps)
[14:14:13.000] [events] race.started_late
[14:14:14.000] [events] career.loaded has no replace phase (notify event); handler from x not attached
[14:15:00.000] [events] race.start
[14:15:01.000] [events] lap: 2
[14:15:02.000] [events] race.start hooked at base+0x7a7880
[14:15:02.000] [events] race.start signature matched 0 time(s); hook not installed
";

#[test]
fn log_and_event_checks_count_lines() {
    assert_eq!(checks::count_log(LOG, r"\[rnd-tyres\] thermal model on"), 1);
    assert_eq!(checks::count_log(LOG, r"\[events\]"), 8);
    assert_eq!(
        checks::count_event(LOG, "race.start"),
        2,
        "not the subscribe line or race.started_late"
    );
    assert_eq!(
        checks::count_event(LOG, "career.loaded"),
        0,
        "a refusal is not the event"
    );
    assert_eq!(checks::count_event(LOG, "lap"), 1);
    assert!(LOG.contains(checks::ready_line(Kind::Boot)));
    assert!(LOG.contains(checks::ready_line(Kind::None)));
    assert!(checks::waited(LOG, &Wait::Event("race.start".into())));
    assert!(!checks::waited(LOG, &Wait::Log("never".into())));

    let ev = Evidence {
        log: LOG.into(),
        log_path: Some("batches/1/loader.log".into()),
        ..Evidence::default()
    };
    let log = |pattern: &str, min, max| Check::Log {
        pattern: pattern.into(),
        count: Count { min, max },
    };
    assert!(checks::evaluate(&log(r"thermal model on", Some(1), None), &ev).is_ok());
    let f = checks::evaluate(&log(r"thermal model off", Some(1), None), &ev).unwrap_err();
    assert_eq!(
        f.text(),
        "expected log /thermal model off/ at least 1 time(s), got 0 times"
    );
    assert_eq!(f.evidence.as_deref(), Some("batches/1/loader.log"));
    let f = checks::evaluate(&log(r"\[events\]", None, Some(3)), &ev).unwrap_err();
    assert_eq!(f.actual, "8 times");
    let event = Check::Event {
        id: "race.start".into(),
        count: Count {
            min: Some(2),
            max: Some(2),
        },
    };
    assert!(checks::evaluate(&event, &ev).is_ok());
    assert!(checks::evaluate(&Check::NoCrash, &ev).is_ok());
    let crashed = Evidence {
        crash: Some("crash: exception 0xc0000005".into()),
        ..ev.clone()
    };
    let f = checks::evaluate(&Check::NoCrash, &crashed).unwrap_err();
    assert_eq!(f.text(), "expected no crash, got crash: exception 0xc0000005");
}

#[test]
fn perf_reads_the_p99_of_the_last_report() {
    let perf = "\
Swofty Launcher performance report (format 1)
== Summary
frame times: p50 8.10 ms, p95 12.00 ms, p99 31.50 ms, max 210 ms
== Summary
frame times: p50 8.10 ms, p95 12.00 ms, p99 22.25 ms, max 90 ms
";
    assert_eq!(checks::p99(perf), Some(22.25));
    assert_eq!(checks::p99("nothing"), None);
    let check = |max| Check::Perf {
        p99_max: Some(max),
        p99_min: None,
    };
    let ev = Evidence {
        perf: Some(perf.into()),
        ..Evidence::default()
    };
    assert!(checks::evaluate(&check(40.0), &ev).is_ok());
    let f = checks::evaluate(&check(20.0), &ev).unwrap_err();
    assert_eq!(f.text(), "expected p99 frame time at most 20 ms, got 22.25 ms");
    let f = checks::evaluate(&check(20.0), &Evidence::default()).unwrap_err();
    assert!(f.actual.contains("no perf.log report"), "{}", f.actual);
}

#[test]
fn the_lock_is_a_folder_with_an_owner_line() {
    let root = temp("lock");
    let dir = root.join("game.lock.d");
    let owner = lock::owner_line(
        "20261028-140200",
        &["swofty.a".into(), "swofty.b".into()],
        "14:02",
    );
    assert_eq!(owner, "swofty-test 20261028-140200 swofty.a,swofty.b 14:02");
    let held = GameLock::try_acquire(&dir, &owner).unwrap().unwrap();
    assert_eq!(
        std::fs::read_to_string(dir.join("owner")).unwrap(),
        format!("{owner}\n")
    );
    let busy = GameLock::try_acquire(&dir, "someone else").unwrap().unwrap_err();
    assert_eq!(busy.owner.as_deref(), Some(owner.as_str()));
    assert!(busy.text(false).contains(&format!("is held by \"{owner}\"")));
    let mut heard = 0;
    let waited = GameLock::acquire(
        &dir,
        "x",
        Duration::from_millis(60),
        Duration::from_millis(10),
        |_| heard += 1,
    )
    .unwrap();
    assert!(waited.is_err(), "still held after the wait");
    assert_eq!(heard, 1, "told once while waiting");
    assert!(dir.is_dir(), "a busy lock is never cleared");
    held.release().unwrap();
    assert!(!dir.exists());

    let first = GameLock::try_acquire(&dir, "first").unwrap().unwrap();
    let releaser = std::thread::spawn(move || {
        std::thread::sleep(Duration::from_millis(50));
        drop(first);
    });
    let second = GameLock::acquire(
        &dir,
        "second",
        Duration::from_secs(10),
        Duration::from_millis(10),
        |_| {},
    )
    .unwrap()
    .unwrap();
    releaser.join().unwrap();
    assert_eq!(std::fs::read_to_string(dir.join("owner")).unwrap(), "second\n");
    drop(second);
    assert!(!dir.exists(), "dropping the lock releases it");

    std::fs::create_dir(&dir).unwrap();
    let anon = GameLock::try_acquire(&dir, "x").unwrap().unwrap_err();
    assert_eq!(anon.owner, None);
    let stale = lock::Busy {
        age: Some(lock::STALE_AFTER + Duration::from_secs(60)),
        ..anon.clone()
    };
    assert!(stale.text(false).contains("only its holder clears it"));
    assert!(!stale.text(true).contains("left over"));
    assert!(dir.is_dir());
    let missing_parent = root.join("no/such/dir.lock.d");
    assert!(GameLock::try_acquire(&missing_parent, "x").is_err());
    std::fs::remove_dir_all(&root).ok();
}

#[test]
fn the_default_lock_follows_the_environment() {
    let data = Path::new("/home/me/.local/share/swofty");
    let path = lock::default_path(data);
    match std::env::var_os(lock::ENV).filter(|v| !v.is_empty()) {
        Some(v) => assert_eq!(path, PathBuf::from(v)),
        None if cfg!(target_os = "linux") && Path::new("/mnt/work").is_dir() => {
            assert_eq!(path, Path::new("/mnt/work/.f1-game.lock.d"))
        }
        None => assert_eq!(path, Path::new("/home/me/.local/share/swofty-game.lock.d")),
    }
}

fn report() -> Report {
    let mut r = Report::new("20261028-1402", "swofty-test/20261028-1402");
    r.game_starts = 2;
    r.ms = 125_000;
    let batch = |index: usize, env: &str| BatchReport {
        index,
        env: env.into(),
        key: env.into(),
        addons: vec![],
        started_game: true,
        ready_ms: None,
        ms: None,
        session: None,
        end: None,
        error: None,
        clean: Some(true),
        notes: vec![],
    };
    r.batches = vec![
        batch(1, "benchmark bahrain 3 laps"),
        batch(2, "career fixtures/my-team-r3"),
    ];
    let test = |id: &str, batch: usize, status: Status| TestReport {
        suite: "swofty.rnd-tyres-thermal".into(),
        id: id.into(),
        env: String::new(),
        batch,
        status,
        ms: None,
        reason: None,
        failures: vec![],
        steps: vec![],
    };
    let mut pass = test("tyre_temps_rise_on_track", 1, Status::Pass);
    pass.ms = Some(112_000);
    let mut fail = test("rnd_upgrade_applies", 1, Status::Fail);
    fail.ms = Some(3_000);
    fail.failures.push(checks::Failure {
        check: "log /x/".into(),
        expected: "log /\\[x\\] <on>/ at least 1 time(s)".into(),
        actual: "0 times".into(),
        evidence: Some("batches/1/loader.log".into()),
    });
    let mut skipped = test("rnd_tab_hidden_in_team_hq", 2, Status::Skipped);
    skipped.reason = Some(suite::NEEDS_SESSION_SETUP.into());
    r.tests = vec![pass, fail, skipped];
    r
}

#[test]
fn the_summary_reads_like_the_docs() {
    let r = report();
    assert_eq!(
        r.summary(),
        "swofty test: 3 tests in 2 game starts\n\
         \x20 benchmark bahrain 3 laps ......... tyre_temps_rise_on_track     pass   (1 m 52 s)\n\
         \x20                                    rnd_upgrade_applies          FAIL   expected log /\\[x\\] <on>/ at least 1 time(s), got 0 times\n\
         \x20 career fixtures/my-team-r3 ....... rnd_tab_hidden_in_team_hq    skipped: needs session setup inside the game's screens\n\
         1 passed, 1 failed, 1 skipped  ·  report: swofty-test/20261028-1402/report.json, junit.xml\n"
    );
}

#[test]
fn exit_codes_rank_infra_over_failures() {
    let mut r = report();
    assert_eq!(r.compute_exit(), exit::FAIL);
    r.tests.retain(|t| t.status != Status::Fail);
    assert_eq!(r.compute_exit(), exit::PASS, "skipped tests don't fail a run");
    r.batches[0].clean = Some(false);
    assert_eq!(r.compute_exit(), exit::INFRA, "a game folder left unclean");
    r.batches[0].clean = Some(true);
    r.tests[0].status = Status::Error;
    assert_eq!(r.compute_exit(), exit::INFRA);
    r.tests[0].status = Status::NotRun;
    assert_eq!(r.compute_exit(), exit::INFRA);
    r.tests[0].status = Status::Pass;
    r.errors.push("Steam is not running".into());
    assert_eq!(r.compute_exit(), exit::INFRA);
    assert_eq!((exit::PASS, exit::FAIL, exit::INFRA, exit::BUSY), (0, 1, 2, 3));
}

#[test]
fn junit_escapes_and_counts() {
    let r = report();
    let x = r.junit();
    assert!(x.starts_with("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"));
    assert!(x.contains(
        "<testsuites name=\"swofty test\" tests=\"3\" failures=\"1\" errors=\"0\" skipped=\"1\" time=\"125.000\">"
    ));
    assert!(x.contains(
        "<testsuite name=\"swofty.rnd-tyres-thermal\" tests=\"3\" failures=\"1\" errors=\"0\" skipped=\"1\" time=\"115.000\">"
    ));
    assert!(x.contains(
        "<testcase classname=\"swofty.rnd-tyres-thermal\" name=\"tyre_temps_rise_on_track\" time=\"112.000\"/>"
    ));
    assert!(
        x.contains("<failure message=\"expected log /\\[x\\] &lt;on&gt;/ at least 1 time(s), got 0 times\">")
    );
    assert!(x.contains("(batches/1/loader.log)</failure>"));
    assert!(x.contains("<skipped message=\"needs session setup inside the game&apos;s screens\"/>"));
    assert_eq!(x.matches("<testcase ").count(), 3);
    assert_eq!(x.matches("</testcase>").count(), 2);
}

#[test]
fn the_json_report_round_trips() {
    let r = report();
    let back: Report = serde_json::from_str(&r.json()).unwrap();
    assert_eq!(back, r);
    let v: serde_json::Value = serde_json::from_str(&r.json()).unwrap();
    assert_eq!(v["tests"][1]["status"], "fail");
    assert_eq!(v["tests"][2]["status"], "skipped");
    assert_eq!(v["schema"], 1);
}

#[test]
fn durations_read_as_minutes_and_seconds() {
    use super::report::duration;
    assert_eq!(duration(112_000), "1 m 52 s");
    assert_eq!(duration(40_400), "40 s");
    assert_eq!(duration(59_600), "1 m 0 s");
}

mod mailbox {
    use std::io::Write;
    use std::path::Path;
    use std::time::Duration;

    use serde_json::{Value, json};

    use super::super::agent::{self, Client, Line, Outcome, WaitError};
    use super::super::sandbox::{self, RunDir, RunOptions};
    use super::{one_test, temp};

    fn append(path: &Path, line: &str) {
        let mut f = std::fs::OpenOptions::new()
            .create(true)
            .append(true)
            .open(path)
            .unwrap();
        f.write_all(line.as_bytes()).unwrap();
    }

    #[test]
    fn the_run_folder_carries_the_token_the_agent_and_the_dlls() {
        let data = temp("mailbox-run");
        let dll = data.join("tests.dll");
        std::fs::write(&dll, b"MZ").unwrap();
        let run = RunDir::create(
            &data,
            "20261028-1402-1",
            RunOptions {
                sandbox: false,
                exe_sha256: None,
                dlls: vec![(dll.clone(), "my.addon.dll".into())],
            },
        )
        .unwrap();
        let dir = data.join("test-runs").join("20261028-1402-1");
        assert_eq!(run.root, dir);
        assert_eq!(run.agent(), dir.join("agent"));
        assert!(!run.sandboxed());
        let doc: Value =
            serde_json::from_str(&std::fs::read_to_string(dir.join(sandbox::RUN_FILE)).unwrap()).unwrap();
        assert_eq!(doc["schema"], 1);
        assert_eq!(doc["run"], "20261028-1402-1");
        assert_eq!(doc["token"].as_str().unwrap().len(), 32);
        assert_eq!(doc["agent"], json!({"v": 1, "dlls": ["dll/my.addon.dll"]}));
        assert!(doc.get("saves").is_none(), "no saves: the agent's run only");
        assert!(!dir.join("saves").exists());
        assert_eq!(std::fs::read(dir.join("agent/dll/my.addon.dll")).unwrap(), b"MZ");
        assert_eq!(
            std::fs::read_to_string(dir.join("agent").join(agent::COMMANDS)).unwrap(),
            ""
        );
        let env = run.env();
        assert_eq!(env.len(), 2, "no early stage without the sandbox: {env:?}");
        assert_eq!(env[0], (sandbox::RUN_ENV.to_owned(), dir.display().to_string()));
        assert_eq!(env[1].0, sandbox::TOKEN_ENV);
        assert_eq!(env[1].1, doc["token"].as_str().unwrap());
        run.remove().unwrap();
        assert!(!dir.exists());
        run.remove().unwrap();
        let bad = RunDir::create(
            &data,
            "r2",
            RunOptions {
                dlls: vec![(dll, "../x.dll".into())],
                ..RunOptions::default()
            },
        );
        assert!(bad.is_err(), "a DLL name with a path is refused");
        std::fs::remove_dir_all(&data).ok();
    }

    #[test]
    fn run_names_are_folder_names() {
        assert!(agent::valid_run("20261028-1402-b1"));
        for bad in ["", "..", "a/b", "a\\b", ".hidden", "a b"] {
            assert!(!agent::valid_run(bad), "{bad}");
        }
    }

    #[test]
    fn result_lines_parse_into_replies_and_events() {
        let r = agent::parse_line(r#"{"seq":3,"status":"unavailable","error":"no home view model","ms":0}"#)
            .unwrap();
        let Line::Reply(r) = r else { panic!() };
        assert_eq!((r.seq, r.outcome), (3, Outcome::Unavailable));
        assert_eq!(r.text(), "unavailable: no home view model");
        let r = agent::parse_line(r#"{"seq":4,"status":"ok","value":{"value":798.5,"type":"f32"},"ms":1}"#)
            .unwrap();
        let Line::Reply(r) = r else { panic!() };
        assert_eq!(agent::number(&r.value), Some(798.5));
        assert!(matches!(
            agent::parse_line(r#"{"event":"hello","v":1,"pid":7}"#),
            Some(Line::Hello(_))
        ));
        assert_eq!(
            agent::parse_line(r#"{"event":"refused","why":"the token does not match"}"#),
            Some(Line::Refused("the token does not match".into()))
        );
        assert!(
            matches!(agent::parse_line(r#"{"event":"fired","id":"race.start"}"#), Some(Line::Event(n, _)) if n == "fired")
        );
        for bad in [
            "",
            "not json",
            "[]",
            r#"{"seq":1}"#,
            r#"{"seq":1,"status":"maybe"}"#,
            r#"{"x":1}"#,
        ] {
            assert_eq!(agent::parse_line(bad), None, "{bad}");
        }
    }

    #[test]
    fn steps_become_agent_commands() {
        let t = one_test(
            r#"{"id":"a","env":{"kind":"boot"},"steps":[
                {"route":"contracts"},{"skip":"session"},{"advance":{"days":7}},{"ai_drives_player":true},
                {"pad":{"press":"a","then":"right"}},{"set":{"car[player].f1_aero.lift_scale":1.05,"career.money":5}},
                {"read":"career.money","as":"m"},{"capture":"frame","name":"hq"},{"save":"now"},
                {"wait":{"event":"race.start"}},{"wait":{"log":"x"}}]}"#,
        )
        .unwrap();
        let cmds: Vec<Vec<(String, Value)>> =
            t.steps.iter().map(|s| agent::step_commands(s).unwrap()).collect();
        let c = |cmd: &str, v: Value| vec![(cmd.to_owned(), v)];
        assert_eq!(cmds[0], c("route", json!({"route":"contracts"})));
        assert_eq!(cmds[1], c("skip", json!({"to":"session"})));
        assert_eq!(cmds[2], c("advance", json!({"days":7})));
        assert_eq!(cmds[3], c("ai_drives_player", json!({"on":true})));
        assert_eq!(cmds[4], c("pad", json!({"press":"a","then":"right"})));
        assert_eq!(cmds[5].len(), 2, "one set per path");
        assert_eq!(
            cmds[5][0],
            (
                "set".to_owned(),
                json!({"path":"car[player].f1_aero.lift_scale","value":1.05})
            )
        );
        assert_eq!(cmds[6], c("read", json!({"path":"career.money"})));
        assert_eq!(cmds[7], c("capture", json!({"name":"hq"})));
        assert_eq!(cmds[8], c("save", json!({"what":"now"})));
        assert_eq!(cmds[9], c("wait", json!({"event":"race.start"})));
        assert!(cmds[10].is_empty(), "the CLI waits for log lines itself");
    }

    #[test]
    fn shell_lines_become_commands() {
        let p = agent::parse_shell;
        assert_eq!(
            p("route career").unwrap(),
            ("route".into(), json!({"route":"career"}), None)
        );
        assert_eq!(
            p("wait race.start 90").unwrap(),
            ("wait".into(), json!({"event":"race.start"}), Some(90_000))
        );
        assert_eq!(
            p("skip session 15").unwrap(),
            ("skip".into(), json!({"to":"session","session":15}), None)
        );
        assert_eq!(
            p("ai off").unwrap(),
            ("ai_drives_player".into(), json!({"on":false}), None)
        );
        assert_eq!(
            p("set career.money 5000").unwrap().1,
            json!({"path":"career.money","value":5000})
        );
        assert_eq!(
            p("pad a then right 200").unwrap().1,
            json!({"press":"a","then":"right","ms":200})
        );
        assert_eq!(p("save").unwrap().1, json!({"what":"now"}));
        assert_eq!(
            p(r#"{"cmd":"read","args":{"path":"x"},"timeout_ms":5}"#).unwrap(),
            ("read".into(), json!({"path":"x"}), Some(5))
        );
        assert_eq!(p("title").unwrap(), ("title".into(), Value::Null, None));
        assert_eq!(
            p("career load 3 at weekend_hub mode my_team_career").unwrap(),
            (
                "career".into(),
                json!({"op":"load","slot":3,"at":"weekend_hub","mode":"my_team_career"}),
                None
            )
        );
        assert_eq!(
            p("career load newest").unwrap(),
            ("career".into(), json!({"op":"load","slot":"newest"}), None)
        );
        assert!(
            p("career load soon")
                .unwrap_err()
                .contains("a slot is a number or newest")
        );
        assert!(p("career save 3").unwrap_err().starts_with("usage: career load"));
        assert!(
            p("career load 3 at")
                .unwrap_err()
                .starts_with("usage: career load")
        );
        assert!(p("bogus").unwrap_err().starts_with("unknown command bogus"));
        assert_eq!(p("advance soon").unwrap_err(), "soon: expected days");
        assert_eq!(p("route").unwrap_err(), "usage: route <name>");
    }

    #[test]
    fn the_client_sends_commands_and_reads_replies_as_they_arrive() {
        let dir = temp("mailbox-client");
        let mut c = Client::new(&dir);
        let seq = c.send("route", json!({"route":"career"}), Some(100)).unwrap();
        assert_eq!(seq, 1);
        assert_eq!(c.send("hello", Value::Null, None).unwrap(), 2);
        let sent = std::fs::read_to_string(dir.join(agent::COMMANDS)).unwrap();
        let lines: Vec<Value> = sent.lines().map(|l| serde_json::from_str(l).unwrap()).collect();
        assert_eq!(
            lines,
            vec![
                json!({"args":{"route":"career"},"cmd":"route","seq":1,"timeout_ms":100}),
                json!({"cmd":"hello","seq":2}),
            ]
        );
        assert!(sent.ends_with('\n'));
        let results = dir.join(agent::RESULTS);
        assert!(c.poll().is_empty(), "no results file yet");
        append(
            &results,
            "{\"event\":\"hello\",\"v\":1}\n{\"seq\":2,\"status\":\"ok\",\"val",
        );
        assert_eq!(c.poll().len(), 1, "a half-written line waits for its end");
        assert!(c.hello.is_some());
        append(&results, "ue\":{\"pid\":1}}\n");
        let r = c
            .wait_reply(2, Duration::from_millis(10), Duration::from_millis(1), || true)
            .unwrap();
        assert_eq!(r.value, json!({"pid":1}));

        let writer = {
            let results = results.clone();
            std::thread::spawn(move || {
                std::thread::sleep(Duration::from_millis(50));
                append(&results, "{\"seq\":1,\"status\":\"ok\",\"ms\":3}\n");
            })
        };
        let r = c
            .wait_reply(1, Duration::from_secs(5), Duration::from_millis(5), || true)
            .unwrap();
        writer.join().unwrap();
        assert_eq!(r.outcome, Outcome::Ok);

        let seq = c.send("wait", json!({"event":"x"}), None).unwrap();
        assert_eq!(
            c.wait_reply(seq, Duration::from_millis(30), Duration::from_millis(5), || true),
            Err(WaitError::Timeout(Duration::from_millis(30)))
        );
        assert_eq!(
            c.wait_reply(seq, Duration::from_secs(5), Duration::from_millis(5), || false),
            Err(WaitError::Gone)
        );
    }

    #[test]
    fn hello_waits_for_the_agent_or_its_refusal() {
        let dir = temp("mailbox-hello");
        let mut c = Client::new(&dir);
        let mut polls = 0;
        let e = c
            .wait_hello(
                Duration::from_millis(20),
                Duration::from_millis(2),
                || true,
                || polls += 1,
            )
            .unwrap_err();
        assert!(
            e.starts_with("the test agent did not say hello within 0 s"),
            "{e}"
        );
        assert!(polls > 0);
        append(
            &dir.join(agent::RESULTS),
            "{\"event\":\"refused\",\"why\":\"the token does not match\"}\n",
        );
        assert_eq!(
            c.wait_hello(Duration::from_secs(1), Duration::from_millis(2), || true, || {}),
            Err("the test agent refused the run: the token does not match".into())
        );
        let dir = temp("mailbox-hello-2");
        let mut c = Client::new(&dir);
        assert_eq!(
            c.wait_hello(Duration::from_secs(1), Duration::from_millis(2), || false, || {}),
            Err("the game exited before the test agent said hello".into())
        );
        append(
            &dir.join(agent::RESULTS),
            "{\"event\":\"hello\",\"v\":1,\"commands\":[\"route\"]}\n",
        );
        assert_eq!(
            c.wait_hello(Duration::from_secs(1), Duration::from_millis(2), || true, || {})
                .unwrap()["commands"],
            json!(["route"])
        );
    }
}
fn sandbox_temp(name: &str) -> PathBuf {
    let dir = std::env::temp_dir().join(format!("swofty-ts3-{name}-{}", std::process::id()));
    std::fs::remove_dir_all(&dir).ok();
    std::fs::create_dir_all(&dir).unwrap();
    dir
}

fn noise(n: usize, seed: u32) -> Vec<u8> {
    let mut x = seed.wrapping_mul(2_654_435_761).max(1);
    (0..n)
        .map(|_| {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            x as u8
        })
        .collect()
}

#[test]
fn a_run_folder_carries_a_fresh_token_until_it_is_retired() {
    use super::sandbox::{self, RunDir, RunOptions};
    let data = sandbox_temp("run");
    let sandboxed = |sha: Option<&str>| RunOptions {
        sandbox: true,
        exe_sha256: sha.map(str::to_owned),
        dlls: Vec::new(),
    };
    let run = RunDir::create(&data, "20261028-1702-1", sandboxed(Some("d873"))).unwrap();
    assert!(run.remote().is_dir());
    assert!(run.sandboxed());
    let doc: serde_json::Value =
        serde_json::from_slice(&std::fs::read(run.root.join(sandbox::RUN_FILE)).unwrap()).unwrap();
    let env = run.env();
    let token = &env.iter().find(|(k, _)| k == sandbox::TOKEN_ENV).unwrap().1;
    assert_eq!(token.len(), 32);
    assert_eq!(doc["token"].as_str(), Some(token.as_str()));
    assert_eq!(
        (
            doc["schema"].as_u64(),
            doc["saves"].as_str(),
            doc["exe_sha256"].as_str()
        ),
        (Some(1), Some("saves"), Some("d873"))
    );
    assert_eq!(
        env.iter()
            .find(|(k, _)| k == sandbox::RUN_ENV)
            .map(|(_, v)| PathBuf::from(v)),
        Some(run.root.clone())
    );
    assert!(env.contains(&(sandbox::EARLY_ENV.to_owned(), "1".to_owned())));
    assert_eq!(
        doc["agent"]["v"], 1,
        "every run folder speaks the agent's protocol"
    );
    let again = RunDir::create(&data, "20261028-1702-2", sandboxed(None)).unwrap();
    assert_ne!(again.env()[1].1, *token, "every run has its own token");
    assert_eq!(run.status(), None);
    std::fs::write(
        run.root.join(sandbox::STATUS_FILE),
        r#"{"schema":1,"run":"20261028-1702-1","state":"armed","pid":42,"remote":"Z:\\x\\saves\\remote","why":""}"#,
    )
    .unwrap();
    assert!(run.status().unwrap().armed());
    run.retire().unwrap();
    let doc: serde_json::Value =
        serde_json::from_slice(&std::fs::read(run.root.join(sandbox::RUN_FILE)).unwrap()).unwrap();
    assert!(
        doc.get("token").is_none(),
        "a retired run folder can't switch test mode on: {doc}"
    );
    assert_eq!(
        (doc["saves"].as_str(), doc["exe_sha256"].as_str()),
        (Some("saves"), Some("d873")),
        "retiring keeps what a capture reads"
    );
    assert!(run.remote().is_dir(), "and the saves");
    assert!(RunDir::create(&data, "../x", sandboxed(None)).is_err());
    assert_eq!(RunDir::latest(&data).map(|r| r.root.exists()), Some(true));
    std::fs::remove_dir_all(&data).ok();
}

fn loader_with(dir: &Path, sets: &[(&str, &str, &[&str])]) {
    let core = dir.join("swofty/addons/core");
    std::fs::create_dir_all(&core).unwrap();
    for (file, sha, ids) in sets {
        let bindings: Vec<serde_json::Value> = ids
            .iter()
            .map(|id| serde_json::json!({"id": id, "kind": "func", "status": "experimental"}))
            .collect();
        let doc = serde_json::json!({"schema": 1, "build": {"sha256": sha}, "bindings": bindings});
        std::fs::write(core.join(file), serde_json::to_vec(&doc).unwrap()).unwrap();
    }
}

#[test]
fn the_sandbox_is_verified_only_for_an_exe_whose_bindings_carry_its_install_point() {
    use super::sandbox;
    let dir = sandbox_temp("verify");
    let none = sandbox::verified(&dir, "aa").unwrap_err();
    assert!(none.contains("no binding sets"), "{none}");
    loader_with(
        &dir,
        &[
            (
                "bindings.json",
                "aa",
                &[
                    "Ne_SaveSystem2Win_Start",
                    "Ne_SaveSystem2Win_Mount",
                    "Ne_SaveSystem2Win_WriteInfoXml",
                ],
            ),
            ("bindings.ea.json", "bb", &["Ne_SaveSystem2Win_Start", "Other"]),
        ],
    );
    let ok = sandbox::verified(&dir, "aa").unwrap();
    assert!(
        ok.contains("bindings.json") && ok.contains("Ne_SaveSystem2Win_Start experimental"),
        "{ok}"
    );
    let lacking = sandbox::verified(&dir, "bb").unwrap_err();
    assert!(
        lacking.contains("lack Ne_SaveSystem2Win_Mount, Ne_SaveSystem2Win_WriteInfoXml"),
        "{lacking}"
    );
    let other = sandbox::verified(&dir, "cc").unwrap_err();
    assert!(
        other.contains("no binding set") && other.contains("not verified for this build"),
        "{other}"
    );
    std::fs::remove_dir_all(&dir).ok();
}

#[test]
fn the_player_save_folder_listing_reports_every_change() {
    use super::sandbox;
    let dir = sandbox_temp("listing");
    std::fs::write(dir.join("savegame@profile#QKRHMYXE"), [1u8; 4]).unwrap();
    std::fs::write(dir.join("savegame@profile#NXDSMWW.BWW"), [2u8; 8]).unwrap();
    let before = sandbox::listing(&dir);
    assert_eq!(before.len(), 2);
    assert!(sandbox::changed(&before, &sandbox::listing(&dir)).is_empty());
    std::fs::write(dir.join("savegame@profile#QKRHMYXE"), [1u8; 5]).unwrap();
    std::fs::remove_file(dir.join("savegame@profile#NXDSMWW.BWW")).unwrap();
    std::fs::write(dir.join("savegame@swtest#QKRHMYXE"), [3u8; 1]).unwrap();
    let ch = sandbox::changed(&before, &sandbox::listing(&dir));
    assert_eq!(
        ch,
        [
            "savegame@profile#NXDSMWW.BWW was removed",
            "savegame@profile#QKRHMYXE changed",
            "savegame@swtest#QKRHMYXE appeared"
        ]
    );
    std::fs::remove_dir_all(&dir).ok();
}

fn fake_sandbox(remote: &Path) -> (Vec<u8>, Vec<u8>) {
    std::fs::create_dir_all(remote).unwrap();
    let (data, index) = (noise(894_408, 1), noise(416, 2));
    std::fs::write(remote.join(super::fixture::PROFILE_FILES[0]), &data).unwrap();
    std::fs::write(remote.join(super::fixture::PROFILE_FILES[1]), &index).unwrap();
    std::fs::write(
        remote.join("savegame@highlights000#FOVUPTYXSA000.PSX"),
        noise(64, 3),
    )
    .unwrap();
    std::fs::write(remote.join("corrupt_saves#76561198803768583.zip"), noise(64, 4)).unwrap();
    std::fs::write(remote.join("savegame@midsessions0#QKRHMYXE"), noise(32, 5)).unwrap();
    (data, index)
}

#[test]
fn a_capture_copies_the_saves_and_storage_and_installs_back_byte_for_byte() {
    use super::fixture::{self, Meta};
    let root = sandbox_temp("capture");
    let (remote, store) = (root.join("remote"), root.join("store"));
    let (data, index) = fake_sandbox(&remote);
    let storage = root.join("storage/swofty.progressive");
    std::fs::create_dir_all(storage.join("sub")).unwrap();
    std::fs::write(storage.join("careers.json"), br#"{"pg-1-bottas":{}}"#).unwrap();
    std::fs::write(storage.join("sub/x.bin"), [9u8; 3]).unwrap();
    std::fs::write(storage.join(".swofty-data-folder"), "").unwrap();
    let meta = Meta {
        description: "a Progressive 2018 career".into(),
        created: "20261028-1702".into(),
        exe: fixture::Exe {
            sha256: "d873".into(),
            version: Some("1.0.141.7865".into()),
        },
        addons: [("swofty.progressive".to_owned(), "1.6.0".to_owned())].into(),
        holds: fixture::Holds {
            careers: vec![fixture::Career {
                mode: "driver_career_progressive".into(),
                slot: Some(1),
                ..Default::default()
            }],
            notes: String::new(),
        },
        source: "test run r1-1".into(),
    };
    let f = fixture::capture(
        "progressive-2018-r1",
        &store,
        &remote,
        &[("swofty.progressive".into(), storage.clone())],
        meta.clone(),
    )
    .unwrap();
    let files: Vec<&str> = f.manifest.files.keys().map(String::as_str).collect();
    assert_eq!(
        files,
        [
            "save/savegame@midsessions0#QKRHMYXE",
            "save/savegame@profile#NXDSMWW.BWW",
            "save/savegame@profile#QKRHMYXE",
            "storage/swofty.progressive/careers.json",
            "storage/swofty.progressive/sub/x.bin",
        ],
        "highlights, corrupt-save bundles and the storage mark are left out"
    );
    assert_eq!(f.manifest.exe.sha256, "d873");
    assert_eq!(f.manifest.addons["swofty.progressive"], "1.6.0");
    assert_eq!(f.manifest.files["save/savegame@profile#QKRHMYXE"].size, 416);
    let again = fixture::capture("progressive-2018-r1", &store, &remote, &[], meta.clone()).unwrap_err();
    assert!(again.contains("never overwritten"), "{again}");
    let empty = root.join("empty");
    std::fs::create_dir_all(&empty).unwrap();
    let none = fixture::capture("x", &store, &empty, &[], meta).unwrap_err();
    assert!(none.contains("has not saved its profile"), "{none}");

    let found = fixture::resolve("progressive-2018-r1", None, &store).unwrap();
    assert_eq!(found, store.join("progressive-2018-r1"));
    assert_eq!(
        fixture::resolve("fixtures/progressive-2018-r1", Some(&root), &store).unwrap(),
        found
    );
    assert!(fixture::resolve("../progressive-2018-r1", None, &store).is_err());
    assert!(
        fixture::resolve("missing", None, &store)
            .unwrap_err()
            .contains("not found")
    );

    let f = fixture::load(&found).unwrap();
    assert_eq!(f.storage_ids(), ["swofty.progressive"]);
    let sandbox = root.join("sandbox/remote");
    std::fs::create_dir_all(&sandbox).unwrap();
    let kept = root.join("profile-copy");
    let copied = f.install(&sandbox, &|id| kept.join(id)).unwrap();
    assert_eq!(copied.len(), 5);
    assert_eq!(
        std::fs::read(sandbox.join(fixture::PROFILE_FILES[0])).unwrap(),
        data
    );
    assert_eq!(
        std::fs::read(sandbox.join(fixture::PROFILE_FILES[1])).unwrap(),
        index
    );
    assert_eq!(
        std::fs::read(kept.join("swofty.progressive/sub/x.bin")).unwrap(),
        [9u8; 3]
    );
    let busy = f.install(&sandbox, &|id| kept.join(id)).unwrap_err();
    assert!(busy.contains("is not empty"), "{busy}");

    std::fs::write(found.join("save").join(fixture::PROFILE_FILES[1]), noise(416, 99)).unwrap();
    let edited = fixture::load(&found).unwrap_err();
    assert!(edited.contains("does not match fixture.json"), "{edited}");
    std::fs::write(found.join("save/extra"), "x").unwrap();
    std::fs::write(found.join("save").join(fixture::PROFILE_FILES[1]), &index).unwrap();
    let extra = fixture::load(&found).unwrap_err();
    assert!(extra.contains("save/extra is not listed"), "{extra}");
    assert_eq!(fixture::list(&store).len(), 1);
    std::fs::remove_dir_all(&root).ok();
}

#[test]
fn a_career_environment_takes_a_known_place_and_asks_the_agent_for_it() {
    let parse = |env: &str| {
        let doc = format!(r#"{{"schema":1,"suite":"s","tests":[{{"id":"t","fn":"f","env":{env}}}]}}"#);
        suite::parse(&doc, Path::new("tests/x.test.json"), None)
    };
    let s =
        parse(r#"{"kind":"career","fixture":"fixtures/my-team-r3","at":"weekend_hub","slot":3}"#).unwrap();
    let env = &s.tests[0].env;
    assert_eq!(super::career::at(env), "weekend_hub");
    assert_eq!(
        super::career::agent_command(env, Some("my_team_career")),
        (
            "career".to_owned(),
            serde_json::json!({"op":"load","slot":3,"at":"weekend_hub","mode":"my_team_career"})
        )
    );
    assert_eq!(
        super::career::agent_command(env, Some("grand_prix"))
            .1
            .get("mode"),
        None,
        "a mode the agent does not know is left out"
    );
    assert_eq!(
        super::career::ready_line(env),
        "[test] career ready at weekend_hub"
    );
    let s = parse(r#"{"kind":"career","fixture":"f"}"#).unwrap();
    assert_eq!(
        super::career::agent_command(&s.tests[0].env, None),
        (
            "career".to_owned(),
            serde_json::json!({"op":"load","slot":"newest","at":"team_hq"})
        )
    );
    let bad = parse(r#"{"kind":"career","fixture":"f","at":"garage"}"#).unwrap_err();
    assert!(bad.0.contains("unknown place \"garage\""), "{bad}");
}
