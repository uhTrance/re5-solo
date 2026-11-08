// Controller remap for the 3-1 airboat, DirectInput side (the loader's padhook.cpp wraps GetDeviceState and
// calls DInputFilter), for pads the game reads through DirectInput rather than XInput (Steam Input pads
// arrive as XInput and never reach this). Untested on a real DirectInput pad.
// The aim button is the boat's throttle. Which DirectInput button and axis that is depends on the pad: the game
// reads its layout from Documents\CAPCOM\RESIDENT EVIL 5\config.ini [JOYPAD] (a DualSense: RB=7, R2, the aim;
// RSTICK_VERT=Z), so the mod reads the same keys (RB, RSTICK_VERT) instead of assuming a DualSense, and
// boat.cpp uses the aim button to let go of the wheel. While the player drives the airboat (state +0x10E0 ==
// 0x6A) the game sees: aim button = right stick pushed up (up = below centre), the stick's vertical axis centred.
// The real aim button is kept for boat.cpp (DInputAimHeld); hiding it while driving also means the game
// sees a fresh aim press on the deck.
#include <windows.h>
#include <shlobj.h>

#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kButtonsOffset = 48;         // DIJOYSTATE(2) rgbButtons
int g_aimButton = 7;                         // config.ini RB (a DualSense: 7, R2)
DWORD g_axisOffset = 8;                      // config.ini RSTICK_VERT as a DIJOYSTATE offset (Z: lZ, 8)
bool g_layoutRead;

// The pad layout the game uses, from config.ini [JOYPAD]: RB = the aim button, RSTICK_VERT = the right stick's
// vertical axis (X, Y, Z, RX, RY, RZ, or SLIDER0 / SLIDER1).
void ReadLayout() {
    g_layoutRead = true;
    char path[MAX_PATH];
    if (!SHGetSpecialFolderPathA(nullptr, path, CSIDL_PERSONAL, FALSE)) return;
    strcat_s(path, "\\CAPCOM\\RESIDENT EVIL 5\\config.ini");
    int button = GetPrivateProfileIntA("JOYPAD", "RB", -1, path);
    if (button >= 0 && button < 128) g_aimButton = button;
    char axis[16] = {};
    GetPrivateProfileStringA("JOYPAD", "RSTICK_VERT", "Z", axis, sizeof axis, path);
    static const struct { const char* name; DWORD offset; } kAxes[] = {
        {"X", 0}, {"Y", 4}, {"Z", 8}, {"RX", 12}, {"RY", 16}, {"RZ", 20}, {"SLIDER0", 24}, {"SLIDER1", 28}};
    for (const auto& a : kAxes)
        if (!_stricmp(axis, a.name)) g_axisOffset = a.offset;
    Log("dinput: pad layout from config.ini: aim button %d, right stick vertical %s (offset %lu)", g_aimButton,
        axis, g_axisOffset);
}

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
    } range = {sizeof range, 16, g_axisOffset, 1 /* DIPH_BYOFFSET */, 0, 0};
    auto getProperty = reinterpret_cast<GetProperty>((*reinterpret_cast<DWORD**>(device))[kGetPropertySlot]);
    // DIPROP_RANGE is MAKEDIPROP(4): the GUID reference is the integer itself.
    if (SUCCEEDED(getProperty(device, *reinterpret_cast<const GUID*>(4), &range)) && range.max > range.min) {
        g_zMin = range.min;
        g_zMax = range.max;
    }
    Log("dinput: pad stick axis range %ld..%ld", g_zMin, g_zMax);
}

}  // namespace

void DInputFilter(void* device, DWORD size, void* data) {
    if (RecordMode()) return;
    __try {
        if (!g_layoutRead) ReadLayout();
        if (!g_rangeRead) ReadZRange(device);
        if (g_aimButton >= (size == 80 ? 32 : 128)) return;  // DIJOYSTATE has 32 buttons, DIJOYSTATE2 128
        auto bytes = static_cast<BYTE*>(data);
        BYTE& aim = bytes[kButtonsOffset + g_aimButton];
        LONG& z = *reinterpret_cast<LONG*>(bytes + g_axisOffset);
        g_aimHeld = (aim & 0x80) != 0;
        if (Driving()) {
            LONG mid = (g_zMin + g_zMax) / 2, half = (g_zMax - g_zMin) / 2;
            aim = z < mid - half * 3 / 10 ? 0x80 : 0;  // stick past ~30% up
            z = mid;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// The real aim button, even while the game is shown the throttle instead.
bool DInputAimHeld() { return g_aimHeld; }
