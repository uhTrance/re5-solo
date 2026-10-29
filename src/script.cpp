// Level-script player conditions that need a partner, for one player, plus an optional script tracer.
//
// Each player has a script-order object (cFSMOrderPlayer, vtable 0x1040C80) at player +0x1890 that runs the
// level's player conditions. AreaHitPl ("player in zone", vtable +0x184, 0xC40830) first asks for the
// player's partner (0xB5BED0(1) = player +0x2A74) and bails if there is none, before looking at the zone;
// its condition +0x14 picks self (0) or partner (1). Solo, those conditions never ran at all.
//
// Fix: the player's script object gets a vtable copy whose +0x184 makes the player their own partner just
// for that call and clears it again (a permanent partner = self is not safe: the 3-1 boat swap crashes).
// "Partner in zone N" then means "player in zone N". On the crank gate that check runs once, when the crank
// is let go: false (the player is at the crank) takes the branch co-op player 0 takes there (switch zone 4
// on, keep area), which the wave needs; forcing it true would take player 1's branch (a voice line). The
// rest of that moment is zones.cpp.
//
// 1-1 "equip a weapon": native stage logic (0x73A2A9), not a script, counts players 0 and 1 with a weapon
// equipped and, at 2, raises the scene number (SceNo, [[0x11B2158] + 0x5940*[+0x20] + 0x273E0]) to 0x497; a
// level script waits for that (cAIFSM transition on property SceNo) and jumps to the cutscene (0x498). Solo
// the count is at most 1, so at SceNo 0x489 (this moment) the mod raises it once the player is armed.
// Equipped weapon = the inventory slot (player +0x21A0 + 0x30*i, i < 24) with +0x20 == 1 and +0x2D == 1;
// its item (+8) must not be 0x137 (0xC4B700(0xE)).
//
// 1-1 "both players in this room": the same stage controller ([[0x1234578]+0x3830], vtable 0xF54310) has a
// step byte at +0x639. Step 0 (0x73AA86) waits until zone 4 of zone manager 6 holds player 0 AND player 1;
// step 1 compares the two players' distances to a point and sets SceNo 0x4FE (player 0 nearer) or 0x4FF, and
// bails if either player is missing. Scripts wait for those numbers; the nearer player gets the scene's
// action and the other is moved to a fixed spot. Solo: once the player is in that zone, SceNo = 0x4FE +
// the player's slot and step = 2 (both steps done), as if the player were the nearer one.
//
// Cutscene QTEs: sEventPlayer ([0x11B2018]) holds a cEventActionButton at +0xF0: +8 active, +0xC players who
// must press (bit per player), +0x10 players who pressed in time, +0x14 players who are done, +0x1C result
// (-1 pending, bit 0/1 = player 0/1 failed). 0x7B23C0 marks a player done only if 0xC42B90(player) (a
// missing player: never), and 0x7B1BE0 scores only once every needed player is done; at the prompt's end
// frame the cutscene (0x7B2A11) waits with no time limit while the result is pending, so a prompt for the
// missing partner freezes it. Solo: the partner's bit is set in "pressed" and "done", so the partner passes
// and the game scores the prompt itself.
//
// Tracer (re5_solo_trace.txt next to the exe): every script function of the players' and the level's
// script objects logs the first time it runs with each param. The 86 functions are all
// order->vtable[slot](param) (thiscall, ret 4); names come from the registrations (script_names.h).
#include <array>
#include <utility>

#include "objhook.h"
#include "script_names.h"
#include "solo.h"

namespace {

constexpr DWORD kScriptOrder = 0x1890;          // player -> cFSMOrderPlayer
constexpr DWORD kScriptOrderVtable = 0x1040C80;
constexpr DWORD kAreaHitPlSlot = 0x184 / 4;
constexpr DWORD kOrderPlayer = 0x8;             // cFSMOrderPlayer -> player
constexpr DWORD kPartner = 0x2A74;              // player -> partner player
constexpr DWORD kSceNo = 0x273E0;
constexpr DWORD kInventory = 0x21A0;
constexpr DWORD kStageController = 0x1234578, kController = 0x3830, kRoomControllerVtable = 0xF54310;
constexpr DWORD kStep = 0x639;
constexpr DWORD kAreaRoot = 0x11E8660;
constexpr DWORD kEventPlayer = 0x11B2018, kActionButton = 0xF0;

bool PlayerInZone(int manager, DWORD zone) {
    auto root = *reinterpret_cast<BYTE**>(kAreaRoot);
    auto mgr = root ? At<BYTE*>(root, 0xD4 + 4 * manager) : nullptr;
    if (!mgr || zone >= At<DWORD>(mgr, 8)) return false;
    auto z = At<BYTE**>(mgr, 0x14)[zone];
    auto state = z ? At<BYTE*>(z, 8) : nullptr;
    return state && (At<DWORD>(state, 0x10) & (1u << SoloSlot()));
}

bool HasWeaponEquipped(BYTE* player) {
    BYTE* slot = player + kInventory;
    for (int i = 0; i < 24; ++i, slot += 0x30)
        if (At<DWORD>(slot, 0x20) == 1 && At<BYTE>(slot, 0x2D) == 1) return At<DWORD>(slot, 8) != 0x137;
    return false;
}

// Level scripts: uFSMTask units (vtable 0xF9DA80) own a cFSMOrder (vtable 0x103D7A8) at +0x18.
constexpr DWORD kFsmTaskVtable = 0xF9DA80, kLevelScriptVtable = 0x103D7A8;

using Handler = void(__fastcall*)(BYTE* self, void* edx, void* param);
constexpr DWORD kSlots = 0x1A0 / 4;
struct Traced { const DWORD* copy; Handler original[kSlots]; };
Traced g_traced[2];  // [0] cFSMOrderPlayer, [1] level-script cFSMOrder

bool TraceMode() {
    static const bool on = MarkerFile("re5_solo_trace.txt");
    return on;
}

Handler Original(BYTE* self, DWORD i) {
    const DWORD* vt = At<DWORD*>(self, 0);
    for (const auto& t : g_traced)
        if (t.copy == vt) return t.original[i];
    return nullptr;
}

void __fastcall AreaHitPlSolo(BYTE* self, void* edx, void* param) {
    BYTE* player = At<BYTE*>(self, kOrderPlayer);
    Handler original = g_traced[0].original[kAreaHitPlSlot];
    if (!player || At<BYTE*>(player, kPartner)) return original(self, edx, param);
    At<BYTE*>(player, kPartner) = player;
    original(self, edx, param);
    if (At<BYTE*>(player, kPartner) == player) At<BYTE*>(player, kPartner) = nullptr;
}

BYTE* Condition(void* param) {  // param +4 -> table of 0x14-byte entries (+0x10 = condition), +0xC = index
    auto p = static_cast<BYTE*>(param);
    auto table = At<BYTE*>(p, 4);
    return table ? At<BYTE*>(table, At<DWORD>(p, 0xC) * 0x14 + 0x10) : nullptr;
}

const char* ScriptName(DWORD slot) {
    for (const auto& n : kScriptNames)
        if (n.slot == slot) return n.name;
    return "?";
}

// Every call is counted too: HotReport (watcher thread) logs what ran most in the last 5 s, which shows the
// condition a stuck script keeps checking (the first-time log above only shows each call once).
struct Seen { DWORD slot; void* param; BYTE* self; volatile LONG calls; };
Seen g_seen[1024];
volatile LONG g_seenCount;

void Trace(DWORD slot, BYTE* self, void* param) {
    LONG count = g_seenCount;
    for (int i = 0; i < count; ++i)
        if (g_seen[i].slot == slot && g_seen[i].param == param && g_seen[i].self == self) {
            InterlockedIncrement(&g_seen[i].calls);
            return;
        }
    if (count == 1024) return;
    g_seen[count] = {slot, param, self, 1};
    g_seenCount = count + 1;
    bool isPlayer = At<DWORD*>(self, 0) == g_traced[0].copy;
    BYTE* player = isPlayer ? At<BYTE*>(self, kOrderPlayer) : nullptr;
    BYTE* cond = Condition(param);
    Log("trace: %s%d %-16s script %p cond %p [%08lX %08lX %08lX %08lX]", isPlayer ? "player " : "level ",
        player ? At<BYTE>(player, 0x2D9C) : 0, ScriptName(slot), self, cond, cond ? At<DWORD>(cond, 0x10) : 0,
        cond ? At<DWORD>(cond, 0x14) : 0, cond ? At<DWORD>(cond, 0x18) : 0, cond ? At<DWORD>(cond, 0x1C) : 0);
}

template <DWORD N>
void __fastcall TracedCall(BYTE* self, void* edx, void* param) {
    __try {
        Trace(N * 4, self, param);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if constexpr (N == kAreaHitPlSlot) {
        if (At<DWORD*>(self, 0) == g_traced[0].copy && !RecordMode()) return AreaHitPlSolo(self, edx, param);
    }
    Original(self, N)(self, edx, param);
}

template <DWORD... N>
constexpr auto MakeTraced(std::integer_sequence<DWORD, N...>) {
    return std::array<Handler, sizeof...(N)>{&TracedCall<N>...};
}

// Route a script class's vtable copy: with tracing, every script function it has; otherwise only the
// player class's AreaHitPl (solo fix).
void PatchClass(DWORD* copy, DWORD lastSlot, int index) {
    static const auto traced = MakeTraced(std::make_integer_sequence<DWORD, kSlots>{});
    g_traced[index].copy = copy;
    for (DWORD i = 0; i < kSlots; ++i) g_traced[index].original[i] = reinterpret_cast<Handler>(copy[i]);
    if (!TraceMode()) {
        copy[kAreaHitPlSlot] = reinterpret_cast<DWORD>(&AreaHitPlSolo);
        return;
    }
    for (const auto& n : kScriptNames)
        if (n.slot <= lastSlot) copy[n.slot / 4] = reinterpret_cast<DWORD>(traced[n.slot / 4]);
}

void PatchPlayerScript(DWORD* copy) { PatchClass(copy, 0x198, 0); }
void PatchLevelScript(DWORD* copy) { PatchClass(copy, 0x13C, 1); }  // cFSMOrder: functions up to ItemAdd

// Level scripts seen by the scanner, for the state log below (tracing only).
BYTE* volatile g_orders[512];
volatile LONG g_orderCount;

// Nothing simple points to all level scripts, so (tracing only) scan private RW memory once per level.
void PatchLevelScripts() {
    int found = 0;
    MEMORY_BASIC_INFORMATION mbi;
    for (BYTE* a = reinterpret_cast<BYTE*>(0x10000); a < reinterpret_cast<BYTE*>(0x7FFE0000); a += mbi.RegionSize) {
        if (!VirtualQuery(a, &mbi, sizeof mbi)) break;
        if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE || (mbi.Protect & PAGE_GUARD) ||
            !(mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)))
            continue;  // (touching a guard page would eat another thread's stack guard)
        __try {
            auto p = static_cast<DWORD*>(mbi.BaseAddress);
            for (SIZE_T i = 0, n = mbi.RegionSize / 4 - 8; i < n; ++i) {
                if (p[i] != kFsmTaskVtable) continue;
                auto order = reinterpret_cast<BYTE*>(p[i + 6]);
                if (order && !IsBadReadPtr(order, 4) && At<DWORD>(order, 0) == kLevelScriptVtable) {
                    PatchObject(order, kLevelScriptVtable, PatchLevelScript);
                    if (g_orderCount < 512) g_orders[g_orderCount++] = order;
                    ++found;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (found) Log("trace: %d level scripts patched", found);
}

HANDLE g_scanner;
volatile bool g_scanNow;  // a new level loaded: scan without waiting out the 5 s

// The scan takes a second or two, so it runs here rather than on the watcher, which it would hold up.
DWORD WINAPI TraceScanner(LPVOID) {
    while (!g_stopping) {
        for (int i = 0; i < 50 && !g_stopping && !g_scanNow; ++i) Sleep(100);  // 5 s, but quick to stop
        if (g_stopping) break;
        g_scanNow = false;
        __try {
            PatchLevelScripts();
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    return 0;
}

void HotReport() {
    static DWORD last;
    if (GetTickCount() - last < 5000) return;
    last = GetTickCount();
    struct Hot { int i; LONG calls; } hot[8]{};
    for (int i = 0; i < g_seenCount; ++i) {
        LONG c = InterlockedExchange(&g_seen[i].calls, 0);
        for (int k = 0; k < 8; ++k)
            if (c > hot[k].calls) {
                for (int j = 7; j > k; --j) hot[j] = hot[j - 1];
                hot[k] = {i, c};
                break;
            }
    }
    for (const auto& h : hot) {
        if (h.calls < 20) break;
        const Seen& e = g_seen[h.i];
        BYTE* cond = nullptr;
        __try {
            cond = Condition(e.param);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        Log("hot: %5ld x %-16s script %p cond %p", h.calls, ScriptName(e.slot), e.self, cond);
    }
}

}  // namespace

// Tracing: logs every level-script state change (cFSMOrder +0x10, the agent's current state) and every
// enemy (unit line 9) state change (+0x10E0), with the scene number, so a co-op moment's chain of steps can
// be compared with solo.
void StateReport() {
    static BYTE* lastOrder[512];
    static DWORD lastState[512];
    static BYTE* enemies[64];
    static DWORD enemyState[64];
    BYTE* stage = Stage();
    DWORD sceNo = stage ? At<DWORD>(stage, kSceNo) : 0;
    LONG count = g_orderCount;
    for (LONG i = 0; i < count; ++i) {
        __try {
            BYTE* order = g_orders[i];
            if (!order || At<DWORD*>(order, 0) != g_traced[1].copy) continue;  // freed or reused
            DWORD state = At<DWORD>(order, 0x10);
            if (lastOrder[i] == order && lastState[i] != state)
                Log("fsm: script %p state %08lX -> %08lX (scene %lX)", order, lastState[i], state, sceNo);
            lastOrder[i] = order;
            lastState[i] = state;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    __try {
        auto units = *reinterpret_cast<BYTE**>(kUnitRoot);
        BYTE* line = units ? UnitLine(units, 9) : nullptr;
        int n = 0;
        for (auto u = line ? At<BYTE*>(line, 0xC) : nullptr; u && n < 64; u = At<BYTE*>(u, 8), ++n) {
            DWORD state = At<DWORD>(u, 0x10E0);
            if (enemies[n] == u && enemyState[n] != state)
                Log("enemy: %p state %lX -> %lX at %.0f %.0f %.0f (scene %lX)", u, enemyState[n], state,
                    At<float>(u, 0x30), At<float>(u, 0x34), At<float>(u, 0x38), sceNo);
            enemies[n] = u;
            enemyState[n] = state;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Watcher thread: patch the players' script-order objects (new ones each level load): the player's in solo,
// both in record mode (trace only).
void ScriptTick() {
    auto list = *reinterpret_cast<BYTE**>(kPlayerList);
    if (!list) return;
    // Tracing: some areas give the player no script object, so a thread of its own also looks for new level
    // scripts every 5 s (already patched ones no longer match the class vtable and are skipped). Not on this
    // watcher: the memory scan is slow enough to delay closing the partner's slot at a level load.
    if (TraceMode() && !g_scanner) g_scanner = CreateThread(nullptr, 0, TraceScanner, nullptr, 0, nullptr);
    if (TraceMode()) HotReport();
    if (TraceMode()) StateReport();
    if (BYTE* stage = Stage(); stage && !RecordMode()) {
        DWORD& sceNo = At<DWORD>(stage, kSceNo);
        BYTE* player = Partner() ? nullptr : SoloPlayer();
        if (sceNo == 0x489 && player && HasWeaponEquipped(player)) {
            sceNo = 0x497;
            Log("1-1: the player has a weapon equipped, scene 0x489 -> 0x497 (both players armed)");
        }
        auto game = *reinterpret_cast<BYTE**>(kStageController);
        auto controller = game ? At<BYTE*>(game, kController) : nullptr;
        if (controller && At<DWORD>(controller, 0) == kRoomControllerVtable && At<BYTE>(controller, kStep) == 0 &&
            sceNo < 0x4FE && player && PlayerInZone(6, 4)) {
            DWORD nearer = 0x4FE + SoloSlot();  // 0x4FE: player 0 nearer, 0x4FF: player 1
            Log("1-1: the player is in the room, scene 0x%lX -> 0x%lX (both players in, player nearer)", sceNo,
                nearer);
            sceNo = nearer;
            At<BYTE>(controller, kStep) = 2;
        }
    }
    if (auto events = *reinterpret_cast<BYTE**>(kEventPlayer); events && !RecordMode() && !Partner()) {
        BYTE* button = events + kActionButton;
        DWORD partner = 1u << PartnerSlot();
        if (At<DWORD>(button, 8) && At<int>(button, 0x1C) < 0 && (At<DWORD>(button, 0xC) & partner) &&
            !(At<DWORD>(button, 0x14) & partner)) {
            At<DWORD>(button, 0x10) |= partner;
            At<DWORD>(button, 0x14) |= partner;
            Log("QTE: the partner's prompt marked passed (players 0x%lX)", At<DWORD>(button, 0xC));
        }
    }
    for (int slot = 0; slot < 2; ++slot) {
        if (!RecordMode() && slot != SoloSlot()) continue;
        BYTE* player = At<BYTE*>(list, 0x24 + 4 * slot);
        auto order = player ? At<BYTE*>(player, kScriptOrder) : nullptr;
        if (order && At<DWORD>(order, 0) == kScriptOrderVtable && At<BYTE*>(order, kOrderPlayer) == player) {
            PatchObject(order, kScriptOrderVtable, PatchPlayerScript);
            if (slot == SoloSlot() && TraceMode()) g_scanNow = true;  // a new level's scripts load with the player
        }
    }
}

// CoreShutdown, after the watcher has stopped: the trace scanner exits too (its patched objects are restored
// with the rest, objhook.cpp).
void ScriptShutdown() {
    if (!g_scanner) return;
    if (WaitForSingleObject(g_scanner, 10000) != WAIT_OBJECT_0) Log("core: script scanner did not stop");
    CloseHandle(g_scanner);
    g_scanner = nullptr;
}
