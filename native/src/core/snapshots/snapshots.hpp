#pragma once
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "../registry/store.hpp"
#include "format.hpp"

// Career snapshots (docs/std/scopes.md#career-snapshots): the swofty_registry entry in memory, the
// freezes at career creation and season start, and the reads a loaded career makes through the
// registry store's snapshot source. No game calls: runtime.cpp feeds it the save splice's bytes and
// the career moments, and the host test drives it directly.
//
// Locking: Book never calls into the store while it holds its own lock, because the store calls
// read() with the store's lock held.

namespace swofty_core::snapshots {

namespace reg = swofty_core::registry;
using format::Bytes;

struct Stats {
    size_t registries = 0, records = 0, raw = 0, compressed = 0, refused = 0;
};

// The stat-id store's career side (W6, apply/ids.hpp through apply::moments): the career's map as a
// JSON object {registry: {record: stat id}} for its entry, and the check of a loaded career's saved
// map against this install, which returns one line per clash. Unset: an empty map and no check.
struct Ids {
    std::function<std::string(const std::string &career)> map;
    std::function<std::vector<std::string>(const std::string &career, const std::string &map_json)> adopt;
};

class Book {
public:
    explicit Book(reg::Store &store, std::function<void(const std::string &)> log = {});
    ~Book();

    void set_ids(Ids ids);

    // Profile load: the body the save splice kept for swofty_registry, or null for a save without it.
    void loaded(const Bytes *body);
    // Profile save: the body to write; false when nothing changed since the last write, or the entry
    // read at load is one this core must not overwrite (a newer entry format).
    bool saving(Bytes &body);
    // The splice wrote the body (or failed to); a failed write keeps the changes for the next save.
    void written(bool ok);
    // Test hook: the next saving() writes an entry with a filler item of that many bytes (0: off), due even
    // when nothing changed; one write only. Set by runtime.cpp from test_pad_bytes.
    void set_test_pad(size_t bytes);

    void career_loaded(const std::string &id, reg::Context ctx);
    void career_unloaded();
    void career_created(const std::string &id, format::Freeze freeze, reg::Context ctx);
    void season_start(int year, reg::Context ctx);

    reg::Frozen read(const std::string &registry, const std::string &id, const reg::Context &ctx);

    // The store's snapshot source, bound to this book.
    reg::SnapshotSource source();

    std::string active() const;
    bool has_career(const std::string &id) const;
    // A per-season career whose entry already holds a snapshot for that year: its season started before.
    bool started(const std::string &id, int year) const;
    // The career's entry keyed from under to (a save slot id for an entry from before it); false when from
    // has no entry or to already has one.
    bool rename(const std::string &from, const std::string &to);
    format::Entry entry() const;
    std::string state() const;

private:
    struct Cache;
    reg::Store &store_;
    std::function<void(const std::string &)> log_;
    mutable std::mutex mu_;
    format::Entry entry_;
    format::Decoded loaded_state_ = format::Decoded::ok;
    bool have_entry_ = false;
    size_t test_pad_ = 0;
    std::set<std::string> dirty_, pending_;
    std::string active_;
    std::unique_ptr<Cache> cache_;
    Ids ids_;

    std::string idmap(const std::string &career) const;

    void note(const std::string &s) const;
    void refresh();
    std::vector<format::Frozen> freeze(int year, const reg::Context &ctx, bool yearless, bool only_year,
                                       const format::Career *have, std::vector<std::string> &refused,
                                       std::vector<std::pair<std::string, std::string>> &owners, Stats &st);
};

// SWOFTY_TEST_SNAPSHOT_PAD: the filler size in bytes, honoured only when the test agent is armed (a live
// `swofty test` run) and the value is a plain decimal between 1 byte and the entry cap minus 64 KB; 0 otherwise.
size_t test_pad_bytes(const char *text, bool agent_armed);

// A record's season: its year (career::Season::year), the year of the season it names
// (career::DriverRatings::season), or a year(...) selector in its within. {0, 0}: no year.
std::pair<int, int> years_of(reg::Store &store, const std::string &registry, const swofty::json::Value &v,
                             const reg::Context &ctx);

}
