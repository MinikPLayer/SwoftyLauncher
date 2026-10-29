#include <swofty/mixin.hpp>

#include "../runtime.hpp"
#include "declare_all.hpp"
#include "older.hpp"
#include "table.hpp"

#include <windows.h>

#include <mutex>

std::string settings_addon(const void *from, std::string &dll);
bool settings_state(const std::string &addon, const std::string &option, size_t &current, std::vector<std::string> *texts,
                    std::vector<std::string> *commands, std::vector<std::string> *values);

namespace {

namespace reg = swofty_core::registry;

std::size_t declared_count = 0;

std::string swofty_dir()
{
    wchar_t path[MAX_PATH] = {};
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&swofty_dir), &self);
    GetModuleFileNameW(self, path, MAX_PATH);
    std::wstring w(path);
    for (int up = 0; up < 3 && w.find_last_of(L'\\') != std::wstring::npos; up++)
        w.resize(w.find_last_of(L'\\'));
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(n > 0 ? n - 1 : 0, '\0');
    if (n > 1)
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), n, nullptr, nullptr);
    return out;
}

reg::Value option_value(const std::string &text)
{
    if (text == "true")
        return reg::Value(true);
    if (text == "false")
        return reg::Value(false);
    char *end = nullptr;
    double d = std::strtod(text.c_str(), &end);
    if (!text.empty() && end && !*end)
        return reg::Value(d);
    return reg::Value(text);
}

void setup()
{
    reg::Store &st = reg::store();
    st.set_log([](const std::string &line) { swofty::log("%s", line.c_str()); });
    st.set_validator(&reg::schema_validator());
    reg::set_caller_resolver([](const void *from) {
        std::string dll;
        return settings_addon(from, dll);
    });
    st.set_option_source([](const std::string &addon, const std::string &id, reg::Value &out) {
        size_t current = 0;
        std::vector<std::string> values;
        if (!settings_state(addon, id, current, nullptr, nullptr, &values) || current >= values.size())
            return false;
        out = option_value(values[current]);
        return true;
    });
    st.set_override_source([](const std::string &addon) {
        size_t current = 0;
        std::vector<std::string> values;
        return settings_state(addon, "also_change_year_packs", current, nullptr, nullptr, &values) &&
               current < values.size() && values[current] == "true";
    });
    std::string dir = swofty_dir();
    reg::IndexResult ix = reg::load_index(dir);
    if (ix.found)
        swofty::log("[registry] index.json: profile '%s', %zu addon(s) in load order, %zu data file(s)%s",
                    ix.profile.empty() ? "default" : ix.profile.c_str(), ix.order.size(), ix.files,
                    ix.failed ? ", some refused (see the lines above)" : "");
    else
        swofty::log("[registry] no swofty/registry/index.json; load order follows the order addons register in");
    const SwoftyRegistry *t = reg::table();
    size_t count = 0;
    if (t && t->declare)
        count = swofty_core::registry::declare_system_registries([&](const std::string &name, const std::string &schema) {
            if (reg::store().declared(name))
                return false;
            return t->declare(name.c_str(), swofty::detail::text(schema)) == SWOFTY_ASSET_OK;
        });
    declared_count = count;
}

}

namespace runtime {

const SwoftyRegistry *registry_table()
{
    static bool once = [] {
        setup();
        return true;
    }();
    (void)once;
    return reg::table();
}

swofty_core::registry::Store &older_store()
{
    registry_table();
    static std::once_flag once;
    std::call_once(once, [] { swofty_core::registry::older::declare(reg::store()); });
    return reg::store();
}

std::string older_addon(const void *from)
{
    std::string dll, id = settings_addon(from, dll);
    if (!id.empty())
        return id;
    if (dll.empty() || dll == "swofty_core.dll")
        return "swofty.core";
    return dll;
}

int registry_write_reports()
{
    return reg::write_reports(swofty_dir() + "\\registry");
}

}

extern "C" __declspec(dllexport) const SwoftyRegistry *SwoftyCoreRegistry(void) { return runtime::registry_table(); }

extern "C" __declspec(dllexport) int SwoftyCoreRegisterDone(void)
{
    reg::Store &st = reg::store();
    st.end_register();
    swofty::log("[registry] core declared %zu registr%s from the SDK headers; an addon that declares one again keeps its own newer minor",
                declared_count, declared_count == 1 ? "y" : "ies");
    for (auto &addon : st.order()) {
        std::size_t layers = st.layer_count(addon, "");
        if (layers)
            swofty::log("[registry] register: %s %zu layer(s)", addon.c_str(), layers);
    }
    swofty::log("[registry] register stage closed: %zu registr%s declared; boot registries refuse later writes as late",
                st.registries().size(), st.registries().size() == 1 ? "y" : "ies");
    if (std::size_t waiting = st.pending())
        swofty::log("[registry] %zu call(s) wait for registries nothing has declared yet; they apply when one is declared",
                    waiting);
    return 1;
}
