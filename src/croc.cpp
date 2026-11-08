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
// of its own) finds a player to bite: its think (+0x94, 0x9B93D0; the +0x98 think is empty) runs 0x9B7E70,
// which only looks at player slots below the player count ([[0x11B27DC]+0x34], 1 in solo). As Sheva (slot 1)
// it never saw her, so the crocodiles never came out. The AI gets a vtable copy whose think sees a count that
// reaches the player's slot; the count is put back straight after (62 places read it, many of them not ready
// for an empty slot 0). The AI's update (+0x70) is not called through the object's vtable, so wrapping it
// does nothing.
//
// The AIs think on several worker threads at once. Each think used to raise the count and put back what it
// found, so one think could put it back to 1 while another started, and that crocodile lost the player for a
// frame: its range flags (+0x1BB4) read 0, and a crocodile surfacing then (cEm66 action, 0x8FF9F8: flag 4 clear ->
// +0x1BA0) gave up, dived and waited 5 s, so most came late (traced 2026-11-08; the flags were 0 in ~13% of
// samples). The raise is now shared: the first think in raises it, the last one out puts it back.
#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kIntelVtable = 0xF97460;  // uEmIntel66
constexpr DWORD kThinkSlot = 0x94 / 4, kThink = 0x9B93D0;
constexpr DWORD kPlayerCount = 0x34;      // in the player list

SRWLOCK g_countLock = SRWLOCK_INIT;
int g_thinking;      // crocodile thinks running with the raised count
DWORD g_savedCount;  // the count before the first of them

void __fastcall Think(BYTE* self, void*) {
    auto list = *reinterpret_cast<BYTE**>(kPlayerList);
    DWORD need = SoloSlot() + 1;
    if (!list || Partner() || need <= 1) return ThisCall(kThink, self);
    AcquireSRWLockExclusive(&g_countLock);
    if (g_thinking++ == 0) {
        g_savedCount = At<DWORD>(list, kPlayerCount);
        if (g_savedCount < need) At<DWORD>(list, kPlayerCount) = need;
    }
    ReleaseSRWLockExclusive(&g_countLock);
    ThisCall(kThink, self);
    AcquireSRWLockExclusive(&g_countLock);
    if (--g_thinking == 0) At<DWORD>(list, kPlayerCount) = g_savedCount;
    ReleaseSRWLockExclusive(&g_countLock);
}

void PatchIntel(DWORD* copy) { copy[kThinkSlot] = reinterpret_cast<DWORD>(&Think); }

constexpr DWORD kCrankVtable = 0xFCCDA0;  // uOm08a6
constexpr DWORD kRaftVtable = 0xFEBD00;   // uOm08a3
constexpr DWORD kRaft = 0x1A00;           // crank -> raft
constexpr DWORD kWind = 0x1970, kWindMax = 0x1974, kWindShadow = 0x17FC, kWindPercent = 0x197C;
constexpr DWORD kRaftPos = 0x2D0;         // raft's current centre (x y z)
constexpr DWORD kAction = 0x10F8;
constexpr float kWindPerSecond = 11.5f;
// Standing on the deck the player is about 50 above the raft's centre. The ledge it is boarded from is within
// the old band, so the raft set off before the drop: it now starts only after the player has stood at deck
// height for a moment, and keeps going while the player stays within the wider band (hops on the deck).
constexpr float kOnRaftRadius = 450.0f, kOnRaftBelow = -20.0f, kOnRaftAbove = 200.0f;
constexpr float kDeckBelow = 10.0f, kDeckAbove = 100.0f, kLandSeconds = 0.25f;

float HeightAbove(BYTE* player, BYTE* raft) { return At<float>(player, 0x34) - At<float>(raft, kRaftPos + 4); }

bool OnRaft(BYTE* player, BYTE* raft, float below, float above) {
    float dx = At<float>(player, 0x30) - At<float>(raft, kRaftPos);
    float dy = HeightAbove(player, raft);
    float dz = At<float>(player, 0x38) - At<float>(raft, kRaftPos + 8);
    return dx * dx + dz * dz < kOnRaftRadius * kOnRaftRadius && dy > below && dy < above;
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
    static bool winding;
    static float landed;  // seconds at deck height while not winding
    // The game's frame time ([0x11B209C]+0x28, seconds): GetTickCount's ~16 ms steps made the raft move in
    // jerks above 60 fps (no time on most frames, two frames' worth on the next).
    auto timer = *reinterpret_cast<BYTE**>(0x11B209C);
    float dt = timer ? At<float>(timer, 0x28) : 0.0f;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.1f) dt = 0.1f;  // after a pause or cutscene
    BYTE* player = SoloPlayer();
    auto raft = At<BYTE*>(crank, kRaft);
    bool valid = player && At<BYTE*>(player, kAction) && raft && At<DWORD>(raft, 0) == kRaftVtable;
    bool on;
    if (winding) {
        on = valid && OnRaft(player, raft, kOnRaftBelow, kOnRaftAbove);
    } else {
        landed = valid && OnRaft(player, raft, kDeckBelow, kDeckAbove) ? landed + dt : 0.0f;
        on = landed >= kLandSeconds;
    }
    if (on != winding) {
        winding = on;
        landed = 0.0f;
        Log("croc crank %p: player %s the raft (%.0f above its centre), crank %s (wind %.0f)", crank,
            on ? "on" : "off", valid ? HeightAbove(player, raft) : 0.0f, on ? "turning by itself" : "let go",
            At<float>(crank, kWind));
    }
    if (!on) return;
    float max = At<float>(crank, kWindMax);
    float wind = At<float>(crank, kWind) + kWindPerSecond * dt;
    if (wind > max) wind = max;
    At<float>(crank, kWind) = wind;
    At<float>(crank, kWindShadow) = wind;
    At<float>(crank, kWindPercent) = max > 0 ? wind / max * 100.0f : 0.0f;
}

// 3-1 (area 200) checkpoints in the marsh saved at scene 4200 never get crocodiles. The enemy manager
// (s200 EmMng) starts on SceNo: 4210 goes straight to its marsh zone checks (zone 7 -> 7_wani spawns them);
// 4200 is the chapter start and waits for a one-off check, run once as the area loads, that a player stands
// on the dock (manager 6 zone 27), then for RSF 10 (the dock checkpoint) before it sets 4210. A checkpoint in
// the marsh still at 4200 (seen on a Sheva save) reloads with the player far from the dock, the check fails,
// and the script waits there for good. So while the player is well away from the dock at 4200, the scene (and
// the checkpoint's copy of it) is moved on to 4210, as the dock would have done; the next checkpoint reload
// then starts the script in the marsh.
constexpr DWORD kStageSceneOff = 0x273E0, kStageArea = 0x273D8;
constexpr DWORD kSceneOwner = 0x1234578, kSceneObj = 0x382C, kSnapshot = 0x130, kSnapshotScene = 0x41D0;
constexpr DWORD kMarshArea = 200, kStartScene = 4200, kMarshScene = 4210;
constexpr float kDockX = 14100.0f, kDockZ = -24950.0f, kAwayFromDock = 6000.0f;

// The scene is moved from the watcher thread, so on a load it can lose the race with EmMng's first update: the
// script has then already read 4200 and waits in chkRSF10_demo (zone 27), wait or chkRSF10_begin (RSF 10, which
// such a save does not have). So once the scene is 4210, EmMng found in one of those is put back on chkScenum,
// whose 4210 transition then takes it to the marsh checks as a 4210 load would.
// EmMng is found by its chkScenum: the only state with two transitions, both on SceNo. States come from the FSM
// definition (order +0x10 core, core +4 definition, +8 state array: +0xC count, +0x18 data); a state's transitions
// are at +0x18 (+0x10 capacity), a transition's target at +0xC and condition at +0x20 (0x50 into it: left
// property +8, its name +0x68). The core's current state is at +8.
constexpr DWORD kFsmTask = 0xF9DA80;

const char* LeftName(BYTE* transition) {
    auto c = At<BYTE*>(transition, 0x20);
    BYTE* left = c ? At<BYTE*>(c - 0x50, 8) : nullptr;
    return left ? reinterpret_cast<const char*>(left + 0x68) : "";
}

BYTE* Target(BYTE* state, DWORD i) {
    if (!state || i >= At<DWORD>(state, 0x10)) return nullptr;
    BYTE* tr = At<BYTE**>(state, 0x18)[i];
    return tr ? At<BYTE*>(tr, 0xC) : nullptr;
}

void RescueEmMng() {
    ForEachUnit([](BYTE* u) {
        if (At<DWORD>(u, 0) != kFsmTask) return;
        BYTE* order = At<BYTE*>(u, 0x18);
        BYTE* core = order ? At<BYTE*>(order, 0x10) : nullptr;
        BYTE* def = core ? At<BYTE*>(core, 4) : nullptr;
        if (!def) return;
        DWORD n = At<DWORD>(def, 0xC);
        auto states = At<BYTE**>(def, 0x18);
        if (!states || n > 64) return;
        for (DWORD i = 0; i < n; ++i) {
            BYTE* s = states[i];
            if (!s || At<DWORD>(s, 0x10) != 2) continue;
            BYTE** trs = At<BYTE**>(s, 0x18);
            if (!trs || !trs[0] || !trs[1] || strcmp(LeftName(trs[0]), "SceNo") || strcmp(LeftName(trs[1]), "SceNo"))
                continue;
            BYTE* demo = Target(s, 0);
            BYTE* wait = Target(demo, 0);
            BYTE* begin = Target(wait, 0);
            BYTE*& cur = At<BYTE*>(core, 8);
            if (cur && (cur == demo || cur == wait || cur == begin)) {
                Log("3-1: enemy manager %p was waiting for the dock at scene 4210: back to chkScenum", order);
                cur = s;
            }
            return;
        }
    });
}

void MarshScene() {
    BYTE* stage = Stage();
    BYTE* player = SoloPlayer();
    if (!stage || !player || Partner() || At<DWORD>(stage, kStageArea) != kMarshArea) return;
    float px = At<float>(player, 0x30), pz = At<float>(player, 0x38);
    // Not placed yet (still loading; seen at -3 22): 3-1's origin is nowhere near the dock or the marsh.
    if (px > -1000.0f && px < 1000.0f && pz > -1000.0f && pz < 1000.0f) return;
    float dx = px - kDockX, dz = pz - kDockZ;
    if (dx * dx + dz * dz < kAwayFromDock * kAwayFromDock) return;
    if (At<DWORD>(stage, kStageSceneOff) == kMarshScene) RescueEmMng();
    auto owner = *reinterpret_cast<BYTE**>(kSceneOwner);
    BYTE* scene = owner ? At<BYTE*>(owner, kSceneObj) : nullptr;
    BYTE* snap = scene ? At<BYTE*>(scene, kSnapshot) : nullptr;
    bool live = At<DWORD>(stage, kStageSceneOff) == kStartScene;
    bool saved = snap && At<DWORD>(snap, kSnapshotScene) == kStartScene;
    if (!live && !saved) return;
    if (live) At<DWORD>(stage, kStageSceneOff) = kMarshScene;
    if (saved) At<DWORD>(snap, kSnapshotScene) = kMarshScene;
    Log("3-1: scene 4200 in the marsh (player %.0f %.0f) moved to 4210%s%s: restart the checkpoint for crocodiles",
        At<float>(player, 0x30), At<float>(player, 0x38), live ? " (live)" : "", saved ? " (checkpoint)" : "");
}

}  // namespace

// Watcher thread: once a second, look for an unhooked crocodile crank on the unit lists and hook it.
void CrocTick() {
    static DWORD lastScan;
    if (GetTickCount() - lastScan < 1000) return;
    lastScan = GetTickCount();
    __try {
        MarshScene();
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
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
