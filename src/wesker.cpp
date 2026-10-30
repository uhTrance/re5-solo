// Final Wesker, first phase (6-3, uEm80 / uEm81): one player holds Wesker (pinion) and the other injects
// him. Each step is a prompt whose press goes to Wesker's press handler (vtable +0x230, 0x9084B0,
// (player, type, ?)) and on to +0x234 (0x9085E0, (player, type, result)): type 1 = hold (Wesker action
// 0x6A, the holder is stored at +0x1F64), type 2 = inject (action 0x6C, only while his state +0x10E0 is
// 0x6A; the injector is the presser, 0x920C50). The injection action (0x91BB00) gives the holder
// cEm80ActInjection_PL2 and the injector cEm80ActInjection_PL, and sets the injected flags only when there
// is an injector. Solo, the player holds and nobody can press inject.
// Fix: once the player has held Wesker for kInjectDelay with no partner, the mod presses inject for the
// player: +0x234(slot, 2, 0) on the game thread, from Wesker's update (per-object vtable copy).
#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kWeskerVtables[] = {0xF8BE60, 0xF8C300};  // uEm80, uEm81
constexpr DWORD kState = 0x10E0, kHeld = 0x6A;
constexpr DWORD kHolder = 0x1F64;
constexpr DWORD kPromptResult = 0x234;  // (player, type, result 0 = success)
constexpr int kInject = 2;
constexpr DWORD kInjectDelay = 1000;  // ms held before the injection
// Enemies' per-frame update is move (+0x1C, uEnemy 0x9D9B80); their +0x160 is a shared uEnemy function and
// +0x14 is setup, so neither runs the hook during the hold.
constexpr DWORD kMoveSlot = 0x1C;

BYTE* g_held;      // Wesker currently held by the player
DWORD g_heldSince;
bool g_injected;   // for this hold

void AfterWeskerUpdate(BYTE* wesker) {
    BYTE* player = SoloPlayer();
    if (!player || Partner()) return;
    bool held = At<DWORD>(wesker, kState) == kHeld && At<BYTE*>(wesker, kHolder) == player;
    if (!held) {
        if (g_held == wesker) g_held = nullptr;
        return;
    }
    if (g_held != wesker) {
        g_held = wesker;
        g_heldSince = GetTickCount();
        g_injected = false;
        Log("Wesker %p: held by the player", wesker);
    }
    if (!g_injected && GetTickCount() - g_heldSince >= kInjectDelay) {
        g_injected = true;
        Log("Wesker %p: no partner, injecting for player %d", wesker, SoloSlot());
        VCall(wesker, kPromptResult, SoloSlot(), kInject, 0);
    }
}

// The volcano (area 508): at scene 11625 a cutscene splits the pair, Chris down onto the lava rocks (1543, -300,
// -1824) where the fight goes on, Sheva up onto a ledge (1436, 292, -2994) to wait for him to save her. Playing
// as Sheva nobody is below, so whenever she turns up on that ledge from then on, she is moved to Chris's spot.
constexpr DWORD kStageArea = 0x273D8, kStageScene = 0x273E0;
constexpr DWORD kVolcano = 508, kSplitScene = 11625;
constexpr float kLedge[3] = {1436.0f, 292.0f, -2994.0f};
constexpr float kChrisSpot[3] = {1543.0f, -300.0f, -1824.0f};
constexpr float kChrisFacing[2] = {0.545f, 0.839f};  // rotation quaternion y, w (+0x44, +0x4C)
constexpr float kOnLedge = 400.0f;
constexpr DWORD kPositionCopies[] = {0x30,  0x90,  0xD0,  0x110,  0x1050, 0x1060, 0x10B0,
                                     0x1170, 0x11D0, 0x1370, 0x1380, 0x1390, 0x13A0, 0x1B10};

bool OnLedge(BYTE* player) {
    float dx = At<float>(player, 0x30) - kLedge[0], dy = At<float>(player, 0x34) - kLedge[1];
    float dz = At<float>(player, 0x38) - kLedge[2];
    return dx * dx + dy * dy + dz * dz < kOnLedge * kOnLedge;
}

}  // namespace

// After each player's move (snag.cpp's hook, game thread).
void VolcanoAfterMove(BYTE* player) {
    BYTE* stage = Stage();
    if (!stage || SoloSlot() != 1 || player != SoloPlayer() || Partner()) return;
    if (At<DWORD>(stage, kStageArea) != kVolcano || At<DWORD>(stage, kStageScene) < kSplitScene || !OnLedge(player))
        return;
    for (DWORD off : kPositionCopies)
        for (int i = 0; i < 3; ++i) At<float>(player, off + 4 * i) = kChrisSpot[i];
    At<float>(player, 0x40) = 0.0f;
    At<float>(player, 0x44) = kChrisFacing[0];
    At<float>(player, 0x48) = 0.0f;
    At<float>(player, 0x4C) = kChrisFacing[1];
    Log("volcano: Sheva put down where Chris fights (the split sent her up to the ledge)");
}

// Watcher thread: once a second, hook any Wesker (first-phase classes) on the unit lists.
void WeskerTick() {
    static DWORD lastScan;
    if (GetTickCount() - lastScan < 1000) return;
    lastScan = GetTickCount();
    ForEachUnit([&](BYTE* u) {
        for (DWORD vt : kWeskerVtables)
            if (At<DWORD>(u, 0) == vt) HookObject(u, vt, AfterWeskerUpdate, kMoveSlot);
    });
}
