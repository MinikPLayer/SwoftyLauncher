#include "runtime.hpp"

#include <windows.h>

#include <swofty/mixin.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// Track Acclimatisation guard (re/db/functions.tsv Game_PracticeTrackAcclimatisation_RefreshState).
//
// The Track Acclimatisation practice programme copies its targets (0x370-byte corner entries at self+0x350..+0x358)
// from the track's TrackAcclimatisationResource (Game_PracticeTrackAcclimatisation_CopyTargets). A track without
// that resource's CornerData, such as an imported community track, leaves the vector empty and null. The
// programme's start and its other slots return when the vector is empty, but the refresh indexes the current entry
// without that check, and the practice programmes' drive-out handler runs it for every programme as the car leaves
// the garage: ACCESS_VIOLATION reading 0x4 at F1_25.exe+0x299e59b.
//
// The guard returns from the refresh when the vector is empty, as the programme's start does. A programme with
// targets runs the game's code unchanged.
//
// SWOFTY_ACCLIM_GUARD=0 turns it off.

namespace {

constexpr const char *MIXIN = "swofty_core.dll (track acclimatisation guard)";
constexpr const char *TARGET = "Game_PracticeTrackAcclimatisation_RefreshState";
constexpr size_t TARGETS_BEGIN = 0x350;
constexpr size_t TARGETS_END = 0x358;
constexpr int LOG_FIRST = 4;

using Refresh = void(void *);
Refresh *orig_refresh = nullptr;
bool offered = false, settled = false, wanted = false;
std::atomic<int> skipped{0};

void refresh(void *self)
{
    uintptr_t begin = 0, end = 0;
    memcpy(&begin, static_cast<const uint8_t *>(self) + TARGETS_BEGIN, sizeof begin);
    memcpy(&end, static_cast<const uint8_t *>(self) + TARGETS_END, sizeof end);
    if (begin != end) {
        orig_refresh(self);
        return;
    }
    int n = ++skipped;
    if (n > LOG_FIRST)
        return;
    swofty::log("[acclim] Track Acclimatisation programme 0x%llx has no targets at this track (targets 0x%llx); "
                "refresh skipped%s",
                (unsigned long long)(uintptr_t)self, (unsigned long long)begin,
                n == LOG_FIRST ? " (further skips are not logged)" : "");
}

}

namespace runtime {

std::vector<CoreMixins> track_acclim_mixins()
{
    std::vector<CoreMixins> out;
    if (offered || !code_ready())
        return out;
    offered = true;
    char v[16] = {};
    DWORD n = GetEnvironmentVariableA("SWOFTY_ACCLIM_GUARD", v, sizeof v);
    if (n && n < sizeof v && (!strcmp(v, "0") || !strcmp(v, "off"))) {
        swofty::log("[acclim] track acclimatisation guard off (SWOFTY_ACCLIM_GUARD=%s)", v);
        return out;
    }
    if (!swofty::api->resolve(TARGET)) {
        swofty::log("[acclim] track acclimatisation guard off: needs %s, which this build does not resolve", TARGET);
        return out;
    }
    wanted = true;
    out.push_back({MIXIN, {{TARGET, SWOFTY_MIXIN_WRAP, 100, (void *)&refresh, (void **)&orig_refresh}}});
    return out;
}

void track_acclim_commit()
{
    if (!wanted || settled)
        return;
    settled = true;
    swofty::log("[acclim] track acclimatisation guard %s",
                core_mixins_active(MIXIN)
                    ? "armed: a practice programme refresh with no targets returns instead of faulting"
                    : "did not install (see the [core] lines)");
}

}
