#pragma once

#include <swofty/json.hpp>

#include <string>

namespace swofty_core::registry {

struct Def;

class Validator {
public:
    virtual ~Validator() = default;
    virtual bool validate(const Def &def, const swofty::json::Value &value, std::string &error) = 0;
};

class PassThrough final : public Validator {
public:
    bool validate(const Def &, const swofty::json::Value &, std::string &) override { return true; }
};

Validator &default_validator();
Validator &schema_validator();

}
