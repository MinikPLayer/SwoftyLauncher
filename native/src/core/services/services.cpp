#include "../runtime.hpp"

#include <swofty/mixin.hpp>

#include "../hooks/guard.hpp"
#include "services.hpp"

std::string settings_addon(const void *from, std::string &dll);

namespace swofty_core::hooks {
std::string describe_fault(unsigned long code, uintptr_t at, const char *what);
}

namespace swofty_core::services {
namespace {

std::string caller(const void *from)
{
    std::string dll;
    std::string id = settings_addon(from, dll);
    HMODULE m = nullptr, self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(from), &m);
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&caller), &self);
    if (m == self)
        return {};
    return id.empty() ? (dll.empty() ? std::string("an unlisted dll") : dll) : id;
}

Registry &registry()
{
    static Registry r([](const std::string &s) { swofty::log("%s", s.c_str()); },
                      [](SwoftyServiceFn fn, void *user, const void *table, size_t size, uint32_t version, std::string *what) {
                          hooks::guard::Seen seen{0, 0, {}};
                          if (hooks::guard::isolated_service(fn, user, table, size, version, &seen))
                              return true;
                          *what = hooks::describe_fault(seen.code, seen.at, seen.what);
                          return false;
                      });
    return r;
}

int s_available()
{
    return 1;
}

int s_publish(const char *name, uint32_t version, const void *table, size_t size)
{
    return registry().publish(caller(__builtin_return_address(0)), name ? name : "", version, table, size);
}

int s_when(const char *name, uint32_t min_version, size_t min_size, SwoftyServiceFn fn, void *user)
{
    return registry().when(caller(__builtin_return_address(0)), name ? name : "", min_version, min_size, fn, user);
}

const SwoftyServices services_table = {SWOFTY_SERVICES_VERSION, s_available, s_publish, s_when};

}

const SwoftyServices *table()
{
    return &services_table;
}

void report_missing()
{
    for (auto &line : registry().missing())
        swofty::log("%s", line.c_str());
}

}

extern "C" __declspec(dllexport) const SwoftyServices *SwoftyCoreServices(void)
{
    return swofty_core::services::table();
}
