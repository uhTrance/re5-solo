// 4-2 collapsing floor (uOma004_Collapse) for one player.
//
// Walking onto it gives every player the event's ready action (cOmEventPL_ActPre). The collapse's wait
// action (uOma004_Collapse::cActWait, update 0xB09450) then, per slot i, sets game flag 0xC8 + i as that
// player takes position (0x77FCE0: sGame flag bit set), and starts the scene (SceNo 0x183D) only when flags
// 0xC8 AND 0xC9 are both set. Solo the partner's flag never comes, ActPre times out and the player just
// staggers and walks on. Fix: once the player's flag is set, set the partner's too.
//
// The QTE that follows registers a per-frame motion callback on the player's model (0xB073C0, via 0x8380C0:
// slot table at model +0x2DE0 + 0x1B0, 8 slots of 0x14: +0 used, +8 data, +0xC function), and that callback
// reads player slot 1's matrix outright (0x7E6220(1)): null solo, so the game faults at 0xB074F3. The slot is
// heap data, so its function is swapped for a wrapper that puts the solo player in slot 1 of the player list
// just for that call. The callback is registered when the player's cActPlRun starts (vtable +0x18, 0xB0AC80)
// and first runs that same frame, before any object update the mod gets. So cActPlRun's type object
// (DTI 0x11FFDE0, static data; its vtable 0x1006228 slot +4 = create, 0xB07E80) gets a vtable copy whose
// create gives each new cActPlRun a vtable copy whose start swaps the callback right after registering it.
//
// 6-2 Uroboros chase (uOma00c_UroAction, same family): at its press prompt (action cActAB, vtable 0x100CE68)
// the update (+0x30, 0xB23340) waits out a timer, then checks players 0 and 1 (0xB22BB0): a player passes
// on their bit in +0x18DC or if AI-controlled (+0x2DA8 == 1, how the AI partner passes in co-op); an empty
// slot fails. Solo the partner's slot fails (+0x17FC = 1) and the chase takes its death branch (5 instead of
// co-op's 6) and hangs, with no "You died". So that update runs with the solo player standing in for the
// partner (slot filled, bits copied): the partner passes exactly when the player does.
#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kCollapseDti = 0x11FFC78;  // uOma004_Collapse
constexpr DWORD kGame = 0x12340A4;
constexpr DWORD kFlagIsSet = 0x77E460, kFlagSet = 0x77FCE0;  // thiscall sGame (id)
constexpr DWORD kInPositionFlag = 0xC8;                       // + slot
constexpr DWORD kPlayerModel = 0xD4;                          // player vtable: its model (callback table)
constexpr DWORD kCallbacks = 0x2DE0 + 0x1B0, kCallbackSize = 0x14;
constexpr int kCallbackSlots = 8;
constexpr DWORD kQteCallback = 0xB073C0;

using Callback = bool(__cdecl*)(void* data, void* out);

DWORD* g_swapped[kCallbackSlots];  // callback slots now pointing at QteCallback (restored on unload)

bool __cdecl QteCallback(void* data, void* out) {
    auto list = *reinterpret_cast<BYTE**>(kPlayerList);
    BYTE** slot1 = list ? reinterpret_cast<BYTE**>(list + 0x24 + 4) : nullptr;
    bool fill = slot1 && !*slot1;
    if (fill) *slot1 = SoloPlayer();
    bool result = reinterpret_cast<Callback>(kQteCallback)(data, out);
    if (fill) *slot1 = nullptr;
    return result;
}

// Swaps the QTE callback in the solo player's model for the wrapper (game thread).
void SwapQteCallback() {
    BYTE* player = SoloPlayer();
    auto model = player ? VCall<BYTE*>(player, kPlayerModel) : nullptr;
    if (!model) return;
    for (int i = 0; i < kCallbackSlots; ++i) {
        BYTE* entry = model + kCallbacks + kCallbackSize * i;
        auto& fn = At<DWORD>(entry, 0xC);
        if (!At<BYTE>(entry, 0) || fn != kQteCallback) continue;
        fn = reinterpret_cast<DWORD>(&QteCallback);
        g_swapped[i] = &fn;
        static int logged;
        if (logged++ < 3) Log("collapse: QTE callback in slot %d now fills player slot 1", i);
    }
}

constexpr DWORD kRunDti = 0x11FFDE0, kRunDtiVtable = 0x1006228;
constexpr DWORD kRunVtable = 0x1006438, kRunStart = 0xB0AC80, kRunCreate = 0xB07E80;
constexpr int kRunSlots = 11;

DWORD g_runVtable[kRunSlots];  // cActPlRun with our start
DWORD g_dtiVtable[2];          // cActPlRun's type object with our create
BYTE* g_runs[16];              // actions given g_runVtable (restored on unload)

void __fastcall RunStart(BYTE* action, void*) {
    ThisCall(kRunStart, action);
    if (PlayerAt(1) == nullptr) SwapQteCallback();
}

BYTE* __fastcall RunCreate(BYTE* dti, void*) {
    auto action = ThisCall<BYTE*>(kRunCreate, dti);
    if (action && At<DWORD>(action, 0) == kRunVtable) {
        At<DWORD>(action, 0) = reinterpret_cast<DWORD>(g_runVtable);
        static LONG next;
        g_runs[InterlockedIncrement(&next) % 16] = action;
    }
    return action;
}

void AfterCollapseUpdate(BYTE*) {
    auto game = *reinterpret_cast<BYTE**>(kGame);
    if (!game) return;
    DWORD mine = kInPositionFlag + SoloSlot(), partner = kInPositionFlag + PartnerSlot();
    if (!ThisCall<DWORD>(kFlagIsSet, game, mine) || ThisCall<DWORD>(kFlagIsSet, game, partner)) return;
    ThisCall(kFlagSet, game, partner);
    Log("collapse: player in position (flag %lX), partner's flag %lX set", mine, partner);
}

constexpr DWORD kUroDti = 0x12011F4;     // uOma00c_UroAction
constexpr DWORD kUroAction = 0x10F8;     // its current action
constexpr DWORD kPromptVtable = 0x100CE68, kPromptUpdate = 0x30;  // uOma00c_UroAction::cActAB
constexpr int kPromptSlots = 13;
constexpr DWORD kReadyBits = 0x18DC;     // low nibble: bit per slot; high nibble: the same, done

DWORD g_promptVtable[kPromptSlots];
BYTE* g_prompts[16];                     // prompt actions given our vtable (restored on unload)

void __fastcall PromptUpdate(BYTE* action, void*, BYTE* uro) {
    auto list = *reinterpret_cast<BYTE**>(kPlayerList);
    BYTE** partner = list ? reinterpret_cast<BYTE**>(list + 0x24 + 4 * PartnerSlot()) : nullptr;
    bool fill = partner && !*partner && SoloPlayer();
    if (fill) {
        *partner = SoloPlayer();
        auto& bits = At<BYTE>(uro, kReadyBits);
        int mine = SoloSlot(), theirs = PartnerSlot();
        bits = static_cast<BYTE>(bits & ~((1 << theirs) | (0x10 << theirs)));
        if (bits & (1 << mine)) bits |= 1 << theirs;
        if (bits & (0x10 << mine)) bits |= 0x10 << theirs;
    }
    ThisCall(reinterpret_cast<DWORD*>(kPromptVtable)[kPromptUpdate / 4], action, uro);
    if (fill) *partner = nullptr;
}

void AfterUroUpdate(BYTE* uro) {
    BYTE* action = At<BYTE*>(uro, kUroAction);
    if (!action || At<DWORD>(action, 0) != kPromptVtable) return;
    if (!g_promptVtable[0]) {
        memcpy(g_promptVtable, reinterpret_cast<void*>(kPromptVtable), sizeof g_promptVtable);
        g_promptVtable[kPromptUpdate / 4] = reinterpret_cast<DWORD>(&PromptUpdate);
    }
    At<DWORD>(action, 0) = reinterpret_cast<DWORD>(g_promptVtable);
    static int next;
    g_prompts[next++ % 16] = action;
    Log("uro chase %p: press prompt %p checks the partner as the player", uro, action);
}

// Puts `original` back where `*slot` still holds `ours` (a freed object no longer does, or faults).
void Restore(DWORD* slot, DWORD ours, DWORD original) {
    __try {
        if (slot && *slot == ours) *slot = original;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

}  // namespace

// CoreShutdown: nothing may keep pointing into this DLL.
void CollapseShutdown() {
    Restore(reinterpret_cast<DWORD*>(kRunDti), reinterpret_cast<DWORD>(g_dtiVtable), kRunDtiVtable);
    for (DWORD* fn : g_swapped) Restore(fn, reinterpret_cast<DWORD>(&QteCallback), kQteCallback);
    for (BYTE* action : g_runs)
        Restore(reinterpret_cast<DWORD*>(action), reinterpret_cast<DWORD>(g_runVtable), kRunVtable);
    for (BYTE* action : g_prompts)
        Restore(reinterpret_cast<DWORD*>(action), reinterpret_cast<DWORD>(g_promptVtable), kPromptVtable);
}

// Watcher thread: hook every collapse event and Uroboros chase in the unit lists (a hooked one no longer passes IsA).
void CollapseTick() {
    static DWORD lastScan;
    if (GetTickCount() - lastScan < 1000) return;
    lastScan = GetTickCount();
    auto& dtiVtable = At<DWORD>(reinterpret_cast<void*>(kRunDti), 0);
    if (dtiVtable == kRunDtiVtable) {
        memcpy(g_runVtable, reinterpret_cast<void*>(kRunVtable), sizeof g_runVtable);
        g_runVtable[0x18 / 4] = reinterpret_cast<DWORD>(&RunStart);
        memcpy(g_dtiVtable, reinterpret_cast<void*>(kRunDtiVtable), sizeof g_dtiVtable);
        g_dtiVtable[1] = reinterpret_cast<DWORD>(&RunCreate);
        dtiVtable = reinterpret_cast<DWORD>(g_dtiVtable);
        Log("collapse: cActPlRun type object hooked");
    }
    ForEachUnit([&](BYTE* u) {
        if (IsA(u, kCollapseDti)) {
            if (HookObject(u, At<DWORD>(u, 0), AfterCollapseUpdate)) Log("collapse %p hooked", u);
        } else if (IsA(u, kUroDti)) {
            if (HookObject(u, At<DWORD>(u, 0), AfterUroUpdate)) Log("uro chase %p hooked", u);
        }
    });
}
