#include "validate.hpp"

#include "schema.hpp"
#include "store.hpp"

#include <map>
#include <memory>
#include <mutex>

namespace swofty_core::registry {

namespace {

class SchemaValidator final : public Validator {
public:
    bool validate(const Def &def, const swofty::json::Value &value, std::string &error) override
    {
        std::lock_guard g(mu_);
        auto &slot = compiled_[def.name];
        if (!slot.first || slot.second != def.schema) {
            Schema::Error e;
            auto s = Schema::compile(std::string_view(def.schema), &e);
            slot.first = s ? std::make_shared<Schema>(std::move(*s)) : nullptr;
            slot.second = def.schema;
            if (!s)
                broken_[def.name] = e.pointer + " " + e.message;
        }
        if (!slot.first)
            return true;
        static const Schema::RefResolver any = [](std::string_view, std::string_view) { return true; };
        Schema::Options o;
        o.keyed_paths = true;
        o.max_errors = 4;
        o.resolve = &any;
        auto r = slot.first->validate(value, o);
        if (r.ok())
            return true;
        error = r.errors.front().pointer + " " + r.errors.front().message;
        return false;
    }

private:
    std::mutex mu_;
    std::map<std::string, std::pair<std::shared_ptr<Schema>, std::string>> compiled_;
    std::map<std::string, std::string> broken_;
};

}

Validator &schema_validator()
{
    static SchemaValidator v;
    return v;
}

}
