# The compatibility gate

`tools/compat/` checks that a launcher under test builds every addon on the Addon Browser exactly as the reference launcher does. It is the first check of [the release gate](../../docs/std/compatibility.md#the-release-gate). Two `swofty` binaries go in: A, the reference (the 1.5 release), and B, the build under test. Each addon set is built with both, and the tool compares what each build would put in the game folder.

It uses only the Python standard library.

```sh
# 1.5 against 1.5: the same binary twice. This run reports no differences.
tools/compat/run.sh run --cli-a ~/SwoftyLauncher/bin/swofty --cli-b ~/SwoftyLauncher/bin/swofty

# 1.5 against a build of this branch, with the branch's loader (native/build.sh output).
# Build B as the release workflow does, with the launcher in the same cargo invocation.
cargo build --release -p swofty-cli -p swofty-launcher
tools/compat/run.sh run --cli-a ~/SwoftyLauncher/bin/swofty \
    --cli-b "$CARGO_TARGET_DIR/release/swofty" --loader-b native/dist --allow buildhash --allow loader

# Compare two saved snapshots again, for example after changing the comparison.
tools/compat/run.sh compare runs/<time>/snapshots/kizi.pitlane@0.3.1.a.json runs/<time>/snapshots/kizi.pitlane@0.3.1.b.json
```

## Commands

- `fetch` downloads addon versions into the cache. With no filter it fetches every published version (about 40 GB on 2026-10-28; the newest of each addon is about 7 GB). `--latest`, `--only ID[@VERSION]` and `--launcher-version` narrow it, and `--with-requirements` adds what each version requires.
- `run` fetches what it needs, builds every set with A and B, and writes the report. It exits 0 when every set is the same, 1 when any set differs, and 2 on a tool error (a download, a binary that doesn't start, a timeout, the disk).
- `compare A.json B.json` compares two saved snapshots, with the same exit codes.
- `recompare RUN` compares every snapshot pair of a finished run again and rewrites its `report.json` and `summary.txt`.
- `harness` is the Wine half of the gate. It exits 2 until it is built (see [The Wine harness](#the-wine-harness)).

`run` options:

- `--latest` builds only the newest version of each addon. Without it, every version whose `launcher` range accepts A's version is built.
- `--only ID[@VERSION]` (repeatable, or comma separated) picks addons.
- `--all-latest` also builds one profile with the newest version of every selected addon. `--profile NAME=ID[@V],ID[@V]` builds any other whole profile. `--no-per-version` skips the one-addon sets. `--exclude ID` leaves an addon out of every set.
- `--allow CATEGORY` reports a category of difference without failing on it. The categories are `step`, `build`, `buildhash`, `order`, `file`, `loader`, `plan`, `lines`, `warnings`, `db`, `parts`, `conflicts`, `waiting` and `generated`.
- `--jobs N` builds N sets at once (default 2). Each CLI call runs under `taskset -c 2-7,10,11,14-31 nice -n 10` unless `--no-pin` is given.
- `--keep` keeps every scratch data dir (`<out>/a/sNNNN`, `<out>/b/sNNNN`). Without it they are deleted once their snapshot is written.
- `--dxc PATH` is the shader compiler both sides use (default: the one bundled with A, else B).
- `--offline` uses the saved catalog. `--cache` moves the cache (default `~/.cache/swofty-compat`, on the root disk).
- `--min-free-gb` (default 10) stops a download or a set that would leave less free space.

## Fetching

The catalog comes from `GET https://api.f1.swofty.net/v1/addons`, the listing the Addon Browser reads (`packages/core/src/catalog`). The tool never publishes, stars or changes anything.

A version's `url` in the catalog is the registry's `/download` route, and every request to it adds one to that addon's public download count. The tool fetches from the CDN bucket instead: it lists `addons/<id>/<version>/` in the R2 bucket with the read-only token (`R2_READ_ACCESS_KEY_ID` and `R2_READ_SECRET_ACCESS_KEY`, from `--r2-env`, default `~/.config/swofty-dev/r2.env`, or the environment) and downloads the object from `https://cdn.swofty.net/<key>`. Every file is checked against the size and SHA-256 the catalog lists. A version that isn't in the bucket fails with a tool error, unless `--count-downloads` is given, which falls back to the registry's URL.

Downloads land in `<cache>/downloads/<id>/<version>/<sha256>.swoftyaddon` and are reused.

## A set

A set is one addon version with the newest published versions that satisfy its `requires`, or a whole profile. Requirements that aren't on the Addon Browser (`swofty.core`) are listed in the set's notes; the launcher provides them.

A run copies each side's binary and loader into `<out>/sides/<a|b>/` and builds with the copies. An update of the installed launcher during a run doesn't reach the builds.

For each set and each side, the tool:

1. makes a fresh data dir and fills its `baseline/` from `--baseline-from` (default `~/.local/share/swofty`): `baseline.json` files are copied, objects are hard-linked;
2. runs `swofty --data-dir D addons install ZIP` for every zip, then `--profile compat addons enable ID` for every addon;
3. runs `swofty --data-dir D --profile compat build --game G`.

Nothing else runs: no `apply`, no `run`. The game folder is read only. The tool takes a stat of every file in it before and after each set, and a change is written into that set's notes. A set whose builds differ while the folder changed is built again, up to `--retries` times (default 2), and is an `ERROR` ("inconclusive") when the folder changed during every attempt. Another session's `apply` changes the folder: the stock originals come from the baseline, but a file it adds, such as a Classic Seasons head, turns the same file from `add` into `replace` in the plan.

Every CLI call runs with `SWOFTY_USAGE=0` and the registry, logs and usage URLs pointed at `http://127.0.0.1:9`. The other `SWOFTY_*` variables are cleared, and `--loader-a` and `--loader-b` set `SWOFTY_LOADER_DIR`.

## What is compared

A snapshot of each build holds:

- every CLI step's exit code and output;
- the build hash, the load order, the build's lines and warnings, and its errors when it fails;
- every file the build puts in the game folder: each path in `plan.json`'s `replace`, `add` and `loader` maps, with its size, SHA-256 and source (`<loader>`, `<overlay>`, `<data>/addons/...`). Files from the loader dir are the `loader` category, apart from addon files;
- where each file comes from and how it's placed (the `plan` category);
- database counts: the `database:` lines, the `runtime:` line's counts, and the number of `db_edits` in `swofty/assets.json`;
- each addon's own outputs from `state/fingerprint-outputs.json` (the `parts` category);
- `built.json`'s conflicts, conflicts report, waiting operations and `registry_override`;
- the files written under `cache/addons/` and `addon-data/` during the build, by content (the `generated` category).

`report.json` holds every set's addons, notes, build hashes, differences and allowed differences. `summary.txt` has one line per set: `PASS`, `DIFF` with the first difference, or `ERROR` with the tool error, then each differing set's first 20 differences with both sides' values.

## Normalising

These differ between two builds of the same addon by the same binary, and the tool takes them out:

- **The data dir's path.** The build hash covers the session settings, which hold the profile's requests path inside the data dir. Both sides of a set build at the same path, `<out>/work/sNNNN/data`, one after the other.
- **When addon files were installed.** The build hash covers the size and modification time of every source file an operation reads. After the installs, every file and folder under `<data>/addons` gets the modification time 2026-01-01 00:00:00 UTC.
- **Paths in files and lines.** The data dir, the loader dir, the game folder, the cache and the home folder become `<data>`, `<loader>`, `<game>`, `<cache>` and `<home>`, in their POSIX, Wine (`Z:\...`) and JSON-escaped forms, in every file up to 64 MB and in every line. `<data>/cache/overlay/<profile>/<hash>` becomes `<overlay>`.
- **Build times.** `built_at` keys are dropped.
- **Files an addon's build program writes.** An addon with a `launcher.program` (Helmet Exchange) writes files into `cache/addons/<id>/` during the build, and the build hash covers their modification times. When every other part of a set is the same and those files have the same contents on both sides, the build hash difference is listed under `allowed` with the reason, and the set passes. JSON files among them are compared with their keys sorted: More Special Liveries' helper writes `liveries.json` with its keys in a different order on each run. When those files differ but every game file, plan entry, line, warning and count is the same, the `generated` and `buildhash` differences are listed under `allowed` with the reason: the same helper also picks a different track key for some liveries on each run (`catalog.track_keys`), and none of it reaches the game files.

## Between launcher versions

A `swofty` built with `cargo build -p swofty-cli` alone differs from the release's. The release builds `-p swofty-cli -p swofty-launcher` in one invocation, and the launcher's dependencies turn on serde_json's `preserve_order`. That changes how the operation files serialise, so every addon's entry in `state/fingerprint-outputs.json` (the `parts` category) differs while the game files are the same.

The build hash covers `swofty.core`'s version, its database operations, the loader's `features.json` and the pipeline id compiled into the binary. Two different launcher versions give different build hashes for every set, the core's files (`version.dll`, `swofty/addons/core/*`) differ, and `swofty/settings.json` lists the core's version. A 1.5 against 1.6 run reports these as `buildhash` and `loader` differences; `--allow buildhash --allow loader` reports them without failing, and every addon file, plan entry, line, warning, database count and per-addon output stays strict.

## CI

`python3 -m unittest discover -s tools/compat/tests -t tools/compat` runs on CI in the `Compatibility gate tool` job. The tests use a fake `swofty` (`tests/fake_swofty.py`) and need no game.

The gate itself can't run on a hosted runner: `swofty build` reads the game's database, UI archives and executable from a game folder, and the comparison needs the stock originals of a baseline. It runs on a machine with F1 25 installed.

## The Wine harness

Round 3 adds the second half, `tools/compat/run.sh harness`: each set's two builds start to the main menu, and the tool compares what the core loaded.

- **Input:** a `run --keep` output folder. For each set that passed, both sides' data dirs are there, with their `plan.json`.
- **The game folder:** a scratch folder per side holds copies of every file in the plan (never links into the real game folder) and `F1_25.exe`. Two levels:
  - *Synthetic*, CI capable: the synthetic `F1_25.exe` from `native/test/fake_game.c`, as in `native/test/run_wine_test.sh`, under `wine` and `xvfb-run`. The core loads, reads the overlay, registers each addon's mixins and runs its start code against the fake image. Addresses that only the real executable has are reported as unresolved, the same on both sides.
  - *In game*, the lead only: `swofty run` on the real game, under the game lock, one build after the other, quitting at the main menu.
- **Compared from `loader.log`:** the DLLs loaded, in order, with their stage; the DLLs dropped and why (no new drops on B); each mixin chain (target, addons in order, priorities, overwrite or wrap, and what the conflict pass dropped), identical apart from hook points that have a subscriber; each addon's start lines (`SWOFTY_ON_LOAD` output and the core's `[assets]` and `[career]` lines for it), after the same normalising as the build half plus timestamps, thread ids and pointer values.
- **Output:** the same report and exit codes, one line per set.
