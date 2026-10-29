// Debug keys, on only when re5_solo_debug.txt is next to the exe.
//
// F9 toggles an enemy freeze: enemies run on unit move line 9 ([0x12340B0] = sUnit, line i at +0x20 + 0x20*i),
// and a line's +0x14 is its speed (1.0); 0 stops everything on it (not every enemy class is on line 9).
//
// F10 arms a teleport watch: a hardware write watchpoint on the player's +0x30 in every game thread, caught by
// our own vectored handler (the game does not survive a real debugger attaching). Any write that moves the
// player more than 300 units logs the writer's EIP and the return addresses on its stack.
#include "objhook.h"
#include "solo.h"

#include <cstdio>
#include <tlhelp32.h>  // after objhook.h (windows.h)

namespace {

constexpr int kEnemyLine = 9;
constexpr DWORD kLineSpeed = 0x14;

bool DebugMode() {
    static const bool on = MarkerFile("re5_solo_debug.txt");
    return on;
}

volatile DWORD g_watchAddr;
volatile bool g_anyWrite;  // re5_solo_watch.txt: log every distinct writer, not just player jumps
DWORD g_seen[8];           // writers already logged for the armed address
LONG g_seenCount;
PVOID g_handler;
float g_prev[3];

void LogStack(DWORD esp) {
    char line[600];
    int n = 0, found = 0;
    auto sp = reinterpret_cast<DWORD*>(esp);
    for (int i = 0; i < 400 && found < 40; ++i) {
        DWORD v = sp[i];
        if (v >= 0x401000 && v < 0xB4A000) {
            n += sprintf(line + n, " %08lX@%X", v, i * 4);
            if (++found % 10 == 0) { Log("watch:  stack%s", line); n = 0; }
        }
    }
    if (n) Log("watch:  stack%s", line);
}

LONG CALLBACK OnWatch(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !g_watchAddr) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* c = ep->ContextRecord;
    if (!(c->Dr6 & 1)) return EXCEPTION_CONTINUE_SEARCH;
    c->Dr6 = 0;
    if (g_anyWrite) {
        for (LONG i = 0; i < g_seenCount; ++i)
            if (g_seen[i] == c->Eip) return EXCEPTION_CONTINUE_EXECUTION;
        if (g_seenCount < 8) {
            g_seen[g_seenCount++] = c->Eip;
            __try {
                Log("watch: %08lX written by eip %08lX tid %lu (value now %08lX)", g_watchAddr, c->Eip,
                    GetCurrentThreadId(), *reinterpret_cast<DWORD*>(g_watchAddr));
                LogStack(c->Esp);
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    __try {
        auto cur = reinterpret_cast<float*>(g_watchAddr);
        float d = 0;
        for (int i = 0; i < 3; ++i) d += (cur[i] - g_prev[i]) * (cur[i] - g_prev[i]);
        if (d > 300.0f * 300.0f) {
            Log("watch: player jumped (%.0f %.0f %.0f) -> (%.0f %.0f %.0f), eip %08lX tid %lu", g_prev[0], g_prev[1],
                g_prev[2], cur[0], cur[1], cur[2], c->Eip, GetCurrentThreadId());
            LogStack(c->Esp);
        }
        for (int i = 0; i < 3; ++i) g_prev[i] = cur[i];
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return EXCEPTION_CONTINUE_EXECUTION;
}

// Set (or clear) DR0 = addr, write, 4 bytes (1 byte for an address watch), in every game thread but this one.
void SetWatch(DWORD addr) {
    DWORD enable = g_anyWrite ? 0x10001u : 0xD0001u;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te{sizeof te};
    int count = 0;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == GetCurrentThreadId()) continue;
        HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
        if (!t) continue;
        if (SuspendThread(t) != DWORD(-1)) {
            CONTEXT c{};
            c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(t, &c)) {
                c.Dr0 = addr;
                c.Dr6 = 0;
                c.Dr7 = addr ? (c.Dr7 & ~0xF0003u) | enable : c.Dr7 & ~0xF0003u;
                if (SetThreadContext(t, &c)) ++count;
            }
            ResumeThread(t);
        }
        CloseHandle(t);
    }
    CloseHandle(snap);
    Log("watch: %s on %08lX in %d threads", addr ? "armed" : "cleared", addr, count);
}

void WatchTick() {
    static bool wasDown;
    bool down = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
    bool pressed = down && !wasDown;
    wasDown = down;
    if (!pressed) return;
    if (!g_handler) g_handler = AddVectoredExceptionHandler(1, OnWatch);
    BYTE* player = SoloPlayer();
    if (g_watchAddr || !player) {
        g_watchAddr = 0;
        SetWatch(0);
        return;
    }
    DWORD addr = reinterpret_cast<DWORD>(player) + 0x30;
    for (int i = 0; i < 3; ++i) g_prev[i] = At<float>(player, 0x30 + 4 * i);
    g_watchAddr = addr;
    SetWatch(addr);
}

// Research aid: a hex address in re5_solo_watch.txt next to the exe arms a 1-byte write watch there and logs the
// first 8 distinct writers (with stacks). Checked once a second; deleting the file clears it.
void WatchFileTick() {
    static DWORD last, armed;
    if (GetTickCount() - last < 1000) return;
    last = GetTickCount();
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    strcpy(slash ? slash + 1 : path, "re5_solo_watch.txt");
    DWORD addr = 0;
    if (FILE* f = fopen(path, "r")) {
        if (fscanf(f, "%lx", &addr) != 1) addr = 0;
        fclose(f);
    }
    if (addr == armed) return;
    if (!g_handler) g_handler = AddVectoredExceptionHandler(1, OnWatch);
    armed = addr;
    g_seenCount = 0;
    g_anyWrite = addr != 0;
    g_watchAddr = addr;
    SetWatch(addr);
}

}  // namespace

void DebugTick() {
    WatchFileTick();
    if (!DebugMode()) return;
    WatchTick();
    static bool frozen, wasDown;
    bool down = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
    bool pressed = down && !wasDown;
    wasDown = down;
    auto units = *reinterpret_cast<BYTE**>(kUnitRoot);
    if (!units) return;
    BYTE* line = UnitLine(units, kEnemyLine);
    if (At<int>(line, 0x18) != kEnemyLine) return;
    if (pressed) {
        frozen = !frozen;
        Log("debug: enemies %s", frozen ? "frozen (F9)" : "unfrozen (F9)");
        if (!frozen) At<float>(line, kLineSpeed) = 1.0f;
    }
    if (frozen) At<float>(line, kLineSpeed) = 0.0f;  // re-applied: a level load may reset it
}

// CoreShutdown: clear the watchpoint in every thread and drop the handler (it lives in this DLL).
void DebugShutdown() {
    if (g_watchAddr) {
        g_watchAddr = 0;
        SetWatch(0);
    }
    if (g_handler) RemoveVectoredExceptionHandler(g_handler);
    g_handler = nullptr;
}
