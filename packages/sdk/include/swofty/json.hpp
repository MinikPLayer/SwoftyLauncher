// SPDX-License-Identifier: MIT
#pragma once
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

// swofty::json: a self-contained JSON library for addons and the core. Nothing in it throws: parse
// reports a malformed document through Error, lookups return null (or false) for what is missing,
// and a JSON Pointer or merge patch that can't apply leaves the value as it was.
//
// - Numbers keep their kind. An integer literal that fits int64_t is an integer; anything with a
//   fraction or an exponent, or too big for int64_t, is a double. dump writes a double with a
//   fraction or exponent, so it parses back as a double.
// - Objects keep their members in insertion order. A repeated key in a document keeps its first
//   place and its last value.
// - Pointers are RFC 6901, with the leading "/" optional, plus the keyed form for a list whose
//   elements are matched by key: "teams/slot=1/seats/0" is the first seat of the element of
//   "teams" whose "slot" is 1, and "changes/round=15,slot=8,seat=1" names a several-field key.
//   A key value with ',' or '=' in it is written as a JSON string: id="a,b".
// - merge_patch is RFC 7396 extended for keyed lists (Keys). A patch list at a keyed path merges
//   element by element, each one a merge patch on the target's element with the same key fields
//   (strings, numbers or bools), appending keys the target lacks; an element with "$delete": true
//   removes the target's element with that key. An empty patch list changes nothing (a missing list
//   becomes an empty one). An element without all its key fields, or a "$delete" that isn't a bool, is
//   an error, and nothing changes. A list at a path that isn't keyed replaces the target's whole, as in
//   RFC 7396.

namespace swofty::json {

class Value;
using Array = std::vector<Value>;

enum class Type : std::uint8_t { null, boolean, integer, floating, string, array, object };

struct Error {
    std::size_t offset = 0;
    std::size_t line = 0;
    std::size_t column = 0;
    std::string message;
    bool ok() const noexcept { return message.empty(); }
};

inline constexpr int max_depth = 256;

class Object {
public:
    using Entry = std::pair<std::string, Value>;
    using iterator = std::vector<Entry>::iterator;
    using const_iterator = std::vector<Entry>::const_iterator;

    Object() = default;
    Object(std::initializer_list<std::pair<std::string_view, Value>> init);
    Object(const Object &o);
    Object(Object &&o) noexcept;
    Object &operator=(const Object &o);
    Object &operator=(Object &&o) noexcept;
    ~Object();

    std::size_t size() const noexcept;
    bool empty() const noexcept;
    iterator begin() noexcept;
    iterator end() noexcept;
    const_iterator begin() const noexcept;
    const_iterator end() const noexcept;
    const Entry &entry(std::size_t i) const;

    const Value *find(std::string_view key) const;
    Value *find(std::string_view key);
    bool contains(std::string_view key) const { return index_of(key) >= 0; }
    Value &operator[](std::string_view key);
    Value &insert(std::string_view key, Value v);
    Value &insert_front(std::string_view key, Value v);
    bool erase(std::string_view key);
    void clear() noexcept;
    void reserve(std::size_t n);

    friend bool operator==(const Object &a, const Object &b);

private:
    static constexpr std::size_t index_from = 16;
    std::ptrdiff_t index_of(std::string_view key) const;
    Value &append(std::string_view key, Value &&v);
    std::vector<Entry> entries_;
    mutable std::unique_ptr<std::unordered_map<std::string_view, std::size_t>> index_;
};

class Keys;

class Value {
public:
    Value() noexcept = default;
    Value(std::nullptr_t) noexcept {}
    Value(bool b) noexcept : v_(b) {}
    template <std::integral I>
        requires(!std::same_as<I, bool> && !std::same_as<I, char> && !std::same_as<I, char8_t> &&
                 !std::same_as<I, char16_t> && !std::same_as<I, char32_t> && !std::same_as<I, wchar_t>)
    Value(I i) noexcept
    {
        if constexpr (std::is_unsigned_v<I> && sizeof(I) >= sizeof(std::int64_t)) {
            if (i > static_cast<std::make_unsigned_t<std::int64_t>>(std::numeric_limits<std::int64_t>::max()))
                v_ = static_cast<double>(i);
            else
                v_ = static_cast<std::int64_t>(i);
        } else {
            v_ = static_cast<std::int64_t>(i);
        }
    }
    Value(double d) noexcept : v_(d) {}
    Value(float f) noexcept : v_(static_cast<double>(f)) {}
    Value(const char *s) : v_(std::string(s ? s : "")) {}
    Value(std::string_view s) : v_(std::string(s)) {}
    Value(std::string s) noexcept : v_(std::move(s)) {}
    Value(Array a) noexcept : v_(std::move(a)) {}
    Value(Object o) noexcept : v_(std::move(o)) {}

    static Value array(std::initializer_list<Value> items = {}) { return Value(Array(items)); }
    static Value object(std::initializer_list<std::pair<std::string_view, Value>> members = {})
    {
        return Value(Object(members));
    }

    Type type() const noexcept { return static_cast<Type>(v_.index()); }
    bool is_null() const noexcept { return type() == Type::null; }
    bool is_bool() const noexcept { return type() == Type::boolean; }
    bool is_int() const noexcept { return type() == Type::integer; }
    bool is_double() const noexcept { return type() == Type::floating; }
    bool is_number() const noexcept { return is_int() || is_double(); }
    bool is_string() const noexcept { return type() == Type::string; }
    bool is_array() const noexcept { return type() == Type::array; }
    bool is_object() const noexcept { return type() == Type::object; }

    // as_int also takes a double with no fraction that fits int64_t; as_double takes either kind.
    bool as_bool(bool fallback = false) const noexcept;
    std::int64_t as_int(std::int64_t fallback = 0) const noexcept;
    double as_double(double fallback = 0) const noexcept;
    std::string_view as_string(std::string_view fallback = {}) const noexcept;

    const std::string *if_string() const noexcept { return std::get_if<std::string>(&v_); }
    std::string *if_string() noexcept { return std::get_if<std::string>(&v_); }
    const Array *if_array() const noexcept { return std::get_if<Array>(&v_); }
    Array *if_array() noexcept { return std::get_if<Array>(&v_); }
    const Object *if_object() const noexcept { return std::get_if<Object>(&v_); }
    Object *if_object() noexcept { return std::get_if<Object>(&v_); }

    // Members of an array or an object; 0 for anything else.
    std::size_t size() const noexcept;

    // Reading: a missing key or index, or a value of another type, gives a null.
    const Value &operator[](std::string_view key) const noexcept;
    const Value &operator[](const char *key) const noexcept { return (*this)[std::string_view(key ? key : "")]; }
    template <std::integral I> const Value &operator[](I i) const noexcept
    {
        if constexpr (std::is_signed_v<I>)
            if (i < 0)
                return null_ref();
        auto a = if_array();
        return a && static_cast<std::size_t>(i) < a->size() ? (*a)[static_cast<std::size_t>(i)] : null_ref();
    }
    const Value *find(std::string_view key) const noexcept;
    Value *find(std::string_view key) noexcept;
    bool contains(std::string_view key) const noexcept { return find(key) != nullptr; }

    // Writing: a value that isn't an object (or an array) becomes an empty one first. Indexing an
    // array past its end grows it with nulls; a negative index appends.
    Value &operator[](std::string_view key);
    Value &operator[](const char *key) { return (*this)[std::string_view(key ? key : "")]; }
    template <std::integral I> Value &operator[](I i)
    {
        if (!is_array())
            v_ = Array();
        auto &a = std::get<Array>(v_);
        if constexpr (std::is_signed_v<I>)
            if (i < 0)
                return a.emplace_back();
        auto n = static_cast<std::size_t>(i);
        if (n >= a.size())
            a.resize(n + 1);
        return a[n];
    }
    Value &push_back(Value v);
    bool erase(std::string_view key);

    // JSON Pointer. get returns null when nothing is there. set creates the objects on the way, and a
    // keyed element that is missing (with its key fields set); "-" or an index equal to the size
    // appends to an array. It refuses to go through a value that isn't an object, array or null, and
    // leaves the value as it was when it refuses. remove can't remove the root.
    const Value *get(std::string_view pointer) const;
    Value *get(std::string_view pointer);
    bool set(std::string_view pointer, Value v, Error *err = nullptr);
    bool remove(std::string_view pointer);

    friend bool operator==(const Value &a, const Value &b) noexcept;

private:
    static const Value &null_ref() noexcept
    {
        static const Value null_value;
        return null_value;
    }
    std::variant<std::monostate, bool, std::int64_t, double, std::string, Array, Object> v_;
};

// Key fields of the keyed lists in a value, by path: "/teams" for a list at the root's "teams", and
// "*" for any element of a keyed list on the way, as in "/teams/*/drivers". "" is the root itself.
class Keys {
public:
    Keys() = default;
    Keys(std::initializer_list<std::pair<std::string_view, std::initializer_list<std::string_view>>> init)
    {
        for (auto &[path, fields] : init)
            add(path, std::vector<std::string>(fields.begin(), fields.end()));
    }
    Keys &add(std::string_view path, std::vector<std::string> fields)
    {
        auto p = normal(path);
        for (auto &e : paths_)
            if (e.first == p) {
                e.second = std::move(fields);
                return *this;
            }
        paths_.emplace_back(std::move(p), std::move(fields));
        return *this;
    }
    const std::vector<std::string> *find(std::string_view path) const
    {
        bool bare = !path.empty() && path[0] != '/';
        for (auto &e : paths_)
            if (bare ? e.first.size() == path.size() + 1 && std::string_view(e.first).substr(1) == path
                     : e.first == path)
                return e.second.empty() ? nullptr : &e.second;
        return nullptr;
    }
    bool empty() const noexcept { return paths_.empty(); }

private:
    static std::string normal(std::string_view path)
    {
        if (path.empty() || path[0] == '/')
            return std::string(path);
        return "/" + std::string(path);
    }
    std::vector<std::pair<std::string, std::vector<std::string>>> paths_;
};

// parse's defaults take what most JSON readers do: a repeated key keeps its first place and its last
// value, and a leading UTF-8 byte order mark is skipped. strict() refuses both, as Python's json with a
// duplicate check does (the registry's data files are read that way).
struct ParseOptions {
    bool unique_keys = false;
    bool allow_bom = true;
    static ParseOptions strict() noexcept { return {true, false}; }
};

Value parse(std::string_view text, Error *err = nullptr);
Value parse(std::string_view text, const ParseOptions &options, Error *err = nullptr);
std::string dump(const Value &v, int indent = -1);

bool merge_patch(Value &target, const Value &patch, const Keys &keys = {}, Error *err = nullptr);
Value merged(const Value &target, const Value &patch, const Keys &keys = {}, Error *err = nullptr);

const Value *get(const Value &root, std::string_view pointer);
Value *get(Value &root, std::string_view pointer);
bool set(Value &root, std::string_view pointer, Value v, Error *err = nullptr);
bool remove(Value &root, std::string_view pointer);

// One pointer segment, escaped (~0, ~1).
std::string escape(std::string_view segment);
// The keyed segment naming an element by its key fields: "slot=1", "round=15,slot=8,seat=1".
// Empty when the element isn't an object carrying every field as a string, number, bool or null.
std::string key_segment(const Value &element, const std::vector<std::string> &fields);

namespace detail {

inline bool utf8_next(std::string_view s, std::size_t &i, std::uint32_t &cp)
{
    auto c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
        cp = c;
        ++i;
        return true;
    }
    int n;
    std::uint32_t min;
    if ((c & 0xE0) == 0xC0) {
        n = 1;
        cp = c & 0x1F;
        min = 0x80;
    } else if ((c & 0xF0) == 0xE0) {
        n = 2;
        cp = c & 0x0F;
        min = 0x800;
    } else if ((c & 0xF8) == 0xF0) {
        n = 3;
        cp = c & 0x07;
        min = 0x10000;
    } else {
        return false;
    }
    if (s.size() - i <= static_cast<std::size_t>(n))
        return false;
    for (int k = 1; k <= n; ++k) {
        auto d = static_cast<unsigned char>(s[i + k]);
        if ((d & 0xC0) != 0x80)
            return false;
        cp = (cp << 6) | (d & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        return false;
    i += n + 1;
    return true;
}

inline void utf8_put(std::string &out, std::uint32_t cp)
{
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

inline bool number_equal(std::int64_t i, double d) noexcept
{
    if (!(d >= -9223372036854775808.0 && d < 9223372036854775808.0))
        return false;
    auto t = static_cast<std::int64_t>(d);
    return static_cast<double>(t) == d && t == i;
}

class Parser {
public:
    Parser(std::string_view text, Error *err, ParseOptions options = {}) : s_(text), err_(err), opt_(options) {}

    Value run()
    {
        if (opt_.allow_bom && s_.size() >= 3 && s_.substr(0, 3) == "\xEF\xBB\xBF")
            i_ = 3;
        ws();
        Value v;
        if (!value(v, 0))
            return Value();
        ws();
        if (i_ != s_.size()) {
            fail("unexpected text after the value");
            return Value();
        }
        return v;
    }

private:
    std::string_view s_;
    std::size_t i_ = 0;
    Error *err_;
    ParseOptions opt_;

    bool fail(const char *what)
    {
        if (err_) {
            err_->offset = i_;
            err_->line = 1;
            std::size_t start = 0;
            for (std::size_t k = 0; k < i_ && k < s_.size(); ++k)
                if (s_[k] == '\n') {
                    ++err_->line;
                    start = k + 1;
                }
            err_->column = i_ - start + 1;
            err_->message = what;
        }
        return false;
    }

    void ws()
    {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r'))
            ++i_;
    }

    bool literal(std::string_view word)
    {
        if (s_.substr(i_, word.size()) != word)
            return fail("invalid literal");
        i_ += word.size();
        return true;
    }

    bool value(Value &out, int depth)
    {
        if (i_ >= s_.size())
            return fail("unexpected end of input");
        switch (s_[i_]) {
        case '{':
            return object(out, depth + 1);
        case '[':
            return array(out, depth + 1);
        case '"': {
            std::string str;
            if (!string(str))
                return false;
            out = Value(std::move(str));
            return true;
        }
        case 't':
            if (!literal("true"))
                return false;
            out = Value(true);
            return true;
        case 'f':
            if (!literal("false"))
                return false;
            out = Value(false);
            return true;
        case 'n':
            if (!literal("null"))
                return false;
            out = Value();
            return true;
        default:
            if (s_[i_] == '-' || (s_[i_] >= '0' && s_[i_] <= '9'))
                return number(out);
            return fail("unexpected character");
        }
    }

    bool array(Value &out, int depth)
    {
        if (depth > max_depth)
            return fail("nested too deeply");
        ++i_;
        Array a;
        ws();
        if (i_ < s_.size() && s_[i_] == ']') {
            ++i_;
            out = Value(std::move(a));
            return true;
        }
        for (;;) {
            ws();
            a.emplace_back();
            if (!value(a.back(), depth))
                return false;
            ws();
            if (i_ >= s_.size())
                return fail("unterminated array");
            if (s_[i_] == ',') {
                ++i_;
                continue;
            }
            if (s_[i_] == ']') {
                ++i_;
                out = Value(std::move(a));
                return true;
            }
            return fail("expected ',' or ']'");
        }
    }

    bool object(Value &out, int depth)
    {
        if (depth > max_depth)
            return fail("nested too deeply");
        ++i_;
        Object o;
        ws();
        if (i_ < s_.size() && s_[i_] == '}') {
            ++i_;
            out = Value(std::move(o));
            return true;
        }
        for (;;) {
            ws();
            if (i_ >= s_.size() || s_[i_] != '"')
                return fail("expected a member name");
            std::string key;
            if (!string(key))
                return false;
            if (opt_.unique_keys && o.contains(key))
                return fail("duplicate key");
            ws();
            if (i_ >= s_.size() || s_[i_] != ':')
                return fail("expected ':'");
            ++i_;
            ws();
            Value v;
            if (!value(v, depth))
                return false;
            o.insert(key, std::move(v));
            ws();
            if (i_ >= s_.size())
                return fail("unterminated object");
            if (s_[i_] == ',') {
                ++i_;
                continue;
            }
            if (s_[i_] == '}') {
                ++i_;
                out = Value(std::move(o));
                return true;
            }
            return fail("expected ',' or '}'");
        }
    }

    bool hex4(std::uint32_t &cp)
    {
        if (s_.size() - i_ < 4)
            return fail("truncated \\u escape");
        cp = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s_[i_ + k];
            cp <<= 4;
            if (c >= '0' && c <= '9')
                cp |= c - '0';
            else if (c >= 'a' && c <= 'f')
                cp |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F')
                cp |= c - 'A' + 10;
            else
                return fail("invalid \\u escape");
        }
        i_ += 4;
        return true;
    }

    bool string(std::string &out)
    {
        ++i_;
        for (;;) {
            std::size_t run = i_;
            while (i_ < s_.size()) {
                auto c = static_cast<unsigned char>(s_[i_]);
                if (c == '"' || c == '\\' || c < 0x20 || c >= 0x80)
                    break;
                ++i_;
            }
            out.append(s_.data() + run, i_ - run);
            if (i_ >= s_.size())
                return fail("unterminated string");
            auto c = static_cast<unsigned char>(s_[i_]);
            if (c == '"') {
                ++i_;
                return true;
            }
            if (c < 0x20)
                return fail("control character in a string");
            if (c >= 0x80) {
                std::size_t at = i_;
                std::uint32_t cp;
                if (!utf8_next(s_, i_, cp))
                    return fail("invalid UTF-8");
                out.append(s_.data() + at, i_ - at);
                continue;
            }
            ++i_;
            if (i_ >= s_.size())
                return fail("unterminated string");
            char e = s_[i_++];
            switch (e) {
            case '"':
                out += '"';
                break;
            case '\\':
                out += '\\';
                break;
            case '/':
                out += '/';
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u': {
                std::uint32_t cp;
                if (!hex4(cp))
                    return false;
                if (cp >= 0xD800 && cp <= 0xDBFF && s_.size() - i_ >= 6 && s_[i_] == '\\' && s_[i_ + 1] == 'u') {
                    std::size_t back = i_;
                    i_ += 2;
                    std::uint32_t lo;
                    if (!hex4(lo))
                        return false;
                    if (lo >= 0xDC00 && lo <= 0xDFFF)
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    else {
                        cp = 0xFFFD;
                        i_ = back;
                    }
                } else if (cp >= 0xD800 && cp <= 0xDFFF) {
                    cp = 0xFFFD;
                }
                utf8_put(out, cp);
                break;
            }
            default:
                --i_;
                return fail("invalid escape");
            }
        }
    }

    bool number(Value &out)
    {
        std::size_t start = i_;
        bool neg = false, real = false;
        if (s_[i_] == '-') {
            neg = true;
            ++i_;
        }
        auto digit = [&] { return i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9'; };
        if (!digit())
            return fail("invalid number");
        std::ptrdiff_t lead = -1;
        std::size_t int_digits = 0;
        if (s_[i_] == '0') {
            ++i_;
            int_digits = 1;
        } else {
            while (digit()) {
                if (lead < 0 && s_[i_] != '0')
                    lead = static_cast<std::ptrdiff_t>(int_digits);
                ++int_digits;
                ++i_;
            }
        }
        std::ptrdiff_t frac_lead = -1;
        if (i_ < s_.size() && s_[i_] == '.') {
            real = true;
            ++i_;
            if (!digit())
                return fail("invalid number");
            std::ptrdiff_t k = 0;
            while (digit()) {
                if (frac_lead < 0 && s_[i_] != '0')
                    frac_lead = k;
                ++k;
                ++i_;
            }
        }
        long long exp = 0;
        if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
            real = true;
            ++i_;
            bool eneg = false;
            if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-'))
                eneg = s_[i_++] == '-';
            if (!digit())
                return fail("invalid number");
            while (digit()) {
                if (exp < 100000000)
                    exp = exp * 10 + (s_[i_] - '0');
                ++i_;
            }
            if (eneg)
                exp = -exp;
        }
        const char *b = s_.data() + start, *e = s_.data() + i_;
        if (!real) {
            std::int64_t n = 0;
            auto r = std::from_chars(b, e, n);
            if (r.ec == std::errc() && r.ptr == e) {
                out = Value(n);
                return true;
            }
        }
        double d = 0;
        auto r = std::from_chars(b, e, d);
        if (r.ec == std::errc::result_out_of_range) {
            long long mag;
            if (lead >= 0)
                mag = static_cast<long long>(int_digits) - lead - 1 + exp;
            else if (frac_lead >= 0)
                mag = -frac_lead - 1 + exp;
            else
                mag = -1;
            if (mag >= 0)
                return fail("number out of range");
            d = neg ? -0.0 : 0.0;
        } else if (r.ec != std::errc() || r.ptr != e) {
            return fail("invalid number");
        }
        out = Value(d);
        return true;
    }
};

inline void dump_string(std::string &out, std::string_view s)
{
    static constexpr char hex[] = "0123456789abcdef";
    out += '"';
    std::size_t i = 0;
    while (i < s.size()) {
        auto c = static_cast<unsigned char>(s[i]);
        if (c >= 0x80) {
            std::size_t at = i;
            std::uint32_t cp;
            if (utf8_next(s, i, cp))
                out.append(s.data() + at, i - at);
            else {
                out += "\\ufffd";
                ++i;
            }
            continue;
        }
        ++i;
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) {
                out += "\\u00";
                out += hex[c >> 4];
                out += hex[c & 15];
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    out += '"';
}

inline void dump_double(std::string &out, double d)
{
    if (d != d || d == std::numeric_limits<double>::infinity() || d == -std::numeric_limits<double>::infinity()) {
        out += "null";
        return;
    }
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof buf, d);
    std::string_view t(buf, static_cast<std::size_t>(r.ptr - buf));
    out += t;
    if (t.find_first_of(".eE") == std::string_view::npos)
        out += ".0";
}

inline void newline(std::string &out, int indent, int level)
{
    out += '\n';
    out.append(static_cast<std::size_t>(indent) * static_cast<std::size_t>(level), ' ');
}

inline void dump_value(std::string &out, const Value &v, int indent, int level)
{
    switch (v.type()) {
    case Type::null:
        out += "null";
        break;
    case Type::boolean:
        out += v.as_bool() ? "true" : "false";
        break;
    case Type::integer: {
        char buf[24];
        auto r = std::to_chars(buf, buf + sizeof buf, v.as_int());
        out.append(buf, r.ptr);
        break;
    }
    case Type::floating:
        dump_double(out, v.as_double());
        break;
    case Type::string:
        dump_string(out, *v.if_string());
        break;
    case Type::array: {
        auto &a = *v.if_array();
        if (a.empty()) {
            out += "[]";
            break;
        }
        out += '[';
        for (std::size_t k = 0; k < a.size(); ++k) {
            if (k)
                out += ',';
            if (indent >= 0)
                newline(out, indent, level + 1);
            dump_value(out, a[k], indent, level + 1);
        }
        if (indent >= 0)
            newline(out, indent, level);
        out += ']';
        break;
    }
    case Type::object: {
        auto &o = *v.if_object();
        if (o.empty()) {
            out += "{}";
            break;
        }
        out += '{';
        bool first = true;
        for (auto &[key, member] : o) {
            if (!first)
                out += ',';
            first = false;
            if (indent >= 0)
                newline(out, indent, level + 1);
            dump_string(out, key);
            out += indent >= 0 ? ": " : ":";
            dump_value(out, member, indent, level + 1);
        }
        if (indent >= 0)
            newline(out, indent, level);
        out += '}';
        break;
    }
    }
}

struct KeyPart {
    std::string field;
    std::string text;
    bool quoted = false;
};

inline bool unescape(std::string_view seg, std::string &out)
{
    out.clear();
    for (std::size_t k = 0; k < seg.size(); ++k) {
        if (seg[k] != '~') {
            out += seg[k];
            continue;
        }
        if (k + 1 >= seg.size())
            return false;
        if (seg[k + 1] == '0')
            out += '~';
        else if (seg[k + 1] == '1')
            out += '/';
        else
            return false;
        ++k;
    }
    return true;
}

inline bool split_pointer(std::string_view pointer, std::vector<std::string> &segs)
{
    segs.clear();
    if (pointer.empty())
        return true;
    if (pointer[0] == '/')
        pointer.remove_prefix(1);
    for (;;) {
        auto slash = pointer.find('/');
        std::string seg;
        if (!unescape(pointer.substr(0, slash), seg))
            return false;
        segs.push_back(std::move(seg));
        if (slash == std::string_view::npos)
            return true;
        pointer.remove_prefix(slash + 1);
    }
}

inline bool key_parts(std::string_view seg, std::vector<KeyPart> &parts)
{
    parts.clear();
    std::size_t k = 0;
    for (;;) {
        auto eq = seg.find('=', k);
        if (eq == std::string_view::npos || eq == k)
            return false;
        KeyPart part;
        part.field = std::string(seg.substr(k, eq - k));
        if (part.field.find(',') != std::string::npos || part.field.find('"') != std::string::npos)
            return false;
        k = eq + 1;
        if (k < seg.size() && seg[k] == '"') {
            std::size_t end = k + 1;
            while (end < seg.size() && seg[end] != '"')
                end += seg[end] == '\\' ? 2 : 1;
            if (end >= seg.size())
                return false;
            Error e;
            Value v = parse(seg.substr(k, end + 1 - k), &e);
            if (!v.is_string())
                return false;
            part.text = *v.if_string();
            part.quoted = true;
            k = end + 1;
        } else {
            auto comma = seg.find(',', k);
            auto end = comma == std::string_view::npos ? seg.size() : comma;
            part.text = std::string(seg.substr(k, end - k));
            k = end;
        }
        parts.push_back(std::move(part));
        if (k == seg.size())
            return true;
        if (seg[k] != ',')
            return false;
        ++k;
    }
}

inline Value key_value(const KeyPart &p)
{
    if (p.quoted)
        return Value(p.text);
    if (p.text == "true")
        return Value(true);
    if (p.text == "false")
        return Value(false);
    if (p.text == "null")
        return Value();
    if (!p.text.empty() && (p.text[0] == '-' || (p.text[0] >= '0' && p.text[0] <= '9'))) {
        Error e;
        Value v = parse(p.text, &e);
        if (v.is_number() && e.message.empty())
            return v;
    }
    return Value(p.text);
}

inline bool key_field_matches(const Value &field, const KeyPart &p)
{
    if (p.quoted)
        return field.is_string() && *field.if_string() == p.text;
    if (field.is_string())
        return *field.if_string() == p.text;
    if (field.is_null() || field.is_bool() || field.is_number()) {
        Value want = key_value(p);
        return !want.is_string() && want == field;
    }
    return false;
}

inline bool element_matches(const Value &elem, const std::vector<KeyPart> &parts)
{
    auto o = elem.if_object();
    if (!o)
        return false;
    for (auto &p : parts) {
        auto f = o->find(p.field);
        if (!f || !key_field_matches(*f, p))
            return false;
    }
    return true;
}

inline bool array_index(std::string_view seg, std::size_t &n)
{
    if (seg.empty() || seg.size() > 18 || (seg.size() > 1 && seg[0] == '0'))
        return false;
    n = 0;
    for (char c : seg) {
        if (c < '0' || c > '9')
            return false;
        n = n * 10 + static_cast<std::size_t>(c - '0');
    }
    return true;
}

inline Value *child(Value &v, const std::string &seg)
{
    if (auto o = v.if_object())
        return o->find(seg);
    auto a = v.if_array();
    if (!a)
        return nullptr;
    std::size_t n;
    if (array_index(seg, n))
        return n < a->size() ? &(*a)[n] : nullptr;
    if (seg.find('=') == std::string::npos)
        return nullptr;
    std::vector<KeyPart> parts;
    if (!key_parts(seg, parts))
        return nullptr;
    for (auto &e : *a)
        if (element_matches(e, parts))
            return &e;
    return nullptr;
}

inline bool fail(Error *err, std::string message)
{
    if (err) {
        err->offset = err->line = err->column = 0;
        err->message = std::move(message);
    }
    return false;
}

inline Value with_keys(Value v, const std::vector<KeyPart> &parts)
{
    if (auto o = v.if_object())
        for (auto it = parts.rbegin(); it != parts.rend(); ++it)
            if (!o->contains(it->field))
                o->insert_front(it->field, key_value(*it));
    return v;
}

inline bool holds_keys(const Value &elem, const std::vector<std::string> &fields)
{
    auto o = elem.if_object();
    if (!o)
        return false;
    for (auto &f : fields) {
        auto v = o->find(f);
        if (!v || v->is_null() || v->is_array() || v->is_object())
            return false;
    }
    return true;
}

inline bool same_key(const Value &a, const Value &b, const std::vector<std::string> &fields)
{
    for (auto &f : fields)
        if (!(*a.find(f) == *b.find(f)))
            return false;
    return true;
}

inline bool is_delete(const Value &elem)
{
    auto d = elem.find("$delete");
    return d && d->is_bool() && d->as_bool();
}

inline bool check_patch(const Value &patch, const Keys &keys, std::string &path, Error *err)
{
    if (auto o = patch.if_object()) {
        for (auto &[name, member] : *o) {
            auto len = path.size();
            path += '/';
            path += escape(name);
            if (!check_patch(member, keys, path, err))
                return false;
            path.resize(len);
        }
        return true;
    }
    auto a = patch.if_array();
    if (!a)
        return true;
    auto fields = keys.find(path);
    if (!fields)
        return true;
    for (std::size_t k = 0; k < a->size(); ++k) {
        auto &e = (*a)[k];
        if (!holds_keys(e, *fields)) {
            std::string names;
            for (auto &f : *fields)
                names += (names.empty() ? "" : ", ") + f;
            return fail(err, path + "/" + std::to_string(k) + ": an element of a keyed list needs its key fields (" + names +
                                 ") as strings, numbers or bools");
        }
        auto d = e.find("$delete");
        if (d && !d->is_bool())
            return fail(err, path + "/" + std::to_string(k) + ": \"$delete\" must be true or false");
    }
    auto len = path.size();
    path += "/*";
    for (auto &e : *a)
        if (!is_delete(e) && !check_patch(e, keys, path, err))
            return false;
    path.resize(len);
    return true;
}

inline void apply(Value &target, const Value &patch, const Keys &keys, std::string &path);

inline void apply_keyed(Value &target, const Array &patch, const std::vector<std::string> &fields, const Keys &keys,
                        std::string &path)
{
    if (!target.is_array())
        target = Array();
    auto &list = *target.if_array();
    auto len = path.size();
    path += "/*";
    for (auto &e : patch) {
        if (is_delete(e)) {
            std::erase_if(list, [&](const Value &t) { return holds_keys(t, fields) && same_key(t, e, fields); });
            continue;
        }
        Value *found = nullptr;
        for (auto &t : list)
            if (holds_keys(t, fields) && same_key(t, e, fields)) {
                found = &t;
                break;
            }
        if (!found) {
            list.emplace_back(Object());
            found = &list.back();
        }
        apply(*found, e, keys, path);
        found->erase("$delete");
    }
    path.resize(len);
}

inline void apply(Value &target, const Value &patch, const Keys &keys, std::string &path)
{
    if (auto po = patch.if_object()) {
        if (!target.is_object())
            target = Object();
        auto &to = *target.if_object();
        for (auto &[name, member] : *po) {
            if (member.is_null()) {
                to.erase(name);
                continue;
            }
            auto len = path.size();
            path += '/';
            path += escape(name);
            auto slot = to.find(name);
            if (!slot)
                slot = &to.insert(name, Value());
            apply(*slot, member, keys, path);
            path.resize(len);
        }
        return;
    }
    if (auto pa = patch.if_array()) {
        if (auto fields = keys.find(path)) {
            apply_keyed(target, *pa, *fields, keys, path);
            return;
        }
    }
    target = patch;
}

}

inline Object::Object(std::initializer_list<std::pair<std::string_view, Value>> init)
{
    for (auto &[k, v] : init)
        insert(k, v);
}

inline Object::Object(const Object &o) : entries_(o.entries_) {}

inline Object::Object(Object &&o) noexcept : entries_(std::move(o.entries_)), index_(std::move(o.index_)) {}

inline Object &Object::operator=(const Object &o)
{
    if (this != &o) {
        entries_ = o.entries_;
        index_.reset();
    }
    return *this;
}

inline Object &Object::operator=(Object &&o) noexcept
{
    if (this != &o) {
        entries_ = std::move(o.entries_);
        index_ = std::move(o.index_);
        o.index_.reset();
    }
    return *this;
}

inline Object::~Object() = default;

inline std::size_t Object::size() const noexcept
{
    return entries_.size();
}

inline bool Object::empty() const noexcept
{
    return entries_.empty();
}

inline Object::iterator Object::begin() noexcept
{
    return entries_.begin();
}

inline Object::iterator Object::end() noexcept
{
    return entries_.end();
}

inline Object::const_iterator Object::begin() const noexcept
{
    return entries_.begin();
}

inline Object::const_iterator Object::end() const noexcept
{
    return entries_.end();
}

inline const Object::Entry &Object::entry(std::size_t i) const
{
    return entries_[i];
}

inline std::ptrdiff_t Object::index_of(std::string_view key) const
{
    if (entries_.size() < index_from) {
        for (std::size_t k = 0; k < entries_.size(); ++k)
            if (entries_[k].first == key)
                return static_cast<std::ptrdiff_t>(k);
        return -1;
    }
    if (!index_) {
        index_ = std::make_unique<std::unordered_map<std::string_view, std::size_t>>();
        index_->reserve(entries_.size());
        for (std::size_t k = 0; k < entries_.size(); ++k)
            index_->emplace(entries_[k].first, k);
    }
    auto it = index_->find(key);
    return it == index_->end() ? -1 : static_cast<std::ptrdiff_t>(it->second);
}

inline Value &Object::append(std::string_view key, Value &&v)
{
    auto cap = entries_.capacity();
    entries_.emplace_back(std::string(key), std::move(v));
    if (entries_.capacity() != cap)
        index_.reset();
    else if (index_)
        index_->emplace(entries_.back().first, entries_.size() - 1);
    return entries_.back().second;
}

inline const Value *Object::find(std::string_view key) const
{
    auto k = index_of(key);
    return k < 0 ? nullptr : &entries_[static_cast<std::size_t>(k)].second;
}

inline Value *Object::find(std::string_view key)
{
    auto k = index_of(key);
    return k < 0 ? nullptr : &entries_[static_cast<std::size_t>(k)].second;
}

inline Value &Object::operator[](std::string_view key)
{
    if (auto v = find(key))
        return *v;
    return append(key, Value());
}

inline Value &Object::insert(std::string_view key, Value v)
{
    if (auto slot = find(key)) {
        *slot = std::move(v);
        return *slot;
    }
    return append(key, std::move(v));
}

inline Value &Object::insert_front(std::string_view key, Value v)
{
    if (auto slot = find(key)) {
        *slot = std::move(v);
        return *slot;
    }
    entries_.emplace(entries_.begin(), std::string(key), std::move(v));
    index_.reset();
    return entries_.front().second;
}

inline bool Object::erase(std::string_view key)
{
    auto k = index_of(key);
    if (k < 0)
        return false;
    entries_.erase(entries_.begin() + k);
    index_.reset();
    return true;
}

inline void Object::clear() noexcept
{
    entries_.clear();
    index_.reset();
}

inline void Object::reserve(std::size_t n)
{
    if (n > entries_.capacity()) {
        entries_.reserve(n);
        index_.reset();
    }
}

inline bool operator==(const Object &a, const Object &b)
{
    if (a.size() != b.size())
        return false;
    for (auto &[key, v] : a) {
        auto w = b.find(key);
        if (!w || !(v == *w))
            return false;
    }
    return true;
}

inline bool Value::as_bool(bool fallback) const noexcept
{
    auto b = std::get_if<bool>(&v_);
    return b ? *b : fallback;
}

inline std::int64_t Value::as_int(std::int64_t fallback) const noexcept
{
    if (auto i = std::get_if<std::int64_t>(&v_))
        return *i;
    if (auto d = std::get_if<double>(&v_))
        if (*d >= -9223372036854775808.0 && *d < 9223372036854775808.0 &&
            static_cast<double>(static_cast<std::int64_t>(*d)) == *d)
            return static_cast<std::int64_t>(*d);
    return fallback;
}

inline double Value::as_double(double fallback) const noexcept
{
    if (auto d = std::get_if<double>(&v_))
        return *d;
    if (auto i = std::get_if<std::int64_t>(&v_))
        return static_cast<double>(*i);
    return fallback;
}

inline std::string_view Value::as_string(std::string_view fallback) const noexcept
{
    auto s = std::get_if<std::string>(&v_);
    return s ? std::string_view(*s) : fallback;
}

inline std::size_t Value::size() const noexcept
{
    if (auto a = if_array())
        return a->size();
    if (auto o = if_object())
        return o->size();
    return 0;
}

inline const Value &Value::operator[](std::string_view key) const noexcept
{
    auto v = find(key);
    return v ? *v : null_ref();
}

inline const Value *Value::find(std::string_view key) const noexcept
{
    auto o = if_object();
    return o ? o->find(key) : nullptr;
}

inline Value *Value::find(std::string_view key) noexcept
{
    auto o = if_object();
    return o ? o->find(key) : nullptr;
}

inline Value &Value::operator[](std::string_view key)
{
    if (!is_object())
        v_ = Object();
    return std::get<Object>(v_)[key];
}

inline Value &Value::push_back(Value v)
{
    if (!is_array())
        v_ = Array();
    auto &a = std::get<Array>(v_);
    a.push_back(std::move(v));
    return a.back();
}

inline bool Value::erase(std::string_view key)
{
    auto o = if_object();
    return o && o->erase(key);
}

inline const Value *Value::get(std::string_view pointer) const
{
    return const_cast<Value *>(this)->get(pointer);
}

inline Value *Value::get(std::string_view pointer)
{
    std::vector<std::string> segs;
    if (!detail::split_pointer(pointer, segs))
        return nullptr;
    Value *at = this;
    for (auto &seg : segs)
        if (!(at = detail::child(*at, seg)))
            return nullptr;
    return at;
}

namespace detail {

inline bool set_path(Value &node, const std::vector<std::string> &segs, std::size_t from, Value v, Error *err)
{
    if (from == segs.size()) {
        node = std::move(v);
        return true;
    }
    Value *at = &node;
    for (std::size_t k = from; k < segs.size(); ++k) {
        auto &seg = segs[k];
        bool last = k + 1 == segs.size();
        Value *next = nullptr;
        std::vector<KeyPart> parts;
        if (at->is_null()) {
            bool list = seg == "-" || (seg.find('=') != std::string::npos && key_parts(seg, parts));
            *at = list ? Value(Array()) : Value(Object());
        }
        if (auto o = at->if_object()) {
            next = o->find(seg);
            if (!next)
                next = &o->insert(seg, Value());
        } else if (auto a = at->if_array()) {
            if (parts.empty()) {
                next = &a->emplace_back();
            } else {
                next = &a->emplace_back(with_keys(Value(Object()), parts));
                if (last) {
                    *next = with_keys(std::move(v), parts);
                    return true;
                }
            }
        } else {
            return fail(err, "\"" + seg + "\" goes through a value that is not an object or an array");
        }
        if (last) {
            *next = std::move(v);
            return true;
        }
        at = next;
    }
    return true;
}

}

inline bool Value::set(std::string_view pointer, Value v, Error *err)
{
    std::vector<std::string> segs;
    if (!detail::split_pointer(pointer, segs))
        return detail::fail(err, "invalid pointer \"" + std::string(pointer) + "\"");
    if (segs.empty()) {
        *this = std::move(v);
        return true;
    }
    Value *at = this;
    for (std::size_t k = 0; k < segs.size(); ++k) {
        auto &seg = segs[k];
        bool last = k + 1 == segs.size();
        if (at->is_null()) {
            Value fresh;
            if (!detail::set_path(fresh, segs, k, std::move(v), err))
                return false;
            *at = std::move(fresh);
            return true;
        }
        Value *next = nullptr;
        if (auto o = at->if_object()) {
            next = o->find(seg);
            if (!next) {
                Value fresh;
                if (!detail::set_path(fresh, segs, k + 1, std::move(v), err))
                    return false;
                o->insert(seg, std::move(fresh));
                return true;
            }
        } else if (auto a = at->if_array()) {
            std::size_t n;
            std::vector<detail::KeyPart> parts;
            if (seg == "-" || (detail::array_index(seg, n) && n == a->size())) {
                Value fresh;
                if (!detail::set_path(fresh, segs, k + 1, std::move(v), err))
                    return false;
                a->push_back(std::move(fresh));
                return true;
            } else if (detail::array_index(seg, n)) {
                if (n > a->size())
                    return detail::fail(err, "index " + seg + " is past the end of the array");
                next = &(*a)[n];
            } else if (seg.find('=') != std::string::npos && detail::key_parts(seg, parts)) {
                for (auto &e : *a)
                    if (detail::element_matches(e, parts)) {
                        next = &e;
                        break;
                    }
                if (!next) {
                    Value fresh = detail::with_keys(last ? std::move(v) : Value(Object()), parts);
                    if (!last && !detail::set_path(fresh, segs, k + 1, std::move(v), err))
                        return false;
                    a->push_back(std::move(fresh));
                    return true;
                }
                if (last) {
                    *next = detail::with_keys(std::move(v), parts);
                    return true;
                }
            } else {
                return detail::fail(err, "\"" + seg + "\" is not an index or a key of the array");
            }
        } else {
            return detail::fail(err, "\"" + seg + "\" goes through a value that is not an object or an array");
        }
        if (last) {
            *next = std::move(v);
            return true;
        }
        at = next;
    }
    return true;
}

inline bool Value::remove(std::string_view pointer)
{
    std::vector<std::string> segs;
    if (!detail::split_pointer(pointer, segs) || segs.empty())
        return false;
    Value *at = this;
    for (std::size_t k = 0; k + 1 < segs.size(); ++k)
        if (!(at = detail::child(*at, segs[k])))
            return false;
    auto &seg = segs.back();
    if (auto o = at->if_object())
        return o->erase(seg);
    auto a = at->if_array();
    if (!a)
        return false;
    std::size_t n;
    if (detail::array_index(seg, n)) {
        if (n >= a->size())
            return false;
        a->erase(a->begin() + static_cast<std::ptrdiff_t>(n));
        return true;
    }
    std::vector<detail::KeyPart> parts;
    if (seg.find('=') == std::string::npos || !detail::key_parts(seg, parts))
        return false;
    for (auto it = a->begin(); it != a->end(); ++it)
        if (detail::element_matches(*it, parts)) {
            a->erase(it);
            return true;
        }
    return false;
}

inline bool operator==(const Value &a, const Value &b) noexcept
{
    auto ta = a.type(), tb = b.type();
    if (ta == Type::integer && tb == Type::floating)
        return detail::number_equal(a.as_int(), b.as_double());
    if (ta == Type::floating && tb == Type::integer)
        return detail::number_equal(b.as_int(), a.as_double());
    if (ta != tb)
        return false;
    switch (ta) {
    case Type::null:
        return true;
    case Type::boolean:
        return a.as_bool() == b.as_bool();
    case Type::integer:
        return a.as_int() == b.as_int();
    case Type::floating:
        return a.as_double() == b.as_double();
    case Type::string:
        return *a.if_string() == *b.if_string();
    case Type::array:
        return *a.if_array() == *b.if_array();
    case Type::object:
        return *a.if_object() == *b.if_object();
    }
    return false;
}

inline Value parse(std::string_view text, Error *err)
{
    if (err)
        *err = Error();
    return detail::Parser(text, err).run();
}

inline Value parse(std::string_view text, const ParseOptions &options, Error *err)
{
    if (err)
        *err = Error();
    return detail::Parser(text, err, options).run();
}

inline std::string dump(const Value &v, int indent)
{
    std::string out;
    detail::dump_value(out, v, indent < 0 ? -1 : indent, 0);
    return out;
}

inline bool merge_patch(Value &target, const Value &patch, const Keys &keys, Error *err)
{
    std::string path;
    if (err)
        *err = Error();
    if (!detail::check_patch(patch, keys, path, err))
        return false;
    path.clear();
    Value own = patch;
    detail::apply(target, own, keys, path);
    return true;
}

inline Value merged(const Value &target, const Value &patch, const Keys &keys, Error *err)
{
    Value out = target;
    merge_patch(out, patch, keys, err);
    return out;
}

inline const Value *get(const Value &root, std::string_view pointer)
{
    return root.get(pointer);
}

inline Value *get(Value &root, std::string_view pointer)
{
    return root.get(pointer);
}

inline bool set(Value &root, std::string_view pointer, Value v, Error *err)
{
    return root.set(pointer, std::move(v), err);
}

inline bool remove(Value &root, std::string_view pointer)
{
    return root.remove(pointer);
}

inline std::string escape(std::string_view segment)
{
    std::string out;
    for (char c : segment) {
        if (c == '~')
            out += "~0";
        else if (c == '/')
            out += "~1";
        else
            out += c;
    }
    return out;
}

inline std::string key_segment(const Value &element, const std::vector<std::string> &fields)
{
    if (!element.is_object() || fields.empty())
        return {};
    std::string seg;
    for (auto &f : fields) {
        auto v = element.find(f);
        if (!v || v->is_array() || v->is_object())
            return {};
        if (!seg.empty())
            seg += ',';
        seg += f;
        seg += '=';
        if (auto s = v->if_string()) {
            detail::KeyPart bare{f, *s, false};
            bool plain = s->find_first_of(",\"=") == std::string::npos && !s->empty() &&
                         detail::key_value(bare).is_string();
            if (plain)
                seg += *s;
            else
                detail::dump_string(seg, *s);
        } else {
            seg += dump(*v);
        }
    }
    return escape(seg);
}

}
