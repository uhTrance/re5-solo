#include "objhook.h"

#include "solo.h"

namespace {

constexpr int kVtableCopy = 0x200;  // dwords copied (well past any hooked class's last slot)
// Classes hooked so far this run (they are never released). 16 ran out on 3-2 once the 120fps fix took a slot
// per player class: the next class hooked, a two-player lift, silently stayed vanilla.
constexpr int kMaxHooks = 64;

using Update = void(__fastcall*)(void* self, void* edx);

struct Hook {
    DWORD classVtable;
    DWORD slot;  // dword index of the per-frame update (thiscall, no args)
    Update original;
    AfterUpdate after;
    DWORD copy[kVtableCopy];
};

Hook g_hooks[kMaxHooks];
volatile LONG g_count;
SRWLOCK g_lock = SRWLOCK_INIT;  // HookObject/PatchObject run on the watcher and the script scanner

// Every object given a copy, so CoreShutdown can put the class vtables back (the copies live in this DLL).
constexpr int kMaxObjects = 4096;
BYTE* g_objects[kMaxObjects];
LONG g_objectCount;

void Track(BYTE* object) {
    for (LONG i = 0; i < g_objectCount; ++i)
        if (g_objects[i] == object) return;
    if (g_objectCount < kMaxObjects) g_objects[g_objectCount++] = object;
}

Hook* ByCopy(const DWORD* vt) {
    for (LONG i = 0; i < g_count; ++i)
        if (g_hooks[i].copy == vt) return &g_hooks[i];
    return nullptr;
}

void __fastcall HookedUpdate(void* self, void* edx) {
    Hook* h = ByCopy(At<DWORD*>(self, 0));
    if (!h) return;  // unreachable: only hook copies point here
    h->original(self, edx);
    __try {
        h->after(static_cast<BYTE*>(self));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("hook %08X: exception in solo logic", h->classVtable);
    }
}

}  // namespace

bool IsHooked(BYTE* object, DWORD classVtable) {
    Hook* h = ByCopy(At<DWORD*>(object, 0));
    return h && h->classVtable == classVtable;
}

// The vtable pointer swap is one aligned dword write.
bool HookObject(BYTE* object, DWORD classVtable, AfterUpdate after, DWORD updateSlot) {
    // Only a game class: hooking one of our copies again would chain copies whose update calls itself.
    if (!object || At<DWORD>(object, 0) != classVtable || classVtable < 0x400000 || classVtable >= 0x1928000)
        return false;
    AcquireSRWLockExclusive(&g_lock);
    Hook* h = nullptr;
    for (LONG i = 0; i < g_count && !h; ++i)
        if (g_hooks[i].classVtable == classVtable) h = &g_hooks[i];
    if (!h) {
        if (g_count == kMaxHooks) {
            ReleaseSRWLockExclusive(&g_lock);
            static LONG warned;
            if (!InterlockedExchange(&warned, 1)) Log("hook table full (%d classes): class %08X not hooked", kMaxHooks, classVtable);
            return false;
        }
        h = &g_hooks[g_count];
        h->classVtable = classVtable;
        h->slot = updateSlot / 4;
        memcpy(h->copy, reinterpret_cast<void*>(classVtable), sizeof h->copy);
        h->original = reinterpret_cast<Update>(h->copy[h->slot]);
        h->after = after;
        h->copy[h->slot] = reinterpret_cast<DWORD>(&HookedUpdate);
        InterlockedIncrement(&g_count);
    }
    At<DWORD*>(object, 0) = h->copy;
    Track(object);
    ReleaseSRWLockExclusive(&g_lock);
    Log("hooked object %p (class vtable %08X)", object, classVtable);
    return true;
}

// Replaces one more slot in the HookObject copy `object` already uses (no-op if it is not on one).
bool PatchHookedSlot(BYTE* object, DWORD slot, DWORD fn) {
    if (!object) return false;
    AcquireSRWLockExclusive(&g_lock);
    Hook* h = ByCopy(At<DWORD*>(object, 0));
    bool changed = h && h->copy[slot / 4] != fn;
    if (changed) h->copy[slot / 4] = fn;
    ReleaseSRWLockExclusive(&g_lock);
    if (changed) Log("hook %08X: slot +%lX replaced", h->classVtable, slot);
    return h != nullptr;
}

DWORD HookedClass(BYTE* object) {
    if (!object) return 0;
    AcquireSRWLockShared(&g_lock);
    Hook* h = ByCopy(At<DWORD*>(object, 0));
    DWORD cls = h ? h->classVtable : 0;
    ReleaseSRWLockShared(&g_lock);
    return cls;
}

void RestoreHookedSlot(DWORD classVtable, DWORD slot) {
    AcquireSRWLockExclusive(&g_lock);
    for (LONG i = 0; i < g_count; ++i)
        if (g_hooks[i].classVtable == classVtable) {
            DWORD original = reinterpret_cast<DWORD*>(classVtable)[slot / 4];
            if (slot / 4 != g_hooks[i].slot) g_hooks[i].copy[slot / 4] = original;
        }
    ReleaseSRWLockExclusive(&g_lock);
}

namespace {

// One copy per class and patch: the 6-2 hiding patches whatever model classes are handed to Chris, which must
// not pick up (or hand out) another module's copy of the same class.
struct Patched {
    DWORD classVtable;
    void (*patch)(DWORD* copy);
    DWORD copy[kVtableCopy];
};
constexpr LONG kMaxPatched = 24;  // 8 ran short once the 6-2 chase hid Chris's weapons and items
Patched g_patched[kMaxPatched];
LONG g_patchedCount;

}  // namespace

bool PatchObject(BYTE* object, DWORD classVtable, void (*patch)(DWORD* copy)) {
    if (!object || At<DWORD>(object, 0) != classVtable) return false;
    AcquireSRWLockExclusive(&g_lock);
    Patched* p = nullptr;
    for (LONG i = 0; i < g_patchedCount && !p; ++i)
        if (g_patched[i].classVtable == classVtable && g_patched[i].patch == patch) p = &g_patched[i];
    if (!p) {
        if (g_patchedCount == kMaxPatched) {
            ReleaseSRWLockExclusive(&g_lock);
            static LONG warned;
            if (!InterlockedExchange(&warned, 1)) Log("patch table full (%ld copies): class %08X not patched", kMaxPatched, classVtable);
            return false;
        }
        p = &g_patched[g_patchedCount++];
        p->classVtable = classVtable;
        p->patch = patch;
        memcpy(p->copy, reinterpret_cast<void*>(classVtable), sizeof p->copy);
        patch(p->copy);
    }
    At<DWORD*>(object, 0) = p->copy;
    Track(object);
    ReleaseSRWLockExclusive(&g_lock);
    Log("patched object %p (class vtable %08X)", object, classVtable);
    return true;
}

// CoreShutdown, after the watcher has stopped: every tracked object still on one of our copies gets its class
// vtable back (one aligned dword write; an object since freed no longer points at a copy and is skipped).
void RestoreHookedObjects() {
    int restored = 0;
    for (LONG i = 0; i < g_objectCount; ++i) {
        __try {
            DWORD* vt = At<DWORD*>(g_objects[i], 0);
            DWORD original = 0;
            if (Hook* h = ByCopy(vt)) original = h->classVtable;
            for (LONG k = 0; k < g_patchedCount && !original; ++k)
                if (g_patched[k].copy == vt) original = g_patched[k].classVtable;
            if (original) {
                At<DWORD>(g_objects[i], 0) = original;
                ++restored;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    Log("core: %d hooked object(s) restored", restored);
}
