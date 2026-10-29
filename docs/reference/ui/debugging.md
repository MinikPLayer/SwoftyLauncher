---
title: Debugging live
description: Attach gdb to F1 25 under Proton or Wine on Linux, log breakpoint hits without stopping the game, find who writes a value with hardware watchpoints, read memory without stopping it, and put the system back afterwards.
---

# Debugging live

On Linux, gdb attaches to F1 25 running under Proton or Wine and shows what the code does: which function builds a screen, how often, with which values, and who writes a field.

## A trace in full

The script logs every hit of one function and every write to one byte, and lets the game run on:

```text [trace.gdb]
set pagination off
set confirm off
handle all nostop noprint pass
handle SIGTRAP stop noprint nopass

set $hits = 0
break *0x14xxxxxxx
commands
silent
set $hits = $hits + 1
printf "[t] hit %d rcx=%#lx rdx=%#lx caller=%#lx\n", $hits, $rcx, $rdx, *(long *)$rsp
continue
end

watch -l *(unsigned char *)0x14xxxxxxx
commands
silent
printf "[t] write -> %d at %#lx\n", *(unsigned char *)0x14xxxxxxx, $pc
continue
end

continue
```

Attach it at a quiet screen, do the action you trace, and read the lines back:

```sh
pid=$(for p in $(pgrep -f F1_25.exe); do grep -q F1_25.exe /proc/$p/maps 2>/dev/null && echo $p && break; done)
prior=$(cat /proc/sys/kernel/yama/ptrace_scope)
sudo sysctl -w kernel.yama.ptrace_scope=0
timeout -s INT 900 gdb -p "$pid" --batch -x trace.gdb 2>&1 | tee trace.log
sudo sysctl -w kernel.yama.ptrace_scope="$prior"
grep '\[t\]' trace.log
```

For the methods these traces support, see [Techniques for menu edits](./techniques). For UI templates, `re/tools/ui_trace/ui_template_trace.sh` is a ready-made trace ([Live trace](./engine#live-trace)).

## Before you attach

- **Addresses.** The [Game API](/reference/game/) and the [RE knowledge base](https://github.com/Swofty-Developments/SwoftyLauncher/tree/main/re/db) give addresses for one build of `F1_25.exe`, with the image base `0x140000000`. swofty_core logs each hook it installs as `@<address> (base+<offset>)`. When an address there is `0x140000000` plus its offset, the game loaded at that base and the documented addresses are live. Otherwise, add each address's offset to the logged base.
- **When to attach.** Attach at a quiet screen, such as the main menu, before the action you trace. Some screens are built when a save loads, not when they open.
- **Hooked bytes.** A software breakpoint writes into the code, and so does a hook. On bytes that swofty_core or a plugin patches, use a hardware breakpoint (`hbreak`) instead. Break only in `.xtls` code, never in `.data1` bodies.

## Attach

```sh
pid=$(for p in $(pgrep -f F1_25.exe); do grep -q F1_25.exe /proc/$p/maps 2>/dev/null && echo $p && break; done)
prior=$(cat /proc/sys/kernel/yama/ptrace_scope)
sudo sysctl -w kernel.yama.ptrace_scope=0
timeout -s INT 900 gdb -p "$pid" --batch -x trace.gdb 2>&1 | tee trace.log
sudo sysctl -w kernel.yama.ptrace_scope="$prior"
```

- Proton starts several processes whose command line names `F1_25.exe`. The game is the one that maps the executable.
- With `ptrace_scope` at 1, the default on most distributions, gdb can't attach to a process it didn't start unless it runs as root. Set it to 0 for the session and put the old value back afterwards.
- `--batch` runs the script, then detaches when the script ends, when gdb gets `SIGINT` from `timeout`, or when the game exits. The game keeps running after a detach.
- If gdb dies while the game is stopped, the game stays frozen. Its state in `/proc/<pid>/stat` is `T`; resume it:

  ```sh
  awk '{print $3}' /proc/$pid/stat    # T: stopped
  kill -CONT "$pid"
  ```

## The script header

```text [trace.gdb]
set pagination off
set confirm off
handle all nostop noprint pass
handle SIGTRAP stop noprint nopass
```

Wine delivers Windows exceptions and its own events through signals such as `SIGSEGV` and `SIGUSR1`. `handle all nostop noprint pass` hands every signal to the game untouched. `SIGTRAP` is how gdb's breakpoints arrive: gdb stops on it and keeps it from the game.

## Log hits without stopping

A breakpoint whose commands end in `continue` works as a tracepoint: it prints and lets the game run on.

```text [trace.gdb]
set $hits = 0
break *0x14xxxxxxx
commands
silent
set $hits = $hits + 1
printf "[t] hit %d rcx=%#lx rdx=%#lx caller=%#lx\n", $hits, $rcx, $rdx, *(long *)$rsp
continue
end
continue
```

- At a function's first instruction, `*(long *)$rsp` is the return address: an address just after the call, inside the caller.
- Add a condition to log only the case you want:

  ```text
  break *0x14xxxxxxx if $r13 == 4
  ```

- Read memory in the commands:

  ```text
  x/4gx $rbx                                  # four qwords
  printf "%s\n", *(char **)($rsp+0x20)        # a string argument passed on the stack
  ```

- Keep the commands short. The game is stopped while they run.
- Put a fixed prefix such as `[t]` on every line, so `grep '\[t\]' trace.log` separates your lines from gdb's.

## Find who writes a value

A hardware watchpoint stops right after a write to an address. `$pc` is then just past the writing instruction:

```text [trace.gdb]
watch -l *(unsigned char *)0x14xxxxxxx
commands
silent
printf "[t] write -> %d at %#lx\n", *(unsigned char *)0x14xxxxxxx, $pc
continue
end
```

A watchpoint that never fires is a result too: it shows that the value doesn't change during the action you traced.

`hbreak *0x14xxxxxxx` is the hardware form of a breakpoint. x86 has four debug registers, which watchpoints and hardware breakpoints share.

## Read memory without stopping

With `ptrace_scope` at 0, `/proc/<pid>/mem` reads the game's memory while it runs:

```python [peek.py]
import sys
pid, addr, size = int(sys.argv[1]), int(sys.argv[2], 16), int(sys.argv[3])
with open(f'/proc/{pid}/mem', 'rb') as mem:
    mem.seek(addr)
    print(mem.read(size).hex())
```

```sh
python3 peek.py "$pid" 0x14xxxxxxx 16
```

The game keeps changing that memory as you read it, so compare a few reads before trusting a value.

## Afterwards

1. Put `ptrace_scope` back, and check that the game isn't left stopped.
2. Read `trace.log` next to `swofty/loader.log`: your addon's lines show what the hooks did around each hit.
3. Quit the game normally, so the session rolls back, then run `swofty verify`. See the [test loop](./techniques#test-loop).
