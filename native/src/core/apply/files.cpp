#include "files.hpp"
#include "files_api.hpp"

#include "../registry/table.hpp"
#include "../runtime.hpp"

#include <swofty/mixin.hpp>

#include <mutex>

namespace swofty_core::apply::files {

namespace {

std::recursive_mutex mu;

Sink core_sink()
{
    Sink s;
    std::string prefix = runtime::file_route_prefix();
    s.prefix = prefix.empty() ? std::string("swofty/") : prefix;
    s.exists = [](const std::string &target) {
        uint64_t size = 0;
        return runtime::file_size(runtime::under_swofty(target), &size);
    };
    s.in_game = [](const std::string &game_path) {
        std::string p = game_path;
        std::replace(p.begin(), p.end(), '/', '\\');
        uint64_t size = 0;
        return runtime::file_size(runtime::game_root() + L"\\" + runtime::wide(p), &size);
    };
    s.route = [](const std::string &game_path, const Claim *winner, std::string &why) {
        if (!winner)
            return runtime::file_route(game_path, nullptr, {}, why);
        return runtime::file_route(game_path, &winner->target, winner->registry + " " + winner->id, why);
    };
    return s;
}

Router &router()
{
    static Router r;
    static std::once_flag once;
    std::call_once(once, [] { r.set_sink(core_sink()); });
    return r;
}

}

Outcome redirect(const std::string &game_path, const std::string &addon, const std::string &file,
                 const std::string &registry, const std::string &id, const std::string &scope)
{
    std::lock_guard g(mu);
    Router &r = router();
    r.set_order(registry::store().order());
    Outcome o = r.redirect(game_path, addon, file, registry, id, scope);
    swofty::log("[registry] file: %s", o.line.c_str());
    if (!o.conflict.empty())
        swofty::log("[registry] %s", o.conflict.c_str());
    return o;
}

std::vector<std::string> put_back(const std::string &scope)
{
    std::lock_guard g(mu);
    Router &r = router();
    r.set_order(registry::store().order());
    auto lines = r.put_back(scope);
    for (auto &l : lines)
        swofty::log("[registry] file: %s", l.c_str());
    return lines;
}

std::string winner(const std::string &game_path)
{
    std::lock_guard g(mu);
    Normal p = files::game_path(game_path);
    const Claim *w = p.ok() ? router().winner(p.path) : nullptr;
    return w ? w->addon : std::string();
}

void report(Value &conflicts)
{
    std::lock_guard g(mu);
    Value found = router().conflicts_json();
    if (!found.is_array() || !found.size())
        return;
    Value &list = conflicts["registry"];
    if (!list.is_array())
        list = Value::array();
    for (std::size_t i = 0; i < found.size(); i++)
        list.push_back(found[i]);
}

}
