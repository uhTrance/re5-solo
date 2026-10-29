// Ndesu (uEm45, vtable 0xF83828, 2-3 boss) prompts: during an attack each player gets a prompt whose press
// calls Ndesu's press handler (vtable +0x230, 0x8E08B0, thiscall (player, type, ?)). For type 0 (duck)
// and 1 it marks the presser in +0x1BB4 (0x40 = player 0, 0x80 = player 1) and, unless sGame-info +0x56C
// is set, only acts once BOTH bits are set: then +0x234 (0x8E09B0, (player, type, ?)) puts the players
// into their side (type 0: player 0 crouches; 1 and 2: both players, nothing at all if either is
// missing). Solo, the player's press alone never acts.
// Fix: Ndesu gets a vtable copy. +0x230 with no partner marks the partner as pressed too, then runs the
// original. +0x234 with no partner does the player's half of the action as the original does.
// 0xF832E0 is another uEm45 vtable, not the live one.
#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kNdesuVtable = 0xF83828;  // uEm45
constexpr DWORD kPressSlot = 0x230 / 4, kPairedSlot = 0x234 / 4;
constexpr DWORD kPress = 0x8E08B0, kPaired = 0x8E09B0;
constexpr DWORD kPressed = 0x1BB4, kPressedBit = 0x40;  // << slot
constexpr DWORD kSetAction = 0xE0;                         // player vtable: (params, source, 0, 0)
// Per slot and type: Crouch, Hold, Stagger (player 0) / Crouch, Hold2P, Stagger2P (player 1). The game only
// ever crouches player 0; player 1 gets the same crouch (unverified).
constexpr DWORD kParams[2][3] = {{0x11EE8C8, 0x11EE8EC, 0x11EE934}, {0x11EE8C8, 0x11EE910, 0x11EE958}};
constexpr DWORD kInScene = 0x2F34;

BYTE* Solo() { return Partner() ? nullptr : SoloPlayer(); }

void __fastcall Press(BYTE* self, void*, int player, int type, int c) {
    if (Solo() && (type == 0 || type == 1)) {
        At<DWORD>(self, kPressed) |= kPressedBit << PartnerSlot();
        Log("Ndesu %p: player %d pressed (type %d), partner's press marked too", self, player, type);
    }
    ThisCall(kPress, self, player, type, c);
}

// The original's steps for the player alone. Type 0 for slot 0 is left to the original (it only uses player 0).
void __fastcall Paired(BYTE* self, void*, int player, int type, int c) {
    BYTE* me = Solo();
    int slot = SoloSlot();
    if (type < 0 || type > 2 || !me || (type == 0 && slot == 0)) {
        ThisCall(kPaired, self, player, type, c);
        return;
    }
    Log("Ndesu %p: paired action type %d without a partner, player %d's half only", self, type, slot);
    if (type != 2) {
        At<DWORD>(self, kPressed) &= ~0xC0u;
        if (BYTE* link = At<BYTE*>(self, 0x1F78)) ThisCall(0xC46C70, link + 0x1290, 0, -1, 0);
    }
    VCall(me, kSetAction, kParams[slot][type], self, 0, 0);
    At<BYTE>(me, kInScene) = 1;
    if (type != 2)
        if (BYTE* link = At<BYTE*>(self, 0x1F78))
            if (BYTE* p = At<BYTE*>(link + 0x1290, 0xC)) At<BYTE>(p, 0x48) = 0;
    At<BYTE>(self, 0x1F81) = 0;
}

void PatchNdesu(DWORD* copy) {
    copy[kPressSlot] = reinterpret_cast<DWORD>(&Press);
    copy[kPairedSlot] = reinterpret_cast<DWORD>(&Paired);
}

}  // namespace

// Watcher thread: once a second, patch Ndesu if he is on the unit lists.
void NdesuTick() {
    static DWORD lastScan;
    if (GetTickCount() - lastScan < 1000) return;
    lastScan = GetTickCount();
    ForEachUnit([&](BYTE* u) {
        if (At<DWORD>(u, 0) == kNdesuVtable) PatchObject(u, kNdesuVtable, PatchNdesu);
    });
}
