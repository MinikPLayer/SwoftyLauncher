---
title: Implementation plan
description: How the 1.6.0 standard library gets built by parallel agents in rounds, with frozen contracts, one integration branch, independent review and a single in-game tester.
---

# Implementation plan

1.6.0 is built by several agents working at the same time, in rounds. One lead, the session that owns the release, integrates each piece as it lands. What 1.6.0 ships is on [Rollout and docs](/design/std-2/rollout#_1-6-0-the-registry-live-in-the-game).

## The rules every round follows

- **Contracts before code.** The lead writes and freezes the headers and schemas first. Every agent codes against them. A change to a contract goes through the lead, never through one agent's branch.
- **One branch, one PR.** All work lands on `feat/std-1.6` and its one draft PR. Each agent works in its own worktree on `std16/<stream>`, branched from `feat/std-1.6`, and the lead merges it in.
- **Each agent owns its files.** A brief lists the files an agent may change, and agents don't edit outside them.
- **Short rounds.** An agent gets about 75 minutes and one job. When it reports, the lead merges, runs the full tests, and either sends it on or gives it its next job, continuing its context.
- **Someone else checks the work.** A different agent reviews every stream's diff. Reverse-engineering rows get an independent verify from a different agent, as `re/PROTOCOL.md` requires.
- **One in-game tester.** Only the lead launches F1 25: with the game lock, never while swofty plays, and with in-game checks batched into one session per round.

## The workstreams

```text
W0  contracts (the lead, first)
├─ W1  swofty::json
│  └─ W2  schema check
│     └─ W3  registry core
│        ├─ W4  per-season snapshots
│        └─ W6  live apply paths                    (also needs W7)
│           ├─ W10 Progressive on the registry      (also needs W5)
│           ├─ W11 Classic Seasons on the registry
│           └─ W12 the what-if and engine sound examples
├─ W5  events, hooks and services
├─ W7  database sizing (RE, a 1.6.0 blocker)
├─ W8  launcher
├─ W9  recorder and Developer mode
├─ W13 compatibility gate
└─ W14 schema tooling and CI
W15 public docs and guides                          (needs W3 to W12 stable)
```

- **W0. Contracts** (the lead, first): `swofty_api.h` with the version 9 `SwoftyApi` and the registry, hooks and services tables and exports; `swofty/registry.hpp`, `events.hpp`, `hooks.hpp`, `services.hpp` and `json.hpp`; the 1.6.0 system headers (`swofty/systems/career.hpp`, `content.hpp`, `cars.hpp`, `sounds.hpp`) with full declarations and stubs that return `unavailable`; their schemas with examples; and `packages/sdk/std/tests/`, a shared corpus of bases, layers and expected effective values.
- **W1. `swofty::json`** (header-only): a value type, a parser, `dump`, JSON Pointer with the `slot=1` key form, and Merge Patch with keyed arrays and `$delete` for data files. Tests: the RFC 7396 vectors, the JSON test suite and the shared corpus.
- **W2. Schema check** (core): the schema subset with `x-key` and `x-ref`, run on every edit's result and every data file, plus `Registry::check`. Tests: every example in `packages/sdk/std/registries/*/examples`, and the must-fail ones.
- **W3. Registry core** (`native/src/core/registry/`): `SWOFTY_REGISTER` at the early stage; `add`, `variant` and `provide`; edits in load order with origins; selectors that run once per record; tags; `.in(...)` filters; `protect`; listing, watching and adapters; the C table and its exports; locking and the flow-thread wait during a career load; reading `swofty/registry/index.json` for addons without code. Tests: the shared corpus, randomized layer stacks checked against a small Python reference, and a read benchmark logged to `perf.log`.
- **W4. Per-season snapshots** (core): the `swofty_registry` save entry, spliced into the profile save outside the game's 3 MB heap; freezing each season's records at `career.season.start`; `career::Freeze::at_creation`; budgets; each career's id map; migration through adapters; keeping unknown majors. Tests: fixture saves from each version in `packages/sdk/std/fixtures/`.
- **W5. Events, hooks and services** (core): `events::on<E>()` with the context, selectors and phases, with a faulting `after` handler isolated and logged; the hooks table (`define`, `fire`, phases and priority); the game hook point machinery, which installs on the first subscriber and yields to an older addon's overwrite, tested on a harmless function (no game points ship in 1.6.0); services with `publish` and `when`.
- **W6. The core's live apply paths** (core): applying the 1.6.0 registries at their moments. `boot` records go into the database in memory before it loads. `content` changes apply when a box or career becomes active and are put back when it ends. Archives go through the asset manifest at boot, and loose files are redirected per open. It also covers the stat-id store (`ids.json`), separate budgets for older addons' database edits and registry records, a log line for every apply and put-back, and `effective.json` and `conflicts.json` at each session load and on a crash. It must agree with W3 on the shared corpus.
- **W7. Database sizing** (RE, then core; a 1.6.0 release blocker): find how to size the game's value map before it loads, with an independent verify, then size it from the records registered in `SWOFTY_REGISTER`. The safety net ships with it: a room report after each boot.
- **W8. Launcher** (Rust: `packages/core`, `apps/cli`, `apps/launcher`): `registry.json` (`requires`, `recommends`, `breaks`, `conflicts`, `guards`); each addon's `registry.lock.json`; the dependency checks at install and Play; writing `swofty/registry/index.json`; per-profile addon storage and career ids; the conflicts view and status lines from the core's reports; `effective.json` in Send Logs; moving older addons' database edits to the 1.5.11 file route when the room report says the database is full. The launcher applies nothing that addons register.
- **W9. Recorder and Developer mode** (`apps/cli`, `packages/sdk`): the recording core that runs an addon's `SWOFTY_REGISTER` (in-process on Windows, under Wine on Linux CI) and writes `registry.lock.json`; the `swofty dev` tools behind Developer mode; the fake core `swofty/fake_core.hpp` (`swofty/test.hpp` is the in-game test API of [Testing your addon](/std/testing)). It must agree with W3 on the shared corpus.
- **W10. Progressive on the registry** (`addons/progressive`): `SWOFTY_REGISTER` adds each season from `data/years.json`; a dynamic ratings provider; every DLL reads effective `career::seasons` and `career::ratings`; the `progressive.history` service and the `swofty.progressive.season.apply` point; the old exports kept.
- **W11. Classic Seasons on the registry** (`addons/classicseasons/`): its generators write each season's `content::packages`, `content::era_boxes`, `cars::vehicles` and `sounds::engine_sounds` records under its own keys, and its operations shrink to what those don't cover. Test: the database the core loads matches 0.6.x's built database, row for row.
- **W12. Examples:** `templates/std/whatif` (the launcher page and the profile layer from [The launcher side](/std/launcher)) and `templates/std/engine-sound` (a sound, a layer on the 2017 Ferrari, and a V10 variant with its own box). The docs and the first two guides include their code.
- **W13. Compatibility gate** (`tools/compat/`): downloads every Addon Browser addon version, builds each with the 1.5 and the 1.6 launcher, and compares the files byte for byte (the build hash and the core's version stamp differ by design). It runs the Wine harness to the main menu and compares the loaded DLLs, mixin chains and start lines. It runs 1.5 against 1.5 first, to prove it reports no false differences. It also runs published addons that use experimental APIs and reports to their authors.
- **W14. Schema tooling and CI:** `tools/std/schemacheck.py` (minor or major, immutable released files), example validation, building the `templates/std/` addons, the fixture-save job, and the compat gate in the release workflow.
- **W15. Public docs and guides:** the pages in [the docs plan](/design/std-2/rollout#the-docs) and the seven guides at `/make/`, written by section once the APIs stop moving.

## The rounds

**Round 0, the lead, about 2 hours.** Write W0, open `feat/std-1.6` and its draft PR, create the worktrees, and write the briefs. swofty reviews the contracts here, the cheapest moment to change them.

**Round 1, six agents:** W1 `swofty::json`; W2 the schema check, against W1's header; W5 events, hooks and services; W7 the database sizing RE; W13 the compat gate, with a 1.5 against 1.5 run; W14 schemacheck and CI. As each reports, the lead merges it and runs the native build, the host tests, `cargo test` and the Wine test. A reviewer agent goes over each merged stream.

**Round 2, six agents:** W3 the registry core; W8 the launcher; W9 the recorder and Developer mode; W10a Progressive's `SWOFTY_REGISTER`, compared against `years.json` record by record; the independent verify of W7's findings; a reviewer on round 1. Fixes from round 1's reviews go back to the agents that wrote the code.

**Round 3, six agents (W11 and W12 start as W6 merges):** W6 the live apply paths; W4 per-season snapshots; W7 the core's sizing; W10b Progressive's DLLs on effective values, the service and the point; a reviewer on round 2; W13's first full gate run on the 1.6 core.

**Round 4, in game** (the lead, one session, with the lock and a save backup):

- Progressive on the registry against 1.2 in a 2016 and a 2017 Follow-your-driver career: seats, swaps, ratings, logos, fuel, contracts;
- the what-if 2017 change on Choose Driver, the grid and in follow mode;
- a career saved in 2017, the layer changed, the career reloaded: it keeps its 2017 snapshot; a career in 2016 advanced to 2017 gets the change;
- Classic Seasons 2016 and 2017 in Time Trial on the registry: the same cars, liveries and sounds as 0.6.x;
- the engine sound on the 2017 Ferrari, and its "F1 2017 (V10)" box as its own entry;
- a profile past the stock database room boots with every record applied;
- a set of addons made for 1.5 and earlier from the gate's list, to the main menu and into a session.

Each failure becomes a fix brief for the agent that owns the code, and is checked again in the next in-game session.

**Round 5, docs and release:** three agents write the public docs by section (the registry and its subfeatures; the game systems, events, hooks, services and operations; the launcher, manifest and compatibility), and the guides are written alongside. A reviewer reads them against the code. The lead writes the release notes, runs the gate one last time, and asks swofty for the go-ahead to tag 1.6.0.

## A brief

Every agent gets a brief in this shape:

```text
Goal         W3: the registry core, as specified in docs/std/{registry,bases,providers,reading,layers}.md
Contracts    packages/sdk/include/swofty_api.h, packages/sdk/include/swofty/registry.hpp,
             packages/sdk/std/tests/ (the shared corpus). Do not change them; ask the lead.
You own      native/src/core/registry/**, native/test/registry/**
Don't touch  everything else; in particular native/src/core/stage.cpp (lead), the save code (W4)
             and the apply paths (W6)
Worktree     /mnt/work/VSC/F1ModManager-wt/std16-registry on std16/registry, from feat/std-1.6
Done when    the shared corpus passes; the randomized precedence test matches tools/std/reference.py for
             10,000 stacks; the C table answers through SwoftyApi; native/build.sh and native/test/run.sh
             pass; a read benchmark is in the report
Report       what you built, the test output, open questions, anything you'd change in the contracts
Rules        no game launches; no stash/reset in other worktrees; taskset -c 2-7,10,11,14-31;
             CARGO_TARGET_DIR under /mnt/work, deleted when you're done; Conventional Commits with no
             attribution (the pnpm PATH for the hook); push std16/registry; no PR of your own
Time         about 75 minutes, then report where you are
```

## Limits

- **Disk:** `/mnt/work` has about 40 GB free, and a Rust target directory takes about 11 GB. At most two Rust streams (W8, W9) build at once, each deletes its target directory when it reports, and native streams use zig, which needs little space.
- **CPU:** every build is pinned to the shared cores, with at most four heavy builds at a time.
- **Agents:** up to six at once per round.
- **The game:** one lock holder, and in-game checks only in the round 4 session and its follow-ups.

## Where it can go wrong

- **Per-season snapshots (W4):** the riskiest piece. It writes into every career save. It's built after the registry is stable, tested on fixture saves first, then in game with a save backup taken before each session.
- **Database sizing (W7):** 1.6.0 doesn't tag until the core sizes the table in game. The room report and the 1.5.11 file route ship with it either way.
- **Two implementations of the rules:** the core (W3) and the fake core and recorder (W9) can disagree. The shared corpus is the referee, and a disagreement fails both builds.
- **Contract churn:** if round 1 shows a contract is wrong, the lead changes it once, between rounds, and every brief is updated before round 2 starts.
- **Scope creep:** 1.6.0 ships everything except tracks. The tracks system, every base track in every fitting box and the track select hook wait for 1.6.x. Each gets its own plan in this shape, including the reverse-engineering streams with independent verifies.
