// 4-1 separation: walking into a player trigger zone (uOmAreaPlHit, x -1404) starts a cutscene after which the
// two players are on opposite sides: one goes across, the other stays put. Solo, a Chris player would be sent
// across; the mod keeps him on the near side.
// The move (write at 0x5837F5, called from 0x724D02): at the end of the area change a loop over player slots 0-1
// sets each player to start point [slot] of the area's start record (0x822E60, thiscall (out, index), ret 8:
// point 0 = the record's base, point 1 = base + its partner offset at +0x20 rotated by the angle at +0x14).
// Records: [[sGame+0x10430] + 0x50 + 4*(area/100)] -> [+4*(area%100)] -> 64-byte records (area = stage
// +0x273D8; 4-1 here is 300, record 1). Slot 0 (Chris) gets point 0, the far side (-133, 1600, 44910); point 1,
// for the partner who stays put, is the near side (about -1753, 1600, 44922).
#include <cmath>

#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kPositionCopies[] = {0x30, 0x90, 0xD0, 0x110, 0x1050, 0x1060, 0x10B0, 0x1170, 0x11D0,
                                     0x1370, 0x1380, 0x1390, 0x13A0, 0x1B10};
constexpr float kNearSide[3] = {-1479, 1602, 44948}, kFarSide[3] = {-133, 1602, 44948};
constexpr float kFarRecord[3] = {-132.8f, 1600.0f, 44910.2f};  // area 300 record 1's base
constexpr float kFarYaw = -1.7146f;                             // and its yaw (+0x14)
constexpr float kNearPoint[3] = {-1753.0f, 1600.0f, 44922.0f};  // its point 1
constexpr float kTolerance = 250;
constexpr DWORD kGame = 0x12340A4, kStartTable = 0x10430, kArea = 0x273D8;
constexpr DWORD kStartPoint = 0x822E60;
constexpr int kRecords = 64;

bool Near(const float* p, const float* q, float tolerance = kTolerance) {
    return std::fabs(p[0] - q[0]) < tolerance && std::fabs(p[1] - q[1]) < tolerance &&
           std::fabs(p[2] - q[2]) < tolerance;
}

// The start record of the current area whose base (point 0) is `pos`.
BYTE* StartRecord(const float* pos) {
    BYTE* stage = Stage();
    auto game = *reinterpret_cast<BYTE**>(kGame);
    auto table = game ? At<BYTE*>(game, kStartTable) : nullptr;
    if (!stage || !table) return nullptr;
    DWORD area = At<DWORD>(stage, kArea);
    auto rooms = At<BYTE**>(table, 0x50 + 4 * (area / 100));
    BYTE* records = rooms ? rooms[area % 100] : nullptr;
    for (int i = 0; records && i < kRecords; ++i)
        if (Near(reinterpret_cast<float*>(records + 64 * i), pos, 5)) return records + 64 * i;
    return nullptr;
}

}  // namespace

// Watcher thread: before the split, the record's point 0 (its base, where slot 0 goes) is rewritten to its
// point 1, so the game itself puts Chris on the near side (moving him after the split shows a visible teleport).
// The level data reloads with the area, so this re-applies; SplitFrame below is the fallback.
void SplitTick() {
    if (RecordMode()) return;
    static DWORD last;
    if (GetTickCount() - last < 250) return;
    last = GetTickCount();
    __try {
        if (SoloSlot() != 0) {
            // Sheva solo: she is slot 1 and already stays put on point 1, which faces the record's rotation plus
            // its offset; the rewrite below would turn her a second time (about 180 degrees). Undo it if a Chris
            // game (or an older build) left it in the area's records.
            BYTE* moved = StartRecord(kNearPoint);
            if (!moved || std::fabs(At<float>(moved, 0x14) - (kFarYaw + At<float>(moved, 0x34))) > 0.01f) return;
            for (int i = 0; i < 3; ++i) {
                At<float>(moved, 4 * i) = kFarRecord[i];
                At<float>(moved, 0x10 + 4 * i) -= At<float>(moved, 0x30 + 4 * i);
            }
            Log("4-1 split: start record %p put back (Sheva solo: she stays put facing the game's way)", moved);
            return;
        }
        BYTE* record = StartRecord(kFarRecord);  // once rewritten its base no longer matches: done
        float point[3];
        if (record) {
            ThisCall(kStartPoint, record, point, 1);  // before the rotation changes: point 1 uses it
            for (int i = 0; i < 3; ++i) At<float>(record, 4 * i) = point[i];
        } else {
            // Base already moved but facing not (a checkpoint reload keeps the area's records).
            record = StartRecord(kNearPoint);
            if (!record || std::fabs(At<float>(record, 0x14) - kFarYaw) > 0.01f) return;
            for (int i = 0; i < 3; ++i) point[i] = At<float>(record, 4 * i);
        }
        // Facing (0x822CC0): point 0 faces the rotation at +0x10; point 1 faces that plus the offset at +0x30
        // (here about -3.0 rad of yaw), so point 0 needs that added to face the same way.
        for (int i = 0; i < 3; ++i) At<float>(record, 0x10 + 4 * i) += At<float>(record, 0x30 + 4 * i);
        Log("4-1 split: start record %p point 0 moved to its point 1 (%.0f %.0f %.0f, yaw %.2f): Chris stays put",
            record, point[0], point[1], point[2], At<float>(record, 0x14));
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Game thread, every frame (gap.cpp's player-manager update).
void SplitFrame(BYTE* player) {
    static BYTE* last;
    static float prev[3];
    float pos[3] = {At<float>(player, 0x30), At<float>(player, 0x34), At<float>(player, 0x38)};
    if (player == last && Near(prev, kNearSide) && Near(pos, kFarSide)) {
        BYTE* record = StartRecord(pos);
        float target[3];
        if (record) {
            ThisCall(kStartPoint, record, target, 1);
        } else {
            for (int i = 0; i < 3; ++i) target[i] = prev[i];  // no record found: back where the player stood
        }
        for (DWORD off : kPositionCopies)
            for (int i = 0; i < 3; ++i) At<float>(player, off + 4 * i) = target[i];
        // Facing too: the far point faces across (yaw +0x14, about -1.7); the partner's point faces that plus the
        // offset at +0x34 (about -3.0), back the way the pair came. Rotation quaternion at +0x40 (x y z w), yaw only.
        float& qy = At<float>(player, 0x44);
        float& qw = At<float>(player, 0x4C);
        if (record) {
            float yaw = At<float>(record, 0x14) + At<float>(record, 0x34);
            qy = std::sin(yaw / 2);
            qw = std::cos(yaw / 2);
        } else {
            float y = qy;  // turned 180 degrees
            qy = qw;
            qw = -y;
        }
        At<float>(player, 0x40) = At<float>(player, 0x48) = 0.0f;
        Log("4-1 split: player sent across (%.0f %.0f %.0f), moved to the partner's point %.0f %.0f %.0f, facing "
            "%.2f%s", pos[0], pos[1], pos[2], target[0], target[1], target[2], 2 * std::atan2(qy, qw),
            record ? "" : " (record not found: where the player stood, turned round)");
    }
    last = player;
    for (int i = 0; i < 3; ++i) prev[i] = At<float>(player, 0x30 + 4 * i);
}
