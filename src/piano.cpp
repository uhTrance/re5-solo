// Lost in Nightmares: the piano's door (uOm0202_Door) for one player.
//
// Played, the piano lowers the door next to it (vtable 0x100FDD0, update +0x160 = 0xB30250):
// state +0x1970 1 moves it down from its top (+0x1974) to its bottom (+0x1978), and when nobody is playing any
// more it rises again. In co-op one player keeps playing while the other goes through. Alone the player has to
// stop playing to get there, so the door closes in front of them. Flag 150 (0x96) is the game's own "done" state:
// the door then stays at the bottom (state 3), and the piano offers itself again only as a replay. The game sets
// it once the scene passes 14150; in solo it is set as soon as the door reaches the bottom.
//
// Its emblem doors (uOmUseItem, e.g. the Centaur Emblem's): the use prompt is a scenario zone, and the one for that
// door is never created when playing as Jill (manager 5 zone 1, which the Key_CHP script switches on once the emblem
// is picked up, stays empty), so she gets no prompt. In solo there, the item is used for her as the prompt would:
// once she stands at a use-item with nothing used yet (+0x1970 0) whose required key item she holds, its action
// (vtable +0x188, 0xB47490, kind 1: use the held item, which takes it from the key items) runs for her slot. This
// runs in the use-item's own update (vtable +0x160), so on the game's thread. It waits for the player to press the
// action button there (A on an XInput pad, which is how Steam Input pads reach the game; the button from config.ini
// [JOYPAD] A on a DirectInput pad, F on the keyboard), as the prompt would, rather than going off as soon as they walk up.
//
// The ceiling trap room (uOm0200_TrapCeil, area 700): whoever walks in is caught (flag 200 + slot) and has to shoot the
// lock off (flag 13) while the other pulls the lever back by the gate (uOm08bc), which sets flag 202, moves the scene
// to 14370 and switches the safe in the room on. Alone, nobody is left to pull the lever, so once the lock is off the
// player is moved through the gate (-3412, 2900), pulls it, and goes back for the safe (confirmed 2026-11-09 by hand).
#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kDoorVtable = 0x100FDD0;
constexpr DWORD kPosY = 0x34, kBottom = 0x1978;
constexpr DWORD kStageFlags = 0x23218;  // on Stage(): bit 0x80000000 >> (n & 31) of dword n >> 5
constexpr DWORD kDoneFlag = 150;

constexpr DWORD kUseItemVtable = 0x1016B20, kUpdateSlot = 0x160 / 4, kUseAction = 0xB47490;
constexpr DWORD kUsed = 0x1970, kNeedCount = 0x1980, kNeeds = 0x198C;  // needs: entries, +0xA0 type (0 = key item), +4 id
constexpr DWORD kKeyItems = 0x232E8;  // on Stage(): 8 held key item ids, one byte each
constexpr float kReach = 150.0f;

constexpr DWORD kLockShot = 13, kRescued = 202;
constexpr float kRoomX = -3365.0f, kRoomZ = 3225.0f, kRoomRadius = 400.0f, kOutsideX = -3412.0f, kOutsideZ = 2900.0f;
constexpr DWORD kPositionCopies[] = {0x30,  0x90,  0xD0,  0x110,  0x1050, 0x1060, 0x10B0,
                                     0x1170, 0x11D0, 0x1370, 0x1380, 0x1390, 0x13A0, 0x1B10};

bool Flag(BYTE* stage, DWORD n) { return At<DWORD>(stage, kStageFlags + 4 * (n >> 5)) & (0x80000000u >> (n & 31)); }

void TrapRoomTick(BYTE* stage, BYTE* player) {
    static bool moved;
    if (!Flag(stage, kLockShot)) {  // a checkpoint before the lock clears it
        moved = false;
        return;
    }
    if (moved || Flag(stage, kRescued)) return;
    float dx = At<float>(player, 0x30) - kRoomX, dz = At<float>(player, 0x38) - kRoomZ;
    if (dx * dx + dz * dz > kRoomRadius * kRoomRadius) return;
    moved = true;
    float y = At<float>(player, 0x34);
    for (DWORD off : kPositionCopies) {
        At<float>(player, off) = kOutsideX;
        At<float>(player, off + 4) = y;
        At<float>(player, off + 8) = kOutsideZ;
    }
    Log("trap room: lock shot off, player moved through the gate to pull the lever");
}

bool ActionHeld() {
    if (DInputActionHeld()) return true;
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (pid == GetCurrentProcessId() && (GetAsyncKeyState('F') & 0x8000)) return true;  // the game's action key
    BYTE state[16];
    for (DWORD user = 0; user < 4; ++user)
        if (RawXInputGetState(user, state) == ERROR_SUCCESS && (*reinterpret_cast<WORD*>(state + 4) & 0x1000))
            return true;  // XINPUT_GAMEPAD_A
    return false;
}

// A fresh press at this use-item: held now, not held at its previous update.
bool PressedAt(BYTE* self) {
    static struct { BYTE* self; bool held; } seen[16];
    bool held = ActionHeld();
    for (auto& e : seen)
        if (e.self == self) {
            bool fresh = held && !e.held;
            e.held = held;
            return fresh;
        }
    for (auto& e : seen)
        if (!e.self) {
            e = {self, held};
            break;
        }
    return false;
}

bool Holds(DWORD id) {
    BYTE* stage = Stage();
    if (!stage) return false;
    for (int i = 0; i < 8; ++i)
        if (At<BYTE>(stage, kKeyItems + i) == (id & 0xFF)) return true;
    return false;
}

void __fastcall UseItemUpdate(BYTE* self, void*) {
    ThisCall(reinterpret_cast<DWORD*>(kUseItemVtable)[kUpdateSlot], self);
    BYTE* player = SoloPlayer();
    if (!player || Partner() || !LostInNightmares() || At<BYTE>(self, kUsed)) return;
    float dx = At<float>(self, 0x30) - At<float>(player, 0x30), dz = At<float>(self, 0x38) - At<float>(player, 0x38);
    if (dx * dx + dz * dz > kReach * kReach) return;
    DWORD count = At<DWORD>(self, kNeedCount);
    auto needs = At<BYTE**>(self, kNeeds);
    if (!needs || count == 0 || count > 8) return;
    DWORD key = 0;
    for (DWORD i = 0; i < count && !key; ++i)
        if (needs[i] && At<BYTE>(needs[i], 0xA0) == 0 && Holds(At<DWORD>(needs[i], 4))) key = At<DWORD>(needs[i], 4);
    if (!key || !PressedAt(self)) return;
    Log("use item %p: key item %lu used for the player on their press (no prompt for them here)", self, key);
    ThisCall(kUseAction, self, SoloSlot(), 1, 0);
}

void PatchUseItem(DWORD* copy) { copy[kUpdateSlot] = reinterpret_cast<DWORD>(&UseItemUpdate); }

}  // namespace

// Watcher thread.
void PianoTick() {
    static DWORD last;
    if (GetTickCount() - last < 100) return;
    last = GetTickCount();
    BYTE* stage = Stage();
    if (!stage || !LostInNightmares() || Partner() || !SoloPlayer()) return;
    TrapRoomTick(stage, SoloPlayer());
    volatile LONG& flags = At<volatile LONG>(stage, kStageFlags + 4 * (kDoneFlag >> 5));
    const LONG bit = static_cast<LONG>(0x80000000u >> (kDoneFlag & 31));
    ForEachUnit([&](BYTE* u) {
        if (At<DWORD>(u, 0) == kUseItemVtable && PatchObject(u, kUseItemVtable, PatchUseItem))
            Log("use item %p: patched (Lost in Nightmares)", u);
        if (At<DWORD>(u, 0) != kDoorVtable || (flags & bit)) return;
        float y = At<float>(u, kPosY), bottom = At<float>(u, kBottom);
        if (bottom == 0.0f || y > bottom + 1.0f) return;
        _InterlockedOr(&flags, bit);
        Log("piano: door %p reached the bottom (%.1f): flag %lu set, it stays open", u, y, kDoneFlag);
    });
}
