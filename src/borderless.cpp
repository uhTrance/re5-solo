// Optional: a borderless window the size of its monitor (re5_solo_borderless.txt next to the exe), so the game
// keeps running when it loses focus.
//
// In exclusive fullscreen the window procedure's WM_ACTIVATEAPP (0x423DBE) minimises the window on losing focus
// (ShowWindow SW_SHOWMINNOACTIVE while [0x12345D4]+0x21, the fullscreen flag, is set). Minimised, the device check
// in the main loop (0x423A47, 0x453FD0) fails and no frame runs: the game stops until it is back in front. The main
// loop itself keeps going without focus ([0x12337DC]+0x21 is set; +0x20, the focus byte, only matters when it
// isn't), so a window is never stopped. The pause menu still opened on losing focus: localjoin.cpp's pause update
// wrapper keeps it from counting that. The launcher sets FullScreen=OFF in the game's config.ini along with the
// marker; this takes the window's frame off and fits it to its monitor. Window calls only: nothing in the game changes.
#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kDisplay = 0x12345D4, kFullScreen = 0x21;

HWND GameWindow() {
    HWND w = nullptr;
    while ((w = FindWindowExA(nullptr, w, "MTFramework", nullptr))) {
        DWORD pid = 0;
        GetWindowThreadProcessId(w, &pid);
        if (pid == GetCurrentProcessId()) return w;
    }
    return nullptr;
}

}  // namespace

bool KeepRunningUnfocused() {
    static const bool on = MarkerFile("re5_solo_borderless.txt");
    return on;
}

// Watcher thread: every 500 ms, take the frame off the game's window (the game can put it back when it resizes it).
void BorderlessTick() {
    if (!KeepRunningUnfocused()) return;
    static DWORD lastCheck;
    if (GetTickCount() - lastCheck < 500) return;
    lastCheck = GetTickCount();
    auto display = *reinterpret_cast<BYTE**>(kDisplay);
    if (!display || At<BYTE>(display, kFullScreen)) return;  // exclusive fullscreen: config.ini still says ON
    HWND w = GameWindow();
    if (!w || IsIconic(w)) return;
    LONG style = GetWindowLongA(w, GWL_STYLE);
    MONITORINFO mi{sizeof mi};
    if (!GetMonitorInfoA(MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST), &mi)) return;
    const RECT& m = mi.rcMonitor;
    RECT r;
    GetWindowRect(w, &r);
    bool framed = style & (WS_CAPTION | WS_THICKFRAME);
    if (!framed && r.left == m.left && r.top == m.top && r.right == m.right && r.bottom == m.bottom) return;
    SetWindowLongA(w, GWL_STYLE, (style & ~(WS_OVERLAPPEDWINDOW)) | WS_POPUP);
    SetWindowPos(w, nullptr, m.left, m.top, m.right - m.left, m.bottom - m.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    Log("borderless: window %p fitted to its monitor (%ld x %ld)", w, m.right - m.left, m.bottom - m.top);
}
