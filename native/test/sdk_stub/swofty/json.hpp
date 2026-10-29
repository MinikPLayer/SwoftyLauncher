// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include <string_view>

// Test-only stand-in for swofty/json.hpp (stream W1 ships the real one): the surface the 1.6 headers
// use, which is json::Value as an opaque value, json::parse and json::dump. Not shipped.

namespace swofty::json {

class Value {
public:
    Value() = default;
    explicit Value(std::string text) : text_(std::move(text)) {}
    const std::string &text() const { return text_; }

private:
    std::string text_;
};

inline Value parse(std::string_view text) { return Value(std::string(text)); }
inline std::string dump(const Value &v, int = -1) { return v.text().empty() ? "null" : v.text(); }

}
