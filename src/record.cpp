// Record mode (re5_solo_record.txt next to the exe): the mod leaves co-op alone (the partner spawns, nothing
// is hooked) and logs where each player ends up after a co-op jump (gap or boost), relative to the jump
// object, for the solo landing positions.
//
// Per player: entering any uOmCoopJumpBroad player action stores the object's centre/axis/subclass and the
// start position; 1.5 s after leaving them (landed and settled) the end position is logged as
//   forward = along the object's axis, away from the start side; up; side = the rest (horizontal).
#include <cmath>

#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kAction = 0x10F8;
constexpr DWORD kJumpActions[] = {0xFC9CB8, 0xFC9CE4, 0xFC9D10};  // cActPlJumpReady / JumpSecond / Jump
constexpr DWORD kActObject = 0x24;
constexpr DWORD kSettleMs = 1500;

struct Track {
    int phase;  // 0 idle, 1 in a jump action, 2 left it at `left`
    DWORD left;
    DWORD dti;
    float centre[3], axis[3], start[3];
};
Track g_track[2];

bool InJump(BYTE* player, BYTE** object) {
    auto act = At<BYTE*>(player, kAction);
    if (!act) return false;
    DWORD vt = At<DWORD>(act, 0);
    for (DWORD v : kJumpActions)
        if (vt == v) {
            *object = At<BYTE*>(act, kActObject);
            return *object != nullptr;
        }
    return false;
}

float Along(const float* d, const float* axis) { return d[0] * axis[0] + d[1] * axis[1] + d[2] * axis[2]; }

void Step(int slot, BYTE* player) {
    Track& t = g_track[slot];
    BYTE* object = nullptr;
    bool inJump = player && InJump(player, &object);
    if (inJump && t.phase != 1) {
        if (t.phase == 0) {  // first jump action of this move: remember where it started
            t.dti = VCall<DWORD>(object, 0x10);
            for (int i = 0; i < 3; ++i) {
                t.centre[i] = At<float>(object, 0x30 + 4 * i);
                t.axis[i] = At<float>(object, 0x80 + 4 * i);
                t.start[i] = At<float>(player, 0x30 + 4 * i);
            }
            Log("record: player %d jump started (DTI %08X, object at %.1f %.1f %.1f, axis %.3f %.3f %.3f)", slot,
                t.dti, t.centre[0], t.centre[1], t.centre[2], t.axis[0], t.axis[1], t.axis[2]);
        }
        t.phase = 1;
    } else if (!inJump && t.phase == 1) {
        t.phase = 2;
        t.left = GetTickCount();
    } else if (t.phase == 2 && GetTickCount() - t.left >= kSettleMs) {
        t.phase = 0;
        if (!player) return;
        float end[3], d[3], s[3];
        for (int i = 0; i < 3; ++i) {
            end[i] = At<float>(player, 0x30 + 4 * i);
            d[i] = end[i] - t.centre[i];
            s[i] = t.start[i] - t.centre[i];
        }
        float sign = Along(s, t.axis) > 0 ? -1.0f : 1.0f;  // forward = away from the start side
        float fwd = sign * Along(d, t.axis);
        float startFwd = sign * Along(s, t.axis);
        float side[3];
        for (int i = 0; i < 3; ++i) side[i] = d[i] - Along(d, t.axis) * t.axis[i];
        float lateral = std::sqrt(side[0] * side[0] + side[2] * side[2]);
        Log("record: player %d LANDED  DTI %08X  forward %.1f  up %.1f  side %.1f  (start forward %.1f, end %.1f "
            "%.1f %.1f)", slot, t.dti, fwd, d[1], lateral, startFwd, end[0], end[1], end[2]);
    }
}

}  // namespace

bool RecordMode() {
    static const bool on = MarkerFile("re5_solo_record.txt");
    return on;
}

void RecordTick() {
    auto list = *reinterpret_cast<BYTE**>(kPlayerList);
    if (!list) return;
    for (int slot = 0; slot < 2; ++slot) Step(slot, At<BYTE*>(list, 0x24 + 4 * slot));
}
