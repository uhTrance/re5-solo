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
//
// Enemies snag the same way: their ground step (+0xD8, 0x9DE2E0 in the enemy classes) works on the same start
// (+0x1060) and moved (+0x1050) positions. Lost in Nightmares' axe man (uEm27, area 705) walked on the spot at
// 120 fps on the way to the ledge he jumps up to, and reached it at 30 (2026-11-09). Patched vtable copies are
// few (objhook.cpp), so an enemy class gets the same treatment only once one of its units is seen held in place
// while moving (watcher samples, 100 ms apart).
#include <cmath>

#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kMoveSlot = 0x1C;
constexpr DWORD kProjectSlot = 0xD8, kProject = 0xB6A670;
constexpr DWORD kStart = 0x1060, kMoved = 0x1050;
constexpr DWORD kMovedCopies[] = {0x30, 0x90, 0xD0};  // where the moved position sits before +0xD8
constexpr DWORD kTimer = 0x11B209C, kFrameSeconds = 0x28;
constexpr float kMinStep = 0.5f, kSame = 0.01f, kHeldShare = 1.0f / 8;
constexpr int kKeepFrames = 8, kGiveUpFrames = 4, kCooldownFrames = 30;
constexpr int kSlots = 4;

bool FixOn() {
    static const bool on = [] {
        bool found = MarkerFile("re5_solo_120fps.txt");
        if (found) Log("120fps fix on (re5_solo_120fps.txt)");
        return found;
    }();
    return on;
}

// re5_solo_120fps_pause.txt next to the exe switches the widening off while it exists (checked each second by the
// watcher), to compare with the game's own behaviour without a restart.
volatile bool g_paused;

struct Track;
bool WidenStep(BYTE* unit, Track* t, bool upTo30);

struct Track {
    float lastStart[3];
    float lastStep;
    int heldBack;
    int clear;     // frames in a row not held back, while heldBack is kept
    int cooldown;   // frames left with no widening (it did not help)
    int fruitless;  // widened frames in a row that got nowhere
    bool widened;   // this frame's step was widened
};
Track g_track[kSlots];

Track* TrackOf(BYTE* player) {
    for (int slot = 0; slot < kSlots; ++slot)
        if (PlayerAt(slot) == player) return &g_track[slot];
    return nullptr;
}

void __fastcall Project(BYTE* player, void*) {
    // Players get the enemies' escalation too: still held after the 60 fps step for a while, the 30 fps one (stair
    // corners in Lost in Nightmares' area 700 held Jill at x2, 2026-11-09).
    if (Track* t = TrackOf(player); t && WidenStep(player, t, true) && (t->heldBack == 2 || t->fruitless == 2) && !t->clear)
        Log("120fps fix: player %d held at %.1f %.1f %.1f, trying the %s fps step", static_cast<int>(t - g_track),
            At<float>(player, kStart), At<float>(player, kStart + 4), At<float>(player, kStart + 8),
            t->fruitless >= 2 ? "30" : "60");
    ThisCall(kProject, player);
}

constexpr DWORD kEnemyProject = 0x9DE2E0;
constexpr int kEnemyTracks = 64;
struct EnemyTrack {
    BYTE* unit;
    Track t;
};
EnemyTrack g_enemies[kEnemyTracks];

Track* EnemyTrackOf(BYTE* unit) {
    EnemyTrack* free = nullptr;
    for (auto& e : g_enemies) {
        if (e.unit == unit) return &e.t;
        if (!e.unit && !free) free = &e;
    }
    if (!free) free = &g_enemies[reinterpret_cast<DWORD>(unit) / 16 % kEnemyTracks];
    *free = {unit, {}};
    return &free->t;
}

// The player fix's step, for one unit: true once it hands the collision the 60 fps step.
bool WidenStep(BYTE* unit, Track* t, bool upTo30) {
    if (g_paused) return false;
    float* start = &At<float>(unit, kStart);
    float* moved = &At<float>(unit, kMoved);
    float step[3] = {moved[0] - start[0], moved[1] - start[1], moved[2] - start[2]};
    float len = std::hypot(step[0], step[2]);
    // Held back: since the last frame it got under an eighth of the step it was given (or nowhere), as sliding
    // along a stair corner does (0.4 units a frame of a 3.5 step). A third caught ordinary stair climbing too.
    float advanced = std::hypot(start[0] - t->lastStart[0], start[2] - t->lastStart[2]);
    bool same = advanced < kSame || advanced < t->lastStep * kHeldShare;
    // The wider step not helping (still nowhere after a few widened frames, as against a stair's riser, where it
    // only pushed into the next step): no widening for a while.
    if (t->cooldown > 0) {
        --t->cooldown;
        t->heldBack = t->clear = 0;
        for (int i = 0; i < 3; ++i) t->lastStart[i] = start[i];
        t->lastStep = len;
        return false;
    }
    // Judged frame by frame: a widened frame that got nowhere counts against it. Two in a row bring the 30 fps step
    // (where allowed), four give up for kCooldownFrames. On stairs it was the widening that stopped her dead.
    if (t->widened) t->fruitless = advanced < kSame ? t->fruitless + 1 : 0;
    t->widened = false;
    if (t->fruitless >= kGiveUpFrames) {
        t->cooldown = kCooldownFrames;
        t->heldBack = t->clear = t->fruitless = 0;
        for (int i = 0; i < 3; ++i) t->lastStart[i] = start[i];
        t->lastStep = len;
        return false;
    }
    // Once widened, the wider step frees it, and dropping back at once caught it again two frames later (a stutter
    // past stair corners): the count is kept until it has gone kKeepFrames frames without being held.
    bool held = len > kMinStep && t->lastStep > kMinStep && same;
    if (held) {
        ++t->heldBack;
        t->clear = 0;
    } else if (t->heldBack >= 2 && len > kMinStep && ++t->clear < kKeepFrames) {
        // still widening
    } else {
        t->heldBack = 0;
        t->clear = 0;
    }
    for (int i = 0; i < 3; ++i) t->lastStart[i] = start[i];
    t->lastStep = len;
    auto timer = *reinterpret_cast<BYTE**>(kTimer);
    float seconds = timer ? At<float>(timer, kFrameSeconds) : 0.0f;
    // Still held after the 60 fps step for a while: the 30 fps one (enemies; the axe man needed it).
    float rate = upTo30 && t->fruitless >= 2 ? 30.0f : 60.0f;
    float factor = seconds > 0.0f ? (1.0f / rate) / seconds : 1.0f;
    if (t->heldBack < 2 || factor <= 1.5f) return false;
    if (factor > 8.0f) factor = 8.0f;
    for (DWORD off : kMovedCopies) {
        At<float>(unit, off) = start[0] + step[0] * factor;
        At<float>(unit, off + 8) = start[2] + step[2] * factor;
    }
    moved[0] = start[0] + step[0] * factor;
    moved[2] = start[2] + step[2] * factor;
    t->widened = true;
    return true;
}

void __fastcall EnemyProject(BYTE* unit, void*) {
    Track* t = EnemyTrackOf(unit);
    if (WidenStep(unit, t, true) && (t->heldBack == 2 || t->fruitless == 2) && !t->clear)
        Log("120fps fix: enemy %p held at %.1f %.1f, trying the %s fps step", unit, At<float>(unit, kStart),
            At<float>(unit, kStart + 8), t->fruitless >= 2 ? "30" : "60");
    ThisCall(kEnemyProject, unit);
}

void PatchEnemy(DWORD* copy) { copy[kProjectSlot / 4] = reinterpret_cast<DWORD>(&EnemyProject); }

void AfterPlayerMove(BYTE* player) {
    JumpFrame();
    DeathAfterMove(player);
    PartnerAfterMove(player);
    VolcanoAfterMove(player);
}

}  // namespace

// Watcher thread, 120fps fix on: an enemy class whose unit stays put while it moves gets EnemyProject.
void EnemySnagTick() {
    static DWORD lastPause;
    if (GetTickCount() - lastPause >= 1000) {
        lastPause = GetTickCount();
        bool paused = MarkerFile("re5_solo_120fps_pause.txt");
        if (paused != g_paused) Log("120fps fix: %s (re5_solo_120fps_pause.txt)", paused ? "paused" : "back on");
        g_paused = paused;
    }
    static DWORD last;
    if (!FixOn() || GetTickCount() - last < 100) return;
    last = GetTickCount();
    static struct { BYTE* unit; float x, z; int seen; } watch[32];
    ForEachUnit([](BYTE* u) {
        DWORD vt = At<DWORD>(u, 0);
        if (vt < 0x400000 || vt >= 0x1928000 || At<DWORD*>(u, 0)[kProjectSlot / 4] != kEnemyProject) return;
        float* start = &At<float>(u, kStart);
        float* moved = &At<float>(u, kMoved);
        float x = At<float>(u, 0x30), z = At<float>(u, 0x38);
        bool held = std::hypot(moved[0] - start[0], moved[2] - start[2]) > kMinStep &&
                    std::fabs(x - start[0]) < kSame && std::fabs(z - start[2]) < kSame;
        auto* w = &watch[reinterpret_cast<DWORD>(u) / 16 % 32];
        if (w->unit != u) *w = {u, x, z, 0};
        w->seen = held && std::fabs(x - w->x) < kSame && std::fabs(z - w->z) < kSame ? w->seen + 1 : 0;
        w->x = x, w->z = z;
        if (w->seen >= 3 && PatchObject(u, vt, PatchEnemy))
            Log("120fps fix: enemy class %08lX (unit %p held at %.0f %.0f while moving) gets the 60 fps step", vt, u, x, z);
    });
}

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
