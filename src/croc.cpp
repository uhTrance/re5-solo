// 3-2's uOm08a6 cranks for one player. The crocodile raft (crank pulls raft uOm08a3): while the player stands
// on the raft, the crank turns by itself at the partner's speed; off the raft, the crank's own unwind takes it
// back.
//
// In co-op one player "investigates" the crank and the other turns it while they ride. The crank's percent
// (+0x197C) is the raft's input (+0x1970, linked by the level), and the raft's idle action follows its input
// (mCurrent +0x1990, 0..100 of mMax -2600 units along its track), so writing the crank's wind/percent after
// each crank update moves the raft. The crank's rewind (about 4.4/s when nobody winds) is vanilla and stays;
// a partner winds about 11.5/s. The four crocodile cutscenes reload both objects with the wind kept.
//
// Nothing points from the player to the crank, so it is found on the unit lists ([0x12340B0] = sUnit; move
// line i at +0x20 + 0x20*i: +0xC first unit, +0x18 = i; units chain through +8).
//
// 3-1's marsh crocodiles (uEm66) hide in cEm66ActAmbush until their AI (uEmIntel66, vtable 0xF97460, a unit
// of its own) finds a player to bite. Its update (+0x70, 0x963350) runs one of two thinks (+0x94 or +0x98 by
// its mode, +0x1C); the first runs 0x9B7E70, which only looks at player slots below the player count
// ([[0x11B27DC]+0x34], 1 in solo). As Sheva (slot 1) it never saw her, so the crocodiles never came out, and
// with only that think fixed they surfaced and dived straight back. The AI gets a vtable copy whose update
// sees a count that reaches the player's slot; the count is put back straight after (62 places read it, many
// of them not ready for an empty slot 0).
#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kIntelVtable = 0xF97460;  // uEmIntel66
constexpr DWORD kUpdateSlot = 0x70 / 4, kUpdate = 0x963350;
constexpr DWORD kPlayerCount = 0x34;      // in the player list

void __fastcall Update(BYTE* self, void*) {
    auto list = *reinterpret_cast<BYTE**>(kPlayerList);
    DWORD need = SoloSlot() + 1;
    if (!list || Partner() || At<DWORD>(list, kPlayerCount) >= need) return ThisCall(kUpdate, self);
    DWORD count = At<DWORD>(list, kPlayerCount);
    At<DWORD>(list, kPlayerCount) = need;
    ThisCall(kUpdate, self);
    At<DWORD>(list, kPlayerCount) = count;
}

void PatchIntel(DWORD* copy) { copy[kUpdateSlot] = reinterpret_cast<DWORD>(&Update); }

constexpr DWORD kCrankVtable = 0xFCCDA0;  // uOm08a6
constexpr DWORD kRaftVtable = 0xFEBD00;   // uOm08a3
constexpr DWORD kRaft = 0x1A00;           // crank -> raft
constexpr DWORD kWind = 0x1970, kWindMax = 0x1974, kWindShadow = 0x17FC, kWindPercent = 0x197C;
constexpr DWORD kRaftPos = 0x2D0;         // raft's current centre (x y z)
constexpr DWORD kAction = 0x10F8;
constexpr float kWindPerSecond = 11.5f;
constexpr float kOnRaftRadius = 450.0f, kOnRaftBelow = -20.0f, kOnRaftAbove = 200.0f;

bool OnRaft(BYTE* player, BYTE* raft) {
    float dx = At<float>(player, 0x30) - At<float>(raft, kRaftPos);
    float dy = At<float>(player, 0x34) - At<float>(raft, kRaftPos + 4);
    float dz = At<float>(player, 0x38) - At<float>(raft, kRaftPos + 8);
    return dx * dx + dz * dz < kOnRaftRadius * kOnRaftRadius && dy > kOnRaftBelow && dy < kOnRaftAbove;
}

// The same crank class raises 3-2's bridge (it pulls a uOmFollow, not the raft): wound all the way (max 150),
// it is held there so the player can let go and cross.
BYTE* g_held[16];

bool IsHeld(BYTE* crank) {
    for (BYTE* c : g_held)
        if (c == crank) return true;
    return false;
}

void HoldAtTop(BYTE* crank) {
    float max = At<float>(crank, kWindMax);
    if (!IsHeld(crank)) {
        if (At<float>(crank, kWind) < max - 0.5f) return;
        for (BYTE*& c : g_held)
            if (!c) {
                c = crank;
                break;
            }
        Log("crank %p: wound all the way, held there", crank);
    }
    At<float>(crank, kWind) = max;
    At<float>(crank, kWindShadow) = max;
    At<float>(crank, kWindPercent) = 100.0f;
}

void AfterCrankUpdate(BYTE* crank) {
    auto link = At<BYTE*>(crank, kRaft);
    if (link && At<DWORD>(link, 0) != kRaftVtable) return HoldAtTop(crank);
    static DWORD last;
    static bool winding;
    DWORD now = GetTickCount();
    float dt = last ? (now - last) / 1000.0f : 0.0f;
    last = now;
    if (dt > 0.1f) dt = 0.1f;  // after a pause or cutscene
    BYTE* player = SoloPlayer();
    auto raft = At<BYTE*>(crank, kRaft);
    bool on = player && At<BYTE*>(player, kAction) && raft && At<DWORD>(raft, 0) == kRaftVtable &&
              OnRaft(player, raft);
    if (on != winding) {
        winding = on;
        Log("croc crank %p: player %s the raft, crank %s (wind %.0f)", crank, on ? "on" : "off",
            on ? "turning by itself" : "let go", At<float>(crank, kWind));
    }
    if (!on) return;
    float max = At<float>(crank, kWindMax);
    float wind = At<float>(crank, kWind) + kWindPerSecond * dt;
    if (wind > max) wind = max;
    At<float>(crank, kWind) = wind;
    At<float>(crank, kWindShadow) = wind;
    At<float>(crank, kWindPercent) = max > 0 ? wind / max * 100.0f : 0.0f;
}

}  // namespace

// Watcher thread: once a second, look for an unhooked crocodile crank on the unit lists and hook it.
void CrocTick() {
    static DWORD lastScan;
    if (GetTickCount() - lastScan < 1000) return;
    lastScan = GetTickCount();
    ForEachUnit([&](BYTE* u) {
        if (At<DWORD>(u, 0) == kCrankVtable) {
            for (BYTE*& c : g_held)  // a new level can reuse a held crank's address
                if (c == u) c = nullptr;
            HookObject(u, kCrankVtable, AfterCrankUpdate);
        }
        if (At<DWORD>(u, 0) == kIntelVtable && SoloSlot() != 0 && PatchObject(u, kIntelVtable, PatchIntel))
            Log("crocodile AI %p: looks for the player in slot %d", u, SoloSlot());
    });
}
