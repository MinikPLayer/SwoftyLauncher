---
title: Mixin API
description: The C++ mixin layer in swofty/mixin.hpp - bindings, the mixin macros, chains, init and per-object fields.
---

# Mixin API

The mixin API in `swofty/mixin.hpp` declares the handlers your plugin runs inside the game's code (wraps, site handlers, vtable slots, redirects and byte patches) over the [mixin system](/reference/addons/hooking).

```cpp [native/src/my_plugin.cpp]
// Trimmed from Progressive Career: addons/progressive/native/src/ui/back.cpp and ui/suffix.cpp
#include <cstring>
#include <string>

#include <swofty/game.hpp>  // the Game API: every binding in namespace swofty::game
#include <swofty/mem.hpp>   // guarded reads of game memory
#include <swofty/mixin.hpp> // the macros, swofty::log, SWOFTY_ADDON

namespace {

constexpr char kGroup[] = "f1_career_progressive";
constexpr char kBack[] = "back";
constexpr char kBackToPage[] = "back_career_wizard_driver_career_progressive";
constexpr uintptr_t kVmWizard = 0x270;    // wizard step view model -> wizard state
constexpr uintptr_t kWizardGroup = 0x128; // wizard state -> data group string

int g_presses = 0;

} // namespace

// A wrap runs in place of the function. orig() calls the next handler in the chain, and the game's code at the end.
SWOFTY_WRAP(FeTile_Route_CareerItem, swofty::game::Action *, swofty::game::HomeItem *item, swofty::game::Action *out)
{
    swofty::game::Action *r = orig(item, out);
    g_presses++;
    return r;
}

// A site handler runs at one instruction. Write to the context to change the game's registers.
SWOFTY_SITE(site_Game_CareerWizard_BackRoute, swofty::Context &c)
{
    auto route = reinterpret_cast<const char *>(c.rdx); // rdx: the route the game is about to open
    uintptr_t wizard = 0, group = 0;
    std::string g;
    if (!route || !swofty::mem::readable(route, sizeof kBack) || std::strcmp(route, kBack) ||
        !swofty::mem::read(c.rsi + kVmWizard, wizard) || !wizard ||   // rsi: the view model
        !swofty::mem::read(wizard + kWizardGroup, group) || !group ||
        !swofty::mem::c_string(group, g, 64) || g != kGroup)
        return;                                                      // not our career: the game's route stays
    c.rdx = reinterpret_cast<uintptr_t>(kBackToPage);                // the game opens this route instead
    swofty::log("[progressive] wizard back: season length step returns to the setup page (%s)", kBackToPage);
}

// Runs once, after every plugin's mixins are installed.
SWOFTY_ON_LOAD
{
    swofty::log("[my-addon] mixins installed");
}

SWOFTY_ADDON() // exports SwoftyAddonInit and SwoftyAddonApi; resolves the bindings and registers the mixins
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
addon my_plugin.dll: loaded at 00006fffea2c0000, calling SwoftyAddonInit (API v8)
[core] addon my_plugin.dll: registered 3 mixin(s)
addon my_plugin.dll: SwoftyAddonInit returned 1 (ok)
[core] wrap FeTile_Route_CareerItem at 0x141725620 (base+0x1725620) installed: swofty_core.dll (career tile route)(100) > my_plugin.dll(0)
[core] site site_Game_CareerWizard_BackRoute at 0x14bc7ab4d (base+0xbc7ab4d) installed: my_plugin.dll(0)
[my-addon] mixins installed
[progressive] wizard back: season length step returns to the setup page (back_career_wizard_driver_career_progressive)
```

`registered 3 mixin(s)` counts the wrap, the site and `SWOFTY_ON_LOAD`. Installation waits until every plugin has loaded.

- **Header:** `swofty/mixin.hpp`, with the bindings from `swofty/game.hpp`
- **Table:** `SwoftyApi::register_mixins` and `SwoftyApi::resolve`, API version 2
- **Feature:** `hook`
- **Thread:** the game thread that reaches the hooked instruction

## Bindings

A binding is a named place in `F1_25.exe`. The [Game API](/reference/game/) headers declare each one as an `inline constexpr` object in `namespace swofty::game`. swofty_core resolves the ones your plugin uses by signature before it registers your mixins.

```cpp
// A function binding, with its signature as the Name_t alias
using FeHome_Handle_Command_t = uint64_t(FEHomeViewModel *vm, NeStr **cmd);
inline constexpr fn<"FeHome_Handle_Command", FeHome_Handle_Command_t> FeHome_Handle_Command{};

uint64_t r = swofty::game::FeHome_Handle_Command(vm, cmd);    // fn: operator()(A...) calls it
auto *p = swofty::game::FeHome_Handle_Command.get();          // fn: get() returns the function pointer
uintptr_t at = swofty::game::FeHome_Handle_Command.address(); // every binding: the resolved address, or 0
const char *id = swofty::game::FeHome_Handle_Command.id;      // every binding: the name string
```

The templates, with the kind of each:

```cpp
fn<"Name", R(A...)>   // func: get() returns the function pointer, operator()(A...) calls it; variadic signatures work
address<"Name">       // func, with no signature
site<"Name">          // site: one instruction
call<"Name">          // call: one call instruction
data<"Name", T>       // data: get() returns T *, operator* returns T &
vtable<"Name", Slots> // vtable: get() returns void **, slots is the slot count
```

Every binding also has `binding_kind`. A binding that doesn't resolve on the running build stops your plugin from registering any mixin:

```text
[core] addon f1_1990_wizard.dll needs Game_SeasonLength_ApplyPreset, which does not resolve on this build
[core] addon parts_detach.dll needs Debris_Register, which bindings.json does not list: this loader is older than the addon, or Debris_Register is not an exported binding
```

## Macros

Each macro takes a binding name from `swofty::game` and opens your handler's body. Match your handler's signature to its binding, and choose the macro for that binding's kind. Get either wrong and it won't compile.

```cpp
SWOFTY_WRAP(target, R, params...)                        // func: wraps the function; R and params match target_t
SWOFTY_WRAP_PRIORITY(prio, target, R, params...)         // the same, with a priority
SWOFTY_OVERWRITE(prio, target, R, params...)             // func: a wrap that replaces the function and does not call orig
SWOFTY_SITE(target, param)                               // site: runs at the instruction with the registers, as swofty::Context &
SWOFTY_SITE_PRIORITY(prio, target, param)                // the same, with a priority
SWOFTY_VTABLE(target, slot, R, params...)                // vtable: replaces one slot; slot is below the binding's slots
SWOFTY_VTABLE_PRIORITY(prio, target, slot, R, params...) // the same, with a priority
SWOFTY_REDIRECT(target, R, params...)                    // call: replaces the callee of one call instruction
SWOFTY_PATCH(target, bytes...)                           // site: writes the bytes at the site; no handler
SWOFTY_ON_LOAD                                           // runs once, after every mixin is installed
```

Inside a handler, call `orig` to pass control to the next handler in the chain. At the end of the chain, `orig` calls the game's code.

### SWOFTY_WRAP

```cpp
SWOFTY_WRAP(FeHome_Handle_Command, uint64_t, FEHomeViewModel *vm, NeStr **cmd)
{
    return orig(vm, cmd) * 10 + 3; // call the rest of the chain, then change its result
}

// with a priority: higher runs first and wraps the lower ones
SWOFTY_WRAP_PRIORITY(10, FeHome_Handle_Command, uint64_t, FEHomeViewModel *vm, NeStr **cmd)
{
    return orig(vm, cmd);
}
```

### SWOFTY_OVERWRITE

```cpp
SWOFTY_OVERWRITE(10, FeHome_Handle_Command, uint64_t, FEHomeViewModel *vm, NeStr **cmd)
{
    return 0; // the game's code never runs
}
```

Handlers below an overwrite still install, with a warning. Two overwrites on one target are a [conflict](#chains).

```text
[core] warning: an overwrite on FeHome_Handle_Command hides the handlers below it: my_plugin.dll(10, overwrite) > other.dll(0)
```

### SWOFTY_SITE

```cpp
// From addons/rnd-tyres-thermal/native/src/rnd_fifth_pie.cpp
SWOFTY_SITE(site_Career_RnDPieSliceNameJoin, swofty::Context &c)
{
    if (!g_live.load() || !code4(c) || c.rdi != err_name())
        return;
    c.rdi = reinterpret_cast<uintptr_t>(kTyresName); // the game goes on with the new register value
}
```

Every site handler on one site runs, in chain order:

```text
[core] site site_Career_RnDCardArtImage0 at 0x14180a668 (base+0x180a668) installed: f1_1990_rnd.dll(0) > rnd_departments.dll(0)
```

### SWOFTY_VTABLE

```cpp
SWOFTY_VTABLE(vt_Fake_Other, 8, uintptr_t)
{
    return orig() + 5; // slot 8, for every object that uses this table
}
```

```text
[core] vtable vt_Game_DriverCareerDriverSelectViewModel at 0x1457298f0 (base+0x57298f0) installed: progressive_cards.dll(0)
```

### SWOFTY_REDIRECT

```cpp
// From addons/rnd-tyres-thermal/native/src/rnd_fifth_pie.cpp
SWOFTY_REDIRECT(site_Career_RnDPieSliceDescFormat, int64_t, NeStr **out, const char *fmt, const char *name)
{
    if (g_live.load() && reinterpret_cast<uintptr_t>(name) == err_name())
        name = kTyresName;       // change an argument of this one call
    return orig(out, fmt, name); // orig is the original callee
}
```

```text
[core] redirect site_Game_DriverSelect_BadgeFormat at 0x1416a5820 (base+0x16a5820) installed: progressive_cards.dll(0)
```

### SWOFTY_PATCH

```cpp
SWOFTY_PATCH(site_Fake_Patch, 0xB8, 0x02, 0x00, 0x00, 0x00) // mov eax, 2
```

```text
[core] patch wine-test:site_Fake_Patch at 0x14003a3ce (base+0x3a3ce) installed: t_chain_c.dll(0)
```

### SWOFTY_ON_LOAD

```cpp
SWOFTY_ON_LOAD
{
    swofty::log("[progressive] suffix: Choose Driver follows the start season (now %d)", start_year());
}
```

It runs once, after the mixin commit, and only for a plugin that is still active. A plugin that was dropped (a binding that doesn't resolve, a conflict) never runs its `SWOFTY_ON_LOAD`.

```text
[core] commit: 10 of 10 mixin addon(s) active; 10 wrap(s), 8 site(s), 1 vtable slot(s), 1 redirect(s), 0 patch(es)
[progressive] suffix: Choose Driver follows the start season (now 2017)
```

### Context

In a site handler, you change the game's registers by writing to the context. `swofty::Context` is `SwoftyContext`:

```c
typedef union SwoftyXmm {
    uint8_t u8[16];
    uint32_t u32[4];
    uint64_t u64[2];
    float f32[4];
    double f64[2];
} SwoftyXmm;

typedef struct SwoftyContext {
    SwoftyXmm xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15;
    uintptr_t rflags, r15, r14, r13, r12, r11, r10, r9, r8, rdi, rsi, rdx, rcx, rbx, rax, rbp, rsp, trampoline_rsp, rip;
} SwoftyContext;
```

```cpp
SWOFTY_SITE(site_Example, swofty::Context &c)
{
    float v = c.xmm1.f32[0]; // the low float of xmm1
    c.rax = 0;               // the game reads rax = 0 after the site
}
```

## Chains

Your mixin may share a target with other addons. swofty_core groups every addon's mixins by target and orders each group:

- Higher priorities run first and wrap the lower ones. The default is 0.
- Equal priorities run in load order: the plugins in `swofty/addons/core/` in file-name order, then the plugins `swofty/settings.json` lists in addon folders, in the profile's load order, then any other DLL directly in `swofty/addons/`, in file-name order.
- Site handlers all run, in the same order.

`swofty/loader.log` prints each chain, highest first, as `name(priority)`:

```text
[core] wrap FeHome_Handle_Command at 0x14003a364 (base+0x3a364) installed: t_chain_c.dll(10) > swofty_core.dll(0) > t_chain_a.dll(0) > t_chain_b.dll(0)
```

Each case, and what the log shows:

```text
# Two overwrites on one target: a conflict
[core] conflict: two overwrites on FeHome_Handle_Command: t_conflict_x.dll, t_conflict_y.dll
[core] addon t_conflict_x.dll dropped: it conflicts with another addon (see the conflict line above); none of its mixins install
[core] addon t_conflict_y.dll dropped: it conflicts with another addon (see the conflict line above); none of its mixins install

# Hooks on different targets whose patched bytes overlap: a conflict
[core] conflict: wrap FeTile_Route_CareerItem at 0x141725620 overlaps a wrap installed at the early stage: f1_1990_ui.dll(0) > f1_1990_wizard.dll(0)

# Handlers below an overwrite: a warning
[core] warning: an overwrite on FeHome_Handle_Command hides the handlers below it: my_plugin.dll(10, overwrite) > other.dll(0)

# A binding that does not resolve: the plugin registers no mixins
[core] addon t_drop.dll dropped: needs wine-test:Fake_Missing, which does not resolve on this build; none of its mixins install

# A mixin not declared in swofty/session.json, when that file exists: the registration is refused
[core] session.json: mixins not declared there are refused
[core] addon my_plugin.dll: wrap on FeHome_Handle_Command is not declared in session.json; registration refused
```

Without `swofty/session.json`, mixins are not checked:

```text
[core] no session.json; mixins are not checked against a manifest
```

A conflict drops every addon involved, so you lose all of their mixins, not just the conflicting ones. In a conflict in one mixin commit between an addon and one of swofty_core's own hook sets (`swofty_core.dll (...)` in the log), only the core's set is dropped:

```text
[core] core mixin set 'swofty_core.dll (career contents)': 3 mixin(s), installed or dropped on its own
```

## Init

```cpp
SWOFTY_ADDON()      // plain init
SWOFTY_ADDON(check) // also runs bool check() during init
```

```cpp
static bool check()
{
    return swofty::game::FeHome_Handle_Command.address() != 0; // false: init returns 0 and nothing registers
}

SWOFTY_ADDON(check)
```

`SWOFTY_ADDON` exports `SwoftyAddonInit`, and `SwoftyAddonApi` with the `SWOFTY_API_VERSION` the plugin was built with. `SWOFTY_MOD` is another name for `SWOFTY_ADDON`. Init proceeds in this order:

1. Stores the API in `swofty::api`.
2. Resolves every binding.
3. Runs `check`, if you supplied it.
4. Registers every mixin.
5. Connects your plugin's [settings](./settings).

If the API is older than version 2, a binding does not resolve, or `check` returns `false`, init returns 0 and registers nothing:

```text
mixin addon: needs the swofty_core mixin API (v2), loader offers v1
addon career_editor.dll: SwoftyAddonInit returned 0 (FAILED)
```

## Objects

### subclass

```cpp
void **swofty::subclass(void *obj, std::initializer_list<std::pair<size_t, void *>> slots, size_t count = 32);
```

```cpp
// One Career tile gets its own populate (slot 4) and get_action (slot 5); the other tiles keep the game's table
swofty::subclass(tile, {{4, reinterpret_cast<void *>(&my_populate)}, {5, reinterpret_cast<void *>(&my_get_action)}});
```

`subclass` copies the object's virtual table, replaces the slots you list, points the object at the copy and returns it. `count` sets how many slots it copies. Objects with the same table and the same replacements share one copy.

### fields

```cpp
template <class T> T *swofty::fields(const void *obj);
void swofty::release_fields(const void *obj);
```

```cpp
struct TileData { // trivially constructible and destructible
    int presses;
    bool highlighted;
};

TileData *d = swofty::fields<TileData>(tile); // a zeroed TileData on the first call
d->presses++;
swofty::release_fields(tile);                 // frees it
```

`fields` attaches your addon's data to a game object. Calls for the same object and addon return the same storage until `release_fields`. Use a trivially constructible and destructible `T`. Ask for a different size on an object your addon already holds and you get `nullptr`:

```text
[core] fields(000000000862E3C0, 16): this addon already keeps 8 bytes on that object
```

## C ABI

To register mixins through the C ABI, pass an array of `SwoftyMixin` to `register_mixins`. You can only call it from `SwoftyAddonInit`:

```c
typedef struct SwoftyMixin {
    const char *target;   // a binding name swofty_core knows
    uint32_t kind;        // SWOFTY_MIXIN_* kind, optionally OR'd with SWOFTY_MIXIN_OVERWRITE
    int32_t priority;
    void *handler;
    void **orig;          // swofty_core writes the trampoline here (WRAP/VTABLE/REDIRECT)
} SwoftyMixin;
```

```c
#include <swofty_api.h>

static uint64_t (*orig_cmd)(void *vm, void **cmd);

static uint64_t my_cmd(void *vm, void **cmd)
{
    return orig_cmd(vm, cmd);
}

SWOFTY_EXPORT const uint32_t SwoftyAddonApi = SWOFTY_API_VERSION;

SWOFTY_EXPORT int SwoftyAddonInit(const SwoftyApi *api)
{
    static const SwoftyMixin mixins[] = {
        {"FeHome_Handle_Command", SWOFTY_MIXIN_WRAP, 0, (void *)my_cmd, (void **)&orig_cmd},
    };
    if (api->version < 2 || !api->resolve("FeHome_Handle_Command")) // resolve returns 0 for a binding that does not resolve
        return 0;
    return api->register_mixins(mixins, 1);
}
```

| Constant | Value |
|---|---|
| `SWOFTY_MIXIN_WRAP` | 1 |
| `SWOFTY_MIXIN_SITE` | 2 |
| `SWOFTY_MIXIN_VTABLE` | 3 |
| `SWOFTY_MIXIN_REDIRECT` | 4 |
| `SWOFTY_MIXIN_PATCH` | 5 |
| `SWOFTY_MIXIN_ON_LOAD` | 6 |
| `SWOFTY_MIXIN_OVERWRITE` | `0x100`, a flag on `kind` |

`SWOFTY_MIXIN_WITH_ARG(kind, arg)` stores the vtable slot or patch length in the top 16 bits of `kind`. `SWOFTY_MIXIN_KIND` and `SWOFTY_MIXIN_ARG` read them back. A call outside `SwoftyAddonInit`, or a malformed record, is refused:

```text
[core] register_mixins outside SwoftyAddonInit; refused
[core] addon my_plugin.dll: mixin 0 is malformed (kind 0x5); registration refused
```

For an end-to-end walkthrough of the C ABI, see [Hooking a game function](/reference/addons/hooking).
