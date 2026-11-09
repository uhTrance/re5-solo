// Playing as Sheva: the mod keeps whichever character the chapter was started with (objhook.h SoloSlot, the
// stage state's +0x273C4, set from the character choice and read by the game's one-player mode at 0x71855C)
// and closes the other slot (solo.cpp).
// Whether the pad then drives slot 1 depends on the session's per-slot control (+0x480), which is logged.
// (A re5_solo_sheva.txt marker that forced Sheva before the game unlocks her was dropped 2026-11-04: the
// character choice in the game's own menu is all the mod needs.)
//
// Lost in Nightmares is the exception: only Jill can open its doors and play its piano (the piano's prompt for
// Chris just sends an AI Jill to play), and the game offers Jill only after a clear as Chris. In its areas
// (loading area 616/617, then 700/701 and 705/706; Desperate Escape uses 618-620 and 702-704) the choice is set to Jill
// while no player exists, so the chapter loads as Jill with Chris's slot closed.
#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kGame = 0x12340A4, kSessionInfo = 0x1042C, kControl = 0x480;

}  // namespace

// Watcher thread, every tick.
void ShevaTick() {
    BYTE* stage = Stage();
    if (!stage) return;
    auto& chosen = At<DWORD>(stage, kChosenSlot);
    DWORD area = At<DWORD>(stage, 0x273D8);
    if (LostInNightmares() && chosen != 1 && !PlayerAt(0) && !PlayerAt(1)) {
        Log("sheva: Lost in Nightmares (area %lu): chosen character %lu -> 1 (Jill)", area, chosen);
        chosen = 1;
    }
    static int lastSlot = -1;
    static DWORD lastControl[2] = {~0u, ~0u};
    auto game = *reinterpret_cast<BYTE**>(kGame);
    auto info = game ? At<BYTE*>(game, kSessionInfo) : nullptr;
    DWORD control[2] = {info ? At<DWORD>(info, kControl) : 0, info ? At<DWORD>(info, kControl + 4) : 0};
    if (SoloSlot() != lastSlot || control[0] != lastControl[0] || control[1] != lastControl[1]) {
        lastSlot = SoloSlot();
        lastControl[0] = control[0];
        lastControl[1] = control[1];
        Log("sheva: playing as %s (slot %d), slot control %lu/%lu", lastSlot ? "Sheva/Jill" : "Chris", lastSlot,
            control[0], control[1]);
    }
}
