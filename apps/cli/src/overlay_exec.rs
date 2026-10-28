//! `swofty overlay-exec`: mount the game's OverlayFS in a private mount
//! namespace and run a command inside it. The wrapper starts this as a fresh
//! process, because `unshare(CLONE_NEWUSER)` fails in one that has threads.
//!
//! The namespace, and with it the mount, ends when the last process in it
//! exits, so there is nothing to recover after a crash and the real game
//! folder is never touched.

use std::ffi::OsString;
use std::path::PathBuf;
use std::process::ExitCode;

#[cfg(target_os = "linux")]
use swofty_core::overlayfs::{EXIT_CHDIR, EXIT_MAPS, EXIT_MOUNT, EXIT_SPAWN, EXIT_UNSHARE, MOUNTED};

/// What `overlay-exec` was asked to do.
#[derive(Debug, Default, PartialEq)]
pub struct Plan {
    /// Highest first.
    pub lower: Vec<PathBuf>,
    pub upper: PathBuf,
    pub work: PathBuf,
    pub target: PathBuf,
    pub check: bool,
    pub command: Vec<OsString>,
}

pub fn parse(args: &[OsString]) -> Result<Plan, String> {
    let mut plan = Plan::default();
    let mut it = args.iter();
    while let Some(a) = it.next() {
        let mut value = |name: &str| it.next().cloned().ok_or(format!("{name} needs a value"));
        match a.to_str() {
            Some("--lower") => plan.lower.push(value("--lower")?.into()),
            Some("--upper") => plan.upper = value("--upper")?.into(),
            Some("--work") => plan.work = value("--work")?.into(),
            Some("--target") => plan.target = value("--target")?.into(),
            Some("--check") => plan.check = true,
            Some("--") => {
                plan.command = it.cloned().collect();
                break;
            }
            _ => return Err(format!("unexpected argument {}", a.to_string_lossy())),
        }
    }
    if plan.lower.is_empty() || plan.upper.as_os_str().is_empty() || plan.work.as_os_str().is_empty() {
        return Err("--lower, --upper and --work are required".into());
    }
    if plan.target.as_os_str().is_empty() {
        return Err("--target is required".into());
    }
    if !plan.check && plan.command.is_empty() {
        return Err("a command after -- is required".into());
    }
    Ok(plan)
}

pub fn main(args: &[OsString]) -> ExitCode {
    match parse(args) {
        Ok(plan) => exec(plan),
        Err(e) => {
            eprintln!("swofty: overlayfs: {e}");
            ExitCode::from(2)
        }
    }
}

#[cfg(not(target_os = "linux"))]
fn exec(_: Plan) -> ExitCode {
    eprintln!("swofty: overlayfs: only available on Linux");
    ExitCode::from(swofty_core::overlayfs::EXIT_UNSHARE)
}

#[cfg(target_os = "linux")]
fn fail(code: u8, step: &str, e: &std::io::Error) -> ExitCode {
    eprintln!("swofty: overlayfs: {step}: {e}");
    ExitCode::from(code)
}

#[cfg(target_os = "linux")]
fn exec(plan: Plan) -> ExitCode {
    use std::io::Write;
    use std::os::unix::process::ExitStatusExt;
    use std::process::{Command, Stdio};

    if std::env::var_os(swofty_core::overlayfs::FAIL_ENV).is_some() {
        return fail(
            EXIT_MOUNT,
            "mount",
            &std::io::Error::other(format!("forced to fail by {}", swofty_core::overlayfs::FAIL_ENV)),
        );
    }
    for dir in [&plan.upper, &plan.work, &plan.target] {
        if let Err(e) = std::fs::create_dir_all(dir) {
            return fail(EXIT_MOUNT, &format!("create {}", dir.display()), &e);
        }
    }
    if let Err((step, code, e)) = enter(&plan) {
        return fail(code, step, &e);
    }
    let mut out = std::io::stdout();
    writeln!(out, "{MOUNTED}").and_then(|()| out.flush()).ok();
    if plan.check {
        return ExitCode::SUCCESS;
    }
    // The game's output goes where this process's stderr goes (the session's
    // log); stdout carried only the handshake.
    let log = std::io::stderr().as_fd().try_clone_to_owned();
    let mut cmd = Command::new(&plan.command[0]);
    cmd.args(&plan.command[1..]);
    if let Ok(log) = log {
        cmd.stdout(Stdio::from(log));
    }
    let mut child = match cmd.spawn() {
        Ok(c) => c,
        Err(e) => {
            return fail(
                EXIT_SPAWN,
                &format!("start {}", plan.command[0].to_string_lossy()),
                &e,
            );
        }
    };
    forward_signals(child.id());
    match child.wait() {
        Ok(status) => match (status.code(), status.signal()) {
            (Some(code), _) => ExitCode::from(code.clamp(0, 255) as u8),
            (None, Some(sig)) => ExitCode::from((128 + sig).clamp(0, 255) as u8),
            _ => ExitCode::FAILURE,
        },
        Err(e) => fail(EXIT_SPAWN, "wait", &e),
    }
}

#[cfg(target_os = "linux")]
use std::os::fd::AsFd;

#[cfg(target_os = "linux")]
static CHILD: std::sync::atomic::AtomicI32 = std::sync::atomic::AtomicI32::new(0);

/// A stop signal for the helper (the wrapper closing the game) goes to the
/// command it runs; the helper then ends with it.
#[cfg(target_os = "linux")]
fn forward_signals(child: u32) {
    use std::sync::atomic::Ordering;
    extern "C" fn pass(sig: libc::c_int) {
        let pid = CHILD.load(Ordering::Relaxed);
        if pid > 0 {
            unsafe {
                libc::kill(pid, sig);
            }
        }
    }
    CHILD.store(child as i32, Ordering::Relaxed);
    for sig in [libc::SIGINT, libc::SIGTERM, libc::SIGHUP] {
        unsafe {
            libc::signal(sig, pass as *const () as libc::sighandler_t);
        }
    }
}

/// A failed step: its name, the exit code and the error.
#[cfg(target_os = "linux")]
type Step = (&'static str, u8, std::io::Error);

/// Unshare, map the user to itself, mount, and enter the mount.
#[cfg(target_os = "linux")]
fn enter(plan: &Plan) -> Result<(), Step> {
    use std::ffi::CString;
    use std::os::unix::ffi::OsStrExt;

    let os = std::io::Error::last_os_error;
    let (uid, gid) = unsafe { (libc::getuid(), libc::getgid()) };
    if unsafe { libc::unshare(libc::CLONE_NEWUSER | libc::CLONE_NEWNS) } != 0 {
        return Err(("unshare", EXIT_UNSHARE, os()));
    }
    // An identity map, so Wine, Proton and Steam still see the real user.
    // Only the user's own id is mapped, which the kernel allows without
    // privileges once setgroups is denied.
    for (file, text) in [
        ("/proc/self/setgroups", "deny".to_owned()),
        ("/proc/self/uid_map", format!("{uid} {uid} 1")),
        ("/proc/self/gid_map", format!("{gid} {gid} 1")),
    ] {
        std::fs::write(file, text).map_err(|e| ("id maps", EXIT_MAPS, e))?;
    }
    let none = std::ptr::null::<libc::c_char>();
    if unsafe {
        libc::mount(
            none,
            c"/".as_ptr(),
            none,
            libc::MS_REC | libc::MS_PRIVATE,
            std::ptr::null(),
        )
    } != 0
    {
        return Err(("make mounts private", EXIT_MOUNT, os()));
    }
    let target = CString::new(plan.target.as_os_str().as_bytes())
        .map_err(|e| ("mount", EXIT_MOUNT, std::io::Error::other(e)))?;
    // Kernels before 5.11 have no unprivileged OverlayFS: the userxattr
    // option is refused (EINVAL), and the retry without it fails the same way
    // with the reason the player sees.
    let mut last = None;
    for userxattr in [true, false] {
        let data = swofty_core::overlayfs::options(&plan.lower, &plan.upper, &plan.work, userxattr);
        let data = CString::new(data).map_err(|e| ("mount", EXIT_MOUNT, std::io::Error::other(e)))?;
        let rc = unsafe {
            libc::mount(
                c"overlay".as_ptr(),
                target.as_ptr(),
                c"overlay".as_ptr(),
                0,
                data.as_ptr().cast(),
            )
        };
        if rc == 0 {
            last = None;
            break;
        }
        let e = os();
        let retry = e.raw_os_error() == Some(libc::EINVAL);
        last = Some(e);
        if !retry {
            break;
        }
    }
    if let Some(e) = last {
        return Err(("mount", EXIT_MOUNT, e));
    }
    // After the mount: before it, the working directory would be the empty
    // folder underneath.
    std::env::set_current_dir(&plan.target).map_err(|e| ("enter the mount", EXIT_CHDIR, e))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn os(args: &[&str]) -> Vec<OsString> {
        args.iter().map(OsString::from).collect()
    }

    #[test]
    fn parses_the_layers_and_the_command() {
        let plan = parse(&os(&[
            "--lower", "/c", "--lower", "/p", "--lower", "/g", "--upper", "/u", "--work", "/w", "--target",
            "/t", "--", "proton", "run", "--check",
        ]))
        .unwrap();
        assert_eq!(plan.lower, [PathBuf::from("/c"), "/p".into(), "/g".into()]);
        assert_eq!(
            (plan.upper, plan.work, plan.target),
            ("/u".into(), "/w".into(), "/t".into())
        );
        assert!(!plan.check);
        assert_eq!(plan.command, os(&["proton", "run", "--check"]));
    }

    #[test]
    fn a_check_needs_no_command() {
        let plan = parse(&os(&[
            "--lower", "/g", "--upper", "/u", "--work", "/w", "--target", "/t", "--check",
        ]))
        .unwrap();
        assert!(plan.check && plan.command.is_empty());
        assert!(
            parse(&os(&[
                "--lower", "/g", "--upper", "/u", "--work", "/w", "--target", "/t"
            ]))
            .is_err()
        );
        assert!(parse(&os(&["--lower"])).is_err());
        assert!(parse(&os(&["--what"])).is_err());
    }
}
