use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::time::Duration;

use semver::{Version, VersionReq};
use sha2::{Digest, Sha256};

use crate::launcher::Progress;
use crate::manifest::{AddonManifest, MANIFEST_FILE};
use crate::store::Store;
use crate::update::{self, Asset, Failure, Release};
use crate::{Error, Result, io_err};

pub const ID: &str = "teamsimplified.mods";
pub const NAME: &str = "Simplified for Swofty";
pub const ASSET: &str = "teamsimplified.mods.swoftyaddon";
pub const MANIFEST_ASSET: &str = MANIFEST_FILE;
pub const LATEST_URL: &str =
    "https://api.github.com/repos/TeamSimplified/simplified-swofty-addon/releases/latest";
pub const LIST_URL: &str =
    "https://api.github.com/repos/TeamSimplified/simplified-swofty-addon/releases?per_page=20";
pub const RELEASES_URL: &str = "https://github.com/TeamSimplified/simplified-swofty-addon/releases";
const SUMS_TIMEOUT: Duration = Duration::from_secs(60);
const CACHE_DIR: &str = "cache/managed";

pub fn is_managed(id: &str) -> bool {
    id == ID
}

pub fn newer(installed: Option<&Version>, release: &Version) -> bool {
    installed.is_none_or(|current| release > current)
}

pub fn installed(store: &Store) -> Option<Version> {
    store.addons().ok()?.get(ID).map(|m| m.version.clone())
}

pub fn fetch_latest(timeout: Duration) -> std::result::Result<Release, Failure> {
    update::fetch_release(LATEST_URL, timeout)
}

pub fn fetch_releases(timeout: Duration) -> std::result::Result<Vec<Release>, Failure> {
    update::fetch_json(LIST_URL, timeout)
}

pub fn package_asset(release: &Release) -> Result<&Asset> {
    release
        .assets
        .iter()
        .find(|a| a.name == ASSET)
        .ok_or_else(|| Error::Other(format!("release {} has no {ASSET}", release.tag_name)))
}

pub fn ready(release: &Release) -> bool {
    package_asset(release).is_ok_and(|a| update::verifiable(release, a))
}

fn cache_name(tag: &str) -> String {
    tag.chars()
        .map(|c| match c {
            'a'..='z' | 'A'..='Z' | '0'..='9' | '.' | '-' | '+' => c,
            _ => '_',
        })
        .collect()
}

fn cached(store: &Store, release: &Release) -> PathBuf {
    store
        .root
        .join(CACHE_DIR)
        .join(cache_name(&release.tag_name))
        .join(ASSET)
}

fn hash_file(path: &Path) -> Option<(u64, String)> {
    let bytes = std::fs::read(path).ok()?;
    let sha = Sha256::digest(&bytes)
        .iter()
        .map(|b| format!("{b:02x}"))
        .collect();
    Some((bytes.len() as u64, sha))
}

static PARTS: AtomicUsize = AtomicUsize::new(0);

pub fn fetch_package(store: &Store, release: &Release, progress: &dyn Progress) -> Result<PathBuf> {
    let asset = package_asset(release)?;
    if !update::verifiable(release, asset) {
        return Err(Error::Other(format!(
            "release {} has no SHA-256 for {ASSET}",
            release.tag_name
        )));
    }
    let sums = match release.sums() {
        Some(s) => Some(update::parse_sums(&update::get_text(
            &s.browser_download_url,
            SUMS_TIMEOUT,
        )?)),
        None => None,
    };
    let file = cached(store, release);
    if hash_file(&file)
        .is_some_and(|(len, sha)| update::check_download(asset, len, &sha, sums.as_ref()).is_ok())
    {
        return Ok(file);
    }
    let dir = file.parent().expect("under the cache folder");
    std::fs::create_dir_all(dir).map_err(io_err(dir))?;
    let part = dir.join(format!(
        "{ASSET}.{}-{}.part",
        std::process::id(),
        PARTS.fetch_add(1, Ordering::Relaxed)
    ));
    let total = asset.size.max(1) as f32;
    let text = format!("Downloading {}", release.tag_name);
    let fetched = update::download(asset, &part, &|n| progress.report(0.9 * n as f32 / total, &text))
        .and_then(|(len, sha256)| update::check_download(asset, len, &sha256, sums.as_ref()))
        .and_then(|()| std::fs::rename(&part, &file).map_err(io_err(&file)));
    std::fs::remove_file(&part).ok();
    fetched.map(|()| file)
}

fn launcher_needs(manifest: &AddonManifest) -> Result<VersionReq> {
    if manifest.id != ID {
        return Err(Error::Other(format!(
            "the manifest is for {}, not {ID}",
            manifest.id
        )));
    }
    Ok(manifest.compat.launcher.clone().unwrap_or(VersionReq::STAR))
}

struct Quiet;

impl Progress for Quiet {
    fn report(&self, _: f32, _: &str) {}
}

pub fn requirement(store: &Store, release: &Release) -> Result<VersionReq> {
    let published = release
        .assets
        .iter()
        .find(|a| a.name == MANIFEST_ASSET)
        .and_then(|a| update::get_text(&a.browser_download_url, update::CHECK_TIMEOUT).ok())
        .and_then(|text| AddonManifest::parse(&text).ok())
        .and_then(|m| launcher_needs(&m).ok());
    match published {
        Some(needs) => Ok(needs),
        None => launcher_needs(&crate::package::manifest(&fetch_package(
            store, release, &Quiet,
        )?)?),
    }
}

pub fn prune(store: &Store, keep: &[&str]) {
    let keep: Vec<String> = keep.iter().map(|t| cache_name(t)).collect();
    let dir = store.root.join(CACHE_DIR);
    for entry in std::fs::read_dir(&dir).into_iter().flatten().flatten() {
        let path = entry.path();
        if keep.iter().any(|k| entry.file_name() == k.as_str()) {
            continue;
        }
        match path.is_dir() {
            true => std::fs::remove_dir_all(&path).ok(),
            false => std::fs::remove_file(&path).ok(),
        };
    }
}

pub fn install_release(
    store: &Store,
    release: &Release,
    launcher: &Version,
    progress: &dyn Progress,
) -> Result<Version> {
    let file = fetch_package(store, release, progress)?;
    compatible(&file, release, launcher)?;
    progress.report(0.95, &format!("Installing {}", release.tag_name));
    let installed = store.install_addon_for(&file, launcher).map(|m| m.version);
    if installed.is_ok()
        && let Some(dir) = file.parent()
    {
        std::fs::remove_dir_all(dir).ok();
    }
    installed
}

fn program(store: &Store) -> Option<PathBuf> {
    let m = store.addons().ok()?.remove(ID)?;
    let rel = m.launcher.as_ref()?.program()?.to_owned();
    Some(store.addons_dir().join(ID).join(rel))
}

pub fn broken(store: &Store) -> Option<Version> {
    program(store)
        .filter(|p| !p.is_file())
        .and_then(|_| installed(store))
}

pub fn release_of(version: &Version, timeout: Duration) -> std::result::Result<Release, Failure> {
    fetch_releases(timeout)?
        .into_iter()
        .find(|r| r.version().as_ref() == Some(version) && ready(r))
        .ok_or_else(|| Failure::Failed(format!("{NAME} {version} is not among its recent releases")))
}

pub fn repair(store: &Store, release: &Release) -> Result<Version> {
    let launcher = Version::parse(env!("CARGO_PKG_VERSION")).expect("the workspace version");
    let version = install_release(store, release, &launcher, &Quiet)?;
    match program(store).filter(|p| !p.is_file()) {
        Some(p) => Err(Error::Other(format!(
            "{} is still missing after installing {NAME} {version}; an antivirus most likely removes it. \
             Allow {} in your antivirus, then press Play again",
            p.display(),
            store.addons_dir().join(ID).display()
        ))),
        None => Ok(version),
    }
}

// Checked before installing, because a launcher only checks an addon's
// launcher requirement when it loads it: installing first would replace a
// working copy with one the launcher then refuses to load.
fn compatible(package: &Path, release: &Release, launcher: &Version) -> Result<()> {
    let needs = launcher_needs(&crate::package::manifest(package)?)?;
    match needs.matches(launcher) {
        true => Ok(()),
        false => Err(Error::Other(format!(
            "{NAME} {} needs Swofty Launcher {needs}, not {launcher}",
            release.tag_name
        ))),
    }
}

#[cfg(test)]
pub(crate) mod testing {
    use super::*;

    pub const PROGRAM: &str = "bin/helper";

    pub fn package(path: &Path, version: &str, launcher: &str) {
        package_with(path, version, launcher, Helper::None);
    }

    #[derive(Clone, Copy, PartialEq)]
    pub enum Helper {
        None,
        Declared,
        Shipped,
    }

    pub fn package_with(path: &Path, version: &str, launcher: &str, program: Helper) {
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        let helper = match program != Helper::None {
            true => format!(
                r#","launcher":{{"program":{{"{}":"{PROGRAM}"}},"build":true}}"#,
                std::env::consts::OS
            ),
            false => String::new(),
        };
        let manifest = format!(
            r#"{{"schema_version":1,"id":"{ID}","name":"{NAME}","version":"{version}","author":"t","compat":{{"game":"f1_25","launcher":"{launcher}"}}{helper}}}"#
        );
        let mut z = zip::ZipWriter::new(std::fs::File::create(path).unwrap());
        z.start_file(MANIFEST_FILE, zip::write::SimpleFileOptions::default())
            .unwrap();
        std::io::Write::write_all(&mut z, manifest.as_bytes()).unwrap();
        if program == Helper::Shipped {
            z.start_file(PROGRAM, zip::write::SimpleFileOptions::default())
                .unwrap();
            std::io::Write::write_all(&mut z, b"helper").unwrap();
        }
        z.finish().unwrap();
    }

    pub fn seed(store: &Store, version: &str, launcher: &str) -> Release {
        seed_with(store, version, launcher, Helper::None)
    }

    pub fn seed_with(store: &Store, version: &str, launcher: &str, program: Helper) -> Release {
        let mut release = Release {
            tag_name: format!("v{version}"),
            ..Default::default()
        };
        let file = cached(store, &release);
        package_with(&file, version, launcher, program);
        let (size, sha) = hash_file(&file).unwrap();
        release.assets.push(Asset {
            name: ASSET.into(),
            size,
            browser_download_url: format!("https://example.invalid/{version}/{ASSET}"),
            digest: Some(format!("sha256:{sha}")),
        });
        release
    }

    pub fn store(name: &str) -> Store {
        let root = std::env::temp_dir().join(format!("swofty-managed-{name}-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        Store::new(root)
    }
}

#[cfg(test)]
mod tests {
    use std::collections::BTreeMap;

    use super::*;
    use crate::update::SUMS_FILE;
    use testing::Helper;

    fn v(s: &str) -> Version {
        Version::parse(s).unwrap()
    }

    fn asset(name: &str, size: u64) -> Asset {
        Asset {
            name: name.into(),
            size,
            browser_download_url: format!("https://example.invalid/{name}"),
            digest: None,
        }
    }

    fn release(tag: &str) -> Release {
        Release {
            tag_name: tag.into(),
            assets: vec![asset(ASSET, 10), asset(SUMS_FILE, 98)],
            ..Default::default()
        }
    }

    #[test]
    fn knows_the_managed_addon_id() {
        assert!(is_managed(ID));
        assert!(is_managed("teamsimplified.mods"));
        assert!(!is_managed("swofty.core"));
        assert!(!is_managed("teamsimplified.mods.extra"));
    }

    #[test]
    fn installs_only_a_newer_release() {
        assert!(newer(None, &v("0.5.0")));
        assert!(newer(Some(&v("0.4.0")), &v("0.5.0")));
        assert!(!newer(Some(&v("0.5.0")), &v("0.5.0")));
        assert!(!newer(Some(&v("0.6.0")), &v("0.5.0")));
    }

    #[test]
    fn selects_the_package_asset_and_verifies_its_sum() {
        let r = release("v0.5.0");
        assert_eq!(r.version(), Some(v("0.5.0")));
        let picked = package_asset(&r).unwrap();
        assert_eq!(picked.name, ASSET);
        let good = "a".repeat(64);
        let bad = "b".repeat(64);
        let sums = BTreeMap::from([(ASSET.to_owned(), good.clone())]);
        assert!(update::check_download(picked, 10, &good, Some(&sums)).is_ok());
        assert!(update::check_download(picked, 10, &bad, Some(&sums)).is_err());
        assert!(update::check_download(picked, 9, &good, Some(&sums)).is_err());
        let empty = Release {
            assets: vec![asset(SUMS_FILE, 98)],
            ..release("v0.5.0")
        };
        assert!(package_asset(&empty).is_err());
    }

    #[test]
    fn offers_only_a_release_it_can_verify() {
        assert!(ready(&release("v0.5.0")));
        let unsummed = Release {
            assets: vec![asset(ASSET, 10)],
            ..release("v0.5.0")
        };
        assert!(!ready(&unsummed));
        let digested = Release {
            assets: vec![Asset {
                digest: Some(format!("sha256:{}", "a".repeat(64))),
                ..asset(ASSET, 10)
            }],
            ..release("v0.5.0")
        };
        assert!(ready(&digested));
        let empty = Release {
            assets: vec![asset(SUMS_FILE, 98)],
            ..release("v0.5.0")
        };
        assert!(!ready(&empty));
    }

    #[test]
    fn refuses_a_release_that_needs_a_newer_launcher() {
        let r = release("v0.7.0");
        let pkg = std::env::temp_dir()
            .join(format!("swofty-managed-compat-{}", std::process::id()))
            .join(ASSET);
        testing::package(&pkg, "0.7.0", ">=1.3.0");
        assert!(compatible(&pkg, &r, &v("1.3.0")).is_ok());
        assert!(compatible(&pkg, &r, &v("1.4.2")).is_ok());
        let err = compatible(&pkg, &r, &v("1.2.6")).unwrap_err().to_string();
        assert!(err.contains("needs Swofty Launcher >=1.3.0, not 1.2.6"), "{err}");
        std::fs::remove_dir_all(pkg.parent().unwrap()).ok();
    }

    #[test]
    fn installs_for_the_launcher_that_runs_next() {
        let store = testing::store("next");
        let release = testing::seed(&store, "0.7.0", ">=1.3.0");
        let quiet = Quiet;
        let err = install_release(&store, &release, &v("1.2.6"), &quiet)
            .unwrap_err()
            .to_string();
        assert!(err.contains("needs Swofty Launcher >=1.3.0"), "{err}");
        assert!(
            installed(&store).is_none(),
            "nothing is installed for a launcher it refuses"
        );
        assert!(
            cached(&store, &release).is_file(),
            "the download is kept for later"
        );
        assert_eq!(
            install_release(&store, &release, &v("1.3.0"), &quiet).unwrap(),
            v("0.7.0")
        );
        assert_eq!(installed(&store), Some(v("0.7.0")));
        assert!(
            !cached(&store, &release).exists(),
            "an installed download is removed"
        );
        std::fs::remove_dir_all(&store.root).ok();
    }

    #[test]
    fn reads_the_launcher_requirement_before_installing() {
        let store = testing::store("needs");
        let release = testing::seed(&store, "0.7.0", ">=1.3.0");
        assert_eq!(requirement(&store, &release).unwrap(), ">=1.3.0".parse().unwrap());
        let file = cached(&store, &release);
        std::fs::write(&file, b"tampered").unwrap();
        assert!(
            requirement(&store, &release).is_err(),
            "a cached package that fails its SHA-256 is fetched again"
        );
        std::fs::remove_dir_all(&store.root).ok();
    }

    #[test]
    fn installs_again_a_copy_that_lost_its_program() {
        let store = testing::store("repair");
        assert_eq!(broken(&store), None);
        let release = testing::seed_with(&store, "0.7.0", ">=1.3.0", Helper::Shipped);
        assert_eq!(repair(&store, &release).unwrap(), v("0.7.0"));
        let program = store.addons_dir().join(ID).join(testing::PROGRAM);
        assert!(program.is_file());
        assert_eq!(broken(&store), None);
        std::fs::remove_file(&program).unwrap();
        assert_eq!(broken(&store), Some(v("0.7.0")));
        let release = testing::seed_with(&store, "0.7.0", ">=1.3.0", Helper::Shipped);
        assert_eq!(repair(&store, &release).unwrap(), v("0.7.0"));
        assert!(program.is_file());
        assert_eq!(broken(&store), None);
        let without = testing::seed_with(&store, "0.7.1", ">=1.3.0", Helper::None);
        install_release(&store, &without, &v(env!("CARGO_PKG_VERSION")), &Quiet).unwrap();
        assert_eq!(broken(&store), None, "a copy with no program is not broken");
        std::fs::remove_dir_all(&store.root).ok();
    }

    #[test]
    fn says_when_the_program_is_still_missing() {
        let store = testing::store("still");
        let release = testing::seed_with(&store, "0.7.0", ">=1.3.0", Helper::Declared);
        let err = repair(&store, &release).unwrap_err().to_string();
        assert!(err.contains("an antivirus most likely removes it"), "{err}");
        std::fs::remove_dir_all(&store.root).ok();
    }

    #[test]
    fn prunes_downloads_no_release_needs() {
        let store = testing::store("prune");
        let keep = testing::seed(&store, "0.8.0", ">=1.4.0");
        let gone = testing::seed(&store, "0.7.0", ">=1.3.0");
        std::fs::write(store.root.join(CACHE_DIR).join(ASSET), b"left by 1.3.0").unwrap();
        prune(&store, &[keep.tag_name.as_str()]);
        assert!(cached(&store, &keep).is_file());
        assert!(!cached(&store, &gone).exists());
        assert!(!store.root.join(CACHE_DIR).join(ASSET).exists());
        std::fs::remove_dir_all(&store.root).ok();
    }
}
