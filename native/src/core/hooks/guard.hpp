#pragma once
#include <cstdint>

#include "swofty_api.h"

// The handler guard: an SEH frame around one SwoftyHookFn call. isolated_call catches access
// violations, C++ exceptions and the other faults a handler can recover from, and reports the code
// and address in seen. blamed_call lets every fault through to the crash path after calling blame,
// so the log and the black box name the handler first. guard.cpp is the only file built with
// -fms-extensions.

namespace swofty_core::hooks::guard {

constexpr unsigned long MSVC_CXX = 0xE06D7363, GCC_CXX = 0x20474343;
// What the SDK entry functions raise once a C++ exception from the addon's handler has been caught in
// the addon's own runtime (swofty::detail::handler_threw_code); its one parameter points at what().
constexpr unsigned long HANDLER_THREW = 0xE0535746;

struct Seen {
    unsigned long code;
    uintptr_t at;
    char what[160];
};

using BlameFn = void (*)(const void *ctx, unsigned long code, uintptr_t at, const char *what);

int isolated_call(SwoftyHookFn fn, void *user, SwoftyHookCall *call, Seen *seen);
int blamed_call(SwoftyHookFn fn, void *user, SwoftyHookCall *call, BlameFn blame, const void *ctx);
bool recoverable(unsigned long code);
bool isolated_service(SwoftyServiceFn fn, void *user, const void *table, size_t size, uint32_t version, Seen *seen);

}
