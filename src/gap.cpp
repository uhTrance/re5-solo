// Co-op gap jumps and boosts ("Assist jump") for one player: pressing the prompt teleports the player
// across/up.
//
// Both are uOmCoopJumpBroad; the subclass only picks the pair animation set (pawn\om\omf111 = broad jump
// across a gap, omf112 = jump up 4 m), so the subclass decides where the player lands.
//
// Vanilla: each player who presses gets cActPlJumpReady and waits; when both are ready the gap's cActJump
// enter (0xAA9450) gives player 1 cActPlJump (the throw) and player 0 cActPlJumpSecond (the leap), and bails
// (both back to idle) if either is missing. Neither action moves its player (the crossing is in the pair
// animation), so solo either one only plays on the spot. Instead, once the player is ready, the object's
// update puts them back on foot and moves them along its axis (0x80 row of its world matrix), away from
// their side, by the subclass's landing offset. Measured: 600 across reaches the far side of the 4-2 gap; a
// boosted player lands 101.9 past the boost's marker and 400 up. The target is a little above the ledge and
// the game settles the player onto it.
//
// Both jump actions wait (step 1) for the player manager's "everyone is in position" flag (+0x38, getter
// 0x7E6460), which the manager's update (0x7E9520) sets only when at least 2 players (hard-coded) have
// +0x10ED set. Only co-op player actions read that flag, so the manager hook below sets it whenever the
// player is in position.
#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kAction = 0x10F8;               // player's current action object
constexpr DWORD kJumpReadyVtable = 0xFC9CB8;    // uOmCoopJumpBroad::cActPlJumpReady
constexpr DWORD kActObject = 0x24;              // the gap
constexpr DWORD kJumpBroadDti = 0x11F9254;      // uOmCoopJumpBroad
struct Landing {
    DWORD dti;      // subclass
    float forward;  // along the axis, away from the player's side
    float up;
};
constexpr Landing kLandings[] = {
    {0x11F9278, 600.0f, 0.0f},    // uOmf111: broad jump across a gap
    {0x11F929C, 102.0f, 405.0f},  // uOmf112: jump up 4 m (boost); measured 101.9 fwd, 400 up
};

// Every copy of the player's position; writing only +0x30 is undone (collision sweep from the old one).
constexpr DWORD kPositionCopies[] = {0x30, 0x90, 0xD0, 0x110, 0x1050, 0x1060, 0x10B0, 0x1170, 0x11D0,
                                     0x1370, 0x1380, 0x1390, 0x13A0, 0x1B10};

constexpr DWORD kManagerVtable = 0xF653AC;      // player manager, *0x11B27DC
constexpr DWORD kManagerUpdate = 0x18;          // 0x7E9520
constexpr DWORD kAllInPosition = 0x38;          // byte, manager
constexpr DWORD kInPosition = 0x10ED;           // byte, player

void CrossGap(BYTE* gap, BYTE* player) {
    DWORD dti = VCall<DWORD>(gap, 0x10);  // getDTI
    const Landing* land = nullptr;
    for (const Landing& l : kLandings)
        if (l.dti == dti) land = &l;
    if (!land) {
        Log("jump %p: unknown subclass (DTI %08X), left alone", gap, dti);
        return;
    }
    auto centre = &At<float>(gap, 0x30);
    auto axis = &At<float>(gap, 0x80);
    float side = 0;
    for (int i = 0; i < 3; ++i) side += (At<float>(player, 0x30 + 4 * i) - centre[i]) * axis[i];
    float dir = side > 0 ? -1.0f : 1.0f;
    float target[3];
    for (int i = 0; i < 3; ++i) target[i] = centre[i] + dir * land->forward * axis[i];
    target[1] += land->up;
    VCall(player, 0xDC, 0, 1, 0, -1, 0);  // back to the normal on-foot action
    for (DWORD off : kPositionCopies)
        for (int i = 0; i < 3; ++i) At<float>(player, off + 4 * i) = target[i];
    Log("jump %p (DTI %08X): player moved to %.0f %.0f %.0f", gap, dti, target[0], target[1], target[2]);
}

void AfterGapUpdate(BYTE* gap) {
    BYTE* player = SoloPlayer();
    if (!player) return;
    auto act = At<BYTE*>(player, kAction);
    if (!act || At<DWORD>(act, 0) != kJumpReadyVtable) return;
    if (At<BYTE*>(act, kActObject) != gap) return;
    CrossGap(gap, player);
}

void AfterManagerUpdate(BYTE* manager) {
    BYTE* player = At<BYTE*>(manager, 0x24 + 4 * SoloSlot());
    if (player && At<BYTE>(player, kInPosition)) At<BYTE>(manager, kAllInPosition) = 1;
    if (player) SplitFrame(player);  // 4-1 separation (split.cpp)
}

}  // namespace

// Watcher thread: hook the player manager once per load, and the gap the player readies at.
void GapTick() {
    auto manager = *reinterpret_cast<BYTE**>(kPlayerList);
    if (manager && At<DWORD>(manager, 0) == kManagerVtable)
        HookObject(manager, kManagerVtable, AfterManagerUpdate, kManagerUpdate);
    BYTE* player = SoloPlayer();
    if (!player) return;
    auto act = At<BYTE*>(player, kAction);
    if (!act || At<DWORD>(act, 0) != kJumpReadyVtable) return;
    auto gap = At<BYTE*>(act, kActObject);
    if (!gap) return;
    if (!IsA(gap, kJumpBroadDti)) return;  // already ours (heap copy) or not a co-op jump
    HookObject(gap, At<DWORD>(gap, 0), AfterGapUpdate);
}
