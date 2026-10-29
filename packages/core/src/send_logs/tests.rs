use std::collections::BTreeSet;
use std::io::{Read, Write};
use std::net::TcpListener;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use super::upload::{SendError, Stage, multipart, payload};
use super::*;

fn tmp(name: &str) -> PathBuf {
    let d = std::env::temp_dir().join(format!("swofty-sendlogs-{name}-{}", std::process::id()));
    std::fs::remove_dir_all(&d).ok();
    std::fs::create_dir_all(&d).unwrap();
    d
}

fn put(path: &Path, data: impl AsRef<[u8]>) {
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    std::fs::write(path, data).unwrap();
}

fn noise(len: usize, seed: u64) -> Vec<u8> {
    let mut x = seed.wrapping_mul(6_364_136_223_846_793_005).wrapping_add(1);
    (0..len)
        .map(|_| {
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
            (x >> 24) as u8
        })
        .collect()
}

fn fake_data(root: &Path) -> (Store, PathBuf) {
    let data = root.join("data");
    put(&data.join("state/launcher-run.log"), "launcher line\n");
    put(&data.join("state/wrapper-run.log"), "wrapper line\n");
    put(&data.join("state/offline.log"), "offline line\n");
    put(&data.join("state/placed.json"), "{}");
    put(&data.join("state/journal/1.json"), "{}");
    for (i, s) in [
        "20261017-090000",
        "20261018-090000",
        "20261019-090000",
        "20261020-090000",
    ]
    .iter()
    .enumerate()
    {
        let dir = data.join("sessions").join(s);
        put(&dir.join("loader.log"), format!("loader {i}\n"));
        put(&dir.join("exit_code"), "0\n");
        put(&dir.join("steam_command.txt"), "steam -applaunch 3059520\n");
        put(
            &dir.join("env.txt"),
            "HOME=/home/player\nDISCORD_TOKEN=abc123\nOPENAI_API_KEY=sk-live\nPATH=/usr/bin\n",
        );
        put(&dir.join("game-logs/loader.prev.log"), "previous\n");
    }
    put(&data.join("sessions/orphaned-logs/loader.log"), "orphan\n");
    put(&data.join("sessions/latest.txt"), "x");
    put(&data.join("cache/addons/t.helper/helper.log"), "helper says hi\n");
    put(
        &data.join("cache/addons/t.quiet/other.txt"),
        "no helper log here\n",
    );
    put(
        &data.join("addons/t.helper/swofty.json"),
        r#"{"schema_version":1,"id":"t.helper","name":"Helper","version":"1.2.3","author":"t","compat":{"game":"f1_25"}}"#,
    );
    put(
        &data.join("profiles/default.json"),
        r#"{"schema_version":1,"name":"default","addons":[{"id":"t.helper","enabled":false}]}"#,
    );
    put(
        &data.join("config.json"),
        r#"{"active_profile":"default","launch":{"env":{"STEAM_API_KEY":"k-123","DXVK_HUD":"1"},"winedebug":"-all"},
            "updates":{"check":true,"github_token":"ghp_secret"},"password":"hunter2","discord_username":"swofty"}"#,
    );
    let game = root.join("game");
    put(&game.join(SNAPSHOT), r#"{"status":{"career":true}}"#);
    (Store::new(&data), game)
}

fn report(game: &Path) -> Report {
    Report {
        name: "swofty".into(),
        message: "It crashed on the grid".into(),
        game: GameInfo {
            dir: Some(game.to_path_buf()),
            build_id: Some(20_553_335),
            version: Some("1.17.0".into()),
        },
        room: None,
    }
}

fn names(zip: &[u8]) -> BTreeSet<String> {
    let mut z = zip::ZipArchive::new(std::io::Cursor::new(zip)).unwrap();
    (0..z.len())
        .map(|i| z.by_index(i).unwrap().name().unwrap().into_owned())
        .collect()
}

fn entry(zip: &[u8], name: &str) -> String {
    let mut z = zip::ZipArchive::new(std::io::Cursor::new(zip)).unwrap();
    let mut s = String::new();
    z.by_name(name).unwrap().read_to_string(&mut s).unwrap();
    s
}

#[test]
fn collects_the_logs_the_developers_need() {
    let root = tmp("collect");
    let (store, game) = fake_data(&root);
    let bundle = Bundle::collect_with(&store, &report(&game), Limits::default(), "20261020-101010");
    assert_eq!(bundle.file_name(), "swofty-logs-swofty-20261020-101010.zip");
    let zip = bundle.zip().unwrap();
    let mut want: BTreeSet<String> = [
        "summary.txt",
        "config.json",
        "addons.json",
        "addons/t.helper/helper.log",
        "game/career-editor/snapshot.json",
        "state/launcher-run.log",
        "state/wrapper-run.log",
        "state/offline.log",
    ]
    .map(String::from)
    .into();
    for s in ["20261018-090000", "20261019-090000", "20261020-090000"] {
        for f in [
            "env.txt",
            "exit_code",
            "game-logs/loader.prev.log",
            "loader.log",
            "steam_command.txt",
        ] {
            want.insert(format!("sessions/{s}/{f}"));
        }
    }
    assert_eq!(names(&zip), want);
    let addons: Value = serde_json::from_str(&entry(&zip, "addons.json")).unwrap();
    let helper = addons["installed"]
        .as_array()
        .unwrap()
        .iter()
        .find(|a| a["id"] == "t.helper")
        .unwrap();
    assert_eq!(helper["version"], "1.2.3");
    let default = addons["profiles"]
        .as_array()
        .unwrap()
        .iter()
        .find(|p| p["name"] == "default")
        .unwrap();
    assert_eq!(default["active"], true);
    assert!(
        default["addons"]
            .as_array()
            .unwrap()
            .contains(&json!({"id": "t.helper", "name": "Helper", "enabled": false}))
    );
    let summary = entry(&zip, "summary.txt");
    for want in [
        "swofty",
        "2026-10-20 10:10:10 UTC",
        "It crashed on the grid",
        "1.17.0 (Steam build 20553335)",
        "state/launcher-run.log",
    ] {
        assert!(summary.contains(want), "{want} missing from:\n{summary}");
    }
    assert!(
        bundle
            .listing()
            .iter()
            .any(|l| l.starts_with("sessions/20261020-090000/env.txt"))
    );
    std::fs::remove_dir_all(&root).ok();
}

#[test]
fn includes_the_db_editor_packages_the_active_profile_runs() {
    use crate::dbedit::package::Package;
    let root = tmp("dbpackages");
    let (store, game) = fake_data(&root);
    let on = Package::create(&store, "On").unwrap();
    let off = Package::create(&store, "Off").unwrap();
    store
        .update_profile("default", |p| {
            p.set_enabled(&on.id, true)?;
            p.set_enabled(&off.id, false)
        })
        .unwrap();
    let zip = Bundle::collect_with(&store, &report(&game), Limits::default(), "x")
        .zip()
        .unwrap();
    let got: Vec<String> = names(&zip)
        .into_iter()
        .filter(|n| n.starts_with("db-packages/"))
        .collect();
    assert_eq!(got, ["db-packages/dbedit.on.json"]);
    assert!(entry(&zip, "db-packages/dbedit.on.json").contains("swofty-db-package"));
    std::fs::remove_dir_all(&root).ok();
}

#[test]
fn redacts_secrets_from_config_and_environment() {
    let root = tmp("redact");
    let (store, game) = fake_data(&root);
    let bundle = Bundle::collect_with(&store, &report(&game), Limits::default(), "20261020-101010");
    let zip = bundle.zip().unwrap();
    let config = entry(&zip, "config.json");
    for secret in ["k-123", "ghp_secret", "hunter2"] {
        assert!(!config.contains(secret), "{secret} leaked:\n{config}");
    }
    let v: Value = serde_json::from_str(&config).unwrap();
    assert_eq!(v["launch"]["env"]["STEAM_API_KEY"], "[redacted]");
    assert_eq!(v["launch"]["env"]["DXVK_HUD"], "1");
    assert_eq!(v["updates"]["github_token"], "[redacted]");
    assert_eq!(v["updates"]["check"], true);
    assert_eq!(v["active_profile"], "default");
    let env = entry(&zip, "sessions/20261020-090000/env.txt");
    assert_eq!(
        env,
        "HOME=/home/player\nDISCORD_TOKEN=[redacted]\nOPENAI_API_KEY=[redacted]\nPATH=/usr/bin\n"
    );
    let notes: Vec<_> = bundle
        .entries
        .iter()
        .filter(|e| e.path == "config.json")
        .collect();
    assert_eq!(notes[0].note.as_deref(), Some("secrets redacted"));
    put(&store.config_path(), "{ not json");
    let bundle = Bundle::collect_with(&store, &report(&game), Limits::default(), "x");
    let config = String::from_utf8(
        bundle
            .entries
            .iter()
            .find(|e| e.path == "config.json")
            .unwrap()
            .data
            .clone(),
    )
    .unwrap();
    assert!(config.starts_with("config.json is not valid JSON"));
    assert!(!config.contains("not json"));
    std::fs::remove_dir_all(&root).ok();
}

#[test]
fn secret_names_and_webhook_values() {
    for n in [
        "token",
        "API_KEY",
        "github_token",
        "Password",
        "client_secret",
        "AUTH",
        "oauth_code",
        "SWOFTY_LOG_WEBHOOK",
    ] {
        assert!(secret_name(n), "{n}");
    }
    for n in [
        "HOME",
        "PATH",
        "author",
        "active_profile",
        "winedebug",
        "PWD",
        "DXVK_HUD",
    ] {
        assert!(!secret_name(n), "{n}");
    }
    assert_eq!(
        redact_env("X=https://discord.com/api/webhooks/1/abc\r\nY=1\n"),
        "X=[redacted]\r\nY=1\n"
    );
}

#[test]
fn caps_big_files_and_the_whole_bundle() {
    let root = tmp("caps");
    let (store, game) = fake_data(&root);
    let lines: String = (0..400)
        .map(|i| format!("line {i:04} of the launcher log\n"))
        .collect();
    put(&store.root.join("state/launcher-run.log"), &lines);
    let limits = Limits {
        file: 1024,
        total: 1 << 20,
    };
    let bundle = Bundle::collect_with(&store, &report(&game), limits, "x");
    let log = bundle
        .entries
        .iter()
        .find(|e| e.path == "state/launcher-run.log")
        .unwrap();
    let text = String::from_utf8(log.data.clone()).unwrap();
    assert!(text.starts_with("[swofty: kept the last 1.0 KB of "), "{text}");
    assert!(text.ends_with("line 0399 of the launcher log\n"));
    assert!(text.lines().nth(1).unwrap().starts_with("line "));
    assert!(log.data.len() <= 1024 + 64);
    assert!(log.note.as_deref().unwrap().starts_with("last 1.0 KB of"));

    let limits = Limits {
        file: 1024,
        total: 2048,
    };
    let bundle = Bundle::collect_with(&store, &report(&game), limits, "x");
    let used: usize = bundle.entries.iter().skip(1).map(|e| e.data.len()).sum();
    assert!(used <= 2048 + 64, "{used}");
    assert!(!bundle.skipped.is_empty());
    assert!(
        bundle
            .skipped
            .iter()
            .any(|s| s.contains("reached its 2.0 KB limit")),
        "{:?}",
        bundle.skipped
    );
    let summary = String::from_utf8(bundle.entries[0].data.clone()).unwrap();
    assert!(summary.contains("Left out"));
    std::fs::remove_dir_all(&root).ok();
}

fn noisy_bundle(sizes: &[usize]) -> Bundle {
    Bundle {
        stem: "swofty-logs-t-20261020-101010".into(),
        name: "t".into(),
        message: "help".into(),
        launcher: "1.0.12".into(),
        os: "Linux".into(),
        game: "1.17.0".into(),
        entries: sizes
            .iter()
            .enumerate()
            .map(|(i, &n)| Entry {
                path: format!("f{i}.bin"),
                data: noise(n, i as u64 + 1),
                note: None,
            })
            .collect(),
        skipped: Vec::new(),
    }
}

#[test]
fn splits_a_big_bundle_into_standalone_zips() {
    let bundle = noisy_bundle(&[600 << 10, 600 << 10, 600 << 10, 100 << 10]);
    let one = bundle.parts(8 << 20).unwrap();
    assert_eq!(one.len(), 1);
    assert_eq!(one[0].file_name, "swofty-logs-t-20261020-101010.zip");
    let parts = bundle.parts(1 << 20).unwrap();
    assert!(parts.len() >= 3, "{}", parts.len());
    let mut seen = BTreeSet::new();
    for (i, p) in parts.iter().enumerate() {
        assert!(p.data.len() <= 1 << 20, "part {} is {}", p.index, p.data.len());
        assert_eq!((p.index, p.total), (i + 1, parts.len()));
        assert_eq!(
            p.file_name,
            format!("swofty-logs-t-20261020-101010-part{}.zip", i + 1)
        );
        seen.extend(names(&p.data));
    }
    assert_eq!(seen, (0..4).map(|i| format!("f{i}.bin")).collect());
}

#[derive(Debug, Clone)]
struct Got {
    head: String,
    body: Vec<u8>,
}

impl Got {
    fn header(&self, name: &str) -> Option<String> {
        self.head.lines().find_map(|l| {
            let (k, v) = l.split_once(':')?;
            k.eq_ignore_ascii_case(name).then(|| v.trim().to_owned())
        })
    }

    fn parts(&self) -> Vec<(String, Vec<u8>)> {
        let ct = self.header("content-type").unwrap();
        let boundary = format!("--{}", ct.split_once("boundary=").unwrap().1);
        let mut out = Vec::new();
        let b = boundary.as_bytes();
        let mut starts: Vec<usize> = self
            .body
            .windows(b.len())
            .enumerate()
            .filter(|(_, w)| *w == b)
            .map(|(i, _)| i)
            .collect();
        starts.push(self.body.len());
        for pair in starts.windows(2) {
            let chunk = &self.body[pair[0] + b.len()..pair[1]];
            if chunk.starts_with(b"--") {
                break;
            }
            let chunk = &chunk[2..chunk.len() - 2];
            let split = chunk.windows(4).position(|w| w == b"\r\n\r\n").unwrap();
            out.push((
                String::from_utf8_lossy(&chunk[..split]).into_owned(),
                chunk[split + 4..].to_vec(),
            ));
        }
        out
    }
}

type Responder = Box<dyn Fn(usize, &Got) -> (u16, String) + Send>;

fn serve(expect: usize, respond: Responder) -> (String, std::thread::JoinHandle<Vec<Got>>) {
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let port = listener.local_addr().unwrap().port();
    listener.set_nonblocking(true).unwrap();
    let handle = std::thread::spawn(move || {
        let mut got = Vec::new();
        let deadline = Instant::now() + Duration::from_secs(30);
        while got.len() < expect && Instant::now() < deadline {
            let Ok((mut stream, _)) = listener.accept() else {
                std::thread::sleep(Duration::from_millis(5));
                continue;
            };
            stream.set_nonblocking(false).unwrap();
            stream.set_read_timeout(Some(Duration::from_secs(10))).unwrap();
            let mut buf = Vec::new();
            let mut chunk = [0u8; 64 << 10];
            let head_end = loop {
                if let Some(i) = buf.windows(4).position(|w| w == b"\r\n\r\n") {
                    break i;
                }
                let n = stream.read(&mut chunk).unwrap();
                assert!(n > 0, "client closed early");
                buf.extend_from_slice(&chunk[..n]);
            };
            let head = String::from_utf8_lossy(&buf[..head_end]).into_owned();
            let mut req = Got {
                head,
                body: Vec::new(),
            };
            let len: usize = req.header("content-length").unwrap().parse().unwrap();
            let mut body = buf[head_end + 4..].to_vec();
            while body.len() < len {
                let n = stream.read(&mut chunk).unwrap();
                assert!(n > 0, "client closed early");
                body.extend_from_slice(&chunk[..n]);
            }
            req.body = body;
            let (code, reply) = respond(got.len(), &req);
            write!(
                stream,
                "HTTP/1.1 {code} X\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{reply}",
                reply.len()
            )
            .unwrap();
            stream.flush().unwrap();
            got.push(req);
        }
        got
    });
    (format!("http://127.0.0.1:{port}/v1/logs"), handle)
}

fn collect_stages() -> (Arc<Mutex<Vec<Stage>>>, impl Fn(Stage)) {
    let stages = Arc::new(Mutex::new(Vec::new()));
    let sink = stages.clone();
    (stages, move |s| sink.lock().unwrap().push(s))
}

#[test]
fn posts_multipart_with_payload_json_and_the_zip() {
    let root = tmp("multipart");
    let (store, game) = fake_data(&root);
    let bundle = Bundle::collect_with(&store, &report(&game), Limits::default(), "20261020-101010");
    let (url, server) = serve(1, Box::new(|_, _| (204, String::new())));
    let hook = LogServer::new(Some(&url)).unwrap();
    let (stages, progress) = collect_stages();
    assert_eq!(hook.send(&bundle, &progress), Ok(1));
    let got = server.join().unwrap();
    assert_eq!(got.len(), 1);
    assert!(got[0].head.starts_with("POST /v1/logs HTTP/1.1"));
    assert!(
        got[0]
            .header("content-type")
            .unwrap()
            .starts_with("multipart/form-data; boundary=")
    );
    let parts = got[0].parts();
    assert_eq!(parts.len(), 2);
    assert!(parts[0].0.contains("name=\"payload_json\""));
    assert!(parts[0].0.contains("Content-Type: application/json"));
    let payload: Value = serde_json::from_slice(&parts[0].1).unwrap();
    let content = payload["content"].as_str().unwrap();
    assert!(
        content.starts_with("**Logs from `swofty`**\nLauncher "),
        "{content}"
    );
    assert!(content.contains("F1 25 1.17.0 (Steam build 20553335)"));
    assert!(content.ends_with(">>> It crashed on the grid"));
    assert!(!content.contains("part"));
    assert_eq!(payload["allowed_mentions"], json!({"parse": []}));
    assert_eq!(
        payload["attachments"][0]["filename"],
        "swofty-logs-swofty-20261020-101010.zip"
    );
    assert!(
        parts[1]
            .0
            .contains("name=\"files[0]\"; filename=\"swofty-logs-swofty-20261020-101010.zip\"")
    );
    assert!(parts[1].0.contains("Content-Type: application/zip"));
    assert_eq!(parts[1].1, bundle.zip().unwrap());
    assert!(names(&parts[1].1).contains("summary.txt"));
    let stages = stages.lock().unwrap();
    assert_eq!(stages.last(), Some(&Stage::Sent { parts: 1 }));
    std::fs::remove_dir_all(&root).ok();
}

#[test]
fn sends_parts_and_waits_out_rate_limits() {
    let bundle = noisy_bundle(&[700 << 10, 700 << 10, 700 << 10]);
    let (url, server) = serve(
        4,
        Box::new(|i, _| match i {
            1 => (
                429,
                r#"{"message":"You are being rate limited.","retry_after":0.05,"global":false}"#.into(),
            ),
            _ => (200, "{}".into()),
        }),
    );
    let hook = LogServer::new(Some(&url)).unwrap();
    let (stages, progress) = collect_stages();
    let sent = hook.send_with(&bundle, 1 << 20, Duration::from_secs(30), &progress);
    assert_eq!(sent, Ok(3));
    let got = server.join().unwrap();
    assert_eq!(got.len(), 4);
    let labels: Vec<String> = got
        .iter()
        .map(|g| {
            let p: Value = serde_json::from_slice(&g.parts()[0].1).unwrap();
            p["content"].as_str().unwrap().lines().next().unwrap().to_owned()
        })
        .collect();
    assert_eq!(
        labels,
        [
            "**Logs from `t`** · part 1/3",
            "**Logs from `t`** · part 2/3",
            "**Logs from `t`** · part 2/3",
            "**Logs from `t`** · part 3/3",
        ]
    );
    for g in &got {
        assert!(g.body.len() < (1 << 20) + 4096);
    }
    assert!(stages.lock().unwrap().contains(&Stage::Waiting { secs: 1 }));
}

#[test]
fn halves_the_part_size_when_the_server_says_too_big() {
    let bundle = noisy_bundle(&[700 << 10, 700 << 10]);
    let (url, server) = serve(
        4,
        Box::new(|_, g| {
            if g.body.len() > 1_100_000 {
                (
                    413,
                    r#"{"message":"Request entity too large","code":40005}"#.into(),
                )
            } else {
                (204, String::new())
            }
        }),
    );
    let hook = LogServer::new(Some(&url)).unwrap();
    let (_, progress) = collect_stages();
    assert_eq!(
        hook.send_with(&bundle, 4 << 20, Duration::from_secs(30), &progress),
        Ok(2)
    );
    let got = server.join().unwrap();
    assert_eq!(got.len(), 4);
    assert!(got[0].body.len() > 1_100_000 && got[1].body.len() > 1_100_000);

    let (url, server) = serve(1, Box::new(|_, _| (413, String::new())));
    let hook = LogServer::new(Some(&url)).unwrap();
    assert_eq!(
        hook.send_with(&bundle, 1 << 20, Duration::from_secs(30), &progress),
        Err(SendError::TooBig)
    );
    server.join().unwrap();
}

#[test]
fn reports_server_errors() {
    let hook = LogServer::new(Some("http://127.0.0.1:1/v1/logs")).unwrap();
    let bundle = noisy_bundle(&[10]);
    let (_, progress) = collect_stages();
    let err = hook
        .send_with(&bundle, 1 << 20, Duration::from_secs(5), &progress)
        .unwrap_err();
    assert!(matches!(err, SendError::Unreachable(_)), "{err:?}");
    assert!(
        err.to_string().starts_with("Couldn't reach the log server"),
        "{err}"
    );

    let (url, server) = serve(
        1,
        Box::new(|_, _| {
            (
                429,
                json!({"message": "too many logs sent from this network; try again later", "retry_after": 600.0})
                    .to_string(),
            )
        }),
    );
    let hook = LogServer::new(Some(&url)).unwrap();
    let err = hook
        .send_with(&bundle, 1 << 20, Duration::from_secs(5), &progress)
        .unwrap_err();
    server.join().unwrap();
    assert_eq!(err, SendError::RateLimited);

    let (url, server) = serve(
        3,
        Box::new(|_, _| {
            (
                503,
                json!({"message": "sending logs is not set up on this server"}).to_string(),
            )
        }),
    );
    let hook = LogServer::new(Some(&url)).unwrap();
    let err = hook
        .send_with(&bundle, 1 << 20, Duration::from_secs(5), &progress)
        .unwrap_err();
    server.join().unwrap();
    assert_eq!(
        err.to_string(),
        "The log server answered HTTP 503: sending logs is not set up on this server"
    );
}

#[test]
fn disabled_without_a_log_server_at_build_time() {
    assert!(LogServer::new(None).is_none());
    assert!(LogServer::new(Some("")).is_none());
    assert!(LogServer::new(Some("   ")).is_none());
    assert!(LogServer::new(Some("not a url")).is_none());
    if option_env!("SWOFTY_LOGS_URL").is_none() && std::env::var_os("SWOFTY_LOGS_URL").is_none() {
        assert!(!available());
        let (_, progress) = collect_stages();
        assert_eq!(send(&noisy_bundle(&[1]), &progress), Err(SendError::Unavailable));
        assert_eq!(
            SendError::Unavailable.to_string(),
            "Sending logs isn't available in this build"
        );
    }
}

#[test]
fn checks_the_name_and_message() {
    assert_eq!(check_name("  swofty  "), Ok("swofty".into()));
    assert!(check_name(" a ").is_err());
    assert!(check_name("").is_err());
    assert!(check_name(&"x".repeat(32)).is_ok());
    assert!(check_name(&"x".repeat(33)).is_err());
    assert_eq!(
        check_message("  it froze on the grid  "),
        Ok("it froze on the grid".into())
    );
    assert!(check_message("   ").is_err());
    assert_eq!(
        check_message("  it broke  "),
        Err("Describe what went wrong in at least 10 characters".into())
    );
    assert!(check_message(&"m".repeat(MESSAGE_MIN)).is_ok());
    assert!(check_message(&"m".repeat(1000)).is_ok());
    assert!(check_message(&"m".repeat(1001)).is_err());
    assert_eq!(utc_stamp(0), "19700101-000000");
    assert_eq!(utc_stamp(1_792_467_946), "20261020-034546");
    let mut b = noisy_bundle(&[1]);
    b.name = "we`ird @everyone".into();
    b.message = "m".repeat(1000);
    let part = &b.parts(1 << 20).unwrap()[0];
    let p: Value = serde_json::from_str(&payload(&b, part)).unwrap();
    let content = p["content"].as_str().unwrap();
    assert!(content.chars().count() <= 2000);
    assert!(content.starts_with("**Logs from `weird @everyone`**"));
    assert!(content.ends_with(&"m".repeat(1000)));
    assert_eq!(b.stem, "swofty-logs-t-20261020-101010");
    let (ct, body) = multipart("{}", "a\".zip", b"zip");
    assert!(ct.starts_with("multipart/form-data; boundary=swofty-logs-"));
    assert!(String::from_utf8_lossy(&body).contains("filename=\"a_.zip\""));
}

const PERF: &str = "Swofty Launcher performance report
written 2026-10-21 09:14:02 UTC, F1 25 build 25251167, launcher 1.0.12

== Summary
uptime: 00:12:41
frames: 41230
fps: 54.2 average, 31.0 1% low
frame times: 18.4 ms average, 33.1 ms p99, 212.6 ms worst
slow frames: 412 over 33.3 ms (1.0%)
swofty time: 0.41 ms per frame (2.2% of the frame)
worst call: swofty.career-editor menu_tick 48.2 ms at 00:03:12
hitches: 7 over 100 ms

== Addons
addon                         calls   total ms  per frame  worst ms
swofty.core                  412300     1803.2      0.044       3.1
swofty.career-editor          41230     9120.7      0.221      48.2
xanpavle.re-optimization        206       12.4      0.000       1.9

== Hooks
hook                    addon                  calls   total ms  worst ms
ui_menu_update          swofty.career-editor   41230     8911.0      48.2
renderer_settings_read  xanpavle.re-optimization 206       12.4       1.9

== Hitches
00:03:12.418  212.6 ms  swofty.career-editor menu_tick 48.2 ms
00:07:55.020  140.1 ms  (no addon call over 1 ms)

== Timeline
00:00:00  loader attached
00:00:41  main menu
00:03:12  career menu
";

#[test]
fn splits_a_perf_report_into_sections() {
    for text in [PERF.to_owned(), PERF.replace('\n', "\r\n")] {
        let s = perf::sections(&text);
        let titles: Vec<_> = s.iter().map(|s| s.title).collect();
        assert_eq!(titles, ["Summary", "Addons", "Hooks", "Hitches", "Timeline"]);
        assert_eq!(s[0].header, "== Summary");
        assert_eq!(s[0].lines.len(), 8);
        assert_eq!(s[0].lines[0], "uptime: 00:12:41");
        assert_eq!(s[1].lines.len(), 4);
        assert!(s[1].lines[2].starts_with("swofty.career-editor          41230"));
        assert_eq!(s[4].lines.last(), Some(&"00:03:12  career menu"));
    }
    let lenient = perf::sections("junk\n== Summary ==\nfps: 60\n\n\n==Not a header\n");
    assert_eq!(lenient.len(), 1);
    assert_eq!(lenient[0].title, "Summary");
    assert_eq!(lenient[0].lines, ["fps: 60", "", "", "==Not a header"]);
    assert!(perf::sections("").is_empty());
}

#[test]
fn excerpts_the_summary_and_addons_sections_in_sixty_lines() {
    let lines = perf::excerpt(PERF, PERF_LINES);
    assert_eq!(lines.first().map(String::as_str), Some("== Summary"));
    assert_eq!(lines[1], "uptime: 00:12:41");
    assert_eq!(lines[9], "");
    assert_eq!(lines[10], "== Addons");
    assert_eq!(
        lines.last().map(String::as_str),
        Some("xanpavle.re-optimization        206       12.4      0.000       1.9")
    );
    assert!(
        !lines
            .iter()
            .any(|l| l.contains("Hooks") || l.contains("loader attached"))
    );

    let rows: String = (0..100)
        .map(|i| format!("t.addon{i:03}  {i}  0.1  0.0  0.1\n"))
        .collect();
    let long = format!("{PERF}\n== Addons\n{rows}");
    let lines = perf::excerpt(&long, PERF_LINES);
    assert_eq!(lines.len(), PERF_LINES);
    assert!(
        lines.last().unwrap().ends_with("more lines in the file]"),
        "{lines:?}"
    );

    let other = perf::excerpt(
        "Swofty Launcher performance report\n== Frames\nfps: 60\n\n",
        PERF_LINES,
    );
    assert_eq!(
        other,
        ["Swofty Launcher performance report", "== Frames", "fps: 60"]
    );
    assert_eq!(perf::excerpt("\n\n", PERF_LINES), ["(the report is empty)"]);
}

fn summary_of(bundle: &Bundle) -> String {
    assert_eq!(bundle.entries[0].path, "summary.txt");
    String::from_utf8(bundle.entries[0].data.clone()).unwrap()
}

#[test]
fn bundles_the_running_games_logs_first_with_its_performance_summary() {
    let root = tmp("live");
    let (store, game) = fake_data(&root);
    put(&game.join("swofty/loader.log"), "live loader line\n");
    put(&game.join("swofty/perf.log"), PERF);
    let bundle = Bundle::collect_with(&store, &report(&game), Limits::default(), "20261021-091500");
    let order: Vec<_> = bundle.entries.iter().take(5).map(|e| e.path.as_str()).collect();
    assert_eq!(
        order,
        [
            "summary.txt",
            "config.json",
            "addons.json",
            "game/loader.log",
            "game/perf.log"
        ]
    );
    let zip = bundle.zip().unwrap();
    assert_eq!(entry(&zip, "game/loader.log"), "live loader line\n");
    assert_eq!(entry(&zip, "game/perf.log"), PERF);
    let summary = summary_of(&bundle);
    let excerpt = perf::excerpt(PERF, PERF_LINES).join("\n");
    assert!(
        summary.contains(&format!(
            "\n\nPerformance\nFrom game/perf.log\n{excerpt}\n\nAddons\n"
        )),
        "{summary}"
    );
    let addons = summary
        .split_once("\nAddons\n")
        .and_then(|(_, rest)| rest.split_once("\nFiles\n"))
        .map(|(block, _)| block)
        .unwrap_or_else(|| panic!("{summary}"));
    let (on, off) = addons
        .split_once("\n  Off:\n")
        .unwrap_or_else(|| panic!("{addons}"));
    assert!(
        on.starts_with("  Active profile: default, 1 of 2 addons on\n"),
        "{addons}"
    );
    assert!(
        on.contains("     1. SwoftyMod ") && on.contains(&format!("({}), built in", crate::builtin::CORE_ID)),
        "{addons}"
    );
    assert!(
        off.contains("        Helper 1.2.3 (t.helper), installed\n"),
        "{addons}"
    );
    assert!(summary.find("\nPerformance\n") < summary.find("\nFiles\n"));
    std::fs::remove_dir_all(&root).ok();
}

#[test]
fn leaves_out_live_logs_the_newest_session_already_kept() {
    let root = tmp("kept");
    let (store, game) = fake_data(&root);
    let newest = store.sessions_dir().join("20261020-090000");
    let perf = PERF.replace("uptime: 00:12:41", "uptime: 00:02:00");
    put(&game.join("swofty/loader.log"), "loader 3\n");
    put(&game.join("swofty/perf.log"), &perf);
    put(&newest.join("perf.log"), &perf);
    let bundle = Bundle::collect_with(&store, &report(&game), Limits::default(), "x");
    assert!(
        !bundle
            .entries
            .iter()
            .any(|e| e.path.starts_with("game/") && e.path.ends_with(".log"))
    );
    assert!(bundle.skipped.is_empty(), "{:?}", bundle.skipped);
    let summary = summary_of(&bundle);
    assert!(
        summary
            .contains("Performance\nFrom sessions/20261020-090000/perf.log\n== Summary\nuptime: 00:02:00\n"),
        "{summary}"
    );

    put(&game.join("swofty/perf.log"), PERF);
    let old = std::time::SystemTime::now() - Duration::from_secs(3600);
    std::fs::File::options()
        .write(true)
        .open(game.join("swofty/perf.log"))
        .unwrap()
        .set_modified(old)
        .unwrap();
    let bundle = Bundle::collect_with(&store, &report(&game), Limits::default(), "x");
    assert!(bundle.entries.iter().any(|e| e.path == "game/perf.log"));
    assert!(summary_of(&bundle).contains("From sessions/20261020-090000/perf.log\n"));
    std::fs::remove_dir_all(&root).ok();
}

#[test]
fn says_when_there_is_no_launch_or_game_session() {
    let root = tmp("nosession");
    let store = Store::new(root.join("data"));
    put(&store.sessions_dir().join("latest.txt"), "x");
    put(&store.sessions_dir().join("orphaned-logs/loader.log"), "orphan\n");
    put(&store.root.join("state/offline.log"), "offline line\n");
    let collect = || {
        summary_of(&Bundle::collect_with(
            &store,
            &report(&root),
            Limits::default(),
            "x",
        ))
    };
    let none = "\nGame sessions    none found: no launch log in state/ and no session in sessions/\n";
    assert!(!has_session(&store));
    assert!(collect().contains(none), "{}", collect());
    for launch in LAUNCH_LOGS {
        put(&store.root.join(launch), "launch line\n");
        assert!(has_session(&store), "{launch}");
        assert!(!collect().contains("Game sessions"), "{}", collect());
        std::fs::remove_file(store.root.join(launch)).unwrap();
    }
    put(&store.sessions_dir().join("20261020-090000/exit_code"), "0");
    assert!(has_session(&store));
    assert!(!collect().contains("Game sessions"), "{}", collect());
    std::fs::remove_dir_all(&root).ok();
}

#[test]
fn says_when_there_is_no_performance_report() {
    let root = tmp("noperf");
    let (store, game) = fake_data(&root);
    put(&store.sessions_dir().join("20261017-090000/perf.log"), PERF);
    let bundle = Bundle::collect_with(&store, &report(&game), Limits::default(), "x");
    let summary = summary_of(&bundle);
    assert!(
        summary.contains(
            "\nPerformance\nNo performance report was found (an older launcher, or the game never reached the menus)\n\nAddons\n"
        ),
        "{summary}"
    );
    put(&store.sessions_dir().join("20261019-090000/perf.log"), PERF);
    let summary = summary_of(&Bundle::collect_with(
        &store,
        &report(&game),
        Limits::default(),
        "x",
    ));
    assert!(
        summary.contains("From sessions/20261019-090000/perf.log\n== Summary\n"),
        "{summary}"
    );
    std::fs::remove_dir_all(&root).ok();
}

const CRASH: &str = "Swofty crash report

written      2026-10-21 12:26:16.158 local time
reason       unhandled ACCESS_VIOLATION (0xc0000005) writing 0x10 at F1_25.exe+0x3962704 in Ne_Heap_FreeTreeRemove+0x1c4
thread       36
process      pid 32, 2.7 s after the core loaded

== Exception
  ACCESS_VIOLATION (0xc0000005), flags 0x0, at F1_25.exe+0x3962704
  writing 0x10: free (unallocated) region

== Registers
  rax=0000000000000000 rbx=0000000000000000

== Stack (unwound with the modules' exception data)
  #0  0000000143962704  F1_25.exe+0x3962704 in Ne_Heap_FreeTreeRemove+0x1c4
  #1  0000000143963f5f  F1_25.exe+0x3963f5f in Ne_Heap_DeallocateNonSmall+0xfdf
  #2  0000000140809502  F1_25.exe+0x809502

== Ne heap
  heap object 0x140034300 'MainHeap': range 0x1000 +0x10000, guard 32 bytes

== Game state (from the black box)
  last flow      race load (1.2 s before this report)
  track and car count: core does not read them

== Minidump
  crash-20261021-122616.dmp, 0.1 MB, thread stacks and registers
";

#[test]
fn includes_the_newest_crash_report_and_minidump() {
    let root = tmp("crash");
    let (store, game) = fake_data(&root);
    let loader = game.join("swofty");
    put(
        &loader.join("crash-20261021-090000.txt"),
        "Swofty crash report\n\nreason older\n",
    );
    put(&loader.join("crash-20261021-122616.txt"), CRASH);
    put(&loader.join("crash-20261021-122616.dmp"), noise(200_000, 7));
    let bundle = Bundle::collect_with(&store, &report(&game), Limits::default(), "20261021-130000");
    let names: Vec<_> = bundle.entries.iter().map(|e| e.path.clone()).collect();
    assert!(
        names.contains(&"crash/crash-20261021-122616.txt".to_string()),
        "{names:?}"
    );
    assert!(
        names.contains(&"crash/crash-20261021-122616.dmp".to_string()),
        "{names:?}"
    );
    assert!(
        !names.iter().any(|n| n.contains("crash-20261021-090000")),
        "only the newest report: {names:?}"
    );
    let zip = bundle.zip().unwrap();
    assert_eq!(entry(&zip, "crash/crash-20261021-122616.txt"), CRASH);
    let mut dump = Vec::new();
    zip::ZipArchive::new(std::io::Cursor::new(&zip))
        .unwrap()
        .by_name("crash/crash-20261021-122616.dmp")
        .unwrap()
        .read_to_end(&mut dump)
        .unwrap();
    assert_eq!(
        dump,
        noise(200_000, 7),
        "the minidump is sent byte for byte, not redacted"
    );
    let summary = summary_of(&bundle);
    assert!(summary.contains("\nCrash\n"), "{summary}");
    assert!(
        summary.contains("reason       unhandled ACCESS_VIOLATION"),
        "{summary}"
    );
    assert!(summary.contains("Ne_Heap_DeallocateNonSmall"), "{summary}");
    assert!(summary.contains("last flow      race load"), "{summary}");
    assert!(summary.find("\nCrash\n") < summary.find("\nPerformance\n"));
    std::fs::remove_dir_all(&root).ok();
}

#[test]
fn skips_a_minidump_too_big_for_the_bundle() {
    let root = tmp("crashbig");
    let (store, game) = fake_data(&root);
    let loader = game.join("swofty");
    put(&loader.join("crash-20261021-122616.txt"), CRASH);
    put(&loader.join("crash-20261021-122616.dmp"), noise(2 << 20, 3));
    let limits = Limits {
        file: FILE_CAP,
        total: 1 << 20,
    };
    let bundle = Bundle::collect_with(&store, &report(&game), limits, "x");
    assert!(
        bundle
            .entries
            .iter()
            .any(|e| e.path == "crash/crash-20261021-122616.txt"),
        "the report still goes"
    );
    assert!(
        !bundle.entries.iter().any(|e| e.path.ends_with(".dmp")),
        "the dump is not included"
    );
    assert!(
        bundle
            .skipped
            .iter()
            .any(|s| s.contains("crash-20261021-122616.dmp") && s.contains("too big")),
        "{:?}",
        bundle.skipped
    );
    assert!(
        summary_of(&bundle).contains("crash-20261021-122616.dmp"),
        "the skip is noted in the summary"
    );
    std::fs::remove_dir_all(&root).ok();
}

#[test]
fn finds_a_crash_report_kept_in_a_session_folder() {
    let root = tmp("crashsess");
    let (store, game) = fake_data(&root);
    let newest = store.sessions_dir().join("20261020-090000");
    put(&newest.join("game-logs/crash-20261020-093000.txt"), CRASH);
    put(
        &newest.join("game-logs/crash-20261020-093000.dmp"),
        noise(50_000, 9),
    );
    let bundle = Bundle::collect_with(&store, &report(&game), Limits::default(), "x");
    let names: Vec<_> = bundle.entries.iter().map(|e| e.path.clone()).collect();
    assert!(
        names.contains(&"crash/crash-20261020-093000.txt".to_string()),
        "{names:?}"
    );
    assert!(
        names.contains(&"crash/crash-20261020-093000.dmp".to_string()),
        "{names:?}"
    );
    assert!(
        !names
            .iter()
            .any(|n| n.starts_with("sessions/") && n.contains("crash-")),
        "the crash files are not also added as redacted session text: {names:?}"
    );
    std::fs::remove_dir_all(&root).ok();
}

#[test]
fn sends_the_career_editor_snapshot_a_game_that_cant_write_its_folder_left_in_the_data_folder() {
    let root = tmp("snapshot-kept");
    let (store, game) = fake_data(&root);
    std::fs::File::options()
        .write(true)
        .open(game.join(SNAPSHOT))
        .unwrap()
        .set_modified(std::time::SystemTime::now() - Duration::from_secs(3600))
        .unwrap();
    let kept = crate::addon_storage::addon_dir(&store.root, "swofty.career-editor").join("snapshot.json");
    put(&kept, r#"{"status":{"career":false}}"#);
    let bundle = Bundle::collect_with(&store, &report(&game), Limits::default(), "20261020-101010");
    assert_eq!(
        entry(&bundle.zip().unwrap(), "game/career-editor/snapshot.json"),
        r#"{"status":{"career":false}}"#
    );
}

#[test]
fn sends_the_loaders_fallback_log_and_says_why_it_was_used() {
    let root = tmp("fallback");
    let (store, game) = fake_data(&root);
    let fallback = "2026-10-24 16:34:40.020 [ 1416] swofty loader (version.dll proxy) attached, pid 24676\r\n\
         2026-10-24 16:34:40.020 [ 1416] log: C:\\Program Files\\EA Games\\F1 25\\swofty\\loader.log could not be \
         opened (error 5); this is the fallback log C:\\Users\\id\\AppData\\Local\\Swofty\\state\\loader-fallback.log\r\n\
         2026-10-24 16:34:40.021 [ 1416] process: user id (S-1-5-21-1-2-3-1001), medium integrity, not elevated, \
         UAC virtualization off, __COMPAT_LAYER=(unset)\r\n";
    put(&store.root.join(LOADER_FALLBACK), fallback);
    let bundle = Bundle::collect_with(&store, &report(&game), Limits::default(), "x");
    assert!(names(&bundle.zip().unwrap()).contains(LOADER_FALLBACK));
    let why = "logged in the data folder: C:\\Program Files\\EA Games\\F1 25\\swofty\\loader.log could not be \
               opened (error 5); the game runs as user id (S-1-5-21-1-2-3-1001), medium integrity, not elevated, UAC \
               virtualization off, __COMPAT_LAYER=(unset)\n";
    let summary = summary_of(&bundle);
    assert!(
        summary.contains(&format!(
            "\nMod loader\n  state/loader-fallback.log                mod loader attached\n  {:40} {why}",
            ""
        )),
        "{summary}"
    );
    put(&store.sessions_dir().join("20261020-090000/loader.log"), fallback);
    let summary = summary_of(&Bundle::collect_with(
        &store,
        &report(&game),
        Limits::default(),
        "x",
    ));
    assert!(
        summary.contains(&format!(
            "\nMod loader\n  sessions/20261020-090000                 mod loader attached\n  {:40} {why}",
            ""
        )),
        "the live fallback is the newest session's, so the session's line stands for it: {summary}"
    );
    std::fs::remove_dir_all(&root).ok();
}

#[test]
fn names_the_game_files_windows_virtualised() {
    let root = tmp("virtualised");
    let copy = root.join("VirtualStore/Program Files/EA Games/F1 25");
    assert_eq!(virtualised(&copy), None);
    for rel in [
        "version.dll",
        "EAAntiCheat.GameServiceLauncher.exe",
        "swofty/addons/core/swofty_core.dll",
    ] {
        put(&copy.join(rel), "x");
    }
    let line = virtualised(&copy).unwrap();
    assert!(
        line.starts_with("Windows keeps 3 file(s) written to the game folder in "),
        "{line}"
    );
    assert!(
        line.ends_with(
            ", where F1 25 doesn't read them: EAAntiCheat.GameServiceLauncher.exe, swofty/addons/core/swofty_core.dll, \
             version.dll"
        ),
        "{line}"
    );
    std::fs::remove_dir_all(&root).ok();
}

#[test]
fn says_when_the_mod_loader_never_attached() {
    let root = tmp("noloader");
    let (store, game) = fake_data(&root);
    let sessions = store.sessions_dir();
    put(
        &sessions.join("20261020-090000/loader.log"),
        "01:12:12.575 [stub] [offline] session launcher invoked, args: <none>\n\
         01:12:13.901 [stub] [offline] game image at 0x140000000, patching\n",
    );
    put(
        &sessions.join("20261019-090000/loader.log"),
        "2026-10-19 21:16:05.789 [ 2548] swofty loader (version.dll proxy) attached, pid 14088\n",
    );
    let summary = summary_of(&Bundle::collect_with(
        &store,
        &report(&game),
        Limits::default(),
        "x",
    ));
    assert!(
        summary.contains(
            "\nMod loader\n  sessions/20261020-090000                 mod loader never attached \
             (version.dll not loaded)\n  sessions/20261019-090000                 \
             mod loader attached\n"
        ),
        "{summary}"
    );
    crate::quarantine::Report::new(
        crate::quarantine::Stage::InGame,
        vec!["version.dll".into()],
        &game,
    )
    .write(&store.root)
    .unwrap();
    let summary = summary_of(&Bundle::collect_with(
        &store,
        &report(&game),
        Limits::default(),
        "x",
    ));
    assert!(
        summary.contains("\nMod loader\n  last launch: Mods did not load (version.dll)\n"),
        "{summary}"
    );
    assert!(!summary.contains("  found: "), "{summary}");
    let mut blocked = crate::quarantine::Report::new(
        crate::quarantine::Stage::InGame,
        vec!["version.dll".into()],
        &game,
    );
    blocked.cause = Some(Box::new(crate::quarantine::Cause::SmartAppControl { seen: true }));
    blocked.write(&store.root).unwrap();
    let summary = summary_of(&Bundle::collect_with(
        &store,
        &report(&game),
        Limits::default(),
        "x",
    ));
    assert!(
        summary.contains(
            "\n  last launch: Mods did not load (version.dll)\n  found: Smart App Control blocked version.dll \
             (the [loader] lines in state/wrapper-run.log say more)\n"
        ),
        "{summary}"
    );
    std::fs::remove_dir_all(&root).ok();
}

#[test]
fn names_a_program_windows_blocked() {
    let root = tmp("blocked");
    let (store, game) = fake_data(&root);
    let cli = std::path::Path::new(r"C:\Swofty\bin\swofty.exe");
    let block = crate::app_control::Block::from_code(cli, 4551).unwrap();
    crate::launch::Failure::new(crate::launch::Cause::Blocked(block))
        .write(&store.root)
        .unwrap();
    let summary = summary_of(&Bundle::collect_with(
        &store,
        &report(&game),
        Limits::default(),
        "x",
    ));
    assert!(
        summary.contains(
            "\nMod loader\n  last launch: Windows blocked a Swofty program (Smart App Control or an app \
             control policy blocked swofty.exe)\n  blocked: C:\\Swofty\\bin\\swofty.exe (os error 4551)\n"
        ),
        "{summary}"
    );
    std::fs::remove_dir_all(&root).ok();
}

#[test]
fn sends_the_newest_addon_logs() {
    let root = tmp("addon-logs");
    let (store, game) = fake_data(&root);
    let dir = store.root.join("addon-data/teamsimplified.mods/logs");
    std::fs::create_dir_all(&dir).unwrap();
    let base = std::time::SystemTime::now() - std::time::Duration::from_secs(3600);
    for i in 0..7 {
        let path = dir.join(format!("convert-{i}.log"));
        std::fs::write(&path, format!("conversion {i}\n")).unwrap();
        let f = std::fs::File::options().write(true).open(&path).unwrap();
        f.set_modified(base + std::time::Duration::from_secs(i * 60))
            .unwrap();
    }
    std::fs::write(dir.join("notes.txt"), "not a log").unwrap();
    let bundle = Bundle::collect_with(&store, &report(&game), Limits::default(), "20261020-101010");
    let zip = bundle.zip().unwrap();
    let logs: Vec<String> = names(&zip)
        .into_iter()
        .filter(|n| n.starts_with("addons/teamsimplified.mods/logs/"))
        .collect();
    assert_eq!(
        logs,
        (2..7)
            .map(|i| format!("addons/teamsimplified.mods/logs/convert-{i}.log"))
            .collect::<Vec<_>>()
    );
    assert_eq!(
        entry(&zip, "addons/teamsimplified.mods/logs/convert-6.log"),
        "conversion 6\n"
    );
    std::fs::remove_dir_all(&root).ok();
}

fn aged(path: &Path, base: std::time::SystemTime, secs: u64) {
    put(path, "zip");
    std::fs::File::options()
        .write(true)
        .open(path)
        .unwrap()
        .set_modified(base + Duration::from_secs(secs))
        .unwrap();
}

fn listed(dir: &Path) -> BTreeSet<String> {
    std::fs::read_dir(dir)
        .unwrap()
        .flatten()
        .map(|e| e.file_name().to_string_lossy().into_owned())
        .collect()
}

#[test]
fn keeps_the_zip_in_the_data_folder() {
    let root = tmp("keep");
    let (store, game) = fake_data(&root);
    let bundle = Bundle::collect_with(&store, &report(&game), Limits::default(), "20261020-101010");
    let dir = kept_dir(&store);
    assert_eq!(dir, store.root.join("logs"));
    let path = bundle.save(&dir, KEPT).unwrap();
    assert_eq!(path, dir.join("swofty-logs-swofty-20261020-101010.zip"));
    assert_eq!(listed(&dir), BTreeSet::from([bundle.file_name()]));
    let kept = std::fs::read(&path).unwrap();
    assert_eq!(names(&kept), names(&bundle.zip().unwrap()));
    assert_eq!(entry(&kept, "state/launcher-run.log"), "launcher line\n");
    let again = Bundle::collect_with(&store, &report(&game), Limits::default(), "20261020-101011");
    assert!(
        !names(&again.zip().unwrap())
            .iter()
            .any(|n| n.contains("swofty-logs-")),
        "a kept bundle went into the next one"
    );
    std::fs::remove_dir_all(&root).ok();
}

#[test]
fn keeps_only_the_newest_bundles() {
    let root = tmp("prune");
    let dir = root.join("logs");
    let base = std::time::SystemTime::now() - Duration::from_secs(3600);
    for i in 0..12 {
        aged(
            &dir.join(format!("swofty-logs-p{i:02}-20261020-1010{i:02}.zip")),
            base,
            i * 60,
        );
    }
    aged(&dir.join("notes.txt"), base, 0);
    aged(&dir.join("swofty-logs-left-20261019-000000.zip.tmp"), base, 0);
    std::fs::create_dir_all(dir.join("swofty-logs-folder.zip")).unwrap();
    let bundle = noisy_bundle(&[100]);
    let path = bundle.save(&dir, 10).unwrap();
    let kept: BTreeSet<String> = listed(&dir)
        .into_iter()
        .filter(|n| n.starts_with("swofty-logs-") && n.ends_with(".zip"))
        .collect();
    let mut want: BTreeSet<String> = (3..12)
        .map(|i| format!("swofty-logs-p{i:02}-20261020-1010{i:02}.zip"))
        .collect();
    want.insert(bundle.file_name());
    want.insert("swofty-logs-folder.zip".into());
    assert_eq!(kept, want);
    assert!(dir.join("notes.txt").is_file());
    assert!(dir.join("swofty-logs-left-20261019-000000.zip.tmp").is_file());
    assert!(path.is_file());

    let old = dir.join("swofty-logs-p03-20261020-101003.zip");
    let removed = prune(&dir, 3, &old);
    assert!(old.is_file(), "pruning never removes the bundle just written");
    assert_eq!(removed.len(), 7, "{removed:?}");
    let left: BTreeSet<String> = listed(&dir)
        .into_iter()
        .filter(|n| n.ends_with(".zip") && dir.join(n).is_file())
        .collect();
    assert_eq!(
        left,
        BTreeSet::from([
            "swofty-logs-p03-20261020-101003.zip".to_owned(),
            "swofty-logs-p11-20261020-101011.zip".to_owned(),
            bundle.file_name(),
        ])
    );
    assert!(prune(&root.join("missing"), 3, &old).is_empty());
    std::fs::remove_dir_all(&root).ok();
}

#[test]
fn lists_the_addons_by_name_in_load_order_with_their_source_and_settings() {
    let root = tmp("addon-list");
    let (store, game) = fake_data(&root);
    put(
        &store.addons_dir().join("mod.cars/swofty.json"),
        r#"{"schema_version":1,"id":"mod.cars","name":"Cars › Classic","version":"1.0.0","author":"t","compat":{"game":"f1_25"},
            "origin":{"tool":"teamsimplified.mods","file":"Cars.zip"}}"#,
    );
    put(
        &store.addons_dir().join("t.grid/swofty.json"),
        r#"{"schema_version":1,"id":"t.grid","name":"Grid","version":"0.2.0","author":"t","compat":{"game":"f1_25"},
            "options":[{"id":"cars","type":"slider","label":"Cars","default":22,"min":22,"max":30,"step":1,"binds":"db"},
                       {"id":"api_token","type":"choice","default":"a","values":["a","b"],"binds":"native"}]}"#,
    );
    put(
        &store.root.join("profiles/default.json"),
        r#"{"schema_version":1,"name":"default","addons":[{"id":"t.grid","enabled":true},{"id":"mod.cars","enabled":true},
            {"id":"t.helper","enabled":false}],"options":{"t.grid":{"cars":26,"api_token":"b"}}}"#,
    );
    put(
        &store.root.join("profiles/test.json"),
        r#"{"schema_version":1,"name":"test","addons":[{"id":"mod.cars","enabled":true}]}"#,
    );
    let pairs = r#"{"profile":"default","built_at":1.0,"pairs":[{"winner":"t.grid","loser":"mod.cars","database":1,"resources":0,"files":0,"items":["tracks.Track_3.laps"]}]}"#;
    put(&crate::conflicts::path(&store, "default"), pairs);
    let bundle = Bundle::collect_with(&store, &report(&game), Limits::default(), "20261023-120000");
    let summary = summary_of(&bundle);
    let addons = summary
        .split_once("\nAddons\n")
        .and_then(|(_, rest)| rest.split_once("\nFiles\n"))
        .map(|(block, _)| block)
        .unwrap_or_else(|| panic!("{summary}"));
    let on: Vec<&str> = addons
        .lines()
        .filter(|l| l.trim_start().starts_with(char::is_numeric))
        .collect();
    assert_eq!(on.len(), 3, "{addons}");
    assert!(on[1].ends_with("2. Grid 0.2.0 (t.grid), installed"), "{addons}");
    assert!(
        on[2].ends_with("3. Cars › Classic 1.0.0 (mod.cars), imported from Cars.zip"),
        "{addons}"
    );
    assert!(
        addons.contains(
            "(t.grid), installed\n          Cars: 26 (default 22)\n          api_token: [redacted] (default [redacted])\n"
        ),
        "{addons}"
    );
    assert!(
        addons.contains("Off:\n        Helper 1.2.3 (t.helper), installed\n"),
        "{addons}"
    );
    assert!(
        addons.contains("Other profiles: vanilla (0 on), test (2 on)"),
        "{addons}"
    );
    let zip = bundle.zip().unwrap();
    assert_eq!(entry(&zip, "conflicts.json"), pairs);
    let json: Value = serde_json::from_str(&entry(&zip, "addons.json")).unwrap();
    let cars = json["installed"]
        .as_array()
        .unwrap()
        .iter()
        .find(|a| a["id"] == "mod.cars")
        .unwrap();
    assert_eq!(cars["source"], "imported from Cars.zip");
    assert_eq!(cars["origin"]["file"], "Cars.zip");
    let default = json["profiles"]
        .as_array()
        .unwrap()
        .iter()
        .find(|p| p["name"] == "default")
        .unwrap();
    let grid = default["addons"]
        .as_array()
        .unwrap()
        .iter()
        .find(|a| a["id"] == "t.grid")
        .unwrap();
    assert_eq!(grid["name"], "Grid");
    assert_eq!(grid["options"]["cars"], 26);
    assert_eq!(grid["options"]["api_token"], "[redacted]");
    std::fs::remove_dir_all(&root).ok();
}

#[test]
fn names_the_overlay_addons_and_whether_the_overlay_drew() {
    let root = tmp("overlay");
    let (store, game) = fake_data(&root);
    put(
        &store.root.join("addons/kizi.pitlane/swofty.json"),
        r#"{"schema_version":1,"id":"kizi.pitlane","name":"Manual Pit Lane","version":"0.2.3","author":"kizi",
            "compat":{"game":"f1_25"},"experimental_features":["overlay"]}"#,
    );
    put(
        &store.root.join("profiles/default.json"),
        r#"{"schema_version":1,"name":"default","addons":[{"id":"t.helper","enabled":true},{"id":"kizi.pitlane","enabled":true}]}"#,
    );
    put(
        &store.root.join("sessions/20261020-090000/loader.log"),
        "1 [overlay] present hook armed: Present (vt+0x40)\n\
         2 [overlay] api: kizi.pitlane on_paint -> ok (token 1)\n\
         3 [overlay] renderer up: 2560x1440, 4 buffer(s), format 24\n",
    );
    put(
        &store.root.join("sessions/20261019-090000/loader.log"),
        "1 [overlay] api: kizi.pitlane on_paint -> ok (token 1)\n\
         2 [overlay] drawing on swapchain 0000000012345678 with the DIRECT queue 00000000AAAA\n\
         3 [overlay] first overlay frame submitted: 96 vertices\n\
         4 [overlay] the GPU was removed (present 0x887a0005, reason 0x887a0006) 4.8 s after the overlay last drew \
         (swapchain by F1_25.exe); the overlay stops drawing\n",
    );
    let bundle = Bundle::collect_with(&store, &report(&game), Limits::default(), "20261021-091500");
    let zip = bundle.zip().unwrap();
    let summary = summary_of(&bundle);
    let overlay = summary
        .split_once("\nOverlay\n")
        .and_then(|(_, rest)| rest.split_once("\n\nCrash\n"))
        .map(|(block, _)| block)
        .unwrap_or_else(|| panic!("{summary}"));
    let lines: Vec<&str> = overlay.lines().collect();
    assert_eq!(
        lines[0],
        "  Addons that draw over the game: Manual Pit Lane (kizi.pitlane)"
    );
    assert!(
        lines.contains(
            &"  sessions/20261020-090000                 drew on 2560x1440, 4 buffer(s), format 24; on_paint by kizi.pitlane"
        ),
        "{overlay}"
    );
    assert!(
        lines.contains(
            &"  sessions/20261019-090000                 drew on 0000000012345678 with the DIRECT queue 00000000AAAA; \
              on_paint by kizi.pitlane; the GPU was \
              removed (present 0x887a0005, reason 0x887a0006) 4.8 s after the overlay last drew (swapchain by \
              F1_25.exe); the overlay stops drawing"
        ),
        "{overlay}"
    );
    assert!(
        lines.contains(&"  sessions/20261018-090000                 no [overlay] lines"),
        "{overlay}"
    );
    let addons: serde_json::Value = serde_json::from_str(&entry(&zip, "addons.json")).unwrap();
    let pitlane = addons["installed"]
        .as_array()
        .unwrap()
        .iter()
        .find(|a| a["id"] == "kizi.pitlane")
        .unwrap();
    assert_eq!(pitlane["experimental_features"], serde_json::json!(["overlay"]));
    std::fs::remove_dir_all(&root).ok();
}

#[test]
fn sends_the_room_logs_trace_and_state_without_secrets() {
    let root = tmp("room");
    let (store, game) = fake_data(&root);
    crate::multiplayer::diag::secret("bundle-test", &["turn-credential-9f8e7d"]);
    put(
        &store.root.join(crate::multiplayer::diag::FILE),
        "20261026-101010.000Z room ABCD opened: 1 member(s)\n20261026-101011.000Z leaked turn-credential-9f8e7d\n",
    );
    put(
        &store.root.join(crate::multiplayer::diag::PREVIOUS),
        "20261026-090000.000Z phase: in the lobby\n",
    );
    put(
        &game.join(crate::multiplayer::bridge::STATUS_FILE),
        r#"{"pid":42,"self":2,"base":41000,"game_port":21414}"#,
    );
    put(&game.join(NET_TRACE), "# swofty net trace\nbind s=1 port=21414\n");
    put(
        &store
            .root
            .join(crate::multiplayer::diag::PRINTS_DIR)
            .join("ABCD-20261026-101010.json"),
        r#"{"room":"ABCD","host":{"note":"turn-credential-9f8e7d"}}"#,
    );
    let mut r = report(&game);
    r.room = Some(serde_json::json!({
        "phase": {"phase": "in_lobby"},
        "room": {"code": "ABCD"},
        "session_token": "tok-should-go",
        "last_error": "relay said turn-credential-9f8e7d",
    }));
    let bundle = Bundle::collect_with(&store, &r, Limits::default(), "20261026-101010");
    crate::multiplayer::diag::secret("bundle-test", &[]);
    let zip = bundle.zip().unwrap();
    let got = names(&zip);
    for want in [
        "state/multiplayer.log",
        "state/multiplayer.1.log",
        "game/netplay.json",
        "game/net-trace.log",
        "multiplayer/session.json",
        "state/multiplayer-prints/ABCD-20261026-101010.json",
    ] {
        assert!(got.contains(want), "no {want} in {got:?}");
    }
    assert!(entry(&zip, "state/multiplayer.log").contains("room ABCD opened"));
    assert!(entry(&zip, "game/netplay.json").contains("\"game_port\":21414"));
    assert!(entry(&zip, "game/net-trace.log").contains("bind s=1 port=21414"));
    let room = entry(&zip, "multiplayer/session.json");
    assert!(room.contains("\"code\": \"ABCD\""), "{room}");
    for name in &got {
        let text = entry(&zip, name);
        assert!(!text.contains("turn-credential-9f8e7d"), "{name}: {text}");
        assert!(!text.contains("tok-should-go"), "{name}: {text}");
    }
}

#[test]
fn leaves_out_room_files_that_are_not_there() {
    let root = tmp("no-room");
    let (store, game) = fake_data(&root);
    let bundle = Bundle::collect_with(&store, &report(&game), Limits::default(), "20261026-101010");
    let got = names(&bundle.zip().unwrap());
    for absent in [
        "state/multiplayer.log",
        "game/netplay.json",
        "game/net-trace.log",
        "multiplayer/session.json",
    ] {
        assert!(!got.contains(absent), "{absent} in {got:?}");
    }
}

#[test]
fn memory_names_the_page_file_when_windows_gives_the_commit_limit() {
    let gb = 1024 * 1024 * 1024;
    assert_eq!(
        memory_text(16 * gb, Some(35 * gb)),
        ", 16 GB memory, 19 GB page file (35 GB commit limit)"
    );
    assert_eq!(memory_text(32 * gb, None), ", 32 GB memory");
}

#[test]
fn an_overlay_sessions_netplay_files_come_from_its_upper_dir() {
    let root = tmp("overlay-room");
    let (store, game) = fake_data(&root);
    let upper = root.join("upper");
    crate::overlayfs::LastSession {
        game: game.clone(),
        profile: "default".into(),
        upper: upper.clone(),
    }
    .write(&store.root)
    .unwrap();
    put(
        &upper.join(crate::multiplayer::bridge::STATUS_FILE),
        r#"{"pid":42,"self":2,"base":41000,"game_port":21414}"#,
    );
    put(&upper.join(NET_TRACE), "# swofty net trace\n");
    let bundle = Bundle::collect_with(&store, &report(&game), Limits::default(), "20261026-101010");
    let got = names(&bundle.zip().unwrap());
    assert!(got.iter().any(|n| n == "game/netplay.json"), "{got:?}");
    assert!(got.iter().any(|n| n == "game/net-trace.log"), "{got:?}");
    std::fs::remove_dir_all(&root).ok();
}

#[test]
fn bundles_the_cores_registry_reports_and_the_career_index() {
    let root = tmp("registry-reports");
    let (store, game) = fake_data(&root);
    put(
        &game.join("swofty/registry/effective.json"),
        "{\"career.season\":{}}\n",
    );
    put(
        &game.join("swofty/registry/conflicts.json"),
        "{\"registry\":[]}\n",
    );
    put(
        &game.join("swofty/registry/room.json"),
        "{\"presize\":\"applied\"}\n",
    );
    put(&game.join("swofty/careers/index.json"), "{\"careers\":[]}\n");
    let bundle = Bundle::collect_with(&store, &report(&game), Limits::default(), "20261028-120000");
    let zip = bundle.zip().unwrap();
    assert_eq!(
        entry(&zip, "game/registry/effective.json"),
        "{\"career.season\":{}}\n"
    );
    assert_eq!(entry(&zip, "game/registry/conflicts.json"), "{\"registry\":[]}\n");
    assert_eq!(
        entry(&zip, "game/registry/room.json"),
        "{\"presize\":\"applied\"}\n"
    );
    assert_eq!(entry(&zip, "game/careers/index.json"), "{\"careers\":[]}\n");
    assert!(
        !bundle
            .entries
            .iter()
            .any(|e| e.path == "game/registry/catalog.json")
    );
}

#[test]
fn the_cores_reports_in_the_data_folder_win_over_the_game_folders() {
    let root = tmp("registry-reports-data");
    let (store, game) = fake_data(&root);
    put(&game.join("swofty/registry/effective.json"), "{\"old\":1}\n");
    put(&game.join("swofty/registry/room.json"), "{\"old\":1}\n");
    put(&store.root.join("registry/effective.json"), "{\"new\":1}\n");
    put(
        &store.root.join("careers/index.json"),
        "{\"careers\":[{\"career\":\"a\"}]}\n",
    );
    put(&game.join("swofty/careers/index.json"), "{\"careers\":[]}\n");
    let bundle = Bundle::collect_with(&store, &report(&game), Limits::default(), "20261028-120000");
    let zip = bundle.zip().unwrap();
    assert_eq!(entry(&zip, "game/registry/effective.json"), "{\"new\":1}\n");
    assert_eq!(entry(&zip, "game/registry/room.json"), "{\"old\":1}\n");
    assert_eq!(
        entry(&zip, "game/careers/index.json"),
        "{\"careers\":[{\"career\":\"a\"}]}\n"
    );
}

#[test]
fn carries_the_blocked_record_when_there_is_one() {
    let root = tmp("blocked");
    let (store, game) = fake_data(&root);
    let bundle = Bundle::collect_with(&store, &report(&game), Limits::default(), "20261020-101010");
    assert!(!bundle.entries.iter().any(|e| e.path == "state/blocked.json"));
    put(&store.root.join("state/blocked.json"), "{\"files\":[]}");
    let bundle = Bundle::collect_with(&store, &report(&game), Limits::default(), "20261020-101010");
    assert!(bundle.entries.iter().any(|e| e.path == "state/blocked.json"));
}
