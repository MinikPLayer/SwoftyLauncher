# T113 run 2026-10-28-gp

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
`trace:T113/2026-10-28-gp:<n>` is line n of `trace.log`.

## Key lines
- 83/84: ApplyTuning on the per-vehicle reset of id 19 (ret 0x142f44f83), tuned values (bias 0.4, pressure 0.97, 10 kg fuel).
- 85..124: first setup apply for ids 0..19 (ret 0x140c0ab84); 86 = id 0 tuned values (pressure 1.0, 13.2 kg).
- 165..: 20 calls after the first all-vehicle reset (ret 0x142f44bf5); 246..: 20 after the second.
- 297, 304: per-vehicle resets of id 19 (ret 0x142f44f83); 299, 306, 314, 316, 321: id 19 via 0x140c0a63a, slider block sel alternating 0/1.
