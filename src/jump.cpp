// Area skipper, a testing tool: jumps to another area the way the level scripts' JumpArea action does
// (cFSMOrder +0x58, 0xC2A190): with the stage controller present ([0x1234578] +0x3830), the area owner's getter
// (0x768C10) gives the object whose 0x71DFC0 (thiscall: area, start point, mode 2) requests the jump, and whose
// 0x71AE60 (thiscall: scene) sets the scene to start on. Doors call the same request with mode 0.
//
// - F11 (with the debug keys on, re5_solo_debug.txt) skips the current area: it jumps where the area's exit leads (uOmf301, DTI 0x11F8E5C, the game's area-exit
//   object: destination area +0x1C20, start point +0x1C24, which it requests the same way at 0xAA5658), or, in an area
//   that ends on a script instead, where its level script's JumpArea goes (table below, from the extracted scripts).
//   With re5_solo_jump.txt next to the exe ("area [start point] [scene]", e.g. "705 0 14700") F11 goes there instead.
// - re5_solo_jump_now.txt (same format) jumps once and is deleted: a way to jump from outside the game.
// The request is made on the game thread, from the player's move hook (snag.cpp), never from the watcher.
#include <cstdio>

#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kAreaOwner = 0x1234578, kStageController = 0x3830;
constexpr DWORD kOwnerGetter = 0x768C10, kRequestJump = 0x71DFC0, kSetScene = 0x71AE60;
constexpr int kScriptMode = 2;

struct Jump {
    int area, point, scene;  // scene -1: keep the area's own
};
volatile LONG g_pending;  // 1 once g_jump is filled for the game thread
Jump g_jump;

bool ReadJump(const char* name, Jump& out) {
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    lstrcpyA(slash ? slash + 1 : path, name);
    FILE* f = fopen(path, "r");
    if (!f) return false;
    Jump j{0, 0, -1};
    int n = fscanf(f, "%d %d %d", &j.area, &j.point, &j.scene);
    fclose(f);
    if (n < 1 || j.area < 100 || j.area > 999) {
        Log("jump: %s should hold \"area [start point] [scene]\"", name);
        return false;
    }
    if (n < 2) j.point = 0;
    if (n < 3) j.scene = -1;
    out = j;
    return true;
}

constexpr DWORD kExitDti = 0x11F8E5C, kExitArea = 0x1C20, kExitPoint = 0x1C24;

// Areas whose way on is a level script's JumpArea, not an exit object (re/fsm/*.txt).
struct ScriptJump {
    int from;
    Jump to;
};
constexpr ScriptJump kScripted[] = {
    {601, {100, 0, 1100}},  {602, {100, 0, 1010}},  {606, {200, 0, 4200}},  {607, {205, 0, 5400}},
    {610, {508, 0, 11600}}, {612, {506, 0, 11400}}, {616, {700, 0, 14100}}, {618, {702, 0, 15100}},
    {701, {705, 0, -1}},    {704, {619, 0, 15800}}, {706, {617, 0, 14800}},
};

// Where the current area leads: an exit object onward (the highest destination above this area, else any other),
// else the area's scripted jump.
bool NextArea(Jump& out) {
    BYTE* stage = Stage();
    if (!stage) return false;
    int here = static_cast<int>(At<DWORD>(stage, 0x273D8));
    int best = -1, bestPoint = 0, other = -1, otherPoint = 0;
    __try {
        ForEachUnit([&](BYTE* u) {
            if (!IsA(u, kExitDti)) return;
            int area = At<int>(u, kExitArea), point = At<int>(u, kExitPoint);
            if (area < 100 || area > 999 || area == here) return;
            if (area > here && area > best) best = area, bestPoint = point;
            if (other < 0) other = area, otherPoint = point;
        });
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (best < 0 && other >= 0) best = other, bestPoint = otherPoint;
    if (best >= 0) {
        out = {best, bestPoint, -1};
        return true;
    }
    for (const ScriptJump& s : kScripted)
        if (s.from == here) {
            out = s.to;
            return true;
        }
    Log("jump: no exit found for area %d (put a target in re5_solo_jump.txt)", here);
    return false;
}

void Request(const Jump& j, const char* why) {
    if (InterlockedCompareExchange(&g_pending, 0, 0)) return;  // one at a time
    g_jump = j;
    InterlockedExchange(&g_pending, 1);
    Log("jump: area %d, start point %d%s (%s)", j.area, j.point, j.scene >= 0 ? ", scene set" : "", why);
}

}  // namespace

// Watcher thread.
void JumpTick() {
    static DWORD last;
    if (GetTickCount() - last < 100) return;
    last = GetTickCount();
    Jump j;
    char now[MAX_PATH];
    GetModuleFileNameA(nullptr, now, MAX_PATH);
    char* slash = strrchr(now, '\\');
    lstrcpyA(slash ? slash + 1 : now, "re5_solo_jump_now.txt");
    if (GetFileAttributesA(now) != INVALID_FILE_ATTRIBUTES) {
        bool ok = ReadJump("re5_solo_jump_now.txt", j);
        DeleteFileA(now);  // read once, good or bad (a bad one was logged every 100 ms)
        if (ok) Request(j, "re5_solo_jump_now.txt");
    }
    static bool wasDown;
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    static const bool debugKeys = MarkerFile("re5_solo_debug.txt");  // a testing tool: not on a player's F11
    bool down = debugKeys && pid == GetCurrentProcessId() && (GetAsyncKeyState(VK_F11) & 0x8000);
    if (down && !wasDown) {
        if (ReadJump("re5_solo_jump.txt", j)) Request(j, "F11, re5_solo_jump.txt");
        else if (NextArea(j)) Request(j, "F11, the area's exit");
    }
    wasDown = down;
}

// Game thread, after a player's move (snag.cpp).
void JumpFrame() {
    if (!InterlockedCompareExchange(&g_pending, 0, 0)) return;
    auto owner = *reinterpret_cast<BYTE**>(kAreaOwner);
    if (!owner || !At<BYTE*>(owner, kStageController)) return;  // mid-load: try next frame
    InterlockedExchange(&g_pending, 0);
    Jump j = g_jump;
    __try {
        BYTE* area = reinterpret_cast<BYTE* (__fastcall*)(void*, void*)>(kOwnerGetter)(owner, nullptr);
        if (!area) return;
        ThisCall(kRequestJump, area, j.area, j.point, kScriptMode);
        if (j.scene >= 0) ThisCall(kSetScene, area, j.scene);
        Log("jump: requested");
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("jump: the request faulted");
    }
}
