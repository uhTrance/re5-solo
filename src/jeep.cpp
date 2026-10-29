// 2-3 jeep turret: the jeep (uOmJeep, and uOm1304 derived from it) makes one turret cockpit (uIdCockpit01)
// per player slot, Chris's at +0x1974 and Sheva's at +0x1978 (0xADC490, vtable +0x180, once: +0x197C). Each
// cockpit's player (+0x78) comes from 0x7E6220(index) and is only set if valid (0x9FA7B0), so the missing
// partner's stays null. The cockpit's first step (vtable +0x14, 0x9FDE10 -> 0x9FA970) reads that player's
// inventory (+0x78 + 0x21A0) with no null check: crash at 0xC4B720 on loading 2-3.
// Fix: a cockpit with no player gets the player, right after the jeep's update (on the game thread, so before
// the cockpit's own first step) and from the watcher as a fallback.
#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kJeepVtables[] = {0xFF17B0, 0xFF1960};  // uOmJeep, uOm1304
constexpr DWORD kCockpits = 0x1974;                     // [2], one per player
constexpr DWORD kCockpitPlayer = 0x78;

void FillCockpits(BYTE* jeep) {
    BYTE* player = SoloPlayer();
    if (!player) return;
    for (int i = 0; i < 2; ++i) {
        BYTE* cockpit = At<BYTE*>(jeep, kCockpits + 4 * i);
        if (cockpit && !At<BYTE*>(cockpit, kCockpitPlayer)) {
            At<BYTE*>(cockpit, kCockpitPlayer) = player;
            Log("jeep %p: cockpit %d (%p) had no player, gave it the player", jeep, i, cockpit);
        }
    }
}

}  // namespace

// Watcher thread, every tick: the jeep can make its cockpits within frames of the level load.
void JeepTick() {
    ForEachUnit([&](BYTE* u) {
        DWORD vt = At<DWORD>(u, 0);
        for (DWORD jeep : kJeepVtables)
            if (vt == jeep || IsHooked(u, jeep)) {
                HookObject(u, jeep, FillCockpits);
                FillCockpits(u);
            }
    });
}
