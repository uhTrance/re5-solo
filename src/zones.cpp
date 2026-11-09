// Level zones that need both players, for one player.
//
// Level scripts test zones with AreaHit (cFSMOrder vtable +0xB4, 0xC340C0; mode 4 -> 0xCBFD50): zone +4 is
// its definition (+0xC type: 0 = any of, 1 = all of; +0x3C required player bits), zone +8 its state (+0x10
// bits of the players inside, +0x18 its own copy of the requirement; bit 4 appears set for any player, e.g. slot 0
// alone = 0x11). The game's setter (0xCBFDC0) writes both requirements, and the state's copy is the one that
// decides scenario zones (the 3-1 dock checkpoint, which sets the flag the crocodiles wait for). Co-op moments use
// "all of 0x13" (player 0, player 1, any), which never passes with one player (e.g. at the crank gate, the
// checkpoint Flag and the second wave wait on such zones). Zones are data, so in solo the partner's bit is
// moved to the player's in every "all of" requirement.
//
// Zone managers: [[0x11E8660] + 0xD4 + 4*i] (0x7EFA90), each with a zone count at +8 and list at +0x14.
//
// Lost in Nightmares also has zones for one character only, "any of 0x11" (Chris) beside "any of 0x12" (Jill).
// Played as Jill (forced there, sheva.cpp), Chris's are unreachable: at the piano, Chris's look at the sheet music
// (manager 6, zone 2, script Moon_Chris) sets flag 17, which the piano waits for before it can be played. There,
// a zone that names only the partner names the player instead.
//
// The same root also holds 9 scenario zone lists (+0x24, 0x14 apart: count +0, array +0xC; walked by 0x7F09C0).
// A scenario zone can run a level callback (cdecl fn(param), zone +0x14, its param +0xC; set at 0x7F0180, called
// by 0xCC0790). 5-3's moving platform (0x74FA30, step at param +4) at step 4, after the lever enemy is dealt with,
// walks players 0 and 1 with getPlayer and reads a missing one's +0x18 without a null check (0x74FD50): the game
// quit as soon as Chris's empty slot came up. That callback is pointed at a wrapper that, for step 4 only, puts
// the player in the partner's slot for the call and empties it again.
#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kAreaRoot = 0x11E8660;
constexpr int kManagers = 12;
constexpr int kScenarioLists = 9;
constexpr DWORD kCallback = 0x14;
constexpr DWORD kPlatformStep = 0x74FA30;

using Callback = int(__cdecl*)(BYTE* param);

// Every zone in the area root's scenario lists (the only zones whose callbacks the game runs, 0x7F09C0).
template <class F>
void ForEachScenarioZone(BYTE* root, F fn) {
    for (int i = 0; i < kScenarioLists; ++i) {
        BYTE* list = root + 0x24 + 0x14 * i;
        DWORD count = At<DWORD>(list, 0);
        auto zones = At<BYTE**>(list, 0xC);
        if (!zones || count > 512) continue;
        for (DWORD z = 0; z < count; ++z)
            if (zones[z]) fn(zones[z]);
    }
}

int __cdecl PlatformStep(BYTE* param) {
    auto original = reinterpret_cast<Callback>(kPlatformStep);
    auto list = *reinterpret_cast<BYTE**>(kPlayerList);
    if (!list || At<BYTE>(param, 4) != 4 || Partner() || !SoloPlayer()) return original(param);
    BYTE*& slot = At<BYTE*>(list, 0x24 + 4 * PartnerSlot());
    slot = SoloPlayer();
    int result = original(param);
    slot = nullptr;
    return result;
}

// Lost in Nightmares: scenario zones that start a level script when a player in them presses the action button
// (def +8 type 6; the zone's boot list, cScenario at zone +0x340 (vtbl 0x10498F8, its zone at +0x28): +0x2C 0 =
// start scripts, +0x34 count, +0x40 entries, each naming a script) never take Jill's press: the zone counts her
// inside and her per-player button (zone +0x10 group, who 1, mask 2) is available, but its callback (0xCC2430)
// never runs. Seen at the lock (Pick_chk, which sets flag 19 for the lockpick) and, by its flag never being set,
// at the piano's sheet music (Moon_Chris, flag 17). There these zones fire as soon as the player walks in (type 3).
// Type 5 does the same at the study's PC (PC_inputAndFileLook); of those only the ones without a prompt message
// (def +0x20 0) change, so the "Check" spots (message 0x8B) stay on the button.
constexpr DWORD kScenarioBoot = 0x340, kScenarioBootVtable = 0x10498F8;
constexpr DWORD kTypePress = 6, kTypePressQuiet = 5, kTypeEnter = 3;

void PressToEnterUnsafe(BYTE* zone) {
    BYTE* def = At<BYTE*>(zone, 4);
    if (!def) return;
    DWORD type = At<DWORD>(def, 8);
    if (type != kTypePress && !(type == kTypePressQuiet && At<DWORD>(def, 0x20) == 0)) return;
    BYTE* boot = zone + kScenarioBoot;
    if (At<DWORD>(boot, 0) != kScenarioBootVtable || At<BYTE*>(boot, 0x28) != zone || At<DWORD>(boot, 0x2C) != 0 ||
        At<DWORD>(boot, 0x34) == 0)
        return;
    At<DWORD>(def, 8) = kTypeEnter;
    Log("zones: script trigger %p (scene %lu) now starts when the player walks in, not on a button press", zone,
        At<DWORD>(def, 0x14));
}

void PressToEnter(BYTE* zone) {
    __try {
        PressToEnterUnsafe(zone);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

}  // namespace

void ZoneTick() {
    static DWORD last;
    if (GetTickCount() - last < 1000) return;
    last = GetTickCount();
    auto root = *reinterpret_cast<BYTE**>(kAreaRoot);
    if (!root) return;
    DWORD mine = 1u << SoloSlot(), partner = 1u << PartnerSlot();
    bool lin = LostInNightmares();
    int changed = 0;
    for (int i = 0; i < kManagers; ++i) {
        auto mgr = At<BYTE*>(root, 0xD4 + 4 * i);
        if (!mgr) continue;
        DWORD count = At<DWORD>(mgr, 8);
        auto zones = At<BYTE**>(mgr, 0x14);
        if (!zones || count > 512) continue;
        for (DWORD z = 0; z < count; ++z) {
            auto def = zones[z] ? At<BYTE*>(zones[z], 4) : nullptr;
            if (!def) continue;
            DWORD type = At<DWORD>(def, 0xC);
            bool onlyPartner = type == 0 && lin && (At<DWORD>(def, 0x3C) & 0xF) == partner;
            if (type != 1 && !onlyPartner) continue;
            auto state = At<BYTE*>(zones[z], 8);
            bool moved = false;
            DWORD* reqs[] = {&At<DWORD>(def, 0x3C), state ? &At<DWORD>(state, 0x18) : nullptr};
            for (DWORD* req : reqs) {
                if (!req || !(*req & partner)) continue;
                *req = (*req & ~partner) | mine;
                moved = true;
            }
            changed += moved;
        }
    }
    if (changed) Log("zones: %d both-players or partner-only zone(s) now need only the player", changed);
    ForEachScenarioZone(root, [lin](BYTE* zone) {
        if (lin) PressToEnter(zone);
        if (At<DWORD>(zone, kCallback) != kPlatformStep) return;
        At<DWORD>(zone, kCallback) = reinterpret_cast<DWORD>(&PlatformStep);
        Log("zones: 5-3 platform callback wrapped (the partner's slot is filled for its player check)");
    });
}

// Checkpoint zones that a two-man lever's other side stands in (6-3, area 505: the scenario zone with scene
// 11229 is a 250 x 250 box around the right lever, "any player inside", enabled when the pair goes; the level
// script moves the scene on to 11230 straight after, so it only saves if someone is standing in it then). Solo,
// the player pulls the other lever and no checkpoint is made. Zone +0x1C bit 0x10 makes a scenario zone fire
// with nobody inside (0xCC0997), so it is set on such zones near the lever the mod marks for the player.
// cScenario: +4 definition (+8 type, 3 = fire on a player inside; +0xC 0 = any of; +0x14 scene, 99999 = none),
// +8 its cAreaHit (centre at +0x30), +0x1C flags (bit 2 = off, bit 3 = fired).
int ForceCheckpointZonesNear(float x, float z, float radius) {
    auto root = *reinterpret_cast<BYTE**>(kAreaRoot);
    if (!root) return 0;
    int forced = 0;
    __try {
        ForEachScenarioZone(root, [&](BYTE* zone) {
            BYTE* def = At<BYTE*>(zone, 4);
            BYTE* area = At<BYTE*>(zone, 8);
            if (!def || !area || At<DWORD>(def, 8) != 3 || At<DWORD>(def, 0xC) != 0) return;
            DWORD scene = At<DWORD>(def, 0x14);
            if (scene == 0 || scene >= 99999) return;
            float dx = At<float>(area, 0x30) - x, dz = At<float>(area, 0x38) - z;
            if (dx * dx + dz * dz > radius * radius) return;
            volatile LONG& flags = At<volatile LONG>(zone, 0x1C);
            if (flags & 0x18) return;  // forced already, or fired
            _InterlockedOr(&flags, 0x10);
            ++forced;
            Log("zones: checkpoint zone %p (scene %lu) at the other lever fires without the player inside", zone,
                scene);
        });
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return forced;
}

namespace {

void UnwrapZones() {
    auto root = *reinterpret_cast<BYTE**>(kAreaRoot);
    if (!root) return;
    ForEachScenarioZone(root, [](BYTE* zone) {
        if (At<DWORD>(zone, kCallback) == reinterpret_cast<DWORD>(&PlatformStep)) At<DWORD>(zone, kCallback) = kPlatformStep;
    });
}

}  // namespace

// CoreShutdown: nothing may keep pointing into this DLL. The game only runs callbacks of zones in the scenario
// lists, so every zone there still holding the wrapper gets the platform step back (none are tracked: a 5-3
// retry builds new zones each time, and a fixed list of them ran out).
void ZoneShutdown() {
    __try {
        UnwrapZones();
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
