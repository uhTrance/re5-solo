#pragma once
#include <windows.h>

// The split between the permanent loader (dinput8.dll: loader.cpp, crashlog.cpp, padhook.cpp) and the mod
// itself (re5_solo_core.dll, everything else), so the mod can be rebuilt and reloaded while the game runs.
//
// The loader owns everything that must outlive a core: the log, the crash recorder, the hooks into Windows'
// input DLLs (they call the core through the filters below), and memory the game may keep pointers to (a
// buffer the game holds after the core unloads would otherwise dangle). The core owns the game hooks (per-
// object vtable copies, watcher thread) and must undo them all in CoreShutdown: anything left pointing into
// an unloaded core crashes the game on its next frame.
struct Host {
    void (*logLine)(const char* line);
    // A zeroed block that lives for the whole process, the same one for the same name on every reload.
    void* (*memory)(const char* name, DWORD size);
    // XInputGetState without the loader's filter.
    DWORD(WINAPI* rawXInputGetState)(DWORD user, void* state);
    // Set by the core, cleared by the loader before CoreShutdown. Called on the game's input thread.
    void (*xinputFilter)(DWORD user, void* state);  // after a successful XInputGetState
    void (*dinputFilter)(void* device, DWORD size, void* data);  // after a successful GetDeviceState
};

using CoreInitFn = bool (*)(Host* host);
using CoreShutdownFn = void (*)();

// Core side (core.cpp).
extern Host* g_host;
