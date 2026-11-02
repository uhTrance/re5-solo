// The absent character keeps their loadout.
//
// The stage record (objhook.h Stage()) holds both characters' 9-slot loadouts, 11 entries of 0x2C (item id +8,
// count +0xC) per character, Chris's block then Sheva's 0x420 later, in three copies: A (+0x23448, the one the level
// works on), B (+0x249D8) and C (+0x27618, committed by Item Management's Ready). Loading a chapter empties both
// blocks in A to hand the items to the players, writes back the items of each player that spawned, then copies A
// over B and C. The mod never spawns the partner, so the partner's weapons vanished from the save for good
// (playing Sheva after a Chris run left her with nothing).
//
// While no player exists (menus, Item Management), the partner's block in C is remembered; once the player exists,
// any copy whose partner block differs from it gets it back, so the loadout is still equipped next time.
//
// Item Management after Restart from Checkpoint runs with a player present and writes A and B, not C, so none of
// that was remembered and putting the old block back undid the player's changes: items given to the partner
// vanished, items taken from them came back on top of the stash. The load only ever empties the partner's block,
// so a copy that changed since the last look is the player's change and is remembered instead, with or without a
// player: Organize can change any one copy alone (logged 2026-11-02: B 0:203 with A and C still 0:202), and a copy
// merely differing can be the stale one. An empty copy with A not empty is the player's too (the load empties A
// first), but all of the partner's items taken with A emptied as well still reads as the load's wipe, and they
// come back.
#include <cstdio>
#include <cstring>

#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kCopies[3] = {0x23448, 0x249D8, 0x27618};
constexpr DWORD kCommitted = 0x27618;
constexpr DWORD kBlockStride = 0x420;     // Chris's block, then Sheva's
constexpr DWORD kEntries = 11, kEntry = 0x2C;
constexpr DWORD kBlockBytes = kEntries * kEntry;

BYTE g_saved[2][kBlockBytes];
bool g_have[2];
BYTE g_seen[2][3][kBlockBytes];  // each copy at the last look
bool g_seenAny[2];

BYTE* Block(BYTE* stage, DWORD copy, int slot) { return stage + copy + kBlockStride * slot; }

bool IsEmpty(const BYTE* block) {
    for (DWORD i = 0; i < kEntries; ++i)
        if (*reinterpret_cast<const DWORD*>(block + kEntry * i + 8)) return false;
    return true;
}

void Describe(const BYTE* block, char* out, size_t size) {
    size_t n = 0;
    out[0] = 0;
    for (DWORD i = 0; i < kEntries && n + 16 < size; ++i) {
        DWORD id = *reinterpret_cast<const DWORD*>(block + kEntry * i + 8);
        if (id) n += snprintf(out + n, size - n, " %lu:%lX", i, id);
    }
    if (!n) snprintf(out, size, " (empty)");
}

}  // namespace

// Watcher thread.
void InventoryTick() {
    static DWORD lastScan;
    if (GetTickCount() - lastScan < 100) return;
    lastScan = GetTickCount();
    BYTE* stage = Stage();
    if (!stage) return;
    int partner = PartnerSlot();
    char items[256];
    bool noPlayer = !PlayerAt(0) && !PlayerAt(1);
    if (noPlayer && !g_have[partner]) {  // the first look: what Item Management last committed
        BYTE* committed = Block(stage, kCommitted, partner);
        memcpy(g_saved[partner], committed, kBlockBytes);
        g_have[partner] = true;
        Describe(committed, items, sizeof items);
        Log("inventory: %s's loadout remembered:%s", partner ? "Sheva" : "Chris", items);
    }
    BYTE* level = Block(stage, kCopies[0], partner);
    bool remembered = false;
    for (int i = 0; i < 3; ++i) {
        BYTE* block = Block(stage, kCopies[i], partner);
        BYTE* seen = g_seen[partner][i];
        bool changed = g_seenAny[partner] && memcmp(block, seen, kBlockBytes);
        memcpy(seen, block, kBlockBytes);
        if (!changed || remembered || !g_have[partner] || !memcmp(block, g_saved[partner], kBlockBytes)) continue;
        if (IsEmpty(block) && IsEmpty(level)) continue;
        memcpy(g_saved[partner], block, kBlockBytes);
        remembered = true;
        Describe(block, items, sizeof items);
        Log("inventory: %s's loadout changed in copy +%lX (area %lu, %s), remembered:%s", partner ? "Sheva" : "Chris",
            kCopies[i], At<DWORD>(stage, 0x273D8), noPlayer ? "no player" : "player", items);
    }
    g_seenAny[partner] = true;
    if (noPlayer || !SoloPlayer() || !g_have[partner]) return;
    for (DWORD copy : kCopies) {
        BYTE* block = Block(stage, copy, partner);
        if (!memcmp(block, g_saved[partner], kBlockBytes)) continue;
        Describe(block, items, sizeof items);
        memcpy(block, g_saved[partner], kBlockBytes);
        char back[256];
        Describe(block, back, sizeof back);
        Log("inventory: %s's loadout in copy +%lX was%s, put back:%s", partner ? "Sheva" : "Chris", copy, items, back);
    }
}
