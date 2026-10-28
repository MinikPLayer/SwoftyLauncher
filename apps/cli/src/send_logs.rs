use std::path::{Path, PathBuf};
use std::process::ExitCode;

use anyhow::{Context, Result, anyhow, bail};
use clap::Args;
use swofty_core::send_logs::{self, Bundle, GameInfo, LogServer, PART_LIMIT, Report, Stage};
use swofty_core::store::Store;

#[derive(Args, Debug)]
pub struct SendArgs {
    #[arg(
        long,
        value_name = "USERNAME",
        help = "Your Discord username, so the developers can reply (default: the last one used)"
    )]
    pub name: Option<String>,
    #[arg(
        long,
        value_name = "TEXT",
        help = "What happened and what you were doing, 10 to 1000 characters (needed unless --dry-run)"
    )]
    pub message: Option<String>,
    #[arg(
        long,
        value_name = "ZIP",
        num_args = 0..=1,
        help = "Write the zip here (a file or a folder; default: the current folder) and list what it holds, without sending"
    )]
    pub dry_run: Option<Option<PathBuf>>,
}

pub fn send(store: &Store, args: SendArgs) -> Result<ExitCode> {
    send_to(store, args, send_logs::destination())
}

pub fn send_to(store: &Store, args: SendArgs, to: Option<LogServer>) -> Result<ExitCode> {
    let remembered = store.config().discord_username;
    let raw = args
        .name
        .or(remembered)
        .context("pass --name <your Discord username> so the developers can reply")?;
    let name = send_logs::check_name(&raw).map_err(|e| anyhow!(e))?;
    let message = match args.message.as_deref() {
        Some(raw) => send_logs::check_message(raw).map_err(|e| anyhow!(e))?,
        None if args.dry_run.is_some() => String::new(),
        None => bail!("pass --message \"<what happened>\" so the developers know what went wrong"),
    };
    if args.dry_run.is_none() && to.is_none() {
        bail!(
            "{}; add --dry-run to write the zip without sending it",
            send_logs::UNAVAILABLE
        );
    }
    if !send_logs::has_session(store) {
        eprintln!(
            "swofty: we don't see a game launch in your logs yet; start the game and reproduce the problem first, then send logs"
        );
    }
    eprintln!("swofty: {}", Stage::Collecting);
    let report = Report {
        name: name.clone(),
        message,
        game: GameInfo::detect(store),
        room: None,
    };
    let bundle = Bundle::collect(store, &report);
    if let Some(out) = args.dry_run {
        return dry_run(&bundle, out);
    }
    let kept = bundle.save(&send_logs::kept_dir(store), send_logs::KEPT);
    let sent = to
        .context(send_logs::UNAVAILABLE)?
        .send(&bundle, &|stage| eprintln!("swofty: {stage}"));
    match (&kept, &sent) {
        (Ok(zip), Ok(_)) => println!("{} ({})", zip.display(), send_logs::size(zip_len(zip))),
        (Ok(zip), Err(_)) => eprintln!(
            "swofty: the zip is at {}; you can send it yourself",
            zip.display()
        ),
        (Err(e), _) => eprintln!("swofty: could not keep a copy of the zip: {e}"),
    }
    sent.map_err(|e| anyhow!(e))?;
    let mut config = store.config();
    if config.discord_username.as_deref() != Some(name.as_str()) {
        config.discord_username = Some(name);
        store.save_config(&config)?;
    }
    Ok(ExitCode::SUCCESS)
}

fn zip_len(zip: &Path) -> u64 {
    std::fs::metadata(zip).map_or(0, |m| m.len())
}

fn dry_run(bundle: &Bundle, out: Option<PathBuf>) -> Result<ExitCode> {
    let out = match out {
        Some(p) if p.is_dir() => p.join(bundle.file_name()),
        Some(p) => p,
        None => PathBuf::from(bundle.file_name()),
    };
    let zip = bundle.zip()?;
    std::fs::write(&out, &zip).with_context(|| format!("writing {}", out.display()))?;
    println!("{} ({})", out.display(), send_logs::size(zip.len() as u64));
    for line in bundle.listing() {
        println!("  {line}");
    }
    for s in &bundle.skipped {
        println!("  left out: {s}");
    }
    let parts = bundle.parts(PART_LIMIT)?.len();
    if parts > 1 {
        println!("Sending it would take {parts} messages.");
    }
    if !send_logs::available() {
        println!("{}.", send_logs::UNAVAILABLE);
    }
    Ok(ExitCode::SUCCESS)
}
