// Crank gates (uOmCrank and its 9 subclasses) for one player: once the crank raises the gate to its cap, the
// gate locks there, so the player can let go and walk under.
//
// In co-op one player holds the crank while the other goes under. The gate follows the crank's wind (+0x1970,
// mirrored at +0x17FC; +0x197C = percent), which rises while the player winds (up to the crank's max, +0x1974,
// 300) and unwinds on letting go (cActRewind). Pinning the wind at 66% (198 of 300 on the uOm0872 gate) after
// each update holds the gate up. The finished byte (+0x19F5) is no use: it stops the crank turning at all.
// Cranks with the stays-wound flag (+0x19A4 bit 4, e.g. the 3-2 valves uOm08df, max 150) are left alone: at
// max they finish for good, and a cap below max would keep them turning forever.
// The 3-2 crocodile crank has its own fix (croc.cpp).
#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kAction = 0x10F8;
constexpr DWORD kPlWindVtable = 0xFCCBA8;  // uOmCrank::cActPlWind
constexpr DWORD kActObject = 0x20;         // the crank (+0x24 is the player, unlike the jump/elevator actions)
constexpr DWORD kCrankDti = 0x11F9A28;     // uOmCrank
constexpr DWORD kWind = 0x1970, kWindMax = 0x1974, kWindShadow = 0x17FC, kWindPercent = 0x197C;
constexpr float kCapFraction = 0.66f;
constexpr DWORD kFlags = 0x19A4, kStaysWound = 4;  // at max the crank finishes for good (0xAB201F)

BYTE* g_locked[16];

bool IsLocked(BYTE* crank) {
    for (BYTE* c : g_locked)
        if (c == crank) return true;
    return false;
}

void AfterCrankUpdate(BYTE* crank) {
    if (At<DWORD>(crank, kFlags) & kStaysWound) return;  // a valve: one player turns it fully and it stays done
    float cap = At<float>(crank, kWindMax) * kCapFraction;
    if (!IsLocked(crank)) {
        if (At<float>(crank, kWind) < cap) return;
        for (BYTE*& c : g_locked)
            if (!c) {
                c = crank;
                break;
            }
        Log("crank %p: gate reached its cap (%.0f), locked open", crank, cap);
    }
    At<float>(crank, kWind) = cap;
    At<float>(crank, kWindShadow) = cap;
    At<float>(crank, kWindPercent) = kCapFraction * 100.0f;
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
