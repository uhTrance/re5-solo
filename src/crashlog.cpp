// Crash recorder: the game has its own crash handler (no WER report), so log faults ourselves.
// A vectored handler only observes and returns CONTINUE_SEARCH; it changes no game code or state.
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

    if (code == EXCEPTION_ACCESS_VIOLATION)
        Log("FAULT %08X at %08X: %s %08X", code, c->Eip, rec->ExceptionInformation[0] ? "write" : "read",
            static_cast<DWORD>(rec->ExceptionInformation[1]));
    else
        Log("FAULT %08X at %08X", code, c->Eip);
    Log("  eax %08X ebx %08X ecx %08X edx %08X esi %08X edi %08X ebp %08X esp %08X", c->Eax, c->Ebx, c->Ecx,
        c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    LogStack(c->Esp);
    if (inGame && InterlockedExchange(&g_dumped, 1) == 0) WriteDump(ep);
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void CrashLogInit() {
    AddVectoredExceptionHandler(1, OnException);
    Log("crash recorder on");
}
