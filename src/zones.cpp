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
    if (changed) Log("zones: %d both-players zone(s) now need only the player", changed);
    ForEachScenarioZone(root, [](BYTE* zone) {
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
