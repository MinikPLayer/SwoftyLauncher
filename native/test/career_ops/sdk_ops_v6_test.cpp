// The SDK's table version 6 operation (swofty/systems/drivers.hpp set_look) against a stand-in SwoftyCareerOps
// table on the host: it sends the driver's and the look's ids as they are, refuses an empty reference, maps a
// refusal of the core to invalid, and is unavailable on a table older than version 6 or without one.

#include <cstdio>
#include <cstring>
#include <string>

#include <swofty/systems/drivers.hpp>

using namespace swofty;

static int failures = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                                    \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

static std::string g_driver, g_look;

static int available() { return 1; }
static int set_look(const char *driver, const char *look)
{
    g_driver = driver, g_look = look;
    if (g_look == "mod.a/missing")
        return SWOFTY_ASSET_INVALID;
    if (g_look == "mod.a/late")
        return SWOFTY_ASSET_LATE;
    return SWOFTY_ASSET_OK;
}

static const SwoftyCareerOps kOps5 = {5,       available, nullptr, nullptr, nullptr, nullptr, nullptr,
                                      nullptr, nullptr,   nullptr, nullptr, nullptr, nullptr, set_look};
static const SwoftyCareerOps kOps6 = {6,       available, nullptr, nullptr, nullptr, nullptr, nullptr,
                                      nullptr, nullptr,   nullptr, nullptr, nullptr, nullptr, set_look};

int main()
{
    Ref<drivers::Identity> hamilton("game/lewis_hamilton"), salo("person/mika_salo"), none("");
    Ref<drivers::Look> look("mod.a/hamilton_2017");
    CHECK(drivers::set_look(hamilton, look) == Result::unavailable);

    career::detail::set_ops_table(&kOps5);
    CHECK(drivers::set_look(hamilton, look) == Result::unavailable);
    CHECK(g_driver.empty());

    career::detail::set_ops_table(&kOps6);
    CHECK(drivers::set_look(hamilton, look) == Result::ok);
    CHECK(g_driver == "game/lewis_hamilton" && g_look == "mod.a/hamilton_2017");
    CHECK(drivers::set_look(salo, look) == Result::ok && g_driver == "person/mika_salo");
    CHECK(drivers::set_look(none, look) == Result::invalid);
    CHECK(drivers::set_look(hamilton, Ref<drivers::Look>("")) == Result::invalid);
    CHECK(drivers::set_look(hamilton, Ref<drivers::Look>("mod.a/missing")) == Result::invalid);
    CHECK(drivers::set_look(hamilton, Ref<drivers::Look>("mod.a/late")) == Result::late);

    career::detail::set_ops_table(nullptr);
    if (failures) {
        std::printf("sdk_ops_v6_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("sdk_ops_v6_test: set_look on table version 6, older tables and no table: ok\n");
    return 0;
}
