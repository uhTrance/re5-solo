// Uroboros (uEm40, 1-2 and later) targets for a player in slot 1.
//
// Its AI (uEmIntel40) keeps a target slot (+0x2EC) and copies that player into its target (+0x2C) whenever the
// slot is valid (0x99B115). The slot is re-picked when a timer (+0x2F0) runs out, by 0x99AEF0, which only looks
// at slots below the player manager's count (+0x34). Playing as Sheva that count is 1, so the pick only sees
// Chris's empty slot and returns -1: the Uroboros never gets a target and runs on the spot. Raising the count
// instead crashes other code that loops over it without checking for an empty slot (0x731A94).
//
// So the solo player's slot is written over any invalid one; a -1 from a later pick leaves the target as it is.
#include "objhook.h"
#include "solo.h"

//
// The 1-2 incinerator's area controller (aS103) has the same blind spot: its update (vtable +0x1C, 0x730460)
// pushes every player in slots 0..count-1 out of the door's way and reads slot 0's position without a check
// (0x731A94), so as Sheva it crashed when the incinerator closed. For that update only, slot 0 holds the
// player, as it does when playing as Chris.
//
// The cutscene after it (scenes 1610 and 1620) ends in a state whose enter, update and exit (0xAF1470, 0xAF1990,
// 0xAF1C80) take slot 0's camera without asking who is playing: enter plays the closing shot on it, update waits for
// that shot to finish. As Sheva nothing plays and the game waits for ever, frozen on the last frame with the player
// locked. While that ending runs (stage flags 0x18000, which the game clears as it hands control back), slot 0 also
// holds the player.

namespace {

constexpr DWORD kIntelVtable = 0xF95E60;  // uEmIntel40
constexpr DWORD kTargetSlot = 0x2EC;

constexpr DWORD kBioArea = 0x1234578, kCurrentArea = 0x3830;  // [[kBioArea] + kCurrentArea] = area controller
constexpr DWORD kS103Vtable = 0xF53668;                          // aS103
constexpr DWORD kAreaUpdateSlot = 0x1C / 4;

constexpr DWORD kStageScene = 0x273E0, kStageFlags = 0x23218;  // on Stage()
constexpr DWORD kEndingFlags = 0x18000;

void __fastcall AreaUpdate(BYTE* area, void*) {
    auto players = *reinterpret_cast<BYTE**>(kPlayerList);
    BYTE* player = SoloPlayer();
    BYTE** slots = players ? &At<BYTE*>(players, 0x24) : nullptr;
    int solo = SoloSlot();
    bool swap = slots && solo != 0 && !slots[0] && player && slots[solo] == player;
    if (swap) slots[0] = player, slots[solo] = nullptr;
    ThisCall(reinterpret_cast<DWORD*>(kS103Vtable)[kAreaUpdateSlot], area);
    if (swap) slots[solo] = player, slots[0] = nullptr;
}

void PatchArea(DWORD* copy) { copy[kAreaUpdateSlot] = reinterpret_cast<DWORD>(&AreaUpdate); }

}  // namespace

// Watcher thread.
void UroborosTick() {
    static DWORD lastScan;
    if (GetTickCount() - lastScan < 100) return;
    lastScan = GetTickCount();
    auto players = *reinterpret_cast<BYTE**>(kPlayerList);
    if (!players || !SoloPlayer()) return;
    auto bioArea = *reinterpret_cast<BYTE**>(kBioArea);
    BYTE* area = bioArea ? At<BYTE*>(bioArea, kCurrentArea) : nullptr;
    static BYTE* patched;  // IsHooked only knows HookObject's copies
    if (area && At<DWORD>(area, 0) == kS103Vtable && PatchObject(area, kS103Vtable, PatchArea)) {
        patched = area;
        Log("incinerator area %p: slot 0 filled during its update", area);
    }
    BYTE* stage = Stage();
    DWORD scene = stage ? At<DWORD>(stage, kStageScene) : 0;
    bool s103 = area && area == patched && At<DWORD>(area, 0) >= 0x1928000;  // still on our vtable copy
    bool ending = s103 && SoloSlot() != 0 && (scene == 1610 || scene == 1620) &&
                  (At<DWORD>(stage, kStageFlags) & kEndingFlags);
    BYTE*& slot0 = At<BYTE*>(players, 0x24);
    static bool mirrored;
    if (ending && !slot0) {
        slot0 = SoloPlayer(), mirrored = true;
        Log("incinerator ending (scene %d): player mirrored into slot 0", scene);
    } else if (!ending && mirrored) {
        if (slot0 == SoloPlayer()) slot0 = nullptr;
        mirrored = false;
        Log("incinerator ending over: slot 0 empty again");
    }
    static BYTE* logged;  // each re-pick resets it, so log once per AI
    ForEachUnit([players](BYTE* u) {
        if (At<DWORD>(u, 0) != kIntelVtable) return;
        int slot = At<int>(u, kTargetSlot);
        if (slot >= 0 && slot < 4 && At<BYTE*>(players, 0x24 + 4 * slot)) return;
        At<int>(u, kTargetSlot) = SoloSlot();
        if (u != logged) Log("uroboros AI %p: target slot %d -> %d", u, slot, SoloSlot());
        logged = u;
    });
}
