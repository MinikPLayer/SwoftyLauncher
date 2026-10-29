#include "schema.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <iterator>
#include <set>
#include <unordered_map>

namespace swofty_core::registry {

namespace schema_detail {

using Value = swofty::json::Value;
using Object = swofty::json::Object;
using Array = swofty::json::Array;
using Type = swofty::json::Type;

bool py_space(char32_t c)
{
    return (c >= 0x09 && c <= 0x0D) || (c >= 0x1C && c <= 0x20) || c == 0x85 || c == 0xA0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}

bool ascii_digit(char32_t c)
{
    return c >= '0' && c <= '9';
}

bool ascii_word(char32_t c)
{
    return c == '_' || ascii_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool in_ranges(char32_t c, const char32_t (*ranges)[2], std::size_t n)
{
    for (std::size_t i = 0; i < n; ++i)
        if (c >= ranges[i][0] && c <= ranges[i][1])
            return true;
    return false;
}

bool py_digit(char32_t c)
{
    static const char32_t ranges[][2] = {{0x660, 0x669}, {0x6F0, 0x6F9}, {0x966, 0x96F}, {0xFF10, 0xFF19}};
    return ascii_digit(c) || (c >= 0x80 && in_ranges(c, ranges, std::size(ranges)));
}

bool py_word(char32_t c)
{
    static const char32_t ranges[][2] = {
        {0xAA, 0xAA},     {0xB2, 0xB3},     {0xB5, 0xB5},     {0xB9, 0xBA},     {0xBC, 0xBE},     {0xC0, 0xD6},
        {0xD8, 0xF6},     {0xF8, 0x2C1},    {0x2C6, 0x2D1},   {0x2E0, 0x2E4},   {0x2EC, 0x2EC},   {0x2EE, 0x2EE},
        {0x370, 0x374},   {0x376, 0x377},   {0x37A, 0x37D},   {0x37F, 0x37F},   {0x386, 0x386},   {0x388, 0x38A},
        {0x38C, 0x38C},   {0x38E, 0x3A1},   {0x3A3, 0x3F5},   {0x3F7, 0x481},   {0x48A, 0x52F},   {0x531, 0x556},
        {0x560, 0x588},   {0x5D0, 0x5EA},   {0x620, 0x64A},   {0x660, 0x669},   {0x6F0, 0x6F9},   {0x966, 0x96F},
        {0x1E00, 0x1F15}, {0x3041, 0x3096}, {0x30A1, 0x30FA}, {0x4E00, 0x9FFF}, {0xAC00, 0xD7A3}, {0xFF10, 0xFF19},
        {0xFF21, 0xFF3A}, {0xFF41, 0xFF5A}};
    return c == '_' || ascii_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= 0x80 && in_ranges(c, ranges, std::size(ranges)));
}

void decode(std::string_view s, std::u32string &out)
{
    out.clear();
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            out.push_back(c);
            ++i;
            continue;
        }
        std::size_t n = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 0;
        bool ok = n > 0 && i + n < s.size();
        char32_t cp = c & (n == 1 ? 0x1F : n == 2 ? 0x0F : 0x07);
        for (std::size_t k = 1; ok && k <= n; ++k) {
            auto d = static_cast<unsigned char>(s[i + k]);
            if ((d & 0xC0) != 0x80)
                ok = false;
            cp = (cp << 6) | (d & 0x3F);
        }
        if (!ok) {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        out.push_back(cp);
        i += n + 1;
    }
}

std::size_t code_points(std::string_view s)
{
    std::size_t n = 0;
    for (char c : s)
        n += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
    return n;
}

std::string escape_segment(std::string_view seg)
{
    std::string out;
    out.reserve(seg.size());
    for (char c : seg) {
        if (c == '~')
            out += "~0";
        else if (c == '/')
            out += "~1";
        else
            out += c;
    }
    return out;
}

std::string pointer_of(const std::vector<std::string> &parts)
{
    if (parts.empty())
        return "/";
    std::string out;
    for (auto &p : parts) {
        out += '/';
        out += escape_segment(p);
    }
    return out;
}

bool integral(double d)
{
    return std::isfinite(d) && std::floor(d) == d;
}

bool is_integer(const Value &v)
{
    return v.is_int() || (v.is_double() && integral(v.as_double()));
}

const char *type_of(const Value &v)
{
    switch (v.type()) {
    case Type::null:
        return "null";
    case Type::boolean:
        return "boolean";
    case Type::integer:
        return "integer";
    case Type::floating:
        return integral(v.as_double()) ? "integer" : "number";
    case Type::string:
        return "string";
    case Type::array:
        return "array";
    case Type::object:
        return "object";
    }
    return "null";
}

int compare_int_double(std::int64_t i, double d)
{
    if (std::isnan(d))
        return 2;
    if (d >= 9223372036854775808.0)
        return -1;
    if (d < -9223372036854775808.0)
        return 1;
    double fl = std::floor(d);
    auto di = static_cast<std::int64_t>(fl);
    if (i < di)
        return -1;
    if (i > di)
        return 1;
    return fl == d ? 0 : -1;
}

int compare_numbers(const Value &a, const Value &b)
{
    if (a.is_int() && b.is_int()) {
        auto x = a.as_int(), y = b.as_int();
        return x < y ? -1 : x > y ? 1 : 0;
    }
    if (a.is_int())
        return compare_int_double(a.as_int(), b.as_double());
    if (b.is_int()) {
        int r = compare_int_double(b.as_int(), a.as_double());
        return r == 2 ? 2 : -r;
    }
    double x = a.as_double(), y = b.as_double();
    if (std::isnan(x) || std::isnan(y))
        return 2;
    return x < y ? -1 : x > y ? 1 : 0;
}

bool json_equal(const Value &a, const Value &b)
{
    if (a.is_number() && b.is_number())
        return compare_numbers(a, b) == 0;
    if (a.type() != b.type())
        return false;
    switch (a.type()) {
    case Type::null:
        return true;
    case Type::boolean:
        return a.as_bool() == b.as_bool();
    case Type::string:
        return *a.if_string() == *b.if_string();
    case Type::array: {
        auto &x = *a.if_array(), &y = *b.if_array();
        if (x.size() != y.size())
            return false;
        for (std::size_t i = 0; i < x.size(); ++i)
            if (!json_equal(x[i], y[i]))
                return false;
        return true;
    }
    case Type::object: {
        auto &x = *a.if_object(), &y = *b.if_object();
        if (x.size() != y.size())
            return false;
        for (auto &[k, v] : x) {
            auto *w = y.find(k);
            if (!w || !json_equal(v, *w))
                return false;
        }
        return true;
    }
    default:
        return false;
    }
}

void double_text(std::string &out, double d)
{
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof buf, d);
    std::string_view t(buf, static_cast<std::size_t>(r.ptr - buf));
    out += t;
    if (t.find_first_of(".eni") == std::string_view::npos)
        out += ".0";
}

void canonical_into(std::string &out, const Value &v, bool top)
{
    switch (v.type()) {
    case Type::null:
        out += "null";
        return;
    case Type::boolean:
        out += v.as_bool() ? "true" : "false";
        return;
    case Type::integer:
        out += std::to_string(v.as_int());
        return;
    case Type::floating: {
        double d = v.as_double();
        if (top && integral(d) && std::fabs(d) < 9.2e18) {
            out += std::to_string(static_cast<std::int64_t>(d));
            return;
        }
        out += 'd';
        double_text(out, d);
        return;
    }
    case Type::string:
        out += swofty::json::dump(v);
        return;
    case Type::array: {
        out += '[';
        bool first = true;
        for (auto &e : *v.if_array()) {
            if (!first)
                out += ',';
            first = false;
            canonical_into(out, e, false);
        }
        out += ']';
        return;
    }
    case Type::object: {
        std::vector<const Object::Entry *> entries;
        for (auto &e : *v.if_object())
            entries.push_back(&e);
        std::sort(entries.begin(), entries.end(), [](auto *a, auto *b) { return a->first < b->first; });
        out += '{';
        bool first = true;
        for (auto *e : entries) {
            if (!first)
                out += ',';
            first = false;
            out += swofty::json::dump(Value(e->first));
            out += ':';
            canonical_into(out, e->second, false);
        }
        out += '}';
        return;
    }
    }
}

std::string canonical(const Value &v)
{
    std::string out;
    canonical_into(out, v, true);
    return out;
}

void show_into(std::string &out, const Value &v, std::size_t cap)
{
    if (out.size() > cap)
        return;
    switch (v.type()) {
    case Type::floating:
        double_text(out, v.as_double());
        return;
    case Type::array: {
        out += '[';
        bool first = true;
        for (auto &e : *v.if_array()) {
            if (out.size() > cap)
                return;
            if (!first)
                out += ", ";
            first = false;
            show_into(out, e, cap);
        }
        out += ']';
        return;
    }
    case Type::object: {
        out += '{';
        bool first = true;
        for (auto &[k, e] : *v.if_object()) {
            if (out.size() > cap)
                return;
            if (!first)
                out += ", ";
            first = false;
            out += swofty::json::dump(Value(k));
            out += ": ";
            show_into(out, e, cap);
        }
        out += '}';
        return;
    }
    default:
        out += swofty::json::dump(v);
        return;
    }
}

std::string show(const Value &v)
{
    std::string out;
    show_into(out, v, 64);
    if (out.size() > 60) {
        out.resize(57);
        while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80)
            out.pop_back();
        if (!out.empty() && static_cast<unsigned char>(out.back()) >= 0xC0)
            out.pop_back();
        out += "...";
    }
    return out;
}

bool record_id_ok(std::string_view s)
{
    if (!s.empty() && s.back() == '\n')
        s.remove_suffix(1);
    auto slash = s.find('/');
    if (slash == std::string_view::npos || slash == 0 || slash + 1 >= s.size())
        return false;
    auto ns_char = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
    if (!ns_char(s[0]))
        return false;
    for (std::size_t i = 1; i < slash; ++i)
        if (!ns_char(s[i]) && s[i] != '.' && s[i] != '_' && s[i] != '-')
            return false;
    std::u32string rest;
    decode(s.substr(slash + 1), rest);
    for (char32_t c : rest)
        if (py_space(c) || c == '|' || c == '?' || c == '*')
            return false;
    return !rest.empty();
}

// ---- pattern: a Pike VM over code points (no backtracking, no recursion while matching) ----

struct CharClass {
    enum : std::uint8_t { digit = 1, not_digit = 2, word = 4, not_word = 8, space = 16, not_space = 32 };
    std::vector<std::pair<char32_t, char32_t>> ranges;
    std::uint8_t flags = 0;
    bool negate = false;

    bool test(char32_t c) const
    {
        bool hit = false;
        for (auto &[lo, hi] : ranges)
            if (c >= lo && c <= hi) {
                hit = true;
                break;
            }
        if (!hit && flags)
            hit = ((flags & digit) && py_digit(c)) || ((flags & not_digit) && !py_digit(c)) ||
                  ((flags & word) && py_word(c)) || ((flags & not_word) && !py_word(c)) ||
                  ((flags & space) && py_space(c)) || ((flags & not_space) && !py_space(c));
        return hit != negate;
    }
};

enum class Op : std::uint8_t { ch, any, cls, split, jmp, match, bol, eol, wordb, nwordb, begin, end };

struct Inst {
    Op op;
    std::uint32_t a = 0;
    std::uint32_t b = 0;
};

struct Ast {
    enum Kind : std::uint8_t { empty, ch, any, cls, assertion, cat, alt, rep } kind = empty;
    char32_t c = 0;
    std::uint32_t idx = 0;
    Op op = Op::match;
    int min = 0;
    int max = 0;
    std::vector<Ast> kids;
};

constexpr int rx_max_repeat = 1000;
constexpr std::size_t rx_max_program = 20000;
constexpr int rx_max_depth = 64;

struct Regex {
    std::vector<Inst> prog;
    std::vector<CharClass> classes;

    bool assertion_holds(Op op, const std::u32string &s, std::size_t i) const
    {
        std::size_t n = s.size();
        switch (op) {
        case Op::bol:
        case Op::begin:
            return i == 0;
        case Op::eol:
            return i == n || (i + 1 == n && s[i] == '\n');
        case Op::end:
            return i == n;
        case Op::wordb:
        case Op::nwordb: {
            bool before = i > 0 && py_word(s[i - 1]);
            bool after = i < n && py_word(s[i]);
            return (before != after) == (op == Op::wordb);
        }
        default:
            return false;
        }
    }

    bool search(std::string_view text) const
    {
        thread_local std::u32string s;
        thread_local std::vector<std::uint32_t> mark;
        thread_local std::vector<std::uint32_t> clist, nlist, stack;
        thread_local std::uint32_t gen = 0;
        decode(text, s);
        if (mark.size() < prog.size())
            mark.assign(prog.size(), 0);
        bool matched = false;
        auto next_gen = [&] {
            if (++gen == 0) {
                std::fill(mark.begin(), mark.end(), 0);
                gen = 1;
            }
        };
        auto add = [&](std::vector<std::uint32_t> &list, std::uint32_t pc, std::size_t i) {
            stack.clear();
            stack.push_back(pc);
            while (!stack.empty()) {
                std::uint32_t p = stack.back();
                stack.pop_back();
                if (mark[p] == gen)
                    continue;
                mark[p] = gen;
                const Inst &in = prog[p];
                switch (in.op) {
                case Op::jmp:
                    stack.push_back(in.a);
                    break;
                case Op::split:
                    stack.push_back(in.b);
                    stack.push_back(in.a);
                    break;
                case Op::match:
                    matched = true;
                    break;
                case Op::bol:
                case Op::eol:
                case Op::wordb:
                case Op::nwordb:
                case Op::begin:
                case Op::end:
                    if (assertion_holds(in.op, s, i))
                        stack.push_back(p + 1);
                    break;
                default:
                    list.push_back(p);
                    break;
                }
            }
        };
        next_gen();
        clist.clear();
        add(clist, 0, 0);
        for (std::size_t i = 0;; ++i) {
            if (matched)
                return true;
            if (i == s.size())
                return false;
            next_gen();
            nlist.clear();
            char32_t c = s[i];
            for (std::uint32_t p : clist) {
                const Inst &in = prog[p];
                bool ok = (in.op == Op::ch && in.a == c) || (in.op == Op::any && c != '\n') ||
                          (in.op == Op::cls && classes[in.a].test(c));
                if (ok)
                    add(nlist, p + 1, i + 1);
            }
            add(nlist, 0, i + 1);
            std::swap(clist, nlist);
        }
    }
};

class RxCompiler {
public:
    RxCompiler(const std::u32string &p, Regex &rx) : p_(p), rx_(rx) {}

    bool run(std::string &err)
    {
        Ast root;
        if (!alternation(root))
            return done(err);
        if (i_ < p_.size()) {
            fail("unbalanced parenthesis");
            return done(err);
        }
        if (!emit(root))
            return done(err);
        rx_.prog.push_back({Op::match});
        return true;
    }

private:
    const std::u32string &p_;
    Regex &rx_;
    std::size_t i_ = 0;
    int depth_ = 0;
    std::string err_;

    bool done(std::string &err)
    {
        err = err_.empty() ? "is not a regular expression the schema check supports" : err_;
        return false;
    }

    bool fail(const char *msg)
    {
        if (err_.empty())
            err_ = std::string(msg) + " at position " + std::to_string(i_);
        return false;
    }

    bool at_end() const { return i_ >= p_.size(); }

    bool alternation(Ast &out)
    {
        Ast first;
        if (!sequence(first))
            return false;
        if (at_end() || p_[i_] != '|') {
            out = std::move(first);
            return true;
        }
        out.kind = Ast::alt;
        out.kids.push_back(std::move(first));
        while (!at_end() && p_[i_] == '|') {
            ++i_;
            Ast next;
            if (!sequence(next))
                return false;
            out.kids.push_back(std::move(next));
        }
        return true;
    }

    bool sequence(Ast &out)
    {
        out.kind = Ast::cat;
        while (!at_end() && p_[i_] != '|' && p_[i_] != ')') {
            Ast a;
            bool quantifiable = true;
            if (!atom(a, quantifiable))
                return false;
            if (!quantifier(a, quantifiable))
                return false;
            out.kids.push_back(std::move(a));
        }
        return true;
    }

    bool brace(std::size_t at, int &min, int &max, std::size_t &end) const
    {
        if (at >= p_.size() || p_[at] != '{')
            return false;
        std::size_t k = at + 1;
        auto number = [&](int &v) {
            std::size_t start = k;
            long long n = 0;
            while (k < p_.size() && ascii_digit(p_[k])) {
                n = std::min<long long>(n * 10 + (p_[k] - '0'), 1LL << 40);
                ++k;
            }
            v = static_cast<int>(std::min<long long>(n, 1LL << 30));
            return k > start;
        };
        int lo = 0, hi = -1;
        bool has_lo = number(lo);
        if (k < p_.size() && p_[k] == '}') {
            if (!has_lo)
                return false;
            hi = lo;
        } else if (k < p_.size() && p_[k] == ',') {
            ++k;
            int h = 0;
            if (number(h))
                hi = h;
            if (k >= p_.size() || p_[k] != '}')
                return false;
        } else {
            return false;
        }
        min = has_lo ? lo : 0;
        max = hi;
        end = k + 1;
        return true;
    }

    bool quantifier(Ast &a, bool quantifiable)
    {
        if (at_end())
            return true;
        int min = 0, max = 0;
        std::size_t end = 0;
        char32_t c = p_[i_];
        if (c == '*') {
            min = 0, max = -1, end = i_ + 1;
        } else if (c == '+') {
            min = 1, max = -1, end = i_ + 1;
        } else if (c == '?') {
            min = 0, max = 1, end = i_ + 1;
        } else if (!brace(i_, min, max, end)) {
            return true;
        }
        if (!quantifiable)
            return fail("nothing to repeat");
        if (max >= 0 && min > max)
            return fail("min repeat greater than max repeat");
        if (min > rx_max_repeat || max > rx_max_repeat)
            return fail("repeat count too large for the schema check");
        i_ = end;
        if (!at_end() && p_[i_] == '?')
            ++i_;
        else if (!at_end() && p_[i_] == '+')
            return fail("possessive repeats are not supported");
        int m2 = 0, x2 = 0;
        std::size_t e2 = 0;
        if (!at_end() && (p_[i_] == '*' || p_[i_] == '+' || p_[i_] == '?' || brace(i_, m2, x2, e2)))
            return fail("multiple repeat");
        Ast r;
        r.kind = Ast::rep;
        r.min = min;
        r.max = max;
        r.kids.push_back(std::move(a));
        a = std::move(r);
        return true;
    }

    bool hex(int digits, char32_t &out)
    {
        out = 0;
        for (int k = 0; k < digits; ++k) {
            if (at_end())
                return fail("incomplete escape");
            char32_t c = p_[i_++];
            int v = ascii_digit(c) ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
            if (v < 0)
                return fail("incomplete escape");
            out = out * 16 + static_cast<char32_t>(v);
        }
        if (out > 0x10FFFF)
            return fail("bad escape");
        return true;
    }

    // An escape after '\'. kind: 0 a literal (out), 1 a class flag (flag), 2 an assertion (op).
    bool escape(bool in_class, int &kind, char32_t &out, std::uint8_t &flag, Op &op)
    {
        if (at_end())
            return fail("bad escape (end of pattern)");
        char32_t c = p_[i_++];
        kind = 0;
        switch (c) {
        case 'd':
            kind = 1, flag = CharClass::digit;
            return true;
        case 'D':
            kind = 1, flag = CharClass::not_digit;
            return true;
        case 'w':
            kind = 1, flag = CharClass::word;
            return true;
        case 'W':
            kind = 1, flag = CharClass::not_word;
            return true;
        case 's':
            kind = 1, flag = CharClass::space;
            return true;
        case 'S':
            kind = 1, flag = CharClass::not_space;
            return true;
        case 'n':
            out = '\n';
            return true;
        case 't':
            out = '\t';
            return true;
        case 'r':
            out = '\r';
            return true;
        case 'f':
            out = 0x0C;
            return true;
        case 'v':
            out = 0x0B;
            return true;
        case 'a':
            out = 0x07;
            return true;
        case 'x':
            return hex(2, out);
        case 'u':
            return hex(4, out);
        case 'U':
            return hex(8, out);
        case 'b':
            if (in_class) {
                out = 0x08;
                return true;
            }
            kind = 2, op = Op::wordb;
            return true;
        case 'B':
        case 'A':
        case 'Z':
            if (in_class)
                return fail("bad escape");
            kind = 2, op = c == 'B' ? Op::nwordb : c == 'A' ? Op::begin : Op::end;
            return true;
        default:
            break;
        }
        if (c >= '0' && c <= '7' && (c == '0' || in_class)) {
            char32_t v = c - '0';
            for (int k = 0; k < 2 && !at_end() && p_[i_] >= '0' && p_[i_] <= '7'; ++k)
                v = v * 8 + (p_[i_++] - '0');
            if (v > 0377)
                return fail("octal escape out of range");
            out = v;
            return true;
        }
        if (ascii_digit(c))
            return fail("backreferences are not supported");
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
            return fail("bad escape");
        out = c;
        return true;
    }

    bool char_class(Ast &out)
    {
        CharClass cc;
        if (!at_end() && p_[i_] == '^') {
            cc.negate = true;
            ++i_;
        }
        bool first = true;
        for (;;) {
            if (at_end())
                return fail("unterminated character set");
            char32_t c = p_[i_];
            if (c == ']' && !first) {
                ++i_;
                break;
            }
            first = false;
            int kind = 0;
            char32_t lo = 0;
            std::uint8_t flag = 0;
            Op op = Op::match;
            ++i_;
            if (c == '\\') {
                if (!escape(true, kind, lo, flag, op))
                    return false;
            } else {
                lo = c;
            }
            if (i_ + 1 < p_.size() && p_[i_] == '-' && p_[i_ + 1] != ']') {
                ++i_;
                char32_t hc = p_[i_++];
                int hkind = 0;
                char32_t hi = hc;
                std::uint8_t hflag = 0;
                if (hc == '\\' && !escape(true, hkind, hi, hflag, op))
                    return false;
                if (kind != 0 || hkind != 0)
                    return fail("bad character range");
                if (hi < lo)
                    return fail("bad character range");
                cc.ranges.emplace_back(lo, hi);
                continue;
            }
            if (kind == 1)
                cc.flags |= flag;
            else
                cc.ranges.emplace_back(lo, lo);
        }
        out.kind = Ast::cls;
        out.idx = static_cast<std::uint32_t>(rx_.classes.size());
        rx_.classes.push_back(std::move(cc));
        return true;
    }

    bool atom(Ast &out, bool &quantifiable)
    {
        char32_t c = p_[i_];
        switch (c) {
        case '(': {
            if (++depth_ > rx_max_depth)
                return fail("groups nested too deeply");
            ++i_;
            if (!at_end() && p_[i_] == '?') {
                if (i_ + 1 < p_.size() && p_[i_ + 1] == ':') {
                    i_ += 2;
                } else if (i_ + 2 < p_.size() && p_[i_ + 1] == 'P' && p_[i_ + 2] == '<') {
                    i_ += 3;
                    std::size_t start = i_;
                    while (!at_end() && (ascii_word(p_[i_])))
                        ++i_;
                    if (at_end() || p_[i_] != '>' || i_ == start || ascii_digit(p_[start]))
                        return fail("bad group name");
                    ++i_;
                } else {
                    return fail("this group syntax is not supported");
                }
            }
            if (!alternation(out))
                return false;
            if (at_end() || p_[i_] != ')')
                return fail("missing ), unterminated subpattern");
            ++i_;
            --depth_;
            return true;
        }
        case '[':
            ++i_;
            return char_class(out);
        case '.':
            ++i_;
            out.kind = Ast::any;
            return true;
        case '^':
        case '$':
            ++i_;
            out.kind = Ast::assertion;
            out.op = c == '^' ? Op::bol : Op::eol;
            quantifiable = false;
            return true;
        case '*':
        case '+':
        case '?':
            return fail("nothing to repeat");
        case '{': {
            int m = 0, x = 0;
            std::size_t e = 0;
            if (brace(i_, m, x, e))
                return fail("nothing to repeat");
            ++i_;
            out.kind = Ast::ch;
            out.c = c;
            return true;
        }
        case '\\': {
            ++i_;
            int kind = 0;
            char32_t lit = 0;
            std::uint8_t flag = 0;
            Op op = Op::match;
            if (!escape(false, kind, lit, flag, op))
                return false;
            if (kind == 0) {
                out.kind = Ast::ch;
                out.c = lit;
            } else if (kind == 1) {
                CharClass cc;
                cc.flags = flag;
                out.kind = Ast::cls;
                out.idx = static_cast<std::uint32_t>(rx_.classes.size());
                rx_.classes.push_back(std::move(cc));
            } else {
                out.kind = Ast::assertion;
                out.op = op;
                quantifiable = false;
            }
            return true;
        }
        default:
            ++i_;
            out.kind = Ast::ch;
            out.c = c;
            return true;
        }
    }

    std::uint32_t push(Inst in)
    {
        rx_.prog.push_back(in);
        return static_cast<std::uint32_t>(rx_.prog.size() - 1);
    }

    std::uint32_t here() const { return static_cast<std::uint32_t>(rx_.prog.size()); }

    bool emit(const Ast &a)
    {
        if (rx_.prog.size() > rx_max_program)
            return fail("pattern too large for the schema check");
        switch (a.kind) {
        case Ast::empty:
            return true;
        case Ast::ch:
            push({Op::ch, static_cast<std::uint32_t>(a.c)});
            return true;
        case Ast::any:
            push({Op::any});
            return true;
        case Ast::cls:
            push({Op::cls, a.idx});
            return true;
        case Ast::assertion:
            push({a.op});
            return true;
        case Ast::cat:
            for (auto &k : a.kids)
                if (!emit(k))
                    return false;
            return true;
        case Ast::alt: {
            std::vector<std::uint32_t> jumps;
            for (std::size_t k = 0; k + 1 < a.kids.size(); ++k) {
                auto s = push({Op::split});
                rx_.prog[s].a = s + 1;
                if (!emit(a.kids[k]))
                    return false;
                jumps.push_back(push({Op::jmp}));
                rx_.prog[s].b = here();
            }
            if (!emit(a.kids.back()))
                return false;
            for (auto j : jumps)
                rx_.prog[j].a = here();
            return true;
        }
        case Ast::rep: {
            for (int k = 0; k < a.min; ++k)
                if (!emit(a.kids[0]))
                    return false;
            if (a.max < 0) {
                auto s = push({Op::split});
                rx_.prog[s].a = s + 1;
                if (!emit(a.kids[0]))
                    return false;
                push({Op::jmp, s});
                rx_.prog[s].b = here();
                return true;
            }
            std::vector<std::uint32_t> splits;
            for (int k = a.min; k < a.max; ++k) {
                auto s = push({Op::split});
                rx_.prog[s].a = s + 1;
                splits.push_back(s);
                if (!emit(a.kids[0]))
                    return false;
            }
            for (auto s : splits)
                rx_.prog[s].b = here();
            return true;
        }
        }
        return true;
    }
};

bool compile_regex(std::string_view pattern, Regex &rx, std::string &err)
{
    std::u32string p;
    decode(pattern, p);
    RxCompiler c(p, rx);
    return c.run(err);
}

// ---- compiled schema nodes ----

enum : std::uint8_t {
    t_object = 1,
    t_array = 2,
    t_string = 4,
    t_integer = 8,
    t_number = 16,
    t_boolean = 32,
    t_null = 64,
};

enum : std::uint8_t { m_required = 1, m_optional = 2, m_fallback = 4 };

constexpr int extra_any = -2;
constexpr int extra_none = -1;

struct Node {
    enum class Kind : std::uint8_t { always, never, check } kind = Kind::check;
    std::uint8_t types = 0;
    std::string type_text;
    bool has_enum = false;
    std::vector<Value> enum_values;
    std::string enum_text;
    bool has_const = false;
    Value const_value;
    std::string const_text;
    bool has_min = false, has_max = false, has_xmin = false, has_xmax = false;
    Value minimum, maximum, xminimum, xmaximum;
    std::int64_t min_length = -1, max_length = -1;
    int regex = -1;
    std::string pattern;
    bool has_ref = false;
    std::string ref_target;
    std::uint8_t ref_allowed = m_required | m_optional | m_fallback;
    std::uint8_t ref_default = m_required;
    std::vector<std::pair<std::string, int>> properties;
    std::unordered_map<std::string, int> property_index;
    std::vector<std::string> required;
    int extra = extra_any;
    int items = -1;
    std::int64_t min_items = -1, max_items = -1;
    bool unique = false;
    bool keyed = false;
    std::vector<std::string> key_fields;
    std::string key_text;
    std::vector<int> all_of, any_of, one_of;

    int property(std::string_view name) const
    {
        if (properties.size() < 8) {
            for (auto &[n, i] : properties)
                if (n == name)
                    return i;
            return -1;
        }
        auto it = property_index.find(std::string(name));
        return it == property_index.end() ? -1 : it->second;
    }

    bool is_required(std::string_view name) const
    {
        for (auto &r : required)
            if (r == name)
                return true;
        return false;
    }
};

const std::set<std::string_view> &annotations()
{
    static const std::set<std::string_view> s{"$comment", "title", "description", "examples", "deprecated", "default"};
    return s;
}

bool type_bit(std::string_view t, std::uint8_t &bit)
{
    if (t == "object")
        bit = t_object;
    else if (t == "array")
        bit = t_array;
    else if (t == "string")
        bit = t_string;
    else if (t == "integer")
        bit = t_integer;
    else if (t == "number")
        bit = t_number;
    else if (t == "boolean")
        bit = t_boolean;
    else if (t == "null")
        bit = t_null;
    else
        return false;
    return true;
}

bool has_type(const Value &v, std::uint8_t types)
{
    switch (v.type()) {
    case Type::null:
        return types & t_null;
    case Type::boolean:
        return types & t_boolean;
    case Type::integer:
        return types & (t_integer | t_number);
    case Type::floating:
        return (types & t_number) || ((types & t_integer) && integral(v.as_double()));
    case Type::string:
        return types & t_string;
    case Type::array:
        return types & t_array;
    case Type::object:
        return types & t_object;
    }
    return false;
}

} // namespace schema_detail

using namespace schema_detail;

struct Schema::Impl {
    std::vector<Node> nodes;
    std::vector<Regex> regexes;
    int root = 0;
    std::string registry, applies, adapter;
    Version version;
    bool snapshot = false;
    std::int64_t budget = -1;
    swofty::json::Keys keys;
    std::vector<std::pair<std::string, std::vector<std::string>>> keyed;
};

namespace schema_detail {

class Compiler {
public:
    Compiler(const Value &root, Schema::Impl &out) : root_(root), out_(out) {}

    bool fail(const std::vector<std::string> &path, std::string message)
    {
        if (err_.message.empty()) {
            err_.pointer = pointer_of(path);
            err_.message = std::move(message);
        }
        return false;
    }

    Schema::Error error() const { return err_; }

    const Value *resolve(std::string_view ref, std::string &why) const
    {
        if (ref.empty() || ref[0] != '#') {
            why = "is not local to the file";
            return nullptr;
        }
        if (ref == "#" || ref == "#/")
            return &root_;
        if (ref.size() < 2 || ref[1] != '/') {
            why = "is not a JSON Pointer";
            return nullptr;
        }
        const Value *node = &root_;
        std::string_view rest = ref.substr(2);
        for (;;) {
            auto slash = rest.find('/');
            std::string_view raw = rest.substr(0, slash);
            std::string part;
            for (std::size_t k = 0; k < raw.size(); ++k) {
                if (raw[k] == '~' && k + 1 < raw.size() && raw[k + 1] == '1') {
                    part += '/';
                    ++k;
                } else if (raw[k] == '~' && k + 1 < raw.size() && raw[k + 1] == '0') {
                    part += '~';
                    ++k;
                } else {
                    part += raw[k];
                }
            }
            if (auto *o = node->if_object()) {
                auto *next = o->find(part);
                if (!next) {
                    why = "does not resolve";
                    return nullptr;
                }
                node = next;
            } else if (auto *a = node->if_array()) {
                bool digits = !part.empty() && std::all_of(part.begin(), part.end(), [](char c) { return c >= '0' && c <= '9'; });
                std::size_t n = 0;
                if (digits && part.size() < 10)
                    n = std::stoul(part);
                if (!digits || part.size() >= 10 || n >= a->size()) {
                    why = "does not resolve";
                    return nullptr;
                }
                node = &(*a)[n];
            } else {
                why = "does not resolve";
                return nullptr;
            }
            if (slash == std::string_view::npos)
                break;
            rest = rest.substr(slash + 1);
        }
        return node;
    }

    int compile(const Value &start, std::vector<std::string> &path, int depth = 0)
    {
        if (depth > 200) {
            fail(path, "the schema is nested too deeply");
            return -1;
        }
        const Value *cur = &start;
        std::vector<std::string> seen;
        while (cur->is_object() && cur->contains("$ref")) {
            auto &ref = (*cur)["$ref"];
            if (!ref.is_string()) {
                fail(path, "$ref is not a string");
                return -1;
            }
            std::string r(ref.as_string());
            for (auto &[k, v] : *cur->if_object()) {
                (void)v;
                if (k != "$ref" && !annotations().count(k)) {
                    fail(path, "$ref " + swofty::json::dump(ref) + " has sibling keyword " + k);
                    return -1;
                }
            }
            if (std::find(seen.begin(), seen.end(), r) != seen.end()) {
                fail(path, "$ref " + swofty::json::dump(ref) + " loops");
                return -1;
            }
            seen.push_back(r);
            std::string why;
            const Value *target = resolve(r, why);
            if (!target) {
                fail(path, "$ref " + swofty::json::dump(ref) + " " + why);
                return -1;
            }
            cur = target;
        }
        if (auto it = memo_.find(cur); it != memo_.end())
            return it->second;
        int idx = static_cast<int>(out_.nodes.size());
        out_.nodes.emplace_back();
        memo_[cur] = idx;
        Node n;
        if (cur->is_bool()) {
            n.kind = cur->as_bool() ? Node::Kind::always : Node::Kind::never;
            out_.nodes[idx] = std::move(n);
            return idx;
        }
        auto *o = cur->if_object();
        if (!o) {
            fail(path, "a schema is a JSON object, true or false");
            return -1;
        }
        if (!fill(n, *o, path, depth))
            return -1;
        out_.nodes[idx] = std::move(n);
        return idx;
    }

private:
    const Value &root_;
    Schema::Impl &out_;
    std::unordered_map<const Value *, int> memo_;
    Schema::Error err_;

    struct Push {
        std::vector<std::string> &p;
        Push(std::vector<std::string> &path, std::string seg) : p(path) { p.push_back(std::move(seg)); }
        ~Push() { p.pop_back(); }
    };

    bool whole(const Value &v, std::int64_t &out)
    {
        if (!is_integer(v) || v.as_double() < 0)
            return false;
        out = v.is_int() ? v.as_int() : static_cast<std::int64_t>(std::min(v.as_double(), 9.0e18));
        return true;
    }

    bool sub(const Value &v, std::vector<std::string> &path, int depth, int &out)
    {
        out = compile(v, path, depth + 1);
        return out >= 0;
    }

    bool list(const Object &o, const char *key, std::vector<int> &out, std::vector<std::string> &path, int depth)
    {
        auto *v = o.find(key);
        if (!v)
            return true;
        Push p(path, key);
        auto *a = v->if_array();
        if (!a || a->empty())
            return fail(path, "is a non-empty list of schemas");
        for (std::size_t i = 0; i < a->size(); ++i) {
            Push q(path, std::to_string(i));
            int s = 0;
            if (!sub((*a)[i], path, depth, s))
                return false;
            out.push_back(s);
        }
        return true;
    }

    bool fill(Node &n, const Object &o, std::vector<std::string> &path, int depth)
    {
        if (auto *t = o.find("type")) {
            Push p(path, "type");
            std::vector<std::string_view> names;
            if (t->is_string())
                names.push_back(t->as_string());
            else if (auto *a = t->if_array())
                for (auto &e : *a) {
                    if (!e.is_string())
                        return fail(path, "type is a type name or a list of them");
                    names.push_back(e.as_string());
                }
            else
                return fail(path, "type is a type name or a list of them");
            if (names.empty())
                return fail(path, "type lists no type");
            for (auto name : names) {
                std::uint8_t bit = 0;
                if (!type_bit(name, bit))
                    return fail(path, "type " + std::string(name) + " is not a JSON Schema type");
                if (n.types & bit)
                    return fail(path, "type names " + std::string(name) + " twice");
                n.types |= bit;
                if (!n.type_text.empty())
                    n.type_text += " or ";
                n.type_text += name;
            }
        }
        if (auto *e = o.find("enum")) {
            Push p(path, "enum");
            auto *a = e->if_array();
            if (!a || a->empty())
                return fail(path, "is a non-empty list");
            n.has_enum = true;
            n.enum_values = *a;
            n.enum_text = show(*e);
        }
        if (auto *c = o.find("const")) {
            n.has_const = true;
            n.const_value = *c;
            n.const_text = show(*c);
        }
        struct Bound {
            const char *key;
            bool Node::*has;
            Value Node::*value;
        };
        const Bound bounds[] = {{"minimum", &Node::has_min, &Node::minimum},
                                {"maximum", &Node::has_max, &Node::maximum},
                                {"exclusiveMinimum", &Node::has_xmin, &Node::xminimum},
                                {"exclusiveMaximum", &Node::has_xmax, &Node::xmaximum}};
        for (auto &b : bounds)
            if (auto *v = o.find(b.key)) {
                Push p(path, b.key);
                if (!v->is_number())
                    return fail(path, "is a number");
                n.*(b.has) = true;
                n.*(b.value) = *v;
            }
        struct Length {
            const char *key;
            std::int64_t Node::*value;
        };
        const Length lengths[] = {{"minLength", &Node::min_length},
                                  {"maxLength", &Node::max_length},
                                  {"minItems", &Node::min_items},
                                  {"maxItems", &Node::max_items}};
        for (auto &l : lengths)
            if (auto *v = o.find(l.key)) {
                Push p(path, l.key);
                if (!whole(*v, n.*(l.value)))
                    return fail(path, "is a whole number");
            }
        if (auto *v = o.find("pattern")) {
            Push p(path, "pattern");
            if (!v->is_string())
                return fail(path, "is a regular expression string");
            Regex rx;
            std::string why;
            if (!compile_regex(v->as_string(), rx, why))
                return fail(path, "is not a regular expression the schema check supports: " + why);
            n.regex = static_cast<int>(out_.regexes.size());
            n.pattern = std::string(v->as_string());
            out_.regexes.push_back(std::move(rx));
        }
        if (auto *v = o.find("x-ref")) {
            Push p(path, "x-ref");
            if (!v->is_string())
                return fail(path, "is a registry name");
            n.has_ref = true;
            n.ref_target = std::string(v->as_string());
            if (auto *policy = o.find("x-missing"); policy && policy->is_object()) {
                auto bit = [](std::string_view s) -> std::uint8_t {
                    return s == "required" ? m_required : s == "optional" ? m_optional : s == "fallback" ? m_fallback : 0;
                };
                auto *d = policy->find("default");
                std::uint8_t def = d ? (d->is_string() ? bit(d->as_string()) : 0) : m_required;
                n.ref_default = def;
                auto *allow = policy->find("allow");
                if (!allow || allow->is_null()) {
                    n.ref_allowed = static_cast<std::uint8_t>(def | m_required);
                } else if (auto *a = allow->if_array()) {
                    std::uint8_t m = m_required;
                    for (auto &e : *a)
                        if (e.is_string())
                            m |= bit(e.as_string());
                    n.ref_allowed = m;
                } else {
                    return fail(path, "x-missing allow is a list");
                }
            }
        }
        if (auto *v = o.find("properties")) {
            Push p(path, "properties");
            auto *props = v->if_object();
            if (!props)
                return fail(path, "is an object of schemas");
            for (auto &[name, s] : *props) {
                Push q(path, name);
                int idx = 0;
                if (!sub(s, path, depth, idx))
                    return false;
                n.properties.emplace_back(name, idx);
            }
            if (n.properties.size() >= 8)
                for (auto &[name, idx] : n.properties)
                    n.property_index.emplace(name, idx);
        }
        if (auto *v = o.find("required")) {
            Push p(path, "required");
            auto *a = v->if_array();
            if (!a)
                return fail(path, "is a list of field names");
            for (auto &e : *a) {
                if (!e.is_string())
                    return fail(path, "is a list of field names");
                n.required.emplace_back(e.as_string());
            }
        }
        if (auto *v = o.find("additionalProperties")) {
            Push p(path, "additionalProperties");
            if (v->is_bool()) {
                n.extra = v->as_bool() ? extra_any : extra_none;
            } else if (v->is_object()) {
                int idx = 0;
                if (!sub(*v, path, depth, idx))
                    return false;
                n.extra = idx;
            } else {
                return fail(path, "is true, false or a schema");
            }
        }
        if (auto *v = o.find("items")) {
            Push p(path, "items");
            if (!v->is_object() && !v->is_bool())
                return fail(path, "items is one schema; tuple items are outside the subset");
            int idx = 0;
            if (!sub(*v, path, depth, idx))
                return false;
            n.items = idx;
        }
        if (auto *v = o.find("uniqueItems")) {
            Push p(path, "uniqueItems");
            if (!v->is_bool())
                return fail(path, "is true or false");
            n.unique = v->as_bool();
        }
        if (auto *v = o.find("x-key")) {
            Push p(path, "x-key");
            if (v->is_string() && !v->as_string().empty()) {
                n.key_fields.emplace_back(v->as_string());
            } else if (auto *a = v->if_array(); a && !a->empty()) {
                for (auto &e : *a) {
                    if (!e.is_string() || e.as_string().empty())
                        return fail(path, "is a field name or a list of field names");
                    n.key_fields.emplace_back(e.as_string());
                }
            } else {
                return fail(path, "is a field name or a list of field names");
            }
            n.keyed = true;
            for (auto &f : n.key_fields) {
                if (!n.key_text.empty())
                    n.key_text += ", ";
                n.key_text += f;
            }
        }
        return list(o, "allOf", n.all_of, path, depth) && list(o, "anyOf", n.any_of, path, depth) &&
               list(o, "oneOf", n.one_of, path, depth);
    }
};

void collect_keys(const std::vector<Node> &nodes, int idx, const std::string &path, int depth,
                  std::vector<std::pair<std::string, std::vector<std::string>>> &out)
{
    if (idx < 0 || depth > 12)
        return;
    const Node &n = nodes[static_cast<std::size_t>(idx)];
    if (n.keyed) {
        bool known = false;
        for (auto &e : out)
            if (e.first == path)
                known = true;
        if (!known && !path.empty())
            out.emplace_back(path, n.key_fields);
        if (n.items >= 0)
            collect_keys(nodes, n.items, path + "/*", depth + 1, out);
    }
    for (auto &[name, sub] : n.properties)
        collect_keys(nodes, sub, path + "/" + escape_segment(name), depth + 1, out);
}

struct Sink {
    std::vector<Schema::Error> *out = nullptr;
    std::size_t limit = 0;
    std::size_t count = 0;
    bool over = false;
};

class Run {
public:
    Run(const Schema::Impl &s, const Schema::Options &o, Sink &main) : s_(s), o_(o), main_(&main), sink_(&main)
    {
        recursion_cap_ = std::max(64, o.max_depth * 4 + 32);
    }

    void check(int ni, const Value &v, bool patch)
    {
        if (stopped())
            return;
        if (++steps_ > o_.max_steps) {
            abort_with("validation stopped after " + std::to_string(o_.max_steps) + " schema steps");
            return;
        }
        if (++recursion_ > recursion_cap_) {
            --recursion_;
            abort_with("the schema recurses too deeply here");
            return;
        }
        struct Leave {
            int &r;
            ~Leave() { --r; }
        } leave{recursion_};
        const Node &n = s_.nodes[static_cast<std::size_t>(ni)];
        if (n.kind == Node::Kind::always)
            return;
        if (n.kind == Node::Kind::never) {
            fail("no value is allowed here");
            return;
        }
        if (n.types && !has_type(v, n.types)) {
            fail(show(v) + " is " + type_of(v) + ", expected " + n.type_text);
            return;
        }
        if (n.has_enum) {
            bool hit = false;
            for (auto &e : n.enum_values)
                if (json_equal(v, e)) {
                    hit = true;
                    break;
                }
            if (!hit)
                fail(show(v) + " is not one of " + n.enum_text);
        }
        if (n.has_const && !json_equal(v, n.const_value))
            fail(show(v) + " is not " + n.const_text);
        if (v.is_number()) {
            if (n.has_min && compare_numbers(v, n.minimum) == -1)
                fail(show(v) + " is below the minimum " + show(n.minimum));
            if (n.has_max && compare_numbers(v, n.maximum) == 1)
                fail(show(v) + " is above the maximum " + show(n.maximum));
            if (n.has_xmin) {
                int c = compare_numbers(v, n.xminimum);
                if (c == -1 || c == 0)
                    fail(show(v) + " is not above " + show(n.xminimum));
            }
            if (n.has_xmax) {
                int c = compare_numbers(v, n.xmaximum);
                if (c == 1 || c == 0)
                    fail(show(v) + " is not below " + show(n.xmaximum));
            }
        }
        if (auto *str = v.if_string()) {
            if (n.min_length >= 0 || n.max_length >= 0) {
                auto len = static_cast<std::int64_t>(code_points(*str));
                if (n.min_length >= 0 && len < n.min_length)
                    fail(show(v) + " is shorter than " + std::to_string(n.min_length) + " characters");
                if (n.max_length >= 0 && len > n.max_length)
                    fail(show(v) + " is longer than " + std::to_string(n.max_length) + " characters");
            }
            if (n.regex >= 0 && !s_.regexes[static_cast<std::size_t>(n.regex)].search(*str))
                fail(show(v) + " does not match " + n.pattern);
        }
        if (n.has_ref)
            check_ref(n, v);
        if (auto *obj = v.if_object())
            check_object(n, *obj, patch);
        if (auto *arr = v.if_array())
            check_array(n, *arr, patch);
        for (int sub : n.all_of)
            check(sub, v, patch);
        if (!n.any_of.empty()) {
            bool any = false;
            for (int sub : n.any_of)
                if (passes(sub, v, patch)) {
                    any = true;
                    break;
                }
            if (!any && !stopped_hard())
                fail(show(v) + " matches none of the anyOf alternatives");
        }
        if (!n.one_of.empty()) {
            int matches = 0;
            for (int sub : n.one_of)
                matches += passes(sub, v, patch);
            if (matches != 1 && !stopped_hard())
                fail(show(v) + " matches " + std::to_string(matches) + " of the oneOf alternatives, expected exactly 1");
        }
    }

    bool aborted() const { return aborted_; }
    const Schema::Error &abort_error() const { return abort_; }

private:
    const Schema::Impl &s_;
    const Schema::Options &o_;
    Sink *main_;
    Sink *sink_;
    std::vector<std::string> path_;
    std::size_t steps_ = 0;
    int recursion_ = 0;
    int recursion_cap_ = 0;
    bool aborted_ = false;
    Schema::Error abort_;

    bool stopped() const { return aborted_ || sink_->over; }
    bool stopped_hard() const { return aborted_ || main_->over; }

    void record(Sink &sink, std::string message, const std::string *extra = nullptr)
    {
        if (sink.count < sink.limit && sink.out) {
            std::string p = pointer_of(path_);
            if (extra)
                p = (p == "/" ? "" : p) + "/" + escape_segment(*extra);
            sink.out->push_back({std::move(p), std::move(message)});
            ++sink.count;
        } else {
            sink.over = true;
        }
    }

    void fail(std::string message) { record(*sink_, std::move(message)); }
    void fail_at(const std::string &seg, std::string message) { record(*sink_, std::move(message), &seg); }

    void abort_with(std::string message)
    {
        if (aborted_)
            return;
        abort_ = {pointer_of(path_), std::move(message)};
        aborted_ = true;
    }

    bool passes(int ni, const Value &v, bool patch)
    {
        Sink probe;
        Sink *keep = sink_;
        sink_ = &probe;
        check(ni, v, patch);
        sink_ = keep;
        return !probe.over && !aborted_;
    }

    bool deep()
    {
        if (static_cast<int>(path_.size()) < o_.max_depth)
            return false;
        fail("is nested deeper than " + std::to_string(o_.max_depth) + " levels");
        return true;
    }

    void check_ref(const Node &n, const Value &v)
    {
        if (v.is_null())
            return;
        auto *str = v.if_string();
        if (!str) {
            fail(show(v) + " is a reference to " + n.ref_target + " and must be a record id string");
            return;
        }
        std::string_view text = *str;
        bool optional = !text.empty() && text.back() == '?';
        std::string_view body = optional ? text.substr(0, text.size() - 1) : text;
        std::vector<std::string_view> alts;
        for (std::size_t start = 0;;) {
            auto bar = body.find('|', start);
            alts.push_back(body.substr(start, bar == std::string_view::npos ? std::string_view::npos : bar - start));
            if (bar == std::string_view::npos)
                break;
            start = bar + 1;
        }
        for (auto a : alts)
            if (!record_id_ok(a)) {
                fail(show(v) + " is not a record id (<namespace>/<key>, with an optional '?' or '|' fallbacks)");
                return;
            }
        bool bad = false;
        if (optional && !(n.ref_allowed & m_optional)) {
            fail(show(v) + " is optional ('?'), which " + n.ref_target + " references here don't allow");
            bad = true;
        }
        if (alts.size() > 1 && !(n.ref_allowed & m_fallback)) {
            fail(show(v) + " has fallbacks ('|'), which " + n.ref_target + " references here don't allow");
            bad = true;
        }
        if (bad || !o_.resolve || !*o_.resolve || optional || n.ref_default != m_required)
            return;
        for (auto a : alts) {
            if (!a.empty() && a.back() == '\n')
                a.remove_suffix(1);
            if ((*o_.resolve)(n.ref_target, a))
                return;
        }
        fail(show(v) + (alts.size() > 1 ? " resolves to no " : " is not a ") + n.ref_target + " record");
    }

    void check_object(const Node &n, const Object &obj, bool patch)
    {
        if (deep())
            return;
        if (!patch)
            for (auto &name : n.required)
                if (!obj.contains(name)) {
                    fail_at(name, "is required");
                    if (stopped())
                        return;
                }
        for (auto &[name, value] : obj) {
            if (stopped())
                return;
            if (patch && value.is_null()) {
                if (n.is_required(name))
                    fail_at(name, "null removes a required field");
                continue;
            }
            int prop = n.property(name);
            if (prop >= 0) {
                path_.push_back(name);
                check(prop, value, patch);
                path_.pop_back();
            } else if (n.extra == extra_none) {
                fail_at(name, "is not a field of this record");
            } else if (n.extra >= 0) {
                path_.push_back(name);
                check(n.extra, value, patch);
                path_.pop_back();
            }
        }
    }

    void check_array(const Node &n, const Array &arr, bool patch)
    {
        if (deep())
            return;
        if (!patch || !n.keyed) {
            if (n.min_items >= 0 && static_cast<std::int64_t>(arr.size()) < n.min_items)
                fail("has " + std::to_string(arr.size()) + " items, fewer than " + std::to_string(n.min_items));
            if (n.max_items >= 0 && static_cast<std::int64_t>(arr.size()) > n.max_items)
                fail("has " + std::to_string(arr.size()) + " items, more than " + std::to_string(n.max_items));
        }
        if (n.unique) {
            std::unordered_map<std::string, std::size_t> seen;
            for (std::size_t i = 0; i < arr.size() && !stopped(); ++i) {
                auto [it, fresh] = seen.emplace(canonical(arr[i]), i);
                if (!fresh)
                    fail_at(std::to_string(i), "repeats item " + std::to_string(it->second));
            }
        }
        if (n.keyed) {
            check_keyed(n, arr, patch);
            return;
        }
        if (n.items < 0)
            return;
        for (std::size_t i = 0; i < arr.size() && !stopped(); ++i) {
            path_.push_back(std::to_string(i));
            check(n.items, arr[i], false);
            path_.pop_back();
        }
    }

    void check_keyed(const Node &n, const Array &arr, bool patch)
    {
        std::unordered_map<std::string, std::size_t> seen;
        for (std::size_t i = 0; i < arr.size() && !stopped(); ++i) {
            const Value &item = arr[i];
            std::string index = std::to_string(i);
            auto *obj = item.if_object();
            if (!obj) {
                fail_at(index, std::string("is ") + type_of(item) + "; a keyed list holds objects keyed by " + n.key_text);
                continue;
            }
            std::string missing;
            for (auto &f : n.key_fields) {
                auto *fv = obj->find(f);
                if (!fv || fv->is_null())
                    missing += (missing.empty() ? "" : ", ") + f;
            }
            if (!missing.empty()) {
                fail_at(index, "has no key field " + missing);
                continue;
            }
            std::string key;
            for (auto &f : n.key_fields) {
                if (!key.empty())
                    key += ',';
                key += f + "=" + canonical(*obj->find(f));
            }
            auto [it, fresh] = seen.emplace(key, i);
            if (!fresh)
                fail_at(index, "repeats key " + key + " of item " + std::to_string(it->second));
            std::string seg = index;
            if (o_.keyed_paths) {
                std::string ks = swofty::json::key_segment(item, n.key_fields);
                if (!ks.empty())
                    seg = std::move(ks);
            }
            if (auto *del = obj->find("$delete")) {
                path_.push_back(seg);
                if (!patch) {
                    fail_at("$delete", "is only allowed in a layer");
                } else if (!(del->is_bool() && del->as_bool())) {
                    fail_at("$delete", "must be true");
                } else {
                    for (auto &[k, val] : *obj) {
                        (void)val;
                        if (k != "$delete" && std::find(n.key_fields.begin(), n.key_fields.end(), k) == n.key_fields.end()) {
                            fail("a $delete element holds only its key fields");
                            break;
                        }
                    }
                }
                path_.pop_back();
                continue;
            }
            if (n.items >= 0) {
                path_.push_back(seg);
                check(n.items, item, patch);
                path_.pop_back();
            }
        }
    }
};

} // namespace schema_detail

Schema::Schema() : impl_(std::make_unique<Impl>()) {}
Schema::Schema(Schema &&) noexcept = default;
Schema &Schema::operator=(Schema &&) noexcept = default;
Schema::~Schema() = default;

std::optional<Schema> Schema::compile(const swofty::json::Value &doc, Error *err)
{
    Schema s;
    Impl &m = *s.impl_;
    Compiler c(doc, m);
    std::vector<std::string> path;
    int root = c.compile(doc, path);
    if (root < 0) {
        if (err)
            *err = c.error();
        return std::nullopt;
    }
    m.root = root;
    if (auto *o = doc.if_object()) {
        if (auto *v = o->find("x-registry"); v && v->is_string())
            m.registry = std::string(v->as_string());
        if (auto *v = o->find("x-applies"); v && v->is_string())
            m.applies = std::string(v->as_string());
        if (auto *v = o->find("x-adapter"); v && v->is_string())
            m.adapter = std::string(v->as_string());
        if (auto *v = o->find("x-snapshot"); v && v->is_bool())
            m.snapshot = v->as_bool();
        if (auto *v = o->find("x-snapshot-budget"); v && v->is_int() && v->as_int() >= 0)
            m.budget = v->as_int();
        if (auto *v = o->find("x-version")) {
            if (auto *str = v->if_string()) {
                auto dot = str->find('.');
                auto digits = [](std::string_view t) {
                    return !t.empty() && t.size() < 9 && std::all_of(t.begin(), t.end(), [](char ch) { return ch >= '0' && ch <= '9'; }) &&
                           (t.size() == 1 || t[0] != '0');
                };
                if (dot != std::string::npos && digits(std::string_view(*str).substr(0, dot)) &&
                    digits(std::string_view(*str).substr(dot + 1)))
                    m.version = {std::stoi(str->substr(0, dot)), std::stoi(str->substr(dot + 1))};
            } else if (v->is_object() && v->size() == 2) {
                auto &mj = (*v)["major"];
                auto &mn = (*v)["minor"];
                if (is_integer(mj) && is_integer(mn) && mj.as_int() >= 0 && mn.as_int() >= 0 && mj.as_int() < (1 << 30) &&
                    mn.as_int() < (1 << 30))
                    m.version = {static_cast<int>(mj.as_int()), static_cast<int>(mn.as_int())};
            }
        }
    }
    collect_keys(m.nodes, m.root, "", 0, m.keyed);
    for (auto &[p, f] : m.keyed)
        m.keys.add(p, f);
    return s;
}

std::optional<Schema> Schema::compile(std::string_view text, Error *err)
{
    swofty::json::Error perr;
    auto doc = swofty::json::parse(text, &perr);
    if (!perr.ok()) {
        if (err)
            *err = {"/", "not JSON: line " + std::to_string(perr.line) + " column " + std::to_string(perr.column) + ": " + perr.message};
        return std::nullopt;
    }
    return compile(doc, err);
}

Schema::Result Schema::validate(const swofty::json::Value &value, const Options &options) const
{
    Result r;
    Sink main;
    main.out = &r.errors;
    main.limit = options.max_errors;
    Run run(*impl_, options, main);
    run.check(impl_->root, value, options.mode == Mode::patch);
    r.truncated = main.over;
    if (run.aborted())
        r.errors.push_back(run.abort_error());
    else if (r.errors.empty() && main.over)
        r.errors.push_back({"/", "is invalid (no room was asked for the errors)"});
    return r;
}

Schema::Result Schema::validate(const swofty::json::Value &value, Mode mode) const
{
    Options o;
    o.mode = mode;
    return validate(value, o);
}

bool Schema::valid(const swofty::json::Value &value, Mode mode) const
{
    Options o;
    o.mode = mode;
    o.max_errors = 0;
    Sink main;
    main.limit = 0;
    Run run(*impl_, o, main);
    run.check(impl_->root, value, mode == Mode::patch);
    return !main.over && !run.aborted();
}

const std::string &Schema::registry() const noexcept
{
    return impl_->registry;
}

Schema::Version Schema::version() const noexcept
{
    return impl_->version;
}

const std::string &Schema::applies() const noexcept
{
    return impl_->applies;
}

bool Schema::snapshot() const noexcept
{
    return impl_->snapshot;
}

std::int64_t Schema::snapshot_budget() const noexcept
{
    return impl_->budget;
}

const std::string &Schema::adapter() const noexcept
{
    return impl_->adapter;
}

const swofty::json::Keys &Schema::keys() const noexcept
{
    return impl_->keys;
}

const std::vector<std::pair<std::string, std::vector<std::string>>> &Schema::keyed_lists() const noexcept
{
    return impl_->keyed;
}

bool is_record_id(std::string_view id)
{
    return schema_detail::record_id_ok(id);
}

bool is_record_key(std::string_view key)
{
    if (!key.empty() && key.back() == '/')
        return false;
    if (key.find("//") != std::string_view::npos)
        return false;
    std::string_view body = key;
    if (!body.empty() && body.back() == '\n')
        body.remove_suffix(1);
    std::u32string cps;
    schema_detail::decode(body, cps);
    if (cps.empty())
        return false;
    for (std::size_t i = 0; i < cps.size(); ++i) {
        char32_t c = cps[i];
        if (schema_detail::py_space(c) || c == '?' || c == '|' || c == '*')
            return false;
        if (i == 0 && (c == '/' || c == '$'))
            return false;
    }
    return true;
}

std::string pointer_join(std::string_view base, std::string_view rel)
{
    if (rel.empty() || rel == "/")
        return std::string(base.empty() ? "/" : base);
    if (base.empty() || base == "/")
        return std::string(rel);
    return std::string(base) + std::string(rel);
}

bool DataReport::ok() const noexcept
{
    if (!problems.empty() || !missing.empty())
        return false;
    for (auto &[v, errs] : by_version)
        if (!errs.empty())
            return false;
    return true;
}

std::vector<Schema::Error> *DataReport::errors_of(std::string_view version)
{
    for (auto &[v, errs] : by_version)
        if (v == version)
            return &errs;
    return nullptr;
}

void DataReport::touch(std::string_view version)
{
    if (!errors_of(version))
        by_version.emplace_back(std::string(version), std::vector<Schema::Error>{});
}

namespace {

bool data_key(std::string_view key, std::string &name, int &major)
{
    if (!key.empty() && key.back() == '\n')
        key.remove_suffix(1);
    auto at = key.find('@');
    std::string_view n = key.substr(0, at);
    if (n.empty() || !((n[0] >= 'a' && n[0] <= 'z') || (n[0] >= '0' && n[0] <= '9')))
        return false;
    for (char c : n)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-'))
            return false;
    major = 0;
    if (at != std::string_view::npos) {
        std::string_view m = key.substr(at + 1);
        if (m.empty() || m[0] < '1' || m[0] > '9' || m.size() > 9)
            return false;
        for (char c : m)
            if (c < '0' || c > '9')
                return false;
        major = std::stoi(std::string(m));
    }
    name = std::string(n);
    return true;
}

std::string at_pointer(const std::vector<std::string> &parts)
{
    return pointer_of(parts);
}

void add_errors(DataReport &report, const std::string &version, const std::vector<std::string> &path, Schema::Result &r)
{
    std::string base = path.empty() ? std::string() : at_pointer(path);
    report.truncated = report.truncated || r.truncated;
    auto *errs = report.errors_of(version);
    for (auto &e : r.errors)
        errs->push_back({path.empty() ? e.pointer : pointer_join(base, e.pointer), std::move(e.message)});
}

void validate_plain(const swofty::json::Value &record, const DataSchemas &versions, const std::vector<std::string> &path,
                    DataReport &report, bool patch, const DataOptions &options)
{
    for (auto &[version, schema] : versions.versions) {
        report.touch(version);
        if (!schema)
            continue;
        Schema::Options o;
        o.mode = patch ? Schema::Mode::patch : Schema::Mode::records;
        o.max_errors = options.max_errors;
        o.resolve = options.resolve;
        auto r = schema->validate(record, o);
        add_errors(report, version, path, r);
    }
}

enum class Effective { ok, unknown, loop, deep };

constexpr std::size_t max_bases = 256;

Effective effective(const Schema &schema, std::string_view registry, const swofty::json::Value &record,
                    std::vector<std::string> &seen, const BaseResolver &resolve, swofty::json::Value &out, std::string &chain)
{
    auto *o = record.if_object();
    if (!o)
        return Effective::unknown;
    swofty::json::Value own = record;
    own.erase("extends");
    own.erase("$protect");
    auto *ext = o->find("extends");
    if (!ext || ext->is_null()) {
        out = std::move(own);
        return Effective::ok;
    }
    if (!ext->is_string())
        return Effective::unknown;
    std::string id(ext->as_string());
    if (std::find(seen.begin(), seen.end(), id) != seen.end()) {
        for (auto &s : seen)
            chain += s + " -> ";
        chain += id;
        return Effective::loop;
    }
    if (seen.size() > max_bases)
        return Effective::deep;
    const swofty::json::Value *base = resolve(registry, id);
    if (!base)
        return Effective::unknown;
    seen.push_back(id);
    swofty::json::Value base_value;
    auto r = effective(schema, registry, *base, seen, resolve, base_value, chain);
    seen.pop_back();
    if (r != Effective::ok)
        return r;
    swofty::json::Error merr;
    if (!swofty::json::merge_patch(base_value, own, schema.keys(), &merr))
        return Effective::unknown;
    out = std::move(base_value);
    return Effective::ok;
}

void validate_variant(const swofty::json::Value &record, std::string_view registry, const DataSchemas &versions,
                      const std::vector<std::string> &path, DataReport &report, const DataOptions &options)
{
    const swofty::json::Value &ext = *record.find("extends");
    auto at_extends = path;
    at_extends.push_back("extends");
    if (!(ext.is_string() && is_record_id(ext.as_string())))
        report.problems.push_back({at_pointer(at_extends), "is the full id of the base record (<namespace>/<key>)"});
    swofty::json::Value patch = record;
    patch.erase("extends");
    validate_plain(patch, versions, path, report, true, options);
    if (!options.base || !*options.base || !ext.is_string())
        return;
    std::string id(ext.as_string());
    for (auto &[version, schema] : versions.versions) {
        if (!schema)
            continue;
        std::vector<std::string> seen{at_pointer(path)};
        swofty::json::Value merged;
        std::string chain;
        auto r = effective(*schema, registry, record, seen, *options.base, merged, chain);
        if (r == Effective::loop) {
            report.problems.push_back({at_pointer(at_extends), "extends loops: " + chain});
            return;
        }
        if (r == Effective::deep) {
            report.problems.push_back({at_pointer(at_extends), "extends goes through more than " + std::to_string(max_bases) + " bases"});
            return;
        }
        if (r != Effective::ok)
            continue;
        Schema::Options o;
        o.max_errors = options.max_errors;
        o.resolve = options.resolve;
        auto whole = schema->validate(merged, o);
        report.truncated = report.truncated || whole.truncated;
        auto *errs = report.errors_of(version);
        std::size_t before = errs->size();
        std::string base = path.empty() ? std::string() : at_pointer(path);
        for (auto &e : whole.errors) {
            std::string where = path.empty() ? e.pointer : pointer_join(base, e.pointer);
            bool known = false;
            for (std::size_t i = 0; i < before && !known; ++i)
                known = (*errs)[i].pointer == where && (*errs)[i].message == e.message;
            if (!known)
                errs->push_back({std::move(where), e.message + " (merged onto " + id + ")"});
        }
    }
}

void validate_record(const swofty::json::Value &record, std::string_view registry, const DataSchemas &versions,
                     const std::vector<std::string> &path, DataReport &report, bool patch, const DataOptions &options)
{
    if (!patch && record.is_object() && record.contains("extends"))
        validate_variant(record, registry, versions, path, report, options);
    else
        validate_plain(record, versions, path, report, patch, options);
}

void validate_select(const swofty::json::Value &entries, std::string_view registry, const DataSchemas &versions,
                     std::vector<std::string> &path, DataReport &report, const DataOptions &options)
{
    auto *list = entries.if_array();
    if (!list || list->empty()) {
        report.problems.push_back({at_pointer(path), "$select is a non-empty list of selector entries"});
        return;
    }
    static const std::set<std::string_view> select_keys{"ids", "where", "tag", "in", "when", "except", "patch"};
    for (std::size_t i = 0; i < list->size(); ++i) {
        path.push_back(std::to_string(i));
        auto &entry = (*list)[i];
        if (auto *o = entry.if_object()) {
            std::vector<std::string> unknown;
            for (auto &[k, v] : *o) {
                (void)v;
                if (!select_keys.count(k))
                    unknown.push_back(k);
            }
            if (!unknown.empty()) {
                std::sort(unknown.begin(), unknown.end());
                std::string text;
                for (auto &u : unknown)
                    text += (text.empty() ? "" : ", ") + swofty::json::dump(swofty::json::Value(u));
                report.problems.push_back({at_pointer(path), "unknown selector fields [" + text + "]"});
            }
            if (!o->contains("ids") && !o->contains("where") && !o->contains("tag"))
                report.problems.push_back({at_pointer(path), "a selector entry has ids, where or tag"});
            auto *patch = o->find("patch");
            path.push_back("patch");
            if (!patch || !patch->is_object())
                report.problems.push_back({at_pointer(path), "a selector entry has a patch object"});
            else
                validate_record(*patch, registry, versions, path, report, true, options);
            path.pop_back();
        } else {
            report.problems.push_back({at_pointer(path), "a selector entry is an object"});
        }
        path.pop_back();
    }
}

const swofty::json::Value &strip_protect_into(const swofty::json::Value &record, const std::vector<std::string> &path,
                                              DataReport &report, swofty::json::Value &scratch)
{
    auto *o = record.if_object();
    if (!o || !o->contains("$protect"))
        return record;
    auto &protect = *o->find("$protect");
    bool good = protect.is_array();
    if (good)
        for (auto &p : *protect.if_array())
            if (!p.is_string() || p.as_string().empty())
                good = false;
    if (!good) {
        auto at = path;
        at.push_back("$protect");
        report.problems.push_back({pointer_of(at), "is a list of field names"});
    }
    scratch = record;
    scratch.erase("$protect");
    return scratch;
}

} // namespace

DataReport check_data(const swofty::json::Value &doc, const DataLookup &lookup, const DataOptions &options)
{
    DataReport report;
    auto *top = doc.if_object();
    if (!top) {
        report.problems.push_back({"/", "a data file is an object keyed by registry name"});
        return report;
    }
    for (auto &[key, records] : *top) {
        std::vector<std::string> path{key};
        std::string name;
        int major = 0;
        if (!data_key(key, name, major)) {
            report.problems.push_back(
                {pointer_of(path), swofty::json::dump(swofty::json::Value(key)) + " is not a registry name (<name> or <name>@<major>)"});
            continue;
        }
        if (!options.only.empty() && name != options.only) {
            report.problems.push_back({pointer_of(path), "an example of " + options.only + " names registry " + name});
            continue;
        }
        DataSchemas versions = lookup(name, major);
        if (versions.versions.empty()) {
            if (options.skip_unknown && versions.missing.empty())
                continue;
            report.missing.push_back({pointer_of(path), versions.missing.empty() ? "no schema for registry " + name : versions.missing});
            continue;
        }
        auto *by_key = records.if_object();
        if (!by_key) {
            report.problems.push_back({pointer_of(path), "holds an object of records by key"});
            continue;
        }
        for (auto &[rk, value] : *by_key) {
            path.push_back(rk);
            if (options.layers) {
                if (rk == "$select") {
                    validate_select(value, name, versions, path, report, options);
                } else if (!is_record_id(rk)) {
                    report.problems.push_back(
                        {pointer_of(path), swofty::json::dump(swofty::json::Value(rk)) + " is not a full record id (<namespace>/<key>)"});
                } else if (!value.is_object()) {
                    report.problems.push_back({pointer_of(path), "a layer is an object (a JSON Merge Patch)"});
                } else {
                    validate_record(value, name, versions, path, report, true, options);
                }
            } else if (!is_record_key(rk)) {
                report.problems.push_back({pointer_of(path), swofty::json::dump(swofty::json::Value(rk)) +
                                                                 " is not a record key (no spaces, '?', '|' or '*', no leading '/' or '$', no empty part)"});
            } else {
                swofty::json::Value scratch;
                const auto &record = strip_protect_into(value, path, report, scratch);
                validate_record(record, name, versions, path, report, false, options);
            }
            path.pop_back();
        }
    }
    return report;
}

DataReport check_record(const swofty::json::Value &record, std::string_view registry, const DataSchemas &versions,
                        const DataOptions &options)
{
    DataReport report;
    if (versions.versions.empty()) {
        report.missing.push_back({"/", versions.missing});
        return report;
    }
    std::vector<std::string> path;
    swofty::json::Value scratch;
    const auto &r = strip_protect_into(record, path, report, scratch);
    validate_record(r, registry, versions, path, report, options.layers, options);
    return report;
}

} // namespace swofty_core::registry
