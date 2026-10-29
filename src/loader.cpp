// dinput8.dll proxy for re5dx9.exe: forwards DirectInput8Create to the real DLL and hosts the solo mod.
//
// The mod itself is re5_solo_core.dll (see host.h). The loader loads a copy of it (so a build can overwrite
// the file at any time), and when the file changes it reloads it while the game runs: clear the core's
// filters, CoreShutdown (the core undoes its game hooks and stops its threads), wait a little for any game
// thread still inside the old code, FreeLibrary, load the new copy, CoreInit.
#include <windows.h>
#include <unknwn.h>
#include <cstdarg>
#include <cstdio>

#include "host.h"
#include "solo.h"

void XInputHookInit();                                    // padhook.cpp
void WrapDirectInput(void* directInput);                  // padhook.cpp
DWORD WINAPI PadRawXInputGetState(DWORD user, void* state);  // padhook.cpp

Host g_loaderHost;

namespace {

HMODULE g_real;
FILE* g_log;
CRITICAL_SECTION g_logLock;
char g_dir[MAX_PATH];  // the game's folder, with a trailing backslash

void LogLine(const char* line) {
    if (!g_log) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    EnterCriticalSection(&g_logLock);
    fprintf(g_log, "%02d:%02d:%02d.%03d %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, line);
    fflush(g_log);
    LeaveCriticalSection(&g_logLock);
}

struct Block { char name[32]; void* p; };
Block g_blocks[32];
CRITICAL_SECTION g_blockLock;

void* Memory(const char* name, DWORD size) {
    EnterCriticalSection(&g_blockLock);
    void* p = nullptr;
    for (auto& b : g_blocks) {
        if (b.p && strcmp(b.name, name) == 0) {
            p = b.p;
            break;
        }
        if (!b.p) {
            p = VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            lstrcpynA(b.name, name, sizeof b.name);
            b.p = p;
            break;
        }
    }
    LeaveCriticalSection(&g_blockLock);
    return p;
}

HMODULE Real() {
    if (!g_real) {
        char path[MAX_PATH];
        GetSystemDirectoryA(path, MAX_PATH);  // SysWOW64 for this 32-bit process (WOW64 redirection)
        strcat(path, "\\dinput8.dll");
        g_real = LoadLibraryA(path);
        Log("real dinput8: %s -> %p", path, g_real);
    }
    return g_real;
}

// SteamStub decrypts the code after imports load, so wait for known bytes before touching anything.
bool CodeReady() {
    // uPlayer property registration: mov dword ptr [esp+14h], offset "pPartner"
    static const BYTE kSig[] = {0xC7, 0x44, 0x24, 0x14, 0x70, 0x95, 0x04, 0x01};
    __try {
        return memcmp(reinterpret_cast<void*>(0xCA7BE1), kSig, sizeof kSig) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

HMODULE g_core;
CoreShutdownFn g_coreShutdown;
char g_coreCopy[MAX_PATH];
int g_loads;

bool Stamp(const char* path, FILETIME* time, DWORD* size) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &a)) return false;
    *time = a.ftLastWriteTime;
    *size = a.nFileSizeLow;
    return true;
}

void UnloadCore() {
    if (!g_core) return;
    g_loaderHost.xinputFilter = nullptr;
    g_loaderHost.dinputFilter = nullptr;
    g_coreShutdown();
    Sleep(300);  // let a game thread still inside the old core's code (an object update) return
    FreeLibrary(g_core);
    g_core = nullptr;
    g_coreShutdown = nullptr;
    DeleteFileA(g_coreCopy);
}

void LoadCore() {
    char src[MAX_PATH];
    wsprintfA(src, "%sre5_solo_core.dll", g_dir);
    wsprintfA(g_coreCopy, "%sre5_solo_core.loaded%d.dll", g_dir, ++g_loads);
    if (!CopyFileA(src, g_coreCopy, FALSE)) {
        Log("core: could not copy %s (%lu)", src, GetLastError());
        return;
    }
    g_core = LoadLibraryA(g_coreCopy);
    auto init = g_core ? reinterpret_cast<CoreInitFn>(GetProcAddress(g_core, "CoreInit")) : nullptr;
    g_coreShutdown = g_core ? reinterpret_cast<CoreShutdownFn>(GetProcAddress(g_core, "CoreShutdown")) : nullptr;
    if (!init || !g_coreShutdown) {
        Log("core: %s did not load (%lu)", g_coreCopy, GetLastError());
        if (g_core) FreeLibrary(g_core);
        g_core = nullptr;
        g_coreShutdown = nullptr;
        return;
    }
    Log("core: load %d", g_loads);
    if (!init(&g_loaderHost)) Log("core: CoreInit failed");
}

DWORD WINAPI Start(LPVOID) {
    for (int i = 0; i < 600 && !CodeReady(); ++i) Sleep(50);
    if (!CodeReady()) {
        Log("code never matched the expected build - mod disabled (game updated?)");
        return 0;
    }
    Log("code decrypted, starting solo mod");
    CrashLogInit();
    XInputHookInit();
    LoadCore();
    // Reload when the core file changes, once its size and time have held still for one check (a copy in
    // progress).
    char path[MAX_PATH];
    wsprintfA(path, "%sre5_solo_core.dll", g_dir);
    FILETIME loaded{}, seen{};
    DWORD size = 0, seenSize = 0;
    Stamp(path, &loaded, &size);
    for (;;) {
        Sleep(500);
        FILETIME now;
        DWORD nowSize;
        if (!Stamp(path, &now, &nowSize) || CompareFileTime(&now, &loaded) == 0) continue;
        if (CompareFileTime(&now, &seen) != 0 || nowSize != seenSize) {
            seen = now;
            seenSize = nowSize;
            continue;
        }
        Log("core: %s changed, reloading", path);
        UnloadCore();
        LoadCore();
        loaded = now;
    }
}

}  // namespace

void Log(const char* fmt, ...) {
    char line[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    LogLine(line);
}

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE inst, DWORD ver, REFIID riid, LPVOID* out, LPUNKNOWN outer) {
    using Fn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    auto fn = reinterpret_cast<Fn>(GetProcAddress(Real(), "DirectInput8Create"));
    HRESULT hr = fn ? fn(inst, ver, riid, out, outer) : E_FAIL;
    if (SUCCEEDED(hr) && out) WrapDirectInput(*out);  // pad remap (padhook.cpp)
    return hr;
}

BOOL WINAPI DllMain(HMODULE self, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
        InitializeCriticalSection(&g_logLock);
        InitializeCriticalSection(&g_blockLock);
        GetModuleFileNameA(self, g_dir, MAX_PATH);
        char* slash = strrchr(g_dir, '\\');
        if (slash) slash[1] = 0;
        char path[MAX_PATH];
        wsprintfA(path, "%sre5_solo.log", g_dir);
        g_log = fopen(path, "w");
        g_loaderHost.logLine = LogLine;
        g_loaderHost.memory = Memory;
        g_loaderHost.rawXInputGetState = PadRawXInputGetState;
        Log("re5_solo loaded into pid %lu (loader; the mod is re5_solo_core.dll, reloaded when it changes)",
            GetCurrentProcessId());
        CloseHandle(CreateThread(nullptr, 0, Start, nullptr, 0, nullptr));
    }
    return TRUE;
}
