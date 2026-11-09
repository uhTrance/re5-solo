// Crank gates (uOmCrank and its 9 subclasses) for one player: once the crank raises the gate to its cap, the
// gate locks there, so the player can let go and walk under.
//
// In co-op one player holds the crank while the other goes under. The gate follows the crank's wind (+0x1970,
// mirrored at +0x17FC; +0x197C = percent), which rises while the player winds (up to the crank's max, +0x1974,
// 300) and unwinds on letting go (cActRewind). Pinning the wind at 50% after each update holds the gate up: 150
// of 300 is three full turns of the 2-2 crank (about 50 a turn, measured 2026-11-03). The finished byte (+0x19F5) is no use: it stops the crank turning at all.
// Cranks with the stays-wound flag (+0x19A4 bit 4, e.g. the 3-2 valves uOm08df, max 150) are left alone: at
// max they finish for good, and a cap below max would keep them turning forever.
// The 3-2 crocodile crank has its own fix (croc.cpp).
//
// Lost in Nightmares, area 705 (the hall of coloured cranks): the crank's grab button (its action at +0x10F8, button
// at +0x1C, condition 0xAB18F0, cdecl (crank, slot)) there lets a human turn it only when their partner exists and is
// human too (an AI partner turns it instead, 0xAB1979), so alone it is never available. Its press callback (0xAB19E0)
// copes with no partner. In solo there, a free crank (+0x1995 0) the original refuses is available to the player.
// Each of those cranks raises a ceiling trap (uOm021d, a uOmS705ceil: crank at +0x197C, the box under it from
// +0x1A00 to +0x1A10) that drops when the crank is let go; in co-op one player turns while the other lures the axe
// man (uEm27, a uEm20) under it. Locked up there in solo, it would never drop, so the lock lets go as soon as a uEm20
// is in the box and the player is not.
#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kAction = 0x10F8;
constexpr DWORD kPlWindVtable = 0xFCCBA8;  // uOmCrank::cActPlWind
constexpr DWORD kActObject = 0x20;         // the crank (+0x24 is the player, unlike the jump/elevator actions)
constexpr DWORD kCrankDti = 0x11F9A28;     // uOmCrank
constexpr DWORD kWind = 0x1970, kWindMax = 0x1974, kWindShadow = 0x17FC, kWindPercent = 0x197C;
constexpr float kCapFraction = 0.5f;
constexpr DWORD kFlags = 0x19A4, kStaysWound = 4;  // at max the crank finishes for good (0xAB201F)

BYTE* g_locked[16];

bool IsLocked(BYTE* crank) {
    for (BYTE* c : g_locked)
        if (c == crank) return true;
    return false;
}

constexpr DWORD kSetActionSlot = 0xDC / 4, kActWait = 0, kActRewind = 2;  // uOmCrank actions: 0 wait, 1 wind, 2 rewind
constexpr DWORD kRewindVtable = 0xFCCB80;  // uOmCrank::cActRewind

constexpr DWORD kCeilVtable = 0x1010D08, kCeilCrank = 0x197C, kCeilMin = 0x1A00, kCeilMax = 0x1A10;
constexpr DWORD kEm20Dti = 0x11EABC8;  // uEm20, the axe man's family

// IsA without the vtable range check: enemies may be on a vtable copy (snag.cpp's 120fps fix).
bool DescendsFrom(BYTE* object, DWORD dti) {
    auto d = VCall<BYTE*>(object, 0x10);
    for (int depth = 0; d && depth < 32; ++depth, d = At<BYTE*>(d, 0x10))
        if (reinterpret_cast<DWORD>(d) == dti) return true;
    return false;
}

bool InBox(BYTE* ceil, BYTE* unit) {
    float x = At<float>(unit, 0x30), y = At<float>(unit, 0x34), z = At<float>(unit, 0x38);
    return x >= At<float>(ceil, kCeilMin) && x <= At<float>(ceil, kCeilMax) && y >= At<float>(ceil, kCeilMin + 4) &&
           y <= At<float>(ceil, kCeilMax + 4) && z >= At<float>(ceil, kCeilMin + 8) && z <= At<float>(ceil, kCeilMax + 8);
}

// Lost in Nightmares' ceiling traps: is it time to let this crank's ceiling drop?
bool CeilingBait(BYTE* crank) {
    BYTE* stage = Stage();
    if (!stage || At<DWORD>(stage, 0x273D8) != 705 || Partner() || !SoloPlayer()) return false;
    BYTE* ceil = nullptr;
    ForEachUnit([&](BYTE* u) {
        if (!ceil && At<DWORD>(u, 0) == kCeilVtable && At<BYTE*>(u, kCeilCrank) == crank) ceil = u;
    });
    if (!ceil || InBox(ceil, SoloPlayer())) return false;
    bool enemy = false;
    ForEachUnit([&](BYTE* u) {
        if (!enemy && DescendsFrom(u, kEm20Dti) && InBox(ceil, u)) enemy = true;
    });
    return enemy;
}

void AfterCrankUpdate(BYTE* crank) {
    if (At<DWORD>(crank, kFlags) & kStaysWound) return;  // a valve: one player turns it fully and it stays done

    // Lost in Nightmares' hall crank (uOm021b, max 150) needs the full wind for the gate to clear Jill: it locks
    // once she has wound it all the way.
    float fraction = LostInNightmares() ? 1.0f : kCapFraction;
    float cap = At<float>(crank, kWindMax) * fraction;
    if (!IsLocked(crank)) {
        if (At<float>(crank, kWind) < cap) return;
        for (BYTE*& c : g_locked)
            if (!c) {
                c = crank;
                break;
            }
        Log("crank %p: gate reached its cap (%.0f), locked open", crank, cap);
    } else if (CeilingBait(crank)) {
        for (BYTE*& c : g_locked)
            if (c == crank) c = nullptr;
        // Nobody holds it, so nothing unwinds it: the crank is put in its rewind action (id 2, through its action
        // setter, vtable +0xDC, called as the ceiling's own code does at 0xB33621), which unwinds it while the
        // ceiling falls (cActFall). Changing the wind alone (to 0, or running it down) left the ceiling in cActRise,
        // which only rises, until it went to cActLand in mid-air: drawn up there with its collision on the floor.
        ThisCall(At<DWORD*>(crank, 0)[kSetActionSlot], crank, kActRewind, 1, 0, -1, 0);
        Log("crank %p: an axe man is under its ceiling and the player is not, let go so it drops", crank);
        return;
    }
    At<float>(crank, kWind) = cap;
    At<float>(crank, kWindShadow) = cap;
    At<float>(crank, kWindPercent) = fraction * 100.0f;
    // Locked in Lost in Nightmares: the rewind the player's letting go starts is turned back into waiting. Area 705's
    // ceilings follow the crank's action: on its rewind a ceiling fell, and with the wind held it went to cActLand
    // in mid-air (drawn up high, its collision on the floor: an invisible platform).
    BYTE* action = At<BYTE*>(crank, kAction);
    if (LostInNightmares() && action && At<DWORD>(action, 0) == kRewindVtable)
        ThisCall(At<DWORD*>(crank, 0)[kSetActionSlot], crank, kActWait, 1, 0, -1, 0);
}

}  // namespace

// Watcher thread: when the player starts winding a crank, hook that crank's update.
void CrankTick() {
    BYTE* player = SoloPlayer();
    if (!player) return;
    auto act = At<BYTE*>(player, kAction);
    if (!act || At<DWORD>(act, 0) != kPlWindVtable) return;
    auto crank = At<BYTE*>(act, kActObject);
    if (!crank) return;
    if (!IsA(crank, kCrankDti)) return;  // already ours (heap copy) or not a crank
    // A new level can reuse a locked crank's address: hooking means it's a fresh object.
    for (BYTE*& c : g_locked)
        if (c == crank) c = nullptr;
    HookObject(crank, At<DWORD>(crank, 0), AfterCrankUpdate);
}

namespace {

constexpr DWORD kCrankAction = 0x10F8, kGrabButton = 0x1C, kCondition = 0x5C, kGrabCondition = 0xAB18F0;
constexpr DWORD kInUse = 0x1995;
constexpr DWORD kLinCrankArea = 705;

using GrabCondition = bool(__cdecl*)(BYTE* crank, int slot);

bool __cdecl SoloGrabCondition(BYTE* crank, int slot) {
    if (reinterpret_cast<GrabCondition>(kGrabCondition)(crank, slot)) return true;
    BYTE* stage = Stage();
    return stage && At<DWORD>(stage, 0x273D8) == kLinCrankArea && !Partner() && slot == SoloSlot() &&
           !At<BYTE>(crank, kInUse);
}

BYTE* g_buttons[16];  // grab buttons pointed at SoloGrabCondition, for CrankShutdown

void PatchGrabButton(BYTE* crank) {
    BYTE* action = At<BYTE*>(crank, kCrankAction);
    BYTE* button = action ? At<BYTE*>(action, kGrabButton) : nullptr;
    if (!button || At<DWORD>(button, kCondition) != kGrabCondition) return;
    BYTE** slot = nullptr;
    for (BYTE*& b : g_buttons)
        if (!b || b == button || At<DWORD>(b, kCondition) != reinterpret_cast<DWORD>(&SoloGrabCondition)) {
            slot = &b;
            break;
        }
    if (!slot) return;
    *slot = button;
    At<DWORD>(button, kCondition) = reinterpret_cast<DWORD>(&SoloGrabCondition);
    Log("crank %p: grab button available to the player alone (Lost in Nightmares)", crank);
}

}  // namespace

// Watcher thread, Lost in Nightmares solo: the crank grab buttons that need a partner.
void CrankButtonTick() {
    static DWORD last;
    if (GetTickCount() - last < 500) return;
    last = GetTickCount();
    if (!LostInNightmares() || Partner() || !SoloPlayer()) return;
    ForEachUnit([](BYTE* u) {
        if (!DescendsFrom(u, kCrankDti)) return;
        PatchGrabButton(u);
        // A wound crank not hooked yet (e.g. after the mod reloads): hook it so its lock and ceiling drop run.
        if (IsA(u, kCrankDti) && At<float>(u, kWind) > 0.0f) HookObject(u, At<DWORD>(u, 0), AfterCrankUpdate);
    });
}

// CoreShutdown: no button may keep pointing into this DLL.
void CrankShutdown() {
    __try {
        for (BYTE*& b : g_buttons) {
            if (b && At<DWORD>(b, kCondition) == reinterpret_cast<DWORD>(&SoloGrabCondition))
                At<DWORD>(b, kCondition) = kGrabCondition;
            b = nullptr;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
