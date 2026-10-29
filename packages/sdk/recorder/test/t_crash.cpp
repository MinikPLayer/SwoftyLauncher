// Adds one record, then crashes in SWOFTY_REGISTER.
#include <swofty/registry.hpp>
#include <swofty/systems/sounds.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
    sounds::engine_sounds.add("before_crash", {.bundle = file("a.bdl")});
    volatile int *p = nullptr;
    *p = 1;
}

SWOFTY_ADDON()
