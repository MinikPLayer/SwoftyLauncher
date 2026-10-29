---
title: The mixin system
description: Hook an F1 25 function from a plugin - name a target, write a handler, register it with SwoftyMixin, and call the trampoline.
---

# The mixin system

A mixin hooks one place in `F1_25.exe` from an [engine plugin](/reference/addons/native-plugins): you name a binding and a handler, and swofty_core installs it once every plugin has loaded, chaining the handlers of every addon that hooks the same place.

## A plugin with two mixins

This plugin uses the C ABI in `swofty_api.h`, to show each step. It wraps `FeHome_Handle_Command`, which runs when you press a home-screen button, and runs code at `site_FeHome_AfterCareerCall`, the instruction right after the game adds the Career tile:

```c [native/src/my_plugin.c]
#include <swofty_api.h>

// The API version the plugin was built against. Without it the loader treats the DLL as an
// API v1 plugin: its init runs after the mixin commit, with no resolve and no register_mixins.
SWOFTY_EXPORT const uint32_t SwoftyAddonApi = SWOFTY_API_VERSION;

static const SwoftyApi *g_api;

// static: swofty_core writes the trampoline here, and it must outlive SwoftyAddonInit.
static uint64_t (*orig_handle)(void *vm, void *cmd);

// The game function's signature. Pointer parameters are void * at the C ABI;
// the Game API has their real types.
static uint64_t my_handle(void *vm, void *cmd)
{
    g_api->log("[my-addon] home command");
    return orig_handle(vm, cmd); // the next handler in the chain; the game's function is last
}

// A site handler gets the CPU registers. Here r14 holds the TileList *.
static void after_career(SwoftyContext *c)
{
    g_api->log("[my-addon] tile list at %p", (void *)c->r14);
}

SWOFTY_EXPORT int SwoftyAddonInit(const SwoftyApi *api)
{
    g_api = api;
    static SwoftyMixin mixins[] = {
        { "FeHome_Handle_Command",       SWOFTY_MIXIN_WRAP, 0, (void *)my_handle,    (void **)&orig_handle },
        { "site_FeHome_AfterCareerCall", SWOFTY_MIXIN_SITE, 0, (void *)after_career, NULL },
    };
    return api->register_mixins(mixins, 2); // 1 when the mixins register, 0 when a malformed mixin is refused
}
```

Name the DLL in `operations.native`:

```json [swofty.json]
"operations": {
  "native": [{ "dll": "native/my_plugin.dll" }]
}
```

Install the addon, enable it and launch the game. `swofty/loader.log` shows each installed chain:

```text
[core] wrap FeHome_Handle_Command at 0x1442f1a40 (base+0x2f1a40) installed: swofty_core.dll(0) > my_plugin.dll(0)
```

Both plugins are in the chain: swofty_core already wraps `FeHome_Handle_Command` for ADDON SETTINGS.

Real plugins use the [C++ Mixin API](/reference/std/mixin). Its macros check each handler's signature against the binding at compile time, and `SWOFTY_ADDON()` exports `SwoftyAddonInit` and `SwoftyAddonApi`. The same plugin:

```cpp [native/src/my_plugin.cpp]
#include <swofty/game.hpp>
#include <swofty/mixin.hpp>

using namespace swofty::game;

SWOFTY_WRAP(FeHome_Handle_Command, uint64_t, FEHomeViewModel *vm, NeStr **cmd)
{
    swofty::log("[my-addon] home command");
    return orig(vm, cmd);
}

SWOFTY_SITE(site_FeHome_AfterCareerCall, swofty::Context &c)
{
    swofty::log("[my-addon] tile list at %p", reinterpret_cast<void *>(c.r14));
}

SWOFTY_ADDON()
```

The C++ API also has `SWOFTY_VTABLE`, `SWOFTY_REDIRECT`, `SWOFTY_PATCH`, `SWOFTY_ON_LOAD`, typed bindings from the [Game API](/reference/game/), per-object [fields](/reference/std/mixin#fields) and [`subclass`](/reference/std/mixin#subclass).

## Targets

A target is a binding: a named place in `F1_25.exe` that swofty_core knows from `swofty/addons/core/bindings.json`. You pass its name as a string, and swofty_core scans the executable for its signature. The [Game API](/reference/game/) lists every binding with its kind and signature.

```c
uintptr_t at = api->resolve("FeHome_Handle_Command"); // the binding's resolved address, or 0

uintptr_t hit = 0;
int n = api->scan(".xtls", "48 89 5C 24 ?? 57 48 83 EC 20", &hit); // the hit count; hit is set only on exactly one match
```

- A target needs exactly one match to resolve. With zero or several, swofty_core drops your addon and installs none of its mixins. `swofty/loader.log` names the binding.
- swofty_core only knows the bindings in the `bindings.json` its launcher shipped. A binding added to the Game API after that launcher was built is missing from it, and swofty_core treats it as unresolved. Your addon then needs a launcher built with that binding:

```text
[core] addon my_plugin.dll needs Game_NewThing, which bindings.json does not list: this loader is older than the addon, or Game_NewThing is not an exported binding
[core] addon my_plugin.dll dropped: needs Game_NewThing, which bindings.json does not list; none of its mixins install
```

A binding that is listed but has no single match on the running build:

```text
[core] addon my_plugin.dll dropped: needs Game_NewThing, which does not resolve on this build; none of its mixins install
```

## Wrap a function

`SWOFTY_MIXIN_WRAP` replaces a `func` binding with your handler. swofty_core writes a trampoline into `orig`; calling it runs the next handler in the chain, and the game's function sits at the end.

```c
static uint64_t (*orig_handle)(void *vm, void *cmd);  // static, so the trampoline outlives SwoftyAddonInit

static uint64_t my_handle(void *vm, void *cmd)          // the same signature as the game's function
{
    if (skip_this_command(cmd))
        return 0;                                       // no orig call: the rest of the chain and the game's code don't run
    return orig_handle(vm, cmd);
}
```

`register_mixins` works only inside `SwoftyAddonInit`. Called anywhere else, swofty_core refuses it:

```text
[core] register_mixins outside SwoftyAddonInit; refused
```

A wrap that never calls `orig` is an overwrite. Mark it with `SWOFTY_MIXIN_OVERWRITE` (see [Chains](#chains)):

```c
{ "FeHome_Handle_Command", SWOFTY_MIXIN_WRAP | SWOFTY_MIXIN_OVERWRITE, 0, (void *)my_handle, (void **)&orig_handle },
```

## Run code at an instruction

A site binding names an address inside a function. `SWOFTY_MIXIN_SITE` runs your handler there with the CPU registers in a `SwoftyContext`, which you can read and write. Leave `orig` null for a site.

swofty_core adds the ADDON SETTINGS home tile this way at `site_FeHome_AfterCareerCall`, the instruction right after the game adds the Career tile. `r14` holds the `TileList *`, and `r12` and `r15` are the builder's counters, which it moves past the new tile ([Add a home tile](./in-game-ui#add-a-home-tile)):

```cpp [native/src/core/swofty_core.cpp]
SWOFTY_SITE(site_FeHome_AfterCareerCall, swofty::Context &c)
{
    auto list = reinterpret_cast<TileList *>(c.r14);   // read a register
    FeHome_Add_CareerTile(list, uint8_t(c.r12 + 1));
    c.r12 += 1;                                        // written registers reach the game when the handler returns
    c.r15 += 1;
}
```

```c
typedef struct SwoftyContext {
    SwoftyXmm xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15;
    uintptr_t rflags, r15, r14, r13, r12, r11, r10, r9, r8, rdi, rsi, rdx, rcx, rbx, rax, rbp, rsp, trampoline_rsp, rip;
} SwoftyContext;
```

## The SwoftyMixin record

```c
typedef struct SwoftyMixin {
    const char *target;   // the binding name, or null for SWOFTY_MIXIN_ON_LOAD
    uint32_t kind;        // a kind below, optionally OR'd with SWOFTY_MIXIN_OVERWRITE
    int32_t priority;     // higher runs first; default 0
    void *handler;        // the handler function
    void **orig;          // swofty_core writes the trampoline here (WRAP, VTABLE, REDIRECT); null otherwise
} SwoftyMixin;

enum {
    SWOFTY_MIXIN_WRAP = 1,      // wraps a func binding; the handler calls orig
    SWOFTY_MIXIN_SITE = 2,      // void(SwoftyContext *) at a site binding
    SWOFTY_MIXIN_VTABLE = 3,    // replaces one slot of a vtable binding
    SWOFTY_MIXIN_REDIRECT = 4,  // replaces the callee of a call binding
    SWOFTY_MIXIN_PATCH = 5,     // writes bytes at a site binding; handler points to the bytes
    SWOFTY_MIXIN_ON_LOAD = 6,   // void(void), after every mixin installs; target null
};

#define SWOFTY_MIXIN_OVERWRITE 0x100u   // OR'd onto a wrap's kind: the handler doesn't call orig
#define SWOFTY_MIXIN_WITH_ARG(k, arg) ((uint32_t)(k) | (uint32_t)(arg) << 16) // a vtable slot or patch length, in the top 16 bits
```

Progressive Career replaces slot 10 (`onCommandRaised`) of the driver select view model. In C that record is:

```c
{ "vt_Game_DriverCareerDriverSelectViewModel", SWOFTY_MIXIN_WITH_ARG(SWOFTY_MIXIN_VTABLE, 10), 0,
  (void *)on_command, (void **)&orig_on_command },
```

A malformed record (an unknown kind, no handler, no target, or a patch without a length) refuses the whole call:

```text
[core] addon my_plugin.dll: mixin 1 is malformed (kind 0x5); registration refused
```

## Chains

swofty_core groups every addon's mixins by target. When several addons hook one target:

- Higher priorities run first and wrap the lower ones. The default is 0.
- Equal priorities run in load order: `swofty/addons/core/` first, then `swofty/addons/`, each in file-name order.
- Site handlers all run, in the same order.

```text
two overwrites on one target          conflict
hooks whose patched bytes overlap     conflict
handlers below an overwrite           warning
a binding that does not resolve       the addon is dropped
```

In a conflict, every addon involved is dropped and none of their mixins install. `swofty/loader.log` names each one. When the conflict in one mixin commit is between your addon and one of swofty_core's own hook sets (`swofty_core.dll (...)` in the log), only the core's set is dropped.

```text
[core] conflict: two overwrites on FeHome_Handle_Command: a.dll, b.dll
[core] addon a.dll dropped: it conflicts with another addon (see the conflict line above); none of its mixins install
[core] warning: an overwrite on FeHome_Handle_Command hides the handlers below it: a.dll(10, overwrite) > swofty_core.dll(0)
```

## Rules

- Hook bindings, not fixed addresses.
- Hook `.xtls` code. A site inside a `.data1` body resolves only when its binding comes from a `.data1` signature, which the knowledge base records for code that no `.xtls` site reaches; it never resolves on the EA build.
- Keep hook setup out of `DllMain`.

## Reference

- [Mixin API](/reference/std/mixin): the C++ macros, bindings, fields and `subclass`.
- [Engine API](/reference/game/): the functions, structs and sites you can hook.
- [In-game UI](./in-game-ui): a site mixin that adds a home tile.
- [Debugging live](/reference/ui/debugging): tracing a hook in the running game.
