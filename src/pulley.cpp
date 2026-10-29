// Two-player pulleys (uOmCoopSwitch_S301, e.g. uOma006 in 4-2) for one player.
//
// Each handle has a state byte (+0x1A51 + handle, 0xAAA1B0; taking hold sets it to 1, 0xAAB072, 2 once
// pulled, and the player's action resets it to 0 on letting go, 0xAAC449). The switch's ready handler (vtable
// +0x184, 0xAAAF10) only goes on event 0xF1 when handles 0 AND 1 are both at 2, else it sends 0xF2.
//
// Solo one pull is enough: once the player's handle reaches 2, the other handle is marked pulled and 0xF1 is
// sent, once per pulley (repeating it would re-pull the handles every frame after the game resets them, and
// the next pulleys stop responding).
//
// Most pulleys (uOm0939..uOm093D in 4-2, not uOma006) also set +0x1A50 at init (0xAAA797), and then a
// handle is only usable by a player who has a partner (0xAAAB50: 0xB5BED0(1), player +0x2A74, checked so
// the two players take different handles), so solo there is no prompt. The flag is cleared when the pulley
// is hooked, which makes them behave like uOma006.
#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kCoopSwitchDti = 0x11F9404;  // uOmCoopSwitch_S301
constexpr DWORD kHandles = 0x1A51;
constexpr BYTE kPulled = 2;
constexpr DWORD kReadyHandler = 0x184;
constexpr DWORD kEventTryGo = 0xF1;
constexpr DWORD kNeedsPartner = 0x1A50;

struct Pulley {
    BYTE* object;
    bool done;  // go sent
};
Pulley g_pulleys[16];

Pulley* Find(BYTE* object) {
    for (auto& p : g_pulleys)
        if (p.object == object) return &p;
    return nullptr;
}

void AfterPulleyUpdate(BYTE* object) {
    Pulley* p = Find(object);
    if (!p || p->done) return;
    BYTE* handle = object + kHandles;
    for (int h = 0; h < 2; ++h) {
        if (handle[h] != kPulled) continue;
        handle[1 - h] = kPulled;
        p->done = true;
        VCall(object, kReadyHandler, SoloSlot(), kEventTryGo, 0);
        Log("pulley %p: handle %d pulled, handle %d marked pulled, go sent", object, h, 1 - h);
        return;
    }
}

}  // namespace

// Watcher thread: hook every co-op pulley in the unit lists (a hooked one no longer passes IsA).
void PulleyTick() {
    static DWORD lastScan;
    if (GetTickCount() - lastScan < 1000) return;
    lastScan = GetTickCount();
    ForEachUnit([&](BYTE* u) {
        if (!IsA(u, kCoopSwitchDti)) return;
        Pulley* p = Find(u);  // a new object (area load): forget whatever was at that address
        if (!p) p = Find(nullptr);
        if (!p) {  // full: reuse the oldest slot
            static int next;
            p = &g_pulleys[next++ % 16];
        }
        if (!HookObject(u, At<DWORD>(u, 0), AfterPulleyUpdate)) return;
        *p = {u, u[kHandles] == kPulled || u[kHandles + 1] == kPulled};  // already gone (e.g. hot reload)
        bool neededPartner = u[kNeedsPartner] != 0;
        u[kNeedsPartner] = 0;
        Log("pulley %p hooked%s", u, neededPartner ? " (partner check off)" : "");
    });
}
