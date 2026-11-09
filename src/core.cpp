// re5_solo_core.dll entry points (see host.h): the loader calls CoreInit after loading this DLL and
// CoreShutdown before unloading it, which must leave nothing in the game pointing into this DLL.
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "host.h"
#include "solo.h"

Host* g_host;

void Log(const char* fmt, ...) {
    char line[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (g_host) g_host->logLine(line);
}

// An option the launcher sets: a marker file next to the game's exe.
bool MarkerFile(const char* name) {
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    strcpy(slash ? slash + 1 : path, name);
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

extern "C" __declspec(dllexport) bool CoreInit(Host* host) {
    g_host = host;
    host->xinputFilter = XInputFilter;
    host->dinputFilter = DInputFilter;
    SoloInit();
    CageInit();
    return true;
}

// The loader has already cleared the filters. Order: stop our threads (no new hooks after this), then undo
// what they hooked.
extern "C" __declspec(dllexport) void CoreShutdown() {
    SoloShutdown();
    ScriptShutdown();
    CageShutdown();
    DebugShutdown();
    CloseHint();
    BoatShutdown();
    CollapseShutdown();
    VoiceShutdown();
    ZoneShutdown();
    CrankShutdown();
    RestoreHookedObjects();
    Log("core: shut down");
}
