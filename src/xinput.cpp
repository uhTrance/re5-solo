// Controller remap for the 3-1 airboat, XInput side (the loader's padhook.cpp hooks XInputGetState and calls
// XInputFilter). The game drives the boat with the triggers (right forward, left reverse), but the right
// trigger is also aim, which boat.cpp uses to let go of the wheel, so the boat is driven with the right stick
// instead. While the player drives (state +0x10E0 == 0x6A) the game sees: right trigger = how far the right
// stick is pushed up, left trigger = how far down (past the dead zone), and the stick's vertical axis centred
// (no camera pitch). boat.cpp reads the real pad through RawXInputGetState. Steam Input pads (e.g. a
// DualSense) arrive here as XInput.
#include <windows.h>

#include "host.h"
#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kState = 0x10E0, kStateDriving = 0x6A;
constexpr int kDeadZone = 8689;  // XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE

#pragma pack(push, 1)
struct PadState {
    DWORD packet;
    WORD buttons;
    BYTE leftTrigger, rightTrigger;
    SHORT lx, ly, rx, ry;
};
#pragma pack(pop)

bool DrivingBoat() {
    BYTE* player = SoloPlayer();
    return player && At<DWORD>(player, kState) == kStateDriving;
}

}  // namespace

void XInputFilter(DWORD, void* data) {
    if (RecordMode()) return;
    __try {
        auto state = static_cast<PadState*>(data);
        if (!DrivingBoat()) return;
        int y = state->ry;
        int up = y > kDeadZone ? (y - kDeadZone) * 255 / (32767 - kDeadZone) : 0;
        int down = y < -kDeadZone ? (-y - kDeadZone) * 255 / (32768 - kDeadZone) : 0;
        state->rightTrigger = static_cast<BYTE>(up > 255 ? 255 : up);
        state->leftTrigger = static_cast<BYTE>(down > 255 ? 255 : down);
        state->ry = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

DWORD RawXInputGetState(DWORD user, void* state) {
    return g_host->rawXInputGetState(user, state);
}
