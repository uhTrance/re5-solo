// Two-man levers (uOmSwitch pairs, e.g. uOm08f3 in 3-3, uOm08bc) for one player: pulling one counts for both.
//
// Each lever of a pair has an index (+0x19A0: 0 or 1; -1 = a single lever) and a link to the other (+0x199C).
// A pulled lever sets its byte in a shared table (0x1202040[index], 0xB36D80); the pair goes when both
// bytes are set (0xB36D60). The check runs at the pull, so setting the partner's byte after the player pulls
// only counts on a second pull: it is set as soon as the player walks up to a lever of a pair, and mirrored
// after a pull as a fallback. The fallback ignores the byte the mod set; read as a pull, it would mark the
// player's own lever pulled early, and in 4-2 the pair then never powers the next lever.
#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kPulled = 0x1202040;  // BYTE[2]
constexpr DWORD kSwitchDti = 0x1202048;  // uOmSwitch
constexpr DWORD kIndex = 0x19A0, kPartner = 0x199C;
constexpr float kNear = 400.0f;

BYTE* g_levers[32];
int g_count;
int g_marked = -1;  // the byte the mod set (the partner's), not a pull

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
        for (int i = 0; i < g_count; ++i) {
            BYTE* lever = g_levers[i];
            float dx = At<float>(lever, 0x30) - At<float>(player, 0x30);
            float dz = At<float>(lever, 0x38) - At<float>(player, 0x38);
            float dy = At<float>(lever, 0x34) - At<float>(player, 0x34);
            if (dx * dx + dz * dz > kNear * kNear || dy > 300 || dy < -300) continue;
            int other = 1 - At<int>(lever, kIndex);
            if (!pulled[other]) {
                pulled[other] = 1;
                g_marked = other;
                Log("lever %p: player is near, partner lever %d marked pulled", lever, other);
            }
        }
    }
    if (!pulled[0] && !pulled[1]) g_marked = -1;  // the game reset the pair
    if (pulled[0] != pulled[1]) {  // fallback: a lever pulled away from the ones found
        int other = pulled[0] ? 1 : 0;
        if (1 - other == g_marked) return;  // only the mod's own mark is set: nobody has pulled yet
        pulled[other] = 1;
        Log("lever: lever %d pulled, partner lever %d marked pulled too", 1 - other, other);
    }
}
