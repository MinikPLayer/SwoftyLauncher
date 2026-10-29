#include "../../src/core/apply/content/lower.hpp"
#include "../../src/core/apply/files.hpp"

#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace files = swofty_core::apply::files;
namespace content = swofty_core::apply::content;
using swofty::json::Value;

namespace {

int failures = 0, checks = 0;

void check(bool ok, const std::string &what)
{
    checks++;
    if (!ok) {
        failures++;
        std::printf("FAIL: %s\n", what.c_str());
    }
}

struct Fake {
    std::set<std::string> installed, game;
    std::map<std::string, std::string> routes;
    std::vector<std::string> calls;
    bool fail = false;

    files::Sink sink()
    {
        files::Sink s;
        s.exists = [this](const std::string &t) { return installed.count(t) != 0; };
        s.in_game = [this](const std::string &p) { return game.count(p) != 0; };
        s.route = [this](const std::string &p, const files::Claim *w, std::string &why) {
            if (fail) {
                why = "no route on this build";
                return false;
            }
            calls.push_back(p + (w ? " -> " + w->target : std::string(" removed")));
            if (w)
                routes[p] = w->target;
            else
                routes.erase(p);
            return true;
        };
        return s;
    }
};

void paths()
{
    auto ok = [](const char *addon, const char *file, const char *target) {
        files::Resolved r = files::resolve(addon, file);
        check(r.ok() && r.target == target, std::string("resolve ") + addon + " " + file + " -> " + target + " (got " +
                                                 r.target + r.why + ")");
    };
    auto bad = [](const char *addon, const char *file, const char *why) {
        files::Resolved r = files::resolve(addon, file);
        check(!r.ok() && r.why.find(why) != std::string::npos,
              std::string("resolve ") + addon + " '" + file + "' refused (" + why + "): " + r.why);
    };
    ok("mod.esp", "sounds/v10_screamer.bdl", "addons/mod.esp/sounds/v10_screamer.bdl");
    ok("mod.esp", "./sounds//v10_screamer.bdl", "addons/mod.esp/sounds/v10_screamer.bdl");
    ok("mod.esp", "sounds\\V10_Screamer.BDL", "addons/mod.esp/sounds/V10_Screamer.BDL");
    ok("swofty.classic-seasons-2017", "sounds/./cs17_ferrari.bdl", "addons/swofty.classic-seasons-2017/sounds/cs17_ferrari.bdl");
    bad("mod.esp", "../other.addon/sounds/x.bdl", "leaves its folder");
    bad("mod.esp", "sounds/../../core/swofty_core.dll", "leaves its folder");
    bad("mod.esp", "sounds/..", "leaves its folder");
    bad("mod.esp", "..\\..\\F1_25.exe", "leaves its folder");
    bad("mod.esp", "/sounds/x.bdl", "absolute");
    bad("mod.esp", "\\sounds\\x.bdl", "absolute");
    bad("mod.esp", "C:/Windows/x.bdl", "reject");
    bad("mod.esp", "sounds/x.bdl:stream", "reject");
    bad("mod.esp", "sounds/v10_\xc3\xa9" "cran.bdl", "non-ASCII");
    bad("mod.esp", "sounds/\xe5\xa3\xb0.bdl", "non-ASCII");
    bad("mod.esp", "sounds/v10 (1).bdl", "reject");
    bad("mod.esp", "sounds/", "folder");
    bad("mod.esp", "", "empty");
    bad("mod.esp", "./.", "names no file");
    bad("", "sounds/x.bdl", "not an addon id");
    bad("..", "sounds/x.bdl", "not an addon id");
    bad("a/b", "sounds/x.bdl", "not an addon id");
    bad("core", "swofty_core.dll", "not an addon id");

    files::Normal g = files::game_path("Audio\\2025\\Bundles\\V10_Screamer.BDL");
    check(g.ok() && g.path == "audio/2025/bundles/v10_screamer.bdl", "a game path is case-folded with forward slashes: " + g.path);
    check(files::game_path("/game/audio/2025/bundles/x.bdl").path == "audio/2025/bundles/x.bdl", "/game/ is dropped");
    check(!files::game_path("swofty/addons/mod.esp/x.bdl").ok(), "a game path inside swofty/ is refused");
    check(!files::game_path("audio/../swofty/x.bdl").ok(), "a game path with .. is refused");
    check(!files::game_path("audio/*.bdl").ok(), "a wildcard game path is refused");

    Fake f;
    files::Router r(f.sink());
    std::string longname(240, 'a');
    f.installed.insert("addons/mod.esp/" + longname + ".bdl");
    files::Outcome o = r.redirect("audio/2025/bundles/long.bdl", "mod.esp", longname + ".bdl", "engine_sound", "mod.esp/long", "boot");
    check(o.kind == files::Kind::invalid && o.line.find("longer than") != std::string::npos,
          "a target past the game's path length is refused: " + o.line);
    check(f.routes.empty(), "a refused redirect installs no route");
}

void routing()
{
    Fake f;
    f.installed = {"addons/mod.esp/sounds/v10_screamer.bdl", "addons/mod.late/sounds/v10.bdl",
                   "addons/mod.early/sounds/v10.bdl", "addons/mod.box/sounds/v10_box.bdl"};
    f.game = {"audio/2025/bundles/cs17_ferrari.bdl"};
    files::Router r(f.sink());
    r.set_order({"swofty.classic-seasons-2017", "mod.early", "mod.esp", "mod.box", "mod.late"});

    files::Outcome o = r.redirect("audio/2025/bundles/v10_screamer.bdl", "mod.esp", "sounds/v10_screamer.bdl", "engine_sound",
                                  "mod.esp/v10_screamer", "boot");
    check(o.kind == files::Kind::routed, "an installed file is routed");
    check(o.line == "redirect audio/2025/bundles/v10_screamer.bdl -> mod.esp/sounds/v10_screamer.bdl (engine_sound mod.esp/v10_screamer)",
          "the redirect line names the game path and the addon's file: " + o.line);
    check(f.routes["audio/2025/bundles/v10_screamer.bdl"] == "addons/mod.esp/sounds/v10_screamer.bdl", "the sink gets the route");
    check(o.conflict.empty(), "one addon is no conflict");
    o = r.redirect("audio/2025/bundles/v10_screamer.bdl", "mod.esp", "sounds/v10_screamer.bdl", "engine_sound",
                   "mod.esp/v10_screamer", "boot");
    check(o.kind == files::Kind::routed && f.calls.size() == 1, "the same claim again changes nothing");

    o = r.redirect("audio/2025/bundles/cs17_ferrari.bdl", "swofty.classic-seasons-2017", "sounds/cs17_ferrari.bdl",
                   "engine_sound", "swofty.classic-seasons-2017/ferrari", "boot");
    check(o.kind == files::Kind::kept && !o.refused(), "a file the addon's folder lacks but the game folder has is kept: " + o.line);
    check(!f.routes.count("audio/2025/bundles/cs17_ferrari.bdl"), "a kept path is not routed");
    o = r.redirect("audio/2025/bundles/nowhere.bdl", "mod.esp", "sounds/nowhere.bdl", "engine_sound", "mod.esp/nowhere", "boot");
    check(o.kind == files::Kind::missing && o.refused(), "a file in neither folder is refused: " + o.line);

    o = r.redirect("audio/2025/bundles/v10.bdl", "mod.late", "sounds/v10.bdl", "engine_sound", "mod.late/v10", "boot");
    check(o.kind == files::Kind::routed, "the later addon claims first");
    o = r.redirect("Audio/2025/Bundles/V10.bdl", "mod.early", "sounds/v10.bdl", "engine_sound", "mod.early/v10", "boot");
    check(o.kind == files::Kind::shadowed && o.refused() == false, "an earlier addon's claim on the same path is shadowed: " + o.line);
    check(f.routes["audio/2025/bundles/v10.bdl"] == "addons/mod.late/sounds/v10.bdl", "the later addon in load order wins, whatever the claim order");
    check(o.conflict == "file conflict: audio/2025/bundles/v10.bdl is redirected by mod.early, mod.late; mod.late wins "
                       "(engine_sound mod.late/v10, later in load order; reorder addons to change it)",
          "the conflict line: " + o.conflict);
    auto cs = r.conflicts();
    check(cs.size() == 1 && cs[0].wins == "mod.late" && cs[0].addons == std::vector<std::string>{"mod.early", "mod.late"},
          "one conflict, in load order");
    Value j = r.conflicts_json();
    check(j.size() == 1 && j[0]["kind"].as_string() == "file" && j[0]["path"].as_string() == "audio/2025/bundles/v10.bdl" &&
              j[0]["wins"].as_string() == "mod.late" && j[0]["registry"].as_string() == "engine_sound" &&
              j[0]["id"].as_string() == "mod.late/v10" && j[0]["addons"].size() == 2,
          "conflicts.json gets a file entry: " + swofty::json::dump(j));

    std::size_t before = f.calls.size();
    o = r.redirect("audio/2025/bundles/v10_screamer.bdl", "mod.box", "sounds/v10_box.bdl", "engine_sound", "mod.box/v10",
                   "content:mod.box/f1_2017_v10");
    check(o.kind == files::Kind::routed && f.routes["audio/2025/bundles/v10_screamer.bdl"] == "addons/mod.box/sounds/v10_box.bdl",
          "a content-scoped claim of a later addon takes the path while its content is active");
    check(f.calls.size() == before + 1, "it changes the route once");
    auto lines = r.put_back("content:mod.box/f1_2017_v10");
    check(lines.size() == 1 && lines[0] == "put back audio/2025/bundles/v10_screamer.bdl: now mod.esp/sounds/v10_screamer.bdl "
                                           "(engine_sound mod.esp/v10_screamer)",
          "put back returns the boot claim: " + (lines.empty() ? std::string() : lines[0]));
    check(f.routes["audio/2025/bundles/v10_screamer.bdl"] == "addons/mod.esp/sounds/v10_screamer.bdl", "the route is the boot claim's again");
    check(r.put_back("content:mod.box/f1_2017_v10").empty(), "a second put back changes nothing");

    f.installed.insert("addons/mod.box/sounds/only.bdl");
    r.redirect("audio/2025/bundles/only.bdl", "mod.box", "sounds/only.bdl", "engine_sound", "mod.box/only", "session");
    check(f.routes.count("audio/2025/bundles/only.bdl"), "a session claim routes");
    lines = r.put_back("session");
    check(lines.size() == 1 && lines[0] == "put back audio/2025/bundles/only.bdl: the game's own file again" &&
              !f.routes.count("audio/2025/bundles/only.bdl"),
          "put back with no other claim removes the route");

    r.set_order({"mod.esp"});
    f.installed.insert("addons/mod.unknown/sounds/v10_screamer.bdl");
    o = r.redirect("audio/2025/bundles/v10_screamer.bdl", "mod.unknown", "sounds/v10_screamer.bdl", "engine_sound",
                   "mod.unknown/v10", "boot");
    check(o.kind == files::Kind::routed, "an addon missing from the load order sorts last, after every listed addon");

    Fake broken;
    broken.fail = true;
    broken.installed = {"addons/mod.esp/sounds/v10_screamer.bdl"};
    files::Router b(broken.sink());
    o = b.redirect("audio/2025/bundles/v10_screamer.bdl", "mod.esp", "sounds/v10_screamer.bdl", "engine_sound",
                   "mod.esp/v10_screamer", "boot");
    check(o.kind == files::Kind::unavailable && o.line.find("no route on this build") != std::string::npos && b.size() == 0,
          "a route the core can't install is refused and not kept: " + o.line);
}

void origins()
{
    std::map<std::string, std::string> o{{"/bundle", "mod.layer"}, {"/settings_from", "mod.other"}};
    check(files::owner_of_field(o, "mod.owner", "/bundle") == "mod.layer", "a layer that set the bundle owns its Path");
    check(files::owner_of_field({{"/settings_from", "mod.other"}}, "mod.owner", "/bundle") == "mod.owner",
          "an unchanged bundle is the record owner's");
    check(files::owner_of_field({{"/bundle", "mod.whole"}}, "mod.owner", "/bundle/stock") == "mod.whole",
          "a field under a changed one is that addon's");
}

void lowering()
{
    Value sound = swofty::json::parse(
        R"({"bundle": "sounds/v10_screamer.bdl", "settings_from": "game/ferrari_2025", "tuning_from": "game/ferrari_2025"})");
    content::Plan p;
    content::lower_engine_sound("mod.esp/v10_screamer", sound, p);
    check(p.redirects.size() == 1, "a file bundle lowers to one redirect");
    if (p.redirects.size() == 1) {
        auto &rd = p.redirects[0];
        check(rd.path == "audio/2025/bundles/v10_screamer.bdl" && rd.file == "sounds/v10_screamer.bdl" && rd.addon == "mod.esp" &&
                  rd.id == "mod.esp/v10_screamer",
              "the redirect is the game's bundle path for the file's name");
    }
    content::Plan stock;
    content::lower_engine_sound("mod.esp/pinned", swofty::json::parse(R"({"bundle": {"stock": "audio/2025/bundles/ferrari_2024.bdl"}})"),
                                stock);
    check(stock.redirects.empty(), "a stock bundle is not redirected");

    Value car = swofty::json::parse(R"({"label": "cs17_ferrari", "donor": "game/ferrari_2025", "engine_sound": "mod.esp/v10_screamer",
        "audio_switch": "f1_M_ferrari", "audio_from": "f1_m_ferrari"})");
    content::Lookup lookup = [&](const std::string &reg, const std::string &id) -> const Value * {
        return reg == "engine_sound" && id == "mod.esp/v10_screamer" ? &sound : nullptr;
    };
    content::Source source = [](const std::string &key) -> std::optional<content::Stock> {
        if (key.rfind("audio.audio_car_data.f1_m_ferrari.", 0) == 0)
            return content::Stock{2, "ferrari_2025.bdl"};
        return std::nullopt;
    };
    content::Plan v;
    content::lower_vehicle("swofty.classic-seasons-2017/ferrari", car, lookup, source, v);
    std::string bundle_name;
    for (std::size_t i = 0; i < v.ops.size(); i++)
        if (v.ops[i]["key"].as_string() == "audio.audio_car_data.f1_M_ferrari.bundle_name") {
            std::string hex(v.ops[i]["value"].as_string());
            for (std::size_t k = 0; k + 1 < hex.size(); k += 2)
                bundle_name += char(std::stoi(hex.substr(k, 2), nullptr, 16));
        }
    std::string redirected = p.redirects.empty() ? std::string() : p.redirects[0].path;
    check(!bundle_name.empty() && redirected == "audio/2025/bundles/" + bundle_name,
          "the car's audio record names the redirected bundle: " + bundle_name + " vs " + redirected);
    Value row = p.ops.size() ? p.ops[0]["set"]["bundle_name"] : Value();
    check(row.is_string() && "audio/2025/bundles/" + std::string(row.as_string()) + ".bdl" == redirected,
          "the bundle settings row's bundle_name is the redirected file's name");
}

}

int main()
{
    paths();
    routing();
    origins();
    lowering();
    std::printf("files: %d check(s), %d failure(s)\n", checks, failures);
    return failures ? 1 : 0;
}
