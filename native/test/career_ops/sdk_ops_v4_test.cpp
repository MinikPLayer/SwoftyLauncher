// The SDK's table version 4 operations (swofty/systems/career.hpp set_seat, set_car_number; swofty/systems/
// tracks.hpp set_track_fuel, copy_track_fuel, restore_track_fuel) against a stand-in SwoftyCareerOps table on
// the host: each resolves the driver first, sends its request, maps SWOFTY_REFUSED and SWOFTY_TOO_SMALL, and is
// unavailable on a table older than version 4 or without one.

#include <cstdio>
#include <cstring>
#include <string>

#include <swofty/systems/career.hpp>
#include <swofty/systems/teams.hpp>
#include <swofty/systems/tracks.hpp>

using namespace swofty;

static int failures = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                                    \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

static std::string g_seat, g_fuel;
static uint32_t g_number_driver = 0, g_number_flags = 99;
static int32_t g_number = 0;

static int available() { return 1; }
static int driver_stat(const char *ref, uint32_t *out)
{
    if (!std::strcmp(ref, "game/fernando_alonso"))
        return *out = 7, SWOFTY_ASSET_OK;
    return SWOFTY_ASSET_INVALID;
}
static int report(const std::string &r, char *out, size_t cap, size_t *len)
{
    *len = r.size();
    if (cap < r.size() + 1)
        return SWOFTY_TOO_SMALL;
    std::memcpy(out, r.c_str(), r.size() + 1);
    return SWOFTY_ASSET_OK;
}
static int set_seat(SwoftyText request, char *out, size_t cap, size_t *len)
{
    g_seat.assign(request.data, request.size);
    if (g_seat.find("\"seat\":5") != std::string::npos)
        return SWOFTY_ASSET_INVALID;
    return report("{\"result\":\"ok\",\"changed\":1,\"holder\":\"released\"}", out, cap, len);
}
static int set_car_number(uint32_t driver, int32_t number, uint32_t flags)
{
    g_number_driver = driver, g_number = number, g_number_flags = flags;
    return number == 44 && !(flags & SWOFTY_NUMBER_SWAP) ? int(SWOFTY_REFUSED) : int(SWOFTY_ASSET_OK);
}
static int set_track_fuel(SwoftyText request, char *out, size_t cap, size_t *len)
{
    g_fuel.assign(request.data, request.size);
    return report(std::string(3000, ' '), out, cap, len);
}

static std::string g_logo_team, g_logo;
static int set_logo(const char *team, const char *logo)
{
    g_logo_team = team;
    g_logo = logo;
    return std::string_view(logo) == "swofty.progressive/missing" ? int(SWOFTY_REFUSED) : int(SWOFTY_ASSET_OK);
}

static const SwoftyCareerOps kOps2 = {2,       available, driver_stat, nullptr, nullptr,       nullptr, nullptr, nullptr,
                                      nullptr, set_seat,  set_car_number, set_track_fuel, nullptr, nullptr};
static const SwoftyCareerOps kOps4 = {4,       available, driver_stat, nullptr, nullptr,       nullptr, nullptr, nullptr,
                                      nullptr, set_seat,  set_car_number, set_track_fuel, set_logo, nullptr};
static const SwoftyCareerOps kOps5 = {5,       available, driver_stat, nullptr, nullptr,       nullptr, nullptr, nullptr,
                                      nullptr, set_seat,  set_car_number, set_track_fuel, set_logo, nullptr};

int main()
{
    Ref<drivers::Identity> alonso("game/fernando_alonso"), nobody("game/nobody");
    CHECK(career::set_seat(1, 0, alonso) == career::Result::unavailable);
    CHECK(career::set_car_number(alonso, 14) == career::Result::unavailable);
    CHECK(tracks::restore_track_fuel() == career::Result::unavailable);

    career::detail::set_ops_table(&kOps2);
    CHECK(career::set_seat(1, 0, alonso) == career::Result::unavailable);
    CHECK(career::set_car_number(alonso, 14) == career::Result::unavailable);
    CHECK(tracks::copy_track_fuel("*", "f1_2026", "f1_2025") == career::Result::unavailable);
    CHECK(g_seat.empty() && g_fuel.empty() && g_number_flags == 99);

    career::detail::set_ops_table(&kOps4);
    CHECK(career::set_seat(3, 1, alonso) == career::Result::ok);
    CHECK(g_seat == "{\"driver\":7,\"slot\":3,\"seat\":1}");
    CHECK(career::set_seat_of_team(476, 0, alonso) == career::Result::ok);
    CHECK(g_seat == "{\"driver\":7,\"team\":476,\"seat\":0}");
    CHECK(career::set_seat(1, 5, alonso) == career::Result::invalid);
    CHECK(career::set_seat(1, 0, nobody) == career::Result::invalid);

    CHECK(career::set_car_number(alonso, 14) == career::Result::ok);
    CHECK(g_number_driver == 7 && g_number == 14 && g_number_flags == 0);
    CHECK(career::set_car_number(alonso, 44) == career::Result::refused);
    CHECK(career::set_car_number(alonso, 44, true) == career::Result::ok && g_number_flags == SWOFTY_NUMBER_SWAP);
    CHECK(career::set_car_number(nobody, 14) == career::Result::invalid);

    CHECK(tracks::set_track_fuel(Ref<tracks::Track>("game/melbourne"), {.burn_per_lap = 1.74f}) == career::Result::ok);
    CHECK(g_fuel.find("\"track\":\"melbourne\"") != std::string::npos && g_fuel.find("\"set\":\"f1_2026\"") != std::string::npos);
    CHECK(g_fuel.find("\"burn_per_lap\":1.74") != std::string::npos);
    CHECK(tracks::copy_track_fuel("*", "sprint_f1_2026", "sprint_f1_2025", "season 2017") == career::Result::ok);
    CHECK(g_fuel == "{\"track\":\"*\",\"set\":\"sprint_f1_2026\",\"from\":\"sprint_f1_2025\",\"why\":\"season 2017\"}");
    CHECK(tracks::restore_track_fuel("2026") == career::Result::ok);
    CHECK(g_fuel == "{\"restore\":true,\"why\":\"2026\"}");

    Ref<teams::Identity> williams("game/williams");
    Ref<teams::Logo> logo("swofty.progressive/williams_2017"), missing("swofty.progressive/missing");
    CHECK(teams::set_logo(williams, logo) == teams::Result::unavailable && g_logo.empty());
    career::detail::set_ops_table(&kOps5);
    CHECK(teams::set_logo(williams, logo) == teams::Result::ok);
    CHECK(g_logo_team == "game/williams" && g_logo == "swofty.progressive/williams_2017");
    CHECK(teams::set_logo(williams, missing) == teams::Result::invalid);
    CHECK(teams::set_logo(Ref<teams::Identity>(), logo) == teams::Result::invalid);

    career::detail::set_ops_table(nullptr);
    if (failures) {
        std::printf("sdk_ops_v4_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("sdk_ops_v4_test: set_seat, set_car_number and the track fuel requests on table version 4, older tables and "
                "no table: ok\n");
    return 0;
}
