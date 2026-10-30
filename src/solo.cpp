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

// 6-2's Uroboros chase (area 503) is staged for two players throughout: its acts, prompts and ending key on
// slot 0, and its camera on the pair. Playing as Sheva the partner's slot is left open there, so the game spawns
// Chris as the AI partner for that area and the chase runs as in the normal one-player game.
constexpr DWORD kStageArea = 0x273D8;
constexpr DWORD kPartnerArea = 503;

bool PartnerArea() {
    BYTE* stage = Stage();
    // Not in record mode: there Chris is a real co-op player, and parking or hiding him would break the game.
    return !RecordMode() && SoloSlot() == 1 && stage && At<DWORD>(stage, kStageArea) == kPartnerArea;
}

// There Chris is not drawn: units draw through vtable +0x24 (0x8300A0 for players, weapons and items), so his
// player class's hook copy (the 120fps fix put him on one) and a private copy for each unit in his inventory
// slots (9 of 0x30 from +0x21D0) get a draw that does nothing. His class copy is put back outside that area,
// where a Chris could be the player.
constexpr DWORD kDrawSlot = 0x24;
constexpr DWORD kInventory = 0x21D0, kInventoryStride = 0x30;
constexpr int kInventorySlots = 9;

void __fastcall NoDraw(void*, void*, void*) {}

void NoDrawPatch(DWORD* copy) { copy[kDrawSlot / 4] = reinterpret_cast<DWORD>(&NoDraw); }

constexpr DWORD kParent = 0x18;
constexpr DWORD kWeaponPanel = 0x2650, kWeaponPanelVtable = 0xFACED4;
constexpr DWORD kModelDraw = 0x8300A0;  // +0x24 of models; other units' +0x24 is something else

// The HUD: each player has a cockpit (uIdCockpit00, its player at +0x1A0). Its update (+0x18, 0x9F8FC0) re-fills
// the partner's entry (+0x1A4, 0x9ED920) with the player's partner (0xB5BED0) if the player list holds him
// (0x7E6370), and then lays out his gauge too. So the player's cockpit updates and draws with Chris out of the
// list for that call, and Chris's own cockpit does neither.
constexpr DWORD kCockpitVtable = 0xF9E480, kCockpitOwner = 0x1A0, kCockpitUpdate = 0x18;
BYTE* volatile g_hiddenChris;

// Runs cockpit slot `slot` without the hidden Chris in the player list (skipped for his own cockpit).
template <class... A>
void WithoutChris(BYTE* cockpit, DWORD slot, A... args) {
    DWORD fn = reinterpret_cast<DWORD*>(kCockpitVtable)[slot / 4];
    BYTE* chris = g_hiddenChris;
    auto list = *reinterpret_cast<BYTE**>(kPlayerList);
    if (!chris || !list) return ThisCall(fn, cockpit, args...);
    if (At<BYTE*>(cockpit, kCockpitOwner) == chris) return;
    BYTE** entry = nullptr;
    for (int i = 0; i < 4 && !entry; ++i)
        if (At<BYTE*>(list, 0x24 + 4 * i) == chris) entry = &At<BYTE*>(list, 0x24 + 4 * i);
    if (entry) *entry = nullptr;
    ThisCall(fn, cockpit, args...);
    if (entry) *entry = chris;
}

void __fastcall CockpitUpdate(BYTE* cockpit, void*) { WithoutChris(cockpit, kCockpitUpdate); }
void __fastcall CockpitDraw(BYTE* cockpit, void*, void* context) { WithoutChris(cockpit, kDrawSlot, context); }

void CockpitPatch(DWORD* copy) {
    copy[kCockpitUpdate / 4] = reinterpret_cast<DWORD>(&CockpitUpdate);
    copy[kDrawSlot / 4] = reinterpret_cast<DWORD>(&CockpitDraw);
}

// Gives a model unit a private vtable copy that draws nothing (ours already, or not a model: skipped).
void HideUnit(BYTE* unit) {
    DWORD vt = unit ? At<DWORD>(unit, 0) : 0;
    if (vt < 0x400000 || vt >= 0x1928000) return;
    if (reinterpret_cast<DWORD*>(vt)[kDrawSlot / 4] == kModelDraw) PatchObject(unit, vt, NoDrawPatch);
}

void HidePartnerTick() {
    static DWORD hiddenClass;
    if (!PartnerArea()) {
        g_hiddenChris = nullptr;
        if (hiddenClass) {
            RestoreHookedSlot(hiddenClass, kDrawSlot);
            Log("partner: Chris's class %08lX draws again", hiddenClass);
            hiddenClass = 0;
        }
        return;
    }
    // Not spawned yet, or out of the player list for a moment (a cockpit call, WithoutChris): keep things as
    // they are rather than draw him again for a frame.
    BYTE* chris = Partner();
    if (!chris) return;
    g_hiddenChris = chris;
    if (DWORD cls = HookedClass(chris); cls && cls != hiddenClass) {
        PatchHookedSlot(chris, kDrawSlot, reinterpret_cast<DWORD>(&NoDraw));
        hiddenClass = cls;
        Log("partner: Chris %p hidden", chris);
    }
    for (int i = 0; i < kInventorySlots; ++i) HideUnit(At<BYTE*>(chris, kInventory + kInventoryStride * i));
    // His own weapon panel (uIdWpSlot00 at +0x2650) still showed, reading "EMPTY".
    if (BYTE* panel = At<BYTE*>(chris, kWeaponPanel); panel && PatchObject(panel, kWeaponPanelVtable, NoDrawPatch))
        Log("partner: Chris's weapon panel %p hidden", panel);
    // Scripted props he is handed (the handgun at the chase start, a uWp02 kept idle at the origin until then)
    // name him as their parent (+0x18), as his inventory units do.
    static DWORD lastScan;
    if (GetTickCount() - lastScan < 50) return;
    lastScan = GetTickCount();
    ForEachUnit([chris](BYTE* unit) {
        if (At<BYTE*>(unit, kParent) == chris) HideUnit(unit);
        if (At<DWORD>(unit, 0) == kCockpitVtable && PatchObject(unit, kCockpitVtable, CockpitPatch))
            Log("partner: HUD cockpit %p draws without Chris", unit);
    });
}

// Hidden, he would still be heard and bumped into, so until the chase starts (cActWait, 0xB24150, waits for
// scene 0x28C8) he is put back 200 m above where he spawned after every move (PartnerAfterMove), and on the
// frame it starts he is set down beside the player for its acts.
constexpr DWORD kPositionCopies[] = {0x30,  0x90,  0xD0,  0x110,  0x1050, 0x1060, 0x10B0,
                                     0x1170, 0x11D0, 0x1370, 0x1380, 0x1390, 0x13A0, 0x1B10};
constexpr DWORD kStageScene = 0x273E0, kChaseScene = 0x28C8;
constexpr float kParkHeight = 20000.0f, kBeside = 100.0f;

void PlaceAt(BYTE* unit, float x, float y, float z) {
    for (DWORD off : kPositionCopies) {
        At<float>(unit, off) = x;
        At<float>(unit, off + 4) = y;
        At<float>(unit, off + 8) = z;
    }
}

void Tick() {
    FilterTick();  // an option of its own, also in vanilla co-op
    BackupTick();  // also in record mode
    PlayerTick();  // likewise (the 120fps fix)
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
    BYTE* stage = Stage();
    DWORD area = stage ? At<DWORD>(stage, kStageArea) : 0;
    if (PartnerArea()) {
        static DWORD logged;
        if (logged != area && (*mask & partnerBit)) {
            logged = area;
            Log("partner slot %d left open in area %lu (the 6-2 chase needs Chris)", PartnerSlot(), area);
        }
    } else if (*mask & partnerBit) {
        LONG before = InterlockedAnd(mask, ~partnerBit);
        Log("partner slot %d closed (mask %X -> %X, area %lu)", PartnerSlot(), before, *mask, area);
    }
    // The game rewrites its flags on some state changes, so re-apply while only the player's slot is open.
    auto flags = reinterpret_cast<volatile LONG*>(game + kGameFlags);
    if ((*mask & 3) == (1 << SoloSlot()) && !(*flags & kSoloFlag)) {
        InterlockedOr(flags, kSoloFlag);
        Log("solo flag set (flags now %08X)", *flags);
    }
    HidePartnerTick();
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
    PauseTick();
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

// After each player's move (snag.cpp's hook, game thread): the hidden 6-2 Chris kept out of the way.
void PartnerAfterMove(BYTE* player) {
    static BYTE* parked;   // the Chris being parked, and where
    static float park[3];
    static BYTE* setDown;  // the Chris already brought back for the chase
    BYTE* stage = Stage();
    if (!stage || !PartnerArea() || player != Partner()) return;
    if (player != parked) {
        parked = player;
        park[0] = At<float>(player, 0x30);
        // Above the player, not above Chris: after a hot reload he is already parked, and his own height
        // would put him another 200 m up each time.
        BYTE* me = PlayerAt(SoloSlot());
        park[1] = (me ? At<float>(me, 0x34) : At<float>(player, 0x34)) + kParkHeight;
        park[2] = At<float>(player, 0x38);
        Log("partner: Chris %p parked out of the way until the chase starts", player);
    }
    if (At<DWORD>(stage, kStageScene) < kChaseScene) {
        PlaceAt(player, park[0], park[1], park[2]);
        setDown = nullptr;  // a retry's new Chris can have the old one's address
    } else if (setDown != player) {
        setDown = player;
        if (BYTE* me = PlayerAt(SoloSlot()))
            PlaceAt(player, At<float>(me, 0x30) + kBeside, At<float>(me, 0x34), At<float>(me, 0x38));
        Log("partner: chase started, Chris set down beside the player");
    }
}

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
