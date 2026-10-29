#include "../../src/core/apply/content/logo_array.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace bc7 = swofty_core::apply::content::bc7;
namespace la = swofty_core::apply::content::logo_array;

static int failures = 0;

static void check(bool ok, const std::string &what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    failures += !ok;
}

static std::string slurp(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

int main(int argc, char **argv)
{
    std::string dir = argc > 1 ? argv[1] : "content/bc7";
    for (const char *name : {"shape64", "noise32", "alpha32", "gradient48", "text64x32", "flatmix32"}) {
        std::string raw = slurp(dir + "/" + name + ".rgba"), want = slurp(dir + "/" + name + ".bc7");
        size_t nl = raw.find('\n');
        int w = 0, h = 0;
        std::sscanf(raw.c_str(), "%d %d", &w, &h);
        std::string got = bc7::encode_logo(reinterpret_cast<const uint8_t *>(raw.data()) + nl + 1, w, h);
        size_t bad = 0, first = 0;
        for (size_t i = 0; i + 16 <= want.size(); i += 16)
            if (got.compare(i, 16, want, i, 16) != 0) {
                if (!bad)
                    first = i / 16;
                bad++;
            }
        check(got.size() == want.size() && !bad, std::string(name) + ": " + std::to_string(want.size() / 16) + " blocks equal the Python bytes" +
                                                      (bad ? " (" + std::to_string(bad) + " differ, first " + std::to_string(first) + ")" : ""));
    }
    la::Stock stock{slurp(dir + "/stock_head.bin"), slurp(dir + "/stock_data.bin"), slurp(dir + "/stock_view.bin"), {"a", "b", "c"}};
    std::string slice = slurp(dir + "/shape64.bc7"), why;
    std::string backwards(slice.rbegin(), slice.rend());
    la::Shape shape;
    check(la::shape_of(stock, shape, why) && shape.width == 64 && shape.height == 64 && shape.count == 3, "the synthetic stock array reads as 64x64 with 3 slices");
    la::Stock grown;
    bool ok = la::grow(stock, {{"d", slice}, {"e", backwards}}, grown, why);
    check(ok && grown.head == slurp(dir + "/grown_head.bin") && grown.data == slurp(dir + "/grown_data.bin") &&
              grown.view == slurp(dir + "/grown_view.bin"),
          "grown fragments equal the ones logos.py builds");
    check(ok && grown.names.size() == 5 && grown.names[3] == "d" && grown.names[4] == "e", "keys are appended in slice order");
    check(!la::grow(stock, {{"b", slice}}, grown, why) && why.find("already lists b") != std::string::npos, "a listed key is refused");
    check(!la::grow(stock, {{"x", slice.substr(1)}}, grown, why) && why.find("not 4096") != std::string::npos, "a slice of the wrong size is refused");
    la::Stock bad = stock;
    bad.names.pop_back();
    check(!la::shape_of(bad, shape, why), "a key list that is not one name per slice is refused");
    bad = stock;
    la::put32(bad.head, 8, 71);
    check(!la::shape_of(bad, shape, why), "a stock array that is not BC7 is refused");
    std::string raw = slurp(dir + "/shape64.rgba"), made;
    const uint8_t *px = reinterpret_cast<const uint8_t *>(raw.data()) + raw.find('\n') + 1;
    check(la::slice_from_rgba(shape, px, 64, 64, made, why) && made == slice, "an RGBA image becomes the slice logos.py encodes");
    check(la::slice_from_rgba(shape, px, 32, 64, made, why) && made.size() == shape.slice_bytes(), "an image of another size is scaled to the slice");
    return failures ? 1 : 0;
}
