// SPDX-License-Identifier: MIT
#pragma once
#if defined(_WIN32)
#include <windows.h>
#endif
#include <atomic>
#include <functional>
#include <swofty/mixin.hpp>

#ifndef SWOFTY_GUARDED_ENTRY
#define SWOFTY_GUARDED_ENTRY 1
#include <cstring>
#include <exception>
#if defined(_WIN32)
extern "C" __declspec(dllimport) void __stdcall RaiseException(unsigned long code, unsigned long flags, unsigned long count,
                                                               const unsigned long long *info);
#endif

// A C++ exception never unwinds out of an addon callback into the core: the core's handler guard
// (SEH) can't unwind another runtime's C++ frames. Every SDK entry the core calls runs its body here;
// a throw is caught in the addon's own runtime and, once the catch block has ended, raised again as
// the SEH exception handler_threw_code, carrying what(), which the core's guard treats like any other
// fault of that handler.
namespace swofty::detail {

inline constexpr unsigned long handler_threw_code = 0xE0535746ul;

template <class F> int guarded_entry(F &&body)
{
#if defined(_WIN32)
    static thread_local char what[256];
    bool threw = false;
    int r = 0;
    try {
        r = body();
    } catch (const std::exception &e) {
        threw = true;
        strncpy(what, e.what() ? e.what() : "", sizeof what - 1);
        what[sizeof what - 1] = 0;
    } catch (...) {
        threw = true;
        strcpy(what, "an exception that is not a std::exception");
    }
    if (threw) {
        unsigned long long info = reinterpret_cast<unsigned long long>(what);
        RaiseException(handler_threw_code, 1, 1, &info);
    }
    return r;
#else
    return body();
#endif
}

}
#endif

// Services (standard library 1.6): versioned function tables one addon publishes for others to look up
// by name. T is the table: static constexpr const char *name and uint32_t version, then uint32_t size and
// function pointers, appended per minor. when<T>() passes only a table of T::version or newer and of at
// least sizeof(T); it runs at once when the table is already published, else once it is. On a core
// without the services table both return unavailable.

namespace swofty::services {

enum class Result { ok, late, unavailable, invalid };

namespace detail {

inline const SwoftyServices *table()
{
    if (!api || api->version < 9 || !api->services)
        return nullptr;
#if defined(_WIN32)
    static std::atomic<int> exported{-1};
    if (exported.load() < 0) {
        HMODULE core = GetModuleHandleW(L"swofty_core.dll");
        exported = core && GetProcAddress(core, "SwoftyCoreServices") ? 1 : 0;
    }
    if (!exported.load())
        return nullptr;
#endif
    return api->services->available && api->services->available() ? api->services : nullptr;
}

inline Result result(int r)
{
    return r == SWOFTY_ASSET_OK            ? Result::ok
           : r == SWOFTY_ASSET_LATE        ? Result::late
           : r == SWOFTY_ASSET_UNAVAILABLE ? Result::unavailable
                                           : Result::invalid;
}

template <class T> void when_entry(void *user, const void *table, size_t size, uint32_t version)
{
    if (!table || size < sizeof(T) || version < T::version)
        return;
    swofty::detail::guarded_entry([&] {
        (*static_cast<std::function<void(const T &)> *>(user))(*static_cast<const T *>(table));
        return 0;
    });
}

}

inline bool available() { return detail::table() != nullptr; }

template <class T> Result publish(const T &table)
{
    auto t = detail::table();
    if (!t || !t->publish)
        return Result::unavailable;
    return detail::result(t->publish(T::name, T::version, &table, sizeof(T)));
}

template <class T> Result when(std::function<void(const T &)> fn)
{
    auto t = detail::table();
    if (!t || !t->when)
        return Result::unavailable;
    return detail::result(t->when(T::name, T::version, sizeof(T), &detail::when_entry<T>,
                                  new std::function<void(const T &)>(std::move(fn))));
}

inline const char *name(Result r)
{
    return r == Result::ok ? "ok" : r == Result::late ? "late" : r == Result::unavailable ? "unavailable" : "invalid";
}

}
