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
//
// Target as Sheva: Ndesu's AI (uEmIntel45, vtable 0xF96C80, a unit of its own; +0x280 = Ndesu) keeps its
// target in +0x2C and Ndesu's +0x1BA4, and about ten places in it (its think, +0x94 0x9AC920, and its states)
// fill both with getPlayer(0). As Sheva slot 0 is empty, so after his opening walk he had no target and
// waited forever ("walks two steps and freezes", 2026-11-04). The walk itself was short too: the state change
// (+0x48) takes player 0's position as its destination and skips it when slot 0 is empty, so he stopped at 6645
// and attacked from there (about 5846 as Chris). Fix: the AI gets a vtable copy whose think, state change and
// resets (+0x68, +0x6C) run with the player also in slot 0 (see AsPlayer0) and
// fill a target left empty afterwards; the watcher also fills an empty target every 10 ms.
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
constexpr DWORD kIntelVtable = 0xF96C80;  // uEmIntel45
constexpr DWORD kThinkSlot = 0x94 / 4, kThink = 0x9AC920;
constexpr DWORD kSetStateSlot = 0x48 / 4, kSetState = 0x9ABF80;
constexpr DWORD kResetSlot = 0x68 / 4, kReset = 0x9AB790;
constexpr DWORD kRestartSlot = 0x6C / 4, kRestart = 0x9AC660;
constexpr DWORD kIntelNdesu = 0x280, kIntelTarget = 0x2C, kNdesuTarget = 0x1BA4;
// Player +0x2DA8: 0 = human, 1 = AI. A refresh while the player stands in slot 0 would give them slot 0's
// (Chris's, AI) setting, and the rumble's "first human player" (0x7E6290) then finds none and crashes (0xB8A458).
constexpr DWORD kPlControl = 0x2DA8;

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

BYTE* g_intel;  // the patched AI

// As Sheva with no partner: give the AI and Ndesu the player as target if theirs is empty.
void FillTarget(BYTE* intel) {
    BYTE* me = Solo();
    if (!me || SoloSlot() == 0 || At<BYTE*>(intel, kIntelTarget)) return;
    At<BYTE*>(intel, kIntelTarget) = me;
    if (BYTE* ndesu = At<BYTE*>(intel, kIntelNdesu)) At<BYTE*>(ndesu, kNdesuTarget) = me;
    Log("Ndesu AI %p: no target (player 0 is empty), player %d targeted", intel, SoloSlot());
}

// Runs fn with the player also in slot 0 (as Sheva with no partner), then empties slot 0 again. The player
// stays in their own slot throughout: other threads read the slots while the AI runs, and moving the player
// (fill 0, empty 1) let the pad rumble's "first human player" scan (0x7E6290) read slot 0 before and slot 1
// after, find nobody and crash at 0xB8A458 (twice, 2026-11-04). Calls nested inside another wrapped call find
// slot 0 filled and leave it alone.
template <class F>
void AsPlayer0(BYTE* intel, F fn) {
    auto list = *reinterpret_cast<BYTE**>(kPlayerList);
    BYTE** slots = list ? &At<BYTE*>(list, 0x24) : nullptr;
    BYTE* me = Solo();
    int solo = SoloSlot();
    bool swap = slots && me && solo != 0 && !slots[0] && slots[solo] == me;
    DWORD control = swap ? At<DWORD>(me, kPlControl) : 0;
    if (swap) slots[0] = me;
    fn();
    if (swap) {
        slots[0] = nullptr;
        if (At<DWORD>(me, kPlControl) != control) {
            Log("Ndesu AI %p: player's control flag changed in slot 0 (%lu -> %lu), put back", intel, control,
                At<DWORD>(me, kPlControl));
            At<DWORD>(me, kPlControl) = control;
        }
    }
    FillTarget(intel);
}

void __fastcall Think(BYTE* self, void*) {
    AsPlayer0(self, [&] { ThisCall(kThink, self); });
}

// +0x48 (0x9ABF80, (state)): the state change. Starting the walk, it reads player 0's position as the
// destination and skips it when slot 0 is empty, so as Sheva the walk stopped after about 250 units.
void __fastcall SetState(BYTE* self, void*, int state) {
    AsPlayer0(self, [&] { ThisCall(kSetState, self, state); });
}

void __fastcall Reset(BYTE* self, void*) {  // +0x68 (0x9AB790): target = player 0
    AsPlayer0(self, [&] { ThisCall(kReset, self); });
}

void __fastcall Restart(BYTE* self, void*) {  // +0x6C (0x9AC660), which can run 0x9AB7C0 (player 0's checks)
    AsPlayer0(self, [&] { ThisCall(kRestart, self); });
}

void PatchIntel(DWORD* copy) {
    copy[kThinkSlot] = reinterpret_cast<DWORD>(&Think);
    copy[kSetStateSlot] = reinterpret_cast<DWORD>(&SetState);
    copy[kResetSlot] = reinterpret_cast<DWORD>(&Reset);
    copy[kRestartSlot] = reinterpret_cast<DWORD>(&Restart);
}

void PatchBulb(DWORD* copy) { copy[kHitAppliedSlot] = reinterpret_cast<DWORD>(&BulbHitApplied); }

}  // namespace

// Watcher thread, every tick (10 ms): patch Ndesu, his bulbs and his AI as soon as they are on the unit lists.
// The AI has to have a target before Ndesu's opening walk starts: the walk's length is set then, and caught
// 0.1 s late (a 1 s scan) he walked a few steps and attacked from 6645 instead of about 5846 (2026-11-04).
void NdesuTick() {
    ForEachUnit([&](BYTE* u) {
        if (At<DWORD>(u, 0) == kNdesuVtable) PatchObject(u, kNdesuVtable, PatchNdesu);
        if (At<DWORD>(u, 0) == kBulbVtable) PatchObject(u, kBulbVtable, PatchBulb);
        if (At<DWORD>(u, 0) == kIntelVtable && PatchObject(u, kIntelVtable, PatchIntel)) g_intel = u;
        if (u == g_intel && At<DWORD>(u, 0) >= 0x1928000) FillTarget(u);  // still on our vtable copy
    });
}
