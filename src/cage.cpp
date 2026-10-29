// The cage moment (area 500) for one player: in co-op, once the wave is cleared, Sheva's AI is sent into
// zone 16 of zone manager 0 (where the cage comes down) and a level script waits for "anyone in zone 16"
// (AreaHit mode 4: zone type 0 "any of", required 0x13) to start the cutscene that cages her. Chris stays
// outside. Solo nobody walks in there (and the player shouldn't: they'd be the one caged), so the cutscene
// never comes and the cage never spawns.
//
// The cage script (room1_Event00) gets there in steps: s001e waits for RSF 25 (set by the ship cutscene's
// script, which a checkpoint after it doesn't run again), s0010 waits for SceNo == 10240, and s0015 waits for
// RSF 200 or 201: which player (slot 0 / 1) walked into the cage. 201 plays the cutscene with Sheva caged
// (camera 2, then hold_sheva: Chris fights the wave outside). Nothing solo sets 10240 or 200/201, so the mod
// answers each step as the script reaches it, setting the scene only once the player is within 5 m of the
// cage in normal play (scene 10230). The partner's slot picks 200/201, so in Sheva mode Chris is caged.
//
// RSF bits start at stage +0x23218, MSB first per dword (the index helper 0xCDC2F0 adds 4); +0x23214 is the
// flag object's vtable (writing a "flag" there crashes the cutscene's first camera call).
//
// The scripts' zone references to the cage zone (small level-data objects: vtable 0x103D790, +4 manager,
// +8 zone; three point at manager 0 zone 16 here) are also pointed at zone 2, the room around it. Nothing
// simple lists them or the scripts, so the thread scans private memory.
#include <math.h>
#include <string.h>
#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kArea = 500;
constexpr DWORD kZoneRefVtable = 0x103D790;
constexpr DWORD kManager = 0, kCageZone = 16, kRoomZone = 2;
constexpr DWORD kAreaRoot = 0x11E8660;
constexpr DWORD kStageArea = 0x273D8;
constexpr DWORD kStageScene = 0x273E0, kStageFlags = 0x23218;
constexpr DWORD kFsmTaskVtable = 0xF9DA80;  // uFSMTask: its level script (cFSMOrder) at +0x18
constexpr DWORD kRsfShipDone = 25, kRsfCageSlot0 = 200;
constexpr DWORD kScenePlay = 10230, kSceneCage = 10240;
constexpr float kCageX = 250, kCageZ = 4321, kTriggerRadius = 500;

DWORD CurrentArea() { return Stage() ? At<DWORD>(Stage(), kStageArea) : 0; }

// Sets RSF `index`; false if it was already set.
bool SetRsf(DWORD index) {
    DWORD& word = At<DWORD>(Stage(), kStageFlags + (index >> 5) * 4);
    DWORD bit = 0x80000000u >> (index & 31);
    bool was = (word & bit) != 0;
    word |= bit;
    return !was;
}

// MT strings: +8 = the characters.
const char* Text(BYTE* str) { return str ? reinterpret_cast<const char*>(str + 8) : ""; }

// A level script's FSM name and current state (cFSMOrder +0x10 core: +4 cAIFSM (+4 name), +8 state (+4 name)).
const char* FsmName(BYTE* order) { return Text(At<BYTE*>(At<BYTE*>(At<BYTE*>(order, 0x10), 4), 4)); }
const char* StateName(BYTE* order) {
    BYTE* state = At<BYTE*>(At<BYTE*>(order, 0x10), 8);
    return state ? Text(At<BYTE*>(state, 4)) : "";
}

// The zone list of manager 0 when the cage zone looks right (type 0, required 0x13), else null.
void* CageZones() {
    auto root = *reinterpret_cast<BYTE**>(kAreaRoot);
    auto mgr = root ? At<BYTE*>(root, 0xD4 + 4 * kManager) : nullptr;
    if (!mgr || At<DWORD>(mgr, 8) <= kCageZone) return nullptr;
    auto zones = At<BYTE**>(mgr, 0x14);
    auto def = zones && zones[kCageZone] ? At<BYTE*>(zones[kCageZone], 4) : nullptr;
    if (!def || At<DWORD>(def, 0xC) != 0 || At<DWORD>(def, 0x3C) != 0x13) return nullptr;
    return zones;
}

template <class Visit>
void ScanPrivate(Visit visit) {
    MEMORY_BASIC_INFORMATION mbi;
    for (BYTE* a = reinterpret_cast<BYTE*>(0x10000); a < reinterpret_cast<BYTE*>(0x7FFE0000); a += mbi.RegionSize) {
        if (!VirtualQuery(a, &mbi, sizeof mbi)) break;
        if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE || (mbi.Protect & PAGE_GUARD) ||
            !(mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)))
            continue;
        __try {
            auto p = static_cast<DWORD*>(mbi.BaseAddress);
            for (SIZE_T i = 0, n = mbi.RegionSize / 4 - 7; i < n; ++i) visit(p + i);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}

int RetargetRefs() {
    int count = 0;
    ScanPrivate([&](DWORD* p) {
        if (p[0] == kZoneRefVtable && p[1] == kManager && p[2] == kCageZone) {
            p[2] = kRoomZone;
            ++count;
        }
    });
    return count;
}

BYTE* FindCageScript() {
    BYTE* found = nullptr;
    ScanPrivate([&](DWORD* p) {
        if (found || p[0] != kFsmTaskVtable) return;
        __try {
            auto order = reinterpret_cast<BYTE*>(p[6]);
            if (order && At<BYTE*>(order, 0x10) && !strcmp(FsmName(order), "room1_Event00")) found = order;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    });
    return found;
}

bool PlayerAtCage() {
    BYTE* player = SoloPlayer();
    if (!player || !At<BYTE*>(player, 0x10F8) || At<BYTE*>(player, 0x18)) return false;  // parented: on something
    float dx = At<float>(player, 0x30) - kCageX, dz = At<float>(player, 0x38) - kCageZ;
    return dx * dx + dz * dz < kTriggerRadius * kTriggerRadius && fabsf(At<float>(player, 0x34)) < 100;
}

// Answer whichever step the cage script is waiting on.
void Drive(BYTE* script) {
    static char last[32];
    const char* state = StateName(script);
    if (strcmp(state, last)) {
        strncpy(last, state, sizeof last - 1);
        Log("cage: script at %s (scene %u)", state, At<DWORD>(Stage(), kStageScene));
    }
    if (!strcmp(state, "s001e")) {
        SetRsf(kRsfShipDone);
    } else if (!strcmp(state, "s0010")) {
        if (At<DWORD>(Stage(), kStageScene) == kScenePlay && PlayerAtCage()) {
            At<DWORD>(Stage(), kStageScene) = kSceneCage;
            Log("cage: player at the cage, scene %u", kSceneCage);
        }
    } else if (!strcmp(state, "s0015")) {
        if (SetRsf(kRsfCageSlot0 + PartnerSlot()))
            Log("cage: partner (slot %d) marked as in the cage: the cutscene starts", PartnerSlot());
    }
}

HANDLE g_thread;

DWORD WINAPI CageWatch(LPVOID) {
    void* done = nullptr;    // the zone list already handled (a new list = the area was loaded again)
    BYTE* script = nullptr;  // that load's room1_Event00
    DWORD lastLook = 0;
    while (!g_stopping) {
        Sleep(100);
        if (g_stopping || RecordMode()) continue;
        __try {
            void* zones = CurrentArea() == kArea ? CageZones() : nullptr;
            if (zones != done) {
                done = zones;
                script = nullptr;
                if (zones)
                    Log("cage: %d zone references to the cage zone now point at the room (player stays outside)",
                        RetargetRefs());
            }
            if (!zones) continue;
            if (!script && GetTickCount() - lastLook > 2000) {  // the scripts come a moment after the zones
                lastLook = GetTickCount();
                if ((script = FindCageScript()) != nullptr) Log("cage: found the cage script %p", script);
            }
            if (script && !strcmp(FsmName(script), "room1_Event00")) Drive(script);
            else script = nullptr;
        } __except (EXCEPTION_EXECUTE_HANDLER) { script = nullptr; }
    }
    return 0;
}

}  // namespace

void CageInit() { g_thread = CreateThread(nullptr, 0, CageWatch, nullptr, 0, nullptr); }

void CageShutdown() {  // after SoloShutdown set g_stopping
    if (g_thread && WaitForSingleObject(g_thread, 5000) != WAIT_OBJECT_0) Log("cage: thread did not stop");
    if (g_thread) CloseHandle(g_thread);
    g_thread = nullptr;
}
