// Dying is game over in solo.
//
// Lethal damage leaves a player at 1 HP (+0x1364) with a dying timer (+0x1BAC, seconds) running: the dying
// state, where co-op waits for the partner to revive them. When the timer runs out the player is down for good
// (0xB6B330: HP == 1 and timer <= 0, or a status bit). With nobody to revive, solo would only limp on until
// the next hit. So the moment the solo player is dying, the mod runs the timer out and starts game over the
// way the game's own instant kills do (e.g. 0x8762E2): the game-over manager [0x123457C] runs 0x76D9F0
// (thiscall, slot), which does nothing once a game over is under way (+0x2FF8 set). Game thread: after the
// player's per-frame move (vtable +0x1C), through the player's vtable copy (snag.cpp hooks every player).
#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kHp = 0x1364, kDyingTimer = 0x1BAC;
constexpr DWORD kIsDown = 0xB6B330;                          // thiscall player -> bool
constexpr DWORD kGameOverManager = 0x123457C, kGameOver = 0x76D9F0;
constexpr DWORD kGameOverStarted = 0x2FF8;                   // byte in the manager

}  // namespace

void DeathAfterMove(BYTE* player) {
    if (player != SoloPlayer() || Partner() || RecordMode()) return;
    bool dying = At<short>(player, kHp) == 1 && At<float>(player, kDyingTimer) > 0.0f;
    if (!dying && !ThisCall<bool>(kIsDown, player)) return;
    auto manager = *reinterpret_cast<BYTE**>(kGameOverManager);
    if (!manager || At<BYTE>(manager, kGameOverStarted)) return;
    Log("death: player %d is dying (timer %.1f) with no partner to revive them, game over", SoloSlot(),
        At<float>(player, kDyingTimer));
    At<float>(player, kDyingTimer) = 0.0f;
    ThisCall(kGameOver, manager, SoloSlot());
}
