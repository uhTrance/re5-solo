// The players' vtable copies, and the 120fps fix: above 60 fps a player can get stuck for good on some spots.
//
// Every player in the player list (Chris and Sheva, solo or co-op) gets a HookObject copy of its class vtable:
// after the per-frame move (+0x1C) it runs death.cpp's check, and with the 120fps fix on (re5_solo_120fps.txt
// next to the exe, also in vanilla co-op) +0xD8 goes through Project below.
//
// Each frame the move (0xB58840) saves the position (+0x1060), applies the motion, records the result
// (+0x1050), lets +0xD8 (0xB6A670, the same in every player class) project that step onto the ground, then
// runs the collision passes (+0x3C/+0x40 -> 0x588230/0x58B340). Those read no frame time; they work on the
// step. At 120 fps the step is half as long, and on some spots (all logged at 120 only) the collision puts
// the player back exactly where the frame started, every frame: the same short step ends in the same place.
// At 60 fps the longer step clears it. So once a player has been put back to the same start two frames
// running while moving, the mod hands the collision the step 60 fps would (this frame's step times
// 1/60 s / frame time) before +0xD8; the game's own collision still decides where the player ends up.
#include <cmath>

#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kMoveSlot = 0x1C;
constexpr DWORD kProjectSlot = 0xD8, kProject = 0xB6A670;
constexpr DWORD kStart = 0x1060, kMoved = 0x1050;
constexpr DWORD kMovedCopies[] = {0x30, 0x90, 0xD0};  // where the moved position sits before +0xD8
constexpr DWORD kTimer = 0x11B209C, kFrameSeconds = 0x28;
constexpr float kMinStep = 0.5f, kSame = 0.01f;
constexpr int kSlots = 4;

bool FixOn() {
    static const bool on = [] {
        bool found = MarkerFile("re5_solo_120fps.txt");
        if (found) Log("120fps fix on (re5_solo_120fps.txt)");
        return found;
    }();
    return on;
}

struct Track {
    float lastStart[3];
    float lastStep;
    int heldBack;
};
Track g_track[kSlots];

Track* TrackOf(BYTE* player) {
    for (int slot = 0; slot < kSlots; ++slot)
        if (PlayerAt(slot) == player) return &g_track[slot];
    return nullptr;
}

void __fastcall Project(BYTE* player, void*) {
    if (Track* t = TrackOf(player)) {
        float* start = &At<float>(player, kStart);
        float* moved = &At<float>(player, kMoved);
        float step[3] = {moved[0] - start[0], moved[1] - start[1], moved[2] - start[2]};
        float len = std::hypot(step[0], step[2]);
        bool same = std::fabs(start[0] - t->lastStart[0]) < kSame && std::fabs(start[2] - t->lastStart[2]) < kSame;
        t->heldBack = len > kMinStep && t->lastStep > kMinStep && same ? t->heldBack + 1 : 0;
        for (int i = 0; i < 3; ++i) t->lastStart[i] = start[i];
        t->lastStep = len;
        auto timer = *reinterpret_cast<BYTE**>(kTimer);
        float seconds = timer ? At<float>(timer, kFrameSeconds) : 0.0f;
        float factor = seconds > 0.0f ? (1.0f / 60.0f) / seconds : 1.0f;
        if (t->heldBack >= 2 && factor > 1.5f) {
            if (factor > 4.0f) factor = 4.0f;
            for (DWORD off : kMovedCopies) {
                At<float>(player, off) = start[0] + step[0] * factor;
                At<float>(player, off + 8) = start[2] + step[2] * factor;
            }
            moved[0] = start[0] + step[0] * factor;
            moved[2] = start[2] + step[2] * factor;
            if (t->heldBack == 2)
                Log("120fps fix: player %d held at %.1f %.1f %.1f, trying the 60 fps step (x%.1f, %.2f units)",
                    static_cast<int>(t - g_track), start[0], start[1], start[2], factor, len * factor);
        }
    }
    ThisCall(kProject, player);
}

void AfterPlayerMove(BYTE* player) { DeathAfterMove(player); }

}  // namespace

// Watcher thread: hook each player (new objects each level), and add the 120fps fix to the copy when it is on.
void PlayerTick() {
    for (int slot = 0; slot < kSlots; ++slot) {
        BYTE* player = PlayerAt(slot);
        if (!player) continue;
        HookObject(player, At<DWORD>(player, 0), AfterPlayerMove, kMoveSlot);
        auto vt = At<DWORD*>(player, 0);
        if (FixOn() && vt[kProjectSlot / 4] == kProject)
            PatchHookedSlot(player, kProjectSlot, reinterpret_cast<DWORD>(&Project));
    }
}
