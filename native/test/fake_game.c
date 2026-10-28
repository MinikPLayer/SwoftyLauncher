#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct NeStr NeStr;
typedef struct { int32_t type, pad; NeStr *str; } Action;
typedef struct { uint8_t *begin, *end, *cap; } Vec;
typedef struct { void *vt; char title[64], description[64]; int title_localised, description_localised; } VmNode;
typedef struct { const char *name; uint32_t index; } AdderCall;

void *g_registry = (void *)0x5157;
uint64_t out_r12, out_r15;
__declspec(dllexport) volatile int64_t probe[48];
void *fake_cmd_global;
const char fake_cmd_name[] = "select_pressed";
static uint8_t list[0x100];
static AdderCall calls[32];
static int ncalls, fails, live_strings, build_leak;
static void *career_vt_storage[40];
void *other_vt_storage[40];
#define CAREER_VT (career_vt_storage + 1)
#define OTHER_VT (other_vt_storage + 1)

void fake_builder(void *list, const uint8_t *flags, uint32_t start);
uint64_t fake_handle_command(void *vm, void *cmd);
int fake_caller(void);
int fake_patchable(void);
__declspec(dllexport) void *fake_exports[] = {(void *)fake_handle_command};
uint64_t handle_command_impl(void *vm, void *cmd) { (void)vm, (void)cmd; return 1; }
int fake_callee(void) { return 1; }
void fake_base_populate(uint8_t *item, void *node, uint32_t a7);
uint32_t fake_nestr_from(NeStr **out, const char *s, uint32_t flags, uintptr_t len);

static const char *chars(const NeStr *s) { return s ? (const char *)s + 9 : ""; }

static void nestr_release(NeStr **s)
{
    if (*s && --*(uint16_t *)((uint8_t *)*s + 4) == 0) {
        free(*s);
        live_strings--;
    }
    *s = NULL;
}

uint32_t nestr_impl(NeStr **out, const char *s)
{
    if (s && *s && *out && !strcmp(chars(*out), s))
        return 1;
    NeStr *old = *out;
    *out = NULL;
    if (s && *s) {
        size_t n = strlen(s);
        uint8_t *p = calloc(1, 16 + n);
        uint32_t h = 5381;
        for (size_t i = 0; i < n; i++)
            h = h * 33 + (uint8_t)s[i];
        memcpy(p, &h, 4);
        *(uint16_t *)(p + 4) = 1;
        memcpy(p + 9, s, n);
        *out = (NeStr *)p;
        live_strings++;
    }
    nestr_release(&old);
    return 1;
}

static VmNode *node_of(void *parent, void *reg)
{
    if (reg != g_registry) {
        printf("FAIL: registry %p != %p\n", reg, g_registry);
        fails++;
    }
    return (VmNode *)((uint8_t *)parent - 8);
}

void *fake_vm_set_string(void *reg, void *parent, const char *name, NeStr **value, int overwrite)
{
    VmNode *n = node_of(parent, reg);
    if (!strcmp(name, "title"))
        snprintf(n->title, sizeof n->title, "%s", chars(*value));
    else if (!strcmp(name, "description"))
        snprintf(n->description, sizeof n->description, "%s", chars(*value));
    (void)overwrite;
    return &g_registry;
}

void *fake_vm_set_bool(void *reg, void *parent, const char *name, uint8_t *value, int overwrite)
{
    VmNode *n = node_of(parent, reg);
    if (!strcmp(name, "title_localised"))
        n->title_localised = *value;
    else if (!strcmp(name, "description_localised"))
        n->description_localised = *value;
    (void)overwrite;
    return &g_registry;
}

void populate_impl(uint8_t *item, void *node, uint32_t a7)
{
    VmNode *n = calloc(1, sizeof *n);
    n->title_localised = n->description_localised = 1;
    *(VmNode **)(item + 0x20) = n;
    (void)node, (void)a7;
}

static void career_populate(uint8_t *item, void *node, uint32_t a7)
{
    fake_base_populate(item, node, a7);
    NeStr *t = NULL;
    fake_nestr_from(&t, "lng_mode_career", 0, 0);
    fake_vm_set_string(g_registry, *(uint8_t **)(item + 0x20) + 8, "title", &t, 1);
    nestr_release(&t);
}

static Action *id_action(uint8_t *item, Action *out)
{
    out->type = 1;
    out->str = *(NeStr **)(item + 0x10);
    if (out->str)
        ++*(uint16_t *)((uint8_t *)out->str + 4);
    return out;
}

static uintptr_t stub_zero(void) { return 0; }

static void add(void *l, uint32_t index, const char *name, void **vt)
{
    Vec *v = (Vec *)((uint8_t *)l + 0x78);
    uint8_t *tile = calloc(1, 0xd8), *item = calloc(1, 0x148);
    *(void ***)item = vt;
    fake_nestr_from((NeStr **)(item + 0x10), name, 0, 0);
    *(uint8_t **)(tile + 0xd0) = item;
    ((void **)v->end)[0] = tile + 8;
    ((void **)v->end)[1] = tile;
    v->end += 16;
    calls[ncalls++] = (AdderCall){name, index & 0xff};
}

void career_adder(void *l, uint32_t index) { add(l, index, "career", CAREER_VT); }
void movie_adder(void *l, uint32_t index) { add(l, index, "zenith_button", OTHER_VT); }
void world_adder(void *l, uint32_t index) { add(l, index, "f1_world", OTHER_VT); }

__asm__(
    ".section .xtls,\"xr\"\n"
    ".globl fake_builder\nfake_builder:\n"
    ".byte 0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x56,0x57,0x55,0x53\n"
    ".byte 0x48,0x81,0xEC,0x88,0x02,0x00,0x00\n"
    ".byte 0xC5,0xF8,0x29,0xB4,0x24,0x70,0x02,0x00,0x00\n"
    ".byte 0x4C,0x89,0x8C,0x24,0xE8,0x02,0x00,0x00\n"
    "mov %rcx, %r14\nmov %rdx, %rsi\nmov %r8d, %r15d\n"
    ".byte 0x45,0x8D,0x67,0x01,0x4C,0x89,0xF1,0x44,0x89,0xE2,0xE8\n.long career_adder - (. + 4)\n"
    ".byte 0xF6,0x46,0x05,0x04,0x74,0x12\n"
    ".byte 0x41,0x80,0xC7,0x02,0x4C,0x89,0xF1,0x44,0x89,0xFA,0xE8\n.long movie_adder - (. + 4)\n"
    ".byte 0x45,0x89,0xFC\n"
    ".byte 0x45,0x8D,0x7C,0x24,0x01,0x4C,0x89,0xF1,0x44,0x89,0xFA,0xE8\n.long world_adder - (. + 4)\n"
    "mov %r12, out_r12(%rip)\nmov %r15, out_r15(%rip)\n"
    "vmovaps 0x270(%rsp), %xmm6\nadd $0x288, %rsp\n"
    "pop %rbx\npop %rbp\npop %rdi\npop %rsi\npop %r12\npop %r13\npop %r14\npop %r15\nret\n"

    ".globl fake_base_populate\nfake_base_populate:\n"
    ".byte 0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x56,0x57,0x55,0x53\n"
    ".byte 0x48,0x81,0xEC,0x88,0x00,0x00,0x00\n"
    ".byte 0xC5,0xF8,0x29,0x74,0x24,0x70,0x44,0x89,0x44,0x24,0x6C\n"
    "call populate_impl\njmp 1f\n"
    ".org fake_base_populate+0x53, 0xCC\nmov g_registry(%rip), %rcx\n"
    ".org fake_base_populate+0x69, 0xCC\ncall fake_vm_set_string\n"
    ".org fake_base_populate+0x28e, 0xCC\ncall fake_vm_set_bool\n"
    "1:\nvmovaps 0x70(%rsp), %xmm6\nadd $0x88, %rsp\n"
    "pop %rbx\npop %rbp\npop %rdi\npop %rsi\npop %r12\npop %r13\npop %r14\npop %r15\nret\n"

    ".globl fake_nestr_from\nfake_nestr_from:\n"
    ".byte 0x41,0x57,0x41,0x56,0x41,0x54,0x56,0x57,0x55,0x53,0x48,0x83,0xEC,0x50,0x44,0x89,0xC6,0x48,0x89,0xCF\n"
    "call nestr_impl\nadd $0x50, %rsp\n"
    "pop %rbx\npop %rbp\npop %rdi\npop %rsi\npop %r12\npop %r14\npop %r15\nret\n"

    ".globl fake_handle_command\nfake_handle_command:\n"
    ".byte 0x41,0x57,0x41,0x56,0x56,0x57,0x53,0x48,0x83,0xEC,0x40,0x48,0x89,0xD7,0x48,0x89,0xCE\n"
    "mov fake_cmd_global(%rip), %rax\nlea fake_cmd_name(%rip), %rcx\n"
    "mov %rsi, %rcx\nmov %rdi, %rdx\ncall handle_command_impl\nadd $0x40, %rsp\n"
    "pop %rbx\npop %rdi\npop %rsi\npop %r14\npop %r15\nret\n"

    ".globl fake_vt_ref\nfake_vt_ref:\n"
    ".byte 0x48,0xB8,0x53,0x57,0x46,0x54,0x56,0x54,0x41,0x42\nlea other_vt_storage+8(%rip), %rax\nret\n"

    ".globl fake_caller\nfake_caller:\n"
    ".byte 0x48,0xB8,0x53,0x57,0x46,0x54,0x43,0x41,0x4C,0x4C\nsub $0x28, %rsp\ncall fake_callee\nadd $0x28, %rsp\nret\n"

    ".globl fake_patchable\nfake_patchable:\n"
    ".byte 0x48,0xB8,0x53,0x57,0x46,0x54,0x50,0x41,0x54,0x43\nmov $1, %eax\nret\n"

    ".globl fake_owned_state\nfake_owned_state:\n"
    ".byte 0x41,0x56,0x56,0x57,0x55,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0x01,0x48,0x8B,0x71,0x08\n"
    ".byte 0xFF,0x50,0x08,0x8B,0x90,0xA0,0x00,0x00,0x00\n"
    "mov %edx, %ecx\ncall owned_impl\nadd $0x20, %rsp\n"
    "pop %rbx\npop %rbp\npop %rdi\npop %rsi\npop %r14\nret\n"

    // Reproduces the Vm_Update_FlowStateMachine prologue so the events module's flow-tick hook
    // resolves and the test can drive the per-frame tick; the body just restores and returns.
    ".globl fake_flow\nfake_flow:\n"
    ".byte 0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x56,0x57,0x55,0x53\n"
    ".byte 0x48,0x81,0xEC,0x58,0x02,0x00,0x00\n"
    ".byte 0xC5,0xF8,0x29,0xB4,0x24,0x40,0x02,0x00,0x00\n"
    ".byte 0xC5,0xF9,0x28,0xF2\n"
    "vmovaps 0x240(%rsp), %xmm6\nadd $0x258, %rsp\n"
    "pop %rbx\npop %rbp\npop %rdi\npop %rsi\npop %r12\npop %r13\npop %r14\npop %r15\nret\n"
    ".section .data1,\"dwx\"\n"
    ".globl vm_thunk_register\nvm_thunk_register:\n"
    "sub $0x28, %rsp\ncall *__imp_RegisterClassExW(%rip)\nadd $0x28, %rsp\nret\n"
    ".text\n");
ATOM vm_thunk_register(const WNDCLASSEXW *wc);
void fake_flow(void);

__asm__(
    ".section .xtls,\"xr\"\n"
    ".globl fake_stage_site\nfake_stage_site:\n"
    ".byte 0x48,0xB8,0x53,0x57,0x46,0x54,0x53,0x49,0x54,0x45\nmov $7, %eax\nret\n"
    ".globl fake_stage_wrap\nfake_stage_wrap:\n"
    ".byte 0x48,0xB8,0x53,0x57,0x46,0x54,0x57,0x52,0x41,0x50\nmov $9, %eax\nret\n"
    ".globl fake_stage_plain\nfake_stage_plain:\n"
    ".byte 0x48,0xB8,0x53,0x57,0x46,0x54,0x50,0x4C,0x41,0x4E\nmov $5, %eax\nret\n"
    ".text\n");
int fake_stage_site(void);
int fake_stage_wrap(void);
int fake_stage_plain(void);

struct FakeLogGate {
    uint32_t hashes[373];
    uint8_t enabled, pad[3];
};
__declspec(dllexport) struct FakeLogGate fake_log_gate;
__declspec(dllexport) volatile int64_t gamelog_body_calls;
uint8_t fake_rsi_area[0x600];
const char fake_chan_warn[] = "TEST_WARN", fake_chan_verbose[] = "TEST_VERBOSE", fake_chan_ui[] = "UI_FLOW";
const char fake_fmt_warn[] = "fake gate message from %s", fake_fmt_verbose[] = "fake verbose message from %s";
const char fake_fmt_ui[] = "fake ui info from %s";
const char fake_arg_a[] = "site_a", fake_arg_b[] = "site_b", fake_arg_c[] = "site_c";
void fake_log_site_a(void);
void fake_log_site_b(void);
void fake_log_site_c(void);
int64_t fake_format_string(NeStr **out, const char *fmt, ...);

void fake_log_body_impl(void) { gamelog_body_calls++; }

int64_t format_impl(NeStr **out, const char *fmt, va_list ap)
{
    char buf[0x200];
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    nestr_impl(out, buf);
    return n;
}

__asm__(
    ".section .xtls,\"xr\"\n"
    ".globl fake_log_site_a\nfake_log_site_a:\n"
    "push %rsi\npush %rdi\nsub $0x28, %rsp\nlea fake_rsi_area(%rip), %rsi\n"
    ".byte 0x80,0x3D\n.long fake_log_gate+0x5d4-(.+5)\n.byte 0x01\n"
    ".byte 0x75,0x74,0xB8,0x5F,0x01,0x00,0x00\n"
    ".byte 0x48,0x8D,0x0D\n.long fake_log_gate-(.+4)\n"
    ".byte 0x66,0x66,0x66,0x66,0x66,0x66,0x2E,0x0F,0x1F,0x84,0x00,0x00,0x00,0x00,0x00\n"
    ".byte 0x8B,0x14,0x81,0x85,0xD2,0x74,0x52,0x81,0xFA,0xCB,0x88,0x7C,0x7C,0x74,0x30\n"
    ".byte 0x83,0xC0,0x19,0x48,0x69,0xD0,0x43,0x43,0x66,0x5F,0x48,0xC1,0xEA,0x20,0x41,0x89,0xC0,0x41,0x29,0xD0,0x41,0xD1,0xE8,0x41,0x01,0xD0,0x41,0xC1,0xE8,0x08,0x41,0x69,0xD0,0x75,0x01,0x00,0x00,0x29,0xD0,0x3D,0x5F,0x01,0x00,0x00,0x75,0xC3,0xEB,0x1A\n"
    ".byte 0x48,0x8D,0x0D\n.long fake_chan_warn-(.+4)\n"
    ".byte 0x48,0x8D,0x15\n.long fake_fmt_warn-(.+4)\n"
    ".byte 0x4C,0x8D,0x05\n.long fake_arg_a-(.+4)\n"
    ".byte 0xE8\n.long fake_log_thunk-(.+4)\n"
    ".byte 0x48,0x8B,0xBE,0x98,0x04,0x00,0x00\n"
    "add $0x28, %rsp\npop %rdi\npop %rsi\nret\n"
    ".globl fake_log_site_b\nfake_log_site_b:\n"
    "push %rsi\npush %rdi\nsub $0x28, %rsp\nlea fake_rsi_area(%rip), %rsi\n"
    ".byte 0x80,0x3D\n.long fake_log_gate+0x5d4-(.+5)\n.byte 0x01\n"
    ".byte 0x75,0x74,0xB8,0x3A,0x01,0x00,0x00\n"
    ".byte 0x48,0x8D,0x0D\n.long fake_log_gate-(.+4)\n"
    ".byte 0x66,0x66,0x66,0x66,0x66,0x66,0x2E,0x0F,0x1F,0x84,0x00,0x00,0x00,0x00,0x00\n"
    ".byte 0x8B,0x14,0x81,0x85,0xD2,0x74,0x52,0x81,0xFA,0x11,0x11,0x11,0x11,0x74,0x30\n"
    ".byte 0x83,0xC0,0x11,0x48,0x69,0xD0,0x43,0x43,0x66,0x5F,0x48,0xC1,0xEA,0x20,0x41,0x89,0xC0,0x41,0x29,0xD0,0x41,0xD1,0xE8,0x41,0x01,0xD0,0x41,0xC1,0xE8,0x08,0x41,0x69,0xD0,0x75,0x01,0x00,0x00,0x29,0xD0,0x3D,0x3A,0x01,0x00,0x00,0x75,0xC3,0xEB,0x1A\n"
    ".byte 0x48,0x8D,0x0D\n.long fake_chan_verbose-(.+4)\n"
    ".byte 0x48,0x8D,0x15\n.long fake_fmt_verbose-(.+4)\n"
    ".byte 0x4C,0x8D,0x05\n.long fake_arg_b-(.+4)\n"
    ".byte 0xE8\n.long fake_log_thunk-(.+4)\n"
    ".byte 0x48,0x8B,0xBE,0x90,0x04,0x00,0x00\n"
    "add $0x28, %rsp\npop %rdi\npop %rsi\nret\n"
    ".globl fake_log_site_c\nfake_log_site_c:\n"
    "push %rsi\npush %rdi\nsub $0x28, %rsp\nlea fake_rsi_area(%rip), %rsi\n"
    ".byte 0x80,0x3D\n.long fake_log_gate+0x5d4-(.+5)\n.byte 0x01\n"
    ".byte 0x75,0x74,0xB8,0xFF,0x00,0x00,0x00\n"
    ".byte 0x48,0x8D,0x0D\n.long fake_log_gate-(.+4)\n"
    ".byte 0x66,0x66,0x66,0x66,0x66,0x66,0x2E,0x0F,0x1F,0x84,0x00,0x00,0x00,0x00,0x00\n"
    ".byte 0x8B,0x14,0x81,0x85,0xD2,0x74,0x52,0x81,0xFA,0x22,0x22,0x22,0x22,0x74,0x30\n"
    ".byte 0x83,0xC0,0x15,0x48,0x69,0xD0,0x43,0x43,0x66,0x5F,0x48,0xC1,0xEA,0x20,0x41,0x89,0xC0,0x41,0x29,0xD0,0x41,0xD1,0xE8,0x41,0x01,0xD0,0x41,0xC1,0xE8,0x08,0x41,0x69,0xD0,0x75,0x01,0x00,0x00,0x29,0xD0,0x3D,0xFF,0x00,0x00,0x00,0x75,0xC3,0xEB,0x1A\n"
    ".byte 0x48,0x8D,0x0D\n.long fake_chan_ui-(.+4)\n"
    ".byte 0x48,0x8D,0x15\n.long fake_fmt_ui-(.+4)\n"
    ".byte 0x4C,0x8D,0x05\n.long fake_arg_c-(.+4)\n"
    ".byte 0xE8\n.long fake_log_thunk-(.+4)\n"
    ".byte 0x48,0x8B,0xBE,0x88,0x04,0x00,0x00\n"
    "add $0x28, %rsp\npop %rdi\npop %rsi\nret\n"
    ".globl fake_log_thunk\nfake_log_thunk:\n"
    ".byte 0xE9\n.long fake_log_body-(.+4)\n"
    ".byte 0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC\n"

    ".globl fake_format_string\nfake_format_string:\n"
    ".byte 0x41,0x57,0x41,0x56,0x56,0x57,0x55,0x53,0x48,0x81,0xEC,0x48,0x02,0x00,0x00\n"
    ".byte 0x48,0x89,0xD7,0x48,0x89,0xCE,0x4C,0x89,0x84,0x24,0x90,0x02,0x00,0x00\n"
    "mov %r9, 0x298(%rsp)\nmov %rsi, %rcx\nmov %rdi, %rdx\nlea 0x290(%rsp), %r8\ncall format_impl\n"
    "add $0x248, %rsp\npop %rbx\npop %rbp\npop %rdi\npop %rsi\npop %r14\npop %r15\nret\n"

    ".section .data1,\"dwx\"\n"
    ".globl fake_log_body\nfake_log_body:\n"
    "sub $0x28, %rsp\ncall fake_log_body_impl\nadd $0x28, %rsp\nret\n"
    ".text\n");

static void expect(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}

static void run_build(int movie, uint32_t start)
{
    uint8_t flags[16] = {0};
    flags[5] = movie ? 4 : 0;
    Vec *v = (Vec *)(list + 0x78);
    if (!v->begin)
        v->begin = v->end = calloc(64, 16);
    v->end = v->begin;
    ncalls = 0;
    int strings_before = live_strings;
    fake_builder(list, flags, start);
    printf("build movie=%d start=%u -> r12=%llu r15=%llu, %d adder calls:", movie, start,
           (unsigned long long)out_r12, (unsigned long long)out_r15, ncalls);
    for (int i = 0; i < ncalls; i++)
        printf(" %s=%u", calls[i].name, calls[i].index);
    printf("\n");
    for (uint8_t *e = v->begin; e < v->end; e += 16) {
        uint8_t *tile = ((uint8_t **)e)[1], *item = *(uint8_t **)(tile + 0xd0);
        void **vt = *(void ***)item;
        ((void (*)(uint8_t *, void *, uint32_t))vt[4])(item, tile + 8, 0);
        Action a = {0};
        ((Action * (*)(uint8_t *, Action *)) vt[5])(item, &a);
        VmNode *n = *(VmNode **)(item + 0x20);
        printf("  item %-14s title='%s' loc=%d desc='%s' dloc=%d action={%d,'%s'}\n", chars(*(NeStr **)(item + 0x10)),
               n ? n->title : "", n ? n->title_localised : -1, n ? n->description : "", n ? n->description_localised : -1,
               a.type, chars(a.str));
        nestr_release(&a.str);
    }
    build_leak = live_strings - strings_before - ncalls;
    printf("  live strings %d -> %d for %d new item id(s): %d leaked reference(s)\n", strings_before, live_strings,
           ncalls, build_leak);
}

static int movie_built(void)
{
    for (int i = 0; i < ncalls; i++)
        if (!strcmp(calls[i].name, "zenith_button"))
            return 1;
    return 0;
}

static int closes;

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    closes += m == WM_CLOSE;
    return DefWindowProcW(h, m, w, l);
}

static const char *const setting_commands[] = {
    "swofty_set:example.sample-mod:verbose:1",
    "swofty_set:example.sample-mod:greeting_count:9",
    "swofty_set:example.sample-mod:offset:7",
    "swofty_set:example.sample-mod:flavour:2",
    "swofty_set:example.sample-mod:mismatched:4",
    "swofty_set:community.grid-2026:team_livery:1",
    "swofty_set:example.sample-mod:verbose:0",
    "swofty_enable:community.grid-2026:0",
    "select_pressed",
    NULL,
    "swofty_restart",
};

static uint64_t send_command(const char *name)
{
    NeStr *s = NULL;
    if (name)
        nestr_impl(&s, name);
    uint64_t r = fake_handle_command(0, &s);
    nestr_release(&s);
    return r;
}

static const char *probe_str(int i) { return (const char *)(intptr_t)probe[i]; }

static int reads_are(void (*read)(void), int64_t verbose, int64_t count, int64_t offset, const char *flavour)
{
    read();
    printf("      read: verbose=%lld greeting_count=%lld offset=%lld/1000 flavour='%s' mismatched=%lld undeclared=%lld "
           "audi_name=%lld\n",
           (long long)probe[24], (long long)probe[25], (long long)probe[26], probe_str(27) ? probe_str(27) : "(null)",
           (long long)probe[28], (long long)probe[29], (long long)probe[30]);
    return probe[24] == verbose && probe[25] == count && probe[26] == offset && probe_str(27) &&
           !strcmp(probe_str(27), flavour) && probe[28] == 1 && probe[29] == 42 && probe[30] == 0;
}

unsigned owned_calls;
void *unlock_vt[11];

uint32_t owned_impl(uint32_t key)
{
    owned_calls++;
    return key == 0x7f88c309 ? 1 : 3;
}

uint8_t *unlock_key(uint8_t *item) { return item + 0x18; }

uint32_t fake_owned_state(void *item);

static void test_items(int on)
{
    const char *items_on = getenv("SWOFTY_TEST_ITEMS");
    if (!items_on || !*items_on)
        return;
    static uint8_t items[3][0x130];
    const uint32_t keys[3] = {0xf01d03c7, 0x7f88c309, 0x5b3c2a10};
    unlock_vt[1] = (void *)unlock_key;
    for (int i = 0; i < 3; i++) {
        *(void ***)items[i] = unlock_vt;
        memcpy(items[i] + 0x18 + 0xa0, &keys[i], 4);
    }
    unsigned before = owned_calls;
    uint32_t mod = fake_owned_state(items[0]), again = fake_owned_state(items[0]);
    uint32_t stock_owned = fake_owned_state(items[1]), stock_locked = fake_owned_state(items[2]);
    printf("      owned state: mod %u/%u, stock owned %u, stock locked %u, game check ran %u time(s)\n", mod, again,
           stock_owned, stock_locked, owned_calls - before);
    expect(mod == (on ? 1u : 3u) && again == mod,
           on ? "items: owned_items key 0xf01d03c7 reported owned" : "items: no owned_items, the mod key gets the game's answer");
    expect(stock_owned == 1 && stock_locked == 3, "items: stock keys keep the game's answer (owned 1, locked 3)");
    expect(owned_calls - before == (on ? 2u : 4u), "items: the game's check runs for every key not on the list");
}

// Game_ErsSystem_HandleVehicleNewLap, copied byte for byte from the Steam build (0x1425180d0..0x14251819e), so the
// core's ERS new-lap guard resolves its two sites here and the test can post a vehicle_newlap through it.
__asm__(
    ".section .xtls,\"xr\"\n"
    ".globl fake_ers_newlap\nfake_ers_newlap:\n"
    ".byte 0x48,0x8B,0x41,0x30,0x4C,0x8B,0x41,0x38,0x4C,0x39,0xC0,0x74,0x2C,0x0F,0xB6,0x52,0x10,0x66,0x66,0x66,0x66,0x66,0x66,0x2E\n"
    ".byte 0x0F,0x1F,0x84,0x00,0x00,0x00,0x00,0x00,0x4C,0x8B,0x08,0x4D,0x8B,0x49,0x28,0x41,0x38,0x91,0xE8,0x00,0x00,0x00,0x74,0x0C\n"
    ".byte 0x48,0x83,0xC0,0x08,0x4C,0x39,0xC0,0x75,0xE7,0x31,0xC0,0xC3,0x49,0x8B,0x81,0xA8,0x05,0x00,0x00,0x48,0x85,0xC0,0x74,0xF1\n"
    ".byte 0x80,0x79,0x21,0x00,0x0F,0xB6,0x51,0x22,0x41,0xB8,0x03,0x00,0x00,0x00,0x44,0x0F,0x44,0xC2,0x80,0x79,0x20,0x00,0xB9,0x02\n"
    ".byte 0x00,0x00,0x00,0x41,0x0F,0x44,0xC8,0xC5,0xFA,0x10,0x80,0xC8,0x19,0x00,0x00,0xC5,0xFA,0x11,0x80,0xDC,0x19,0x00,0x00,0xC5\n"
    ".byte 0xF8,0x10,0x80,0xCC,0x19,0x00,0x00,0xC5,0xF8,0x11,0x80,0xE0,0x19,0x00,0x00,0xC5,0xF8,0x57,0xC0,0xC5,0xF8,0x11,0x80,0xC8\n"
    ".byte 0x19,0x00,0x00,0xC7,0x80,0xD8,0x19,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0xF9,0x03,0x75,0x07,0xC5,0xFA,0x10,0x40,0x58,0xEB\n"
    ".byte 0x1A,0xFE,0xC9,0xC5,0xFA,0x10,0x40,0x50,0x80,0xF9,0x01,0x77,0x0E,0x80,0xB8,0x61,0x1A,0x00,0x00,0x00,0x74,0x05,0xC5,0xFA\n"
    ".byte 0x58,0x40,0x54,0xC5,0xFA,0x11,0x80,0xF4,0x19,0x00,0x00,0x31,0xC0,0xC3\n"
    ".text\n");
uint64_t fake_ers_newlap(void *self, void *msg);

static void test_ers_newlap(void)
{
    static uint8_t self[0x40], msg[0x18], blank[0x30], bound[2][0x30], vehicle[2][0x5b0], ers[0x1a70];
    void *entries[4] = {blank, NULL, bound[0], bound[1]};
    for (int i = 0; i < 2; i++) {
        *(void **)(bound[i] + 0x28) = vehicle[i];
        vehicle[i][0xe8] = i ? 24 : 5;
    }
    *(void **)(vehicle[1] + 0x5a8) = ers;
    *(void **)(self + 0x30) = entries;
    *(void **)(self + 0x38) = entries + 4;
    self[0x22] = 1;
    float lap = 7.0f, base = 2.0f, extra = 0.5f, got = 0.0f, prev = 0.0f, now = 1.0f;
    memcpy(ers + 0x19c8, &lap, 4);
    memcpy(ers + 0x50, &base, 4);
    memcpy(ers + 0x54, &extra, 4);
    ers[0x1a61] = 1;
    msg[0x10] = 9;
    uint64_t r = fake_ers_newlap(self, msg);
    memcpy(&now, ers + 0x19c8, 4);
    expect(r == 0 && now == 7.0f, "ers: a vehicle_newlap for a vehicle no participant has walks past the empty slots and "
                                  "changes nothing");
    msg[0x10] = 24;
    r = fake_ers_newlap(self, msg);
    memcpy(&got, ers + 0x19f4, 4);
    memcpy(&prev, ers + 0x19dc, 4);
    memcpy(&now, ers + 0x19c8, 4);
    printf("      vehicle_newlap for 24: returned %llu, lap budget %.2f, last lap %.2f, this lap %.2f\n",
           (unsigned long long)r, got, prev, now);
    expect(r == 0 && got == 2.5f && prev == 7.0f && now == 0.0f,
           "ers: past a participant without a vehicle and a null participant, the car's lap energy counters reset");
}

// Game_PracticeTrackAcclimatisation_RefreshState, copied byte for byte from the Steam build (0x14299e3a0..0x14299e8a6)
// with its unwind data, except that its one direct call (to 0x1429a02d0) goes to fake_acclim_gates. Core's track
// acclimatisation guard resolves it here, and the test refreshes a programme with targets and one without.
__asm__(
    ".section .xtls,\"xr\"\n"
    ".globl fake_acclim_refresh\n"
    ".def fake_acclim_refresh; .scl 2; .type 32; .endef\n"
    ".seh_proc fake_acclim_refresh\n"
    "fake_acclim_refresh:\n"
    ".byte 0x41,0x57\n.seh_pushreg %r15\n"
    ".byte 0x41,0x56\n.seh_pushreg %r14\n"
    ".byte 0x41,0x54\n.seh_pushreg %r12\n"
    ".byte 0x56\n.seh_pushreg %rsi\n"
    ".byte 0x57\n.seh_pushreg %rdi\n"
    ".byte 0x55\n.seh_pushreg %rbp\n"
    ".byte 0x53\n.seh_pushreg %rbx\n"
    ".byte 0x48,0x83,0xEC,0x20\n.seh_stackalloc 32\n"
    ".seh_endprologue\n"
    ".byte 0x48,0x89,0xCE,0x48,0x8B,0x81,0x68,0x03,0x00,0x00,0x48,0x85,0xC0,0x74,0x09,0x0F,0xB6,0x80,0xA7,0x00,0x00,0x00,0xEB,0x02\n"
    ".byte 0x31,0xC0,0x88,0x86,0x00,0x08,0x00,0x00,0x0F,0xB6,0x96,0x36,0x03,0x00,0x00,0x8B,0x8E,0x28,0x03,0x00,0x00,0x44,0x8D,0x14\n"
    ".byte 0x11,0x44,0x03,0x96,0x08,0x08,0x00,0x00,0x4C,0x8B,0x86,0x58,0x03,0x00,0x00,0x48,0x8B,0x86,0x50,0x03,0x00,0x00,0x49,0x29\n"
    ".byte 0xC0,0x49,0xC1,0xE8,0x04,0x41,0x69,0xD8,0x87,0xB5,0x6F,0x58,0xFF,0xCB,0x8B,0xAE,0x04,0x08,0x00,0x00,0x01,0xDD,0x44,0x39\n"
    ".byte 0xD5,0x44,0x0F,0x42,0xD5,0x44,0x8B,0x8E,0x30,0x03,0x00,0x00,0x44,0x8B,0x9E,0xE4,0x07,0x00,0x00,0x44,0x0F,0xB6,0x86,0x34\n"
    ".byte 0x03,0x00,0x00,0x45,0x84,0xC0,0x74,0x1F,0x45,0x85,0xC9,0x75,0x1A,0x41,0x8D,0x7A,0xFF,0x45,0x31,0xE4,0x41,0x39,0xFB,0x73\n"
    ".byte 0x1D,0x40,0xB7,0x01,0x41,0xBC,0x01,0x00,0x00,0x00,0x45,0x31,0xFF,0xEB,0x3C,0x45,0x31,0xE4,0x0F,0xB6,0xBE,0x82,0x03,0x00\n"
    ".byte 0x00,0x44,0x08,0xC7,0xEB,0x07,0x80,0xBE,0x82,0x03,0x00,0x00,0x00,0xBF,0x00,0x00,0x00,0x00,0x41,0xBF,0x00,0x00,0x00,0x00\n"
    ".byte 0x75,0x19,0xC5,0xFA,0x10,0x86,0x38,0x03,0x00,0x00,0xC5,0xF8,0x2E,0x86,0xE8,0x04,0x00,0x00,0x41,0x0F,0x92,0xC7,0x45,0x31\n"
    ".byte 0xE4,0x31,0xFF,0x45,0x89,0xD6,0x45,0x29,0xE6,0x44,0x39,0xF5,0x44,0x0F,0x42,0xF5,0x44,0x29,0xE1,0x39,0xCB,0x0F,0x42,0xCB\n"
    ".byte 0x80,0xBE,0x30,0x08,0x00,0x00,0x00,0x0F,0xB6,0x9E,0x80,0x03,0x00,0x00,0x0F,0x84,0x4F,0x01,0x00,0x00,0x84,0xDB,0x74,0x41\n"
    ".byte 0x8B,0x9E,0x7C,0x03,0x00,0x00,0xF7,0xC3,0xFD,0xFF,0xFF,0xFF,0x75,0x2C,0x45,0x31,0xFF,0x83,0xFB,0x02,0x41,0x0F,0x94,0xC7\n"
    ".byte 0x49,0xC1,0xE7,0x05,0x4A,0x8B,0x9C,0x3E,0xD0,0x03,0x00,0x00,0x4A,0x2B,0x9C,0x3E,0xC8,0x03,0x00,0x00,0x48,0xC1,0xEB,0x04\n"
    ".byte 0x69,0xDB,0x39,0x8E,0xE3,0x38,0x85,0xDB,0x75,0x07,0x80,0x8E,0xFC,0x07,0x00,0x00,0x08,0xC6,0x86,0x80,0x03,0x00,0x00,0x00\n"
    ".byte 0x89,0x8E,0xE0,0x07,0x00,0x00,0x44,0x89,0xB6,0xE4,0x07,0x00,0x00,0xC6,0x86,0x30,0x08,0x00,0x00,0x01,0x45,0x39,0xD3,0x41\n"
    ".byte 0x0F,0x94,0xC2,0x44,0x39,0x8E,0xE8,0x07,0x00,0x00,0x74,0x28,0x41,0x08,0xFA,0x74,0x23,0xC6,0x86,0x30,0x08,0x00,0x00,0x01\n"
    ".byte 0x84,0xD2,0x74,0x11,0x89,0xC9,0x48,0x69,0xC9,0x70,0x03,0x00,0x00,0x44,0x8B,0x4C,0x08,0x04,0x41,0xFF,0xC9,0x44,0x89,0x8E\n"
    ".byte 0xE8,0x07,0x00,0x00,0xC5,0xFA,0x10,0x86,0x34,0x08,0x00,0x00,0x45,0x84,0xC0,0x74,0x16,0xC5,0xF0,0x57,0xC9,0xC5,0xF8,0x2E\n"
    ".byte 0xC1,0x76,0x0C,0xC7,0x86,0x34,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0xEB,0x0A,0xC5,0xF0,0x57,0xC9,0xC5,0xF8,0x2E,0xC8,0x72\n"
    ".byte 0x0F,0x48,0x89,0xF1\n"
    "call fake_acclim_gates\n"
    ".byte 0x48,0x8B,0x86,0x50,0x03,0x00,0x00,0x8B,0x8E,0xE0,0x07,0x00,0x00,0x48,0x69,0xC9,0x70,0x03,0x00,0x00,0x48,0x8D,0x14,0x08\n"
    ".byte 0x48,0x83,0xC2,0x04,0x44,0x8B,0x4C,0x08,0x04,0x45,0x31,0xD2,0x4D,0x85,0xC9,0x74,0x76,0x44,0x8B,0x9C,0x08,0xC4,0x00,0x00\n"
    ".byte 0x00,0x41,0xF6,0xC3,0x02,0x74,0x0D,0x41,0x83,0xE3,0x18,0x41,0xB0,0x01,0x41,0x83,0xFB,0x08,0x75,0x6A,0x4C,0x8D,0x1C,0x01\n"
    ".byte 0x49,0x81,0xC3,0x94,0x01,0x00,0x00,0x49,0x8D,0x79,0xFF,0x45,0x31,0xC0,0x66,0x66,0x2E,0x0F,0x1F,0x84,0x00,0x00,0x00,0x00\n"
    ".byte 0x00,0x4C,0x39,0xC7,0x74,0x3E,0x41,0x8B,0x1B,0x49,0x81,0xC3,0xD0,0x00,0x00,0x00,0x49,0xFF,0xC0,0xF6,0xC3,0x02,0x74,0xE9\n"
    ".byte 0x83,0xE3,0x18,0x83,0xFB,0x08,0x74,0xE1,0xEB,0x25,0x45,0x39,0xD3,0x40,0x0F,0x94,0xC5,0x84,0xDB,0x0F,0x85,0xA2,0xFE,0xFF\n"
    ".byte 0xFF,0x41,0x08,0xEF,0x0F,0x85,0xF9,0xFE,0xFF,0xFF,0xE9,0x94,0xFE,0xFF,0xFF,0x45,0x31,0xC0,0xEB,0x0A,0x4D,0x89,0xC8,0x4D\n"
    ".byte 0x39,0xC8,0x41,0x0F,0x92,0xC0,0x4C,0x8D,0x0C,0x08,0x49,0x81,0xC1,0x62,0x03,0x00,0x00,0xC7,0x86,0xC4,0x07,0x00,0x00,0x00\n"
    ".byte 0x00,0x00,0x00,0x83,0x3A,0x00,0x74,0x3A,0x41,0x80,0x39,0x00,0x41,0x0F,0x95,0xC2,0x45,0x08,0xC2,0x41,0xBA,0x02,0x00,0x00\n"
    ".byte 0x00,0x75,0x27,0x44,0x8B,0x9C,0x08,0xC4,0x00,0x00,0x00,0x41,0xBA,0x01,0x00,0x00,0x00,0x41,0xF6,0xC3,0x02,0x74,0x13,0x41\n"
    ".byte 0x83,0xE3,0x24,0x45,0x31,0xD2,0x41,0x83,0xFB,0x04,0x41,0x0F,0x94,0xC2,0x41,0x83,0xCA,0x04,0x44,0x39,0x96,0xC0,0x07,0x00\n"
    ".byte 0x00,0x74,0x29,0x44,0x8B,0x9C,0x08,0xC4,0x00,0x00,0x00,0x41,0xF7,0xD3,0x41,0xF6,0xC3,0x18,0x75,0x0A,0xC7,0x86,0xC4,0x07\n"
    ".byte 0x00,0x00,0x02,0x00,0x00,0x00,0x44,0x89,0x96,0xC0,0x07,0x00,0x00,0xC6,0x86,0x30,0x08,0x00,0x00,0x01,0xC7,0x86,0xCC,0x07\n"
    ".byte 0x00,0x00,0x00,0x00,0x00,0x00,0x45,0x31,0xD2,0x83,0x3A,0x02,0x72,0x3A,0x41,0x80,0x39,0x00,0x41,0x0F,0x95,0xC2,0x45,0x08\n"
    ".byte 0xC2,0x41,0xBA,0x02,0x00,0x00,0x00,0x75,0x27,0x44,0x8B,0x9C,0x08,0x94,0x01,0x00,0x00,0x41,0xBA,0x01,0x00,0x00,0x00,0x41\n"
    ".byte 0xF6,0xC3,0x02,0x74,0x13,0x41,0x83,0xE3,0x24,0x45,0x31,0xD2,0x41,0x83,0xFB,0x04,0x41,0x0F,0x94,0xC2,0x41,0x83,0xCA,0x04\n"
    ".byte 0x44,0x39,0x96,0xC8,0x07,0x00,0x00,0x74,0x29,0x44,0x8B,0x9C,0x08,0x94,0x01,0x00,0x00,0x41,0xF7,0xD3,0x41,0xF6,0xC3,0x18\n"
    ".byte 0x75,0x0A,0xC7,0x86,0xCC,0x07,0x00,0x00,0x02,0x00,0x00,0x00,0x44,0x89,0x96,0xC8,0x07,0x00,0x00,0xC6,0x86,0x30,0x08,0x00\n"
    ".byte 0x00,0x01,0xC7,0x86,0xD4,0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x45,0x31,0xD2,0x83,0x3A,0x03,0x72,0x3A,0x41,0x80,0x39,0x00\n"
    ".byte 0x41,0x0F,0x95,0xC2,0x45,0x08,0xC2,0x41,0xBA,0x02,0x00,0x00,0x00,0x75,0x27,0x44,0x8B,0x9C,0x08,0x64,0x02,0x00,0x00,0x41\n"
    ".byte 0xBA,0x01,0x00,0x00,0x00,0x41,0xF6,0xC3,0x02,0x74,0x13,0x41,0x83,0xE3,0x24,0x45,0x31,0xD2,0x41,0x83,0xFB,0x04,0x41,0x0F\n"
    ".byte 0x94,0xC2,0x41,0x83,0xCA,0x04,0x44,0x39,0x96,0xD0,0x07,0x00,0x00,0x74,0x29,0x44,0x8B,0x9C,0x08,0x64,0x02,0x00,0x00,0x41\n"
    ".byte 0xF7,0xD3,0x41,0xF6,0xC3,0x18,0x75,0x0A,0xC7,0x86,0xD4,0x07,0x00,0x00,0x02,0x00,0x00,0x00,0x44,0x89,0x96,0xD0,0x07,0x00\n"
    ".byte 0x00,0xC6,0x86,0x30,0x08,0x00,0x00,0x01,0xC7,0x86,0xDC,0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x45,0x31,0xD2,0x83,0x3A,0x04\n"
    ".byte 0x72,0x35,0x41,0x80,0x39,0x00,0x0F,0x95,0xC2,0x41,0x08,0xD0,0x41,0xBA,0x02,0x00,0x00,0x00,0x75,0x23,0x8B,0x94,0x08,0x34\n"
    ".byte 0x03,0x00,0x00,0x41,0xBA,0x01,0x00,0x00,0x00,0xF6,0xC2,0x02,0x74,0x11,0x83,0xE2,0x24,0x45,0x31,0xD2,0x83,0xFA,0x04,0x41\n"
    ".byte 0x0F,0x94,0xC2,0x41,0x83,0xCA,0x04,0x44,0x39,0x96,0xD8,0x07,0x00,0x00,0x75,0x0B,0x80,0xBE,0x30,0x08,0x00,0x00,0x00,0x75\n"
    ".byte 0x27,0xEB,0x5A,0x8B,0x84,0x08,0x34,0x03,0x00,0x00,0xF7,0xD0,0xA8,0x18,0x75,0x0A,0xC7,0x86,0xDC,0x07,0x00,0x00,0x02,0x00\n"
    ".byte 0x00,0x00,0x44,0x89,0x96,0xD8,0x07,0x00,0x00,0xC6,0x86,0x30,0x08,0x00,0x00,0x01,0x48,0x8D,0xBE,0x08,0x05,0x00,0x00,0x48\n"
    ".byte 0x8B,0x9E,0xD0,0x01,0x00,0x00,0x48,0x8B,0x86,0x08,0x05,0x00,0x00,0x48,0x89,0xF9,0xFF,0x50,0x08,0x48,0x98,0x48,0x39,0x7C\n"
    ".byte 0xC3,0x20,0x74,0x20,0xC7,0x86,0xFC,0x07,0x00,0x00,0x00,0x00,0x00,0x00,0xC6,0x86,0x30,0x08,0x00,0x00,0x00,0x48,0x83,0xC4\n"
    ".byte 0x20,0x5B,0x5D,0x5F,0x5E,0x41,0x5C,0x41,0x5E,0x41,0x5F,0xC3,0x48,0x8B,0x44,0xC3,0x28,0x48,0x8B,0x78,0x08,0x48,0x8B,0x58\n"
    ".byte 0x10,0x48,0x39,0xDF,0x74,0xCE,0x66,0x66,0x2E,0x0F,0x1F,0x84,0x00,0x00,0x00,0x00,0x00,0x48,0x8B,0x4F,0x08,0xBA,0x02,0x00\n"
    ".byte 0x00,0x00,0xFF,0x17,0x48,0x83,0xC7,0x10,0x48,0x39,0xDF,0x75,0xEC,0xEB,0xAD\n"
    ".seh_endproc\n"
    ".section .drectve\n"
    ".ascii \" -export:fake_acclim_refresh\"\n"
    ".text\n");
void fake_acclim_refresh(void *self);

static int acclim_gates, acclim_notified;

__attribute__((used, noinline)) void fake_acclim_gates(void *self)
{
    (void)self;
    acclim_gates++;
}

static int acclim_slot(void *self)
{
    (void)self;
    return 0;
}

static void acclim_listener(void *ctx, int code)
{
    (void)ctx;
    acclim_notified += code == 2;
}

static void test_track_acclim(void)
{
    static uint8_t empty[0x840], before[0x840], held[0x840], targets[2][0x370], table[0x30], list[0x18];
    static void *vt[2] = {NULL, (void *)acclim_slot};
    static void *listeners[2] = {(void *)acclim_listener, NULL};
    memcpy(before, empty, sizeof empty);
    fake_acclim_refresh(empty);
    expect(!memcmp(empty, before, sizeof empty) && acclim_gates == 0,
           "acclim: refreshing a Track Acclimatisation programme without targets returns and changes nothing");
    *(uint32_t *)(targets[0] + 4) = 2;
    *(void **)(held + 0x350) = targets;
    *(void **)(held + 0x358) = targets + 2;
    *(int32_t *)(held + 0x7e8) = 5;
    *(void **)(held + 0x508) = vt;
    *(void **)(held + 0x1d0) = table;
    *(void **)(table + 0x20) = held + 0x508;
    *(void **)(table + 0x28) = list;
    *(void **)(list + 8) = listeners;
    *(void **)(list + 0x10) = listeners + 2;
    fake_acclim_refresh(held);
    uint32_t g0 = *(uint32_t *)(held + 0x7c0), g1 = *(uint32_t *)(held + 0x7c8), g2 = *(uint32_t *)(held + 0x7d0);
    int32_t lap = *(int32_t *)(held + 0x7e8);
    printf("      refresh with 2 targets: gate states %u %u %u, lap mark %d, dirty %u, gates %d, notified %d\n", g0, g1, g2,
           lap, held[0x830], acclim_gates, acclim_notified);
    expect(g0 == 1 && g1 == 1 && g2 == 0 && lap == 0 && held[0x830] == 0 && acclim_gates == 1 && acclim_notified == 1,
           "acclim: a programme with two targets runs the game's refresh: two gate states set, listeners notified once");
}

static void crash_track_acclim(void)
{
    static uint8_t programme[0x840];
    SetErrorMode(SEM_NOGPFAULTERRORBOX);
    printf("      acclim: refreshing a Track Acclimatisation programme without targets\n");
    fflush(stdout);
    fake_acclim_refresh(programme);
    printf("      acclim: the refresh returned\n");
    fflush(stdout);
}

static void test_settings(int on, uint64_t chain)
{
    HMODULE t = GetModuleHandleW(L"t_settings.dll");
    void (*read)(void) = t ? (void (*)(void))(void *)GetProcAddress(t, "t_settings_read") : NULL;
    expect(read != NULL, "settings: t_settings.dll loaded and exports t_settings_read");
    if (!read)
        return;
    expect(reads_are(read, !on, on ? 1 : 7, on ? -250 : 500, on ? "tier01" : "plain"),
           on ? "settings: reads come from settings.json (bool, int, double, string_view); the type mismatch, the "
                "undeclared setting and another mod's option read their code defaults"
              : "settings: no settings.json, every setting reads its code default");
    wchar_t req[MAX_PATH] = {0}, snap[MAX_PATH + 16];
    GetEnvironmentVariableW(L"SWOFTY_TEST_REQUESTS", req, MAX_PATH);
    int n = sizeof setting_commands / sizeof *setting_commands, same = 1;
    for (int i = 0; i < n; i++) {
        same &= send_command(setting_commands[i]) == chain;
        if (*req) {
            swprintf(snap, MAX_PATH + 16, L"%ls.%d", req, i + 1);
            CopyFileW(req, snap, FALSE);
        }
        if (i == 3) {
            expect(reads_are(read, 1, on ? 9 : 7, on ? 750 : 500, on ? "replica" : "plain"),
                   on ? "settings: after four swofty_set commands the reads follow the new states"
                      : "settings: commands change nothing without settings.json");
            expect(on ? probe[8] == 1 && probe[9] == 9 && probe[10] == 750 && probe_str(11) &&
                            !strcmp(probe_str(11), "replica") && probe[16] == 1 && probe[17] == 1 && probe[18] == 1 &&
                            probe[19] == 1
                      : probe[16] + probe[17] + probe[18] + probe[19] == 0,
                   on ? "settings: SWOFTY_ON_CHANGE watchers got verbose=true, greeting_count=9, offset=0.75, "
                        "flavour='replica', once each"
                      : "settings: no watcher ran");
            if (on)
                expect(probe[11] == probe[27], "settings: the choice string is the same core-owned pointer in the watcher "
                                               "and in later reads");
        }
        if (i == 4)
            expect(probe[20] == 0 && reads_are(read, 1, on ? 9 : 7, on ? 750 : 500, on ? "replica" : "plain"),
                   "settings: a command for the mistyped option runs no watcher and its read keeps the code default");
        if (i == 6)
            expect(on ? probe[8] == 0 && probe[16] == 2 && reads_are(read, 0, 9, 750, "replica") : probe[16] == 0,
                   on ? "settings: verbose back to false, its watcher ran a second time" : "settings: still no watcher");
    }
    expect(same, "settings: FeHome_Handle_Command returns the chain's value for every command (core's wrap keeps rax)");
    for (DWORD t0 = GetTickCount(); GetTickCount() - t0 < 1000 && !closes;) {
        MSG m;
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE))
            DispatchMessageW(&m);
        Sleep(10);
    }
    expect(closes == on, on ? "settings: swofty_restart posted WM_CLOSE to the main window"
                            : "settings: swofty_restart without settings.json closes nothing");
}

extern ATOM(WINAPI *__imp_RegisterClassExW)(const WNDCLASSEXW *);
extern HWND(WINAPI *__imp_CreateWindowExW)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE,
                                           LPVOID);

__declspec(dllexport) __attribute__((noinline)) void fake_crash(volatile int *p)
{
    *p = 1;
    printf("not reached\n");
}

static uintptr_t slot8(void) { return ((uintptr_t (*)(void))OTHER_VT[8])(); }

// A stand-in NeMemoryHeap for the crash report's heap decoding: Ne_ReportMemoryError with the game's
// prologue (the core hooks it by that pattern), Ne_FatalAbort's write to address 1, and the start
// of the heap error method that the heap's vtable carries at +0x118, by which the core recognises a
// heap object. The heap object and its blocks have the field offsets of Ne_Heap_DeallocateNonSmall.
int fake_report_memory_error(int code, const char *msg, char fatal, char brk, uint64_t a5, void *heap);
void fake_fatal_abort(void);
void fake_heap_error_method(void);
__asm__(
    ".section .xtls,\"xr\"\n"
    ".globl fake_report_memory_error\nfake_report_memory_error:\n"
    ".byte 0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x56,0x57,0x55,0x53\n"
    ".byte 0x48,0x81,0xEC,0xB8,0x02,0x00,0x00\n"
    ".byte 0xC5,0x78,0x29,0x8C,0x24,0xA0,0x02,0x00,0x00\n"
    ".byte 0xC5,0x79,0x29,0x84,0x24,0x90,0x02,0x00,0x00\n"
    ".byte 0xC5,0xF9,0x29,0xBC,0x24,0x80,0x02,0x00,0x00\n"
    "mov 0x320(%rsp), %rax\nmov %rax, 0x20(%rsp)\nmov 0x328(%rsp), %rax\nmov %rax, 0x28(%rsp)\n"
    "call report_memory_error_impl\nadd $0x2b8, %rsp\n"
    "pop %rbx\npop %rbp\npop %rdi\npop %rsi\npop %r12\npop %r13\npop %r14\npop %r15\nret\n"
    ".globl fake_fatal_abort\nfake_fatal_abort:\n"
    ".byte 0x48,0x83,0xEC,0x28,0xC7,0x04,0x25,0x01,0x00,0x00,0x00,0xAD,0xDE,0xAD,0xDE\nint3\n"
    ".globl fake_heap_error_method\nfake_heap_error_method:\n"
    ".byte 0x55,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x56,0x57,0x53,0x48,0x83,0xEC,0x18\n"
    ".byte 0x48,0x8D,0x6C,0x24,0x10,0x89,0xD6,0x48,0x89,0xCF,0x48,0x8B,0x5D,0x78\nint3\n"
    ".text\n");

#define HEAP_GUARD 32
static uint64_t fake_heap_obj[0x120 / 8];
static void *fake_heap_vt[0x130 / 8];
static const char fake_heap_name[] = "FakeHeap";
static uint64_t heap_capacity(void *h) { (void)h; return 0x10000; }
static uint64_t heap_free_bytes(void *h) { (void)h; return 0xfe00; }
static uint64_t heap_used(void *h) { (void)h; return 0x200; }
static uint64_t heap_largest(void *h, int a) { (void)h, (void)a; return 0xfc00; }
static const char *heap_name(void *h) { (void)h; return fake_heap_name; }

int report_memory_error_impl(int code, const char *msg, char fatal, char brk, uint64_t a5, void *heap)
{
    printf("      fake Ne_ReportMemoryError(0x%x, \"%s\", fatal %d, heap %p)\n", code, msg, fatal, heap);
    (void)brk, (void)a5;
    if (fatal)
        fake_fatal_abort();
    return code;
}

static uint32_t heap_block_size(uint32_t user) { return ((user + 15) & ~15u) + 2 * HEAP_GUARD + 0x10; }

static uint8_t *heap_block(uint8_t *at, uint32_t user, uint8_t fill)
{
    *(int32_t *)at = 0;
    *(int32_t *)(at + 4) = 0;
    *(uint32_t *)(at + 8) = heap_block_size(user);
    *(uint32_t *)(at + 12) = user;
    memcpy(at + 0x10, "[BLOCKSTART] Bound checked mem.", HEAP_GUARD);
    memset(at + 0x10 + HEAP_GUARD, fill, user);
    memcpy(at + 0x10 + HEAP_GUARD + user, "[BLOCKEND] Bound checked mem.  ", HEAP_GUARD);
    return at + 0x10 + HEAP_GUARD;
}

__declspec(dllexport) __attribute__((noinline)) void fake_heap_free(uint8_t *user)
{
    uint8_t *header = user - HEAP_GUARD - 0x10;
    if (*(uint32_t *)(header + 8) != heap_block_size(*(uint32_t *)(header + 12))) {
        char msg[96];
        snprintf(msg, sizeof msg, "Failed to find address %p in the allocated list", (void *)user);
        fake_report_memory_error(0x16, msg, 1, 0, 0, fake_heap_obj);
    }
}

static void heap_error(void)
{
    uint8_t *base = VirtualAlloc(NULL, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    fake_heap_vt[0x60 / 8] = (void *)heap_capacity;
    fake_heap_vt[0x68 / 8] = (void *)heap_free_bytes;
    fake_heap_vt[0x70 / 8] = (void *)heap_used;
    fake_heap_vt[0x80 / 8] = (void *)heap_largest;
    fake_heap_vt[0xa0 / 8] = (void *)heap_name;
    fake_heap_vt[0x118 / 8] = (void *)fake_heap_error_method;
    fake_heap_obj[0] = (uint64_t)(uintptr_t)fake_heap_vt;
    fake_heap_obj[0x58 / 8] = (uint64_t)(uintptr_t)fake_heap_name;
    fake_heap_obj[0x98 / 8] = (uint64_t)(uintptr_t)base;
    fake_heap_obj[0xa0 / 8] = 0x10000;
    fake_heap_obj[0x100 / 8] = (uint64_t)(uintptr_t)base;
    fake_heap_obj[0x108 / 8] = HEAP_GUARD;
    fake_heap_obj[0x110 / 8] = 0x200;
    uint8_t *a = heap_block(base, 40, 0xaa);
    uint8_t *b = heap_block(base + heap_block_size(40), 64, 0xbb);
    heap_block(base + heap_block_size(40) + heap_block_size(64), 16, 0xcc);
    fake_report_memory_error(0x50, "Start bounding check buffer corrupt", 0, 0, 0, fake_heap_obj);
    static const char record[0x80] = "serialized replay record that is longer than its holder";
    memcpy(a, record, sizeof record - 0x18);
    printf("      copied 0x%x bytes into a 40-byte block; freeing the block after it\n", (unsigned)(sizeof record - 0x18));
    fake_heap_free(b);
}


static int gate_has(uint32_t h)
{
    for (size_t i = 0, start = h % 373; i < 373; i++) {
        uint32_t cur = fake_log_gate.hashes[(start + (13 + h % 13) * i) % 373];
        if (cur == h)
            return 1;
        if (!cur)
            return 0;
    }
    return 0;
}

static DWORD WINAPI gamelog_thread(LPVOID arg)
{
    for (int i = 0; i < 40; i++)
        fake_log_site_c();
    (void)arg;
    return 0;
}

static void test_gamelog(int on)
{
    expect(on ? fake_log_gate.enabled == 1 && gate_has(0x7c7c88cb) && gate_has(0x22222222) && !gate_has(0x11111111)
              : fake_log_gate.enabled == 0 && !gate_has(0x7c7c88cb),
           on ? "gamelog: gate enabled, TEST_WARN and UI_FLOW opened by the default channels, TEST_VERBOSE closed"
              : "gamelog: SWOFTY_GAME_LOG=off leaves the gate closed");
    fake_log_site_b();
    fake_log_site_c();
    for (int i = 0; i < 150; i++)
        fake_log_site_a();
    HANDLE t[4];
    for (int i = 0; i < 4; i++)
        t[i] = CreateThread(NULL, 0, gamelog_thread, NULL, 0, NULL);
    WaitForMultipleObjects(4, t, TRUE, 10000);
    for (int i = 0; i < 4; i++)
        CloseHandle(t[i]);
    NeStr *s = NULL;
    int64_t n = fake_format_string(&s, "Screen %hs failed to open because of %d", "swofty_settings", 3);
    expect(n == (int64_t)strlen("Screen swofty_settings failed to open because of 3") &&
               !strcmp(chars(s), "Screen swofty_settings failed to open because of 3"),
           "gamelog: Ne_Format_String still builds its string with the entry hook installed");
    fake_format_string(&s, "Failed to find screen %hs. Solution: Make sure the the screen is loaded into the runtime",
                       "swofty_settings");
    fake_format_string(&s, "button_%d", 7);
    Sleep(1100);
    fake_format_string(&s, "No screen called %hs exists", "late_screen");
    expect(!strcmp(chars(s), "No screen called late_screen exists"), "gamelog: a later route error is formatted normally");
    nestr_release(&s);
    expect(gamelog_body_calls == 0, "gamelog: the overwritten logger never runs the game's own body");
}

static void stage_early(void)
{
    expect(fake_stage_site() == 7 && fake_stage_wrap() == 9 && fake_stage_plain() == 5 && probe[32] == 0,
           "stages: before mods load, the stage targets are vanilla");
    HANDLE mx = CreateMutexW(NULL, FALSE, L"Codemasters F1 25");
    expect(mx && GetModuleHandleW(L"t_stage_early.dll") && !GetModuleHandleW(L"t_stage_late.dll") &&
               !GetModuleHandleW(L"t_stage_refused.dll"),
           "stages: CreateMutexW('Codemasters F1 25') loaded the early plugin, the window-stage plugins wait");
    int site = fake_stage_site(), wrap = fake_stage_wrap();
    printf("      after the early stage: site target %d (site handler ran %lld time(s)), wrap target %d\n", site,
           (long long)probe[32], wrap);
    expect(site == 7 && probe[32] == 1 && wrap == 91,
           "stages: the early plugin's site and wrap are installed at the entries of both targets");
    if (mx)
        CloseHandle(mx);
}

static int stage_window(HWND w)
{
    printf("      window stage: fn<Fake_Stage_Site> = %p (function at %p) returned %lld; fn<Fake_Stage_Wrap> = %p "
           "(function at %p) returned %lld; refused plugin marker %lld\n",
           (void *)(intptr_t)probe[33], (void *)fake_stage_site, (long long)probe[34], (void *)(intptr_t)probe[35],
           (void *)fake_stage_wrap, (long long)probe[36], (long long)probe[37]);
    expect(GetModuleHandleW(L"t_stage_late.dll") && GetModuleHandleW(L"t_stage_refused.dll"),
           "stages: the window-stage plugins load with the window");
    expect(probe[33] == (intptr_t)fake_stage_site,
           "stages: fn<Fake_Stage_Site> bound at the window stage resolves to the function under the early site hook");
    expect(probe[34] == 7 && probe[32] == 2,
           "stages: the call through fn<Fake_Stage_Site> runs the early site handler and returns the game's 7");
    expect(probe[35] == (intptr_t)fake_stage_wrap,
           "stages: fn<Fake_Stage_Wrap> bound at the window stage resolves to the function under the early wrap");
    expect(probe[36] == 91, "stages: the call through fn<Fake_Stage_Wrap> returns the early wrap's 91");
    expect(probe[37] == 0, "stages: the plugin whose patterns fail without a hook to explain it is refused and runs nothing");
    expect(probe[40] == 5, "stages: fn<Fake_Stage_Plain>, whose KB address holds other bytes, is found by a scan and runs");
    expect(fake_stage_site() == 7 && probe[32] == 3 && fake_stage_wrap() == 91 && fake_stage_plain() == 5,
           "stages: the game's own calls still go through the early hooks, the unhooked target is vanilla");
    printf("%s (%d failure(s))\n", fails ? "FAILED" : "ALL PASSED", fails);
    DestroyWindow(w);
    return fails != 0;
}

static DWORD WINAPI launch_error_box(LPVOID unused)
{
    (void)unused;
    MessageBoxW(NULL, L"Game launch error. Error code: 0x00000004", L"Error", MB_OK);
    return 0;
}

/* The EA App build's launch errors come up as a dialog before any game window: the loader must log it and keep
   waiting instead of loading addons on it. */
static void show_launch_error(void)
{
    HANDLE t = CreateThread(NULL, 0, launch_error_box, NULL, 0, NULL);
    HWND box = NULL;
    for (DWORD t0 = GetTickCount(); !box && GetTickCount() - t0 < 5000; Sleep(20))
        box = FindWindowW(L"#32770", L"Error");
    Sleep(600);
    if (box)
        PostMessageW(box, WM_COMMAND, IDOK, 0);
    WaitForSingleObject(t, 5000);
    CloseHandle(t);
    expect(box != NULL, "fallback mode: a launch error dialog came up before the window");
}

#ifdef PACKED_TEST
// Two full pages of filler so every page the core samples is covered by encrypted bytes.
__asm__(".section .xtls,\"xr\"\n.fill 0x2000, 1, 0xCC\n.text\n");
#endif

// The packed build: run_wine_test.sh encrypts .xtls in the file with this xorshift stream, like the EA App
// build whose code reads encrypted at load; the exe decrypts it in bulk before its window, as that build does.
#define PACK_SEED 0x5EEDC0DEu

static IMAGE_SECTION_HEADER *exe_section(const char *name)
{
    uint8_t *b = (uint8_t *)GetModuleHandleW(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(b + ((IMAGE_DOS_HEADER *)b)->e_lfanew);
    IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, s++)
        if (!strncmp((const char *)s->Name, name, 8))
            return s;
    return NULL;
}

static double page_entropy(const uint8_t *p)
{
    unsigned cnt[256] = {0};
    for (int i = 0; i < 0x1000; i++)
        cnt[p[i]]++;
    double h = 0;
    for (int i = 0; i < 256; i++)
        if (cnt[i]) {
            double pr = cnt[i] / 4096.0;
            h -= pr * log2(pr);
        }
    return h;
}

static void decrypt_code(uint8_t *p, size_t n)
{
    DWORD old;
    VirtualProtect(p, n, PAGE_EXECUTE_READWRITE, &old);
    uint32_t x = PACK_SEED;
    for (size_t i = 0; i < n; i++) {
        x ^= x << 13, x ^= x >> 17, x ^= x << 5;
        p[i] ^= (uint8_t)(x >> 8);
    }
    VirtualProtect(p, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, n);
}

/* packed: .xtls reads encrypted at the early stage and is decrypted before the window. packed_never: it never
   decrypts. packed_plain: the same flow on the exe left unencrypted, with a set that is not packed. */
static void expect_in(const char *mode, int ok, const char *what)
{
    printf("%s: %s: %s\n", ok ? "ok  " : "FAIL", mode, what);
    fails += !ok;
}

static int packed_main(const char *mode)
{
    int encrypted = strcmp(mode, "packed_plain") != 0, decrypts = strcmp(mode, "packed_never") != 0;
    IMAGE_SECTION_HEADER *s = exe_section(".xtls");
    uint8_t *code = (uint8_t *)GetModuleHandleW(NULL) + s->VirtualAddress;
    size_t n = s->Misc.VirtualSize;
    uint8_t *before = malloc(n);
    memcpy(before, code, n);
    double e = page_entropy(code);
    printf("      %s: .xtls is %zu bytes, its first page reads entropy %.2f\n", mode, n, e);
    expect_in(mode, encrypted ? e > 7.5 : e < 6.5, encrypted ? "the game code section reads encrypted at load"
                                                    : "the game code section reads as code at load");
    HANDLE mx = CreateMutexW(NULL, FALSE, L"Codemasters F1 25");
    expect_in(mode, mx && GetModuleHandleW(L"swofty_core.dll") && GetModuleHandleW(L"t_packed.dll") &&
               GetModuleHandleW(L"t_packed_scan.dll"),
           "CreateMutexW loaded the core and the early plugins");
    if (encrypted)
        expect_in(mode, !memcmp(before, code, n), "nothing was written into the encrypted code at the early stage");
    if (decrypts && encrypted) {
        decrypt_code(code, n);
        printf("      packed: decrypted .xtls in bulk, first page entropy now %.2f\n", page_entropy(code));
        expect_in(mode, fake_stage_wrap() == 9 && probe[41] == 0, "before the window stage the decrypted code runs unhooked");
    }
    WNDCLASSEXW wc = {.cbSize = sizeof wc, .lpfnWndProc = wndproc, .hInstance = GetModuleHandleW(NULL),
                      .lpszClassName = L"EGO_WindowClass"};
    __imp_RegisterClassExW(&wc);
    HWND w = __imp_CreateWindowExW(0, L"EGO_WindowClass", L"F1 25", WS_OVERLAPPEDWINDOW, 0, 0, 320, 200, NULL, NULL,
                                   wc.hInstance, NULL);
    if (!decrypts) {
        Sleep(10500);
        expect_in(mode, !memcmp(before, code, n), "no hook was ever written into the code that never decrypted");
        expect_in(mode, probe[41] == 0 && probe[44] == 0 && probe[43] == 0,
               "no handler of the early plugins ran and no scan found anything");
    } else {
        run_build(1, 1);
        expect_in(mode, ncalls == 3 && !strcmp(calls[1].name, "career") && calls[1].index == 3 && !movie_built(),
               "the core's site on the home builder fires after the window stage (mod tile added, movie tile skipped)");
        expect_in(mode, fake_stage_wrap() == 89, "t_packed's wrap on Fake_Stage_Wrap runs");
        expect_in(mode, fake_stage_site() == 7 && probe[41] == 1, "t_packed's site on the entry of Fake_Stage_Site runs");
        expect_in(mode, probe[44] == 1, "t_packed's on_load handler ran once");
        expect_in(mode, probe[43] == (intptr_t)fake_stage_plain && probe[42] == (encrypted ? 2 : 1),
               encrypted ? "t_packed_scan's init was refused its scan of encrypted code, then found "
                           "Fake_Stage_Plain when called again"
                         : "t_packed_scan's init found Fake_Stage_Plain on its first call");
    }
    printf("%s (%d failure(s))\n", fails ? "FAILED" : "ALL PASSED", fails);
    DestroyWindow(w);
    return fails != 0;
}

/* nearfull / nopool: take every free 64 KB slot within 2 GB of the image before the core loads, the way the
   game's own allocations have by the window stage on a build whose core loads there. nopool first gives
   back the hook pool the loader reserved at attach. */
static int exhaust_near(int release_pool)
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    wcscpy(wcsrchr(path, L'\\') + 1, L"version.dll");
    HMODULE loader = GetModuleHandleW(path);
    int (*take)(uintptr_t *, size_t *) = loader ? (void *)GetProcAddress(loader, "SwoftyLoaderHookPool") : NULL;
    uintptr_t pool = 0;
    size_t pool_size = 0;
    if (release_pool && take && take(&pool, &pool_size))
        VirtualFree((void *)pool, 0, MEM_RELEASE);
    uintptr_t base = (uintptr_t)GetModuleHandleW(NULL);
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
    uintptr_t lo = base - 0x80010000ULL, hi = base + nt->OptionalHeader.SizeOfImage + 0x80010000ULL;
    MEMORY_BASIC_INFORMATION m;
    int taken = 0;
    for (uintptr_t p = lo; p < hi && VirtualQuery((void *)p, &m, sizeof m); p = (uintptr_t)m.BaseAddress + m.RegionSize) {
        uintptr_t a = (p + 0xFFFF) & ~(uintptr_t)0xFFFF, e = (uintptr_t)m.BaseAddress + m.RegionSize;
        e = (e < hi ? e : hi + 0xFFFF) & ~(uintptr_t)0xFFFF;
        if (m.State == MEM_FREE && a < e && VirtualAlloc((void *)a, e - a, MEM_RESERVE, PAGE_NOACCESS))
            taken++;
    }
    printf("      reserved %d free region(s) within 2 GB of the image%s\n", taken,
           release_pool ? (pool ? ", after releasing the loader's hook pool" : ", the loader had no hook pool") : "");
    return taken;
}

int main(int argc, char **argv)
{
    int fallback = argc > 1 && !strcmp(argv[1], "fallback"), vm = argc > 1 && !strcmp(argv[1], "vm");
    int session = argc > 1 && !strcmp(argv[1], "session"), settings = argc > 1 && !strncmp(argv[1], "settings", 8);
    int stages = argc > 1 && !strcmp(argv[1], "stages");
    int nopool = argc > 1 && !strcmp(argv[1], "nopool");
    setvbuf(stdout, NULL, _IONBF, 0);
    fake_log_gate.hashes[0x22222222 % 373] = 0xbeef;
    for (int i = 0; i < 40; i++)
        career_vt_storage[i] = other_vt_storage[i] = (void *)stub_zero;
    CAREER_VT[4] = (void *)career_populate;
    CAREER_VT[5] = (void *)id_action;
    OTHER_VT[4] = (void *)fake_base_populate;
    OTHER_VT[5] = (void *)id_action;
    if (argc > 1 && !strncmp(argv[1], "packed", 6))
        return packed_main(argv[1]);

    DWORD dummy, size = GetFileVersionInfoSizeW(L"C:\\windows\\system32\\kernel32.dll", &dummy);
    void *buf = malloc(size ? size : 1);
    VS_FIXEDFILEINFO *fi = NULL;
    UINT len = 0;
    int vok = size && GetFileVersionInfoW(L"C:\\windows\\system32\\kernel32.dll", 0, size, buf) &&
              VerQueryValueW(buf, L"\\", (void **)&fi, &len) && fi;
    expect(vok, "version.dll forwarding (GetFileVersionInfoSizeW/GetFileVersionInfoW/VerQueryValueW on kernel32)");
    if (vok)
        printf("      kernel32 version %u.%u.%u.%u\n", HIWORD(fi->dwFileVersionMS), LOWORD(fi->dwFileVersionMS),
               HIWORD(fi->dwFileVersionLS), LOWORD(fi->dwFileVersionLS));

    run_build(1, 1);
    expect(ncalls == 3 && calls[1].index == 3 && movie_built(), "before mods load: vanilla order career=2 movie=3 world=4");
    expect(fake_handle_command(0, 0) == 1 && slot8() == 0 && fake_caller() == 1 && fake_patchable() == 1,
           "before mods load: wrap, vtable, redirect and patch targets are vanilla");

    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    void *real_rc = (void *)GetProcAddress(u32, "RegisterClassExW"), *real_cw = (void *)GetProcAddress(u32, "CreateWindowExW");
    expect(fallback || ((void *)__imp_RegisterClassExW != real_rc && (void *)__imp_CreateWindowExW != real_cw),
           "IAT trigger armed on USER32!RegisterClassExW and CreateWindowExW before the window exists");
    if (stages)
        stage_early();
    if (nopool || (argc > 1 && !strcmp(argv[1], "nearfull")))
        expect(exhaust_near(nopool) > 0, "the address space within 2 GB of the image is taken before the core loads");
    ATOM(WINAPI * rc)(const WNDCLASSEXW *) = fallback ? (void *)real_rc : (void *)__imp_RegisterClassExW;
    HWND(WINAPI * cw)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID) =
        fallback ? (void *)real_cw : (void *)__imp_CreateWindowExW;
    WNDCLASSEXW wc = {.cbSize = sizeof wc, .lpfnWndProc = wndproc, .hInstance = GetModuleHandleW(NULL),
                      .lpszClassName = L"EGO_WindowClass"};
    if (vm) {
        WNDCLASSEXW vc = wc;
        vc.lpszClassName = L"VmThunkClass";
        vm_thunk_register(&vc);
        expect((void *)__imp_RegisterClassExW != real_rc, "vm mode: call from a writable+exec section does not fire the trigger");
        run_build(1, 1);
        expect(ncalls == 3 && movie_built(), "vm mode: build after the skipped call is still vanilla");
    }
    rc(&wc);
    HWND w = cw(0, L"EGO_WindowClass", L"F1 25", WS_OVERLAPPEDWINDOW, 0, 0, 320, 200, NULL, NULL, wc.hInstance, NULL);
    expect((void *)__imp_RegisterClassExW == real_rc && (void *)__imp_CreateWindowExW == real_cw || fallback,
           "IAT slots restored after the trigger");
    if (stages)
        return stage_window(w);
    if (nopool) {
        printf("%s (%d failure(s))\n", fails ? "FAILED" : "ALL PASSED", fails);
        DestroyWindow(w);
        return fails != 0;
    }

    run_build(1, 1);
    if (fallback)
        expect(ncalls == 3 && movie_built(), "fallback mode: first build before the window is visible is vanilla");
    if (fallback) {
        show_launch_error();
        run_build(1, 1);
        expect(ncalls == 3 && movie_built(), "fallback mode: a dialog before the window does not load addons");
    }
    else
        expect(ncalls == 3 && calls[1].index == 3 && !movie_built(),
               "first home build right after window creation already has the mod tile and no movie tile (career=2, "
               "mod=3, world=4)");

    ShowWindow(w, SW_SHOW);
    for (DWORD t0 = GetTickCount(); GetTickCount() - t0 < 1500;) {
        MSG m;
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE))
            DispatchMessageW(&m);
        Sleep(20);
    }

    run_build(1, 1);
    expect(ncalls == 3 && !movie_built(), "hooked build with the movie flag set: movie tile skipped, 3 adder calls");
    expect(ncalls == 3 && !strcmp(calls[1].name, "career") && !strcmp(calls[2].name, "f1_world") && calls[0].index == 2 &&
               calls[1].index == 3 && calls[2].index == 4,
           "hooked: career=2, mod tile=3, world=4");
    expect(out_r12 == 3 && out_r15 == 4, "hooked: r12/r15 renumbered (3/4)");
    expect(build_leak == 0, "hooked build, populate and getAction leave no game string reference behind "
                            "(replaced Career id released, title/description/action strings released)");
    expect(probe[1] == 3 && probe[6] >= 1, "site chain: t_chain_a (priority -10) runs after core and sees the r12 it advanced");
    uint64_t chain = fake_handle_command(0, 0);
    printf("      FeHome_Handle_Command chain result %llu\n", (unsigned long long)chain);
    expect(chain == (session ? 132 : 1432),
           session ? "session: undeclared t_chain_b refused, chain t_chain_c(10) > swofty_core(0, passes through) > "
                     "t_chain_a(0) > game"
                   : "wrap chain on FeHome_Handle_Command: t_chain_c(10) > swofty_core(0, passes through) > t_chain_a(0) > "
                     "t_chain_b(0, loaded later) > game");
    expect(slot8() == 15, "vtable chain on one slot: t_chain_c(10) * 3 around t_chain_a(0) + 5");
    expect(fake_caller() == 101, "redirect: the call reaches the mod, orig is the old callee");
    expect(fake_patchable() == 2, "patch: expect bytes checked, new bytes written");
    expect(probe[0] == 1, "SWOFTY_ON_LOAD ran after the commit; fields are per object and released");
    expect(probe[2] == 1 && probe[3] == 1, "v1 plugin gets API v1 after the mixin commit (wrap already installed)");
    expect(probe[4] == 0 && probe[5] == 0, "dropped mods (unresolved wrap target, unresolved call) run nothing");
    Vec *v = (Vec *)(list + 0x78);
    uint8_t *mod = *(uint8_t **)(((uint8_t **)v->begin)[3] + 0xd0);
    uint8_t *career = *(uint8_t **)(((uint8_t **)v->begin)[1] + 0xd0);
    VmNode *mn = *(VmNode **)(mod + 0x20), *cn = *(VmNode **)(career + 0x20);
    Action a = {0};
    ((Action * (*)(uint8_t *, Action *))(*(void ***)mod)[5])(mod, &a);
    expect(!strcmp(chars(*(NeStr **)(mod + 0x10)), "mod_menu"), "mod item id = mod_menu");
    expect(mn && !strcmp(mn->title, "ADDON SETTINGS") && mn->title_localised == 0, "mod item title 'ADDON SETTINGS', not localised");
    expect(a.type == 1 && !strcmp(chars(a.str), "mod_menu"), "mod item action {1, mod_menu}");
    expect(cn && !strcmp(cn->title, "lng_mode_career") && cn->title_localised == 1, "career item untouched");
    expect((*(void ***)mod)[0] == CAREER_VT[0] && (*(void ***)mod)[-1] == CAREER_VT[-1], "mod vtable keeps career slots 0 and -1");

    expect((void *)__imp_RegisterClassExW == real_rc && (void *)__imp_CreateWindowExW == real_cw,
           "IAT slots pristine after mods loaded");

    run_build(0, 0);
    expect(ncalls == 3 && calls[0].index == 1 && calls[1].index == 2 && calls[2].index == 3, "hooked without movie: career=1, mod=2, world=3");
    expect(out_r12 == 2 && out_r15 == 3, "hooked without movie: r12/r15 = 2/3");
    expect(build_leak == 0, "second hooked build leaves no game string reference behind");

    test_settings(settings, chain);
    test_items(settings);
    if (argc > 1 && !strcmp(argv[1], "trigger")) {
        test_ers_newlap();
        test_track_acclim();
    }
    HMODULE ta = GetModuleHandleW(L"t_assets.dll");
    void (*assets_late)(void) = ta ? (void (*)(void))(void *)GetProcAddress(ta, "t_assets_late") : NULL;
    if (assets_late)
        assets_late();

    if (argc > 1 && !strcmp(argv[1], "trigger")) {
        fake_flow();
        Sleep(40);
        fake_flow();
        fake_flow();
        printf("      drove the flow tick three times: two frames 40 ms apart, the second with a nested tick\n");
        expect((probe[39] & 3) == 3, "events: at ON_LOAD, before the events setup, t_events_early saw available() true and "
                                     "on_frame ok");
        expect(probe[38] == 2, "events: the frame callback t_events_early queued at ON_LOAD fired once per frame after "
                               "the setup armed it, not on the nested tick");
    }

    if (argc > 1 && (!strcmp(argv[1], "trigger") || !strcmp(argv[1], "vm") || !strcmp(argv[1], "gamelog_off")))
        test_gamelog(strcmp(argv[1], "gamelog_off") != 0);
    printf("%s (%d failure(s))\n", fails ? "FAILED" : "ALL PASSED", fails);
    if (argc > 1 && !strcmp(argv[1], "crash")) {
        SetErrorMode(SEM_NOGPFAULTERRORBOX);
        fake_crash((volatile int *)0x10);
    }
    if (argc > 1 && !strcmp(argv[1], "exit"))
        ExitProcess(255);
    if (argc > 1 && !strcmp(argv[1], "acclim"))
        crash_track_acclim();
    if (argc > 1 && !strcmp(argv[1], "heap")) {
        SetErrorMode(SEM_NOGPFAULTERRORBOX);
        for (int i = 0; i < 3; i++)
            fake_log_site_a();
        heap_error();
    }
    if (argc > 1 && !strcmp(argv[1], "abort")) {
        SetErrorMode(SEM_NOGPFAULTERRORBOX);
        fake_fatal_abort();
    }
    DestroyWindow(w);
    return fails != 0;
}
