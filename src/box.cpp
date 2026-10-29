// Two-player co-op boxes (uOmCoopBox and its 5 subclasses, e.g. the 2-2 ending's uOm089b) for one player.
//
// +0x1A48 is how many players the box needs (constructor-set: 1 or 2). Pressing the prompt (event 0, local
// handler vtable +0x188, 0xA9B530) marks the presser at the box (+0x18DC bit slot+4), ready (bit slot, set
// via 0x11B2720), pushing (+0x1A62[slot]) and their side (+0x17FC, 2 bits per slot), and gives them
// pl_act_ready (+0x20 box, +0x24 player). The "go" check is event 0xF1 in the box's ready handler (+0x184,
// 0xA9B1A0): needing 2, it counts ready bits (0xA99E30) and with one only plays the box's shake action (6).
// Setting +0x1A48 to 1 instead does not push (that path also claims a lock, 0x7F2E80). So, as for elevators,
// once the player is ready the box's update marks the partner's slot ready/at the box/pushing on the player's
// side and re-sends 0xF1. The partner's marks are cleared again when the player lets go.
#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kAction = 0x10F8;
constexpr DWORD kPlReadyVtable = 0xFC5E1C;  // cOmCoopBoxAction_pl_act_ready
constexpr DWORD kActObject = 0x20;          // the box (+0x24 is the player)
constexpr DWORD kBoxDti = 0x11F87E4;        // uOmCoopBox
constexpr DWORD kNeeded = 0x1A48, kBits = 0x18DC, kSides = 0x17FC, kPushing = 0x1A62;
constexpr BYTE kReady = 0x01, kAtBox = 0x10;  // << slot
constexpr DWORD kReadyHandler = 0x184;
constexpr DWORD kEventTryGo = 0xF1;

void AfterBoxUpdate(BYTE* box) {
    if (At<int>(box, kNeeded) < 2) return;
    auto& bits = At<BYTE>(box, kBits);
    auto& sides = At<DWORD>(box, kSides);
    int me = SoloSlot(), partner = PartnerSlot();
    BYTE myReady = kReady << me, partnerMarks = static_cast<BYTE>((kReady | kAtBox) << partner);
    bool ready = (bits & myReady) && (bits & (kAtBox << me));
    if (ready && !(bits & partnerMarks)) {
        DWORD side = (sides >> (2 * me)) & 3;  // the partner on the player's side
        sides = (sides & ~(3u << (2 * partner))) | (side << (2 * partner));
        At<BYTE>(box, kPushing + partner) = 1;
        bits |= partnerMarks;
        VCall(box, kReadyHandler, SoloSlot(), kEventTryGo, 0);
        Log("box %p: player ready, partner slot %d marked ready on their side, go sent", box, partner);
        g_boxGoTick = GetTickCount();
    } else if (!(bits & myReady) && (bits & partnerMarks) && At<BYTE>(box, kPushing + partner)) {
        bits &= ~partnerMarks;
        At<BYTE>(box, kPushing + partner) = 0;
        sides &= ~(3u << (2 * partner));
    }
}

}  // namespace

volatile DWORD g_boxGoTick;

// Watcher thread: when the player readies at a co-op box, hook that box's update.
void BoxTick() {
    BYTE* player = SoloPlayer();
    if (!player) return;
    auto act = At<BYTE*>(player, kAction);
    if (!act || At<DWORD>(act, 0) != kPlReadyVtable) return;
    auto box = At<BYTE*>(act, kActObject);
    if (!box) return;
    if (!IsA(box, kBoxDti)) return;  // already ours (heap copy) or not a box
    HookObject(box, At<DWORD>(box, 0), AfterBoxUpdate);
}
