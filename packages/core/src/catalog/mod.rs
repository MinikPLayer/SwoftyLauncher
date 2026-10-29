//! The addon catalog the Addon Browser shows: the addons published to the
//! Swofty registry, fetched and saved so the last catalog shows offline, and
//! downloads checked against the size and SHA-256 the registry lists.

pub mod publishers;
pub mod requires;
pub mod search;
pub mod stars;

use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use std::sync::Mutex;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use semver::VersionReq;
use serde::{Deserialize, Deserializer, Serialize};
use sha2::{Digest, Sha256};

use crate::manifest::{AddonManifest, Category, Requirement};
use crate::store::Store;
use crate::{Error, Result, io_err};

pub const URL_ENV: &str = "SWOFTY_REGISTRY_URL";
pub const DEFAULT_URL: &str = "https://api.f1.swofty.net";
pub const CACHE_DIR: &str = "cache/registry";
pub const CATALOG_FILE: &str = "catalog.json";
pub const LOG_FILE: &str = "state/registry.log";
pub const TIMEOUT: Duration = Duration::from_secs(10);
pub const DOWNLOAD_LIMIT: u64 = 4 * 1024 * 1024 * 1024;
const CATALOG_LIMIT: u64 = 16 * 1024 * 1024;
/// The least time the catalog's body gets.
const CATALOG_FLOOR: Duration = Duration::from_secs(60);
/// The least time a download's body gets.
const DOWNLOAD_FLOOR: Duration = Duration::from_secs(30 * 60);
/// The slowest average a download may run at, in bytes a second (1 Mbit/s).
const DOWNLOAD_RATE: u64 = 128 * 1024;
const LOG_LIMIT: u64 = 512 * 1024;

static INSTALLING: Mutex<()> = Mutex::new(());

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Publisher {
    pub id: String,
    pub name: String,
    #[serde(default)]
    pub avatar: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Addon {
    pub id: String,
    pub name: String,
    #[serde(default)]
    pub author: String,
    #[serde(default)]
    pub summary: String,
    #[serde(default)]
    pub description: String,
    #[serde(default)]
    pub category: Option<String>,
    #[serde(default)]
    pub kind: Vec<String>,
    #[serde(default)]
    pub homepage: Option<String>,
    #[serde(default)]
    pub icon: Option<String>,
    #[serde(default)]
    pub publisher: Option<Publisher>,
    #[serde(default)]
    pub publishers: Vec<Publisher>,
    #[serde(default)]
    pub downloads: u64,
    #[serde(default)]
    pub downloads_30d: u64,
    #[serde(default)]
    pub stars: u64,
    #[serde(default)]
    pub trend: Option<Trend>,
    #[serde(default)]
    pub created_at: String,
    #[serde(default)]
    pub updated_at: String,
    #[serde(default)]
    pub latest: Option<Version>,
    #[serde(default)]
    pub versions: Vec<Version>,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq)]
pub struct Trend {
    #[serde(default)]
    pub days: Vec<String>,
    #[serde(default)]
    pub downloads: Vec<u64>,
    #[serde(default)]
    pub stars: Vec<i64>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Version {
    pub version: semver::Version,
    #[serde(default)]
    pub launcher: Option<VersionReq>,
    #[serde(default)]
    pub game: String,
    pub size: u64,
    pub sha256: String,
    pub url: String,
    #[serde(default)]
    pub published_at: String,
    #[serde(default)]
    pub changelog: String,
    #[serde(default)]
    pub native: bool,
    #[serde(default)]
    pub yanked: bool,
    /// The `requires` of the version's `swofty.json`, when the registry lists them.
    #[serde(
        default,
        deserialize_with = "listed_requires",
        skip_serializing_if = "Option::is_none"
    )]
    pub requires: Option<Vec<Requirement>>,
}

/// A `requires` list the launcher can't read counts as unlisted, so the
/// install reads the download's `swofty.json` instead.
fn listed_requires<'de, D: Deserializer<'de>>(
    d: D,
) -> std::result::Result<Option<Vec<Requirement>>, D::Error> {
    let raw = Option::<serde_json::Value>::deserialize(d)?;
    Ok(raw.and_then(|v| serde_json::from_value(v).ok()))
}

impl Version {
    pub fn accepts(&self, launcher: &semver::Version) -> bool {
        self.launcher.as_ref().is_none_or(|r| r.matches(launcher))
    }
}

impl Addon {
    /// Every listed version once, newest first, yanked ones left out.
    pub fn listed(&self) -> Vec<&Version> {
        let mut out: Vec<&Version> = Vec::new();
        for v in self.versions.iter().chain(self.latest.iter()) {
            if !v.yanked && !out.iter().any(|o| o.version == v.version) {
                out.push(v);
            }
        }
        out.sort_by(|a, b| b.version.cmp(&a.version));
        out
    }

    /// The newest version `launcher` can install.
    pub fn offered(&self, launcher: &semver::Version) -> Option<&Version> {
        self.listed().into_iter().find(|v| v.accepts(launcher))
    }

    pub fn newest(&self) -> Option<&Version> {
        self.listed().into_iter().next()
    }

    pub fn native(&self, launcher: &semver::Version) -> bool {
        self.offered(launcher)
            .or_else(|| self.newest())
            .is_some_and(|v| v.native)
    }

    /// The category it lists, read as the launcher reads a manifest's: unknown
    /// and retired ones are none.
    pub fn category(&self) -> Option<Category> {
        let raw = self.category.as_deref()?.trim();
        serde_json::from_value(serde_json::Value::String(raw.to_owned())).ok()
    }

    pub fn category_label(&self) -> Option<String> {
        let raw = self.category.as_deref()?.trim();
        if raw.is_empty() {
            return None;
        }
        Some(match self.category() {
            Some(c) => c.label().to_owned(),
            None => title_case(raw),
        })
    }

    /// Its kinds as the Addons page names them, each once.
    pub fn kind_labels(&self) -> Vec<&'static str> {
        let mut out: Vec<&'static str> = Vec::new();
        for k in &self.kind {
            if let Some(l) = kind_label(k).filter(|l| !out.contains(l)) {
                out.push(l);
            }
        }
        out
    }

    /// Who it is by: its owner's name, or the manifest's author for a catalog
    /// saved before the registry listed owners.
    pub fn author_name(&self) -> &str {
        match self.owner().map(|o| o.name.trim()) {
            Some(name) if !name.is_empty() => name,
            _ => self.author.trim(),
        }
    }
}

pub fn kind_label(kind: &str) -> Option<&'static str> {
    Some(match kind {
        "database" => "Database",
        "erp" => "Archives",
        "text" | "ui" => "Text & UI",
        "simplified" => "Simplified",
        "files" => "Files",
        "native" => "Code",
        _ => return None,
    })
}

fn title_case(text: &str) -> String {
    text.split(['_', '-', ' '])
        .filter(|w| !w.is_empty())
        .map(|w| {
            let mut c = w.chars();
            c.next()
                .map(|f| f.to_uppercase().chain(c).collect::<String>())
                .unwrap_or_default()
        })
        .collect::<Vec<_>>()
        .join(" ")
}

#[derive(Debug, Clone, PartialEq)]
pub struct Update {
    pub id: String,
    pub name: String,
    pub installed: semver::Version,
    pub to: Version,
}

/// The installed addons the catalog has a newer version of that `launcher` can install.
pub fn updates<'a>(
    installed: impl IntoIterator<Item = &'a AddonManifest>,
    catalog: &[Addon],
    launcher: &semver::Version,
) -> Vec<Update> {
    installed
        .into_iter()
        .filter_map(|m| {
            let addon = catalog.iter().find(|a| a.id == m.id)?;
            let to = addon.offered(launcher).filter(|v| v.version > m.version)?;
            Some(Update {
                id: m.id.clone(),
                name: addon.name.clone(),
                installed: m.version.clone(),
                to: to.clone(),
            })
        })
        .collect()
}

/// [`updates`] for the addons in `store`, leaving out the ones the launcher ships.
pub fn store_updates(store: &Store, catalog: &[Addon], launcher: &semver::Version) -> Result<Vec<Update>> {
    let addons = store.addons()?;
    let installed = addons
        .values()
        .filter(|m| !store.is_builtin(&m.id) && !crate::managed::is_managed(&m.id));
    Ok(updates(installed, catalog, launcher))
}

/// The Addon Browser download an installed addon came from.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Installed {
    pub version: semver::Version,
    pub sha256: String,
}

/// Records that `id` was just installed from `v`'s download.
pub fn record_install(store: &Store, id: &str, v: &Version) -> Result<()> {
    let installed = Installed {
        version: v.version.clone(),
        sha256: v.sha256.trim().to_ascii_lowercase(),
    };
    crate::write_json_shared(&store.registry_install_path(id), &installed)
}

/// The SHA-256 of the Addon Browser download `m` was installed from, while the installed
/// version is still the one that download had.
pub fn installed_from(store: &Store, m: &AddonManifest) -> Option<String> {
    let installed: Installed = crate::read_json(&store.registry_install_path(&m.id)).ok()?;
    (installed.version == m.version).then_some(installed.sha256)
}

pub fn base_url() -> String {
    std::env::var(URL_ENV)
        .ok()
        .map(|u| u.trim().trim_end_matches('/').to_owned())
        .filter(|u| !u.is_empty())
        .unwrap_or_else(|| DEFAULT_URL.to_owned())
}

pub fn catalog_url() -> String {
    format!("{}/v1/addons", base_url())
}

#[derive(Deserialize, Serialize)]
struct Catalog {
    addons: Vec<Addon>,
}

pub fn parse(text: &str) -> Option<Vec<Addon>> {
    serde_json::from_str::<Catalog>(text).ok().map(|c| c.addons)
}

pub fn cache_path(root: &Path) -> PathBuf {
    root.join(CACHE_DIR).join(CATALOG_FILE)
}

/// When the saved catalog was last fetched.
pub fn cached_at(root: &Path) -> Option<SystemTime> {
    std::fs::metadata(cache_path(root))
        .and_then(|m| m.modified())
        .ok()
}

pub fn cached(root: &Path) -> Option<Vec<Addon>> {
    std::fs::read_to_string(cache_path(root))
        .ok()
        .and_then(|t| parse(&t))
}

#[derive(Debug, Clone, PartialEq)]
pub enum Trouble {
    Offline(String),
    Failed(String),
}

impl std::fmt::Display for Trouble {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Offline(text) | Self::Failed(text) => f.write_str(text),
        }
    }
}

pub(crate) fn agent_name() -> &'static str {
    concat!("swofty-launcher/", env!("CARGO_PKG_VERSION"))
}

fn get(url: &str, timeout: Duration) -> std::result::Result<ureq::Body, Trouble> {
    let request = ureq::get(url)
        .header("User-Agent", agent_name())
        .config()
        .timeout_global(Some(timeout))
        .timeout_connect(Some(Duration::from_secs(15)))
        .http_status_as_error(false)
        .build();
    answer(url, request)
}

fn answer(
    url: &str,
    request: ureq::RequestBuilder<ureq::typestate::WithoutBody>,
) -> std::result::Result<ureq::Body, Trouble> {
    let response = request
        .call()
        .map_err(|e| Trouble::Offline(format!("{url}: {e}")))?;
    let status = response.status().as_u16();
    if status >= 400 {
        return Err(Trouble::Failed(format!("{url}: HTTP {status}")));
    }
    Ok(response.into_body())
}

/// How long each part of a catalog fetch may take.
#[derive(Debug, Clone, Copy, PartialEq)]
struct Waits {
    connect: Duration,
    response: Duration,
    body: Duration,
}

impl Waits {
    fn catalog(root: &Path) -> Self {
        let saved = std::fs::metadata(cache_path(root)).map_or(0, |m| m.len());
        Self {
            connect: Duration::from_secs(15),
            response: Duration::from_secs(30),
            body: catalog_timeout(saved),
        }
    }
}

/// How long the body of a `size`-byte catalog may take: [`DOWNLOAD_RATE`] on
/// average, and never less than [`CATALOG_FLOOR`].
fn catalog_timeout(size: u64) -> Duration {
    CATALOG_FLOOR.max(Duration::from_secs(size.div_ceil(DOWNLOAD_RATE)))
}

fn read_catalog(url: &str, waits: Waits) -> std::result::Result<String, Trouble> {
    let request = ureq::get(url)
        .header("User-Agent", agent_name())
        .config()
        .timeout_resolve(Some(waits.connect))
        .timeout_connect(Some(waits.connect))
        .timeout_send_request(Some(waits.response))
        .timeout_recv_response(Some(waits.response))
        .timeout_recv_body(Some(waits.body))
        .http_status_as_error(false)
        .build();
    answer(url, request)?
        .with_config()
        .limit(CATALOG_LIMIT)
        .read_to_string()
        .map_err(|e| match e {
            ureq::Error::Timeout(_) | ureq::Error::Io(_) => Trouble::Offline(format!("{url}: {e}")),
            _ => Trouble::Failed(format!("{url}: {e}")),
        })
}

/// The catalog from the registry, saved for the next start. A fetch that
/// can't reach the registry is tried once more.
pub fn fetch(root: &Path) -> std::result::Result<Vec<Addon>, Trouble> {
    fetch_from(root, &catalog_url(), Waits::catalog(root))
}

fn fetch_from(root: &Path, url: &str, waits: Waits) -> std::result::Result<Vec<Addon>, Trouble> {
    let text = match read_catalog(url, waits) {
        Err(Trouble::Offline(_)) => read_catalog(url, waits)?,
        read => read?,
    };
    let addons = parse(&text).ok_or_else(|| Trouble::Failed(format!("{url}: not an addon catalog")))?;
    let path = cache_path(root);
    if let Some(dir) = path.parent() {
        std::fs::create_dir_all(dir).ok();
    }
    if let Err(e) = std::fs::write(&path, text) {
        eprintln!("swofty-core: could not save the addon catalog: {e}");
    }
    Ok(addons)
}

/// How far an install has got.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Stage {
    Downloading(u64),
    Verifying,
    Installing,
}

/// One addon with every version and its 30-day trend.
pub fn fetch_one(id: &str) -> std::result::Result<Addon, Trouble> {
    let url = format!("{}/v1/addons/{id}", base_url());
    let text = get(&url, TIMEOUT)?
        .with_config()
        .limit(CATALOG_LIMIT)
        .read_to_string()
        .map_err(|e| Trouble::Failed(format!("{url}: {e}")))?;
    serde_json::from_str(&text).map_err(|e| Trouble::Failed(format!("{url}: {e}")))
}

pub fn now_secs() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_or(0, |d| d.as_secs() as i64)
}

/// `secs` since the Unix epoch as `2026-10-23T01:02:03Z`.
pub fn iso(secs: i64) -> String {
    let (days, rem) = (secs.div_euclid(86_400), secs.rem_euclid(86_400));
    let z = days + 719_468;
    let era = z.div_euclid(146_097);
    let doe = z - era * 146_097;
    let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = doy - (153 * mp + 2) / 5 + 1;
    let m = if mp < 10 { mp + 3 } else { mp - 9 };
    let y = yoe + era * 400 + i64::from(m <= 2);
    format!(
        "{y:04}-{m:02}-{d:02}T{:02}:{:02}:{:02}Z",
        rem / 3600,
        rem % 3600 / 60,
        rem % 60
    )
}

/// Now as `2026-10-23T01:02:03.456Z`.
pub fn iso_now() -> String {
    let millis = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_or(0, |d| d.as_millis() as i64);
    let secs = iso(millis.div_euclid(1000));
    format!("{}.{:03}Z", &secs[..secs.len() - 1], millis.rem_euclid(1000))
}

/// The seconds since the Unix epoch of an ISO 8601 UTC time, to the second.
pub fn epoch(iso: &str) -> Option<i64> {
    let num = |r: std::ops::Range<usize>| iso.get(r)?.parse::<i64>().ok();
    let (y, m, d) = (num(0..4)?, num(5..7)?, num(8..10)?);
    let (hh, mm, ss) = (
        num(11..13).unwrap_or(0),
        num(14..16).unwrap_or(0),
        num(17..19).unwrap_or(0),
    );
    let y = if m <= 2 { y - 1 } else { y };
    let era = y.div_euclid(400);
    let yoe = y - era * 400;
    let doy = (153 * (if m > 2 { m - 3 } else { m + 9 }) + 2) / 5 + d - 1;
    let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    let days = era * 146_097 + doe - 719_468;
    Some(days * 86_400 + hh * 3600 + mm * 60 + ss)
}

/// Adds a line to the registry log Save Logs sends, and to stderr.
pub fn log(root: &Path, line: &str) {
    eprintln!("swofty-launcher: registry: {line}");
    let path = root.join(LOG_FILE);
    if std::fs::metadata(&path).is_ok_and(|m| m.len() > LOG_LIMIT)
        && let Ok(text) = std::fs::read_to_string(&path)
    {
        let keep = text.len() / 2;
        let cut = text[keep..].find('\n').map_or(text.len(), |i| keep + i + 1);
        std::fs::write(&path, &text[cut..]).ok();
    }
    if let Some(dir) = path.parent() {
        std::fs::create_dir_all(dir).ok();
    }
    let opened = std::fs::OpenOptions::new().create(true).append(true).open(&path);
    if let Ok(mut file) = opened {
        writeln!(file, "{} {line}", iso(now_secs())).ok();
    }
}

pub fn verify(v: &Version, len: u64, sha256: &str) -> Result<()> {
    if len != v.size {
        return Err(Error::Other(format!(
            "downloaded {len} bytes, the registry lists {}",
            v.size
        )));
    }
    if !sha256.eq_ignore_ascii_case(v.sha256.trim()) {
        return Err(Error::Other(format!(
            "the download's SHA-256 is {sha256}, the registry lists {}",
            v.sha256
        )));
    }
    Ok(())
}

fn within_limit(v: &Version) -> Result<()> {
    if v.size > DOWNLOAD_LIMIT {
        return Err(Error::Other(format!(
            "{} bytes is over the {DOWNLOAD_LIMIT}-byte download limit",
            v.size
        )));
    }
    Ok(())
}

/// Copies `reader` to `to`, refusing more bytes than `v` lists, then checks
/// the size and SHA-256.
pub fn save(mut reader: impl Read, v: &Version, to: &Path, progress: &dyn Fn(Stage)) -> Result<()> {
    within_limit(v)?;
    let mut file = std::fs::File::create(to).map_err(io_err(to))?;
    let mut hash = Sha256::new();
    let mut buf = vec![0; 64 * 1024];
    let mut len = 0u64;
    loop {
        let n = reader
            .read(&mut buf)
            .map_err(|e| Error::Other(format!("{}: {e}", v.url)))?;
        if n == 0 {
            break;
        }
        len += n as u64;
        if len > v.size {
            return Err(Error::Other(format!(
                "larger than the {} bytes the registry lists",
                v.size
            )));
        }
        hash.update(&buf[..n]);
        file.write_all(&buf[..n]).map_err(io_err(to))?;
        progress(Stage::Downloading(len));
    }
    progress(Stage::Verifying);
    file.sync_all().map_err(io_err(to))?;
    let digest: String = hash.finalize().iter().map(|b| format!("{b:02x}")).collect();
    verify(v, len, &digest)
}

/// How long the body of a `size`-byte download may take: [`DOWNLOAD_RATE`]
/// on average, and never less than [`DOWNLOAD_FLOOR`].
pub fn body_timeout(size: u64) -> Duration {
    DOWNLOAD_FLOOR.max(Duration::from_secs(size.div_ceil(DOWNLOAD_RATE)))
}

/// Downloads `v` to `to`, following redirects to where the registry keeps the
/// file, and checks it with [`save`]. The body streams to disk.
pub fn download(v: &Version, to: &Path, progress: &dyn Fn(Stage)) -> Result<()> {
    within_limit(v)?;
    let url = &v.url;
    let response = ureq::get(url)
        .header("User-Agent", agent_name())
        .config()
        .timeout_resolve(Some(Duration::from_secs(15)))
        .timeout_connect(Some(Duration::from_secs(15)))
        .timeout_send_request(Some(Duration::from_secs(30)))
        .timeout_recv_response(Some(Duration::from_secs(60)))
        .timeout_recv_body(Some(body_timeout(v.size)))
        .http_status_as_error(false)
        .build()
        .call()
        .map_err(|e| Error::Other(format!("{url}: {e}")))?;
    let status = response.status().as_u16();
    if status >= 400 {
        return Err(Error::Other(format!("{url}: HTTP {status}")));
    }
    let encoded = response.headers().contains_key("content-encoding");
    let sent = response
        .headers()
        .get("content-length")
        .and_then(|h| h.to_str().ok())
        .and_then(|h| h.trim().parse::<u64>().ok());
    if let Some(sent) = sent.filter(|s| !encoded && *s != v.size) {
        return Err(Error::Other(format!(
            "{url}: the server sends {sent} bytes, the registry lists {}",
            v.size
        )));
    }
    let mut body = response.into_body();
    save(body.with_config().limit(v.size + 1).reader(), v, to, progress)
}

fn downloads_dir(store: &Store) -> Result<PathBuf> {
    let dir = store.root.join(CACHE_DIR).join("downloads");
    ensure_dir(&store.root, &dir).map_err(io_err(&dir))?;
    Ok(dir)
}

fn ensure_dir(root: &Path, dir: &Path) -> std::io::Result<()> {
    let first = match std::fs::create_dir_all(dir) {
        Ok(()) => return Ok(()),
        Err(e) => e,
    };
    let Ok(rel) = dir.strip_prefix(root) else {
        return Err(first);
    };
    let mut at = root.to_path_buf();
    for part in rel.components() {
        at.push(part);
        let Ok(meta) = std::fs::symlink_metadata(&at) else {
            break;
        };
        if at.is_dir() {
            continue;
        }
        if meta.file_type().is_symlink() || meta.file_type().is_dir() {
            eprintln!("swofty-core: removing the broken link {}", at.display());
            if std::fs::remove_file(&at).is_err() {
                std::fs::remove_dir(&at)?;
            }
        } else {
            let mut aside = at.clone().into_os_string();
            aside.push(".stale");
            let aside = PathBuf::from(aside);
            eprintln!(
                "swofty-core: {} is not a folder, moving it to {}",
                at.display(),
                aside.display()
            );
            if std::fs::rename(&at, &aside).is_err() {
                std::fs::remove_file(&at)?;
            }
        }
        break;
    }
    std::fs::create_dir_all(dir)
}

fn file_name(addon: &Addon, v: &Version) -> String {
    let safe: String = addon
        .id
        .chars()
        .map(|c| {
            if c.is_ascii_alphanumeric() || c == '.' || c == '-' {
                c
            } else {
                '_'
            }
        })
        .collect();
    format!("{safe}-{}.zip", v.version)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ensure_dir_moves_a_file_aside() {
        let root = scratch("file");
        let dir = root.join(CACHE_DIR).join("downloads");
        std::fs::create_dir_all(dir.parent().unwrap()).unwrap();
        std::fs::write(&dir, b"x").unwrap();
        ensure_dir(&root, &dir).unwrap();
        assert!(dir.is_dir());
        let _ = std::fs::remove_dir_all(&root);
    }

    #[test]
    fn ensure_dir_replaces_a_file_higher_up() {
        let root = scratch("parent");
        let dir = root.join(CACHE_DIR).join("downloads");
        std::fs::create_dir_all(root.join("cache")).unwrap();
        std::fs::write(root.join(CACHE_DIR), b"x").unwrap();
        ensure_dir(&root, &dir).unwrap();
        assert!(dir.is_dir());
        let _ = std::fs::remove_dir_all(&root);
    }

    #[cfg(unix)]
    #[test]
    fn ensure_dir_removes_a_dangling_link() {
        let root = scratch("link");
        let dir = root.join(CACHE_DIR).join("downloads");
        std::fs::create_dir_all(dir.parent().unwrap()).unwrap();
        std::os::unix::fs::symlink(root.join("gone"), &dir).unwrap();
        ensure_dir(&root, &dir).unwrap();
        assert!(dir.is_dir());
        let _ = std::fs::remove_dir_all(&root);
    }

    fn version(v: &str, launcher: Option<&str>) -> Version {
        Version {
            version: semver::Version::parse(v).unwrap(),
            launcher: launcher.map(|l| VersionReq::parse(l).unwrap()),
            game: "f1_25".into(),
            size: 3,
            sha256: String::new(),
            url: format!("http://localhost/{v}"),
            published_at: String::new(),
            changelog: String::new(),
            native: false,
            yanked: false,
            requires: None,
        }
    }

    fn addon(id: &str, versions: Vec<Version>) -> Addon {
        Addon {
            id: id.into(),
            name: id.into(),
            author: "a".into(),
            summary: String::new(),
            description: String::new(),
            category: None,
            kind: Vec::new(),
            homepage: None,
            icon: None,
            publisher: None,
            publishers: Vec::new(),
            downloads: 0,
            downloads_30d: 0,
            stars: 0,
            trend: None,
            created_at: String::new(),
            updated_at: String::new(),
            latest: versions.first().cloned(),
            versions,
        }
    }

    fn manifest(id: &str, v: &str) -> AddonManifest {
        serde_json::from_value(serde_json::json!({
            "schema_version": 1,
            "id": id,
            "name": id,
            "version": v,
            "author": "a",
            "compat": { "game": "f1_25" }
        }))
        .unwrap()
    }

    fn launcher(v: &str) -> semver::Version {
        semver::Version::parse(v).unwrap()
    }

    #[test]
    fn offers_the_newest_version_this_launcher_can_install() {
        let mut yanked = version("0.4.0", None);
        yanked.yanked = true;
        let a = addon(
            "x",
            vec![
                yanked,
                version("0.3.0", Some(">=2.0.0")),
                version("0.2.0", Some(">=1.3.0")),
                version("0.1.0", None),
            ],
        );
        assert_eq!(
            a.offered(&launcher("1.3.7")).unwrap().version.to_string(),
            "0.2.0"
        );
        assert_eq!(
            a.offered(&launcher("2.1.0")).unwrap().version.to_string(),
            "0.3.0"
        );
        assert_eq!(
            a.offered(&launcher("1.0.0")).unwrap().version.to_string(),
            "0.1.0"
        );
        assert_eq!(a.newest().unwrap().version.to_string(), "0.3.0");
        let picky = addon("y", vec![version("1.0.0", Some(">=9.0.0"))]);
        assert!(picky.offered(&launcher("1.3.7")).is_none());
    }

    #[test]
    fn finds_updates_for_installed_addons_only() {
        let catalog = vec![
            addon("a", vec![version("0.2.0", None), version("0.1.0", None)]),
            addon("b", vec![version("1.0.0", None)]),
            addon(
                "c",
                vec![version("2.0.0", Some(">=9.0.0")), version("1.0.0", None)],
            ),
            addon("d", vec![version("1.0.0", None)]),
        ];
        let installed = [
            manifest("a", "0.1.0"),
            manifest("b", "1.0.0"),
            manifest("c", "1.0.0"),
            manifest("e", "1.0.0"),
        ];
        let found = updates(&installed, &catalog, &launcher("1.3.7"));
        assert_eq!(found.len(), 1);
        assert_eq!(found[0].id, "a");
        assert_eq!(found[0].installed.to_string(), "0.1.0");
        assert_eq!(found[0].to.version.to_string(), "0.2.0");
        let newer = [manifest("a", "0.3.0")];
        assert!(updates(&newer, &catalog, &launcher("1.3.7")).is_empty());
    }

    #[test]
    fn rejects_downloads_that_do_not_match() {
        let dir = std::env::temp_dir().join(format!("swofty-catalog-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let to = dir.join("addon.zip");
        let mut v = version("1.0.0", None);
        v.sha256 = "39a3ab5f19d0e1e2a7d6ad4b1c4d68c0f5c6ff2e1ae4b8c5bbce7c5c2a8b3e6b".into();
        let wrong = save(&b"abc"[..], &v, &to, &|_| {}).unwrap_err();
        assert!(wrong.to_string().contains("SHA-256"), "{wrong}");
        v.sha256 = "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD".into();
        save(&b"abc"[..], &v, &to, &|_| {}).unwrap();
        assert_eq!(std::fs::read(&to).unwrap(), b"abc");
        let long = save(&b"abcd"[..], &v, &to, &|_| {}).unwrap_err();
        assert!(long.to_string().contains("larger"), "{long}");
        v.size = 4;
        let short = save(&b"abc"[..], &v, &to, &|_| {}).unwrap_err();
        assert!(short.to_string().contains("downloaded 3 bytes"), "{short}");
        v.size = DOWNLOAD_LIMIT + 1;
        assert!(save(&b""[..], &v, &to, &|_| {}).is_err());
        std::fs::remove_dir_all(&dir).ok();
    }

    struct Served {
        url: String,
        requests: std::sync::Arc<Mutex<Vec<String>>>,
    }

    /// An HTTP server on a free local port that answers each connection with
    /// the next of `answers` and keeps every request head it reads.
    fn serve(answers: Vec<Vec<u8>>) -> Served {
        let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
        let url = format!("http://{}", listener.local_addr().unwrap());
        let requests = std::sync::Arc::new(Mutex::new(Vec::new()));
        let seen = requests.clone();
        std::thread::spawn(move || {
            for answer in answers {
                let Ok((mut stream, _)) = listener.accept() else {
                    return;
                };
                seen.lock().unwrap().push(read_head(&mut stream));
                stream.write_all(&answer).ok();
            }
        });
        Served { url, requests }
    }

    fn read_head(stream: &mut std::net::TcpStream) -> String {
        let mut head = Vec::new();
        let mut byte = [0u8; 1];
        while !head.ends_with(b"\r\n\r\n") && stream.read(&mut byte).is_ok_and(|n| n == 1) {
            head.push(byte[0]);
        }
        String::from_utf8_lossy(&head).into_owned()
    }

    /// Like [`serve`], but each connection waits its `Duration` before
    /// answering, then writes 1 KiB every 50 ms.
    fn serve_slowly(answers: Vec<(Duration, Vec<u8>)>) -> Served {
        let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
        let url = format!("http://{}", listener.local_addr().unwrap());
        let requests = std::sync::Arc::new(Mutex::new(Vec::new()));
        let seen = requests.clone();
        std::thread::spawn(move || {
            for (wait, answer) in answers {
                let Ok((mut stream, _)) = listener.accept() else {
                    return;
                };
                let seen = seen.clone();
                std::thread::spawn(move || {
                    seen.lock().unwrap().push(read_head(&mut stream));
                    std::thread::sleep(wait);
                    for piece in answer.chunks(1024) {
                        if stream.write_all(piece).and_then(|_| stream.flush()).is_err() {
                            return;
                        }
                        std::thread::sleep(Duration::from_millis(50));
                    }
                });
            }
        });
        Served { url, requests }
    }

    fn catalog_json(padding: usize) -> Vec<u8> {
        format!(r#"{{"addons":[{{"id":"x","name":"X"}}{}]}}"#, " ".repeat(padding)).into_bytes()
    }

    fn waits(response_ms: u64, body_ms: u64) -> Waits {
        Waits {
            connect: Duration::from_secs(5),
            response: Duration::from_millis(response_ms),
            body: Duration::from_millis(body_ms),
        }
    }

    fn ok(body: &[u8], length: bool) -> Vec<u8> {
        let mut out = b"HTTP/1.1 200 OK\r\nContent-Type: application/zip\r\nConnection: close\r\n".to_vec();
        if length {
            out.extend(format!("Content-Length: {}\r\n", body.len()).into_bytes());
        }
        out.extend(b"\r\n");
        out.extend(body);
        out
    }

    fn sha256_hex(data: &[u8]) -> String {
        Sha256::digest(data).iter().map(|b| format!("{b:02x}")).collect()
    }

    fn scratch(name: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!("swofty-catalog-{name}-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        dir
    }

    fn header<'a>(head: &'a str, name: &str) -> Option<&'a str> {
        head.lines().find_map(|l| {
            let (k, v) = l.split_once(':')?;
            k.trim().eq_ignore_ascii_case(name).then(|| v.trim())
        })
    }

    #[test]
    fn follows_the_registry_redirect_to_the_cdn() {
        let body = b"PK fake addon bytes".repeat(1000);
        let cdn = serve(vec![ok(&body, true)]);
        let location = format!("{}/addons/x/1.0.0/k/x-1.0.0.swoftyaddon", cdn.url);
        let registry = serve(vec![
            format!(
                "HTTP/1.1 302 Found\r\nLocation: {location}\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
            )
            .into_bytes(),
        ]);
        let dir = scratch("redirect");
        let to = dir.join("x.zip");
        let mut v = version("1.0.0", None);
        v.url = format!("{}/v1/addons/x/versions/1.0.0/download", registry.url);
        v.size = body.len() as u64;
        v.sha256 = sha256_hex(&body);
        download(&v, &to, &|_| {}).unwrap();
        assert_eq!(std::fs::read(&to).unwrap(), body);
        let first = registry.requests.lock().unwrap()[0].clone();
        assert!(
            first.starts_with("GET /v1/addons/x/versions/1.0.0/download "),
            "{first}"
        );
        let second = cdn.requests.lock().unwrap()[0].clone();
        assert!(
            second.starts_with("GET /addons/x/1.0.0/k/x-1.0.0.swoftyaddon "),
            "{second}"
        );
        assert_eq!(header(&second, "user-agent"), Some(agent_name()));
        assert_eq!(header(&second, "host"), cdn.url.strip_prefix("http://"));
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn refuses_downloads_over_the_limit_or_longer_than_listed() {
        let dir = scratch("limits");
        let to = dir.join("x.zip");
        let mut v = version("1.0.0", None);
        v.url = "http://127.0.0.1:9/never-called".into();
        v.size = DOWNLOAD_LIMIT + 1;
        let over = download(&v, &to, &|_| {}).unwrap_err();
        assert!(over.to_string().contains("download limit"), "{over}");
        v.size = DOWNLOAD_LIMIT;
        assert!(within_limit(&v).is_ok());

        let body = b"0123456789".repeat(10);
        v.size = 50;
        v.sha256 = sha256_hex(&body[..50]);
        let declared = serve(vec![ok(&body, true)]);
        v.url = format!("{}/d", declared.url);
        let told = download(&v, &to, &|_| {}).unwrap_err();
        assert!(told.to_string().contains("the server sends 100 bytes"), "{told}");

        let streamed = serve(vec![ok(&body, false)]);
        v.url = format!("{}/d", streamed.url);
        let long = download(&v, &to, &|_| {}).unwrap_err();
        assert!(long.to_string().contains("larger than the 50 bytes"), "{long}");

        let exact = serve(vec![ok(&body[..50], false)]);
        v.url = format!("{}/d", exact.url);
        download(&v, &to, &|_| {}).unwrap();
        assert_eq!(std::fs::read(&to).unwrap(), &body[..50]);
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn gives_large_downloads_time_by_size() {
        assert_eq!(body_timeout(0), DOWNLOAD_FLOOR);
        assert_eq!(body_timeout(200 * 1024 * 1024), DOWNLOAD_FLOOR);
        assert_eq!(body_timeout(1024 * 1024 * 1024), Duration::from_secs(8192));
        assert_eq!(body_timeout(DOWNLOAD_LIMIT), Duration::from_secs(32768));
    }

    #[test]
    fn gives_the_catalog_time_by_size() {
        assert_eq!(catalog_timeout(0), CATALOG_FLOOR);
        assert_eq!(catalog_timeout(200 * 1024), CATALOG_FLOOR);
        assert_eq!(catalog_timeout(CATALOG_LIMIT), Duration::from_secs(128));
        let dir = scratch("waits");
        assert_eq!(Waits::catalog(&dir).body, CATALOG_FLOOR);
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn reads_a_slow_catalog_body_past_the_response_wait() {
        let body = catalog_json(8 * 1024);
        let registry = serve_slowly(vec![(Duration::ZERO, ok(&body, true))]);
        let dir = scratch("slow");
        let url = format!("{}/v1/addons", registry.url);
        let addons = fetch_from(&dir, &url, waits(200, 10_000)).unwrap();
        assert_eq!(addons[0].id, "x");
        assert_eq!(cached(&dir).unwrap(), addons);
        assert_eq!(registry.requests.lock().unwrap().len(), 1);
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn tries_the_catalog_once_more_when_the_registry_is_unreachable() {
        let body = catalog_json(0);
        let registry = serve_slowly(vec![
            (Duration::from_secs(5), ok(&body, true)),
            (Duration::ZERO, ok(&body, true)),
        ]);
        let dir = scratch("retry");
        let url = format!("{}/v1/addons", registry.url);
        let addons = fetch_from(&dir, &url, waits(500, 5_000)).unwrap();
        assert_eq!(addons[0].id, "x");
        assert_eq!(registry.requests.lock().unwrap().len(), 2);

        let body = catalog_json(8 * 1024);
        let stalled = serve_slowly(vec![
            (Duration::ZERO, ok(&body, true)),
            (Duration::ZERO, ok(&body, true)),
            (Duration::ZERO, ok(&body, true)),
        ]);
        let url = format!("{}/v1/addons", stalled.url);
        let e = fetch_from(&dir, &url, waits(500, 150)).unwrap_err();
        assert!(
            matches!(&e, Trouble::Offline(t) if t.contains("timeout")),
            "{e:?}"
        );
        assert_eq!(stalled.requests.lock().unwrap().len(), 2);

        let missing = serve(vec![
            b"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n".to_vec(),
        ]);
        let url = format!("{}/v1/addons", missing.url);
        let e = fetch_from(&dir, &url, waits(500, 5_000)).unwrap_err();
        assert_eq!(e, Trouble::Failed(format!("{url}: HTTP 404")));
        assert_eq!(missing.requests.lock().unwrap().len(), 1);
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn reads_the_registry_catalog() {
        let text = r#"{"addons":[{"id":"swofty.career-editor","name":"Career Mode Editor","author":"Swofty",
            "summary":"s","description":"","category":"tracks","kind":["native"],"homepage":null,
            "icon":null,"publishers":[{"id":"1","name":"swofty","avatar":null}],"downloads":12,
            "created_at":"2026-10-22T10:00:00Z","updated_at":"2026-10-23T01:00:00Z",
            "latest":{"version":"0.2.3","launcher":">=1.2.0","game":"F1 25","size":10,"sha256":"ab",
            "url":"http://x/d","published_at":"","changelog":"","native":true,"yanked":false},
            "versions":[{"version":"0.2.3","launcher":null,"game":"F1 25","size":10,"sha256":"ab",
            "url":"http://x/d","published_at":"","changelog":"- fix","native":true,"yanked":false}],
            "extra":1}]}"#;
        let addons = parse(text).unwrap();
        assert_eq!(addons[0].downloads, 12);
        assert_eq!(addons[0].category_label().as_deref(), Some("Tracks"));
        assert!(addons[0].native(&launcher("1.3.7")));
        assert_eq!(addons[0].listed().len(), 1);
        assert_eq!(addons[0].kind_labels(), ["Code"]);
        assert_eq!(addons[0].stars, 0);
        assert!(parse("<html>").is_none());
    }

    #[test]
    fn reads_and_writes_utc_times() {
        for t in [
            "1970-01-01T00:00:00Z",
            "2000-02-29T23:59:59Z",
            "2026-10-24T01:02:03Z",
        ] {
            assert_eq!(iso(epoch(t).unwrap()), t);
        }
        assert_eq!(epoch("2026-10-24T01:02:03.456Z"), epoch("2026-10-24T01:02:03Z"));
        assert_eq!(
            epoch("2026-10-24").map(iso).as_deref(),
            Some("2026-10-24T00:00:00Z")
        );
        assert!(epoch("soon").is_none());
        assert_eq!(iso_now().len(), "2026-10-24T01:02:03.456Z".len());
    }
}
