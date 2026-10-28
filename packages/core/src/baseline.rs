use std::collections::BTreeMap;
use std::io::Read;
use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};

use crate::{Error, Result, io_err};

pub fn hex(bytes: &[u8]) -> String {
    const DIGITS: &[u8; 16] = b"0123456789abcdef";
    let mut out = String::with_capacity(bytes.len() * 2);
    for &b in bytes {
        out.push(DIGITS[(b >> 4) as usize] as char);
        out.push(DIGITS[(b & 15) as usize] as char);
    }
    out
}

pub fn sha256_bytes(data: &[u8]) -> String {
    hex(&Sha256::digest(data))
}

pub fn sha256_file(path: &Path) -> Result<String> {
    crate::worked();
    let mut f = std::fs::File::open(path).map_err(io_err(path))?;
    let mut h = Sha256::new();
    let mut buf = [0; 1 << 16];
    loop {
        match f.read(&mut buf) {
            Ok(0) => break,
            Ok(n) => h.update(&buf[..n]),
            Err(e) if e.kind() == std::io::ErrorKind::Interrupted => {}
            Err(e) => return Err(io_err(path)(e)),
        }
    }
    Ok(hex(&h.finalize()))
}

pub fn sha256_opt(path: &Path) -> Result<Option<String>> {
    if path.is_file() {
        sha256_file(path).map(Some)
    } else {
        Ok(None)
    }
}

pub const HASHES_FILE: &str = "state/hashes.json";

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
struct Stamp {
    len: u64,
    modified: u128,
    changed: i128,
    ino: u64,
}

impl Stamp {
    fn of(path: &Path) -> Option<Self> {
        let meta = std::fs::metadata(path).ok().filter(std::fs::Metadata::is_file)?;
        let modified = meta
            .modified()
            .ok()?
            .duration_since(std::time::UNIX_EPOCH)
            .ok()?
            .as_nanos();
        #[cfg(unix)]
        let (changed, ino) = {
            use std::os::unix::fs::MetadataExt;
            (
                i128::from(meta.ctime()) * 1_000_000_000 + i128::from(meta.ctime_nsec()),
                meta.ino(),
            )
        };
        #[cfg(not(unix))]
        let (changed, ino) = (
            meta.created()
                .ok()
                .and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok())
                .map_or(0, |d| d.as_nanos() as i128),
            0,
        );
        Some(Self {
            len: meta.len(),
            modified,
            changed,
            ino,
        })
    }
}

/// SHA-256 digests of files the launcher reads on every launch, kept with the
/// size, times and inode they had when hashed. A file whose metadata still
/// matches is not read again; any write, rename or copy over it changes the
/// metadata, so its next lookup hashes it afresh.
#[derive(Debug, Default)]
pub struct Hashes {
    path: PathBuf,
    files: BTreeMap<PathBuf, (Stamp, String)>,
    dirty: bool,
}

impl Hashes {
    pub fn load(data: &Path) -> Self {
        Self::open(data.join(HASHES_FILE))
    }

    /// The digests kept in `path` rather than [`HASHES_FILE`].
    pub fn open(path: PathBuf) -> Self {
        let files = crate::read_json(&path).unwrap_or_default();
        Self {
            path,
            files,
            dirty: false,
        }
    }

    pub fn sha256(&mut self, path: &Path) -> Result<String> {
        let before = Stamp::of(path);
        if let (Some(now), Some((was, sha))) = (before, self.files.get(path))
            && now == *was
        {
            return Ok(sha.clone());
        }
        let sha = sha256_file(path)?;
        if let Some(stamp) = before.filter(|b| Stamp::of(path) == Some(*b)) {
            self.files.insert(path.to_path_buf(), (stamp, sha.clone()));
            self.dirty = true;
        }
        Ok(sha)
    }

    pub fn sha256_opt(&mut self, path: &Path) -> Result<Option<String>> {
        if path.is_file() {
            self.sha256(path).map(Some)
        } else {
            Ok(None)
        }
    }

    /// Record the digest of bytes the caller just wrote to `path`.
    pub fn wrote(&mut self, path: &Path, sha: &str) {
        match Stamp::of(path) {
            Some(stamp) => self.files.insert(path.to_path_buf(), (stamp, sha.to_owned())),
            None => self.files.remove(path),
        };
        self.dirty = true;
    }

    pub fn save(&mut self) {
        if !self.dirty {
            return;
        }
        self.files.retain(|p, (stamp, _)| Stamp::of(p) == Some(*stamp));
        if crate::write_json(&self.path, &self.files).is_ok() {
            self.dirty = false;
        }
    }
}

#[derive(Debug, Clone)]
pub struct Objects {
    pub root: PathBuf,
}

impl Objects {
    pub fn path(&self, sha: &str) -> PathBuf {
        self.root.join(&sha[..2]).join(sha)
    }

    pub fn has(&self, sha: &str) -> bool {
        self.path(sha).is_file()
    }

    /// Back up `src` under its SHA-256. The copy is flushed to disk before it
    /// takes its name, since it is the only copy of an original once the game
    /// file is replaced; an object a crash left damaged is written again.
    pub fn put_file(&self, src: &Path) -> Result<String> {
        let sha = sha256_file(src)?;
        let dst = self.path(&sha);
        if !dst.is_file() || sha256_file(&dst)? != sha {
            let dir = dst.parent().expect("has parent");
            std::fs::create_dir_all(dir).map_err(io_err(dir))?;
            let tmp = dst.with_extension("tmp");
            std::fs::copy(src, &tmp).map_err(io_err(&tmp))?;
            crate::sync_file(&tmp)?;
            if sha256_file(&tmp)? != sha {
                return Err(Error::Other(format!(
                    "{} changed while it was backed up",
                    src.display()
                )));
            }
            std::fs::rename(&tmp, &dst).map_err(io_err(&dst))?;
            crate::sync_dir(Some(dir));
        }
        Ok(sha)
    }

    pub fn restore_to(&self, sha: &str, dst: &Path) -> Result<()> {
        let src = self.path(sha);
        if !src.is_file() {
            return Err(Error::Other(format!(
                "backup {sha} for {} is missing",
                dst.display()
            )));
        }
        if let Some(dir) = dst.parent() {
            std::fs::create_dir_all(dir).map_err(io_err(dir))?;
        }
        let tmp = dst.with_file_name(format!(
            ".{}.swofty-restore",
            dst.file_name().unwrap_or_default().to_string_lossy()
        ));
        std::fs::copy(&src, &tmp).map_err(io_err(&tmp))?;
        crate::sync_file(&tmp)?;
        if sha256_file(&tmp)? != sha {
            std::fs::remove_file(&tmp).ok();
            return Err(Error::Other(format!(
                "the backup of {} is damaged; verify the game files in Steam",
                dst.display()
            )));
        }
        let renamed = match std::fs::rename(&tmp, dst) {
            Err(e) if e.kind() == std::io::ErrorKind::PermissionDenied && crate::clear_readonly(dst) => {
                std::fs::rename(&tmp, dst)
            }
            r => r,
        };
        if let Err(e) = renamed {
            std::fs::remove_file(&tmp).ok();
            return Err(io_err(dst)(e));
        }
        crate::sync_dir(dst.parent());
        Ok(())
    }
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "kebab-case")]
pub enum Source {
    Depot,
    FirstSight,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct FileRecord {
    pub size: u64,
    pub sha256: String,
    pub source: Source,
}

pub const EXE_HASHES_FILE: &str = "state/exe_hash.json";

/// `state/exe_hash.json`: each game exe's digest by path, with the
/// `path|size|mtime` stamp it had when hashed. Up to 1.3.0 the file held a
/// single `[stamp, digest]` pair.
#[derive(Deserialize)]
#[serde(untagged)]
enum ExeHashes {
    Each(BTreeMap<String, (String, String)>),
    One(String, String),
}

impl ExeHashes {
    fn read(path: &Path) -> BTreeMap<String, (String, String)> {
        match crate::read_json(path) {
            Ok(Self::Each(each)) => each,
            Ok(Self::One(stamp, sha)) => stamp
                .rsplitn(3, '|')
                .nth(2)
                .map(|exe| BTreeMap::from([(exe.to_owned(), (stamp.clone(), sha))]))
                .unwrap_or_default(),
            Err(_) => BTreeMap::new(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Identity {
    pub build_id: String,
    pub exe_sha256: String,
}

impl Identity {
    pub fn detect(game_dir: &Path, build_id: Option<u64>) -> Result<Self> {
        let exe = sha256_file(&game_dir.join(crate::GAME_EXE))?;
        Ok(Self {
            build_id: build_id.map_or_else(|| format!("exe-{}", &exe[..12]), |b| b.to_string()),
            exe_sha256: exe,
        })
    }

    pub fn detect_cached(data: &Path, game_dir: &Path, build_id: Option<u64>) -> Result<Self> {
        Self::detect_cached_with(data, game_dir, build_id, sha256_file)
    }

    fn detect_cached_with(
        data: &Path,
        game_dir: &Path,
        build_id: Option<u64>,
        hash: impl FnOnce(&Path) -> Result<String>,
    ) -> Result<Self> {
        let exe = game_dir.join(crate::GAME_EXE);
        let meta = std::fs::metadata(&exe).map_err(io_err(&exe))?;
        let mtime = meta
            .modified()
            .ok()
            .and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok())
            .map_or(0, |d| d.as_secs());
        let key = exe.display().to_string();
        let stamp = format!("{key}|{}|{mtime}", meta.len());
        let cache = data.join(EXE_HASHES_FILE);
        let mut known = ExeHashes::read(&cache);
        let sha = match known.get(&key) {
            Some((s, sha)) if *s == stamp => sha.clone(),
            _ => {
                let sha = hash(&exe)?;
                known.retain(|k, _| Path::new(k).is_file());
                known.insert(key, (stamp, sha.clone()));
                crate::write_json_shared(&cache, &known)?;
                sha
            }
        };
        Ok(Self {
            build_id: build_id.map_or_else(|| format!("exe-{}", &sha[..12]), |b| b.to_string()),
            exe_sha256: sha,
        })
    }

    pub fn key(&self) -> String {
        format!("{}-{}", self.build_id, &self.exe_sha256[..12])
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Baseline {
    pub schema_version: u32,
    pub build_id: String,
    pub exe_sha256: String,
    #[serde(default)]
    pub files: BTreeMap<String, FileRecord>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub rebuild: Option<Rebuild>,
    /// Every file Steam installed had its size when this build was last
    /// checked against Steam's manifests (see [`crate::stock`]).
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub stock_checked: bool,
}

/// Set on a baseline started over because the previous one was damaged.
/// Until the player has been told to verify the game files, a game file is
/// only recorded as an original when its bytes are already a backed-up
/// original in the object store.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Rebuild {
    pub damaged: String,
    pub at: u64,
    #[serde(default)]
    pub told_to_verify: bool,
}

impl Rebuild {
    pub fn checking(&self) -> bool {
        !self.told_to_verify
    }
}

pub const DAMAGED: &str = "the launcher's record of your original game files is damaged; verify the game files in Steam, then press Play again";

/// Steam's record of the game's files: `Some(true)` when the file at `path`
/// is Steam's copy of `rel`, `Some(false)` when it differs, `None` when
/// Steam's list is unknown or doesn't hold `rel`.
pub trait DepotHashes {
    fn stock(&self, rel: &str, path: &Path) -> Result<Option<bool>>;
}

impl DepotHashes for () {
    fn stock(&self, _: &str, _: &Path) -> Result<Option<bool>> {
        Ok(None)
    }
}

impl Baseline {
    pub fn new(id: &Identity) -> Self {
        Self {
            schema_version: 1,
            build_id: id.build_id.clone(),
            exe_sha256: id.exe_sha256.clone(),
            files: BTreeMap::new(),
            rebuild: None,
            stock_checked: false,
        }
    }

    pub fn path(data: &Path, id: &Identity) -> PathBuf {
        data.join("baseline").join(id.key()).join("baseline.json")
    }

    /// The baseline for `id`. A damaged one is moved aside and started over
    /// in rebuild mode (see [`Rebuild`]), which is saved at once so every
    /// later load sees it; a missing one next to a damaged copy is rebuilt
    /// the same way.
    pub fn load_or_new(data: &Path, id: &Identity) -> Result<Self> {
        let p = Self::path(data, id);
        match crate::load_json(&p)? {
            crate::Loaded::Found(b) => Ok(b),
            crate::Loaded::Missing => Ok(match damaged_copy(&p) {
                Some(name) => Self::rebuilding(id, name),
                None => Self::new(id),
            }),
            crate::Loaded::Damaged(moved) => {
                let name = moved
                    .file_name()
                    .unwrap_or_default()
                    .to_string_lossy()
                    .into_owned();
                eprintln!(
                    "swofty: warning: the record of the original game files for build {} was damaged; \
                     rebuilding it from game files that match backed-up originals",
                    id.build_id
                );
                let b = Self::rebuilding(id, name);
                b.save(data)?;
                Ok(b)
            }
        }
    }

    fn rebuilding(id: &Identity, damaged: String) -> Self {
        let at = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map_or(0, |d| d.as_secs());
        Self {
            rebuild: Some(Rebuild {
                damaged,
                at,
                told_to_verify: false,
            }),
            ..Self::new(id)
        }
    }

    pub fn checking(&self) -> bool {
        self.rebuild.as_ref().is_some_and(Rebuild::checking)
    }

    pub fn save(&self, data: &Path) -> Result<()> {
        let id = Identity {
            build_id: self.build_id.clone(),
            exe_sha256: self.exe_sha256.clone(),
        };
        crate::write_json(&Self::path(data, &id), self)
    }

    pub fn record(
        &mut self,
        objects: &Objects,
        game: &Path,
        rel: &str,
        depot: &dyn DepotHashes,
    ) -> Result<&FileRecord> {
        if !self.files.contains_key(rel) {
            let path = game.join(rel);
            let size = std::fs::metadata(&path).map_err(io_err(&path))?.len();
            let source = match depot.stock(rel, &path)? {
                Some(true) => Source::Depot,
                Some(false) => return Err(Error::NotStock(vec![rel.to_owned()])),
                None => Source::FirstSight,
            };
            let sha = objects.put_file(&path)?;
            self.files.insert(
                rel.to_owned(),
                FileRecord {
                    size,
                    sha256: sha,
                    source,
                },
            );
        }
        Ok(&self.files[rel])
    }
}

fn damaged_copy(path: &Path) -> Option<String> {
    let name = format!("{}{}", path.file_name()?.to_string_lossy(), crate::DAMAGED_SUFFIX);
    std::fs::read_dir(path.parent()?)
        .ok()?
        .flatten()
        .map(|e| e.file_name().to_string_lossy().into_owned())
        .filter(|n| n.starts_with(&name))
        .max()
}

pub const LANG_PREFIX: &str = "localisation/2025_release/language_";

pub fn lang_dir(data: &Path, id: &Identity) -> PathBuf {
    data.join("baseline").join(id.key()).join("lang")
}

pub fn original_lang(data: &Path, game: &Path, id: &Identity, code: &str) -> Result<PathBuf> {
    let dir = lang_dir(data, id);
    if !dir.is_dir() {
        extract_lang(game, &dir)?;
    }
    let path = dir.join(format!("language_{code}.lng"));
    if path.is_file() {
        Ok(path)
    } else {
        Err(Error::Other(format!("the game has no {code} language file")))
    }
}

pub fn extract_lang(game: &Path, dir: &Path) -> Result<Vec<String>> {
    let exe_path = game.join(crate::GAME_EXE);
    let mut exe = std::fs::File::open(&exe_path).map_err(io_err(&exe_path))?;
    let archive = swofty_formats::nefs::Archive::open_exe(&mut exe).map_err(crate::formats_at(&exe_path))?;
    let data_path = game.join(&archive.volume);
    let mut data = std::fs::File::open(&data_path).map_err(io_err(&data_path))?;
    let tmp = dir.with_extension(format!("tmp-{}", std::process::id()));
    if tmp.exists() {
        std::fs::remove_dir_all(&tmp).map_err(io_err(&tmp))?;
    }
    std::fs::create_dir_all(&tmp).map_err(io_err(&tmp))?;
    let mut written = Vec::new();
    for entry in &archive.entries {
        let Some(name) = entry
            .path
            .strip_prefix(LANG_PREFIX)
            .filter(|n| n.ends_with(".lng") && !n.contains('/'))
        else {
            continue;
        };
        let bytes = archive
            .read(entry, &mut data)
            .map_err(crate::formats_at(&data_path))?;
        if !swofty_formats::lng::is_lng(&bytes) {
            std::fs::remove_dir_all(&tmp).ok();
            return Err(Error::Other(format!(
                "{} in {} is not a language file",
                entry.path,
                crate::GAME_EXE
            )));
        }
        let name = format!("language_{name}");
        let path = tmp.join(&name);
        std::fs::write(&path, bytes).map_err(io_err(&path))?;
        written.push(name);
    }
    if written.is_empty() {
        std::fs::remove_dir_all(&tmp).ok();
        return Err(Error::Other(format!(
            "{} holds no language files",
            crate::GAME_EXE
        )));
    }
    written.sort();
    if let Err(e) = std::fs::rename(&tmp, dir) {
        std::fs::remove_dir_all(&tmp).ok();
        if !dir.is_dir() {
            return Err(io_err(dir)(e));
        }
    }
    Ok(written)
}

pub fn exe_version(game: &Path) -> Result<Option<String>> {
    let path = game.join(crate::GAME_EXE);
    let mut exe = std::fs::File::open(&path).map_err(io_err(&path))?;
    Ok(swofty_formats::pe::file_version(&mut exe)?)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_cached_digest_holds_until_the_file_is_written_again() {
        let data = std::env::temp_dir().join(format!("swofty-hashes-{}", std::process::id()));
        std::fs::remove_dir_all(&data).ok();
        std::fs::create_dir_all(&data).unwrap();
        let file = data.join("f.bin");
        std::fs::write(&file, "one").unwrap();
        let mut hashes = Hashes::load(&data);
        assert_eq!(hashes.sha256(&file).unwrap(), sha256_bytes(b"one"));
        hashes.save();

        let mut hashes = Hashes::load(&data);
        let (stamp, _) = hashes.files[&file];
        hashes.files.insert(file.clone(), (stamp, "remembered".into()));
        assert_eq!(hashes.sha256(&file).unwrap(), "remembered");

        std::fs::write(&file, "two").unwrap();
        assert_eq!(hashes.sha256(&file).unwrap(), sha256_bytes(b"two"));
        let moved = data.join("moved.bin");
        std::fs::write(&moved, "one").unwrap();
        std::fs::rename(&moved, &file).unwrap();
        assert_eq!(hashes.sha256(&file).unwrap(), sha256_bytes(b"one"));

        hashes.wrote(&file, &sha256_bytes(b"one"));
        std::fs::remove_file(&file).unwrap();
        assert_eq!(hashes.sha256_opt(&file).unwrap(), None);
        hashes.save();
        assert!(Hashes::load(&data).files.is_empty());
        std::fs::remove_dir_all(&data).ok();
    }

    #[test]
    fn switching_copies_hashes_each_exe_once() {
        let root = std::env::temp_dir().join(format!("swofty-exe-hashes-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let (data, steam, ea) = (root.join("data"), root.join("steam"), root.join("ea"));
        for (dir, bytes) in [(&steam, &b"MZ steam"[..]), (&ea, &b"MZ ea"[..])] {
            std::fs::create_dir_all(dir).unwrap();
            std::fs::write(dir.join(crate::GAME_EXE), bytes).unwrap();
        }
        let hashed = std::cell::RefCell::new(Vec::new());
        let detect = |dir: &Path| {
            Identity::detect_cached_with(&data, dir, None, |exe| {
                hashed.borrow_mut().push(exe.to_path_buf());
                sha256_file(exe)
            })
            .unwrap()
        };
        let keys: Vec<String> = [&steam, &ea, &steam, &ea, &steam].map(|d| detect(d).key()).into();
        assert_eq!(
            *hashed.borrow(),
            [steam.join(crate::GAME_EXE), ea.join(crate::GAME_EXE)]
        );
        assert_eq!(keys[0], format!("exe-{0}-{0}", &sha256_bytes(b"MZ steam")[..12]));
        assert_eq!((&keys[0], &keys[1]), (&keys[2], &keys[3]));
        assert_ne!(keys[0], keys[1]);

        std::fs::write(ea.join(crate::GAME_EXE), b"MZ ea, updated").unwrap();
        assert_eq!(detect(&ea).exe_sha256, sha256_bytes(b"MZ ea, updated"));
        assert_eq!(detect(&steam).exe_sha256, sha256_bytes(b"MZ steam"));
        assert_eq!(hashed.borrow().len(), 3);
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn keeps_the_single_exe_digest_an_older_launcher_saved() {
        let root = std::env::temp_dir().join(format!("swofty-exe-hash-v1-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let (data, game) = (root.join("data"), root.join("game"));
        std::fs::create_dir_all(&game).unwrap();
        let exe = game.join(crate::GAME_EXE);
        std::fs::write(&exe, b"MZ").unwrap();
        let mtime = std::fs::metadata(&exe)
            .unwrap()
            .modified()
            .unwrap()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_secs();
        let stamp = format!("{}|2|{mtime}", exe.display());
        crate::write_json(&data.join(EXE_HASHES_FILE), &(stamp, "a".repeat(64))).unwrap();
        let id = Identity::detect_cached_with(&data, &game, Some(7), |_| panic!("hashed again")).unwrap();
        assert_eq!(id.exe_sha256, "a".repeat(64));
        let saved = ExeHashes::read(&data.join(EXE_HASHES_FILE));
        assert_eq!(saved.keys().collect::<Vec<_>>(), [&exe.display().to_string()]);
        std::fs::remove_dir_all(&root).ok();
    }
}
