#include "wine_test.hpp"
#include <swofty/assets.hpp>
#include <swofty/db.hpp>
#include <swofty/events.hpp>
#include <swofty/overlay.hpp>
#include <swofty/storage.hpp>

namespace assets = swofty::assets;
namespace storage = swofty::storage;
#include <swofty/telemetry.hpp>

namespace assets = swofty::assets;
namespace tel = swofty::telemetry;
namespace events = swofty::events;
namespace db = swofty::db;

namespace {

bool on()
{
    char v[4] = {};
    return GetEnvironmentVariableA("SWOFTY_TEST_ASSETS", v, sizeof v) && v[0] == '1';
}

void on_session(const SwoftyEvent *, void *)
{
}

void on_tick(double, void *)
{
}

}

SWOFTY_ON_LOAD
{
    if (!on())
        return;
    swofty::log("[t_assets] available: group %d, erp %d, db %d, lang %d, file %d, sound %d", assets::available("group"),
                assets::available("erp"), assets::available("db"), assets::available("lang"), assets::available("file"),
                assets::available("sound"));
    std::string_view erps[] = {"2025_asset_groups/t_assets/t.erp"};
    assets::Fragment frags[] = {{"erp/frag.bin"}};
    const std::byte one[] = {std::byte('1')};
    assets::Result r[] = {
        assets::register_group("asset_groups/t_assets/t.assetgroup", erps),
        assets::put_resource("eaid://t_assets/t.tif.image", frags, erps),
        assets::db_set("entrants.teams.team.t_assets", "int", "1"),
        assets::db_set(0xf01d03c7a1b2c3d4ull, 0, one),
        assets::lang_set("eng", "lng_t_assets", "Runtime assets API"),
        assets::lang_table("eng", "lang/extra.lng"),
        assets::overlay_file("t_assets/sponsor.dds", "files/sponsor.dds"),
        assets::lang_table("eng", "../escape.lng"),
    };
    for (auto x : r)
        swofty::log("[t_assets] result %s", assets::name(x));

    swofty::overlay::Result pr = swofty::overlay::on_paint([](SwoftyPaint *, void *) {});
    swofty::log("[t_assets] overlay available %d on_paint %s", swofty::overlay::available() ? 1 : 0,
                swofty::overlay::name(pr));
    swofty::log("[t_assets] storage available %d", storage::available());
    storage::Result sw = storage::write("save/state.txt", std::string_view("round-trip-ok"));
    std::string back;
    storage::Result sr = storage::read("save/state.txt", back);
    uint64_t sz = 0;
    storage::Result ss = storage::size("save/state.txt", &sz);
    storage::Result sa = storage::append("save/log.txt", std::string_view("one;"));
    storage::Result sa2 = storage::append("save/log.txt", std::string_view("two;"));
    char names[256] = {};
    size_t nl = 0;
    storage::Result sl = storage::list("save", names, sizeof names, &nl);
    for (char &ch : names)
        if (ch == '\n')
            ch = ',';
    storage::Result se = storage::write("../escape.txt", std::string_view("nope"));
    storage::Result sabs = storage::write("/abs.txt", std::string_view("nope"));
    storage::Result sdrv = storage::write("Z:\\drive.txt", std::string_view("nope"));
    storage::Result sbk = storage::write("..\\back.txt", std::string_view("nope"));
    storage::Result sm = storage::size("missing.txt", &sz);
    storage::Result srm = storage::remove("save/state.txt");
    storage::Result srm2 = storage::remove("save/state.txt");
    swofty::log("[t_assets] storage write %s", storage::name(sw));
    swofty::log("[t_assets] storage read %s '%s'", storage::name(sr), back.c_str());
    swofty::log("[t_assets] storage size %s %llu", storage::name(ss), (unsigned long long)sz);
    swofty::log("[t_assets] storage list %s %zu %s", storage::name(sl), nl, names[0] ? names : "(empty)");
    swofty::log("[t_assets] storage append %s %s", storage::name(sa), storage::name(sa2));
    swofty::log("[t_assets] storage escapes %s %s %s %s", storage::name(se), storage::name(sabs), storage::name(sdrv),
                storage::name(sbk));
    swofty::log("[t_assets] storage missing %s", storage::name(sm));
    swofty::log("[t_assets] storage remove %s", storage::name(srm));
    swofty::log("[t_assets] storage remove-again %s", storage::name(srm2));
    std::string seeded;
    storage::Result sseed = storage::read("seeded.txt", seeded);
    storage::Result srmseed = storage::remove("seeded.txt");
    swofty::log("[t_assets] storage seeded %s '%s' remove %s", storage::name(sseed), seeded.c_str(),
                storage::name(srmseed));

    uint32_t etok = 0;
    events::Result esub = events::subscribe(SWOFTY_EVENT_SESSION_START, on_session, nullptr, &etok);
    events::Result efrm = events::on_frame(on_tick, nullptr, nullptr);
    events::Result ebad = events::subscribe("not.an.event", on_session, nullptr, nullptr);
    events::Result euns = events::unsubscribe(etok);
    events::Result euns2 = events::unsubscribe(etok);
    swofty::log("[t_assets] events available %d", events::available());
    swofty::log("[t_assets] events subscribe %s on_frame %s unknown %s unsubscribe %s unsubscribe-again %s",
                events::name(esub), events::name(efrm), events::name(ebad), events::name(euns), events::name(euns2));

    SwoftyDbValue dv{};
    db::Result dget = db::get("entrants.teams.team.t_assets", &dv);
    swofty::log("[t_assets] db available %d get %s", db::available(), db::name(dget));
}

extern "C" __declspec(dllexport) void t_assets_late()
{
    if (!on())
        return;
    assets::Fragment frags[] = {{"erp/frag.bin"}};
    swofty::log("[t_assets] late put_resource: %s",
                assets::name(assets::put_resource("eaid://t_assets/late.tif.image", frags)));

    tel::SessionState ss = {};
    tel::CarState cs = {};
    swofty::log("[t_assets] telemetry: available %d, session %s, car0 %s, car99 %s", tel::available(),
                tel::name(tel::session(&ss)), tel::name(tel::car(0, &cs)), tel::name(tel::car(99, &cs)));
    const char *feed = nullptr;
    tel::Result sm = tel::shared_memory(&feed);
    swofty::log("[t_assets] telemetry: shared_memory %s name=%s", tel::name(sm), feed ? feed : "(none)");
}

SWOFTY_ADDON()
