// Ndesu (uEm45, 2-3 boss) main bulb at 13000 health instead of 26000, a launcher option (re5_solo_ndesuhalf.txt
// next to the exe); off, the fight runs at its default values apart from NdesuEaseTick's boulder. His body health
// (+0x1364), his other bulbs and his stagger gauges are left alone.
//
// Ndesu's bulbs are uEm4502 (DTI 0x11EEBE0), linked from Ndesu +0x1F64[5]; the main one (index 4, +0x1BB4 bit 0x10)
// is what kills him. Its real health is the int at +0x1F50, not +0x1364: the damage handler (vtable +0x204,
// 0x8E14F0) subtracts there while +0x1BB4 bit 0 is set. 0x8E15A9 / 0x8E21D7 arm it (26000 for the main bulb, 8000
// for the others); it holds its damage across exposures and only re-arms on a retry (logged 2026-11-01). Any rise
// of the main bulb's value above 13000 (an arm) is brought down to 13000, every tick.
#include <math.h>

#include "objhook.h"
#include "solo.h"

namespace {

constexpr int kMainBulbHp = 13000;
constexpr short kBoulderHp = 2600;  // default 5200; a fixed value, so a reload of this DLL cannot halve it again
constexpr DWORD kStopFlags = 0x448, kStopWindow = 0x2000;  // the watch log's stop window
constexpr DWORD kNdesuDti = 0x11EE880;  // uEm45 (not its parts, uEm4501/uEm4502)
constexpr DWORD kBulbDti = 0x11EEBE0;   // uEm4502
constexpr DWORD kFlags = 0x1BB4, kMainBulb = 0x10;
constexpr DWORD kHp = 0x1364, kMaxHp = 0x1366;

constexpr DWORD kGauges = 0x1C10;  // value; max at +4; 12 bytes per gauge id
constexpr int kFirstGauge = 1, kLastGauge = 4;

BYTE* g_ndesu;

// By getDTI (+0x10), which Ndesu's hook copy of his vtable (ndesu.cpp) keeps.
bool IsNdesu(BYTE* object) { return VCall<DWORD>(object, 0x10) == kNdesuDti; }

constexpr DWORD kBulbs = 0x1F64, kBulbHp = 0x1F50;
constexpr int kMainBulbIndex = 4;

void CapMainBulb(BYTE* ndesu) {
    BYTE* b = At<BYTE*>(ndesu, kBulbs + 4 * kMainBulbIndex);
    if (!b || VCall<DWORD>(b, 0x10) != kBulbDti || !(At<DWORD>(b, kFlags) & kMainBulb)) return;
    int& hp = At<int>(b, kBulbHp);
    if (hp <= kMainBulbHp) return;
    Log("Ndesu main bulb %p: health %d -> %d", b, hp, kMainBulbHp);
    hp = kMainBulbHp;
}

}  // namespace

// Watcher thread: every 200 ms, find Ndesu; every tick, keep his main bulb at 13000 at most (an arm must not stand
// for long).
void EnemyHpTick() {
    static const bool on = MarkerFile("re5_solo_ndesuhalf.txt");
    if (!on) return;
    static DWORD lastScan;
    if (GetTickCount() - lastScan >= 200) {
        lastScan = GetTickCount();
        BYTE* found = nullptr;
        ForEachUnit([&](BYTE* u) {
            if (IsNdesu(u)) found = u;
        });
        g_ndesu = found;
    }
    if (g_ndesu && IsNdesu(g_ndesu)) CapMainBulb(g_ndesu);
}

// A read-only log of the fight at its default values: every change in Ndesu's health, gauges and bulbs, what he is
// doing (his action's class, +0x10F8) with his distance from the player, and the player's health. Only with
// re5_solo_ndesuwatch.txt next to the exe.
namespace {

const char* ActionName(BYTE* action) {
    __try {
        auto dti = action ? VCall<BYTE*>(action, 0x10) : nullptr;
        return dti ? At<const char*>(dti, 4) : "none";
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "?";
    }
}

template <class T>
bool Changed(T& last, T now) {
    bool changed = last != now;
    last = now;
    return changed;
}

}  // namespace

// One player's fire against Ndesu: his thrown boulder (uWp78) has half its health (+0x1364). His attacks' stop
// values (+0x1F54) are left at their defaults.
void NdesuEaseTick() {
    static DWORD lastScan;
    if (GetTickCount() - lastScan >= 200) {
        lastScan = GetTickCount();
        ForEachUnit([&](BYTE* u) {
            if (strcmp(ActionName(u), "uWp78")) return;
            short& hp = At<short>(u, kHp);
            short& maxHp = At<short>(u, kMaxHp);
            if (maxHp <= 0 || maxHp == kBoulderHp) {
                if (hp > kBoulderHp) hp = kBoulderHp;
                return;
            }
            Log("Ndesu boulder %p: health %d/%d -> max %d", u, hp, maxHp, kBoulderHp);
            hp = static_cast<short>(hp * kBoulderHp / maxHp);
            maxHp = kBoulderHp;
        });
    }
}

void NdesuWatchTick() {
    static const bool on = MarkerFile("re5_solo_ndesuwatch.txt");
    if (!on) return;
    static DWORD lastScan;
    static BYTE* ndesu;
    if (GetTickCount() - lastScan >= 200) {
        lastScan = GetTickCount();
        BYTE* found = nullptr;
        // Every unit class on the lists, once, to find his thrown boulder.
        static DWORD seen[256];
        static int seenCount;
        ForEachUnit([&](BYTE* u) {
            if (IsNdesu(u)) found = u;
            const char* cls = ActionName(u);
            if (!strcmp(cls, "uWp78") || !strcmp(cls, "uWp79")) {
                static short held[2];
                short& last = held[cls[4] - '8'];
                short now = At<short>(u, kHp);
                if (last != now) Log("ndesu watch: %s %p health %d -> %d (max %d)", cls, u, last, now, At<short>(u, kMaxHp));
                last = now;
            }
            DWORD dti = VCall<DWORD>(u, 0x10);
            for (int i = 0; i < seenCount; ++i)
                if (seen[i] == dti) return;
            if (seenCount == 256) return;
            seen[seenCount++] = dti;
            Log("ndesu watch: class %s at %p, +0x1364 %d/%d", ActionName(u), u, At<short>(u, kHp), At<short>(u, kMaxHp));
        });
        ndesu = found;
    }
    if (!ndesu || !IsNdesu(ndesu)) return;
    static short hp, playerHp;
    short nowHp = At<short>(ndesu, kHp);
    int before = hp;
    if (Changed(hp, nowHp)) Log("ndesu watch: health %d -> %d (max %d)", before, nowHp, At<short>(ndesu, kMaxHp));
    static DWORD lastRate;
    static int rate;
    if (GetTickCount() - lastRate >= 1000) {  // the frame time (snag.cpp's timer), for 30 vs 120 fps comparisons
        lastRate = GetTickCount();
        auto timer = *reinterpret_cast<BYTE**>(0x11B209C);
        float seconds = timer ? At<float>(timer, 0x28) : 0.0f;
        int now = seconds > 0.0f ? static_cast<int>(1.0f / seconds + 0.5f) : 0;
        if (abs(now - rate) > rate / 10 + 1) {
            Log("ndesu watch: frame rate %d (was %d)", now, rate);
            rate = now;
        }
    }
    static int gauges[kLastGauge + 1];
    for (int id = kFirstGauge; id <= kLastGauge; ++id) {
        int value = At<int>(ndesu, kGauges + 12 * id);
        before = gauges[id];
        if (Changed(gauges[id], value))
            Log("ndesu watch: gauge %d %d -> %d (max %d)", id, before, value, At<int>(ndesu, kGauges + 12 * id + 4));
    }
    static int bulbs[5];
    static bool exposed[5];
    static DWORD exposedAt[5];
    static int exposedHp[5];
    for (int i = 0; i < 5; ++i) {
        BYTE* b = At<BYTE*>(ndesu, kBulbs + 4 * i);
        int value = b ? At<int>(b, kBulbHp) : 0;
        before = bulbs[i];
        if (Changed(bulbs[i], value)) Log("ndesu watch: bulb %d %d -> %d", i, before, value);
        // Exposed = +0x1BB4 bit 0, the bit the bulb's damage handler (0x8E14F0) needs to take damage.
        bool open = b && (At<DWORD>(b, kFlags) & 1);
        if (open == exposed[i]) continue;
        exposed[i] = open;
        if (open) {
            exposedAt[i] = GetTickCount();
            exposedHp[i] = value;
            Log("ndesu watch: bulb %d exposed (health %d, %d fps)", i, value, rate);
        } else {
            Log("ndesu watch: bulb %d closed after %.2f s, damage %d (health %d, %d fps)", i,
                (GetTickCount() - exposedAt[i]) / 1000.0f, exposedHp[i] - value, value, rate);
        }
    }
    // +0x1F54: what an attack has left before it is stopped (the damage handler, 0x8E1AE6); +0x1F50: what that costs him.
    static float stopValue, bodyValue;
    float nowStop = At<float>(ndesu, 0x1F54), nowBody = At<float>(ndesu, 0x1F50);
    float wasStop = stopValue, wasBody = bodyValue;
    if (Changed(stopValue, nowStop)) Log("ndesu watch: stop %.0f -> %.0f", wasStop, nowStop);
    if (Changed(bodyValue, nowBody)) Log("ndesu watch: body %.0f -> %.0f", wasBody, nowBody);
    static bool window;
    if (Changed(window, (At<DWORD>(ndesu, kStopFlags) & kStopWindow) != 0))
        Log("ndesu watch: stop window %s", window ? "open" : "closed");
    BYTE* player = SoloPlayer();
    float distance = 0.0f;
    if (player) {
        float dx = At<float>(ndesu, 0x30) - At<float>(player, 0x30), dz = At<float>(ndesu, 0x38) - At<float>(player, 0x38);
        distance = sqrtf(dx * dx + dz * dz);
        short now = At<short>(player, kHp);
        before = playerHp;
        if (Changed(playerHp, now)) Log("ndesu watch: player health %d -> %d, distance %.0f", before, now, distance);
    }
    static const char* action;
    const char* name = ActionName(At<BYTE*>(ndesu, 0x10F8));
    if (action != name) {
        action = name;
        Log("ndesu watch: action %s, distance %.0f, flags %08lX", name, distance, At<DWORD>(ndesu, kFlags));
    }
}
