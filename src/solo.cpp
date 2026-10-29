// Solo mod: removes Sheva (player 2) from story mode.
//
// Steam's DRM refuses to run ("INTEGRITY CHECK FAILURE") if it sees changed code, and it also trips on
// hardware breakpoints and swapped vtable slots in the image. So the mod only writes game *state* (heap
// objects): a watcher thread keeps the session in the game's own one-player shape, and per-object vtable
// copies (objhook.cpp) run code on the game thread where needed.
#include <windows.h>

#include "objhook.h"
#include "solo.h"

namespace {

// sGame (*0x12340A4) +0x1042C -> session info: +0x478 bitmask of active player slots (story: 3), +0x480
// control per slot (1 local, 2 remote). Slots are per character (0 Chris, 1 Sheva). The level's spawn loop
// (0x7E99F0) spawns one player per set bit, so clearing the partner's bit before a level loads means the
// partner is never created. The player's character is the one chosen for the chapter (objhook.h SoloSlot;
// re5_solo_sheva.txt next to the exe forces Sheva).
constexpr DWORD kGame = 0x12340A4;
constexpr DWORD kSessionInfo = 0x1042C;
constexpr DWORD kActiveMask = 0x478;

// sGame +0x10434 flag 0x80000: the game's own leftover "solo, no partner" mode (set at 0x718597 right after
// it builds a one-player slot mask). Every co-op object asks requiredPlayers() (0xA83070), which returns
// 1 with this flag and 2 without, so with it set a door/gate/box/lever works for one player.
constexpr DWORD kGameFlags = 0x10434;
constexpr DWORD kSoloFlag = 0x80000;

void Tick() {
    FilterTick();  // an option of its own, also in vanilla co-op
    BackupTick();  // also in record mode
    if (RecordMode()) {  // vanilla co-op, only watch the jumps
        RecordTick();
        ScriptTick();
        return;
    }
    auto game = *reinterpret_cast<BYTE* volatile*>(kGame);
    if (!game) return;
    auto info = *reinterpret_cast<BYTE* volatile*>(game + kSessionInfo);
    if (!info) return;
    ShevaTick();
    auto mask = reinterpret_cast<volatile LONG*>(info + kActiveMask);
    LONG partnerBit = 1 << PartnerSlot();
    if (*mask & partnerBit) {
        LONG before = InterlockedAnd(mask, ~partnerBit);
        Log("partner slot %d closed (mask %X -> %X)", PartnerSlot(), before, *mask);
    }
    // The game rewrites its flags on some state changes, so re-apply while only the player's slot is open.
    auto flags = reinterpret_cast<volatile LONG*>(game + kGameFlags);
    if ((*mask & 3) == (1 << SoloSlot()) && !(*flags & kSoloFlag)) {
        InterlockedOr(flags, kSoloFlag);
        Log("solo flag set (flags now %08X)", *flags);
    }
    BoatTick();
    GapTick();
    UroborosTick();
    InventoryTick();
    SplitTick();
    ElevatorTick();
    TwistTick();
    CollapseTick();
    PulleyTick();
    VoiceTick();
    CrankTick();
    BoxTick();
    CrocTick();
    LeverTick();
    LiftTick();
    JeepTick();
    NdesuTick();
    EnemyHpTick();
    WeskerTick();
    DebugTick();
    ScriptTick();
    ZoneTick();
}

HANDLE g_watch;

DWORD WINAPI Watch(LPVOID) {
    while (!g_stopping) {
        __try {
            Tick();
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        Sleep(10);
    }
    return 0;
}

}  // namespace

volatile bool g_stopping;

void SoloInit() {
    g_watch = CreateThread(nullptr, 0, Watch, nullptr, 0, nullptr);
    if (RecordMode())
        Log("RECORD MODE (re5_solo_record.txt): Sheva stays, co-op is vanilla, jump landings are logged");
    else
        Log("watching player slots");
}

// CoreShutdown: the watcher finishes its tick and exits.
void SoloShutdown() {
    g_stopping = true;
    if (g_watch && WaitForSingleObject(g_watch, 3000) != WAIT_OBJECT_0) Log("core: watcher did not stop");
    if (g_watch) CloseHandle(g_watch);
    g_watch = nullptr;
}
