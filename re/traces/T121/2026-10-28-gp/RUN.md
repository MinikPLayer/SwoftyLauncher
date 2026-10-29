# T121 run 2026-10-28-gp

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
- `snap-grid.txt`: car snapshot on the grid (engine idling, parked).
- `snap-race1.txt`: car snapshot about 40 s into the race.

## Citing lines
`trace:T121/2026-10-28-gp:<n>` is line n of `trace.log`. The snapshot readings are cited without a line number and name the snapshot file and line in the text.

## Key lines
- 18, 21, ..., 78: t121.register, al = 0..20 in create order (78: id 20, the safety car).
- snap-grid.txt:4-5 and snap-race1.txt:4: 21 cars, every one with is_ai == 1 (the player's car included, AI-driven).
- snap-grid.txt:39 / snap-race1.txt:39: id 20 is the safety car (1734 kg, ERS none, 980 rpm).
