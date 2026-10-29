// Calls a memory scan from SWOFTY_REGISTER, which the recorder doesn't serve.
#include <swofty/registry.hpp>
#include <swofty/systems/sounds.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    sounds::engine_sounds.add("one", {.bundle = file("a.bdl")});
    uintptr_t at = 0;
    swofty::api->scan(".text", "48 8b ??", &at);
}

SWOFTY_ADDON()
