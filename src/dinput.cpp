// Controller remap for the 3-1 airboat, DirectInput side (the loader's padhook.cpp wraps GetDeviceState and
// calls DInputFilter), for pads the game reads through DirectInput rather than XInput (Steam Input pads
// arrive as XInput and never reach this). Untested on a real DirectInput pad.
// The aim button is the boat's throttle; here it is taken as DirectInput button 7 (R2 on a DualSense,
// Documents\CAPCOM\RESIDENT EVIL 5\config.ini "RB=7"; L2 is the knife), and boat.cpp uses it to let go of
// the wheel. While the player drives the airboat (state +0x10E0 == 0x6A) the game sees: aim button = right
// stick pushed up (RSTICK_VERT=Z, the lZ axis, up = below centre), and the stick's vertical axis centred.
// The real aim button is kept for boat.cpp (DInputAimHeld); hiding it while driving also means the game
// sees a fresh aim press on the deck.
#include <windows.h>

#include "objhook.h"
#include "solo.h"

namespace {

constexpr int kAimButton = 7;                // R2 on a DualSense (config.ini RB=7)
constexpr DWORD kButtonsOffset = 48;         // DIJOYSTATE(2) rgbButtons
constexpr DWORD kZOffset = 8;                // lZ
constexpr DWORD kState = 0x10E0, kStateDriving = 0x6A;
constexpr DWORD kGetPropertySlot = 5;

using GetProperty = HRESULT(WINAPI*)(void* self, REFGUID prop, void* header);

volatile bool g_aimHeld;
LONG g_zMin = 0, g_zMax = 65535;
bool g_rangeRead;

bool Driving() {
    BYTE* player = SoloPlayer();
    return player && At<DWORD>(player, kState) == kStateDriving;
}

void ReadZRange(void* device) {
    g_rangeRead = true;
    struct {
        DWORD size, headerSize, obj, how;
        LONG min, max;
    } range = {sizeof range, 16, kZOffset, 1 /* DIPH_BYOFFSET */, 0, 0};
    auto getProperty = reinterpret_cast<GetProperty>((*reinterpret_cast<DWORD**>(device))[kGetPropertySlot]);
    // DIPROP_RANGE is MAKEDIPROP(4): the GUID reference is the integer itself.
    if (SUCCEEDED(getProperty(device, *reinterpret_cast<const GUID*>(4), &range)) && range.max > range.min) {
        g_zMin = range.min;
        g_zMax = range.max;
    }
    Log("dinput: pad Z range %ld..%ld", g_zMin, g_zMax);
}

}  // namespace

void DInputFilter(void* device, DWORD, void* data) {
    if (RecordMode()) return;
    __try {
        if (!g_rangeRead) ReadZRange(device);
        auto bytes = static_cast<BYTE*>(data);
        BYTE& aim = bytes[kButtonsOffset + kAimButton];
        LONG& z = *reinterpret_cast<LONG*>(bytes + kZOffset);
        g_aimHeld = (aim & 0x80) != 0;
        if (Driving()) {
            LONG mid = (g_zMin + g_zMax) / 2, half = (g_zMax - g_zMin) / 2;
            aim = z < mid - half * 3 / 10 ? 0x80 : 0;  // stick past ~30% up
            z = mid;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// The real aim button (R2), even while the game is shown the throttle instead.
bool DInputAimHeld() { return g_aimHeld; }
