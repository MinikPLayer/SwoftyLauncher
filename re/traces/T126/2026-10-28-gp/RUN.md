# T126 run 2026-10-28-gp

- Date: 2026-10-28, run by car-lead with the user (one batched session for T112-T126).
- Build: Steam F1_25.exe sha256 d873a1c8d2044cef2fd76c99c9d5a97649c5b815d055540995105f46619865ae (the trace plugin hashed the exe: `result=match`).
- Loader profile `cartrace` with only the `re.car-trace` addon (`swofty_trace.dll` from `re/trace_plugin`, plugin version 1, 11 hooks from `re/tests/T112/T113/T119/T120/T121/T123.trace.json`, all installed, none rejected).
- What the user did: new Grand Prix, F1 2025, Ferrari / Leclerc, Australia, race only. The player stayed idle, so the game handed the car to the AI for the race. Podium, back to the menus, then the start of Braking Point 3 (the recap video).
- Snapshots: gdb reads of every car through g_Sess_VehicleTable with `work/exe/trace-session/carsnap.sh`, once on the grid (12:55:11) and once about 40 s into the race (12:56:12). Bink: `work/exe/trace-session/bink_trace.sh` (gdb breakpoints on the bink2w64.dll exports).
- Raw copies: `work/exe/lead/run/` (trace-1.log and trace-2.log are prefixes of trace-final.log, which is `trace.log` here).

## Files
- `bink.log`: every BinkOpen / BinkGetTrackID / BinkOpenTrack / BinkCloseTrack / BinkClose call while the menus and the recap video played.

## Citing lines
`trace:T126/2026-10-28-gp:<n>` is line n of `bink.log`.

## Key lines
- 8..41: a UI wipe video: one track, index 0 -> id 0.
- 178: BinkOpen of videos/2025_recap/F125_cinematic_recap.bk2.
- 355: index 18 -> id 40 read by the selector 0x143f8b46c.
- 375..385: indexes 19..29 -> ids 41..51.
- 508, 510, 549: the opens kept for playback (caller 0x143f8b5d2): index 9 (id 20), index 0 (id 10), index 18 (id 40).
- 557: per-file summary: index:id table and the opened indexes [9, 0, 18, 18, 9, 0, 18].
