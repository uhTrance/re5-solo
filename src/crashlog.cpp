// Crash recorder: the game has its own crash handler (no WER report), so log faults ourselves.
// A vectored handler only observes and returns CONTINUE_SEARCH; it changes no game code or state.
//
// It sees handled faults too (the core probes memory under __try), and those used up the budget for faults outside
// the game's image: on 2026-11-09 the game closed after nine hours with nothing logged and no dump. So an
// unhandled-exception filter, chained in front of the game's own (re-asserted, as the game may set its filter after
// us), logs and dumps the fault that actually ends the process, in any module.
#include <windows.h>
#include <dbghelp.h>
#include <intrin.h>

#include "solo.h"

namespace {

constexpr DWORD kImageLo = 0x401000, kImageHi = 0x1928000;
// Steam DRM's anti-debug tricks fault on purpose here at startup (div-by-zero / ud2); the DRM handles them.
constexpr DWORD kDrmLo = 0x1465000, kDrmHi = 0x1466000;
LONG g_logged;
LONG g_loggedOther;
LONG g_dumped;
LPTOP_LEVEL_EXCEPTION_FILTER g_previous;

// "name+offset" for an address in a loaded module, or "?".
void ModuleOf(DWORD a, char* out, int size) {
    HMODULE m = nullptr;
    char path[MAX_PATH];
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(a), &m) ||
        !GetModuleFileNameA(m, path, MAX_PATH)) {
        lstrcpynA(out, "?", size);
        return;
    }
    const char* name = strrchr(path, '\\');
    char line[MAX_PATH + 16];
    wsprintfA(line, "%s+%X", name ? name + 1 : path, a - reinterpret_cast<DWORD>(m));
    lstrcpynA(out, line, size);
}

bool IsReturnAddress(DWORD a) {
    if (a < kImageLo + 6 || a >= kImageHi) return false;
    auto p = reinterpret_cast<const BYTE*>(a);
    return p[-5] == 0xE8                                  // call rel32
        || (p[-2] == 0xFF && (p[-1] & 0x38) == 0x10)      // call reg / [reg]
        || (p[-3] == 0xFF && (p[-2] & 0x38) == 0x10)      // call [reg+disp8]
        || (p[-6] == 0xFF && (p[-5] & 0x38) == 0x10);     // call [reg+disp32] / [abs]
}

void LogStack(DWORD esp) {
    char line[512];
    int n = wsprintfA(line, "  callers:");
    int found = 0;
    DWORD stackBase = __readfsdword(4);  // NT_TIB.StackBase of the faulting (current) thread
    for (int i = 0; i < 512 && found < 16 && esp + 4 * i + 4 <= stackBase; ++i) {
        DWORD v;
        __try {
            v = reinterpret_cast<const DWORD*>(esp)[i];
            if (!IsReturnAddress(v)) continue;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            break;
        }
        n += wsprintfA(line + n, " %08X", v);
        ++found;
    }
    Log("%s", line);
}

void WriteDump(EXCEPTION_POINTERS* ep) {
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    SYSTEMTIME t;
    GetLocalTime(&t);
    wsprintfA(slash ? slash + 1 : path, "re5_solo_%02d%02d%02d.dmp", t.wHour, t.wMinute, t.wSecond);
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), ep, FALSE};
    BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), f,
                                static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory |
                                                           MiniDumpWithDataSegs),
                                &mei, nullptr, nullptr);
    CloseHandle(f);
    Log("  minidump %s: %s", ok ? "written" : "FAILED", path);
}

LONG CALLBACK OnException(EXCEPTION_POINTERS* ep) {
    auto rec = ep->ExceptionRecord;
    DWORD code = rec->ExceptionCode;
    // Only real faults; C++ throws, debug prints and breakpoints are ignored.
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_STACK_OVERFLOW &&
        code != EXCEPTION_PRIV_INSTRUCTION)
        return EXCEPTION_CONTINUE_SEARCH;
    auto c = ep->ContextRecord;
    if (c->Eip >= kDrmLo && c->Eip < kDrmHi) return EXCEPTION_CONTINUE_SEARCH;
    // Faults in other modules (usually handled ones) get their own budget, so they cannot use up the game's.
    bool inGame = c->Eip >= kImageLo && c->Eip < kImageHi;
    if (InterlockedIncrement(inGame ? &g_logged : &g_loggedOther) > 8) return EXCEPTION_CONTINUE_SEARCH;

    char where[MAX_PATH + 16];
    ModuleOf(c->Eip, where, sizeof where);
    if (code == EXCEPTION_ACCESS_VIOLATION)
        Log("FAULT %08X at %08X (%s): %s %08X", code, c->Eip, where, rec->ExceptionInformation[0] ? "write" : "read",
            static_cast<DWORD>(rec->ExceptionInformation[1]));
    else
        Log("FAULT %08X at %08X (%s)", code, c->Eip, where);
    Log("  eax %08X ebx %08X ecx %08X edx %08X esi %08X edi %08X ebp %08X esp %08X", c->Eax, c->Ebx, c->Ecx,
        c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    LogStack(c->Esp);
    if (inGame && InterlockedExchange(&g_dumped, 1) == 0) WriteDump(ep);
    return EXCEPTION_CONTINUE_SEARCH;
}

// The fault nobody handled: the process is about to end.
LONG WINAPI OnUnhandled(EXCEPTION_POINTERS* ep) {
    static LONG once;
    if (InterlockedExchange(&once, 1) == 0) {
        auto rec = ep->ExceptionRecord;
        auto c = ep->ContextRecord;
        char where[MAX_PATH + 16];
        ModuleOf(c->Eip, where, sizeof where);
        Log("UNHANDLED %08X at %08X (%s), info %08X %08X: the game is closing", rec->ExceptionCode, c->Eip, where,
            rec->NumberParameters > 0 ? static_cast<DWORD>(rec->ExceptionInformation[0]) : 0,
            rec->NumberParameters > 1 ? static_cast<DWORD>(rec->ExceptionInformation[1]) : 0);
        Log("  eax %08X ebx %08X ecx %08X edx %08X esi %08X edi %08X ebp %08X esp %08X", c->Eax, c->Ebx, c->Ecx,
            c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
        LogStack(c->Esp);
        g_dumped = 1;
        WriteDump(ep);
    }
    return g_previous ? g_previous(ep) : EXCEPTION_CONTINUE_SEARCH;
}

DWORD WINAPI KeepFilter(LPVOID) {
    for (;;) {
        LPTOP_LEVEL_EXCEPTION_FILTER prev = SetUnhandledExceptionFilter(OnUnhandled);
        if (prev != OnUnhandled) {
            if (prev) Log("crash recorder: chained in front of the unhandled-exception filter at %p", prev);
            g_previous = prev;
        }
        Sleep(2000);
    }
}

}  // namespace

void CrashLogInit() {
    AddVectoredExceptionHandler(1, OnException);
    CloseHandle(CreateThread(nullptr, 0, KeepFilter, nullptr, 0, nullptr));
    Log("crash recorder on");
}
