use std::collections::BTreeMap;
use std::io::{self, ErrorKind};
use std::path::{Path, PathBuf};

use swofty_core::disk;
use swofty_core::launcher::Progress;
use swofty_core::process::{same_folder, same_path};

pub const ENV: &str = "SWOFTY_DATA_DIR";
pub const FOLDER: &str = if cfg!(windows) { "Swofty" } else { "swofty" };
const MOVING: &str = "Moving your data folder";
const COPYING: &str = "Copying your data folder";
const CHECKING: &str = "Checking the copied data folder";

#[derive(Debug, Clone, PartialEq)]
pub struct Data {
    pub default: PathBuf,
    pub env: Option<PathBuf>,
}

impl Data {
    pub fn find() -> Self {
        Self {
            default: swofty_core::paths::platform_data_dir(),
            env: std::env::var_os(ENV).filter(|v| !v.is_empty()).map(PathBuf::from),
        }
    }

    pub fn link(&self) -> Option<PathBuf> {
        link_target(&self.default)
    }

    pub fn current(&self) -> PathBuf {
        self.env
            .clone()
            .or_else(|| self.link())
            .unwrap_or_else(|| self.default.clone())
    }

    pub fn movable(&self) -> bool {
        self.env.as_ref().is_none_or(|e| cfg!(windows) && e.is_absolute())
    }

    pub fn describe(&self, to: &Path) -> Result<String, String> {
        if let Some(env) = &self.env
            && !self.movable()
        {
            return Ok(format!(
                "{ENV} sets the data folder to {}. Change the variable to move it.",
                env.display()
            ));
        }
        Ok(match plan(self, to)? {
            Some(m) => m.describe(),
            None => match (&self.env, self.link()) {
                (Some(_), _) => format!("Stays in {}, the folder {ENV} names.", to.display()),
                (None, Some(link)) => format!(
                    "Stays in {}. {} links there.",
                    link.display(),
                    self.default.display()
                ),
                (None, None) => format!("Stays in {}.", self.default.display()),
            },
        })
    }
}

pub fn pick(picked: PathBuf, data: &Data) -> PathBuf {
    let empty = std::fs::read_dir(&picked).is_ok_and(|mut d| d.next().is_none());
    let named = picked.file_name().is_some_and(|n| n.eq_ignore_ascii_case(FOLDER));
    if empty || named || same_folder(&picked, &data.current()) {
        picked
    } else {
        picked.join(FOLDER)
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
enum Record {
    Link,
    Unlink,
    Env,
}

#[derive(Debug, Clone, PartialEq)]
pub struct DataMove {
    from: PathBuf,
    to: PathBuf,
    default: PathBuf,
    linked: bool,
    record: Record,
}

pub fn plan(data: &Data, to: &Path) -> Result<Option<DataMove>, String> {
    let linked = data.link();
    let (from, record) = match (&data.env, &linked) {
        (Some(env), _) if same_folder(env, to) => return Ok(None),
        (Some(env), _) if !data.movable() => {
            return Err(format!(
                "{ENV} sets the data folder to {}. Change the variable to move it.",
                env.display()
            ));
        }
        (Some(env), _) => (env.clone(), Record::Env),
        (None, Some(target)) if same_path(to, &data.default) => (target.clone(), Record::Unlink),
        (None, Some(target)) if same_folder(to, target) => return Ok(None),
        (None, None) if same_folder(to, &data.default) => return Ok(None),
        (None, _) => (
            linked.clone().unwrap_or_else(|| data.default.clone()),
            Record::Link,
        ),
    };
    if !to.is_absolute() {
        return Err("Choose a full path for the data folder.".into());
    }
    let within = |dir: &Path| to.ancestors().any(|a| same_path(a, dir));
    if within(&from) || (record == Record::Link && within(&data.default)) {
        return Err(format!(
            "Choose a data folder outside {}.",
            if within(&from) { &from } else { &data.default }.display()
        ));
    }
    if record != Record::Unlink {
        vacant(to)?;
    }
    Ok(Some(DataMove {
        from,
        to: to.to_path_buf(),
        default: data.default.clone(),
        linked: linked.is_some(),
        record,
    }))
}

fn vacant(to: &Path) -> Result<(), String> {
    let Ok(meta) = to.symlink_metadata() else {
        return Ok(());
    };
    if !meta.is_dir() || meta.file_type().is_symlink() {
        return Err(format!(
            "{} is not a folder. Choose an empty folder or a new one.",
            to.display()
        ));
    }
    match std::fs::read_dir(to).map(|mut d| d.next().is_none()) {
        Ok(true) => Ok(()),
        Ok(false) => Err(format!(
            "{} is not empty. Choose an empty folder or a new one.",
            to.display()
        )),
        Err(e) => Err(format!("Could not read {}: {e}", to.display())),
    }
}

pub struct Ops {
    pub rename: fn(&Path, &Path) -> io::Result<()>,
    pub blocked: fn() -> Option<String>,
    pub free: fn(&Path) -> Option<u64>,
    pub set_env: fn(&Path) -> io::Result<()>,
}

impl Ops {
    pub fn system() -> Self {
        Self {
            rename: |a, b| std::fs::rename(a, b),
            blocked: crate::running::data_block,
            free: disk::free_space,
            set_env,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
enum Moved {
    Renamed,
    Copied,
    Created,
}

impl DataMove {
    pub fn env(&self) -> Option<&Path> {
        (self.record == Record::Env).then_some(self.to.as_path())
    }

    pub fn describe(&self) -> String {
        let (from, to, default) = (self.from.display(), self.to.display(), self.default.display());
        let fresh = self.from.symlink_metadata().is_err();
        match self.record {
            Record::Link if fresh => format!("Creates {to} for your data and links {default} there."),
            Record::Link => format!("Moves your data from {from} to {to} and links {default} there."),
            Record::Unlink => format!("Moves your data from {from} back to {default} and removes the link."),
            Record::Env => format!("Moves your data from {from} to {to} and sets {ENV} to {to}."),
        }
    }

    pub fn run(&self, ops: &Ops, progress: &dyn Progress) -> Result<(), String> {
        if let Some(why) = (ops.blocked)() {
            return Err(why);
        }
        progress.report(0.0, MOVING);
        match self.record {
            Record::Link => self.relink(ops, progress),
            Record::Unlink => self.unlink(ops, progress),
            Record::Env => self.set_env(ops, progress),
        }
    }

    fn relink(&self, ops: &Ops, progress: &dyn Progress) -> Result<(), String> {
        let moved = transfer(&self.from, &self.to, ops, progress)?;
        let aside = (!self.linked && moved == Moved::Copied).then(|| aside(&self.default));
        let cleared = if self.linked {
            unlink(&self.default)
                .map_err(|e| format!("Could not remove the link at {}: {e}", self.default.display()))
        } else if let Some(aside) = &aside {
            std::fs::rename(&self.default, aside)
                .map_err(|e| format!("Could not set {} aside: {e}", self.default.display()))
        } else {
            Ok(())
        };
        let linked = cleared.and_then(|()| {
            if let Some(parent) = self.default.parent() {
                std::fs::create_dir_all(parent).ok();
            }
            link(&self.to, &self.default).map_err(|e| {
                format!(
                    "Could not link {} to {}: {e}",
                    self.default.display(),
                    self.to.display()
                )
            })
        });
        if let Err(e) = linked {
            if self.default.symlink_metadata().is_err() {
                match &aside {
                    Some(aside) => std::fs::rename(aside, &self.default).ok(),
                    None if self.linked => link(&self.from, &self.default).ok(),
                    None => None,
                };
            }
            undo(moved, &self.from, &self.to);
            return Err(e);
        }
        if moved == Moved::Copied {
            discard(aside.as_deref().unwrap_or(&self.from), &self.to)?;
        }
        Ok(())
    }

    fn unlink(&self, ops: &Ops, progress: &dyn Progress) -> Result<(), String> {
        unlink(&self.default)
            .map_err(|e| format!("Could not remove the link at {}: {e}", self.default.display()))?;
        let moved = match transfer(&self.from, &self.to, ops, progress) {
            Ok(moved) => moved,
            Err(e) => {
                link(&self.from, &self.default).ok();
                return Err(e);
            }
        };
        if moved == Moved::Copied {
            discard(&self.from, &self.to)?;
        }
        Ok(())
    }

    fn set_env(&self, ops: &Ops, progress: &dyn Progress) -> Result<(), String> {
        let moved = transfer(&self.from, &self.to, ops, progress)?;
        if let Err(e) = (ops.set_env)(&self.to) {
            undo(moved, &self.from, &self.to);
            return Err(format!("Could not set {ENV} to {}: {e}", self.to.display()));
        }
        if moved == Moved::Copied {
            discard(&self.from, &self.to)?;
        }
        Ok(())
    }
}

fn transfer(from: &Path, to: &Path, ops: &Ops, progress: &dyn Progress) -> Result<Moved, String> {
    if from.symlink_metadata().is_err() {
        std::fs::create_dir_all(to).map_err(|e| format!("Could not create {}: {e}", to.display()))?;
        return Ok(Moved::Created);
    }
    if let Some(parent) = to.parent() {
        std::fs::create_dir_all(parent).map_err(|e| format!("Could not create {}: {e}", parent.display()))?;
    }
    let existed = to.symlink_metadata().is_ok();
    if existed {
        std::fs::remove_dir(to).map_err(|e| format!("Could not replace the empty {}: {e}", to.display()))?;
    }
    let restore = || {
        if existed {
            std::fs::create_dir(to).ok();
        }
    };
    match (ops.rename)(from, to) {
        Ok(()) => return Ok(Moved::Renamed),
        Err(e) if e.kind() == ErrorKind::CrossesDevices => {}
        Err(e) => {
            restore();
            return Err(format!(
                "Could not move {} to {}: {e}",
                from.display(),
                to.display()
            ));
        }
    }
    copy_verified(from, to, ops, progress).inspect_err(|_| {
        remove(to).ok();
        restore();
    })?;
    Ok(Moved::Copied)
}

fn copy_verified(from: &Path, to: &Path, ops: &Ops, progress: &dyn Progress) -> Result<(), String> {
    let total: u64 = listing(from)
        .map_err(|e| format!("Could not read {}: {e}", from.display()))?
        .values()
        .map(Entry::size)
        .sum();
    if let Some(free) = (ops.free)(to)
        && free < total
    {
        return Err(format!(
            "Not enough free space for the data folder in {}: it needs {} GB and {} GB is free.",
            to.display(),
            disk::gb(total),
            disk::gb(free)
        ));
    }
    progress.report(0.0, COPYING);
    let mut done = 0u64;
    copy_tree(from, to, &mut |n| {
        done += n;
        progress.report(done as f32 / total.max(1) as f32, COPYING);
    })
    .map_err(|e| format!("Could not copy {} to {}: {e}", from.display(), to.display()))?;
    progress.report(1.0, CHECKING);
    let same = listing(from)
        .ok()
        .zip(listing(to).ok())
        .is_some_and(|(a, b)| a == b);
    if !same {
        return Err(format!(
            "The copy in {} does not match {}. Your data stays in {}.",
            to.display(),
            from.display(),
            from.display()
        ));
    }
    Ok(())
}

fn undo(moved: Moved, from: &Path, to: &Path) {
    match moved {
        Moved::Renamed => std::fs::rename(to, from).ok(),
        Moved::Copied => remove(to).ok(),
        Moved::Created => std::fs::remove_dir(to).ok(),
    };
}

fn discard(old: &Path, to: &Path) -> Result<(), String> {
    remove(old).map_err(|e| {
        format!(
            "Moved your data folder to {}, but could not delete the old copy in {}: {e}. Delete it yourself.",
            to.display(),
            old.display()
        )
    })
}

fn aside(default: &Path) -> PathBuf {
    let name = default
        .file_name()
        .map_or_else(|| FOLDER.into(), |n| n.to_string_lossy().into_owned());
    (1..)
        .map(|i| match i {
            1 => default.with_file_name(format!("{name}.old")),
            i => default.with_file_name(format!("{name}.old-{i}")),
        })
        .find(|p| p.symlink_metadata().is_err())
        .expect("an unused name")
}

pub fn link_target(at: &Path) -> Option<PathBuf> {
    at.symlink_metadata()
        .ok()
        .filter(|m| m.file_type().is_symlink())?;
    let target = std::fs::read_link(at).ok()?;
    let target = match target.to_str().and_then(|s| s.strip_prefix(r"\\?\")) {
        Some(plain) => PathBuf::from(plain),
        None => target,
    };
    if target.is_absolute() {
        Some(target)
    } else {
        Some(at.parent()?.join(target))
    }
}

#[cfg(windows)]
fn link(target: &Path, at: &Path) -> io::Result<()> {
    crate::win32::junction(target, at)
}

#[cfg(not(windows))]
fn link(target: &Path, at: &Path) -> io::Result<()> {
    std::os::unix::fs::symlink(target, at)
}

fn unlink(at: &Path) -> io::Result<()> {
    std::fs::remove_dir(at).or_else(|_| std::fs::remove_file(at))
}

#[cfg(windows)]
fn set_env(to: &Path) -> io::Result<()> {
    crate::win32::set_user_env(ENV, to)
}

#[cfg(not(windows))]
fn set_env(_: &Path) -> io::Result<()> {
    Err(io::Error::other(format!("set {ENV} where you set it before")))
}

#[derive(Debug, PartialEq)]
enum Entry {
    Dir,
    File(u64),
    Link(PathBuf),
}

impl Entry {
    fn size(&self) -> u64 {
        match self {
            Entry::File(n) => *n,
            _ => 0,
        }
    }
}

fn listing(root: &Path) -> io::Result<BTreeMap<PathBuf, Entry>> {
    fn walk(path: &Path, rel: PathBuf, out: &mut BTreeMap<PathBuf, Entry>) -> io::Result<()> {
        let meta = std::fs::symlink_metadata(path)?;
        let entry = if meta.file_type().is_symlink() {
            Entry::Link(std::fs::read_link(path)?)
        } else if meta.is_dir() {
            for entry in std::fs::read_dir(path)? {
                let entry = entry?;
                walk(&entry.path(), rel.join(entry.file_name()), out)?;
            }
            Entry::Dir
        } else {
            Entry::File(meta.len())
        };
        out.insert(rel, entry);
        Ok(())
    }
    let mut out = BTreeMap::new();
    walk(root, PathBuf::new(), &mut out)?;
    Ok(out)
}

fn copy_tree(src: &Path, dst: &Path, copied: &mut dyn FnMut(u64)) -> io::Result<()> {
    let meta = std::fs::symlink_metadata(src)?;
    if meta.file_type().is_symlink() {
        copy_link(src, dst)
    } else if meta.is_dir() {
        std::fs::create_dir_all(dst)?;
        for entry in std::fs::read_dir(src)? {
            let entry = entry?;
            copy_tree(&entry.path(), &dst.join(entry.file_name()), copied)?;
        }
        Ok(())
    } else {
        std::fs::copy(src, dst)?;
        copied(meta.len());
        Ok(())
    }
}

#[cfg(windows)]
fn copy_link(src: &Path, dst: &Path) -> io::Result<()> {
    let target = std::fs::read_link(src)?;
    if std::fs::metadata(src).is_ok_and(|m| m.is_dir()) {
        crate::win32::junction(&target, dst)
    } else {
        std::os::windows::fs::symlink_file(&target, dst)
    }
}

#[cfg(not(windows))]
fn copy_link(src: &Path, dst: &Path) -> io::Result<()> {
    std::os::unix::fs::symlink(std::fs::read_link(src)?, dst)
}

fn remove(path: &Path) -> io::Result<()> {
    let meta = path.symlink_metadata()?;
    if meta.is_dir() && !meta.file_type().is_symlink() {
        std::fs::remove_dir_all(path)
    } else {
        unlink(path)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Mutex;

    struct Quiet;

    impl Progress for Quiet {
        fn report(&self, _: f32, _: &str) {}
    }

    fn temp(name: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!("swofty-installer-data-{name}-{}", std::process::id()));
        std::fs::remove_dir_all(&dir).ok();
        std::fs::create_dir_all(&dir).unwrap();
        dir
    }

    fn write(path: &Path, text: &str) {
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        std::fs::write(path, text).unwrap();
    }

    fn read(path: &Path) -> String {
        std::fs::read_to_string(path).unwrap()
    }

    fn seed(data: &Path) {
        write(
            &data.join("addons/classic-seasons/swofty.json"),
            "{\"id\":\"cs\"}",
        );
        write(&data.join("profiles/default.json"), "{}");
        write(&data.join("state/session.json"), "{\"a\":1}");
    }

    fn data(default: &Path) -> Data {
        Data {
            default: default.to_path_buf(),
            env: None,
        }
    }

    const fn ops(rename: fn(&Path, &Path) -> io::Result<()>) -> Ops {
        Ops {
            rename,
            blocked: || None,
            free: |_| None,
            set_env: |_| Ok(()),
        }
    }

    const SAME_VOLUME: Ops = ops(|a, b| std::fs::rename(a, b));
    const OTHER_DRIVE: Ops = ops(|_, _| Err(io::Error::from(ErrorKind::CrossesDevices)));

    fn moved_whole(to: &Path) {
        assert_eq!(
            read(&to.join("addons/classic-seasons/swofty.json")),
            "{\"id\":\"cs\"}"
        );
        assert_eq!(read(&to.join("profiles/default.json")), "{}");
        assert_eq!(read(&to.join("state/session.json")), "{\"a\":1}");
    }

    #[test]
    fn renames_the_data_folder_on_the_same_volume_and_links_the_default_to_it() {
        let dir = temp("rename");
        let (default, to) = (dir.join("Local/Swofty"), dir.join("D/SwoftyData"));
        seed(&default);
        let d = data(&default);
        let m = plan(&d, &to).unwrap().unwrap();
        assert_eq!(m.record, Record::Link);
        m.run(&SAME_VOLUME, &Quiet).unwrap();
        moved_whole(&to);
        assert_eq!(link_target(&default), Some(to.clone()));
        moved_whole(&default);
        assert_eq!(d.current(), to);
        assert!(!dir.join("Local/Swofty.old").exists());
        assert_eq!(plan(&d, &to).unwrap(), None, "nothing left to move");
        std::fs::remove_dir_all(&dir).ok();
    }

    #[cfg(unix)]
    #[test]
    fn copies_to_another_drive_checks_the_copy_then_deletes_the_old_folder() {
        let dir = temp("copy");
        let (default, to) = (dir.join("Local/Swofty"), dir.join("D/SwoftyData"));
        seed(&default);
        let game = dir.join("F1 25");
        write(&game.join("F1_25.exe"), "game");
        std::os::unix::fs::symlink(&game, default.join("state/game-link")).unwrap();
        std::fs::create_dir_all(&to).unwrap();
        plan(&data(&default), &to)
            .unwrap()
            .unwrap()
            .run(&OTHER_DRIVE, &Quiet)
            .unwrap();
        moved_whole(&to);
        assert_eq!(
            std::fs::read_link(to.join("state/game-link")).unwrap(),
            game,
            "links inside the data folder stay links"
        );
        assert_eq!(link_target(&default), Some(to.clone()));
        assert!(!dir.join("Local/Swofty.old").exists());
        assert_eq!(read(&game.join("F1_25.exe")), "game");
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn refuses_a_copy_that_does_not_fit() {
        let dir = temp("full");
        let (default, to) = (dir.join("Local/Swofty"), dir.join("D/SwoftyData"));
        seed(&default);
        let full = Ops {
            free: |_| Some(3),
            ..OTHER_DRIVE
        };
        let err = plan(&data(&default), &to)
            .unwrap()
            .unwrap()
            .run(&full, &Quiet)
            .unwrap_err();
        assert!(err.contains("Not enough free space"), "{err}");
        moved_whole(&default);
        assert!(link_target(&default).is_none());
        assert!(!to.exists());
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn re_points_an_existing_link_and_moves_the_data_from_its_old_target() {
        for (name, how) in [("repoint-rename", SAME_VOLUME), ("repoint-copy", OTHER_DRIVE)] {
            let dir = temp(name);
            let (default, old, new) = (dir.join("Local/Swofty"), dir.join("D/Old"), dir.join("E/New"));
            seed(&old);
            std::fs::create_dir_all(default.parent().unwrap()).unwrap();
            link(&old, &default).unwrap();
            let d = data(&default);
            assert_eq!(d.current(), old);
            assert_eq!(plan(&d, &old).unwrap(), None);
            plan(&d, &new).unwrap().unwrap().run(&how, &Quiet).unwrap();
            moved_whole(&new);
            assert_eq!(link_target(&default), Some(new.clone()), "{name}");
            assert!(!old.exists(), "{name}");
            moved_whole(&default);
            std::fs::remove_dir_all(&dir).ok();
        }
    }

    #[test]
    fn moving_back_to_the_default_removes_the_link() {
        for (name, how) in [("back-rename", SAME_VOLUME), ("back-copy", OTHER_DRIVE)] {
            let dir = temp(name);
            let (default, old) = (dir.join("Local/Swofty"), dir.join("D/Old"));
            seed(&old);
            std::fs::create_dir_all(default.parent().unwrap()).unwrap();
            link(&old, &default).unwrap();
            let m = plan(&data(&default), &default).unwrap().unwrap();
            assert_eq!(m.record, Record::Unlink);
            m.run(&how, &Quiet).unwrap();
            assert!(link_target(&default).is_none(), "{name}");
            assert!(default.symlink_metadata().unwrap().is_dir());
            moved_whole(&default);
            assert!(!old.exists(), "{name}");
            std::fs::remove_dir_all(&dir).ok();
        }
    }

    #[test]
    fn refuses_a_target_that_is_not_empty_or_inside_the_data_folder() {
        let dir = temp("refuse");
        let default = dir.join("Local/Swofty");
        seed(&default);
        let d = data(&default);
        let busy = dir.join("D/Busy");
        write(&busy.join("notes.txt"), "mine");
        let err = plan(&d, &busy).unwrap_err();
        assert!(err.contains("not empty"), "{err}");
        let file = dir.join("D/file");
        write(&file, "x");
        assert!(plan(&d, &file).unwrap_err().contains("not a folder"));
        let err = plan(&d, &default.join("addons/deeper")).unwrap_err();
        assert!(err.contains("outside"), "{err}");
        assert_eq!(plan(&d, &default).unwrap(), None);
        std::fs::create_dir_all(dir.join("D/Empty")).unwrap();
        assert!(plan(&d, &dir.join("D/Empty")).unwrap().is_some());
        assert_eq!(read(&busy.join("notes.txt")), "mine");
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn refuses_to_move_while_the_launcher_or_the_game_runs() {
        let dir = temp("running");
        let (default, to) = (dir.join("Local/Swofty"), dir.join("D/SwoftyData"));
        seed(&default);
        let busy = Ops {
            blocked: || {
                crate::running::holding(
                    &[crate::running::Running {
                        pid: 7,
                        part: "swofty-launcher",
                    }],
                    true,
                )
            },
            ..SAME_VOLUME
        };
        let err = plan(&data(&default), &to)
            .unwrap()
            .unwrap()
            .run(&busy, &Quiet)
            .unwrap_err();
        assert_eq!(
            err,
            "Close Swofty Launcher and F1 25 before moving the data folder, then select Try again."
        );
        moved_whole(&default);
        assert!(!to.exists());
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn leaves_the_data_where_it_was_when_the_move_fails() {
        let dir = temp("fail");
        let (default, to) = (dir.join("Local/Swofty"), dir.join("D/SwoftyData"));
        seed(&default);
        let locked = ops(|_, _| Err(io::Error::from(ErrorKind::PermissionDenied)));
        let err = plan(&data(&default), &to)
            .unwrap()
            .unwrap()
            .run(&locked, &Quiet)
            .unwrap_err();
        assert!(err.starts_with("Could not move"), "{err}");
        moved_whole(&default);
        assert!(link_target(&default).is_none());
        std::fs::remove_dir_all(&dir).ok();
    }

    static SET: Mutex<Option<PathBuf>> = Mutex::new(None);

    #[test]
    fn follows_swofty_data_dir_and_updates_it() {
        let dir = temp("env");
        let (default, old, new) = (dir.join("Local/Swofty"), dir.join("D/Old"), dir.join("E/New"));
        seed(&old);
        let d = Data {
            default: default.clone(),
            env: Some(old.clone()),
        };
        assert_eq!(d.current(), old);
        assert_eq!(plan(&d, &old).unwrap(), None);
        if !d.movable() {
            assert!(plan(&d, &new).unwrap_err().contains(ENV));
            assert!(d.describe(&new).unwrap().contains("Change the variable"));
            std::fs::remove_dir_all(&dir).ok();
            return;
        }
        let records = Ops {
            set_env: |to| {
                *SET.lock().unwrap() = Some(to.to_path_buf());
                Ok(())
            },
            ..SAME_VOLUME
        };
        let m = plan(&d, &new).unwrap().unwrap();
        assert_eq!(m.env(), Some(new.as_path()));
        m.run(&records, &Quiet).unwrap();
        moved_whole(&new);
        assert_eq!(SET.lock().unwrap().as_deref(), Some(new.as_path()));
        assert!(!default.exists(), "no link with {ENV}");
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn creates_the_chosen_folder_on_a_fresh_install() {
        let dir = temp("fresh");
        let (default, to) = (dir.join("Local/Swofty"), dir.join("D/SwoftyData"));
        let m = plan(&data(&default), &to).unwrap().unwrap();
        assert!(m.describe().starts_with("Creates"));
        m.run(&SAME_VOLUME, &Quiet).unwrap();
        assert!(to.is_dir());
        assert_eq!(link_target(&default), Some(to.clone()));
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn puts_a_swofty_folder_inside_a_busy_folder_it_is_given() {
        let dir = temp("pick");
        let d = data(&dir.join("Local/Swofty"));
        write(&dir.join("D/Games/notes.txt"), "x");
        assert_eq!(pick(dir.join("D/Games"), &d), dir.join("D/Games").join(FOLDER));
        std::fs::create_dir_all(dir.join("D/Empty")).unwrap();
        assert_eq!(pick(dir.join("D/Empty"), &d), dir.join("D/Empty"));
        assert_eq!(pick(dir.join("D").join(FOLDER), &d), dir.join("D").join(FOLDER));
        std::fs::remove_dir_all(&dir).ok();
    }
}
