#pragma once
#include <coroutine>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

// Tests in code (docs/std/testing.md, "Tests in code"). A test DLL holds SWOFTY_TEST functions, which
// are C++20 coroutines: the core's test agent loads the DLL only during `swofty test`, starts a test
// when the CLI asks for it, and steps it once per game frame on the flow thread, so a test that waits
// never blocks the game.
//
//     #include <swofty/test.hpp>
//
//     SWOFTY_TEST(rnd_tab_hidden)(swofty::test::Context &t) -> swofty::test::Task
//     {
//         co_await t.at("team_hq", 60);
//         t.expect(!t.read<bool>("ui.team_hq.tabs.rnd.visible"), "the R&D tab is hidden");
//         co_await t.route("laptop_rnd");
//         co_await t.log("\\[rnd-tyres\\] blocked", 5);
//         co_await t.capture("team_hq");
//     }
//
//     SWOFTY_TESTS()
//
// Every co_await is an agent command (at, route, log, wait, capture, pad, skip...) and returns
// whether it succeeded; a command that fails or times out fails the test. t.expect records a check
// and lets the test go on. The agent answers the CLI with every expectation.

extern "C" {

struct SwoftyTestHost {
    uint32_t size, version;
    void *self;
    // Start an agent command, {"cmd": ..., "args": {...}, "timeout_ms": ...}; returns its op id, 0 when
    // the line is refused.
    uint64_t (*start)(void *self, const char *command_json);
    // 0 while the op runs; 1 when it succeeded, 2 when it failed. out gets the result line.
    int (*poll)(void *self, uint64_t op, char *out, size_t cap);
    void (*expect)(void *self, int ok, const char *what);
    void (*log)(void *self, const char *text);
    // A value now: 1 with the value's JSON in out, 0 with the reason in out.
    int (*read)(void *self, const char *path, char *out, size_t cap);
};

#define SWOFTY_TEST_HOST_VERSION 1u

}

#if defined(_WIN32)
#define SWOFTY_TEST_EXPORT extern "C" __declspec(dllexport)
#else
#define SWOFTY_TEST_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace swofty::test {

class Context;

struct Task {
    struct promise_type {
        bool faulted = false;
        Task get_return_object() { return Task{std::coroutine_handle<promise_type>::from_promise(*this)}; }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { faulted = true; }
    };

    std::coroutine_handle<promise_type> h;

    explicit Task(std::coroutine_handle<promise_type> handle) : h(handle) {}
    Task(Task &&o) noexcept : h(o.h) { o.h = {}; }
    Task(const Task &) = delete;
    Task &operator=(const Task &) = delete;
    ~Task()
    {
        if (h)
            h.destroy();
    }
};

namespace detail {

inline std::string quote(const char *s)
{
    std::string o = "\"";
    for (; s && *s; ++s) {
        unsigned char c = static_cast<unsigned char>(*s);
        if (c == '"' || c == '\\')
            o += '\\', o += char(c);
        else if (c < 0x20) {
            char b[8];
            std::snprintf(b, sizeof b, "\\u%04x", c);
            o += b;
        } else
            o += char(c);
    }
    return o + "\"";
}

// The value of {"value": X, ...} or a bare X.
inline const char *value_of(const char *json)
{
    const char *v = std::strstr(json, "\"value\":");
    return v ? v + 8 : json;
}

using Fn = Task (*)(Context &);

struct Entry {
    const char *name;
    Fn fn;
};

inline std::vector<Entry> &registry()
{
    static std::vector<Entry> r;
    return r;
}

struct Reg {
    Reg(const char *name, Fn fn) { registry().push_back({name, fn}); }
};

}

class Context {
  public:
    explicit Context(const SwoftyTestHost *host) : host_(host) {}

    struct Op {
        Context &t;
        std::string line;

        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<>) { t.begin(line); }
        bool await_resume() const noexcept { return t.last_ok_; }
    };

    // Any agent command: t.command("skip", R"({"to":"session"})", 30).
    Op command(const char *cmd, const char *args_json = "null", double timeout_s = 0)
    {
        std::string l = std::string("{\"cmd\":") + detail::quote(cmd) + ",\"args\":" + args_json;
        if (timeout_s > 0)
            l += ",\"timeout_ms\":" + std::to_string(static_cast<long long>(timeout_s * 1000));
        return Op{*this, l + "}"};
    }

    // Until the screen or flow state is `phase` (fe_ may be left off a screen name).
    Op at(const char *phase, double timeout_s = 30)
    {
        return command("at", ("{\"phase\":" + detail::quote(phase) + "}").c_str(), timeout_s);
    }
    Op route(const char *name) { return command("route", ("{\"route\":" + detail::quote(name) + "}").c_str()); }
    // Until a loader.log line written from now on matches the regular expression.
    Op log(const char *pattern, double timeout_s = 30)
    {
        return command("log", ("{\"pattern\":" + detail::quote(pattern) + "}").c_str(), timeout_s);
    }
    Op wait(const char *event, double timeout_s = 30)
    {
        return command("wait", ("{\"event\":" + detail::quote(event) + "}").c_str(), timeout_s);
    }
    Op capture(const char *name) { return command("capture", ("{\"name\":" + detail::quote(name) + "}").c_str()); }
    Op pad(const char *button, int ms = 120)
    {
        return command("pad", ("{\"press\":" + detail::quote(button) + ",\"ms\":" + std::to_string(ms) + "}").c_str());
    }

    void expect(bool ok, const char *what)
    {
        if (host_ && host_->expect)
            host_->expect(host_->self, ok ? 1 : 0, what);
        failed_ |= !ok;
    }

    void log_text(const char *text)
    {
        if (host_ && host_->log)
            host_->log(host_->self, text);
    }

    // A value now: bool, a number or std::string. A failed read is a failed expectation and reads
    // as T{}.
    template <class T> T read(const char *path)
    {
        char out[1024] = {};
        if (!host_ || !host_->read || !host_->read(host_->self, path, out, sizeof out)) {
            expect(false, (std::string("read ") + path + ": " + out).c_str());
            return T{};
        }
        const char *v = detail::value_of(out);
        if constexpr (std::is_same_v<T, bool>)
            return std::strncmp(v, "true", 4) == 0 || (*v >= '1' && *v <= '9');
        else if constexpr (std::is_arithmetic_v<T>)
            return static_cast<T>(std::strtod(v, nullptr));
        else {
            std::string s = v;
            if (!s.empty() && s[0] == '"') {
                size_t end = s.find('"', 1);
                return s.substr(1, end == std::string::npos ? std::string::npos : end - 1);
            }
            return s;
        }
    }

    bool failed() const { return failed_; }

    // The runner's side: the op the test waits on, and whether it is done.
    bool step()
    {
        if (!op_)
            return true;
        char out[4096] = {};
        int r = host_->poll(host_->self, op_, out, sizeof out);
        if (r == 0)
            return false;
        last_ok_ = r == 1;
        if (!last_ok_) {
            failed_ = true;
            expect(false, (op_line_ + " failed: " + out).c_str());
        }
        op_ = 0;
        return true;
    }

  private:
    const SwoftyTestHost *host_;
    uint64_t op_ = 0;
    std::string op_line_;
    bool last_ok_ = false, failed_ = false;

    void begin(const std::string &line)
    {
        op_line_ = line;
        op_ = host_ && host_->start ? host_->start(host_->self, line.c_str()) : 0;
        if (!op_) {
            last_ok_ = false;
            failed_ = true;
            expect(false, (line + " was refused").c_str());
        }
    }
};

namespace detail {

struct Run {
    Context ctx;
    Task task;

    Run(const SwoftyTestHost *host, Fn fn) : ctx(host), task(fn(ctx)) {}
};

}

}

#define SWOFTY_TEST(name)                                                                                              \
    static auto swofty_test_fn_##name(swofty::test::Context &)->swofty::test::Task;                                    \
    static const swofty::test::detail::Reg swofty_test_reg_##name{#name, &swofty_test_fn_##name};                      \
    static auto swofty_test_fn_##name

// Once per test DLL: the exports the agent runs tests through.
#define SWOFTY_TESTS()                                                                                                 \
    SWOFTY_TEST_EXPORT const char *SwoftyTestNames()                                                     \
    {                                                                                                                  \
        static std::string names;                                                                                      \
        names.clear();                                                                                                 \
        for (auto &e : swofty::test::detail::registry())                                                               \
            names += std::string(e.name) + "\n";                                                                       \
        return names.c_str();                                                                                          \
    }                                                                                                                  \
    SWOFTY_TEST_EXPORT void *SwoftyTestStart(const char *name, const SwoftyTestHost *host)               \
    {                                                                                                                  \
        for (auto &e : swofty::test::detail::registry())                                                               \
            if (!std::strcmp(e.name, name))                                                                            \
                return new swofty::test::detail::Run(host, e.fn);                                                      \
        return nullptr;                                                                                                \
    }                                                                                                                  \
    SWOFTY_TEST_EXPORT int SwoftyTestStep(void *p)                                                       \
    {                                                                                                                  \
        auto *r = static_cast<swofty::test::detail::Run *>(p);                                                         \
        if (!r->ctx.step())                                                                                            \
            return 0;                                                                                                  \
        if (!r->task.h.done())                                                                                         \
            r->task.h.resume();                                                                                        \
        if (!r->task.h.done())                                                                                         \
            return 0;                                                                                                  \
        return r->task.h.promise().faulted ? 3 : r->ctx.failed() ? 2 : 1;                                              \
    }                                                                                                                  \
    SWOFTY_TEST_EXPORT void SwoftyTestEnd(void *p) { delete static_cast<swofty::test::detail::Run *>(p); }
