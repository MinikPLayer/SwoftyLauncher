---
title: Packaging and publishing
description: Build an addon without touching the game, test it in game, read the logs, package it as a .swoftyaddon and publish a release.
---

# Packaging and publishing

A finished addon is built and tested in a profile, then zipped as a `.swoftyaddon` file and attached to a release. The steps are the same for data addons and code addons:

```sh
# 1. build: checks every operation, the game folder is untouched
swofty addons install ./my-addon
swofty addons enable yourname.my-addon
swofty build

# 2. test in game
swofty run --apply

# 3. read the logs
swofty logs

# 4. package: swofty.json at the zip root
cd my-addon
zip -r ../yourname.my-addon.swoftyaddon .

# 5. publish: attach yourname.my-addon.swoftyaddon to a release
```

## 1. Build {#build}

Install the addon folder, enable it in a modded profile, then build the profile. The build checks every operation without touching the game folder:

```sh
swofty addons install ./my-addon
swofty addons enable yourname.my-addon
swofty build
```

```text
profile default: swofty.core -> … -> yourname.my-addon
warning: …
overlay 2ce84ff2c20476d4: $DATA/cache/overlay/default/2ce84ff2c20476d4
  replace game_package/gamedatabases/gamedatabases.2025.bin
```

`swofty build` applies the profile's operations and writes the result to `$DATA/cache/overlay/<profile>/<buildhash>/`. That folder holds `plan.json`, listing the game files the build replaces and adds with their sources, and the session's `swofty/settings.json`.

Read every line of output before you launch. It prints every `warn` message, each owned unlock row, skipped operations and each conflict. A conflict means a later addon replaced a target that an earlier addon wrote. Fix the warnings, then build again. See [`swofty build`](/reference/cli#swofty-build).

Installing the folder again replaces the copy in `$DATA/addons/<id>/`, so reinstall after each change.

## 2. Test in game {#test-in-game}

```sh
swofty run --apply
```

`swofty run --apply` installs your profile, starts the game and follows the logs. When the game exits, the game folder returns to its stock files. If the game crashes, run `swofty restore` to put them back.

1. Test with only your addon enabled.
2. Test with the addons and mods players combine it with.
3. Try every value of each setting. Changing a `db` setting rebuilds the profile, so build and relaunch after changing one.

## 3. Read the logs {#logs}

- **`<game>/swofty/loader.log`**: the in-game loader, your engine plugin's `swofty::log` lines, the runtime asset steps (`[assets]`) and the game's own warnings.
- **`$DATA/sessions/<id>/`**: one folder per session. `swofty logs` prints the latest summary.
- **`$DATA/cache/addons/<id>/helper.log`**: your launcher plugin's stderr.

An engine plugin that loads and initialises writes these lines to `loader.log`:

```text
addon my_plugin.dll: loading …\swofty\addons\yourname.my-addon\native\my_plugin.dll
addon my_plugin.dll: loaded at 00006fffea3e0000, calling SwoftyAddonInit (API v8)
addon my_plugin.dll: SwoftyAddonInit returned 1 (ok)
```

## 4. Package it {#package-it}

A `.swoftyaddon` is a zip of your addon folder's contents. Change into the folder before zipping it, so `swofty.json` sits at the zip root:

```sh
cd my-addon
zip -r ../yourname.my-addon.swoftyaddon .
```

A `.swoftyaddon` whose only entry is one folder holding `swofty.json` installs too.

Players drop the file on the launcher, or install and enable it from the command line:

```sh
swofty addons install yourname.my-addon.swoftyaddon
swofty addons enable yourname.my-addon
```

`swofty addons install` also takes a plain `.zip`, or the folder itself. A `.swoftymod` file from an earlier version installs too. A `.zip` that holds more than one addon installs from the launcher only:

```text
swofty: my-addons.zip holds 3 addons; drop it on the launcher to install them all
```

Before you zip:

1. Set `id`, `name`, `version`, `author` and `description` in [`swofty.json`](./manifest).
2. Set `compat.game_builds` and `compat.seasons` to the builds and seasons your addon was made for.
3. Remove every file that none of your operations name. Leave out build folders such as `target/`.
4. Ship your compiled binaries: the engine plugin DLL and one launcher plugin binary per OS in `launcher.program`.
5. Include a README crediting each author whose files you ship, and get their permission to redistribute those files. Never include the game's own archives, database or executable.

## 5. Publish {#publish}

1. Bump `version` in `swofty.json` for each release.
2. Tell players which game build and season you tested on, and check that `compat.game_builds` and `compat.seasons` match.
3. Credit every author whose files you include.
4. Attach the `.swoftyaddon` to your release.

To list the addon in the [Addon Browser](/using/addon-browser), publish the addons it [requires](./manifest#requires-conflicts-load-after) there first.

## Reference

- [Addon manifest](./manifest): every `swofty.json` field, including `compat`.
- [Command line](/reference/cli): `swofty build`, `swofty run`, `swofty logs`, `swofty restore` and `swofty addons`.
- [Logging](/reference/std/logging): writing to `swofty/loader.log` from an engine plugin.
- [`.serps` packages](/reference/mods/serps): sharing whole game files as a mod instead.
