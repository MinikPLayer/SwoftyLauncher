// The SDK's career operations (swofty/systems/career.hpp) against a stand-in SwoftyCareerOps table on the host:
// apply_lineup sends {"season": record} and reads the report, set_driver_ratings and swap_drivers resolve the
// driver references first, report_season needs table version 2 and maps SWOFTY_REFUSED to refused, apply_teams
// needs table version 3 and sends {"season": record} like apply_lineup, and every call is unavailable without a
// table.

#include <cstdio>
#include <cstring>
#include <string>

#include <swofty/systems/career.hpp>

using namespace swofty;

static int failures = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                                    \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

static std::string g_request;
static uint32_t g_rated = 0, g_swap_a = 0, g_swap_b = 0;
static float g_values[8];
static uint16_t g_ids[8];
static size_t g_count = 0;

static int available() { return 1; }
static int driver_stat(const char *ref, uint32_t *out)
{
    if (!std::strcmp(ref, "game/fernando_alonso"))
        return *out = 7, SWOFTY_ASSET_OK;
    if (!std::strcmp(ref, "game/valtteri_bottas"))
        return *out = 9, SWOFTY_ASSET_OK;
    return SWOFTY_ASSET_INVALID;
}
static int set_slots(const char *, const uint32_t *, size_t) { return SWOFTY_ASSET_OK; }
static int apply_lineup(SwoftyText request, char *out, size_t cap, size_t *len)
{
    g_request.assign(request.data, request.size);
    std::string r = "{\"result\":\"ok\",\"changed\":2,\"unchanged\":19,\"refused\":1,\"refusals\":[\"slot 9 seat 2: person/x "
                    "has no career row\"],\"written\":3}";
    *len = r.size();
    if (cap < r.size() + 1)
        return SWOFTY_TOO_SMALL;
    std::memcpy(out, r.c_str(), r.size() + 1);
    return SWOFTY_ASSET_OK;
}
static int set_driver_ratings(uint32_t d, const uint16_t *ids, const float *values, size_t n)
{
    g_rated = d;
    g_count = n;
    std::memcpy(g_ids, ids, n * sizeof *ids);
    std::memcpy(g_values, values, n * sizeof *values);
    return SWOFTY_ASSET_OK;
}
static int swap(uint32_t a, uint32_t b)
{
    g_swap_a = a, g_swap_b = b;
    return SWOFTY_ASSET_LATE;
}

static int g_report_year = 0, g_report_index = -1;
static int report_season(int32_t year, int32_t index)
{
    g_report_year = year, g_report_index = index;
    return year == 2099 ? int(SWOFTY_REFUSED) : int(SWOFTY_ASSET_OK);
}

static std::string g_teams_request;
static int apply_teams(SwoftyText request, char *out, size_t cap, size_t *len)
{
    g_teams_request.assign(request.data, request.size);
    std::string r = "{\"result\":\"ok\",\"changed\":11,\"unchanged\":0,\"refused\":1,\"refusals\":[\"slot 12: constructor/hrt "
                    "resolves to no team\"]}";
    *len = r.size();
    if (cap < r.size() + 1)
        return SWOFTY_TOO_SMALL;
    std::memcpy(out, r.c_str(), r.size() + 1);
    return SWOFTY_ASSET_OK;
}

static const SwoftyCareerOps kOps = {1,       available, driver_stat, set_slots, apply_lineup, set_driver_ratings, swap,
                                     nullptr, nullptr,   nullptr,     nullptr,   nullptr, nullptr, nullptr};
static const SwoftyCareerOps kOps2 = {2,    available,     driver_stat, set_slots, apply_lineup, set_driver_ratings,
                                      swap, report_season, nullptr,     nullptr,   nullptr,      nullptr, nullptr, nullptr};
static const SwoftyCareerOps kOps3 = {3,    available,     driver_stat, set_slots, apply_lineup, set_driver_ratings,
                                      swap, report_season, apply_teams, nullptr,   nullptr,      nullptr, nullptr, nullptr};

int main()
{
    career::Season s;
    s.data_group = "f1_career_progressive";
    s.year = 2017;
    s.teams.push_back({.slot = 1, .team = {}, .seats = {Ref<drivers::Identity>("game/fernando_alonso"),
                                                          Ref<drivers::Identity>("game/valtteri_bottas")}});
    career::ApplyReport report;
    report.changed = 99;
    CHECK(career::apply_lineup(s, &report) == career::Result::unavailable);
    CHECK(report.changed == 0 && report.refusals.empty());
    CHECK(career::swap_drivers(Ref<drivers::Identity>("game/a"), Ref<drivers::Identity>("game/b")) == career::Result::unavailable);
    CHECK(!career::ops());

    career::detail::set_ops_table(&kOps);
    CHECK(career::ops() == &kOps);
    CHECK(career::apply_lineup(s, &report) == career::Result::ok);
    CHECK(g_request.rfind("{\"season\":{", 0) == 0);
    CHECK(g_request.find("\"game/fernando_alonso\"") != std::string::npos);
    CHECK(g_request.find("\"f1_career_progressive\"") != std::string::npos);
    CHECK(report.changed == 2 && report.unchanged == 19 && report.refused == 1);
    CHECK(report.refusals.size() == 1 && report.refusals[0].find("person/x") != std::string::npos);

    career::DriverRatings r{.experience = 70, .racecraft = 88, .awareness = 85, .pace = 96};
    CHECK(career::set_driver_ratings(Ref<drivers::Identity>("game/fernando_alonso"), r) == career::Result::ok);
    CHECK(g_rated == 7 && g_count == 4 && g_ids[0] == 0 && g_ids[1] == 1 && g_ids[2] == 3 && g_ids[3] == 4);
    CHECK(g_values[0] == 70 && g_values[3] == 96);
    CHECK(career::set_driver_ratings(Ref<drivers::Identity>("game/nobody"), r) == career::Result::invalid);

    CHECK(career::swap_drivers(Ref<drivers::Identity>("game/fernando_alonso"), Ref<drivers::Identity>("game/valtteri_bottas")) ==
          career::Result::late);
    CHECK(g_swap_a == 7 && g_swap_b == 9);
    CHECK(career::set_seat(1, 0, {}) == career::Result::unavailable);
    CHECK(career::report_season(2017, 1) == career::Result::unavailable);
    CHECK(g_report_index == -1);

    career::detail::set_ops_table(&kOps2);
    CHECK(career::report_season(2017, 1) == career::Result::ok);
    CHECK(g_report_year == 2017 && g_report_index == 1);
    CHECK(career::report_season(2099, 3) == career::Result::refused);
    CHECK(std::strcmp(career::name(career::Result::refused), "refused") == 0);
    CHECK(career::apply_teams(s, &report) == career::Result::unavailable);
    CHECK(report.changed == 0 && g_teams_request.empty());

    career::detail::set_ops_table(&kOps3);
    s.teams.push_back({.slot = 12, .team = Ref<teams::Identity>("constructor/hrt"), .seats = {}});
    CHECK(career::apply_teams(s, &report) == career::Result::ok);
    CHECK(g_teams_request.rfind("{\"season\":{", 0) == 0);
    CHECK(g_teams_request.find("\"slot\":12") != std::string::npos);
    CHECK(g_teams_request.find("\"constructor/hrt\"") != std::string::npos);
    CHECK(report.changed == 11 && report.unchanged == 0 && report.refused == 1);
    CHECK(report.refusals.size() == 1 && report.refusals[0].find("slot 12") != std::string::npos);
    CHECK(career::apply_lineup(s, &report) == career::Result::ok && report.changed == 2);

    career::detail::set_ops_table(nullptr);
    CHECK(career::report_season(2017, 1) == career::Result::unavailable);
    CHECK(career::apply_teams(s, &report) == career::Result::unavailable);

    if (failures) {
        std::printf("sdk_ops_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("sdk_ops_test: apply_lineup request and report, set_driver_ratings ids, swap_drivers, report_season on table versions 1 and 2, apply_teams on table versions 2 and 3, no table: ok\n");
    return 0;
}
