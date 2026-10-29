#include "../../src/core/registry/declare_all.hpp"
#include "../../src/core/registry/older.hpp"
#include "../../src/core/registry/table.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>

using namespace swofty_core::registry;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(c, ...)                                  \
    do {                                               \
        if (!(c)) {                                    \
            std::printf("FAIL: " __VA_ARGS__);         \
            std::printf("\n");                         \
            failures++;                                \
        }                                              \
    } while (0)

static std::string slurp(const fs::path &p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void write(const fs::path &p, const std::string &text)
{
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

int main()
{
    const char *tmp = std::getenv("TMPDIR");
    fs::path dir = fs::path(tmp ? tmp : "/tmp") / "swofty-declare-all-test";
    fs::remove_all(dir);
    write(dir / "registry/index.json",
          R"({"version":1,"profile":"default","order":["b.data"],"data":{"b.data":["registry/b.data/records.json"]},"registry":{},"addons":{}})");
    write(dir / "registry/b.data/records.json", R"({
  "vehicle": {"car": {"label": "ig_car"}},
  "vehicle.package": {"pkg": {"label": "ig_pkg"}},
  "team.identity": {"team": {"names": {"title": {"literal": "IG Racing"}, "code": "IGR"}, "livery_name": "ig_racing"}},
  "team.logo": {"logo": {"images": {"hud": "logos/hud.png"}}},
  "suit": {"suit": {"archive": "kit/suit.erp"}},
  "driver.look": {"look": {"helmet": null, "suit": null, "gloves": null}},
  "driver.identity": {"driver": {"names": {"first": {"literal": "Ida"}, "last": {"literal": "Gamer"}, "full": {"literal": "Ida Gamer"}, "code": "IGA"}, "look": "b.data/look"}}
})");
    std::vector<std::pair<std::string, std::string>> keys = {
        {"vehicle", "b.data/car"}, {"vehicle.package", "b.data/pkg"}, {"team.identity", "b.data/team"}, {"team.logo", "b.data/logo"},
        {"suit", "b.data/suit"},   {"driver.look", "b.data/look"},    {"driver.identity", "b.data/driver"}};

    IndexResult ix = load_index(dir.string());
    CHECK(ix.found && ix.files == 1, "the data-only addon's index.json loads (found %d, %zu file(s))", ix.found, ix.files);
    std::printf("data-only addon: %zu pending call(s) before any declaration\n", store().pending());
    CHECK(store().pending() > 0, "the records wait for registries nothing has declared");

    std::map<std::string, std::string> declared;
    std::size_t n = declare_system_registries([&](const std::string &name, const std::string &schema) {
        if (store().declared(name))
            return false;
        declared[name] = schema;
        return store().declare(name, swofty::json::parse(schema), false, "") == 0;
    });
    std::printf("core declared %zu registries\n", n);
    CHECK(n == declared.size() && n >= 24, "the core declares every system registry (%zu)", n);
    CHECK(store().pending() == 0, "nothing waits after the core declared (%zu)", store().pending());

    for (auto &[name, schema] : declared) {
        auto def = store().def(name);
        CHECK(def.has_value(), "%s is declared", name.c_str());
        if (!def)
            continue;
        swofty::json::Value s = swofty::json::parse(schema);
        Def want = parse_schema(name, s);
        CHECK(def->applies == want.applies && def->snapshot == want.snapshot && def->budget == want.budget &&
                  def->major == want.major && def->minor == want.minor,
              "%s: the declaration carries the header's options", name.c_str());
    }
    {
        auto d = store().def("team.logo");
        CHECK(d && d->applies == Applies::read, "team.logo applies on read as its header says");
        auto v = store().def("vehicle");
        CHECK(v && v->applies == Applies::boot, "vehicle applies at boot as its header says");
    }

    {
        swofty_core::registry::older::declare(store());
        std::set<std::string> core;
        for (auto &name : store().registries())
            core.insert(name);
        std::set<std::string> shipped;
        fs::path root = "../../packages/sdk/std/registries";
        for (auto &e : fs::directory_iterator(root))
            if (e.is_directory())
                shipped.insert(e.path().filename().string());
        CHECK(!shipped.empty(), "the registry directories are found at %s", root.string().c_str());
        for (auto &name : shipped)
            CHECK(core.count(name), "packages/sdk/std/registries/%s is declared by the core (declare_all.hpp or older.hpp)", name.c_str());
        for (auto &name : core)
            CHECK(shipped.count(name), "%s is declared by the core but has no directory in packages/sdk/std/registries", name.c_str());
        CHECK(core.size() == shipped.size(), "the core declares %zu registries, the directories hold %zu", core.size(), shipped.size());
    }

    std::size_t before = store().registries().size();
    for (auto &[name, schema] : declared)
        store().declare(name, swofty::json::parse(schema), false, "some.addon");
    CHECK(store().registries().size() == before, "an addon declaring the same registries again adds none (%zu -> %zu)", before,
          store().registries().size());
    for (auto &[name, schema] : declared) {
        auto def = store().def(name);
        Def want = parse_schema(name, swofty::json::parse(schema));
        CHECK(def && def->applies == want.applies && def->minor == want.minor, "%s is unchanged by the second declaration", name.c_str());
    }

    store().end_register();
    for (auto &[reg, id] : keys) {
        Record r = store().get(reg, id);
        std::printf("%s %s: %s\n", reg.c_str(), id.c_str(), status_name(r.status));
        CHECK(r.status == Status::ok, "%s %s reaches the registry", reg.c_str(), id.c_str());
    }
    for (auto &f : store().refusals())
        std::printf("  refused: %s %s: %s\n", f.addon.c_str(), f.registry.c_str(), f.message.c_str());
    fs::remove_all(dir);
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok: the core's declarations let a data-only addon's records land and agree with the headers\n");
    return 0;
}
