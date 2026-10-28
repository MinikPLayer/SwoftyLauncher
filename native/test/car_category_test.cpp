// Host unit test for the LAN Car Category addon boxes (car_category_logic.hpp): the press commands, the
// option value name decoding, the value match and the handle release. run_events_test.sh builds and runs it.
#include "../src/core/car_category_logic.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace cc = runtime::car_category;
namespace era = runtime::era;

static int fails;

static void expect(bool ok, const std::string &what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
    fails += !ok;
}

struct Node {
    alignas(8) uint8_t mem[0x30] = {};
    void *&holders() { return *reinterpret_cast<void **>(mem + era::NODE_HOLDERS); }
    int16_t &refs() { return *reinterpret_cast<int16_t *>(mem + era::NODE_REFS); }
};

int main()
{
    expect(cc::button_hash(1) == cc::BUTTON_1 && cc::button_hash(2) == cc::BUTTON_2, "button_N hashes match the handler's");
    expect(cc::added_box_of(cc::button_hash(3), 2) == 0, "button_3 is the first added box");
    expect(cc::added_box_of(cc::button_hash(4), 2) == 1, "button_4 is the second added box");
    expect(cc::added_box_of(cc::button_hash(5), 2) == -1, "a button past the added boxes is none");
    expect(cc::added_box_of(cc::BUTTON_1, 2) == -1 && cc::added_box_of(cc::BACK, 2) == -1,
           "stock buttons and back are not added boxes");
    expect(cc::added_box_of(cc::button_hash(3), 0) == -1, "no added boxes, no match");

    const uint8_t key[4] = {0x17, 0x2d, 0x57, 0x5a};
    std::string plain = "cs_f1_2016";
    std::vector<uint8_t> enc(plain.size() + 1);
    for (size_t i = 0; i < plain.size(); ++i)
        enc[i] = uint8_t(plain[i]) ^ key[(i - 1) & 3];
    expect(cc::decode_name(enc.data(), enc.size(), key) == plain, "a value name decodes with the key");
    expect(cc::decode_name(enc.data(), 1, key).empty(), "a length below 2 decodes to nothing");
    expect(cc::decode_name(enc.data(), cc::VALUE_NAME_MAX + 1, key).empty(), "an oversized length decodes to nothing");
    expect(cc::decode_name(nullptr, 5, key).empty(), "no source decodes to nothing");

    std::vector<cc::Value> values = {{"f1_generic", "multiplayer_car_25"}, {"f1_generic_2026", "multiplayer_car_26"},
                                     {"cs_f1_2016", "lng_cs_f1_2016"}};
    expect(cc::value_of(values, "cs_f1_2016") == 2, "a box finds its category value by game name");
    expect(cc::value_of(values, "cs_f1_2017") == -1, "a box without a value finds none");
    std::vector<cc::Scenario> scenarios = {{0, "f1"}, {5, "f1_generic_2026"}, {15, "cs_f1_2016"}};
    expect(cc::scenario_of(scenarios, "cs_f1_2016") == 15, "a category value finds its scenario by name");
    expect(cc::scenario_of(scenarios, "cs_f1_2017") == -1, "a value without a scenario finds none");
    expect(cc::decides_scenario(0) && cc::decides_scenario(1), "offline and the host decide the scenario");
    expect(!cc::decides_scenario(2) && !cc::decides_scenario(-1), "a client takes the host's scenario");

    Node node;
    era::Handle a{}, b{}, c{};
    era::link(a, node.mem);
    era::link(b, node.mem);
    era::link(c, node.mem);
    expect(node.holders() == &c && node.refs() == 3, "three handles linked, newest first");
    int released = 0;
    auto release = [&](uint8_t *n) {
        expect(n == node.mem, "release gets the handle's node");
        --node.refs();
        ++released;
    };
    expect(cc::unlink(b, release), "the middle handle unlinks");
    expect(c.next == &a && a.prev == &c && node.holders() == &c, "its neighbours join");
    expect(cc::unlink(c, release), "the head unlinks");
    expect(node.holders() == &a && a.prev == nullptr, "the head moves to the next handle");
    expect(cc::unlink(a, release), "the last handle unlinks");
    expect(node.holders() == nullptr && node.refs() == 0 && released == 3, "the list is empty and every reference dropped");
    expect(!cc::unlink(a, release) && released == 3, "a cleared handle is left alone");

    printf("%s\n", fails ? "car category logic: FAIL" : "car category logic: ok");
    return fails ? 1 : 0;
}
