# T117 run 2026-10-28-gp

- Date: 2026-10-28, run by car-lead with the user (one batched session for T112-T126).
- Build: Steam F1_25.exe sha256 d873a1c8d2044cef2fd76c99c9d5a97649c5b815d055540995105f46619865ae (the trace plugin hashed the exe: `result=match`).
- Loader profile `cartrace` with only the `re.car-trace` addon (`swofty_trace.dll` from `re/trace_plugin`, plugin version 1, 11 hooks from `re/tests/T112/T113/T119/T120/T121/T123.trace.json`, all installed, none rejected).
- What the user did: new Grand Prix, F1 2025, Ferrari / Leclerc, Australia, race only. The player stayed idle, so the game handed the car to the AI for the race. Podium, back to the menus, then the start of Braking Point 3 (the recap video).
- Snapshots: gdb reads of every car through g_Sess_VehicleTable with `work/exe/trace-session/carsnap.sh`, once on the grid (12:55:11) and once about 40 s into the race (12:56:12). Bink: `work/exe/trace-session/bink_trace.sh` (gdb breakpoints on the bink2w64.dll exports).
- Raw copies: `work/exe/lead/run/` (trace-1.log and trace-2.log are prefixes of trace-final.log, which is `trace.log` here).

## Files
- `snap-grid.txt`: car snapshot on the grid (engine idling, parked).
- `snap-race1.txt`: car snapshot about 40 s into the race.

## Citing lines
`trace:T117/2026-10-28-gp:<n>` is line n of the snapshots. Both files have the same layout, so line n is the same reading on the grid and in the race (7 engine and gear, 8 fuel tank, 9 fuel mode, 10 brakes, 11 aero outputs, 12 lift/drag scales, 13 DRS, 14 tyres, 15 chassis, 16 ERS, 19..39 one row per car id 0..20).

## Key lines
- 14: CarTyreState +0x88 per wheel, 0 on the grid, 0.0011..0.0029 after about 40 s.
- 19..39 column wear_avg per car.
