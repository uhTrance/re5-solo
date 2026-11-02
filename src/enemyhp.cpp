// Ndesu damage boost: one player deals about half a team's damage, so Ndesu's (uEm45, 2-3 boss) health is
// scaled to 1 / kDamageScale (Ndesu only). Data only: enemy health is two shorts at +0x1364 (current) and
// +0x1366 (max) on every uEnemy, read all over the enemy code; health checks are fractions of max, so scaling
// both keeps them in step. Scaled once; again if the max changes to one this did not write (a respawn or a
// phase that resets it).
//
// Stagger: Ndesu's damage handler (vtable +0x204, 0x8E19E0) drains enemy gauges ids 1-4 (0x9DA890(id,
// -damage * table factor)). A gauge is 12 bytes at +0x1C0C + 12*id: broken flag, value (+0x1C10), max
// (+0x1C14); value <= 0 breaks it (the stagger, +0x1BB4 |= 4). 0x9D9AA0(id, v) sets value and max,
// 0x9D9AD0(id, v) refills the value only. Two players drain them twice as fast, so each max is divided by
// kStaggerScale and any value above it (a refill) is clamped.
//
// Main bulb: Ndesu's bulbs are uEm4502 (DTI 0x11EEBE0), linked from Ndesu +0x1F64[5]; the main one (index 4,
// +0x1BB4 bit 0x10) is what kills him. Its real health is the int at +0x1F50, not +0x1364: its damage handler
// (vtable +0x204, 0x8E14F0) subtracts there while +0x1BB4 bit 0 is set. 0x8E15A9 / 0x8E21D7 arm it (26000 for the
// main bulb, 8000 for the others); it holds its damage across exposures and only re-arms on a retry (logged
// 2026-11-01). Any rise of that value (a re-arm) is divided by kBulbScale, every tick.
//
// EnemyHpTick is off (solo.cpp): the fight runs at its default values, with only NdesuEaseTick's changes.
#include <math.h>
#include <unordered_map>

#include "objhook.h"
#include "solo.h"

namespace {

constexpr float kDamageScale = 1.5f;
constexpr float kBulbScale = 2.0f;
constexpr short kBoulderHp = 2600;  // default 5200; a fixed value, so a reload of this DLL cannot halve it again
constexpr DWORD kAttackStop = 0x1F54;
constexpr float kEasedStops[] = {1500.0f, 2600.0f, 1200.0f, 6000.0f};  // kick, two-handed slam, overhead pole swing, tackle
constexpr float kStopScale = 0.9f;
constexpr DWORD kStopFlags = 0x448, kStopWindow = 0x2000;
constexpr DWORD kAttacking = 0x2;  // +0x1BB4, set at an attack's start
constexpr float kStopHeld = 1.0f;
constexpr DWORD kNdesuDti = 0x11EE880;  // uEm45 (not its parts, uEm4501/uEm4502)
constexpr DWORD kBulbDti = 0x11EEBE0;   // uEm4502
constexpr DWORD kFlags = 0x1BB4, kMainBulb = 0x10;
constexpr DWORD kHp = 0x1364, kMaxHp = 0x1366;

constexpr float kStaggerScale = 2.0f;
constexpr DWORD kGauges = 0x1C10;  // value; max at +4; 12 bytes per gauge id
constexpr int kFirstGauge = 1, kLastGauge = 4;

std::unordered_map<BYTE*, short> g_scaled;  // Ndesu -> max health this wrote
BYTE* g_ndesu;
int g_gaugeMax[kLastGauge + 1];  // max each gauge got from this

void ScaleGauges(BYTE* ndesu) {
    for (int id = kFirstGauge; id <= kLastGauge; ++id) {
        int& value = At<int>(ndesu, kGauges + 12 * id);
        int& max = At<int>(ndesu, kGauges + 12 * id + 4);
        if (max <= 0) continue;
        if (max != g_gaugeMax[id]) {
            int newMax = static_cast<int>(max / kStaggerScale + 0.5f);
            if (newMax < 1) newMax = 1;
            Log("Ndesu %p: stagger gauge %d %d/%d -> max %d", ndesu, id, value, max, newMax);
            max = newMax;
            g_gaugeMax[id] = newMax;
        }
        if (value > max) value = max;
    }
}

// By getDTI (+0x10), which Ndesu's hook copy of his vtable (ndesu.cpp) keeps.
bool IsNdesu(BYTE* object) { return VCall<DWORD>(object, 0x10) == kNdesuDti; }

bool IsMainBulb(BYTE* object) {
    return VCall<DWORD>(object, 0x10) == kBulbDti && (At<DWORD>(object, kFlags) & kMainBulb);
}

constexpr DWORD kBulbs = 0x1F64, kBulbHp = 0x1F50;
constexpr int kMainBulbIndex = 4;

void ScaleMainBulb(BYTE* ndesu) {
    static BYTE* bulb;
    static int last;
    BYTE* b = At<BYTE*>(ndesu, kBulbs + 4 * kMainBulbIndex);
    if (!b || !IsMainBulb(b)) return;
    int& hp = At<int>(b, kBulbHp);
    if (b != bulb) {
        bulb = b;
        last = 0;
    }
    if (hp > last && hp > 0) {
        int scaled = static_cast<int>(hp / kBulbScale + 0.5f);
        Log("Ndesu main bulb %p: health %d -> %d", b, hp, scaled);
        hp = scaled;
    }
    last = hp;
}

void Scale(BYTE* enemy, float scale, const char* name) {  // +0x1364 health
    short& hp = At<short>(enemy, kHp);
    short& maxHp = At<short>(enemy, kMaxHp);
    if (maxHp <= 0) return;
    auto it = g_scaled.find(enemy);
    if (it != g_scaled.end() && it->second == maxHp) {
        if (hp > maxHp) hp = maxHp;  // the game set full health after this scaled max
        return;
    }
    short newMax = static_cast<short>(maxHp / scale + 0.5f);
    if (newMax < 1) newMax = 1;
    short newHp = hp;
    if (hp > 0) {  // a dead enemy stays dead
        newHp = static_cast<short>(hp / scale + 0.5f);
        if (newHp < 1) newHp = 1;
    }
    Log("%s %p (vtable %08X): health %d/%d -> %d/%d", name, enemy, At<DWORD>(enemy, 0), hp, maxHp, newHp, newMax);
    hp = newHp;
    maxHp = newMax;
    g_scaled[enemy] = newMax;
}

}  // namespace

// Watcher thread: every 200 ms, find Ndesu and scale his health; every tick, keep his gauges and main bulb
// scaled (a refill or re-arm must not stand for long).
void EnemyHpTick() {
    static DWORD lastScan;
    if (GetTickCount() - lastScan >= 200) {
        lastScan = GetTickCount();
        BYTE* found = nullptr;
        ForEachUnit([&](BYTE* u) {
            if (IsNdesu(u)) {
                Scale(u, kDamageScale, "Ndesu");
                found = u;
            }
        });
        if (found != g_ndesu) memset(g_gaugeMax, 0, sizeof g_gaugeMax);
        g_ndesu = found;
    }
    if (g_ndesu && IsNdesu(g_ndesu)) {
        ScaleGauges(g_ndesu);
        ScaleMainBulb(g_ndesu);
    }
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

// One player's fire against Ndesu's attacks. His thrown boulder (uWp78) has half its health (+0x1364).
// An attack's stop value (+0x1F54) is loaded whole from a table (0xF82ABC) that is left alone, so the
// attacks in kEasedStops are caught at exactly their loaded values, every tick; damage (115 or 92 a hit, from any
// other value) never lands on them. A shot landing in the same tick misses the reduction for that one attack.
void NdesuEaseTick() {
    static DWORD lastScan;
    static BYTE* ndesu;
    if (GetTickCount() - lastScan >= 200) {
        lastScan = GetTickCount();
        BYTE* found = nullptr;
        ForEachUnit([&](BYTE* u) {
            if (IsNdesu(u)) found = u;
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
        ndesu = found;
    }
    if (!ndesu || !IsNdesu(ndesu)) return;
    float& stop = At<float>(ndesu, kAttackStop);
    for (float loaded : kEasedStops)
        if (stop == loaded) {
            stop = loaded * kStopScale;
            Log("Ndesu attack stop %.0f -> %.0f", loaded, stop);
        }
    // The damage handler (0x8E1AE6) stops an attack only on the hit that takes the stop value from above 0 to 0
    // or below, and only while his stop window (+0x448 bit 0x2000) is open: that hit costs him 5000 (+0x1F50) and
    // he flinches. A value already at 0 is never checked again, and the bulbs' handler (0x8E153D) keeps
    // subtracting past 0. So a stop used up before the window opened let the attack land (2-3, the slam crossed
    // 0 at 2.22 s and hit; every crossing from 2.25 s stopped it). Until the window opens it is held just above 0.
    if ((At<DWORD>(ndesu, kFlags) & kAttacking) && stop <= 0.0f && !(At<DWORD>(ndesu, kStopFlags) & kStopWindow)) {
        Log("Ndesu attack stop %.0f before the stop window opened, held at %.0f", stop, kStopHeld);
        stop = kStopHeld;
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
