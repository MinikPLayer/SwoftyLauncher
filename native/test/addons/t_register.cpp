#include <swofty/registry.hpp>

struct Thing {
    static constexpr swofty::Version version{1, 0};
    std::string name;
    int n = 0;
};
SWOFTY_RECORD(Thing, name, n)

inline swofty::Registry<Thing> things{"wine.thing", {.applies = swofty::Applies::boot}};

SWOFTY_REGISTER
{
    swofty::log("[t_register] register: SWOFTY_REGISTER runs");
    swofty::Ref<Thing> one = things.add("one", {.name = "one", .n = 1});
    swofty::log("[t_register] register: added %s", std::string(one.id()).c_str());
}

SWOFTY_ON_LOAD
{
    auto one = things.get(things["t.register/one"]);
    auto extra = things.get(things["wine.data/extra"]);
    swofty::log("[t_register] on_load: t.register/one n=%d, wine.data/extra n=%d", one ? one->n : -1,
                extra ? extra->n : -1);
}

SWOFTY_ADDON()
