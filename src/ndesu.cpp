// Ndesu (uEm45, vtable 0xF83828, 2-3 boss) prompts: during an attack each player gets a prompt whose press
// calls Ndesu's press handler (vtable +0x230, 0x8E08B0, thiscall (player, type, ?)). For type 0 (duck)
// and 1 it marks the presser in +0x1BB4 (0x40 = player 0, 0x80 = player 1) and, unless sGame-info +0x56C
// is set, only acts once BOTH bits are set: then +0x234 (0x8E09B0, (player, type, ?)) puts the players
// into their side (type 0: player 0 crouches; 1 and 2: both players, nothing at all if either is
// missing). Solo, the player's press alone never acts.
// Fix: Ndesu gets a vtable copy. +0x230 with no partner marks the partner as pressed too, then runs the
// original. +0x234 with no partner does the player's half of the action as the original does.
// 0xF832E0 is another uEm45 vtable, not the live one.
//
// Hit cooldown at high frame rates: an applied hit (0x9E0840) arms a per-part cooldown, the hit's +0x34
// (0.1 s for the turret), in +0x1E10[part]; it becomes +0x1DFC[part] after the frame's hits and ticks down by
// frame time (0x963500), and while it is above 0 that part ignores hits (0x9E0700). The turret fires every
// 0.104 s, so its hits clear 0.1 s by one frame at 30 fps, but at 120 fps a bullet arriving one 8 ms frame
// early (Ndesu or the aim moving) is thrown away: about 6% of body hits, measured 2026-11-02. Fix: right after
// the cooldown is armed (vtable +0x200, called next by 0x9E0840), shorten it by (1/30 s - frame time), the
// leeway 30 fps frames give. Ndesu's body and his bulbs (uEm4502, vtable 0xF83B90) each get this.
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
constexpr DWORD kBulbVtable = 0xF83B90;  // uEm4502
constexpr DWORD kHitAppliedSlot = 0x200 / 4;
constexpr DWORD kNdesuHitApplied = 0x8E0620, kBulbHitApplied = 0x8E3B40;
constexpr DWORD kCooldownArmed = 0x1E10, kHitCooldown = 0x34;  // [5] floats; in the hit record
constexpr DWORD kFrameTimer = 0x11B209C, kFrameSeconds = 0x28;

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

void ShortenCooldown(BYTE* self, BYTE* hit) {
    auto timer = *reinterpret_cast<BYTE**>(kFrameTimer);
    float cut = 1.0f / 30.0f - (timer ? At<float>(timer, kFrameSeconds) : 0.0f);
    if (!hit || cut <= 0.0f) return;
    float armed = At<float>(hit, kHitCooldown);
    for (int i = 0; i < 5; ++i) {
        float& c = At<float>(self, kCooldownArmed + 4 * i);
        if (c > 0.0f && c == armed) c = c > cut ? c - cut : 0.001f;  // 0 would skip arming it at all
    }
}

void __fastcall NdesuHitApplied(BYTE* self, void*, BYTE* hit) {
    ThisCall(kNdesuHitApplied, self, hit);
    ShortenCooldown(self, hit);
}

void __fastcall BulbHitApplied(BYTE* self, void*, BYTE* hit) {
    ThisCall(kBulbHitApplied, self, hit);
    ShortenCooldown(self, hit);
}

void PatchNdesu(DWORD* copy) {
    copy[kPressSlot] = reinterpret_cast<DWORD>(&Press);
    copy[kPairedSlot] = reinterpret_cast<DWORD>(&Paired);
    copy[kHitAppliedSlot] = reinterpret_cast<DWORD>(&NdesuHitApplied);
}

void PatchBulb(DWORD* copy) { copy[kHitAppliedSlot] = reinterpret_cast<DWORD>(&BulbHitApplied); }

}  // namespace

// Watcher thread: once a second, patch Ndesu and his bulbs if they are on the unit lists.
void NdesuTick() {
    static DWORD lastScan;
    if (GetTickCount() - lastScan < 1000) return;
    lastScan = GetTickCount();
    ForEachUnit([&](BYTE* u) {
        if (At<DWORD>(u, 0) == kNdesuVtable) PatchObject(u, kNdesuVtable, PatchNdesu);
        if (At<DWORD>(u, 0) == kBulbVtable) PatchObject(u, kBulbVtable, PatchBulb);
    });
}
