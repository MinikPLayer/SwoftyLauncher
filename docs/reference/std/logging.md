---
title: Logging
description: Write to swofty/loader.log from a plugin with swofty::log, and configure the copy of the game's own log.
---

# Logging

You'll find everything the loader, swofty_core and plugins write in `swofty/loader.log` in the game folder. Watch it live in the launcher while the game runs, or use `swofty logs` for a summary of a past session's logs and crashes.

When the game can't create `swofty/loader.log`, the loader writes the log to `%LOCALAPPDATA%\Swofty\state\loader-fallback.log` instead. Its second line names the file it couldn't open and the Windows error, and the third the rights the game runs with: the user, the integrity level, whether it's elevated, UAC virtualization and `__COMPAT_LAYER`. The session copies it to its folder as `loader.log`.

| | |
|---|---|
| Header | `swofty/mixin.hpp` |
| Member | `SwoftyApi::log` |
| Feature | `log` |
| API version | 1 |
| Thread | Any |

## From a plugin

Call `swofty::log` with a `printf` format. You're sharing the log with the loader and other plugins, so prefix each line with your addon's name:

```cpp
#include <swofty/mixin.hpp>

SWOFTY_ON_LOAD
{
    swofty::log("[my-addon] loaded, game base 0x%llx", (unsigned long long)swofty::api->module_base());
}

SWOFTY_ADDON()
```

Wait until `SwoftyAddonInit` stores the API before logging; `swofty::log` writes nothing before then. If you're writing C, call `api->log(fmt, ...)`. You'll see the line in `swofty/loader.log`:

```text
[my-addon] loaded, game base 0x7ff6a4120000
```

## What the log holds

To trace what happened to your addon, look for:

- Plugins as they load, with the API version each receives: `addon my_plugin.dll: loaded at 0x..., calling SwoftyAddonInit (API v5)`.
- Signature scans and the hit count for each.
- Mixin chains as they install, including every conflict and dropped addon.
- Settings declared in code but missing from `swofty.json`.
- Addon screens that fail to build, with the engine's result code. With `SWOFTY_UI_DIAG=1`, each node the engine rejects, with a reason.
- Crashes, with the exception, registers and a stack named from `symbols.tsv`; calls to `ExitProcess` and `TerminateProcess` include the caller's stack.

## The game's own log

You'll also see the engine's own messages in `loader.log`. `swofty_gamelog.dll` copies warnings and errors from every channel. To also copy info from the UI, flow and ERP loader channels, add `verbose` to `channels`, as in `"default,verbose"`. To configure that copy, edit `swofty/session.json`:

```json [swofty/session.json]
"game_log": { "level": "info", "channels": "default", "rate": 100, "repeat": 100, "max_lines": 20000 }
```

| Field | Default | Meaning |
|---|---|---|
| `enabled` | `true` | Set to `false` to turn the copy off |
| `level` | `warn` | `off`, `error`, `warn` or `info` |
| `channels` | The default rules | Supply comma-separated channel patterns or a list of strings, each with an optional `:level`. `*` matches any text, `default` adds the default rules, `verbose` adds info from the UI, flow and ERP loader channels, `all` matches every channel |
| `rate` | 100 | Lines per second; lines beyond this limit are dropped and counted |
| `repeat` | 100 | How many times one format string is logged before further repeats are suppressed |
| `max_lines` | 20000 | Lines per session |

You can override those fields with environment variables. If you set both, the environment variable takes precedence:

| Variable | Field |
|---|---|
| `SWOFTY_GAME_LOG` | `level` |
| `SWOFTY_GAME_LOG_CHANNELS` | `channels` |
| `SWOFTY_GAME_LOG_RATE` | `rate` |
| `SWOFTY_GAME_LOG_REPEAT` | `repeat` |
| `SWOFTY_GAME_LOG_MAX` | `max_lines` |

To find the engine's channel names, look in `swofty/addons/core/log_channels.tsv`.

## Diagnostic switches

These environment variables change what swofty_core and the loader log. Set them for the game under `"env"` in `"launch"` in `$DATA/config.json`, as in [Advanced launch settings](/guide/linux#advanced-launch-settings).

| Variable | Effect |
|---|---|
| `SWOFTY_UI_DIAG=1` | Logs each node of an addon screen as it builds, and every node the engine rejects with a reason. `"diagnostics": true` in the `"ui"` block of `swofty/settings.json` does the same |
| `SWOFTY_LOG_SYNC=1` | Writes each `loader.log` line as it's logged. Without it, a background thread writes lines in batches and crash lines go out at once |
| `SWOFTY_STORAGE_LOG=all` | Logs every storage call. Without it, swofty_core logs the first three successful calls of each kind from each addon, then only failures |
| `SWOFTY_RESOLVE=scan` | Finds every binding by scanning for its pattern and leaves the scan cache off. Without it, swofty_core checks each pattern at its address on the analysed build first, and keeps the addresses it scanned for in `swofty/cache/scan-cache.tsv`, or in `%LOCALAPPDATA%\Swofty\cache\game\` when the game can't create files in its folder |
| `SWOFTY_PERF=1` | Adds `[perf]` lines: every screen open, each career flow from Entering to its next state, the game log's volume every 10 seconds, each Career Editor section pass, and every hitch in the performance report. Screen opens of 50 ms or more are logged without it |
| `SWOFTY_PROFILE=0` | Turns the performance report off |
| `SWOFTY_FRAME_CAPTURE=<folder>` | Writes the presented frame, overlay included, to `<folder>\frame-NNNN.png` every `SWOFTY_FRAME_CAPTURE_EVERY` seconds (default 10). Under Proton, give a Windows path such as `Z:\mnt\work\shots`. It arms the overlay's present hook even when no addon turns the overlay on, and captures only swapchains the overlay would draw on |
| `SWOFTY_OVERLAY_KILL=<list>` | Turns overlay parts off: `all`, `paint`, `capture` or `hdr`, comma-separated. `"overlay_kill"` in `swofty/assets.json` does the same |
| `SWOFTY_OVERLAY_NITS=<nits>` | The overlay's paper white on HDR10 and scRGB output, 80 to 1000. Default 350, the level of the game's own HUD white |
| `SWOFTY_KEEP_FOCUS=1` | Keeps the game running when its window loses focus: swofty_core drops `WM_KILLFOCUS`, `WM_ACTIVATE(WA_INACTIVE)` and `WM_ACTIVATEAPP(FALSE)` before the game's window procedure sees them, and hands a window that starts without focus one `WM_SETFOCUS`. A race in a parked or hidden window then runs instead of stopping at the pause menu. For test runs only |
| `SWOFTY_POSTFX_CAPTURE=<folder>` | For developing [post-process passes](/reference/addons/postfx). Writes the picture just before and just after the passes to `<folder>\postfx-NN-before.ppm` and `postfx-NN-after.ppm`, at most six pairs, ten seconds apart, the first thirty seconds after the passes start. The pictures have no HUD. Under Proton, give a Windows path |
| `SWOFTY_POSTFX=0` | Turns every post-process pass off for the session |
| `SWOFTY_PERF_HITCH_MS=<ms>` | The frame time the performance report counts as a hitch. Default 50 |
| `SWOFTY_PERF_SLOW_CALL_MS=<ms>` | The time a single hook or callback takes before the performance report lists it as a slow call. Default 8 |

## Performance report

swofty_core times every hook, site handler, event callback and storage call it runs, and writes the result to `swofty/perf.log` in the game folder every 15 seconds and when the game exits. Each timed call costs about 12 ns. After the game exits, `swofty run` keeps a copy in the session folder, and [Send Logs to Developers](/using/send-logs) sends it with the other logs. Every minute, a line such as this one goes to `loader.log`:

```text
[perf] last 60 s: 8640 frame(s) (144 fps, p99 9.8 ms, 2 over 50 ms); swofty 0.912 ms/s on game threads (core flow tick (events, career poll) 31.21 ms, swofty.career-editor on_frame 20.87 ms); worst call 1.20 ms (core ui screen open)
```

Swofty time is the time spent in swofty_core and addon code, without the game code that a hook calls. Frames are counted from the frame index the game passes to NVIDIA Streamline, and from the flow-state ticks while the game does not update it, as on the pause screen.

| Section | Contents |
|---|---|
| `== Summary` | Frame times and slow frames, swofty time in total and per second on game threads and on addon threads, the most swofty time in one frame, the slowest single call, the hitch count and the game's CPU use |
| `== Addons` | Calls, own time, time per second and slowest call for swofty_core and each addon |
| `== Hooks` | Every hook, callback and storage call: calls, total time, average, slowest call and calls per second |
| `== Hitches` | The latest frames over the hitch time, with the screen or flow state at the time and the swofty calls that ran in that frame |
| `== Slow calls` | The latest single calls over the slow call time, with the thread and the screen or flow state |
| `== Timeline` | Screens, flow states, sessions and career events in order. Each row runs until the next one, with its duration, frames, slow frames, slowest frame and swofty time |

A plugin can time its own work in the report through three functions that `swofty_core.dll` exports. Find them with `GetProcAddress`; a core without them has no report.

| Export | Use |
|---|---|
| `uint32_t SwoftyPerfProbe(const char *owner, const char *what)` | Returns the id of a probe. Pass `owner = nullptr` for the calling addon's id |
| `void SwoftyPerfEnter(uint32_t id)` | Starts timing the probe on this thread. Id 0 starts game code, which is not counted |
| `void SwoftyPerfLeave(void)` | Ends the latest `SwoftyPerfEnter` on this thread |
