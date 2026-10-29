#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "../registry/store.hpp"

namespace swofty_core::apply {

using registry::Applies;
using registry::Context;
using registry::Store;

struct Report {
    int applied = 0;
    int put_back = 0;
    int refused = 0;
    std::vector<std::string> lines;
    // A few words after the registry's count in the boot line, "[registry] applied at boot: engine_sound 1 (<summary>)".
    std::string summary;
};

// One per registry the core applies to the game. The scheduler (native/src/core/apply/scheduler.cpp)
// calls each applier at the moments its registry's `applies` names, on the flow thread unless noted,
// and logs "[apply] <registry> <moment>: N applied, M put back, K refused" plus every line in the report.
class Applier {
public:
    virtual ~Applier() = default;

    virtual const char *registry() const = 0;
    virtual Applies applies() const = 0;

    // Before the game database loads (Applies::boot): how many new database keys this registry's
    // effective records will add, for the value-map sizing. Called once, after the register pass.
    virtual std::size_t database_keys(Store &) { return 0; }

    // Before the game database loads (Applies::boot), after sizing: put the records into the
    // database in memory, or into the asset manifest.
    virtual void boot(Store &, Report &) {}

    // Applies::content: a box or career became active / stopped being active. deactivate puts back
    // everything activate changed, in reverse order, and is called before the next activate.
    virtual void activate(Store &, const Context &, Report &) {}
    virtual void deactivate(Store &, Report &) {}

    // Applies::session: at each session load, and when the session ends.
    virtual void session_load(Store &, const Context &, Report &) {}
    virtual void session_end(Store &, Report &) {}

    // Applies::read: install the read path once at the window stage. Reads go through the store
    // with the current context from then on.
    virtual void install_read(Store &, Report &) {}
};

void add(std::unique_ptr<Applier> applier);
std::vector<Applier *> appliers();

template <class T, class... Args> T &make(Args &&...args)
{
    auto p = std::make_unique<T>(static_cast<Args &&>(args)...);
    T &ref = *p;
    add(std::move(p));
    return ref;
}

}
