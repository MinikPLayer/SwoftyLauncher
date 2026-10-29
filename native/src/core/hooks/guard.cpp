#include <windows.h>

#include "guard.hpp"

namespace swofty_core::hooks::guard {
namespace {

void thrown_what(const EXCEPTION_RECORD *r, char *out, size_t cap)
{
    out[0] = 0;
    if (r->ExceptionCode != HANDLER_THREW || r->NumberParameters < 1 || !r->ExceptionInformation[0])
        return;
    auto text = reinterpret_cast<const char *>(r->ExceptionInformation[0]);
    if (IsBadReadPtr(text, 1))
        return;
    size_t i = 0;
    for (; i + 1 < cap && text[i]; i++)
        out[i] = text[i];
    out[i] = 0;
}

int isolate_filter(EXCEPTION_POINTERS *e, Seen *seen)
{
    DWORD code = e->ExceptionRecord->ExceptionCode;
    if (!recoverable(code))
        return EXCEPTION_CONTINUE_SEARCH;
    seen->code = code;
    seen->at = reinterpret_cast<uintptr_t>(e->ExceptionRecord->ExceptionAddress);
    thrown_what(e->ExceptionRecord, seen->what, sizeof seen->what);
    return EXCEPTION_EXECUTE_HANDLER;
}

int blame_filter(EXCEPTION_POINTERS *e, BlameFn blame, const void *ctx)
{
    DWORD code = e->ExceptionRecord->ExceptionCode;
    if (recoverable(code) && blame) {
        char what[160];
        thrown_what(e->ExceptionRecord, what, sizeof what);
        blame(ctx, code, reinterpret_cast<uintptr_t>(e->ExceptionRecord->ExceptionAddress), what);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

}

bool recoverable(unsigned long code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_INT_OVERFLOW:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_DATATYPE_MISALIGNMENT:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_INVALID_OPERATION:
    case MSVC_CXX:
    case GCC_CXX:
    case HANDLER_THREW:
        return true;
    default:
        return false;
    }
}

int isolated_call(SwoftyHookFn fn, void *user, SwoftyHookCall *call, Seen *seen)
{
    __try {
        return fn(user, call);
    } __except (isolate_filter(GetExceptionInformation(), seen)) {
        return 0;
    }
}

bool isolated_service(SwoftyServiceFn fn, void *user, const void *table, size_t size, uint32_t version, Seen *seen)
{
    __try {
        fn(user, table, size, version);
        return true;
    } __except (isolate_filter(GetExceptionInformation(), seen)) {
        return false;
    }
}

int blamed_call(SwoftyHookFn fn, void *user, SwoftyHookCall *call, BlameFn blame, const void *ctx)
{
    __try {
        return fn(user, call);
    } __except (blame_filter(GetExceptionInformation(), blame, ctx)) {
        return 0;
    }
}

}
