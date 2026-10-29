// Host unit test for the test save sandbox (save_sandbox_logic.hpp): the run token, the device checks
// made before the Steam remote storage interface is replaced, and the folder that stands in for Steam
// remote storage. The profile save's two files are random bytes here, as opaque to the sandbox as the
// game's encrypted data file and its security index. run_save_sandbox_test.sh builds and runs it.
#include "../src/core/save_sandbox_logic.hpp"

#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

namespace ss = runtime::save_sandbox;
namespace fs = std::filesystem;

static int fails;

static void expect(bool ok, const std::string &what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
    fails += !ok;
}

static std::vector<uint8_t> noise(size_t n, uint32_t seed)
{
    std::mt19937 r(seed);
    std::vector<uint8_t> v(n);
    for (auto &b : v)
        b = uint8_t(r());
    return v;
}

static const char *TOKEN = "0123456789abcdef0123456789abcdef";

static std::string esc(const char *s)
{
    std::string o;
    for (; *s; s++)
        o += *s == '\\' ? std::string("\\\\") : std::string(1, *s);
    return o;
}

static void run_token()
{
    std::string good = std::string(R"({"schema":1,"run":"20261028-1702","token":")") + TOKEN +
                       R"(","saves":"saves","exe_sha256":"d873"})";
    auto r = ss::parse_run(good, TOKEN);
    expect(r.ok && r.saves == "saves" && r.id == "20261028-1702" && r.exe_sha256 == "d873",
           "run.json with the matching token verifies");
    expect(!ss::parse_run(good, "").ok, "no token in the environment: refused");
    expect(!ss::parse_run(good, "ffffffffffffffffffffffffffffffff").ok, "another run's token: refused");
    expect(!ss::parse_run("", TOKEN).ok, "no run.json: refused");
    expect(!ss::parse_run("{not json", TOKEN).ok, "run.json that does not parse: refused");
    expect(!ss::parse_run(R"({"schema":2,"token":"0123456789abcdef0123456789abcdef","saves":"saves"})", TOKEN).ok,
           "another schema: refused");
    for (const char *saves : {"..", "../saves", "a/b", "a\\b", "C:", ""}) {
        std::string doc = std::string(R"({"schema":1,"token":")") + TOKEN + R"(","saves":")" + saves + "\"}";
        expect(!ss::parse_run(doc, TOKEN).ok, std::string("saves '") + saves + "' is not a plain folder name: refused");
    }
    expect(!ss::parse_run(R"({"schema":1,"token":"short","saves":"saves"})", "short").ok, "a short token: refused");
    expect(r.sandbox && r.agent_v == 0 && r.dlls.empty(), "saves asks for the sandbox; no agent object, no DLLs");
    std::string agent = std::string(R"({"schema":1,"run":"r2-b1","token":")") + TOKEN +
                        R"(","agent":{"v":1,"dlls":["dll/my.addon.dll"]}})";
    auto a = ss::parse_run(agent, TOKEN);
    expect(a.ok && !a.sandbox && a.saves.empty() && a.agent_v == 1 && a.dlls.size() == 1 && a.dlls[0] == "dll/my.addon.dll",
           "a run without saves is the agent's only, with its test DLLs");
    for (const char *dll : {"../x.dll", "/x.dll", "\\x.dll", "C:x.dll", ""}) {
        std::string doc = std::string(R"({"schema":1,"run":"r","token":")") + TOKEN + R"(","agent":{"v":1,"dlls":[")" +
                          esc(dll) + "\"]}}";
        expect(!ss::parse_run(doc, TOKEN).ok, std::string("a test DLL '") + dll + "' outside the agent folder: refused");
    }
    expect(!ss::parse_run(std::string(R"({"schema":1,"run":"../r","token":")") + TOKEN + "\"}", TOKEN).ok,
           "a run id with a path: refused");
    expect(ss::still_armed(agent, TOKEN) && !ss::still_armed(R"({"schema":1,"run":"r2-b1"})", TOKEN) &&
               !ss::still_armed("", TOKEN),
           "a retired run.json (no token) stops the agent");
}

static void paths()
{
    expect(ss::windows_path("/home/swofty/.local/share/x/test-runs/r1/saves/") ==
               "Z:\\home\\swofty\\.local\\share\\x\\test-runs\\r1\\saves",
           "a Unix path maps to Wine's Z: drive");
    expect(ss::windows_path("C:\\Users\\a\\saves\\") == "C:\\Users\\a\\saves", "a Windows path stays");
    expect(ss::ansi_path("Z:\\home\\a\\local") && !ss::ansi_path("Z:\\h\xc3\xa9") && !ss::ansi_path(""),
           "the user data folder must be plain ASCII");
    for (const char *n : {"savegame@profile#NXDSMWW.BWW", "savegame@profile#QKRHMYXE", "corrupt_saves#7656.zip"})
        expect(ss::valid_name(n), std::string("remote name ") + n + " is valid");
    for (const char *n : {"", ".", "..", "../x", "a/b", "a\\b", "C:x", "x.swofty-tmp", "a..b"})
        expect(!ss::valid_name(n), std::string("remote name '") + n + "' is refused");
}

static void device()
{
    ss::Device d;
    d.readable = true;
    d.vtable = 0x145a5c640;
    d.start = d.start_slot = 0x14413af70;
    d.helper = 0x1000;
    d.iface = 0x2000;
    d.iface_vtable = 0x7ff000;
    d.iface_slots_ok = true;
    d.store_in_cloud = 1;
    d.user_data_len = 60;
    expect(ss::check_device(d).empty(), "the Steam save device passes");
    auto bad = [&](auto change, const char *what) {
        ss::Device x = d;
        change(x);
        std::string why = ss::check_device(x);
        printf("      %s -> %s\n", what, why.c_str());
        expect(!why.empty(), std::string("refused: ") + what);
    };
    bad([](ss::Device &x) { x.readable = false; }, "an unreadable device");
    bad([](ss::Device &x) { x.start_slot = 0x14413b000; }, "a device whose slot +0x08 is another function");
    bad([](ss::Device &x) { x.start = x.start_slot = 0; }, "no resolved start");
    bad([](ss::Device &x) { x.helper = 0; }, "no Steam helper (local saves)");
    bad([](ss::Device &x) { x.store_in_cloud = 0; }, "m_storeInCloud off (local saves in the player's folder)");
    bad([](ss::Device &x) { x.iface = 0; }, "no remote storage interface");
    bad([](ss::Device &x) { x.iface_vtable_in_game = true; }, "an interface whose vtable is in the game");
    bad([](ss::Device &x) { x.iface_slots_ok = false; }, "an interface whose slots are not code");
    bad([](ss::Device &x) { x.user_data_len = ss::USER_DATA_CAP; }, "an unterminated user data folder");
}

static void slots()
{
    int n = 0;
    for (int i = 0; i < ss::SLOTS; i++)
        n += ss::served(i);
    expect(n == 19, "19 slots are served from the folder, the rest answer 0");
    for (int i : {ss::FileWrite, ss::FileRead, ss::FileDelete, ss::FileExists, ss::GetFileSize, ss::GetFileTimestamp,
                  ss::GetFileCount, ss::GetFileNameAndSize})
        expect(ss::served(i), std::string("the save device's slot ") + ss::slot_name(i) + " is served");
    for (int i : {int(ss::FileWriteAsync), int(ss::FileReadAsync), int(ss::FileShare), int(ss::FileWriteStreamOpen), 24, 40})
        expect(!ss::served(i), std::string("slot ") + ss::slot_name(i) + " is not served");
    expect(std::string(ss::slot_name(ss::GetFileNameAndSize)) == "GetFileNameAndSize" &&
               std::string(ss::slot_name(ss::EndFileWriteBatch)) == "EndFileWriteBatch",
           "slot names follow the v016 declaration order");
}

static void folder(const fs::path &tmp)
{
    fs::remove_all(tmp);
    ss::RemoteDir dir(tmp / "remote");
    expect(dir.count() == 0, "an empty sandbox lists no files, even before its folder exists");
    auto data = noise(894408, 1), index = noise(416, 2);
    expect(dir.write("savegame@profile#NXDSMWW.BWW", data.data(), int32_t(data.size())), "FileWrite of the data file");
    expect(dir.write("savegame@profile#QKRHMYXE", index.data(), int32_t(index.size())), "FileWrite of the index");
    std::vector<uint8_t> back(data.size() + 100);
    expect(dir.read("savegame@profile#NXDSMWW.BWW", back.data(), int32_t(back.size())) == int32_t(data.size()) &&
               std::equal(data.begin(), data.end(), back.begin()),
           "FileRead returns the data file byte for byte");
    std::vector<uint8_t> small(100);
    expect(dir.read("savegame@profile#QKRHMYXE", small.data(), 100) == 100 &&
               std::equal(small.begin(), small.end(), index.begin()),
           "FileRead into a short buffer returns its first bytes");
    expect(dir.size("savegame@profile#QKRHMYXE") == 416 && dir.size("missing") == 0, "GetFileSize");
    expect(dir.exists("savegame@profile#QKRHMYXE") && !dir.exists("savegame@profile#X"), "FileExists");
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
    int64_t ts = dir.timestamp("savegame@profile#QKRHMYXE");
    expect(ts > now - 120 && ts < now + 120, "GetFileTimestamp is the file's write time in Unix seconds");
    expect(dir.count() == 2, "GetFileCount lists both files");
    int32_t sz = -1;
    std::string first = dir.name_at(0, &sz);
    expect(first == "savegame@profile#NXDSMWW.BWW" && sz == int32_t(data.size()), "GetFileNameAndSize(0)");
    expect(std::string(dir.name_at(1, &sz)) == "savegame@profile#QKRHMYXE" && sz == 416, "GetFileNameAndSize(1)");
    expect(std::string(dir.name_at(2, &sz)).empty() && sz == 0, "GetFileNameAndSize past the end is empty");
    auto index2 = noise(416, 3);
    expect(dir.write("savegame@profile#QKRHMYXE", index2.data(), 416), "FileWrite over an existing file");
    expect(dir.read("savegame@profile#QKRHMYXE", small.data(), 100) == 100 &&
               std::equal(small.begin(), small.end(), index2.begin()),
           "the rewrite replaced the bytes");
    expect(!fs::exists(tmp / "remote" / "savegame@profile#QKRHMYXE.swofty-tmp"), "no temporary file is left");
    fs::create_directories(tmp / "remote" / "sub");
    std::ofstream(tmp / "remote" / "stray.swofty-tmp") << "x";
    expect(dir.count() == 2, "folders and temporary files are not listed");
    expect(!dir.write("../escape", data.data(), 10) && !fs::exists(tmp / "escape"), "a name that leaves the folder is refused");
    expect(!dir.write("a\\b", data.data(), 10), "a name with a separator is refused");
    expect(dir.write("empty", nullptr, 0) && dir.exists("empty") && dir.size("empty") == 0, "an empty file round-trips");
    expect(dir.remove("empty") && !dir.exists("empty") && !dir.remove("empty"), "FileDelete, twice");
    expect(dir.used() == data.size() + 416, "the quota counts the files' bytes");
    fs::remove_all(tmp);
}

int main(int argc, char **argv)
{
    fs::path tmp = argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path() / "save_sandbox_test";
    run_token();
    paths();
    device();
    slots();
    folder(tmp);
    printf("%s (%d failure(s))\n", fails ? "SOME FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
