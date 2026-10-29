// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
#include <swofty/json.hpp>
#include <swofty/mixin.hpp>
#include <swofty/selector.hpp>

#if defined(__clang__) && __has_warning("-Wmissing-designated-field-initializers")
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

// Record values (standard library 1.6): the value types records are made of, SWOFTY_RECORD, Keyed lists
// and their JSON form. Records cross the C ABI as JSON text; this header converts a SWOFTY_RECORD struct
// to and from that text. Fields the struct doesn't list are kept in its `extra` member when it has one
// (json::Value), so a record from a newer minor round-trips unchanged.

#ifndef SWOFTY_GUARDED_ENTRY
#define SWOFTY_GUARDED_ENTRY 1
#include <cstring>
#include <exception>
#if defined(_WIN32)
extern "C" __declspec(dllimport) void __stdcall RaiseException(unsigned long code, unsigned long flags, unsigned long count,
                                                               const unsigned long long *info);
#endif

// A C++ exception never unwinds out of an addon callback into the core: the core's handler guard
// (SEH) can't unwind another runtime's C++ frames. Every SDK entry the core calls runs its body here;
// a throw is caught in the addon's own runtime and, once the catch block has ended, raised again as
// the SEH exception handler_threw_code, carrying what(), which the core's guard treats like any other
// fault of that handler.
namespace swofty::detail {

inline constexpr unsigned long handler_threw_code = 0xE0535746ul;

template <class F> int guarded_entry(F &&body)
{
#if defined(_WIN32)
    static thread_local char what[256];
    bool threw = false;
    int r = 0;
    try {
        r = body();
    } catch (const std::exception &e) {
        threw = true;
        strncpy(what, e.what() ? e.what() : "", sizeof what - 1);
        what[sizeof what - 1] = 0;
    } catch (...) {
        threw = true;
        strcpy(what, "an exception that is not a std::exception");
    }
    if (threw) {
        unsigned long long info = reinterpret_cast<unsigned long long>(what);
        RaiseException(handler_threw_code, 1, 1, &info);
    }
    return r;
#else
    return body();
#endif
}

}
#endif

namespace swofty {

enum class Result { ok, late, unavailable, invalid, too_small };

inline const char *result_name(Result r)
{
    switch (r) {
    case Result::ok:
        return "ok";
    case Result::late:
        return "late";
    case Result::unavailable:
        return "unavailable";
    case Result::too_small:
        return "too_small";
    default:
        return "invalid";
    }
}

enum class Applies { boot, content, session, read };
enum class Scope { global, profile, career };
enum class Status { ok, no_record, no_registry, invalid, late };

struct Version {
    uint32_t major = 1, minor = 0;
};

struct StatId {
    static constexpr int64_t automatic_value = -1;
    int64_t value = automatic_value;
    static const StatId automatic;
    constexpr StatId() = default;
    constexpr StatId(int64_t v) : value(v) {}
    constexpr bool is_automatic() const { return value < 0; }
    friend constexpr bool operator==(StatId a, StatId b) { return a.value == b.value; }
};
inline constexpr StatId StatId::automatic{};

class Text {
public:
    Text() = default;
    Text(const char *lang_key) : value_(lang_key ? lang_key : "") {}
    Text(std::string_view lang_key) : value_(lang_key) {}
    Text(const std::string &lang_key) : value_(lang_key) {}
    static Text key(std::string_view lang_key) { return Text(lang_key); }
    static Text literal(std::string_view text)
    {
        Text t(text);
        t.literal_ = true;
        return t;
    }
    bool is_literal() const { return literal_; }
    const std::string &str() const { return value_; }
    bool empty() const { return value_.empty(); }
    friend bool operator==(const Text &a, const Text &b) { return a.literal_ == b.literal_ && a.value_ == b.value_; }

private:
    std::string value_;
    bool literal_ = false;
};

class Colour {
public:
    Colour() = default;
    Colour(uint32_t argb) : argb_(argb) {}
    Colour(std::string_view hex)
    {
        if (!hex.empty() && hex[0] == '#')
            hex.remove_prefix(1);
        uint32_t v = static_cast<uint32_t>(std::strtoul(std::string(hex).c_str(), nullptr, 16));
        argb_ = hex.size() <= 6 ? 0xff000000u | v : v;
    }
    Colour(const char *hex) : Colour(std::string_view(hex)) {}
    uint32_t argb() const { return argb_; }
    std::string hex() const
    {
        char buf[10];
        if ((argb_ >> 24) == 0xff)
            std::snprintf(buf, sizeof buf, "#%06X", argb_ & 0xffffffu);
        else
            std::snprintf(buf, sizeof buf, "#%08X", argb_);
        return buf;
    }
    friend bool operator==(Colour a, Colour b) { return a.argb_ == b.argb_; }

private:
    uint32_t argb_ = 0xff000000u;
};

class Path {
public:
    enum class Kind { none, file, stock, archive };
    Path() = default;
    static Path file(std::string_view path) { return Path(Kind::file, std::string(path)); }
    static Path stock(std::string_view game_path) { return Path(Kind::stock, std::string(game_path)); }
    static Path archive(std::string json) { return Path(Kind::archive, std::move(json)); }
    Kind kind() const { return kind_; }
    const std::string &str() const { return value_; }
    bool empty() const { return kind_ == Kind::none; }
    friend bool operator==(const Path &a, const Path &b) { return a.kind_ == b.kind_ && a.value_ == b.value_; }

private:
    Path(Kind k, std::string v) : kind_(k), value_(std::move(v)) {}
    Kind kind_ = Kind::none;
    std::string value_;
};

inline Path file(std::string_view path) { return Path::file(path); }
inline Path stock(std::string_view game_path) { return Path::stock(game_path); }

struct Curve {
    std::vector<std::array<float, 2>> points;
    static Curve flat(float from, float to, float value) { return Curve{{{from, value}, {to, value}}}; }
    Curve &scale_y(float f)
    {
        for (auto &p : points)
            p[1] *= f;
        return *this;
    }
    Curve &add_y(float f)
    {
        for (auto &p : points)
            p[1] += f;
        return *this;
    }
    friend bool operator==(const Curve &, const Curve &) = default;
};

class Errors {
public:
    void add(std::string_view path, std::string_view message) { list_.push_back({std::string(path), std::string(message)}); }
    bool empty() const { return list_.empty(); }
    const std::vector<std::pair<std::string, std::string>> &all() const { return list_; }

private:
    std::vector<std::pair<std::string, std::string>> list_;
};

inline bool is_key(std::string_view s)
{
    if (s.empty())
        return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
            return false;
    return true;
}

class Token {
public:
    Token() = default;
    Token(uint32_t id, int (*release)(uint32_t)) : id_(id), release_(release) {}
    Token(const Token &) = delete;
    Token &operator=(const Token &) = delete;
    Token(Token &&o) noexcept : id_(std::exchange(o.id_, 0)), release_(o.release_) {}
    Token &operator=(Token &&o) noexcept
    {
        if (this != &o) {
            reset();
            id_ = std::exchange(o.id_, 0);
            release_ = o.release_;
        }
        return *this;
    }
    ~Token() { reset(); }
    void reset()
    {
        if (id_ && release_)
            release_(id_);
        id_ = 0;
    }
    uint32_t id() const { return id_; }
    explicit operator bool() const { return id_ != 0; }

private:
    uint32_t id_ = 0;
    int (*release_)(uint32_t) = nullptr;
};

template <class T> class Ref {
public:
    Ref() = default;
    explicit Ref(std::string_view id) : id_(id) {}
    std::string_view id() const { return id_; }
    bool empty() const { return id_.empty(); }
    Ref optional() const
    {
        Ref r = *this;
        if (!r.id_.empty() && r.id_.back() != '?')
            r.id_ += '?';
        return r;
    }
    Ref or_else(Ref other) const
    {
        Ref r = *this;
        if (!other.id_.empty())
            r.id_ += (r.id_.empty() ? "" : "|") + other.id_;
        return r;
    }
    bool resolved() const;
    friend bool operator==(const Ref &a, const Ref &b) { return a.id_ == b.id_; }
    friend bool operator<(const Ref &a, const Ref &b) { return a.id_ < b.id_; }

private:
    std::string id_;
};

template <class T, auto... Keys> class Keyed {
    static_assert(sizeof...(Keys) > 0, "Keyed needs at least one key field");

public:
    using value_type = T;
    Keyed() = default;
    Keyed(std::initializer_list<T> items) : items_(items) {}
    Keyed(std::vector<T> items) : items_(std::move(items)) {}

    template <class... K> T *find(const K &...key)
    {
        static_assert(sizeof...(K) == sizeof...(Keys), "pass one value per key field");
        for (auto &e : items_)
            if (matches(e, key...))
                return &e;
        return nullptr;
    }
    template <class... K> const T *find(const K &...key) const { return const_cast<Keyed *>(this)->find(key...); }
    template <class... K> T &at(const K &...key)
    {
        if (T *e = find(key...))
            return *e;
        T e{};
        assign(e, key...);
        items_.push_back(std::move(e));
        return items_.back();
    }
    template <class... K> bool erase(const K &...key)
    {
        for (auto it = items_.begin(); it != items_.end(); ++it)
            if (matches(*it, key...)) {
                items_.erase(it);
                return true;
            }
        return false;
    }

    auto begin() { return items_.begin(); }
    auto end() { return items_.end(); }
    auto begin() const { return items_.begin(); }
    auto end() const { return items_.end(); }
    size_t size() const { return items_.size(); }
    bool empty() const { return items_.empty(); }
    void clear() { items_.clear(); }
    void push_back(T e) { items_.push_back(std::move(e)); }
    std::vector<T> &items() { return items_; }
    const std::vector<T> &items() const { return items_; }

private:
    template <class... K> static bool matches(const T &e, const K &...key)
    {
        return ((e.*Keys == key) && ...);
    }
    template <class... K> static void assign(T &e, const K &...key) { ((e.*Keys = key), ...); }
    std::vector<T> items_;
};

namespace detail {

template <class C, class M> struct field_t {
    const char *name;
    M C::*member;
};
struct field_end {};

template <class C, class M> constexpr field_t<C, M> field(const char *name, M C::*member)
{
    return {name[0] == 'c' && name[1] == 'l' && name[2] == 's' && !name[3] ? "class" : name, member};
}

template <class T> using record_fields_t = decltype(swofty_fields(static_cast<const T *>(nullptr)));

template <class T>
concept record = requires { swofty_fields(static_cast<const T *>(nullptr)); };

template <class T>
concept has_extra = requires(T t) { t.extra; } && std::is_same_v<std::remove_cvref_t<decltype(std::declval<T &>().extra)>, json::Value>;

template <class T> constexpr const char *registry_name()
{
    if constexpr (requires { swofty_registry_name(static_cast<const T *>(nullptr)); })
        return swofty_registry_name(static_cast<const T *>(nullptr));
    else
        return "";
}

template <class F> void each_field(F &&f, const auto &tuple)
{
    std::apply([&](const auto &...fs) { (f(fs), ...); }, tuple);
}

}

template <class T> using Edit = std::function<void(T &)>;

}

#define SWOFTY_PARENS_ ()
#define SWOFTY_EXPAND_(...) SWOFTY_EXPAND4_(SWOFTY_EXPAND4_(SWOFTY_EXPAND4_(SWOFTY_EXPAND4_(__VA_ARGS__))))
#define SWOFTY_EXPAND4_(...) SWOFTY_EXPAND3_(SWOFTY_EXPAND3_(SWOFTY_EXPAND3_(SWOFTY_EXPAND3_(__VA_ARGS__))))
#define SWOFTY_EXPAND3_(...) SWOFTY_EXPAND2_(SWOFTY_EXPAND2_(SWOFTY_EXPAND2_(SWOFTY_EXPAND2_(__VA_ARGS__))))
#define SWOFTY_EXPAND2_(...) SWOFTY_EXPAND1_(SWOFTY_EXPAND1_(SWOFTY_EXPAND1_(SWOFTY_EXPAND1_(__VA_ARGS__))))
#define SWOFTY_EXPAND1_(...) __VA_ARGS__
#define SWOFTY_FOR_EACH_(macro, ...) __VA_OPT__(SWOFTY_EXPAND_(SWOFTY_FOR_EACH_STEP_(macro, __VA_ARGS__)))
#define SWOFTY_FOR_EACH_STEP_(macro, a, ...) macro(a) __VA_OPT__(SWOFTY_FOR_EACH_AGAIN_ SWOFTY_PARENS_(macro, __VA_ARGS__))
#define SWOFTY_FOR_EACH_AGAIN_() SWOFTY_FOR_EACH_STEP_
#define SWOFTY_RECORD_FIELD_(f) ::swofty::detail::field(#f, &swofty_self::f),

// SWOFTY_RECORD(Type, field, ...): the record's fields, in the order data files, origins and the schema
// use. Put it at namespace scope, in the struct's namespace, after the struct.
#define SWOFTY_RECORD(T, ...)                                                                                          \
    [[maybe_unused]] inline auto swofty_fields(const T *)                                                              \
    {                                                                                                                  \
        using swofty_self [[maybe_unused]] = T;                                                                        \
        return std::make_tuple(SWOFTY_FOR_EACH_(SWOFTY_RECORD_FIELD_, __VA_ARGS__)::swofty::detail::field_end{});      \
    }

// SWOFTY_RECORD_REGISTRY(Type, "name"): the registry a record type belongs to (for x-ref and
// events::Context::record<T>()). It works on a forward-declared type.
#define SWOFTY_RECORD_REGISTRY(T, name)                                                                                \
    [[maybe_unused]] constexpr const char *swofty_registry_name(const T *) { return name; }

#define SWOFTY_ENUM_NAME_(e) #e,
// SWOFTY_ENUM(Type, value, ...): enumerator names for the JSON form, in declaration order from 0.
#define SWOFTY_ENUM(T, ...)                                                                                            \
    [[maybe_unused]] inline std::string_view swofty_enum_name(T v)                                                     \
    {                                                                                                                  \
        static constexpr const char *names[] = {SWOFTY_FOR_EACH_(SWOFTY_ENUM_NAME_, __VA_ARGS__)};                     \
        size_t i = static_cast<size_t>(v);                                                                             \
        return i < sizeof names / sizeof *names ? names[i] : "";                                                       \
    }                                                                                                                  \
    [[maybe_unused]] inline bool swofty_enum_parse(std::string_view s, T *out)                                         \
    {                                                                                                                  \
        static constexpr const char *names[] = {SWOFTY_FOR_EACH_(SWOFTY_ENUM_NAME_, __VA_ARGS__)};                     \
        for (size_t i = 0; i < sizeof names / sizeof *names; i++)                                                      \
            if (s == names[i]) {                                                                                       \
                *out = static_cast<T>(i);                                                                              \
                return true;                                                                                           \
            }                                                                                                          \
        return false;                                                                                                  \
    }

namespace swofty {

SWOFTY_ENUM(Applies, boot, content, session, read)
SWOFTY_ENUM(Scope, global, profile, career)

namespace detail {

class Writer {
public:
    std::string out;
    void raw(std::string_view s) { out += s; }
    void str(std::string_view s) { out += events::detail::quote(s); }
    void sep()
    {
        if (!out.empty() && out.back() != '{' && out.back() != '[' && out.back() != ':')
            out += ',';
    }
    void key(std::string_view k)
    {
        sep();
        str(k);
        out += ':';
    }
    template <class F>
        requires std::is_floating_point_v<F>
    void number(F v)
    {
        if (!std::isfinite(v)) {
            out += "null";
            return;
        }
        char buf[64];
        auto r = std::to_chars(buf, buf + sizeof buf, v);
        out.append(buf, r.ptr);
    }
    void integer(int64_t v) { out += std::to_string(v); }
};

struct Node {
    enum Kind { null, boolean, number, string, array, object } kind = null;
    bool b = false;
    double num = 0;
    std::string s;
    std::string_view text;
    std::vector<Node> items;
    std::vector<std::pair<std::string, Node>> members;
    const Node *get(std::string_view k) const
    {
        for (const auto &[name, v] : members)
            if (name == k)
                return &v;
        return nullptr;
    }
};

class Reader {
public:
    explicit Reader(std::string_view text) : t_(text) {}
    bool parse(Node &out)
    {
        ws();
        if (!value(out))
            return false;
        ws();
        return i_ == t_.size();
    }

private:
    void ws()
    {
        while (i_ < t_.size() && (t_[i_] == ' ' || t_[i_] == '\n' || t_[i_] == '\r' || t_[i_] == '\t'))
            i_++;
    }
    bool lit(std::string_view w)
    {
        if (t_.substr(i_, w.size()) != w)
            return false;
        i_ += w.size();
        return true;
    }
    bool str(std::string &out)
    {
        if (i_ >= t_.size() || t_[i_] != '"')
            return false;
        i_++;
        while (i_ < t_.size() && t_[i_] != '"') {
            char c = t_[i_++];
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i_ >= t_.size())
                return false;
            char e = t_[i_++];
            switch (e) {
            case 'n':
                out += '\n';
                break;
            case 't':
                out += '\t';
                break;
            case 'r':
                out += '\r';
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'u': {
                if (i_ + 4 > t_.size())
                    return false;
                uint32_t cp = static_cast<uint32_t>(std::strtoul(std::string(t_.substr(i_, 4)).c_str(), nullptr, 16));
                i_ += 4;
                if (cp >= 0xd800 && cp < 0xdc00 && t_.substr(i_, 2) == "\\u" && i_ + 6 <= t_.size()) {
                    uint32_t lo = static_cast<uint32_t>(std::strtoul(std::string(t_.substr(i_ + 2, 4)).c_str(), nullptr, 16));
                    i_ += 6;
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
                }
                if (cp < 0x80)
                    out += static_cast<char>(cp);
                else if (cp < 0x800)
                    out += static_cast<char>(0xc0 | cp >> 6), out += static_cast<char>(0x80 | (cp & 0x3f));
                else if (cp < 0x10000)
                    out += static_cast<char>(0xe0 | cp >> 12), out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f)),
                        out += static_cast<char>(0x80 | (cp & 0x3f));
                else
                    out += static_cast<char>(0xf0 | cp >> 18), out += static_cast<char>(0x80 | ((cp >> 12) & 0x3f)),
                        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f)), out += static_cast<char>(0x80 | (cp & 0x3f));
                break;
            }
            default:
                out += e;
            }
        }
        if (i_ >= t_.size())
            return false;
        i_++;
        return true;
    }
    bool value(Node &n)
    {
        if (++depth_ > 256)
            return false;
        size_t start = i_;
        bool ok = true;
        if (i_ >= t_.size())
            ok = false;
        else if (t_[i_] == '{') {
            n.kind = Node::object;
            i_++;
            ws();
            if (i_ < t_.size() && t_[i_] == '}')
                i_++;
            else
                for (;;) {
                    ws();
                    std::string k;
                    Node v;
                    ws();
                    if (!str(k))
                        return false;
                    ws();
                    if (!lit(":"))
                        return false;
                    ws();
                    if (!value(v))
                        return false;
                    n.members.emplace_back(std::move(k), std::move(v));
                    ws();
                    if (lit(","))
                        continue;
                    if (!lit("}"))
                        return false;
                    break;
                }
        } else if (t_[i_] == '[') {
            n.kind = Node::array;
            i_++;
            ws();
            if (i_ < t_.size() && t_[i_] == ']')
                i_++;
            else
                for (;;) {
                    ws();
                    Node v;
                    if (!value(v))
                        return false;
                    n.items.push_back(std::move(v));
                    ws();
                    if (lit(","))
                        continue;
                    if (!lit("]"))
                        return false;
                    break;
                }
        } else if (t_[i_] == '"') {
            n.kind = Node::string;
            ok = str(n.s);
        } else if (lit("true"))
            n.kind = Node::boolean, n.b = true;
        else if (lit("false"))
            n.kind = Node::boolean;
        else if (lit("null"))
            n.kind = Node::null;
        else {
            char *end = nullptr;
            std::string num(t_.substr(i_, 40));
            n.num = std::strtod(num.c_str(), &end);
            if (end == num.c_str())
                return false;
            n.kind = Node::number;
            i_ += static_cast<size_t>(end - num.c_str());
        }
        n.text = t_.substr(start, i_ - start);
        depth_--;
        return ok;
    }
    std::string_view t_;
    size_t i_ = 0;
    int depth_ = 0;
};

inline Result result(int r)
{
    switch (r) {
    case SWOFTY_ASSET_OK:
        return Result::ok;
    case SWOFTY_ASSET_LATE:
        return Result::late;
    case SWOFTY_ASSET_UNAVAILABLE:
        return Result::unavailable;
    case SWOFTY_TOO_SMALL:
        return Result::too_small;
    default:
        return Result::invalid;
    }
}

inline SwoftyText text(std::string_view s) { return SwoftyText{s.data(), s.size()}; }

template <class F> int fill(std::string &out, F &&call)
{
    out.assign(1024, '\0');
    for (int tries = 0; tries < 4; tries++) {
        size_t len = 0;
        int r = call(out.data(), out.size(), &len);
        if (r == SWOFTY_TOO_SMALL && len > out.size()) {
            out.assign(len + 1, '\0');
            continue;
        }
        out.resize(r == SWOFTY_ASSET_OK ? std::min(len, out.size()) : 0);
        return r;
    }
    out.clear();
    return SWOFTY_TOO_SMALL;
}

inline int put(const std::string &value, char *out, size_t cap, size_t *len)
{
    *len = value.size();
    if (value.size() > cap)
        return SWOFTY_TOO_SMALL;
    std::memcpy(out, value.data(), value.size());
    return SWOFTY_ASSET_OK;
}

inline std::string cstr(const char *buf, size_t cap) { return std::string(buf, strnlen(buf, cap)); }

template <class T> struct Json;

template <class T> void write(Writer &w, const T &v) { Json<T>::write(w, v); }
template <class T> bool read(const Node &n, T &v) { return Json<T>::read(n, v); }

template <> struct Json<bool> {
    static void write(Writer &w, bool v) { w.raw(v ? "true" : "false"); }
    static bool read(const Node &n, bool &v)
    {
        if (n.kind != Node::boolean)
            return false;
        v = n.b;
        return true;
    }
};

template <> struct Json<char> {
    static void write(Writer &w, char v) { w.str(std::string(1, v)); }
    static bool read(const Node &n, char &v)
    {
        if (n.kind != Node::string || n.s.size() != 1)
            return false;
        v = n.s[0];
        return true;
    }
};

template <class T>
    requires(std::is_arithmetic_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char>)
struct Json<T> {
    static void write(Writer &w, T v)
    {
        if constexpr (std::is_integral_v<T>)
            w.integer(static_cast<int64_t>(v));
        else
            w.number(v);
    }
    static bool read(const Node &n, T &v)
    {
        if (n.kind != Node::number)
            return false;
        v = static_cast<T>(n.num);
        return true;
    }
};

template <class T>
    requires std::is_enum_v<T>
struct Json<T> {
    static void write(Writer &w, T v)
    {
        if constexpr (requires { swofty_enum_name(v); })
            w.str(swofty_enum_name(v));
        else
            w.integer(static_cast<int64_t>(v));
    }
    static bool read(const Node &n, T &v)
    {
        if constexpr (requires { swofty_enum_parse(std::string_view{}, &v); })
            return n.kind == Node::string && swofty_enum_parse(n.s, &v);
        else {
            if (n.kind != Node::number)
                return false;
            v = static_cast<T>(static_cast<int64_t>(n.num));
            return true;
        }
    }
};

template <> struct Json<std::string> {
    static void write(Writer &w, const std::string &v) { w.str(v); }
    static bool read(const Node &n, std::string &v)
    {
        if (n.kind != Node::string)
            return false;
        v = n.s;
        return true;
    }
};

template <class T> struct Json<Ref<T>> {
    static void write(Writer &w, const Ref<T> &v)
    {
        if (v.empty())
            w.raw("null");
        else
            w.str(v.id());
    }
    static bool read(const Node &n, Ref<T> &v)
    {
        if (n.kind == Node::null) {
            v = Ref<T>();
            return true;
        }
        if (n.kind != Node::string)
            return false;
        v = Ref<T>(n.s);
        return true;
    }
};

template <> struct Json<Text> {
    static void write(Writer &w, const Text &v)
    {
        if (v.is_literal()) {
            w.raw("{\"literal\":");
            w.str(v.str());
            w.raw("}");
        } else
            w.str(v.str());
    }
    static bool read(const Node &n, Text &v)
    {
        if (n.kind == Node::string) {
            v = Text::key(n.s);
            return true;
        }
        if (n.kind == Node::null) {
            v = Text();
            return true;
        }
        const Node *l = n.kind == Node::object ? n.get("literal") : nullptr;
        if (!l || l->kind != Node::string)
            return false;
        v = Text::literal(l->s);
        return true;
    }
};

template <> struct Json<Colour> {
    static void write(Writer &w, const Colour &v) { w.str(v.hex()); }
    static bool read(const Node &n, Colour &v)
    {
        if (n.kind != Node::string || n.s.empty() || n.s[0] != '#')
            return false;
        v = Colour(std::string_view(n.s));
        return true;
    }
};

template <> struct Json<Path> {
    static void write(Writer &w, const Path &v)
    {
        switch (v.kind()) {
        case Path::Kind::none:
            w.raw("null");
            break;
        case Path::Kind::file:
            w.str(v.str());
            break;
        case Path::Kind::stock:
            w.raw("{\"stock\":");
            w.str(v.str());
            w.raw("}");
            break;
        case Path::Kind::archive:
            w.raw(v.str());
            break;
        }
    }
    static bool read(const Node &n, Path &v)
    {
        if (n.kind == Node::null)
            v = Path();
        else if (n.kind == Node::string)
            v = Path::file(n.s);
        else if (n.kind == Node::object && n.get("stock") && n.get("stock")->kind == Node::string)
            v = Path::stock(n.get("stock")->s);
        else if (n.kind == Node::object && n.get("erp"))
            v = Path::archive(std::string(n.text));
        else
            return false;
        return true;
    }
};

template <> struct Json<StatId> {
    static void write(Writer &w, StatId v)
    {
        if (v.is_automatic())
            w.str("auto");
        else
            w.integer(v.value);
    }
    static bool read(const Node &n, StatId &v)
    {
        if (n.kind == Node::null || (n.kind == Node::string && n.s == "auto")) {
            v = StatId::automatic;
            return true;
        }
        if (n.kind != Node::number || n.num < 0)
            return false;
        v = StatId(static_cast<int64_t>(n.num));
        return true;
    }
};

template <> struct Json<Curve> {
    static void write(Writer &w, const Curve &v)
    {
        w.raw("[");
        for (size_t i = 0; i < v.points.size(); i++) {
            w.raw(i ? ",[" : "[");
            w.number(v.points[i][0]);
            w.raw(",");
            w.number(v.points[i][1]);
            w.raw("]");
        }
        w.raw("]");
    }
    static bool read(const Node &n, Curve &v)
    {
        if (n.kind != Node::array)
            return false;
        v.points.clear();
        for (const auto &p : n.items) {
            if (p.kind != Node::array || p.items.size() != 2 || p.items[0].kind != Node::number ||
                p.items[1].kind != Node::number)
                return false;
            v.points.push_back({static_cast<float>(p.items[0].num), static_cast<float>(p.items[1].num)});
        }
        return true;
    }
};

template <> struct Json<std::chrono::year_month_day> {
    static void write(Writer &w, const std::chrono::year_month_day &v)
    {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%04d-%02u-%02u", static_cast<int>(v.year()), static_cast<unsigned>(v.month()),
                      static_cast<unsigned>(v.day()));
        w.str(buf);
    }
    static bool read(const Node &n, std::chrono::year_month_day &v)
    {
        int y = 0;
        unsigned m = 0, d = 0;
        if (n.kind != Node::string || std::sscanf(n.s.c_str(), "%d-%u-%u", &y, &m, &d) != 3)
            return false;
        v = std::chrono::year_month_day{std::chrono::year{y}, std::chrono::month{m}, std::chrono::day{d}};
        return true;
    }
};

template <> struct Json<json::Value> {
    static void write(Writer &w, const json::Value &v) { w.raw(json::dump(v)); }
    static bool read(const Node &n, json::Value &v)
    {
        v = json::parse(n.text);
        return true;
    }
};

template <class T> struct Json<std::optional<T>> {
    static void write(Writer &w, const std::optional<T> &v)
    {
        if (v)
            detail::write(w, *v);
        else
            w.raw("null");
    }
    static bool read(const Node &n, std::optional<T> &v)
    {
        if (n.kind == Node::null) {
            v.reset();
            return true;
        }
        T t{};
        if (!detail::read(n, t))
            return false;
        v = std::move(t);
        return true;
    }
};

template <class Seq> void write_seq(Writer &w, const Seq &seq)
{
    w.raw("[");
    bool first = true;
    for (const auto &e : seq) {
        if (!first)
            w.raw(",");
        first = false;
        detail::write(w, e);
    }
    w.raw("]");
}

template <class T> struct Json<std::vector<T>> {
    static void write(Writer &w, const std::vector<T> &v) { write_seq(w, v); }
    static bool read(const Node &n, std::vector<T> &v)
    {
        if (n.kind != Node::array)
            return false;
        v.clear();
        for (const auto &item : n.items) {
            T t{};
            if (!detail::read(item, t))
                return false;
            v.push_back(std::move(t));
        }
        return true;
    }
};

template <class T, size_t N> struct Json<std::array<T, N>> {
    static void write(Writer &w, const std::array<T, N> &v) { write_seq(w, v); }
    static bool read(const Node &n, std::array<T, N> &v)
    {
        if (n.kind != Node::array || n.items.size() != N)
            return false;
        for (size_t i = 0; i < N; i++)
            if (!detail::read(n.items[i], v[i]))
                return false;
        return true;
    }
};

template <class T, auto... Keys> struct Json<Keyed<T, Keys...>> {
    static void write(Writer &w, const Keyed<T, Keys...> &v) { write_seq(w, v.items()); }
    static bool read(const Node &n, Keyed<T, Keys...> &v) { return Json<std::vector<T>>::read(n, v.items()); }
};

template <class K> std::string map_key(const K &k)
{
    if constexpr (std::is_same_v<K, std::string>)
        return k;
    else if constexpr (std::is_enum_v<K> && requires { swofty_enum_name(k); })
        return std::string(swofty_enum_name(k));
    else
        return std::to_string(static_cast<int64_t>(k));
}

template <class K> bool map_key_parse(const std::string &s, K &k)
{
    if constexpr (std::is_same_v<K, std::string>) {
        k = s;
        return true;
    } else if constexpr (std::is_enum_v<K> && requires { swofty_enum_parse(std::string_view{}, &k); })
        return swofty_enum_parse(s, &k);
    else {
        k = static_cast<K>(std::strtoll(s.c_str(), nullptr, 10));
        return true;
    }
}

template <class K, class V> struct Json<std::map<K, V>> {
    static void write(Writer &w, const std::map<K, V> &v)
    {
        w.raw("{");
        for (const auto &[k, e] : v) {
            w.key(map_key(k));
            detail::write(w, e);
        }
        w.raw("}");
    }
    static bool read(const Node &n, std::map<K, V> &v)
    {
        if (n.kind != Node::object)
            return false;
        v.clear();
        for (const auto &[name, e] : n.members) {
            K k{};
            V t{};
            if (!map_key_parse(name, k) || !detail::read(e, t))
                return false;
            v.emplace(std::move(k), std::move(t));
        }
        return true;
    }
};

template <record T> struct Json<T> {
    static void write(Writer &w, const T &v)
    {
        w.raw("{");
        each_field(
            [&](const auto &f) {
                if constexpr (!std::is_same_v<std::remove_cvref_t<decltype(f)>, field_end>) {
                    w.key(f.name);
                    detail::write(w, v.*(f.member));
                }
            },
            swofty_fields(static_cast<const T *>(nullptr)));
        if constexpr (has_extra<T>) {
            std::string extra = json::dump(v.extra);
            Node n;
            if (Reader(extra).parse(n) && n.kind == Node::object)
                for (const auto &[name, e] : n.members) {
                    w.key(name);
                    w.raw(e.text);
                }
        }
        w.raw("}");
    }
    static bool read(const Node &n, T &v)
    {
        if (n.kind != Node::object)
            return false;
        bool ok = true;
        each_field(
            [&](const auto &f) {
                if constexpr (!std::is_same_v<std::remove_cvref_t<decltype(f)>, field_end>) {
                    if (const Node *e = n.get(f.name))
                        ok = detail::read(*e, v.*(f.member)) && ok;
                }
            },
            swofty_fields(static_cast<const T *>(nullptr)));
        if constexpr (has_extra<T>) {
            Writer extra;
            extra.raw("{");
            for (const auto &[name, e] : n.members) {
                bool known = false;
                each_field(
                    [&](const auto &f) {
                        if constexpr (!std::is_same_v<std::remove_cvref_t<decltype(f)>, field_end>)
                            known = known || name == f.name;
                    },
                    swofty_fields(static_cast<const T *>(nullptr)));
                if (!known && name != "extra") {
                    extra.key(name);
                    extra.raw(e.text);
                }
            }
            extra.raw("}");
            v.extra = json::parse(extra.out);
        }
        return ok;
    }
};

template <class T> std::string to_json(const T &v)
{
    Writer w;
    detail::write(w, v);
    return w.out;
}

template <class T> bool from_json(std::string_view text, T &v)
{
    Node n;
    if (!Reader(text).parse(n))
        return false;
    return detail::read(n, v);
}

template <class T> void schema(Writer &w);

template <class T> void schema_ref(Writer &w)
{
    w.raw("{\"type\":[\"string\",\"null\"],\"x-ref\":");
    w.str(registry_name<T>());
    w.raw("}");
}

template <class T> struct Schema {
    static void write(Writer &w)
    {
        if constexpr (std::is_same_v<T, bool>)
            w.raw("{\"type\":\"boolean\"}");
        else if constexpr (std::is_integral_v<T>)
            w.raw("{\"type\":\"integer\"}");
        else if constexpr (std::is_floating_point_v<T>)
            w.raw("{\"type\":\"number\"}");
        else if constexpr (std::is_enum_v<T>) {
            if constexpr (requires { swofty_enum_name(T{}); }) {
                w.raw("{\"type\":\"string\",\"enum\":[");
                for (size_t i = 0;; i++) {
                    auto name = swofty_enum_name(static_cast<T>(i));
                    if (name.empty())
                        break;
                    if (i)
                        w.raw(",");
                    w.str(name);
                }
                w.raw("]}");
            } else
                w.raw("{\"type\":\"integer\"}");
        } else if constexpr (std::is_same_v<T, std::string>)
            w.raw("{\"type\":\"string\"}");
        else if constexpr (std::is_same_v<T, Text>)
            w.raw("{\"oneOf\":[{\"type\":\"string\"},{\"type\":\"object\",\"properties\":{\"literal\":{\"type\":\"string\"}},"
                  "\"required\":[\"literal\"],\"additionalProperties\":false}]}");
        else if constexpr (std::is_same_v<T, Colour>)
            w.raw("{\"type\":\"string\",\"pattern\":\"^#([0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})$\"}");
        else if constexpr (std::is_same_v<T, Path>)
            w.raw("{\"oneOf\":[{\"type\":\"null\"},{\"type\":\"string\"},{\"type\":\"object\"}]}");
        else if constexpr (std::is_same_v<T, StatId>)
            w.raw("{\"oneOf\":[{\"type\":\"integer\",\"minimum\":0},{\"const\":\"auto\"}]}");
        else if constexpr (std::is_same_v<T, Curve>)
            w.raw("{\"type\":\"array\",\"items\":{\"type\":\"array\",\"items\":{\"type\":\"number\"},\"minItems\":2,"
                  "\"maxItems\":2}}");
        else if constexpr (std::is_same_v<T, std::chrono::year_month_day>)
            w.raw("{\"type\":\"string\",\"pattern\":\"^[0-9]{4}-[0-9]{2}-[0-9]{2}$\"}");
        else if constexpr (std::is_same_v<T, json::Value>)
            w.raw("{}");
        else if constexpr (record<T>)
            schema<T>(w);
        else
            w.raw("{}");
    }
};

template <class U> struct SchemaOf : Schema<U> {};

template <class T> struct SchemaOf<Ref<T>> {
    static void write(Writer &w) { schema_ref<T>(w); }
};
template <class T> struct SchemaOf<std::optional<T>> {
    static void write(Writer &w)
    {
        Writer inner;
        SchemaOf<T>::write(inner);
        std::string_view s = inner.out;
        constexpr std::string_view head = "{\"type\":\"";
        if (s.starts_with(head) && s.find("\"enum\"") == std::string_view::npos && s.find("\"null\"") == std::string_view::npos) {
            auto end = s.find('"', head.size());
            w.raw(s.substr(0, head.size() - 1));
            w.raw("[\"");
            w.raw(s.substr(head.size(), end - head.size()));
            w.raw("\",\"null\"]");
            w.raw(s.substr(end + 1));
        } else if (s.find("\"null\"") != std::string_view::npos && s.starts_with("{\"type\":[")) {
            w.raw(s);
        } else {
            w.raw("{\"oneOf\":[{\"type\":\"null\"},");
            w.raw(s);
            w.raw("]}");
        }
    }
};
template <class T> struct SchemaOf<std::vector<T>> {
    static void write(Writer &w)
    {
        w.raw("{\"type\":\"array\",\"items\":");
        SchemaOf<T>::write(w);
        w.raw("}");
    }
};
template <class T, size_t N> struct SchemaOf<std::array<T, N>> {
    static void write(Writer &w)
    {
        w.raw("{\"type\":\"array\",\"items\":");
        SchemaOf<T>::write(w);
        w.raw(",\"minItems\":" + std::to_string(N) + ",\"maxItems\":" + std::to_string(N) + "}");
    }
};
template <class K, class V> struct SchemaOf<std::map<K, V>> {
    static void write(Writer &w)
    {
        w.raw("{\"type\":\"object\",\"additionalProperties\":");
        SchemaOf<V>::write(w);
        w.raw("}");
    }
};

template <class T, auto M> const char *member_name()
{
    const char *found = "";
    each_field(
        [&](const auto &f) {
            if constexpr (!std::is_same_v<std::remove_cvref_t<decltype(f)>, field_end>) {
                if constexpr (std::is_same_v<decltype(f.member), decltype(M)>)
                    if (f.member == M)
                        found = f.name;
            }
        },
        swofty_fields(static_cast<const T *>(nullptr)));
    return found;
}

template <class T, auto... Keys> struct SchemaOf<Keyed<T, Keys...>> {
    static void write(Writer &w)
    {
        w.raw("{\"type\":\"array\",\"items\":");
        SchemaOf<T>::write(w);
        w.raw(",\"x-key\":[");
        bool first = true;
        ((w.raw(first ? "" : ","), w.str(member_name<T, Keys>()), first = false), ...);
        w.raw("]}");
    }
};

template <class T> void schema(Writer &w)
{
    w.raw("{\"type\":\"object\",\"properties\":{");
    each_field(
        [&](const auto &f) {
            if constexpr (!std::is_same_v<std::remove_cvref_t<decltype(f)>, field_end>) {
                w.key(f.name);
                SchemaOf<std::remove_cvref_t<decltype(std::declval<T &>().*(f.member))>>::write(w);
            }
        },
        swofty_fields(static_cast<const T *>(nullptr)));
    w.raw(has_extra<T> ? "},\"additionalProperties\":true}" : "},\"additionalProperties\":false}");
}

}

template <class T> std::string to_json(const T &value) { return detail::to_json(value); }
template <class T> bool from_json(std::string_view text, T &value) { return detail::from_json(text, value); }

}
