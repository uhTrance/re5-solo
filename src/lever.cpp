// Two-man levers (uOmSwitch pairs, e.g. uOm08f3 in 3-3, uOm08bc) for one player: each is pulled alone, in turn.
//
// Each lever of a pair has an index (+0x19A0: 0 or 1; -1 = a single lever) and a link to the other (+0x199C).
// A pulled lever sets its byte in a shared table (0x1202040[index], 0xB36D80); the pair goes when both
// bytes are set (0xB36D60). The check runs at the pull, so setting the partner's byte after the player pulls
// only counts on a second pull: it is set as soon as the player walks up to a lever of a pair, and mirrored
// after a pull as a fallback. The fallback ignores the byte the mod set; read as a pull, it would mark the
// player's own lever pulled early, and in 4-2 the pair then never powers the next lever.
//
// A lever whose own byte is set counts as pulled (by the player in +0x19A4), and the prompt check (0xC997B5)
// will not let anyone else pull it. 3-3's pair (uOm08f3) stands 209 apart, so "mark the partner of every lever
// in reach" marked both bytes before any pull: the player's lever was then taken and the power never came on.
// Only the nearest lever's partner is marked, the mark moves if the player walks to the other one, and nothing
// is touched once a real pull has set a byte.
//
// The pull bytes are not all a level reads: 3-3's room controller (aS307, step +0x639 1) powers the belt levers
// (flag 68) only once both levers' own state (+0x1970, read by vtable +0x194) is 1, and the lever the mod marked
// never moved, so it stayed 0 and the power never came on. So each lever is pulled in turn: once the first is on,
// both bytes are cleared and the walk-up mark then serves the other lever, whose own pull turns it on. The first
// lever gets no mark while it is the nearest. Holding one lever and letting go does not latch it for this: the
// second lever then never registers a pull (tried 2026-11-02).
#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kPulled = 0x1202040;  // BYTE[2]
constexpr DWORD kSwitchDti = 0x1202048;  // uOmSwitch
constexpr DWORD kIndex = 0x19A0, kPartner = 0x199C;
constexpr DWORD kState = 0x1970;  // 1 = on
constexpr float kNear = 400.0f;

BYTE* g_levers[32];
int g_count;
int g_marked = -1;  // the byte the mod set (the partner's), not a pull
bool g_firstOn;     // one lever of the pair is on: the other is still to pull

void FindLevers() {
    g_count = 0;
    ForEachUnit([](BYTE* u) {
        if (g_count == 32 || !IsA(u, kSwitchDti)) return;  // IsA first: small units end before +0x19A0
        int index = At<int>(u, kIndex);
        if ((index == 0 || index == 1) && At<BYTE*>(u, kPartner)) g_levers[g_count++] = u;
    });
}

}  // namespace

void LeverTick() {
    static DWORD lastScan;
    if (GetTickCount() - lastScan >= 1000) {
        lastScan = GetTickCount();
        FindLevers();
    }
    auto pulled = reinterpret_cast<volatile BYTE*>(kPulled);
    BYTE* player = SoloPlayer();
    if (player) {
        BYTE* nearest = nullptr;
        float best = kNear * kNear;
        for (int i = 0; i < g_count; ++i) {
            BYTE* lever = g_levers[i];
            float dx = At<float>(lever, 0x30) - At<float>(player, 0x30);
            float dz = At<float>(lever, 0x38) - At<float>(player, 0x38);
            float dy = At<float>(lever, 0x34) - At<float>(player, 0x34);
            if (dy > 300 || dy < -300 || dx * dx + dz * dz > best) continue;
            best = dx * dx + dz * dz;
            nearest = lever;
        }
        bool realPull = (pulled[0] && g_marked != 0) || (pulled[1] && g_marked != 1);
        if (nearest && !realPull && At<BYTE>(nearest, kState) != 1) {
            int own = At<int>(nearest, kIndex), other = 1 - own;
            if (g_marked == own) pulled[own] = 0;  // walked over to the lever the mod had marked
            if (!pulled[other]) {
                pulled[other] = 1;
                g_marked = other;
                Log("lever %p: player is near, partner lever %d marked pulled", nearest, other);
            }
        }
    }
    if (!pulled[0] && !pulled[1]) g_marked = -1;  // the game reset the pair
    bool anyOn = false;
    for (int i = 0; i < g_count; ++i) {
        BYTE* lever = g_levers[i];
        // The list is a second old at most: only a live lever pair (the partner links back).
        if (!IsA(lever, kSwitchDti)) continue;
        BYTE* partner = At<BYTE*>(lever, kPartner);
        if (!partner || !IsA(partner, kSwitchDti) || At<BYTE*>(partner, kPartner) != lever) continue;
        if (At<BYTE>(lever, kState) != 1) continue;
        anyOn = true;
        if (g_firstOn || At<BYTE>(partner, kState) == 1) continue;
        g_firstOn = true;
        pulled[0] = pulled[1] = 0;
        g_marked = -1;
        Log("lever %p: on, partner lever %d is still to pull", lever, At<int>(partner, kIndex));
        ShowHint("Now pull the other lever.", 4.0f);
    }
    if (!anyOn) g_firstOn = false;
    // Fallback: a lever pulled away from the ones found. Not while a pair is found: mirroring a real pull would
    // set the other lever's own byte, and that lever could then no longer be pulled (3-3).
    if (g_count == 0 && pulled[0] != pulled[1]) {
        int other = pulled[0] ? 1 : 0;
        if (1 - other == g_marked) return;  // only the mod's own mark is set: nobody has pulled yet
        pulled[other] = 1;
        Log("lever: lever %d pulled, partner lever %d marked pulled too", 1 - other, other);
    }
}
