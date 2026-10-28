use std::collections::HashMap;
use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::time::SystemTime;

use serde_json::{Value, json};

pub fn now_ms() -> u64 {
    SystemTime::now()
        .duration_since(SystemTime::UNIX_EPOCH)
        .map_or(0, |d| d.as_millis() as u64)
}

#[derive(Clone, Debug)]
pub struct Entry {
    pub seq: u64,
    pub i: usize,
    pub t: u64,
    pub label: String,
    pub op: String,
    pub args: Value,
    pub ok: bool,
    pub error: Option<String>,
    pub before: Value,
    pub after: Value,
    pub undo_of: Option<(u64, usize)>,
    pub career: Option<String>,
    pub mode: String,
}

impl Entry {
    fn to_json(&self) -> Value {
        json!({
            "seq": self.seq, "i": self.i, "t": self.t, "label": self.label, "op": self.op,
            "args": self.args, "ok": self.ok, "error": self.error, "before": self.before,
            "after": self.after, "undo_of": self.undo_of.map(|(s, i)| json!([s, i])),
            "career": self.career, "mode": self.mode,
        })
    }

    fn from_json(v: &Value) -> Option<Entry> {
        Some(Entry {
            seq: v["seq"].as_u64()?,
            i: v["i"].as_u64()? as usize,
            t: v["t"].as_u64().unwrap_or(0),
            label: v["label"].as_str().unwrap_or_default().to_owned(),
            op: v["op"].as_str()?.to_owned(),
            args: v["args"].clone(),
            ok: v["ok"].as_bool().unwrap_or(false),
            error: v["error"].as_str().map(str::to_owned),
            before: v["before"].clone(),
            after: v["after"].clone(),
            undo_of: v["undo_of"]
                .as_array()
                .and_then(|a| Some((a.first()?.as_u64()?, a.get(1)?.as_u64()? as usize))),
            career: v["career"].as_str().map(str::to_owned),
            mode: v["mode"].as_str().unwrap_or_default().to_owned(),
        })
    }
}

#[derive(Clone, Debug, Default)]
pub struct Sent {
    pub seq: u64,
    pub t: u64,
    pub label: String,
    pub undo_of: Option<(u64, usize)>,
    pub career: Option<String>,
    pub ops: Vec<Value>,
}

#[derive(Clone, Debug)]
pub struct Backup {
    pub name: String,
    pub path: PathBuf,
    pub created: u64,
    pub reason: String,
    pub files: u64,
    pub bytes: u64,
    pub career: Option<String>,
}

pub struct Pending {
    pub seq: u64,
    pub label: String,
    pub ops: usize,
}

pub struct Store {
    pub data_dir: PathBuf,
    pub mailbox: Option<PathBuf>,
    snap_key: Option<(SystemTime, u64)>,
    snap: Value,
    pub snap_error: Option<String>,
    pub history: Vec<Entry>,
    pub sent: HashMap<u64, Sent>,
    last_seq: u64,
    pub prefs: Value,
    outbox_key: Option<SystemTime>,
    viewer_at: u64,
}

const VIEWER_EVERY_MS: u64 = 3_000;

fn write_atomic(path: &Path, text: &str) -> Result<(), String> {
    if let Some(dir) = path.parent() {
        fs::create_dir_all(dir).map_err(|e| format!("{}: {e}", dir.display()))?;
    }
    let tmp = path.with_extension("json.tmp");
    fs::write(&tmp, text).map_err(|e| format!("{}: {e}", tmp.display()))?;
    fs::rename(&tmp, path).map_err(|e| format!("{}: {e}", path.display()))
}

fn append_line(path: &Path, v: &Value) {
    if let Some(dir) = path.parent() {
        fs::create_dir_all(dir).ok();
    }
    if let Ok(mut f) = fs::OpenOptions::new().create(true).append(true).open(path) {
        writeln!(f, "{v}").ok();
    }
}

fn read_lines(path: &Path) -> Vec<Value> {
    fs::read_to_string(path)
        .unwrap_or_default()
        .lines()
        .filter_map(|l| serde_json::from_str(l).ok())
        .collect()
}

fn seq_of(name: &str) -> Option<u64> {
    let stem = name.strip_suffix(".json")?;
    if stem.is_empty() || !stem.bytes().all(|b| b.is_ascii_digit()) {
        return None;
    }
    stem.parse().ok()
}

fn copy_tree(from: &Path, to: &Path, files: &mut u64, bytes: &mut u64) -> Result<(), String> {
    fs::create_dir_all(to).map_err(|e| format!("{}: {e}", to.display()))?;
    for e in fs::read_dir(from).map_err(|e| format!("{}: {e}", from.display()))? {
        let e = e.map_err(|e| e.to_string())?;
        let src = e.path();
        let dst = to.join(e.file_name());
        let kind = e.file_type().map_err(|e| e.to_string())?;
        if kind.is_dir() {
            copy_tree(&src, &dst, files, bytes)?;
        } else if kind.is_file() {
            *bytes += fs::copy(&src, &dst).map_err(|e| format!("{}: {e}", src.display()))?;
            *files += 1;
        }
    }
    Ok(())
}

impl Store {
    pub fn new(data_dir: PathBuf) -> Store {
        let mut s = Store {
            prefs: fs::read_to_string(data_dir.join("prefs.json"))
                .ok()
                .and_then(|t| serde_json::from_str(&t).ok())
                .unwrap_or_else(|| json!({})),
            data_dir,
            mailbox: None,
            snap_key: None,
            snap: Value::Null,
            snap_error: None,
            history: Vec::new(),
            sent: HashMap::new(),
            last_seq: 0,
            outbox_key: None,
            viewer_at: 0,
        };
        for v in read_lines(&s.data_dir.join("history.jsonl")) {
            if let Some(e) = Entry::from_json(&v) {
                s.last_seq = s.last_seq.max(e.seq);
                s.history.push(e);
            }
        }
        for v in read_lines(&s.data_dir.join("sent.jsonl")) {
            let Some(seq) = v["seq"].as_u64() else { continue };
            s.last_seq = s.last_seq.max(seq);
            s.sent.insert(
                seq,
                Sent {
                    seq,
                    t: v["t"].as_u64().unwrap_or(0),
                    label: v["label"].as_str().unwrap_or_default().to_owned(),
                    undo_of: v["undo_of"]
                        .as_array()
                        .and_then(|a| Some((a.first()?.as_u64()?, a.get(1)?.as_u64()? as usize))),
                    career: v["career"].as_str().map(str::to_owned),
                    ops: v["ops"].as_array().cloned().unwrap_or_default(),
                },
            );
        }
        s
    }

    pub fn save_prefs(&self) {
        write_atomic(&self.data_dir.join("prefs.json"), &self.prefs.to_string()).ok();
    }

    pub fn pref_str(&self, key: &str, default: &str) -> String {
        self.prefs[key].as_str().unwrap_or(default).to_owned()
    }

    pub fn set_pref(&mut self, key: &str, value: Value) {
        if !self.prefs.is_object() {
            self.prefs = json!({});
        }
        self.prefs[key] = value;
        self.save_prefs();
    }

    pub fn snapshot(&mut self) -> &Value {
        let Some(dir) = &self.mailbox else {
            self.snap = Value::Null;
            return &self.snap;
        };
        let path = dir.join("snapshot.json");
        let key = fs::metadata(&path)
            .ok()
            .and_then(|m| Some((m.modified().ok()?, m.len())));
        if key.is_none() {
            self.snap = Value::Null;
            self.snap_key = None;
        } else if key != self.snap_key {
            match fs::read(&path) {
                Ok(bytes) => match serde_json::from_str::<Value>(&String::from_utf8_lossy(&bytes)) {
                    Ok(v) => {
                        self.snap = v;
                        self.snap_error = None;
                    }
                    Err(e) => self.snap_error = Some(format!("{e}, {} bytes", bytes.len())),
                },
                Err(e) => self.snap_error = Some(e.to_string()),
            }
            self.snap_key = key;
        }
        &self.snap
    }

    /// Point the mailbox at `dir`, the folder the launcher names for this
    /// addon (in the game copy, or in its data folder when the game can't
    /// write there), and forget what was read from the one before.
    pub fn set_mailbox(&mut self, dir: PathBuf) {
        if self.mailbox.as_ref() == Some(&dir) {
            return;
        }
        self.mailbox = Some(dir);
        self.snap = Value::Null;
        self.snap_key = None;
        self.snap_error = None;
        self.outbox_key = None;
        self.viewer_at = 0;
    }

    /// Forget the mailbox, when the launcher can't name one: a stale folder
    /// must not be written to.
    pub fn clear_mailbox(&mut self) {
        if self.mailbox.is_some() {
            self.mailbox = None;
            self.snap = Value::Null;
            self.snap_key = None;
            self.snap_error = None;
            self.outbox_key = None;
            self.viewer_at = 0;
        }
    }

    pub fn touch_viewer(&mut self) {
        let Some(dir) = &self.mailbox else { return };
        let now = now_ms();
        if now.saturating_sub(self.viewer_at) < VIEWER_EVERY_MS {
            return;
        }
        if write_atomic(&dir.join("viewer.json"), &now.to_string()).is_ok() {
            self.viewer_at = now;
        }
    }

    pub fn snapshot_changed(&mut self) -> bool {
        let before = self.snap_key;
        self.snapshot();
        before != self.snap_key
    }

    pub fn outbox_changed(&mut self) -> bool {
        let Some(dir) = &self.mailbox else { return false };
        let key = fs::metadata(dir.join("outbox"))
            .ok()
            .and_then(|m| m.modified().ok());
        let changed = key != self.outbox_key;
        self.outbox_key = key;
        changed
    }

    pub fn collect_acks(&mut self) -> usize {
        let Some(dir) = self.mailbox.clone() else { return 0 };
        let mut seqs: Vec<u64> = fs::read_dir(dir.join("outbox"))
            .into_iter()
            .flatten()
            .filter_map(|e| seq_of(e.ok()?.file_name().to_str()?))
            .collect();
        seqs.sort_unstable();
        let mut n = 0;
        for seq in seqs {
            let path = dir.join("outbox").join(format!("{seq}.json"));
            let Some(ack) = fs::read_to_string(&path)
                .ok()
                .and_then(|t| serde_json::from_str::<Value>(&t).ok())
            else {
                continue;
            };
            let sent = self.sent.get(&seq).cloned().unwrap_or_default();
            let t = ack["t"].as_u64().unwrap_or_else(now_ms);
            let mode = ack["mode"].as_str().unwrap_or_default().to_owned();
            let results = ack["results"].as_array().cloned().unwrap_or_default();
            if results.is_empty() {
                let e = Entry {
                    seq,
                    i: 0,
                    t,
                    label: sent.label.clone(),
                    op: "batch".into(),
                    args: Value::Null,
                    ok: false,
                    error: Some(ack["error"].as_str().unwrap_or("rejected").to_owned()),
                    before: Value::Null,
                    after: Value::Null,
                    undo_of: None,
                    career: sent.career.clone(),
                    mode: mode.clone(),
                };
                append_line(&self.data_dir.join("history.jsonl"), &e.to_json());
                self.history.push(e);
            }
            for r in results {
                let i = r["i"].as_u64().unwrap_or(0) as usize;
                let args = if r["args"].is_null() {
                    sent.ops.get(i).map(|o| o["args"].clone()).unwrap_or(Value::Null)
                } else {
                    r["args"].clone()
                };
                let e = Entry {
                    seq,
                    i,
                    t,
                    label: sent.label.clone(),
                    op: r["op"].as_str().unwrap_or_default().to_owned(),
                    args,
                    ok: r["ok"].as_bool().unwrap_or(false),
                    error: r["error"].as_str().map(str::to_owned),
                    before: r["before"].clone(),
                    after: r["after"].clone(),
                    undo_of: sent.undo_of,
                    career: sent.career.clone(),
                    mode: mode.clone(),
                };
                append_line(&self.data_dir.join("history.jsonl"), &e.to_json());
                self.history.push(e);
            }
            fs::remove_file(&path).ok();
            n += 1;
        }
        n
    }

    pub fn pending(&self) -> Vec<Pending> {
        let Some(dir) = &self.mailbox else {
            return Vec::new();
        };
        let mut out: Vec<Pending> = fs::read_dir(dir.join("inbox"))
            .into_iter()
            .flatten()
            .filter_map(|e| {
                let e = e.ok()?;
                let seq = seq_of(e.file_name().to_str()?)?;
                let v: Value =
                    serde_json::from_str(&fs::read_to_string(e.path()).ok()?).unwrap_or(Value::Null);
                Some(Pending {
                    seq,
                    label: v["label"].as_str().unwrap_or("Edit").to_owned(),
                    ops: v["ops"].as_array().map_or(0, Vec::len),
                })
            })
            .collect();
        out.sort_by_key(|p| p.seq);
        out
    }

    pub fn withdraw_all(&self) -> usize {
        self.pending()
            .iter()
            .filter(|p| {
                self.mailbox
                    .as_ref()
                    .is_some_and(|d| fs::remove_file(d.join("inbox").join(format!("{}.json", p.seq))).is_ok())
            })
            .count()
    }

    pub fn send(
        &mut self,
        label: &str,
        ops: Vec<Value>,
        undo_of: Option<(u64, usize)>,
        career: Option<String>,
    ) -> Result<u64, String> {
        let dir = self.mailbox.clone().ok_or("the game folder is not known yet")?;
        let seq = (self.last_seq + 1).max(now_ms());
        self.last_seq = seq;
        let batch = json!({ "seq": seq, "session": "launcher", "label": label, "ops": ops });
        let inbox = dir.join("inbox");
        fs::create_dir_all(&inbox).map_err(|e| format!("{}: {e}", inbox.display()))?;
        fs::create_dir_all(dir.join("outbox")).ok();
        write_atomic(&inbox.join(format!("{seq}.json")), &batch.to_string())?;
        let sent = Sent {
            seq,
            t: now_ms(),
            label: label.to_owned(),
            undo_of,
            career,
            ops,
        };
        append_line(
            &self.data_dir.join("sent.jsonl"),
            &json!({ "seq": seq, "t": sent.t, "label": sent.label, "undo_of": undo_of.map(|(s, i)| json!([s, i])),
                     "career": sent.career, "ops": sent.ops }),
        );
        self.sent.insert(seq, sent);
        Ok(seq)
    }

    pub fn backups_dir(&self) -> PathBuf {
        self.data_dir.join("backups")
    }

    pub fn backups(&self) -> Vec<Backup> {
        let mut out: Vec<Backup> = fs::read_dir(self.backups_dir())
            .into_iter()
            .flatten()
            .filter_map(|e| {
                let e = e.ok()?;
                if !e.file_type().ok()?.is_dir() {
                    return None;
                }
                let info: Value = fs::read_to_string(e.path().join("backup.json"))
                    .ok()
                    .and_then(|t| serde_json::from_str(&t).ok())?;
                Some(Backup {
                    name: e.file_name().to_string_lossy().into_owned(),
                    path: e.path(),
                    created: info["created"].as_u64().unwrap_or(0),
                    reason: info["reason"].as_str().unwrap_or_default().to_owned(),
                    files: info["files"].as_u64().unwrap_or(0),
                    bytes: info["bytes"].as_u64().unwrap_or(0),
                    career: info["career"].as_str().map(str::to_owned),
                })
            })
            .collect();
        out.sort_by_key(|b| std::cmp::Reverse(b.created));
        out
    }

    pub fn backup(
        &self,
        steam: &Path,
        reason: &str,
        career: Option<&str>,
        session: Option<&str>,
    ) -> Result<Backup, String> {
        if !steam.is_dir() {
            return Err(format!(
                "the Steam save folder {} does not exist",
                steam.display()
            ));
        }
        let stamp = chrono::Local::now().format("%Y-%m-%d_%H-%M-%S").to_string();
        let mut name = stamp.clone();
        let mut n = 2;
        while self.backups_dir().join(&name).exists() {
            name = format!("{stamp}-{n}");
            n += 1;
        }
        let dest = self.backups_dir().join(&name);
        let files_dir = dest.join("remote");
        let (mut files, mut bytes) = (0, 0);
        if let Err(e) = copy_tree(steam, &files_dir, &mut files, &mut bytes) {
            fs::remove_dir_all(&dest).ok();
            return Err(e);
        }
        let created = now_ms();
        let info = json!({ "created": created, "reason": reason, "source": steam, "files": files, "bytes": bytes,
                           "career": career, "game_session": session });
        write_atomic(
            &dest.join("backup.json"),
            &serde_json::to_string_pretty(&info).unwrap_or_default(),
        )?;
        Ok(Backup {
            name,
            path: dest,
            created,
            reason: reason.to_owned(),
            files,
            bytes,
            career: career.map(str::to_owned),
        })
    }

    pub fn restore(&self, name: &str, steam: &Path) -> Result<u64, String> {
        let src = self.backups_dir().join(name).join("remote");
        if !src.is_dir() {
            return Err(format!("backup {name} has no files"));
        }
        let (mut files, mut bytes) = (0, 0);
        copy_tree(&src, steam, &mut files, &mut bytes)?;
        Ok(files)
    }

    pub fn delete_backup(&self, name: &str) -> Result<(), String> {
        if name.is_empty() || name.contains(['/', '\\']) || name.starts_with('.') {
            return Err("bad backup name".into());
        }
        fs::remove_dir_all(self.backups_dir().join(name)).map_err(|e| e.to_string())
    }

    pub fn session_marker(&self) -> Option<String> {
        fs::read_to_string(self.backups_dir().join("session.json"))
            .ok()
            .and_then(|t| serde_json::from_str::<Value>(&t).ok())
            .and_then(|v| v["key"].as_str().map(str::to_owned))
    }

    pub fn set_session_marker(&self, key: &str) {
        write_atomic(
            &self.backups_dir().join("session.json"),
            &json!({ "key": key, "t": now_ms() }).to_string(),
        )
        .ok();
    }

    pub fn undone(&self, seq: u64, i: usize) -> bool {
        self.history.iter().any(|e| e.ok && e.undo_of == Some((seq, i)))
    }
}
