// Fake game for the test agent's Wine test: the window trigger loads the loader and the core, then the
// main loop drives the flow tick (a copy of the Vm_Update_FlowStateMachine prologue, so the events
// module's hook resolves) every 20 ms for T_SECONDS, the way the game's frames would. The agent arms or
// refuses on its own from SWOFTY_TEST, SWOFTY_TEST_TOKEN and the mailbox run_testagent_test.sh writes.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

__asm__(".section .xtls,\"xr\"\n"
        ".globl fake_flow\nfake_flow:\n"
        ".byte 0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x56,0x57,0x55,0x53\n"
        ".byte 0x48,0x81,0xEC,0x58,0x02,0x00,0x00\n"
        ".byte 0xC5,0xF8,0x29,0xB4,0x24,0x40,0x02,0x00,0x00\n"
        ".byte 0xC5,0xF9,0x28,0xF2\n"
        "vmovaps 0x240(%rsp), %xmm6\nadd $0x258, %rsp\n"
        "pop %rbx\npop %rbp\npop %rdi\npop %rsi\npop %r12\npop %r13\npop %r14\npop %r15\nret\n"
        ".text\n");
void fake_flow(void);

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    DWORD dummy;
    printf("%s version.dll forwarding works\n",
           GetFileVersionInfoSizeW(L"C:\\windows\\system32\\kernel32.dll", &dummy) ? "ok  :" : "FAIL:");
    WNDCLASSEXW wc = {.cbSize = sizeof wc, .lpfnWndProc = DefWindowProcW, .hInstance = GetModuleHandleW(NULL),
                      .lpszClassName = L"EGO_WindowClass"};
    extern void *__imp_RegisterClassExW;
    ATOM(WINAPI * rc)(const WNDCLASSEXW *) = (void *)__imp_RegisterClassExW;
    rc(&wc);
    int ok = GetModuleHandleW(L"swofty_core.dll") != NULL;
    printf("%s the window trigger loaded the core\n", ok ? "ok  :" : "FAIL:");
    const char *s = getenv("T_SECONDS");
    DWORD until = GetTickCount() + (DWORD)(s ? atoi(s) : 10) * 1000;
    unsigned ticks = 0;
    while ((int)(until - GetTickCount()) > 0) {
        fake_flow();
        ticks++;
        Sleep(20);
    }
    printf("      drove the flow tick %u times\n", ticks);
    ExitProcess(ok ? 0 : 1);
}
