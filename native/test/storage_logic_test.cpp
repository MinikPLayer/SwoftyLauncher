// Host unit test for the storage path logic (storage_logic.hpp): the relative paths an addon may name, the
// data folder root its files move to when the game can't create files in its addon folder, and the
// listing of a folder split between that root and the game folder. run_events_test.sh builds and runs it.
#include "../src/core/storage_logic.hpp"

#include <cstdio>
#include <string>

namespace sl = runtime::storage_logic;

static int fails;

static void expect(bool ok, const std::string &what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
    fails += !ok;
}

static std::string rel(const char *path, bool allow_root = false)
{
    std::string out;
    return sl::inside(path, allow_root, out) ? out : "(refused: " + out + ")";
}

int main()
{
    expect(rel("careers.json") == "careers.json", "a plain name stays as it is");
    expect(rel("slots\\1\\state.json") == "slots/1/state.json", "backslashes become slashes");
    expect(rel("a/..b/c") == "a/..b/c", "a name starting with two dots is not a climb");
    expect(rel("../escape.txt") == "(refused: ../escape.txt)", "a climb above the folder is refused");
    expect(rel("a\\..\\..\\b") == "(refused: a/../../b)", "a backslash climb is refused");
    expect(rel("save/..") == "(refused: save/..)", "a trailing climb is refused");
    expect(rel("/abs.txt") == "(refused: /abs.txt)", "an absolute path is refused");
    expect(rel("Z:\\drive.txt") == "(refused: Z:/drive.txt)", "a drive letter is refused");
    expect(rel("") == "(refused: )", "an empty path is refused for a file");
    expect(rel(nullptr) == "(refused: )", "a null path is refused for a file");
    expect(rel("", true).empty() && rel(".", true).empty() && rel(nullptr, true).empty(),
           "the folder itself is the empty path where a folder is allowed");
    expect(rel("inbox", true) == "inbox", "a subfolder lists as itself");
    expect(rel(sl::MARK) == std::string("(refused: ") + sl::MARK + ")", "the data folder mark is not an addon's file");
    expect(rel(sl::PROBE) == std::string("(refused: ") + sl::PROBE + ")", "the write test file is not an addon's file");
    expect(rel("slots/.swofty-data-folder") == "slots/.swofty-data-folder", "the mark's name in a subfolder is a file");

    expect(sl::data_root(L"C:\\Users\\Leon\\AppData\\Local\\Swofty", L"swofty.progressive") ==
               L"C:\\Users\\Leon\\AppData\\Local\\Swofty\\addon-storage\\swofty.progressive\\",
           "the data root is addon-storage/<id>/ in the data folder");
    expect(sl::data_root(L"C:\\Users\\Leon\\AppData\\Local\\Swofty\\", L"a.b") ==
               L"C:\\Users\\Leon\\AppData\\Local\\Swofty\\addon-storage\\a.b\\",
           "a trailing backslash on the data folder is dropped");
    expect(sl::data_root(L"", L"a.b").empty(), "no data folder, no data root");
    expect(sl::data_root(L"C:\\x", L"").empty(), "no addon, no data root");

    expect(sl::join(L"C:\\d\\", L"slots/1/state.json") == L"C:\\d\\slots\\1\\state.json",
           "a relative path joins its root with backslashes");
    expect(sl::join(L"C:\\d\\", L"") == L"C:\\d\\", "the empty path is the root");

    expect(sl::merge_listing("careers.json\nslots/\n", "careers.json\ndata/\nnative/\n") ==
               "careers.json\nslots/\ndata/\nnative/\n",
           "the listing has the data folder's names, then the game folder's others");
    expect(sl::merge_listing("", "a.txt\nb/\n") == "a.txt\nb/\n", "an empty data folder lists the game folder");
    expect(sl::merge_listing("a.txt\n", "") == "a.txt\n", "an empty game folder lists the data folder");
    expect(sl::merge_listing("x/\n", "x\ny\n") == "x/\ny\n", "a name in both is listed once, as the data folder has it");
    expect(sl::merge_listing("", std::string(sl::PROBE) + "\nz\n") == "z\n", "the write test file is never listed");
    expect(sl::merge_listing(std::string(sl::MARK) + "\nseasons.json\n", "data/\n") == "seasons.json\ndata/\n",
           "the data folder mark is never listed");

    printf(fails ? "storage logic test: %d FAILED\n" : "storage logic test: ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
