// Host unit test for the state folder path logic (state_logic.hpp): the folder the launcher names for the core's
// state files, as a Windows drive path (a Wine Z: path on Linux), with non-ASCII names. run_events_test.sh builds and
// runs it.
#include "../src/core/state_logic.hpp"

#include <cstdio>
#include <string>

namespace sl = runtime::state_logic;

static int fails;

static void expect(bool ok, const std::string &what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
    fails += !ok;
}

int main()
{
    expect(sl::root(L"Z:\\home\\a\\.local\\share\\swofty") == L"Z:\\home\\a\\.local\\share\\swofty\\",
           "a Wine Z: path gets a trailing backslash");
    expect(sl::root(L"Z:/home/a/share/swofty/") == L"Z:\\home\\a\\share\\swofty\\", "slashes become backslashes, one trailing");
    expect(sl::root(L"C:\\Users\\a\\AppData\\Local\\Swofty\\\\") == L"C:\\Users\\a\\AppData\\Local\\Swofty\\",
           "repeated trailing backslashes collapse");
    expect(sl::root(L"\"D:\\Mods Data\\Swofty\" ") == L"D:\\Mods Data\\Swofty\\", "quotes and spaces around the path go");
    expect(sl::root(L"Z:\\home\\jos\u00e9\\\u30b9\u30dd\u30fc\u30c4") == L"Z:\\home\\jos\u00e9\\\u30b9\u30dd\u30fc\u30c4\\",
           "non-ASCII names are kept as they are");
    expect(sl::root(L"\\\\nas\\share\\swofty") == L"\\\\nas\\share\\swofty\\", "a share path is accepted");
    expect(sl::root(L"Z:\\") == L"Z:\\", "a drive root keeps its one backslash");
    expect(sl::root(L"").empty(), "an empty path is no folder");
    expect(sl::root(L"relative\\dir").empty(), "a relative path is refused");
    expect(sl::root(L"/home/a/swofty").empty(), "a Linux path is refused: the game sees Z: paths");
    expect(sl::root(L"\\\\\\x").empty(), "a malformed share path is refused");
    expect(sl::join(L"Z:\\d\\", L"registry/ids.json") == L"Z:\\d\\registry\\ids.json", "join turns slashes into backslashes");
    printf(fails ? "state logic test: %d FAILED\n" : "state logic test: ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
