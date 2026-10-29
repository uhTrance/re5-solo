// Level zones that need both players, for one player.
//
// Level scripts test zones with AreaHit (cFSMOrder vtable +0xB4, 0xC340C0; mode 4 -> 0xCBFD50): zone +4 is
// its definition (+0xC type: 0 = any of, 1 = all of; +0x3C required player bits), zone +8 its state (+0x10
// bits of the players inside; bit 4 appears set for any player, e.g. slot 0 alone = 0x11). Co-op moments use
// "all of 0x13" (player 0, player 1, any), which never passes with one player (e.g. at the crank gate, the
// checkpoint Flag and the second wave wait on such zones). Zones are data, so in solo the partner's bit is
// moved to the player's in every "all of" requirement.
//
// Zone managers: [[0x11E8660] + 0xD4 + 4*i] (0x7EFA90), each with a zone count at +8 and list at +0x14.
#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kAreaRoot = 0x11E8660;
constexpr int kManagers = 12;

}  // namespace

void ZoneTick() {
    static DWORD last;
    if (GetTickCount() - last < 1000) return;
    last = GetTickCount();
    auto root = *reinterpret_cast<BYTE**>(kAreaRoot);
    if (!root) return;
    DWORD mine = 1u << SoloSlot(), partner = 1u << PartnerSlot();
    int changed = 0;
    for (int i = 0; i < kManagers; ++i) {
        auto mgr = At<BYTE*>(root, 0xD4 + 4 * i);
        if (!mgr) continue;
        DWORD count = At<DWORD>(mgr, 8);
        auto zones = At<BYTE**>(mgr, 0x14);
        if (!zones || count > 512) continue;
        for (DWORD z = 0; z < count; ++z) {
            auto def = zones[z] ? At<BYTE*>(zones[z], 4) : nullptr;
            if (!def || At<DWORD>(def, 0xC) != 1) continue;
            auto& req = At<DWORD>(def, 0x3C);
            if (!(req & partner)) continue;
            req = (req & ~partner) | mine;
            ++changed;
        }
    }
    if (changed) Log("zones: %d both-players zone(s) now need only the player", changed);
}
