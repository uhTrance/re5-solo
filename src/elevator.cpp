// Two-player elevators (uOmElevator and its 11 subclasses) for one player.
//
// Pressing the prompt sends the elevator player event 0 (0xAC6FE0): it sets the presser's ready bit (low
// nibble of +0x18DC, one bit per slot, 0xA82C50) and gives them cActPlReady (+0x20 elevator, +0x24 player).
// The "go" check is event 0xF1 in the elevator's own ready handler (vtable +0x184, 0xAC6D50, not the shared
// 0xAA2DD0 the solo flag covers): it counts ready bits and needs 2, hard-coded. So once the player is ready,
// the elevator's update marks the partner's slot ready too and re-sends 0xF1. The game clears the bits after
// each ride.
#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kAction = 0x10F8;
constexpr DWORD kPlReadyVtable = 0xFE70E4;  // uOmElevator::cActPlReady
constexpr DWORD kActObject = 0x20;          // the elevator (+0x24 is the player)
constexpr DWORD kElevatorDti = 0x11FBBD8;   // uOmElevator
constexpr DWORD kReadyBits = 0x18DC;
constexpr DWORD kReadyHandler = 0x184;
constexpr DWORD kEventTryGo = 0xF1;

void AfterElevatorUpdate(BYTE* elevator) {
    auto& bits = At<BYTE>(elevator, kReadyBits);
    BYTE mine = 1 << SoloSlot(), partner = 1 << PartnerSlot();
    if (!(bits & mine) || (bits & partner)) return;
    bits |= partner;
    VCall(elevator, kReadyHandler, SoloSlot(), kEventTryGo, 0);
    Log("elevator %p: player ready, partner slot marked ready, go sent", elevator);
}

}  // namespace

// Watcher thread: when the player readies at an elevator, hook that elevator's update.
void ElevatorTick() {
    BYTE* player = SoloPlayer();
    if (!player) return;
    auto act = At<BYTE*>(player, kAction);
    if (!act || At<DWORD>(act, 0) != kPlReadyVtable) return;
    auto elevator = At<BYTE*>(act, kActObject);
    if (!elevator) return;
    if (!IsA(elevator, kElevatorDti)) return;  // already ours (heap copy) or not an elevator
    HookObject(elevator, At<DWORD>(elevator, 0), AfterElevatorUpdate);
}
