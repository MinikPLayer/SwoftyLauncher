mod build;
mod db;
mod doctor;
mod ea;
mod offline;
mod overlay;
mod overlay_exec;
mod postfx;
mod recover;
mod run;
mod send_logs;
mod setup;
mod time;
mod update;

use std::ffi::OsString;
use std::io::{IsTerminal, Write};
use std::path::{Path, PathBuf};
use std::process::ExitCode;

use anyhow::{Context, Result, bail};
use clap::{Arg, ArgAction, ArgMatches, CommandFactory, FromArgMatches, Parser, Subcommand};
use swofty_core::launcher::CommandSpec;
use swofty_core::manifest::{Binds, MANIFEST_FILE};
use swofty_core::registry::{Host, Registry};
use swofty_core::saves::SavesWarning;
use swofty_core::store::Store;

#[derive(Parser)]
#[command(
    name = "swofty",
    version,
    about = "Swofty Launcher for F1 25: addons, mods and tracks"
)]
struct Cli {
    #[arg(
        long,
        global = true,
        env = "SWOFTY_DATA_DIR",
        help = "Data directory (default: platform data dir)"
    )]
    data_dir: Option<PathBuf>,
    #[arg(long, global = true, help = "Profile (default: the active profile)")]
    profile: Option<String>,
    #[command(subcommand)]
    command: Command,
}

const GAME_HELP: &str = "Game folder (default: the active copy, see swofty game list)";

#[derive(Subcommand)]
enum Command {
    #[command(about = "Launch F1 25 modded and offline, streaming logs; use as the Steam launch option")]
    Run(run::RunArgs),
    #[command(
        hide = true,
        about = "Internal: mount the game's OverlayFS and run a command in it"
    )]
    OverlayExec {
        #[arg(num_args = 0.., trailing_var_arg = true, allow_hyphen_values = true)]
        args: Vec<OsString>,
    },
    #[command(about = "Resolve the profile and build its overlay")]
    Build {
        #[arg(long, help = GAME_HELP)]
        game: Option<PathBuf>,
        #[arg(
            long,
            help = "Also list every side-data finding of add-row copies: known lookups, matches to review, matches in other game areas and acknowledged ones"
        )]
        side_data: bool,
    },
    #[command(about = "Show which addons override which in the profile's last build")]
    Conflicts {
        #[arg(long, help = "Build the profile without installing it and show its conflicts")]
        check: bool,
        #[arg(long, help = GAME_HELP)]
        game: Option<PathBuf>,
    },
    #[command(
        about = "Build the profile and install it into the game folder (database, tracks); swofty restore undoes it"
    )]
    Apply {
        #[arg(long, help = GAME_HELP)]
        game: Option<PathBuf>,
    },
    #[command(about = "Report drift from the baseline and check the game files parse and round-trip")]
    Verify {
        #[arg(long)]
        game: Option<PathBuf>,
    },
    #[command(
        about = "Put the game folder and prefix back to the baseline: roll back journals, remove additions"
    )]
    Restore {
        #[arg(long)]
        game: Option<PathBuf>,
    },
    #[command(
        subcommand,
        alias = "mods",
        about = "List, install, enable and disable addons"
    )]
    Addons(AddonsCommand),
    #[command(
        subcommand,
        about = "Offline play: consent and the launch extension that runs sessions without EA AntiCheat"
    )]
    Offline(offline::OfflineCommand),
    #[command(
        subcommand,
        about = "The one-time setup before modded play: put the game folder back, verify it, then play"
    )]
    Setup(setup::SetupCommand),
    #[command(subcommand, about = "Show and change addon settings in the profile")]
    Settings(SettingsCommand),
    #[command(subcommand, about = "List and delete profiles")]
    Profile(ProfileCommand),
    #[command(about = "Browse the game database and edit DB Editor addons")]
    Db {
        #[arg(long, help = GAME_HELP)]
        game: Option<PathBuf>,
        #[command(subcommand)]
        cmd: db::DbCommand,
    },
    #[command(
        about = "Summarise a previous session's logs and crashes; `logs send` sends your logs to the developers"
    )]
    Logs {
        #[command(subcommand)]
        action: Option<LogsCommand>,
        dir: Option<PathBuf>,
        #[arg(long, env = "SWOFTY_EXE_COPY")]
        exe: Option<PathBuf>,
    },
    #[command(about = "Report what was discovered: Steam, the game, Proton, the launch option and the store")]
    Doctor,
    #[command(
        subcommand,
        about = "List the copies of F1 25 (Steam, EA app, added folders) and pick the one to play"
    )]
    Game(GameCommand),
    #[command(
        about = "Show whether Steam is running and signed in; --start starts it and waits, as Play does"
    )]
    Steam {
        #[arg(
            long,
            help = "Start Steam if it's closed and wait up to 90 s for it to sign in"
        )]
        start: bool,
    },
    #[command(
        subcommand,
        about = "Post-process passes: compile an addon's shaders to check them"
    )]
    Postfx(postfx::PostfxCommand),
    #[command(about = "Check for a new release and install it into this copy")]
    Update {
        #[arg(long, help = "Only report whether a new release is available")]
        check: bool,
    },
}

#[derive(Subcommand)]
enum LogsCommand {
    #[command(
        about = "Send the launcher and game logs to the developers on Discord, with your username and what went wrong"
    )]
    Send(send_logs::SendArgs),
}

#[derive(Subcommand)]
enum GameCommand {
    #[command(about = "List every copy of F1 25 found: store, version, folder, and the active one")]
    List,
    #[command(about = "Play this copy: steam, ea, or a game folder")]
    Use { copy: String },
    #[command(about = "Add a game folder (with F1_25.exe) and play it")]
    Add { path: PathBuf },
    #[command(
        about = "Start the EA app copy through the EA app as installed: no profile, no offline play, EA AntiCheat runs"
    )]
    Launch {
        #[arg(long, help = "Start F1 25 in VR (passes -vr)")]
        vr: bool,
        #[arg(long, help = "Don't start the EA app when it's closed")]
        no_start_store: bool,
        #[arg(long, help = crate::GAME_HELP)]
        game: Option<PathBuf>,
    },
}

#[derive(Subcommand)]
enum AddonsCommand {
    List,
    Install {
        path: PathBuf,
    },
    Enable {
        id: String,
    },
    Disable {
        id: String,
        #[arg(long, short, help = "Don't ask, even when the addon has affected your saves")]
        yes: bool,
    },
    #[command(about = "Delete an installed addon and take it out of every profile")]
    Remove {
        id: String,
        #[arg(long, short, help = "Don't ask, even when the addon has affected your saves")]
        yes: bool,
    },
}

#[derive(Subcommand)]
enum ProfileCommand {
    #[command(about = "List the profiles and mark the active one")]
    List,
    #[command(
        about = "Delete a profile with its settings, build cache and saves; the active profile switches first"
    )]
    Delete {
        #[arg(help = "The profile to delete")]
        name: String,
        #[arg(long, short, help = "Delete without asking")]
        yes: bool,
    },
}

#[derive(Subcommand)]
enum SettingsCommand {
    #[command(about = "List every addon's settings with the profile's values")]
    List { id: Option<String> },
    #[command(about = "Set an addon setting; the value is JSON (true, 5, \"replica\") or plain text")]
    Set {
        id: String,
        option: String,
        value: String,
    },
    #[command(about = "Put an addon's settings back to their defaults")]
    Reset { id: String },
    #[command(about = "Write the session settings file (swofty/settings.json) for the profile")]
    Export {
        #[arg(long, help = "File to write (default: stdout)")]
        out: Option<PathBuf>,
    },
    #[command(about = "Merge the changes made in game into the profile")]
    Merge,
}

fn main() -> ExitCode {
    let args: Vec<OsString> = std::env::args_os().collect();
    // Before anything that could start a thread: it makes a user namespace.
    if args.get(1).is_some_and(|a| a == "overlay-exec") {
        return overlay_exec::main(&args[2..]);
    }
    let store = data_dir_arg(&args)
        .map(Store::new)
        .unwrap_or_else(Store::open_default)
        .with_builtins(swofty_builtins::all());
    match run_cli(&store, args) {
        Ok(code) => code,
        Err(e) => {
            eprintln!("swofty: {e:#}");
            ExitCode::FAILURE
        }
    }
}

fn migrate(store: &Store) {
    match store.migrate() {
        Ok(notes) => notes.iter().for_each(|n| eprintln!("swofty: {n}")),
        Err(e) => eprintln!("swofty: data migration failed: {e}"),
    }
}

fn data_dir_arg(args: &[OsString]) -> Option<PathBuf> {
    args.iter().enumerate().find_map(|(i, a)| {
        if a == "--data-dir" {
            return args.get(i + 1).map(PathBuf::from);
        }
        a.to_str()?.strip_prefix("--data-dir=").map(PathBuf::from)
    })
}

struct AddonCommands {
    registry: Registry,
    specs: Vec<(String, CommandSpec)>,
}

impl AddonCommands {
    fn load(store: &Store) -> Self {
        let registry = store.registry().unwrap_or_else(|e| {
            eprintln!("swofty: {e}");
            Registry::default()
        });
        let reserved: Vec<String> = Cli::command()
            .get_subcommands()
            .map(|c| c.get_name().to_owned())
            .chain(["help".to_owned()])
            .collect();
        let reserved: Vec<&str> = reserved.iter().map(String::as_str).collect();
        let (specs, refused) = registry.commands(&reserved);
        refused.iter().for_each(|r| eprintln!("swofty: {r}"));
        Self { registry, specs }
    }

    fn provides(&self, name: &str) -> bool {
        self.specs.iter().any(|(_, s)| s.name == name)
    }

    fn command(&self) -> clap::Command {
        let mut cmd = Cli::command();
        if self.provides("mods") {
            cmd = cmd.mut_subcommand("addons", |c| c.alias(None));
        }
        self.specs
            .iter()
            .fold(cmd, |cmd, (_, spec)| cmd.subcommand(clap_command(spec)))
    }
}

fn clap_command(spec: &CommandSpec) -> clap::Command {
    let mut cmd = clap::Command::new(spec.name.clone()).about(spec.about.clone());
    for a in &spec.args {
        let arg = Arg::new(a.name.clone()).help(a.help.clone()).required(a.required);
        cmd = cmd.arg(if a.many {
            arg.num_args(1..).action(ArgAction::Append)
        } else {
            arg
        });
    }
    if !spec.subcommands.is_empty() && spec.args.is_empty() {
        cmd = cmd.subcommand_required(true);
    }
    spec.subcommands
        .iter()
        .fold(cmd, |cmd, s| cmd.subcommand(clap_command(s)))
}

fn command_input(m: &ArgMatches, spec: &CommandSpec, path: &mut Vec<String>, args: &mut Vec<String>) {
    for a in &spec.args {
        if let Some(values) = m.get_many::<String>(&a.name) {
            args.extend(values.cloned());
        }
    }
    if let Some((name, sub)) = m.subcommand()
        && let Some(s) = spec.subcommands.iter().find(|s| s.name == name)
    {
        path.push(name.to_owned());
        command_input(sub, s, path, args);
    }
}

/// Addon commands run in their own folder, so a value naming an existing
/// path relative to `cwd` is passed on as an absolute path.
fn absolute_paths(args: &mut [String], cwd: &Path) {
    for a in args {
        let at = cwd.join(a.as_str());
        if !a.is_empty()
            && Path::new(a.as_str()).is_relative()
            && at.exists()
            && let Some(abs) = std::path::absolute(&at)
                .ok()
                .and_then(|p| p.to_str().map(str::to_owned))
        {
            *a = abs;
        }
    }
}

fn command_at(args: &[OsString]) -> Option<usize> {
    let mut skip = false;
    args.iter().enumerate().skip(1).find_map(|(i, a)| {
        if std::mem::take(&mut skip) {
            return None;
        }
        let a = a.to_str()?;
        skip = a == "--data-dir" || a == "--profile";
        (!a.starts_with('-')).then_some(i)
    })
}

fn forward_maps(args: &mut [OsString]) {
    if let Some(at) = command_at(args).filter(|&at| args[at] == "maps") {
        args[at] = "mods".into();
    }
}

fn forward_mods(store: &Store, args: &[OsString]) -> Option<(Vec<OsString>, String)> {
    let at = command_at(args)?;
    if args[at] != "mods" {
        return None;
    }
    let verb = args.get(at + 1)?.to_str()?;
    let target = args.get(at + 2)?;
    let addon = match verb {
        "install" => Path::new(target).join(MANIFEST_FILE).is_file(),
        "enable" | "disable" => store.addons().ok()?.contains_key(target.to_str()?),
        _ => false,
    };
    if !addon {
        return None;
    }
    let mut forwarded = args.to_vec();
    forwarded[at] = "addons".into();
    Some((
        forwarded,
        format!("swofty mods {verb} is now swofty addons {verb}"),
    ))
}

fn run_cli(store: &Store, mut args: Vec<OsString>) -> Result<ExitCode> {
    let addons = AddonCommands::load(store);
    if addons.provides("mods") {
        forward_maps(&mut args);
    }
    if addons.provides("mods")
        && let Some((forwarded, note)) = forward_mods(store, &args)
    {
        eprintln!("swofty: {note}");
        args = forwarded;
    }
    let matches = addons.command().get_matches_from(args);
    migrate(store);
    if let Some((name, sub)) = matches.subcommand()
        && let Some((id, spec)) = addons.specs.iter().find(|(_, s)| s.name == name)
    {
        let profile = matches
            .get_one::<String>("profile")
            .cloned()
            .unwrap_or_else(|| store.active_profile());
        let (mut path, mut input) = (vec![name.to_owned()], Vec::new());
        command_input(sub, spec, &mut path, &mut input);
        if let Ok(cwd) = std::env::current_dir() {
            absolute_paths(&mut input, &cwd);
        }
        let entry = addons.registry.get(id).context("addon is gone")?;
        let p = store.profile(&profile)?;
        let game = build::game(store, None).ok();
        let host = Host::new(store, id, &p, game.as_ref())?;
        let out = entry.addon.command(&host.cx(), &path, &input)?;
        if !out.is_empty() {
            println!("{}", out.trim_end());
        }
        return Ok(ExitCode::SUCCESS);
    }
    dispatch(store, Cli::from_arg_matches(&matches)?)
}

fn dispatch(store: &Store, cli: Cli) -> Result<ExitCode> {
    let store = store.clone();
    let profile = cli.profile.unwrap_or_else(|| store.active_profile());
    match cli.command {
        Command::Run(args) => run::run(&store, &profile, args),
        Command::OverlayExec { args } => Ok(overlay_exec::main(&args)),
        Command::Build { game, side_data } => build::build(&store, &profile, game, side_data),
        Command::Conflicts { check, game } => build::conflicts(&store, &profile, check, game),
        Command::Apply { game } => build::apply(&store, &profile, game),
        Command::Verify { game } => build::verify(&store, game),
        Command::Restore { game } => build::restore(&store, game),
        Command::Addons(cmd) => addons(&store, &profile, cmd).map(|_| ExitCode::SUCCESS),
        Command::Offline(cmd) => offline::command(&store, cmd).map(|_| ExitCode::SUCCESS),
        Command::Setup(cmd) => setup::command(&store, cmd).map(|_| ExitCode::SUCCESS),
        Command::Settings(cmd) => settings(&store, &profile, cmd).map(|_| ExitCode::SUCCESS),
        Command::Profile(cmd) => profiles(&store, cmd).map(|_| ExitCode::SUCCESS),
        Command::Db { game, cmd } => db::command(&store, &profile, game, cmd),
        Command::Logs {
            action: Some(LogsCommand::Send(args)),
            ..
        } => send_logs::send(&store, args),
        Command::Logs { dir, exe, .. } => run::logs(&store, dir, exe),
        Command::Doctor => doctor::doctor(&store),
        Command::Game(cmd) => game(&store, cmd).map(|_| ExitCode::SUCCESS),
        Command::Steam { start } => run::steam_check(&store, start),
        Command::Postfx(cmd) => postfx::command(cmd),
        Command::Update { check } => update::update(check),
    }
}

fn game(store: &Store, cmd: GameCommand) -> Result<()> {
    use swofty_core::copies::{self, Choice};
    let mut config = store.config();
    match cmd {
        GameCommand::List => {
            let found = copies::discover(&config);
            let active = copies::active(&found, &config).map(|c| c.dir.clone());
            if found.is_empty() {
                println!("no copy of F1 25 found; add one with swofty game add <folder>");
            }
            for c in &found {
                let mark = if active.as_ref() == Some(&c.dir) { "*" } else { " " };
                let version = c.version().unwrap_or_else(|| "unknown".into());
                println!(
                    "{mark} {:7} {:14} {}",
                    copies::store_name(c.source),
                    version,
                    c.dir.display()
                );
            }
        }
        GameCommand::Use { copy } => {
            let found = copies::discover(&config);
            let c = Choice::parse(&copy)
                .pick(&found)
                .with_context(|| format!("no copy of F1 25 matches {copy}; see swofty game list"))?;
            let dir = c.dir.clone();
            copies::set_active(&mut config, &dir);
            store.save_config(&config)?;
            println!(
                "playing the {} copy in {}",
                copies::store_name(c.source),
                dir.display()
            );
        }
        GameCommand::Launch {
            vr,
            no_start_store,
            game,
        } => {
            let g = crate::build::game(store, game)?;
            crate::ea::launch_installed(store, &g, vr, no_start_store)?;
        }
        GameCommand::Add { path } => {
            let dir = std::path::absolute(&path)?;
            copies::add(&mut config, &dir).map_err(anyhow::Error::msg)?;
            store.save_config(&config)?;
            println!(
                "added {} ({}); it is now the active copy",
                dir.display(),
                copies::store_name(swofty_core::install::Source::detect(&dir))
            );
        }
    }
    Ok(())
}

fn addons(store: &Store, profile: &str, cmd: AddonsCommand) -> Result<()> {
    match cmd {
        AddonsCommand::List => {
            let p = store.profile(profile)?;
            let enabled: Vec<&str> = p.enabled().collect();
            for (id, m) in store.addons()? {
                let state = if m.is_builtin() {
                    "built in"
                } else if enabled.contains(&id.as_str()) {
                    "enabled"
                } else {
                    "disabled"
                };
                println!("{id:32} {:10} {state:9} {}", m.version.to_string(), m.name);
            }
            for u in store.unreadable_addons() {
                let version = u.version.as_deref().unwrap_or("?");
                println!(
                    "{:32} {version:10} {:9} {}: it {}",
                    u.id, "unusable", u.name, u.reason
                );
            }
        }
        AddonsCommand::Install { path } => {
            for result in store.install_addons(&path) {
                let m = result.with_context(|| format!("installing {}", path.display()))?;
                println!("installed {} {}", m.id, m.version);
                for line in swofty_core::tracks::left_out_lines(store, &m) {
                    println!("warning: {line}");
                }
            }
        }
        AddonsCommand::Enable { id } if let Some(u) = unreadable(store, &id) => {
            bail!("{}", u.line())
        }
        AddonsCommand::Disable { id, .. } if unreadable(store, &id).is_some() => {
            store.update_profile(profile, |p| p.set_enabled(&id, false))?;
            println!("disabled {id} in profile {profile}");
        }
        AddonsCommand::Remove { id, .. } if unreadable(store, &id).is_some() => {
            store.remove_addon(&id)?;
            println!("removed {id}");
        }
        AddonsCommand::Enable { id }
        | AddonsCommand::Disable { id, .. }
        | AddonsCommand::Remove { id, .. }
            if !store.addons()?.contains_key(&id) =>
        {
            bail!("{id} is not installed")
        }
        AddonsCommand::Enable { id } => toggle(store, profile, &id, true)?,
        AddonsCommand::Disable { id, yes } => {
            let p = store.profile(profile)?;
            if let Some(w) = store.registry()?.turn_off_warning(&p, &id) {
                confirm_saves(&w, "turn off", yes)?;
            }
            toggle(store, profile, &id, false)?
        }
        AddonsCommand::Remove { id, yes } => {
            if let Some(w) = store.addons()?.get(&id).and_then(SavesWarning::of) {
                confirm_saves(&w, "remove", yes)?;
            }
            store.remove_addon(&id)?;
            println!("removed {id}");
        }
    }
    Ok(())
}

fn unreadable(store: &Store, id: &str) -> Option<swofty_core::store::Unreadable> {
    store.unreadable_addons().into_iter().find(|u| u.id == id)
}

fn confirm_saves(w: &SavesWarning, verb: &str, yes: bool) -> Result<()> {
    if yes {
        return Ok(());
    }
    let (id, warning) = (&w.id, w.text());
    if !std::io::stdin().is_terminal() {
        bail!("{warning}\nstdin is not a terminal: pass --yes to {verb} {id} anyway");
    }
    println!("{warning}");
    let mut ask = verb.to_owned();
    ask[..1].make_ascii_uppercase();
    print!("{ask} {id} anyway? [y/N] ");
    std::io::stdout().flush().ok();
    let mut line = String::new();
    std::io::stdin().read_line(&mut line)?;
    if !matches!(line.trim(), "y" | "Y" | "yes") {
        bail!("{id} is unchanged");
    }
    Ok(())
}

fn profiles(store: &Store, cmd: ProfileCommand) -> Result<()> {
    match cmd {
        ProfileCommand::List => {
            let active = store.active_profile();
            for name in store.profile_names()? {
                let note = if name == active {
                    "active"
                } else if name == swofty_core::profile::VANILLA {
                    "built in"
                } else {
                    ""
                };
                let line = format!("{name:40} {note}");
                println!("{}", line.trim_end());
            }
        }
        ProfileCommand::Delete { name, yes } => {
            let plan = store.plan_profile_deletion(&name)?;
            if swofty_core::process::game_running() {
                bail!("close F1 25 before deleting a profile");
            }
            if !yes {
                confirm_delete(&plan)?;
            }
            let done = store.delete_profile(&name)?;
            if let Some(to) = &done.switch_to {
                println!("{to} is now the active profile");
            }
            println!("deleted profile {name}");
        }
    }
    Ok(())
}

fn confirm_delete(plan: &swofty_core::store::ProfileDeletion) -> Result<()> {
    let name = &plan.name;
    if !std::io::stdin().is_terminal() {
        bail!("stdin is not a terminal: pass --yes to delete {name} without asking");
    }
    println!(
        "Deleting {name} removes its choice of addons and mods, their settings{}.",
        if plan.saves {
            ", its build cache and its saves"
        } else {
            " and its build cache"
        }
    );
    println!("Installed addons and mods stay in the library, and other profiles don't change.");
    if let Some(to) = &plan.switch_to {
        println!("{name} is the active profile, so {to} becomes active first.");
    }
    print!("Delete profile {name}? [y/N] ");
    std::io::stdout().flush().ok();
    let mut line = String::new();
    std::io::stdin().read_line(&mut line)?;
    if !matches!(line.trim(), "y" | "Y" | "yes") {
        bail!("not deleted: {name} is unchanged");
    }
    Ok(())
}

fn toggle(store: &Store, profile: &str, id: &str, on: bool) -> Result<()> {
    let mut p = store.profile(profile)?;
    store.registry()?.set_enabled(&mut p, id, on)?;
    store.save_profile(&p)?;
    println!(
        "{} {id} in profile {profile}",
        if on { "enabled" } else { "disabled" }
    );
    Ok(())
}

fn settings(store: &Store, profile: &str, cmd: SettingsCommand) -> Result<()> {
    let addons = store.addons()?;
    let installed = |id: &str| addons.get(id).with_context(|| format!("{id} is not installed"));
    match cmd {
        SettingsCommand::List { id } => {
            let p = store.profile(profile)?;
            let pending = p.rebuild_pending(&addons);
            for m in addons
                .values()
                .filter(|m| id.as_deref().is_none_or(|i| i == m.id))
            {
                if m.options.is_empty() {
                    continue;
                }
                println!("{} ({})", m.name, m.id);
                for o in &m.options {
                    let v = p.option(m, o);
                    let mark = if pending.contains(&format!("{}.{}", m.id, o.id)) {
                        ", rebuild pending"
                    } else {
                        ""
                    };
                    let binds = match o.binds {
                        Binds::Native => "live",
                        Binds::Db => "rebuild",
                    };
                    println!("  {:24} {:16} {binds}{mark}", o.id, o.text(&v));
                }
            }
        }
        SettingsCommand::Set { id, option, value } => {
            let m = installed(&id)?;
            let value = serde_json::from_str(&value).unwrap_or(serde_json::Value::String(value));
            let mut p = store.profile(profile)?;
            let changed = p.set_option(m, &option, value)?;
            store.save_profile(&p)?;
            let o = m
                .options
                .iter()
                .find(|o| o.id == option)
                .expect("set_option checked it");
            println!(
                "{id}.{option} = {}{}",
                o.text(&p.option(m, o)),
                if !changed {
                    " (unchanged)"
                } else if o.binds == Binds::Db {
                    "; rebuild to apply"
                } else {
                    ""
                }
            );
        }
        SettingsCommand::Reset { id } => {
            let m = installed(&id)?;
            let mut p = store.profile(profile)?;
            p.reset_options(m)?;
            store.save_profile(&p)?;
            println!("{id}: settings back to their defaults in profile {profile}");
        }
        SettingsCommand::Export { out } => {
            let s = store.session_settings(profile)?;
            let text = serde_json::to_string_pretty(&s)? + "\n";
            match out {
                Some(path) => {
                    if let Some(dir) = path.parent().filter(|d| !d.as_os_str().is_empty()) {
                        std::fs::create_dir_all(dir)?;
                    }
                    std::fs::write(&path, text)?;
                    println!("wrote {}", path.display());
                }
                None => print!("{text}"),
            }
        }
        SettingsCommand::Merge => match store.merge_requests(profile, true)? {
            None => println!("no in-game changes for profile {profile}"),
            Some(m) => report_merge(&m),
        },
    }
    Ok(())
}

pub(crate) fn report_merge(m: &swofty_core::settings::Merge) {
    for c in &m.changed {
        println!("in game: {c}");
    }
    for s in &m.skipped {
        println!("in game, skipped: {s}");
    }
    if m.changed.is_empty() && m.skipped.is_empty() {
        println!("in game: no changes");
    }
}

#[cfg(test)]
mod tests {
    use std::sync::Arc;

    use swofty_core::launcher::{Addon, ArgSpec, Builtin};
    use swofty_core::manifest::AddonManifest;

    use super::*;

    struct Mods(AddonManifest);

    impl Addon for Mods {
        fn manifest(&self) -> &AddonManifest {
            &self.0
        }

        fn commands(&self) -> Vec<CommandSpec> {
            let spec = |name: &str, args: Vec<ArgSpec>| CommandSpec {
                name: name.into(),
                about: String::new(),
                args,
                subcommands: Vec::new(),
            };
            let paths = ArgSpec {
                name: "path".into(),
                help: String::new(),
                required: true,
                many: true,
            };
            vec![
                CommandSpec {
                    subcommands: vec![spec("list", Vec::new()), spec("import", vec![paths])],
                    ..spec("mods", Vec::new())
                },
                spec("build", Vec::new()),
            ]
        }
    }

    fn store(root: &Path) -> Store {
        let manifest = AddonManifest::parse(
            r#"{"schema_version":1,"id":"t.mods","name":"Mods","version":"1.0.0","author":"t","compat":{"game":"f1_25"}}"#,
        )
        .unwrap();
        Store::new(root).with_builtins(vec![Builtin {
            addon: Arc::new(Mods(manifest)),
            locked: false,
            default_on: true,
        }])
    }

    fn os(args: &[&str]) -> Vec<OsString> {
        args.iter().map(OsString::from).collect()
    }

    #[test]
    fn logs_takes_a_session_folder_or_send() {
        let parse = |args: &[&str]| Cli::try_parse_from(os(args)).map(|c| c.command);
        let Ok(Command::Logs {
            action: None, dir, ..
        }) = parse(&["swofty", "logs", "/tmp/s1"])
        else {
            panic!("logs <dir>");
        };
        assert_eq!(dir, Some(PathBuf::from("/tmp/s1")));
        assert!(matches!(
            parse(&["swofty", "logs"]),
            Ok(Command::Logs {
                action: None,
                dir: None,
                ..
            })
        ));
        let Ok(Command::Logs {
            action: Some(LogsCommand::Send(a)),
            dir: None,
            ..
        }) = parse(&[
            "swofty",
            "logs",
            "send",
            "--name",
            "swofty",
            "--message",
            "it crashed",
        ])
        else {
            panic!("logs send");
        };
        assert_eq!(
            (a.name.as_deref(), a.message.as_deref()),
            (Some("swofty"), Some("it crashed"))
        );
        assert_eq!(a.dry_run, None);
        let Ok(Command::Logs {
            action: Some(LogsCommand::Send(a)),
            ..
        }) = parse(&["swofty", "logs", "send", "--dry-run"])
        else {
            panic!("--dry-run");
        };
        assert_eq!(a.dry_run, Some(None));
        let Ok(Command::Logs {
            action: Some(LogsCommand::Send(a)),
            ..
        }) = parse(&["swofty", "logs", "send", "--name", "x1", "--dry-run", "out.zip"])
        else {
            panic!("--dry-run <zip>");
        };
        assert_eq!(a.dry_run, Some(Some(PathBuf::from("out.zip"))));
    }

    #[test]
    fn logs_send_dry_run_writes_the_zip() {
        let root = std::env::temp_dir().join(format!("swofty-cli-sendlogs-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let data = root.join("data");
        std::fs::create_dir_all(data.join("state")).unwrap();
        std::fs::write(data.join("state/launcher-run.log"), "hello\n").unwrap();
        std::fs::write(data.join("config.json"), r#"{"discord_username":"remembered"}"#).unwrap();
        let store = Store::new(&data);
        let out = root.join("out");
        std::fs::create_dir_all(&out).unwrap();
        let args = send_logs::SendArgs {
            name: None,
            message: Some("  it froze on the grid  ".into()),
            dry_run: Some(Some(out.clone())),
        };
        assert_eq!(send_logs::send(&store, args).unwrap(), ExitCode::SUCCESS);
        let written: Vec<_> = std::fs::read_dir(&out)
            .unwrap()
            .flatten()
            .map(|e| e.file_name())
            .collect();
        assert_eq!(written.len(), 1);
        let name = written[0].to_string_lossy().into_owned();
        assert!(
            name.starts_with("swofty-logs-remembered-") && name.ends_with(".zip"),
            "{name}"
        );
        let zip = zip_names(&out.join(&name));
        assert!(zip.contains(&"state/launcher-run.log".to_owned()), "{zip:?}");
        assert!(zip.contains(&"summary.txt".to_owned()));
        let bad = send_logs::SendArgs {
            name: Some("x".into()),
            message: None,
            dry_run: Some(None),
        };
        assert!(send_logs::send(&store, bad).is_err());
        let unsaid = send_logs::SendArgs {
            name: None,
            message: None,
            dry_run: Some(Some(out.join("unsaid.zip"))),
        };
        assert_eq!(send_logs::send(&store, unsaid).unwrap(), ExitCode::SUCCESS);
        assert!(out.join("unsaid.zip").is_file());
        let sending = |message: Option<&str>| send_logs::SendArgs {
            name: None,
            message: message.map(Into::into),
            dry_run: None,
        };
        let err = send_logs::send_to(&store, sending(None), None).unwrap_err();
        assert!(err.to_string().contains("pass --message"), "{err}");
        let err = send_logs::send_to(&store, sending(Some("  it froze ")), None).unwrap_err();
        assert!(err.to_string().contains("at least 10 characters"), "{err}");
        std::fs::remove_dir_all(&root).ok();
    }

    fn stub_log_server(code: u16) -> (String, std::thread::JoinHandle<usize>) {
        use std::io::Read;
        let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
        let url = format!("http://{}/v1/logs", listener.local_addr().unwrap());
        let handle = std::thread::spawn(move || {
            let (mut stream, _) = listener.accept().unwrap();
            stream
                .set_read_timeout(Some(std::time::Duration::from_secs(10)))
                .unwrap();
            let mut buf = Vec::new();
            let mut chunk = [0u8; 16 << 10];
            let end = loop {
                if let Some(head) = buf.windows(4).position(|w| w == b"\r\n\r\n") {
                    let text = String::from_utf8_lossy(&buf[..head]).to_ascii_lowercase();
                    let len: usize = text
                        .lines()
                        .find_map(|l| l.strip_prefix("content-length:"))
                        .unwrap()
                        .trim()
                        .parse()
                        .unwrap();
                    break head + 4 + len;
                }
                let n = stream.read(&mut chunk).unwrap();
                assert!(n > 0, "client closed early");
                buf.extend_from_slice(&chunk[..n]);
            };
            while buf.len() < end {
                let n = stream.read(&mut chunk).unwrap();
                assert!(n > 0, "client closed early");
                buf.extend_from_slice(&chunk[..n]);
            }
            write!(
                stream,
                "HTTP/1.1 {code} X\r\nContent-Type: application/json\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{{}}"
            )
            .unwrap();
            end
        });
        (url, handle)
    }

    #[test]
    fn logs_send_keeps_the_zip_in_the_data_folder() {
        let root = std::env::temp_dir().join(format!("swofty-cli-keeplogs-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let data = root.join("data");
        std::fs::create_dir_all(data.join("state")).unwrap();
        std::fs::write(data.join("state/launcher-run.log"), "hello\n").unwrap();
        let store = Store::new(&data);
        let kept = |store: &Store| -> Vec<PathBuf> {
            let mut zips: Vec<PathBuf> = std::fs::read_dir(swofty_core::send_logs::kept_dir(store))
                .map(|d| d.flatten().map(|e| e.path()).collect())
                .unwrap_or_default();
            zips.sort();
            zips
        };
        let args = |name: &str| send_logs::SendArgs {
            name: Some(name.into()),
            message: Some("it froze on the grid".into()),
            dry_run: None,
        };

        let (url, server) = stub_log_server(200);
        let hook = swofty_core::send_logs::LogServer::new(Some(&url));
        assert_eq!(
            send_logs::send_to(&store, args("sender"), hook).unwrap(),
            ExitCode::SUCCESS
        );
        assert!(server.join().unwrap() > 0);
        let zips = kept(&store);
        assert_eq!(zips.len(), 1, "{zips:?}");
        assert_eq!(zips[0].parent(), Some(data.join("logs").as_path()));
        let name = zips[0].file_name().unwrap().to_string_lossy().into_owned();
        assert!(
            name.starts_with("swofty-logs-sender-") && name.ends_with(".zip"),
            "{name}"
        );
        assert!(zip_names(&zips[0]).contains(&"state/launcher-run.log".to_owned()));
        assert_eq!(store.config().discord_username.as_deref(), Some("sender"));

        let (url, server) = stub_log_server(400);
        let hook = swofty_core::send_logs::LogServer::new(Some(&url));
        let err = send_logs::send_to(&store, args("refused"), hook).unwrap_err();
        assert!(err.to_string().contains("HTTP 400"), "{err}");
        server.join().unwrap();
        let zips = kept(&store);
        assert_eq!(zips.len(), 2, "a refused send keeps its zip too: {zips:?}");
        assert!(zips.iter().any(|z| {
            z.file_name()
                .unwrap()
                .to_string_lossy()
                .starts_with("swofty-logs-refused-")
        }));

        let err = send_logs::send_to(&store, args("nowhere"), None).unwrap_err();
        assert!(err.to_string().contains("--dry-run"), "{err}");
        assert_eq!(kept(&store).len(), 2);
        std::fs::remove_dir_all(&root).ok();
    }

    fn zip_names(path: &Path) -> Vec<String> {
        let data = std::fs::read(path).unwrap();
        let text = String::from_utf8_lossy(&data);
        [
            "summary.txt",
            "config.json",
            "addons.json",
            "state/launcher-run.log",
        ]
        .iter()
        .filter(|n| text.contains(*n))
        .map(|n| (*n).to_owned())
        .collect()
    }

    #[test]
    fn addon_commands_take_mods_and_forward_the_old_forms() {
        let root = std::env::temp_dir().join(format!("swofty-cli-commands-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let addon = root.join("my-addon");
        std::fs::create_dir_all(&addon).unwrap();
        std::fs::write(addon.join(MANIFEST_FILE), "{}").unwrap();
        let store = store(&root.join("data"));
        let addons = AddonCommands::load(&store);
        assert_eq!(addons.specs.len(), 1);
        let cmd = addons.command();
        assert_eq!(
            cmd.find_subcommand("addons").unwrap().get_all_aliases().count(),
            0
        );
        let m = cmd
            .try_get_matches_from(["swofty", "--profile", "p", "mods", "import", "a.serps", "b.zip"])
            .unwrap();
        let (name, sub) = m.subcommand().unwrap();
        let (mut path, mut input) = (vec![name.to_owned()], Vec::new());
        command_input(sub, &addons.specs[0].1, &mut path, &mut input);
        assert_eq!(path, ["mods", "import"]);
        assert_eq!(input, ["a.serps", "b.zip"]);
        std::fs::write(root.join("b.zip"), "zip").unwrap();
        absolute_paths(&mut input, &root);
        assert_eq!(input, ["a.serps", root.join("b.zip").to_str().unwrap()]);
        let mut dotted = vec!["./my-addon".to_owned(), "on".to_owned()];
        absolute_paths(&mut dotted, &root);
        assert_eq!(dotted, [addon.to_str().unwrap(), "on"]);
        let dir = addon.to_str().unwrap();
        let (args, note) =
            forward_mods(&store, &os(&["swofty", "--profile", "p", "mods", "install", dir])).unwrap();
        assert_eq!(args, os(&["swofty", "--profile", "p", "addons", "install", dir]));
        assert_eq!(note, "swofty mods install is now swofty addons install");
        assert!(forward_mods(&store, &os(&["swofty", "mods", "enable", "swofty.core"])).is_some());
        assert!(forward_mods(&store, &os(&["swofty", "mods", "enable", "rm.livery"])).is_none());
        assert!(forward_mods(&store, &os(&["swofty", "mods", "install", "pack.serps"])).is_none());
        assert_eq!(
            data_dir_arg(&os(&["swofty", "build", "--data-dir", "/d"])),
            Some("/d".into())
        );
        assert_eq!(
            data_dir_arg(&os(&["swofty", "--data-dir=/e", "build"])),
            Some("/e".into())
        );
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn mods_is_a_hidden_alias_of_addons() {
        for name in ["addons", "mods"] {
            let cli = Cli::try_parse_from(["swofty", name, "enable", "a.b"]).unwrap();
            assert!(matches!(cli.command, Command::Addons(AddonsCommand::Enable { id }) if id == "a.b"));
        }
        let cmd = Cli::command();
        let addons = cmd.find_subcommand("addons").unwrap();
        assert_eq!(addons.get_all_aliases().collect::<Vec<_>>(), ["mods"]);
        assert_eq!(addons.get_visible_aliases().count(), 0);
    }
}
