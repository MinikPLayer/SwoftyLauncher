#include "../../src/core/registry/table.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace swofty_core::registry;
namespace fs = std::filesystem;

static void write(const fs::path &p, const std::string &text)
{
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

int main()
{
    const char *tmp = std::getenv("TMPDIR");
    fs::path dir = fs::path(tmp ? tmp : "/tmp") / "swofty-index-load-test";
    fs::remove_all(dir);
    write(dir / "registry/index.json",
          R"({"version":1,"profile":"default","order":["a.code","b.data"],)"
          R"("data":{"b.data":["registry/b.data/records.json"]},"registry":{},"addons":{}})");
    write(dir / "registry/b.data/records.json", R"({"test.thing":{"extra_car":{"name":"extra","n":1}}})");

    IndexResult ix = load_index(dir.string());
    std::printf("index.json: found %d, %zu file(s), %zu failed\n", ix.found, ix.files, ix.failed);

    store().declare("test.thing",
                    swofty::json::parse(R"({"x-registry":"test.thing","x-version":"1.0","x-applies":"boot","type":"object",)"
                                        R"("properties":{"name":{"type":"string"},"n":{"type":"integer"}}})"));
    store().end_register();

    Record r = store().get("test.thing", "b.data/extra_car");
    std::printf("b.data/extra_car: %s\n", status_name(r.status));
    for (auto &f : store().refusals())
        std::printf("  refused: %s %s: %s\n", f.addon.c_str(), f.registry.c_str(), f.message.c_str());
    fs::remove_all(dir);
    if (r.status != Status::ok) {
        std::printf("FAIL: a data-only addon's record is refused because index.json is read at core attach, before any "
                    "SWOFTY_REGISTER declares the registry it writes to\n");
        return 1;
    }
    std::printf("ok: a data file's record lands once the registry is declared\n");
    return 0;
}
