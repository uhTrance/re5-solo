#pragma once
#include <windows.h>

// Run code on the game thread after a game object's per-frame update, without touching game code or the
// game's vtables (Steam's DRM): the object gets a private copy of its class vtable whose update slot
// (+0x160 on every uOm co-op object, the default) calls the original, then `after(object)`.
using AfterUpdate = void (*)(BYTE* object);

// Hooks `object` if its vtable is `classVtable`; no-op if it is already hooked or a different class.
bool HookObject(BYTE* object, DWORD classVtable, AfterUpdate after, DWORD updateSlot = 0x160);

// Lower-level: give `object` a private copy of `classVtable` (one copy per class), which `patch` edits
// once when the copy is made (replace any slots; originals stay readable at `classVtable`).
bool PatchObject(BYTE* object, DWORD classVtable, void (*patch)(DWORD* copy));

// True if `object` currently uses a hook copy of `classVtable`.
bool IsHooked(BYTE* object, DWORD classVtable);

// Small helpers shared by the hook users.
template <class R = void, class... A>
R ThisCall(DWORD fn, void* self, A... args) {
    return reinterpret_cast<R(__fastcall*)(void*, void*, A...)>(fn)(self, nullptr, args...);
}

template <class R = void, class... A>
R VCall(void* self, DWORD offset, A... args) {
    DWORD fn = (*reinterpret_cast<DWORD**>(self))[offset / 4];
    return ThisCall<R>(fn, self, args...);
}

template <class T>
T& At(void* base, DWORD off) {
    return *reinterpret_cast<T*>(static_cast<BYTE*>(base) + off);
}

// True if `object`'s class is `dti` or derives from it (getDTI is vtable +0x10; DTI +0x10 = parent DTI).
inline bool IsA(BYTE* object, DWORD dti) {
    DWORD vt = At<DWORD>(object, 0);
    if (vt < 0x400000 || vt >= 0x1928000) return false;  // not a game class (or already a hook copy)
    auto d = VCall<BYTE*>(object, 0x10);
    for (int depth = 0; d && depth < 32; ++depth, d = At<BYTE*>(d, 0x10))  // chain ends in a null parent
        if (reinterpret_cast<DWORD>(d) == dti) return true;
    return false;
}

constexpr DWORD kPlayerList = 0x11B27DC;  // *ptr + 0x24: player slots [4]; 0x7E6370 validates against it

// The current stage's state: [0x11B2158] + 0x5940 * [+0x20]. Area at +0x273D8, scene number at +0x273E0.
inline BYTE* Stage() {
    auto root = *reinterpret_cast<BYTE**>(0x11B2158);
    return root ? root + 0x5940 * At<DWORD>(root, 0x20) : nullptr;
}

// Slots are per character: slot 0 is always Chris, slot 1 always Sheva. The character the local player
// chose is the stage state's +0x273C4; the game's own one-player mode (0x71855C) opens just that slot, and
// the mod keeps that one and closes the other (solo.cpp).
constexpr DWORD kChosenSlot = 0x273C4;

inline int SoloSlot() {
    BYTE* stage = Stage();
    return stage ? At<DWORD>(stage, kChosenSlot) & 1 : 0;
}

inline int PartnerSlot() { return SoloSlot() ^ 1; }

inline BYTE* PlayerAt(int slot) {
    auto list = *reinterpret_cast<BYTE**>(kPlayerList);
    return list ? At<BYTE*>(list, 0x24 + 4 * slot) : nullptr;
}

// The one player in solo (Chris or Sheva), and the partner, who is null in solo.
inline BYTE* SoloPlayer() { return PlayerAt(SoloSlot()); }
inline BYTE* Partner() { return PlayerAt(PartnerSlot()); }

// The unit lists ([0x12340B0] = sUnit): move line i at +0x20 + 0x20*i (its index at +0x18, first unit at
// +0xC), each unit's next at +8. Enemies run on line 9.
constexpr DWORD kUnitRoot = 0x12340B0;

inline BYTE* UnitLine(BYTE* units, int i) { return units + 0x20 + 0x20 * i; }

template <class F>
void ForEachUnit(F fn) {
    auto units = *reinterpret_cast<BYTE**>(kUnitRoot);
    if (!units) return;
    for (int i = 0; i < 64; ++i) {
        BYTE* line = UnitLine(units, i);
        if (At<int>(line, 0x18) != i) break;
        int n = 0;
        for (auto u = At<BYTE*>(line, 0xC); u && n < 4096; u = At<BYTE*>(u, 8), ++n) fn(u);
    }
}
