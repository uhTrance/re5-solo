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
// (vtable +0x204, 0x8E14F0) subtracts there while +0x1BB4 bit 0 is set, and each exposure re-arms it
// (0x8E15A9 / 0x8E21D7: 26000 for the main bulb, 8000 for the others), so it must die within one window.
// Any rise of that value (a re-arm) is divided by kBulbScale, every tick.
#include <unordered_map>

#include "objhook.h"
#include "solo.h"

namespace {

constexpr float kDamageScale = 1.5f;
constexpr float kBulbScale = 2.0f;
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
