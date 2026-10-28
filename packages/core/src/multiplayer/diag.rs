//! The session diagnostics log, `<data>/state/multiplayer.log`: one timestamped line per event,
//! rotated to `multiplayer.1.log` at [`ROTATE_AT`]. Nothing is written until [`open`] names the
//! data folder. Registered secrets are replaced with `[redacted]` in every line.

use std::fmt::Display;
use std::fs::{File, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::sync::{Mutex, MutexGuard};
use std::time::{SystemTime, UNIX_EPOCH};

pub const FILE: &str = "state/multiplayer.log";
pub const PREVIOUS: &str = "state/multiplayer.1.log";
pub const ROTATE_AT: u64 = 5 << 20;
/// Where a session keeps both sides' fingerprints when they differ; see [`save_prints`].
pub const PRINTS_DIR: &str = "state/multiplayer-prints";
/// How many saved fingerprint comparisons are kept.
pub const PRINTS_KEPT: usize = 10;

const REDACTED: &str = "[redacted]";
const SECRET_MIN: usize = 6;

struct Sink {
    path: PathBuf,
    previous: PathBuf,
    file: Option<File>,
    len: u64,
    rotate_at: u64,
}

static SINK: Mutex<Option<Sink>> = Mutex::new(None);
static SECRETS: Mutex<Vec<(&'static str, String)>> = Mutex::new(Vec::new());
#[cfg(test)]
static TAPS: Mutex<Vec<std::sync::Arc<Mutex<String>>>> = Mutex::new(Vec::new());

fn lock<T>(m: &Mutex<T>) -> MutexGuard<'_, T> {
    m.lock().unwrap_or_else(|e| e.into_inner())
}

pub fn open(data_root: &Path) {
    *lock(&SINK) = Some(Sink {
        path: data_root.join(FILE),
        previous: data_root.join(PREVIOUS),
        file: None,
        len: 0,
        rotate_at: ROTATE_AT,
    });
}

pub fn path(data_root: &Path) -> PathBuf {
    data_root.join(FILE)
}

/// Saves `doc` as `<data>/state/multiplayer-prints/<name>.json` in the data folder [`open`] named,
/// with registered secrets redacted, and keeps the newest [`PRINTS_KEPT`]. Nothing happens
/// before [`open`].
pub fn save_prints(name: &str, doc: &serde_json::Value) {
    let root = lock(&SINK)
        .as_ref()
        .and_then(|s| Some(s.path.parent()?.parent()?.to_path_buf()));
    if let Some(root) = root
        && let Err(e) = write_prints(&root, name, doc)
    {
        line(format!("fingerprint: saving the comparison failed: {e}"));
    }
}

/// [`save_prints`] into the data folder `data_root`.
pub fn write_prints(data_root: &Path, name: &str, doc: &serde_json::Value) -> std::io::Result<PathBuf> {
    let dir = data_root.join(PRINTS_DIR);
    std::fs::create_dir_all(&dir)?;
    let name: String = name
        .chars()
        .map(|c| {
            if c.is_ascii_alphanumeric() || c == '-' || c == '_' {
                c
            } else {
                '_'
            }
        })
        .collect();
    let path = dir.join(format!("{name}.json"));
    let text = scrub(&serde_json::to_string_pretty(doc).unwrap_or_default());
    std::fs::write(&path, text)?;
    for old in newest_prints(data_root, usize::MAX).into_iter().skip(PRINTS_KEPT) {
        let _ = std::fs::remove_file(old);
    }
    Ok(path)
}

/// The newest `n` saved fingerprint comparisons, newest first.
pub fn newest_prints(data_root: &Path, n: usize) -> Vec<PathBuf> {
    let Ok(entries) = std::fs::read_dir(data_root.join(PRINTS_DIR)) else {
        return Vec::new();
    };
    let mut files: Vec<(SystemTime, PathBuf)> = entries
        .filter_map(|e| {
            let path = e.ok()?.path();
            let modified = std::fs::metadata(&path).ok()?.modified().ok()?;
            (path.extension()? == "json").then_some((modified, path))
        })
        .collect();
    files.sort_by(|a, b| b.0.cmp(&a.0).then_with(|| b.1.cmp(&a.1)));
    files.into_iter().take(n).map(|(_, p)| p).collect()
}

/// The current time as `YYYYMMDD-HHMMSS` in UTC, for file names.
pub fn file_stamp() -> String {
    let now = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default();
    crate::send_logs::utc_stamp(i64::try_from(now.as_secs()).unwrap_or(0))
}

pub fn secret(slot: &'static str, values: &[&str]) {
    let mut secrets = lock(&SECRETS);
    secrets.retain(|(s, _)| *s != slot);
    secrets.extend(
        values
            .iter()
            .filter(|v| v.len() >= SECRET_MIN)
            .map(|v| (slot, (*v).to_owned())),
    );
}

pub fn scrub(text: &str) -> String {
    let secrets = lock(&SECRETS);
    let mut out = text.to_owned();
    for (_, s) in secrets.iter() {
        if out.contains(s.as_str()) {
            out = out.replace(s.as_str(), REDACTED);
        }
    }
    out
}

pub fn line(text: impl Display) {
    let text = text.to_string();
    #[cfg(not(test))]
    if lock(&SINK).is_none() {
        return;
    }
    let mut body = scrub(&text);
    body.retain(|c| c != '\r');
    let body = body.replace('\n', " | ");
    let entry = format!("{} {body}\n", stamp());
    #[cfg(test)]
    for tap in lock(&TAPS).iter() {
        lock(tap).push_str(&entry);
    }
    let mut sink = lock(&SINK);
    let Some(sink) = sink.as_mut() else { return };
    sink.write(entry.as_bytes());
}

impl Sink {
    fn write(&mut self, data: &[u8]) {
        if self.file.is_none() {
            if let Some(dir) = self.path.parent() {
                let _ = std::fs::create_dir_all(dir);
            }
            self.file = OpenOptions::new().create(true).append(true).open(&self.path).ok();
            self.len = std::fs::metadata(&self.path).map_or(0, |m| m.len());
        }
        if self.len > 0 && self.len + data.len() as u64 > self.rotate_at {
            self.file = None;
            let _ = std::fs::rename(&self.path, &self.previous);
            self.file = File::create(&self.path).ok();
            self.len = 0;
        }
        if let Some(f) = self.file.as_mut()
            && f.write_all(data).is_ok()
        {
            self.len += data.len() as u64;
        }
    }
}

fn stamp() -> String {
    let now = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default();
    let secs = i64::try_from(now.as_secs()).unwrap_or(0);
    format!(
        "{}.{:03}Z",
        crate::send_logs::utc_stamp(secs),
        now.subsec_millis()
    )
}

pub fn digest_prefix(digest: &str) -> String {
    let hex = digest.strip_prefix("sha256:").unwrap_or(digest);
    hex.chars().take(12).collect()
}

#[cfg(test)]
pub struct Tap(std::sync::Arc<Mutex<String>>);

#[cfg(test)]
impl Tap {
    pub fn text(&self) -> String {
        lock(&self.0).clone()
    }
}

#[cfg(test)]
impl Drop for Tap {
    fn drop(&mut self) {
        lock(&TAPS).retain(|t| !std::sync::Arc::ptr_eq(t, &self.0));
    }
}

#[cfg(test)]
pub fn tap() -> Tap {
    let t = std::sync::Arc::new(Mutex::new(String::new()));
    lock(&TAPS).push(t.clone());
    Tap(t)
}

#[cfg(test)]
fn write_to(path: &Path, previous: &Path, rotate_at: u64, lines: &[&str]) {
    let mut sink = Sink {
        path: path.to_path_buf(),
        previous: previous.to_path_buf(),
        file: None,
        len: 0,
        rotate_at,
    };
    for l in lines {
        sink.write(format!("{} {}\n", stamp(), scrub(l)).as_bytes());
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn saved_comparisons_keep_the_newest_and_have_safe_names() {
        let root = std::env::temp_dir().join(format!("swofty-diag-prints-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        for i in 0..12 {
            write_prints(&root, &format!("F1-ROOM-{i:02}"), &serde_json::json!({ "i": i })).unwrap();
        }
        let kept = newest_prints(&root, usize::MAX);
        assert_eq!(kept.len(), PRINTS_KEPT);
        assert!(kept[0].ends_with("F1-ROOM-11.json"), "{kept:?}");
        assert!(!root.join(PRINTS_DIR).join("F1-ROOM-00.json").exists());
        assert_eq!(newest_prints(&root, 3).len(), 3);
        let odd = write_prints(&root, "a/b c", &serde_json::json!({})).unwrap();
        assert_eq!(odd.file_name().unwrap(), "a_b_c.json");
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn registered_secrets_never_reach_a_line() {
        let tap = tap();
        secret("test-slot", &["s3cr3t-token-value", "abc"]);
        line("connecting with s3cr3t-token-value and abc");
        secret("test-slot", &[]);
        let text = tap.text();
        assert!(!text.contains("s3cr3t-token-value"), "{text}");
        assert!(text.contains("connecting with [redacted] and abc"), "{text}");
    }

    #[test]
    fn rotates_past_the_limit() {
        let dir = std::env::temp_dir().join(format!("swofty-mp-diag-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        let (cur, prev) = (dir.join("multiplayer.log"), dir.join("multiplayer.1.log"));
        let long = "x".repeat(100);
        let lines: Vec<&str> = std::iter::repeat_n(long.as_str(), 30).collect();
        write_to(&cur, &prev, 1000, &lines);
        assert!(prev.is_file());
        assert!(std::fs::metadata(&cur).unwrap().len() <= 1000);
        assert!(std::fs::metadata(&prev).unwrap().len() <= 1000);
        let _ = std::fs::remove_dir_all(&dir);
    }
}
