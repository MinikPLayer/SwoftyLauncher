---
title: Logging
description: Write to swofty/loader.log from a plugin with swofty::log, and configure the copy of the game's own log.
---

# Logging

`swofty::log` writes a `printf`-formatted line to `swofty/loader.log` in the game folder, the one log the loader, swofty_core, the game's own messages and every plugin share.

```cpp [native/src/my_plugin.cpp]
#include <windows.h>

#include <swofty/game.hpp>
#include <swofty/mixin.hpp>

using swofty::game::FEHomeViewModel;
using swofty::game::NeStr;

namespace {

// Time your own work in swofty/perf.log (pattern from addons/career-editor/native/src/plugin.cpp)
struct Profiler {
    uint32_t (*probe)(const char *, const char *) = nullptr;
    void (*enter)(uint32_t) = nullptr;
    void (*leave)() = nullptr;
};

const Profiler &profiler()
{
    static const Profiler p = [] {
        Profiler out;
        HMODULE core = GetModuleHandleW(L"swofty_core.dll");
        if (!core)
            return out;
        out.probe = reinterpret_cast<uint32_t (*)(const char *, const char *)>(
            reinterpret_cast<void *>(GetProcAddress(core, "SwoftyPerfProbe")));
        out.enter = reinterpret_cast<void (*)(uint32_t)>(reinterpret_cast<void *>(GetProcAddress(core, "SwoftyPerfEnter")));
        out.leave = reinterpret_cast<void (*)()>(reinterpret_cast<void *>(GetProcAddress(core, "SwoftyPerfLeave")));
        if (!out.probe || !out.enter || !out.leave)
            out = Profiler{}; // a core without them has no report
        return out;
    }();
    return p;
}

int g_logged = 0;

} // namespace

SWOFTY_WRAP(FeHome_Handle_Command, uint64_t, FEHomeViewModel *vm, NeStr **cmd)
{
    static const uint32_t probe = profiler().probe ? profiler().probe(nullptr, "wrap FeHome_Handle_Command") : 0;
    if (probe)
        profiler().enter(probe);
    uint64_t r = orig(vm, cmd);
    if (probe)
        profiler().leave();
    if (g_logged++ < 8) // a hook can run every frame: cap what it logs
        swofty::log("[my-addon] home command -> %llu", (unsigned long long)r);
    return r;
}

SWOFTY_ON_LOAD
{
    // prefix every line with your addon's name: the log is shared
    swofty::log("[my-addon] loaded, game base 0x%llx", (unsigned long long)swofty::api->module_base());
}

SWOFTY_ADDON()
```

```json [swofty.json]
{
  "schema_version": 1,
  "id": "author.my-addon",
  "name": "My Addon",
  "version": "1.0.0",
  "author": "Your name",
  "kind": ["native"],
  "compat": { "game": "f1_25" },
  "requires": [{ "id": "swofty.core", "version": ">=0.1.0" }],
  "operations": { "native": [{ "dll": "native/my_plugin.dll" }] }
}
```

```text [swofty/loader.log]
2026-10-24 23:02:13.420 [  384] addon my_plugin.dll: loaded at 00006fffea2c0000, calling SwoftyAddonInit (API v8)
2026-10-24 23:02:13.421 [  384] [core] addon my_plugin.dll: registered 2 mixin(s)
2026-10-24 23:02:13.421 [  384] addon my_plugin.dll: SwoftyAddonInit returned 1 (ok)
2026-10-24 23:02:15.007 [  384] [my-addon] loaded, game base 0x140000000
2026-10-24 23:02:31.552 [  384] [my-addon] home command -> 1
```

Each line starts with the time and the thread id. Watch the log live in the launcher while the game runs, or run `swofty logs` for a summary of a past session's logs and crashes.

- **Header:** `swofty/mixin.hpp`
- **Table:** `SwoftyApi::log`, API version 1
- **Feature:** `log`
- **Thread:** any

## From a plugin

```cpp
template <class... A> void swofty::log(const char *fmt, A... a); // calls api->log(fmt, a...)
```

```cpp
swofty::log("[my-addon] %d car(s), best lap %.3f s", cars, best);
```

```c
api->log("[my-addon] loaded"); /* from C: the same call through the table */
```

`swofty::log` writes nothing until `SwoftyAddonInit` stores the API in `swofty::api`. Log from `SWOFTY_ON_LOAD` or later, not from a static constructor.

## The log file

The game writes `swofty/loader.log` in its own folder. When the game can't create that file, the loader writes to `%LOCALAPPDATA%\Swofty\state\loader-fallback.log` instead:

- The second line names the file it couldn't open and the Windows error.
- The third line gives the rights the game runs with: the user, the integrity level, whether it's elevated, UAC virtualization and `__COMPAT_LAYER`.
- The session copies it to its folder as `loader.log`.

The loader writes lines from a background thread in batches. Crash lines go out at once:

```text
log: asynchronous writer (batched, crash lines flush at once; SWOFTY_LOG_SYNC=1 writes each line synchronously)
```

## What the log holds

To trace what happened to your addon, look for these lines.

Plugins as they load, with the API version each was built against (the plugin's `SwoftyAddonApi`), and the result of init:

```text
addon progressive_back.dll: loaded at 00006fffea2c0000, calling SwoftyAddonInit (API v8)
addon progressive_back.dll: SwoftyAddonInit returned 1 (ok)
addon career_editor.dll: SwoftyAddonInit returned 0 (FAILED)
addon swofty_trace.dll: loaded at 00006fffea440000, API v1 plugin, SwoftyModInit runs after the mixin commit
```

Signature scans (`api->scan`) and the hit count for each. A scan writes its result only for exactly one hit:

```text
scan .data1 [0F B6 53 20 8D 42 F5 3C 06 77 08 48 89 F1 E8] -> 1 hit at 0x14fa4e018 (base+0xfa4e018)
```

Mixin chains as they install, including every conflict and dropped addon (see [Chains](./mixin#chains)):

```text
[core] wrap FeTile_Route_CareerItem at 0x141725620 (base+0x1725620) installed: swofty_core.dll (career tile route)(100) > progressive_suffix.dll(0)
[core] addon t_conflict_x.dll dropped: it conflicts with another addon (see the conflict line above); none of its mixins install
```

Settings declared in code but missing from `swofty.json` (see [Settings](./settings)):

```text
addon settings: greeting_count is not a slider option of this addon in swofty.json; the code default is used
```

Addon screens that fail to build, with the engine's result code. With `SWOFTY_UI_DIAG=1`, each node the engine rejects, with a reason:

```text
[ui] per-node screen build diagnostics off (set SWOFTY_UI_DIAG=1 or "diagnostics": true in the "ui" block to turn them on); the screen result hook stays on
[ui] screen swofty_addon_0 (flow_options): root 'persistent' built with result 0x0 (ok, the engine inserts it into the screen table next)
```

Crashes, with the exception, registers and a stack named from `symbols.tsv`. Calls to `ExitProcess` and `TerminateProcess` include the caller's stack:

```text
[crash] crash report swofty\crash-20261023-092347.txt: unhandled ACCESS_VIOLATION (0xc0000005) reading 0x978 at F1_25.exe+0x15436fda .data1 fn base+0x15436e70+0x16a; stack F1_25.exe+0x15436fda .data1 fn base+0x15436e70+0x16a < F1_25.exe+0x46b1829 .xtls fn base+0x46b1460+0x3c9 < F1_25.exe+0x4629740 .xtls fn base+0x4628190+0x15b0 < F1_25.exe+0x4637a08 .xtls Game_Check_MovingWingNodes+0x18; minidump crash-20261023-092347.dmp, 2.2 MB, thread stacks and registers, written in 2.5 s
[crash] ExitProcess(1) on thread 340, 11 s after load; caller stack:
```

## The game's own log

`swofty_gamelog.dll` copies the engine's own messages into `loader.log`: warnings and errors from every channel by default. To configure that copy, edit `swofty/session.json`:

```jsonc [swofty/session.json]
"game_log": {
  "enabled": true,          // false turns the copy off
  "level": "info",          // off, error, warn (the default) or info
  "channels": "default",    // comma-separated channel patterns, or a list of strings; see below
  "rate": 100,              // lines per second; lines beyond it are dropped and counted
  "repeat": 100,            // times one format string is logged before further repeats are suppressed
  "max_lines": 20000        // lines per session
}
```

`channels` takes patterns, each with an optional `:level`:

- `*` matches any text.
- `default` adds the default rules.
- `verbose` adds info from the UI, flow and ERP loader channels. `"default,verbose"` copies those at info as well.
- `all` matches every channel.

To find the engine's channel names, look in `swofty/addons/core/log_channels.tsv`. The copy logs its settings when it starts, and its limits as they hit:

```text
[gamelog] game log to loader.log: on (level info, from environment), channels 'default,GAME:info', 100 line(s)/s, 100 repeat(s) per format, cap 20000 line(s)
[game] (102 message(s) dropped by the rate limit of 100 per second)
[game] (line cap of 20000 reached; no further game messages are logged)
```

Environment variables override those fields. If you set both, the environment variable takes precedence:

| Variable | Field |
|---|---|
| `SWOFTY_GAME_LOG` | `level` |
| `SWOFTY_GAME_LOG_CHANNELS` | `channels` |
| `SWOFTY_GAME_LOG_RATE` | `rate` |
| `SWOFTY_GAME_LOG_REPEAT` | `repeat` |
| `SWOFTY_GAME_LOG_MAX` | `max_lines` |

## Diagnostic switches

These environment variables change what swofty_core and the loader log. Set them for the game under `"env"` in `"launch"` in `$DATA/config.json`, as in [Advanced launch settings](/guide/linux#advanced-launch-settings):

```jsonc [$DATA/config.json]
"launch": {
  "env": { "SWOFTY_UI_DIAG": "1", "SWOFTY_PERF": "1" }
}
```

- `SWOFTY_UI_DIAG=1`: logs each node of an addon screen as it builds, and every node the engine rejects with a reason. `"diagnostics": true` in the `"ui"` block of `swofty/settings.json` does the same.
- `SWOFTY_LOG_SYNC=1`: writes each `loader.log` line as it's logged. Without it, a background thread writes lines in batches and crash lines go out at once.
- `SWOFTY_STORAGE_LOG=all`: logs every storage call. Without it, swofty_core logs the first three successful calls of each kind from each addon, then only failures:

  ```text
  [storage] api: swofty.career-editor (career_editor.dll) append journal.jsonl (293 byte(s)) -> ok; further successful calls like this are not logged (failures still are; SWOFTY_STORAGE_LOG=all logs every call)
  ```

- `SWOFTY_RESOLVE=scan`: finds every binding by scanning for its pattern and leaves the scan cache off. Without it, swofty_core checks each pattern at its address on the analysed build first, and keeps the addresses it scanned for in `swofty/cache/scan-cache.tsv`, or in `%LOCALAPPDATA%\Swofty\cache\game\` when the game can't create files in its folder:

  ```text
  [core] binding resolution: 284 of 284 pattern(s) carry their KB address; each is checked there first and scanned only when the bytes differ
  [core] scan cache: 16 address(es) saved to swofty\cache\scan-cache.tsv
  ```

- `SWOFTY_PERF=1`: adds `[perf]` lines for every screen open, each career flow from Entering to its next state, the game log's volume every 10 seconds, each Career Editor section pass, and every hitch in the performance report. Screen opens of 50 ms or more are logged without it:

  ```text
  [perf] screen open 'audio_themes': 0.02 ms (addon screen registration 0.00 ms, engine open 0.02 ms)
  [perf] flow "#04fc8ffa" Entering -> Updating took 3.0 ms
  ```

- `SWOFTY_PROFILE=0`: turns the [performance report](#performance-report) off.
- `SWOFTY_PERF_HITCH_MS=<ms>`: the frame time the performance report counts as a hitch. Default 50.
- `SWOFTY_PERF_SLOW_CALL_MS=<ms>`: the time a single hook or callback takes before the performance report lists it as a slow call. Default 8.
- `SWOFTY_FRAME_CAPTURE=<folder>`: writes the presented frame, overlay included, to `<folder>\frame-NNNN.png` every `SWOFTY_FRAME_CAPTURE_EVERY` seconds (default 10). Under Proton, give a Windows path such as `Z:\mnt\work\shots`. It arms the overlay's present hook even when no addon turns the overlay on, and captures only swapchains the overlay would draw on:

  ```text
  [capture] wrote Z:\mnt\work\.f1-pfx\AA\frames\frame-0001.png (1908x1028, format 28, 39 KB, 24 ms off the render thread)
  ```

- `SWOFTY_OVERLAY_KILL=<list>`: turns overlay parts off: `all`, `paint`, `capture` or `hdr`, comma-separated. `"overlay_kill"` in `swofty/assets.json` does the same.
- `SWOFTY_OVERLAY_NITS=<nits>`: the overlay's paper white on HDR10 and scRGB output, 80 to 1000. Default 350, the level of the game's own HUD white.
- `SWOFTY_KEEP_FOCUS=1`: keeps the game running when its window loses focus. swofty_core drops `WM_KILLFOCUS`, `WM_ACTIVATE(WA_INACTIVE)` and `WM_ACTIVATEAPP(FALSE)` before the game's window procedure sees them, and hands a window that starts without focus one `WM_SETFOCUS`. A race in a parked or hidden window then runs instead of stopping at the pause menu. For test runs only.
- `SWOFTY_POSTFX_CAPTURE=<folder>`: for developing [post-process passes](/reference/addons/postfx). Writes the picture just before and just after the passes to `<folder>\postfx-NN-before.ppm` and `postfx-NN-after.ppm`, at most six pairs, ten seconds apart, the first thirty seconds after the passes start. The pictures have no HUD. Under Proton, give a Windows path:

  ```text
  [postfx] 2 pass(es) wait for the game's tonemap; captures go to Z:\mnt\work\.f1-pfx\A\cap
  ```

- `SWOFTY_POSTFX=0`: turns every post-process pass off for the session.

## Performance report

swofty_core times every hook, site handler, event callback and storage call it runs, and writes the result to `swofty/perf.log` in the game folder every 15 seconds and when the game exits. Each timed call costs about 12 ns. After the game exits, `swofty run` keeps a copy in the session folder, and [Send Logs to Developers](/using/send-logs) sends it with the other logs. Every minute, a line such as this one goes to `loader.log`:

```text
[perf] last 60 s: 10029 frame(s) (178 fps, p99 11.9 ms, 0 over 50 ms); swofty 1.417 ms/s on game threads (core flow tick (events, career poll) 60.69 ms, core game log (Ne_Log_Thunk) 23.59 ms); worst call 0.32 ms (swofty.progressive api storage read)
```

Swofty time is the time spent in swofty_core and addon code, without the game code that a hook calls. Frames are counted from the frame index the game passes to NVIDIA Streamline, and from the flow-state ticks while the game does not update it, as on the pause screen.

`swofty/perf.log` has six sections:

```text
== Summary
# frame times and slow frames, swofty time in total and per second on game threads and on addon threads,
# the most swofty time in one frame, the slowest single call, the hitch count and the game's CPU use
frame times: p50 31.12 ms, p95 1050.00 ms, p99 1050.00 ms, max 1008 ms
swofty time: 86.7 ms in total, 0.718 ms/s (0.072% of one core); game threads 0.718 ms/s, addon threads 0.000 ms/s
swofty per frame: max 6.12 ms; 20 frame(s) over 1 ms, 1 over 4 ms
worst call: 3.19 ms, core postfx SetName
game process cpu: 1.29 cores on average since the core attached (user 1.19, kernel 0.10)

== Addons
# calls, own time, time per second and slowest call for swofty_core and each addon
owner                                   calls      self ms       ms/s   worst ms  busiest
swofty.core                                99          1.3      0.010       0.38  site site_FeFlow_SetStateStatus

== Hooks
# every hook, callback and storage call: calls, total time, average, slowest call and calls per second
owner                        probe                                               calls     total ms     avg us   worst ms    calls/s
core                         game log (Ne_Log_Thunk)                            101612          9.0       0.09       0.75      841.4

== Hitches (last 64 of 125, frames over 50 ms)
# the latest frames over the hitch time, with the screen or flow state and the swofty calls in that frame
00:00:57      1001 ms  swofty    0.55 ms  during "screen loading"; core postfx SetName 0.46 ms, core postfx ResourceBarrier 0.07 ms

== Slow calls (last 0, single calls over 8 ms)
# the latest single calls over the slow call time, with the thread and the screen or flow state

== Timeline (last 20; each row runs until the next one)
# screens, flow states, sessions and career events in order, each with its duration, frames, slow frames,
# slowest frame and swofty time
at          took ms   frames   slow   worst ms  swofty ms  what
00:00:06        280       27      1         96       2.84  screen persistent; screen audio_themes; screen tutorials_masked  [core postfx SetName 1.22 ms, core ui screen loader handler 0.66 ms, core postfx ResourceBarrier 0.48 ms]
```

### Timing your own work

A plugin times its own work in the report through three functions that `swofty_core.dll` exports. Find them with `GetProcAddress`, as in the example at the top; a core without them has no report.

```cpp
uint32_t SwoftyPerfProbe(const char *owner, const char *what); // the id of a probe; owner nullptr is the calling addon's id
void SwoftyPerfEnter(uint32_t id);                             // starts timing the probe on this thread; id 0 starts game code, which is not counted
void SwoftyPerfLeave(void);                                    // ends the latest SwoftyPerfEnter on this thread
```

```cpp
static const uint32_t probe = profiler().probe("swofty.career-editor", "wrap Sess_GameWeatherSystem_SetupSession");
profiler().enter(probe);
// ... your work ...
profiler().leave();
```

The probe shows in `== Hooks` and in the `[perf]` line under its owner and name.
