// Wine test for the 1.6 handler guard (src/core/hooks/guard.cpp) built the way native/build.sh builds it:
// zig c++ -target x86_64-windows-gnu -fms-extensions. The host hooks test swaps in a C++ try/catch
// Platform, so nothing else runs the real __try/__except frames. Each case runs in its own process
// (guard_test.exe <case>) because a failing case takes the process down. Cases: a clean handler; an
// access violation with no C++ locals; an access violation in a handler that has C++ locals to destroy
// (every SDK handler does: std::function and swofty::events::Context); a libc++ throw with and without
// locals; an MSVC-code exception; an SDK-shaped after handler (std::function + a Context-like local)
// that faults; blamed_call letting the fault reach an outer frame.
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <swofty/hooks.hpp>
#include <swofty/services.hpp>

#include "../../src/core/hooks/guard.hpp"

using namespace swofty_core::hooks::guard;

static void say(bool ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    fflush(stdout);
}

static int fine(void *, SwoftyHookCall *)
{
    return 5;
}

static int av_plain(void *, SwoftyHookCall *)
{
    volatile int *p = nullptr;
    *p = 1;
    return 7;
}

__attribute__((noinline)) static void poke(size_t n)
{
    volatile size_t *p = nullptr;
    *p = n;
}

static int av_locals(void *, SwoftyHookCall *)
{
    std::string s(64, 'x');
    std::vector<int> v(16, 1);
    poke(s.size() + v.size());
    return 7;
}

static int throw_plain(void *, SwoftyHookCall *)
{
    throw 42;
}

static int throw_locals(void *, SwoftyHookCall *)
{
    return swofty::detail::guarded_entry([] {
        std::string s(64, 'x');
        std::vector<int> v(16, 1);
        if (s.size() + v.size())
            throw std::runtime_error("stoi");
        return 7;
    });
}

static int msvc_code(void *, SwoftyHookCall *)
{
    RaiseException(MSVC_CXX, 0, 0, nullptr);
    return 7;
}

struct FakeContext {
    explicit FakeContext(const void *h) : handle(h) {}
    const void *handle;
    std::optional<std::string> content;
    std::optional<std::vector<std::string>> participants;
};

struct Payload {
    uint32_t size, payload_version;
    int value;
};

static int sdk_after_entry(void *user, SwoftyHookCall *call)
{
    (*static_cast<std::function<void(const Payload &, const FakeContext &)> *>(user))(
        *static_cast<const Payload *>(call->payload), FakeContext(call->context));
    return 0;
}

static int blamed;

struct ServiceTable {
    static constexpr const char *name = "t.service";
    static constexpr uint32_t version = 1;
    uint32_t size;
};

static std::string blamed_what;

static void blame(const void *, unsigned long, uintptr_t, const char *what)
{
    blamed++;
    blamed_what = what ? what : "";
}

static int outer(SwoftyHookFn fn)
{
    __try {
        SwoftyHookCall c{sizeof c, 0, "t.point", nullptr, 0, nullptr};
        blamed_call(fn, nullptr, &c, blame, nullptr);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 1;
    }
}

static bool isolated(SwoftyHookFn fn, void *user, unsigned long want, const char *what = nullptr)
{
    Payload pl{sizeof pl, 1, 3};
    SwoftyHookCall c{sizeof c, 2, "t.point", &pl, 0, nullptr};
    Seen s{0, 0, {}};
    int r = isolated_call(fn, user, &c, &s);
    printf("      returned %d, seen code 0x%08lx, what '%s'\n", r, s.code, s.what);
    if (what && strcmp(s.what, what))
        return false;
    bool caught = false;
    try {
        throw std::runtime_error("after");
    } catch (const std::exception &) {
        caught = true;
    }
    return r == 0 && s.code == want && caught;
}

int main(int argc, char **argv)
{
    const char *k = argc > 1 ? argv[1] : "";
    bool ok = false;
    if (!strcmp(k, "fine")) {
        SwoftyHookCall c{sizeof c, 2, "t.point", nullptr, 0, nullptr};
        Seen s{0, 0, {}};
        ok = isolated_call(fine, nullptr, &c, &s) == 5 && !s.code;
        say(ok, "fine: a clean handler's result passes through");
    } else if (!strcmp(k, "av_plain")) {
        ok = isolated(av_plain, nullptr, EXCEPTION_ACCESS_VIOLATION);
        say(ok, "av_plain: an access violation with no C++ locals is isolated");
    } else if (!strcmp(k, "av_locals")) {
        ok = isolated(av_locals, nullptr, EXCEPTION_ACCESS_VIOLATION);
        say(ok, "av_locals: an access violation in a handler with C++ locals is isolated");
    } else if (!strcmp(k, "throw_plain")) {
        ok = isolated(throw_plain, nullptr, GCC_CXX);
        say(ok, "throw_plain: a libc++ throw with no locals is isolated");
    } else if (!strcmp(k, "throw_locals")) {
        ok = isolated(throw_locals, nullptr, HANDLER_THREW, "stoi");
        say(ok, "throw_locals: a libc++ throw from a handler with C++ locals, through the SDK's guarded entry, is isolated");
    } else if (!strcmp(k, "msvc_code")) {
        ok = isolated(msvc_code, nullptr, MSVC_CXX);
        say(ok, "msvc_code: an MSVC C++ exception code is isolated");
    } else if (!strcmp(k, "sdk_av")) {
        auto *fn = new std::function<void(const Payload &, const FakeContext &)>(
            [](const Payload &p, const FakeContext &) { poke(size_t(p.value)); });
        ok = isolated(sdk_after_entry, fn, EXCEPTION_ACCESS_VIOLATION);
        say(ok, "sdk_av: an SDK-shaped after handler (std::function + Context) that faults is isolated");
    } else if (!strcmp(k, "sdk_throw")) {
        auto *fn = new std::function<void(const Payload &, const swofty::hooks::Outcome &)>(
            [](const Payload &, const swofty::hooks::Outcome &) {
                std::string local(64, 'y');
                throw std::runtime_error("handler fault");
            });
        ok = isolated(&swofty::hooks::detail::after_entry<Payload>, fn, HANDLER_THREW, "handler fault");
        say(ok, "sdk_throw: an SDK after handler (hooks::after) that throws is isolated and its what() reaches the core");
    } else if (!strcmp(k, "sdk_service_throw")) {
        static const ServiceTable table{sizeof(ServiceTable)};
        auto *fn =
            new std::function<void(const ServiceTable &)>([](const ServiceTable &) { throw std::out_of_range("at"); });
        Seen s{0, 0, {}};
        bool done = isolated_service(&swofty::services::detail::when_entry<ServiceTable>, fn, &table, sizeof table, 1, &s);
        printf("      returned %d, seen code 0x%08lx, what '%s'\n", done, s.code, s.what);
        ok = !done && s.code == HANDLER_THREW && !strcmp(s.what, "at");
        say(ok, "sdk_service_throw: a services::when callback that throws is isolated");
    } else if (!strcmp(k, "sdk_blamed")) {
        auto *fn = new std::function<bool(Payload &)>([](Payload &) -> bool { throw std::runtime_error("replace fault"); });
        blamed = 0;
        int r = 0;
        __try {
            Payload pl{sizeof pl, 1, 3};
            SwoftyHookCall c{sizeof c, 1, "t.point", &pl, 0, nullptr};
            blamed_call(&swofty::hooks::detail::replace_entry<Payload>, fn, &c, blame, nullptr);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            r = 1;
        }
        ok = r == 1 && blamed == 1 && blamed_what == "replace fault";
        say(ok, "sdk_blamed: a throwing SDK replace handler is named (with what()) and takes the crash path");
    } else if (!strcmp(k, "blamed")) {
        blamed = 0;
        ok = outer(av_plain) == 1 && blamed == 1;
        blamed = 0;
        ok = ok && outer(fine) == 0 && blamed == 0;
        say(ok, "blamed: blamed_call names the handler and lets the fault reach the outer frame");
    } else {
        printf("usage: guard_test.exe fine|av_plain|av_locals|throw_plain|throw_locals|msvc_code|sdk_av|sdk_throw|"
               "sdk_service_throw|sdk_blamed|blamed\n");
        return 2;
    }
    return ok ? 0 : 1;
}
