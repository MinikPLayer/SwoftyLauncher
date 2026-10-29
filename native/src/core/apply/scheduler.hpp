#pragma once

#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "applier.hpp"
#include "ids.hpp"

namespace swofty_core::apply {

enum class Moment { boot, activate, deactivate, session_load, session_end, read };

const char *moment_name(Moment m);

struct Totals {
    int appliers = 0, applied = 0, put_back = 0, refused = 0;
};

struct SessionFacts {
    std::string mode, type, session, track;
};

class Scheduler {
public:
    explicit Scheduler(Store &store);

    void set_log(std::function<void(const std::string &)> fn);
    void set_facts(std::function<void(const Context &)> fn);

    std::size_t plan_boot();
    std::size_t planned_keys() const;
    std::size_t planned_keys(const std::string &registry) const;
    bool boot_work() const;
    bool boot_planned() const;
    Totals run_boot();
    bool booted() const;

    Totals career_loaded(const std::string &career, const std::string &mode, const std::string &data_group);
    Totals career_season(int year);
    Totals career_unloaded();
    Totals content_picked(const std::string &entrant_list);
    Totals content_picked(const std::string &entrant_list, const std::string &mode);
    Totals activate(const std::string &content, const std::string &why);
    Totals deactivate(const std::string &why);

    Totals session_load(const std::string &type, const std::string &track);
    Totals session_load(const SessionFacts &facts);
    std::string session_name() const;
    std::string facts_line() const;
    Totals session_end(const std::string &why);
    bool in_session() const;

    Totals install_read();

    Context context() const;
    std::string active() const;
    std::string find_package(const std::string &entrant_list);
    std::vector<std::string> chain_of(const std::string &registry, const std::string &id);

    void reset();

private:
    using Lock = std::unique_lock<std::recursive_mutex>;

    void publish(Lock &);
    Totals run(Moment m, Applies kind, const std::function<void(Applier &, Report &)> &call,
               std::vector<Applier *> *ran, bool reverse_order, const std::vector<Applier *> *only);
    void log(const std::string &line);
    bool has_addon_content(const std::string &registry);
    Totals activate_locked(Lock &, const std::string &content, const std::string &label, const std::string &why);
    Totals deactivate_locked(Lock &, const std::string &why);
    Totals session_end_locked(Lock &, const std::string &why);
    Totals content_picked_locked(Lock &, const std::string &entrant_list, const std::string *mode);
    void log_facts();
    void log_unapplied();

    Store &store_;
    mutable std::recursive_mutex mu_;
    std::function<void(const std::string &)> log_;
    std::function<void(const Context &)> facts_;
    Context ctx_;
    bool have_ctx_ = false;
    bool planned_ = false, booted_ = false, read_ = false, session_ = false;
    std::vector<std::pair<std::string, std::size_t>> keys_;
    bool work_ = false;
    std::string active_, active_label_;
    int career_year_ = 0;
    std::string session_name_;
    std::vector<Applier *> activated_, sessioned_;
    std::vector<std::string> boot_summary_;
};

Scheduler &scheduler();
void clear_appliers();
std::string summary(const Context &c);
int stat_id(Store &records, ids::Store &ids, const std::string &registry, const std::string &id, int64_t *out,
            const std::string &today, std::string *line);

}
