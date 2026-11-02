// Two-man levers (uOmSwitch pairs, e.g. uOm08f3 in 3-3, uOm08bc) for one player: pulling one counts for both.
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
// never moved, so it stayed 0 and the power never came on. Once a lever of a pair is on and its partner's byte
// is set, the partner's state is set to 1 too.
//
// 3-3's pair takes both pulls. In co-op a lever is held: letting go clears its byte and +0x19A4 (0xB3A9F6), so
// the pair goes only while both are held. Here the byte is put back when the player lets go, which latches that
// lever (the prompt check refuses it to him now, +0x19A4 being -1); pulling the other one finds both bytes set,
// the same state the walk-up mark gives every other pair.
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

constexpr DWORD kHolder = 0x19A4;  // the player holding it, -1 for nobody
bool g_bothPulls;                  // the pair found takes both pulls
int g_held = -1, g_latched = -1;   // its lever the player is holding; its lever he has let go of
bool g_pairWent;

bool TakesBothPulls(BYTE* lever) {
    auto dti = VCall<BYTE*>(lever, 0x10);
    return dti && !strcmp(At<const char*>(dti, 4), "uOm08f3");
}

void BothPullsTick(volatile BYTE* pulled) {
    if (!g_bothPulls) {  // a load: a lever let go of does not outlast its pair
        if (g_latched >= 0 && !g_pairWent) pulled[g_latched] = 0;
        g_held = g_latched = -1;
        g_pairWent = false;
        return;
    }
    if (g_pairWent) return;
    if (pulled[0] && pulled[1]) {
        Log("lever: both pulled, the pair goes");
        g_pairWent = true;
        return;
    }
    if (g_latched >= 0) pulled[g_latched] = 1;  // the game clears it, or a pull of the other lever must find it
    for (int i = 0; i < g_count; ++i) {
        BYTE* lever = g_levers[i];
        int own = At<int>(lever, kIndex);
        if (pulled[own] && At<int>(lever, kHolder) >= 0) g_held = own;
    }
    if (g_held >= 0 && !pulled[g_held]) {
        pulled[g_held] = 1;
        g_latched = g_held;
        g_held = -1;
        Log("lever: let go of lever %d, it stays pulled", g_latched);
        ShowHint("Now pull the other lever.", 4.0f);
    }
}

void FindLevers() {
    g_count = 0;
    g_bothPulls = false;
    ForEachUnit([](BYTE* u) {
        if (g_count == 32 || !IsA(u, kSwitchDti)) return;  // IsA first: small units end before +0x19A0
        int index = At<int>(u, kIndex);
        if ((index == 0 || index == 1) && At<BYTE*>(u, kPartner)) {
            g_levers[g_count++] = u;
            if (TakesBothPulls(u)) g_bothPulls = true;
        }
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
    BothPullsTick(pulled);
    BYTE* player = SoloPlayer();
    if (player && !g_bothPulls) {
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
        if (nearest && !realPull) {
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
    for (int i = 0; i < g_count; ++i) {
        BYTE* lever = g_levers[i];
        BYTE* partner = At<BYTE*>(lever, kPartner);
        if (At<BYTE>(lever, kState) != 1 || !partner || At<BYTE>(partner, kState) == 1) continue;
        // The list is a second old at most: write only to a live lever pair (the partner links back).
        if (!IsA(lever, kSwitchDti) || !IsA(partner, kSwitchDti) || At<BYTE*>(partner, kPartner) != lever) continue;
        int other = At<int>(partner, kIndex);
        if ((other != 0 && other != 1) || !pulled[other]) continue;
        At<BYTE>(partner, kState) = 1;
        Log("lever %p: on, partner lever %d set on too", lever, other);
    }
    // Fallback: a lever pulled away from the ones found. Not while a pair is found: mirroring a real pull would
    // set the other lever's own byte, and that lever could then no longer be pulled (3-3).
    if (g_count == 0 && pulled[0] != pulled[1]) {
        int other = pulled[0] ? 1 : 0;
        if (1 - other == g_marked) return;  // only the mod's own mark is set: nobody has pulled yet
        pulled[other] = 1;
        Log("lever: lever %d pulled, partner lever %d marked pulled too", 1 - other, other);
    }
}
