# T112 run 2026-10-28-gp

- Date: 2026-10-28, run by car-lead with the user (one batched session for T112-T126).
- Build: Steam F1_25.exe sha256 d873a1c8d2044cef2fd76c99c9d5a97649c5b815d055540995105f46619865ae (the trace plugin hashed the exe: `result=match`).
- Loader profile `cartrace` with only the `re.car-trace` addon (`swofty_trace.dll` from `re/trace_plugin`, plugin version 1, 11 hooks from `re/tests/T112/T113/T119/T120/T121/T123.trace.json`, all installed, none rejected).
- What the user did: new Grand Prix, F1 2025, Ferrari / Leclerc, Australia, race only. The player stayed idle, so the game handed the car to the AI for the race. Podium, back to the menus, then the start of Braking Point 3 (the recap video).
- Snapshots: gdb reads of every car through g_Sess_VehicleTable with `work/exe/trace-session/carsnap.sh`, once on the grid (12:55:11) and once about 40 s into the race (12:56:12). Bink: `work/exe/trace-session/bink_trace.sh` (gdb breakpoints on the bink2w64.dll exports).
- Raw copies: `work/exe/lead/run/` (trace-1.log and trace-2.log are prefixes of trace-final.log, which is `trace.log` here).

## Files
- `trace.log`: the plugin log for the whole session (350 lines; same file in every trace-hook run of this session).
- `trace.json`: the runtime config built by `re/tools/trace_build.py` from the six authoring files.
- `loader-trace.txt`: the plugin lines of the session loader.log, each with its line number in the full log.

## Citing lines
`trace:T112/2026-10-28-gp:<n>` is line n of `trace.log`.

## Key lines
- 82: first replay, per-vehicle reset of id 19 (block "medium", perc 1.0, applied vector empty).
- 145..164: replay for ids 0..19 after the first all-vehicle reset (flag 0x1301); 164 id 19 with begin == end.
- 226..245: replay after the second all-vehicle reset (flag 0x0).
- 296, 303: replays after the per-vehicle resets of id 19 at race start and finish.
- 4: t112.section_call installed; no event line follows (never hit).
