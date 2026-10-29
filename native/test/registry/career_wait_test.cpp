#include "../../src/core/registry/table.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>

using namespace swofty_core::registry;
using namespace std::chrono_literals;

namespace {

std::atomic<std::thread::id> flow_id;

bool on_flow() { return std::this_thread::get_id() == flow_id.load(); }

int edit_reads_other(void *, SwoftyText in, char *out, size_t cap, size_t *len)
{
    Record other = store().get("test.other", "game/o");
    Value v = swofty::json::parse(std::string_view(in.data, in.size));
    v["n"] = Value(other.value["n"].as_int() + 1);
    std::string text = swofty::json::dump(v);
    *len = text.size();
    if (cap < text.size())
        return SWOFTY_TOO_SMALL;
    std::memcpy(out, text.data(), text.size());
    return SWOFTY_ASSET_OK;
}

template <class F> bool finishes(F f, std::chrono::milliseconds limit)
{
    auto done = std::make_shared<std::atomic<bool>>(false);
    std::thread([f, done]() mutable {
        f();
        *done = true;
    }).detach();
    for (auto t = std::chrono::steady_clock::now(); std::chrono::steady_clock::now() - t < limit;) {
        if (*done)
            return true;
        std::this_thread::sleep_for(5ms);
    }
    return false;
}

}

int main()
{
    Store &st = store();
    st.declare("test.main", swofty::json::parse(R"({"x-registry":"test.main","type":"object"})"));
    st.declare("test.other", swofty::json::parse(R"({"x-registry":"test.other","type":"object"})"));
    st.add_base("test.main", "m", swofty::json::parse(R"({"n":0})"));
    st.add_base("test.other", "o", swofty::json::parse(R"({"n":41})"));
    LayerSpec sp;
    sp.addon = "a.reader";
    sp.registry = "test.main";
    sp.id = "game/m";
    sp.change.is_patch = false;
    sp.change.fn = edit_reads_other;
    st.layer(sp, nullptr);
    st.set_flow_thread(on_flow);
    st.end_register();
    int failures = 0;

    st.begin_career_load();
    std::atomic<bool> listed{false};
    std::thread flow([&] {
        flow_id = std::this_thread::get_id();
        char id[256];
        listed = table()->first("test.main", "", id, sizeof id) == SWOFTY_ASSET_OK;
    });
    std::this_thread::sleep_for(200ms);
    bool ended = finishes([&] { st.end_career_load(); }, 3000ms);
    if (!ended || !finishes([&] { flow.join(); }, 3000ms)) {
        std::printf("FAIL: listing on the flow thread during a career load, with an edit that reads another registry, "
                    "deadlocks the store\n");
        std::fflush(stdout);
        std::_Exit(1);
    }
    long long got = store().get("test.main", "game/m").value["n"].as_int(-1);
    if (!listed || got != 42) {
        std::printf("FAIL: the listing gave %d and the nested read %lld, want 1 and 42\n", int(listed.load()), got);
        failures++;
    }

    st.begin_career_load();
    std::atomic<bool> done{false};
    std::thread waiter([&] {
        flow_id = std::this_thread::get_id();
        store().get("test.other", "game/o");
        done = true;
    });
    std::this_thread::sleep_for(150ms);
    if (done) {
        std::printf("FAIL: a flow-thread read during a career load did not wait for the load\n");
        failures++;
    }
    Record off = store().get("test.other", "game/o");
    if (off.status != Status::ok) {
        std::printf("FAIL: a read on another thread during a career load did not answer\n");
        failures++;
    }
    st.end_career_load();
    if (!finishes([&] { waiter.join(); }, 3000ms)) {
        std::printf("FAIL: the waiting flow-thread read never woke after the career load ended\n");
        std::fflush(stdout);
        std::_Exit(1);
    }
    if (!done) {
        std::printf("FAIL: the flow-thread read did not finish after the load\n");
        failures++;
    }
    if (!failures)
        std::printf("ok: the career-load wait holds top-level flow reads only and never deadlocks a nested read\n");
    return failures ? 1 : 0;
}
