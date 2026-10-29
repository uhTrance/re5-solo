// Loader side of the pad remaps (the core's xinput.cpp / dinput.cpp decide what to change): hooks that live
// in the permanent dinput8.dll and hand each pad read to the loaded core's filter, so reloading the core never
// touches them.
//
// XInput: the game reads the pad through XINPUT1_3.dll's XInputGetState (IAT 0xF4A40C, per frame at
// 0x437B72). That is Windows code, not the game's, so the DRM's integrity check does not cover it. With
// Steam's overlay (and Steam Input) the function already starts with Steam's jump: the mod chains, with a trampoline to Steam's target and the jump repointed at
// Hooked in one atomic 8-byte write (the function is 16-byte aligned). Without Steam it is hooked
// hotpatch-style: "push ebp; mov ebp, esp" after 5+ int3s, a far jump into the padding and a 2-byte short
// jump over the first two bytes; the original runs through a trampoline (its first 3 bytes, then +3).
//
// DirectInput: the loader is the game's dinput8.dll, so it sees DirectInput8Create; the IDirectInput8's
// CreateDevice and each device's GetDeviceState (dinput8.dll's vtables, not the game's) are wrapped.
#include <windows.h>

#include "host.h"
#include "solo.h"

extern Host g_loaderHost;  // loader.cpp

namespace {

using GetState = DWORD(WINAPI*)(DWORD, void*);
GetState g_original;
BYTE* g_trampoline;

DWORD WINAPI Hooked(DWORD user, void* state) {
    DWORD result = g_original(user, state);
    if (result == ERROR_SUCCESS && state)
        if (auto filter = g_loaderHost.xinputFilter) filter(user, state);
    return result;
}

bool ChainExistingJump(BYTE* fn) {
    if (reinterpret_cast<DWORD>(fn) & 7) return false;
    BYTE* target = fn + 5 + *reinterpret_cast<int*>(fn + 1);
    g_trampoline[0] = 0xE9;
    *reinterpret_cast<int*>(g_trampoline + 1) = static_cast<int>(target - (g_trampoline + 5));
    g_original = reinterpret_cast<GetState>(g_trampoline);
    LONG64 before = *reinterpret_cast<LONG64*>(fn), after = before;
    *reinterpret_cast<int*>(reinterpret_cast<BYTE*>(&after) + 1) =
        static_cast<int>(reinterpret_cast<BYTE*>(&Hooked) - (fn + 5));
    DWORD old;
    if (!VirtualProtect(fn, 8, PAGE_EXECUTE_READWRITE, &old)) return false;
    InterlockedCompareExchange64(reinterpret_cast<LONG64*>(fn), after, before);
    VirtualProtect(fn, 8, old, &old);
    FlushInstructionCache(GetCurrentProcess(), fn, 8);
    Log("xinput: XInputGetState %p already jumps to %p (Steam), chained", fn, target);
    return true;
}

constexpr DWORD kJoyState = 80, kJoyState2 = 272;  // DIJOYSTATE, DIJOYSTATE2
constexpr DWORD kGetDeviceStateSlot = 9, kCreateDeviceSlot = 3;

using CreateDevice = HRESULT(WINAPI*)(void* self, REFGUID guid, void** device, void* outer);
using GetDeviceState = HRESULT(WINAPI*)(void* self, DWORD size, void* data);
CreateDevice g_createDevice;
GetDeviceState g_getDeviceState;

HRESULT WINAPI HookedGetDeviceState(void* self, DWORD size, void* data) {
    HRESULT hr = g_getDeviceState(self, size, data);
    if (SUCCEEDED(hr) && data && (size == kJoyState || size == kJoyState2))
        if (auto filter = g_loaderHost.dinputFilter) filter(self, size, data);
    return hr;
}

bool PatchSlot(DWORD* vtable, DWORD slot, void* hook, void** original) {
    if (vtable[slot] == reinterpret_cast<DWORD>(hook)) return false;
    DWORD old;
    if (!VirtualProtect(vtable + slot, 4, PAGE_READWRITE, &old)) return false;
    *original = reinterpret_cast<void*>(vtable[slot]);
    vtable[slot] = reinterpret_cast<DWORD>(hook);
    VirtualProtect(vtable + slot, 4, old, &old);
    return true;
}

HRESULT WINAPI HookedCreateDevice(void* self, REFGUID guid, void** device, void* outer) {
    HRESULT hr = g_createDevice(self, guid, device, outer);
    if (SUCCEEDED(hr) && device && *device &&
        PatchSlot(*reinterpret_cast<DWORD**>(*device), kGetDeviceStateSlot, &HookedGetDeviceState,
                  reinterpret_cast<void**>(&g_getDeviceState)))
        Log("dinput: device GetDeviceState hooked");
    return hr;
}

}  // namespace

DWORD WINAPI PadRawXInputGetState(DWORD user, void* state) {
    if (g_original) return g_original(user, state);
    static auto direct = reinterpret_cast<GetState>(GetProcAddress(LoadLibraryA("xinput1_3.dll"), "XInputGetState"));
    return direct ? direct(user, state) : ERROR_DEVICE_NOT_CONNECTED;
}

// Once, from the loader's start thread.
void XInputHookInit() {
    auto fn = reinterpret_cast<BYTE*>(GetProcAddress(LoadLibraryA("xinput1_3.dll"), "XInputGetState"));
    if (!fn) {
        Log("xinput: XInputGetState not found");
        return;
    }
    g_trampoline = static_cast<BYTE*>(VirtualAlloc(nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!g_trampoline) return;
    if (fn[0] == 0xE9) {
        if (!ChainExistingJump(fn)) Log("xinput: could not chain XInputGetState %p, not hooked", fn);
        return;
    }
    static const BYTE kPrologue[] = {0x55, 0x8B, 0xEC};  // push ebp; mov ebp, esp
    if (memcmp(fn, kPrologue, 3) != 0 || memcmp(fn - 5, "\xCC\xCC\xCC\xCC\xCC", 5) != 0) {
        Log("xinput: unexpected code at XInputGetState %p (%02X %02X %02X), not hooked", fn, fn[0], fn[1], fn[2]);
        return;
    }
    memcpy(g_trampoline, kPrologue, 3);
    g_trampoline[3] = 0xE9;
    *reinterpret_cast<int*>(g_trampoline + 4) = static_cast<int>((fn + 3) - (g_trampoline + 8));
    g_original = reinterpret_cast<GetState>(g_trampoline);

    DWORD old;
    if (!VirtualProtect(fn - 5, 7, PAGE_EXECUTE_READWRITE, &old)) return;
    fn[-5] = 0xE9;
    *reinterpret_cast<int*>(fn - 4) = static_cast<int>(reinterpret_cast<BYTE*>(&Hooked) - fn);
    InterlockedExchange16(reinterpret_cast<SHORT*>(fn), static_cast<SHORT>(0xF9EB));  // jmp short -7
    VirtualProtect(fn - 5, 7, old, &old);
    FlushInstructionCache(GetCurrentProcess(), fn - 5, 7);
    Log("xinput: XInputGetState %p hooked", fn);
}

// From the proxy's DirectInput8Create, with the new IDirectInput8.
void WrapDirectInput(void* directInput) {
    if (directInput && PatchSlot(*static_cast<DWORD**>(directInput), kCreateDeviceSlot, &HookedCreateDevice,
                                 reinterpret_cast<void**>(&g_createDevice)))
        Log("dinput: CreateDevice hooked");
}
