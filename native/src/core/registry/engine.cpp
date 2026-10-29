#include <swofty/fake_core.hpp>

#include "table.hpp"

namespace {

std::string g_addon;

void engine_begin_addon(const char *addon) { g_addon = addon ? addon : ""; }

void engine_end_register() { swofty_core::registry::store().end_register(); }

void engine_reset()
{
    swofty_core::registry::reset_store();
    g_addon.clear();
}

}

extern "C" const SwoftyFakeEngine *swofty_registry_engine(void)
{
    static const SwoftyFakeEngine engine = [] {
        swofty_core::registry::set_caller_resolver([](const void *) { return g_addon; });
        return SwoftyFakeEngine{sizeof(SwoftyFakeEngine), swofty_core::registry::table(), engine_begin_addon,
                                engine_end_register, engine_reset};
    }();
    return &engine;
}
