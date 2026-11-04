// No split screen: drop-in is switched off, the pause menu never runs its join/drop-out pages, and a local
// player 2 joining anyway is dropped.
//
// Drop-in: in game, a pad pressing Start that is not the player's (switching between keyboard and controller
// does it) is caught by 0x723460 (scene in-game state 0x724A20), which books a join (0x720CE0: pad at scene
// +0x350, slot +0x354) and restarts the game scene with two local players (0x725B20 -> 0x716720 join, session
// +0x24 = 2). Solo the joinable slot (0xC42CB0: active and AI-controlled) is -1, as the partner is never in the
// active mask, and the restart crashes loading the area (0x716C80 -> 0x768D0E). 0x723460 does nothing while
// the session's byte +0x54 is set; that is its only reader (only the session reset, 0xC43A39, writes it), so
// the mod holds it at 1.
//
// The pause manager (vtable 0xFA821C, created by 0xA2DDB0, kept at the scene [[0x1234578]+0x382C] +0xE8;
// update +0x18 = 0xA308C0) has one entry per pad slot: 0x68 bytes from +0x48, page at +0x34, page step at
// +0x58. Switching between keyboard and controller looks to the game like a pad disconnecting (0x720A50
// checks each slot's pad, session +0x490 + 4*slot, against the pad manager) and opens the menu in its
// drop-in/drop-out mode. Pages 7 and 9 drop a player out (0x716720 with join 0) and hand the pad to the chosen
// character's slot, page 10 lets a pad join; then the viewports are laid out again or the area reloads. Solo
// that leaves no local player, or a local partner who never spawns, and the reload crashes (0x768D0E). So in
// the mod's copy of the update, an entry on one of those pages goes back to the main pause page (0).
//
// The fallback: 0x716720 (join, slot, pad; uses no `this`) with join 1 sets the slot's bit in the session's
// local-player mask (+0x47C), its control to local (0 at +0x480 + 4*slot) and its pad; 0x76B520 on the split
// manager [0x123457C] lays out one viewport per local player (0xC42B60 counts +0x47C). If the partner's bit
// shows up anyway, it is dropped the way the game's drop-out does: 0x716720 with join 0, then 0x76B520 again.
#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kGame = 0x12340A4, kSessionInfo = 0x1042C;
constexpr DWORD kLocalMask = 0x47C;
constexpr DWORD kNoDropIn = 0x54;                            // byte in the session

void NoDropIn() {
    auto game = *reinterpret_cast<BYTE**>(kGame);
    auto info = game ? At<BYTE*>(game, kSessionInfo) : nullptr;
    if (!info || At<BYTE>(info, kNoDropIn)) return;
    At<BYTE>(info, kNoDropIn) = 1;
    Log("drop-in split screen off (session +0x54 set)");
}
constexpr DWORD kLocalJoin = 0x716720;                       // (bool join, slot, pad), ret 0xC
constexpr DWORD kSplitManager = 0x123457C, kLayOutViewports = 0x76B520;

constexpr DWORD kSceneOwner = 0x1234578, kScene = 0x382C, kPauseMenu = 0xE8;
constexpr DWORD kPauseVtable = 0xFA821C, kPauseUpdateSlot = 0x18 / 4, kPauseUpdate = 0xA308C0;
constexpr DWORD kEntries = 0x48, kEntrySize = 0x68, kEntryPage = 0x34, kEntryStep = 0x58;
constexpr DWORD kEntryOpen = 0;
// The main page's items are the pages they open (offline [1 2 5 3]: 2 and 3 are Restart and Quit, both
// confirmed by 0xA2F660), so only the drop-out and join pages are kept off.
constexpr DWORD kBlockedPages[] = {7, 9, 10};  // drop out (x2), join

void KeepOffJoinPages(BYTE* pause) {
    for (int i = 0; i < 4; ++i) {
        BYTE* entry = pause + kEntries + kEntrySize * i;
        if (!At<BYTE>(entry, kEntryOpen)) continue;
        DWORD page = At<DWORD>(entry, kEntryPage);
        for (DWORD blocked : kBlockedPages) {
            if (page != blocked) continue;
            At<DWORD>(entry, kEntryPage) = 0;
            At<BYTE>(entry, kEntryStep) = 0;
            Log("pause menu: entry %d was on page %lu (join / drop-out), back to the main page", i, page);
        }
    }
}

// The closed menu opens when 0xA2D0C0 says so: Start pressed, or the window not in front (0x77F4B0: sMain's
// not-minimised / foreground / focus bytes, set from Windows each frame by 0x783AF0). With the borderless option
// the window counts as in front for the update's call only, so tabbing out no longer pauses.
constexpr DWORD kMenuState = 0x14;                                     // 0 = closed
constexpr DWORD kMinimised = 0x10548, kForeground = 0x10549, kFocus = 0x1054A;  // on sMain [kGame]

void __fastcall PauseUpdate(BYTE* self, void*) {
    bool solo = !RecordMode() && !Partner();
    if (solo) KeepOffJoinPages(self);
    auto game = *reinterpret_cast<BYTE**>(kGame);
    bool fake = game && KeepRunningUnfocused() && At<BYTE>(self, kMenuState) == 0;
    BYTE saved[3] = {};
    if (fake) {
        memcpy(saved, &At<BYTE>(game, kMinimised), 3);
        At<BYTE>(game, kMinimised) = 0, At<BYTE>(game, kForeground) = 1, At<BYTE>(game, kFocus) = 1;
    }
    ThisCall(kPauseUpdate, self);
    if (fake) memcpy(&At<BYTE>(game, kMinimised), saved, 3);
    if (solo) KeepOffJoinPages(self);
}

void PatchPause(DWORD* copy) { copy[kPauseUpdateSlot] = reinterpret_cast<DWORD>(&PauseUpdate); }

}  // namespace

// Watcher thread: give each pause manager (a new one each time the game makes one) the copy.
void PauseTick() {
    if ((RecordMode() || Partner()) && !KeepRunningUnfocused()) return;
    if (!RecordMode() && !Partner()) NoDropIn();
    auto owner = *reinterpret_cast<BYTE**>(kSceneOwner);
    auto scene = owner ? At<BYTE*>(owner, kScene) : nullptr;
    auto pause = scene ? At<BYTE*>(scene, kPauseMenu) : nullptr;
    if (pause && At<DWORD>(pause, 0) == kPauseVtable) PatchObject(pause, kPauseVtable, PatchPause);
}

// Game thread, every frame (gap.cpp's player-manager update).
void LocalJoinFrame() {
    if (RecordMode() || Partner()) return;
    NoDropIn();
    auto game = *reinterpret_cast<BYTE**>(kGame);
    auto info = game ? At<BYTE*>(game, kSessionInfo) : nullptr;
    auto split = *reinterpret_cast<BYTE**>(kSplitManager);
    if (!info || !split) return;
    // Only both local: while a chapter loads the mask can briefly hold just slot 0, before the choice applies.
    DWORD mask = At<DWORD>(info, kLocalMask);
    DWORD both = (1u << PartnerSlot()) | (1u << SoloSlot());
    if ((mask & both) != both) return;
    ThisCall(kLocalJoin, nullptr, 0, PartnerSlot(), -1);
    ThisCall(kLayOutViewports, split);
    Log("local join: player %d dropped in (local mask %lX -> %lX), dropped out again: no split screen",
        PartnerSlot(), mask, At<DWORD>(info, kLocalMask));
}
