// Two-player twist levers (uOmCoopTwist, e.g. uOm08e4 in 4-1) and button doors (uOmCoopCardDoor, e.g.
// uOm0918 in 4-2: both players press a button; ready handler 0xAA0F30, same count) for one player.
//
// Same shape as the elevators (elevator.cpp): a player who takes hold sets their ready bit (low nibble of
// +0x18DC, 0xA82C50), and the lever's ready handler (vtable +0x184, 0xAAFAD0) only acts on events 0xF0/0xF1
// when it counts 2 ready slots (0xA82CA0 over slots 0-3). So once the solo player is ready, the lever's update
// marks the partner's slot ready too and re-sends 0xF1 for the player.
#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kReadyClasses[] = {0x11F98E4, 0x11F8B84};  // uOmCoopTwist, uOmCoopCardDoor
constexpr DWORD kReadyBits = 0x18DC;
constexpr DWORD kReadyHandler = 0x184;
constexpr DWORD kEventTryGo = 0xF1;

void AfterTwistUpdate(BYTE* lever) {
    auto& bits = At<BYTE>(lever, kReadyBits);
    BYTE mine = 1 << SoloSlot(), partner = 1 << PartnerSlot();
    if (!(bits & mine) || (bits & partner)) return;
    bits |= partner;
    VCall(lever, kReadyHandler, SoloSlot(), kEventTryGo, 0);
    Log("co-op object %p: player ready, partner slot marked ready, go sent", lever);
}

}  // namespace

// Watcher thread: hook every such object in the unit lists (a hooked one no longer passes IsA).
void TwistTick() {
    static DWORD lastScan;
    if (GetTickCount() - lastScan < 1000) return;
    lastScan = GetTickCount();
    ForEachUnit([&](BYTE* u) {
        bool ours = false;
        for (DWORD dti : kReadyClasses) ours = ours || IsA(u, dti);
        if (ours && HookObject(u, At<DWORD>(u, 0), AfterTwistUpdate)) Log("co-op object %p hooked", u);
    });
}
