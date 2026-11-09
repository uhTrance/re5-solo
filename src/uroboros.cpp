// Uroboros (uEm40, 1-2 and later; uEm41, Uroboros Mkono, 5-2) targets for a player in slot 1.
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
// player too, as it does when playing as Chris. The player stays in their own slot as well: other threads scan
// the slots meanwhile, and moving the player (fill 0, empty 1) could let a scan miss them (at Ndesu that crashed
// the pad rumble's "first human player" lookup, 0x7E6290; changed here to match, 2026-11-04).
//
// The cutscene after it (scenes 1610 and 1620) ends in a state whose enter, update and exit (0xAF1470, 0xAF1990,
// 0xAF1C80) take slot 0's camera without asking who is playing: enter plays the closing shot on it, update waits for
// that shot to finish. As Sheva nothing plays and the game waits for ever, frozen on the last frame with the player
// locked. While that ending runs (stage flags 0x18000, which the game clears as it hands control back), slot 0 also
// holds the player.

namespace {

// uEmIntel40, and uEmIntel41 (Uroboros Mkono, 5-2 area 308), which shares its target pick and slot (logged
// 2026-11-07: slot -1, no target, the boss standing still as Sheva).
constexpr DWORD kIntelVtables[] = {0xF95E60, 0xF960F0};
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
    if (swap) slots[0] = player;  // a copy: the player never leaves their own slot (see below)
    ThisCall(reinterpret_cast<DWORD*>(kS103Vtable)[kAreaUpdateSlot], area);
    if (swap) slots[0] = nullptr;
}

void PatchArea(DWORD* copy) { copy[kAreaUpdateSlot] = reinterpret_cast<DWORD>(&AreaUpdate); }

// A Desperate Escape area controller (vtable 0xF57810, update +0x1C = 0x7604D0) checks players 0 and 1 for a
// state (0xB56600, reads +0x10E0) without testing for an empty slot (0x760970), so it crashed as a cutscene was
// due (logged 2026-11-08: read of 0x10E0 at 0xB56603). For that update an empty slot 0 or 1 holds the player too.
constexpr DWORD kDeAreaVtable = 0xF57810;

void __fastcall DeAreaUpdate(BYTE* area, void*) {
    auto players = *reinterpret_cast<BYTE**>(kPlayerList);
    BYTE* player = SoloPlayer();
    BYTE** slots = players ? &At<BYTE*>(players, 0x24) : nullptr;
    bool filled[2] = {};
    if (slots && player)
        for (int i = 0; i < 2; ++i)
            if (!slots[i]) slots[i] = player, filled[i] = true;
    ThisCall(reinterpret_cast<DWORD*>(kDeAreaVtable)[kAreaUpdateSlot], area);
    for (int i = 0; i < 2; ++i)
        if (filled[i] && slots[i] == player) slots[i] = nullptr;
}

void PatchDeArea(DWORD* copy) { copy[kAreaUpdateSlot] = reinterpret_cast<DWORD>(&DeAreaUpdate); }

// Lost in Nightmares' basement (area 705, aS705, vtable 0xF579B0): its wake-up runs in steps (+0x6EC). Step 1 waits
// until players 0 and 1 are both in and ready (+0x14 >= 1 and +0x15 >= 1, counted at 0x761C27, needing 2); step 2
// gives each the aS705::Motion action (DTI object 0x11B1698, through the player's vtable +0xE0), the wake-up
// animation under the camera's pan. Alone the count stays 1, so Jill never got up (seen 2026-11-09, confirmed against
// vanilla co-op, where both players take that action at scene 14700). With the player ready and no partner, step 1
// is passed for her, just before the controller's own update.
constexpr DWORD kS705Vtable = 0xF579B0, kWakeStep = 0x6EC;

void __fastcall S705Update(BYTE* area, void*) {
    BYTE* me = SoloPlayer();
    if (At<BYTE>(area, kWakeStep) == 1 && me && !Partner() && At<BYTE>(me, 0x14) >= 1 && At<BYTE>(me, 0x15) >= 1) {
        At<BYTE>(area, kWakeStep) = 2;
        Log("basement wake-up: the player is ready alone, the wake-up motion goes ahead");
    }
    ThisCall(reinterpret_cast<DWORD*>(kS705Vtable)[kAreaUpdateSlot], area);
}

void PatchS705(DWORD* copy) { copy[kAreaUpdateSlot] = reinterpret_cast<DWORD>(&S705Update); }

// Uroboros Mkono (5-2) only opens up for the flamethrower at 30 fps. Its body opens (cEm41ActDisclose, which sets
// exposed +0x20B6, the only time part hits do real damage) when the AI sees flag 8 in +0x1E2C, set by its hit handler
// (vtable +0x208, 0x8B4D80) once a heat meter (+0x2148) passes 2200. Each flame hit adds (30 - n) * 3 times the
// unit's frame step (vtable +0x9C: +0xFD4 * [timer] +0x2C * +0x10, 1 at 30 fps, 0.25 at 120), as if it were a
// per-frame amount, while the meter cools per frame (0x8B8210) by the same amount per second at any frame rate.
// Hits come at the same rate at any frame rate, so at 120 fps each adds a quarter as much and the meter never fills.
// The hit's addition is divided by the frame part of the step (+0x10 and the timer's +0x2C; +0xFD4 is slow motion,
// left in), and flag 8 set as the handler would.
constexpr DWORD kMkonoVtable = 0xF7D160, kMkonoHitSlot = 0x208 / 4, kMkonoHit = 0x8B4D80;
constexpr DWORD kHeat = 0x2148, kMkonoFlags = 0x1E2C, kExposed = 0x20B6, kUnitStep = 0x10;
constexpr DWORD kTimer = 0x11B209C, kTimerStep = 0x2C;
constexpr float kOpenHeat = 2200.0f;  // 0xF54D30
constexpr DWORD kOpenFlag = 8;

bool __fastcall MkonoHit(BYTE* self, void*, void* hit) {
    float before = At<float>(self, kHeat);
    bool r = ThisCall<bool>(kMkonoHit, self, hit);
    float gain = At<float>(self, kHeat) - before;
    auto timer = *reinterpret_cast<BYTE**>(kTimer);
    float step = At<float>(self, kUnitStep) * (timer ? At<float>(timer, kTimerStep) : 1.0f);
    if (gain > 0 && step > 0.01f && step != 1.0f) {
        float heat = before + gain / step;
        At<float>(self, kHeat) = heat;
        if (!At<BYTE>(self, kExposed) && heat > kOpenHeat) At<DWORD>(self, kMkonoFlags) |= kOpenFlag;
    }
    return r;
}

void PatchMkono(DWORD* copy) { copy[kMkonoHitSlot] = reinterpret_cast<DWORD>(&MkonoHit); }

// Lost in Nightmares, area 705: uEmIntel70 and uEmIntel65 (the enemies there) take player 0 as their target and
// never pick another, so with Chris's slot empty their target (+0x2C) stays null and they stand about, as the
// Uroboros did (seen 2026-11-09). Like the game's own setters (e.g. 0x9C19C7), the player goes in the AI's target and
// in its enemy's copy (+0x280 -> +0x1BA4).
constexpr DWORD kLinIntelVtables[] = {0xF977B8, 0xF97348};
constexpr DWORD kIntelTarget = 0x2C, kIntelUnit = 0x280, kUnitTarget = 0x1BA4;

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
    if (area && At<DWORD>(area, 0) == kDeAreaVtable && PatchObject(area, kDeAreaVtable, PatchDeArea))
        Log("Desperate Escape area %p: empty player slots filled during its update", area);
    if (area && At<DWORD>(area, 0) == kS705Vtable && PatchObject(area, kS705Vtable, PatchS705))
        Log("basement area %p: its wake-up no longer waits for two players", area);
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
        if (At<DWORD>(u, 0) == kMkonoVtable && PatchObject(u, kMkonoVtable, PatchMkono))
            Log("uroboros mkono %p: flame heat no longer scaled by the frame rate", u);
        DWORD vt = At<DWORD>(u, 0);
        if ((vt == kLinIntelVtables[0] || vt == kLinIntelVtables[1]) && LostInNightmares() && !Partner() &&
            !At<BYTE*>(u, kIntelTarget)) {
            BYTE* unit = At<BYTE*>(u, kIntelUnit);
            At<BYTE*>(u, kIntelTarget) = SoloPlayer();
            if (unit) At<BYTE*>(unit, kUnitTarget) = SoloPlayer();
            Log("enemy AI %p (vtable %08lX): no target, now the player", u, vt);
        }
        if (vt != kIntelVtables[0] && vt != kIntelVtables[1]) return;
        int slot = At<int>(u, kTargetSlot);
        if (slot >= 0 && slot < 4 && At<BYTE*>(players, 0x24 + 4 * slot)) return;
        At<int>(u, kTargetSlot) = SoloSlot();
        if (u != logged) Log("uroboros AI %p: target slot %d -> %d", u, slot, SoloSlot());
        logged = u;
    });
}
