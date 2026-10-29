// Compiles every standard library 1.6 header and instantiates its templates. Built for x86_64-windows-gnu
// (compile and link) and for the host (run): with no core, every call reports unavailable and nothing
// crashes; records round-trip through their JSON form; schemas carry the registry keywords.
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>

#include <swofty/events.hpp>
#include <swofty/hooks.hpp>
#include <swofty/registry.hpp>
#include <swofty/services.hpp>
#include <swofty/systems/ai.hpp>
#include <swofty/systems/career.hpp>
#include <swofty/systems/cars.hpp>
#include <swofty/systems/content.hpp>
#include <swofty/systems/drivers.hpp>
#include <swofty/systems/liveries.hpp>
#include <swofty/systems/sessions.hpp>
#include <swofty/systems/sounds.hpp>
#include <swofty/systems/spectator.hpp>
#include <swofty/systems/teams.hpp>
#include <swofty/systems/wearables.hpp>
#include <swofty/systems/tracks.hpp>

#include "sdk_payloads.h"

#if defined(_WIN32)
#include <swofty/assets.hpp>
#include <swofty/career_content.hpp>
#include <swofty/career_wizard.hpp>
#include <swofty/characters.hpp>
#include <swofty/db.hpp>
#include <swofty/hub.hpp>
#include <swofty/hud.hpp>
#include <swofty/lang.hpp>
#include <swofty/popup.hpp>
#include <swofty/storage.hpp>
#include <swofty/telemetry.hpp>
#endif

#define SWOFTY_CPP_SIZE(C, ns, T, n)                                                                                   \
    static_assert(std::is_standard_layout_v<swofty::ns::events::T> && std::is_trivially_copyable_v<swofty::ns::events::T>, \
                  #T " is standard layout and trivially copyable");                                                    \
    static_assert(sizeof(swofty::ns::events::T) == n && sizeof(C) == n, #T " size matches " #C);                       \
    static_assert(alignof(swofty::ns::events::T) == alignof(C), #T " alignment matches " #C);
SWOFTY_EV_PAYLOADS(SWOFTY_CPP_SIZE)
#define SWOFTY_CPP_FIELD(C, ns, T, f, off)                                                                             \
    static_assert(offsetof(swofty::ns::events::T, f) == off && offsetof(C, f) == off, #T "." #f " offset matches " #C);
SWOFTY_EV_FIELDS(SWOFTY_CPP_FIELD)
static_assert(std::is_standard_layout_v<swofty::cars::live::Car> && std::is_trivially_copyable_v<swofty::cars::live::Car> &&
              sizeof(swofty::cars::live::Car) == sizeof(SwoftyEvCar) && alignof(swofty::cars::live::Car) == alignof(SwoftyEvCar));
static_assert(std::is_same_v<std::underlying_type_t<swofty::cars::Department>, int32_t>);
static_assert(std::is_same_v<std::underlying_type_t<swofty::teams::LogoContext>, int32_t>);
static_assert(std::is_same_v<std::underlying_type_t<swofty::drivers::Stat>, int32_t>);

using namespace swofty;

static int failures = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                                   \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

struct ShopApi {
    static constexpr const char *name = "test.shop";
    static constexpr uint32_t version = 1;
    uint32_t size;
    int (*count)(void);
};

struct ShopPoint {
    static constexpr const char *point = "test.shop.open";
    static constexpr uint32_t version = 1;
    uint32_t size, payload_version;
    int day;
};

namespace test {
struct Thing {
    static constexpr Version version{1, 2};
    std::string name;
    int n = 0;
    json::Value extra;
};
SWOFTY_RECORD(Thing, name, n)
inline Registry<Thing> things{"test.thing"};
}

SWOFTY_REGISTER
{
    auto v10 = sounds::engine_sounds.add("v10_screamer", {
                                                             .bundle = file("sounds/v10_screamer.bdl"),
                                                             .settings_from = sounds::engine_sounds["game/ferrari_2025"],
                                                         });
    cars::vehicles.layer(cars::vehicles["swofty.classic-seasons-2017/ferrari"], [=](cars::Vehicle &v) { v.engine_sound = v10; });
}

static void unavailable_without_core()
{
    CHECK(!detail::registry_table());
    auto v10 = sounds::engine_sounds.add("v10", {.bundle = file("a.bdl")});
    CHECK(v10.empty());
    auto ferrari = cars::vehicles.variant("ferrari", cars::vehicles["game/ferrari_2025"], [](cars::Vehicle &v) { v.label = "x"; });
    CHECK(ferrari.empty());
    auto h = content::packages.layer(content::packages["swofty.classic-seasons-2017/f1_2017"],
                                     [](content::Package &p) { p.teams.at("ferrari").engine_sound = {}; });
    h.in(events::mode(events::Mode::time_trial)).in(events::content("swofty.classic-seasons-2017/f1_2017"));
    CHECK(h.result() == Result::unavailable);
    auto sel = cars::handling.layer(tagged<cars::Handling>(cars::handling.tag("c/f1_cars")), [](cars::Handling &x) {
        if (option<std::string>("wing_damage") == "heavy")
            x.wing_damage.damage_scale *= 2;
        x.upgrade("radUpgradeEngine").set("FuelTank", "FuelBsfcMin", '*', 0.85f);
    });
    CHECK(sel.result() == Result::unavailable);
    cars::vehicles.layer(under<cars::Vehicle>("game/").except({cars::vehicles["game/ferrari_2025"]}), [](cars::Vehicle &) {},
                         Scope::profile);
    cars::vehicles.layer(where<cars::Vehicle>([](const cars::Vehicle &v) { return v.performance_class == cars::Class::f1_26; }),
                         [](cars::Vehicle &) {});
    content::packages.layer(tagged(&content::Package::calendar, Tag<career::Calendar>("career.calendar", "c/x")),
                            [](content::Package &) {});
    CHECK(cars::handling.protect(cars::handling["mod.a/ferrari"], {"chassis", "f1_aero"}) == Result::unavailable);
    CHECK(!career::seasons.get(career::seasons["swofty.progressive/2017"]));
    CHECK(career::seasons.status(career::seasons["swofty.progressive/2017"]) == Status::no_registry);
    CHECK(career::seasons.all("swofty.progressive/").empty());
    CHECK(career::seasons.origin(career::seasons["swofty.progressive/2017"], "teams").empty());
    CHECK(!career::seasons.watch([](Ref<career::Season>) {}));
    CHECK(career::ratings.check([](const career::DriverRatings &r, Errors &e) {
              if (r.pace > 100)
                  e.add("pace", "0-100");
          }) == Result::unavailable);
    CHECK(career::seasons.provide([](std::string_view) -> std::optional<career::Season> { return std::nullopt; }) ==
          Result::unavailable);
    CHECK(career::seasons.invalidate() == Result::unavailable);
    CHECK(cars::vehicles.tag_add(cars::vehicles.tag("c/f1_cars"), cars::vehicles["mod.a/rb"]) == Result::unavailable);
    CHECK(!cars::vehicles.tag("c/f1_cars").contains(cars::vehicles["game/ferrari_2025"]));
    CHECK(cars::vehicles.tag("c/f1_cars").members().empty());
    CHECK(!cars::vehicles["game/ferrari_2025"].resolved());
    CHECK(option<int>("laps") == 0 && option<std::string>("x").empty() && !option<bool>("y"));
    CHECK(!cars::vehicles.stat_id(cars::vehicles["game/ferrari_2025"]));
    CHECK(last_error().empty());
    Registry<json::Value> untyped{"tails.whatif.presets"};
    CHECK(!untyped.get(untyped["tails.whatif-history/real_2016"]));
    CHECK((adapter<test::Thing, test::Thing>(test::things, test::things, [](test::Thing t) { return t; },
                                              [](test::Thing t) { return t; }) == Result::unavailable));
    CHECK((migrate<test::Thing, test::Thing>(test::things, test::things, [](test::Thing t) { return t; }) ==
           Result::unavailable));

    using events::any, events::content, events::Context, events::Mode, events::mode, events::not_, events::on,
        events::owner, events::track, events::year;
    CHECK(on<career::events::SeasonStart>()
              .in(content("swofty.progressive/f1_career_progressive"))
              .when(year(2016, 2019))
              .after([](const career::events::SeasonStart &, const Context &ctx) {
                  auto s = ctx.record<career::Season>();
                  (void)s;
                  (void)ctx.career();
                  (void)ctx.participants();
              }) == swofty::events::Result::unavailable);
    CHECK(on<career::events::AddActivity>().in(any({content("a/x"), not_(track("game/monaco"))})).replace(
              [](career::events::AddActivity &, const Context &) { return false; }) == swofty::events::Result::unavailable);
    CHECK(on<cars::events::Built>().before([](cars::events::Built &e, const Context &) { e.car.f1_aero().lift_scale = 1.01f; }) ==
          swofty::events::Result::unavailable);
    CHECK(on("swofty.progressive.seat_change").after([](std::span<const std::byte>, const Context &) {}) ==
          swofty::events::Result::unavailable);
    CHECK(events::define<ShopPoint>(owner("test")) == swofty::events::Result::unavailable);
    ShopPoint p{sizeof p, 1, 3};
    CHECK(events::fire(p));
    CHECK(!Context().career() && Context().participants().empty() && !Context().in(mode(Mode::grand_prix)));

    CHECK(hooks::before<career::events::AddActivity>([](career::events::AddActivity &) {}) == hooks::Result::unavailable);
    CHECK(hooks::before<career::events::AddActivity>(hooks::Priority{10}, [](career::events::AddActivity &) {}) ==
          hooks::Result::unavailable);
    CHECK(hooks::replace<ShopPoint>([](ShopPoint &) { return true; }) == hooks::Result::unavailable);
    CHECK(hooks::after<ShopPoint>([](const ShopPoint &, const hooks::Outcome &) {}) == hooks::Result::unavailable);
    CHECK(hooks::define<ShopPoint>() == hooks::Result::unavailable);
    CHECK(hooks::fire(p) && !hooks::live<ShopPoint>());

    static const ShopApi shop{sizeof(ShopApi), [] { return 3; }};
    CHECK(services::publish(shop) == services::Result::unavailable);
    CHECK(services::when<ShopApi>([](const ShopApi &) {}) == services::Result::unavailable);
    CHECK(!services::available());

    career::ApplyReport report;
    CHECK(career::apply_lineup(career::Season{}, &report) == career::Result::unavailable);
    CHECK(career::apply_teams(career::Season{}, &report) == career::Result::unavailable);
    CHECK(teams::set_logo(teams::identities["game/williams"], teams::logos["swofty.progressive/williams_2017"]) ==
          Result::unavailable);
    CHECK(teams::set_name(teams::identities["game/williams"], {.title = Text::literal("Williams")}) == Result::unavailable);
    CHECK(teams::set_colours(teams::identities["game/williams"], {.primary = Colour("#00A0DE")}) == Result::unavailable);
    CHECK(drivers::set_look(drivers::identities["game/lewis_hamilton"], drivers::looks["swofty.classic-seasons-2017/hamilton"]) ==
          Result::unavailable);
    auto toro = teams::identities.add("toro_rosso", {
                                                        .donor = teams::identities["game/tororosso"],
                                                        .constructor = teams::constructors["constructor/toro_rosso"],
                                                        .provides = {teams::constructors["constructor/toro_rosso"]},
                                                        .names = {.title = Text::key("lng_cs_team_17_toro_rosso"), .code = "STR"},
                                                        .livery_name = "cs17_toro_rosso",
                                                    });
    CHECK(toro.empty());
    teams::pit_times.add("toro_rosso", {.team = toro, .from = teams::pit_times["game/tororosso"]});
    teams::logos.layer(teams::logos["game/williams"], [](teams::Logo &l) { l.images[teams::LogoContext::hud] = file("logos/w.png"); });
    auto look = drivers::looks.add("salo_look", {.helmet = wearables::helmets["game/helmet_tier01_livery03"], .pose = "arms_crossed"});
    drivers::identities.variant("salo", drivers::identities["game/jarno_opmeer"], [=](drivers::Identity &i) {
        i.person = drivers::people["person/mika_salo"];
        i.look = look;
    });
    drivers::looks.layer(drivers::looks["game/lewis_hamilton"], [](drivers::Look &l) { l.helmet = wearables::helmets["x/y"]; },
                         Scope::career);
    wearables::season_sets.add("mercedes_2016", {.team = teams::identities["swofty.classic-seasons-2016/mercedes"], .season = 2016});
    CHECK(wearables::helmets.all("giggles.helmet-exchange/").empty());
    CHECK(!drivers::heads.get(drivers::heads["mod.senna-face/senna"]));
    CHECK(career::set_seat(7, 1, Ref<drivers::Identity>("game/pierre_gasly")) == career::Result::unavailable);
    CHECK(cars::reset_upgrades(0) == Result::unavailable);
    CHECK(!cars::live::available() && cars::live::cars().empty());
    CHECK(detail::run_register(nullptr) == 1);
}

static std::string added_node;
static float added_at;

static int32_t add_part(void *user, const char *node, float detach_at, bool debris)
{
    added_node = node;
    added_at = detach_at;
    return debris && user ? SWOFTY_ASSET_OK : SWOFTY_ASSET_INVALID;
}

static std::string chosen_sound;

static int32_t set_sound(void *user, const char *id)
{
    chosen_sound = id;
    static_cast<sounds::events::BundleResolve *>(user)->engine_sound_id = chosen_sound.c_str();
    return SWOFTY_ASSET_OK;
}

static std::string logo_key;

static int32_t set_logo_key(void *user, const char *key)
{
    logo_key = key;
    static_cast<teams::events::LogoResolve *>(user)->key_str = logo_key.c_str();
    return SWOFTY_ASSET_OK;
}

static std::string added_item;

static int32_t add_item(void *, const char *registry, const char *id)
{
    added_item = std::string(registry) + " " + id;
    return SWOFTY_ASSET_OK;
}

static void payload_accessors()
{
    int wing = 0;
    cars::live::Car car(4, &wing);
    SwoftyEvCar c = std::bit_cast<SwoftyEvCar>(car);
    CHECK(c.index == 4 && c.handle == &wing && car.handle() == &wing);

    cars::events::VtfRead r{sizeof r, 1, car, "F1WingDamage", "DamageScale", nullptr, 1.5f};
    CHECK(r.section() == "F1WingDamage" && r.field() == "DamageScale" && r.upgrade().empty() && r.car.index() == 4);
    SwoftyEvVtfRead rc;
    std::memcpy(&rc, &r, sizeof rc);
    CHECK(std::strcmp(rc.section_str, "F1WingDamage") == 0 && rc.car.index == 4 && rc.value == 1.5f);

    cars::events::UpgradesFit f{};
    CHECK(f.vehicle().empty());
    f.vehicle_id = "game/haas_2026";
    f.department = cars::Department::aero;
    CHECK(f.vehicle() == cars::vehicles["game/haas_2026"] && f.vehicle().id() == "game/haas_2026");

    cars::events::PartsRegister p{};
    CHECK(p.add({.node = "mirror_l"}) == Result::unavailable);
    p.add_fn = &add_part;
    p.user = &wing;
    CHECK(p.add({.node = "mirror_l", .detach_at = 0.6f}) == Result::ok && added_node == "mirror_l" && added_at == 0.6f);
    CHECK(p.add({.node = "mirror_r", .debris = false}) == Result::invalid);

    sounds::events::BundleResolve b{sizeof b, 1, 4, "game/williams_2026", "williams", "game/williams_2026", nullptr, nullptr};
    CHECK(b.vehicle().id() == "game/williams_2026" && b.switch_name() == "williams");
    CHECK(b.set_engine_sound(sounds::engine_sounds["mod.esp/v10_screamer"]) == Result::unavailable);
    b.set_engine_sound_fn = &set_sound;
    b.user = &b;
    CHECK(b.set_engine_sound(sounds::engine_sounds["mod.esp/v10_screamer"]) == Result::ok &&
          b.engine_sound().id() == "mod.esp/v10_screamer");

    teams::events::LogoResolve lr{sizeof lr, 1, teams::LogoContext::driver_select, "game/williams", "williams", "williams_logo",
                                  nullptr, nullptr};
    CHECK(lr.team().id() == "game/williams" && lr.livery_name() == "williams" && lr.key() == "williams_logo");
    CHECK(lr.set_key("progressive_williams_2017") == Result::unavailable);
    lr.set_key_fn = &set_logo_key;
    lr.user = &lr;
    CHECK(lr.set_key("progressive_williams_2017") == Result::ok && lr.key() == "progressive_williams_2017");
    SwoftyEvLogoResolve lc;
    std::memcpy(&lc, &lr, sizeof lc);
    CHECK(lc.context == 0 && std::strcmp(lc.team_id, "game/williams") == 0);

    teams::events::NameResolve nr{sizeof nr, 1, "game/mercedes", "Mercedes", true, nullptr, nullptr};
    CHECK(nr.team().id() == "game/mercedes" && nr.name() == Text::literal("Mercedes"));
    CHECK(nr.set_name(Text::key("lng_x")) == Result::unavailable);

    drivers::events::RatingsRead rr{sizeof rr, 1, "game/lando_norris", drivers::Stat::pace, 90.0f};
    CHECK(rr.driver().id() == "game/lando_norris" && rr.stat == drivers::Stat::pace);
    SwoftyEvRatingsRead rrc = std::bit_cast<SwoftyEvRatingsRead>(rr);
    CHECK(rrc.stat == 3 && rrc.value == 90.0f);
    drivers::events::NumberAssign na{sizeof na, 1, nullptr, 27};
    CHECK(na.driver().empty() && na.number == 27);
    drivers::events::HeadResolve hr{sizeof hr, 1, "game/daniil_kvyat", "game/kvyat", nullptr, nullptr};
    CHECK(hr.driver().id() == "game/daniil_kvyat" && hr.head().id() == "game/kvyat");
    CHECK(hr.set_head(drivers::heads["mod.kvyat-face/kvyat"]) == Result::unavailable);

    wearables::events::CatalogueBuild cb{sizeof cb, 1, nullptr, nullptr};
    CHECK(cb.add(wearables::helmets["giggles.helmet-exchange/hamilton_2017"]) == Result::unavailable);
    cb.add_fn = &add_item;
    cb.user = &cb;
    CHECK(cb.add(wearables::helmets["giggles.helmet-exchange/hamilton_2017"]) == Result::ok &&
          added_item == "helmet giggles.helmet-exchange/hamilton_2017");
    CHECK(cb.add(wearables::gloves["mod.kit/g"]) == Result::ok && added_item == "gloves mod.kit/g");

    career::events::SeasonStart s{sizeof s, 1, 2017, 1};
    SwoftyEvSeasonStart sc = std::bit_cast<SwoftyEvSeasonStart>(s);
    CHECK(sc.year == 2017 && sc.season_index == 1);
}

static void round_trip()
{
    career::Season s{
        .data_group = "f1_career_progressive",
        .year = 2017,
        .box = content::packages["swofty.classic-seasons-2017/f1_2017"].or_else(content::packages["game/f1_2025"]),
        .calendar = career::calendars["swofty.progressive/2017"],
        .teams = {{.slot = 1,
                   .team = Ref<teams::Identity>("game/mercedes"),
                   .seats = {Ref<drivers::Identity>("game/lewis_hamilton"), Ref<drivers::Identity>("game/valtteri_bottas")}}},
        .changes = {{.round = 15,
                     .slot = 7,
                     .seat = 1,
                     .out = Ref<drivers::Identity>("game/daniil_kvyat"),
                     .in = Ref<drivers::Identity>("game/pierre_gasly")}},
        .extra = json::parse("{\"reserve\":\"game/x\"}"),
    };
    s.teams.at(2).team = Ref<teams::Identity>("game/ferrari");
    CHECK(s.teams.size() == 2 && s.teams.find(2) && !s.teams.find(3));
    CHECK(s.changes.find(15, 7, 1) && s.changes.erase(15, 7, 1) == true && s.changes.empty());
    std::string text = to_json(s);
    CHECK(text.find("\"box\":\"swofty.classic-seasons-2017/f1_2017|game/f1_2025\"") != std::string::npos);
    CHECK(text.find("\"reserve\":\"game/x\"") != std::string::npos);
    career::Season back;
    CHECK(from_json(text, back));
    CHECK(back.year == 2017 && back.teams.size() == 2 && back.teams.find(1)->seats[1].id() == "game/valtteri_bottas");
    CHECK(to_json(back) == text);

    cars::Handling h{.cls = cars::Class::f1_26};
    h.upgrade("radUpgradeEngine").set("Engine", "WearICERate", '*', 0.8f);
    h.engine.revs_power_spline = Curve::flat(0, 1200, 550000).scale_y(1.05f);
    std::string ht = to_json(h);
    CHECK(ht.find("\"class\":\"f1_26\"") != std::string::npos && ht.find("\"op\":\"*\"") != std::string::npos);
    cars::Handling hb;
    CHECK(from_json(ht, hb) && hb.cls == cars::Class::f1_26 && hb.rad_upgrades.size() == 1);

    content::Package pkg{.title = Text::literal("F1 2017 (V10)"), .entrant_list = "esp_f1_2017_v10", .season = 2017};
    pkg.teams.at("ferrari").engine_sound = Ref<sounds::EngineSound>("mod.esp/v10_screamer");
    std::string pt = to_json(pkg);
    CHECK(pt.find("{\"literal\":\"F1 2017 (V10)\"}") != std::string::npos && pt.find("\"stat_id\":\"auto\"") != std::string::npos);
    content::Package pb;
    CHECK(from_json(pt, pb) && pb.title.is_literal() && pb.teams.find(std::string("ferrari")));

    sounds::EngineSound es{.bundle = stock("audio/2025/bundles/redbull_2025.bdl")};
    CHECK(to_json(es).find("{\"stock\":\"audio/2025/bundles/redbull_2025.bdl\"}") != std::string::npos);

    career::Calendar cal{.rounds = {{.round = 1,
                                     .race = "lng_gp_usa",
                                     .venue = Ref<tracks::Venue>("venue/phoenix"),
                                     .date = std::chrono::year{1990} / 3 / 11}},
                         .seasons = 1};
    CHECK(to_json(cal).find("\"date\":\"1990-03-11\"") != std::string::npos);
    career::Calendar cb;
    CHECK(from_json(to_json(cal), cb) && cb.rounds.find(1)->date == cal.rounds.find(1)->date);

    teams::Identity t{.donor = teams::identities["game/tororosso"],
                      .constructor = teams::constructors["constructor/toro_rosso"],
                      .names = {.title = Text::literal("Toro Rosso"), .code = "STR"},
                      .colours = {.menu = Colour(0xFF0A1E3Cu), .primary = Colour("#0A1E3C")},
                      .livery_name = "cs17_toro_rosso",
                      .stat_id = 300};
    std::string tt = to_json(t);
    CHECK(tt.find("\"constructor\":\"constructor/toro_rosso\"") != std::string::npos &&
          tt.find("\"wear\":null") != std::string::npos && tt.find("\"stat_id\":300") != std::string::npos &&
          tt.find("\"menu\":\"#0A1E3C\"") != std::string::npos);
    teams::Identity tb;
    CHECK(from_json(tt, tb) && tb.names.title.is_literal() && tb.colours.primary == Colour("#0A1E3C") && !tb.wear);
    CHECK(to_json(tb) == tt);

    teams::Logo logo;
    logo.images[teams::LogoContext::hud] = file("logos/williams_2017_hud.png");
    logo.images[teams::LogoContext::driver_select] = stock("ui/williams_driver_select.dds");
    std::string lt = to_json(logo);
    CHECK(lt.find("\"driver_select\":{\"stock\":\"ui/williams_driver_select.dds\"}") != std::string::npos &&
          lt.find("\"hud\":\"logos/williams_2017_hud.png\"") != std::string::npos);
    teams::Logo lb;
    CHECK(from_json(lt, lb) && lb.images.size() == 2 && lb.images[teams::LogoContext::hud] == file("logos/williams_2017_hud.png"));

    drivers::Identity di{.person = drivers::people["person/mika_salo"],
                         .names = {Text::literal("Mika"), Text::literal("Salo"), Text::literal("Mika Salo"), "SAL"},
                         .number = 17,
                         .look = drivers::looks["swofty.driver-manager/mika_salo_look"],
                         .pools = {"f1_2026_driver_pool"}};
    drivers::Identity db;
    CHECK(from_json(to_json(di), db) && db.number == 17 && db.pools.size() == 1 && db.names.code == "SAL" &&
          db.look.id() == "swofty.driver-manager/mika_salo_look");
    drivers::Stats st{.driver = drivers::identities["swofty.driver-manager/mika_salo"], .pace = {78, 84}};
    CHECK(to_json(st).find("\"pace\":{\"start\":78,\"peak\":84}") != std::string::npos);
    drivers::Head hd{.archive = file("heads/senna/"), .rig_from = drivers::heads["game/male_ls222"], .body = drivers::BodyType::male};
    CHECK(to_json(hd).find("\"body\":\"male\"") != std::string::npos);

    wearables::Helmet hm{.archive = file("helmets/hamilton_2017_helmet.erp"),
                         .from = wearables::helmets["game/lewis_hamilton"],
                         .name = Text::literal("Hamilton 2017 (HX)"),
                         .driver = drivers::identities["game/lewis_hamilton"],
                         .colours = std::array<Colour, 4>{Colour("#FFE600"), Colour("#00A19B"), Colour("#7A2A8B"), Colour("#FFFFFF")}};
    std::string hmt = to_json(hm);
    CHECK(hmt.find("\"colours\":[\"#FFE600\",\"#00A19B\",\"#7A2A8B\",\"#FFFFFF\"]") != std::string::npos);
    wearables::Helmet hmb;
    CHECK(from_json(hmt, hmb) && hmb.colours && (*hmb.colours)[3] == Colour("#FFFFFF"));
    wearables::Suit su{.archive = file("kit/mercedes_2016_suit.erp"), .from = wearables::suits["game/mercedes"]};
    CHECK(to_json(su).find("\"colours\":null") != std::string::npos);
    wearables::SeasonSet ss{.team = teams::identities["swofty.classic-seasons-2016/mercedes"], .season = 2016};
    wearables::SeasonSet ssb;
    CHECK(from_json(to_json(ss), ssb) && ssb.season == 2016 && ssb.suit.empty());

    career::Calendar cal_json{.seasons = 3, .transfer_windows = {10}};
    CHECK(to_json(cal_json).find("\"seasons\":3") != std::string::npos);
    CHECK(!from_json("{\"year\":", back));
}

static void schemas()
{
    std::string s = career::seasons.schema();
    CHECK(s.find("\"x-registry\":\"career.season\"") != std::string::npos);
    CHECK(s.find("\"x-version\":\"1.0\"") != std::string::npos);
    CHECK(s.find("\"x-applies\":\"read\"") != std::string::npos);
    CHECK(s.find("\"x-snapshot\":true") != std::string::npos && s.find("\"x-snapshot-budget\":65536") != std::string::npos);
    CHECK(s.find("\"x-key\":[\"slot\"]") != std::string::npos);
    CHECK(s.find("\"x-key\":[\"round\",\"slot\",\"seat\"]") != std::string::npos);
    CHECK(s.find("\"x-ref\":\"content.package\"") != std::string::npos);
    CHECK(s.find("\"x-ref\":\"driver.identity\"") != std::string::npos);
    std::string v = cars::vehicles.schema();
    CHECK(v.find("\"x-applies\":\"boot\"") != std::string::npos && v.find("\"enum\":[\"f1\",\"f1_26\"") != std::string::npos);
    CHECK(cars::handling.schema().find("\"class\":") != std::string::npos);
    CHECK(content::packages.schema().find("\"x-key\":[\"key\"]") != std::string::npos);
    CHECK(test::things.schema().find("\"x-version\":\"1.2\"") != std::string::npos);
    CHECK(Registry<json::Value>("x.y").schema().empty());
    CHECK(sounds::engine_sounds.at_major() == "engine_sound@1");
    CHECK(teams::identities.schema().find("\"x-ref\":\"team.constructor\"") != std::string::npos);
    CHECK(teams::identities.schema().find("\"x-ref\":\"team.logo\"") != std::string::npos);
    CHECK(teams::logos.schema().find("\"x-applies\":\"read\"") != std::string::npos);
    CHECK(teams::pit_times.schema().find("\"x-ref\":\"team.pit_times\"") != std::string::npos);
    CHECK(drivers::identities.schema().find("\"x-ref\":\"driver.look\"") != std::string::npos);
    CHECK(drivers::identities.schema().find("\"x-ref\":\"driver.person\"") != std::string::npos);
    CHECK(drivers::looks.schema().find("\"x-ref\":\"helmet\"") != std::string::npos);
    CHECK(drivers::stats.schema().find("\"x-ref\":\"driver.identity\"") != std::string::npos);
    CHECK(wearables::season_sets.schema().find("\"x-ref\":\"suit\"") != std::string::npos);
    CHECK(wearables::helmets.schema().find("\"x-applies\":\"boot\"") != std::string::npos);
    CHECK(wearables::season_sets.at_major() == "wearable.season_set@1" && drivers::heads.at_major() == "driver.head@1");
    std::string l = liveries::liveries.schema();
    CHECK(l.find("\"x-registry\":\"livery\"") != std::string::npos && l.find("\"x-applies\":\"boot\"") != std::string::npos);
    CHECK(l.find("\"x-ref\":\"vehicle\"") != std::string::npos && l.find("\"enum\":[\"owned\",\"game\"]") != std::string::npos);
    CHECK(liveries::liveries.at_major() == "livery@1");
}

static void liveries_records()
{
    auto car = cars::vehicles["game/redbull_2025"];
    auto look = liveries::liveries.add("repaint", {.vehicle = car, .textures = file("livery/")});
    CHECK(look.empty());
    cars::vehicles.layer(car, [=](cars::Vehicle &v) { v.livery = look; });
    liveries::Livery special{.vehicle = cars::vehicles["game/toro_rosso_2026"],
                             .archive = file("archives/toro_rosso_alphatauri.erp"),
                             .special = true,
                             .picker_name = "lng_rm_livery_alphatauri",
                             .unlock = liveries::Unlock::owned};
    std::string j = to_json(special);
    CHECK(j.find("\"special\":true") != std::string::npos && j.find("\"unlock\":\"owned\"") != std::string::npos);
    liveries::Livery back;
    CHECK(from_json(j, back) && back.special && back.suffix == "auto" && back.picker_name == "lng_rm_livery_alphatauri");
}

static uint32_t g_mode, g_flags;
static int g_active = 1, g_follow = -1, g_camera = -1, g_on = -1;
static int fake_control(uint32_t car, uint32_t *mode, uint32_t *flags)
{
    if (car == 9)
        return SWOFTY_ASSET_LATE;
    *mode = g_mode;
    *flags = g_flags;
    return SWOFTY_ASSET_OK;
}
static int fake_set_control(uint32_t car, uint32_t mode)
{
    if (car == 7)
        return SWOFTY_REFUSED;
    g_mode = mode;
    return SWOFTY_ASSET_OK;
}
static int fake_active(int on) { return g_on = on, SWOFTY_ASSET_OK; }
static int fake_follow(uint32_t car) { return g_follow = int(car), SWOFTY_ASSET_OK; }
static int fake_camera(int32_t c) { return g_camera = c, c == 1 || c == 2 ? SWOFTY_ASSET_OK : SWOFTY_ASSET_INVALID; }
static int fake_current(int32_t *car, int32_t *camera)
{
    if (g_follow < 0)
        return SWOFTY_ASSET_LATE;
    *car = g_follow;
    *camera = g_camera;
    return SWOFTY_ASSET_OK;
}
static int fake_available() { return g_active; }

static void session_operations()
{
    cars::live::Car car(3);
    CHECK(sessions::set_control(car, sessions::Control::ai) == sessions::Result::unavailable);
    CHECK(!car.control());
    CHECK(spectator::set_active(true) == Result::unavailable && !spectator::current());
    SwoftySessionOps ops{SWOFTY_SESSION_OPS_VERSION, fake_available, fake_control, fake_set_control, fake_active,
                         fake_follow, fake_camera, fake_current};
    swofty::detail::set_session_ops_table(&ops);
    CHECK(sessions::set_control(car, sessions::Control::ai) == sessions::Result::ok && g_mode == 1);
    g_flags = 1;
    auto st = car.control();
    CHECK(st && st->mode == sessions::Control::ai && st->pit_lane());
    g_flags = 0x20;
    CHECK(car.control() && !car.control()->pit_lane());
    CHECK(!cars::live::Car(9).control());
    CHECK(sessions::set_control(cars::live::Car(7), sessions::Control::human) == sessions::Result::blocked);
    CHECK(spectator::set_active(true) == Result::ok && g_on == 1);
    CHECK(!spectator::current());
    CHECK(spectator::follow(cars::live::Car(5)) == Result::ok);
    CHECK(spectator::set_camera(spectator::Camera::broadcast) == Result::ok && g_camera == 2);
    auto view = spectator::current();
    CHECK(view && view->following.index() == 5 && view->camera == spectator::Camera::broadcast);
    CHECK(spectator::set_camera(static_cast<spectator::Camera>(9)) == Result::invalid);
    g_active = 0;
    CHECK(sessions::set_control(car, sessions::Control::human) == sessions::Result::unavailable && !car.control());
    swofty::detail::set_session_ops_table(nullptr);
}

int main()
{
    unavailable_without_core();
    session_operations();
    liveries_records();
    payload_accessors();
    round_trip();
    schemas();
    if (failures) {
        std::printf("sdk_headers_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("sdk_headers_test: ok\n");
    return 0;
}

#if defined(_WIN32)
SWOFTY_ADDON()
#endif
