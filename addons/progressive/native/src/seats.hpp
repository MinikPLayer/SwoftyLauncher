// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Progressive Career: the career's entrant rows and the game's own writer for them. The write
// sequence is the Career Mode Editor's Driver Career one (addons/career-editor/native/src/
// drivers.cpp, protocol/drivers.md), copied so this addon stands alone:
//   rows       CareerEntity +0x2e0..+0x2e8, 0x88-byte EntrantRecords (swofty/game/entrants.hpp)
//   writer     Career_Set_EntrantData_Thunk(entity, identity, EntrantWriteData, position, number).
//              It finds the row by identity (AI rows by ordinal_flags, the human row by its bit),
//              posts the entity's entrant-change begin message before it searches and the end
//              message only on a match, so a miss leaves CareerEntity::entrant_update_busy set:
//              the identity must match exactly one row before the call.
//   moves      MessageDriverMoveConfirmed before each write that moves a driver (the Driver Career
//              signing 0x140d766b0 does it), so UpgradesComponent reassigns per-driver state.
//   finalise   Career_Build_LineupChanges against the line-up from before the batch, then
//              MessageDriverMovesFinalised_upgrades, _recognition and the plain one. Mandatory after
//              a driver move: the _upgrades handler makes the new driver's team modifiers
//              (without it Career_Calc_UpgradeCost crashed, 2026-10-20).
// Flow thread only (career.hub, career.loaded).

#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <swofty/game.hpp>

namespace pg {

using swofty::game::EntrantRecord;
using swofty::game::EntrantWriteData;

constexpr uint32_t kNoKey = 0x3fff0000u;
constexpr uint32_t kHumanBit = 0x10000000u;
constexpr uint8_t kNoNumber = 0xff;
constexpr size_t kMaxEntrants = 256;
constexpr size_t kIdentitySize = offsetof(EntrantRecord, id_lo);
constexpr uint8_t kReasonSigned = 0;
constexpr uint8_t kReasonReleased = 1;
constexpr uint8_t kReasonBenched = 3;
static_assert(sizeof(EntrantRecord) == 0x88 && sizeof(EntrantWriteData) == 0x2c);

struct Row {
    uintptr_t at = 0;
    EntrantRecord rec{};
};

uintptr_t entity();
uintptr_t contracts_manager();
uintptr_t championship(uintptr_t entity);

struct SeasonInfo {
    uint32_t index = 0;
    size_t seasons = 0;
    uint32_t completion = 0;
};
bool season(uintptr_t entity, SeasonInfo &out);

// The current season's calendar (Championship.seasons[season_index] -> Season.weekends, each
// Weekend.track_stat_id, the tracks.Track statId) and the next race: Season.current_weekend when
// that weekend has not started (Weekend.progress_state 0), else the one after it (tails' history
// engine reads the same). next is 1-based; weekends + 1 once the season's last race is done.
// At a season break the new season's weekends can exist before its current weekend does: then
// tracks are filled, current_ok is false and the caller decides the next race.
struct Calendar {
    std::vector<int> tracks;
    int next = 1;
    uint32_t current = 0;
    uint32_t progress = 0;
    bool current_ok = false;
    bool started = false;
};
bool calendar(uintptr_t entity, Calendar &out);

bool entrant_rows(uintptr_t entity, std::vector<Row> &out);
uint32_t driver_of(const EntrantRecord &r);
inline uint32_t stat_of(uint32_t key)
{
    return key >> 16;
}
inline bool none(uint32_t key)
{
    return (key & 0xffff0000u) == kNoKey;
}
inline bool has_car(const EntrantRecord &r)
{
    return !none(r.vehicle_ref);
}
inline bool teamless(const EntrantRecord &r)
{
    return none(r.team_stat_id);
}
inline bool human(const EntrantRecord &r)
{
    return (r.ordinal_flags & kHumanBit) != 0;
}
inline int position_of(const EntrantRecord &r)
{
    return (r.team_position >> 8) ? (r.team_position & 0xff) : -1;
}

const Row *row_of(const std::vector<Row> &rows, uint32_t driver);
bool entity_busy(uintptr_t entity);
bool unique_identity(uintptr_t entity, const Row &id_row);
EntrantWriteData own_data(const Row &r);
EntrantWriteData seat_data(const Row &driver_row, const Row &seat_row);
EntrantWriteData released_data(const Row &r);

bool announce(uintptr_t manager, uint32_t driver, uint32_t old_team, uint32_t new_team, uint8_t reason);
bool write_row(uintptr_t entity, const Row &id_row, const EntrantWriteData &data, uint16_t position, uint8_t number);

uintptr_t weekend_row(uintptr_t entity, const EntrantRecord &row);
bool patch(uintptr_t at, size_t offset, uint32_t value);
bool patch_u8(uintptr_t at, size_t offset, uint8_t value);

class Baseline {
  public:
    explicit Baseline(const std::vector<Row> &rows);
    Baseline(const Baseline &) = delete;
    Baseline &operator=(const Baseline &) = delete;
    const void *vector() const { return &vec_; }
    const std::vector<EntrantRecord> &rows() const { return rows_; }

  private:
    struct Vec {
        void *allocator = nullptr;
        const EntrantRecord *begin = nullptr;
        const EntrantRecord *end = nullptr;
        const EntrantRecord *capacity = nullptr;
    };
    std::vector<EntrantRecord> rows_;
    Vec vec_;
};

// Driver Career contracts (DriverCareerContractsManagerComponent +0x350 owner, a DriverContract per
// row). A driver who races for a team without an active contract for it signs one the way the game's
// signing (0x140d77a70..0x140d77cbb) adds a contract: the terms are built for him and the team by the
// game's Career_Build_DriverContract (type 0, one season, the negotiation screen's recommended
// target from the manager's rating ranges +0x318), as the contract screen builds an offer; his own
// contract ends (state 4 same team, 5 another team), the terms are copied in, set active and
// announced with MessageDriverContractChanged. A new career has no active contract to copy terms
// from, which is why they are built. A driver taken off the grid has his contract released (state
// 3), as the game's replace does.
constexpr size_t kContractRecord = offsetof(swofty::game::DriverContract, active);

struct Signing {
    uint32_t driver = 0;
    uintptr_t contract = 0;
    uint8_t offer[kContractRecord] = {};
};

bool contract_objects(uintptr_t manager, std::vector<uintptr_t> &out);
uintptr_t contract_of(const std::vector<uintptr_t> &objects, uint32_t driver);
bool contract_fits(uintptr_t contract, uint32_t team);
const char *plan_signing(uintptr_t manager, const std::vector<uintptr_t> &objects, uint32_t driver, uint32_t team,
                         Signing &s);
bool sign_contract(uintptr_t entity, uintptr_t manager, const Signing &s);
bool release_contract(uintptr_t entity, uintptr_t manager, uint32_t driver, bool &found);

// The contract the game cached for a driver's next season (manager +0x408..+0x410, 0x38-byte
// entries: the 0x2c contract record with its team at +0x00, the driver key's kind at +0x2c and stat
// at +0x2e), which the Driver Career season processor 0x140d766b0 applies at the season change. Read
// only; the team key, or kNoKey when the driver has none.
uint32_t cached_contract_team(uintptr_t manager, uint32_t driver);

// Career_DriverNumbers_CheckExisting(CarNumberComponent, driver): the game's signings give a seated
// row without a number a free one after its entrant write.
uintptr_t car_numbers(uintptr_t entity, uintptr_t manager);

// The game copies the line-up into each weekend (+0x18 vector object, rows +0x20..+0x28) and each of
// its sessions (Session participants +0x20..+0x28) when it builds them, and a season's standings
// are initialised from its first session's participants (Career_Rebuild_SeasonStandings passes
// &weekend[0].sessions[0] + 0x18). Copies made before a season apply hold the old seats: mirror
// writes each career row's seat fields (look, team, car keys, seat value, position, number) into the
// copy row of the same driver (matched as the game matches rows: the human bit, else ordinal_flags,
// and the same driver key). A copy that holds only the race seats has no row for a driver the apply
// seated: his career row is copied over the row of a driver who left the grid. Returns false when
// the vector does not look like a line-up copy.
struct Mirror {
    size_t rows = 0, matched = 0, changed = 0, replaced = 0, race_before = 0, race_after = 0;
};
bool mirror_rows(uintptr_t begin_field, const std::vector<Row> &career, Mirror &out);

uint64_t lineup_changes(uintptr_t entity, const Baseline &baseline);
bool finalise(uintptr_t entity, const Baseline &baseline, uint64_t &changes);

} // namespace pg
