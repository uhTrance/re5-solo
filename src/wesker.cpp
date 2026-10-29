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

}  // namespace

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
